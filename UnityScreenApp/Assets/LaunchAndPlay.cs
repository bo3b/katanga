using System;
using System.Runtime.InteropServices;
using System.IO;

using UnityEngine;
using UnityEngine.UI;

public class LaunchAndPlay : MonoBehaviour
{
    // Game object that handles launching and communicating with the running game.
    Game game;

    // Primary Texture received from game as shared ID3D11ShaderResourceView
    // It automatically updates as the injected DLL copies the bits into the
    // shared resource.
    Texture2D _bothEyes = null;
    System.Int32 gGameSharedHandle = 0;

    // Original grey texture for the screen at launch, used again for resolution changes.
    public Renderer screen;
    Material screenMaterial;
    Texture greyTexture;

    public Text infoText;

    // -----------------------------------------------------------------------------
    // -----------------------------------------------------------------------------

    // The very first thing that happens for the app.

    private void Awake()
    {
        // Set our FatalExit as the handler for exceptions so that we get usable 
        // information from the users.
        Application.logMessageReceived += FatalExit;
    }

    // -----------------------------------------------------------------------------

    void Start()
    {
        print("Command line arguments: " + System.Environment.CommandLine);

        game = GetComponent<Game>();
        game.ParseGameArgs(System.Environment.GetCommandLineArgs());

        // Store the current Texture2D on the Quad as the original grey
        screenMaterial = screen.material;
        greyTexture = screenMaterial.mainTexture;

        // With the game properly selected, add name to the big screen as info on launch.
        infoText.text = "Launching...\n\n" + game.DisplayName();

        game.Launch();
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
        System.Int32 pollHandle = game.GetSharedHandle();

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

    void Update()
    {
        // Keep checking for a change in resolution by the game. This needs to be
        // done every frame to avoid using textures disposed by Reset.

        // ToDo: Seems pretty likely to be a race condition this way.  
        // Can be destroyed in mid-use.  Hangs/crashes at resolution changes?

        PollForSharedSurface();

        // Doing GC on an ongoing basis is recommended for VR, to avoid weird stalls
        // at random times.
        if (Time.frameCount % 30 == 0)
            System.GC.Collect();

        if (Input.GetKey("escape"))
            Application.Quit();
        if (game.Exited())
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


    // -----------------------------------------------------------------------------
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
    //    System.Int32 _gameEventSignal = 0;
    //static int ResetEvent = 0;
    //static int SetEvent = 1;


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

