// DeviarePlugin.cpp : Defines the exported functions for the DLL.
//
// This part of the code is the Deviare style hook that handles the 
// very first DX11 or DX9 object creation.  Even though this is C++, it is 
// most useful for Deviare style hooks that are supported by their DB,
// and are direct exports from a DLL.  It's not well suited for vtable
// based calls like Present used by DX11 or DX9.

#include "DeviarePlugin.h"


#include <atlbase.h>
#include <thread>
#include <shlobj_core.h>


// We need the Deviare interface though, to be able to provide the OnLoad,
// OnFunctionCalled interfaces, and to be able to LoadCustomDLL this DLL from
// the C# app.

#if defined _M_IX86
#import "DeviareCOM.dll" raw_interfaces_only, named_guids, raw_dispinterfaces, auto_rename
#define my_ssize_t long
#define my_size_t unsigned long
#elif defined _M_X64
#import "DeviareCOM64.dll" raw_interfaces_only, named_guids, raw_dispinterfaces, auto_rename
#define my_ssize_t __int64
#define my_size_t unsigned __int64
#endif


// --------------------------------------------------------------------------------------------------
// Globals definition here, with extern reference in the .h file ensures that we 
// only have a single instance of each.  Even though they aren't used in this
// compilation unit, it is the named target of the project, so they belong here.

// Always logging to Unity LocalLow file. 
FILE* LogFile;

// This is automatically instantiated by C++, so the hooking library
// is immediately available.
CNktHookLib nktInProc;

// Required for reverse stereo blit to give us stereo backbuffer.
StereoHandle gNVAPI = nullptr;

// The actual shared Handle to the DX11 or DX9 VR side.  Filled in by an active InProc_*.
HANDLE gGameSharedHandle = nullptr;

// The Named Mutex to prevent the VR side from interfering with game side, during
// the creation or reset of the graphic device.
HANDLE gSetupMutex = nullptr;


// --------------------------------------------------------------------------------------------------
// Return the current value of the gGameSurfaceShare.  This is the HANDLE
// that is necessary to share from either DX9Ex or DX11 here, to DX11 in the VR app.
//
// This routine is defined here, so that we have only a single implementation of the
// routine with a declaration in the DeviarePlugin.h file.  This routine is specific
// to the DeviarePlugin itself, not the InProc sides.

HANDLE WINAPI GetSharedHandle(int* in)
{
#ifdef _DEBUG
	LogInfo("GetSharedHandle::%p\n", gGameSharedHandle);
#endif

	return gGameSharedHandle;
}

//-----------------------------------------------------------

// Used for both CreateDevice and Reset/Resize functions, where we are setting up the
// graphic environment, and thus cannot allow the VR side to draw using anything
// from the game side here.

void CaptureSetupMutex()
{
	DWORD waitResult;

	std::thread::id tid = std::this_thread::get_id();
	LogInfo("-> CaptureSetupMutex@%d\n", tid);

	if (gSetupMutex == NULL)
		FatalExit(L"CaptureSetupMutex: mutex does not exist.");

	waitResult = WaitForSingleObject(gSetupMutex, 1000);
	if (waitResult != WAIT_OBJECT_0)
	{
		wchar_t info[512];
		DWORD hr = GetLastError();
		std::thread::id tid = std::this_thread::get_id();
		swprintf_s(info, _countof(info), L"CaptureSetupMutex@%d: WaitForSingleObject failed.\nwaitResult: 0x%x, err: 0x%x\n", tid, waitResult, hr);
		FatalExit(info);
	}
}

// Release use of shared mutex, so the VR side can grab the mutex, and thus know that
// it can fetch the shared surface and use it to draw.  Normal operation is that the
// VR side grabs and releases the mutex for every frame, and is only blocked when
// we are setting up the graphics environment here, either as first run where this
// side creates the mutex as active and locked, or when Reset/Resize is called and we grab
// the mutex to lock out the VR side.

void ReleaseSetupMutex()
{
	std::thread::id tid = std::this_thread::get_id();
	LogInfo("<- ReleaseSetupMutex@%d\n", tid);

	if (gSetupMutex == NULL)
		FatalExit(L"ReleaseSetupMutex: mutex does not exist.");

	bool ok = ReleaseMutex(gSetupMutex);
	if (!ok)
	{
		DWORD hr = GetLastError();
		std::thread::id tid = std::this_thread::get_id();
		LogInfo("ReleaseSetupMutex@%d: ReleaseMutex failed, err: 0x%x\n", tid,  hr);
	}
}

// --------------------------------------------------------------------------------------------------

// A bit too involved for inline, let's put this log file creation/appending here.
// Trying to append to whatever Unity is using to log, so that game side info will
// be properly interspersed with VR side info.
// If LogFile is null, we'll get errors on logging, but does not seem worth failing for.

void OpenLogFile()
{
	// Fetch App Data LocalLow folder path where Unity log is stored.
	wchar_t* localLowAppData = 0;
	SHGetKnownFolderPath(FOLDERID_LocalAppDataLow, 0, NULL, &localLowAppData);

	std::wstring localLowPath(localLowAppData);
	std::wstring w_logFilePath = localLowPath + L"\\Katanga\\Katanga\\katanga.log";
	LPCWSTR logFilePath = w_logFilePath.c_str();

	LogFile = _wfsopen(logFilePath, L"a", _SH_DENYNO);
	setvbuf(LogFile, NULL, _IONBF, 0);

	LogInfo("\nGamePlugin C++ logging enabled.\n\n");
}

// --------------------------------------------------------------------------------------------------
//
//1) Regardless of the functionality of the plugin, the dll must export: OnLoad, OnUnload, OnHookAdded,
//   OnHookRemoved and OnFunctionCall (Tip: add a .def file to avoid name mangling)
//
//2) Code inside methods should try/catch exceptions to avoid possible crashes in hooked application.
//
//3) Methods that returns an HRESULT value should return S_OK if success or an error value.
//
//   3.1) If a method returns a value less than zero, all hooks will be removed and agent will unload
//        from the process.
//
//   3.2) The recommended way to handle errors is to let the SpyMgr to decide what to do. For e.g. if
//        you hit an error in OnFunctionCall, probably, some custom parameter will not be added to the
//        CustomParams() collection. So, when in your app received the DNktSpyMgrEvents::OnFunctionCall
//        event, you will find the parameters is missing and at this point you can choose what to do.

using namespace Deviare2;

HRESULT WINAPI OnLoad()
{
	OpenLogFile();
	
	LogInfo("GamePlugin::OnLoad called\n");

	// This is running inside the game itself, so make sure we can use
	// COM here.
	::CoInitialize(NULL);

	// At this earliest moment, setup a hook for the direct d3d9.dll
	// call of Direct3DCreate9, using the In-Proc mechanism.
	HookDirect3DCreate9();

	// Hook the NVAPI.DLL!nvapi_QueryInterface, so that we can watch
	// for Direct Mode by games.  ToDo: DX9 variant?
	HookNvapiSetDriverMode();

	// Find the Present call for current game
	FindAndHookPresent();


	// Setup shared mutex, with the VR side owning it.  We should never arrive
	// here without the Katanga side already having created it.
	// We will grab mutex to lock their drawing, whenever we are setting up
	// the shared surface.
	gSetupMutex = OpenMutex(SYNCHRONIZE, false, L"KatangaSetupMutex");
	if (gSetupMutex == NULL)
		FatalExit(L"OnLoad: could not find KatangaSetupMutex");

	return S_OK;
}


VOID WINAPI OnUnload()
{
	LogInfo("GamePlugin::OnUnLoad called\n");

	if (gSetupMutex != NULL)
		ReleaseMutex(gSetupMutex);

	::CoUninitialize();

	LogInfo("\nGamePlugin C++ log closed.\n\n");
	fclose(LogFile);

	return;
}


HRESULT WINAPI OnHookAdded(__in INktHookInfo *lpHookInfo, __in DWORD dwChainIndex,
	__in LPCWSTR szParametersW)
{
	BSTR name;
	my_ssize_t address;
	CHAR szBufA[1024];
	INktProcess* pProc;
	long gGamePID;

	HRESULT hr = lpHookInfo->get_FunctionName(&name);
	if (FAILED(hr))
		FatalExit(L"Failed GetFunctionName");
	lpHookInfo->get_Address(&address);
	sprintf_s(szBufA, 1024, "GamePlugin::OnHookAdded called [Hook: %S @ 0x%IX / Chain:%lu]\n",
		name, address, dwChainIndex);
	LogInfo(szBufA);

	hr = lpHookInfo->CurrentProcess(&pProc);
	if (FAILED(hr))
		FatalExit(L"Failed CurrentProcess");
	hr = pProc->get_Id(&gGamePID);
	if (FAILED(hr))
		FatalExit(L"Failed get_Id");

	return S_OK;
}


VOID WINAPI OnHookRemoved(__in INktHookInfo *lpHookInfo, __in DWORD dwChainIndex)
{
	BSTR name;
	my_ssize_t address;
	CHAR szBufA[1024];

	HRESULT hr = lpHookInfo->get_FunctionName(&name);
	if (FAILED(hr))
		FatalExit(L"Failed GetFunctionName");
	lpHookInfo->get_Address(&address);
	sprintf_s(szBufA, 1024, "GamePlugin::OnHookRemoved called [Hook: %S @ 0x%IX / Chain:%lu]\n",
		name, address, dwChainIndex);
	LogInfo(szBufA);

	return;
}


// This is the primary call we are interested in.  It must be called before CreateSwapChain
// is called by the game.  We can then fetch the returned IDXGIFactory1 object, and
// use that to hook the next level.
// 
// We can use the Deviare side to hook this function, because CreateDXGIFactory1 is
// a direct export from the dxgi DLL, and is also directly supported in the 
// Deviare DB.
//
// Chain of events:
//  3Dmigoto dxgi.dll is hard linked to (some) games, so loaded at game launch.
//   Game calls CreateDXGIFactory1
//    3Dmigoto version is called, as a wrapper.  It calls through to System32 CreateDXGIFactory1
//     Our hook is called here instead, because we hook when it's called, but post call.
//      We call to to HookCreateSwapChain to capture any SwapChains
//     We return the IDXGIFactory1 object. Subclass, so can be directly cast to DXGIFactory
//    3Dmigoto receives IDXGIFactory1, and saves it for wrapping use.
//   3Dmigoto returns IDXGIFactory1 to game, which uses it to CreateSwapChain.



HRESULT WINAPI OnFunctionCall(__in INktHookInfo *lpHookInfo, __in DWORD dwChainIndex,
	__in INktHookCallInfoPlugin *lpHookCallInfoPlugin)
{
	HRESULT hr;

	BSTR name;
	CComPtr<INktParamsEnum> paramsEnum;
	CComPtr<INktParam> param;
	my_ssize_t pointeraddress;
	VARIANT_BOOL isNull;
	VARIANT_BOOL isPreCall;

	IDXGIFactory* pDXGIFactory = nullptr;
	ID3D11Device* pDevice = nullptr;
	IDXGISwapChain* pSwapChain = nullptr;

	hr = lpHookInfo->get_FunctionName(&name);
	if (FAILED(hr))
		FatalExit(L"Failed GetFunctionName");

#ifdef _DEBUG
	my_ssize_t address;
	CHAR szBufA[1024];

	lpHookInfo->get_Address(&address);
	sprintf_s(szBufA, 1024, "GamePlugin::OnFunctionCall called [Hook: %S @ 0x%IX / Chain:%lu]\n",
		name, address, dwChainIndex);
	LogInfo(szBufA);
#endif



	//HRESULT WINAPI D3D11CreateDeviceAndSwapChain(
	//	_In_opt_ IDXGIAdapter* pAdapter,
	//	D3D_DRIVER_TYPE DriverType,
	//	HMODULE Software,
	//	UINT Flags,
	//	_In_reads_opt_(FeatureLevels) CONST D3D_FEATURE_LEVEL* pFeatureLevels,
	//	UINT FeatureLevels,
	//	UINT SDKVersion,
	//	_In_opt_ CONST DXGI_SWAP_CHAIN_DESC* pSwapChainDesc,
	//	_COM_Outptr_opt_ IDXGISwapChain** ppSwapChain,
	//	_COM_Outptr_opt_ ID3D11Device** ppDevice,
	//	_Out_opt_ D3D_FEATURE_LEVEL* pFeatureLevel,
	//	_COM_Outptr_opt_ ID3D11DeviceContext** ppImmediateContext);
	if (wcscmp(name, L"D3D11.DLL!D3D11CreateDeviceAndSwapChain") == 0)
	{
		hr = lpHookCallInfoPlugin->Params(&paramsEnum);
		if (FAILED(hr))
			FatalExit(L"Failed Nektra lpHookCallInfoPlugin->Params");

		lpHookCallInfoPlugin->get_IsPreCall(&isPreCall);
		if (FAILED(hr))
			FatalExit(L"Failed Nektra get_IsPreCall");

		if (isPreCall)
		{
#ifdef _DEBUG 
			unsigned long flags;	// should be UINT. long and int are both 32 bits on windows.
			hr = paramsEnum->GetAt(3, &param.p);
			if (FAILED(hr))
				FatalExit(L"Failed Nektra paramsEnum->GetAt(3)");
			hr = param->get_ULongVal(&flags);
			if (FAILED(hr))
				FatalExit(L"Failed Nektra paramsEnum->get_ULongVal()");

			flags |= D3D11_CREATE_DEVICE_DEBUG;

			hr = param->put_ULongVal(flags);
			if (FAILED(hr))
				FatalExit(L"Failed Nektra paramsEnum->put_ULongVal()");
#endif
			return S_OK;
		}

		// This is PostCall.
		// Param 8 is returned _COM_Outptr_opt_ IDXGISwapChain** ppSwapChain
		hr = paramsEnum->GetAt(8, &param.p);
		if (FAILED(hr))
			FatalExit(L"Failed Nektra paramsEnum->GetAt(8)");
		hr = param->get_IsNullPointer(&isNull);
		if (FAILED(hr))
			FatalExit(L"Failed Nektra param->get_IsNullPointer");
		if (!isNull)
		{
			hr = param->Evaluate(&param.p);
			if (FAILED(hr))
				FatalExit(L"Failed Nektra param->Evaluate");
			hr = param->get_PointerVal(&pointeraddress);
			if (FAILED(hr))
				FatalExit(L"Failed Nektra param->get_PointerVal");
			pSwapChain = reinterpret_cast<IDXGISwapChain*>(pointeraddress);
		}

		HookPresent(pSwapChain);
	}

	// If it's CreateDevice, let's fetch the 7th parameter, which is
	// the returned ppDevice from this Post call.
	//HRESULT D3D11CreateDevice(
	//	IDXGIAdapter            *pAdapter,
	//	D3D_DRIVER_TYPE         DriverType,
	//	HMODULE                 Software,
	//	UINT                    Flags,
	//	const D3D_FEATURE_LEVEL *pFeatureLevels,
	//	UINT                    FeatureLevels,
	//	UINT                    SDKVersion,
	//	ID3D11Device            **ppDevice,
	//	D3D_FEATURE_LEVEL       *pFeatureLevel,
	//	ID3D11DeviceContext     **ppImmediateContext
	//);
	if (wcscmp(name, L"D3D11.DLL!D3D11CreateDevice") == 0)
	{
		hr = lpHookCallInfoPlugin->Params(&paramsEnum);
		if (FAILED(hr))
			FatalExit(L"Failed Nektra lpHookCallInfoPlugin->Params");

		lpHookCallInfoPlugin->get_IsPreCall(&isPreCall);
		if (FAILED(hr))
			FatalExit(L"Failed Nektra get_IsPreCall");

		if (isPreCall)
		{
#ifdef _DEBUG 
			unsigned long flags;	// should be UINT. long and int are both 32 bits on windows.
			hr = paramsEnum->GetAt(3, &param.p);
			if (FAILED(hr))
				FatalExit(L"Failed Nektra paramsEnum->GetAt(3)");
			hr = param->get_ULongVal(&flags);
			if (FAILED(hr))
				FatalExit(L"Failed Nektra paramsEnum->get_ULongVal()");

			flags |= D3D11_CREATE_DEVICE_DEBUG;

			hr = param->put_ULongVal(flags);
			if (FAILED(hr))
				FatalExit(L"Failed Nektra paramsEnum->put_ULongVal()");
#endif
			return S_OK;
		}

		// Param 7 is returned ID3D11Device** ppDevice
		hr = paramsEnum->GetAt(7, &param.p);
		if (FAILED(hr))
			FatalExit(L"Failed Nektra paramsEnum->GetAt(7)");
		hr = param->get_IsNullPointer(&isNull);
		if (FAILED(hr))
			FatalExit(L"Failed Nektra param->get_IsNullPointer");
		if (!isNull)
		{
			hr = param->Evaluate(&param.p);
			if (FAILED(hr))
				FatalExit(L"Failed Nektra param->Evaluate");
			hr = param->get_PointerVal(&pointeraddress);
			if (FAILED(hr))
				FatalExit(L"Failed Nektra param->get_PointerVal");
			pDevice = reinterpret_cast<ID3D11Device*>(pointeraddress);
		}
	}


	// If it's CreateDXGIFactory, or CreateDXGIFactory1, let's fetch the 2nd parameter, which is
	// the returned ppFactory from this Post Only call.  The Desired CreateSwapChain will be the
	// same location for both factories, as Factory1 is descended from Factory.
	// 
	//HRESULT CreateDXGIFactory(
	//	REFIID riid,
	//	void   **ppFactory
	//);
	// HRESULT CreateDXGIFactory1(
	//	REFIID riid,
	//	void   **ppFactory
	// );
	if (wcscmp(name, L"DXGI.DLL!CreateDXGIFactory") == 0 || wcscmp(name, L"DXGI.DLL!CreateDXGIFactory1") == 0)
	{
		hr = lpHookCallInfoPlugin->Params(&paramsEnum);
		if (FAILED(hr))
			FatalExit(L"Failed Nektra lpHookCallInfoPlugin->Params");

		hr = paramsEnum->GetAt(1, &param.p);
		if (FAILED(hr))
			FatalExit(L"Failed Nektra paramsEnum->GetAt(1)");
		hr = param->Evaluate(&param.p);
		if (FAILED(hr))
			FatalExit(L"Failed Nektra param->Evaluate");
		hr = param->get_PointerVal(&pointeraddress);
		if (FAILED(hr))
			FatalExit(L"Failed Nektra param->get_PointerVal");
		pDXGIFactory = reinterpret_cast<IDXGIFactory*>(pointeraddress);

		//ToDo: Not sure this is best way.
		// Upcast to IDXGIFactory2.  
		IUnknown* pUnknown;
		hr = pDXGIFactory->QueryInterface(__uuidof(IUnknown), (void **)&pUnknown);
		if (FAILED(hr))
			FatalExit(L"Failed QueryInterface for IUnknown");

		IDXGIFactory2* pFactory2;
		hr = pUnknown->QueryInterface(__uuidof(IDXGIFactory2), (void **)&pFactory2);
		if (FAILED(hr))
			FatalExit(L"Failed QueryInterface for IDXGIFactory2");

		HookCreateSwapChain(pFactory2);
		HookCreateSwapChainForHwnd(pFactory2);
	}

	// ----------------------------------------------------------------------
	// This is the primary call we are interested in, for DX9.  It will be called before CreateDevice
	// is called by the game.  We can then fetch the returned IDirect3D9 object, and
	// use that to hook the next level, and daisy chain through the call sequence to 
	// ultimately get the Present routine.
	// 
	// We can use the Deviare side to hook this function, because Direct3DCreate9 is
	// a direct export from the d3d9 DLL, and is also directly supported in the 
	// Deviare DB.
	//
	// We are going to actually call the Direct3DCreate9Ex instead however, so that we
	// can get the Ex interface.  We need the Ex objects in order to share surfaces
	// outside of the game Device.  This was a long uphill struggle to understand
	// exactly what it takes.  There is no way to share surfaces with just DX9 itself,
	// it must be DX9Ex.
	// This also means the game is only ever getting a IDirect3D9Ex factory, which should
	// be transparent to the game.
	//
	// We will return the Ex interface created, and do SkipCall on the original.

	// Original API:
	//	IDirect3D9* Direct3DCreate9(
	//		UINT SDKVersion
	//	);
	if (wcscmp(name, L"D3D9.DLL!Direct3DCreate9") == 0 || wcscmp(name, L"D3D9.DLL!Direct3DCreate9Ex") == 0)
	{
		IDirect3D9Ex* pDX9Ex = nullptr;
		INktParam* nktResult;

		//pDX9Ex = reinterpret_cast<IDirect3D9Ex*>(Direct3DCreate9(D3D_SDK_VERSION));
		hr = Direct3DCreate9Ex(D3D_SDK_VERSION, &pDX9Ex);
		if (FAILED(hr))
			FatalExit(L"Failed Direct3DCreate9Ex");

		// At this point, we are going to switch from using Deviare style calls
		// to In-Proc style calls, because the routines we need to hook are not
		// found in the Deviare database.  It would be possible to add them 
		// and then rebuilding it, but In-Proc works alongside Deviare so this
		// approach is simpler.

		HookCreateDevice(pDX9Ex);

		// The result of the Direct3DCreate9Ex function is the IDirect3D9Ex object, which you 
		// can think of as DX9 itself. 
		//
		// We want to skip the original call to Direct3DCreate9, because we want to just
		// return this IDirect3D9Ex object.  This will tell Nektra to skip it.

		hr = lpHookCallInfoPlugin->SkipCall();
		if (FAILED(hr))
			FatalExit(L"Failed SkipCall");

		// However, we still need a proper return result from this call, so we set the 
		// Nektra Result to be our IDirect3D9Ex object.  This will ultimately return to
		// game, and be used as its IDirect3D9, even though it is IDirect3D9Ex.
		//
		// The Nektra API is poor and uses long, and long long here, so we need to
		// carefully convert to those values.

		LONG_PTR retPtr = (LONG_PTR)pDX9Ex;

		hr = lpHookCallInfoPlugin->Result(&nktResult);
		if (FAILED(hr))
			FatalExit(L"Failed Get NktResult");
		hr = nktResult->put_PointerVal(retPtr);
		if (FAILED(hr))
			FatalExit(L"Failed put pointer");
	}

	// ToDo: wrong get I think for CreateDXGIFactory
	//INktParam* nktResult;
	//hr = lpHookCallInfoPlugin->Result(&nktResult);
	//if (FAILED(hr))
	//	FatalExit(L"Failed Get NktResult");
	//hr = nktResult->get_PointerVal((__int64*)&pDXGIFactory);
	//if (FAILED(hr))
	//	FatalExit\(L"Failed put pointer"\);

	// At this point, we are going to switch from using Deviare style calls
	// to In-Proc style calls, because the routines we need to hook are not
	// found in the Deviare database.  It would be possible to add them 
	// and then rebuilding it, but In-Proc works alongside Deviare so this
	// approach is simpler.

	// HookCreateSwapChain(pDXGIFactory);


	return S_OK;
}


// ----------------------------------------------------------------------
// Fatal error handling.  This is for scenarios that should never happen,
// but should be checked just for reliability and unforeseen scenarios.
//
// We tried using std::exception, but that was mostly useless because nearly every
// game has an exception handler of some form that wraps and silently kills any
// exceptions, which looked to the end-user as just a game-hang.  
//
// This attempt here is to put up a MessageBox with the informative error text
// and exit the game.  This should be better than simple logging, because at
// least the user gets immediate notification and does not have to sift around
// to find log files.  

void FatalExit(LPCWSTR errorString)
{
	MessageBox(NULL, errorString, L"Fatal Error", MB_OK);
	exit(1);
}
