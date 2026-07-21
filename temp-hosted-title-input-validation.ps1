using namespace System;
using namespace System.Diagnostics;
using namespace System.IO;
using namespace System.Runtime.InteropServices;
using namespace System.Threading;
using namespace System.Drawing;
using namespace System.Drawing.Imaging;
using namespace System.Windows.Forms;

Add-Type -ReferencedAssemblies @('System.Drawing','System.Windows.Forms') @'
using System;
using System.Runtime.InteropServices;
using System.Threading;
using System.Drawing;
using System.Drawing.Imaging;
using System.Windows.Forms;
public static class HostedTitleInputNative {
    [DllImport("user32.dll", CharSet=CharSet.Ansi)] public static extern IntPtr FindWindow(string cls,string title);
    [DllImport("user32.dll")] public static extern bool IsWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x,int y);
    [DllImport("user32.dll")] public static extern uint SendInput(uint n,INPUT[] inputs,int size);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h,ref POINT p);
    [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X,Y; }
    [StructLayout(LayoutKind.Sequential)] public struct INPUT { public uint type; public MOUSEINPUT mi; }
    [StructLayout(LayoutKind.Sequential)] public struct MOUSEINPUT { public int dx,dy; public uint mouseData,flags,time; public IntPtr extra; }
    public const uint LEFTDOWN=0x0002, LEFTUP=0x0004;
    public static uint Click(IntPtr h,int clientX,int clientY) {
        var p=new POINT{X=clientX,Y=clientY};
        ClientToScreen(h,ref p); SetForegroundWindow(h); Thread.Sleep(200); SetCursorPos(p.X,p.Y);
        var down=new INPUT{type=0,mi=new MOUSEINPUT{flags=LEFTDOWN}};
        var up=new INPUT{type=0,mi=new MOUSEINPUT{flags=LEFTUP}};
        uint sent=SendInput(1,new[]{down},Marshal.SizeOf(typeof(INPUT)));
        Thread.Sleep(200); sent+=SendInput(1,new[]{up},Marshal.SizeOf(typeof(INPUT)));
        return sent;
    }
    public static void Capture(string path) {
        var b=Screen.PrimaryScreen.Bounds;
        using(var bmp=new Bitmap(b.Width,b.Height))
        using(var g=Graphics.FromImage(bmp)) {
            g.CopyFromScreen(b.Location,Point.Empty,b.Size);
            bmp.Save(path,ImageFormat.Png);
        }
    }
}
'@

$root='D:\dev\guideXOSServer'
$exe=Join-Path $root 'guideXOSServer.experimental.exe'
$log=Join-Path $root 'hosted-title-input-validation.log'
$psi=[Diagnostics.ProcessStartInfo]::new()
$psi.FileName=$env:ComSpec
$psi.Arguments='/d /c ".\guideXOSServer.experimental.exe > hosted-title-input-validation-raw.log 2>&1"'
$psi.WorkingDirectory=$root
$psi.UseShellExecute=$false
$psi.CreateNoWindow=$false
$psi.RedirectStandardInput=$true
$psi.RedirectStandardOutput=$true
$psi.RedirectStandardError=$true
$psi.EnvironmentVariables['GXOS_HOSTED_TITLE_INPUT_DIAGNOSTICS']='1'
$proc=[Diagnostics.Process]::new()
$proc.StartInfo=$psi
[void]$proc.Start()
$proc.StandardInput.WriteLine('gui.start')
$proc.StandardInput.Flush()
$hwnd=[IntPtr]::Zero
for($i=0;$i -lt 50 -and $hwnd -eq [IntPtr]::Zero;$i++) {
    Start-Sleep -Milliseconds 200
    $hwnd=[HostedTitleInputNative]::FindWindow('GXOS_COMPOSITOR','guideXOSCpp Compositor')
}
$results=New-Object System.Collections.Generic.List[string]
$results.Add("compositorHwnd=$hwnd")
$results.Add("compositorFound=$([HostedTitleInputNative]::IsWindow($hwnd))")
for($cycle=0;$cycle -lt 3;$cycle++) {
    $proc.StandardInput.WriteLine('desktop.launch Nexgen PacMan')
    $proc.StandardInput.Flush()
    Start-Sleep -Seconds 4
    [HostedTitleInputNative]::Capture((Join-Path $root ("hosted-title-cycle-{0}-before.png" -f ($cycle+1))))
    $id=1000+$cycle
    $wx=60+(($id % 7)*40)
    $wy=$wx
    $cx=$wx+468
    $cy=$wy+12
    $sent=[HostedTitleInputNative]::Click($hwnd,$cx,$cy)
    Start-Sleep -Seconds 3
    [HostedTitleInputNative]::Capture((Join-Path $root ("hosted-title-cycle-{0}-after.png" -f ($cycle+1))))
    $results.Add("cycle=$($cycle+1) windowId=$id clientCloseX=$cx clientCloseY=$cy sendInputCount=$sent")
}
$proc.StandardInput.WriteLine('exit')
$proc.StandardInput.Flush()
$proc.StandardInput.Close()
if(-not $proc.WaitForExit(20000)) {
    $proc.Kill()
    $proc.WaitForExit()
}
$cmdStdout=$proc.StandardOutput.ReadToEnd()
$cmdStderr=$proc.StandardError.ReadToEnd()
$rawLog=Join-Path $root 'hosted-title-input-validation-raw.log'
$stdout=if(Test-Path -LiteralPath $rawLog){[IO.File]::ReadAllText($rawLog)}else{''}
$stderr=$cmdStderr
$interesting=@($stdout -split "`r?`n" | Where-Object {
    $_ -match 'Hosted title input diag|NativeAppHost.*(close|exit)|NativeAppRuntime.*(Cleanup|remaining|cleaned)|NativeAppProcessTable|Compositor created|Compositor service|server exiting|Desktop launch'
})
[IO.File]::WriteAllLines($log,[string[]]$interesting)
$results.Add("processExitCode=$($proc.ExitCode)")
$results.Add("stdoutLines=$(@($stdout -split "`r?`n").Count) filteredLines=$($interesting.Count)")
$results.Add("stderr=$stderr")
$results | ForEach-Object { $_ }
