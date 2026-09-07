[CmdletBinding()]
param(
    [string]$LlvmRoot = 'C:\Program Files\LLVM\bin',
    [string]$QemuPath = 'C:\Program Files\qemu\qemu-system-aarch64.exe',
    [string]$FirmwareCode = 'C:\Program Files\qemu\share\edk2-aarch64-code.fd',
    [int]$TimeoutSeconds = 180,
    [int]$HistoricalTimeoutSeconds = 120,
    [switch]$SkipHistoricalRegressions
)

$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$artifactDirectory = Join-Path $repoRoot 'out\aarch64-phase6'
& (Join-Path $PSScriptRoot 'build-aarch64-phase6.ps1') -LlvmRoot $LlvmRoot -OutputDirectory $artifactDirectory
if ($LASTEXITCODE -ne 0) { throw 'AArch64 Phase 6 build failed' }

$esp = Join-Path $artifactDirectory 'esp'
$kernelMetadata = Get-Content -Raw (Join-Path $artifactDirectory 'kernel-llvm-readobj.txt')
$appMetadata = Get-Content -Raw (Join-Path $artifactDirectory 'phase6-app-llvm-readobj.txt')
if ($kernelMetadata -notmatch 'Machine: EM_AARCH64' -or $appMetadata -notmatch 'Machine: EM_AARCH64' -or
    $appMetadata -notmatch 'Name: gx_main') {
    throw 'Phase 6 ARM64 artifact metadata is incomplete'
}
if (!(Test-Path -LiteralPath (Join-Path $artifactDirectory 'ramdisk-root\wall\blueflwr.gxi') -PathType Leaf)) {
    throw 'normal desktop wallpaper resource is missing from the staged VFS root'
}

# The selected ramfb surface is deterministic with this firmware/QEMU pair.
# Changing the common desktop render path must update this value deliberately.
$expectedFramebufferHash = 'cd1073981e16c449'
$requiredMarkers = @(
    '[guideXOS] GOP framebuffer: OK', '[guideXOS] framebuffer mapping: OK',
    '[guideXOS] framebuffer: base=', '[guideXOS] common boot resources: OK',
    '[guideXOS] common physical allocator: PASS', '[guideXOS] kernel heap: PASS',
    '[guideXOS] pixel format: OK', '[guideXOS] ramdisk: mounted', '[guideXOS] VFS root: OK',
    '[guideXOS] common kernel entry: OK', '[guideXOS] architecture interface: ARM64',
    '[guideXOS] compositor: initialized', '[guideXOS] text rendering: PASS',
    '[guideXOS] desktop resources: OK',
    '[guideXOS] desktop frame: rendered', '[guideXOS] framebuffer verification: PASS hash=',
    '[guideXOS] graphics primitives: PASS',
    '[guideXOS] graphics durability: PASS redraws=96',
    '[guideXOS] App Model: initialized', '[guideXOS] App discovery count=2',
    '[guideXOS] NativeElf architecture: ARM64', '[guideXOS] NativeElf ELF validation: PASS',
    '[guideXOS] NativeElf segments: loaded', '[guideXOS] NativeElf icache sync: PASS',
    '[guideXOS] NativeElf application stack: OK', '[phase5-app] gx_main entered',
    '[guideXOS] NativeElf ARM64 return: 42', '[guideXOS] NativeElf cleanup: PASS',
    '[guideXOS] ARM64 App Model relaunch: PASS', '[guideXOS] NativeElf wrong architecture: rejected',
    '[guideXOS] App Model durability: PASS', '[guideXOS] scheduler completion task: entered',
    '[guideXOS] graphics/scheduler integration: PASS', '[guideXOS] scheduler/VFS integration: PASS',
    '[guideXOS] memory/VFS durability: PASS', 'AARCH64_PHASE6_PASS'
)

function Assert-GoodBoot([string]$Text, [string]$Name) {
    if ($Text -match 'AARCH64_PHASE[23456]_ERROR|\[guideXOS\].*(FAIL|FATAL|ERROR)|\[A64 UEFI\] ERROR') {
        throw "$Name contains an error marker"
    }
    $last = -1
    foreach ($marker in $requiredMarkers) {
        $index = $Text.IndexOf($marker, [StringComparison]::Ordinal)
        if ($index -lt 0) { throw "$Name is missing marker: $marker" }
        if ($index -le $last) { throw "$Name markers are out of order at: $marker" }
        $last = $index
    }
    if ($Text -notmatch 'framebuffer: base=0x[0-9a-f]+ size=0x[0-9a-f]+ width=800 height=600 pitch=3200 bpp=32 format=2') {
        throw "$Name lacks the concrete framebuffer handoff"
    }
    if ($Text -notmatch ('framebuffer verification: PASS hash=0x' + $expectedFramebufferHash)) {
        throw "$Name framebuffer hash differs from the expected common desktop frame"
    }
    if ($Text -notmatch 'preemptions=1[0-9]{4,}') { throw "$Name lacks active preemption statistics" }
    if ($Text -notmatch 'unexpected-irq=0 exceptions=0') { throw "$Name reports unexpected IRQs or exceptions" }
    if ($Text -notmatch 'scheduler/VFS integration: PASS reads=[1-9][0-9]* enumerations=[1-9][0-9]*') {
        throw "$Name lacks the repeated VFS workload"
    }
    if ($Text -notmatch 'memory/VFS durability: PASS pages-used=[1-9][0-9]*') {
        throw "$Name lacks memory durability statistics"
    }
    if (([regex]::Matches($Text, '\[phase5-app\] gx_main entered')).Count -lt 100) {
        throw "$Name did not preserve the Phase-5 100-launch App Model proof"
    }
}

$logsDirectory = Join-Path $artifactDirectory 'logs'
$null = New-Item -ItemType Directory -Path $logsDirectory -Force
$varsTemplate = 'C:\Program Files\qemu\share\edk2-arm-vars.fd'
if (!(Test-Path -LiteralPath $varsTemplate -PathType Leaf)) { throw "AArch64 UEFI variable template not found: $varsTemplate" }
$hashes = @()
for ($run = 1; $run -le 3; ++$run) {
    $varsPath = Join-Path $artifactDirectory ("edk2-aarch64-vars-{0}.fd" -f $run)
    Copy-Item -LiteralPath $varsTemplate -Destination $varsPath -Force
    $logPath = Join-Path $logsDirectory ("boot-{0}.log" -f $run)
    $screenshot = ''
    $monitorPort = 0
    if ($run -eq 1) {
        $monitorPort = 4546
        $screenshot = Join-Path $artifactDirectory 'desktop-frame.ppm'
    }
    Write-Host "Starting fresh AArch64 Phase 6 QEMU boot $run/3 (ramfb GOP)..." -ForegroundColor Yellow
    $runArguments = @{
        ArtifactDirectory = $artifactDirectory; QemuPath = $QemuPath; FirmwareCode = $FirmwareCode
        FirmwareVars = $varsPath; LogPath = $logPath; DisplayDevice = 'ramfb'
        DisplayBackend = 'none'; MonitorPort = $monitorPort; ScreenshotPath = $screenshot
        TimeoutSeconds = $TimeoutSeconds
    }
    & (Join-Path $PSScriptRoot 'run-aarch64-phase6.ps1') @runArguments
    $runExit = $LASTEXITCODE
    $text = Get-Content -LiteralPath $logPath -Raw
    if ($runExit -ne 0) { throw "Fresh Phase 6 QEMU boot $run did not return a pass result" }
    Assert-GoodBoot $text ("fresh Phase 6 QEMU boot $run")
    $hash = [regex]::Match($text, 'framebuffer verification: PASS hash=(0x[0-9a-f]+)').Groups[1].Value
    $hashes += $hash
    Write-Host "Fresh Phase 6 QEMU boot $run/3: PASS ($hash)" -ForegroundColor Green
}
if (($hashes | Select-Object -Unique).Count -ne 1) { throw 'fresh Phase 6 boots produced different framebuffer hashes' }
if ($hashes[0] -ne ('0x' + $expectedFramebufferHash)) { throw 'fresh Phase 6 hash is not the expected deterministic value' }

if (!$SkipHistoricalRegressions) {
    foreach ($phase in 1..4) {
        $script = Join-Path $PSScriptRoot "test-aarch64-phase$phase.ps1"
        $phaseTimeout = $HistoricalTimeoutSeconds
        # The unchanged Phase-3 kernel reaches its 10,000-preemption proof
        # after more than two minutes under the installed QEMU TCG build.
        if ($phase -eq 3 -and $phaseTimeout -lt 240) { $phaseTimeout = 240 }
        if ($phase -eq 4 -and $phaseTimeout -lt 240) { $phaseTimeout = 240 }
        & $script -TimeoutSeconds $phaseTimeout
        if ($LASTEXITCODE -ne 0) { throw "Historical Phase $phase regression failed" }
    }
    & (Join-Path $PSScriptRoot 'test-aarch64-phase5.ps1') -TimeoutSeconds $TimeoutSeconds -HistoricalTimeoutSeconds $HistoricalTimeoutSeconds -SkipHistoricalRegressions
    if ($LASTEXITCODE -ne 0) { throw 'Historical Phase 5 regression failed' }
}

Write-Host 'AARCH64 Phase 6 test suite: PASS (host controls + three fresh GOP boots + Phase 1-5 regressions)' -ForegroundColor Green
exit 0
