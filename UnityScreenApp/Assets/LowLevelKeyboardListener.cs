/* 
Copyright (c) 2019 dylansweb.com
Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:
The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

using System;
using System.Collections;
using System.Runtime.InteropServices;
using System.IO;
using System.Text;

using UnityEngine;
using UnityEngine.UI;

using Nektra.Deviare2;
using System.Diagnostics;
using System.Collections.Generic;

using System.Linq;

// We have to use a low level Keyboard listener because Unity's built in listener doesn't 
// detect keyboard events when the Unity app isn't in the foreground

// private LowLevelKeyboardListener _listener;


public class LowLevelKeyboardListener
{
    private const int WH_KEYBOARD_LL = 13;
    private const int WM_KEYDOWN = 0x0100;
    private const int WM_SYSKEYDOWN = 0x0104;

    [DllImport("user32.dll", CharSet = CharSet.Auto, SetLastError = true)]
    private static extern IntPtr SetWindowsHookEx(int idHook, LowLevelKeyboardProc lpfn, IntPtr hMod, uint dwThreadId);

    [DllImport("user32.dll", CharSet = CharSet.Auto, SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool UnhookWindowsHookEx(IntPtr hhk);

    [DllImport("user32.dll", CharSet = CharSet.Auto, SetLastError = true)]
    private static extern IntPtr CallNextHookEx(IntPtr hhk, int nCode, IntPtr wParam, IntPtr lParam);

    [DllImport("kernel32.dll", CharSet = CharSet.Auto, SetLastError = true)]
    private static extern IntPtr GetModuleHandle(string lpModuleName);

    public delegate IntPtr LowLevelKeyboardProc(int nCode, IntPtr wParam, IntPtr lParam);

    public event EventHandler<KeyPressedArgs> OnKeyPressed;

    private LowLevelKeyboardProc _proc;
    private IntPtr _hookID = IntPtr.Zero;

    //public Text qualityText;

    public LowLevelKeyboardListener()
    {
        _proc = HookCallback;

        // hook keyboard to detect when the user presses a button
        //        _listener = new LowLevelKeyboardListener();
        //        _listener.OnKeyPressed += _listener_OnKeyPressed;
        //        _listener.HookKeyboard();


        // Set the Quality level text on the floor to match whatever we start with.
        //qualityText.text = "Quality: " + QualitySettings.names[QualitySettings.GetQualityLevel()];
    }

    // Destructor or exit- mandatory, otherwise random crashes.
    //        _listener.UnHookKeyboard();

    public void HookKeyboard()
    {
        _hookID = SetHook(_proc);
    }

    public void UnHookKeyboard()
    {
        UnhookWindowsHookEx(_hookID);
    }

    private IntPtr SetHook(LowLevelKeyboardProc proc)
    {
        using (Process curProcess = Process.GetCurrentProcess())
        using (ProcessModule curModule = curProcess.MainModule)
        {
            return SetWindowsHookEx(WH_KEYBOARD_LL, proc, GetModuleHandle(curModule.ModuleName), 0);
        }
    }

    private IntPtr HookCallback(int nCode, IntPtr wParam, IntPtr lParam)
    {
        if (nCode >= 0 && wParam == (IntPtr)WM_KEYDOWN || wParam == (IntPtr)WM_SYSKEYDOWN)
        {
            int vkCode = Marshal.ReadInt32(lParam);

            if (OnKeyPressed != null) { OnKeyPressed(this, new KeyPressedArgs(vkCode)); }
        }

        return CallNextHookEx(_hookID, nCode, wParam, lParam);
    }

    readonly int VK_F12 = 123;
    readonly int VK_LSB = 219;  // [ key
    readonly int VK_RSB = 221;  // ] key
    readonly int VK_BS = 220;   // \ key

    void _listener_OnKeyPressed(object sender, KeyPressedArgs e)
    {
        // if user pressed F12 then recenter the view of the VR headset
        if (e.KeyPressed == VK_F12)
//            RecenterHMD();

        // If user presses ], let's bump the Quality to the next level and rebuild
        // the environment.  [ will lower quality setting.  Mostly AA settings.
        if (e.KeyPressed == VK_LSB)
            QualitySettings.DecreaseLevel(true);
        if (e.KeyPressed == VK_RSB)
            QualitySettings.IncreaseLevel(true);
        if (e.KeyPressed == VK_BS)
            QualitySettings.anisotropicFiltering = (QualitySettings.anisotropicFiltering == AnisotropicFiltering.Disable) ? AnisotropicFiltering.ForceEnable : AnisotropicFiltering.Disable;

        //qualityText.text = "Quality: " + QualitySettings.names[QualitySettings.GetQualityLevel()];
        //qualityText.text += "\nMSAA: " + QualitySettings.antiAliasing;
        //qualityText.text += "\nAnisotropic: " + QualitySettings.anisotropicFiltering;
    }

}

public class KeyPressedArgs : EventArgs
{
    public int KeyPressed { get; private set; }

    public KeyPressedArgs(int key)
    {
        KeyPressed = key;
    }
}
