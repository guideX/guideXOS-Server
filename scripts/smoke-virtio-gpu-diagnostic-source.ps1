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

$virtioGpuCpp = Read-Text -Path (Join-Path $Root 'kernel\core\virtio_gpu.cpp')
$mainCpp = Read-Text -Path (Join-Path $Root 'kernel\core\main.cpp')
$probeSmoke = Read-Text -Path (Join-Path $Root 'scripts\smoke-qemu-display-probe.ps1')
$launcher = Read-Text -Path (Join-Path $Root 'scripts\run-qemu-display-probe.bat')

Assert-True ($virtioGpuCpp.Contains('GXOS_QEMU_VIRTIO_GPU_PROBE_ACTIVE')) 'virtio_gpu.cpp should gate diagnostics behind GXOS_QEMU_VIRTIO_GPU_PROBE_ACTIVE'
Assert-True ($virtioGpuCpp.Contains('CMD_GET_DISPLAY_INFO')) 'virtio_gpu.cpp should issue GET_DISPLAY_INFO for diagnostics'
Assert-True ($virtioGpuCpp.Contains('[VIRTIO-GPU] Capability inventory ')) 'virtio_gpu.cpp should log a capability inventory summary'
Assert-True ($virtioGpuCpp.Contains('PCI capability walk complete caps=')) 'virtio_gpu.cpp should log full PCI capability-walk completion'
Assert-True ($virtioGpuCpp.Contains('Transport type detected:')) 'virtio_gpu.cpp should log the detected transport type before initialization'
Assert-True ($virtioGpuCpp.Contains('Feature negotiation status=')) 'virtio_gpu.cpp should log feature negotiation status when it is reached'
Assert-True ($virtioGpuCpp.Contains('Common config queueCount=')) 'virtio_gpu.cpp should log the common config queue count when it is reached'
Assert-True ($virtioGpuCpp.Contains('log_init_step("reset_device begin")')) 'virtio_gpu.cpp should mark the start of risky modern-transport init'
Assert-True ($virtioGpuCpp.Contains('log_init_step("GET_DISPLAY_INFO begin")')) 'virtio_gpu.cpp should mark the GET_DISPLAY_INFO request boundary'
Assert-True ($virtioGpuCpp.Contains('Safe MMIO check failed')) 'virtio_gpu.cpp should refuse unsafe direct-mapped MMIO bases before reset'
Assert-True ($virtioGpuCpp.Contains('Probe complete: devices=')) 'virtio_gpu.cpp should end with a clear probe result line'
Assert-True ($virtioGpuCpp.Contains('only cfg_type=0x05 pci capability observed')) 'virtio_gpu.cpp should report a cfg_type=0x05-only transport blocker explicitly'
Assert-Regex -Text $virtioGpuCpp -Pattern '(?s)GpuStatus setup_framebuffer\(GpuDevice\* dev, uint32_t width, uint32_t height,\s*uint32_t scanoutId\)\s*\{.*?return GPU_ERR_UNSUPPORTED;' -Message 'setup_framebuffer must stay unsupported in this probe-only branch'
Assert-Regex -Text $virtioGpuCpp -Pattern '(?s)GpuStatus flush_framebuffer\(GpuDevice\* dev, uint32_t x, uint32_t y,\s*uint32_t width, uint32_t height\)\s*\{.*?return GPU_ERR_UNSUPPORTED;' -Message 'flush_framebuffer must stay unsupported in this probe-only branch'
Assert-Regex -Text $virtioGpuCpp -Pattern '(?s)GpuStatus create_resource_2d\(GpuDevice\* dev, uint32_t\* resourceIdOut,\s*uint32_t width, uint32_t height, GpuFormat format\)\s*\{.*?return GPU_ERR_UNSUPPORTED;' -Message 'create_resource_2d must stay unsupported in this probe-only branch'
Assert-Regex -Text $virtioGpuCpp -Pattern '(?s)GpuStatus attach_backing\(GpuDevice\* dev, uint32_t resourceId,\s*uint64_t physAddr, size_t size\)\s*\{.*?return GPU_ERR_UNSUPPORTED;' -Message 'attach_backing must stay unsupported in this probe-only branch'
Assert-Regex -Text $virtioGpuCpp -Pattern '(?s)GpuStatus detach_backing\(GpuDevice\* dev, uint32_t resourceId\)\s*\{.*?return GPU_ERR_UNSUPPORTED;' -Message 'detach_backing must stay unsupported in this probe-only branch'
Assert-Regex -Text $virtioGpuCpp -Pattern '(?s)GpuStatus transfer_to_host\(GpuDevice\* dev, uint32_t resourceId,\s*uint32_t x, uint32_t y, uint32_t width, uint32_t height\)\s*\{.*?return GPU_ERR_UNSUPPORTED;' -Message 'transfer_to_host must stay unsupported in this probe-only branch'
Assert-Regex -Text $virtioGpuCpp -Pattern '(?s)GpuStatus destroy_resource\(GpuDevice\* dev, uint32_t resourceId\)\s*\{.*?return GPU_ERR_UNSUPPORTED;' -Message 'destroy_resource must stay unsupported in this probe-only branch'
Assert-Regex -Text $virtioGpuCpp -Pattern '(?s)GpuStatus register_as_framebuffer\(GpuDevice\* dev\)\s*\{.*?return GPU_ERR_UNSUPPORTED;' -Message 'register_as_framebuffer must stay unsupported in this probe-only branch'
Assert-Regex -Text $virtioGpuCpp -Pattern '(?s)return transport->commonCfg\.present &&\s*transport->notifyCfg\.present &&\s*transport->isrCfg\.present &&\s*transport->deviceCfg\.present;' -Message 'cfg_type=0x05 must not be the sole transport readiness gate'

Assert-True ($mainCpp.Contains('Diagnostic-only virtio-gpu probe runs regardless of framebuffer')) 'main.cpp should run virtio-gpu init before framebuffer-dependent rendering'
Assert-True ($mainCpp.Contains('kernel::virtio::gpu::init();')) 'main.cpp should invoke the virtio-gpu probe'

Assert-True ($probeSmoke.Contains('-DGXOS_QEMU_VIRTIO_GPU_PROBE_ACTIVE')) 'runtime smoke should rebuild the kernel with the virtio-gpu probe flag'
Assert-Regex -Text $probeSmoke -Pattern '(?s)Backend = ''std''.*?Required = \$true.*?WaitPattern = ''\\\[KERNEL\\\] Framebuffer ready''' -Message 'std backend must remain the required GOP baseline in the probe smoke'
Assert-Regex -Text $probeSmoke -Pattern '(?s)Backend = ''virtio-gpu''.*?Required = \$false.*?WaitPattern = ''\\\[VIRTIO-GPU\\\] Probe complete: devices=''' -Message 'virtio-gpu backend must stay optional and diagnostic-only in the probe smoke'
Assert-Regex -Text $probeSmoke -Pattern '(?s)Backend = ''virtio-gpu-modern-only''.*?Required = \$false.*?Supported = \(Test-QemuVirtioGpuModernOnlySupport\).*?WaitPattern = ''\\\[VIRTIO-GPU\\\] Probe complete: devices=''' -Message 'modern-only virtio-gpu backend must be optional and gated by QEMU help'
Assert-True ($probeSmoke.Contains('disable-legacy=<OnOffAuto>')) 'runtime smoke should inspect QEMU help for virtio-gpu-pci modern-only support'
Assert-True ($probeSmoke.Contains('disable-legacy=on')) 'launcher should use disable-legacy=on for the modern-only virtio-gpu mode'

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
