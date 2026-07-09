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
Assert-Regex -Text $virtioGpuCpp -Pattern '(?s)GpuStatus setup_framebuffer\(GpuDevice\* dev, uint32_t width, uint32_t height,\s*uint32_t scanoutId\)\s*\{.*?return GPU_ERR_UNSUPPORTED;' -Message 'setup_framebuffer must stay unsupported in this probe-only branch'
Assert-Regex -Text $virtioGpuCpp -Pattern '(?s)GpuStatus flush_framebuffer\(GpuDevice\* dev, uint32_t x, uint32_t y,\s*uint32_t width, uint32_t height\)\s*\{.*?return GPU_ERR_UNSUPPORTED;' -Message 'flush_framebuffer must stay unsupported in this probe-only branch'
Assert-Regex -Text $virtioGpuCpp -Pattern '(?s)GpuStatus create_resource_2d\(GpuDevice\* dev, uint32_t\* resourceIdOut,\s*uint32_t width, uint32_t height, GpuFormat format\)\s*\{.*?return GPU_ERR_UNSUPPORTED;' -Message 'create_resource_2d must stay unsupported in this probe-only branch'
Assert-Regex -Text $virtioGpuCpp -Pattern '(?s)GpuStatus attach_backing\(GpuDevice\* dev, uint32_t resourceId,\s*uint64_t physAddr, size_t size\)\s*\{.*?return GPU_ERR_UNSUPPORTED;' -Message 'attach_backing must stay unsupported in this probe-only branch'
Assert-Regex -Text $virtioGpuCpp -Pattern '(?s)GpuStatus transfer_to_host\(GpuDevice\* dev, uint32_t resourceId,\s*uint32_t x, uint32_t y, uint32_t width, uint32_t height\)\s*\{.*?return GPU_ERR_UNSUPPORTED;' -Message 'transfer_to_host must stay unsupported in this probe-only branch'
Assert-Regex -Text $virtioGpuCpp -Pattern '(?s)GpuStatus destroy_resource\(GpuDevice\* dev, uint32_t resourceId\)\s*\{.*?return GPU_ERR_UNSUPPORTED;' -Message 'destroy_resource must stay unsupported in this probe-only branch'
Assert-Regex -Text $virtioGpuCpp -Pattern '(?s)GpuStatus register_as_framebuffer\(GpuDevice\* dev\)\s*\{.*?return GPU_ERR_UNSUPPORTED;' -Message 'register_as_framebuffer must stay unsupported in this probe-only branch'

Assert-True ($mainCpp.Contains('Diagnostic-only virtio-gpu probe runs regardless of framebuffer')) 'main.cpp should run virtio-gpu init before framebuffer-dependent rendering'
Assert-True ($mainCpp.Contains('kernel::virtio::gpu::init();')) 'main.cpp should invoke the virtio-gpu probe'

Assert-True ($probeSmoke.Contains('-DGXOS_QEMU_VIRTIO_GPU_PROBE_ACTIVE')) 'runtime smoke should rebuild the kernel with the virtio-gpu probe flag'
Assert-Regex -Text $probeSmoke -Pattern '(?s)Backend = ''std''.*?Required = \$true.*?WaitPattern = ''\\\[KERNEL\\\] Framebuffer ready''' -Message 'std backend must remain the required GOP baseline in the probe smoke'
Assert-Regex -Text $probeSmoke -Pattern '(?s)Backend = ''virtio-gpu''.*?Required = \$false.*?WaitPattern = ''\\\[VIRTIO-GPU\\\] Probe complete, devices=''' -Message 'virtio-gpu backend must stay optional and diagnostic-only in the probe smoke'

Assert-True ($launcher.Contains('diagnostic virtio-gpu-pci probe (no rendering)')) 'launcher should advertise diagnostic-only virtio-gpu probing'
Assert-True ($launcher.Contains('diagnostic probe')) 'launcher should not promise rendering or multi-output support'

$runtimeCppFiles = Get-ChildItem -Path (Join-Path $Root 'kernel\core') -Filter '*.cpp' -File
$renderApiPattern = 'setup_framebuffer\(|flush_framebuffer\(|create_resource_2d\(|attach_backing\(|transfer_to_host\(|register_as_framebuffer\('
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
