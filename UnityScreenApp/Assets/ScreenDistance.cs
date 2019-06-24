using System.Collections;
using System.Collections.Generic;
using UnityEngine;
using Valve.VR;
using Valve.VR.InteractionSystem;

// This class is for changing the screen distance, or screen size, or vertical location
//
// On the Right trackpad, a vertical scroll action will change the distance away.
// On the Left trackpad, a vertical scroll action will change the screen vertical location.
// On the Left trackpad, a horizontal scroll action will change the screen size.

public class ScreenDistance : MonoBehaviour {

    public SteamVR_Action_Boolean fartherAction;
    public SteamVR_Action_Boolean nearerAction;
    //public SteamVR_Action_Vector2 sizingAction;
    //public SteamVR_Action_Vector2 heightAction;

    // This script is attached to the main Screen object, as the most logical place
    // to put all the screen sizing and location code.
    private GameObject mainScreen;

    //-------------------------------------------------
    private void OnEnable()
    {
        mainScreen = GameObject.Find("Screen");

        fartherAction.AddOnChangeListener(OnFartherAction, SteamVR_Input_Sources.RightHand);
        nearerAction.AddOnChangeListener(OnNearerAction, SteamVR_Input_Sources.RightHand);
        //sizingAction.AddOnChangeListener(OnSizingScroll, SteamVR_Input_Sources.LeftHand);
        //heightAction.AddOnChangeListener(OnHeightScroll, SteamVR_Input_Sources.LeftHand);
    }

    private void OnDisable()
    {
        if (fartherAction != null)
            fartherAction.RemoveOnChangeListener(OnFartherAction, SteamVR_Input_Sources.RightHand);
        if (nearerAction != null)
            nearerAction.RemoveOnChangeListener(OnNearerAction, SteamVR_Input_Sources.RightHand);
        //if (sizingAction != null)
        //    sizingAction.RemoveOnChangeListener(OnSizingScroll, SteamVR_Input_Sources.LeftHand);
        //if (heightAction != null)
        //    heightAction.RemoveOnChangeListener(OnHeightScroll, SteamVR_Input_Sources.LeftHand);
    }

    //-------------------------------------------------
    // Whenever we get a down click on controller touchpad, let's loop moving the Screen out.
    Coroutine moveOut;
    private void OnFartherAction(SteamVR_Action_Boolean fromAction, SteamVR_Input_Sources fromSource, bool active)
    {
        if (active)
            moveOut = StartCoroutine(MovingFarther());
        else
            StopCoroutine(moveOut);
    }
    IEnumerator MovingFarther()
    {
        while (true)
        {
            // Each tick is 10cm.
            mainScreen.transform.Translate(new Vector3(0, 0, 0.1f));
            print("State down loop");
            yield return new WaitForSeconds(0.1f);
        }
    }

    // Same, but moving screen in for D-pad down.
    Coroutine moveIn;
    private void OnNearerAction(SteamVR_Action_Boolean fromAction, SteamVR_Input_Sources fromSource, bool active)
    {
        if (active)
            moveIn = StartCoroutine(MovingNearer());
        else
            StopCoroutine(moveIn);
    }
    IEnumerator MovingNearer()
    {
        while (true)
        {
            // Each tick is 10cm.
            mainScreen.transform.Translate(new Vector3(0, 0, -0.1f));
            print("State down loop");
            yield return new WaitForSeconds(0.1f);
        }
    }

    // For horizontal scroll on Left trackpad, we want to grow or shrink the screen rectangle.
    // Also using a 1/10 as a more manageable change than the default 1m.
    // But, the screen must maintain the aspect ratio of 16:9, so for each 1m in X, we'll
    // change 9/16m in Y.  The unintuitive negative for Y is because Unity uses the OpenGL
    // layout, and Y is inverted.
    private void OnSizingScroll(SteamVR_Action_Vector2 fromAction, SteamVR_Input_Sources fromSource, Vector2 axis, Vector2 delta)
    {
        if (axis.x != 0)
        {
            float dX = delta.x / 10.0f;
            float dY = -(delta.x / 10.0f * 9f / 16f);
            Vector3 scaling = new Vector3(dX, dY);

            mainScreen.transform.localScale += scaling;
        }
    }

    // For a vertical scroll on the left trackpad, we want to move the height of the screen.
    private void OnHeightScroll(SteamVR_Action_Vector2 fromAction, SteamVR_Input_Sources fromSource, Vector2 axis, Vector2 delta)
    {
        if (axis.y != 0)
            mainScreen.transform.Translate(new Vector3(0, delta.y / 10.0f), 0);
    }

}
