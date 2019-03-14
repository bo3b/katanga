// NativePlugin.cpp : Defines the exported functions for the DLL.
//
// This part of the code is the Deviare style hook that handles the 
// very first DX11 object creation.  Even though this is C++, it is 
// most useful for Deviare style hooks that are supported by their DB,
// and are direct exports from a DLL.  It's not well suited for vtable
// based calls like those used by DX11.

#include "DeviarePlugin.h"

// We need the Deviare interface though, to be able to provide the OnLoad,
// OnFunctionCalled interfaces, and to be able to LoadCustomDLL this DLL from
// the C# app.

#import "DeviareCOM64.dll" raw_interfaces_only, named_guids, raw_dispinterfaces, auto_rename



// Reference to the game's dxgifactory interface, that we want global scope.
IDXGIFactory1* pDXGIFactory = nullptr;


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

// Original API:
// HRESULT CreateDXGIFactory1(
//	REFIID riid,
//	void   **ppFactory
// );


HRESULT WINAPI OnFunctionCall(__in INktHookInfo *lpHookInfo, __in DWORD dwChainIndex,
	__in INktHookCallInfoPlugin *lpHookCallInfoPlugin)
{
	BSTR name;
	INktParam* nktResult;
	HRESULT hr;

	hr = lpHookInfo->get_FunctionName(&name);
	if (FAILED(hr))
		throw std::exception("Failed GetFunctionName");

	::OutputDebugString(L"NativePlugin::OnFunctionCall called for ");
	::OutputDebugString(name);
	::OutputDebugString(L"\n");


	// We only expect this to be called for DXGI.DLL!CreateDXGIFactory1. We want to daisy chain
	// through the call sequence to ultimately get the Present routine.
	// At this moment we are right after the call has finished.

	hr = lpHookCallInfoPlugin->Result(&nktResult);
	if (FAILED(hr))
		throw std::exception("Failed Get NktResult");
	hr = nktResult->get_PointerVal((__int64*)&pDXGIFactory);
	if (FAILED(hr))
		throw std::exception("Failed put pointer");

	// At this point, we are going to switch from using Deviare style calls
	// to In-Proc style calls, because the routines we need to hook are not
	// found in the Deviare database.  It would be possible to add them 
	// and then rebuilding it, but In-Proc works alongside Deviare so this
	// approach is simpler.

	HookCreateSwapChain(pDXGIFactory);

	return S_OK;
}


