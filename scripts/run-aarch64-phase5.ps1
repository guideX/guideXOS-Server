[CmdletBinding()]
param(
    [string]$ArtifactDirectory = '',
    [string]$QemuPath = 'C:\Program Files\qemu\qemu-system-aarch64.exe',
    [string]$FirmwareCode = 'C:\Program Files\qemu\share\edk2-aarch64-code.fd',
    [string]$FirmwareVars = '',
    [string]$LogPath = '',
    [int]$TimeoutSeconds = 180
)
$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ([string]::IsNullOrWhiteSpace($ArtifactDirectory)) { $ArtifactDirectory = Join-Path $repoRoot 'out\aarch64-phase5' } else { $ArtifactDirectory = [IO.Path]::GetFullPath($ArtifactDirectory) }
$esp = Join-Path $ArtifactDirectory 'esp'
foreach ($required in @('EFI\BOOT\BOOTAA64.EFI','kernel.elf','ramdisk.img')) { if (!(Test-Path -LiteralPath (Join-Path $esp $required) -PathType Leaf)) { throw "Phase 5 ESP is missing $required" } }
if (!(Test-Path -LiteralPath $QemuPath -PathType Leaf)) { throw "QEMU not found: $QemuPath" }
if (!(Test-Path -LiteralPath $FirmwareCode -PathType Leaf)) { throw "AArch64 UEFI code firmware not found: $FirmwareCode" }
if ([string]::IsNullOrWhiteSpace($FirmwareVars)) { $FirmwareVars = Join-Path $ArtifactDirectory 'edk2-aarch64-vars.fd' }
if (!(Test-Path -LiteralPath $FirmwareVars -PathType Leaf)) { Copy-Item 'C:\Program Files\qemu\share\edk2-arm-vars.fd' $FirmwareVars -Force }
if ([string]::IsNullOrWhiteSpace($LogPath)) { $LogPath = Join-Path $ArtifactDirectory 'qemu-aarch64.log' } else { $LogPath = [IO.Path]::GetFullPath($LogPath) }
$null = New-Item -ItemType Directory -Path (Split-Path -Parent $LogPath) -Force
$arguments = @('-machine','virt,gic-version=2,acpi=off','-cpu','cortex-a53','-m','512M',"-drive", "if=pflash,format=raw,unit=0,readonly=on,file=$FirmwareCode", "-drive", "if=pflash,format=raw,unit=1,file=$FirmwareVars", '-drive', "file=fat:rw:$esp,format=raw", '-nographic','-monitor','none','-serial','stdio','-no-reboot')
$info = [Diagnostics.ProcessStartInfo]::new(); $info.FileName = $QemuPath; $info.Arguments = (($arguments | ForEach-Object { '"' + $_.Replace('"','\"') + '"' }) -join ' '); $info.UseShellExecute = $false; $info.CreateNoWindow = $true; $info.RedirectStandardOutput = $true; $info.RedirectStandardError = $true
$process = [Diagnostics.Process]::new(); $process.StartInfo = $info; $null = $process.Start(); $stdout = $process.StandardOutput.ReadToEndAsync(); $stderr = $process.StandardError.ReadToEndAsync(); $completed = $process.WaitForExit($TimeoutSeconds * 1000); $timedOut = !$completed
if ($timedOut) { try { $process.Kill() } catch {}; $process.WaitForExit() }
$output = $stdout.GetAwaiter().GetResult() + $stderr.GetAwaiter().GetResult(); $output | Set-Content -LiteralPath $LogPath -Encoding utf8; Write-Output $output; Write-Host "QEMU log: $LogPath" -ForegroundColor DarkGray
if ($output -match 'AARCH64_PHASE5_PASS') { exit 0 }; exit 1
