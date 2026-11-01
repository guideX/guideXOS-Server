[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$QacGrantFile,
    [string]$ArtifactDirectory = '',
    [string]$QemuPath = 'C:\Program Files\qemu\qemu-system-aarch64.exe',
    [string]$FirmwareCode = 'C:\Program Files\qemu\share\edk2-aarch64-code.fd',
    [string]$FirmwareVarsTemplate = 'C:\Program Files\qemu\share\edk2-arm-vars.fd',
    [int]$TimeoutSeconds = 120
)

$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ([string]::IsNullOrWhiteSpace($ArtifactDirectory)) {
    $ArtifactDirectory = Join-Path $repoRoot 'out\aarch64-gicv3'
} else {
    $ArtifactDirectory = [IO.Path]::GetFullPath($ArtifactDirectory)
}
$resolvedRoot = [IO.Path]::GetFullPath($repoRoot).TrimEnd('\')
$resolvedOutput = [IO.Path]::GetFullPath($ArtifactDirectory).TrimEnd('\')
if (!$resolvedOutput.StartsWith($resolvedRoot + '\out\', [StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to use GICv3 guest artifacts outside repository out/: $resolvedOutput"
}

function Assert-QacGrant {
    param([string]$Path)
    if (!(Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "QAC authorization file not found. No QEMU process was started: $Path"
    }
    try { $grant = Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json }
    catch { throw "QAC authorization file is not valid JSON. No QEMU process was started." }
    $required = @(
        $grant.issuer -ceq 'guideXOS-QAC',
        $grant.status -ceq 'granted',
        $grant.resource -ceq 'qemu-system-aarch64',
        $grant.purpose -ceq 'AARCH64-GIC3-1',
        $grant.exclusive -eq $true,
        ![string]::IsNullOrWhiteSpace([string]$grant.reservation_id)
    )
    if ($required -contains $false) {
        throw 'QAC grant must identify guideXOS-QAC, an active exclusive qemu-system-aarch64 reservation, purpose AARCH64-GIC3-1, and a reservation ID. No QEMU process was started.'
    }
    $expires = [DateTimeOffset]::MinValue
    if (![DateTimeOffset]::TryParse([string]$grant.expires_utc, [ref]$expires) -or
        $expires -le [DateTimeOffset]::UtcNow) {
        throw 'QAC grant is expired or has no valid expires_utc value. No QEMU process was started.'
    }
    return $grant
}

# The authorization file is a QAC-issued, short-lived exclusive lease. Check
# it before any guest launch and again immediately before each fresh boot.
$grant = Assert-QacGrant $QacGrantFile
if (!(Test-Path -LiteralPath $QemuPath -PathType Leaf)) { throw "QEMU not found: $QemuPath" }
if (!(Test-Path -LiteralPath $FirmwareCode -PathType Leaf)) { throw "AArch64 UEFI code firmware not found: $FirmwareCode" }
if (!(Test-Path -LiteralPath $FirmwareVarsTemplate -PathType Leaf)) { throw "AArch64 UEFI vars template not found: $FirmwareVarsTemplate" }

$version = & $QemuPath --version 2>&1 | Out-String
if ($LASTEXITCODE -ne 0) { throw 'Unable to read installed QEMU version under the active QAC reservation' }
$cpuList = & $QemuPath -cpu help 2>&1 | Out-String
if ($LASTEXITCODE -ne 0 -or $cpuList -notmatch '(?im)^\s*cortex-a57\b') {
    throw 'Installed QEMU does not report cortex-a57 in its supported CPU models'
}
$machineList = & $QemuPath -machine help 2>&1 | Out-String
if ($LASTEXITCODE -ne 0 -or $machineList -notmatch '(?im)^\s*virt\b') {
    throw 'Installed QEMU does not report the virt machine'
}
Write-Host "QAC reservation $($grant.reservation_id) active; $($version.Trim())" -ForegroundColor Cyan

& (Join-Path $PSScriptRoot 'build-aarch64-gicv3.ps1') -OutputDirectory $ArtifactDirectory
if ($LASTEXITCODE -ne 0) { throw 'AArch64 GICv3 build failed' }
$esp = Join-Path $ArtifactDirectory 'esp'
foreach ($required in @('EFI\BOOT\BOOTAA64.EFI', 'kernel.elf', 'ramdisk.img')) {
    if (!(Test-Path -LiteralPath (Join-Path $esp $required) -PathType Leaf)) {
        throw "GICv3 ESP is missing $required"
    }
}
$logsDirectory = Join-Path $ArtifactDirectory 'logs-gicv3'
$null = New-Item -ItemType Directory -Path $logsDirectory -Force

function Invoke-Gicv3Boot {
    param([int]$Run, [string]$VarsFile, [string]$LogFile)
    $arguments = @('-machine', 'virt,gic-version=3,acpi=off', '-cpu', 'cortex-a57', '-m', '512M',
        '-drive', "if=pflash,format=raw,unit=0,readonly=on,file=$FirmwareCode",
        '-drive', "if=pflash,format=raw,unit=1,file=$VarsFile",
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
    $output | Set-Content -LiteralPath $LogFile -Encoding utf8
    Write-Host $output -NoNewline
    Write-Host "GICv3 QEMU log: $LogFile" -ForegroundColor DarkGray
    if ($timedOut) { Write-Host "QEMU stopped after $TimeoutSeconds seconds." -ForegroundColor DarkGray }
    return [pscustomobject]@{ ExitCode = $process.ExitCode; TimedOut = $timedOut; Output = $output }
}

$requiredMarkers = @(
    '[guideXOS] AARCH64 kernel entry', '[guideXOS] DTB: OK',
    '[guideXOS] GIC: OK version=3', '[guideXOS] scheduler: initialized',
    'AARCH64_PHASE4_PASS'
)
for ($run = 1; $run -le 3; ++$run) {
    $null = Assert-QacGrant $QacGrantFile
    $varsPath = Join-Path $ArtifactDirectory ("edk2-gicv3-vars-{0}.fd" -f $run)
    Copy-Item -LiteralPath $FirmwareVarsTemplate -Destination $varsPath -Force
    $logPath = Join-Path $logsDirectory ("boot-{0}.log" -f $run)
    Write-Host "Starting QAC-authorized fresh AArch64 GICv3 boot $run/3..." -ForegroundColor Yellow
    $result = Invoke-Gicv3Boot -Run $run -VarsFile $varsPath -LogFile $logPath
    $exitCode = [int]$result.ExitCode
    $timedOut = [bool]$result.TimedOut
    $text = [string]$result.Output
    if ($timedOut -or $exitCode -ne 0) { throw "GICv3 QEMU boot $run did not exit with a guest pass result" }
    if ($text -match 'AARCH64_PHASE[234]_ERROR|\[guideXOS\].*(FAIL|FATAL|ERROR)|\[A64 UEFI\] ERROR') {
        throw "GICv3 QEMU boot $run contains an error marker"
    }
    $last = -1
    foreach ($marker in $requiredMarkers) {
        $index = $text.IndexOf($marker, [StringComparison]::Ordinal)
        if ($index -lt 0 -or $index -le $last) { throw "GICv3 QEMU boot $run lacks ordered marker: $marker" }
        $last = $index
    }
    if ($text -notmatch 'preemptions=([1-9][0-9]*)') { throw "GICv3 QEMU boot $run lacks scheduler preemption evidence" }
    if ([uint64]$Matches[1] -lt 1000) { throw "GICv3 QEMU boot $run measured fewer than 1,000 timer preemptions" }
    if ($text -notmatch 'unexpected-irq=0 exceptions=0') { throw "GICv3 QEMU boot $run measured an unexpected IRQ or exception" }
    Write-Host "Fresh GICv3 QEMU boot $run/3: PASS" -ForegroundColor Green
}
Write-Host 'GICV3_GUEST_VERIFIED: three QAC-authorized boots, timer PPI, common IRQ, scheduler preemption, zero unexpected IRQs/exceptions.' -ForegroundColor Green
exit 0
