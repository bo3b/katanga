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

#include <exception>


void HookCreateDevice(IDirect3D9Ex* pDX9Ex);


// These need to be declared as extern "C" so that the names are not mangled.
// They are coming from the straight C compilation unit.

extern "C" LPVOID lpvtbl_CreateDevice(IDirect3D9* pDX9);

extern "C" LPVOID lpvtbl_Present(IDirect3DDevice9* pDX9Device);

extern "C" LPVOID lpvtbl_CreateTexture(IDirect3DDevice9* pDX9Device);

extern "C" LPVOID lpvtbl_CreateCubeTexture(IDirect3DDevice9* pDX9Device);

extern "C" LPVOID lpvtbl_CreateVertexBuffer(IDirect3DDevice9* pDX9Device);

extern "C" LPVOID lpvtbl_CreateIndexBuffer(IDirect3DDevice9* pDX9Device);

extern "C" LPVOID lpvtbl_DrawIndexedPrimitiveUP(IDirect3DDevice9* pDX9Device);
