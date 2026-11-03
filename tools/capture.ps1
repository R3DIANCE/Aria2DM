param(
    [string]$Out = "$env:TEMP\aria2dm-capture.png",
    [int]$ProcId = 0
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
using System.Drawing;
using System.Drawing.Imaging;

public static class WindowShot {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }

    [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr ctx);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
    [DllImport("user32.dll")] public static extern IntPtr GetDC(IntPtr h);
    [DllImport("user32.dll")] public static extern int ReleaseDC(IntPtr h, IntPtr dc);
    [DllImport("gdi32.dll")] public static extern bool BitBlt(IntPtr dst, int x, int y, int w, int h, IntPtr src, int sx, int sy, int rop);
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, IntPtr pid);
    [DllImport("user32.dll")] public static extern bool AttachThreadInput(uint a, uint b, bool attach);
    [DllImport("kernel32.dll")] public static extern uint GetCurrentThreadId();
    [DllImport("user32.dll")] public static extern bool BringWindowToTop(IntPtr h);
    [DllImport("user32.dll")] public static extern IntPtr SetFocus(IntPtr h);

    static void ForceForeground(IntPtr hwnd) {
        IntPtr fg = GetForegroundWindow();
        uint target = GetWindowThreadProcessId(hwnd, IntPtr.Zero);
        uint current = GetCurrentThreadId();
        bool attached = false;
        if (fg != IntPtr.Zero && target != current) {
            attached = AttachThreadInput(current, target, true);
        }
        SetWindowPos(hwnd, new IntPtr(-1), 0, 0, 0, 0, 0x0001 | 0x0002 | 0x0010);
        BringWindowToTop(hwnd);
        SetForegroundWindow(hwnd);
        SetFocus(hwnd);
        if (attached) AttachThreadInput(current, target, false);
        System.Threading.Thread.Sleep(300);
        SetWindowPos(hwnd, new IntPtr(-2), 0, 0, 0, 0, 0x0001 | 0x0002 | 0x0010);
    }

    public static string Capture(IntPtr hwnd, string path) {
        // Per-monitor v2 for this thread so every coordinate is a physical pixel.
        SetThreadDpiAwarenessContext(new IntPtr(-4));
        ShowWindow(hwnd, 9);
        ForceForeground(hwnd);
        System.Threading.Thread.Sleep(500);

        RECT r;
        if (!GetWindowRect(hwnd, out r)) return "no-rect";
        int w = r.Right - r.Left;
        int h = r.Bottom - r.Top;
        if (w <= 0 || h <= 0) return "empty";

        using (Bitmap bmp = new Bitmap(w, h, PixelFormat.Format32bppArgb)) {
            using (Graphics g = Graphics.FromImage(bmp)) {
                IntPtr screen = GetDC(IntPtr.Zero);
                IntPtr dest = g.GetHdc();
                BitBlt(dest, 0, 0, w, h, screen, r.Left, r.Top, 0x00CC0020);
                g.ReleaseHdc(dest);
                ReleaseDC(IntPtr.Zero, screen);
            }
            bmp.Save(path, ImageFormat.Png);
        }
        return string.Format("{0},{1} {2}x{3}", r.Left, r.Top, w, h);
    }
}
"@

$proc = if ($ProcId -gt 0) { Get-Process -Id $ProcId } else { Get-Process aria2dm }
$hwnd = $proc.MainWindowHandle
if ($hwnd -eq [IntPtr]::Zero) { throw "no window" }
$info = [WindowShot]::Capture($hwnd, $Out)
Write-Output "$info -> $Out"
