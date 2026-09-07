[CmdletBinding()]
param(
    [string]$ArtifactDirectory = '',
    [string]$QemuPath = 'C:\Program Files\qemu\qemu-system-aarch64.exe',
    [string]$FirmwareCode = 'C:\Program Files\qemu\share\edk2-aarch64-code.fd',
    [string]$FirmwareVars = '',
    [string]$LogPath = '',
    [int]$TimeoutSeconds = 120
)

$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ([string]::IsNullOrWhiteSpace($ArtifactDirectory)) { $ArtifactDirectory = Join-Path $repoRoot 'out\aarch64-phase4' }
else { $ArtifactDirectory = [IO.Path]::GetFullPath($ArtifactDirectory) }
$esp = Join-Path $ArtifactDirectory 'esp'
foreach ($required in @('EFI\BOOT\BOOTAA64.EFI', 'kernel.elf', 'ramdisk.img')) {
    if (!(Test-Path -LiteralPath (Join-Path $esp $required) -PathType Leaf)) { throw "Phase 4 ESP is missing $required" }
}
if (!(Test-Path -LiteralPath $QemuPath -PathType Leaf)) { throw "QEMU not found: $QemuPath" }
if (!(Test-Path -LiteralPath $FirmwareCode -PathType Leaf)) { throw "AArch64 UEFI code firmware not found: $FirmwareCode" }
if ([string]::IsNullOrWhiteSpace($FirmwareVars)) { $FirmwareVars = Join-Path $ArtifactDirectory 'edk2-aarch64-vars.fd' }
if (!(Test-Path -LiteralPath $FirmwareVars -PathType Leaf)) {
    $template = 'C:\Program Files\qemu\share\edk2-arm-vars.fd'
    if (!(Test-Path -LiteralPath $template -PathType Leaf)) { throw "AArch64 UEFI vars firmware not found: $template" }
    Copy-Item -LiteralPath $template -Destination $FirmwareVars -Force
}
if ([string]::IsNullOrWhiteSpace($LogPath)) { $LogPath = Join-Path $ArtifactDirectory 'qemu-aarch64.log' }
else { $LogPath = [IO.Path]::GetFullPath($LogPath) }
$null = New-Item -ItemType Directory -Path (Split-Path -Parent $LogPath) -Force

$arguments = @('-machine', 'virt,gic-version=2,acpi=off', '-cpu', 'cortex-a53', '-m', '512M',
    '-drive', "if=pflash,format=raw,unit=0,readonly=on,file=$FirmwareCode",
    '-drive', "if=pflash,format=raw,unit=1,file=$FirmwareVars",
    '-drive', "file=fat:rw:$esp,format=raw", '-nographic', '-monitor', 'none', '-serial', 'stdio', '-no-reboot')
$startInfo = [Diagnostics.ProcessStartInfo]::new()
$startInfo.FileName = $QemuPath
$startInfo.Arguments = (($arguments | ForEach-Object { '"' + $_.Replace('"', '\"') + '"' }) -join ' ')
$startInfo.UseShellExecute = $false
$startInfo.CreateNoWindow = $true
$startInfo.RedirectStandardOutput = $true
$startInfo.RedirectStandardError = $true
$process = [Diagnostics.Process]::new()
$process.StartInfo = $startInfo
$null = $process.Start()
$stdoutTask = $process.StandardOutput.ReadToEndAsync()
$stderrTask = $process.StandardError.ReadToEndAsync()
$completed = $process.WaitForExit($TimeoutSeconds * 1000)
$timedOut = !$completed
if ($timedOut) { try { $process.Kill() } catch { }; $process.WaitForExit() }
$output = $stdoutTask.GetAwaiter().GetResult() + $stderrTask.GetAwaiter().GetResult()
$output | Set-Content -LiteralPath $LogPath -Encoding utf8
Write-Output $output
Write-Host "QEMU log: $LogPath" -ForegroundColor DarkGray
if ($timedOut) { Write-Host "QEMU stopped after $TimeoutSeconds seconds." -ForegroundColor DarkGray }
if ($output -match 'AARCH64_PHASE4_PASS') { exit 0 }
exit 1
