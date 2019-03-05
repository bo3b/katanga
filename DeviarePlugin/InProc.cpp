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


// Next up, trying to use cuda copies to share surfaces.  This DX9 side will only set
// up the resources, not actually copy.  The only thing needed on this side is the
// copy of the stereo bits, but as a cudaResource. That will be shared through IPC
// to the DX11 side, where it can be copied using cudaMemcpyArrayToArray into 
// a DX11 Texture2D.
//
// Cuda share doesn't work because the CudaRuntime doesn't support cudaContext
// sharing across the two processes. Using CudaDriver API.

//-----------------------------------------------------------

#include <thread>

#include "DeviarePlugin.h"

#include "NktHookLib.h"

//#include "D3dx9tex.h"

#include "nvapi.h"
#include <cuda.h>
#include <cudaD3D9.h>


//-----------------------------------------------------------

// This is automatically instantiated by C++, so the hooking library
// is immediately available.
CNktHookLib nktInProc;

// The surface that we copy the current game frame into. It is shared.
// It starts as a Texture so that it is created stereo, but is converted
// to a Surface for use in StretchRect.

//HANDLE gSharedThread = nullptr;				// will copy from GameSurface to SharedSurface

HANDLE gFreshBits = nullptr;				// Synchronization Event object

IDirect3DTexture9* gGameBitsCopy = nullptr;	// Actual shared RenderTarget
IDirect3DSurface9* gGameSurface = nullptr;	// created as a reference to Texture


// The nvapi stereo handle, to access the reverse blit.
StereoHandle gNVAPI = nullptr;

// RenderTarget, will be copy of stereo bits 
IDirect3DSurface9* g_cudaStereoRT = NULL;
CUdevice g_cudaDevice = NULL;
// These two are shared across to the DX11 side
CUcontext g_cudaContext = NULL;
CUgraphicsResource g_cudaStereoResource = NULL;


//HANDLE gameThread = nullptr;

wchar_t info[512];
static unsigned long triggerCount = 0;


// --------------------------------------------------------------------------------------------------

// Custom routines for this DeviarePlugin.dll, that the master app can call,
// using Deviare access routines.


// Return the current value of the g_cudaStereoResource. This is the cuda
// graphic resource that has been registered.  Through the cudaInterop, this
// is the IDirect3DSurface9* g_cudaStereoRT.
// On the other side, in DX11 land, it will take that cudaGraphicsResource,
// Map it, then do an array copy into the DX11 Texture2D.

CUgraphicsResource WINAPI GetSharedCudaResource(int* in)
{
	return g_cudaStereoResource;
}

CUcontext WINAPI GetSharedCudaContext(int* in)
{
	return g_cudaContext;
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
				L"SetEvent - ms: %lld, frequency: %lld, triggerCount: %d\n", startMS, frequency.QuadPart, triggerCount);
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
				L"ResetEvent - ms: %lld\n", endMS);
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
	RECT backBufferRect = { 0, 0, (LONG)(bufferDesc.Width), (LONG)(bufferDesc.Height) };
	RECT stereoImageRect = { 0, 0, (LONG)(bufferDesc.Width * 2), (LONG)(bufferDesc.Height) };

	int insetW = 300;
	int insetH = (int)(300.0 * stereoImageRect.bottom / stereoImageRect.right);
	RECT topScreen = { 5, 5, insetW, insetH };
	device->StretchRect(surface, &stereoImageRect, back, &topScreen, D3DTEXF_NONE);
}


//-----------------------------------------------------------
// StretchRect the decoded snapshot back onto the backbuffer so we can see what we got.
// Keep aspect ratio intact, because we want to see if it's a stretched image.
// This takes as input the compressed data from the NvCodec, and draws it to the game
// screen so we can see what it looks like.  Should still be stereo.

//void DrawDecodedOnGame(IDirect3DDevice9* device, IDirect3DSurface9* surface, IDirect3DSurface9* back)
//{
//	D3DSURFACE_DESC bufferDesc;
//	back->GetDesc(&bufferDesc);
//	RECT backBufferRect = { 0, 0, bufferDesc.Width, bufferDesc.Height };
//	RECT stereoImageRect = { 0, 0, bufferDesc.Width * 2, bufferDesc.Height };
//
//	int insetW = 300;
//	int insetH = (int)(300.0 * stereoImageRect.bottom / stereoImageRect.right);
//	RECT topScreen = { 5, 5, insetW, insetH };
//	device->StretchRect(surface, &stereoImageRect, back, &topScreen, D3DTEXF_NONE);
//}
//

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

// D3DXLoadSurfaceFromSurface  Maybe useful. Does conversions.

HRESULT __stdcall Hooked_Present(IDirect3DDevice9* This,
	/* [in] */ const RECT    *pSourceRect,
	/* [in] */ const RECT    *pDestRect,
	/* [in] */       HWND    hDestWindowOverride,
	/* [in] */ const RGNDATA *pDirtyRegion)
{
	HRESULT hr;
	IDirect3DSurface9* backBuffer;

	hr = This->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backBuffer);
	if (SUCCEEDED(hr))
	{
		hr = NvAPI_Stereo_ReverseStereoBlitControl(gNVAPI, true);
		{
			hr = This->StretchRect(backBuffer, nullptr, gGameSurface, nullptr, D3DTEXF_NONE);
			if (FAILED(hr))
				::OutputDebugString(L"Bad StretchRect to Texture.\n");

//			SetEvent(gFreshBits);		// Signal other thread to start StretchRect
		}
		hr = NvAPI_Stereo_ReverseStereoBlitControl(gNVAPI, false);

#ifdef _DEBUG
		DrawStereoOnGame(This, gGameSurface, backBuffer);
#endif

		// Copy the stereo bits into the cuda RenderTarget
		hr = This->StretchRect(gGameSurface, nullptr, g_cudaStereoRT, nullptr, D3DTEXF_NONE);

		backBuffer->Release();
	}

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



// Creates gGameSurface, which is the ending destination for the stereo game bits.
// Must be a RenderTargetTexture for ReverseStereoBlit to return stereo on StretchRect.

void CreateStereoTarget(IDirect3DDevice9 * pDevice9, const UINT &width, const UINT &height, const D3DFORMAT &format)
{
	HRESULT hr;
	NvAPI_Status res;

	// We still need the NvAPI to fetch the stereo backbuffer, so init it here.

	res = NvAPI_Initialize();
	if (FAILED(res))
		throw std::exception("Failed to NvAPI_Initialize");

	// ToDo: need to handle stereo disabled...
	res = NvAPI_Stereo_CreateHandleFromIUnknown(pDevice9, &gNVAPI);
	if (FAILED(res))
		throw std::exception("Failed to NvAPI_Stereo_CreateHandleFromIUnknown");

	res = NvAPI_Stereo_SetSurfaceCreationMode(__in gNVAPI, __in NVAPI_STEREO_SURFACECREATEMODE_FORCESTEREO);
	if (FAILED(res))
		throw std::exception("Failed to NvAPI_Stereo_SetSurfaceCreationMode");

	// In between bits for stereo game data.  This must be a RenderTargetTexture,
	// and cannot be a RenderTarget, because ReverseStereoBlit will only copy one
	// eye for RT.

	hr = pDevice9->CreateTexture(width, height, 0, D3DUSAGE_RENDERTARGET, format, D3DPOOL_DEFAULT,
		&gGameBitsCopy, nullptr);
	if (FAILED(hr))
		throw std::exception("Fail to create shared stereo Texture");

	// The surface derived from the rendertarget texture is the actual destination
	// of the StrectRect of game bits.
	hr = gGameBitsCopy->GetSurfaceLevel(0, &gGameSurface);
	if (FAILED(hr))
		throw std::exception("Fail to GetSurfaceLevel of stereo Texture");
}


// Creates g_cudaStereoRT, the destination of the stereo bits in order to be shared
// using the cuda copies.  This one must be an actual RenderTarget, and is thus a
// copy of the gGameSurface, otherwise we'd just use gGameSurface.  If it's not a RT
// the cuda copy will fail.  Even if you extract the surface from the Texture2D.

void CreateCudaStereoTarget(IDirect3DDevice9 * pDevice9, const UINT &width, const UINT &height, const D3DFORMAT &format)
{
	HRESULT hr;
	CUresult cuErr;

	hr = pDevice9->CreateRenderTarget(width, height, format, D3DMULTISAMPLE_NONE, 0, false,
		&g_cudaStereoRT, nullptr);
	if (FAILED(hr))
		throw std::exception("Fail to CreateRenderTarget in CreateCudaStereoTarget");

	cuErr = cuInit(0);
	if (cuErr != CUDA_SUCCESS)
		throw std::exception("Fail to cuInit in CreateCudaStereoTarget");

	// For cuda driver mode use, we need to make the context, it is not
	// automatically created.

	cuErr = cuDeviceGet(&g_cudaDevice, 0);
	if (cuErr != CUDA_SUCCESS)
		throw std::exception("Fail to cuDeviceGet in CreateCudaStereoTarget");
	cuErr = cuCtxCreate(&g_cudaContext, CU_CTX_SCHED_BLOCKING_SYNC, g_cudaDevice);
	if (cuErr != CUDA_SUCCESS)
		throw std::exception("Fail to cuInit in CreateCudaStereoTarget");

	// Register the DX9 copy to be readable by Cuda.
	cuErr = cuGraphicsD3D9RegisterResource(&g_cudaStereoResource, g_cudaStereoRT, CU_GRAPHICS_REGISTER_FLAGS_NONE);
	if (cuErr != CUDA_SUCCESS)
		throw std::exception("Fail to cuGraphicsD3D9RegisterResource in CreateCudaStereoTarget");
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
	UINT gameWidth = pPresentationParameters->BackBufferWidth;
	UINT gameHeight = pPresentationParameters->BackBufferHeight;
	D3DFORMAT gameFormat = pPresentationParameters->BackBufferFormat;

	::OutputDebugString(L"NativePlugin::Hooked_CreateDevice called\n");
	if (pPresentationParameters)
	{
		swprintf_s(info, _countof(info), L"  Width: %d, Height: %d, Format: %d\n"
			, gameWidth, gameHeight, gameFormat);
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

	BehaviorFlags |= D3DCREATE_MULTITHREADED;
	   

	// Run original call game is expecting.

	hr = pOrigCreateDevice(This, Adapter, DeviceType, hFocusWindow, BehaviorFlags, pPresentationParameters,
		ppReturnedDeviceInterface);
	if (FAILED(hr))
		throw std::exception("Failed to create IDirect3DDevice9");

	IDirect3DDevice9* pDevice9 = *ppReturnedDeviceInterface;

	// Using that fresh DX9 Device, we can now hook the Present call.
	// Do this one-time init, including setting up NvAPI, create the
	// stereo target for ReverseStereoBlit, and create the cuda
	// destination surface.

	if (pOrigPresent == nullptr && SUCCEEDED(hr) && ppReturnedDeviceInterface != nullptr)
	{
		SIZE_T hook_id;
		DWORD dwOsErr;
		UINT width = pPresentationParameters->BackBufferWidth * 2;
		UINT height = pPresentationParameters->BackBufferHeight;
		D3DFORMAT format = pPresentationParameters->BackBufferFormat;

		dwOsErr = nktInProc.Hook(&hook_id, (void**)&pOrigPresent,
			lpvtbl_Present(pDevice9), Hooked_Present, 0);
		if (FAILED(dwOsErr))
			::OutputDebugStringA("Failed to hook IDirect3DDevice9::Present");

		// Stereo target for every frame from ReverseStereoBlit
		CreateStereoTarget(pDevice9, width, height, format);

		// Stereo target for the cuda DX9->DX11 copies
		CreateCudaStereoTarget(pDevice9, width, height, format);


	//	DebugBreak();

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

		SIZE_T hook_id;
		DWORD dwOsErr = nktInProc.Hook(&hook_id, (void**)&pOrigCreateDevice,
			lpvtbl_CreateDevice(pDX9), Hooked_CreateDevice, 0);

		if (FAILED(dwOsErr))
			throw std::exception("Failed to hook IDirect3D9::CreateDevice");
	}
}


