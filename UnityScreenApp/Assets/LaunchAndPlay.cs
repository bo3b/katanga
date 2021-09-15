using System;
using System.Runtime.InteropServices;
using System.IO;

using UnityEngine;
using UnityEngine.UI;
using System.Collections;
using System.Threading;
using System.Text;

public class LaunchAndPlay : MonoBehaviour
{
    // For multipoint logging, including native C++ plugins/
    private static FileStream m_FileStream;

    // Game object that handles launching and communicating with the running game.
    // If launched with no arguments, will be SlideShow.
    SlideShow game;

    // Reference to the 2D shader for DesktopDuplication use.  Connected in Unity
    // Inspector.  Needs to have a reference, otherwise builds are missing this shader.
    public Shader shader2D;


    // Filled in once the game is live.  
    public static float gameAspectRatio = 16f/9f;

    // Attached reference from Unity editor to main screen object
    public Renderer screenRenderer;

    // Original grey texture for the screen at launch, used again for resolution changes.
    Texture greyTexture;

    // On screen status text.
    public Text infoText;

    // -----------------------------------------------------------------------------
    // -----------------------------------------------------------------------------

    // The very first thing that happens for the app.


    private void Awake()
    {
        print("Awake");

        // Add our log handler, to capture all output to a non-unity debug log,
        // including our native C++ plugins for both game and Unity.

        // Set our FatalExit as the handler for exceptions so that we get usable 
        // information from the users.
        Application.logMessageReceived += FatalExit;

    }

    // -----------------------------------------------------------------------------

    // Three launch modes:
    // 1) No params on input, show Desktop Duplicator. (Texture.cs on StereoScreen)
    // 2) Single param of --slideshow-mode show slides mode. (SlideShow.cs subclass of Game, on Player)
    // 3) Multiple params specify game to connect normally. (Game.cs on Player)
    //
    // The uDesktopDuplication is attached to the Stereoscreen object, and will
    // automatically run at launch.  We will check here first, and if we have
    // a no-params launch, we'll let that run as the way to see the desktop in VR.
    // If we have a --slideshow-mode or normal VR launch, we'll disable the uDesktopDuplication
    // so that it does not interfere or waste any GPU cycles.

    void Start()
    {
        print("Start: Command line arguments: " + System.Environment.CommandLine);
        string[] args = System.Environment.GetCommandLineArgs();

        // Store the current Texture2D on the Quad as the original grey. We use this
        // as the default when images stop arriving, or we lose the original.
        greyTexture = screenRenderer.material.mainTexture;

        // If we are setup for Demo/SlideShow mode, we can replace the game object and
        // use the SlideShow subclass to just show screenshots.
        {
            game = GetComponent<SlideShow>();
            print("** Running as Demo Slideshow **");
        }


        // Allow the launching process to continue asychronously.
        StartCoroutine(game.Launch());

    }


    // -----------------------------------------------------------------------------

    // Update is called once per frame, before rendering. Great diagram:
    // https://docs.unity3d.com/Manual/ExecutionOrder.html
    //
    // We are grabbing the mutex at the top of Update, then releasing at the 
    // WaitForEndOfFrame, which is after scene rendering.  This should block
    // any game side usage during the time this Unity side is drawing.

    void Update()
    {
        // Doing GC on an ongoing basis is recommended for VR, to avoid weird stalls
        // at random times.
        if (Time.frameCount % 30 == 0)
            System.GC.Collect();

        // Check for Escape to exit
    }


    // If running in Editor, Application.Quit doesn't happen, which leaves the mutex open.
    // For the UnityEditor case, we'll specify it should quit, which will call our
    // OnApplicationQuit methods.

    void Quit()
    {
#if UNITY_EDITOR
        UnityEditor.EditorApplication.isPlaying = false;
#else
         Application.Quit();
#endif
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
            MessageBox(IntPtr.Zero, condition, "Katanga Fatal Error", 0);
            Application.Quit();
        }
    }

}

