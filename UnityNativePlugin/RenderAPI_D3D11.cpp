#include "RenderAPI.h"
#include "PlatformBase.h"

// Direct3D 11 implementation of RenderAPI.

#if SUPPORT_D3D11

#include <assert.h>
#include <exception>
#include <d3d11_1.h>
#include "Unity/IUnityGraphicsD3D11.h"

#include <stdio.h>
#include <time.h>

// ----------------------------------------------------------------------
// Fatal error handling.  This is for scenarios that should never happen,
// but should be checked just for reliability and unforeseen scenarios.
//
// We tried using std::exception, but that was mostly useless because nearly every
// game has an exception handler of some form that wraps and silently kills any
// exceptions, which looked to the end-user as just a game-hang.  
//
// This attempt here is to put up a MessageBox with the informative error text
// and exit the game.  This should be better than simple logging, because at
// least the user gets immediate notification and does not have to sift around
// to find log files.  

void FatalExit(LPCWSTR errorString)
{
	MessageBox(NULL, errorString, L"Fatal Error", MB_OK);
	exit(1);
}


// ----------------------------------------------------------------------
class RenderAPI_D3D11 : public RenderAPI
{
public:
	RenderAPI_D3D11();
	virtual ~RenderAPI_D3D11() { }

	virtual void ProcessDeviceEvent(UnityGfxDeviceEventType type, IUnityInterfaces* interfaces);

	virtual bool GetUsesReverseZ() { return (int)m_Device->GetFeatureLevel() >= (int)D3D_FEATURE_LEVEL_10_0; }

	virtual void* BeginModifyTexture(void* textureHandle, int textureWidth, int textureHeight, int* outRowPitch);
	virtual void EndModifyTexture(void* textureHandle, int textureWidth, int textureHeight, int rowPitch, void* dataPtr);

	virtual ID3D11ShaderResourceView* CreateSharedSurface(HANDLE shared);
	virtual UINT GetGameWidth();
	virtual UINT GetGameHeight();
	virtual DXGI_FORMAT GetGameFormat();
	virtual void FindAndHookPresent();

private:
	void CreateResources();
	void ReleaseResources();

private:
	ID3D11Device* m_Device;
	ID3D11Buffer* m_VB; // vertex buffer
	ID3D11Buffer* m_CB; // constant buffer
	ID3D11VertexShader* m_VertexShader;
	ID3D11PixelShader* m_PixelShader;
	ID3D11InputLayout* m_InputLayout;
	ID3D11RasterizerState* m_RasterState;
	ID3D11BlendState* m_BlendState;
	ID3D11DepthStencilState* m_DepthState;

	// Width, Height, format of the game's backbuffer, once it makes it
	// here as a shared surface.  We will use this to pass back to Unity
	// so that it can make a matching Texture2D to match full resolution 
	// of the game.
	UINT gWidth;
	UINT gHeight;
	DXGI_FORMAT gFormat;

	//ID3D11Texture2D* m_SharedSurface;	// Same as DX9Ex surface
};


RenderAPI* CreateRenderAPI_D3D11()
{
	return new RenderAPI_D3D11();
}


// Simple compiled shader bytecode.
//
// Shader source that was used:
#if 0
cbuffer MyCB : register(b0)
{
	float4x4 worldMatrix;
}
void VS(float3 pos : POSITION, float4 color : COLOR, out float4 ocolor : COLOR, out float4 opos : SV_Position)
{
	opos = mul(worldMatrix, float4(pos, 1));
	ocolor = color;
}
float4 PS(float4 color : COLOR) : SV_TARGET
{
	return color;
}
#endif // #if 0
//
// Which then was compiled with:
// fxc /Tvs_4_0_level_9_3 /EVS source.hlsl /Fh outVS.h /Qstrip_reflect /Qstrip_debug /Qstrip_priv
// fxc /Tps_4_0_level_9_3 /EPS source.hlsl /Fh outPS.h /Qstrip_reflect /Qstrip_debug /Qstrip_priv
// and results pasted & formatted to take less lines here
const BYTE kVertexShaderCode[] =
{
	68,88,66,67,86,189,21,50,166,106,171,1,10,62,115,48,224,137,163,129,1,0,0,0,168,2,0,0,4,0,0,0,48,0,0,0,0,1,0,0,4,2,0,0,84,2,0,0,
	65,111,110,57,200,0,0,0,200,0,0,0,0,2,254,255,148,0,0,0,52,0,0,0,1,0,36,0,0,0,48,0,0,0,48,0,0,0,36,0,1,0,48,0,0,0,0,0,
	4,0,1,0,0,0,0,0,0,0,0,0,1,2,254,255,31,0,0,2,5,0,0,128,0,0,15,144,31,0,0,2,5,0,1,128,1,0,15,144,5,0,0,3,0,0,15,128,
	0,0,85,144,2,0,228,160,4,0,0,4,0,0,15,128,1,0,228,160,0,0,0,144,0,0,228,128,4,0,0,4,0,0,15,128,3,0,228,160,0,0,170,144,0,0,228,128,
	2,0,0,3,0,0,15,128,0,0,228,128,4,0,228,160,4,0,0,4,0,0,3,192,0,0,255,128,0,0,228,160,0,0,228,128,1,0,0,2,0,0,12,192,0,0,228,128,
	1,0,0,2,0,0,15,224,1,0,228,144,255,255,0,0,83,72,68,82,252,0,0,0,64,0,1,0,63,0,0,0,89,0,0,4,70,142,32,0,0,0,0,0,4,0,0,0,
	95,0,0,3,114,16,16,0,0,0,0,0,95,0,0,3,242,16,16,0,1,0,0,0,101,0,0,3,242,32,16,0,0,0,0,0,103,0,0,4,242,32,16,0,1,0,0,0,
	1,0,0,0,104,0,0,2,1,0,0,0,54,0,0,5,242,32,16,0,0,0,0,0,70,30,16,0,1,0,0,0,56,0,0,8,242,0,16,0,0,0,0,0,86,21,16,0,
	0,0,0,0,70,142,32,0,0,0,0,0,1,0,0,0,50,0,0,10,242,0,16,0,0,0,0,0,70,142,32,0,0,0,0,0,0,0,0,0,6,16,16,0,0,0,0,0,
	70,14,16,0,0,0,0,0,50,0,0,10,242,0,16,0,0,0,0,0,70,142,32,0,0,0,0,0,2,0,0,0,166,26,16,0,0,0,0,0,70,14,16,0,0,0,0,0,
	0,0,0,8,242,32,16,0,1,0,0,0,70,14,16,0,0,0,0,0,70,142,32,0,0,0,0,0,3,0,0,0,62,0,0,1,73,83,71,78,72,0,0,0,2,0,0,0,
	8,0,0,0,56,0,0,0,0,0,0,0,0,0,0,0,3,0,0,0,0,0,0,0,7,7,0,0,65,0,0,0,0,0,0,0,0,0,0,0,3,0,0,0,1,0,0,0,
	15,15,0,0,80,79,83,73,84,73,79,78,0,67,79,76,79,82,0,171,79,83,71,78,76,0,0,0,2,0,0,0,8,0,0,0,56,0,0,0,0,0,0,0,0,0,0,0,
	3,0,0,0,0,0,0,0,15,0,0,0,62,0,0,0,0,0,0,0,1,0,0,0,3,0,0,0,1,0,0,0,15,0,0,0,67,79,76,79,82,0,83,86,95,80,111,115,
	105,116,105,111,110,0,171,171
};
const BYTE kPixelShaderCode[]=
{
	68,88,66,67,196,65,213,199,14,78,29,150,87,236,231,156,203,125,244,112,1,0,0,0,32,1,0,0,4,0,0,0,48,0,0,0,124,0,0,0,188,0,0,0,236,0,0,0,
	65,111,110,57,68,0,0,0,68,0,0,0,0,2,255,255,32,0,0,0,36,0,0,0,0,0,36,0,0,0,36,0,0,0,36,0,0,0,36,0,0,0,36,0,1,2,255,255,
	31,0,0,2,0,0,0,128,0,0,15,176,1,0,0,2,0,8,15,128,0,0,228,176,255,255,0,0,83,72,68,82,56,0,0,0,64,0,0,0,14,0,0,0,98,16,0,3,
	242,16,16,0,0,0,0,0,101,0,0,3,242,32,16,0,0,0,0,0,54,0,0,5,242,32,16,0,0,0,0,0,70,30,16,0,0,0,0,0,62,0,0,1,73,83,71,78,
	40,0,0,0,1,0,0,0,8,0,0,0,32,0,0,0,0,0,0,0,0,0,0,0,3,0,0,0,0,0,0,0,15,15,0,0,67,79,76,79,82,0,171,171,79,83,71,78,
	44,0,0,0,1,0,0,0,8,0,0,0,32,0,0,0,0,0,0,0,0,0,0,0,3,0,0,0,0,0,0,0,15,0,0,0,83,86,95,84,65,82,71,69,84,0,171,171
};


RenderAPI_D3D11::RenderAPI_D3D11()
	: m_Device(NULL)
	, m_VB(NULL)
	, m_CB(NULL)
	, m_VertexShader(NULL)
	, m_PixelShader(NULL)
	, m_InputLayout(NULL)
	, m_RasterState(NULL)
	, m_BlendState(NULL)
	, m_DepthState(NULL)
{
}


void RenderAPI_D3D11::ProcessDeviceEvent(UnityGfxDeviceEventType type, IUnityInterfaces* interfaces)
{
	switch (type)
	{
	case kUnityGfxDeviceEventInitialize:
	{
		IUnityGraphicsD3D11* d3d = interfaces->Get<IUnityGraphicsD3D11>();
		m_Device = d3d->GetDevice();
		CreateResources();
		break;
	}
	case kUnityGfxDeviceEventShutdown:
		ReleaseResources();
		break;
	}
}


void RenderAPI_D3D11::CreateResources()
{
	D3D11_BUFFER_DESC desc;
	memset(&desc, 0, sizeof(desc));

	// vertex buffer
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.ByteWidth = 1024;
	desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	m_Device->CreateBuffer(&desc, NULL, &m_VB);

	// constant buffer
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.ByteWidth = 64; // hold 1 matrix
	desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	desc.CPUAccessFlags = 0;
	m_Device->CreateBuffer(&desc, NULL, &m_CB);

	// shaders
	HRESULT hr;
	hr = m_Device->CreateVertexShader(kVertexShaderCode, sizeof(kVertexShaderCode), nullptr, &m_VertexShader);
	if (FAILED(hr))
		OutputDebugStringA("Failed to create vertex shader.\n");
	hr = m_Device->CreatePixelShader(kPixelShaderCode, sizeof(kPixelShaderCode), nullptr, &m_PixelShader);
	if (FAILED(hr))
		OutputDebugStringA("Failed to create pixel shader.\n");

	// input layout
	if (m_VertexShader)
	{
		D3D11_INPUT_ELEMENT_DESC s_DX11InputElementDesc[] =
		{
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		};
		m_Device->CreateInputLayout(s_DX11InputElementDesc, 2, kVertexShaderCode, sizeof(kVertexShaderCode), &m_InputLayout);
	}

	// render states
	D3D11_RASTERIZER_DESC rsdesc;
	memset(&rsdesc, 0, sizeof(rsdesc));
	rsdesc.FillMode = D3D11_FILL_SOLID;
	rsdesc.CullMode = D3D11_CULL_NONE;
	rsdesc.DepthClipEnable = TRUE;
	m_Device->CreateRasterizerState(&rsdesc, &m_RasterState);

	D3D11_DEPTH_STENCIL_DESC dsdesc;
	memset(&dsdesc, 0, sizeof(dsdesc));
	dsdesc.DepthEnable = TRUE;
	dsdesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
	dsdesc.DepthFunc = GetUsesReverseZ() ? D3D11_COMPARISON_GREATER_EQUAL : D3D11_COMPARISON_LESS_EQUAL;
	m_Device->CreateDepthStencilState(&dsdesc, &m_DepthState);

	D3D11_BLEND_DESC bdesc;
	memset(&bdesc, 0, sizeof(bdesc));
	bdesc.RenderTarget[0].BlendEnable = FALSE;
	bdesc.RenderTarget[0].RenderTargetWriteMask = 0xF;
	m_Device->CreateBlendState(&bdesc, &m_BlendState);
}


void RenderAPI_D3D11::ReleaseResources()
{
	SAFE_RELEASE(m_VB);
	SAFE_RELEASE(m_CB);
	SAFE_RELEASE(m_VertexShader);
	SAFE_RELEASE(m_PixelShader);
	SAFE_RELEASE(m_InputLayout);
	SAFE_RELEASE(m_RasterState);
	SAFE_RELEASE(m_BlendState);
	SAFE_RELEASE(m_DepthState);
}


// Minor copy protection here, based on date.  If someone is using an old version,
// it needs to crash and burn so that they get a new version, or lose their stolen toy.
// Just check date and __debugbreak if it's overdue.  In Unity, this will look like a 
// crash at game launch, and not be particularly suspicious.
// Removed the call before Steam launch.

void CheckDate()
{
	time_t nowTime = time(nullptr);

	char buildDateString[100] = __DATE__;

	char s_month[5];
	int month, day, year;
	struct tm t = { 0 };
	static const char month_names[] = "JanFebMarAprMayJunJulAugSepOctNovDec";

	sscanf_s(buildDateString, "%s %d %d", s_month, (unsigned)_countof(s_month), &day, &year);

	month = (int)(strstr(month_names, s_month) - month_names) / 3;

	t.tm_mon = month;
	t.tm_mday = day;
	t.tm_year = year - 1900;
	t.tm_isdst = -1; 

	time_t buildTime = mktime(&t);

	// If the nowTime is greater than 20 days past buildTime, we'd like to 
	// crash and burn.  
	double deltaT = difftime(nowTime, buildTime);
	if (deltaT > 20 * 24 * 60 * 60)
		__debugbreak();
}


void* RenderAPI_D3D11::BeginModifyTexture(void* textureHandle, int textureWidth, int textureHeight, int* outRowPitch)
{
	const int rowPitch = textureWidth * 4;
	// Just allocate a system memory buffer here for simplicity
	unsigned char* data = new unsigned char[rowPitch * textureHeight];
	*outRowPitch = rowPitch;
	return data;
}


void RenderAPI_D3D11::EndModifyTexture(void* textureHandle, int textureWidth, int textureHeight, int rowPitch, void* dataPtr)
{
	ID3D11Texture2D* d3dtex = (ID3D11Texture2D*)textureHandle;
	assert(d3dtex);

	ID3D11DeviceContext* ctx = NULL;
	m_Device->GetImmediateContext(&ctx);
	// Update texture data, and free the memory buffer
	ctx->UpdateSubresource(d3dtex, 0, NULL, dataPtr, rowPitch, 0);
	delete[] (unsigned char*)dataPtr;
	ctx->Release();
}


UINT RenderAPI_D3D11::GetGameWidth()
{
	return gWidth;
}
UINT RenderAPI_D3D11::GetGameHeight()
{
	return gHeight;
}
DXGI_FORMAT RenderAPI_D3D11::GetGameFormat()
{
	return gFormat;
}


// Get the shared surface specified by the input HANDLE.  This will be from 
// the game side, in DX9Ex. This technique is specified in the documentation:
// https://msdn.microsoft.com/en-us/library/windows/desktop/ff476531(v=vs.85).aspx
// https://msdn.microsoft.com/en-us/library/windows/desktop/ee913554(v=vs.85).aspx

ID3D11ShaderResourceView* RenderAPI_D3D11::CreateSharedSurface(HANDLE shared)
{
	HRESULT hr;
	ID3D11Resource* resource;
	ID3D11Texture2D* texture;
	ID3D11ShaderResourceView* pSRView;

	if (shared == NULL)
		return nullptr;

	hr = m_Device->OpenSharedResource(shared, __uuidof(ID3D11Resource), (void**)(&resource));
	{
		if (FAILED(hr)) FatalExit(L"Failed to open shared.");

		if (resource == nullptr)
			return nullptr;

		// Even though the input shared surface is a RenderTarget Surface, this
		// Query for Texture still works.  Not sure if it is good or bad.
		hr = resource->QueryInterface(__uuidof(ID3D11Texture2D), (void**)(&texture));
		if (FAILED(hr)) FatalExit(L"Failed to QueryInterface of ID3D11Texture2D.");

		// By capturing the Width/Height/Format here, we can let Unity side
		// know what buffer to build to match.
		D3D11_TEXTURE2D_DESC tdesc;
		texture->GetDesc(&tdesc);
		gWidth = tdesc.Width;
		gHeight = tdesc.Height;
		gFormat = tdesc.Format;

#ifdef _DEBUG
		wchar_t info[512];
		swprintf_s(info, _countof(info),
			L"RenderAPI_D3D11::CreateSharedSurface - Width: %d, Height: %d, Format: %d\n",
			tdesc.Width, tdesc.Height, tdesc.Format);
		::OutputDebugString(info);
#endif 
	}
	resource->Release();

	// This is theoretically the exact same surface in the video card memory,
	// that the game's DX11 is using as the stereo shared surface. 
	//
	// Now we need to create a ShaderResourceView using this, because that
	// is what Unity requires for its CreateExternalTexture.
	//
	// No need to change description, we want it to be the same as what the game
	// specifies, so passing NULL to make it identical.

	hr = m_Device->CreateShaderResourceView(texture, NULL, &pSRView);
	if (FAILED(hr))	FatalExit(L"Failed to CreateShaderResourceView.");

	return pSRView;
}


extern "C" LPVOID lpvtbl_Present_DX11(IDXGISwapChain* pSwapChain);
extern "C" LPVOID lpvtbl_ResizeBuffers(IDXGISwapChain* pSwapChain);

// Here we want to hook IDXGISwapChain::Present
//
// SwapChain can be created from D3D11 with CreateDeviceAndSwapChain, hence this 
// might be called implicitly from there.
//
// It is common code for both that path, and the direct path from CreateSwapChain
// or CreateSwapChainForHwnd.

void HookPresent(IDXGISwapChain* pSwapChain)
{

#ifdef _DEBUG
	wchar_t info[512];

	::OutputDebugString(L"NativePlugin::HookPresent called. \n");

	LPVOID addrPresent = lpvtbl_Present_DX11(pSwapChain);

	swprintf_s(info, _countof(info), L"Looking for system d3d11.dll.  Present Address: %p \n",
		addrPresent);
	::OutputDebugString(info);
#endif


	// This can be called multiple times by a game, so let's be sure to
	// only hook once.
//	if (pOrigPresent == nullptr && pSwapChain != nullptr)
//	{
//#ifdef _DEBUG 
//		nktInProc.SetEnableDebugOutput(TRUE);
//#endif
//
//		SIZE_T hook_id;
//		DWORD dwOsErr;
//
//		dwOsErr = nktInProc.Hook(&hook_id, (void**)&pOrigPresent,
//			lpvtbl_Present_DX11(pSwapChain), Hooked_Present, 0);
//		if (FAILED(dwOsErr))
//			::OutputDebugStringA("Failed to hook IDXGISwapChain::Present\n");
//
//		dwOsErr = nktInProc.Hook(&hook_id, (void**)&pOrigResizeBuffers,
//			lpvtbl_ResizeBuffers(pSwapChain), Hooked_ResizeBuffers, 0);
//		if (FAILED(dwOsErr))
//			::OutputDebugStringA("Failed to hook IDXGISwapChain::ResizeBuffers\n");

		// Create Texture2D and HANDLE we'll use to share the stereo game bits across
		// the process boundary.

//		pDevice = CreateSharedTexture(pSwapChain);


		// Since we are doing setup here, also create a thread that will be used to copy
		// from the stereo game surface into the shared surface.  This way the game will
		// not stall while waiting for that copy.
		//
		// And the thread synchronization Event object. Signaled when we get fresh bits.
		// Starts in off state, thread active, so it should pause at launch.
		//ToDo: Not presently active
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
		// as this is CreateSwapChain.  Save the reference.
		// ToDo: Can't use Suspend/Resume, because the task switching time is too
		// high, like >16ms, which is must larger than we can use.
		//HANDLE thread = GetCurrentThread();
		//DuplicateHandle(GetCurrentProcess(), thread, GetCurrentProcess(), &gameThread, 0, TRUE, DUPLICATE_SAME_ACCESS);
	//}
}

#ifdef _UNICODE
# define KIERO_TEXT(text) L##text
#else
# define KIERO_TEXT(text) text
#endif

struct Status
{
	enum Enum
	{
		UnknownError = -1,
		NotSupportedError = -2,
		ModuleNotFoundError = -3,

		AlreadyInitializedError = -4,
		NotInitializedError = -5,

		Success = 0,
	};
};



// From: https://github.com/Rebzzel/kiero/blob/master/kiero.cpp
//
// Create a window, then a DX11 device and swapchain so we can then fetch the Present
// call Address.  It will be the same address for both this fake window and the game.
// 
// This is only for the DX11 games.  Should have no impact on them to hook the DX11
// Present call.

void RenderAPI_D3D11::FindAndHookPresent()
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

	HMODULE libD3D11;
	if ((libD3D11 = ::GetModuleHandle(KIERO_TEXT("d3d11.dll"))) == NULL)
	{
		::OutputDebugStringA("Failed to load d3d11.dll");
		::DestroyWindow(window);
		::UnregisterClass(windowClass.lpszClassName, windowClass.hInstance);
//		return Status::ModuleNotFoundError;
	}

#ifdef _DEBUG
	wchar_t info[512];

	::OutputDebugString(L"NativePlugin::FindAndHookPresent called. \n");

	swprintf_s(info, _countof(info), L"Looking for system d3d11.dll.  libD3D11: %p \n",
		libD3D11);
	::OutputDebugString(info);
#endif


	void* fnD3D11CreateDeviceAndSwapChain;
	if ((fnD3D11CreateDeviceAndSwapChain = ::GetProcAddress(libD3D11, "D3D11CreateDeviceAndSwapChain")) == NULL)
	{
		::OutputDebugStringA("Failed to GetProcAddress on D3D11CreateDeviceAndSwapChain");
		::DestroyWindow(window);
		::UnregisterClass(windowClass.lpszClassName, windowClass.hInstance);
//		return Status::UnknownError;
	}

	DXGI_RATIONAL refreshRate = { 0 };
	refreshRate.Numerator = 60;
	refreshRate.Denominator = 1;

	DXGI_MODE_DESC bufferDesc = { 0 };
	bufferDesc.Width = 100;
	bufferDesc.Height = 100;
	bufferDesc.RefreshRate = refreshRate;
	bufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	bufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
	bufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;

	DXGI_SAMPLE_DESC sampleDesc = { 0 };
	sampleDesc.Count = 1;
	sampleDesc.Quality = 0;

	DXGI_SWAP_CHAIN_DESC swapChainDesc = { 0 };
	swapChainDesc.BufferDesc = bufferDesc;
	swapChainDesc.SampleDesc = sampleDesc;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.BufferCount = 1;
	swapChainDesc.OutputWindow = window;
	swapChainDesc.Windowed = 1;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
	swapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

	IDXGISwapChain* swapChain;
	ID3D11Device* device;
	ID3D11DeviceContext* context;

	// This can fetch the 3Dmigoto dll
	// Which can lead to errors if the game d3dx.ini specifies returning errors.
	// Also seems to introduce a bug if we request only the swapchain.

	HRESULT hr = ((long(__stdcall*)(
		IDXGIAdapter*,
		D3D_DRIVER_TYPE,
		HMODULE,
		UINT,
		const D3D_FEATURE_LEVEL*,
		UINT,
		UINT,
		const DXGI_SWAP_CHAIN_DESC*,
		IDXGISwapChain**,
		ID3D11Device**,
		D3D_FEATURE_LEVEL*,
		ID3D11DeviceContext**))(fnD3D11CreateDeviceAndSwapChain))(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, NULL, D3D11_SDK_VERSION, &swapChainDesc, &swapChain, &device, NULL, &context);
	if (FAILED(hr))
	{
		::OutputDebugStringA("Failed call to D3D11CreateDeviceAndSwapChain");
		::DestroyWindow(window);
		::UnregisterClass(windowClass.lpszClassName, windowClass.hInstance);
//		return Status::UnknownError;
	}

	// At this point, it's created the Device and SwapChain, so now we can fetch the address of Present
	// from the SwapChain's vtable.

	HookPresent(swapChain);

	// Now release all the created objects, as they were just used to get us to the vtable.

	swapChain->Release();
	swapChain = NULL;

	device->Release();
	device = NULL;

	context->Release();
	context = NULL;

	::DestroyWindow(window);
	::UnregisterClass(windowClass.lpszClassName, windowClass.hInstance);

	::OutputDebugStringA("Successfully hooked DXGI::Present");

//	return Status::Success;
}


#endif // #if SUPPORT_D3D11

