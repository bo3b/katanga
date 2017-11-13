// For this part of the DLL, it makes more sense to use Nektra In-Proc
// instead of Nektra Deviare.  The interface for DX9 is not well supported
// by the DB for Deviare, so getting access to the DX9 APIs is simpler
// using In-Proc.  It would be possible to add the DX9 interfaces to 
// the DB files, and rebuild the DB if you wanted to use Deviare.
//
// All In-Proc use is in this file, all Deviare use is in NativePlugin.

// We moved the DX9 to DX9Ex, because only that can share surfaces.  This
// has knock-on effects that require us to hook other calls and change their
// inputs.  All creation calls like CreateTexture must use D3DUSAGE_DYNAMIC,
// because D3DUSAGE_MANAGED is not supported on DX9Ex.  That then leads to
// problems with textures when locked, requiring that they be created with
// D3DUSAGE_DYNAMIC.  But that can't be done for rendertargets or stencils.
// Bit of a mess.

// Here are all the creation calls that very likely need to be hooked to
// force them D3DUSAGE_DYNAMIC.
//  Resource creation APIs include - 
//	CreateTexture, CreateVolumeTexture, CreateCubeTexture, CreateRenderTarget, 
//	CreateVertexBuffer, CreateIndexBuffer, CreateDepthStencilSurface, 
//	CreateOffscreenPlainSurface, CreateDepthStencilSurfaceEx, 
//  CreateOffscreenPlainSurfaceEx, and CreateRenderTargetEx.
//
// Good table of usages:
// https://msdn.microsoft.com/en-us/library/windows/desktop/bb172625(v=vs.85).aspx


//-----------------------------------------------------------

#include "DeviarePlugin.h"

#include "NktHookLib.h"

#include "nvapi\nvapi.h"


//-----------------------------------------------------------

// This is automatically instantiated by C++, so the hooking library
// is immediately available.
CNktHookLib nktInProc;

// The surface that we copy the current game frame into. It is shared.
IDirect3DSurface9* gGameSurface = nullptr;
HANDLE gGameSharedHandle = nullptr;

// The nvapi stereo handle, to access the reverse blit.
StereoHandle gNVAPI = nullptr;


// --------------------------------------------------------------------------------------------------

// Custom routines for this DeviarePlugin.dll, that the master app can call,
// using Deviare access routines.


// Return the current value of the gGameSurfaceShare.  This is the HANDLE
// that is necessary to share from DX9Ex here to DX11 in the VR app.

HANDLE WINAPI GetSharedHandle(int* in)
{
	::OutputDebugString(L"GetSharedHandle::\n");

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
		D3DSURFACE_DESC bufferDesc, copyDesc;
		backBuffer->GetDesc(&bufferDesc);
		gGameSurface->GetDesc(&copyDesc);
		RECT srcRect = { 0, 0, bufferDesc.Width, bufferDesc.Height };
		RECT dstRect = { 0, 0, copyDesc.Width, copyDesc.Height };

		hr = NvAPI_Stereo_ReverseStereoBlitControl(gNVAPI, true);

		// Copy current frame from backbuffer to our shared storage surface.
		hr = This->StretchRect(backBuffer, &srcRect, gGameSurface, &dstRect, D3DTEXF_NONE);
		if (FAILED(hr))
			::OutputDebugString(L"Bad StretchRect.\n");

		hr = NvAPI_Stereo_ReverseStereoBlitControl(gNVAPI, false);

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
// Direct3D9: (ERROR) : Lock is not supported for textures allocated with POOL_DEFAULT unless they are marked D3DUSAGE_DYNAMIC.
// Then:
// Direct3D9: (ERROR) :Dynamic textures cannot be rendertargets or depth/stencils.

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
	wchar_t info[512];
	swprintf_s(info, _countof(info),
		L"NativePlugin::Hooked_CreateTexture - Levels: %d, Usage: %x, Format: %d, Pool: %d\n",
		Levels, Usage, Format, Pool);
	::OutputDebugString(info);

	// Force Managed_Pool to always be default, the only possibility on DX9Ex.
	// D3DPOOL_SYSTEMMEM is still valid however, and should not be tweaked.
	if (Pool == D3DPOOL_MANAGED)
		Pool = D3DPOOL_DEFAULT;

	// Any texture not used as RenderTarget or as a DepthStencil needs to be
	// made dynamic, otherwise we get a POOL_DEFAULT error.
	int renderOrStencil = Usage & (D3DUSAGE_RENDERTARGET | D3DUSAGE_DEPTHSTENCIL);
	if (!renderOrStencil)
		Usage |= D3DUSAGE_DYNAMIC;

	HRESULT hr = pOrigCreateTexture(This, Width, Height, Levels, Usage, Format, Pool,
		ppTexture, pSharedHandle);
	if (FAILED(hr))
		::OutputDebugStringA("Failed to call pOrigCreateTexture\n");

	return hr;
}

//-----------------------------------------------------------
// Interface to implement the hook for IDirect3DDevice9->CreateCubeTexture

// This declaration serves a dual purpose of defining the interface routine as required by
// DX9, and also is the storage for the original call, returned by nktInProc.Hook

HRESULT(__stdcall *pOrigCreateCubeTexture)(IDirect3DDevice9* This,
	/* [in] */          UINT                  EdgeLength,
	/* [in] */          UINT                  Levels,
	/* [in] */          DWORD                 Usage,
	/* [in] */          D3DFORMAT             Format,
	/* [in] */          D3DPOOL               Pool,
	/* [out, retval] */ IDirect3DCubeTexture9 **ppCubeTexture,
	/* [in] */          HANDLE                *pSharedHandle
	) = nullptr;


// We need to implement a hook on CreateCubeTexture, because the game
// will create this after we return the IDirect3DDevice9Ex Device,
// and the debug layer crashes with:
// Direct3D9: (ERROR) : D3DPOOL_MANAGED is not valid with IDirect3DDevice9Ex
// Then:
// Direct3D9: (ERROR) :Lock is not supported for textures allocated with POOL_DEFAULT unless they are marked D3DUSAGE_DYNAMIC.

HRESULT __stdcall Hooked_CreateCubeTexture(IDirect3DDevice9* This,
	/* [in] */          UINT                  EdgeLength,
	/* [in] */          UINT                  Levels,
	/* [in] */          DWORD                 Usage,
	/* [in] */          D3DFORMAT             Format,
	/* [in] */          D3DPOOL               Pool,
	/* [out, retval] */ IDirect3DCubeTexture9 **ppCubeTexture,
	/* [in] */          HANDLE                *pSharedHandle)
{
	wchar_t info[512];
	swprintf_s(info, _countof(info),
		L"NativePlugin::Hooked_CreateTexture - Levels: %d, Usage: %x, Format: %d, Pool: %d\n",
		Levels, Usage, Format, Pool);
	::OutputDebugString(info);

	// Force Managed_Pool to always be default, the only possibility on DX9Ex.
	// D3DPOOL_SYSTEMMEM is still valid however, and should not be tweaked.
	if (Pool == D3DPOOL_MANAGED)
		Pool = D3DPOOL_DEFAULT;

	// Any texture not used as RenderTarget or as a DepthStencil needs to be
	// made dynamic, otherwise we get a POOL_DEFAULT error.
	int renderOrStencil = Usage & (D3DUSAGE_RENDERTARGET | D3DUSAGE_DEPTHSTENCIL);
	if (!renderOrStencil)
		Usage |= D3DUSAGE_DYNAMIC;

	HRESULT hr = pOrigCreateCubeTexture(This, EdgeLength, Levels, Usage, Format, Pool,
		ppCubeTexture, pSharedHandle);
	if (FAILED(hr))
		::OutputDebugStringA("Failed to call pOrigCreateCubeTexture\n");

	return hr;
}

//-----------------------------------------------------------
// Interface to implement the hook for IDirect3DDevice9->CreateVertexBuffer

// This declaration serves a dual purpose of defining the interface routine as required by
// DX9, and also is the storage for the original call, returned by nktInProc.Hook

HRESULT(__stdcall *pOrigCreateVertexBuffer)(IDirect3DDevice9* This,
	/* [in] */          UINT                   Length,
	/* [in] */          DWORD                  Usage,
	/* [in] */          DWORD                  FVF,
	/* [in] */          D3DPOOL                Pool,
	/* [out, retval] */ IDirect3DVertexBuffer9 **ppVertexBuffer,
	/* [in] */          HANDLE                 *pSharedHandle
	) = nullptr;


// We need to implement a hook on CreateVertexBuffer, because the game
// will create this after we return the IDirect3DDevice9Ex Device,
// and the debug layer crashes with:
// Direct3D9: (ERROR) : D3DPOOL_MANAGED is not valid with IDirect3DDevice9Ex
// Then:
// Direct3D9: (WARN) : Vertexbuffer created with POOL_DEFAULT but WRITEONLY not set.Performance penalty could be severe.
// I don't think it can be set to writeonly, but setting it to dynamic like the
// the other texture buffers should work, based on:
// https://msdn.microsoft.com/en-us/library/windows/desktop/bb147263(v=vs.85).aspx#Using_Dynamic_Vertex_and_Index_Buffers

HRESULT __stdcall Hooked_CreateVertexBuffer(IDirect3DDevice9* This,
	/* [in] */          UINT                   Length,
	/* [in] */          DWORD                  Usage,
	/* [in] */          DWORD                  FVF,
	/* [in] */          D3DPOOL                Pool,
	/* [out, retval] */ IDirect3DVertexBuffer9 **ppVertexBuffer,
	/* [in] */          HANDLE                 *pSharedHandle)
{
	wchar_t info[512];
	swprintf_s(info, _countof(info),
		L"NativePlugin::Hooked_CreateVertexBuffer -  Usage: %x, Pool: %d\n",
		Usage, Pool);
	::OutputDebugString(info);

	// Force Managed_Pool to always be default, the only possibility on DX9Ex.
	// D3DPOOL_SYSTEMMEM is still valid however, and should not be tweaked.
	if (Pool == D3DPOOL_MANAGED)
		Pool = D3DPOOL_DEFAULT;

	// Never used as Stencil or Target, so just make it dynamic because it's default pool.
	Usage |= D3DUSAGE_DYNAMIC;

	HRESULT hr = pOrigCreateVertexBuffer(This, Length, Usage, FVF, Pool,
		ppVertexBuffer, pSharedHandle);
	if (FAILED(hr))
		::OutputDebugStringA("Failed to call pOrigCreateVertexBuffer\n");

	return hr;
}

//-----------------------------------------------------------
// Interface to implement the hook for IDirect3DDevice9->CreateIndexBuffer

// This declaration serves a dual purpose of defining the interface routine as required by
// DX9, and also is the storage for the original call, returned by nktInProc.Hook

HRESULT(__stdcall *pOrigCreateIndexBuffer)(IDirect3DDevice9* This,
	/* [in] */          UINT                  Length,
	/* [in] */          DWORD                 Usage,
	/* [in] */          D3DFORMAT             Format,
	/* [in] */          D3DPOOL               Pool,
	/* [out, retval] */ IDirect3DIndexBuffer9 **ppIndexBuffer,
	/* [in] */          HANDLE                *pSharedHandle
	) = nullptr;


// We need to implement a hook on CreateIndexBuffer, because the game
// will create this after we return the IDirect3DDevice9Ex Device,
// and the debug layer crashes with:
// Direct3D9: (ERROR) : D3DPOOL_MANAGED is not valid with IDirect3DDevice9Ex

HRESULT __stdcall Hooked_CreateIndexBuffer(IDirect3DDevice9* This,
	/* [in] */          UINT                  Length,
	/* [in] */          DWORD                 Usage,
	/* [in] */          D3DFORMAT             Format,
	/* [in] */          D3DPOOL               Pool,
	/* [out, retval] */ IDirect3DIndexBuffer9 **ppIndexBuffer,
	/* [in] */          HANDLE                *pSharedHandle)
{
	wchar_t info[512];
	swprintf_s(info, _countof(info),
		L"NativePlugin::Hooked_CreateIndexBuffer -  Usage: %x, Format: %d, Pool: %d\n",
		Usage, Format, Pool);
	::OutputDebugString(info);

	// Force Managed_Pool to always be default, the only possibility on DX9Ex.
	// D3DPOOL_SYSTEMMEM is still valid however, and should not be tweaked.
	if (Pool == D3DPOOL_MANAGED)
		Pool = D3DPOOL_DEFAULT;

	// Never used as Stencil or Target, so just make it dynamic because it's default pool.
	Usage |= D3DUSAGE_DYNAMIC;

	HRESULT hr = pOrigCreateIndexBuffer(This, Length, Usage, Format, Pool,
		ppIndexBuffer, pSharedHandle);
	if (FAILED(hr))
		::OutputDebugStringA("Failed to call pOrigCreateIndexBuffer\n");

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
		swprintf_s(info, _countof(info), L"  Width: %d, Height: %d, Format: %d\n"
			, pPresentationParameters->BackBufferWidth, pPresentationParameters->BackBufferHeight, pPresentationParameters->BackBufferFormat);
		::OutputDebugString(info);
	}

	// Once we make it here, we can be certain that the This factory is 
	// actually an IDirect3D9Ex, but passed back to the game as IDirect3D9.
	// We can upcast the factory so that we can make a IDirect3DDevice9Ex.

	IDirect3D9Ex* pDX9Ex;
	HRESULT hr = This->QueryInterface(IID_PPV_ARGS(&pDX9Ex));
	if (FAILED(hr))
		throw std::exception("Failed to upcast to IDirect3D9Ex factory");

	// This is called out in the debug layer as a potential performance problem, but the
	// docs suggest adding this will slow things down.  It is unlikely to be actually
	// necessary, because this is in the running game, and the other threads are actually
	// in a different process altogether.  
	// Direct3D9: (WARN) : Device that was created without D3DCREATE_MULTITHREADED is being used by a thread other than the creation thread.
	// Also- this warning happens in TheBall, when run with only the debug layer. Not our fault.
	//BehaviorFlags |= D3DCREATE_MULTITHREADED;	// ToDo: not certain this is needed, said to slow things down.

	IDirect3DDevice9Ex* pDevice9Ex = nullptr;
	hr = pDX9Ex->CreateDeviceEx(Adapter, DeviceType, hFocusWindow, BehaviorFlags, pPresentationParameters, NULL,
		&pDevice9Ex);
	if (FAILED(hr))
		throw std::exception("Failed to create IDirect3DDevice9Ex");

	if (ppReturnedDeviceInterface)
		*ppReturnedDeviceInterface = static_cast<IDirect3DDevice9*>(pDevice9Ex);

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

		dwOsErr = nktInProc.Hook(&hook_id, (void**)&pOrigCreateCubeTexture,
			lpvtbl_CreateCubeTexture(pDevice9Ex), Hooked_CreateCubeTexture, 0);
		if (FAILED(dwOsErr))
			::OutputDebugStringA("Failed to hook IDirect3DDevice9::CreateCubeTexture\n");

		dwOsErr = nktInProc.Hook(&hook_id, (void**)&pOrigCreateVertexBuffer,
			lpvtbl_CreateVertexBuffer(pDevice9Ex), Hooked_CreateVertexBuffer, 0);
		if (FAILED(dwOsErr))
			::OutputDebugStringA("Failed to hook IDirect3DDevice9::CreateVertexBuffer\n");

		dwOsErr = nktInProc.Hook(&hook_id, (void**)&pOrigCreateIndexBuffer,
			lpvtbl_CreateIndexBuffer(pDevice9Ex), Hooked_CreateIndexBuffer, 0);
		if (FAILED(dwOsErr))
			::OutputDebugStringA("Failed to hook IDirect3DDevice9::CreateIndexBuffer\n");


		// Now that we have a proper Ex Device from the game, let's also make a 
		// DX9 Surface, so that we can snapshot the game output.  This surface needs to
		// use the Shared parameter, so that we can share it to another Device.  Because
		// these are all DX9Ex objects, the share will work.

		UINT width = pPresentationParameters->BackBufferWidth * 2;
		UINT height = pPresentationParameters->BackBufferHeight;
		D3DFORMAT format = pPresentationParameters->BackBufferFormat;
		HRESULT res = pDevice9Ex->CreateRenderTarget(width, height, format, D3DMULTISAMPLE_NONE, 0, false,
			&gGameSurface, &gGameSharedHandle);
		if (FAILED(res))
			throw std::exception("Fail to create shared RenderTarget");

		res = NvAPI_Initialize();
		if (FAILED(res))
			throw std::exception("Failed to NvAPI_Initialize\n");

		res = NvAPI_Stereo_CreateHandleFromIUnknown(pDevice9Ex, &gNVAPI);
		if (FAILED(res))
			throw std::exception("Failed to NvAPI_Stereo_CreateHandleFromIUnknown\n");
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

void HookCreateDevice(IDirect3D9Ex* pDX9Ex)
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
			throw std::exception("Failed to hook IDirect3D9::CreateDevice");
	}
}


