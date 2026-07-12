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
$mainCpp = Read-Text -Path (Join-Path $Root 'kernel\core\main.cpp')
$probeSmoke = Read-Text -Path (Join-Path $Root 'scripts\smoke-qemu-display-probe.ps1')
$launcher = Read-Text -Path (Join-Path $Root 'scripts\run-qemu-display-probe.bat')

Assert-True ($mmioHeader.Contains('SAFE_DIRECT_MAP_CEILING')) 'mmio.h should keep the legacy direct-map ceiling marker'
Assert-True ($mmioHeader.Contains('MMIO_WINDOW_BASE')) 'mmio.h should define the reserved MMIO window base'
Assert-True ($mmioHeader.Contains('MMIO_WINDOW_SIZE')) 'mmio.h should define the reserved MMIO window size'
Assert-True ($mmioHeader.Contains('MMIO_WINDOW_LIMIT')) 'mmio.h should define the reserved MMIO window limit'
Assert-True ($mmioHeader.Contains('MMIO_WINDOW_PAGE_COUNT')) 'mmio.h should define the reserved MMIO window page count'
Assert-True ($mmioHeader.Contains('MappingReport')) 'mmio.h should expose a mapping report structure'
Assert-True ($mmioHeader.Contains('pageCount')) 'mmio.h should expose a page-count field'
Assert-True ($mmioHeader.Contains('cacheMode')) 'mmio.h should expose a cache-mode field'
Assert-True ($mmioHeader.Contains('canMap')) 'mmio.h should expose a feasibility check helper'
Assert-True ($mmioHeader.Contains('mapForDevice')) 'mmio.h should declare the device-mapping helper'
Assert-True ($mmioHeader.Contains('unmap')) 'mmio.h should declare the unmap helper'
Assert-True ($mmioHeader.Contains('MAP_FLAG_NON_USER')) 'mmio.h should carry a non-user page flag'
Assert-True ($mmioHeader.Contains('MAP_FLAG_NO_EXEC')) 'mmio.h should carry a no-execute page flag'
Assert-True ($mmioHeader.Contains('MAP_FLAG_UNCACHED')) 'mmio.h should carry an uncached page flag'
Assert-True ($mmioHeader.Contains('MMIO mappings must be kernel-only, no-executable, and uncached')) 'mmio.h should enforce kernel-only, no-exec, uncached MMIO mappings'
Assert-True ($mmioHeader.Contains('Reserved kernel MMIO window')) 'mmio.h should document the reserved MMIO window'
Assert-True ($mmioHeader.Contains('bounded bump allocator')) 'mmio.h should document the allocation strategy'
Assert-True ($mmioHeader.Contains('uc(pcd+pwt)')) 'mmio.h should name the UC cache mode'

Assert-True ($mmioCpp.Contains('GXOS_QEMU_VIRTIO_GPU_PROBE_ACTIVE')) 'mmio.cpp should gate the active path behind the QEMU probe flag'
Assert-True ($mmioCpp.Contains('runtime MMIO mapping is gated to the x86_64 QEMU probe build')) 'mmio.cpp should keep the fallback gate explicit'
Assert-True ($mmioCpp.Contains('PTE_PCD')) 'mmio.cpp should use PCD for MMIO'
Assert-True ($mmioCpp.Contains('PTE_PWT')) 'mmio.cpp should use PWT for MMIO'
Assert-True ($mmioCpp.Contains('PTE_NX')) 'mmio.cpp should mark MMIO non-executable'
Assert-True ($mmioCpp.Contains('mapped into reserved kernel MMIO window')) 'mmio.cpp should report successful mappings'
Assert-True ($mmioCpp.Contains('reused reserved kernel MMIO page')) 'mmio.cpp should deduplicate repeated mappings'
Assert-True ($mmioCpp.Contains('reserved MMIO window exhausted')) 'mmio.cpp should stop on exhaustion'
Assert-True ($mmioCpp.Contains('kernel::arch::invalidate_tlb_entry')) 'mmio.cpp should invalidate TLB entries'
Assert-True ($mmioCpp.Contains('MMIO unmap is gated to the x86_64 QEMU probe build')) 'mmio.cpp should keep the unmap gate explicit'

Assert-True ($virtioGpuCpp.Contains('GXOS_QEMU_VIRTIO_GPU_PROBE_ACTIVE')) 'virtio_gpu.cpp should gate diagnostics behind GXOS_QEMU_VIRTIO_GPU_PROBE_ACTIVE'
Assert-True ($virtioGpuCpp.Contains('#include "include/kernel/mmio.h"')) 'virtio_gpu.cpp should include the generic MMIO diagnostics header'
Assert-True ($virtioGpuCpp.Contains('kernel::mmio::mapForDevice')) 'virtio_gpu.cpp should use the runtime MMIO mapping helper'
Assert-True ($virtioGpuCpp.Contains('kernel::mmio::MappingReport mmioBlockerReport{};')) 'virtio_gpu.cpp should capture the detailed MMIO mapping report'
Assert-True ($virtioGpuCpp.Contains('transport_mmio_blocker_reason(transport, &mmioProbe, &mmioBlockerReport)')) 'virtio_gpu.cpp should route the probe through the MMIO feasibility helper'
Assert-True ($virtioGpuCpp.Contains('[VIRTIO-GPU] MMIO mapping report ')) 'virtio_gpu.cpp should log a compact MMIO mapping report'
Assert-True ($virtioGpuCpp.Contains('MMIO mapping blocked:')) 'virtio_gpu.cpp should still log explicit blockers when mapping fails'
Assert-True ($virtioGpuCpp.Contains('Required next kernel memory feature:')) 'virtio_gpu.cpp should name the next kernel memory feature when blocked'
Assert-True ($virtioGpuCpp.Contains('MMIO transport mapped; read-only sanity reads complete; controlled transport initialization begins')) 'virtio_gpu.cpp should enter the controlled transport-init milestone'
Assert-True ($virtioGpuCpp.Contains('GET_DISPLAY_INFO blocked: read-only transport probe stops before command submission')) 'virtio_gpu.cpp should block the public GET_DISPLAY_INFO entry point'
Assert-True ($virtioGpuCpp.Contains('requestBase=0x')) 'virtio_gpu.cpp should log the requested MMIO base'
Assert-True ($virtioGpuCpp.Contains('requestLength=0x')) 'virtio_gpu.cpp should log the requested MMIO length'
Assert-True ($virtioGpuCpp.Contains('kernelVirtualBase=')) 'virtio_gpu.cpp should log the kernel virtual base'
Assert-True ($virtioGpuCpp.Contains('mappedVirtual=')) 'virtio_gpu.cpp should log the mapped virtual address field'
Assert-True ($virtioGpuCpp.Contains('mappedLength=')) 'virtio_gpu.cpp should log the mapped length field'
Assert-True ($virtioGpuCpp.Contains('pages=')) 'virtio_gpu.cpp should log the page count'
Assert-True ($virtioGpuCpp.Contains('flags=0x')) 'virtio_gpu.cpp should log the mapping flags'
Assert-True ($virtioGpuCpp.Contains('qemuProbeOnly=')) 'virtio_gpu.cpp should note that the mapping pass is QEMU-probe-only'
Assert-True ($virtioGpuCpp.Contains('MMIO mappings must be kernel-only, no-executable, and uncached')) 'virtio_gpu.cpp should keep the mapping safety rule visible'
Assert-True ($virtioGpuCpp.Contains('uc(pcd+pwt)')) 'virtio_gpu.cpp should report the UC cache mode'
Assert-True ($virtioGpuCpp.Contains('cacheAttrs=')) 'virtio_gpu.cpp should log the cache-attribute status field'
Assert-True ($virtioGpuCpp.Contains('MMIO transport summary mmioMapped=')) 'virtio_gpu.cpp should summarize the mapped transport'
Assert-True ($virtioGpuCpp.Contains('sanityReads=')) 'virtio_gpu.cpp should report the read-only sanity-read result'
Assert-True ($virtioGpuCpp.Contains('mmioProbe.sanityReadsOk ? "ok" : "failed"')) 'virtio_gpu.cpp should report the read-only sanity-read result'
Assert-True ($virtioGpuCpp.Contains('transport.mmioStopReason = mmioProbe.stopReason != nullptr ? mmioProbe.stopReason : "transport writes intentionally disabled";')) 'virtio_gpu.cpp should carry the final stop reason'
Assert-True ($virtioGpuCpp.Contains('kernel::serial::puts(" displayInfo=");')) 'virtio_gpu.cpp should label the display-info field in the final probe line'
Assert-True ($virtioGpuCpp.Contains('case DisplayInfoOutcome::Ok:')) 'virtio_gpu.cpp should report the successful display-info milestone in the final probe line'
Assert-True ($virtioGpuCpp.Contains('REAL HARDWARE GPU/MMIO ENABLEMENT IS MULE TERRITORY')) 'virtio_gpu.cpp should keep the real-hardware warning prominent'
Assert-True ($virtioGpuCpp.Contains('reset_device begin')) 'virtio_gpu.cpp should begin the bounded transport reset'
Assert-True ($virtioGpuCpp.Contains('log_status_transition("reset", 0, readback);')) 'virtio_gpu.cpp should log the bounded reset result'
Assert-True ($virtioGpuCpp.Contains('log_status_transition(label, updated, readback);')) 'virtio_gpu.cpp should log the status transitions through the helper'
Assert-True ($virtioGpuCpp.Contains('log_status_transition("FAILED", updated, readback);')) 'virtio_gpu.cpp should log the FAILED transition when initialization aborts'
Assert-True ($virtioGpuCpp.Contains('Reset poll spins=')) 'virtio_gpu.cpp should report the bounded reset polling'
Assert-True ($virtioGpuCpp.Contains('timeout=')) 'virtio_gpu.cpp should report whether reset polling timed out'
Assert-True ($virtioGpuCpp.Contains('Feature bitmap rawLow=0x')) 'virtio_gpu.cpp should log the raw device feature bitmap'
Assert-True ($virtioGpuCpp.Contains('Feature bitmap recognized=0x')) 'virtio_gpu.cpp should log the recognized and rejected feature bits'
Assert-True ($virtioGpuCpp.Contains('VIRTIO_F_VERSION_1 required=yes')) 'virtio_gpu.cpp should require VERSION_1'
Assert-True ($virtioGpuCpp.Contains('FEATURES_OK readback cleared by device status=')) 'virtio_gpu.cpp should check the FEATURES_OK readback'
Assert-True ($virtioGpuCpp.Contains('Feature negotiation status=ok')) 'virtio_gpu.cpp should log the negotiated feature set'
Assert-True ($virtioGpuCpp.Contains('Common config queueCount=')) 'virtio_gpu.cpp should log the common config queue inventory'
Assert-True ($virtioGpuCpp.Contains('Control queue ready size=')) 'virtio_gpu.cpp should log the control queue setup'
Assert-True ($virtioGpuCpp.Contains('queueEnable=')) 'virtio_gpu.cpp should log the queue enable state'
Assert-True ($virtioGpuCpp.Contains('queueNotifyOff=')) 'virtio_gpu.cpp should log the queue notify offset'
Assert-True ($virtioGpuCpp.Contains('notifyAddr=0x')) 'virtio_gpu.cpp should log the resolved notify address'
Assert-True ($virtioGpuCpp.Contains('dma_address(request)')) 'virtio_gpu.cpp should use DMA-safe queue memory for the request descriptor'
Assert-True ($virtioGpuCpp.Contains('dma_address(&s_responseBuffer[0])')) 'virtio_gpu.cpp should use DMA-safe queue memory for the response descriptor'
Assert-True ($virtioGpuCpp.Contains('VRING_DESC_F_NEXT')) 'virtio_gpu.cpp should publish a two-descriptor request chain'
Assert-True ($virtioGpuCpp.Contains('log_init_step("GET_DISPLAY_INFO begin");')) 'virtio_gpu.cpp should begin the diagnostic GET_DISPLAY_INFO request'
Assert-True ($virtioGpuCpp.Contains('CMD_GET_DISPLAY_INFO')) 'virtio_gpu.cpp should issue only the GET_DISPLAY_INFO command'
Assert-True ($virtioGpuCpp.Contains('GET_DISPLAY_INFO response type=0x')) 'virtio_gpu.cpp should log the display-info response type'
Assert-True ($virtioGpuCpp.Contains('GET_DISPLAY_INFO completion usedIdx=')) 'virtio_gpu.cpp should log the used-ring completion'
Assert-True ($virtioGpuCpp.Contains('Display info summary slots=')) 'virtio_gpu.cpp should log the scanout inventory summary'
Assert-True ($virtioGpuCpp.Contains('qemuTwoUsableScanouts=')) 'virtio_gpu.cpp should log the QEMU two-scanout observation'
Assert-True ($virtioGpuCpp.Contains('GET_DISPLAY_INFO milestone complete; rendering remains disabled')) 'virtio_gpu.cpp should stop after the GET_DISPLAY_INFO milestone'
Assert-True ($virtioGpuCpp.Contains('[VIRTIO-GPU] Capability inventory ')) 'virtio_gpu.cpp should log a capability inventory summary'
Assert-True ($virtioGpuCpp.Contains('PCI capability walk complete caps=')) 'virtio_gpu.cpp should log full PCI capability-walk completion'
Assert-True ($virtioGpuCpp.Contains('Transport type detected:')) 'virtio_gpu.cpp should log the detected transport type before initialization'
Assert-True ($virtioGpuCpp.Contains('Probe complete: devices=')) 'virtio_gpu.cpp should end with a clear probe result line'
Assert-True ($virtioGpuCpp.Contains('transport->mmioMapped ? "yes" : "no"')) 'virtio_gpu.cpp should report the mapped transport state in the final probe line'
Assert-True ($virtioGpuCpp.Contains('kernel::serial::puts(" cacheMode=");')) 'virtio_gpu.cpp should report the cache mode in the final probe line'
Assert-True ($virtioGpuCpp.Contains('kernel::serial::puts(" featuresOk=");')) 'virtio_gpu.cpp should label the FEATURES_OK field in the final probe line'
Assert-True ($virtioGpuCpp.Contains('s_probeOutcome.featuresOk ? "yes" : "no"')) 'virtio_gpu.cpp should report the FEATURES_OK state in the final probe line'
Assert-True ($virtioGpuCpp.Contains('kernel::serial::puts(" controlq=");')) 'virtio_gpu.cpp should label the control-queue field in the final probe line'
Assert-True ($virtioGpuCpp.Contains('s_probeOutcome.controlQueueReady ? "ready" : "blocked"')) 'virtio_gpu.cpp should report the control queue state in the final probe line'
Assert-True ($virtioGpuCpp.Contains('kernel::serial::puts(" displayInfo=");')) 'virtio_gpu.cpp should label the display-info field in the final probe line'
Assert-True ($virtioGpuCpp.Contains('case DisplayInfoOutcome::Ok:')) 'virtio_gpu.cpp should report the successful display-info milestone in the final probe line'
Assert-True ($virtioGpuCpp.Contains('scanoutSlots=')) 'virtio_gpu.cpp should report the scanout slot count in the final probe line'
Assert-True ($virtioGpuCpp.Contains('enabledScanouts=')) 'virtio_gpu.cpp should report the enabled scanout count in the final probe line'
Assert-True ($virtioGpuCpp.Contains('disabledScanouts=')) 'virtio_gpu.cpp should report the disabled scanout count in the final probe line'
Assert-True ($virtioGpuCpp.Contains('rendering=disabled')) 'virtio_gpu.cpp should keep rendering disabled in the final probe line'
Assert-True ($virtioGpuCpp.Contains('kernel::serial::puts(" reason=");')) 'virtio_gpu.cpp should label the final stop reason field'
Assert-True ($virtioGpuCpp.Contains('transport.mmioStopReason = "GET_DISPLAY_INFO milestone complete";')) 'virtio_gpu.cpp should store the GET_DISPLAY_INFO milestone reason'

$initializeBody = Get-FunctionBody -Text $virtioGpuCpp -SignaturePattern 'static bool initialize_device\(DeviceState& state\)'
Assert-True ($null -ne $initializeBody) 'initialize_device should be parsable as a standalone function body'
Assert-True ($initializeBody.Contains('transport_mmio_blocker_reason(transport, &mmioProbe, &mmioBlockerReport)')) 'initialize_device should consult the MMIO blocker helper'
Assert-True ($initializeBody.Contains('MMIO mapping blocked:')) 'initialize_device should log a blocked MMIO report when mapping fails'
Assert-True ($initializeBody.Contains('Required next kernel memory feature:')) 'initialize_device should report the next required kernel feature'
Assert-True ($initializeBody.Contains('MMIO transport mapped; read-only sanity reads complete; controlled transport initialization begins')) 'initialize_device should enter the controlled transport-init milestone'
Assert-True ($initializeBody.Contains('reset_transport(transport)')) 'initialize_device should perform the bounded transport reset'
Assert-True ($initializeBody.Contains('set_status_and_verify(transport, STATUS_ACKNOWLEDGE, "ACKNOWLEDGE")')) 'initialize_device should set ACKNOWLEDGE'
Assert-True ($initializeBody.Contains('set_status_and_verify(transport, static_cast<uint8_t>(read_status(transport) | STATUS_DRIVER), "DRIVER")')) 'initialize_device should set DRIVER'
Assert-True ($initializeBody.Contains('negotiate_features(transport)')) 'initialize_device should negotiate the required minimal feature set'
Assert-True ($initializeBody.Contains('setup_control_queue(transport)')) 'initialize_device should configure the control queue'
Assert-True ($initializeBody.Contains('set_status_and_verify(transport, static_cast<uint8_t>(read_status(transport) | STATUS_DRIVER_OK), "DRIVER_OK")')) 'initialize_device should set DRIVER_OK'
Assert-True ($initializeBody.Contains('submit_display_info_request(state)')) 'initialize_device should issue one GET_DISPLAY_INFO request'
Assert-True ($initializeBody.Contains('transport.mmioStopReason = "GET_DISPLAY_INFO milestone complete";')) 'initialize_device should record the GET_DISPLAY_INFO milestone'
Assert-True ($initializeBody.Contains('device.initialized = true;')) 'initialize_device should mark the device initialized after the milestone'
Assert-True ($initializeBody.Contains('record_probe_outcome(state, true, DisplayInfoOutcome::Ok')) 'initialize_device should record the successful probe outcome'
Assert-True (-not $initializeBody.Contains('create_resource_2d(')) 'initialize_device must not create GPU resources in this pass'
Assert-True (-not $initializeBody.Contains('attach_backing(')) 'initialize_device must not attach backing memory in this pass'
Assert-True (-not $initializeBody.Contains('setup_framebuffer(')) 'initialize_device must not integrate scanouts into rendering in this pass'
Assert-True (-not $initializeBody.Contains('flush_framebuffer(')) 'initialize_device must not flush resources in this pass'
Assert-True (-not $initializeBody.Contains('transfer_to_host(')) 'initialize_device must not transfer pixels in this pass'
Assert-True (-not $initializeBody.Contains('register_as_framebuffer(')) 'initialize_device must not register a framebuffer in this pass'
Assert-True (-not $initializeBody.Contains('set_scanout(')) 'initialize_device must not set a scanout in this pass'
Assert-True (-not $initializeBody.Contains('cursorq')) 'initialize_device must not configure the cursor queue in this pass'
Assert-True ($initializeBody.Contains('transport.mmioStopReason = mmioBlocker;')) 'initialize_device should preserve the mapped blocker reason on failure'
Assert-Regex -Text $initializeBody -Pattern '(?s)record_probe_outcome\(state, true, DisplayInfoOutcome::Ok,\s*transport\.enabledScanouts,\s*transport\.disabledScanouts,\s*transport\.displayInfoSlots,\s*transport\.mmioStopReason\);' -Message 'initialize_device should end with the successful transport stop reason'

$getDisplayBody = Get-FunctionBody -Text $virtioGpuCpp -SignaturePattern 'GpuStatus get_display_info\(GpuDevice\* dev\)'
Assert-True ($null -ne $getDisplayBody) 'get_display_info should be parsable as a standalone function body'
Assert-True ($getDisplayBody.Contains('return GPU_ERR_UNSUPPORTED;')) 'get_display_info should remain blocked in the diagnostic-only probe'
Assert-True (-not $getDisplayBody.Contains('submit_display_info_request(')) 'get_display_info must not reach the request submission path in this pass'
Assert-True ($getDisplayBody.Contains('read-only transport probe stops before command submission')) 'get_display_info should keep the read-only stop message'

$resetBody = Get-FunctionBody -Text $virtioGpuCpp -SignaturePattern 'GpuStatus reset_device\(GpuDevice\* dev\)'
Assert-True ($null -ne $resetBody) 'reset_device should be parsable as a standalone function body'
Assert-True ($resetBody.Contains('reset_device blocked: transport reset is disabled in diagnostic-only probe')) 'reset_device should block probe-only transport resets'
Assert-True ($resetBody.Contains('return GPU_ERR_UNSUPPORTED;')) 'reset_device should refuse to write the transport in this pass'
Assert-True (-not $resetBody.Contains('reset_device(state->transport);')) 'reset_device must not touch the transport MMIO reset register in this pass'

Assert-Regex -Text $virtioGpuCpp -Pattern '(?s)GpuStatus setup_framebuffer\(GpuDevice\* dev, uint32_t width, uint32_t height,\s*uint32_t scanoutId\)\s*\{.*?return GPU_ERR_UNSUPPORTED;' -Message 'setup_framebuffer must stay unsupported in this probe-only branch'
Assert-Regex -Text $virtioGpuCpp -Pattern '(?s)GpuStatus flush_framebuffer\(GpuDevice\* dev, uint32_t x, uint32_t y,\s*uint32_t width, uint32_t height\)\s*\{.*?return GPU_ERR_UNSUPPORTED;' -Message 'flush_framebuffer must stay unsupported in this probe-only branch'
Assert-Regex -Text $virtioGpuCpp -Pattern '(?s)GpuStatus create_resource_2d\(GpuDevice\* dev, uint32_t\* resourceIdOut,\s*uint32_t width, uint32_t height, GpuFormat format\)\s*\{.*?return GPU_ERR_UNSUPPORTED;' -Message 'create_resource_2d must stay unsupported in this probe-only branch'
Assert-Regex -Text $virtioGpuCpp -Pattern '(?s)GpuStatus attach_backing\(GpuDevice\* dev, uint32_t resourceId,\s*uint64_t physAddr, size_t size\)\s*\{.*?return GPU_ERR_UNSUPPORTED;' -Message 'attach_backing must stay unsupported in this probe-only branch'
Assert-Regex -Text $virtioGpuCpp -Pattern '(?s)GpuStatus detach_backing\(GpuDevice\* dev, uint32_t resourceId\)\s*\{.*?return GPU_ERR_UNSUPPORTED;' -Message 'detach_backing must stay unsupported in this probe-only branch'
Assert-Regex -Text $virtioGpuCpp -Pattern '(?s)GpuStatus transfer_to_host\(GpuDevice\* dev, uint32_t resourceId,\s*uint32_t x, uint32_t y, uint32_t width, uint32_t height\)\s*\{.*?return GPU_ERR_UNSUPPORTED;' -Message 'transfer_to_host must stay unsupported in this probe-only branch'
Assert-Regex -Text $virtioGpuCpp -Pattern '(?s)GpuStatus destroy_resource\(GpuDevice\* dev, uint32_t resourceId\)\s*\{.*?return GPU_ERR_UNSUPPORTED;' -Message 'destroy_resource must stay unsupported in this probe-only branch'
Assert-Regex -Text $virtioGpuCpp -Pattern '(?s)GpuStatus register_as_framebuffer\(GpuDevice\* dev\)\s*\{.*?return GPU_ERR_UNSUPPORTED;' -Message 'register_as_framebuffer must stay unsupported in this probe-only branch'
Assert-Regex -Text $virtioGpuCpp -Pattern '(?s)return transport->commonCfg\.present &&\s*transport->notifyCfg\.present &&\s*transport->isrCfg\.present &&\s*transport->deviceCfg\.present;' -Message 'cfg_type=0x05 must not be the sole transport readiness gate'
Assert-True ($virtioGpuCpp.Contains('mmio_read16(region_mmio_addr(transport.commonCfg, pci::COMMON_NUM_QUEUES))')) 'virtio_gpu.cpp should read num_queues only after mapping'
Assert-True ($virtioGpuCpp.Contains('mmio_read8(region_mmio_addr(transport.commonCfg, pci::COMMON_STATUS))')) 'virtio_gpu.cpp should read the common status register only after mapping'
Assert-True ($virtioGpuCpp.Contains('mmio_read8(region_mmio_addr(transport.commonCfg, pci::COMMON_CFG_GEN))')) 'virtio_gpu.cpp should read config generation only after mapping'
Assert-True ($virtioGpuCpp.Contains('mmio_read32(device_cfg_addr(transport, 0x08))')) 'virtio_gpu.cpp should read the device numScanouts field after GET_DISPLAY_INFO'
Assert-True ($virtioGpuCpp.Contains('mmio_read32(device_cfg_addr(transport, 0x0C))')) 'virtio_gpu.cpp should read the device numCapsets field after GET_DISPLAY_INFO'
Assert-True ($virtioGpuCpp.Contains('commonCfg.mapped = true;')) 'virtio_gpu.cpp should mark the common config mapping'
Assert-True ($virtioGpuCpp.Contains('notifyCfg.mapped = true;')) 'virtio_gpu.cpp should mark the notify config mapping'
Assert-True ($virtioGpuCpp.Contains('isrCfg.mapped = true;')) 'virtio_gpu.cpp should mark the ISR config mapping'
Assert-True ($virtioGpuCpp.Contains('deviceCfg.mapped = true;')) 'virtio_gpu.cpp should mark the device config mapping'

Assert-True ($mainCpp.Contains('Diagnostic-only virtio-gpu probe runs regardless of framebuffer')) 'main.cpp should run virtio-gpu init before framebuffer-dependent rendering'
Assert-True ($mainCpp.Contains('kernel::virtio::gpu::init();')) 'main.cpp should invoke the virtio-gpu probe'
Assert-True ($mainCpp.Contains('kernel::virtio::gpu::set_kernel_physical_base(bootinfo->KernelPhysicalBase);')) 'main.cpp should publish the kernel physical base to the virtio-gpu probe'

Assert-True ($probeSmoke.Contains('-DGXOS_QEMU_VIRTIO_GPU_PROBE_ACTIVE')) 'runtime smoke should rebuild the kernel with the virtio-gpu probe flag'
Assert-Regex -Text $probeSmoke -Pattern '(?s)Backend = ''std''.*?Required = \$true.*?WaitPattern = ''\\\[KERNEL\\\] Framebuffer ready''' -Message 'std backend must remain the required GOP baseline in the probe smoke'
Assert-Regex -Text $probeSmoke -Pattern '(?s)Backend = ''virtio-gpu''.*?Required = \$false.*?WaitPattern = ''\\\[VIRTIO-GPU\\\] Probe complete: devices=''' -Message 'virtio-gpu backend must stay optional and diagnostic-only in the probe smoke'
Assert-Regex -Text $probeSmoke -Pattern '(?s)Backend = ''virtio-gpu-modern-only''.*?Required = \$false.*?Supported = \(Test-QemuVirtioGpuModernOnlySupport\).*?WaitPattern = ''\\\[VIRTIO-GPU\\\] Probe complete: devices=''' -Message 'modern-only virtio-gpu backend must be optional and gated by QEMU help'
Assert-True ($probeSmoke.Contains('disable-legacy=<OnOffAuto>')) 'runtime smoke should inspect QEMU help for virtio-gpu-pci modern-only support'
Assert-True ($probeSmoke.Contains('disable-legacy=on')) 'launcher should use disable-legacy=on for the modern-only virtio-gpu mode'
Assert-True ($probeSmoke.Contains('gpuMmioReportLine')) 'runtime smoke should capture the MMIO mapping report line'
Assert-True ($probeSmoke.Contains('gpuMmioSummaryLine')) 'runtime smoke should capture the MMIO transport summary line'
Assert-True ($probeSmoke.Contains('gpuMmioMappedLine')) 'runtime smoke should capture the MMIO mapped milestone line'
Assert-True ($probeSmoke.Contains('gpuStatusResetLine')) 'runtime smoke should capture the transport reset result line'
Assert-True ($probeSmoke.Contains('gpuAckLine')) 'runtime smoke should capture the ACKNOWLEDGE status line'
Assert-True ($probeSmoke.Contains('gpuDriverLine')) 'runtime smoke should capture the DRIVER status line'
Assert-True ($probeSmoke.Contains('gpuFeaturesOkStatusLine')) 'runtime smoke should capture the FEATURES_OK status line'
Assert-True ($probeSmoke.Contains('gpuDriverOkStatusLine')) 'runtime smoke should capture the DRIVER_OK status line'
Assert-True ($probeSmoke.Contains('gpuFeatureBitmapLine')) 'runtime smoke should capture the raw feature bitmap line'
Assert-True ($probeSmoke.Contains('gpuFeatureNegotiationLine')) 'runtime smoke should capture the negotiated feature set line'
Assert-True ($probeSmoke.Contains('gpuQueueCountLine')) 'runtime smoke should capture the queue sizing line'
Assert-True ($probeSmoke.Contains('gpuQueueLine')) 'runtime smoke should capture the control queue layout line'
Assert-True ($probeSmoke.Contains('gpuDisplayInfoResponseLine')) 'runtime smoke should capture the GET_DISPLAY_INFO response type line'
Assert-True ($probeSmoke.Contains('gpuDisplayInfoLine')) 'runtime smoke should capture the GET_DISPLAY_INFO completion line'
Assert-True ($probeSmoke.Contains('gpuDisplayInfoSummaryLine')) 'runtime smoke should capture the scanout summary line'
Assert-True ($probeSmoke.Contains('gpuEnabledScanoutMatches')) 'runtime smoke should capture enabled scanout geometry matches'
Assert-True ($probeSmoke.Contains('mmioMapped=yes')) 'runtime smoke should capture the mapped transport state'
Assert-True ($probeSmoke.Contains('featuresOk=yes')) 'runtime smoke should capture the negotiated FEATURES_OK final state'
Assert-True ($probeSmoke.Contains('controlq=ready')) 'runtime smoke should capture the control queue final state'
Assert-True ($probeSmoke.Contains('displayInfo=ok')) 'runtime smoke should capture the GET_DISPLAY_INFO milestone result'
Assert-True ($probeSmoke.Contains('scanoutSlots=16')) 'runtime smoke should capture the final scanout slot count'
Assert-True ($probeSmoke.Contains('enabledScanouts=\d+')) 'runtime smoke should capture the enabled scanout count pattern'
Assert-True ($probeSmoke.Contains('disabledScanouts=\d+')) 'runtime smoke should capture the disabled scanout count pattern'
Assert-True ($probeSmoke.Contains('sanityReads=ok')) 'runtime smoke should capture the read-only sanity-read result'
Assert-True ($probeSmoke.Contains('reason=GET_DISPLAY_INFO milestone complete')) 'runtime smoke should capture the final stop reason'

Assert-True ($launcher.Contains('diagnostic virtio-gpu-pci probe (no rendering)')) 'launcher should advertise diagnostic-only virtio-gpu probing'
Assert-True ($launcher.Contains('modern-only diagnostic probe (no rendering)')) 'launcher should advertise the modern-only diagnostic virtio-gpu probe'
Assert-True ($launcher.Contains('diagnostic probe')) 'launcher should not promise rendering or multi-output support'

$runtimeCppFiles = Get-ChildItem -Path (Join-Path $Root 'kernel\core') -Filter '*.cpp' -File
$renderApiPattern = 'setup_framebuffer\(|flush_framebuffer\(|create_resource_2d\(|attach_backing\(|detach_backing\(|transfer_to_host\(|destroy_resource\(|register_as_framebuffer\(|set_scanout\('
$runtimeCallSites = foreach ($file in $runtimeCppFiles) {
    if ($file.Name -eq 'virtio_gpu.cpp') {
        continue
    }

    $content = Read-Text -Path $file.FullName
    if ([regex]::IsMatch($content, $renderApiPattern)) {
        $file.FullName
    }
}

Assert-True (-not $runtimeCallSites) ('No runtime call sites for virtio-gpu rendering APIs should exist outside kernel/core/virtio_gpu.cpp. Found: ' + ($runtimeCallSites -join ', '))

Write-Host 'VirtIO GPU diagnostic source smoke passed.'
