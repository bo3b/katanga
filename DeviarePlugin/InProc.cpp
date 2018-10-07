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

// Worth noting that the destination of the backbuffer copy cannot be a plain
// surface, which directly contradicts the examples given in GDC presentations.
// If the destination is a plain surface as they show, then the DX9 debug layer
// will break with:
// Direct3D9: (ERROR) :If the destination surface is an offscreen plain surface, the source must also be an offscreen plain.  StretchRect failed
//
// Obviously that is less than ideal.  NVidia documentation sucks, and no samples.
//
// A note in the documentation: https://msdn.microsoft.com/en-us/library/windows/desktop/bb172586(v=vs.85).aspx
// "A cross-process shared-surface should not be locked."
//
// It can also not be a RenderTarget, although that is what the nvapi header file
// suggests.  CreateRenderTarget does not work, presumably because it does not
// make a stereo texture.
//
// After much experimentation, including writing a sample program that could be
// debugged directly using Pix, I hit upon using CreateTexture with the RenderTarget
// flag set.  This succeeds in the sample app, to StretchRect a 2x Surface, with
// both eyes.  The Texture can be converted to Surface using GetSurfaceLevel.
// https://github.com/bo3b/ReverseBlit-DX9
//  
// With that use of CreateTexture, the next step was to find that shared surfaces
// cannot be used as the target, necessitating a second copy from the game.  This is
// a second destination of the game bits, which is the final shared resource.  This
// is OK, because we need to swap eyes for Unity, and this is where it's done.

// Overall, seriously hard to get this all working.  Multiple problems with bad
// and misleading and missing documentation. And no sample code.  This complicated
// runtime environment makes it hard to debug as well.  Careful with that axe, Eugene,
// this is all pretty fragile.

//-----------------------------------------------------------

#include <thread>

#include "DeviarePlugin.h"

#include "NktHookLib.h"

#include "nvapi\nvapi.h"


//-----------------------------------------------------------

// This is automatically instantiated by C++, so the hooking library
// is immediately available.
CNktHookLib nktInProc;

// The surface that we copy the current game frame into. It is shared.
// It starts as a Texture so that it is created stereo, but is converted
// to a Surface for use in StretchRect.

IDirect3DTexture9* gGameTexture = nullptr;
IDirect3DSurface9* gGameSurface = nullptr;	// created as a reference to Texture

HANDLE gSharedThread = nullptr;				// will copy from GameSurface to SharedSurface

HANDLE gFreshBits = nullptr;				// Synchronization Event object

IDirect3DSurface9* gSharedTarget = nullptr;	// Actual shared RenderTarget
HANDLE gGameSharedHandle = nullptr;			// Handle to share with DX11

// The nvapi stereo handle, to access the reverse blit.
StereoHandle gNVAPI = nullptr;

//HANDLE gameThread = nullptr;

wchar_t info[512];
static unsigned long triggerCount = 0;


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

// Shared Event object that is the notification that the VR side
// has called Present.

HANDLE WINAPI GetEventHandle(int* in)
{
	::OutputDebugString(L"GetSharedEvent::\n");

	return gFreshBits;
}

// Called from C# side after VR app has presented its frame.
// This allows our locked present for the target game to continue.

static LARGE_INTEGER startTriggeredTicks, resetTriggerTicks;
static LARGE_INTEGER frequency;

HANDLE WINAPI TriggerEvent(int* in)
{
	//	::OutputDebugString(L"TriggerEvent::\n");


	if ((int)in == 1)		// Active triggered
	{
		BOOL set = SetEvent(gFreshBits);
		if (!set)
			::OutputDebugString(L"Bad SetEvent in TriggerEvent.\n");

		// Waste time spinning, while we wait for high resolution timer.
		// This timer using QueryPerformanceCounter should be accurate
		// to some 100ns or so, for any system we care about.

		QueryPerformanceFrequency(&frequency);
		QueryPerformanceCounter(&startTriggeredTicks);

		triggerCount += 1;

		if ((triggerCount % 30) == 0)
		{
			LONGLONG startMS = startTriggeredTicks.QuadPart * 1000 / frequency.QuadPart;
			swprintf_s(info, _countof(info),
				L"SetEvent - ms: %d, frequency: %d, triggerCount: %d\n", startMS, frequency.QuadPart, triggerCount);
			::OutputDebugString(info);
		}
	}
	else					// Reset untriggered
	{
		BOOL reset = ResetEvent(gFreshBits);
		if (!reset)
			::OutputDebugString(L"Bad ResetEvent in TriggerEvent.\n");

		QueryPerformanceCounter(&resetTriggerTicks);

		if ((triggerCount % 30) == 0)
		{
			LONGLONG frameTicks = resetTriggerTicks.QuadPart - startTriggeredTicks.QuadPart;
			LONGLONG endMS = frameTicks * 1000 / frequency.QuadPart;
			swprintf_s(info, _countof(info),
				L"ResetEvent - ms: %d\n", endMS);
			::OutputDebugString(info);
		}
	}

	return NULL;
}


//-----------------------------------------------------------
// StretchRect the stereo snapshot back onto the backbuffer so we can see what we got.
// Keep aspect ratio intact, because we want to see if it's a stretched image.

void DrawStereoOnGame(IDirect3DDevice9* device, IDirect3DSurface9* surface, IDirect3DSurface9* back)
{
	D3DSURFACE_DESC bufferDesc;
	back->GetDesc(&bufferDesc);
	RECT backBufferRect = { 0, 0, bufferDesc.Width, bufferDesc.Height };
	RECT stereoImageRect = { 0, 0, bufferDesc.Width * 2, bufferDesc.Height };

	int insetW = 300;
	int insetH = (int)(300.0 * stereoImageRect.bottom / stereoImageRect.right);
	RECT topScreen = { 5, 5, insetW, insetH };
	device->StretchRect(surface, &stereoImageRect, back, &topScreen, D3DTEXF_NONE);
}


//-----------------------------------------------------------
// Interface to implement the hook for IDirect3DDevice9->Present

// This declaration serves a dual purpose of defining the interface routine as required by
// DX9, and also is the storage for the original call, returned by nktInProc.Hook

HRESULT (__stdcall *pOrigPresent)(IDirect3DDevice9* This,
	/* [in] */ const RECT    *pSourceRect,
	/* [in] */ const RECT    *pDestRect,
	/* [in] */       HWND    hDestWindowOverride,
	/* [in] */ const RGNDATA *pDirtyRegion
	) = nullptr;


// This is it. The one we are after.  This is the hook for the DX9 Present call
// which the game will call for every frame.  At each call, we will make a copy
// of whatever the game drew, and that will be passed along via the shared surface
// to the VR layer.
//
// The StretchRect to the gGameSurface will be duplicated in the gGameTexture. So
// even though we are sharing the original Texture and not the target gGameSurface
// here, we still expect it to be stereo and match the gGameSurface.

HRESULT __stdcall Hooked_Present(IDirect3DDevice9* This,
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
		hr = NvAPI_Stereo_ReverseStereoBlitControl(gNVAPI, true);
		{
			hr = This->StretchRect(backBuffer, nullptr, gGameSurface, nullptr, D3DTEXF_NONE);
			if (FAILED(hr))
				::OutputDebugString(L"Bad StretchRect to Texture.\n");
			hr = This->StretchRect(gGameSurface, nullptr, gSharedTarget, nullptr, D3DTEXF_NONE);

//			SetEvent(gFreshBits);		// Signal other thread to start StretchRect
		}
		hr = NvAPI_Stereo_ReverseStereoBlitControl(gNVAPI, false);

#ifdef _DEBUG
		DrawStereoOnGame(This, gSharedTarget, backBuffer);
#endif
	}
	backBuffer->Release();

	HRESULT hrp = pOrigPresent(This, pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion);

	// Sync starting next frame with VR app.
	//DWORD object;
	//do
	//{
	//	object = WaitForSingleObject(gFreshBits, 0);
	//} while (object != WAIT_OBJECT_0);

	return hrp;
}


// This thread routine will be Resumed whenever the Present call has fresh data.
// It will be Suspended mostly.  This is a separate thread, even with the complexity
// that adds, because we otherwise have two back to back StretchRect on the main
// game thread.  The first one is inescapable, we must copy the stereo bits from the 
// backbuffer into the gGameSurface.  The second is to copy to the gSharedTarget
// RenderTarget, and this second copy would stall waiting for the first to finish.
// The GPU driver enforces the stall.  Rather than stall there, which would affect
// the game, it's better to stall here in a separate thread where it causes no harm.
//
// WaitForSingleObject returns 0=WAIT_OBJECT_0 when signaled, anything else can be
// considered and error and will exit the thread.  This includes timeouts, because
// we should never have a timeout. The thread is started suspended, and so we wait
// until the first Present call to act, which avoids the largest startup delay.
// After that, anything taking 5 seconds has to be a fatal error, we are expecting
// this to fire every frame.

//DWORD __stdcall CopyGameToShared(LPVOID lpDevice)
//{
//	IDirect3DDevice9* device = static_cast<IDirect3DDevice9*>(lpDevice);
//	HRESULT hr;
//	DWORD object;
//	BOOL reset;
//
//	while (true)
//	{
//		object = WaitForSingleObject(gFreshBits, 60 * 1000);
//		if (object != WAIT_OBJECT_0)
//			break;
//
//		hr = device->StretchRect(gGameSurface, nullptr, gSharedTarget, nullptr, D3DTEXF_NONE);
//		// Not supposed to use CLR.
//		//if (FAILED(hr))
//		//	::OutputDebugString(L"Bad StretchRect to RenderTarget in CopyGameToShared thread.\n");
//
//		reset = ResetEvent(gFreshBits);
//		if (!reset)
//			break;
//	}
//
//	return E_FAIL;
//}


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
#ifdef _DEBUG
	wchar_t info[512];
	swprintf_s(info, _countof(info),
		L"NativePlugin::Hooked_CreateTexture - Levels: %d, Usage: %x, Format: %d, Pool: %d\n",
		Levels, Usage, Format, Pool);
	::OutputDebugString(info);
#endif

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
#ifdef _DEBUG
	wchar_t info[512];
	swprintf_s(info, _countof(info),
		L"NativePlugin::Hooked_CreateTexture - Levels: %d, Usage: %x, Format: %d, Pool: %d\n",
		Levels, Usage, Format, Pool);
	::OutputDebugString(info);
#endif

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
#ifdef _DEBUG
	wchar_t info[512];
	swprintf_s(info, _countof(info),
		L"NativePlugin::Hooked_CreateVertexBuffer -  Usage: %x, Pool: %d\n",
		Usage, Pool);
	::OutputDebugString(info);
#endif

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
#ifdef _DEBUG
	wchar_t info[512];
	swprintf_s(info, _countof(info),
		L"NativePlugin::Hooked_CreateIndexBuffer -  Usage: %x, Format: %d, Pool: %d\n",
		Usage, Format, Pool);
	::OutputDebugString(info);
#endif

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
	HRESULT hr;

	::OutputDebugString(L"NativePlugin::Hooked_CreateDevice called\n");
	if (pPresentationParameters)
	{
		swprintf_s(info, _countof(info), L"  Width: %d, Height: %d, Format: %d\n"
			, pPresentationParameters->BackBufferWidth, pPresentationParameters->BackBufferHeight, pPresentationParameters->BackBufferFormat);
		::OutputDebugString(info);
	}

	// This is called out in the debug layer as a potential performance problem, but the
	// docs suggest adding this will slow things down.  It is unlikely to be actually
	// necessary, because this is in the running game, and the other threads are actually
	// in a different process altogether.  
	// Direct3D9: (WARN) : Device that was created without D3DCREATE_MULTITHREADED is being used by a thread other than the creation thread.
	// Also- this warning happens in TheBall, when run with only the debug layer. Not our fault.
	// But, we are doing multithreaded StretchRect now, so let's add this.  Otherwise we get a
	// a crash.  Not certain this is best, said to slow things down.
	//BehaviorFlags |= D3DCREATE_MULTITHREADED;

	// Run original call game is expecting.
	// This will return a IDirect3DDevice9Ex variant regardless, but using the original
	// call allows us to avoid a lot of weirdness with full screen handling.

	hr = pOrigCreateDevice(This, Adapter, DeviceType, hFocusWindow, BehaviorFlags, pPresentationParameters,
		ppReturnedDeviceInterface);
	if (FAILED(hr))
		throw std::exception("Failed to create IDirect3DDevice9");


	// Using that fresh DX9 Device, we can now hook the Present and CreateTexture calls.

	IDirect3DDevice9* pDevice9 = *ppReturnedDeviceInterface;

	if (pOrigPresent == nullptr && SUCCEEDED(hr) && ppReturnedDeviceInterface != nullptr)
	{
		SIZE_T hook_id;
		DWORD dwOsErr;
		
		dwOsErr = nktInProc.Hook(&hook_id, (void**)&pOrigPresent,
			lpvtbl_Present(pDevice9), Hooked_Present, 0);
		if (FAILED(dwOsErr))
			::OutputDebugStringA("Failed to hook IDirect3DDevice9::Present\n");

		dwOsErr = nktInProc.Hook(&hook_id, (void**)&pOrigCreateTexture,
			lpvtbl_CreateTexture(pDevice9), Hooked_CreateTexture, 0);
		if (FAILED(dwOsErr))
			::OutputDebugStringA("Failed to hook IDirect3DDevice9::CreateTexture\n");

		dwOsErr = nktInProc.Hook(&hook_id, (void**)&pOrigCreateCubeTexture,
			lpvtbl_CreateCubeTexture(pDevice9), Hooked_CreateCubeTexture, 0);
		if (FAILED(dwOsErr))
			::OutputDebugStringA("Failed to hook IDirect3DDevice9::CreateCubeTexture\n");

		dwOsErr = nktInProc.Hook(&hook_id, (void**)&pOrigCreateVertexBuffer,
			lpvtbl_CreateVertexBuffer(pDevice9), Hooked_CreateVertexBuffer, 0);
		if (FAILED(dwOsErr))
			::OutputDebugStringA("Failed to hook IDirect3DDevice9::CreateVertexBuffer\n");

		dwOsErr = nktInProc.Hook(&hook_id, (void**)&pOrigCreateIndexBuffer,
			lpvtbl_CreateIndexBuffer(pDevice9), Hooked_CreateIndexBuffer, 0);
		if (FAILED(dwOsErr))
			::OutputDebugStringA("Failed to hook IDirect3DDevice9::CreateIndexBuffer\n");


		HRESULT res = NvAPI_Initialize();
		if (FAILED(res))
			throw std::exception("Failed to NvAPI_Initialize\n");

		// ToDo: need to handle stereo disabled...
		res = NvAPI_Stereo_CreateHandleFromIUnknown(pDevice9, &gNVAPI);
		if (FAILED(res))
			throw std::exception("Failed to NvAPI_Stereo_CreateHandleFromIUnknown\n");

		res = NvAPI_Stereo_SetSurfaceCreationMode(__in gNVAPI, __in NVAPI_STEREO_SURFACECREATEMODE_FORCESTEREO);
		if (FAILED(res))
			throw std::exception("Failed to NvAPI_Stereo_SetSurfaceCreationMode\n");


		// Make the priority lower for this game thread, to allow VR side preference.
		// ToDo: doesn't seem to change anything.

		IDirect3DDevice9Ex* pExDevice;
		res = pDevice9->QueryInterface(__uuidof(IDirect3DDevice9Ex), (void**)(&pExDevice));
		if (FAILED(res))
			throw std::exception("Failed to QueryInterface for IDirect3DDevice9Ex\n");
		res = pExDevice->SetGPUThreadPriority(-1);
		if (FAILED(res))
			throw std::exception("Failed to SetGPUThreadPriority for IDirect3DDevice9Ex\n");

		// Now that we have a proper Ex Device from the game, let's also make a 
		// DX9 Surface, so that we can snapshot the game output.  This surface needs to
		// use the Shared parameter, so that we can share it to another Device.  Because
		// these are all DX9Ex objects, the share will work.
		// 
		// The Nvidia documentation suggests this should be an OffScreenPlainSurface,
		// but that has never worked, because it is disallowed by DX9.  Experimenting
		// quite a bit, I hit upon using CreateTexture, with the D3DUSAGE_RENDERTARGET
		// flag set.  
		//
		// This Texture cannot be shared here.  When shared variable is set for input,
		// the StretchRect fails at Present, and makes a mono copy.  Thus, we need to
		// create second target, which will be the one passed to the VR/Unity side.

		UINT width = pPresentationParameters->BackBufferWidth * 2;
		UINT height = pPresentationParameters->BackBufferHeight;
		D3DFORMAT format = pPresentationParameters->BackBufferFormat;

		res = pDevice9->CreateTexture(width, height, 0, D3DUSAGE_RENDERTARGET, format, D3DPOOL_DEFAULT,
			&gGameTexture, nullptr);
		if (FAILED(res))
			throw std::exception("Fail to create shared stereo Texture");
			
		res = gGameTexture->GetSurfaceLevel(0, &gGameSurface);
		if (FAILED(res))
			throw std::exception("Fail to GetSurfaceLevel of stereo Texture");

		// Actual shared surface, as a RenderTarget. RenderTarget because its main
		// goal at this moment is to be written to.
		res = pDevice9->CreateRenderTarget(width, height, format, D3DMULTISAMPLE_NONE, 0, true,
			&gSharedTarget, &gGameSharedHandle);
		if (FAILED(res))
			throw std::exception("Fail to CreateRenderTarget for copy of stereo Texture");

		// Since we are doing setup here, also create a thread that will be used to copy
		// from the stereo game surface into the shared surface.  This way the game will
		// not stall while waiting for that copy.
		//
		// And the thread synchronization Event object. Signaled when we get fresh bits.
		// Starts in off state, thread active, so it should pause at launch.
		gFreshBits = CreateEvent(
			NULL,               // default security attributes
			TRUE,               // manual, not auto-reset event
			FALSE,              // initial state is nonsignaled
			nullptr);			// object name
		if (gFreshBits == nullptr)
			throw std::exception("Fail to CreateEvent for gFreshBits");

		//gSharedThread = CreateThread(
		//	NULL,                   // default security attributes
		//	0,                      // use default stack size  
		//	CopyGameToShared,       // thread function name
		//	pDevice9,		        // device, as argument to thread function 
		//	0,				        // runs immediately, to a pause state. 
		//	nullptr);			    // returns the thread identifier 
		//if (gSharedThread == nullptr)
		//	throw std::exception("Fail to CreateThread for GameToShared");

		// We are certain to be being called from the game's primary thread here,
		// as this is CreateDevice.  Save the reference.
		// ToDo: Can't use Suspend/Resume, because the task switching time is too
		// high, like >16ms, which is must larger than we can use.
		//HANDLE thread = GetCurrentThread();
		//DuplicateHandle(GetCurrentProcess(), thread, GetCurrentProcess(), &gameThread, 0, TRUE, DUPLICATE_SAME_ACCESS);
	}

	// We are returning the IDirect3DDevice9Ex object, because the Device the game
	// is going to use needs to be Ex type, so we can share from its backbuffer.

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


