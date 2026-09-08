[CmdletBinding()]
param(
    [string]$ArtifactDirectory = '',
    [string]$QemuPath = 'C:\Program Files\qemu\qemu-system-aarch64.exe',
    [string]$FirmwareCode = 'C:\Program Files\qemu\share\edk2-aarch64-code.fd',
    [string]$FirmwareVars = '',
    [string]$LogPath = '',
    [string]$DisplayBackend = 'none',
    [int]$MonitorPort = 0,
    [int]$TimeoutSeconds = 240
)

$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ([string]::IsNullOrWhiteSpace($ArtifactDirectory)) { $ArtifactDirectory = Join-Path $repoRoot 'out\aarch64-phase8' }
else { $ArtifactDirectory = [IO.Path]::GetFullPath($ArtifactDirectory) }
$esp = Join-Path $ArtifactDirectory 'esp'
foreach ($required in @('EFI\BOOT\BOOTAA64.EFI','kernel.elf','ramdisk.img')) {
    if (!(Test-Path -LiteralPath (Join-Path $esp $required) -PathType Leaf)) { throw "Phase 8 ESP is missing $required" }
}
if (!(Test-Path -LiteralPath $QemuPath -PathType Leaf)) { throw "QEMU not found: $QemuPath" }
if (!(Test-Path -LiteralPath $FirmwareCode -PathType Leaf)) { throw "AArch64 UEFI code firmware not found: $FirmwareCode" }
if ([string]::IsNullOrWhiteSpace($FirmwareVars)) { $FirmwareVars = Join-Path $ArtifactDirectory 'edk2-aarch64-vars.fd' }
if (!(Test-Path -LiteralPath $FirmwareVars -PathType Leaf)) {
    $varsTemplate = 'C:\Program Files\qemu\share\edk2-arm-vars.fd'
    if (!(Test-Path -LiteralPath $varsTemplate -PathType Leaf)) { throw "AArch64 UEFI variable template not found: $varsTemplate" }
    Copy-Item -LiteralPath $varsTemplate -Destination $FirmwareVars -Force
}
if ([string]::IsNullOrWhiteSpace($LogPath)) { $LogPath = Join-Path $ArtifactDirectory 'qemu-aarch64-phase8.log' }
else { $LogPath = [IO.Path]::GetFullPath($LogPath) }
$null = New-Item -ItemType Directory -Path (Split-Path -Parent $LogPath) -Force

$monitorArguments = if ($MonitorPort -gt 0) {
    @('-qmp', "tcp:127.0.0.1:$MonitorPort,server=on,wait=off")
} else { @('-monitor', 'none') }
$arguments = @(
    '-machine','virt,gic-version=2,acpi=off','-cpu','cortex-a53','-m','512M',
    '-drive',"if=pflash,format=raw,unit=0,readonly=on,file=$FirmwareCode",
    '-drive',"if=pflash,format=raw,unit=1,file=$FirmwareVars",
    '-drive',"file=fat:rw:$esp,format=raw",
    '-display',$DisplayBackend,'-device','ramfb','-global','virtio-mmio.force-legacy=false',
    '-device','virtio-keyboard-device','-device','virtio-tablet-device')
$arguments += $monitorArguments
$arguments += @('-serial',"file:$LogPath",'-no-reboot')
$info = [Diagnostics.ProcessStartInfo]::new()
$info.FileName = $QemuPath
$info.Arguments = (($arguments | ForEach-Object { '"' + $_.Replace('"','\"') + '"' }) -join ' ')
$info.UseShellExecute = $false
$info.CreateNoWindow = $true
$process = [Diagnostics.Process]::new()
$process.StartInfo = $info
$null = $process.Start()

$deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
$output = ''
while ([DateTime]::UtcNow -lt $deadline) {
    if (Test-Path -LiteralPath $LogPath -PathType Leaf) { $output = Get-Content -Raw -LiteralPath $LogPath }
    if ($output -match 'AARCH64_PHASE8_PASS|AARCH64_PHASE8_ERROR|\[guideXOS\].*(FATAL|ERROR)') { break }
    if ($process.HasExited) { break }
    Start-Sleep -Milliseconds 250
}
if (Test-Path -LiteralPath $LogPath -PathType Leaf) { $output = Get-Content -Raw -LiteralPath $LogPath }
if (!$process.HasExited) { try { $process.Kill() } catch {} }
$process.WaitForExit()
if (Test-Path -LiteralPath $LogPath -PathType Leaf) { $output = Get-Content -Raw -LiteralPath $LogPath }
Write-Output $output
Write-Host "QEMU log: $LogPath" -ForegroundColor DarkGray
if ($output -match 'AARCH64_PHASE8_PASS') { exit 0 }
exit 1
