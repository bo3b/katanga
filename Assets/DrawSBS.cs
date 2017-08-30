using System;
using System.Collections;
using System.Collections.Generic;
using UnityEngine;

using Nektra.Deviare2;
using System.IO;


public class DrawSBS : MonoBehaviour
{
    static NktSpyMgr _spyMgr;
    static NktProcess _gameProcess;
    public int width = 1024, height = 1024;
    public Texture2D noiseTex;

    // Use this for initialization
    void Start()
    {
        int hresult;
        object continueevent;
        string drawSBS_directory = Environment.CurrentDirectory;
        //string nativeDLLName = Environment.CurrentDirectory + @"\NativePlugin.dll";
        string game = @"G:\Games\The Ball\Binaries\Win32\theball.exe";

        //        noiseTex = new Texture2D(width, height, TextureFormat.ARGB32, false);
        _spyMgr = new NktSpyMgr();
        hresult = _spyMgr.Initialize();
        if (hresult != 0)
            throw new Exception("Deviare initialization error.");

        print("Successful SpyMgr Init");


        // We must set the game directory specifically, otherwise it winds up being the 
        // C# app directory which can make the game crash.  This must be done before CreateProcess.
        // This also changes the working directory, which will break Deviare's ability to find
        // the NativePlugin, so we'll use full path descriptions for the DLL load.
        // This must be reset back to the Unity game directory, otherwise Unity will
        // crash with a fatal error.

        Directory.SetCurrentDirectory(Path.GetDirectoryName(game));
        {
            // Launch the game, but suspended, so we can hook our first call and be certain to catch it.

            print("Launching: " + game + "...");
            _gameProcess = _spyMgr.CreateProcess(game, true, out continueevent);
            if (_gameProcess == null)
                throw new Exception("Game launch failed.");

            // Hook the primary DX9 creation call of Direct3DCreate9, which is a direct export of 
            // the d3d9 DLL.  All DX9 games must call this interface.
            // We set this to flgOnlyPostCall, because we want to use the IDirect3D9 object it returns.

            print("Hook the D3D9.DLL!Direct3DCreate9...");
            NktHook d3dHook = _spyMgr.CreateHook("D3D9.DLL!Direct3DCreate9", (int)eNktHookFlags.flgOnlyPostCall);
            if (d3dHook == null)
                throw new Exception("Failed to hook D3D9.DLL!Direct3DCreate9");

            // Make sure the CustomHandler in the NativePlugin at OnFunctionCall gets called when this 
            // object is created. At that point, the native code will take over.

//            d3dHook.AddCustomHandler(nativeDLLName, 0, "");

            // Finally attach and activate the hook in the still suspended game process.

            d3dHook.Attach(_gameProcess, true);
            d3dHook.Hook(true);


            // Ready to go.  Let the game startup.  When it calls Direct3DCreate9, we'll be
            // called in the NativePlugin::OnFunctionCall

            print("Continue game launch...");
            _spyMgr.ResumeProcess(_gameProcess, continueevent);
        }

        print("Restoring Working Directory to: " + drawSBS_directory);
        Directory.SetCurrentDirectory(drawSBS_directory);
    }


    // Update is called once per frame
    void Update()
    {

    }
}
