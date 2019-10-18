using System;
using System.Text;
using System.Runtime.InteropServices;
using System.Threading;
using System.IO;

using Nektra.Deviare2;

using UnityEngine;


// Game object to handle launching and connection duties to the game itself.
// Primarily handles all Deviare hooking and communication.
//
// Also handles the variants of launching.
// DX9: Needs a first instruction hook, so we can replace DX9 with DX9Ex
// DX11: Normal use will wait for specified exe to show up, delayed connection.
// DX11 Direct-mode: Direct mode games currently need first instruction to catch DirectMode call.

public class Game : MonoBehaviour
{
    // Starting working directory for Unity app.
    string katanga_directory;

    // Absolute file path to the executable of the game. We use this path to start the game.
    string gamePath;

    // User friendly name of the game. This is shown on the big screen as info on launch.
    private string displayName;

    // Exe name for DX11 games only. Used to lookup process ID for injection.  If DX9 this arrives empty.
    private string waitForExe;
    private bool isDX11Game = false;

    static NktSpyMgr _spyMgr;
    static NktProcess _gameProcess = null;
    static string _nativeDLLName = null;


    // We jump out to the native C++ to open the file selection box.  There might be a
    // way to do it here in Unity, but the Mono runtime is old and creaky, and does not
    // support modern .Net, so I'm leaving it over there in C++ land.
    [DllImport("UnityNativePlugin64")]
    static extern void SelectGameDialog([MarshalAs(UnmanagedType.LPWStr)] StringBuilder unicodeFileName, int len);

    // -----------------------------------------------------------------------------
    // -----------------------------------------------------------------------------

    // We need to save and restore to this Katanga directory, or Unity editor gets super mad.

    private void Awake()
    {
        katanga_directory = Environment.CurrentDirectory;
    }

    // -----------------------------------------------------------------------------

    // Just in case we somehow managed to leave it set badly.

    private void OnApplicationQuit()
    {
        Directory.SetCurrentDirectory(katanga_directory);

        print("Katanga Quit");
    }

    // -----------------------------------------------------------------------------

    // Normal launch from 3DFM will be to specify launch arguments.
    // Any '-' arguments will be for the game itself. '--' style is for our arguments.

    // Full .exe path to launch.  --game-path: 
    // Cleaned title to display.  --game-title:
    // If dx11 proc-wait style.   --game-waitfor-exe: 

    public void ParseGameArgs(string[] args)
    {
        // Check if the CmdLine arguments include a game path to be launched.
        // We are using --game-path to make it clearly different than Unity arguments.
        for (int i = 0; i < args.Length; i++)
        {
            print(args[i]);
            if (args[i] == "--game-path")
            {
                gamePath = args[i + 1];
            }
            else if (args[i] == "--game-title")
            {
                displayName = args[i + 1];
            }
            else if (args[i] == "--game-waitfor-exe")
            {
                waitForExe = args[i + 1];
            }
        }


        // If they didn't pass a --game-path argument, then bring up the GetOpenFileName
        // dialog to let them choose.
        if (String.IsNullOrEmpty(gamePath))
        {
            // Ask user to select the game to run in virtual 3D.  
            // We are doing this super early because there are scenarios where Unity
            // has been crashing out because the working directory changes in GetOpenFileName.

            int MAX_PATH = 260;
            StringBuilder sb = new StringBuilder("", MAX_PATH);
            SelectGameDialog(sb, sb.Capacity);

            if (sb.Length != 0)
                gamePath = sb.ToString();
        }

        if (String.IsNullOrEmpty(gamePath))
            throw new Exception("No game specified to launch.");
     
        // If game title wasn't passed via cmd argument then take the name of the game exe as the title instead
        if (String.IsNullOrEmpty(displayName))
        {
            displayName = gamePath.Substring(gamePath.LastIndexOf('\\') + 1);
        }

        isDX11Game = String.IsNullOrEmpty(waitForExe) ? false : true;
    }

    // -----------------------------------------------------------------------------

    public string DisplayName()
    {
        return displayName;
    }


    // -----------------------------------------------------------------------------

    // When the gameProcess dies, the targeted game will have exited.
    // We can't just simply use the _gameProcess.IsActive however.  Because of
    // some stupid Unity/Mono thing, that routine defaults always to the full
    // one second timeout, and we cannot stall the main Unity thread like that.
    // This thus just keeps looking up the named exe instead, which should
    // be fast and cause no problems.

    public bool Exited()
    {
        bool hasQuit = (_spyMgr.FindProcessId(waitForExe) == 0);
        if (hasQuit)
            print("Game has exited.");

        return hasQuit;
    }

    // -----------------------------------------------------------------------------

    public System.Int32 GetSharedHandle()
    {
        // ToDo: To work, we need to pass in a parameter? Could use named pipe instead.
        // This will call to DeviarePlugin native DLL in the game, to fetch current gGameSurfaceShare HANDLE.
        System.Int32 native = 0; // (int)_tex.GetNativeTexturePtr();
        object parm = native;
        System.Int32 pollHandle = _spyMgr.CallCustomApi(_gameProcess, _nativeDLLName, "GetSharedHandle", ref parm, true);

        return pollHandle;
    }

    // -----------------------------------------------------------------------------

    // When launching in DX9, we will continue to use the Deviare direct launch, so
    // that we can hook Direct3DCreate9 before it is called, and convert it to 
    // Direct3DCreate9Ex.  For DX11 games, 3DFM will have already launched the game
    // using its normal techniques, and we will find it via gameProc ID and inject
    // directly without hooking anything except Present.

    public void Launch()
    {
        int hresult;
        object continueevent = null;

        print("Running: " + gamePath + "\n");

        string wd = System.IO.Directory.GetCurrentDirectory();
        print("WorkingDirectory: " + wd);
        print("CurrentDirectory: " + katanga_directory);

        //print("App Directory:" + Environment.CurrentDirectory);
        //foreach (var path in Directory.GetFileSystemEntries(Environment.CurrentDirectory))
        //    print(System.IO.Path.GetFileName(path)); // file name
        //foreach (var path in Directory.GetFileSystemEntries(Environment.CurrentDirectory + "\\Assets\\Plugins\\"))
        //    print(System.IO.Path.GetFileName(path)); // file name

        _spyMgr = new NktSpyMgr();
        hresult = _spyMgr.Initialize();
        if (hresult != 0)
            throw new Exception("Deviare Initialize error.");
#if _DEBUG
    _spyMgr.SettingOverride("SpyMgrDebugLevelMask", 0x2FF8);
    // _spyMgr.SettingOverride("SpyMgrAgentLevelMask", 0x040);
#endif
        print("Successful SpyMgr Init");



        // We must set the game directory specifically, otherwise it winds up being the 
        // C# app directory which can make the game crash.  This must be done before CreateProcess.
        // This also changes the working directory, which will break Deviare's ability to find
        // the NativePlugin, so we'll use full path descriptions for the DLL load.
        // This must be reset back to the Unity game directory, otherwise Unity will
        // crash with a fatal error.

        Directory.SetCurrentDirectory(Path.GetDirectoryName(gamePath));
        {
            bool suspend = (isDX11Game) ? false : true;

            print("Launching: " + gamePath + "...");
            _gameProcess = _spyMgr.CreateProcess(gamePath, suspend, out continueevent);
            if (_gameProcess == null)
                throw new Exception("CreateProcess game launch failed: " + gamePath);
            
            // If DX11, let's wait and watch for game exe to launch.
            // This works a lot better than launching it here and hooking
            // first instructions, because we can wait past launchers or 
            // Steam launch itself, or different sub processes being launched.

            if (isDX11Game)
            {
                int procid = 0;

                print("Waiting for process: " + waitForExe);
                Thread.Sleep(3000);     // ToDo: needed? Letting game get underway.

                do
                {
                    if (Input.GetKey("escape"))
                        Application.Quit();
                    Thread.Sleep(500);
                    procid = _spyMgr.FindProcessId(waitForExe);
                } while (procid == 0);

                print("->Found " + waitForExe + ":" + procid);
                _gameProcess = _spyMgr.ProcessFromPID(procid);
            }
            else  // DX9 game, or DX11 requiring first instruction hook
            {
                waitForExe = gamePath.Substring(gamePath.LastIndexOf('\\') + 1);
            }


            print("LoadAgent");
            _spyMgr.LoadAgent(_gameProcess);

            // Load the NativePlugin for the C++ side.  The NativePlugin must be in this app folder.
            // The Agent supports the use of Deviare in the CustomDLL, but does not respond to hooks.
            //
            // The native DeviarePlugin has two versions, one for x32, one for x64, so we can handle
            // either x32 or x64 games.

            print("Load DeviarePlugin");
            if (_gameProcess.PlatformBits == 64)
                _nativeDLLName = Application.dataPath + "/Plugins/DeviarePlugin64.dll";
            else
                _nativeDLLName = Application.dataPath + "/Plugins/DeviarePlugin.dll";

            int loadResult = _spyMgr.LoadCustomDll(_gameProcess, _nativeDLLName, true, true);
            if (loadResult <= 0)
            {
                int lastHR = GetLastDeviareError();
                string deadbeef = String.Format("Could not load {0}: 0x{1:X}", _nativeDLLName, lastHR);
                throw new Exception(deadbeef);
            }

            print(String.Format("Successfully loaded {0}", _nativeDLLName));


            // Hook the primary DX11 creation calls of CreateDevice, CreateDeviceAndSwapChain,
            // CreateDXGIFactory, and CreateDXGIFactory1.  These are all direct exports for either
            // D3D11.dll, or DXGI.dll. All DX11 games must call one of these interfaces to 
            // create a SwapChain.  These must be spelled exactly right, including Case.
            //
            // Only hooking single call now, D3D11CreateDevice so that Deviare is activated.
            // This call does not hook other calls, and seems to be necessary for the Agent
            // to activate in the gameProcess.  This will also activate the DX9 path.

            print("Hook the D3D11.DLL!D3D11CreateDevice...");
            NktHook deviceHook = _spyMgr.CreateHook("D3D11.DLL!D3D11CreateDevice", 0);
            if (deviceHook == null)
                throw new Exception("Failed to hook D3D11.DLL!D3D11CreateDevice");
            deviceHook.AddCustomHandler(_nativeDLLName, 0, "");
            deviceHook.Attach(_gameProcess, true);
            deviceHook.Hook(true);

            // But if we happen to be launching direct with a selected exe, or DX9 game,
            // we can still hook the old way.

            if (!isDX11Game)
            {
                print("Hook the D3D11.DLL!D3D11CreateDeviceAndSwapChain...");
                NktHook deviceAndSwapChainHook = _spyMgr.CreateHook("D3D11.DLL!D3D11CreateDeviceAndSwapChain", 0);
                if (deviceAndSwapChainHook == null)
                    throw new Exception("Failed to hook D3D11.DLL!D3D11CreateDeviceAndSwapChain");

                print("Hook the DXGI.DLL!CreateDXGIFactory...");
                NktHook factoryHook = _spyMgr.CreateHook("DXGI.DLL!CreateDXGIFactory", (int)eNktHookFlags.flgOnlyPostCall);
                if (factoryHook == null)
                    throw new Exception("Failed to hook DXGI.DLL!CreateDXGIFactory");

                print("Hook the DXGI.DLL!CreateDXGIFactory1...");
                NktHook factory1Hook = _spyMgr.CreateHook("DXGI.DLL!CreateDXGIFactory1", (int)eNktHookFlags.flgOnlyPostCall);
                if (factory1Hook == null)
                    throw new Exception("Failed to hook DXGI.DLL!CreateDXGIFactory1");


                // Hook the primary DX9 creation call of Direct3DCreate9, which is a direct export of 
                // the d3d9 DLL.  All DX9 games must call this interface, or the Direct3DCreate9Ex.
                // This is not hooked here though, it is hooked in DeviarePlugin at OnLoad.
                // We need to do special handling to fetch the System32 version of d3d9.dll,
                // in order to avoid unhooking HelixMod's d3d9.dll.

                // Hook the nvapi.  This is required to support Direct Mode in the driver, for 
                // games like Tomb Raider and Deus Ex that have no SBS.
                // There is only one call in the nvidia dll, nvapi_QueryInterface.  That will
                // be hooked, and then the _NvAPI_Stereo_SetDriverMode call will be hooked
                // so that we can see when a game sets Direct Mode and change behavior in Present.
                // This is also done in DeviarePlugin at OnLoad.


                // Make sure the CustomHandler in the NativePlugin at OnFunctionCall gets called when this 
                // object is created. At that point, the native code will take over.

                deviceAndSwapChainHook.AddCustomHandler(_nativeDLLName, 0, "");
                factoryHook.AddCustomHandler(_nativeDLLName, 0, "");
                factory1Hook.AddCustomHandler(_nativeDLLName, 0, "");

                // Finally attach and activate the hook in the still suspended game process.

                deviceAndSwapChainHook.Attach(_gameProcess, true);
                deviceAndSwapChainHook.Hook(true);
                factoryHook.Attach(_gameProcess, true);
                factoryHook.Hook(true);
                factory1Hook.Attach(_gameProcess, true);
                factory1Hook.Hook(true);

                // Ready to go.  Let the game startup.  When it calls Direct3DCreate9, we'll be
                // called in the NativePlugin::OnFunctionCall

                print("Continue game launch...");
                _spyMgr.ResumeProcess(_gameProcess, continueevent);
            }
        }
        Directory.SetCurrentDirectory(katanga_directory);

        print("Restored Working Directory to: " + katanga_directory);

        // We've gotten everything launched, hooked, and setup.  Now we need to wait for the
        // game to call through to CreateDevice, so that we can create the shared surface.
    }

    // -----------------------------------------------------------------------------

    // Deviare has a bizarre model where they don't actually return HRESULT for calls
    // that are defined that way.  Suggestion is to use GetLastError to get the real
    // error.  This is problematic, because the DeviareCOM.dll must be found to do
    // this. So, encapsulating all that here to get and print the real error.
    //
    // Also for some damn reason the LoadCustomDLL call can also return 2, not just
    // 1, so that's extra special.  0 means it failed.  Backwards of HRESULT.
    //
    // https://github.com/nektra/Deviare2/issues/32

    [DllImport("DeviareCOM64.dll")]
    static extern int GetLastErrorCode();

    int GetLastDeviareError()
    {
        // We set back to the katanga_directory here, in case we throw
        // an error.  This keeps the editor from crashing.
        string activeDirectory = Directory.GetCurrentDirectory();
        Directory.SetCurrentDirectory(katanga_directory);

        int result;
        result = GetLastErrorCode();
        print(string.Format("Last Deviare error: 0x{0:X}", result));

        Directory.SetCurrentDirectory(activeDirectory);

        return result;
    }

}

