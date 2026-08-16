param(
    [Parameter(Mandatory = $true)]
    [int[]] $ProcessId,
    [Parameter(Mandatory = $true)]
    [string] $OutputDirectory
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$source = @'
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;

public static class Dkc1WindowCapture
{
    [StructLayout(LayoutKind.Sequential)]
    private struct Rect { public int Left, Top, Right, Bottom; }

    [DllImport("user32.dll")]
    private static extern bool GetWindowRect(IntPtr window, out Rect rect);

    [DllImport("user32.dll")]
    private static extern bool PrintWindow(IntPtr window, IntPtr dc, uint flags);

    public static void Save(IntPtr window, string path)
    {
        Rect rect;
        if (!GetWindowRect(window, out rect))
            throw new InvalidOperationException("GetWindowRect failed");
        using (var bitmap = new Bitmap(rect.Right - rect.Left,
                                       rect.Bottom - rect.Top,
                                       PixelFormat.Format32bppArgb))
        using (var graphics = Graphics.FromImage(bitmap))
        {
            var dc = graphics.GetHdc();
            try
            {
                if (!PrintWindow(window, dc, 2))
                    throw new InvalidOperationException("PrintWindow failed");
            }
            finally { graphics.ReleaseHdc(dc); }
            bitmap.Save(path, ImageFormat.Png);
        }
    }
}
'@

Add-Type -TypeDefinition $source -ReferencedAssemblies System.Drawing
$resolvedOutput = [IO.Path]::GetFullPath($OutputDirectory)
[IO.Directory]::CreateDirectory($resolvedOutput) | Out-Null

foreach ($id in $ProcessId) {
    $process = Get-Process -Id $id -ErrorAction Stop
    if ($process.MainWindowHandle -eq [IntPtr]::Zero) {
        throw "Process $id has no main window"
    }
    $path = Join-Path $resolvedOutput "window-$id.png"
    [Dkc1WindowCapture]::Save($process.MainWindowHandle, $path)
    Get-Item -LiteralPath $path
}
