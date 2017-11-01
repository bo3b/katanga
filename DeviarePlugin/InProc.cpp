// For this part of the DLL, it makes more sense to use Nektra In-Proc
// instead of Nektra Deviare.  The interface for DX9 is not well supported
// by the DB for Deviare, so getting access to the DX9 APIs is simpler
// using In-Proc.  It would be possible to add the DX9 interfaces to 
// the DB files, and rebuild the DB if you wanted to use Deviare.
//
// All In-Proc use is in this file, all Deviare use is in NativePlugin.

//-----------------------------------------------------------

#include "DeviarePlugin.h"

#include "NktHookLib.h"


//-----------------------------------------------------------

// This is automatically instantiated by C++, so the hooking library
// is immediately available.
CNktHookLib nktInProc;

// The surface that we copy the current game frame into. It is shared.
IDirect3DSurface9* gGameSurface = nullptr;
HANDLE gGameSharedHandle = nullptr;

// --------------------------------------------------------------------------------------------------

// Custom routines for this DeviarePlugin.dll, that the master app can call,
// using Deviare access routines.


// Return the current value of the gGameSurfaceShare.  This is the HANDLE
// that is necessary to share from DX9Ex here to DX11 in the VR app.

HANDLE WINAPI GetSharedHandle(int* in)
{
	::OutputDebugString(L"GetSharedHandle::");

	return gGameSharedHandle;
}



//-----------------------------------------------------------
// Interface to implement the hook for IDirect3DDevice9->Present

// This declaration serves a dual purpose of defining the interface routine as required by
// DX9, and also is the storage for the original call, returned by nktInProc.Hook

HRESULT (__stdcall *pOrigPresent)(IDirect3DDevice9Ex* This,
	/* [in] */ const RECT    *pSourceRect,
	/* [in] */ const RECT    *pDestRect,
	/* [in] */       HWND    hDestWindowOverride,
	/* [in] */ const RGNDATA *pDirtyRegion
	) = nullptr;


// This is it. The one we are after.  This is the hook for the DX9 Present call
// which the game will call for every frame.  

HRESULT __stdcall Hooked_Present(IDirect3DDevice9Ex* This,
	/* [in] */ const RECT    *pSourceRect,
	/* [in] */ const RECT    *pDestRect,
	/* [in] */       HWND    hDestWindowOverride,
	/* [in] */ const RGNDATA *pDirtyRegion)
{
	HRESULT hr;
	IDirect3DSurface9* backBuffer;

	hr = This->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backBuffer);
	if (SUCCEEDED(hr) && gGameSurface > 0)
	{
		// Copy current frame from backbuffer to our shared storage surface.
		hr = This->StretchRect(backBuffer, NULL, gGameSurface, NULL, D3DTEXF_NONE);
		if (FAILED(hr))
			::OutputDebugString(L"Bad StretchRect.");
	}

	hr = pOrigPresent(This, pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion);
	return hr;
}

//-----------------------------------------------------------
// Interface to implement the hook for IDirect3DDevice9->CreateTexture

// This declaration serves a dual purpose of defining the interface routine as required by
// DX9, and also is the storage for the original call, returned by nktInProc.Hook

HRESULT(__stdcall *pOrigCreateTexture)(IDirect3DDevice9* This,
	/* [in] */          UINT              Width,
	/* [in] */          UINT              Height,
	/* [in] */          UINT              Levels,
	/* [in] */          DWORD             Usage,
	/* [in] */          D3DFORMAT         Format,
	/* [in] */          D3DPOOL           Pool,
	/* [out, retval] */ IDirect3DTexture9 **ppTexture,
	/* [in] */          HANDLE            *pSharedHandle
	) = nullptr;


// We need to implement a hook on CreateTexture, because the game
// will create this after we return the IDirect3DDevice9Ex Device,
// and the debug layer crashes with:
// Direct3D9: (ERROR) : D3DPOOL_MANAGED is not valid with IDirect3DDevice9Ex
// Then:
// Direct3D9: (ERROR) :Lock is not supported for textures allocated with POOL_DEFAULT unless they are marked D3DUSAGE_DYNAMIC.

HRESULT __stdcall Hooked_CreateTexture(IDirect3DDevice9* This,
	/* [in] */          UINT              Width,
	/* [in] */          UINT              Height,
	/* [in] */          UINT              Levels,
	/* [in] */          DWORD             Usage,
	/* [in] */          D3DFORMAT         Format,
	/* [in] */          D3DPOOL           Pool,
	/* [out, retval] */ IDirect3DTexture9 **ppTexture,
	/* [in] */          HANDLE            *pSharedHandle)
{
	::OutputDebugString(L"NativePlugin::Hooked_CreateTexture called\n");

	// Force Pool to always be default.
	//Pool = D3DPOOL_DEFAULT;

	HRESULT hr = pOrigCreateTexture(This, Width, Height, Levels, Usage, Format, Pool,
		ppTexture, pSharedHandle);
	if (FAILED(hr))
		::OutputDebugStringA("Failed to call pOrigCreateTexture\n");

	return hr;
}

//-----------------------------------------------------------
// Interface to implement the hook for IDirect3D9->CreateDevice

// This declaration serves a dual purpose of defining the interface routine as required by
// DX9, and also is the storage for the original call, returned by nktInProc.Hook

HRESULT(__stdcall *pOrigCreateDevice)(IDirect3D9* This,
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

HRESULT __stdcall Hooked_CreateDevice(IDirect3D9* This,
	/* [in] */          UINT                  Adapter,
	/* [in] */          D3DDEVTYPE            DeviceType,
	/* [in] */          HWND                  hFocusWindow,
	/* [in] */          DWORD                 BehaviorFlags,
	/* [in, out] */     D3DPRESENT_PARAMETERS *pPresentationParameters,
	/* [out, retval] */ IDirect3DDevice9      **ppReturnedDeviceInterface)
{
	wchar_t info[512];

	::OutputDebugString(L"NativePlugin::Hooked_CreateDevice called\n");
	if (pPresentationParameters)
	{
		wsprintf(info, L"  Width: %d, Height: %d, Format: %d\n"
			, pPresentationParameters->BackBufferWidth, pPresentationParameters->BackBufferHeight, pPresentationParameters->BackBufferFormat);
		::OutputDebugString(info);
	}

	// Once we make it here, we can be certain that the This factory is 
	// actually an IDirect3D9Ex, but passed back to the game as IDirect3D9.
	// Create factory and Device here. Return just device.

	IDirect3D9Ex* pDX9Ex;
	HRESULT hr = Direct3DCreate9Ex(D3D_SDK_VERSION, &pDX9Ex);
	if (FAILED(hr))
		::OutputDebugStringA("Failed to create dx9ex factory\n");
	IDirect3DDevice9Ex* pDevice9Ex = nullptr;
	hr = pDX9Ex->CreateDeviceEx(Adapter, DeviceType, hFocusWindow, BehaviorFlags, pPresentationParameters, NULL,
		&pDevice9Ex);
	if (FAILED(hr))
		::OutputDebugStringA("Failed to create IDirect3DDevice9Ex\n");

//	HRESULT hr = pOrigCreateDevice(This, Adapter,DeviceType,hFocusWindow,BehaviorFlags,pPresentationParameters,ppReturnedDeviceInterface);

	if (ppReturnedDeviceInterface)
		*ppReturnedDeviceInterface = (IDirect3DDevice9*)pDevice9Ex;

	// Using that fresh DX9 Device, we can now hook the Present and CreateTexture calls.

	if (pOrigPresent == nullptr && SUCCEEDED(hr) && ppReturnedDeviceInterface != nullptr)
	{
		SIZE_T hook_id;
		DWORD dwOsErr;
		
		dwOsErr = nktInProc.Hook(&hook_id, (void**)&pOrigPresent,
			lpvtbl_Present(pDevice9Ex), Hooked_Present, 0);
		if (FAILED(dwOsErr))
			::OutputDebugStringA("Failed to hook IDirect3DDevice9::Present\n");

		dwOsErr = nktInProc.Hook(&hook_id, (void**)&pOrigCreateTexture,
			lpvtbl_CreateTexture(pDevice9Ex), Hooked_CreateTexture, 0);
		if (FAILED(dwOsErr))
			::OutputDebugStringA("Failed to hook IDirect3DDevice9::CreateTexture\n");


		// Now that we have a proper Ex Device from the game, let's also make a 
		// DX9 Surface, so that we can snapshot the game output.  This surface needs to
		// use the Shared parameter, so that we can share it to another Device.  Because
		// these are all DX9Ex objects, the share will work.

		HRESULT hr = pDevice9Ex->CreateRenderTarget(pPresentationParameters->BackBufferWidth, pPresentationParameters->BackBufferHeight,
			pPresentationParameters->BackBufferFormat, D3DMULTISAMPLE_NONE, 0, false,
			&gGameSurface, &gGameSharedHandle);
		if (FAILED(hr))
			::OutputDebugStringA("Fail to create shared RenderTarget\n");
	}

	// We are returning the IDirect3DDevice9Ex object, because the Device the game
	// is going to use needs to be Ex type, so we can share from it's backbuffer.

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
// It's also worth noting that we convert the key objects into Ex
// versions, because we need to share our copy of the game backbuffer.
// We need to hook the CreateDevice, not CreateDeviceEx, because the
// game is nearly certain to be calling CreateDevice.
// 

void HookCreateDevice(IDirect3D9* pDX9Ex)
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

		SIZE_T hook_id;
		DWORD dwOsErr = nktInProc.Hook(&hook_id, (void**)&pOrigCreateDevice,
			lpvtbl_CreateDevice(pDX9Ex), Hooked_CreateDevice, 0);

		if (FAILED(dwOsErr))
			::OutputDebugStringA("Failed to hook IDirect3D9::CreateDevice\n");
	}
}


