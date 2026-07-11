param()

$ErrorActionPreference = 'Stop'

$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path

function Assert-Match {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,
        [Parameter(Mandatory = $true)]
        [string]$Pattern,
        [Parameter(Mandatory = $true)]
        [string]$Message
    )

    if (-not (Select-String -Path $Path -Pattern $Pattern -Quiet)) {
        throw "$Message`nPattern: $Pattern`nFile: $Path"
    }
}

$mmioHeader = Join-Path $root 'kernel\core\include\kernel\mmio.h'
$virtioGpuCpp = Join-Path $root 'kernel\core\virtio_gpu.cpp'
$probeSmoke = Join-Path $root 'scripts\smoke-qemu-display-probe.ps1'

Assert-Match -Path $mmioHeader -Pattern 'SAFE_DIRECT_MAP_CEILING' -Message 'mmio.h should define the conservative direct-map ceiling.'
Assert-Match -Path $mmioHeader -Pattern 'pageCount' -Message 'mmio.h should expose a page-count field in MappingReport.'
Assert-Match -Path $mmioHeader -Pattern 'MAP_FLAG_NON_USER' -Message 'mmio.h should define the non-user mapping flag.'
Assert-Match -Path $mmioHeader -Pattern 'MAP_FLAG_NO_EXEC' -Message 'mmio.h should define the no-exec mapping flag.'
Assert-Match -Path $mmioHeader -Pattern 'MMIO mappings must be kernel-only and no-executable' -Message 'mmio.h should enforce kernel-only, no-exec MMIO mappings.'
Assert-Match -Path $mmioHeader -Pattern 'runtime MMIO page-table mapping is not implemented yet' -Message 'mmio.h should name the runtime page-table blocker.'
Assert-Match -Path $mmioHeader -Pattern 'MMIO unmap is not implemented yet; runtime page-table tracking is absent' -Message 'mmio.h should keep the unmap blocker explicit.'

Assert-Match -Path $virtioGpuCpp -Pattern 'kernel::mmio::mapForDevice' -Message 'virtio_gpu.cpp should route MMIO access through the mapping helper.'
Assert-Match -Path $virtioGpuCpp -Pattern 'MAP_FLAG_NON_USER \| kernel::mmio::MAP_FLAG_NO_EXEC' -Message 'virtio_gpu.cpp should request kernel-only, no-exec MMIO mappings.'
Assert-Match -Path $virtioGpuCpp -Pattern 'requestBase=0x' -Message 'virtio_gpu.cpp should log the requested MMIO base.'
Assert-Match -Path $virtioGpuCpp -Pattern 'requestLength=0x' -Message 'virtio_gpu.cpp should log the requested MMIO length.'
Assert-Match -Path $virtioGpuCpp -Pattern 'mappedVirtual=' -Message 'virtio_gpu.cpp should log the mapped virtual address field.'
Assert-Match -Path $virtioGpuCpp -Pattern 'pages=' -Message 'virtio_gpu.cpp should log the page count.'
Assert-Match -Path $virtioGpuCpp -Pattern 'flags=0x' -Message 'virtio_gpu.cpp should log the mapping flags.'
Assert-Match -Path $virtioGpuCpp -Pattern 'nonUser=' -Message 'virtio_gpu.cpp should keep the non-user mapping rule visible.'
Assert-Match -Path $virtioGpuCpp -Pattern 'noExec=' -Message 'virtio_gpu.cpp should keep the no-exec mapping rule visible.'
Assert-Match -Path $virtioGpuCpp -Pattern 'cacheAttrs=' -Message 'virtio_gpu.cpp should log the cache-attribute status field.'
Assert-Match -Path $virtioGpuCpp -Pattern 'todo\(PAT/MTRR\)' -Message 'virtio_gpu.cpp should mention the cache-attribute TODO.'
Assert-Match -Path $virtioGpuCpp -Pattern 'qemuProbeOnly=' -Message 'virtio_gpu.cpp should note that this path is QEMU-probe-only.'
Assert-Match -Path $virtioGpuCpp -Pattern 'runtime MMIO page-table mapping is not implemented yet' -Message 'virtio_gpu.cpp should surface the precise runtime mapping blocker.'
Assert-Match -Path $virtioGpuCpp -Pattern 'runtime MMIO mapping helper is still stubbed' -Message 'virtio_gpu.cpp should keep the stubbed-helper blocker visible.'
Assert-Match -Path $virtioGpuCpp -Pattern 'GET_DISPLAY_INFO blocked: MMIO mapping layer is not enabled yet' -Message 'virtio_gpu.cpp should leave GET_DISPLAY_INFO blocked in this pass.'
Assert-Match -Path $virtioGpuCpp -Pattern 'reset_device blocked: transport reset is disabled in diagnostic-only probe' -Message 'virtio_gpu.cpp should keep transport reset disabled.'

Assert-Match -Path $probeSmoke -Pattern 'requestBase=0x' -Message 'runtime smoke should check the requested MMIO base.'
Assert-Match -Path $probeSmoke -Pattern 'mappedVirtual=' -Message 'runtime smoke should check the mapped virtual address field.'
Assert-Match -Path $probeSmoke -Pattern 'pages=' -Message 'runtime smoke should check the page count.'
Assert-Match -Path $probeSmoke -Pattern 'qemuProbeOnly=yes' -Message 'runtime smoke should check the QEMU-probe-only marker.'
Assert-Match -Path $probeSmoke -Pattern 'runtime MMIO page-table mapping is not implemented yet' -Message 'runtime smoke should expect the precise MMIO blocker.'

Write-Host '[SMOKE] MMIO mapping source checks passed.'
