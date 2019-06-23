using System.Collections;
using System.Collections.Generic;
using UnityEngine;
using Valve.VR;
using Valve.VR.InteractionSystem;

public class ScreenDistance : MonoBehaviour {

    public SteamVR_Action_Vector2 sizingAction;
    public Hand hand;

    private GameObject mainScreen;

    //-------------------------------------------------
    private void OnEnable()
    {
        mainScreen = GameObject.Find("Screen");

        if (hand == null)
            hand = this.GetComponent<Hand>();

        if (sizingAction == null)
        {
            Debug.LogError("<b>[SteamVR Interaction]</b> No screen sizing action assigned");
            return;
        }

        sizingAction.AddOnChangeListener(OnScrollChange, hand.handType);
    }

    private void OnDisable()
    {
        if (sizingAction != null)
            sizingAction.RemoveOnChangeListener(OnScrollChange, hand.handType);
    }

    //-------------------------------------------------
    // Whenever we get scroll on controller touchpad, let's move the Screen either in or out.
    // Using 1/10 a delta as a smaller chunk, so each tick is 10cm.

    private void OnScrollChange(SteamVR_Action_Vector2 fromAction, SteamVR_Input_Sources fromSource, Vector2 axis, Vector2 delta)
    {
        if (axis.y != 0)
            mainScreen.transform.Translate(new Vector3(0, 0, delta.y / 10));
    }
}
