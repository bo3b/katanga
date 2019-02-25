// NativePlugin.cpp : Defines the exported functions for the DLL.
//
// This part of the code is the Deviare style hook that handles the 
// very first DX9 object creation.  Even though this is C++, it is 
// most useful for Deviare style hooks that are supported by their DB,
// and are direct exports from a DLL.  It's not well suited for vtable
// based calls like those used by DX9.

#include "DeviarePlugin.h"

// We need the Deviare interface though, to be able to provide the OnLoad,
// OnFunctionCalled interfaces, and to be able to LoadCustomDLL this DLL from
// the C# app.

#import "DeviareCOM.dll" raw_interfaces_only, named_guids, raw_dispinterfaces, auto_rename



// --------------------------------------------------------------------------------------------------
//IMPORTANT NOTES:
//---------------
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
	::OutputDebugStringA("NativePlugin::OnHookAdded called\n");
	return S_OK;
}


VOID WINAPI OnHookRemoved(__in INktHookInfo *lpHookInfo, __in DWORD dwChainIndex)
{
	::OutputDebugStringA("NativePlugin::OnHookRemoved called\n");
	return;
}


// This is the primary call we are interested in.  It will be called before CreateDevice
// is called by the game.  We can then fetch the returned IDirect3D9 object, and
// use that to hook the next level.
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
// Still true, using cuda copies, cuda requires DX9Ex or will throw an error. However,
// using Cuda we no longer need follow on calls like CreateDevice be CreateDeviceEx,
// which was causing problems.
//
// We will return the Ex interface created, and do SkipCall on the original.

// Original APIs:
//	IDirect3D9* Direct3DCreate9(
//		UINT SDKVersion
//	);
// HRESULT WINAPI Direct3DCreate9Ex(
//		UINT SDKVersion, IDirect3D9Ex**
// );


HRESULT WINAPI OnFunctionCall(__in INktHookInfo *lpHookInfo, __in DWORD dwChainIndex,
	__in INktHookCallInfoPlugin *lpHookCallInfoPlugin)
{
	BSTR name;
	INktParam* nktResult;
	IDirect3D9Ex* pDX9Ex = nullptr;
	HRESULT hr;

	hr = lpHookInfo->get_FunctionName(&name);
	if (FAILED(hr))
		throw std::exception("Failed GetFunctionName");

	::OutputDebugString(L"NativePlugin::OnFunctionCall called for ");
	::OutputDebugString(name);
	::OutputDebugString(L"\n");

	// We only expect this to be called for D3D9.DLL!Direct3DCreate9. We want to daisy chain
	// through the call sequence to ultimately get the Present routine.

	hr = Direct3DCreate9Ex(D3D_SDK_VERSION, &pDX9Ex);
	if (FAILED(hr))
		throw std::exception("Failed Direct3DCreate9Ex");

	// At this point, we are going to switch from using Deviare style calls
	// to In-Proc style calls, because the routines we need to hook are not
	// found in the Deviare database.  It would be possible to add them 
	// and then rebuilding it, but In-Proc works alongside Deviare so this
	// approach is simpler.

	HookCreateDevice(pDX9Ex);

	// The result of the Direct3DCreate9 function is the IDirect3D9 object, which you 
	// can think of as DX9 itself. They never call it a DX9 factory, but it is.
	//
	// We want to skip the original call to Direct3DCreate9, because we want to just
	// return this IDirect3D9 object.  This will tell Nektra to skip it.

	hr = lpHookCallInfoPlugin->SkipCall();
	if (FAILED(hr))
		throw std::exception("Failed SkipCall");

	// However, we still need a proper return result from this call, so we set the 
	// Nektra Result to be our IDirect3D9Ex object.  This will ultimately return to
	// game, and be used as its IDirect3D9.

	hr = lpHookCallInfoPlugin->Result(&nktResult);
	if (FAILED(hr))
		throw std::exception("Failed Get NktResult");
	hr = nktResult->put_PointerVal((long)pDX9Ex);
	if (FAILED(hr))
		throw std::exception("Failed put pointer");

	return S_OK;
}


