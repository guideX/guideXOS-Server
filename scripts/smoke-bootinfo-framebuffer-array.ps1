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

$bootInfoHeader = Join-Path $root 'guideXOSBootLoader\guidexOSBootInfo.h'
$bootloaderMain = Join-Path $root 'guideXOSBootLoader\main.cpp'
$kernelMain = Join-Path $root 'kernel\core\main.cpp'
$kernelFramebufferHeader = Join-Path $root 'kernel\core\include\kernel\framebuffer.h'
$docsFile = Join-Path $root 'docs\MULTI_MONITOR_V0_2_EXPERIMENT.md'

Assert-Match -Path $bootInfoHeader -Pattern 'GUIDEXOS_BOOTINFO_VERSION\s*=\s*2' -Message 'BootInfo version was not bumped for the framebuffer array handoff.'
Assert-Match -Path $bootInfoHeader -Pattern 'GUIDEXOS_MAX_FRAMEBUFFERS\s*=\s*8' -Message 'BootInfo max framebuffer bound is missing or incorrect.'
Assert-Match -Path $bootInfoHeader -Pattern 'struct FramebufferDescriptor' -Message 'FramebufferDescriptor is missing from BootInfo.'
Assert-Match -Path $bootInfoHeader -Pattern 'FramebufferCount' -Message 'FramebufferCount is missing from BootInfo.'
Assert-Match -Path $bootInfoHeader -Pattern 'FramebufferUniqueCount' -Message 'FramebufferUniqueCount is missing from BootInfo.'
Assert-Match -Path $bootInfoHeader -Pattern 'FramebufferDuplicateCount' -Message 'FramebufferDuplicateCount is missing from BootInfo.'
Assert-Match -Path $bootInfoHeader -Pattern 'FramebufferSuspiciousCount' -Message 'FramebufferSuspiciousCount is missing from BootInfo.'
Assert-Match -Path $bootInfoHeader -Pattern 'FramebufferDescriptors\[GUIDEXOS_MAX_FRAMEBUFFERS\]' -Message 'Framebuffer descriptor array is missing or unbounded.'
Assert-Match -Path $bootInfoHeader -Pattern 'FRAMEBUFFER_DESCRIPTOR_FLAG_PRIMARY' -Message 'Framebuffer descriptor flags are missing.'
Assert-Match -Path $bootInfoHeader -Pattern 'FRAMEBUFFER_DESCRIPTOR_FLAG_DUPLICATE' -Message 'Duplicate framebuffer descriptor flag is missing.'
Assert-Match -Path $bootInfoHeader -Pattern 'FRAMEBUFFER_DESCRIPTOR_FLAG_ALIAS' -Message 'Alias framebuffer descriptor flag is missing.'
Assert-Match -Path $bootInfoHeader -Pattern 'FRAMEBUFFER_DESCRIPTOR_FLAG_SAME_AS_PRIMARY' -Message 'Same-as-primary framebuffer descriptor flag is missing.'
Assert-Match -Path $bootInfoHeader -Pattern 'FRAMEBUFFER_DESCRIPTOR_FLAG_SUSPICIOUS' -Message 'Suspicious framebuffer descriptor flag is missing.'
Assert-Match -Path $bootInfoHeader -Pattern 'guidexos_framebuffer_descriptor_identity_matches' -Message 'Framebuffer identity helper is missing.'
Assert-Match -Path $bootInfoHeader -Pattern 'guidexos_framebuffer_descriptor_base_size_matches' -Message 'Framebuffer base/size helper is missing.'
Assert-Match -Path $bootInfoHeader -Pattern 'guidexos_classify_framebuffer_descriptors' -Message 'Framebuffer classification helper is missing.'

Assert-Match -Path $bootloaderMain -Pattern 'PopulateGopFramebufferDiagnostics' -Message 'UEFI GOP framebuffer diagnostic population helper is missing.'
Assert-Match -Path $bootloaderMain -Pattern 'LocateHandleBuffer' -Message 'UEFI GOP handle enumeration is missing.'
Assert-Match -Path $bootloaderMain -Pattern 'HandleProtocol' -Message 'UEFI GOP handle querying is missing.'
Assert-Match -Path $bootloaderMain -Pattern 'FramebufferCount = 1u' -Message 'Bootloader does not seed framebufferCount from the primary GOP framebuffer.'
Assert-Match -Path $bootloaderMain -Pattern 'FramebufferUniqueCount = classification\.UniqueCount' -Message 'Bootloader does not populate unique framebuffer count.'
Assert-Match -Path $bootloaderMain -Pattern 'FramebufferDuplicateCount = classification\.DuplicateCount' -Message 'Bootloader does not populate duplicate framebuffer count.'
Assert-Match -Path $bootloaderMain -Pattern 'FramebufferSuspiciousCount = classification\.SuspiciousCount' -Message 'Bootloader does not populate suspicious framebuffer count.'
Assert-Match -Path $bootloaderMain -Pattern 'guidexos_classify_framebuffer_descriptors' -Message 'Bootloader does not classify framebuffer descriptors.'
Assert-Match -Path $bootloaderMain -Pattern 'LogFramebufferDescriptorStatus' -Message 'Bootloader per-descriptor framebuffer status logging is missing.'
Assert-Match -Path $bootloaderMain -Pattern 'LogFramebufferDiagnosticsSummary' -Message 'Bootloader framebuffer summary logging is missing.'
Assert-Match -Path $bootloaderMain -Pattern 'GOP FramebufferCount=' -Message 'Bootloader diagnostic summary is missing.'
Assert-Match -Path $bootloaderMain -Pattern 'raw GOP framebuffer descriptor' -Message 'Bootloader still implies extra GOP handles are separate monitors.'

Assert-Match -Path $kernelMain -Pattern 'FramebufferCount' -Message 'Kernel does not read BootInfo framebufferCount.'
Assert-Match -Path $kernelMain -Pattern 'FramebufferDescriptors\[i\]' -Message 'Kernel does not iterate BootInfo framebuffer descriptors.'
Assert-Match -Path $kernelMain -Pattern 'log_framebuffer_descriptor' -Message 'Kernel framebuffer logging helper is missing.'
Assert-Match -Path $kernelMain -Pattern 'log_framebuffer_summary' -Message 'Kernel framebuffer summary logging helper is missing.'
Assert-Match -Path $kernelMain -Pattern 'DiagnosticFramebufferInventorySummary' -Message 'Kernel does not use the cached diagnostic framebuffer inventory summary.'
Assert-Match -Path $kernelMain -Pattern 'diagnostic_framebuffer_inventory_summary' -Message 'Kernel does not read the cached diagnostic framebuffer inventory summary.'
Assert-Match -Path $kernelMain -Pattern 'DuplicateFramebufferCount' -Message 'Kernel does not read BootInfo duplicate framebuffer count.'
Assert-Match -Path $kernelMain -Pattern 'SuspiciousFramebufferCount' -Message 'Kernel does not read BootInfo suspicious framebuffer count.'
Assert-Match -Path $kernelMain -Pattern 'ActiveFramebufferTargetCount' -Message 'Kernel does not report the active framebuffer target count.'
Assert-Match -Path $kernelMain -Pattern 'DisabledDiagnosticFramebufferCandidateCount' -Message 'Kernel does not report the disabled diagnostic framebuffer candidate count.'
Assert-Match -Path $kernelMain -Pattern 'status=' -Message 'Kernel framebuffer descriptor status logging is missing.'
Assert-Match -Path $kernelMain -Pattern 'primary remains render target' -Message 'Kernel TODO / primary-only rendering note is missing.'
Assert-Match -Path $kernelFramebufferHeader -Pattern 'DiagnosticFramebufferInventorySummary' -Message 'Kernel framebuffer inventory summary is missing.'
Assert-Match -Path $kernelFramebufferHeader -Pattern 'diagnostic_framebuffer_candidate_count' -Message 'Kernel framebuffer candidate count helper is missing.'
Assert-Match -Path $kernelFramebufferHeader -Pattern 'diagnostic_framebuffer_candidate\(' -Message 'Kernel framebuffer candidate accessor is missing.'
Assert-Match -Path $kernelFramebufferHeader -Pattern 'has_diagnostic_framebuffer_inventory' -Message 'Kernel framebuffer inventory presence helper is missing.'
Assert-Match -Path $docsFile -Pattern 'Framebuffer Array Handoff' -Message 'Documentation section for framebuffer array handoff is missing.'
Assert-Match -Path $docsFile -Pattern 'Bare-Metal Display Target Inventory' -Message 'Documentation section for bare-metal display target inventory is missing.'
Assert-Match -Path $docsFile -Pattern 'FramebufferUniqueCount' -Message 'Documentation does not describe deduplicated framebuffer counts.'
Assert-Match -Path $docsFile -Pattern 'ActiveRenderTargetCount=1' -Message 'Documentation does not describe the primary-only active target count.'
Assert-Match -Path $docsFile -Pattern 'DisabledCandidateCount=0' -Message 'Documentation does not describe the disabled candidate count.'
Assert-Match -Path $docsFile -Pattern 'duplicate alias same-as-primary' -Message 'Documentation does not call out the alias classification.'
Assert-Match -Path $docsFile -Pattern 'Deduplication prevents guideXOS from treating aliased GOP handles as separate monitors' -Message 'Documentation does not clarify the deduplication purpose.'

Write-Host '[SMOKE] BootInfo framebuffer array source checks passed.'
