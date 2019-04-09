using System;
using System.Collections;
using System.Runtime.InteropServices;
using System.IO;
using System.Text;

using UnityEngine;
using UnityEngine.UI;

using Nektra.Deviare2;
using System.Diagnostics;

public class DrawSBS : MonoBehaviour
{
    string katanga_directory;
    string gameToLaunch;

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

    public Text infoText;

    // -----------------------------------------------------------------------------

    // We jump out to the native C++ to open the file selection box.  There might be a
    // way to do it here in Unity, but the Mono runtime is old and creaky, and does not
    // support modern .Net, so I'm leaving it over there in C++ land.
    [DllImport("UnityNativePlugin64")]
    static extern void SelectGameDialog([MarshalAs(UnmanagedType.LPWStr)] StringBuilder unicodeFileName, int len);

    // The very first thing that happens for the app.
    private void Awake()
    {
        // Set our FatalExit as the handler for exceptions so that we get usable 
        // information from the users.
        Application.logMessageReceived += FatalExit;

        // We need to save and restore to this Katanga directory, or Unity editor gets super mad.
        katanga_directory = Environment.CurrentDirectory;


        // Check if the CmdLine arguments include a game path to be launched.
        // We are using --game-path to make it clearly different than Unity arguments.
        var args = System.Environment.GetCommandLineArgs();
        for (int i = 0; i < args.Length; i++)
        {
            print(args[i]);
            if (args[i] == "--game-path")
                gameToLaunch = args[i + 1];
        }

        // If they didn't pass a --game-path argument, then bring up the GetOpenFileName
        // dialog to let them choose.
        if (String.IsNullOrEmpty(gameToLaunch))
        {
            // Ask user to select the game to run in virtual 3D.  
            // We are doing this super early because there are scenarios where Unity
            // has been crashing out because the working directory changes in GetOpenFileName.

            int MAX_PATH = 260;
            StringBuilder sb = new StringBuilder("", MAX_PATH);
            SelectGameDialog(sb, sb.Capacity);

            if (sb.Length != 0)
                gameToLaunch = sb.ToString();
        }

        if (String.IsNullOrEmpty(gameToLaunch))
            throw new Exception("No game specified to launch.");

        // With the game properly selected, add name to the big screen as info on launch.
        string gameName = gameToLaunch.Substring(gameToLaunch.LastIndexOf('\\') + 1);
        infoText.text = "Launching...\n" + gameName;
    }

    // -----------------------------------------------------------------------------

    void Start()
    {
        int hresult;
        object continueevent;

        print("Running: " + gameToLaunch + "\n");

        // ToDo: only do this when we also have a recenter key/button.
        //  This makes floor move to wherever it starts.
        // Let's recenter around wherever the headset is pointing. Seems to be the model
        // that people are expecting, instead of the facing forward based on room setup.
        //UnityEngine.XR.XRDevice.SetTrackingSpaceType(UnityEngine.XR.TrackingSpaceType.Stationary);
        //UnityEngine.XR.InputTracking.Recenter();



        string wd = System.IO.Directory.GetCurrentDirectory();
        print("WorkingDirectory: " + wd);
        print("CurrentDirectory: " + katanga_directory);

        //print("App Directory:" + Environment.CurrentDirectory);
        //foreach (var path in Directory.GetFileSystemEntries(Environment.CurrentDirectory))
        //    print(System.IO.Path.GetFileName(path)); // file name
        //foreach (var path in Directory.GetFileSystemEntries(Environment.CurrentDirectory + "\\Assets\\Plugins\\"))
        //    print(System.IO.Path.GetFileName(path)); // file name

        _spyMgr = new NktSpyMgr();
        hresult = _spyMgr.Initialize();
        if (hresult != 0)
            throw new Exception("Deviare Initialize error.");
#if _DEBUG
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

        Directory.SetCurrentDirectory(Path.GetDirectoryName(gameToLaunch));
        {
            // Launch the game, but suspended, so we can hook our first call and be certain to catch it.

            print("Launching: " + gameToLaunch + "...");
            _gameProcess = _spyMgr.CreateProcess(gameToLaunch, true, out continueevent);
            if (_gameProcess == null)
                throw new Exception("CreateProcess game launch failed: " + gameToLaunch);


            // Load the NativePlugin for the C++ side.  The NativePlugin must be in this app folder.
            // The Agent supports the use of Deviare in the CustomDLL, but does not respond to hooks.
            //
            // The native DeviarePlugin has two versions, one for x32, one for x64, so we can handle
            // either x32 or x64 games.

            _spyMgr.LoadAgent(_gameProcess);

            if (_gameProcess.PlatformBits == 64)
                _nativeDLLName = Application.dataPath + "/Plugins/DeviarePlugin64.dll";
            else
                _nativeDLLName = Application.dataPath + "/Plugins/DeviarePlugin.dll";

            int loadResult = _spyMgr.LoadCustomDll(_gameProcess, _nativeDLLName, false, true);
            if (loadResult <= 0)
            {
                int lastHR = GetLastDeviareError();
                string deadbeef = String.Format("Could not load {0}: 0x{1:X}", _nativeDLLName, lastHR);
                throw new Exception(deadbeef);
            }

            print(String.Format("Successfully loaded {0}", _nativeDLLName));


            // Hook the primary DX11 creation calls of CreateDevice, CreateDeviceAndSwapChain,
            // CreateDXGIFactory, and CreateDXGIFactory1.  These are all direct exports for either
            // D3D11.dll, or DXGI.dll. All DX11 games must call one of these interfaces to 
            // create a SwapChain.  These must be spelled exactly right, including Case.

            print("Hook the D3D11.DLL!D3D11CreateDevice...");
            NktHook deviceHook = _spyMgr.CreateHook("D3D11.DLL!D3D11CreateDevice", 0);
            if (deviceHook == null)
                throw new Exception("Failed to hook D3D11.DLL!D3D11CreateDevice");

            print("Hook the D3D11.DLL!D3D11CreateDeviceAndSwapChain...");
            NktHook deviceAndSwapChainHook = _spyMgr.CreateHook("D3D11.DLL!D3D11CreateDeviceAndSwapChain", 0);
            if (deviceAndSwapChainHook == null)
                throw new Exception("Failed to hook D3D11.DLL!D3D11CreateDeviceAndSwapChain");

            print("Hook the DXGI.DLL!CreateDXGIFactory...");
            NktHook factoryHook = _spyMgr.CreateHook("DXGI.DLL!CreateDXGIFactory", (int)eNktHookFlags.flgOnlyPostCall);
            if (factoryHook == null)
                throw new Exception("Failed to hook DXGI.DLL!CreateDXGIFactory");

            print("Hook the DXGI.DLL!CreateDXGIFactory1...");
            NktHook factory1Hook = _spyMgr.CreateHook("DXGI.DLL!CreateDXGIFactory1", (int)eNktHookFlags.flgOnlyPostCall);
            if (factory1Hook == null)
                throw new Exception("Failed to hook DXGI.DLL!CreateDXGIFactory1");


            // Make sure the CustomHandler in the NativePlugin at OnFunctionCall gets called when this 
            // object is created. At that point, the native code will take over.

            deviceHook.AddCustomHandler(_nativeDLLName, 0, "");
            deviceAndSwapChainHook.AddCustomHandler(_nativeDLLName, 0, "");
            factoryHook.AddCustomHandler(_nativeDLLName, 0, "");
            factory1Hook.AddCustomHandler(_nativeDLLName, 0, "");

            // Finally attach and activate the hook in the still suspended game process.

            deviceHook.Attach(_gameProcess, true);
            deviceHook.Hook(true);
            deviceAndSwapChainHook.Attach(_gameProcess, true);
            deviceAndSwapChainHook.Hook(true);
            factoryHook.Attach(_gameProcess, true);
            factoryHook.Hook(true);
            factory1Hook.Attach(_gameProcess, true);
            factory1Hook.Hook(true);


            // Ready to go.  Let the game startup.  When it calls Direct3DCreate9, we'll be
            // called in the NativePlugin::OnFunctionCall

            print("Continue game launch...");
            _spyMgr.ResumeProcess(_gameProcess, continueevent);
        }
        Directory.SetCurrentDirectory(katanga_directory);

        print("Restored Working Directory to: " + katanga_directory);


        // We've gotten everything launched, hooked, and setup.  Now we need to wait for the
        // game to call through to CreateDevice, so that we can create the shared surface.
        // Let's yield until that happens.

        StartCoroutine("WaitForSharedSurface");
    }


    // Our x64 Native DLL allows us direct access to DX11 in order to take
    // the shared handle and turn it into a ID3D11ShaderResourceView for Unity.
    [DllImport("UnityNativePlugin64")]
    private static extern IntPtr CreateSharedTexture(int sharedHandle);
    [DllImport("UnityNativePlugin64")]
    private static extern int GetGameWidth();
    [DllImport("UnityNativePlugin64")]
    private static extern int GetGameHeight();
    [DllImport("UnityNativePlugin64")]
    private static extern int GetGameFormat();

    bool noMipMaps = false;
    bool linearColorSpace = true;

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

        print("... Waiting for SharedSurface");

        while (gameSharedHandle == 0)
        {
            // Check-in every 200ms.
            yield return new WaitForSecondsRealtime(0.2f);

            // ToDo: To work, we need to pass in a parameter? 
            // This will call to DeviarePlugin native DLL routine to fetch current gGameSurfaceShare HANDLE.
            System.Int32 native = 0; // (int)_tex.GetNativeTexturePtr();
            object parm = native;
            gameSharedHandle = _spyMgr.CallCustomApi(_gameProcess, _nativeDLLName, "GetSharedHandle", ref parm, true);
        }

        // >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
        // We finally have a valid gGameSurfaceShare as a DX11 HANDLE.  
        // We can thus finish up the init.

        print("-> Got shared handle: " + gameSharedHandle.ToString("x"));


        // Call into the x64 UnityNativePlugin DLL for DX11 access, in order to create a ID3D11ShaderResourceView.
        // You'd expect this to be a IDX11Texture2D, but that's not what Unity wants.
        // We also fetch the Width/Height/Format from the C++ side, as it's simpler than
        // making an interop for the GetDesc call.

        IntPtr shared = CreateSharedTexture(gameSharedHandle);
        int width = GetGameWidth();
        int height = GetGameHeight();
        int format = GetGameFormat();

        // This is the Unity Texture2D, double width texture, with right eye on the left half.
        // It will always be up to date with latest game image, because we pass in 'shared'.

        // DXGI_FORMAT_R8G8B8A8_UNORM = 28,
        // DXGI_FORMAT_R8G8B8A8_UNORM_SRGB = 29,
        bool colorSpace;
        if (format == 28)
            colorSpace = linearColorSpace;
        else if (format == 29)
            colorSpace = !linearColorSpace;
        else
            throw new Exception(String.Format("Game uses unknown DXGI_FORMAT: {0}", format));

        _bothEyes = Texture2D.CreateExternalTexture(width, height, TextureFormat.RGBA32, noMipMaps, colorSpace, shared);

        // ToDo: might always require BGRA32, not sure.  Games typically use DXGI_FORMAT_R8G8B8A8_UNORM, 
        //  but if it's different, not sure what's the right approach.
        //        _bothEyes = Texture2D.CreateExternalTexture(3200, 900, TextureFormat.BGRA32, false, true, shared);

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

        // With the game fully launched and showing frames, we no longer need InfoText.
        // Setting it Inactive makes it not take any drawing cycles, as opposed to an empty string.
        infoText.gameObject.SetActive(false);

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

    // -----------------------------------------------------------------------------

    // Error handling.  Anytime we get an error that should *never* happen, we'll
    // just exit by putting up a MessageBox. We still want to check for any and all
    // possible error returns. Whenever we throw an exception anywhere in C# or
    // the C++ plugin, it will come here, so we can use the throw on fatal error model.
    //
    // We are using user32.dll MessageBox, instead of Windows Forms, because Unity
    // only supports an old version of .Net, because of its antique Mono runtime.

    [DllImport("user32.dll")]
    static extern int MessageBox(IntPtr hWnd, string text, string caption, int type);

    static void FatalExit(string condition, string stackTrace, LogType type)
    {
        if (type == LogType.Exception)
        {
            MessageBox(IntPtr.Zero, condition, "Fatal Error", 0);
            Application.Quit();
        }
    }


    // Deviare has a bizarre model where they don't actually return HRESULT for calls
    // that are defined that way.  Suggestion is to use GetLastError to get the real
    // error.  This is problematic, because the DeviareCOM.dll must be found to do
    // this. So, encapsulating all that here to get and print the real error.
    //
    // Also for some damn reason the LoadCustomDLL call can also return 2, not just
    // 1, so that's extra special.  0 means it failed.  Backwards of HRESULT.
    //
    // https://github.com/nektra/Deviare2/issues/32

    [DllImport("DeviareCOM64.dll")]
    static extern int GetLastErrorCode();

    int GetLastDeviareError()
    {
        // We set back to the katanga_directory here, in case we throw
        // an error.  This keeps the editor from crashing.
        string activeDirectory = Directory.GetCurrentDirectory();
        Directory.SetCurrentDirectory(katanga_directory);

        int result;
        result = GetLastErrorCode();
        print(string.Format("Last Deviare error: 0x{0:X}", result));
        
        Directory.SetCurrentDirectory(activeDirectory);

        return result;
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


