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

    public SteamVR_Action_Vector2 distanceAction;
    public SteamVR_Action_Vector2 sizingAction;
    public SteamVR_Action_Vector2 heightAction;

    private GameObject mainScreen;

    //-------------------------------------------------
    private void OnEnable()
    {
        mainScreen = GameObject.Find("Screen");

        if (distanceAction == null)
        {
            Debug.LogError("<b>[SteamVR Interaction]</b> No screen distance action assigned");
            return;
        }
        if (sizingAction == null)
        {
            Debug.LogError("<b>[SteamVR Interaction]</b> No screen sizing action assigned");
            return;
        }
        if (heightAction == null)
        {
            Debug.LogError("<b>[SteamVR Interaction]</b> No screen vertical action assigned");
            return;
        }

        distanceAction.AddOnChangeListener(OnDistanceScroll, SteamVR_Input_Sources.RightHand);
        sizingAction.AddOnChangeListener(OnSizingScroll, SteamVR_Input_Sources.LeftHand);
        heightAction.AddOnChangeListener(OnHeightScroll, SteamVR_Input_Sources.LeftHand);
    }

    private void OnDisable()
    {
        if (distanceAction != null)
            distanceAction.RemoveOnChangeListener(OnDistanceScroll, SteamVR_Input_Sources.RightHand);
        if (sizingAction != null)
            sizingAction.RemoveOnChangeListener(OnSizingScroll, SteamVR_Input_Sources.LeftHand);
        if (heightAction != null)
            heightAction.RemoveOnChangeListener(OnHeightScroll, SteamVR_Input_Sources.LeftHand);
    }

    //-------------------------------------------------
    // Whenever we get vertical scroll on controller touchpad, let's move the Screen either in or out.
    // Using 1/10 a delta as a smaller chunk, so each tick is 10cm.
    private void OnDistanceScroll(SteamVR_Action_Vector2 fromAction, SteamVR_Input_Sources fromSource, Vector2 axis, Vector2 delta)
    {
        if (axis.y != 0)
            mainScreen.transform.Translate(new Vector3(0, 0, delta.y / 10.0f));
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
