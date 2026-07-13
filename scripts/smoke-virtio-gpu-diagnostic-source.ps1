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

function Assert-Regex {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Text,
        [Parameter(Mandatory = $true)]
        [string]$Pattern,
        [Parameter(Mandatory = $true)]
        [string]$Message
    )

    Assert-True ([regex]::IsMatch($Text, $Pattern, [System.Text.RegularExpressions.RegexOptions]::Singleline)) $Message
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

$mmioHeader = Read-Text -Path (Join-Path $Root 'kernel\core\include\kernel\mmio.h')
$mmioCpp = Read-Text -Path (Join-Path $Root 'kernel\core\mmio.cpp')
$virtioGpuCpp = Read-Text -Path (Join-Path $Root 'kernel\core\virtio_gpu.cpp')
$backendHeader = Read-Text -Path (Join-Path $Root 'virtio_gpu_display_backend.h')
$displayModel = Read-Text -Path (Join-Path $Root 'display_model.h')
$compositorCpp = Read-Text -Path (Join-Path $Root 'compositor.cpp')
$probeSmoke = Read-Text -Path (Join-Path $Root 'scripts\smoke-qemu-display-probe.ps1')
$launcher = Read-Text -Path (Join-Path $Root 'scripts\run-qemu-display-probe.bat')

Assert-True ($mmioHeader.Contains('SAFE_DIRECT_MAP_CEILING')) 'mmio.h should keep the legacy direct-map ceiling marker'
Assert-True ($mmioHeader.Contains('MMIO_WINDOW_BASE')) 'mmio.h should define the reserved MMIO window base'
Assert-True ($mmioHeader.Contains('MMIO_WINDOW_SIZE')) 'mmio.h should define the reserved MMIO window size'
Assert-True ($mmioHeader.Contains('MMIO_WINDOW_LIMIT')) 'mmio.h should define the reserved MMIO window limit'
Assert-True ($mmioHeader.Contains('MMIO_WINDOW_PAGE_COUNT')) 'mmio.h should define the reserved MMIO window page count'
Assert-True ($mmioHeader.Contains('MappingReport')) 'mmio.h should expose a mapping report structure'
Assert-True ($mmioHeader.Contains('MAP_FLAG_NON_USER')) 'mmio.h should carry a non-user page flag'
Assert-True ($mmioHeader.Contains('MAP_FLAG_NO_EXEC')) 'mmio.h should carry a no-execute page flag'
Assert-True ($mmioHeader.Contains('MAP_FLAG_UNCACHED')) 'mmio.h should carry an uncached page flag'
Assert-True ($mmioHeader.Contains('MMIO mappings must be kernel-only, no-executable, and uncached')) 'mmio.h should enforce kernel-only, no-exec, uncached MMIO mappings'
Assert-True ($mmioHeader.Contains('Reserved kernel MMIO window')) 'mmio.h should document the reserved MMIO window'
Assert-True ($mmioHeader.Contains('bounded bump allocator')) 'mmio.h should document the allocation strategy'

Assert-True ($mmioCpp.Contains('GXOS_QEMU_VIRTIO_GPU_PROBE_ACTIVE')) 'mmio.cpp should gate the active path behind the QEMU probe flag'
Assert-True ($mmioCpp.Contains('runtime MMIO mapping is gated to the x86_64 QEMU probe build')) 'mmio.cpp should keep the fallback gate explicit'
Assert-True ($mmioCpp.Contains('PTE_PCD')) 'mmio.cpp should use PCD for MMIO'
Assert-True ($mmioCpp.Contains('PTE_PWT')) 'mmio.cpp should use PWT for MMIO'
Assert-True ($mmioCpp.Contains('PTE_NX')) 'mmio.cpp should mark MMIO non-executable'
Assert-True ($mmioCpp.Contains('mapped into reserved kernel MMIO window')) 'mmio.cpp should report successful mappings'
Assert-True ($mmioCpp.Contains('reserved MMIO window exhausted')) 'mmio.cpp should stop on exhaustion'

Assert-True ($virtioGpuCpp.Contains('GXOS_QEMU_VIRTIO_GPU_PROBE_ACTIVE')) 'virtio_gpu.cpp should gate diagnostics behind GXOS_QEMU_VIRTIO_GPU_PROBE_ACTIVE'
Assert-True ($virtioGpuCpp.Contains('GXOS_QEMU_VIRTIO_GPU_SCANOUT1_ACTIVE')) 'virtio_gpu.cpp should keep the Stage B path behind a separate gate'
Assert-True ($virtioGpuCpp.Contains('GXOS_QEMU_VIRTIO_GPU_COMPOSITOR_FRAME_ACTIVE')) 'virtio_gpu.cpp should keep the compositor-frame path behind a separate QEMU-only gate'
Assert-True ($virtioGpuCpp.Contains('No physical Intel GPU support')) 'virtio_gpu.cpp should keep physical Intel GPU support out of the QEMU-only track'
Assert-True ($virtioGpuCpp.Contains('No real hardware GPU BAR access')) 'virtio_gpu.cpp should keep real hardware GPU BAR access out of the QEMU-only track'
Assert-True ($virtioGpuCpp.Contains('No display hotplug')) 'virtio_gpu.cpp should keep display hotplug out of the QEMU-only track'
Assert-True ($virtioGpuCpp.Contains('QEMU-only compositor desktop rendering is single-shot unless the explicit')) 'virtio_gpu.cpp should keep the default compositor proof conservative'
Assert-True ($virtioGpuCpp.Contains('No 3D, virgl, Venus, blob, or unrestricted production compositor integration')) 'virtio_gpu.cpp should keep compositor integration bounded and non-3D'
Assert-True ($virtioGpuCpp.Contains('No unbounded busy rendering loops or unlimited queue polling')) 'virtio_gpu.cpp should keep live rendering bounded'
Assert-True ($virtioGpuCpp.Contains('GXOS_QEMU_VIRTIO_GPU_COMPOSITOR_LIVE')) 'virtio_gpu.cpp should expose the explicit live QEMU gate'
Assert-True ($virtioGpuCpp.Contains('redraw_generation')) 'virtio_gpu.cpp should consume compositor dirty-generation state'
Assert-True ($virtioGpuCpp.Contains('presentation_tick')) 'virtio_gpu.cpp should expose the scheduler-owned presentation step'
Assert-True ($virtioGpuCpp.Contains('REAL HARDWARE GPU/MMIO ENABLEMENT IS MULE TERRITORY')) 'virtio_gpu.cpp should keep the real-hardware warning prominent'
Assert-True ($backendHeader.Contains('struct VirtioGpuScanoutState')) 'virtio_gpu_display_backend.h should define the scanout state record'
Assert-True ($backendHeader.Contains('struct VirtioGpuOutputInventory')) 'virtio_gpu_display_backend.h should define the output inventory record'
Assert-True ($backendHeader.Contains('static bool isQemuOnly()')) 'virtio_gpu_display_backend.h should keep the QEMU-only gate helper'
Assert-True ($backendHeader.Contains('getVirtioGpuOutputInventory')) 'virtio_gpu_display_backend.h should build a virtio-gpu output inventory'
Assert-True ($backendHeader.Contains('buildVirtioGpuDisplayMonitors')) 'virtio_gpu_display_backend.h should bridge output inventory into DisplayMonitor descriptors'
Assert-True ($backendHeader.Contains('buildVirtioGpuDisplayTargets')) 'virtio_gpu_display_backend.h should bridge output inventory into DisplayRenderTarget descriptors'
Assert-True ($backendHeader.Contains('presentVirtioGpuTarget')) 'virtio_gpu_display_backend.h should keep the present helper for future compositor integration'
Assert-True ($backendHeader.Contains('updateVirtioGpuTarget')) 'virtio_gpu_display_backend.h should keep the update helper for future compositor integration'
Assert-True ($backendHeader.Contains('backedByOutputResource')) 'display_model.h should distinguish output-backed render targets from hosted framebuffer targets'
Assert-True ($backendHeader.Contains('presentationConfirmed')) 'virtio_gpu_display_backend.h should track presentation confirmation separately from connector state'
Assert-True ($virtioGpuCpp.Contains('s_diagnosticBackingStorage0')) 'virtio_gpu.cpp should keep the first diagnostic backing store'
Assert-True ($virtioGpuCpp.Contains('s_diagnosticBackingStorage1')) 'virtio_gpu.cpp should keep the second diagnostic backing store'
Assert-True ($virtioGpuCpp.Contains('kDiagnosticResourceIdSecondary')) 'virtio_gpu.cpp should reserve a second diagnostic resource id'
Assert-True ($virtioGpuCpp.Contains('build_diagnostic_backing_layout(')) 'virtio_gpu.cpp should validate the backing layout'
Assert-True ($virtioGpuCpp.Contains('log_diagnostic_backing_layout(')) 'virtio_gpu.cpp should log backing coverage'
Assert-True ($virtioGpuCpp.Contains('checksum_diagnostic_pattern(')) 'virtio_gpu.cpp should emit a deterministic checksum'
Assert-True ($virtioGpuCpp.Contains('issue_resource_create_2d(')) 'virtio_gpu.cpp should create 2D resources'
Assert-True ($virtioGpuCpp.Contains('issue_resource_attach_backing(')) 'virtio_gpu.cpp should attach backing memory'
Assert-True ($virtioGpuCpp.Contains('issue_set_scanout(')) 'virtio_gpu.cpp should assign scanouts'
Assert-True ($virtioGpuCpp.Contains('issue_transfer_to_host_2d(')) 'virtio_gpu.cpp should transfer the pattern to host memory'
Assert-True ($virtioGpuCpp.Contains('issue_resource_flush(')) 'virtio_gpu.cpp should flush the resource'
Assert-True ($virtioGpuCpp.Contains('Single-output proof:')) 'virtio_gpu.cpp should keep the Stage A proof line'
Assert-True ($virtioGpuCpp.Contains('Dual-output proof:')) 'virtio_gpu.cpp should keep the Stage B proof line'
Assert-True ($virtioGpuCpp.Contains('Stage B scanout capacity deviceConfigNumScanouts=')) 'virtio_gpu.cpp should log scanout capacity before Stage B'
Assert-True ($virtioGpuCpp.Contains('Diagnostic scanout 1 initial enabled=')) 'virtio_gpu.cpp should log the initial scanout 1 state'

$backingBody = Get-FunctionBody -Text $virtioGpuCpp -SignaturePattern 'static bool build_diagnostic_backing_layout\(uint8_t\* backingBase,'
Assert-True ($null -ne $backingBody) 'build_diagnostic_backing_layout should be parsable'
Assert-True ($backingBody.Contains('dma_address(pageVirtual)')) 'build_diagnostic_backing_layout should resolve every page through dma_address'
Assert-True ($backingBody.Contains('contiguousRunCount')) 'build_diagnostic_backing_layout should track contiguous physical runs'
Assert-True ($backingBody.Contains('physicalCoverageValid')) 'build_diagnostic_backing_layout should validate physical coverage'
Assert-True ($backingBody.Contains('entriesOut[entryCount].addr = runStartPhysical;')) 'build_diagnostic_backing_layout should emit one entry per verified run'

$attachBody = Get-FunctionBody -Text $virtioGpuCpp -SignaturePattern 'static bool issue_resource_attach_backing\(DeviceState& state,'
Assert-True ($null -ne $attachBody) 'issue_resource_attach_backing should be parsable'
Assert-True ($attachBody.Contains('backingVirtualBase=0x')) 'issue_resource_attach_backing should log the virtual base'
Assert-True ($attachBody.Contains('totalMemEntries=')) 'issue_resource_attach_backing should log the mem-entry count'
Assert-True ($attachBody.Contains('contiguousRunCount=')) 'issue_resource_attach_backing should log the run count'
Assert-True ($attachBody.Contains('physicalCoverageValid=')) 'issue_resource_attach_backing should report coverage validity'

$setScanoutBody = Get-FunctionBody -Text $virtioGpuCpp -SignaturePattern 'static bool issue_set_scanout\(DeviceState& state,'
Assert-True ($null -ne $setScanoutBody) 'issue_set_scanout should be parsable'
Assert-True ($setScanoutBody.Contains('scanoutId > 1u')) 'issue_set_scanout should reject scanout ids above 1'
Assert-True ($setScanoutBody.Contains('scanout id is not permitted')) 'issue_set_scanout should keep the explicit scanout-id blocker'

$initializeBody = Get-FunctionBody -Text $virtioGpuCpp -SignaturePattern 'static bool initialize_device\(DeviceState& state\)'
Assert-True ($null -ne $initializeBody) 'initialize_device should be parsable'
Assert-True ($initializeBody.Contains('submit_display_info_request(state, "pre-render", true)')) 'initialize_device should issue the pre-render GET_DISPLAY_INFO request'
Assert-True ($initializeBody.Contains('submit_display_info_request(state, "post-render", false)')) 'initialize_device should issue the post-render GET_DISPLAY_INFO request'
Assert-True ($initializeBody.Contains('deviceConfigNumScanouts reported fewer than two scanouts')) 'initialize_device should stop if the device reports fewer than two scanouts'
Assert-True ($initializeBody.Contains('Stage B scanout capacity deviceConfigNumScanouts=')) 'initialize_device should log Stage B scanout capacity'
Assert-True ($initializeBody.Contains('Diagnostic scanout 1 initial enabled=')) 'initialize_device should log the initial scanout 1 state'
Assert-True ($initializeBody.Contains('resource2dReadySecondary')) 'initialize_device should track the second resource state'
Assert-True ($initializeBody.Contains('backingAttachedSecondary')) 'initialize_device should track the second backing state'
Assert-True ($initializeBody.Contains('scanout1Set')) 'initialize_device should track scanout 1 assignment'
Assert-True ($initializeBody.Contains('transfer1Ok')) 'initialize_device should track the second transfer'
Assert-True ($initializeBody.Contains('flush1Ok')) 'initialize_device should track the second flush'
Assert-True ($initializeBody.Contains('distinctPatternsConfirmed')) 'initialize_device should track the dual-pattern verdict'
Assert-True ($initializeBody.Contains('cleanup_diagnostic_resource_if_safe(')) 'initialize_device should support bounded cleanup'
Assert-True (-not $initializeBody.Contains('cursorq')) 'initialize_device must not configure the cursor queue'
Assert-True (-not $initializeBody.Contains('CMD_UPDATE_CURSOR')) 'initialize_device must not issue cursor update commands'
Assert-True (-not $initializeBody.Contains('CMD_MOVE_CURSOR')) 'initialize_device must not issue cursor move commands'
Assert-True (-not $initializeBody.Contains('FEATURE_VIRGL')) 'initialize_device must not enable virgl'
Assert-True (-not $initializeBody.Contains('RESOURCE_BLOB')) 'initialize_device must not use blob resources'
Assert-True (-not $initializeBody.Contains('CONTEXT_INIT')) 'initialize_device must not create 3D contexts'

$printBody = Get-FunctionBody -Text $virtioGpuCpp -SignaturePattern 'static void print_probe_outcome\(\)'
Assert-True ($null -ne $printBody) 'print_probe_outcome should be parsable'
Assert-True ($printBody.Contains('resource2dSecondary=')) 'print_probe_outcome should report the secondary resource'
Assert-True ($printBody.Contains('backingSecondary=')) 'print_probe_outcome should report the secondary backing'
Assert-True ($printBody.Contains('scanout1=')) 'print_probe_outcome should report the secondary scanout'
Assert-True ($printBody.Contains('transfer1=')) 'print_probe_outcome should report the secondary transfer'
Assert-True ($printBody.Contains('flush1=')) 'print_probe_outcome should report the secondary flush'
Assert-True ($printBody.Contains('distinctPatterns=')) 'print_probe_outcome should report the distinct-pattern verdict'
Assert-True ($printBody.Contains('qemuTwoUsableScanouts=')) 'print_probe_outcome should report the two-scanout verdict'
Assert-True ($printBody.Contains('contentMode=')) 'print_probe_outcome should report the compositor content mode'
Assert-True ($printBody.Contains('frameMode=')) 'print_probe_outcome should report the frame mode'
Assert-True ($printBody.Contains('continuousPresentation=')) 'print_probe_outcome should report the continuous-presentation state'
Assert-True ($printBody.Contains('dual-output-test-pattern')) 'print_probe_outcome should name the dual-output rendering mode'

Assert-True ($launcher.Contains('id=gpu0')) 'run-qemu-display-probe.bat should assign a stable gpu0 device id'
Assert-True ($launcher.Contains('GXOS_QEMU_DISPLAY_PROBE_QMP_PORT')) 'run-qemu-display-probe.bat should expose a QMP port hook'
Assert-True ($launcher.Contains('GXOS_QEMU_DISPLAY_PROBE_CAPTURE')) 'run-qemu-display-probe.bat should expose capture mode'
Assert-True ($launcher.Contains('QEMU_QMP_ARGS')) 'run-qemu-display-probe.bat should forward QMP arguments to QEMU'

Assert-True ($probeSmoke.Contains('ProbeStage')) 'smoke-qemu-display-probe.ps1 should track probe stages'
Assert-True ($probeSmoke.Contains('stageA')) 'smoke-qemu-display-probe.ps1 should include the Stage A pass'
Assert-True ($probeSmoke.Contains('stageB')) 'smoke-qemu-display-probe.ps1 should include the Stage B pass'
Assert-True ($probeSmoke.Contains('visualCaptureStatus')) 'smoke-qemu-display-probe.ps1 should record visual capture status'
Assert-True ($probeSmoke.Contains('visualScanout0=')) 'smoke-qemu-display-probe.ps1 should record scanout 0 capture state'
Assert-True ($probeSmoke.Contains('visualScanout1=')) 'smoke-qemu-display-probe.ps1 should record scanout 1 capture state'
Assert-True ($probeSmoke.Contains('visualCaptureReason')) 'smoke-qemu-display-probe.ps1 should record the capture reason'
Assert-True ($probeSmoke.Contains('distinctPatternsConfirmed')) 'smoke-qemu-display-probe.ps1 should record the distinct-pattern verdict'
Assert-True ($probeSmoke.Contains('dualOutputVisualProof')) 'smoke-qemu-display-probe.ps1 should record the dual-output verdict'
Assert-True ($probeSmoke.Contains('stageBBlockerReason')) 'smoke-qemu-display-probe.ps1 should preserve the Stage B blocker reason'
Assert-True ($probeSmoke.Contains('GpuBackingPhysicalCoverageValid')) 'smoke-qemu-display-probe.ps1 should capture backing coverage validity'
Assert-True ($probeSmoke.Contains('GpuBackingMemEntryCount')) 'smoke-qemu-display-probe.ps1 should capture the backing mem-entry count'
Assert-True ($probeSmoke.Contains('GpuResourceCreateSecondaryLine')) 'smoke-qemu-display-probe.ps1 should capture the second resource creation line'
Assert-True ($probeSmoke.Contains('GpuAttachSecondaryLine')) 'smoke-qemu-display-probe.ps1 should capture the second backing attachment line'
Assert-True ($probeSmoke.Contains('GpuSetScanout1Line')) 'smoke-qemu-display-probe.ps1 should capture the scanout 1 assignment line'
Assert-True ($probeSmoke.Contains('GpuTransfer1Line')) 'smoke-qemu-display-probe.ps1 should capture the scanout 1 transfer line'
Assert-True ($probeSmoke.Contains('GpuFlush1Line')) 'smoke-qemu-display-probe.ps1 should capture the scanout 1 flush line'
Assert-True ($probeSmoke.Contains('GpuPreRenderEnabledScanouts')) 'smoke-qemu-display-probe.ps1 should capture pre-render enabled scanout count'
Assert-True ($probeSmoke.Contains('GpuPostRenderEnabledScanouts')) 'smoke-qemu-display-probe.ps1 should capture post-render enabled scanout count'
Assert-True ($probeSmoke.Contains('GpuProbeCompleteEnabledScanoutsAfter')) 'smoke-qemu-display-probe.ps1 should capture the final enabled-scanout count'
Assert-True ($probeSmoke.Contains('GpuProbeCompleteDistinctPatterns')) 'smoke-qemu-display-probe.ps1 should capture the distinct-pattern verdict'
Assert-True ($probeSmoke.Contains('GpuProbeCompleteQemuTwoUsableScanouts')) 'smoke-qemu-display-probe.ps1 should capture the two-usable-scanouts verdict'
Assert-True ($probeSmoke.Contains('GpuProbeCompleteEnabledScanoutsBefore')) 'smoke-qemu-display-probe.ps1 should capture the pre-flush enabled-scanout count'
Assert-True ($probeSmoke.Contains('GpuProbeCompleteDisabledScanoutsBefore')) 'smoke-qemu-display-probe.ps1 should capture the pre-flush disabled-scanout count'
Assert-True ($probeSmoke.Contains('GpuProbeCompleteDeviceConfigNumScanouts')) 'smoke-qemu-display-probe.ps1 should capture the device scanout capacity'
Assert-True ($probeSmoke.Contains('GpuPrimaryPatternChecksum = $gpuPrimaryPatternChecksum')) 'smoke-qemu-display-probe.ps1 should capture the primary checksum value'
Assert-True ($probeSmoke.Contains('GpuSecondaryPatternChecksum = $gpuSecondaryPatternChecksum')) 'smoke-qemu-display-probe.ps1 should capture the secondary checksum value'
Assert-True ($probeSmoke.Contains('GpuSingleOutputProofLine')) 'smoke-qemu-display-probe.ps1 should capture the single-output proof line'
Assert-True ($probeSmoke.Contains('GpuDualOutputProofLine')) 'smoke-qemu-display-probe.ps1 should capture the dual-output proof line'
Assert-True ($probeSmoke.Contains('gpuPrimaryPatternChecksumLine')) 'smoke-qemu-display-probe.ps1 should capture the primary checksum line'
Assert-True ($probeSmoke.Contains('gpuSecondaryPatternChecksumLine')) 'smoke-qemu-display-probe.ps1 should capture the secondary checksum line'
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
Assert-True ($probeSmoke.Contains('post-render scanout 1 connector disabled')) 'smoke-qemu-display-probe.ps1 should treat scanout 1 connector state as diagnostic-only'
Assert-True ($probeSmoke.Contains('qemuTwoUsableScanouts=no')) 'smoke-qemu-display-probe.ps1 should keep the connector-state diagnostic separate from operational readiness'
Assert-True (-not $probeSmoke.Contains('enabledScanoutsAfter=2')) 'smoke-qemu-display-probe.ps1 should not require two enabled scanouts for success'
Assert-True ($probeSmoke.Contains('gpuResourceCreateMatches')) 'smoke-qemu-display-probe.ps1 should count resource-creation lines'
Assert-True ($probeSmoke.Contains('gpuAttachMatches')) 'smoke-qemu-display-probe.ps1 should count attach-backing lines'
Assert-True ($probeSmoke.Contains('gpuStageBCapacityLine')) 'smoke-qemu-display-probe.ps1 should capture Stage B capacity evidence'
Assert-True ($probeSmoke.Contains('gpuStageBInitialScanoutLine')) 'smoke-qemu-display-probe.ps1 should capture Stage B scanout state evidence'
Assert-True ($probeSmoke.Contains('gpuDualOutputProofLine')) 'smoke-qemu-display-probe.ps1 should capture the dual-output proof line'

Assert-True ($displayModel.Contains('struct DisplayMonitorDescriptor')) 'display_model.h should define the monitor descriptor'
Assert-True ($displayModel.Contains('struct DisplayViewport')) 'display_model.h should define the viewport type'
Assert-True ($displayModel.Contains('struct DisplayRenderTarget')) 'display_model.h should define the render target type'
Assert-True ($compositorCpp.Contains('hostedRenderTargetsForDesktop')) 'compositor.cpp should still build hosted render targets'
Assert-True ($compositorCpp.Contains('renderToFramebuffer(const DisplayRenderTarget& renderTarget)')) 'compositor.cpp should still render a target to the framebuffer'

Write-Host 'virtio-gpu diagnostic source smoke passed.'
