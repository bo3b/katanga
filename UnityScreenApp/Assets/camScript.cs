using System.Collections;
using System.Collections.Generic;
using UnityEngine;
using UnityEngine.XR;

public class camScript : MonoBehaviour {
    private bool _leftEye;

	// Use this for initialization
	void Start () {
		
	}
    
    // OnRenderObject similar to OnPostRender
    public void OnPreRender()
    {
        if (DrawSBS._quadTexture == null)
            return;

        _leftEye = !_leftEye;

        if (_leftEye)
        {
            Graphics.CopyTexture(DrawSBS._bothEyes, 0, 0, 1600, 0, 1600, 900, DrawSBS._quadTexture, 0, 0, 0, 0);
        }
        else
        {
            Graphics.CopyTexture(DrawSBS._bothEyes, 0, 0, 0, 0, 1600, 900, DrawSBS._quadTexture, 0, 0, 0, 0);
        }
    }

	// Update is called once per frame
	void Update () {
		
	}
}
