// Example low level rendering Unity plugin

#include "PlatformBase.h"
#include "RenderAPI.h"

#include <windows.h>
#include <assert.h>
#include <math.h>
#include <vector>

#include <d3d11.h>

// --------------------------------------------------------------------------
// SetTimeFromUnity, an example function we export which is called by one of the scripts.

static float g_Time;

extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API SetTimeFromUnity (float t) 
{ 
	g_Time = t; 
}



// --------------------------------------------------------------------------
// SetTextureFromUnity, an example function we export which is called by one of the scripts.

static void* g_TextureHandle = NULL;
static int   g_TextureWidth = 0;
static int   g_TextureHeight = 0;

extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API SetTextureFromUnity(void* textureHandle, int w, int h)
{
	// A script calls this at initialization time; just remember the texture pointer here.
	// Will update texture pixels each frame from the plugin rendering event (texture update
	// needs to happen on the rendering thread).
	g_TextureHandle = textureHandle;
	g_TextureWidth = w;
	g_TextureHeight = h;

	//do
	//{
	//	Sleep(250);
	//} while (!IsDebuggerPresent());
	//__debugbreak();

}



// --------------------------------------------------------------------------
// UnitySetInterfaces

static void UNITY_INTERFACE_API OnGraphicsDeviceEvent(UnityGfxDeviceEventType eventType);

static IUnityInterfaces* s_UnityInterfaces = NULL;
static IUnityGraphics* s_Graphics = NULL;

extern "C" void	UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API UnityPluginLoad(IUnityInterfaces* unityInterfaces)
{
	s_UnityInterfaces = unityInterfaces;
	s_Graphics = s_UnityInterfaces->Get<IUnityGraphics>();
	s_Graphics->RegisterDeviceEventCallback(OnGraphicsDeviceEvent);
	
	// Run OnGraphicsDeviceEvent(initialize) manually on plugin load
	OnGraphicsDeviceEvent(kUnityGfxDeviceEventInitialize);
}

extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API UnityPluginUnload()
{
	s_Graphics->UnregisterDeviceEventCallback(OnGraphicsDeviceEvent);
}


// --------------------------------------------------------------------------
// GraphicsDeviceEvent


static RenderAPI* s_CurrentAPI = NULL;
static UnityGfxRenderer s_DeviceType = kUnityGfxRendererNull;


static void UNITY_INTERFACE_API OnGraphicsDeviceEvent(UnityGfxDeviceEventType eventType)
{
	// Create graphics API implementation upon initialization
	if (eventType == kUnityGfxDeviceEventInitialize)
	{
		assert(s_CurrentAPI == NULL);
		s_DeviceType = s_Graphics->GetRenderer();
		s_CurrentAPI = CreateRenderAPI(s_DeviceType);
	}

	// Let the implementation process the device related events
	if (s_CurrentAPI)
	{
		s_CurrentAPI->ProcessDeviceEvent(eventType, s_UnityInterfaces);
	}

	// Cleanup graphics API implementation upon shutdown
	if (eventType == kUnityGfxDeviceEventShutdown)
	{
		delete s_CurrentAPI;
		s_CurrentAPI = NULL;
		s_DeviceType = kUnityGfxRendererNull;
	}
}




// --------------------------------------------------------------------------

extern "C" UNITY_INTERFACE_EXPORT void UNITY_INTERFACE_API OpenLogFile(char* logFile)
{
	return s_CurrentAPI->OpenLogFile(logFile);
}
extern "C" UNITY_INTERFACE_EXPORT void UNITY_INTERFACE_API CloseLogFile()
{
	return s_CurrentAPI->CloseLogFile();
}

extern "C" UNITY_INTERFACE_EXPORT ID3D11ShaderResourceView* UNITY_INTERFACE_API CreateSharedTexture(HANDLE sharedHandle)
{
	return s_CurrentAPI->CreateSharedSurface(sharedHandle);
}


extern "C" UNITY_INTERFACE_EXPORT UINT UNITY_INTERFACE_API GetGameWidth()
{
	return s_CurrentAPI->GetGameWidth();
}
extern "C" UNITY_INTERFACE_EXPORT UINT UNITY_INTERFACE_API GetGameHeight()
{
	return s_CurrentAPI->GetGameHeight();
}
extern "C" UNITY_INTERFACE_EXPORT DXGI_FORMAT UNITY_INTERFACE_API GetGameFormat()
{
	return s_CurrentAPI->GetGameFormat();
}

extern "C" UNITY_INTERFACE_EXPORT void UNITY_INTERFACE_API CreateSetupMutex()
{
	return s_CurrentAPI->CreateSetupMutex();
}
extern "C" UNITY_INTERFACE_EXPORT bool UNITY_INTERFACE_API GrabSetupMutex()
{
	return s_CurrentAPI->GrabSetupMutex();
}
extern "C" UNITY_INTERFACE_EXPORT bool UNITY_INTERFACE_API ReleaseSetupMutex()
{
	return s_CurrentAPI->ReleaseSetupMutex();
}
extern "C" UNITY_INTERFACE_EXPORT void UNITY_INTERFACE_API DestroySetupMutex()
{
	return s_CurrentAPI->DestroySetupMutex();
}

static void ModifyTexturePixels()
{
	void* textureHandle = g_TextureHandle;
	int width = g_TextureWidth;
	int height = g_TextureHeight;
	if (!textureHandle)
		return;

	int textureRowPitch;
	void* textureDataPtr = s_CurrentAPI->BeginModifyTexture(textureHandle, width, height, &textureRowPitch);
	if (!textureDataPtr)
		return;

	const float t = g_Time * 4.0f;

	unsigned char* dst = (unsigned char*)textureDataPtr;
	for (int y = 0; y < height; ++y)
	{
		unsigned char* ptr = dst;
		for (int x = 0; x < width; ++x)
		{
			// Simple "plasma effect": several combined sine waves
			int vv = int(
				(127.0f + (127.0f * sinf(x / 7.0f + t))) +
				(127.0f + (127.0f * sinf(y / 5.0f - t))) +
				(127.0f + (127.0f * sinf((x + y) / 6.0f - t))) +
				(127.0f + (127.0f * sinf(sqrtf(float(x*x + y*y)) / 4.0f - t)))
				) / 4;

			// Write the texture pixel
			ptr[0] = vv;
			ptr[1] = vv;
			ptr[2] = vv;
			ptr[3] = vv;

			// To next pixel (our pixels are 4 bpp)
			ptr += 4;
		}

		// To next image row
		dst += textureRowPitch;
	}

	s_CurrentAPI->EndModifyTexture(textureHandle, width, height, textureRowPitch, textureDataPtr);
}



// --------------------------------------------------------------------------
// OnRenderEvent
// This will be called for GL.IssuePluginEvent script calls; eventID will
// be the integer passed to IssuePluginEvent. In this example, we just ignore
// that value.

static void UNITY_INTERFACE_API OnRenderEvent(int eventID)
{
	// Unknown / unsupported graphics device type? Do nothing
	if (s_CurrentAPI == NULL)
		return;

	ModifyTexturePixels();
}


// --------------------------------------------------------------------------
// GetRenderEventFunc, an example function we export which is used to get a rendering event callback function.

extern "C" UnityRenderingEvent UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API GetRenderEventFunc()
{
	return OnRenderEvent;
}


// --------------------------------------------------------------------------
// SelectGameDialog, to use windows select dialog to choose game exe.
//
// Modifies the input string to be the game path the user chooses.

extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API SelectGameDialog(wchar_t *filename, int len)
{
	OPENFILENAME ofn = { 0 };
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = NULL;  // If you have a window to center over, put its HANDLE here
	ofn.lpstrFilter = L".exe\0*.exe\0\0";
	ofn.lpstrFile = filename;	// Input filename, becomes primary return value.
	ofn.nMaxFile = len;
	ofn.lpstrTitle = L"Select a Game Exe to launch in Virtual 3D.";
	ofn.Flags = OFN_DONTADDTORECENT | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

	BOOL ok = GetOpenFileName(&ofn);
	if (!ok)
		*filename = NULL;			// empty string default for failures/cancel.
}


// --------------------------------------------------------------------------
// TriggerEvent, an example function we export which is used to trigger the Event
// object in the game process.  That will allow the drawing to continue at Present.

//extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API TriggerEvent(HANDLE eventHandle)
//{
//	BOOL res = SetEvent(eventHandle);
//	if (!res)
//		__debugbreak();
//}


