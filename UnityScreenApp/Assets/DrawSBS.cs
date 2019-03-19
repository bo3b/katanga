using System;
using System.Collections;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.IO;
using System.Text;
using System.Threading;

using UnityEngine;
using UnityEngine.XR;
using UnityEngine.UI;

using Nektra.Deviare2;
using System.Diagnostics;


public class DrawSBS : MonoBehaviour
{
    static NktSpyMgr _spyMgr;
    static NktProcess _gameProcess;
    static string _nativeDLLName;

    // Primary Texture received from game as shared ID3D11ShaderResourceView
    // It automatically updates as the injected DLL copies the bits into the
    // shared resource.
    Texture2D _bothEyes;

    //    System.Int32 _gameEventSignal = 0;
    static int ResetEvent = 0;
    static int SetEvent = 1;

    // -----------------------------------------------------------------------------

    [DllImport("UnityNativePlugin64")]
    static extern void SelectGameDialog([MarshalAs(UnmanagedType.LPWStr)] StringBuilder unicodeFileName, int len);

    void Start()
    {
        int hresult;
        object continueevent;
        string drawSBS_directory = Environment.CurrentDirectory;
        _nativeDLLName = Application.dataPath + "/Plugins/DeviarePlugin64.dll";

        //string game = @"W:\SteamLibrary\steamapps\common\Alien Isolation\ai.exe";
        //string game = @"W:\Games\Opus Magnum\Lightning.exe";
        string game = @"W:\Games\The Witcher 3 Wild Hunt\bin\x64\witcher3.exe";

        //  string game = @"W:\Games\The Ball\Binaries\Win32\theball.exe";

        print("Running: " + game + "\n");
        
        // Ask user to select the game to run in virtual 3D.  
        // If they hold Ctrl at launch. Test for ctrl is in C++,
        // because Unity does not support GetKey until Update.

        int MAX_PATH = 260;
        StringBuilder sb = new StringBuilder("arf arf arf", MAX_PATH);
        SelectGameDialog(sb, sb.Capacity);
        Directory.SetCurrentDirectory(drawSBS_directory);

        if (sb.Length != 0)
            game = sb.ToString();


        string wd = System.IO.Directory.GetCurrentDirectory();
        print("WorkingDirectory: " + wd);
        print("CurrentDirectory: " + drawSBS_directory);

        //print("App Directory:" + Environment.CurrentDirectory);
        //foreach (var path in Directory.GetFileSystemEntries(Environment.CurrentDirectory))
        //    print(System.IO.Path.GetFileName(path)); // file name
        //foreach (var path in Directory.GetFileSystemEntries(Environment.CurrentDirectory + "\\Assets\\Plugins\\"))
        //    print(System.IO.Path.GetFileName(path)); // file name

        _spyMgr = new NktSpyMgr();
        hresult = _spyMgr.Initialize();
        if (hresult != 0)
            throw new Exception("Deviare initialization error.");
#if DEBUG
        _spyMgr.SettingOverride("SpyMgrDebugLevelMask", 0x2FF8);
       // _spyMgr.SettingOverride("SpyMgrAgentLevelMask", 0x040);
#endif
        print("Successful SpyMgr Init");


        // We must set the game directory specifically, otherwise it winds up being the 
        // C# app directory which can make the game crash.  This must be done before CreateProcess.
        // This also changes the working directory, which will break Deviare's ability to find
        // the NativePlugin, so we'll use full path descriptions for the DLL load.
        // This must be reset back to the Unity game directory, otherwise Unity will
        // crash with a fatal error.

        Directory.SetCurrentDirectory(Path.GetDirectoryName(game));
        {
            // Launch the game, but suspended, so we can hook our first call and be certain to catch it.

            print("Launching: " + game + "...");
            _gameProcess = _spyMgr.CreateProcess(game, true, out continueevent);
            if (_gameProcess == null)
                throw new Exception("Game launch failed.");


            // Load the NativePlugin for the C++ side.  The NativePlugin must be in this app folder.
            // The Agent supports the use of Deviare in the CustomDLL, but does not respond to hooks.

            print("Load NativePlugin... " + _nativeDLLName);
            _spyMgr.LoadAgent(_gameProcess);
            int result = _spyMgr.LoadCustomDll(_gameProcess, _nativeDLLName, false, true);
            if (result != 1)
                throw new Exception("Could not load NativePlugin DLL.");

            // Hook the primary DX11 creation call of CreateDXGIFactory1, which is a direct export of 
            // the dxgi DLL.  All DX11 games must call this interface, or possibly CreateDeviceAndSwapChain.

            print("Hook the D3D11.DLL!D3D11CreateDevice...");
            NktHook d3dHook = _spyMgr.CreateHook("D3D11.DLL!D3D11CreateDevice", (int)eNktHookFlags.flgOnlyPostCall);
            if (d3dHook == null)
                throw new Exception("Failed to hook D3D11.DLL!D3D11CreateDevice");

            print("Hook the D3D11.DLL!D3D11CreateDeviceAndSwapChain...");
            NktHook d3dHook1 = _spyMgr.CreateHook("D3D11.DLL!D3D11CreateDeviceAndSwapChain", 0); // (int)eNktHookFlags.flgOnlyPostCall);
            if (d3dHook1 == null)
                throw new Exception("Failed to hook D3D11.DLL!D3D11CreateDeviceAndSwapChain");

            print("Hook the DXGI.DLL!CreateDXGIFactory...");
            NktHook dxgiHook = _spyMgr.CreateHook("DXGI.DLL!CreateDXGIFactory", (int)eNktHookFlags.flgOnlyPostCall);
            if (dxgiHook == null)
                throw new Exception("Failed to hook DXGI.DLL!CreateDXGIFactory");

            print("Hook the DXGI.DLL!CreateDXGIFactory1...");
            NktHook dxgiHook1 = _spyMgr.CreateHook("DXGI.DLL!CreateDXGIFactory1",    0); // (int)eNktHookFlags.flgOnlyPostCall);
            if (dxgiHook1 == null)
                throw new Exception("Failed to hook DXGI.DLL!CreateDXGIFactory1");


            // Make sure the CustomHandler in the NativePlugin at OnFunctionCall gets called when this 
            // object is created. At that point, the native code will take over.

            d3dHook.AddCustomHandler(_nativeDLLName, 0, "");
            d3dHook1.AddCustomHandler(_nativeDLLName, 0, "");
            dxgiHook.AddCustomHandler(_nativeDLLName, 0, "");
            dxgiHook1.AddCustomHandler(_nativeDLLName, 0, "");

            // Finally attach and activate the hook in the still suspended game process.

            d3dHook.Attach(_gameProcess, true);
            d3dHook.Hook(true);
            d3dHook1.Attach(_gameProcess, true);
            d3dHook1.Hook(true);
            dxgiHook.Attach(_gameProcess, true);
            dxgiHook.Hook(true);
            dxgiHook1.Attach(_gameProcess, true);
            dxgiHook1.Hook(true);


            // Ready to go.  Let the game startup.  When it calls Direct3DCreate9, we'll be
            // called in the NativePlugin::OnFunctionCall

            print("Continue game launch...");
            _spyMgr.ResumeProcess(_gameProcess, continueevent);
        }
        Directory.SetCurrentDirectory(drawSBS_directory);

        print("Restored Working Directory to: " + drawSBS_directory);


        // We've gotten everything launched, hooked, and setup.  Now we need to wait for the
        // game to call through to CreateDevice, so that we can create the shared surface.
        // Let's yield until that happens.

        StartCoroutine("WaitForSharedSurface");
    }


    // Our x64 Native DLL allows us direct access to DX11 in order to take
    // the shared handle and turn it into a ID3D11ShaderResourceView for Unity.
    [DllImport("UnityNativePlugin64")]
    private static extern IntPtr CreateSharedTexture(int sharedHandle);

    // WaitForSharedSurface will just wait until the CreateDevice has been called in 
    // DeviarePlugin, and thus we have created a shared surface for copying game bits into.
    // This is asynchronous because it's in the game world, and we don't know when
    // it will happen.
    //
    // Once the GetSharedSurface returns with non-null, we are ready to continue
    // with the VR side of showing those bits.

    private IEnumerator WaitForSharedSurface()
    {
        System.Int32 gameSharedHandle = 0;

        while (gameSharedHandle == 0)
        {
            // Check-in every 200ms.
            yield return new WaitForSecondsRealtime(0.2f);

            print("... WaitForSharedSurface");

            // ToDo: To work, we need to pass in a parameter? 
            // This will call to DeviarePlugin native DLL routine to fetch current gGameSurfaceShare HANDLE.
            System.Int32 native = 0; // (int)_tex.GetNativeTexturePtr();
            object parm = native;
            gameSharedHandle = _spyMgr.CallCustomApi(_gameProcess, _nativeDLLName, "GetSharedHandle", ref parm, true);
        }

        // We finally have a valid gGameSurfaceShare as a DX11 HANDLE.  
        // We can thus finish up the init.

        print("-> Got shared handle: " + gameSharedHandle.ToString("x"));


        // Call into the x64 UnityNativePlugin DLL for DX11 access, in order to create a ID3D11ShaderResourceView.
        // You'd expect this to be a IDX11Texture2D, but that's not what Unity wants.

        IntPtr shared = CreateSharedTexture(gameSharedHandle);

        // This is the Unity Texture2D, double width texture, with right eye on the left half.
        // It will always be up to date with latest game image, because we pass in 'shared'.

        _bothEyes = Texture2D.CreateExternalTexture(3200, 900, TextureFormat.BGRA32, false, true, shared);
        //_bothEyes = new Texture2D(3200, 900, TextureFormat.BGRA32, false, true);

        print("..eyes width: " + _bothEyes.width + " height: " + _bothEyes.height + " format: " + _bothEyes.format);


        // This is the primary Material for the Quad used for the virtual TV.
        // Assigning the 2x width _bothEyes texture to it means it always has valid
        // game bits.  The custom sbsShader for the material takes care of 
        // showing the correct half for each eye.

        GetComponent<Renderer>().material.mainTexture = _bothEyes;


        // These are test Quads, and will be removed.  One for each eye.
        Material leftMat = GameObject.Find("left").GetComponent<Renderer>().material;
        leftMat.mainTexture = _bothEyes;
        Material rightMat = GameObject.Find("right").GetComponent<Renderer>().material;
        rightMat.mainTexture = _bothEyes;

        // Using same primary 2x width shared texture, specify which half is used.
        leftMat.mainTextureScale = new Vector2(0.5f, 1.0f);
        leftMat.mainTextureOffset = new Vector2(0.5f, 0);
        rightMat.mainTextureScale = new Vector2(0.5f, 1.0f);
        rightMat.mainTextureOffset = new Vector2(0.0f, 0);


        // ToDo: To work, we need to pass in a parameter? 
        // This will call to DeviarePlugin native DLL routine to fetch current gGameSurfaceShare HANDLE.
        //System.Int32 dummy = 0; // (int)_tex.GetNativeTexturePtr();
        //object deviare = dummy;
        //_gameEventSignal = _spyMgr.CallCustomApi(_gameProcess, _nativeDLLName, "GetEventHandle", ref deviare, true);


        //StartCoroutine("SyncAtStartOfFrame");
        //StartCoroutine("SyncAtEndofFrame");

        yield return null;
    }


    // -----------------------------------------------------------------------------
    // Wait for the EndOfFrame, and then trigger the sync Event to allow
    // the game to continue.  Will use the _gameEventSignal from the NativePlugin
    // to trigger the Event which was actually created in the game process.
    // _gameEventSignal is a HANDLE from x32 game.
    //
    // WaitForFixedUpdate happens right at the start of next frame.
    // The goal here is to sync up the game with the VR state.  VR state needs
    // to take precedence, and is running at 90 Hz.  As long as the game can
    // maintain better than that rate, we can delay the game each frame to keep 
    // them in sync. If the game cannot keep that rate, it will drop to 1/2
    // rate at 45 Hz. Not as good, but acceptable.
    //
    // At end of frame, stall the game draw calls with TriggerEvent.

    long startTime = Stopwatch.GetTimestamp();

    // At start of frame, immediately after we've presented in VR, 
    // restart the game app.
    private IEnumerator SyncAtStartOfFrame()
    {
        int callcount = 0;
        print("SyncAtStartOfFrame, first call: " + startTime.ToString());

        while (true)
        {
            // yield, will run again after Update.  
            // This is super early in the frame, measurement shows maybe 
            // 0.5ms after start.  
            yield return null;

            callcount += 1;

            // Here at very early in frame, allow game to carry on.
            System.Int32 dummy = SetEvent;
            object deviare = dummy;
            _spyMgr.CallCustomApi(_gameProcess, _nativeDLLName, "TriggerEvent", ref deviare, false);


            long nowTime = Stopwatch.GetTimestamp();

            // print every 30 frames 
            if ((callcount % 30) == 0)
            {
                long elapsedTime = nowTime - startTime;
                double elapsedMS = elapsedTime * (1000.0 / Stopwatch.Frequency);
                print("SyncAtStartOfFrame: " + elapsedMS.ToString("F1"));
            }

            startTime = nowTime;

            // Since this is another thread as a coroutine, we won't block the main
            // drawing thread from doing its thing.
            // Wait here by CPU spin, for 9ms, close to end of frame, before we
            // pause the running game.  
            // The CPU spin here is necessary, no normal waits or sleeps on Windows
            // can do anything faster than about 16ms, which is way to slow for VR.
            // Burning one CPU core for this is not a big deal.

            double waited;
            do
            {
                waited = Stopwatch.GetTimestamp() - startTime;
                waited *= (1000.0 / Stopwatch.Frequency);
                //if ((callcount % 30) == 0)
                //{
                //    print("waiting: " + waited.ToString("F1"));
                //}
            } while (waited < 3.0);


            // Now at close to the end of each VR frame, tell game to pause.
            dummy = ResetEvent;
            deviare = dummy;
            _spyMgr.CallCustomApi(_gameProcess, _nativeDLLName, "TriggerEvent", ref deviare, false);
        }
    }

    private IEnumerator SyncAtEndofFrame()
    {
        int callcount = 0;
        long firstTime = Stopwatch.GetTimestamp();
        print("SyncAtEndofFrame, first call: " + firstTime.ToString());

        while (true)
        {
            yield return new WaitForEndOfFrame();

            // print every 30 frames 
            if ((callcount % 30) == 0)
            {
                long nowTime = Stopwatch.GetTimestamp();
                long elapsedTime = nowTime - startTime;
                double elapsedMS = elapsedTime * (1000.0 / Stopwatch.Frequency);
                print("SyncAtEndofFrame: " + elapsedMS.ToString("F1"));
            }

            //TriggerEvent(_gameEventSignal);        

            System.Int32 dummy = 0;  // ResetEvent
            object deviare = dummy;
            _spyMgr.CallCustomApi(_gameProcess, _nativeDLLName, "TriggerEvent", ref deviare, false);
        }
    }

    // -----------------------------------------------------------------------------
    //private IEnumerator UpdateFPS()
    //{
    //    TextMesh rate = GameObject.Find("rate").GetComponent<TextMesh>();

    //    while (true)
    //    {
    //        yield return new WaitForSecondsRealtime(0.2f);

    //        float gpuTime;
    //        if (XRStats.TryGetGPUTimeLastFrame(out gpuTime))
    //        {
    //            // At 90 fps, we want to know the % of a single VR frame we are using.
    //            //    gpuTime = gpuTime / ((1f / 90f) * 1000f) * 100f;
    //            rate.text = System.String.Format("{0:F1} ms", gpuTime);
    //        }
    //    }
    //}


    // -----------------------------------------------------------------------------
    // Update is called once per frame, before rendering. Great diagram:
    // https://docs.unity3d.com/Manual/ExecutionOrder.html
    // Update is much slower than coroutines.  Unless it's required for VR, skip it.

    void Update()
    {
        if (Time.frameCount % 30 == 0)
            System.GC.Collect();

        if (Input.GetKey("escape"))
            Application.Quit();

        // The triangle from camera to quad edges is setup as
        //  camera: 0, 1, 0
        //  quad:   0, 2.75, 5
        // So the distance to screen is h=5, and width is w=8.
        // Triangle calculator says the inner angle and corner angle is thus
        //  1.349 rad  0.896 rad
        // h=w/2*tan(corner) => w=h*2/tan(corner) 

        double h, w;
        float width;
        if (Input.GetAxis("Mouse ScrollWheel") < 0)
        {
            this.transform.position += Vector3.back;
            h = this.transform.position.z;
            w = h * 2 / Math.Tan(0.896);
            width = (float)w;
            this.transform.localScale = new Vector3(width, -width * 9 / 16, 1);
        }
        if (Input.GetAxis("Mouse ScrollWheel") > 0)
        {
            this.transform.position += Vector3.forward;
            h = this.transform.position.z;
            w = h * 2 / Math.Tan(0.896);
            width = (float)w;
            this.transform.localScale = new Vector3(width, -width * 9 / 16, 1);
        }
    }
}

//// Sierpinksky triangles for a default view, shows if other updates fail.
//for (int y = 0; y < _tex.height; y++)
//{
//    for (int x = 0; x < _tex.width; x++)
//    {
//        Color color = ((x & y) != 0 ? Color.white : Color.grey);
//        _tex.SetPixel(x, y, color);
//    }
//}
//// Call Apply() so it's actually uploaded to the GPU
//_tex.Apply();

