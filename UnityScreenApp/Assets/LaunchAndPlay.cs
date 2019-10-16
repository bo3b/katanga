using System;
using System.Collections;
using System.Runtime.InteropServices;
using System.IO;
using System.Text;
using System.Threading;

using UnityEngine;
using UnityEngine.UI;

using Nektra.Deviare2;
using System.Diagnostics;
using Valve.VR;

public class LaunchAndPlay : MonoBehaviour
{
    string katanga_directory;

    // Absolute file path to the executable of the game. We use this path to start the game.
    string gamePath;

    // User friendly name of the game. This is shown on the big screen as info on launch.
    private string gameTitle;

    // Exe name for DX11 games only. Used to lookup process ID for injection.  If DX9 this is empty.
    private string gameDX11exe;
    private bool isDX11Game = false;

    static NktSpyMgr _spyMgr;
    static NktProcess _gameProcess = null;
    static string _nativeDLLName = null;

    private static Thread watchThread;

    // Primary Texture received from game as shared ID3D11ShaderResourceView
    // It automatically updates as the injected DLL copies the bits into the
    // shared resource.
    Texture2D _bothEyes = null;
    System.Int32 gGameSharedHandle = 0;

    // Original grey texture for the screen at launch, used again for resolution changes.
    public Renderer screen;
    Material screenMaterial;
    Texture greyTexture;

    //    System.Int32 _gameEventSignal = 0;
    static int ResetEvent = 0;
    static int SetEvent = 1;

    public Text infoText;
    //public Text qualityText;

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
            {
                gamePath = args[i + 1];
            }
            else if (args[i] == "--game-title")
            {
                gameTitle = args[i + 1];
            }
            else if (args[i] == "--game-DX11-exe")
            {
                gameDX11exe = args[i + 1];
            }
        }

        isDX11Game = String.IsNullOrEmpty(gameDX11exe) ? false : true;

        // If they didn't pass a --game-path argument, then bring up the GetOpenFileName
        // dialog to let them choose.
        if (String.IsNullOrEmpty(gamePath))
        {
            // Ask user to select the game to run in virtual 3D.  
            // We are doing this super early because there are scenarios where Unity
            // has been crashing out because the working directory changes in GetOpenFileName.

            int MAX_PATH = 260;
            StringBuilder sb = new StringBuilder("", MAX_PATH);
            SelectGameDialog(sb, sb.Capacity);

            if (sb.Length != 0)
                gamePath = sb.ToString();
        }

        if (String.IsNullOrEmpty(gamePath))
            throw new Exception("No game specified to launch.");

        // If game title wasn't passed via cmd argument then take the name of the game exe as the title instead
        if (String.IsNullOrEmpty(gameTitle))
        {
            gameTitle = gamePath.Substring(gamePath.LastIndexOf('\\') + 1);
        }

        // With the game properly selected, add name to the big screen as info on launch.
        infoText.text = "Launching...\n\n" + gameTitle;

        // Set the Quality level text on the floor to match whatever we start with.
        //qualityText.text = "Quality: " + QualitySettings.names[QualitySettings.GetQualityLevel()];
    }


    // -----------------------------------------------------------------------------

    // When launching in DX9, we will continue to use the Deviare direct launch, so
    // that we can hook Direct3DCreate9 before it is called, and convert it to 
    // Direct3DCreate9Ex.  For DX11 games, 3DFM will have already launched the game
    // using its normal techniques, and we will find it via gameProc ID and inject
    // directly without hooking anything except Present.

    void Start()
    {
        int hresult;
        object continueevent = null;

        // Store the current Texture2D on the Quad as the original grey
        screenMaterial = screen.material;
        greyTexture = screenMaterial.mainTexture;

        print("Running: " + gamePath + "\n");

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

        Directory.SetCurrentDirectory(Path.GetDirectoryName(gamePath));
        {
            // If DX11, let's wait and watch for game exe to launch.
            // This works a lot better than launching it here and hooking
            // first instructions, because we can wait past launchers or 
            // Steam launch itself, or different sub processes being launched.

            if (isDX11Game)
            {
                int procid = 0;

                print("Waiting for process: " + gameDX11exe);
                Thread.Sleep(3 * 1000);    // ToDo: time delay here, good or bad?

                do
                {
                    if (Input.GetKey("escape"))
                        Application.Quit();
                    Thread.Sleep(500);
                    procid =_spyMgr.FindProcessId(gameDX11exe);
                } while (procid == 0);

                print("->Found " + gameDX11exe + ":" + procid);
                _gameProcess = _spyMgr.ProcessFromPID(procid);
            }


            // For DX9, Launch the named game, but suspended, so we can hook our first call 
            // and be certain to catch it. 

            if (!isDX11Game)
            {
                print("Launching: " + gamePath + "...");
                _gameProcess = _spyMgr.CreateProcess(gamePath, true, out continueevent);
                if (_gameProcess == null)
                    throw new Exception("CreateProcess game launch failed: " + gamePath);
            }

            print("LoadAgent");
            _spyMgr.LoadAgent(_gameProcess);

            // Load the NativePlugin for the C++ side.  The NativePlugin must be in this app folder.
            // The Agent supports the use of Deviare in the CustomDLL, but does not respond to hooks.
            //
            // The native DeviarePlugin has two versions, one for x32, one for x64, so we can handle
            // either x32 or x64 games.

            print("Load DeviarePlugin");
            if (_gameProcess.PlatformBits == 64)
                _nativeDLLName = Application.dataPath + "/Plugins/DeviarePlugin64.dll";
            else
                _nativeDLLName = Application.dataPath + "/Plugins/DeviarePlugin.dll";

            int loadResult = _spyMgr.LoadCustomDll(_gameProcess, _nativeDLLName, true, true);
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
            //
            // Only hooking single call now, D3D11CreateDevice so that Deviare is activated.
            // This call does not hook other calls, and seems to be necessary for the Agent
            // to activate in the gameProcess.  This will also activate the DX9 path.

            print("Hook the D3D11.DLL!D3D11CreateDevice...");
            NktHook deviceHook = _spyMgr.CreateHook("D3D11.DLL!D3D11CreateDevice", 0);
            if (deviceHook == null)
                throw new Exception("Failed to hook D3D11.DLL!D3D11CreateDevice");
            deviceHook.AddCustomHandler(_nativeDLLName, 0, "");
            deviceHook.Attach(_gameProcess, true);
            deviceHook.Hook(true);

            // But if we happen to be launching direct with a selected exe, or DX9 game,
            // we can still hook the old way.

            if (!isDX11Game)
            {
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


                // Hook the primary DX9 creation call of Direct3DCreate9, which is a direct export of 
                // the d3d9 DLL.  All DX9 games must call this interface, or the Direct3DCreate9Ex.
                // This is not hooked here though, it is hooked in DeviarePlugin at OnLoad.
                // We need to do special handling to fetch the System32 version of d3d9.dll,
                // in order to avoid unhooking HelixMod's d3d9.dll.

                // Hook the nvapi.  This is required to support Direct Mode in the driver, for 
                // games like Tomb Raider and Deus Ex that have no SBS.
                // There is only one call in the nvidia dll, nvapi_QueryInterface.  That will
                // be hooked, and then the _NvAPI_Stereo_SetDriverMode call will be hooked
                // so that we can see when a game sets Direct Mode and change behavior in Present.
                // This is also done in DeviarePlugin at OnLoad.


                // Make sure the CustomHandler in the NativePlugin at OnFunctionCall gets called when this 
                // object is created. At that point, the native code will take over.

                deviceAndSwapChainHook.AddCustomHandler(_nativeDLLName, 0, "");
                factoryHook.AddCustomHandler(_nativeDLLName, 0, "");
                factory1Hook.AddCustomHandler(_nativeDLLName, 0, "");

                // Finally attach and activate the hook in the still suspended game process.

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
        }
        Directory.SetCurrentDirectory(katanga_directory);

        print("Restored Working Directory to: " + katanga_directory);


        // Setup a second thread that will run and look for the game to have exited.
        // When it does exit, we want to Quit here so that the window does not stay up.

        watchThread = new Thread(new ThreadStart(exitWatch));
        watchThread.Start();

        // We've gotten everything launched, hooked, and setup.  Now we need to wait for the
        // game to call through to CreateDevice, so that we can create the shared surface.
    }


    // This is a small routine just to handle the game exit gracefully.  The IsActive function
    // can only be called with the default parameter of a 1,000ms wait, which means that it
    // will stall this thread for a full second before returning.  Since this is a secondary
    // thread, it won't stall the Unity drawing. 
    // 
    // The point of all this is to have Katanga cleanly exit after the game does, so that the
    // user is not left with the running VR environment with nothing in it.
    // 
    // Unity complains if we Quit out of here, although it worked.  Moved the check to the
    // Update routine for watchThread.IsAlive.

    private static void exitWatch()
    {
        // First call, let's stall for 10 seconds to avoid launch-time interference
        Thread.Sleep(10000);    

        while (_gameProcess.IsActive)
        {
            Thread.Sleep(2000); // Each sleep cycle will be 3 seconds to allow a non-abrupt exit
        }
    }


    // -----------------------------------------------------------------------------
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

    readonly bool noMipMaps = false;
    readonly bool linearColorSpace = true;

    // PollForSharedSurface will just wait until the CreateDevice has been called in 
    // DeviarePlugin, and thus we have created a shared surface for copying game bits into.
    // This is asynchronous because it's in the game world, and we don't know when
    // it will happen.  This is a polling mechanism, which is not
    // great, but should be checking on a 4 byte HANDLE from the game side,
    // once a frame, which is every 11ms.  Only worth doing something more 
    // heroic if this proves to be a problem.
    //
    // Once the PollForSharedSurface returns with non-null, we are ready to continue
    // with the VR side of showing those bits.  This can also happen later in the
    // game if the resolution is changed mid-game, and either DX9->Reset or
    // DX11->ResizeBuffers is called.
    //
    // The goal here is to rebuild the drawing chain when the resolution changes,
    // but also to try very hard to avoid using those old textures, as they may
    // have been disposed in mid-drawing here.  This is all 100% async from the
    // game itself, as multi-threaded as it gets.  We have been getting crashes
    // during multiple resolution changes, that are likely to be related to this.

    void PollForSharedSurface()
    {
        // ToDo: To work, we need to pass in a parameter? Could use named pipe instead.
        // This will call to DeviarePlugin native DLL in the game, to fetch current gGameSurfaceShare HANDLE.
        System.Int32 native = 0; // (int)_tex.GetNativeTexturePtr();
        object parm = native;
        System.Int32 pollHandle = _spyMgr.CallCustomApi(_gameProcess, _nativeDLLName, "GetSharedHandle", ref parm, true);

        // When the game notifies us to Resize or Reset, we will set the gGameSharedHANDLE
        // to NULL to notify this side.  When this happens, immediately set the Quad
        // drawing texture to the original grey, so that we stop using the shared buffer 
        // that might very well be dead by now.

        if (pollHandle == 0)
        {
            screenMaterial.mainTexture = greyTexture;
            return;
        }

        // The game side is going to kick gGameSharedHandle to null *before* it resets the world
        // over there, so we'll likely get at least one frame of grey space.  If it's doing a full
        // screen reset, it will be much longer than that.  In any case, as soon as it switches
        // back from null to a valid Handle, we can rebuild our chain here.  This also holds
        // true for initial setup, where it will start as null.

        if (pollHandle != gGameSharedHandle)
        {
            gGameSharedHandle = pollHandle;

            print("-> Got shared handle: " + gGameSharedHandle.ToString("x"));


            // Call into the x64 UnityNativePlugin DLL for DX11 access, in order to create a ID3D11ShaderResourceView.
            // You'd expect this to be a ID3D11Texture2D, but that's not what Unity wants.
            // We also fetch the Width/Height/Format from the C++ side, as it's simpler than
            // making an interop for the GetDesc call.

            IntPtr shared = CreateSharedTexture(gGameSharedHandle);
            int width = GetGameWidth();
            int height = GetGameHeight();
            int format = GetGameFormat();

            // Really not sure how this color format works.  The DX9 values are completely different,
            // and typically the games are ARGB format there, but still look fine here once we
            // create DX11 texture with RGBA format.
            // DXGI_FORMAT_R8G8B8A8_UNORM = 28,
            // DXGI_FORMAT_R8G8B8A8_UNORM_SRGB = 29,    (The Surge, DX11)
            // DXGI_FORMAT_B8G8R8A8_UNORM = 87          (The Ball, DX9)
            // DXGI_FORMAT_B8G8R8A8_UNORM_SRGB = 91,
            // DXGI_FORMAT_R10G10B10A2_UNORM = 24       ME:Andromenda, Alien, CallOfCthulu   Unity RenderTextureFormat, but not TextureFormat
            //  No SRGB variant of R10G10B10A2.
            // DXGI_FORMAT_B8G8R8X8_UNORM = 88,         Trine   Unity RGB24

            // ToDo: This colorSpace doesn't do anything.
            //  Tested back to back, setting to false/true has no effect on TV gamma

            // After quite a bit of testing, this CreateExternalTexture does not appear to respect the
            // input parameters for TextureFormat, nor colorSpace.  It appears to use whatever is 
            // defined in the Shared texture as the creation parameters.  
            // If we see a format we are not presently handling properly, it's better to know about
            // it than silently do something wrong, so fire off a FatalExit.

            bool colorSpace = linearColorSpace;
            if (format == 28 || format == 87 || format == 88)
                colorSpace = linearColorSpace;
            else if (format == 29 || format == 91)
                colorSpace = !linearColorSpace;
            else if (format == 24)
                colorSpace = linearColorSpace;
            else
                MessageBox(IntPtr.Zero, String.Format("Game uses unknown DXGI_FORMAT: {0}", format), "Unknown format", 0);

            // This is the Unity Texture2D, double width texture, with right eye on the left half.
            // It will always be up to date with latest game image, because we pass in 'shared'.

            _bothEyes = Texture2D.CreateExternalTexture(width, height, TextureFormat.RGBA32, noMipMaps, colorSpace, shared);

            print("..eyes width: " + _bothEyes.width + " height: " + _bothEyes.height + " format: " + _bothEyes.format);


            // This is the primary Material for the Quad used for the virtual TV.
            // Assigning the 2x width _bothEyes texture to it means it always has valid
            // game bits.  The custom sbsShader.shader for the material takes care of 
            // showing the correct half for each eye.

            screenMaterial.mainTexture = _bothEyes;


            // These are test Quads, and will be removed.  One for each eye. Might be deactivated.
            GameObject leftScreen = GameObject.Find("left");
            if (leftScreen != null)
            {
                Material leftMat = leftScreen.GetComponent<Renderer>().material;
                leftMat.mainTexture = _bothEyes;
                // Using same primary 2x width shared texture, specify which half is used.
                leftMat.mainTextureScale = new Vector2(0.5f, 1.0f);
                leftMat.mainTextureOffset = new Vector2(0.5f, 0);
            }
            GameObject rightScreen = GameObject.Find("right");
            if (rightScreen != null)
            {
                Material rightMat = rightScreen.GetComponent<Renderer>().material;
                rightMat.mainTexture = _bothEyes;
                rightMat.mainTextureScale = new Vector2(0.5f, 1.0f);
                rightMat.mainTextureOffset = new Vector2(0.0f, 0);
            }

            // With the game fully launched and showing frames, we no longer need InfoText.
            // Setting it Inactive makes it not take any drawing cycles, as opposed to an empty string.
            infoText.gameObject.SetActive(false);
        }
    }

    // -----------------------------------------------------------------------------
    // Update is called once per frame, before rendering. Great diagram:
    // https://docs.unity3d.com/Manual/ExecutionOrder.html
    // Update is much slower than coroutines.  Unless it's required for VR, skip it.

    void Update()
    {
        // Keep checking for a change in resolution by the game. This needs to be
        // done every frame to avoid using textures disposed by Reset.
        PollForSharedSurface();

        // Doing GC on an ongoing basis is recommended for VR, to avoid weird stalls
        // at random times.
        if (Time.frameCount % 30 == 0)
            System.GC.Collect();

        if (Input.GetKey("escape"))
            Application.Quit();
        if (!watchThread.IsAlive)
            Application.Quit();
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

    //long startTime = Stopwatch.GetTimestamp();

    //// At start of frame, immediately after we've presented in VR, 
    //// restart the game app.
    //private IEnumerator SyncAtStartOfFrame()
    //{
    //    int callcount = 0;
    //    print("SyncAtStartOfFrame, first call: " + startTime.ToString());

    //    while (true)
    //    {
    //        // yield, will run again after Update.  
    //        // This is super early in the frame, measurement shows maybe 
    //        // 0.5ms after start.  
    //        yield return null;

    //        callcount += 1;

    //        // Here at very early in frame, allow game to carry on.
    //        System.Int32 dummy = SetEvent;
    //        object deviare = dummy;
    //        _spyMgr.CallCustomApi(_gameProcess, _nativeDLLName, "TriggerEvent", ref deviare, false);


    //        long nowTime = Stopwatch.GetTimestamp();

    //        // print every 30 frames 
    //        if ((callcount % 30) == 0)
    //        {
    //            long elapsedTime = nowTime - startTime;
    //            double elapsedMS = elapsedTime * (1000.0 / Stopwatch.Frequency);
    //            print("SyncAtStartOfFrame: " + elapsedMS.ToString("F1"));
    //        }

    //        startTime = nowTime;

    //        // Since this is another thread as a coroutine, we won't block the main
    //        // drawing thread from doing its thing.
    //        // Wait here by CPU spin, for 9ms, close to end of frame, before we
    //        // pause the running game.  
    //        // The CPU spin here is necessary, no normal waits or sleeps on Windows
    //        // can do anything faster than about 16ms, which is way to slow for VR.
    //        // Burning one CPU core for this is not a big deal.

    //        double waited;
    //        do
    //        {
    //            waited = Stopwatch.GetTimestamp() - startTime;
    //            waited *= (1000.0 / Stopwatch.Frequency);
    //            //if ((callcount % 30) == 0)
    //            //{
    //            //    print("waiting: " + waited.ToString("F1"));
    //            //}
    //        } while (waited < 3.0);


    //        // Now at close to the end of each VR frame, tell game to pause.
    //        dummy = ResetEvent;
    //        deviare = dummy;
    //        _spyMgr.CallCustomApi(_gameProcess, _nativeDLLName, "TriggerEvent", ref deviare, false);
    //    }
    //}

    //private IEnumerator SyncAtEndofFrame()
    //{
    //    int callcount = 0;
    //    long firstTime = Stopwatch.GetTimestamp();
    //    print("SyncAtEndofFrame, first call: " + firstTime.ToString());

    //    while (true)
    //    {
    //        yield return new WaitForEndOfFrame();

    //        // print every 30 frames 
    //        if ((callcount % 30) == 0)
    //        {
    //            long nowTime = Stopwatch.GetTimestamp();
    //            long elapsedTime = nowTime - startTime;
    //            double elapsedMS = elapsedTime * (1000.0 / Stopwatch.Frequency);
    //            print("SyncAtEndofFrame: " + elapsedMS.ToString("F1"));
    //        }

    //        //TriggerEvent(_gameEventSignal);        

    //        System.Int32 dummy = 0;  // ResetEvent
    //        object deviare = dummy;
    //        _spyMgr.CallCustomApi(_gameProcess, _nativeDLLName, "TriggerEvent", ref deviare, false);
    //    }
    //}


}

