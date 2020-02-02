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

// If we are in 3D Vision Direct Mode, we need to copy the textures from each
// eye, instead of using the ReverseStereoBlit.  This changes the mode of
// copying in Present.  Now works in DX9 as well as DX11.

bool gDirectMode = false;


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
	LogInfo(L"GetSharedHandle::%p\n", gGameSharedHandle);
#endif

	return gGameSharedHandle;
}

//-----------------------------------------------------------

// Used for both CreateDevice and Reset/Resize functions, where we are setting up the
// graphic environment, and thus cannot allow the VR side to draw using anything
// from the game side here.
// We should be OK using a 1 second wait here, if we cannot grab the mutex from the
// Katanga side in 1 second, something is definitely broken.

void CaptureSetupMutex()
{
	DWORD waitResult;

	LogInfo(L"-> CaptureSetupMutex mutex:%p\n", gSetupMutex);

	if (gSetupMutex == NULL)
		FatalExit(L"CaptureSetupMutex: mutex does not exist.", GetLastError());

	waitResult = WaitForSingleObject(gSetupMutex, 1000);
	LogInfo(L"  WaitForSingleObject mutex:%p, result:0x%x\n", gSetupMutex, waitResult);
	if (waitResult != WAIT_OBJECT_0)
	{
		wchar_t info[512];
		DWORD hr = GetLastError();
		swprintf_s(info, _countof(info), L"CaptureSetupMutex: WaitForSingleObject failed.\nwaitResult: 0x%x, err: 0x%x\n", waitResult, hr);
		LogInfo(info);
		FatalExit(info, hr);
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
	LogInfo(L"<- ReleaseSetupMutex mutex:%p\n", gSetupMutex);

	if (gSetupMutex == NULL)
		FatalExit(L"ReleaseSetupMutex: mutex does not exist.", GetLastError());

	bool ok = ReleaseMutex(gSetupMutex);
	LogInfo(L"  ReleaseSetupMutex mutex:%p, result:%s\n", gSetupMutex, ok? L"OK" : L"FAIL");
	if (!ok)
	{
		DWORD hr = GetLastError();
		LogInfo(L"ReleaseSetupMutex: ReleaseMutex failed, err: 0x%x\n",  hr);
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

	LogInfo(L"\nGamePlugin C++ logging enabled.\n\n");
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
	
	LogInfo(L"GamePlugin::OnLoad called\n");

	// Setup shared mutex, with the VR side owning it.  We should never arrive
	// here without the Katanga side already having created it.
	// We will grab mutex to lock their drawing, whenever we are setting up
	// the shared surface.
	gSetupMutex = OpenMutex(SYNCHRONIZE, false, L"KatangaSetupMutex");
	LogInfo(L"GamePlugin: OpenMutex called: %p\n", gSetupMutex);
	if (gSetupMutex == NULL)
		FatalExit(L"OnLoad: could not find KatangaSetupMutex", GetLastError());


	// This is running inside the game itself, so make sure we can use
	// COM here.
	::CoInitialize(NULL);

	// Hook the NVAPI.DLL!nvapi_QueryInterface, so that we can watch
	// for Direct Mode by games.  ToDo: DX9 variant?
	HookNvapiSetDriverMode();

	return S_OK;
}


VOID WINAPI OnUnload()
{
	LogInfo(L"GamePlugin::OnUnLoad called\n");

	LogInfo(L"GamePlugin: ReleaseMutex for %p\n", gSetupMutex);
	if (gSetupMutex != NULL)
	{
		ReleaseMutex(gSetupMutex);
		LogInfo(L"GamePlugin: ReleaseMutex err:0x%x\n", GetLastError());
		CloseHandle(gSetupMutex);
		LogInfo(L"GamePlugin: CloseHandle for %p, err:0x%x\n", gSetupMutex, GetLastError());
	}

	::CoUninitialize();

	LogInfo(L"\nGamePlugin C++ log closed.\n\n");
	fclose(LogFile);

	return;
}


// Since the calling Unity app has the info passed to it from 3DFM, it will know what
// type of game and what API we are targeting.  Let's use that info to specify the 
// required hook to activate the agent here.  
// If it's DX11, we'll send in the CreateDevice call.
// If it's DX9, we'll send in the Direct3DCreate9 call.
// If it's DX9Ex we'll send in the Direct3DCreate9Ex call.

HRESULT WINAPI OnHookAdded(__in INktHookInfo *lpHookInfo, __in DWORD dwChainIndex,
	__in LPCWSTR szParametersW)
{
	BSTR name;
	my_ssize_t address;
	INktProcess* pProc;
	long gGamePID;

	HRESULT hr = lpHookInfo->get_FunctionName(&name);
	if (FAILED(hr))
		FatalExit(L"Failed GetFunctionName", hr);
	lpHookInfo->get_Address(&address);
	
	LogInfo(L"GamePlugin::OnHookAdded called [Hook: %ls @ 0x%IX / Chain:%lu]\n",
		name, address, dwChainIndex);

	hr = lpHookInfo->CurrentProcess(&pProc);
	if (FAILED(hr))
		FatalExit(L"Failed CurrentProcess", hr);
	hr = pProc->get_Id(&gGamePID);
	if (FAILED(hr))
		FatalExit(L"Failed get_Id", hr);

	// Only hook the API the game is using.

	if (wcscmp(name, L"D3D11.DLL!D3D11CreateDevice") == 0)
		FindAndHookDX11Present();
	if (wcscmp(name, L"D3D9.DLL!Direct3DCreate9") == 0)
		HookDirect3DCreate9();
	if (wcscmp(name, L"D3D9.DLL!Direct3DCreate9Ex") == 0)
		FindAndHookDX9ExPresent();

	return S_OK;
}


VOID WINAPI OnHookRemoved(__in INktHookInfo *lpHookInfo, __in DWORD dwChainIndex)
{
	BSTR name;
	my_ssize_t address;

	HRESULT hr = lpHookInfo->get_FunctionName(&name);
	if (FAILED(hr))
		FatalExit(L"Failed GetFunctionName", hr);
	lpHookInfo->get_Address(&address);

	LogInfo(L"GamePlugin::OnHookRemoved called [Hook: %ls @ 0x%IX / Chain:%lu]\n",
			name, address, dwChainIndex);

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

// Previously was used for actual startup, now just logging.

HRESULT WINAPI OnFunctionCall(__in INktHookInfo *lpHookInfo, __in DWORD dwChainIndex,
	__in INktHookCallInfoPlugin *lpHookCallInfoPlugin)
{
	HRESULT hr;

	BSTR name;
	CComPtr<INktParamsEnum> paramsEnum;
	CComPtr<INktParam> param;

	IDXGIFactory* pDXGIFactory = nullptr;
	ID3D11Device* pDevice = nullptr;
	IDXGISwapChain* pSwapChain = nullptr;

	hr = lpHookInfo->get_FunctionName(&name);
	if (FAILED(hr))
		FatalExit(L"GamePlugin::OnFunctionCall Failed GetFunctionName", hr);

	my_ssize_t address;

	lpHookInfo->get_Address(&address);
	LogInfo(L"GamePlugin::OnFunctionCall called [Hook: %ls @ 0x%IX / Chain:%lu]\n",
		name, address, dwChainIndex);

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

void FatalExit(LPCWSTR errorString, HRESULT code)
{
	wchar_t info[512];

	swprintf_s(info, _countof(info), L" Fatal Error: %s  (0x%x)\n", errorString, code);
	LogInfo(info);

	MessageBox(NULL, info, L"GamePlugin: Fatal Error", MB_OK);
	exit(1);
}
