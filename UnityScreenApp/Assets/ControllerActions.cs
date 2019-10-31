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

    // This script is attached to the main Screen object, as the most logical place
    // to put all the screen sizing and location code.
    private GameObject mainScreen;

    private readonly float wait = 0.020f;  // 20 ms
    private readonly float distance = 0.010f; // 10 cm

    //-------------------------------------------------

    private void Start()
    {
        // Here at launch, let's recenter around wherever the user last saved the state.
        // If we have no state we'll just recenter to where the headset is pointing. Seems to be the 
        // model that people are expecting, instead of the facing forward based on room setup.

        RecenterHMD(false);

        // Restore any saved state the user has setup.  If nothing has been setup for a given
        // state, it will use the specified defaults.

        Int32 showFloor = PlayerPrefs.GetInt("floor", 1);
        shown = Convert.ToBoolean(showFloor);
        floor.SetActive(shown);

        float screenX = PlayerPrefs.GetFloat("screen-x", 0);
        float screenY = PlayerPrefs.GetFloat("screen-y", 2);
        float screenZ = PlayerPrefs.GetFloat("screen-z", 5);
        mainScreen.transform.position = new Vector3(screenX, screenY, screenZ);

        float sizeX = PlayerPrefs.GetFloat("size-x", 8.0f);
        float sizeY = PlayerPrefs.GetFloat("size-y", -4.5f);
        mainScreen.transform.localScale = new Vector3(sizeX, sizeY, 1);

        // Let's also clip the floor to whatever the size of the user's boundary.
        // If it's not yet fully tracking, that's OK, we'll just leave as is.  This seems better
        // than adding in the SteamVR_PlayArea script.

        var chaperone = OpenVR.Chaperone;
        if (chaperone != null)
        {
            float width = 0, height = 0;
            if (chaperone.GetPlayAreaSize(ref width, ref height))
                floor.transform.localScale = new Vector3(width, height, 1);
        }
    }

    //-------------------------------------------------

    // These ChangeListeners are all added during Enable and removed on Disable, rather
    // than at Start, because they will error out if the controller is not turned on.
    // These are called when the controllers are powered up, and then off, which makes
    // it a reliable place to activate.

    private void OnEnable()
    {
        mainScreen = GameObject.Find("Screen");

        fartherAction.AddOnChangeListener(OnFartherAction, SteamVR_Input_Sources.RightHand);
        nearerAction.AddOnChangeListener(OnNearerAction, SteamVR_Input_Sources.RightHand);

        biggerAction.AddOnChangeListener(OnBiggerAction, SteamVR_Input_Sources.LeftHand);
        smallerAction.AddOnChangeListener(OnSmallerAction, SteamVR_Input_Sources.LeftHand);
        higherAction.AddOnChangeListener(OnHigherAction, SteamVR_Input_Sources.LeftHand);
        lowerAction.AddOnChangeListener(OnLowerAction, SteamVR_Input_Sources.LeftHand);

        recenterAction.AddOnChangeListener(OnRecenterAction, SteamVR_Input_Sources.RightHand);
        hideFloorAction.AddOnStateDownListener(OnHideFloorAction, SteamVR_Input_Sources.LeftHand);
    }

    private void OnDisable()
    {
        if (fartherAction != null)
            fartherAction.RemoveOnChangeListener(OnFartherAction, SteamVR_Input_Sources.RightHand);
        if (nearerAction != null)
            nearerAction.RemoveOnChangeListener(OnNearerAction, SteamVR_Input_Sources.RightHand);

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
    }

    //-------------------------------------------------
    
    // Whenever we get clicks on Right controller trackpad, we want to loop on moving the
    // screen either in or out. Each tick of the Coroutine is worth 10cm in 3D space.Coroutine moving;
    Coroutine moving;

    // On D-pad up click, make screen farther away
    private void OnFartherAction(SteamVR_Action_Boolean fromAction, SteamVR_Input_Sources fromSource, bool active)
    {
        if (active)
            moving = StartCoroutine(MovingScreen(distance));
        else
            StopCoroutine(moving);
    }

    // On D-pad down click, make screen closer.
    private void OnNearerAction(SteamVR_Action_Boolean fromAction, SteamVR_Input_Sources fromSource, bool active)
    {
        if (active)
            moving = StartCoroutine(MovingScreen(-distance));
        else
            StopCoroutine(moving);
    }

    IEnumerator MovingScreen(float delta)
    {
        while (true)
        {
            mainScreen.transform.Translate(new Vector3(0, 0, delta));

            PlayerPrefs.SetFloat("screen-z", mainScreen.transform.position.z);

            yield return new WaitForSeconds(wait);
        }
    }

    //-------------------------------------------------
    
    // For left/right clicks on Left trackpad, we want to loop on growing or shrinking
    // the main Screen rectangle.  Each tick of the Coroutine is worth 10cm of screen height.
    Coroutine sizing;

    // For D-pad right click, grow the Screen rectangle.
    private void OnBiggerAction(SteamVR_Action_Boolean fromAction, SteamVR_Input_Sources fromSource, bool active)
    {
        if (active)
            sizing = StartCoroutine(SizingScreen(distance));
        else
            StopCoroutine(sizing);
    }

    // For D-pad left click, shrink the Screen rectangle.
    private void OnSmallerAction(SteamVR_Action_Boolean fromAction, SteamVR_Input_Sources fromSource, bool active)
    {
        if (active)
            sizing = StartCoroutine(SizingScreen(-distance));
        else
            StopCoroutine(sizing);
    }

    // ToDo: This is sizing bug.  Forced 16:9 aspect ratio is not correct for all cases.
    //  Needs to be based on the resolution in the game itself.

    IEnumerator SizingScreen(float delta)
    {
        while (true)
        {
            // But, the screen must maintain the aspect ratio of 16:9, so for each 1m in X, we'll
            // change 9/16m in Y.  The unintuitive negative for Y is because Unity uses the OpenGL
            // layout, and Y is inverted.
            float dX = delta;
            float dY = -(delta * 9f / 16f);
            mainScreen.transform.localScale += new Vector3(dX, dY);

            PlayerPrefs.SetFloat("size-x", mainScreen.transform.localScale.x);
            PlayerPrefs.SetFloat("size-y", mainScreen.transform.localScale.y);

            yield return new WaitForSeconds(wait);
        }
    }

    //-------------------------------------------------

    // For up/down clicks on the Left trackpad, we want to move the screen higher
    // or lower.  Each tick of the Coroutine will be worth 10cm in 3D space.
    Coroutine sliding;

    // For an up click on the left trackpad, we want to move the screen up.
    private void OnHigherAction(SteamVR_Action_Boolean fromAction, SteamVR_Input_Sources fromSource, bool active)
    {
        if (active)
            sliding = StartCoroutine(SlidingScreen(distance));
        else
            StopCoroutine(sliding);
    }

    // For a down click on the left trackpad, we want to move the screen down.
    private void OnLowerAction(SteamVR_Action_Boolean fromAction, SteamVR_Input_Sources fromSource, bool active)
    {
        if (active)
            sliding = StartCoroutine(SlidingScreen(-distance));
        else
            StopCoroutine(sliding);
    }

    IEnumerator SlidingScreen(float delta)
    {
        while (true)
        {
            mainScreen.transform.Translate(new Vector3(0, delta));

            PlayerPrefs.SetFloat("screen-y", mainScreen.transform.position.y);

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
    // to avoid making players sick.  But we can move the world around the camera, by changing
    // the player position.
    //
    // We want to save the user state, so they don't have to move the screen at every launch.
    // There are three states.  1) No saved state, 2) Saved state, power up, 3) Recenter command.
    // State is only saved on user command for recenter, grip on right controller.  Once saved,
    // we'll use that state for all future power up/launches.  Will be reset whenever the user
    // does another recenter command with right controller.

    public Transform player;       // Where user is looking and head position.
    public Transform vrCamera;     // Unity camera for drawing scene.  Parent is player.

    private void RecenterHMD(bool saveAngle)
    {
        print("RecenterHMD");

        //ROTATION
        // Get current head heading in scene (y-only, to avoid tilting the floor)
        float offsetAngle = vrCamera.localRotation.eulerAngles.y;

        if (saveAngle)
            PlayerPrefs.SetFloat("rotation", offsetAngle);
        else
            offsetAngle = PlayerPrefs.GetFloat("rotation", offsetAngle);

        // Now rotate CameraRig/Player in opposite direction to compensate
        // We want to set to that specific angle though, not simply rotate from where it is.
        Vector3 playerAngles = player.localEulerAngles;
        playerAngles.y = -offsetAngle;
        player.localEulerAngles = playerAngles;

        // Let's rotate the floor itself back, so that it remains stable and
        // matches their play space.  
        Vector3 floorAngles = floor.transform.localEulerAngles;
        floorAngles.y = -offsetAngle;
        floor.transform.localEulerAngles = floorAngles;


        //POSITION
        // Calculate postional offset between CameraRig and Camera
        //        Vector3 offsetPos = steamCamera.position - cameraRig.position;
        // Reposition CameraRig to desired position minus offset
        //        cameraRig.position = (desiredHeadPos.position - offsetPos);
    }


    // We'll also handle the Right Controller Grip action as a RecenterHMD command.
    // And whenever the user is going out of there way to specify this, save that angle.

    public SteamVR_Action_Boolean recenterAction;

    private void OnRecenterAction(SteamVR_Action_Boolean fromAction, SteamVR_Input_Sources fromSource, bool active)
    {
        if (active)
            RecenterHMD(true);
    }

    // -----------------------------------------------------------------------------

    // Hide the floor on center click of left trackpad. Toggle on/off.
    // Creating our own Toggle here, because the touchpad is setup as d-pad and center 
    // cannot be toggle by itself.
    // Upon any state change, save it as the new user preference.

    public SteamVR_Action_Boolean hideFloorAction;
    public GameObject floor;
    private bool shown;

    private void OnHideFloorAction(SteamVR_Action_Boolean fromAction, SteamVR_Input_Sources fromSource)
    {
        if (shown)
        {
            floor.SetActive(false);
            shown = false;
        }
        else
        {
            floor.SetActive(true);
            shown = true;
        }

        PlayerPrefs.SetInt("floor", Convert.ToInt32(shown));
    }

}
