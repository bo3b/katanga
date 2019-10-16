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
    private void OnEnable()
    {
        mainScreen = GameObject.Find("Screen");

        fartherAction.AddOnChangeListener(OnFartherAction, SteamVR_Input_Sources.RightHand);
        nearerAction.AddOnChangeListener(OnNearerAction, SteamVR_Input_Sources.RightHand);
        biggerAction.AddOnChangeListener(OnBiggerAction, SteamVR_Input_Sources.LeftHand);
        smallerAction.AddOnChangeListener(OnSmallerAction, SteamVR_Input_Sources.LeftHand);
        higherAction.AddOnChangeListener(OnHigherAction, SteamVR_Input_Sources.LeftHand);
        lowerAction.AddOnChangeListener(OnLowerAction, SteamVR_Input_Sources.LeftHand);
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

            yield return new WaitForSeconds(wait);
        }
    }

}
