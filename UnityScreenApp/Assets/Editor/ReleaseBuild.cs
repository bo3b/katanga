

using System.IO;
using UnityEditor;
using UnityEngine;

public class ReleaseBuild : MonoBehaviour
{
    [MenuItem("Build/Release Build _F5")]
    public static void Build()
    {
        string releaseFolder = "../Release/";

        // Always clean build, delete the prior build completely.
        FileUtil.DeleteFileOrDirectory(releaseFolder);

        UnityEditor.WindowsStandalone.UserBuildSettings.copyPDBFiles = false;

        BuildPlayerOptions buildPlayerOptions = new BuildPlayerOptions();
        buildPlayerOptions.scenes = new[] { "Assets/BigScreen 3D.unity" };
        buildPlayerOptions.locationPathName = releaseFolder + "katanga.exe";
        buildPlayerOptions.target = BuildTarget.StandaloneWindows64;
        buildPlayerOptions.options = BuildOptions.None;
        BuildPipeline.BuildPlayer(buildPlayerOptions);


        // Setup for Demo, but drop all the BonusShots for ReleaseBuilds, to save space.
        //FileUtil.CopyFileOrDirectory("Stereo Pictures", releaseFolder + "Stereo Pictures");
        //FileUtil.DeleteFileOrDirectory(releaseFolder + "Stereo Pictures/BonusShots");

    }

    [MenuItem("Build/Debug Build")]
    public static void DebugBuild()
    {
        string releaseFolder = "../Debug/";

        // Always clean build, delete the prior build completely.
        FileUtil.DeleteFileOrDirectory(releaseFolder);

        BuildPlayerOptions buildPlayerOptions = new BuildPlayerOptions();
        buildPlayerOptions.scenes = new[] { "Assets/BigScreen 3D.unity" };
        buildPlayerOptions.locationPathName = releaseFolder + "katanga.exe";
        buildPlayerOptions.target = BuildTarget.StandaloneWindows64;
        buildPlayerOptions.options = BuildOptions.Development | BuildOptions.ShowBuiltPlayer;
        BuildPipeline.BuildPlayer(buildPlayerOptions);


        FileUtil.CopyFileOrDirectory("Stereo Pictures", releaseFolder + "Stereo Pictures");

        //FileUtil.CopyFileOrDirectory("Assets/Dependencies/katanga.exe.manifest", releaseFolder + "katanga_data/Plugins/katanga.exe.manifest");
    }


    // Demo build, which will remove all game functionality except being able to show stereo photos on the big screen.

    [MenuItem("Build/Demo Build")]
    public static void DemoBuild()
    {
        string demoFolder = "../Demo/";

        // Always clean build, delete the prior build completely.
        FileUtil.DeleteFileOrDirectory(demoFolder);

        UnityEditor.WindowsStandalone.UserBuildSettings.copyPDBFiles = false;

        BuildPlayerOptions buildPlayerOptions = new BuildPlayerOptions();
        buildPlayerOptions.scenes = new[] { "Assets/BigScreen 3D.unity" };
        buildPlayerOptions.locationPathName = demoFolder + "HelixVisionDemo.exe";
        buildPlayerOptions.target = BuildTarget.StandaloneWindows64;
        buildPlayerOptions.options = BuildOptions.ShowBuiltPlayer;
        BuildPipeline.BuildPlayer(buildPlayerOptions);

        // Add in our sample stereo pictures. Can be anything in the folder however.
        FileUtil.CopyFileOrDirectory("Stereo Pictures", demoFolder + "Stereo Pictures");

    }
}
