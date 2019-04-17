// DeviarePlugin.cpp : Defines the exported functions for the DLL.
//
// This part of the code is the Deviare style hook that handles the 
// very first DX11 or DX9 object creation.  Even though this is C++, it is 
// most useful for Deviare style hooks that are supported by their DB,
// and are direct exports from a DLL.  It's not well suited for vtable
// based calls like Present used by DX11 or DX9.

#include "DeviarePlugin.h"

#include <atlbase.h>


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

// This is automatically instantiated by C++, so the hooking library
// is immediately available.
CNktHookLib nktInProc;

// Required for reverse stereo blit to give us stereo backbuffer.
StereoHandle gNVAPI = nullptr;

// The actual shared Handle to the DX11 VR side.  Filled in by an active InProc_*.
HANDLE gGameSharedHandle = nullptr;


// --------------------------------------------------------------------------------------------------
// Return the current value of the gGameSurfaceShare.  This is the HANDLE
// that is necessary to share from either DX9Ex or DX11 here, to DX11 in the VR app.
//
// This routine is defined here, so that we have only a single implementation of the
// routine with a declaration in the DeviarePlugin.h file.  This routine is specific
// to the DeviarePlugin itself, not the InProc sides.

HANDLE WINAPI GetSharedHandle(int* in)
{
	::OutputDebugString(L"GetSharedHandle::\n");

	return gGameSharedHandle;
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
	::OutputDebugStringA("NativePlugin::OnLoad called\n");

	// This is running inside the game itself, so make sure we can use
	// COM here.
	::CoInitializeEx(NULL, COINIT_MULTITHREADED);

	// bump ref count.  keep it loaded.
	LoadLibrary(L"d3d11.dll");
	return S_OK;
}


VOID WINAPI OnUnload()
{
	::OutputDebugStringA("NativePlugin::OnUnLoad called\n");

	return;
}


HRESULT WINAPI OnHookAdded(__in INktHookInfo *lpHookInfo, __in DWORD dwChainIndex,
	__in LPCWSTR szParametersW)
{
	CComBSTR name;
	my_ssize_t address;
	CHAR szBufA[1024];
	INktProcess* pProc;
	long gGamePID;

	HRESULT hr = lpHookInfo->get_FunctionName(&name);
	if (FAILED(hr))
		throw std::exception("Failed GetFunctionName");
	lpHookInfo->get_Address(&address);
	sprintf_s(szBufA, 1024, "DeviarePlugin::OnHookAdded called [Hook: %S @ 0x%IX / Chain:%lu]\n",
		name, address, dwChainIndex);
	::OutputDebugStringA(szBufA);

	hr = lpHookInfo->CurrentProcess(&pProc);
	if (FAILED(hr))
		throw std::exception("Failed CurrentProcess");
	hr = pProc->get_Id(&gGamePID);
	if (FAILED(hr))
		throw std::exception("Failed get_Id");

	return S_OK;
}


VOID WINAPI OnHookRemoved(__in INktHookInfo *lpHookInfo, __in DWORD dwChainIndex)
{
	CComBSTR name;
	my_ssize_t address;
	CHAR szBufA[1024];

	HRESULT hr = lpHookInfo->get_FunctionName(&name);
	if (FAILED(hr))
		throw std::exception("Failed GetFunctionName");
	lpHookInfo->get_Address(&address);
	sprintf_s(szBufA, 1024, "DeviarePlugin::OnHookRemoved called [Hook: %S @ 0x%IX / Chain:%lu]\n",
		name, address, dwChainIndex);
	::OutputDebugStringA(szBufA);

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

	CComBSTR name;
	CComPtr<INktParamsEnum> paramsEnum;
	CComPtr<INktParam> param;
	my_ssize_t pointeraddress;
	VARIANT_BOOL notNull;
	VARIANT_BOOL isPreCall;

	IDXGIFactory* pDXGIFactory = nullptr;
	ID3D11Device* pDevice = nullptr;
	IDXGISwapChain* pSwapChain = nullptr;

	hr = lpHookInfo->get_FunctionName(&name);
	if (FAILED(hr))
		throw std::exception("Failed GetFunctionName");

#ifdef _DEBUG
	my_ssize_t address;
	CHAR szBufA[1024];

	lpHookInfo->get_Address(&address);
	sprintf_s(szBufA, 1024, "DeviarePlugin::OnFunctionCall called [Hook: %S @ 0x%IX / Chain:%lu]\n",
		name, address, dwChainIndex);
	::OutputDebugStringA(szBufA);
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
			throw std::exception("Failed Nektra lpHookCallInfoPlugin->Params");

		lpHookCallInfoPlugin->get_IsPreCall(&isPreCall);
		if (FAILED(hr))
			throw std::exception("Failed Nektra get_IsPreCall");

		if (isPreCall)
		{
#ifdef _DEBUG 
			unsigned long flags;	// should be UINT. long and int are both 32 bits on windows.
			hr = paramsEnum->GetAt(3, &param);
			if (FAILED(hr))
				throw std::exception("Failed Nektra paramsEnum->GetAt(3)");
			hr = param->get_ULongVal(&flags);
			if (FAILED(hr))
				throw std::exception("Failed Nektra paramsEnum->get_ULongVal()");

			flags |= D3D11_CREATE_DEVICE_DEBUG;

			hr = param->put_ULongVal(flags);
			if (FAILED(hr))
				throw std::exception("Failed Nektra paramsEnum->put_ULongVal()");
#endif
			return S_OK;
		}

		// This is PostCall.
		// Param 8 is returned _COM_Outptr_opt_ IDXGISwapChain** ppSwapChain
		hr = paramsEnum->GetAt(8, &param.p);
		if (FAILED(hr))
			throw std::exception("Failed Nektra paramsEnum->GetAt(8)");
		hr = param->get_IsNullPointer(&notNull);
		if (FAILED(hr))
			throw std::exception("Failed Nektra param->get_IsNullPointer");
		if (notNull)
		{
			hr = param->Evaluate(&param.p);
			if (FAILED(hr))
				throw std::exception("Failed Nektra param->Evaluate");
			hr = param->get_PointerVal(&pointeraddress);
			if (FAILED(hr))
				throw std::exception("Failed Nektra param->get_PointerVal");
			pSwapChain = reinterpret_cast<IDXGISwapChain*>(pointeraddress);
		}
		// Param 9 is returned _COM_Outptr_opt_ ID3D11Device** ppDevice
		hr = paramsEnum->GetAt(9, &param.p);
		if (FAILED(hr))
			throw std::exception("Failed Nektra paramsEnum->GetAt(9)");
		hr = param->get_IsNullPointer(&notNull);
		if (FAILED(hr))
			throw std::exception("Failed Nektra param->get_IsNullPointer");
		if (notNull)
		{
			hr = param->Evaluate(&param.p);
			if (FAILED(hr))
				throw std::exception("Failed Nektra param->Evaluate");
			hr = param->get_PointerVal(&pointeraddress);
			if (FAILED(hr))
				throw std::exception("Failed Nektra param->get_PointerVal");
			pDevice = reinterpret_cast<ID3D11Device*>(pointeraddress);
		}

		HookPresent(pDevice, pSwapChain);
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
			throw std::exception("Failed Nektra lpHookCallInfoPlugin->Params");

		lpHookCallInfoPlugin->get_IsPreCall(&isPreCall);
		if (FAILED(hr))
			throw std::exception("Failed Nektra get_IsPreCall");

		if (isPreCall)
		{
#ifdef _DEBUG 
			unsigned long flags;	// should be UINT. long and int are both 32 bits on windows.
			hr = paramsEnum->GetAt(3, &param.p);
			if (FAILED(hr))
				throw std::exception("Failed Nektra paramsEnum->GetAt(3)");
			hr = param->get_ULongVal(&flags);
			if (FAILED(hr))
				throw std::exception("Failed Nektra paramsEnum->get_ULongVal()");

			flags |= D3D11_CREATE_DEVICE_DEBUG;

			hr = param->put_ULongVal(flags);
			if (FAILED(hr))
				throw std::exception("Failed Nektra paramsEnum->put_ULongVal()");
#endif
			return S_OK;
		}

		// Param 7 is returned ID3D11Device** ppDevice
		hr = paramsEnum->GetAt(7, &param.p);
		if (FAILED(hr))
			throw std::exception("Failed Nektra paramsEnum->GetAt(7)");
		hr = param->get_IsNullPointer(&notNull);
		if (FAILED(hr))
			throw std::exception("Failed Nektra param->get_IsNullPointer");
		if (notNull)
		{
			hr = param->Evaluate(&param.p);
			if (FAILED(hr))
				throw std::exception("Failed Nektra param->Evaluate");
			hr = param->get_PointerVal(&pointeraddress);
			if (FAILED(hr))
				throw std::exception("Failed Nektra param->get_PointerVal");
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
			throw std::exception("Failed Nektra lpHookCallInfoPlugin->Params");

		hr = paramsEnum->GetAt(1, &param.p);
		if (FAILED(hr))
			throw std::exception("Failed Nektra paramsEnum->GetAt(1)");
		hr = param->Evaluate(&param.p);
		if (FAILED(hr))
			throw std::exception("Failed Nektra param->Evaluate");
		hr = param->get_PointerVal(&pointeraddress);
		if (FAILED(hr))
			throw std::exception("Failed Nektra param->get_PointerVal");
		pDXGIFactory = reinterpret_cast<IDXGIFactory*>(pointeraddress);

		//ToDo: Not sure this is best way.
		// Upcast to IDXGIFactory2.  
		IUnknown* pUnknown;
		hr = pDXGIFactory->QueryInterface(__uuidof(IUnknown), (void **)&pUnknown);
		if (FAILED(hr))
			throw std::exception("Failed QueryInterface for IUnknown");

		IDXGIFactory2* pFactory2;
		hr = pUnknown->QueryInterface(__uuidof(IDXGIFactory2), (void **)&pFactory2);
		if (FAILED(hr))
			throw std::exception("Failed QueryInterface for IDXGIFactory2");

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
	if (wcscmp(name, L"D3D9.DLL!Direct3DCreate9") == 0)
	{
		IDirect3D9Ex* pDX9Ex = nullptr;
		INktParam* nktResult;

		hr = Direct3DCreate9Ex(D3D_SDK_VERSION, &pDX9Ex);
		if (FAILED(hr))
			throw std::exception("Failed Direct3DCreate9Ex");

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
			throw std::exception("Failed SkipCall");

		// However, we still need a proper return result from this call, so we set the 
		// Nektra Result to be our IDirect3D9Ex object.  This will ultimately return to
		// game, and be used as its IDirect3D9, even though it is IDirect3D9Ex.

		hr = lpHookCallInfoPlugin->Result(&nktResult);
		if (FAILED(hr))
			throw std::exception("Failed Get NktResult");
		hr = nktResult->put_PointerVal((long)pDX9Ex);
		if (FAILED(hr))
			throw std::exception("Failed put pointer");
	}

	// ToDo: wrong get I think for CreateDXGIFactory
	//INktParam* nktResult;
	//hr = lpHookCallInfoPlugin->Result(&nktResult);
	//if (FAILED(hr))
	//	throw std::exception("Failed Get NktResult");
	//hr = nktResult->get_PointerVal((__int64*)&pDXGIFactory);
	//if (FAILED(hr))
	//	throw std::exception("Failed put pointer");

	// At this point, we are going to switch from using Deviare style calls
	// to In-Proc style calls, because the routines we need to hook are not
	// found in the Deviare database.  It would be possible to add them 
	// and then rebuilding it, but In-Proc works alongside Deviare so this
	// approach is simpler.

	// HookCreateSwapChain(pDXGIFactory);


	return S_OK;
}

