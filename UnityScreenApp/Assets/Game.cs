using System;
using System.Text;
using System.Runtime.InteropServices;
using System.Threading;
using System.IO;

using Nektra.Deviare2;

using UnityEngine;
using System.Diagnostics;
using System.Collections;


// Also handles the variants of launching.
//  DX9: Needs a first instruction hook, so we can replace DX9 with DX9Ex
//  DX9Ex: Rare. Can do late binding, but specifies to look for CreateDX9Ex
//  DirectModeDX11: Direct mode games currently need first instruction to catch DirectMode call.
//  DirectModeDX9: Direct mode, but DX9 API.  Used for OpenGL wrapper
//  Steam: Steam version preferred use will launch using Steam.exe -applaunch
//  Exe: Non-Steam exe.  Will launch exe directly.

    // Duplicated in 3DFM startGameWithKatanga.  Must be kept in sync.
enum LaunchType
{
    DX9,                // Requires SpyMgr launch
    DX9Ex,              // For games that require DX9Ex. Late-binding injection via waitForExe.
    DirectModeDX11,     // Requires SpyMgr launch to watch for DirectMode
    DirectModeDX9Ex,    // Requires SpyMgr launch, used for OpenGL wrapper games
    Steam,              // Steam.exe is available, use -applaunch to avoid relaunchers.
    Exe                 // Implies DX11 direct Exe launch, but only for non-Steam games.
}

// Game object to handle launching and connection duties to the game itself.
// Primarily handles all Deviare hooking and communication.

public class Game : MonoBehaviour
{
    // Starting working directory for Unity app.
    string katanga_directory;

    // Absolute file path to the executable of the game. We use this path to start the game.
    string gamePath;

    // User friendly name of the game. This is shown on the big screen as info on launch.
    string displayName;

    // Launch type, specified by 3DFM and passed in here as --launch-type.
    LaunchType launchType;

    // Becomes concatenated version of all arguments so we can properly pass them to the game.
    string launchArguments = "";

    // We can improve launch behavior if we know specific exe to wait for, because we skip 
    // launchers.  If it's DX9 launchType+ waitForExe, that lets us know it's a DX9Ex game.
    string waitForExe = "";

    // If it's a Steam launch, these will be non-null.
    // ToDo: Non-functional Steam launch because DesktopGameTheater intercepts.
    string steamPath;
    string steamAppID;


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

    public static T ParseLaunchType<T>(string value)
    {
        return (T)Enum.Parse(typeof(T), value, true);
    }

    // Normal launch from 3DFM will be to specify launch arguments.
    // Any '-' arguments will be for the game itself. '--' style is for our arguments.

    // Full .exe path to launch.  --game-path: 
    // Cleaned title to display.  --game-title:
    // Launch type as Enum        --launch-type:
    // Exe name to spinwait for   --waitfor-exe:
    // Full path to Steam.exe     --steam-path:
    // Game SteamAppID            --steam-appid:

    public void ParseGameArgs(string[] args)
    {
        for (int i = 1; i < args.Length; i++)
        {
            if (args[i] == "--game-path")
            {
                i++;
                gamePath = args[i];
            }
            else if (args[i] == "--game-title")
            {
                i++;
                displayName = args[i];
            }
            else if (args[i] == "--launch-type")
            {
                i++;
                launchType = ParseLaunchType<LaunchType>(args[i]);
            }
            else if (args[i] == "--waitfor-exe")
            {
                i++;
                waitForExe = args[i];
            }
            else if (args[i] == "--steam-path")
            {
                i++;
                steamPath = args[i];
            }
            else if (args[i] == "--steam-appid")
            {
                i++;
                steamAppID = args[i];
            }
            else
            {
                // Accumulate all other arguments into launchArguments for the game,
                // for things like -window-mode exclusive.  We start at 1 in the loop,
                // to skip past the katanga.exe input parameter.
                launchArguments += args[i] + " ";
            }
        }

        //gamePath = @"W:\SteamLibrary\steamapps\common\Prey\Binaries\Danielle\x64\Release\prey.exe";
        //displayName = "prey";
        //launchType = LaunchType.Exe;
        //steamPath = @"C:\Program Files (x86)\Steam";
        //steamAppID = "480490";

        //gamePath = @"W:\SteamLibrary\steamapps\common\Kingdoms of Amalur - Reckoning Demo\reckoningdemo.exe";
        //displayName = "Reck";
        //launchType = LaunchType.DX9;
        //steamPath = @"C:\Program Files (x86)\Steam";
        //steamAppID = "102501";

        //gamePath = @"W:\SteamLibrary\steamapps\common\Portal 2\portal2.exe";
        //displayName = "portal 2";
        //launchType = LaunchType.DX9;

        //gamePath = @"W:\SteamLibrary\steamapps\common\Bayonetta\bayonetta.exe";
        //displayName = "bayonetta";
        //waitForExe = "bayonetta.exe";
        //launchType = LaunchType.DX9Ex;

        //gamePath = @"W:\Games\SOMA\soma.exe";
        //displayName = "soma";
        //launchType = LaunchType.DirectModeDX9Ex;
        //waitForExe = "soma.exe";

        //gamePath = @"W:\SteamLibrary\steamapps\common\Life Is Strange\Binaries\Win32\lifeisstrange.exe";
        //launchType = LaunchType.Steam;
        //displayName = "Life is";
        //steamAppID = "319630";


        //gamePath = @"W:\SteamLibrary\steamapps\common\DiRT 4\dirt4.exe";
        //displayName = "Dirt4";
        //launchType = LaunchType.Exe;

        //gamePath = @"W:\SteamLibrary\steamapps\common\Headlander\Headlander.exe";
        //displayName = "Headlander";
        //launchType = LaunchType.Exe;
        //steamPath = @"C:\Program Files (x86)\Steam";
        //steamAppID = "340000";

        //gamePath = @"W:\SteamLibrary\steamapps\common\Tomb Raider\tombraider.exe";
        //displayName = "Tomb Raider";
        //launchType = LaunchType.DirectModeDX11;
        //steamPath = @"C:\Program Files (x86)\Steam";
        //steamAppID = "203160";

        //steamPath = @"C:\Program Files (x86)\Steam";
        //steamAppID = "39210";
        //waitForExe = "ffxiv_dx11.exe";
        //launchType = LaunchType.Steam;
        //gamePath = @"W:\SteamLibrary\steamapps\common\FINAL FANTASY XIV Online\game\ffxiv_dx11.exe";


        // If they didn't pass a --game-path argument, then bring up the GetOpenFileName
        // dialog to let them choose. More for testing, not a usual path.
        if (String.IsNullOrEmpty(gamePath) && String.IsNullOrEmpty(waitForExe))
        {
            // Ask user to select the game to run in virtual 3D.  
            // We are doing this super early because there are scenarios where Unity
            // has been crashing out because the working directory changes in GetOpenFileName.

            int MAX_PATH = 260;
            StringBuilder sb = new StringBuilder("", MAX_PATH);
            SelectGameDialog(sb, sb.Capacity);

            if (sb.Length != 0)
            {
                gamePath = sb.ToString();
                displayName = gamePath.Substring(gamePath.LastIndexOf('\\') + 1);
                launchType = LaunchType.Exe;
            }
        }

        if (String.IsNullOrEmpty(gamePath))
            throw new Exception("No game specified to launch.");
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
        if (_gameProcess == null)
            return false;

        if (_spyMgr.FindProcessId(_gameProcess.Name) == 0)
        {
            print("Game has exited.");
            return true;
        }

        return false;
    }

    // -----------------------------------------------------------------------------

    public System.Int32 GetSharedHandle()
    {
        if (_gameProcess == null)
            return 0;

        // ToDo: To work, we need to pass in a parameter? Could use named pipe instead.
        // This will call to DeviarePlugin native DLL in the game, to fetch current gGameSurfaceShare HANDLE.
        System.Int32 native = 0; // (int)_tex.GetNativeTexturePtr();
        object parm = native;
        System.Int32 pollHandle = _spyMgr.CallCustomApi(_gameProcess, _nativeDLLName, "GetSharedHandle", ref parm, true);

        return pollHandle;
    }

    // -----------------------------------------------------------------------------

    // When launching in DX9 or DirectMode, we will continue to use the Deviare direct 
    // launch, so that we can hook Direct3DCreate9 before it is called, and convert it to 
    // Direct3DCreate9Ex.  
    // For DX11 games, we will launch the game either by Steam -applaunch, or by exe.
    // And we will find it via gameProc ID and inject directly without hooking anything 
    // except Present. 
    // In either case, we do the hooking in the OnLoad call in the deviare plugin.

    public IEnumerator Launch()
    {
        int hresult;
        object continueevent = null;
        NktProcess gameProc = null;

        print("CurrentDirectory: " + katanga_directory);

        _spyMgr = new NktSpyMgr();
        hresult = _spyMgr.Initialize();
        if (hresult != 0)
            throw new Exception("Hook library initialize error.");
#if _DEBUG
    _spyMgr.SettingOverride("SpyMgrDebugLevelMask", 0x2FF8);
    // _spyMgr.SettingOverride("SpyMgrAgentLevelMask", 0x040);
#endif
        print("Successful hook library Init");


        // We must set the game directory specifically, otherwise it winds up being the 
        // C# app directory which can make the game crash.  This must be done before CreateProcess.
        // This also changes the working directory, which will break Deviare's ability to find
        // the NativePlugin, so we'll use full path descriptions for the DLL load.
        // This must be reset back to the Unity game directory, otherwise Unity will
        // crash with a fatal error.

        Directory.SetCurrentDirectory(Path.GetDirectoryName(gamePath));
        {
            // Treat input Steam launch as straight exe launch, because they ruin it by forcing Katanga 
            // to exit so they can do their dumb game theater.
            // Probably will break some double-launch games that need SteamAPI, but fixes always launching
            // SteamGameTheater. I'm not sure this is the right path, but people are already getting
            // confused by the DGT.

            if (launchType == LaunchType.Steam)
                launchType = LaunchType.Exe;

            print("Launch type: " + launchType);

            switch (launchType)
            {
                case LaunchType.DX9:
                case LaunchType.DirectModeDX9Ex:
                case LaunchType.DirectModeDX11:
                    StartGameBySpyMgr(gamePath, out continueevent);
                    break;

                case LaunchType.Steam:
                    StartGameBySteamAppID(steamPath, steamAppID, launchArguments);
                    break;

                case LaunchType.DX9Ex:
                case LaunchType.Exe:
                default:
                    StartGameByExeFile(gamePath, launchArguments);
                    break;
            }

            // For any launch, let's wait and watch for game exe to launch.
            // This works a lot better than launching it here and hooking
            // first instructions, because we can wait past launchers or 
            // Steam launch itself, or different sub processes being launched.
            // In scenarios where we are using the SpyMgr launch, this check
            // will still succeed.  Putting this here allows us to return the
            // IEnumerator so that this can be asynchronous from the VR UI,
            // and thus not hang their VR environment while launching.

            string gameExe;
            if (String.IsNullOrEmpty(waitForExe))
                gameExe = gamePath.Substring(gamePath.LastIndexOf('\\') + 1);
            else
                gameExe = waitForExe;

            // We finally answered the question for whether we want a startup delay or
            // not, and the answer is yes.  Battlefield3 in particular has a retarded
            // browser based launcher, which runs after we launch bf3.exe.  If we 
            // catch that first one, we exit when it exits, and miss the entire game.
            // Now that we have a coroutine, we can stall without it impacting the VR view.
            // Waiting for 8 seconds is not bad, no game will launch faster than that
            // anyway.  Waiting for 3 seconds would still fail, 5 seconds was OK,
            // but this is on a super fast machine.

            print("Waiting for process: " + gamePath);

            yield return new WaitForSecondsRealtime(8.0f);

            int procid = 0;
            do
            {
                yield return new WaitForSecondsRealtime(0.100f);

                procid = _spyMgr.FindProcessId(gameExe);
            } while (procid == 0);

            print("->Found " + gameExe + ":" + procid);

            gameProc = _spyMgr.ProcessFromPID(procid);


            // Game has been launched.  Either deferred, or first instruction hook.
            // gameProc will exist, or we forced an exception.  Time for injection.

            print("LoadAgent");
            _spyMgr.LoadAgent(gameProc);

            // Load the NativePlugin for the C++ side.  The NativePlugin must be in this app folder.
            // The Agent supports the use of Deviare in the CustomDLL, but does not respond to hooks.
            //
            // The native DeviarePlugin has two versions, one for x32, one for x64, so we can handle
            // either x32 or x64 games.

            print("Load GamePlugin");
            if (gameProc.PlatformBits == 64)
                _nativeDLLName = Application.dataPath + "/Plugins/GamePlugin64.dll";
            else
                _nativeDLLName = Application.dataPath + "/Plugins/GamePlugin.dll";

            int loadResult = _spyMgr.LoadCustomDll(gameProc, _nativeDLLName, true, true);
            if (loadResult <= 0)
            {
                int lastHR = GetLastDeviareError();
                string deadbeef = String.Format("Could not load {0}: 0x{1:X}", _nativeDLLName, lastHR);
                throw new Exception(deadbeef);
            }

            print(String.Format("Successfully loaded {0}", _nativeDLLName));


            // Hook the appropriate calls, based on game launch type.

            switch (launchType)
            {
                case LaunchType.DX9:
                    HookDX9(_nativeDLLName, gameProc);
                    _spyMgr.ResumeProcess(gameProc, continueevent);
                    break;
                case LaunchType.DX9Ex:
                    HookDX9Ex(_nativeDLLName, gameProc);
                    break;
                case LaunchType.DirectModeDX11:
                    HookDX11(_nativeDLLName, gameProc);
                    _spyMgr.ResumeProcess(gameProc, continueevent);
                    break;
                case LaunchType.DirectModeDX9Ex:
                    HookDX9Ex(_nativeDLLName, gameProc);
                    _spyMgr.ResumeProcess(gameProc, continueevent);
                    break;
                case LaunchType.Steam:
                    HookDX11(_nativeDLLName, gameProc);
                    break;
                case LaunchType.Exe:
                    HookDX11(_nativeDLLName, gameProc);
                    break;
            }
        }
        Directory.SetCurrentDirectory(katanga_directory);

        print("Restored Working Directory to: " + katanga_directory);

        // We've gotten everything launched, hooked, and setup.  Now we wait for the
        // game to call through to CreateDevice, so that we can create the shared surface.
        // Now we can also set the _gameProcess, so that other async routines can see 
        // it as valid.  We can't do this earlier, because it could race condition into
        // a process that was not setup.

        _gameProcess = gameProc;
    }

    // -----------------------------------------------------------------------------

    // Only hooking single call now, D3D11CreateDevice so that Deviare is activated.
    // This call does not hook other calls, and it seems to be necessary to activate a
    // hook so that the Agent is activated in the gameProcess.
    // 
    // Also hooks the nvapi.  This is required to support Direct Mode in the driver, for 
    // games like Tomb Raider and Deus Ex that have no SBS.
    // There is only one call in the nvidia dll, nvapi_QueryInterface.  That will
    // be hooked, and then the _NvAPI_Stereo_SetDriverMode call will be hooked
    // so that we can see when a game sets Direct Mode and change behavior in Present.
    // This is also done in DeviarePlugin at OnLoad.

    private void HookDX11(string katangaDll, NktProcess gameProc)
    {
        print("Hook the D3D11.DLL!D3D11CreateDevice...");

        NktHook deviceHook = _spyMgr.CreateHook("D3D11.DLL!D3D11CreateDevice", 0);
        if (deviceHook == null)
            throw new Exception("Failed to hook D3D11.DLL!D3D11CreateDevice");
        deviceHook.AddCustomHandler(katangaDll, 0, "");
        deviceHook.Attach(gameProc, true);
        deviceHook.Hook(true);
    }

    // -----------------------------------------------------------------------------

    // Hook the primary DX9 creation call of Direct3DCreate9, which is a direct export of 
    // the d3d9 DLL.  All DX9 games must call this interface, or the Direct3DCreate9Ex.
    // It is actually hooked in DeviarePlugin at OnLoad, rather than use these hooks, because
    // we need to do special handling to fetch the System32 version of d3d9.dll,
    // in order to avoid unhooking HelixMod's d3d9.dll.  However, these will still log 
    // calls, and also we need to hook something in order to activate the native DLL.

    private void HookDX9(string katangaDLL, NktProcess gameProc)
    {
        // We set this to flgOnlyPreCall, because we are just logging these.

        print("Hook the D3D9.DLL!Direct3DCreate9...");
        NktHook create9Hook = _spyMgr.CreateHook("D3D9.DLL!Direct3DCreate9", (int)eNktHookFlags.flgOnlyPreCall);
        if (create9Hook == null)
            throw new Exception("Failed to hook D3D9.DLL!Direct3DCreate9");
        create9Hook.AddCustomHandler(katangaDLL, 0, "");
        create9Hook.Attach(gameProc, true);
        create9Hook.Hook(true);
    }

    // Same as DX9, but targeting DX9Ex API, which makes it simpler because we can just
    // do Kiero style find the Present call, and not need to tweak texture calls.

    private void HookDX9Ex(string katangaDLL, NktProcess gameProc)
    {
        print("Hook the D3D9.DLL!Direct3DCreate9Ex...");
        NktHook create9HookEx = _spyMgr.CreateHook("D3D9.DLL!Direct3DCreate9Ex", (int)eNktHookFlags.flgOnlyPreCall);
        if (create9HookEx == null)
            throw new Exception("Failed to hook D3D9.DLL!Direct3DCreate9Ex");
        create9HookEx.AddCustomHandler(katangaDLL, 0, "");
        create9HookEx.Attach(gameProc, true);
        create9HookEx.Hook(true);
    }

    // -----------------------------------------------------------------------------

    // For DX9 games or DX11 that require first instruction hook, we need to launch
    // their exe directly.  This is inferior in a lot of respects. It sometimes hangs
    // at launch, and cannot handle pre-game launchers or things like Origin launches.
    // Should be path of last resort, but required for DX9 and DirectMode games.

    private NktProcess StartGameBySpyMgr(string game, out object continueevent)
    {
        print("Launching: " + game + "...");

        NktProcess gameProc = _spyMgr.CreateProcess(game, true, out continueevent);
        if (gameProc == null)
            throw new Exception("CreateProcess game launch failed: " + game);

        return gameProc;
    }

    
    // -----------------------------------------------------------------------------

    // For Non-Steam, DX11 games, we still want to do a deferred launch so that we can
    // more reliably hook the games.  But, we can't use Steam, so let's just launch the
    // exe directly.  

    public Process StartGameByExeFile(string exePath, string arguments)
    {
        Process proc;

        print("Start game with game exe path: " + exePath);
        print("launchArguments: " + arguments);

        proc = new Process();
        proc.StartInfo.FileName = exePath;
        proc.StartInfo.Arguments = arguments;
        proc.StartInfo.WorkingDirectory = Path.GetDirectoryName(exePath);

        // ToDo: necessary here?
        //if (fixProfile.RunGameAsAdmin == 1)
        //{
        //    proc.StartInfo.Verb = "runas";
        //}

        proc.Start();

        return proc;
    }
    
    // -----------------------------------------------------------------------------

    // Katanga is now responsible for launching the game, so we need to do it the same
    // was as 3DFM.  In particular, the steam launch should be done using the gameId
    // and steam.exe so that we do not see the double launch behavior of some games.
    // Steam suggests relaunching games if they were directly launched by exe, and 
    // this causes us to never connect to some games, because we hook the first launch,
    // but not the second.  

    private Process StartGameBySteamAppID(string steamDir, string appID, string arguments)
    {
        Process proc;

        print("Start game with Steam App Id: " + appID);
        print("launchArguments: " + arguments);

        if (!String.IsNullOrEmpty(steamDir))
        {
            proc = new Process();

            print("Starting game by calling Steam.exe: " + steamDir);
            proc.StartInfo.FileName = Path.Combine(steamDir, "Steam.exe");
            proc.StartInfo.Arguments = "-applaunch " + appID + " " + arguments;
            proc.StartInfo.WorkingDirectory = steamDir;

            // ToDo: necessary here?
            //if (fixProfile.RunGameAsAdmin == 1)
            //{
            //    proc.StartInfo.Verb = "runas";
            //}

            proc.Start();
        }
        else
        {
            print("Starting game by calling steam://rungameid");
            proc = Process.Start("steam://rungameid/" + appID + "//" + arguments);
        }

        return proc;
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
        print(string.Format("Last hook library error: 0x{0:X}", result));

        Directory.SetCurrentDirectory(activeDirectory);

        return result;
    }

}

