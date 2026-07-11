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
$mmioCpp = Join-Path $root 'kernel\core\mmio.cpp'
$virtioGpuCpp = Join-Path $root 'kernel\core\virtio_gpu.cpp'
$probeSmoke = Join-Path $root 'scripts\smoke-qemu-display-probe.ps1'

Assert-Match -Path $mmioHeader -Pattern 'SAFE_DIRECT_MAP_CEILING' -Message 'mmio.h should retain the legacy ceiling constant.'
Assert-Match -Path $mmioHeader -Pattern 'MMIO_WINDOW_BASE' -Message 'mmio.h should define the reserved MMIO base.'
Assert-Match -Path $mmioHeader -Pattern 'MMIO_WINDOW_SIZE' -Message 'mmio.h should define the reserved MMIO size.'
Assert-Match -Path $mmioHeader -Pattern 'MMIO_WINDOW_LIMIT' -Message 'mmio.h should define the reserved MMIO limit.'
Assert-Match -Path $mmioHeader -Pattern 'MMIO_WINDOW_PAGE_COUNT' -Message 'mmio.h should define the reserved MMIO page count.'
Assert-Match -Path $mmioHeader -Pattern 'MAP_FLAG_NON_USER' -Message 'mmio.h should define the non-user mapping flag.'
Assert-Match -Path $mmioHeader -Pattern 'MAP_FLAG_NO_EXEC' -Message 'mmio.h should define the no-exec mapping flag.'
Assert-Match -Path $mmioHeader -Pattern 'MAP_FLAG_UNCACHED' -Message 'mmio.h should define the uncached MMIO flag.'
Assert-Match -Path $mmioHeader -Pattern 'reserved kernel MMIO window' -Message 'mmio.h should document the reserved MMIO window.'
Assert-Match -Path $mmioHeader -Pattern 'bounded bump allocator' -Message 'mmio.h should document the bump-allocator strategy.'
Assert-Match -Path $mmioHeader -Pattern 'mapped slots are retained' -Message 'mmio.h should document the unmap limitation.'
Assert-Match -Path $mmioHeader -Pattern 'MMIO mappings must be kernel-only, no-executable, and uncached' -Message 'mmio.h should enforce kernel-only, no-exec, uncached MMIO mappings.'
Assert-Match -Path $mmioHeader -Pattern 'uc\(pcd\+pwt\)' -Message 'mmio.h should expose the UC cache-mode label.'
Assert-Match -Path $mmioHeader -Pattern 'mapForDevice' -Message 'mmio.h should declare the runtime device-mapping helper.'
Assert-Match -Path $mmioHeader -Pattern 'unmap' -Message 'mmio.h should declare the runtime unmap helper.'

Assert-Match -Path $mmioCpp -Pattern 'GXOS_QEMU_VIRTIO_GPU_PROBE_ACTIVE' -Message 'mmio.cpp should gate the active mapper behind the QEMU probe build flag.'
Assert-Match -Path $mmioCpp -Pattern 'runtime MMIO mapping is gated to the x86_64 QEMU probe build' -Message 'mmio.cpp should keep the non-QEMU fallback explicit.'
Assert-Match -Path $mmioCpp -Pattern 'PTE_PCD' -Message 'mmio.cpp should use PCD for device MMIO.'
Assert-Match -Path $mmioCpp -Pattern 'PTE_PWT' -Message 'mmio.cpp should use PWT for device MMIO.'
Assert-Match -Path $mmioCpp -Pattern 'PTE_NX' -Message 'mmio.cpp should mark MMIO mappings non-executable.'
Assert-Match -Path $mmioCpp -Pattern 'mapped into reserved kernel MMIO window' -Message 'mmio.cpp should report successful window mapping.'
Assert-Match -Path $mmioCpp -Pattern 'reused reserved kernel MMIO page' -Message 'mmio.cpp should deduplicate repeated page mappings.'
Assert-Match -Path $mmioCpp -Pattern 'reserved MMIO window exhausted' -Message 'mmio.cpp should stop on window exhaustion.'
Assert-Match -Path $mmioCpp -Pattern 'kernel::arch::invalidate_tlb_entry' -Message 'mmio.cpp should invalidate TLB entries after mapping and unmapping.'
Assert-Match -Path $mmioCpp -Pattern 'MMIO unmap is gated to the x86_64 QEMU probe build' -Message 'mmio.cpp should keep the unmap gate explicit.'

Assert-Match -Path $virtioGpuCpp -Pattern 'kernel::mmio::mapForDevice' -Message 'virtio_gpu.cpp should route MMIO access through the mapping helper.'
Assert-Match -Path $virtioGpuCpp -Pattern 'MAP_FLAG_NON_USER \| kernel::mmio::MAP_FLAG_NO_EXEC \| kernel::mmio::MAP_FLAG_UNCACHED' -Message 'virtio_gpu.cpp should request kernel-only, no-exec, uncached MMIO mappings.'
Assert-Match -Path $virtioGpuCpp -Pattern 'requestBase=0x' -Message 'virtio_gpu.cpp should log the requested MMIO base.'
Assert-Match -Path $virtioGpuCpp -Pattern 'requestLength=0x' -Message 'virtio_gpu.cpp should log the requested MMIO length.'
Assert-Match -Path $virtioGpuCpp -Pattern 'kernelVirtualBase=' -Message 'virtio_gpu.cpp should log the mapped kernel virtual base.'
Assert-Match -Path $virtioGpuCpp -Pattern 'mappedVirtual=' -Message 'virtio_gpu.cpp should log the mapped virtual address field.'
Assert-Match -Path $virtioGpuCpp -Pattern 'mappedLength=' -Message 'virtio_gpu.cpp should log the mapped length field.'
Assert-Match -Path $virtioGpuCpp -Pattern 'pages=' -Message 'virtio_gpu.cpp should log the page count.'
Assert-Match -Path $virtioGpuCpp -Pattern 'flags=0x' -Message 'virtio_gpu.cpp should log the mapping flags.'
Assert-Match -Path $virtioGpuCpp -Pattern 'nonUser=' -Message 'virtio_gpu.cpp should keep the non-user mapping rule visible.'
Assert-Match -Path $virtioGpuCpp -Pattern 'noExec=' -Message 'virtio_gpu.cpp should keep the no-exec mapping rule visible.'
Assert-Match -Path $virtioGpuCpp -Pattern 'uncached=' -Message 'virtio_gpu.cpp should keep the uncached mapping rule visible.'
Assert-Match -Path $virtioGpuCpp -Pattern 'cacheAttrs=' -Message 'virtio_gpu.cpp should log the cache-attribute status field.'
Assert-Match -Path $virtioGpuCpp -Pattern 'cacheMode=uc\(pcd\+pwt\)' -Message 'virtio_gpu.cpp should report the UC cache mode.'
Assert-Match -Path $virtioGpuCpp -Pattern 'qemuProbeOnly=' -Message 'virtio_gpu.cpp should note that this path is QEMU-probe-only.'
Assert-Match -Path $virtioGpuCpp -Pattern 'MMIO transport summary mmioMapped=yes' -Message 'virtio_gpu.cpp should report the mapped transport summary.'
Assert-Match -Path $virtioGpuCpp -Pattern 'MMIO transport mapped; read-only sanity reads complete; GET_DISPLAY_INFO remains disabled in this diagnostic pass' -Message 'virtio_gpu.cpp should stop after the read-only MMIO milestone.'
Assert-Match -Path $virtioGpuCpp -Pattern 'GET_DISPLAY_INFO blocked: read-only transport probe stops before command submission' -Message 'virtio_gpu.cpp should leave GET_DISPLAY_INFO blocked in this pass.'
Assert-Match -Path $virtioGpuCpp -Pattern 'reset_device blocked: transport reset is disabled in diagnostic-only probe' -Message 'virtio_gpu.cpp should keep transport reset disabled.'
Assert-Match -Path $virtioGpuCpp -Pattern 'REAL HARDWARE GPU/MMIO ENABLEMENT IS MULE TERRITORY' -Message 'virtio_gpu.cpp should keep the real-hardware warning prominent.'

Assert-Match -Path $probeSmoke -Pattern 'requestBase=0x' -Message 'runtime smoke should check the requested MMIO base.'
Assert-Match -Path $probeSmoke -Pattern 'mappedVirtual=' -Message 'runtime smoke should check the mapped virtual address field.'
Assert-Match -Path $probeSmoke -Pattern 'pages=' -Message 'runtime smoke should check the page count.'
Assert-Match -Path $probeSmoke -Pattern 'cacheMode=uc\(pcd\+pwt\)' -Message 'runtime smoke should check the UC cache mode.'
Assert-Match -Path $probeSmoke -Pattern 'mmioMapped=yes' -Message 'runtime smoke should expect the mapped transport summary.'
Assert-Match -Path $probeSmoke -Pattern 'sanityReads=ok' -Message 'runtime smoke should check the read-only sanity-read result.'
Assert-Match -Path $probeSmoke -Pattern 'reason=transport writes intentionally disabled' -Message 'runtime smoke should expect the final stop reason.'

Write-Host '[SMOKE] MMIO mapping source checks passed.'
