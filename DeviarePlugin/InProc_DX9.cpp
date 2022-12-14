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

#include "DeviarePlugin.h"

#include <thread>


// The surface that we copy the current game frame into. It is shared
// via a StretchRect copy into the shared RenderTarget.

IDirect3DSurface9* gGameSurface = nullptr;

// Actual shared RenderTarget. When this is updated via a StretchRect,
// the bits are automatically shared to the DX11 VR side.

IDirect3DSurface9* gSharedTarget = nullptr;


// --------------------------------------------------------------------------------------------------

// Custom routines for this DeviarePlugin.dll, that the master app can call,
// using Deviare access routines.


//HANDLE gSharedThread = nullptr;				// will copy from GameSurface to SharedSurface
//HANDLE gFreshBits = nullptr;				// Synchronization Event object
//static unsigned long triggerCount = 0;


// Shared Event object that is the notification that the VR side
// has called Present.

//HANDLE WINAPI GetEventHandle(int* in)
//{
//	LogInfo(LL"GetSharedEvent::\n");
//
//	return gFreshBits;
//}

// Called from C# side after VR app has presented its frame.
// This allows our locked present for the target game to continue.

//static LARGE_INTEGER startTriggeredTicks, resetTriggerTicks;
//static LARGE_INTEGER frequency;
//
//HANDLE WINAPI TriggerEvent(int* in)
//{
//
//	//	LogInfo(LL"TriggerEvent::\n");
//
//
//	if ((int)in == 1)		// Active triggered
//	{
//		BOOL set = SetEvent(gFreshBits);
//		if (!set)
//			LogInfo(L"Bad SetEvent in TriggerEvent.\n");
//
//		// Waste time spinning, while we wait for high resolution timer.
//		// This timer using QueryPerformanceCounter should be accurate
//		// to some 100ns or so, for any system we care about.
//
//		QueryPerformanceFrequency(&frequency);
//		QueryPerformanceCounter(&startTriggeredTicks);
//
//		triggerCount += 1;
//
//		if ((triggerCount % 30) == 0)
//		{
//			LONGLONG startMS = startTriggeredTicks.QuadPart * 1000 / frequency.QuadPart;
//			swprintf_s(info, _countof(info),
//				L"SetEvent - ms: %d, frequency: %d, triggerCount: %d\n", startMS, frequency.QuadPart, triggerCount);
//			fwprintf(LogFile, info);
//		}
//	}
//	else					// Reset untriggered
//	{
//		BOOL reset = ResetEvent(gFreshBits);
//		if (!reset)
//			LogInfo(L"Bad ResetEvent in TriggerEvent.\n");
//
//		QueryPerformanceCounter(&resetTriggerTicks);
//
//		if ((triggerCount % 30) == 0)
//		{
//			LONGLONG frameTicks = resetTriggerTicks.QuadPart - startTriggeredTicks.QuadPart;
//			LONGLONG endMS = frameTicks * 1000 / frequency.QuadPart;
//			swprintf_s(info, _countof(info),
//				L"ResetEvent - ms: %d\n", endMS);
//			fwprintf(LogFile, info);
//		}
//	}
//
//	return NULL;
//}


//-----------------------------------------------------------
// Common code for both initial setup, and changing of the game resolution
// via the Reset call.  When the shared gGameSharedHandle changes
// from null or an prior version, the Unity side will pick it up
// and rebuild the drawing Quad in VR.
//
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
// This Texture itself cannot be shared here.  When shared variable is set for input,
// the StretchRect fails at Present, and makes a mono copy.  Thus, we need to
// create a second RenderTarget, which will be the one passed to the VR/Unity side.
//
// For DX9Ex this setup process will be done with a late binding approach, where we setup
// this side right before the game draws at Present, to avoid potential conflicts.
// During the CreateShared process, we'll lock out the mutex so the unity side cannot
// get bad data. We double up the mutex lock during Reset call, so that it can lock
// early.  As long as the Capture/Release are balanced, it will work.

void CreateSharedRenderTarget(IDirect3DDevice9* pDevice9)
{
	HRESULT res;
	IDirect3DSurface9* pBackBuffer;
	D3DSURFACE_DESC desc;
	HANDLE tempSharedHandle = NULL;

	LogInfo(L"GamePlugin:DX9 CreateSharedRenderTarget called. gGameSurface: %p, gGameSharedHandle: %p\n", gGameSurface, gGameSharedHandle);

	CaptureSetupMutex();
	{
		NvAPI_Status nvres = NvAPI_Initialize();
		if (nvres != NVAPI_OK) FatalExit(L"NVidia driver not available.\n\nFailed to NvAPI_Initialize\n", nvres);

		nvres = NvAPI_Stereo_CreateHandleFromIUnknown(pDevice9, &gNVAPI);
		if (nvres != NVAPI_OK) FatalExit(L"3D Vision is not enabled.\n\nFailed to NvAPI_Stereo_CreateHandleFromIUnknown\n", nvres);

		res = pDevice9->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &pBackBuffer);
		if (FAILED(res)) FatalExit(L"Fail to GetBackBuffer in CreateSharedRenderTarget", res);
		res = pBackBuffer->GetDesc(&desc);
		if (FAILED(res)) FatalExit(L"Fail to GetDesc on BackBuffer", res);
		pBackBuffer->Release();

		UINT width = desc.Width * 2;
		UINT height = desc.Height;
		D3DFORMAT format = desc.Format;
		IDirect3DTexture9* stereoCopy = nullptr;

		LogInfo(L"  Width: %d, Height: %d, Format: %d\n", width, height, format);

		res = pDevice9->CreateTexture(width, height, 0, D3DUSAGE_RENDERTARGET, format, D3DPOOL_DEFAULT,
			&stereoCopy, nullptr);
		if (FAILED(res)) FatalExit(L"Fail to create shared stereo Texture", res);

		res = stereoCopy->GetSurfaceLevel(0, &gGameSurface);
		if (FAILED(res)) FatalExit(L"Fail to GetSurfaceLevel of stereo Texture", res);

		// Actual shared surface, as a RenderTarget. RenderTarget because that is
		// what the Unity side is expecting.  tempSharedHandle, to avoid kicking
		// off changes just yet, and reusing the current gGameSharedHandle errors out.
		res = pDevice9->CreateRenderTarget(width, height, format, D3DMULTISAMPLE_NONE, 0, true,
			&gSharedTarget, &tempSharedHandle);
		if (FAILED(res)) FatalExit(L"Fail to CreateRenderTarget for copy of stereo Texture", res);

		// Everything has been setup, or cleanly re-setup, and we can now enable the
		// VR side to kick in and use the new surfaces.
		gGameSharedHandle = tempSharedHandle;

		// Move that shared handle into the MappedView to IPC the Handle to Katanga.
		// The HANDLE is always 32 bit, even for 64 bit processes.
		// https://docs.microsoft.com/en-us/windows/win32/winprog64/interprocess-communication

		*(PUINT)(gMappedView) = PtrToUint(gGameSharedHandle);

		LogInfo(L"  Successfully created new shared surface: %p, new shared handle: %p, mapped: %p\n", gGameSurface, gGameSharedHandle, gMappedView);
	}
	ReleaseSetupMutex();
}

//-----------------------------------------------------------
// StretchRect the stereo snapshot back onto the backbuffer so we can see what we got.
// Keep aspect ratio intact, because we want to see if it's a stretched image.

void DrawStereoOnGame(IDirect3DDevice9* device, IDirect3DSurface9* surface, IDirect3DSurface9* back)
{
	D3DSURFACE_DESC bufferDesc;
	back->GetDesc(&bufferDesc);
	RECT backBufferRect = { 0, 0, (LONG) bufferDesc.Width, (LONG) bufferDesc.Height };
	RECT stereoImageRect = { 0, 0, (LONG) bufferDesc.Width * 2, (LONG) bufferDesc.Height };

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
// The StretchRect to the gGameSurface will be duplicated in the gSharedTarget. So
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

	// This only happens for first device creation, because we inject into an already
	// setup game, and thus first thing we'll see is Present in DX9Ex case.
	if (gGameSharedHandle == NULL)
		CreateSharedRenderTarget(This);

	hr = This->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backBuffer);
	if (SUCCEEDED(hr) && gGameSurface != nullptr)
	{
		if (gDirectMode)
		{
			D3DSURFACE_DESC pDesc;
			RECT destRect = { 0, 0, 0, 0 };

			backBuffer->GetDesc(&pDesc);
			destRect.bottom = pDesc.Height;

			hr = NvAPI_Stereo_SetActiveEye(gNVAPI, NVAPI_STEREO_EYE_RIGHT);
			destRect.right = pDesc.Width;
			hr = This->StretchRect(backBuffer, nullptr, gGameSurface, &destRect, D3DTEXF_NONE);

			hr = NvAPI_Stereo_SetActiveEye(gNVAPI, NVAPI_STEREO_EYE_LEFT);
			destRect.left = pDesc.Width;
			destRect.right = pDesc.Width * 2;
			hr = This->StretchRect(backBuffer, nullptr, gGameSurface, &destRect, D3DTEXF_NONE);

			hr = This->StretchRect(gGameSurface, nullptr, gSharedTarget, nullptr, D3DTEXF_NONE);
		}
		else
		{
			hr = NvAPI_Stereo_ReverseStereoBlitControl(gNVAPI, true);
			{
				hr = This->StretchRect(backBuffer, nullptr, gGameSurface, nullptr, D3DTEXF_NONE);
				if (FAILED(hr))
					LogInfo(L"Bad StretchRect to Texture.\n");
				hr = This->StretchRect(gGameSurface, nullptr, gSharedTarget, nullptr, D3DTEXF_NONE);

				//			SetEvent(gFreshBits);		// Signal other thread to start StretchRect
			}
			hr = NvAPI_Stereo_ReverseStereoBlitControl(gNVAPI, false);
		}
#ifdef _DEBUG
		DrawStereoOnGame(This, gSharedTarget, backBuffer);
#endif
	}
	backBuffer->Release();

	HRESULT hrp = pOrigPresent(This, pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion);

	//// Sync starting next frame with VR app.
	//DWORD object;
	//do
	//{
	//	object = WaitForSingleObject(gFreshBits, 0);
	//} while (object != WAIT_OBJECT_0);

	return hrp;
}


//-----------------------------------------------------------
//
// For IDirect3DDevice9->Reset, this is used to setup the backbuffer
// again for a different resolution.  So we need to handle this and
// rebuild our drawing chain whenever it changes.
// 
// We need to lock out the VR side while this is happening to avoid that
// side from using stale or released surfaces, so we'll capture the Mutex first.

HRESULT(__stdcall *pOrigReset)(IDirect3DDevice9* This,
	D3DPRESENT_PARAMETERS *pPresentationParameters
) = nullptr;

HRESULT __stdcall Hooked_Reset(IDirect3DDevice9* This,
	D3DPRESENT_PARAMETERS *pPresentationParameters)
{
	HRESULT hr;
	
	LogInfo(L"GamePlugin: IDirect3DDevice9->Reset called. gGameSurface: %p, gSharedTarget: %p, gGameSharedHandle: %p\n", 
		gGameSurface, gSharedTarget, gGameSharedHandle);
	if (pPresentationParameters != nullptr)
	{
		LogInfo(L"  Width: %d, Height: %d, Format: %d\n", 
			pPresentationParameters->BackBufferWidth, pPresentationParameters->BackBufferHeight, pPresentationParameters->BackBufferFormat);
	}

	// Grab the KatangaSetupMutex, so that the VR side will be locked out of touching
	// any shared surfaces until we rebuild the shared surface after CreateRenderedSurface.

	CaptureSetupMutex();
	{
		// As soon as we know we are setting up a new resolution, we want to set the
		// gGameSharedHandle to null, to notify the VR side that this is going away.
		// Given the async and multi-threaded nature of these pieces in different
		// processes, it's not clear if this will work in every case. 
		//
		// ToDo: We might need to keep VR and game side in sync to avoid dead texture use.
		//
		// No good way to properly dispose of this shared handle, we cannot CloseHandle
		// because it's not a real handle.  Microsoft.  Geez.

		gGameSharedHandle = NULL;

		// We are also supposed to release any of our rendertargets before calling
		// Reset, so let's go ahead and release these, now that the null gGameSharedHandle
		// will have stalled drawing in the VR side.  

		if (gGameSurface)
		{
			LogInfo(L"  Release gGameSurface: %p\n", gGameSurface);
			gGameSurface->Release();
			gGameSurface = NULL;
		}
		if (gSharedTarget)
		{
			LogInfo(L"  Release gSharedTarget: %p\n", gSharedTarget);
			gSharedTarget->Release();
			gSharedTarget = NULL;
		}

		// Fire off the Reset.  After this is called every single texture on the
		// device will have been released, including our shared one.  Any drawing
		// use our shared texture can thus crash the device.

		hr = pOrigReset(This, pPresentationParameters);
		LogInfo(L"  IDirect3DDevice9->Reset result: %d\n", hr);

		// Remove and replace the shared texture to match new setup.
		CreateSharedRenderTarget(This);
	}

	// Free up the VR side to continue drawing, now that we have a cleanly setup shared surface,
	// with no further conflicts.
	ReleaseSetupMutex();

	return hr;
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
//		//	LogInfo(L"Bad StretchRect to RenderTarget in CopyGameToShared thread.\n");
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
		LogInfo(L"GamePlugin::Hooked_CreateTexture - Levels: %d, Usage: %x, Format: %d, Pool: %d\n",
			Levels, Usage, Format, Pool);
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
		LogInfo(L"Failed to call pOrigCreateTexture\n");

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
	LogInfo(L"GamePlugin::Hooked_CreateTexture - Levels: %d, Usage: %x, Format: %d, Pool: %d\n",
		Levels, Usage, Format, Pool);
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
		LogInfo(L"Failed to call pOrigCreateCubeTexture\n");

	return hr;
}

//-----------------------------------------------------------
// Interface to implement the hook for IDirect3DDevice9->CreateVolumeTexture
//
// We know this one is necessary for sure in Left4Dead2.  Found via the DX9
// debug layer on Windows 7.  Missed it before, leading to bad graphics.

// This declaration serves a dual purpose of defining the interface routine as required by
// DX9, and also is the storage for the original call, returned by nktInProc.Hook

HRESULT(__stdcall *pOrigCreateVolumeTexture)(IDirect3DDevice9* This,
	UINT                    Width,
	UINT                    Height,
	UINT                    Depth,
	UINT                    Levels,
	DWORD                   Usage,
	D3DFORMAT               Format,
	D3DPOOL                 Pool,
	IDirect3DVolumeTexture9 **ppVolumeTexture,
	HANDLE                  *pSharedHandle
	) = nullptr;

// We need to implement a hook on CreateCubeTexture, because the game
// will create this after we return the IDirect3DDevice9Ex Device,
// and the debug layer crashes with:
// Direct3D9: (ERROR) : D3DPOOL_MANAGED is not valid with IDirect3DDevice9Ex
// Then:
// Direct3D9: (ERROR) :Lock is not supported for textures allocated with POOL_DEFAULT unless they are marked D3DUSAGE_DYNAMIC.

HRESULT __stdcall Hooked_CreateVolumeTexture(IDirect3DDevice9* This,
	UINT                    Width,
	UINT                    Height,
	UINT                    Depth,
	UINT                    Levels,
	DWORD                   Usage,
	D3DFORMAT               Format,
	D3DPOOL                 Pool,
	IDirect3DVolumeTexture9 **ppVolumeTexture,
	HANDLE                  *pSharedHandle)
{
#ifdef _DEBUG
	LogInfo(L"GamePlugin::Hooked_CreateVolumeTexture - Levels: %d, Usage: %x, Format: %d, Pool: %d\n",
		Levels, Usage, Format, Pool);
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

	HRESULT hr = pOrigCreateVolumeTexture(This, Width, Height, Depth, Levels, Usage, Format, Pool,
		ppVolumeTexture, pSharedHandle);
	if (FAILED(hr))
		LogInfo(L"Failed call to pOrigCreateVolumeTexture\n");

	return hr;
}

//-----------------------------------------------------------
// Interface to implement the hook for IDirect3DDevice9->CreateOffscreenPlainSurface
//
// Not known to be necessary, but we know for sure that D3DPOOL must be Dynamic in
// DX9Ex, so let's make that happen here too. Could be responsible for game glitches
// that we have not tested under Debug Layer.

// This declaration serves a dual purpose of defining the interface routine as required by
// DX9, and also is the storage for the original call, returned by nktInProc.Hook

HRESULT(__stdcall *pOrigCreateOffscreenPlainSurface)(IDirect3DDevice9* This,
	UINT              Width,
	UINT              Height,
	D3DFORMAT         Format,
	D3DPOOL           Pool,
	IDirect3DSurface9 **ppSurface,
	HANDLE            *pSharedHandle
	) = nullptr;

// We need to implement a hook on CreateOffscreenPlainSurface, because the game
// will create this after we return the IDirect3DDevice9Ex Device,
// and the debug layer crashes with:
// Direct3D9: (ERROR) : D3DPOOL_MANAGED is not valid with IDirect3DDevice9Ex

HRESULT __stdcall Hooked_CreateOffscreenPlainSurface(IDirect3DDevice9* This,
	UINT              Width,
	UINT              Height,
	D3DFORMAT         Format,
	D3DPOOL           Pool,
	IDirect3DSurface9 **ppSurface,
	HANDLE            *pSharedHandle)
{
#ifdef _DEBUG
	LogInfo(L"GamePlugin::Hooked_CreateOffscreenPlainSurface - Width: %d, Height: %x, Format: %d, Pool: %d\n",
		Width, Height, Format, Pool);
#endif

	// Force Managed_Pool to always be default, the only possibility on DX9Ex.
	// D3DPOOL_SYSTEMMEM is still valid however, and should not be tweaked.
	if (Pool == D3DPOOL_MANAGED)
		Pool = D3DPOOL_DEFAULT;

	HRESULT hr = pOrigCreateOffscreenPlainSurface(This, Width, Height, Format, Pool,
		ppSurface, pSharedHandle);
	if (FAILED(hr))
		LogInfo(L"Failed to call pOrigCreateOffscreenPlainSurface\n");

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
		LogInfo(L"GamePlugin::Hooked_CreateVertexBuffer -  Usage: %x, Pool: %d\n",
			Usage, Pool);
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
		LogInfo(L"Failed to call pOrigCreateVertexBuffer\n");

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
		LogInfo(L"GamePlugin::Hooked_CreateIndexBuffer -  Usage: %x, Format: %d, Pool: %d\n",
			Usage, Format, Pool);
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
		LogInfo(L"Failed to call pOrigCreateIndexBuffer\n");

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
	HRESULT hr;

	LogInfo(L"\nGamePlugin::Hooked_CreateDevice called\n");
	if (pPresentationParameters != nullptr)
	{
		LogInfo(L"  Width: %d, Height: %d, Format: %d\n",
			pPresentationParameters->BackBufferWidth, pPresentationParameters->BackBufferHeight, pPresentationParameters->BackBufferFormat);
	}

	// Grab Mutex here during Device creation, and release it after we 
	// have built our shared surface.  This will keep the VR side from drawing
	// or conflicting with anything in game side here.
	// This must be done here, because the Mutex cares what thread it is captured
	// from, and we have to be on the same thread.

	CaptureSetupMutex();
	{

		// This is called out in the debug layer as a potential performance problem, but the
		// docs suggest adding this will slow things down.  It is unlikely to be actually
		// necessary, because this is in the running game, and the other threads are actually
		// in a different process altogether.  
		// Direct3D9: (WARN) : Device that was created without D3DCREATE_MULTITHREADED is being used by a thread other than the creation thread.
		// Also- this warning happens in TheBall, when run with only the debug layer. Not our fault.
		// But, we are doing multithreaded StretchRect now, so let's add this.  Otherwise we get a
		// a crash.  Not certain this is best, said to slow things down.  But removes warnings in debug layer.
		BehaviorFlags |= D3DCREATE_MULTITHREADED;

		// Run original call game is expecting.
		// This will return a IDirect3DDevice9Ex variant regardless, but using the original
		// call allows us to avoid a lot of weirdness with full screen handling.

		hr = pOrigCreateDevice(This, Adapter, DeviceType, hFocusWindow, BehaviorFlags, pPresentationParameters,
			ppReturnedDeviceInterface);
		if (FAILED(hr)) FatalExit(L"Failed to create IDirect3DDevice9", hr);

		// Using that fresh DX9 Device, we can now hook the Present and CreateTexture calls.

		IDirect3DDevice9* pDevice9 = (ppReturnedDeviceInterface != nullptr) ? *ppReturnedDeviceInterface : nullptr;

		LogInfo(L"  IDirect3D9->CreateDevice result: %d, device: %p\n", hr, pDevice9);

		if (pOrigPresent == nullptr && SUCCEEDED(hr) && pDevice9 != nullptr)
		{
			SIZE_T hook_id;
			DWORD dwOsErr;

			LogInfo(L"  Create hooks for all DX9 calls.\n");

			dwOsErr = nktInProc.Hook(&hook_id, (void**)&pOrigPresent,
				lpvtbl_Present_DX9(pDevice9), Hooked_Present, 0);
			if (FAILED(dwOsErr))
				LogInfo(L"Failed to hook IDirect3DDevice9::Present\n");

			dwOsErr = nktInProc.Hook(&hook_id, (void**)&pOrigReset,
				lpvtbl_Reset(pDevice9), Hooked_Reset, 0);
			if (FAILED(dwOsErr))
				LogInfo(L"Failed to hook IDirect3DDevice9::Reset\n");

			dwOsErr = nktInProc.Hook(&hook_id, (void**)&pOrigCreateTexture,
				lpvtbl_CreateTexture(pDevice9), Hooked_CreateTexture, 0);
			if (FAILED(dwOsErr))
				LogInfo(L"Failed to hook IDirect3DDevice9::CreateTexture\n");

			dwOsErr = nktInProc.Hook(&hook_id, (void**)&pOrigCreateCubeTexture,
				lpvtbl_CreateCubeTexture(pDevice9), Hooked_CreateCubeTexture, 0);
			if (FAILED(dwOsErr))
				LogInfo(L"Failed to hook IDirect3DDevice9::CreateCubeTexture\n");

			dwOsErr = nktInProc.Hook(&hook_id, (void**)&pOrigCreateVolumeTexture,
				lpvtbl_CreateVolumeTexture(pDevice9), Hooked_CreateVolumeTexture, 0);
			if (FAILED(dwOsErr))
				LogInfo(L"Failed to hook IDirect3DDevice9::CreateVolumeTexture\n");

			dwOsErr = nktInProc.Hook(&hook_id, (void**)&pOrigCreateOffscreenPlainSurface,
				lpvtbl_CreateOffscreenPlainSurface(pDevice9), Hooked_CreateOffscreenPlainSurface, 0);
			if (FAILED(dwOsErr))
				LogInfo(L"Failed to hook IDirect3DDevice9::CreateOffscreenPlainSurface\n");

			dwOsErr = nktInProc.Hook(&hook_id, (void**)&pOrigCreateVertexBuffer,
				lpvtbl_CreateVertexBuffer(pDevice9), Hooked_CreateVertexBuffer, 0);
			if (FAILED(dwOsErr))
				LogInfo(L"Failed to hook IDirect3DDevice9::CreateVertexBuffer\n");

			dwOsErr = nktInProc.Hook(&hook_id, (void**)&pOrigCreateIndexBuffer,
				lpvtbl_CreateIndexBuffer(pDevice9), Hooked_CreateIndexBuffer, 0);
			if (FAILED(dwOsErr))
				LogInfo(L"Failed to hook IDirect3DDevice9::CreateIndexBuffer\n");


			NvAPI_Status res = NvAPI_Initialize();
			if (res != NVAPI_OK) FatalExit(L"NVidia driver not available.\n\nFailed to NvAPI_Initialize\n", res);

			res = NvAPI_Stereo_CreateHandleFromIUnknown(pDevice9, &gNVAPI);
			if (res != NVAPI_OK) FatalExit(L"3D Vision is not enabled.\n\nFailed to NvAPI_Stereo_CreateHandleFromIUnknown\n", res);

			// ToDo: Is this necessary?
			// Seems like I just added it without knowing impact. Since we create 2x buffer, might just 
			// cause problems.
			//res = NvAPI_Stereo_SetSurfaceCreationMode(__in gNVAPI, __in NVAPI_STEREO_SURFACECREATEMODE_FORCESTEREO);
			//if (FAILED(res)) FatalExit(L"Failed to NvAPI_Stereo_SetSurfaceCreationMode\n");



			// Since we are doing setup here, also create a thread that will be used to copy
			// from the stereo game surface into the shared surface.  This way the game will
			// not stall while waiting for that copy.
			//
			// And the thread synchronization Event object. Signaled when we get fresh bits.
			// Starts in off state, thread active, so it should pause at launch.
			//gFreshBits = CreateEvent(
			//	NULL,               // default security attributes
			//	TRUE,               // manual, not auto-reset event
			//	FALSE,              // initial state is nonsignaled
			//	nullptr);			// object name
			//if (gFreshBits == nullptr) FatalExit(L"Fail to CreateEvent for gFreshBits");

			//gSharedThread = CreateThread(
			//	NULL,                   // default security attributes
			//	0,                      // use default stack size  
			//	CopyGameToShared,       // thread function name
			//	pDevice9,		        // device, as argument to thread function 
			//	0,				        // runs immediately, to a pause state. 
			//	nullptr);			    // returns the thread identifier 
			//if (gSharedThread == nullptr) FatalExit(L"Fail to CreateThread for GameToShared");

			// We are certain to be being called from the game's primary thread here,
			// as this is CreateDevice.  Save the reference.
			// ToDo: Can't use Suspend/Resume, because the task switching time is too
			// high, like >16ms, which is must larger than we can use.
			//HANDLE thread = GetCurrentThread();
			//DuplicateHandle(GetCurrentProcess(), thread, GetCurrentProcess(), &gameThread, 0, TRUE, DUPLICATE_SAME_ACCESS);
		}

		CreateSharedRenderTarget(pDevice9);
	}
	ReleaseSetupMutex();

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

		LogInfo(L"GamePlugin::HookCreateDevice\n");
		SIZE_T hook_id;
		DWORD dwOsErr = nktInProc.Hook(&hook_id, (void**)&pOrigCreateDevice,
			lpvtbl_CreateDevice(pDX9Ex), Hooked_CreateDevice, 0);

		if (FAILED(dwOsErr)) FatalExit(L"Failed to hook IDirect3D9::CreateDevice", dwOsErr);
	}
}


//-----------------------------------------------------------

IDirect3D9* (__stdcall *pOrigDirect3DCreate9)(
	UINT SDKVersion
	) = nullptr;

IDirect3D9* __stdcall Hooked_Direct3DCreate9(
	UINT SDKVersion)
{
	LogInfo(L"GamePlugin::Hooked_Direct3DCreate9 - SDK: %d\n", SDKVersion);

	IDirect3D9Ex* pDX9Ex = nullptr;
	HRESULT hr = Direct3DCreate9Ex(SDKVersion, &pDX9Ex);
	if (FAILED(hr)) FatalExit(L"Failed Direct3DCreate9Ex", hr);

	// Hook the next level of CreateDevice so that ultimately we
	// can get to the SwapChain->Present.

	HookCreateDevice(pDX9Ex);

	LogInfo(L"  Returned IDirect3D9Ex: %p\n", pDX9Ex);
	return pDX9Ex;
}


//-----------------------------------------------------------

// Here we want to start the daisy chain of hooking DX9 interfaces, to
// ultimately get access to IDirect3DDevice9::Present
//
// At this point, we are at initial game launch, before anything has been
// created, and need to install a hook on Direct3DCreate9.
// 
// It is a different mechanism than that used for COM objects, because this
// call is directly exported by the d3d9.dll.  We need to hook this call
// here instead of with Deviare calls, because we need to get the address
// of the System32 d3d9.dll.  If we don't do this special handling, we get
// the address of the proxy HelixMod d3d9.dll, and cannot return our 
// required IDirect3D9Ex object to HelixMod.  Without this, we just unhook
// HelixMod, and we need it to fix the 3D effects.
//
// Now hooking Direct3Create9Ex as well, because some games actually use this,
// like Alice, and also the Helifax OpenGL wrapper uses DX9Ex to show stereo.

void HookDirect3DCreate9()
{
	WCHAR d3d9SystemPath[MAX_PATH];

	LogInfo(L"GamePlugin::HookDirect3DCreate9\n");

	UINT size = GetSystemDirectory(d3d9SystemPath, MAX_PATH);
	if (size == 0) FatalExit(L"Failed to GetSystemDirectory at HookDirect3DCreat9", GetLastError());

	errno_t err = wcscat_s(d3d9SystemPath, MAX_PATH, L"\\D3D9.DLL");
	if (err != 0) FatalExit(L"Failed to concat string at HookDirect3DCreat9", err);

	HMODULE hSystemD3D9 = LoadLibrary(d3d9SystemPath);
	if (hSystemD3D9 == NULL) FatalExit(L"Failed to LoadLibrary for System32 d3d9.dll", GetLastError());

	FARPROC systemDirect3DCreate9 = GetProcAddress(hSystemD3D9, "Direct3DCreate9");
	if (systemDirect3DCreate9 == NULL) FatalExit(L"Failed to getProcedureAddress for system Direct3DCreate9", GetLastError());

	// This can be called multiple times by a game, so let's be sure to
	// only hook once.
	if (pOrigDirect3DCreate9 == nullptr && systemDirect3DCreate9 != nullptr)
	{
#ifdef _DEBUG 
		nktInProc.SetEnableDebugOutput(TRUE);
#endif

		SIZE_T hook_id;
		DWORD dwOsErr = nktInProc.Hook(&hook_id, (void**)&pOrigDirect3DCreate9,
			systemDirect3DCreate9, Hooked_Direct3DCreate9, 0);

		if (FAILED(dwOsErr)) FatalExit(L"Failed to hook D3D9.DLL::Direct3DCreate9", dwOsErr);
	}
}

//-----------------------------------------------------------

// This is using the Kiero style creation of hooks for the Direct3DCreate9Ex variant, where the
// game specifically requests Direct3DCreate9Ex, like Bayonetta.  In this case, we don't need 
// to hook the other texture calls like CreateTexture, CreateCubeTexture to fix bugs.  It's thus
// similar to the DX11 launch path, where we can just fetch the Present call at the start and
// not daisychain to find it.  This lets us do the late-binding approach where the game can
// fully launch before start to create the shared texture.  Bypasses launchers, and makes it 
// more reliable.

Status::Enum FindAndHookDX9ExPresent()
{
	WNDCLASSEX windowClass;
	windowClass.cbSize = sizeof(WNDCLASSEX);
	windowClass.style = CS_HREDRAW | CS_VREDRAW;
	windowClass.lpfnWndProc = DefWindowProc;
	windowClass.cbClsExtra = 0;
	windowClass.cbWndExtra = 0;
	windowClass.hInstance = GetModuleHandle(NULL);
	windowClass.hIcon = NULL;
	windowClass.hCursor = NULL;
	windowClass.hbrBackground = NULL;
	windowClass.lpszMenuName = NULL;
	windowClass.lpszClassName = KIERO_TEXT("Kiero");
	windowClass.hIconSm = NULL;

	::RegisterClassEx(&windowClass);

	HWND window = ::CreateWindow(windowClass.lpszClassName, KIERO_TEXT("Kiero DirectX Window"), WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, NULL, NULL, windowClass.hInstance, NULL);

	HMODULE libD3D9;
	if ((libD3D9 = ::GetModuleHandle(KIERO_TEXT("d3d9.dll"))) == NULL)
	{
		::DestroyWindow(window);
		::UnregisterClass(windowClass.lpszClassName, windowClass.hInstance);
		return Status::ModuleNotFoundError;
	}

	void* Direct3DCreate9Ex;
	if ((Direct3DCreate9Ex = ::GetProcAddress(libD3D9, "Direct3DCreate9Ex")) == NULL)
	{
		::DestroyWindow(window);
		::UnregisterClass(windowClass.lpszClassName, windowClass.hInstance);
		return Status::UnknownError;
	}

	IDirect3D9Ex* pDirect3D9Ex;
	HRESULT hr = ((HRESULT(__stdcall*)(uint32_t, IDirect3D9Ex**))(Direct3DCreate9Ex))(D3D_SDK_VERSION, &pDirect3D9Ex);
	if (FAILED(hr) || pDirect3D9Ex == NULL)
	{
		::DestroyWindow(window);
		::UnregisterClass(windowClass.lpszClassName, windowClass.hInstance);
		return Status::UnknownError;
	}

	D3DDISPLAYMODE displayMode;
	if (pDirect3D9Ex->GetAdapterDisplayMode(D3DADAPTER_DEFAULT, &displayMode) < 0)
	{
		::DestroyWindow(window);
		::UnregisterClass(windowClass.lpszClassName, windowClass.hInstance);
		return Status::UnknownError;
	}

	D3DPRESENT_PARAMETERS params;
	params.BackBufferWidth = 0;
	params.BackBufferHeight = 0;
	params.BackBufferFormat = displayMode.Format;
	params.BackBufferCount = 0;
	params.MultiSampleType = D3DMULTISAMPLE_NONE;
	params.MultiSampleQuality = NULL;
	params.SwapEffect = D3DSWAPEFFECT_DISCARD;
	params.hDeviceWindow = window;
	params.Windowed = 1;
	params.EnableAutoDepthStencil = 0;
	params.AutoDepthStencilFormat = D3DFMT_UNKNOWN;
	params.Flags = NULL;
	params.FullScreen_RefreshRateInHz = 0;
	params.PresentationInterval = 0;

	IDirect3DDevice9Ex* pDevice9Ex;
	if (pDirect3D9Ex->CreateDeviceEx(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, window, D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_DISABLE_DRIVER_MANAGEMENT, &params, NULL, &pDevice9Ex) < 0)
	{
		pDirect3D9Ex->Release();
		::DestroyWindow(window);
		::UnregisterClass(windowClass.lpszClassName, windowClass.hInstance);
		return Status::UnknownError;
	}

	// With the device created, we can now fetch the address of the Present call by looking it up
	// in the object vtable.  Let's convert the IDirect3DDevice9Ex device to the IDirect3DDevice9
	// as we cannot trust the IDirect3DDevice9Ex APIs, and we know that it's the same address in 
	// any case.  Let's still do this the proper way with QueryInterface, and not just use pointers.

	IDirect3DDevice9* pDevice9;
	hr = pDevice9Ex->QueryInterface(__uuidof(IDirect3DDevice9), (void**)&pDevice9);

	LogInfo(L"  IDirect3D9Ex->CreateDevice result: %d, device9Ex: %p, device9: %p\n", hr, pDevice9Ex, pDevice9);

	SIZE_T hook_id;
	DWORD dwOsErr;
	dwOsErr = nktInProc.Hook(&hook_id, (void**)&pOrigPresent,
		lpvtbl_Present_DX9(pDevice9), Hooked_Present, 0);
	if (FAILED(dwOsErr))
		LogInfo(L"Failed to hook IDirect3DDevice9Ex::Present\n");
	dwOsErr = nktInProc.Hook(&hook_id, (void**)&pOrigReset,
		lpvtbl_Reset(pDevice9), Hooked_Reset, 0);
	if (FAILED(dwOsErr))
		LogInfo(L"Failed to hook IDirect3DDevice9::Reset\n");


	// Now release all the created objects, as they were just used to get us to the vtable.
	
	pDevice9->Release();
	pDevice9 = NULL;
	pDevice9Ex->Release();
	pDevice9Ex = NULL;

	::DestroyWindow(window);
	::UnregisterClass(windowClass.lpszClassName, windowClass.hInstance);

	LogInfo(L"Successfully hooked IDirect3DDevice9Ex::Present\n");

	return Status::Success;
}

