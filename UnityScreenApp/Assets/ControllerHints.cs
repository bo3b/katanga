using UnityEngine;
using System.Collections;
using System;

namespace Valve.VR.InteractionSystem.Sample
{
    //-------------------------------------------------------------------------
    public class ControllerHints : MonoBehaviour
    {
        // hintAction is attached in RightHand Controller under Player
        public SteamVR_Action_Boolean hintAction;
        public Hand hand;
        public ControllerButtonHints hintController;

        private Coroutine buttonHintCoroutine;
        private Coroutine textHintCoroutine;

        private bool showing;


        //-------------------------------------------------
        private void Start()
        {
            hintAction.AddOnChangeListener(OnToggleActionChange, hand.handType);

            Int32 showHints = PlayerPrefs.GetInt("hints", 1);
            showing = Convert.ToBoolean(showHints);

            StartCoroutine(WaitForInitialize());
        }

        private void OnApplicationQuit()
        {
            if (hintAction != null)
                hintAction.RemoveOnChangeListener(OnToggleActionChange, hand.handType);
        }

        // Always start with the hint showing, by default, but if the user has specifically
        // turned them off, then restore that as their preference.
        //
        // This is complicated by how late the ControllerButtonHints is initialized.
        // Until it processes the hint info via the bound actions, it will fail to show.
        // So, we'll loop here, waiting for that init to happen.  This will also allow
        // us to properly show the help/hints whenever the controller is turned on,
        // so it does not need to be active when launched.  

        IEnumerator WaitForInitialize()
        {
            while (hintController.initialized == false)
                yield return null;

            if (showing)
                ShowTextHints(hand);
        }

        //-------------------------------------------------
        private void OnToggleActionChange(SteamVR_Action_Boolean actionIn, SteamVR_Input_Sources inputSource, bool newValue)
        {
            if (showing)
            {
                DisableHints();
                showing = false;
            }
            else
            {
                ShowTextHints(hand);
                showing = true;
            }

            PlayerPrefs.SetInt("hints", Convert.ToInt32(showing));
        }


        //-------------------------------------------------
        public void ShowButtonHints(Hand hand)
        {
            if (buttonHintCoroutine != null)
            {
                StopCoroutine(buttonHintCoroutine);
            }
            buttonHintCoroutine = StartCoroutine(TestButtonHints(hand));
        }


        //-------------------------------------------------
        public void ShowTextHints(Hand hand)
        {
            //if (textHintCoroutine != null)
            //{
            //    StopCoroutine(textHintCoroutine);
            //}
            //textHintCoroutine = StartCoroutine(TestTextHints(hand));

            // This script is only attached to the right hand, but we want to display
            // help for both hands.
            Hand rightHand = (hand.handType == SteamVR_Input_Sources.RightHand) ? hand : hand.otherHand;
            Hand leftHand = (hand.handType == SteamVR_Input_Sources.LeftHand) ? hand : hand.otherHand;

            // We only have boolean actions, so sift through the array of these to put
            // proper names on them.  There is no API to fetch the LocalizedString from the actions.

            foreach (SteamVR_Action_Boolean action in SteamVR_Input.actionsBoolean)
            {
                switch (action.GetShortName())
                {    
                    case "ToggleHintAction":
                        ControllerButtonHints.ShowTextHint(rightHand, action, "Show/Hide Help");
                        break;

                        // For RightHand action, we'll look for single action to show the text
                        // for the dpad controls.  ControllerButtonHints does not support dpad specific 
                        // names, so we need one text for the whole dpad.
                    case "RecenterAction":
                        ControllerButtonHints.ShowTextHint(rightHand, action, "Recenter Screen");
                        break;
                    case "ScreenFartherAction":
                        ControllerButtonHints.ShowTextHint(rightHand, action, "Up: Screen Farther\nDown: Screen Nearer");
                        break;

                    // For LeftHand actions, we'll look for the single action and build the
                    // large text help for all 5 dpad actions.
                    case "HideFloorAction":
                        ControllerButtonHints.ShowTextHint(leftHand, action, "Show/Hide Floor");
                        break;
                    case "ScreenBiggerAction":
                        string help = "Up: Screen Up\nDown: Screen Down\nLeft: Screen Smaller\nRight: Screen Bigger";
                        ControllerButtonHints.ShowTextHint(leftHand, action, help);
                        break;

                    default:
                        break;
                }
            }
        }


        //-------------------------------------------------
        public void DisableHints()
        {
            if (buttonHintCoroutine != null)
            {
                StopCoroutine(buttonHintCoroutine);
                buttonHintCoroutine = null;
            }

            if (textHintCoroutine != null)
            {
                StopCoroutine(textHintCoroutine);
                textHintCoroutine = null;
            }

            foreach (Hand hand in Player.instance.hands)
            {
                ControllerButtonHints.HideAllButtonHints(hand);
                ControllerButtonHints.HideAllTextHints(hand);
            }
        }


        //-------------------------------------------------
        // Cycles through all the button hints on the controller
        //-------------------------------------------------
        private IEnumerator TestButtonHints(Hand hand)
        {
            ControllerButtonHints.HideAllButtonHints(hand);

            while (true)
            {
                for (int actionIndex = 0; actionIndex < SteamVR_Input.actionsIn.Length; actionIndex++)
                {
                    ISteamVR_Action_In action = SteamVR_Input.actionsIn[actionIndex];
                    if (action.GetActive(hand.handType))
                    {
                        ControllerButtonHints.ShowButtonHint(hand, action);
                        yield return new WaitForSeconds(1.0f);
                        ControllerButtonHints.HideButtonHint(hand, action);
                        yield return new WaitForSeconds(0.5f);
                    }
                    yield return null;
                }

                ControllerButtonHints.HideAllButtonHints(hand);
                yield return new WaitForSeconds(1.0f);
            }
        }


        //-------------------------------------------------
        // Cycles through all the text hints on the controller
        //-------------------------------------------------
        private IEnumerator TestTextHints(Hand hand)
        {
            ControllerButtonHints.HideAllTextHints(hand);

            while (true)
            {
                for (int actionIndex = 0; actionIndex < SteamVR_Input.actionsIn.Length; actionIndex++)
                {
                    ISteamVR_Action_In action = SteamVR_Input.actionsIn[actionIndex];
                    if (action.GetActive(hand.handType))
                    {
                        ControllerButtonHints.ShowTextHint(hand, action, action.GetShortName());
                        yield return new WaitForSeconds(3.0f);
                        ControllerButtonHints.HideTextHint(hand, action);
                        yield return new WaitForSeconds(0.5f);
                    }
                    yield return null;
                }

                ControllerButtonHints.HideAllTextHints(hand);
                yield return new WaitForSeconds(3.0f);
            }
        }
    }
}
