using System;
using System.Collections;
using System.IO;
using System.Runtime.InteropServices;
using UnityEngine;

// This is a subclass of the Game object.  Game object is setup to inject into and run
// a given game specified by the input arguments.  
// 
// This object is to simply show a slideshow of stereo photographs/screenshots on the
// big screen.  Same basic principle, run a game, except that there the 'game' is just
// the slideshow.

public class SlideShow : Game
{
    public Renderer screen;


    [DllImport("shell32.dll")]
    private static extern int SHGetKnownFolderPath(
     [MarshalAs(UnmanagedType.LPStruct)] Guid rfid,
     uint dwFlags,
     IntPtr hToken,
     out IntPtr pszPath  // API uses CoTaskMemAlloc
     );
    public static readonly Guid Documents = new Guid("FDD39AD0-238F-46AF-ADB4-6C85480369C7");

    // We will use the IENumerator as the asynchronous loop for showing the next slides
    // in the slideshow.  This loop will never exit until quit, but it can be paused.

    public override IEnumerator Launch()
    {
        IntPtr documentsPathName;
        SHGetKnownFolderPath(Documents, 0, IntPtr.Zero, out documentsPathName);
        string nvidiaScreenShotFolder = Marshal.PtrToStringUni(documentsPathName);
        nvidiaScreenShotFolder += @"\NVStereoscopic3D.IMG";

        string[] stereoFiles = Directory.GetFiles(nvidiaScreenShotFolder, "*.jps");

        while (true)
        {
            foreach (string file in stereoFiles)
            {
                LoadJPS(file);
                yield return new WaitForSecondsRealtime(5);
            }
        }
    }

    public override string DisplayName()
    {
        return "SlideShow";
    }

    // Bit of a hack here for SlideShow only.  In the main loop for the game, we check
    // for nonzero result from this routine, and then whether it changed from the last
    // version saved.  We don't want to sprinkle #ifdef all through the code for Demo
    // version, so this hack bypasses the check in PollForSharedSurface so that it 
    // does nothing.

    public override int GetSharedHandle()
    {
        LaunchAndPlay.gGameSharedHandle = -1;
        return -1;
    }

    // -----------------------------------------------------------------------------

    public void LoadJPS(string filePath)
    {
        Texture2D stereoTex = null;
        byte[] fileData;

        if (File.Exists(filePath))
        {
            fileData = File.ReadAllBytes(filePath);
            stereoTex = new Texture2D(2, 2);
            stereoTex.LoadImage(fileData);            //..this will auto-resize the texture dimensions.
        }

        Material screenMaterial = screen.material;
        screenMaterial.mainTexture = stereoTex;

        // The loaded image will be upside down, because JPG does not follow OpenGL coordinate
        // systems.  Invert the image in Y axis by using the TextureScale trick.
        // We can't use the normal Transform for the Screen itself, because the
        // screen needs to be dynamic for the user to specify, and if we invert
        // there we also invert the infoText.

        screenMaterial.SetTextureScale("_MainTex", new Vector2(1, -1));
    }
}
