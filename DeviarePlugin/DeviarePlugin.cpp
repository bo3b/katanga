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


// This sequence is to handle being able to directly call the 
// Direct3DCreate9Ex function in System32.  We need to directly call
// it because HelixMod does not export this function when it wraps.
HMODULE d3d9LibHandle = NULL;
typedef HRESULT(__stdcall *fnOrigDirect3DCreate9Ex)(
	UINT SDKVersion,
	IDirect3D9Ex**);
fnOrigDirect3DCreate9Ex pOrigDirect3DCreate9Ex = nullptr;

// Reference to the d9Ex interface, that we want global scope.
IDirect3D9Ex* pDX9Ex = nullptr;


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

	FreeLibrary(d3d9LibHandle);

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
//
// Chain of events:
//  HelixMod d3d9.dll is hard linked to (some) games, so loaded at game launch.
//   Game calls Direct3DCreate9
//    HelixMod version is called, as a wrapper.  It calls through to System32 Direct3DCreate9
//     Our hook is called here instead, because we hook before it's called, but after init.
//      We call through to System32 Direct3DCreate9Ex
//     We return the IDirect3D9Ex object. Subclass, so can be directly cast to IDirect3D9
//    HelixMod receives IDirect3D9Ex, and saves it for wrapping use.
//   HelixMod returns IDirect3D9Ex to game, which uses it to CreateDevice.
//
// This needs be done as the pre-call, so that we can pass back the object IDirect3D9Ex
// object for both game and HelixMod to use.  If it were post call, they can have already
// cached the wrong object.

// Original API:
//	IDirect3D9* Direct3DCreate9(
//		UINT SDKVersion
//	);


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

	// bring it into memory, make it the d3d9.
	HMODULE d3d9Helix = LoadLibraryEx(L"d3d9.dll", NULL, LOAD_LIBRARY_SEARCH_APPLICATION_DIR);
	IDirect3D9* create9 = Direct3DCreate9(D3D_SDK_VERSION);

	wchar_t info[512];
	wchar_t fullpath[_MAX_PATH];

	GetModuleFileName(d3d9Helix, fullpath, MAX_PATH);
	swprintf_s(info, _countof(info),
		L"LoadLibrary %s @%p\n", fullpath, d3d9Helix);
	::OutputDebugString(info);

	LPVOID create9Address = GetProcAddress(d3d9Helix, "Direct3DCreate9");
	LPVOID create9ExAddress = GetProcAddress(d3d9Helix, "Direct3DCreate9Ex");
	swprintf_s(info, _countof(info),
		L"HelixD3D9: Direct3DCreate9 @%p  Direct3DCreate9Ex @%p\n", create9Address, create9ExAddress);
	::OutputDebugString(info);


	// We only expect this to be called for D3D9.DLL!Direct3DCreate9. We want to daisy chain
	// through the call sequence to ultimately get the Present routine.
	// 
	// However, because we want a Direct3DCreate9Ex interface instead of the normal one, we will 
	// go ahead and call it directly.  This might bypass hooks on the original Direct3DCreate9.
	//
	// This also means the game is only ever getting a IDirect3D9Ex factory, which is probably
	// superior, because everything will use it.  We must use IDirect3D9Ex in order to share surfaces.
	// 
	// HelixMod does not include a pass through reference for Direct3DCreate9Ex however.  That
	// means that anytime we connect to a game using HelixMod, we'll fail, because the function 
	// is missing.  On early versions of HelixMod. Last 3 versions the call exists.
	// Still, rather than static link to the call, we'll fetch the address and jump to the
	// pointer manually. This way the game fixes don't need to change the d3d9.dll, which can
	// sometimes cause fixes to break.

	WCHAR d3d9Path[MAX_PATH];
	GetSystemDirectoryW(d3d9Path, MAX_PATH);
	wcscat_s(d3d9Path, MAX_PATH, L"\\d3d9.dll");

	d3d9LibHandle = LoadLibraryEx(d3d9Path, NULL, 0);
	if (d3d9LibHandle == NULL)
		throw std::exception("Failed LoadLibraryEx for System32 d3d9.dll");


	GetModuleFileName(d3d9LibHandle, fullpath, MAX_PATH);
	swprintf_s(info, _countof(info),
		L"LoadLibrary %s @%p\n", fullpath, d3d9LibHandle);
	::OutputDebugString(info);

	create9Address = GetProcAddress(d3d9LibHandle, "Direct3DCreate9");
	create9ExAddress = GetProcAddress(d3d9LibHandle, "Direct3DCreate9Ex");
	swprintf_s(info, _countof(info),
		L"System32 D3D9: Direct3DCreate9 @%p  Direct3DCreate9Ex @%p\n", create9Address, create9ExAddress);
	::OutputDebugString(info);


	LPVOID system32Create9ExAddress = GetProcAddress(d3d9LibHandle, "Direct3DCreate9Ex");
	if (system32Create9ExAddress == NULL)
	{
		DWORD err = GetLastError();
		wchar_t info[512];
		swprintf_s(info, _countof(info),
			L"GetProcAddress error: %d\n", err);
		::OutputDebugString(info);

		throw std::exception("Failed to GetProcAddress for Direct3DCreate9Ex");
	}

	pOrigDirect3DCreate9Ex = reinterpret_cast<fnOrigDirect3DCreate9Ex>(system32Create9ExAddress);


	hr = pOrigDirect3DCreate9Ex(D3D_SDK_VERSION, &pDX9Ex);
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

	//hr = lpHookCallInfoPlugin->SkipCall();
	//if (FAILED(hr))
	//	throw std::exception("Failed SkipCall");

	// However, we still need a proper return result from this call, so we set the 
	// Nektra Result to be our IDirect3D9Ex object.  This will ultimately return to
	// game, and be used as its IDirect3D9, even though it is IDirect3D9Ex.

	hr = lpHookCallInfoPlugin->Result(&nktResult);
	if (FAILED(hr))
		throw std::exception("Failed Get NktResult");
	hr = nktResult->put_PointerVal((long)pDX9Ex);
	if (FAILED(hr))
		throw std::exception("Failed put pointer");

	// Toss it, we are using Create9Ex.
	create9->Release();

	return S_OK;
}


