using System;
using System.Collections;
using System.Collections.Generic;
using UnityEngine;
using Valve.VR;
using Valve.VR.InteractionSystem;

// This class is for changing the screen distance, or screen size, or vertical location
//
// On the Right trackpad, up/down on the trackpad will change the distance away.
// On the Left trackpad, up/down on the trackpad will change the screen vertical location.
// On the Left trackpad, left/right on the trackpad will change the screen size.

public class ControllerActions : MonoBehaviour {

    public SteamVR_Action_Boolean fartherAction;
    public SteamVR_Action_Boolean nearerAction;
    public SteamVR_Action_Boolean biggerAction;
    public SteamVR_Action_Boolean smallerAction;
    public SteamVR_Action_Boolean higherAction;
    public SteamVR_Action_Boolean lowerAction;

    public SteamVR_Action_Boolean hideFloorAction;
    public SteamVR_Action_Boolean recenterAction;
    public SteamVR_Action_Boolean toggleSharpening;

    // This script is attached to the main Screen object, as the most logical place
    // to put all the screen sizing and location code.
    public Transform screen;
    public Transform globalScreen;  // container for screen, centered around zero.
    public Transform player;        // Where user is looking and head position.
    public Transform vrCamera;      // Unity camera for drawing scene.  Parent is player.


    private readonly float wait = 0.020f;  // 20 ms
    private readonly float distance = 0.010f; // 10 cm

    //-------------------------------------------------

    private void Start()
    {
        // Here at launch, let's recenter around wherever the user last saved the state.
        // If we have no state we'll just recenter to where the headset is pointing. Seems to be the 
        // model that people are expecting, instead of the facing forward based on room setup.
        // This will rotate and translate the GlobalScreen, a container for the actual screen.

        RecenterHMD(false);

        // Restore any saved state the user has setup.  If nothing has been setup for a given
        // state, it will use the specified defaults.  
        // For the actual screen size and location, these are the local values for the screen 
        // itself, not the screen container.

        float screenX = PlayerPrefs.GetFloat("screen-x", 0);  // not presently changed.
        float screenY = PlayerPrefs.GetFloat("screen-y", 2);
        float screenZ = PlayerPrefs.GetFloat("screen-z", 5);
        screen.localPosition = new Vector3(screenX, screenY, screenZ);

        float sizeX = PlayerPrefs.GetFloat("size-x", 8.0f);
        float sizeY = PlayerPrefs.GetFloat("size-y", -4.5f);
        screen.localScale = new Vector3(sizeX, sizeY, 1);

        UpdateFloor();
        UpdateSharpening();

        // Let's also clip the floor to whatever the size of the user's boundary.
        // If it's not yet fully tracking, that's OK, we'll just leave as is.  This seems better
        // than adding in the SteamVR_PlayArea script.

        //var chaperone = OpenVR.Chaperone;
        //if (chaperone != null)
        //{
        //    float width = 0, height = 0;
        //    if (chaperone.GetPlayAreaSize(ref width, ref height))
        //        floor.transform.localScale = new Vector3(width, height, 1);
        //}
    }

    //-------------------------------------------------

    // These ChangeListeners are all added during Enable and removed on Disable, rather
    // than at Start, because they will error out if the controller is not turned on.
    // These are called when the controllers are powered up, and then off, which makes
    // it a reliable place to activate.

    private void OnEnable()
    {
        fartherAction.AddOnChangeListener(OnZoomAction, SteamVR_Input_Sources.RightHand);
        nearerAction.AddOnChangeListener(OnZoomAction, SteamVR_Input_Sources.RightHand);

        biggerAction.AddOnChangeListener(OnBiggerAction, SteamVR_Input_Sources.LeftHand);
        smallerAction.AddOnChangeListener(OnSmallerAction, SteamVR_Input_Sources.LeftHand);
        higherAction.AddOnChangeListener(OnHigherAction, SteamVR_Input_Sources.LeftHand);
        lowerAction.AddOnChangeListener(OnLowerAction, SteamVR_Input_Sources.LeftHand);

        recenterAction.AddOnChangeListener(OnRecenterAction, SteamVR_Input_Sources.RightHand);
        hideFloorAction.AddOnStateDownListener(OnHideFloorAction, SteamVR_Input_Sources.LeftHand);
        toggleSharpening.AddOnStateDownListener(OnToggleSharpeningAction, SteamVR_Input_Sources.LeftHand);
    }

    private void OnDisable()
    {
        if (fartherAction != null)
            fartherAction.RemoveOnChangeListener(OnZoomAction, SteamVR_Input_Sources.RightHand);
        if (nearerAction != null)
            nearerAction.RemoveOnChangeListener(OnZoomAction, SteamVR_Input_Sources.RightHand);

        if (biggerAction != null)
            biggerAction.RemoveOnChangeListener(OnBiggerAction, SteamVR_Input_Sources.LeftHand);
        if (smallerAction != null)
            smallerAction.RemoveOnChangeListener(OnSmallerAction, SteamVR_Input_Sources.LeftHand);
        if (higherAction != null)
            higherAction.RemoveOnChangeListener(OnHigherAction, SteamVR_Input_Sources.LeftHand);
        if (lowerAction != null)
            lowerAction.RemoveOnChangeListener(OnLowerAction, SteamVR_Input_Sources.LeftHand);

        if (recenterAction != null)
            recenterAction.RemoveOnChangeListener(OnRecenterAction, SteamVR_Input_Sources.RightHand);
        if (hideFloorAction != null)
            hideFloorAction.RemoveOnStateDownListener(OnHideFloorAction, SteamVR_Input_Sources.LeftHand);
        if (toggleSharpening != null)
            toggleSharpening.RemoveOnStateDownListener(OnToggleSharpeningAction, SteamVR_Input_Sources.LeftHand);
    }

    //-------------------------------------------------

    // Called during update loop, to watch for user input on the keyboard or on a connected
    // controller.  

    void Update()
    {
        ScreenZoom();
        ScreenBiggerSmaller();
        ScreenHigherLower();

        Recenter();
        CycleEnvironment();
        SharpeningToggle();
    }


    //-------------------------------------------------

    // Whenever we get clicks on Right controller trackpad, we want to loop on moving the
    // screen either in or out. Each tick of the Coroutine is worth 10cm in 3D space.
    // 
    // Dpad up or joystick up on VR controllers moves it away, down moves it closer.  This
    // uses the SteamVR InputActions, and is bound through the Unity SteamVR Input menu.
    //
    // Keyboard pageup/dn and Xbox controller right stick also work using Unity InputManager.
    // These can only be used when Katanga is frontmost, so they won't interfere with the game.

    Coroutine unityMoving = null;

    private void ScreenZoom()
    {
        float movement = Input.GetAxis("ZoomScreen") + Input.GetAxis("Controller ZoomScreen");

        if ((unityMoving == null) && (movement != 0.0f))
        {
            float delta = (movement > 0) ? distance : -distance;
            unityMoving = StartCoroutine(MovingScreen(delta));
        }
        if ((unityMoving != null) && (movement == 0.0f))
        {
            StopCoroutine(unityMoving);
            unityMoving = null;
        }
    }

    Coroutine vrMoving = null;

    private void OnZoomAction(SteamVR_Action_Boolean fromAction, SteamVR_Input_Sources fromSource, bool active)
    {
        float delta = (fromAction == fartherAction) ? distance : -distance;

        if (active)
            vrMoving = StartCoroutine(MovingScreen(delta));
        else
        {
            StopCoroutine(vrMoving);
            vrMoving = null;
        }
    }

    IEnumerator MovingScreen(float delta)
    {
        while (true)
        {
            screen.Translate(new Vector3(0, 0, delta));

            PlayerPrefs.SetFloat("screen-z", screen.localPosition.z);

            yield return new WaitForSeconds(wait);
        }
    }

    //-------------------------------------------------

    // For left/right clicks on Left trackpad, we want to loop on growing or shrinking
    // the main Screen rectangle.  Each tick of the Coroutine is worth 10cm of screen height.
    //
    // For keyboard left arrow is smaller, right arrow is bigger.
    // For controller, left joystick x axis  is bigger/smaller.

    Coroutine unitySizing = null;

    private void ScreenBiggerSmaller()
    {
        float movement = Input.GetAxis("SizeScreen") + Input.GetAxis("Controller SizeScreen");

        if ((unitySizing == null) && (movement != 0.0f))
        {
            float delta = (movement > 0) ? distance : -distance;
            unitySizing = StartCoroutine(SizingScreen(delta));
        }
        if ((unitySizing != null) && (movement == 0.0f))
        {
            StopCoroutine(unitySizing);
            unitySizing = null;
        }
    }

    Coroutine vrSizing;

    // For D-pad right click, grow the Screen rectangle.
    private void OnBiggerAction(SteamVR_Action_Boolean fromAction, SteamVR_Input_Sources fromSource, bool active)
    {
        if (active)
            vrSizing = StartCoroutine(SizingScreen(distance));
        else
            StopCoroutine(vrSizing);
    }

    // For D-pad left click, shrink the Screen rectangle.
    private void OnSmallerAction(SteamVR_Action_Boolean fromAction, SteamVR_Input_Sources fromSource, bool active)
    {
        if (active)
            vrSizing = StartCoroutine(SizingScreen(-distance));
        else
            StopCoroutine(vrSizing);
    }

    // Whenever we are saving the x/y for the screen size, we want this to be at the 16:9 aspect
    // ratio, because at launch we want to show that normal shaped screen.  
    //
    // However, when moving, we might be moving a 4:3 or 25:9 screen, and we want them to be
    // able to see that live, like normal, so the actual move operation will be based off of
    // whatever the current game is running at.

    IEnumerator SizingScreen(float delta)
    {
        while (true)
        {
            // But, the screen must maintain the aspect ratio of 16:9, so for each 1m in X, we'll
            // change 9/16m in Y.  The unintuitive negative for Y is because Unity uses the OpenGL
            // layout, and Y is inverted.
            float dX = delta;
            float dY = -(delta * 9f / 16f);
            PlayerPrefs.SetFloat("size-x", screen.localScale.x);
            PlayerPrefs.SetFloat("size-y", screen.localScale.y);

            dX = -dY * LaunchAndPlay.gameAspectRatio;
            screen.localScale += new Vector3(dX, dY);

            yield return new WaitForSeconds(wait);
        }
    }

    //-------------------------------------------------

    // For up/down clicks on the Left trackpad, we want to move the screen higher
    // or lower.  Each tick of the Coroutine will be worth 10cm in 3D space.
    //
    // For keyboard, arrow up moves screen up, arrow down moves down.
    // For xbox controller, left stick Y axis moves screen up/down.

    Coroutine unitySliding = null;

    private void ScreenHigherLower()
    {
        float movement = Input.GetAxis("SlideScreen") + Input.GetAxis("Controller SlideScreen");

        if ((unitySliding == null) && (movement != 0.0f))
        {
            float delta = (movement > 0) ? distance : -distance;
            unitySliding = StartCoroutine(SlidingScreen(delta));
        }
        if ((unitySliding != null) && (movement == 0.0f))
        {
            StopCoroutine(unitySliding);
            unitySliding = null;
        }
    }

    Coroutine vrSliding;

    // For an up click on the left trackpad, we want to move the screen up.
    private void OnHigherAction(SteamVR_Action_Boolean fromAction, SteamVR_Input_Sources fromSource, bool active)
    {
        if (active)
            vrSliding = StartCoroutine(SlidingScreen(distance));
        else
            StopCoroutine(vrSliding);
    }

    // For a down click on the left trackpad, we want to move the screen down.
    private void OnLowerAction(SteamVR_Action_Boolean fromAction, SteamVR_Input_Sources fromSource, bool active)
    {
        if (active)
            vrSliding = StartCoroutine(SlidingScreen(-distance));
        else
            StopCoroutine(vrSliding);
    }

    IEnumerator SlidingScreen(float delta)
    {
        while (true)
        {
            screen.Translate(new Vector3(0, delta));

            PlayerPrefs.SetFloat("screen-y", screen.localPosition.y);

            yield return new WaitForSeconds(wait);
        }
    }

    // -----------------------------------------------------------------------------

    // We need to allow Recenter, even for room-scale, because people ask for it. 
    // The usual Recenter does not work for room-scale because the assumption is that
    // you will simply rotate to see.  This following code sequence works in all cases.
    // https://forum.unity.com/threads/openvr-how-to-reset-camera-properly.417509/#post-2792972
    //
    // vrCamera object cannot be moved or altered, Unity VR doesn't allow moving the camera
    // to avoid making players sick.  We now move the GlobalScreen for Recenter operations,
    // as it is centered around zero, and thus only requires matching the vrCamera, which
    // will rotate and translate the contained screen.
    //
    // We want to save the user state, so they don't have to move the screen at every launch.
    // There are three states.  1) No saved state, 2) Saved state, power up, 3) Recenter command.
    // State is only saved on user command for recenter, grip on right controller.  Once saved,
    // we'll use that state for all future power up/launches.  Will be reset whenever the user
    // does another recenter command with right controller.

    private void RecenterHMD(bool saveState)
    {
        print("RecenterHMD");

        // Get current head rotation in scene (y-only, to avoid tilting the floor)

        float offsetAngle = vrCamera.localRotation.eulerAngles.y;

        if (saveState)
            PlayerPrefs.SetFloat("global-rotation", offsetAngle);
        else
            offsetAngle = PlayerPrefs.GetFloat("global-rotation", offsetAngle);

        // Now set the globalScreen euler angle to that same rotation, so that the screen
        // environment will rotate from the starting zero location to current gaze.
        // Because this is called for any Recenter operation, we don't want to simply
        // rotate from where we are, we want to set wherever the vrCamera is looking.

        Vector3 screenAngles = globalScreen.eulerAngles;
        screenAngles.y = offsetAngle;
        globalScreen.eulerAngles = screenAngles;


        // Do the same job for translation, and move the globalScreen to the same
        // offset that the vrCamer has.  Since this moves the global container instead
        // of the screen, the relative offsets and screen size will appear unchanged,
        // just centered to the viewing position.

        Vector3 offsetPos = vrCamera.position;

        if (saveState)
        {
            PlayerPrefs.SetFloat("global-x", offsetPos.x);
            PlayerPrefs.SetFloat("global-z", offsetPos.z);
        }
        else
        {
            offsetPos.x = PlayerPrefs.GetFloat("global-x", offsetPos.x);
            offsetPos.z = PlayerPrefs.GetFloat("global-z", offsetPos.z);
        }

        Vector3 globalPos = globalScreen.position;
        globalPos.x = offsetPos.x;
        globalPos.z = offsetPos.z;
        globalScreen.position = globalPos;
    }


    // We'll also handle the Right Controller Grip action as a RecenterHMD command.
    // And whenever the user is going out of there way to specify this, save that angle.

    private void OnRecenterAction(SteamVR_Action_Boolean fromAction, SteamVR_Input_Sources fromSource, bool active)
    {
        if (active)
            RecenterHMD(true);
    }

    // For Keyboard, recenter key is Home.
    // For xbox controller, recenter is right bumper.

    private void Recenter()
    {
        if (Input.GetButtonDown("Recenter"))
            RecenterHMD(true);
    }

    // -----------------------------------------------------------------------------

    // Upon any state change, save it as the new user preference.

    public GameObject floor;
    public Camera sky;          // As VRCamera object
    public GameObject leftEmitter;
    public GameObject rightEmitter;

    private void OnHideFloorAction(SteamVR_Action_Boolean fromAction, SteamVR_Input_Sources fromSource)
    {
        int state = PlayerPrefs.GetInt("floor", 3);
        state++;
        if (state > 4)
            state = 0;
        PlayerPrefs.SetInt("floor", state);

        UpdateFloor();
    }

    // For Keyboard, cycle environment key is backspace.
    // For xbox controller, cycle is left bumper.

    private void CycleEnvironment()
    {
        if (Input.GetButtonDown("Cycle Environment"))
        {
            int state = PlayerPrefs.GetInt("floor", 3);
            state++;
            if (state > 4)
                state = 0;
            PlayerPrefs.SetInt("floor", state);

            UpdateFloor();
        }
    }


    // Hide the floor on grip of left control. Now cycle:
    //  1) Snow less
    //  2) Snow off
    //  3) Sky off
    //  4) Floor off
    //  5) All on

    private void UpdateFloor()
    {
        int state = PlayerPrefs.GetInt("floor", 3);
        print("Set environment state: " + state);

        ParticleSystem.EmissionModule leftSnow = leftEmitter.GetComponent<ParticleSystem>().emission;
        ParticleSystem.EmissionModule rightSnow = rightEmitter.GetComponent<ParticleSystem>().emission;

        switch (state)
        {
            case 1:
                floor.SetActive(true);
                sky.clearFlags = CameraClearFlags.Skybox;
                leftEmitter.SetActive(false);                   // Disable, then enable makes immediate 
                rightEmitter.SetActive(false);                  // visible difference in rate from prewarm.
                leftSnow.rateOverTime = 50f;
                rightSnow.rateOverTime = 50f;
                leftEmitter.SetActive(true);
                rightEmitter.SetActive(true);
                break;
            case 2:
                floor.SetActive(true);
                sky.clearFlags = CameraClearFlags.Skybox;
                leftEmitter.SetActive(false);
                rightEmitter.SetActive(false);
                break;
            case 3:
                floor.SetActive(true);
                sky.clearFlags = CameraClearFlags.SolidColor;
                leftEmitter.SetActive(false);
                rightEmitter.SetActive(false);
                break;
            case 4:
                floor.SetActive(false);
                sky.clearFlags = CameraClearFlags.SolidColor;
                leftEmitter.SetActive(false);
                rightEmitter.SetActive(false);
                break;

            default:
                floor.SetActive(true);
                sky.clearFlags = CameraClearFlags.Skybox;
                leftSnow.rateOverTime = 500f;
                rightSnow.rateOverTime = 500f;
                leftEmitter.SetActive(true);
                rightEmitter.SetActive(true);
                break;
        }
    }


    // -----------------------------------------------------------------------------

    // Sharpening effect will be on by default, but the user has the option to disable it,
    // because it will cost some performance, and they may not care for it.

    private void OnToggleSharpeningAction(SteamVR_Action_Boolean fromAction, SteamVR_Input_Sources fromSource)
    {
        int state = PlayerPrefs.GetInt("sharpening", 1);
        state++;
        if (state > 1)
            state = 0;
        PlayerPrefs.SetInt("sharpening", state);

        UpdateSharpening();
    }

    // For Keyboard, sharpening key is Insert.
    // For xbox controller, sharpening toggle is Y button.

    private void SharpeningToggle()
    {
        if (Input.GetButtonDown("Sharpening Toggle"))
        {
            int state = PlayerPrefs.GetInt("sharpening", 1);
            state++;
            if (state > 1)
                state = 0;
            PlayerPrefs.SetInt("sharpening", state);

            UpdateSharpening();
        }
    }

    private void UpdateSharpening()
    {
        int state = PlayerPrefs.GetInt("sharpening", 1);

        PrismSharpen sharpener = vrCamera.GetComponent<PrismSharpen>();

        if (state == 1)
            sharpener.enabled = true;
        else
            sharpener.enabled = false;

        print("Sharpening state: " + state);
    }
}
