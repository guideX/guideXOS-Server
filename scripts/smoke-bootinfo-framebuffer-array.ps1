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
$docsFile = Join-Path $root 'docs\MULTI_MONITOR_V0_2_EXPERIMENT.md'

Assert-Match -Path $bootInfoHeader -Pattern 'GUIDEXOS_BOOTINFO_VERSION\s*=\s*2' -Message 'BootInfo version was not bumped for the framebuffer array handoff.'
Assert-Match -Path $bootInfoHeader -Pattern 'GUIDEXOS_MAX_FRAMEBUFFERS\s*=\s*8' -Message 'BootInfo max framebuffer bound is missing or incorrect.'
Assert-Match -Path $bootInfoHeader -Pattern 'struct FramebufferDescriptor' -Message 'FramebufferDescriptor is missing from BootInfo.'
Assert-Match -Path $bootInfoHeader -Pattern 'FramebufferCount' -Message 'FramebufferCount is missing from BootInfo.'
Assert-Match -Path $bootInfoHeader -Pattern 'FramebufferDescriptors\[GUIDEXOS_MAX_FRAMEBUFFERS\]' -Message 'Framebuffer descriptor array is missing or unbounded.'
Assert-Match -Path $bootInfoHeader -Pattern 'FRAMEBUFFER_DESCRIPTOR_FLAG_PRIMARY' -Message 'Framebuffer descriptor flags are missing.'

Assert-Match -Path $bootloaderMain -Pattern 'PopulateGopFramebufferDiagnostics' -Message 'UEFI GOP framebuffer diagnostic population helper is missing.'
Assert-Match -Path $bootloaderMain -Pattern 'LocateHandleBuffer' -Message 'UEFI GOP handle enumeration is missing.'
Assert-Match -Path $bootloaderMain -Pattern 'HandleProtocol' -Message 'UEFI GOP handle querying is missing.'
Assert-Match -Path $bootloaderMain -Pattern 'FramebufferCount = 1u' -Message 'Bootloader does not seed framebufferCount from the primary GOP framebuffer.'
Assert-Match -Path $bootloaderMain -Pattern 'FramebufferDescriptors\[discoveredIndex\]' -Message 'Bootloader does not append GOP framebuffer descriptors.'
Assert-Match -Path $bootloaderMain -Pattern 'Diagnostic framebuffer array exported' -Message 'Bootloader diagnostic summary is missing.'

Assert-Match -Path $kernelMain -Pattern 'FramebufferCount' -Message 'Kernel does not read BootInfo framebufferCount.'
Assert-Match -Path $kernelMain -Pattern 'FramebufferDescriptors\[i\]' -Message 'Kernel does not iterate BootInfo framebuffer descriptors.'
Assert-Match -Path $kernelMain -Pattern 'log_framebuffer_descriptor' -Message 'Kernel framebuffer logging helper is missing.'
Assert-Match -Path $kernelMain -Pattern 'primary remains render target' -Message 'Kernel TODO / primary-only rendering note is missing.'
Assert-Match -Path $docsFile -Pattern 'Framebuffer Array Handoff' -Message 'Documentation section for framebuffer array handoff is missing.'

Write-Host '[SMOKE] BootInfo framebuffer array source checks passed.'
