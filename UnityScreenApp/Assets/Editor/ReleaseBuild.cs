

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

        BuildPlayerOptions buildPlayerOptions = new BuildPlayerOptions();
        buildPlayerOptions.scenes = new[] { "Assets/Movie.unity" };
        buildPlayerOptions.locationPathName = releaseFolder + "katanga.exe";
        buildPlayerOptions.target = BuildTarget.StandaloneWindows64;
        buildPlayerOptions.options = BuildOptions.None;
        BuildPipeline.BuildPlayer(buildPlayerOptions);

        // Copy the key Deviare pieces to the output folder, as the
        // Plugin folder by itself is not sufficient for SpyMgr.Init to succeed.
        FileUtil.CopyFileOrDirectory("Assets/Dependencies/deviare32.db", releaseFolder + "katanga_data/Plugins/deviare32.db");
        FileUtil.CopyFileOrDirectory("Assets/Dependencies/deviare64.db", releaseFolder + "katanga_data/Plugins/deviare64.db");
    }
}
