param(
    [int]$TimeoutSeconds = 120,
    [switch]$SkipRuntime
)

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)

function Read-Text {
    param([Parameter(Mandatory = $true)][string]$Path)
    return Get-Content -LiteralPath $Path -Raw
}

function Assert-True {
    param(
        [Parameter(Mandatory = $true)][bool]$Condition,
        [Parameter(Mandatory = $true)][string]$Message
    )
    if (-not $Condition) {
        throw "[VirtioGpuLivePresentationSmoke] $Message"
    }
}

$virtioGpuCpp = Read-Text (Join-Path $Root 'kernel\core\virtio_gpu.cpp')
$desktopCpp = Read-Text (Join-Path $Root 'kernel\core\desktop.cpp')
$desktopHeader = Read-Text (Join-Path $Root 'kernel\core\include\kernel\desktop.h')
$virtioHeader = Read-Text (Join-Path $Root 'kernel\core\include\kernel\virtio_gpu.h')
$mainCpp = Read-Text (Join-Path $Root 'kernel\core\main.cpp')
$probeSmoke = Read-Text (Join-Path $Root 'scripts\smoke-qemu-display-probe.ps1')
$warning = 'REAL HARDWARE GPU/MMIO ENABLEMENT IS MULE TERRITORY AND REQUIRES A SEPARATE SAFETY CHECKPOINT.'

Assert-True ($virtioGpuCpp.Contains('GXOS_QEMU_VIRTIO_GPU_PROBE_ACTIVE')) 'QEMU probe gate must remain present'
Assert-True ($virtioGpuCpp.Contains('GXOS_QEMU_VIRTIO_GPU_COMPOSITOR_LIVE')) 'live mode gate must remain present'
Assert-True ($virtioGpuCpp.Contains('#if !defined(GXOS_QEMU_VIRTIO_GPU_PROBE_ACTIVE) || !defined(GXOS_QEMU_VIRTIO_GPU_COMPOSITOR_LIVE)')) 'presentation_tick must require both QEMU gates'
Assert-True ($virtioGpuCpp.Contains($warning)) 'Mule Territory warning must remain exact'

Assert-True ($desktopCpp.Contains('s_redrawGeneration')) 'desktop must own a monotonic redraw generation'
Assert-True ($desktopCpp.Contains('uint64_t redraw_generation()')) 'desktop redraw generation accessor must exist'
Assert-True ($desktopCpp.Contains('void request_redraw()')) 'desktop redraw bridge must exist'
Assert-True ($desktopHeader.Contains('uint64_t redraw_generation()')) 'desktop header must expose redraw generation'
Assert-True ($virtioGpuCpp.Contains('redraw_generation()')) 'live presenter must consume redraw generation'

Assert-True ($virtioGpuCpp.Contains('kLivePresentationFrameCap = 10u')) 'hard 10 FPS cap must be configured'
Assert-True ($virtioGpuCpp.Contains('kLivePresentationIntervalTicks = 10u')) 'hard minimum presentation interval must exist'
Assert-True ($virtioGpuCpp.Contains('rateLimitSkips')) 'rate-limit skips must be tracked'
Assert-True ($virtioGpuCpp.Contains('kLivePresentationBoundedAttemptLimit = 60u')) 'bounded live attempt limit must exist'
Assert-True ($virtioGpuCpp.Contains('kLivePresentationBoundedTimeLimitTicks = 800u')) 'bounded live time limit must exist'
Assert-True ($virtioGpuCpp.Contains('stop_live_presentation("frame-limit")')) 'bounded frame-limit stop must exist'
Assert-True ($virtioGpuCpp.Contains('stop_live_presentation("time-limit")')) 'bounded time-limit stop must exist'
Assert-True (-not $virtioGpuCpp.Contains('while (true)')) 'live presentation must not contain an unlimited loop'

Assert-True ($mainCpp.Contains('kernel::virtio::gpu::presentation_tick();')) 'scheduler must call the presenter through the normal update cadence'
Assert-True ($mainCpp.Contains('(void)kernel::desktop::needs_redraw();')) 'QEMU live pump must consume the normal invalidation request'
Assert-True ($virtioHeader.Contains('void presentation_tick();')) 'virtio-gpu header must expose the scheduler-owned tick'
Assert-True ($virtioGpuCpp.Contains('s_livePresentation.target0')) 'persistent target 0 state must exist'
Assert-True ($virtioGpuCpp.Contains('s_livePresentation.target1')) 'persistent target 1 state must exist'
Assert-True ($virtioGpuCpp.Contains('target.viewportOriginX')) 'target viewport origin X must be used'
Assert-True ($virtioGpuCpp.Contains('target.viewportOriginY')) 'target viewport origin Y must be used'
Assert-True ($virtioGpuCpp.Contains('draw_desktop_windows(surface, target.primary)')) 'taskbar visibility must remain primary-only'

Assert-True ($virtioGpuCpp.Contains('present_target_once(*live.device')) 'live mode must consume the synchronous target presenter'
Assert-True ($virtioGpuCpp.Contains('target0Result = render_target')) 'live mode must render target 0'
Assert-True ($virtioGpuCpp.Contains('target1Result = render_target')) 'live mode must render target 1'
Assert-True ($virtioGpuCpp.Contains('issue_transfer_to_host_2d')) 'whole-target transfer must remain explicit'
Assert-True ($virtioGpuCpp.Contains('issue_resource_flush')) 'whole-target flush must remain explicit'
Assert-True ($virtioGpuCpp.Contains('queue_release_descriptor')) 'synchronous descriptor reclamation must remain present'
Assert-True ($virtioGpuCpp.Contains('repaint_static_fallback_target')) 'static-pattern fallback must remain callable'
Assert-True ($virtioGpuCpp.Contains('fallbackActivated')) 'fallback diagnostics must be reported'

Assert-True (-not $virtioGpuCpp.Contains('CMD_UPDATE_CURSOR')) 'live source must not add cursor commands'
Assert-True (-not $virtioGpuCpp.Contains('CMD_MOVE_CURSOR')) 'live source must not add cursor movement commands'
Assert-True (-not $virtioGpuCpp.Contains('FEATURE_VIRGL')) 'live source must not negotiate virgl'
Assert-True (-not $virtioGpuCpp.Contains('CMD_SUBMIT_3D')) 'live source must not add 3D submission'
Assert-True (-not $virtioGpuCpp.Contains('CMD_RESOURCE_CREATE_3D')) 'live source must not add 3D resources'
Assert-True (-not $virtioGpuCpp.Contains('CMD_RESOURCE_CREATE_BLOB')) 'live source must not add blob resources'
Assert-True (-not $virtioGpuCpp.Contains('CMD_CTX_CREATE')) 'live source must not add context commands'
Assert-True (-not $virtioGpuCpp.Contains('mouse routing')) 'live source must not add multi-output input routing'
Assert-True (-not $virtioGpuCpp.Contains('warp cursor')) 'live source must not warp the cursor'
Assert-True ($virtioGpuCpp.Contains('No physical Intel GPU support')) 'real physical Intel GPU support must remain disabled'
Assert-True ($virtioGpuCpp.Contains('No real hardware GPU BAR access')) 'real hardware BAR access must remain disabled'
Assert-True ($virtioGpuCpp.Contains('No real hardware GPU/MMIO enablement')) 'real hardware enablement must remain disabled'

Assert-True ($probeSmoke.Contains('compositorLiveBounded')) 'QEMU probe smoke must expose the bounded live mode'
Assert-True ($probeSmoke.Contains('initial-head0.png')) 'QEMU probe smoke must capture initial head 0'
Assert-True ($probeSmoke.Contains('initial-head1.png')) 'QEMU probe smoke must capture initial head 1'
Assert-True ($probeSmoke.Contains('final-head0.png')) 'QEMU probe smoke must capture final head 0'
Assert-True ($probeSmoke.Contains('final-head1.png')) 'QEMU probe smoke must capture final head 1'
Assert-True ($probeSmoke.Contains('initialChecksum0')) 'QEMU probe smoke must report initial checksums'
Assert-True ($probeSmoke.Contains('visualChanged0')) 'QEMU probe smoke must report visual changes'

Write-Host '[VirtioGpuLivePresentationSmoke] source checks passed.'

if (-not $SkipRuntime) {
    $probeScript = Join-Path $Root 'scripts\smoke-qemu-display-probe.ps1'
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $probeScript -Backends @('virtio-gpu') -Mode compositorLiveBounded -TimeoutSeconds $TimeoutSeconds
    if ($LASTEXITCODE -ne 0) {
        throw "[VirtioGpuLivePresentationSmoke] bounded QEMU runtime smoke failed with exit code $LASTEXITCODE."
    }
}

Write-Host '[VirtioGpuLivePresentationSmoke] passed.'
