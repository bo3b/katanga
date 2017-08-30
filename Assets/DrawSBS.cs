using System;
using System.Collections;
using System.Collections.Generic;
using UnityEngine;

using Nektra.Deviare2;


public class DrawSBS : MonoBehaviour
{
    static NktSpyMgr _spyMgr;
    public int width = 1024, height = 1024;
    public Texture2D noiseTex;

    // Use this for initialization
    void Start()
    {
        int hresult;

//        noiseTex = new Texture2D(width, height, TextureFormat.ARGB32, false);
        _spyMgr = new NktSpyMgr();
        hresult = _spyMgr.Initialize();
        if (hresult != 0)
            throw new Exception("Deviare initialization error.");

        print("Successful SpyMgr Init");
    }

    // Update is called once per frame
    void Update()
    {

    }
}
