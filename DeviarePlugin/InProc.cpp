// For this part of the DLL, it makes more sense to use Nektra In-Proc
// instead of Nektra Deviare.  The interface for DX11 is not well supported
// by the DB for Deviare, so getting access to the DX11 APIs is simpler
// using In-Proc.  It would be possible to add the DX11 interfaces to 
// the DB files, and rebuild the DB if you wanted to use Deviare.
//
// All In-Proc use is in this file, all Deviare use is in NativePlugin.

// A note in the documentation: https://msdn.microsoft.com/en-us/library/windows/desktop/bb172586(v=vs.85).aspx
// "A cross-process shared-surface should not be locked."
//
// With that use of CreateTexture, the next step was to find that shared surfaces
// cannot be used as the target, necessitating a second copy from the game.  This is
// a second destination of the game bits, which is the final shared resource.  This
// is OK, because we need to swap eyes for Unity, and this is where it's done.

// Overall, seriously hard to get this all working.  Multiple problems with bad
// and misleading and missing documentation. And no sample code.  This complicated
// runtime environment makes it hard to debug as well.  Careful with that axe, Eugene,
// this is all pretty fragile.

// 3-13-19: Changing all DX9 code and comments to use DX11 instead.
//	DX11 SurfaceSharing does not appear to have as many problems as DX9.
//  Using the StereoSnapShot code from 3Dmigoto to get stereo back buffer
//  with NvAPI_Stereo_ReverseStereoBlitControl. 

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

ID3D11Texture2D* gGameTexture = nullptr;

HANDLE gSharedThread = nullptr;				// will copy from GameSurface to SharedSurface

HANDLE gFreshBits = nullptr;				// Synchronization Event object

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

void DrawStereoOnGame(ID3D11DeviceContext* pContext, ID3D11Texture2D* surface, ID3D11Texture2D* back)
{
	D3D11_BOX srcBox = { 300, 0, 0, 1600+300, 900, 1};
	pContext->CopySubresourceRegion(back, 0, 0, 0, 0, surface, 0, &srcBox);
}


//-----------------------------------------------------------
// Interface to implement the hook for IDXGISwapChain->Present

// This declaration serves a dual purpose of defining the interface routine as required by
// DX11, and also is the storage for the original call, returned by nktInProc.Hook

HRESULT (__stdcall *pOrigPresent)(IDXGISwapChain * This,
	/* [in] */ UINT SyncInterval,
	/* [in] */ UINT Flags
	) = nullptr;


// This is it. The one we are after.  This is the hook for the DX11 Present call
// which the game will call for every frame.  At each call, we will make a copy
// of whatever the game drew, and that will be passed along via the shared surface
// to the VR layer.

HRESULT __stdcall Hooked_Present(IDXGISwapChain * This,
	/* [in] */ UINT SyncInterval,
	/* [in] */ UINT Flags)
{
	HRESULT hr;
	ID3D11Texture2D* backBuffer = nullptr;
	D3D11_TEXTURE2D_DESC pDesc;
	ID3D11Device* pDevice = nullptr;
	ID3D11DeviceContext* pContext = nullptr;

	hr = This->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer);
	if (SUCCEEDED(hr) && gGameTexture > 0)
	{
		backBuffer->GetDesc(&pDesc);
		backBuffer->GetDevice(&pDevice);
		pDevice->GetImmediateContext(&pContext);

		hr = NvAPI_Stereo_ReverseStereoBlitControl(gNVAPI, true);
		{
			pContext->CopySubresourceRegion(gGameTexture, 0, 0, 0, 0, backBuffer, 0, nullptr);

//			SetEvent(gFreshBits);		// Signal other thread to start StretchRect
		}
		hr = NvAPI_Stereo_ReverseStereoBlitControl(gNVAPI, false);

#ifdef _DEBUG
		DrawStereoOnGame(pContext, gGameTexture, backBuffer);
#endif
		pContext->Release();
		pDevice->Release();
	}
	backBuffer->Release();

	HRESULT hrp = pOrigPresent(This, SyncInterval, Flags);

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
// Interface to implement the hook for IDXGIFactory1->CreateSwapChain

// This declaration serves a dual purpose of defining the interface routine as required by
// DX11, and also is the storage for the original call, returned by nktInProc.Hook

HRESULT(__stdcall *pOrigCreateSwapChain)(IDXGIFactory1 * This,
	_In_  IUnknown *pDevice,
	_In_  DXGI_SWAP_CHAIN_DESC *pDesc,
	_COM_Outptr_  IDXGISwapChain **ppSwapChain
	) = nullptr;

// The actual Hooked routine for CreateSwapChain, called whenever the game
// makes a IDXGIFactory1->CreateSwapChain call.

HRESULT __stdcall Hooked_CreateSwapChain(IDXGIFactory1 * This,
	_In_  IUnknown *pDevice,
	_In_  DXGI_SWAP_CHAIN_DESC *pDesc,
	_COM_Outptr_  IDXGISwapChain **ppSwapChain)
{
	wchar_t info[512];
	HRESULT hr;

#ifdef _DEBUG
	::OutputDebugString(L"NativePlugin::Hooked_CreateSwapChain called\n");

	if (pDesc)
	{
		swprintf_s(info, _countof(info), L"  Width: %d, Height: %d, Format: %d\n"
			, pDesc->BufferDesc.Width, pDesc->BufferDesc.Height, pDesc->BufferDesc.Format);
		::OutputDebugString(info);
	}
#endif

	// Run original call game is expecting.

	hr = pOrigCreateSwapChain(This, pDevice, pDesc, ppSwapChain);
	if (FAILED(hr))
		throw std::exception("Failed to create CreateSwapChain");


	// Using that fresh IDXGISwapChain, we can now hook the Present call, which 
	// is what we are really after.

	HookPresent(reinterpret_cast<ID3D11Device*>(pDevice), *ppSwapChain);

	return hr;
}


//-----------------------------------------------------------

// Here we want to start the daisy chain of hooking DX11 interfaces, to
// ultimately get access to IDXGISwapChain::Present
//
// The sequence a game will use is:
//   IDXGIFactory1* CreateDXGIFactory1();
//   IDXGIFactory1::CreateSwapChain(return pIDXGISwapChain);
//   pIDXGISwapChain->Present
//
// This hook call is called from the Deviare side, to continue the 
// daisy-chain to IDXGISwapChain::Present.

void HookCreateSwapChain(IDXGIFactory* dDXGIFactory)
{
	// This can be called multiple times by a game, so let's be sure to
	// only hook once.
	if (pOrigCreateSwapChain == nullptr && dDXGIFactory != nullptr)
	{
#ifdef _DEBUG 
		nktInProc.SetEnableDebugOutput(TRUE);
#endif

		// If we are here, we want to now hook the IDXGIFactory::CreateSwapChain
		// routine, as that will be the next thing the game does, and we
		// need access to the IDXGISwapChain.
		// This can't be done directly, because this is a vtable based API
		// call, not an export from a DLL, so we need to directly hook the 
		// address of the CreateSwapChain function. This is also why we are 
		// using In-Proc here.  Since we are using the CINTERFACE, we can 
		// just directly access the address.

		SIZE_T hook_id;
		DWORD dwOsErr = nktInProc.Hook(&hook_id, (void**)&pOrigCreateSwapChain,
			lpvtbl_CreateSwapChain(dDXGIFactory), Hooked_CreateSwapChain, 0);

		if (dwOsErr != S_OK)
			throw std::exception("Failed to hook IDXGIFactory1::CreateSwapChain");
	}
}


// Here we want to hook IDXGISwapChain::Present
//
// SwapChain can be created from D3D11 with CreateDeviceAndSwapChain, hence this 
// might be called implicitly from there.
//
// It is common code for both that path, and the direct path from CreateSwapChain.

void HookPresent(ID3D11Device* pDevice, IDXGISwapChain* pSwapChain)
{
	// This can be called multiple times by a game, so let's be sure to
	// only hook once.
	if (pOrigPresent == nullptr && pSwapChain != nullptr)
	{
#ifdef _DEBUG 
		nktInProc.SetEnableDebugOutput(TRUE);
#endif

		SIZE_T hook_id;
		DWORD dwOsErr;

		dwOsErr = nktInProc.Hook(&hook_id, (void**)&pOrigPresent,
			lpvtbl_Present(pSwapChain), Hooked_Present, 0);
		if (FAILED(dwOsErr))
			::OutputDebugStringA("Failed to hook IDXGISwapChain::Present\n");

		// ToDo: not needed, or is it more reliable?
		//hres = pSwapChain->GetDevice(__uuidof(ID3D11Device), (void**)&pDevice);
		//if (FAILED(hres))
		//	throw std::exception("Failed to GetDevice");

		NvAPI_Status res = NvAPI_Initialize();
		if (res != NVAPI_OK)
			throw std::exception("Failed to NvAPI_Initialize\n");

		// ToDo: need to handle stereo disabled...
		res = NvAPI_Stereo_CreateHandleFromIUnknown(pDevice, &gNVAPI);
		if (res != NVAPI_OK)
			throw std::exception("Failed to NvAPI_Stereo_CreateHandleFromIUnknown\n");
		

		// Now that we have a proper SwapChain from the game, let's also make a 
		// DX11 Texture2D, so that we can snapshot the game output.  This texture needs to
		// use the Shared flag, so that we can share it to another Device.  Because
		// these are all DX11 objects, the share will work.
		//
		// Make it exactly match the backbuffer, which ensures that the stereo copy
		// using ReverseStereoBlit will work.

		ID3D11Texture2D* backBuffer;
		D3D11_TEXTURE2D_DESC desc;
		HRESULT hr;

		hr = pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer);
		if (FAILED(hr))
			throw std::exception("Fail to get backbuffer");

		backBuffer->GetDesc(&desc);
		backBuffer->Release();

		desc.Width *= 2; // Double width texture for stereo.
		desc.BindFlags |= D3D11_BIND_SHADER_RESOURCE;	// Must add bind flag, so SRV can be created in Unity.
		desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;  // To be shared. maybe D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX is better

		hr = pDevice->CreateTexture2D(&desc, NULL, &gGameTexture);
		if (FAILED(hr))
			throw std::exception("Fail to create shared stereo Texture");

		// Now create the HANDLE which is used to share surfaces.  This follows the model from:
		// https://docs.microsoft.com/en-us/windows/desktop/api/d3d11/nf-d3d11-id3d11device-opensharedresource

		IDXGIResource* pDXGIResource = NULL;

		hr = gGameTexture->QueryInterface(__uuidof(IDXGIResource), (LPVOID*)&pDXGIResource);
		if (FAILED(hr))
			throw std::exception("Fail to QueryInterface on shared surface");

		hr = pDXGIResource->GetSharedHandle(&gGameSharedHandle);
		if (FAILED(hr))
			throw std::exception("Fail to GetSharedHandle");
		pDXGIResource->Release();
		if (gGameSharedHandle == nullptr)
			throw std::exception("Fail to create gGameSharedHandle");


		// Since we are doing setup here, also create a thread that will be used to copy
		// from the stereo game surface into the shared surface.  This way the game will
		// not stall while waiting for that copy.
		//
		// And the thread synchronization Event object. Signaled when we get fresh bits.
		// Starts in off state, thread active, so it should pause at launch.
		//ToDo: Not presently active
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
		// as this is CreateSwapChain.  Save the reference.
		// ToDo: Can't use Suspend/Resume, because the task switching time is too
		// high, like >16ms, which is must larger than we can use.
		//HANDLE thread = GetCurrentThread();
		//DuplicateHandle(GetCurrentProcess(), thread, GetCurrentProcess(), &gameThread, 0, TRUE, DUPLICATE_SAME_ACCESS);
	}
}
