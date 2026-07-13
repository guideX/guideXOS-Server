param()

$ErrorActionPreference = 'Stop'

$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)

function Assert-True {
    param(
        [Parameter(Mandatory = $true)]
        [bool]$Condition,
        [Parameter(Mandatory = $true)]
        [string]$Message
    )

    if (-not $Condition) {
        throw $Message
    }
}

function Read-Text {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    if (-not (Test-Path -LiteralPath $Path)) {
        throw "Missing file: $Path"
    }

    $text = Get-Content -LiteralPath $Path -Raw
    if ($null -eq $text) {
        throw "Unable to read file: $Path"
    }

    return $text
}

function Get-FunctionBody {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Text,
        [Parameter(Mandatory = $true)]
        [string]$SignaturePattern
    )

    $match = [regex]::Match($Text, $SignaturePattern, [System.Text.RegularExpressions.RegexOptions]::Singleline)
    if (-not $match.Success) {
        return $null
    }

    $start = $Text.IndexOf('{', $match.Index + $match.Length - 1)
    if ($start -lt 0) {
        return $null
    }

    $depth = 0
    for ($index = $start; $index -lt $Text.Length; ++$index) {
        $ch = $Text[$index]
        if ($ch -eq '{') {
            ++$depth
        } elseif ($ch -eq '}') {
            --$depth
            if ($depth -eq 0) {
                return $Text.Substring($start, $index - $start + 1)
            }
        }
    }

    return $null
}

$backendHeader = Read-Text -Path (Join-Path $Root 'virtio_gpu_display_backend.h')
$displayModel = Read-Text -Path (Join-Path $Root 'display_model.h')
$virtioGpuCpp = Read-Text -Path (Join-Path $Root 'kernel\core\virtio_gpu.cpp')
$compositorCpp = Read-Text -Path (Join-Path $Root 'compositor.cpp')
$probeSmoke = Read-Text -Path (Join-Path $Root 'scripts\smoke-qemu-display-probe.ps1')

Assert-True ($backendHeader.Contains('struct VirtioGpuScanoutState')) 'virtio_gpu_display_backend.h should define the scanout state record'
Assert-True ($backendHeader.Contains('struct VirtioGpuOutputInventory')) 'virtio_gpu_display_backend.h should define the output inventory record'
Assert-True ($backendHeader.Contains('static bool isQemuOnly()')) 'virtio_gpu_display_backend.h should keep the QEMU-only gate helper'
Assert-True ($backendHeader.Contains('getVirtioGpuOutputInventory')) 'virtio_gpu_display_backend.h should build a virtio-gpu output inventory'
Assert-True ($backendHeader.Contains('buildVirtioGpuDisplayMonitors')) 'virtio_gpu_display_backend.h should bridge output inventory into DisplayMonitor descriptors'
Assert-True ($backendHeader.Contains('buildVirtioGpuDisplayViewports')) 'virtio_gpu_display_backend.h should bridge output inventory into DisplayViewport descriptors'
Assert-True ($backendHeader.Contains('buildVirtioGpuDisplayTargets')) 'virtio_gpu_display_backend.h should bridge output inventory into DisplayRenderTarget descriptors'
Assert-True ($backendHeader.Contains('presentVirtioGpuTarget')) 'virtio_gpu_display_backend.h should keep the present helper for future compositor integration'
Assert-True ($backendHeader.Contains('updateVirtioGpuTarget')) 'virtio_gpu_display_backend.h should keep the update helper for future compositor integration'

$qemuOnlyBody = Get-FunctionBody -Text $backendHeader -SignaturePattern 'static bool isQemuOnly\(\)'
Assert-True ($null -ne $qemuOnlyBody) 'isQemuOnly should be parsable'
Assert-True ($qemuOnlyBody.Contains('return true;')) 'isQemuOnly should remain a hard QEMU-only gate'

$operationalBody = Get-FunctionBody -Text $backendHeader -SignaturePattern 'bool isOperational\(uint32_t deviceConfigNumScanouts\) const'
Assert-True ($null -ne $operationalBody) 'VirtioGpuScanoutState::isOperational should be parsable'
Assert-True ($operationalBody.Contains('scanoutId < deviceConfigNumScanouts')) 'operational scanouts should stay bounded by the device-reported scanout count'
Assert-True ($operationalBody.Contains('resourceId != 0u')) 'operational scanouts should require a nonzero resource id'
Assert-True ($operationalBody.Contains('backingAttached')) 'operational scanouts should require backing attachment'
Assert-True ($operationalBody.Contains('resourceBound')) 'operational scanouts should require a bound resource'
Assert-True ($operationalBody.Contains('transferReady')) 'operational scanouts should require a successful transfer'
Assert-True ($operationalBody.Contains('presentReady')) 'operational scanouts should require a successful flush'
Assert-True (-not $operationalBody.Contains('connectorEnabled')) 'operational readiness should not be an alias for connector state'

$inventoryBody = Get-FunctionBody -Text $backendHeader -SignaturePattern 'static VirtioGpuOutputInventory getVirtioGpuOutputInventory\('
Assert-True ($null -ne $inventoryBody) 'getVirtioGpuOutputInventory should be parsable'
Assert-True ($inventoryBody.Contains('scanout.isOperational')) 'the inventory builder should filter operational scanouts explicitly'
Assert-True ($inventoryBody.Contains('inventory.monitors.push_back(monitor);')) 'the inventory builder should create DisplayMonitor descriptors'
Assert-True ($inventoryBody.Contains('inventory.viewports.push_back(viewport);')) 'the inventory builder should create DisplayViewport descriptors'
Assert-True ($inventoryBody.Contains('inventory.renderTargets.push_back(target);')) 'the inventory builder should create DisplayRenderTarget descriptors'
Assert-True ($inventoryBody.Contains('inventory.virtualDesktop.recomputeBounds();')) 'the inventory builder should recompute virtual desktop bounds from the assigned geometry'
Assert-True ($inventoryBody.Contains('inventory.primaryOutput')) 'the inventory builder should track the primary output'
Assert-True ($inventoryBody.Contains('inventory.backedTargetCount')) 'the inventory builder should track the backed target count'
Assert-True ($inventoryBody.Contains('output.active = true;')) 'the inventory builder should mark operational outputs active'

Assert-True ($displayModel.Contains('preferredBounds() const')) 'display_model.h should keep preferred geometry separate from assigned geometry'
Assert-True ($displayModel.Contains('assignedBounds() const')) 'display_model.h should keep assigned geometry separate from preferred geometry'
Assert-True ($displayModel.Contains('backedByOutputResource')) 'display_model.h should distinguish output-backed render targets from hosted framebuffer targets'
Assert-True ($displayModel.Contains('backedByOutputResource || backedByHostedFramebuffer')) 'display_model.h should treat output-backed and hosted-backed targets as backed inventory entries'
Assert-True ($displayModel.Contains('makeDisplayRenderTarget(')) 'display_model.h should still build render targets'
Assert-True ($displayModel.Contains('makeHostedFallbackRenderTarget(')) 'display_model.h should still build the hosted fallback target'
Assert-True ($displayModel.Contains('activeDisplayRenderTarget(')) 'display_model.h should still resolve the active render target'
Assert-True ($displayModel.Contains('displayRenderTargetsSummary(')) 'display_model.h should still summarize render targets'

$buildBody = Get-FunctionBody -Text $virtioGpuCpp -SignaturePattern 'static VirtioGpuOutputInventory build_output_inventory\('
Assert-True ($null -ne $buildBody) 'build_output_inventory should be parsable'
Assert-True ($buildBody.Contains('make_scanout_state(0u')) 'build_output_inventory should build scanout 0 explicitly'
Assert-True ($buildBody.Contains('make_scanout_state(1u')) 'build_output_inventory should build scanout 1 explicitly'
Assert-True ($buildBody.Contains('scanout1Initial')) 'build_output_inventory should keep scanout 1 preferred geometry separate'
Assert-True ($buildBody.Contains('selectedWidth')) 'build_output_inventory should use the assigned width for operational geometry'
Assert-True ($buildBody.Contains('selectedHeight')) 'build_output_inventory should use the assigned height for operational geometry'
Assert-True ($buildBody.Contains('VirtioGpuDisplayBackend::getVirtioGpuOutputInventory')) 'build_output_inventory should delegate to the backend inventory builder'

$initializeBody = Get-FunctionBody -Text $virtioGpuCpp -SignaturePattern 'static bool initialize_device\(DeviceState& state\)'
Assert-True ($null -ne $initializeBody) 'initialize_device should be parsable'
Assert-True ($initializeBody.Contains('updateOutputInventory();')) 'initialize_device should publish the output inventory after the render milestones'
Assert-True ($initializeBody.Contains('scanout1PresentationConfirmed')) 'initialize_device should track scanout 1 presentation separately'
Assert-True ($initializeBody.Contains('distinctPatternsConfirmed')) 'initialize_device should track distinct scanout patterns separately'
Assert-True ($initializeBody.Contains('Stage B scanout capacity deviceConfigNumScanouts=')) 'initialize_device should log Stage B scanout capacity'
Assert-True ($initializeBody.Contains('Diagnostic scanout 1 initial enabled=')) 'initialize_device should log the initial scanout 1 state'
Assert-True ($initializeBody.Contains('submit_display_info_request(state, "pre-render", true)')) 'initialize_device should issue the pre-render GET_DISPLAY_INFO request'
Assert-True ($initializeBody.Contains('submit_display_info_request(state, "post-render", false)')) 'initialize_device should issue the post-render GET_DISPLAY_INFO request'
Assert-True ($initializeBody.Contains('transport.mmioStopReason = "dual-output scanout 1 test pattern milestone complete";')) 'initialize_device should keep the dual-output completion marker'
Assert-True ($initializeBody.Contains('resource2.flushOk && resource2.patternChecksum != 0u')) 'initialize_device should gate scanout 1 presentation confirmation on the flushed secondary resource'
Assert-True ($initializeBody.Contains('cleanup_diagnostic_resource_if_safe(')) 'initialize_device should keep bounded cleanup'
Assert-True (-not $initializeBody.Contains('cursorq')) 'initialize_device must not configure the cursor queue'
Assert-True (-not $initializeBody.Contains('CMD_UPDATE_CURSOR')) 'initialize_device must not issue cursor update commands'
Assert-True (-not $initializeBody.Contains('CMD_MOVE_CURSOR')) 'initialize_device must not issue cursor move commands'
Assert-True (-not $initializeBody.Contains('FEATURE_VIRGL')) 'initialize_device must not enable virgl'
Assert-True (-not $initializeBody.Contains('RESOURCE_BLOB')) 'initialize_device must not use blob resources'
Assert-True (-not $initializeBody.Contains('CONTEXT_INIT')) 'initialize_device must not create 3D contexts'
Assert-True (-not $initializeBody.Contains('render loop')) 'initialize_device must not introduce a continuous render loop'

$setScanoutBody = Get-FunctionBody -Text $virtioGpuCpp -SignaturePattern 'static bool issue_set_scanout\(DeviceState& state,'
Assert-True ($null -ne $setScanoutBody) 'issue_set_scanout should be parsable'
Assert-True ($setScanoutBody.Contains('scanoutId > 1u')) 'issue_set_scanout should still reject scanout ids above 1'
Assert-True ($setScanoutBody.Contains('scanout id is not permitted')) 'issue_set_scanout should keep the explicit scanout-id blocker'

$printBody = Get-FunctionBody -Text $virtioGpuCpp -SignaturePattern 'static void print_probe_outcome\(\)'
Assert-True ($null -ne $printBody) 'print_probe_outcome should be parsable'
Assert-True ($printBody.Contains('virtioGpuOutputInventorySummary')) 'print_probe_outcome should emit the virtio-gpu output inventory summary'
Assert-True ($printBody.Contains('virtioGpuOutputSummaryLine')) 'print_probe_outcome should print per-output inventory lines'
Assert-True ($printBody.Contains('virtioGpuMonitorSummaryLine')) 'print_probe_outcome should print per-monitor inventory lines'
Assert-True ($printBody.Contains('virtioGpuRenderTargetSummaryLine')) 'print_probe_outcome should print per-target inventory lines'
Assert-True ($printBody.Contains('qemuTwoUsableScanouts=')) 'print_probe_outcome should keep the connector-state verdict separate from operational readiness'
Assert-True ($printBody.Contains('contentMode=')) 'print_probe_outcome should report the compositor content mode'
Assert-True ($printBody.Contains('frameMode=')) 'print_probe_outcome should report the frame mode'
Assert-True ($printBody.Contains('continuousPresentation=')) 'print_probe_outcome should report the continuous-presentation state'
Assert-True ($printBody.Contains('dual-output-test-pattern')) 'print_probe_outcome should name the dual-output rendering mode'

Assert-True ($compositorCpp.Contains('hostedRenderTargetsForDesktop')) 'compositor.cpp should still build hosted render targets'
Assert-True ($compositorCpp.Contains('renderToFramebuffer(const DisplayRenderTarget& renderTarget)')) 'compositor.cpp should still render a target to the framebuffer'
Assert-True (-not $compositorCpp.Contains('VirtioGpuDisplayBackend')) 'compositor.cpp should not integrate the virtio-gpu backend yet'
Assert-True (-not $compositorCpp.Contains('presentVirtioGpuTarget')) 'compositor.cpp should not call the present helper yet'
Assert-True (-not $compositorCpp.Contains('updateVirtioGpuTarget')) 'compositor.cpp should not call the update helper yet'

Assert-True ($probeSmoke.Contains('gpuOutputInventoryLine')) 'smoke-qemu-display-probe.ps1 should capture the virtio-gpu output inventory summary'
Assert-True ($probeSmoke.Contains('gpuOutputConfiguredCount')) 'smoke-qemu-display-probe.ps1 should capture the configured output count'
Assert-True ($probeSmoke.Contains('gpuOutputOperationalCount')) 'smoke-qemu-display-probe.ps1 should capture the operational output count'
Assert-True ($probeSmoke.Contains('gpuOutputPresentationConfirmedCount')) 'smoke-qemu-display-probe.ps1 should capture the presentation-confirmed output count'
Assert-True ($probeSmoke.Contains('gpuOutput0Line')) 'smoke-qemu-display-probe.ps1 should capture the first output descriptor'
Assert-True ($probeSmoke.Contains('gpuOutput1Line')) 'smoke-qemu-display-probe.ps1 should capture the second output descriptor'
Assert-True ($probeSmoke.Contains('gpuMonitor0Line')) 'smoke-qemu-display-probe.ps1 should capture the first DisplayMonitor descriptor'
Assert-True ($probeSmoke.Contains('gpuMonitor1Line')) 'smoke-qemu-display-probe.ps1 should capture the second DisplayMonitor descriptor'
Assert-True ($probeSmoke.Contains('gpuTarget0Line')) 'smoke-qemu-display-probe.ps1 should capture the first DisplayRenderTarget descriptor'
Assert-True ($probeSmoke.Contains('gpuTarget1Line')) 'smoke-qemu-display-probe.ps1 should capture the second DisplayRenderTarget descriptor'
Assert-True ($probeSmoke.Contains('gpuOutputProtocolConnectorEnabledCount')) 'smoke-qemu-display-probe.ps1 should report the protocol connector-enabled count separately'
Assert-True ($probeSmoke.Contains('gpuOutputOperationalOutputCount')) 'smoke-qemu-display-probe.ps1 should report the operational output count separately'
Assert-True ($probeSmoke.Contains('gpuOutputPresentationConfirmedCountDetailed')) 'smoke-qemu-display-probe.ps1 should report the presentation-confirmed output count separately'
Assert-True ($probeSmoke.Contains('post-render scanout 1 connector disabled')) 'smoke-qemu-display-probe.ps1 should treat scanout 1 connector state as diagnostic-only'
Assert-True ($probeSmoke.Contains('qemuTwoUsableScanouts=no')) 'smoke-qemu-display-probe.ps1 should keep the connector-state diagnostic separate from operational readiness'
Assert-True (-not $probeSmoke.Contains('enabledScanoutsAfter=2')) 'smoke-qemu-display-probe.ps1 should not require two enabled scanouts for success'

Assert-True ($virtioGpuCpp.Contains('GXOS_QEMU_VIRTIO_GPU_PROBE_ACTIVE')) 'virtio_gpu.cpp should gate diagnostics behind GXOS_QEMU_VIRTIO_GPU_PROBE_ACTIVE'
Assert-True ($virtioGpuCpp.Contains('GXOS_QEMU_VIRTIO_GPU_SCANOUT1_ACTIVE')) 'virtio_gpu.cpp should keep the Stage B path behind a separate gate'
Assert-True ($virtioGpuCpp.Contains('No physical Intel GPU support')) 'virtio_gpu.cpp should keep physical Intel GPU support out of the QEMU-only track'
Assert-True ($virtioGpuCpp.Contains('No real hardware GPU BAR access')) 'virtio_gpu.cpp should keep real hardware GPU BAR access out of the QEMU-only track'
Assert-True ($virtioGpuCpp.Contains('No display hotplug')) 'virtio_gpu.cpp should keep display hotplug out of the QEMU-only track'
Assert-True ($virtioGpuCpp.Contains('QEMU-only compositor desktop rendering is single-shot unless the explicit')) 'virtio_gpu.cpp should keep the default compositor proof conservative'
Assert-True ($virtioGpuCpp.Contains('No 3D, virgl, Venus, blob, or unrestricted production compositor integration')) 'virtio_gpu.cpp should keep compositor integration bounded and non-3D'
Assert-True ($virtioGpuCpp.Contains('No unbounded busy rendering loops or unlimited queue polling')) 'virtio_gpu.cpp should keep live rendering bounded'
Assert-True ($virtioGpuCpp.Contains('REAL HARDWARE GPU/MMIO ENABLEMENT IS MULE TERRITORY')) 'virtio_gpu.cpp should keep the real-hardware warning prominent'

Write-Host 'virtio-gpu output backend source smoke passed.'
