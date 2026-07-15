param(
    [int]$TimeoutSeconds = 120,
    [switch]$Runtime
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
        throw "[VirtioGpuInputRoutingSmoke] $Message"
    }
}

$mapperHeader = Read-Text (Join-Path $Root 'kernel\core\include\kernel\display_input_mapper.h')
$mapperCpp = Read-Text (Join-Path $Root 'kernel\core\display_input_mapper.cpp')
$inputHeader = Read-Text (Join-Path $Root 'kernel\core\include\kernel\input_manager.h')
$inputCpp = Read-Text (Join-Path $Root 'kernel\core\input_manager.cpp')
$proofHeader = Read-Text (Join-Path $Root 'kernel\core\include\kernel\qemu_display_input_proof.h')
$proofCpp = Read-Text (Join-Path $Root 'kernel\core\qemu_display_input_proof.cpp')
$mainCpp = Read-Text (Join-Path $Root 'kernel\core\main.cpp')
$virtioGpuCpp = Read-Text (Join-Path $Root 'kernel\core\virtio_gpu.cpp')
$virtioGpuHeader = Read-Text (Join-Path $Root 'kernel\core\include\kernel\virtio_gpu.h')
$probeSmoke = Read-Text (Join-Path $Root 'scripts\smoke-qemu-display-probe.ps1')
$launcher = Read-Text (Join-Path $Root 'scripts\run-qemu-display-probe.bat')
$warning = 'REAL HARDWARE GPU/MMIO ENABLEMENT IS MULE TERRITORY AND REQUIRES A SEPARATE SAFETY CHECKPOINT.'

Assert-True ($mapperHeader.Contains('struct DisplayPointerEvent')) 'DisplayPointerEvent must be backend-independent'
Assert-True ($mapperHeader.Contains('class DisplayInputMapper')) 'DisplayInputMapper must be present'
Assert-True ($mapperHeader.Contains('sourceHead') -and $mapperHeader.Contains('sourceMonitor')) 'source head and monitor must be tracked separately'
Assert-True ($mapperHeader.Contains('rawMinX') -and $mapperHeader.Contains('rawMaxX')) 'absolute raw ranges must be represented'
Assert-True ($mapperHeader.Contains('mappingReason')) 'mapping reason must be represented'
Assert-True ($mapperCpp.Contains('mapHeadLocalToVirtual')) 'head-local absolute mapping must exist'
Assert-True ($mapperCpp.Contains('mapNormalizedAbsolute')) 'normalized absolute mapping must exist'
Assert-True ($mapperCpp.Contains('mapRelativePointer')) 'relative mapping must exist'
Assert-True ($mapperCpp.Contains('mapUnknownHeadAbsolute')) 'unknown-head fallback must be explicit'
Assert-True ($mapperCpp.Contains('m_cursorX') -and $mapperCpp.Contains('m_cursorY')) 'relative input must use the global virtual cursor'
Assert-True ($mapperCpp.Contains('clampVirtualPoint')) 'cursor must clamp to virtual desktop bounds'
Assert-True ($mapperCpp.Contains('last-active-monitor') -and $mapperCpp.Contains('primary-monitor')) 'unknown-head policy must be diagnosed'
Assert-True (-not $mapperCpp.Contains('sourceHead + 1')) 'head mapping must not hardcode monitor ID arithmetic'
Assert-True ($inputHeader.Contains('DisplayPointerEvent mapping')) 'input manager must expose the mapped event'
Assert-True ($inputCpp.Contains('Ps2Relative') -and $inputCpp.Contains('UsbRelative')) 'relative PS/2 and USB paths must map through the abstraction'
Assert-True ($inputCpp.Contains('mapUnknownHeadAbsolute')) 'absolute unknown-head path must use the documented fallback'
Assert-True ($inputCpp.Contains('set_mapping_diagnostics')) 'input diagnostics must have a verbosity gate'
Assert-True ($inputCpp.Contains('mappingDiagnosticLimit') -or $inputCpp.Contains('diagnosticsLimit')) 'input diagnostics must be bounded'

Assert-True ($proofHeader.Contains('dragCapture') -and $proofHeader.Contains('captureReleased')) 'drag capture and cleanup state must be tracked'
Assert-True ($proofCpp.Contains('KernelCompositor::hitTest')) 'proof must reuse compositor hit testing'
Assert-True ($proofCpp.Contains('KernelCompositor::handleMouseDown') -and $proofCpp.Contains('KernelCompositor::handleMouseUp')) 'proof must use normal window-manager button handling'
Assert-True ($proofCpp.Contains('KernelCompositor::handleMouseMove')) 'proof must use normal window-manager movement handling'
Assert-True ($proofCpp.Contains('dragCrossedBoundary') -and $proofCpp.Contains('boundaryX')) 'boundary crossing must be diagnosed'
Assert-True ($proofCpp.Contains('guest-input-path-ps2-relative-no-source-head')) 'head-aware absolute limitation must be explicit'

Assert-True ($mainCpp.Contains('GXOS_QEMU_VIRTIO_GPU_PROBE_ACTIVE') -and $mainCpp.Contains('GXOS_QEMU_VIRTIO_GPU_COMPOSITOR_LIVE')) 'QEMU live gates must remain mandatory'
Assert-True ($mainCpp.Contains('QEMU input path=ps2-relative')) 'actual launcher input path must be recorded'
Assert-True ($virtioGpuHeader.Contains('get_display_input_layout')) 'display geometry must be exported to the backend-independent mapper'
Assert-True ($virtioGpuCpp.Contains('get_display_input_layout')) 'display geometry export must be implemented'

Assert-True ($launcher.Contains('-machine q35,usb=off')) 'default QEMU launcher must preserve the documented PS/2-relative setup'
Assert-True ($launcher.Contains('virtio-gpu-pci,id=gpu0,max_outputs=2')) 'launcher must preserve gpu0 with two outputs'
Assert-True (-not $launcher.Contains('usb-tablet')) 'default launcher must not silently change to an absolute tablet'
Assert-True ($probeSmoke.Contains('query-qmp-schema') -and $probeSmoke.Contains('input-send-event')) 'QMP capability discovery and bounded injection must exist'
Assert-True ($probeSmoke.Contains('compositorInputBounded')) 'input-routing mode must be exposed by the QEMU smoke'
Assert-True ($probeSmoke.Contains("& `$capture 'click'") -and $probeSmoke.Contains("& `$capture 'boundary'") -and $probeSmoke.Contains("& `$capture 'after-drag'")) 'before/during/after input captures must be named'
Assert-True ($probeSmoke.Contains('bounded relative move exceeded the injected-event limit')) 'injected events must have a bounded limit'

Assert-True ($mainCpp.Contains($warning) -or $virtioGpuCpp.Contains($warning) -or $proofCpp.Contains($warning)) 'Mule Territory warning must remain exact'
Assert-True (-not $virtioGpuCpp.Contains('CMD_UPDATE_CURSOR')) 'no virtio-gpu cursor queue command may be added'
Assert-True (-not $virtioGpuCpp.Contains('CMD_MOVE_CURSOR')) 'no hardware cursor command may be added'
Assert-True (-not $virtioGpuCpp.Contains('CMD_SUBMIT_3D')) 'no 3D command path may be added'
Assert-True (-not $virtioGpuCpp.Contains('CMD_RESOURCE_CREATE_3D')) 'no 3D resource path may be added'
Assert-True (-not $virtioGpuCpp.Contains('CMD_RESOURCE_CREATE_BLOB')) 'no blob resource path may be added'
Assert-True (-not $virtioGpuCpp.Contains('CMD_CTX_CREATE')) 'no context command path may be added'
Assert-True ($virtioGpuCpp.Contains('No physical Intel GPU support')) 'physical Intel GPU support must remain disabled'
Assert-True ($virtioGpuCpp.Contains('No real hardware GPU BAR access')) 'real hardware GPU BAR access must remain disabled'
Assert-True ($virtioGpuCpp.Contains('No real hardware GPU/MMIO enablement')) 'real hardware GPU/MMIO enablement must remain disabled'

Write-Host '[VirtioGpuInputRoutingSmoke] source checks passed.'

if ($Runtime) {
    $probeScript = Join-Path $Root 'scripts\smoke-qemu-display-probe.ps1'
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $probeScript -Backends @('virtio-gpu') -Mode compositorInputBounded -TimeoutSeconds $TimeoutSeconds
    if ($LASTEXITCODE -ne 0) {
        throw "[VirtioGpuInputRoutingSmoke] bounded QEMU input-routing smoke failed with exit code $LASTEXITCODE."
    }
}

Write-Host '[VirtioGpuInputRoutingSmoke] passed.'
