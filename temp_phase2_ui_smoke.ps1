Add-Type -AssemblyName System.Windows.Forms
$signature = @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class NativeMethods {
    public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc lpEnumFunc, IntPtr lParam);
    [DllImport("user32.dll", SetLastError=true)] public static extern int GetWindowText(IntPtr hWnd, StringBuilder lpString, int nMaxCount);
    [DllImport("user32.dll", SetLastError=true)] public static extern int GetWindowTextLength(IntPtr hWnd);
    [DllImport("user32.dll", SetLastError=true)] public static extern bool IsWindowVisible(IntPtr hWnd);
    [DllImport("user32.dll", SetLastError=true)] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll", SetLastError=true)] public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);
    [DllImport("user32.dll", SetLastError=true)] public static extern bool PostMessage(IntPtr hWnd, uint Msg, IntPtr wParam, IntPtr lParam);
}
"@
Add-Type -TypeDefinition $signature
function Find-WindowByTitle([string]$title) {
    $script:found = [IntPtr]::Zero
    $callback = [NativeMethods+EnumWindowsProc]{
        param([IntPtr]$hWnd, [IntPtr]$lParam)
        if (-not [NativeMethods]::IsWindowVisible($hWnd)) { return $true }
        $len = [NativeMethods]::GetWindowTextLength($hWnd)
        if ($len -le 0) { return $true }
        $sb = New-Object System.Text.StringBuilder ($len + 1)
        [void][NativeMethods]::GetWindowText($hWnd, $sb, $sb.Capacity)
        if ($sb.ToString() -eq $title) {
            $script:found = $hWnd
            return $false
        }
        return $true
    }
    [void][NativeMethods]::EnumWindows($callback, [IntPtr]::Zero)
    return $script:found
}
function Click-ClientPoint([IntPtr]$hWnd, [int]$x, [int]$y, [int]$button) {
    $lParam = [IntPtr](($y -shl 16) -bor ($x -band 0xFFFF))
    if ($button -eq 1) {
        [void][NativeMethods]::PostMessage($hWnd, 0x0201, [IntPtr]1, $lParam)
        Start-Sleep -Milliseconds 90
        [void][NativeMethods]::PostMessage($hWnd, 0x0202, [IntPtr]0, $lParam)
    } elseif ($button -eq 2) {
        [void][NativeMethods]::PostMessage($hWnd, 0x0204, [IntPtr]2, $lParam)
        Start-Sleep -Milliseconds 90
        [void][NativeMethods]::PostMessage($hWnd, 0x0205, [IntPtr]0, $lParam)
    }
    Start-Sleep -Milliseconds 260
}
$desktopJsonPath = 'D:\dev\guideXOSServer\desktop.json'
$desktopBackupPath = 'D:\dev\guideXOSServer\desktop.json.phase2.bak'
Copy-Item $desktopJsonPath $desktopBackupPath -Force
$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = 'D:\dev\guideXOSServer\guideXOSServer.exe'
$psi.WorkingDirectory = 'D:\dev\guideXOSServer'
$psi.UseShellExecute = $false
$psi.RedirectStandardInput = $true
$psi.RedirectStandardOutput = $true
$psi.RedirectStandardError = $true
$proc = New-Object System.Diagnostics.Process
$proc.StartInfo = $psi
[void]$proc.Start()
Start-Sleep -Seconds 1
$proc.StandardInput.WriteLine('gui.start')
Start-Sleep -Seconds 4
$hWnd = Find-WindowByTitle 'guideXOSCpp Compositor'
if ($hWnd -eq [IntPtr]::Zero) {
    $proc.StandardInput.WriteLine('exit')
    $proc.WaitForExit()
    Copy-Item $desktopBackupPath $desktopJsonPath -Force
    throw 'Compositor window not found after gui.start'
}
[void][NativeMethods]::ShowWindow($hWnd, 5)
[void][NativeMethods]::SetForegroundWindow($hWnd)
Start-Sleep -Milliseconds 500
Click-ClientPoint $hWnd 20 740 1
Click-ClientPoint $hWnd 60 425 2
Click-ClientPoint $hWnd 80 469 1
Start-Sleep -Seconds 1
$cfg = Get-Content $desktopJsonPath -Raw | ConvertFrom-Json
$shortcut = $cfg.iconPositions | Where-Object { $_.name -eq 'shortcut:app:gxos.builtin.appmodeldemo' } | Select-Object -First 1
if (-not $shortcut) {
    $proc.StandardInput.WriteLine('log')
    $proc.StandardInput.WriteLine('exit')
    $stdout = $proc.StandardOutput.ReadToEnd()
    $stderr = $proc.StandardError.ReadToEnd()
    $proc.WaitForExit()
    Copy-Item $desktopBackupPath $desktopJsonPath -Force
    throw ('Pinned desktop shortcut position not found. Output:' + [Environment]::NewLine + $stdout + [Environment]::NewLine + $stderr)
}
$clickX = [int]$shortcut.x + 10
$clickY = [int]$shortcut.y + 10
Click-ClientPoint $hWnd $clickX $clickY 1
Click-ClientPoint $hWnd $clickX $clickY 1
Start-Sleep -Seconds 1
$proc.StandardInput.WriteLine('desktop.appmodel.summary')
$proc.StandardInput.WriteLine('desktop.appmodel.coverage')
$proc.StandardInput.WriteLine('log')
$proc.StandardInput.WriteLine('exit')
$stdout = $proc.StandardOutput.ReadToEnd()
$stderr = $proc.StandardError.ReadToEnd()
$proc.WaitForExit()
Copy-Item $desktopBackupPath $desktopJsonPath -Force
Remove-Item $desktopBackupPath -Force
Write-Output $stdout
if ($stderr) { Write-Output 'STDERR:'; Write-Output $stderr }
