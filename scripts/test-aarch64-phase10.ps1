[CmdletBinding()]
param(
    [string]$LlvmRoot = 'C:\Program Files\LLVM\bin',
    [string]$QemuPath = 'C:\Program Files\qemu\qemu-system-aarch64.exe',
    [string]$FirmwareCode = 'C:\Program Files\qemu\share\edk2-aarch64-code.fd',
    [int]$Boots = 3,
    [int]$StartingMonitorPort = 4910,
    [int]$TimeoutSeconds = 900,
    [switch]$SkipBuild,
    [switch]$SkipHistoricalRegressions
)

$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$artifactDirectory = Join-Path $repoRoot 'out\aarch64-phase10'
if (!$SkipBuild) {
    & (Join-Path $PSScriptRoot 'build-aarch64-phase10.ps1') -LlvmRoot $LlvmRoot -OutputDirectory $artifactDirectory
    if ($LASTEXITCODE -ne 0) { throw 'AArch64 Phase 10 build failed' }
}

function Read-GuestText([string]$Path) {
    if (!(Test-Path -LiteralPath $Path -PathType Leaf)) { return '' }
    $text = Get-Content -Raw -LiteralPath $Path
    if ($null -eq $text) { return '' }
    return [string]$text
}
function Wait-GuestMarker([string]$Path, [string]$Marker, [Diagnostics.Process]$Runner, [datetime]$Deadline) {
    while ([DateTime]::UtcNow -lt $Deadline) {
        $text = Read-GuestText $Path
        if ($text -match 'AARCH64_PHASE10_ERROR|\[guideXOS\].*(FATAL|ERROR)') { throw "Guest error while waiting for $Marker" }
        if ($text.Contains($Marker)) { return $text }
        if ($Runner.HasExited -and !$text.Contains($Marker)) { throw "Runner exited before $Marker" }
        Start-Sleep -Milliseconds 100
    }
    throw "Timed out waiting for guest marker: $Marker"
}
function Wait-Occurrence([string]$Path, [string]$Marker, [int]$Count, [Diagnostics.Process]$Runner, [datetime]$Deadline) {
    while ([DateTime]::UtcNow -lt $Deadline) {
        $text = Read-GuestText $Path
        if ($text -match 'AARCH64_PHASE10_ERROR|\[guideXOS\].*(FATAL|ERROR)') { throw "Guest error while waiting for $Marker" }
        if ([regex]::Matches($text, [regex]::Escape($Marker)).Count -ge $Count) { return $text }
        if ($Runner.HasExited) { throw "Runner exited before occurrence $Count of $Marker" }
        Start-Sleep -Milliseconds 100
    }
    throw "Timed out waiting for occurrence $Count of $Marker"
}
function Test-GuestMarker([string]$Path, [string]$Marker, [Diagnostics.Process]$Runner, [datetime]$Deadline) {
    while ([DateTime]::UtcNow -lt $Deadline) {
        $text = Read-GuestText $Path
        if ($text -match 'AARCH64_PHASE10_ERROR|\[guideXOS\].*(FATAL|ERROR)') { throw "Guest error while waiting for $Marker" }
        if ($text.Contains($Marker)) { return $true }
        if ($Runner.HasExited) { return $false }
        Start-Sleep -Milliseconds 100
    }
    return $false
}
function Stop-GuestQemu([string]$LogPath) {
    $guestProcesses = Get-CimInstance Win32_Process -Filter "Name='qemu-system-aarch64.exe'" -ErrorAction SilentlyContinue
    foreach ($guest in $guestProcesses) {
        if ($guest.CommandLine -and $guest.CommandLine.Contains($LogPath)) {
            Stop-Process -Id $guest.ProcessId -Force -ErrorAction SilentlyContinue
        }
    }
}

if ($Boots -lt 1) { throw 'Boots must be positive' }
$varsTemplate = 'C:\Program Files\qemu\share\edk2-arm-vars.fd'
if (!(Test-Path -LiteralPath $varsTemplate -PathType Leaf)) { throw "AArch64 UEFI variable template not found: $varsTemplate" }
$runScript = Join-Path $PSScriptRoot 'run-aarch64-phase10.ps1'
$qmpClickScript = Join-Path $PSScriptRoot 'send-aarch64-qmp-click.ps1'
$logsDirectory = Join-Path $artifactDirectory 'logs'
$null = New-Item -ItemType Directory -Path $logsDirectory -Force
for ($boot = 1; $boot -le $Boots; ++$boot) {
    $port = $StartingMonitorPort + $boot - 1
    $varsPath = Join-Path $artifactDirectory ("edk2-aarch64-phase10-vars-{0}.fd" -f $boot)
    Copy-Item -LiteralPath $varsTemplate -Destination $varsPath -Force
    $logPath = Join-Path $logsDirectory ("phase10-boot-{0}.log" -f $boot)
    $arguments = @('-NoProfile','-ExecutionPolicy','Bypass','-File',$runScript,'-ArtifactDirectory',$artifactDirectory,
        '-QemuPath',$QemuPath,'-FirmwareCode',$FirmwareCode,'-FirmwareVars',$varsPath,'-LogPath',$logPath,
        '-MonitorPort',$port,'-DisplayBackend','gtk,gl=off','-TimeoutSeconds',$TimeoutSeconds)
    Write-Host "Starting fresh AArch64 Phase 10 QEMU boot $boot/$Boots..." -ForegroundColor Yellow
    $argumentString = ($arguments | ForEach-Object { $value = [string]$_; '"' + $value.Replace('"', '\"') + '"' }) -join ' '
    $runner = Start-Process -FilePath (Get-Command powershell.exe).Source -ArgumentList $argumentString -WindowStyle Hidden -PassThru
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    try {
        $null = Wait-GuestMarker $logPath '[guideXOS] App Model package: com.guidexos.phase10.multiarchproof' $runner $deadline
        $null = Wait-GuestMarker $logPath '[phase10-app] package identity: PASS' $runner $deadline
        $null = Wait-GuestMarker $logPath '[phase10-app] GUI ready: PASS' $runner $deadline
        $null = Wait-GuestMarker $logPath '[guideXOS] multiarch app waiting: PASS' $runner $deadline
        # The guest writes the waiting marker immediately before entering the
        # shared blocking wait; allow the scheduler to complete that transition
        # before injecting the first input event.
        Start-Sleep -Milliseconds 1500
        # Window is placed at x=55,y=95 by the shared Phase-9 GUI owner;
        # the Phase-10 verify and close buttons are in the client at x=70/260,y=132.
        $buttonSeen = $false
        for ($attempt = 1; $attempt -le 4 -and !$buttonSeen; ++$attempt) {
            & (Get-Command powershell.exe).Source -NoProfile -ExecutionPolicy Bypass -File $qmpClickScript -Port $port -X 9200 -Y 14500
            if ($LASTEXITCODE -ne 0) { throw "Verify click helper failed for boot $boot" }
            Write-Host "Phase 10 boot ${boot}: verify click sent (attempt $attempt)" -ForegroundColor DarkGray
            $buttonSeen = Test-GuestMarker $logPath '[phase10-app] button: PASS' $runner ([DateTime]::UtcNow.AddSeconds(3))
        }
        if (!$buttonSeen) { throw "Fresh Phase 10 boot $boot did not receive the verify click" }
        & (Get-Command powershell.exe).Source -NoProfile -ExecutionPolicy Bypass -File $qmpClickScript -Port $port -X 13000 -Y 14500
        if ($LASTEXITCODE -ne 0) { throw "Close click helper failed for boot $boot" }
        $null = Wait-GuestMarker $logPath '[phase10-app] close requested' $runner $deadline
        $null = Wait-GuestMarker $logPath 'AARCH64_PHASE10_PASS' $runner $deadline
        $text = Read-GuestText $logPath
        $required = @(
            '[guideXOS] package architecture: arm64','[guideXOS] selected payload: bin/arm64/phase10-multiarch-proof.elf',
            '[guideXOS] selected ELF machine: EM_AARCH64','[phase10-app] package identity: PASS',
            '[phase10-app] running architecture: arm64','[phase10-app] GUI ABI: PASS','[phase10-app] payload selection: PASS',
            '[phase10-app] GUI ready: PASS','[guideXOS] multiarch app waiting: PASS','[guideXOS] multiarch app wait/wake: PASS','[phase10-app] button: PASS','[guideXOS] NativeElf GUI cleanup: PASS',
            '[guideXOS] multiarch app cleanup: PASS','[guideXOS] multiarch package relaunch: PASS',
            '[guideXOS] architecture resolver durability: PASS launches=100','[guideXOS] multiarch lifecycle durability: PASS launches=26',
            '[guideXOS] graphics/scheduler integration: PASS','AARCH64_PHASE10_PASS')
        $last = -1
        foreach ($marker in $required) {
            $index = $text.IndexOf($marker, [StringComparison]::Ordinal)
            if ($index -lt 0) { throw "Fresh Phase 10 boot $boot is missing marker: $marker" }
            if ($index -le $last) { throw "Fresh Phase 10 boot $boot marker order failed at: $marker" }
            $last = $index
        }
        if ([regex]::Matches($text, '\[phase10-app\] package identity: PASS').Count -ne 26) { throw "Boot $boot did not complete 26 application launches" }
        if ([regex]::Matches($text, '\[guideXOS\] multiarch app cleanup: PASS').Count -ne 26) { throw "Boot $boot did not clean up every lifecycle" }
        if ($text -match 'AARCH64_PHASE9_PASS|AARCH64_PHASE7_PASS|unexpected-irq=[1-9]|exceptions=[1-9]') { throw "Boot $boot contains a forbidden or unhealthy marker" }
        Write-Host "Fresh Phase 10 multiarch boot $boot/${Boots}: PASS (identity selection, input, wait/wake, relaunch, 26 clean lifecycles)" -ForegroundColor Green
    } finally {
        if (!$runner.HasExited) { try { $runner.Kill() } catch {} }
        $runner.WaitForExit()
        Start-Sleep -Milliseconds 200
        Stop-GuestQemu $logPath
    }
}

if (!$SkipHistoricalRegressions) {
    & (Join-Path $PSScriptRoot 'test-aarch64-phase9.ps1') -TimeoutSeconds 240
    if ($LASTEXITCODE -ne 0) { throw 'Phase 9 regression failed' }
}
Write-Host "AARCH64_PHASE10_QMP_HARNESS_PASS boots=$Boots" -ForegroundColor Green
