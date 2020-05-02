using System;
using System.Collections;
using System.IO;
using System.Runtime.InteropServices;
using UnityEngine;
using UnityEngine.UI;
using Valve.VR;

// This is a subclass of the Game object.  Game object is setup to inject into and run
// a given game specified by the input arguments.  
// 
// This object is to simply show a slideshow of stereo photographs/screenshots on the
// big screen.  Same basic principle, run a game, except that there the 'game' is just
// the slideshow.

public class SlideShow : Game
{
    public SteamVR_Action_Boolean pauseAction;
    public SteamVR_Action_Boolean skipAction;

    public Renderer screen;
    public Text infoText;

    private bool playing = true;
    private bool skip = false;
    private readonly float deltaT = 0.4f;    // minimal time to show a slide.
    private string[] stereoFiles;

    // -----------------------------------------------------------------------------

    // We want to be able to control the slideshow as well, pausing on great shots, and
    // skipping quickly if not interesting.  

    private void OnEnable()
    {
        pauseAction.AddOnChangeListener(OnPauseAction, SteamVR_Input_Sources.RightHand);
        skipAction.AddOnChangeListener(OnSkipAction, SteamVR_Input_Sources.LeftHand);
    }

    private void OnDisable()
    {
        if (pauseAction != null)
            pauseAction.RemoveOnChangeListener(OnPauseAction, SteamVR_Input_Sources.RightHand);
        if (skipAction != null)
            skipAction.RemoveOnChangeListener(OnSkipAction, SteamVR_Input_Sources.LeftHand);
    }

    private void Update()
    {
        PauseToggle();
        NextSlide();
    }

    // -----------------------------------------------------------------------------

    // We will use the IENumerator as the asynchronous loop for showing the next slides
    // in the slideshow.  This loop will never exit until quit, but it can be paused.

    public override IEnumerator Launch()
    {
        // Folder storing all the photos to be shown will be a subfolder of the katanga
        // unity app. Any jps image in that folder will be part of the slide show.
        // We will ship a default set of good examples, but people can add any they like.
        string screenShotFolder = Environment.CurrentDirectory + @"\Stereo Pictures";
        stereoFiles = Directory.GetFiles(screenShotFolder, "*.jps");

        // Start with first one.
        float spinTime = 0;
        LoadNextJPS();

        // Disable "Launching..." as we start showing slides.
        infoText.gameObject.SetActive(false);

        while (true)
        {
            if (skip)
            {
                LoadNextJPS();

                skip = false;
                spinTime = 0.0f;
            }
            else if (playing)
            {
                spinTime += deltaT;

                if (spinTime > 5.0f)
                {
                    LoadNextJPS();

                    skip = false;
                    spinTime = 0.0f;
                }
            }

            yield return new WaitForSecondsRealtime(deltaT);
        }
    }

    public override string DisplayName()
    {
        return "SlideShow";
    }

    // Bit of a hack here for SlideShow only.  In the main loop for the game, we check
    // for nonzero result from this routine, and then whether it changed from the last
    // version saved.  We don't want to sprinkle #ifdef all through the code for Demo
    // version, so this hack bypasses the check in PollForSharedSurface so that it 
    // does nothing.

    public override int GetSharedHandle()
    {
        LaunchAndPlay.gGameSharedHandle = -1;
        return -1;
    }


    // -----------------------------------------------------------------------------

    // -- For VR controller actions

    private void OnPauseAction(SteamVR_Action_Boolean fromAction, SteamVR_Input_Sources fromSource, bool active)
    {
        playing = !active;
    }

    private void OnSkipAction(SteamVR_Action_Boolean fromAction, SteamVR_Input_Sources fromSource, bool active)
    {
        if (active)
            skip = true;
    }

    // -- For keyboard or xbox controllers

    private void PauseToggle()
    {
        if (Input.GetButtonDown("Pause SlideShow"))
            playing = !playing;
    }

    private void NextSlide()
    {
        if (Input.GetButtonDown("Next Slide"))
            skip = true;
    }


    // -----------------------------------------------------------------------------

    private int index = 0;

    public void LoadNextJPS()
    {
        Texture2D stereoTex = null;
        byte[] fileData;
        string filePath = stereoFiles[index];

        if (File.Exists(filePath))
        {
            fileData = File.ReadAllBytes(filePath);
            stereoTex = new Texture2D(2, 2);
            stereoTex.LoadImage(fileData);            //..this will auto-resize the texture dimensions.
        }

        Material screenMaterial = screen.material;
        screenMaterial.mainTexture = stereoTex;

        // The loaded image will be upside down, because JPG does not follow OpenGL coordinate
        // systems.  Invert the image in Y axis by using the TextureScale trick.
        // We can't use the normal Transform for the Screen itself, because the
        // screen needs to be dynamic for the user to specify, and if we invert
        // there we also invert the infoText.

        screenMaterial.SetTextureScale("_MainTex", new Vector2(1, -1));

        // Circular loop of stereo file name array
        index += 1;
        index %= stereoFiles.Length;
    }
}
