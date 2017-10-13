// For this part of the DLL, it makes more sense to use Nektra In-Proc
// instead of Nektra Deviare.  The interface for DX9 is not well supported
// by the DB for Deviare, so getting access to the DX9 APIs is simpler
// using In-Proc.  It would be possible to add the DX9 interfaces to 
// the DB files, and rebuild the DB if you wanted to use Deviare.
//
// All In-Proc use is in this file, all Deviare use is in NativePlugin.
// 
// This file uses a special way of including d3d9.h, and is thus a
// standalone compilation unit to avoid conflicting with normal d3d9 use.

//-----------------------------------------------------------

// Exclude rarely-used stuff from Windows headers, and use a header
// set that will be workable upon our base target OS of Win7.

#define WINVER 0x0500
#define _WIN32_WINNT 0x0500
#define _WIN32_WINDOWS 0x0410
#define _WIN32_IE 0x0700
#define WIN32_LEAN_AND_MEAN

// Including SDKDDKVer.h defines the highest available Windows platform.

// If you wish to build your application for a previous Windows platform, include WinSDKVer.h and
// set the _WIN32_WINNT macro to the platform you wish to support before including SDKDDKVer.h.

#include <SDKDDKVer.h>

#include <windows.h>

#include <stdlib.h>

#include <wchar.h>


// This is a bit weird.  By setting the CINTERFACE before including d3d9.h, we get access
// to the C style interface, which includes direct access to the vTable for the objects.
// That makes it possible to just reference the lpVtbl->CreateDevice, instead of having
// magic constants, and multiple casts to fetch the address of the CreateDevice routine.
//
// This can only be included here where it's used to fetch those routine addresses, because
// it will make other C++ units fail to compile, like NativePlugin.cpp.

#define CINTERFACE
#include <d3d9.h>
#undef CINTERFACE


#include "NktHookLib.h"


//-----------------------------------------------------------

// This is automatically instantiated by C++, so the hooking library
// is immediately available.
CNktHookLib nktInProc;

// The surface that we copy the current game frame into. It is shared.
IDirect3DSurface9* gGameSurface = nullptr;
HANDLE gGameSurfaceShare = nullptr;

// --------------------------------------------------------------------------------------------------

// Custom routines for this NativePlugin.dll, that the master app can call.
// The Input is the IDirect3DBaseTexture9 for the main screen.

IDirect3DSurface9* WINAPI GetGameSurface(int* in)
{
//	::OutputDebugString(L"NativePlugin::GetGameSurface called\n");
	

	return gGameSurface;
}


//-----------------------------------------------------------
// Interface to implement the hook for IDirect3DDevice9->Present

// This declaration serves a dual purpose of defining the interface routine as required by
// DX9, and also is the storage for the original call, returned by nktInProc.Hook

SIZE_T hook_id_Present;
STDMETHOD_(HRESULT, pOrigPresent)(IDirect3DDevice9Ex* This,
	/* [in] */ const RECT    *pSourceRect,
	/* [in] */ const RECT    *pDestRect,
	/* [in] */       HWND    hDestWindowOverride,
	/* [in] */ const RGNDATA *pDirtyRegion
	) = nullptr;


// This is it. The one we are often after.  This is the hook for the DX9 Present call
// which the game will call for every frame.  


STDMETHODIMP_(HRESULT) Hooked_Present(IDirect3DDevice9Ex* This,
	CONST RECT* pSourceRect, CONST RECT* pDestRect, HWND hDestWindowOverride, CONST RGNDATA* pDirtyRegion)
{
	HRESULT hr;
//	::OutputDebugStringA("NativePlugin::Hooked_Present called\n");	// Called too often to log

	// Let's get the backbuffer for the game.
	IDirect3DSurface9* backBuffer;
	hr = This->lpVtbl->GetBackBuffer(This, 0, 0, D3DBACKBUFFER_TYPE_MONO, &backBuffer);
	if (SUCCEEDED(hr) && gGameSurface > 0)
	{
		// And copy to our storage surface.
		hr = This->lpVtbl->StretchRect(This, backBuffer, NULL, gGameSurface, NULL, D3DTEXF_NONE);
		if (FAILED(hr))
			::OutputDebugString(L"Bad StretchRect.");
	}

	hr = pOrigPresent(This, pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion);
	return hr;
}

//-----------------------------------------------------------
// Interface to implement the hook for IDirect3D9->CreateDevice

// This declaration serves a dual purpose of defining the interface routine as required by
// DX9, and also is the storage for the original call, returned by nktInProc.Hook

SIZE_T hook_id_CreateDevice;
STDMETHOD_(HRESULT, pOrigCreateDevice)(IDirect3D9* This,
	/* [in] */          UINT                  Adapter,
	/* [in] */          D3DDEVTYPE            DeviceType,
	/* [in] */          HWND                  hFocusWindow,
	/* [in] */          DWORD                 BehaviorFlags,
	/* [in, out] */     D3DPRESENT_PARAMETERS *pPresentationParameters,
	/* [out, retval] */ IDirect3DDevice9      **ppReturnedDeviceInterface
	) = nullptr;


// The actual Hooked routine for CreateDevice, called whenever the game
// makes a IDirect3D9->CreateDevice call.
//
// However, because we always need to have a shared surface from the game
// backbuffer, this device must actually be created as a IDirect3DDevice9Ex.
// That allows us to create a shared surface, still on the GPU.  IDirect3DDevice9
// objects can only share through system memory, which is too slow.
// This should be OK, because the game should not know or care that it went Ex.

STDMETHODIMP_(HRESULT) Hooked_CreateDevice(IDirect3D9* This,
	UINT Adapter, D3DDEVTYPE DeviceType, HWND hFocusWindow, DWORD BehaviorFlags, D3DPRESENT_PARAMETERS* pPresentationParameters,
	IDirect3DDevice9** ppReturnedDeviceInterface)
{
	wchar_t info[512];

	::OutputDebugString(L"NativePlugin::Hooked_CreateDevice called\n");
	wsprintf(info, L"  Width: %d, Height: %d, Format: %d\n"
		, pPresentationParameters->BackBufferWidth, pPresentationParameters->BackBufferHeight, pPresentationParameters->BackBufferFormat);
	::OutputDebugString(info);

	HRESULT hr = pOrigCreateDevice(This, Adapter, DeviceType, hFocusWindow, BehaviorFlags, pPresentationParameters,
		ppReturnedDeviceInterface);
	//HRESULT hr = This->lpVtbl->CreateDeviceEx(This, Adapter, DeviceType, hFocusWindow, BehaviorFlags, pPresentationParameters, NULL,
	//	ppReturnedDeviceInterface);


	if (pOrigPresent == nullptr && SUCCEEDED(hr) && ppReturnedDeviceInterface != nullptr)
	{
		//DWORD dwOsErr;
		//IDirect3DDevice9Ex* game_Device = *ppReturnedDeviceInterface;

		//// Using the DX9 Device, we can now hook the Present call.
		//dwOsErr = nktInProc.Hook(&hook_id_Present, (void**)&pOrigPresent,
		//	game_Device->lpVtbl->Present, Hooked_Present, 0);

		//if (FAILED(dwOsErr))
		//	::OutputDebugStringA("Failed to hook IDirect3DDevice9::Present\n");

		//// Now that we have a proper Device from the game, let's also make a 
		//// DX9 Surface, so that we can snapshot the game output.  This surface needs to
		//// use the Shared parameter, so that we can share it to another Device.  Because
		//// these are all DX9Ex objects, the share will work.

		//game_Device->lpVtbl->CreateRenderTargetEx(game_Device, pPresentationParameters->BackBufferWidth, pPresentationParameters->BackBufferHeight,
		//	pPresentationParameters->BackBufferFormat, D3DMULTISAMPLE_NONE, 0, false, 
		//	&gGameSurface, &gGameSurfaceShare, 
		//	D3DUSAGE_NONSECURE | D3DUSAGE_RENDERTARGET);
	}

	return hr;
}


//-----------------------------------------------------------

// Here we want to start the daisy chain of hooking DX9 interfaces, to
// ultimately get access to IDirect3DDevice9::Present
//
// The sequence a game will use is:
//   IDirect3D9* Direct3DCreate9();
//   IDirect3D9::CreateDevice(return pIDirect3DDevice9);
//   pIDirect3DDevice9->Present
//
// This hook call is called from the Deviare side, to continue the 
// daisy-chain to IDirect3DDevice9::Present.
// 

void HookCreateDeviceEx(IDirect3D9Ex* pDX9Ex)
{
	// This can be called multiple times by a game, so let's be sure to
	// only hook once.
	if (pOrigCreateDevice == nullptr && pDX9Ex != nullptr)
	{
#ifdef _DEBUG 
		nktInProc.SetEnableDebugOutput(TRUE);
#endif
		// If we are here, we want to now hook the IDirect3D9::CreateDevice
		// routine, as that will be the next thing the game does, and we
		// need access to the Direct3DDevice9.
		// This can't be done directly, because this is a vtable based API
		// call, not an export from a DLL, so we need to directly hook the 
		// address of the CreateDevice function. This is also why we are 
		// using In-Proc here.  Since we are using the CINTERFACE, we can 
		// just directly access the address.

		//DWORD dwOsErr = nktInProc.Hook(&hook_id_CreateDevice, (void**)&pOrigCreateDevice,
		//	pDX9Ex->lpVtbl->CreateDevice, Hooked_CreateDevice, 0);

		//if (FAILED(dwOsErr))
		//	::OutputDebugStringA("Failed to hook IDirect3D9::CreateDevice\n");
	}
}


void HookCreateDevice(IDirect3D9* pDX9)
{
	// This can be called multiple times by a game, so let's be sure to
	// only hook once.
	if (pOrigCreateDevice == nullptr && pDX9 != nullptr)
	{
#ifdef _DEBUG 
		nktInProc.SetEnableDebugOutput(TRUE);
#endif
		// If we are here, we want to now hook the IDirect3D9::CreateDevice
		// routine, as that will be the next thing the game does, and we
		// need access to the Direct3DDevice9.
		// This can't be done directly, because this is a vtable based API
		// call, not an export from a DLL, so we need to directly hook the 
		// address of the CreateDevice function. This is also why we are 
		// using In-Proc here.  Since we are using the CINTERFACE, we can 
		// just directly access the address.

		DWORD dwOsErr = nktInProc.Hook(&hook_id_CreateDevice, (void**)&pOrigCreateDevice,
			pDX9->lpVtbl->CreateDevice, Hooked_CreateDevice, 0);

		if (FAILED(dwOsErr))
			::OutputDebugStringA("Failed to hook IDirect3D9::CreateDevice\n");
	}
}
