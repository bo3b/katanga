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
    string _nativeDLLName;
    public Texture2D _noiseTex;

    // Use this for initialization
    void Start()
    {

        // Create a texture
        _noiseTex = new Texture2D(512, 512, TextureFormat.RGBA32, false);
        // Set point filtering just so we can see the pixels clearly
        _noiseTex.filterMode = FilterMode.Point;

        
        // Call Apply() so it's actually uploaded to the GPU
        for (int y = 0; y < _noiseTex.height; y++)
        {
            for (int x = 0; x < _noiseTex.width; x++)
            {
                Color color = ((x & y) != 0 ? Color.white : Color.grey);
                _noiseTex.SetPixel(x, y, color);
            }
        }
        _noiseTex.Apply();

        // Set texture onto our material
        Renderer render = GetComponent<Renderer>();
        render.material.mainTexture = _noiseTex;

        print("applied tex");

        int hresult;
        object continueevent;
        string drawSBS_directory = Environment.CurrentDirectory;
        _nativeDLLName = Environment.CurrentDirectory + @"\Debug\NativePlugin.dll";
        string game = @"G:\Games\The Ball\Binaries\Win32\theball.exe";

        _spyMgr = new NktSpyMgr();
        hresult = _spyMgr.Initialize();
        if (hresult != 0)
            throw new Exception("Deviare initialization error.");
#if DEBUG
        _spyMgr.SettingOverride("SpyMgrDebugLevelMask", 0xCF8);
#endif
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

            // Load the NativePlugin for the C++ side.  The NativePlugin must be in this app folder.
            // The Agent supports the use of Deviare in the CustomDLL, but does not respond to hooks.

            print("Load NativePlugin... " + _nativeDLLName);
            _spyMgr.LoadAgent(_gameProcess);
            int result = _spyMgr.LoadCustomDll(_gameProcess, _nativeDLLName, true, true);
            if (result < 0)
                throw new Exception("Could not load NativePlugin DLL.");

            // Hook the primary DX9 creation call of Direct3DCreate9, which is a direct export of 
            // the d3d9 DLL.  All DX9 games must call this interface, or the Direct3DCreate9Ex.
            // We set this to flgOnlyPreCall, because we want to always create the IDirect3D9Ex object.

            print("Hook the D3D9.DLL!Direct3DCreate9...");
            NktHook d3dHook = _spyMgr.CreateHook("D3D9.DLL!Direct3DCreate9", (int)eNktHookFlags.flgOnlyPreCall);
            if (d3dHook == null)
                throw new Exception("Failed to hook D3D9.DLL!Direct3DCreate9");

            // Make sure the CustomHandler in the NativePlugin at OnFunctionCall gets called when this 
            // object is created. At that point, the native code will take over.

            d3dHook.AddCustomHandler(_nativeDLLName, 0, "");

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



    void ModifyTexturePixels()
    {
        int width = _noiseTex.width;
        int height = _noiseTex.height;

        float t = Time.timeSinceLevelLoad * 4.0f;
        Color col = new Color();

        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                // Simple "plasma effect": several combined sine waves
                float vv = (float)(
                    (127.0f + (127.0f * Math.Sin(x / 7.0f + t))) +
                    (127.0f + (127.0f * Math.Sin(y / 5.0f - t))) +
                    (127.0f + (127.0f * Math.Sin((x + y) / 6.0f - t))) +
                    (127.0f + (127.0f * Math.Sin(Math.Sqrt((x * x + y * y)) / 4.0f - t)))
                    ) / 4 / 256;

                // Write the texture pixel
                col[0] = vv;
                col[1] = vv;
                col[2] = vv;
                col[3] = vv;
                _noiseTex.SetPixel(x, y, col);
            }
        }
        _noiseTex.Apply();
    }

    // Update is called once per frame
    void Update()
    {
     //   ModifyTexturePixels();
        System.Int32 pGameScreen;
        System.Int32 native = (int)_noiseTex.GetNativeTexturePtr();
        object parm = native;
        pGameScreen = _spyMgr.CallCustomApi(_gameProcess, _nativeDLLName, "GetGameSurface", ref parm, true);

        if (pGameScreen != 0)
        {
            _noiseTex.UpdateExternalTexture((IntPtr)pGameScreen);
        }
    }
}
