using System;
using System.Collections;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.IO;
using System.Text;
using System.Threading;

using UnityEngine;
using UnityEngine.XR;
using UnityEngine.UI;

using Nektra.Deviare2;


public class DrawSBS : MonoBehaviour
{
    static NktSpyMgr _spyMgr;
    static NktProcess _gameProcess;
    static string _nativeDLLName;

    // Primary Texture received from game as shared ID3D11ShaderResourceView
    // It automatically updates as the injected DLL copies the bits into the
    // shared resource.
    Texture2D _bothEyes;

//    System.Int32 _gameEventSignal = 0;

    // -----------------------------------------------------------------------------
    
    [DllImport("UnityNativePlugin64")]
    static extern void SelectGameDialog([MarshalAs(UnmanagedType.LPWStr)] StringBuilder unicodeFileName, int len);

    void Start()
    {
        int hresult;
        object continueevent;
        string drawSBS_directory = Environment.CurrentDirectory;
        _nativeDLLName = Application.dataPath + "/Plugins/DeviarePlugin.dll";
        //string game = @"G:\Games\S.T.A.L.K.E.R. Shadow of Chernobyl\bin\XR_3DA.exe";
        //string game = "C:\\Program Files (x86)\\Steam\\steam.exe -applaunch 35460";
        string game = @"G:\Games\The Ball\Binaries\Win32\theball.exe";


        // Ask user to select the game to run in virtual 3D.  
        // If they hold Ctrl at launch. Test for ctrl is in C++,
        // because Unity does not support GetKey until Update.

        int MAX_PATH = 260;
        StringBuilder sb = new StringBuilder("arf arf arf", MAX_PATH);
        SelectGameDialog(sb, sb.Capacity);
        Directory.SetCurrentDirectory(drawSBS_directory);

        if (sb.Length != 0)
            game = sb.ToString();


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
            if (result != 1)
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
        Directory.SetCurrentDirectory(drawSBS_directory);

        print("Restored Working Directory to: " + drawSBS_directory);


        // We've gotten everything launched, hooked, and setup.  Now we need to wait for the
        // game to call through to CreateDevice, so that we can create the shared surface.
        // Let's yield until that happens.

        StartCoroutine("WaitForSharedSurface");
    }


    // Our x64 Native DLL allows us direct access to DX11 in order to take
    // the shared handle and turn it into a ID3D11ShaderResourceView for Unity.
    [DllImport("UnityNativePlugin64")]
    private static extern IntPtr CreateSharedTexture(int sharedHandle);

    // WaitForSharedSurface will just wait until the CreateDevice has been called in 
    // DeviarePlugin, and thus we have created a shared surface for copying game bits into.
    // This is asynchronous because it's in the game world, and we don't know when
    // it will happen.
    //
    // Once the GetSharedSurface returns with non-null, we are ready to continue
    // with the VR side of showing those bits.

    private IEnumerator WaitForSharedSurface()
    {
        System.Int32 gameSharedHandle = 0;

        while (gameSharedHandle == 0)
        {
            // Check-in every 200ms.
            yield return new WaitForSecondsRealtime(0.2f);

            print("... WaitForSharedSurface");

            // ToDo: To work, we need to pass in a parameter? 
            // This will call to DeviarePlugin native DLL routine to fetch current gGameSurfaceShare HANDLE.
            System.Int32 native = 0; // (int)_tex.GetNativeTexturePtr();
            object parm = native;
            gameSharedHandle = _spyMgr.CallCustomApi(_gameProcess, _nativeDLLName, "GetSharedHandle", ref parm, true);
        }

        // We finally have a valid gGameSurfaceShare as a DX11 HANDLE.  
        // We can thus finish up the init.

        print("-> Got shared handle: " + gameSharedHandle.ToString("x"));


        // Call into the x64 UnityNativePlugin DLL for DX11 access, in order to create a ID3D11ShaderResourceView.
        // You'd expect this to be a IDX11Texture2D, but that's not what Unity wants.

        IntPtr shared = CreateSharedTexture(gameSharedHandle);

        // This is the Unity Texture2D, double width texture, with right eye on the left half.
        // It will always be up to date with latest game image, because we pass in 'shared'.

        _bothEyes = Texture2D.CreateExternalTexture(3200, 900, TextureFormat.BGRA32, false, true, shared);
        //_bothEyes = new Texture2D(3200, 900, TextureFormat.BGRA32, false, true);

        print("..eyes width: " + _bothEyes.width + " height: " + _bothEyes.height + " format: " + _bothEyes.format);


        // This is the primary Material for the Quad used for the virtual TV.
        // Assigning the 2x width _bothEyes texture to it means it always has valid
        // game bits.  The custom sbsShader for the material takes care of 
        // showing the correct half for each eye.

        GetComponent<Renderer>().material.mainTexture = _bothEyes;


        // These are test Quads, and will be removed.  One for each eye.
        Material leftMat = GameObject.Find("left").GetComponent<Renderer>().material;
        leftMat.mainTexture = _bothEyes;
        Material rightMat = GameObject.Find("right").GetComponent<Renderer>().material;
        rightMat.mainTexture = _bothEyes;

        // Using same primary 2x width shared texture, specify which half is used.
        leftMat.mainTextureScale = new Vector2(0.5f, 1.0f);
        leftMat.mainTextureOffset = new Vector2(0.5f, 0);
        rightMat.mainTextureScale = new Vector2(0.5f, 1.0f);
        rightMat.mainTextureOffset = new Vector2(0.0f, 0);


        // ToDo: To work, we need to pass in a parameter? 
        // This will call to DeviarePlugin native DLL routine to fetch current gGameSurfaceShare HANDLE.
        //System.Int32 dummy = 0; // (int)_tex.GetNativeTexturePtr();
        //object deviare = dummy;
        //_gameEventSignal = _spyMgr.CallCustomApi(_gameProcess, _nativeDLLName, "GetEventHandle", ref deviare, true);


//        StartCoroutine("SyncAtEndofFrame");

        yield return null;
    }


    // -----------------------------------------------------------------------------
    // Wait for the EndOfFrame, and then trigger the sync Event to allow
    // the game to continue.  Will use the _gameEventSignal from the NativePlugin
    // to trigger the Event which was actually created in the game process.
    // _gameEventSignal is a HANDLE from x32 game.

    // Our x64 Native DLL allows us direct access to DX11 in order to take
    // the shared handle and turn it into a ID3D11ShaderResourceView for Unity.
    //[DllImport("UnityNativePlugin64")]
    //private static extern void TriggerEvent(int eventHandle);

    //private IEnumerator SyncAtEndofFrame()
    //{
    //    while (true)
    //    {
    //        yield return new WaitForFixedUpdate();

    //        //TriggerEvent(_gameEventSignal);        

    //        System.Int32 dummy = 0; 
    //        object deviare = dummy;
    //        _spyMgr.CallCustomApi(_gameProcess, _nativeDLLName, "TriggerEvent", ref deviare, true);
    //    }
    //}


    // -----------------------------------------------------------------------------
    //private IEnumerator UpdateFPS()
    //{
    //    TextMesh rate = GameObject.Find("rate").GetComponent<TextMesh>();

    //    while (true)
    //    {
    //        yield return new WaitForSecondsRealtime(0.2f);

    //        float gpuTime;
    //        if (XRStats.TryGetGPUTimeLastFrame(out gpuTime))
    //        {
    //            // At 90 fps, we want to know the % of a single VR frame we are using.
    //            //    gpuTime = gpuTime / ((1f / 90f) * 1000f) * 100f;
    //            rate.text = System.String.Format("{0:F1} ms", gpuTime);
    //        }
    //    }
    //}


    // -----------------------------------------------------------------------------
    // Update is called once per frame, before rendering. Great diagram:
    // https://docs.unity3d.com/Manual/ExecutionOrder.html
    // Update is much slower than coroutines.  Unless it's required for VR, skip it.
   
    void Update()
    {
        if (Time.frameCount % 30 == 0)
            System.GC.Collect();

        if (Input.GetKey("escape"))
            Application.Quit();

        // The triangle from camera to quad edges is setup as
        //  camera: 0, 1, 0
        //  quad:   0, 2.75, 5
        // So the distance to screen is h=5, and width is w=8.
        // Triangle calculator says the inner angle and corner angle is thus
        //  1.349 rad  0.896 rad
        // h=w/2*tan(corner) => w=h*2/tan(corner) 

        double h, w;
        float width;
        if (Input.GetAxis("Mouse ScrollWheel") < 0)
        {
            this.transform.position += Vector3.back;
            h = this.transform.position.z;
            w = h * 2 / Math.Tan(0.896);
            width = (float)w;
            this.transform.localScale = new Vector3(width, -width * 9 / 16, 1);
        }
        if (Input.GetAxis("Mouse ScrollWheel") > 0)
        {
            this.transform.position += Vector3.forward;
            h = this.transform.position.z;
            w = h * 2 / Math.Tan(0.896);
            width = (float)w;
            this.transform.localScale = new Vector3(width, -width * 9 / 16, 1);
        }
    }
}

//// Sierpinksky triangles for a default view, shows if other updates fail.
//for (int y = 0; y < _tex.height; y++)
//{
//    for (int x = 0; x < _tex.width; x++)
//    {
//        Color color = ((x & y) != 0 ? Color.white : Color.grey);
//        _tex.SetPixel(x, y, color);
//    }
//}
//// Call Apply() so it's actually uploaded to the GPU
//_tex.Apply();

