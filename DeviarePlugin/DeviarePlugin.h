#pragma once

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
#include <stdio.h>

#ifdef _DEBUG
	#define D3D_DEBUG_INFO
#endif

#include <d3d9.h>
#include <d3d11_1.h>

#include <exception>

#include "NktHookLib.h"
#include "nvapi\nvapi.h"


//-----------------------------------------------------------
// Careful with this header file.  It's used for three separate
// compilation units, InProc_DX9, InProc_DX11, and DeviarePlugin.
// Anything declared here can conflict or be lost from the other units.

// Interface to InProc side

// DX9 - InProc_DX9.cpp
void HookDirect3DCreate9();
void HookCreateDevice(IDirect3D9Ex* pDX9Ex);
void CreateSharedRenderTarget(D3DPRESENT_PARAMETERS * pPresentationParameters, HRESULT &res, IDirect3DDevice9 * pDevice9);
// DX11 - InProc_DX11.cpp
void HookCreateSwapChain(IDXGIFactory* dDXGIFactory);
void HookCreateSwapChainForHwnd(IDXGIFactory2* dDXGIFactory);
void HookPresent(IDXGISwapChain* pSwapChain);
void HookResizeBuffers(IDXGISwapChain* pSwapChain);


// These need to be declared as extern "C" so that the names are not mangled.
// They are coming from the straight C compilation unit.  All these are 
// implemented in the straight c file Addresses.c

// DX9
extern "C" LPVOID lpvtbl_CreateDevice(IDirect3D9* pDX9);
extern "C" LPVOID lpvtbl_CreateTexture(IDirect3DDevice9* pDX9Device);
extern "C" LPVOID lpvtbl_CreateCubeTexture(IDirect3DDevice9* pDX9Device);
extern "C" LPVOID lpvtbl_CreateVertexBuffer(IDirect3DDevice9* pDX9Device);
extern "C" LPVOID lpvtbl_CreateIndexBuffer(IDirect3DDevice9* pDX9Device);
extern "C" LPVOID lpvtbl_DrawIndexedPrimitiveUP(IDirect3DDevice9* pDX9Device);

extern "C" LPVOID lpvtbl_Present_DX9(IDirect3DDevice9* pDX9Device);
extern "C" LPVOID lpvtbl_Reset(IDirect3DDevice9* pDX9Device);

// DX11
extern "C" LPVOID lpvtbl_CreateSwapChain(IDXGIFactory* dDXGIFactory);
extern "C" LPVOID lpvtbl_CreateSwapChainForHwnd(IDXGIFactory2* dDXGIFactory);

extern "C" LPVOID lpvtbl_Present_DX11(IDXGISwapChain* pSwapChain);
extern "C" LPVOID lpvtbl_ResizeBuffers(IDXGISwapChain* pSwapChain);


//-----------------------------------------------------------
// These are the global variables used by the Inproc sections.
// Used for similar, but not identical purposes in DX9 and DX11
// variants.  But, only one will ever be active at a given time,
// because we only hook one game.

// Variables used as globals for each side, declared here, defined
// in DeviarePlugin as the owner.
extern CNktHookLib nktInProc;
extern StereoHandle gNVAPI;
extern HANDLE gGameSharedHandle;

extern "C" HANDLE WINAPI GetSharedHandle(int* in);
