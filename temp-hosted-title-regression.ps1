Add-Type -ReferencedAssemblies @('System.Drawing','System.Windows.Forms') @'
using System;
using System.Runtime.InteropServices;
using System.Threading;
public static class HostedTitleRegressionNative {
    [DllImport("user32.dll", CharSet=CharSet.Ansi)] public static extern IntPtr FindWindow(string cls,string title);
    [DllImport("user32.dll")] public static extern bool IsWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x,int y);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h,ref POINT p);
    [DllImport("user32.dll")] public static extern uint SendInput(uint n,INPUT[] inputs,int size);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h,uint message,IntPtr w,IntPtr l);
    [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X,Y; }
    [StructLayout(LayoutKind.Sequential)] public struct INPUT { public uint type; public MOUSEINPUT mi; }
    [StructLayout(LayoutKind.Sequential)] public struct MOUSEINPUT { public int dx,dy; public uint mouseData,flags,time; public IntPtr extra; }
    public const uint LEFTDOWN=0x0002, LEFTUP=0x0004;
    static void Move(IntPtr h,int x,int y) { var p=new POINT{X=x,Y=y}; ClientToScreen(h,ref p); SetForegroundWindow(h); SetCursorPos(p.X,p.Y); }
    static uint Button(uint flags) { var input=new INPUT{type=0,mi=new MOUSEINPUT{flags=flags}}; return SendInput(1,new[]{input},Marshal.SizeOf(typeof(INPUT))); }
    public static uint Press(IntPtr h,int x,int y) { Move(h,x,y); return Button(LEFTDOWN); }
    public static uint Release(IntPtr h,int x,int y) { Move(h,x,y); return Button(LEFTUP); }
    public static uint Click(IntPtr h,int x,int y) { var a=Press(h,x,y); Thread.Sleep(150); return a+Release(h,x,y); }
    public static bool DiagnosticCancel(IntPtr h) { return PostMessage(h,0x001F,IntPtr.Zero,IntPtr.Zero); }
    public static bool DiagnosticCaptureLost(IntPtr h) { return PostMessage(h,0x0215,IntPtr.Zero,IntPtr.Zero); }
}
'@

$root='D:\dev\guideXOSServer'
$psi=[Diagnostics.ProcessStartInfo]::new()
$psi.FileName=$env:ComSpec
$psi.Arguments='/d /c ".\guideXOSServer.experimental.exe > hosted-title-regression-raw.log 2>&1"'
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
for($i=0;$i -lt 50 -and $hwnd -eq [IntPtr]::Zero;$i++) { Start-Sleep -Milliseconds 200; $hwnd=[HostedTitleRegressionNative]::FindWindow('GXOS_COMPOSITOR','guideXOSCpp Compositor') }
$results=New-Object System.Collections.Generic.List[string]
$results.Add("compositorHwnd=$hwnd")
$results.Add("compositorFound=$([HostedTitleRegressionNative]::IsWindow($hwnd))")

# Window 1000: press Close, drag away, release outside. It must remain open.
$proc.StandardInput.WriteLine('gui.win DragAwayCancel 480 640'); $proc.StandardInput.Flush(); Start-Sleep -Seconds 2
[void][HostedTitleRegressionNative]::Press($hwnd,768,312); Start-Sleep -Milliseconds 300
[void][HostedTitleRegressionNative]::Release($hwnd,20,600); Start-Sleep -Seconds 2
$results.Add('dragAwayReleaseOutside=completed')

# Window 1001: press outside Close, release over Close. It must remain open.
$proc.StandardInput.WriteLine('gui.win OutsidePressRelease 480 640'); $proc.StandardInput.Flush(); Start-Sleep -Seconds 2
[void][HostedTitleRegressionNative]::Press($hwnd,200,112); Start-Sleep -Milliseconds 300
[void][HostedTitleRegressionNative]::Release($hwnd,528,72); Start-Sleep -Seconds 2
$results.Add('pressOutsideReleaseOverClose=completed')

# Window 1002: force capture loss while Close is pressed, then release. It must remain open.
$proc.StandardInput.WriteLine('gui.win CaptureLossReset 480 640'); $proc.StandardInput.Flush(); Start-Sleep -Seconds 2
[void][HostedTitleRegressionNative]::Press($hwnd,568,112); Start-Sleep -Milliseconds 300
$results.Add("captureChangedPost=$([HostedTitleRegressionNative]::DiagnosticCaptureLost($hwnd))")
Start-Sleep -Milliseconds 300; [void][HostedTitleRegressionNative]::Release($hwnd,568,112); Start-Sleep -Seconds 2
$results.Add('captureLossRelease=completed')

# Window 1003: WM_CANCELMODE must clear the pressed state and cancel the action.
$proc.StandardInput.WriteLine('gui.win CancelModeReset 480 640'); $proc.StandardInput.Flush(); Start-Sleep -Seconds 2
[void][HostedTitleRegressionNative]::Press($hwnd,608,152); Start-Sleep -Milliseconds 300
$results.Add("cancelModePost=$([HostedTitleRegressionNative]::DiagnosticCancel($hwnd))")
Start-Sleep -Milliseconds 300; [void][HostedTitleRegressionNative]::Release($hwnd,608,152); Start-Sleep -Seconds 2
$results.Add('cancelModeRelease=completed')

# Confirm a subsequent normal close still activates.
$proc.StandardInput.WriteLine('gui.win NormalCloseAfterCancel 480 640'); $proc.StandardInput.Flush(); Start-Sleep -Seconds 2
$results.Add("normalCloseSendInput=$([HostedTitleRegressionNative]::Click($hwnd,648,192))")
Start-Sleep -Seconds 2
$proc.StandardInput.WriteLine('exit'); $proc.StandardInput.Flush(); $proc.StandardInput.Close()
$proc.WaitForExit(20000) | Out-Null
$proc.StandardOutput.ReadToEnd() | Out-Null; $proc.StandardError.ReadToEnd() | Out-Null
$raw=Join-Path $root 'hosted-title-regression-raw.log'
$rawText=''
if(Test-Path -LiteralPath $raw){$rawText=[IO.File]::ReadAllText($raw)}
$interesting=@($rawText -split "`r?`n" | Where-Object { $_ -match 'Hosted title input diag|Compositor created|server exiting' })
[IO.File]::WriteAllLines((Join-Path $root 'hosted-title-regression.log'),[string[]]$interesting)
$results.Add("processExitCode=$($proc.ExitCode)")
$results | ForEach-Object { $_ }
