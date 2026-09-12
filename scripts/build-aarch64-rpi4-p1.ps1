[CmdletBinding()]
param(
    [string]$LlvmRoot = 'C:\Program Files\LLVM\bin',
    [string]$OutputDirectory = '',
    [string]$FirmwareDirectory = ''
)

$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $repoRoot 'out\aarch64-rpi4-p1'
}
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
$outPrefix = $repoRoot.TrimEnd('\') + '\out\'
if (!$OutputDirectory.StartsWith($outPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing output outside repository out/: $OutputDirectory"
}
if (![string]::IsNullOrWhiteSpace($FirmwareDirectory)) {
    $FirmwareDirectory = [IO.Path]::GetFullPath($FirmwareDirectory)
    if (!(Test-Path -LiteralPath $FirmwareDirectory -PathType Container)) {
        throw "Firmware directory not found: $FirmwareDirectory"
    }
}
if (Test-Path -LiteralPath $OutputDirectory) {
    Remove-Item -LiteralPath $OutputDirectory -Recurse -Force
}
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null

$clang = Join-Path $LlvmRoot 'clang++.exe'
$ld = Join-Path $LlvmRoot 'ld.lld.exe'
$lldLink = Join-Path $LlvmRoot 'lld-link.exe'
$readobj = Join-Path $LlvmRoot 'llvm-readobj.exe'
foreach ($tool in @($clang, $ld, $lldLink, $readobj)) {
    if (!(Test-Path -LiteralPath $tool -PathType Leaf)) {
        throw "Required LLVM tool not found: $tool"
    }
}
$hostCxx = 'C:\mingw64\bin\g++.exe'
if (!(Test-Path -LiteralPath $hostCxx -PathType Leaf)) {
    $hostCxx = 'g++.exe'
}
function Invoke-Checked {
    param([string]$FilePath, [string[]]$Arguments)
    Write-Host "      $([IO.Path]::GetFileName($FilePath)) $($Arguments -join ' ')" -ForegroundColor DarkGray
    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed ($LASTEXITCODE): $FilePath"
    }
}
function Read-U16([byte[]]$Bytes, [int]$Offset) {
    return [int]$Bytes[$Offset] -bor ([int]$Bytes[$Offset + 1] -shl 8)
}
function Read-U32([byte[]]$Bytes, [int]$Offset) {
    return [uint32]$Bytes[$Offset] -bor ([uint32]$Bytes[$Offset + 1] -shl 8) -bor
        ([uint32]$Bytes[$Offset + 2] -shl 16) -bor ([uint32]$Bytes[$Offset + 3] -shl 24)
}
function Test-PeArm64([string]$Path) {
    $bytes = [IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 64) { return $false }
    $offset = [int](Read-U32 $bytes 60)
    return $offset -ge 0 -and $offset + 6 -le $bytes.Length -and
        $bytes[$offset] -eq 0x50 -and $bytes[$offset + 1] -eq 0x45 -and
        (Read-U16 $bytes ($offset + 4)) -eq 0xaa64
}
function Test-ElfArm64([string]$Path) {
    $bytes = [IO.File]::ReadAllBytes($Path)
    return $bytes.Length -ge 20 -and $bytes[0] -eq 0x7f -and
        $bytes[1] -eq 0x45 -and $bytes[2] -eq 0x4c -and $bytes[3] -eq 0x46 -and
        $bytes[4] -eq 2 -and $bytes[5] -eq 1 -and $bytes[6] -eq 1 -and
        (Read-U16 $bytes 18) -eq 183
}

Write-Host '============================================' -ForegroundColor Cyan
Write-Host '  guideXOS Raspberry Pi 4 AARCH64 P1' -ForegroundColor Cyan
Write-Host '============================================' -ForegroundColor Cyan
Write-Host '  profile: DTB-verified BCM2711, single-core, UEFI, no physical media writes'

$loaderSource = Join-Path $repoRoot 'guideXOSBootLoader\aarch64\phase1_loader.cpp'
$loaderObject = Join-Path $OutputDirectory 'rpi4-p1-loader.obj'
$platformObject = Join-Path $OutputDirectory 'rpi4-p1-loader-platform.obj'
$efiPath = Join-Path $OutputDirectory 'BOOTAA64.EFI'
$loaderFlags = @(
    '--target=aarch64-pc-windows-msvc', '-std=c++14',
    '-DGXOS_AARCH64_PHASE2', '-DGXOS_AARCH64_RPI4_P1',
    '-O2', '-ffreestanding', '-fno-builtin', '-fno-stack-protector',
    '-fno-exceptions', '-fno-rtti', '-fno-unwind-tables',
    '-fno-asynchronous-unwind-tables', '-fno-ident',
    '-I', (Join-Path $repoRoot 'guideXOSBootLoader'), '-I', $repoRoot,
    '-c', $loaderSource, '-o', $loaderObject
)
Write-Host '[1/8] Compiling the DTB-aware ARM64 UEFI loader...' -ForegroundColor Yellow
Invoke-Checked $clang $loaderFlags
Invoke-Checked $clang @(
    '--target=aarch64-pc-windows-msvc', '-std=c++14',
    '-O2', '-ffreestanding', '-fno-builtin', '-fno-stack-protector',
    '-fno-exceptions', '-fno-rtti', '-fno-unwind-tables',
    '-fno-asynchronous-unwind-tables', '-fno-ident', '-I', $repoRoot,
    '-c', (Join-Path $repoRoot 'kernel\arch\arm64\phase2_platform.cpp'),
    '-o', $platformObject
)
Invoke-Checked $lldLink @(
    '/subsystem:efi_application', '/entry:efi_main', '/nodefaultlib',
    '/machine:arm64', '/timestamp:0', "/out:$efiPath",
    $loaderObject, $platformObject
)
if (!(Test-PeArm64 $efiPath)) { throw 'BOOTAA64.EFI is not an ARM64 EFI application' }

$kernelSources = @(
    'kernel\arch\arm64\phase3_entry.S',
    'kernel\arch\arm64\phase3_context.S',
    'kernel\arch\arm64\phase3_register_test.S',
    'kernel\arch\arm64\phase3_preemptive_worker.S',
    'kernel\arch\arm64\phase3_serial.cpp',
    'kernel\arch\arm64\phase2_platform.cpp',
    'kernel\arch\arm64\phase2_mmu.cpp',
    'kernel\arch\arm64\phase2_memory.cpp',
    'kernel\arch\arm64\phase2_gic.cpp',
    'kernel\arch\arm64\phase3_timer.cpp',
    'kernel\arch\arm64\phase3_arch.cpp',
    'kernel\arch\arm64\phase3_exceptions.cpp',
    'kernel\arch\arm64\phase3_main.cpp',
    'kernel\core\common_scheduler.cpp',
    'kernel\core\common_kernel_entry.cpp'
)
$kernelFlags = @(
    '--target=aarch64-none-elf', '-std=c++14', '-march=armv8-a', '-O2',
    '-ffreestanding', '-nostdlib', '-nostdinc++', '-fno-builtin',
    '-fno-stack-protector', '-fno-exceptions', '-fno-rtti',
    '-fno-unwind-tables', '-fno-asynchronous-unwind-tables',
    '-fno-pic', '-fno-pie', '-mcmodel=small', '-mgeneral-regs-only',
    '-mstrict-align', '-mno-outline-atomics', '-ffunction-sections',
    '-fdata-sections', '-DGXOS_AARCH64_PHASE2', '-DGXOS_AARCH64_RPI4_P1',
    '-I', $repoRoot, '-I', (Join-Path $repoRoot 'kernel\core\include'),
    '-I', (Join-Path $repoRoot 'kernel\arch\arm64\include')
)
$kernelObjects = @()
$index = 0
Write-Host '[2/8] Compiling common ARM64 architecture and scheduler services...' -ForegroundColor Yellow
foreach ($relative in $kernelSources) {
    $source = Join-Path $repoRoot $relative
    $object = Join-Path $OutputDirectory ('kernel-{0:D2}.o' -f $index)
    ++$index
    Invoke-Checked $clang ($kernelFlags + @('-c', $source, '-o', $object))
    $kernelObjects += $object
}
$kernelPath = Join-Path $OutputDirectory 'kernel.elf'
Write-Host '[3/8] Linking the fixed-address ARM64 P1 kernel...' -ForegroundColor Yellow
Invoke-Checked $ld (@('-m', 'aarch64elf', '-T',
    (Join-Path $repoRoot 'kernel\arch\arm64\phase3_linker.ld'),
    '--gc-sections', '-o', $kernelPath) + $kernelObjects)
if (!(Test-ElfArm64 $kernelPath)) { throw 'kernel.elf is not ELF64 EM_AARCH64' }

Write-Host '[4/8] Running BCM2711/QEMU/unknown DTB and range host controls...' -ForegroundColor Yellow
$hostTest = Join-Path $OutputDirectory 'aarch64-rpi4-p1-host-tests.exe'
Invoke-Checked $hostCxx @(
    '-std=c++17', '-Wall', '-Wextra', '-O2', '-iquote', $repoRoot,
    (Join-Path $repoRoot 'tests\aarch64_rpi4_p1_host_tests.cpp'),
    (Join-Path $repoRoot 'kernel\arch\arm64\phase2_platform.cpp'),
    '-o', $hostTest
)
Invoke-Checked $hostTest @()

Write-Host '[5/8] Recording loader and kernel headers...' -ForegroundColor Yellow
& $readobj '--file-headers' '--program-headers' $efiPath |
    Set-Content -LiteralPath (Join-Path $OutputDirectory 'BOOTAA64-llvm-readobj.txt') -Encoding ascii
if ($LASTEXITCODE -ne 0) { throw 'llvm-readobj failed for BOOTAA64.EFI' }
& $readobj '--file-headers' '--program-headers' '--sections' $kernelPath |
    Set-Content -LiteralPath (Join-Path $OutputDirectory 'kernel-llvm-readobj.txt') -Encoding ascii
if ($LASTEXITCODE -ne 0) { throw 'llvm-readobj failed for kernel.elf' }

Write-Host '[6/8] Staging the non-destructive boot tree...' -ForegroundColor Yellow
if (![string]::IsNullOrWhiteSpace($FirmwareDirectory)) {
    & (Join-Path $PSScriptRoot 'stage-aarch64-rpi4-p1.ps1') `
        -ArtifactDirectory $OutputDirectory `
        -OutputDirectory $OutputDirectory `
        -FirmwareDirectory $FirmwareDirectory
} else {
    & (Join-Path $PSScriptRoot 'stage-aarch64-rpi4-p1.ps1') `
        -ArtifactDirectory $OutputDirectory `
        -OutputDirectory $OutputDirectory
}
if ($LASTEXITCODE -ne 0) { throw 'AARCH64-P1 staging failed' }

Write-Host '[7/8] Artifact details...' -ForegroundColor Yellow
foreach ($artifact in @($efiPath, $kernelPath,
                        (Join-Path $OutputDirectory 'manifest.txt'),
                        (Join-Path $OutputDirectory 'hashes.txt'))) {
    $item = Get-Item -LiteralPath $artifact
    $hash = (Get-FileHash -LiteralPath $artifact -Algorithm SHA256).Hash.ToLowerInvariant()
    Write-Host "      $($item.Name): $($item.Length) bytes sha256=$hash" -ForegroundColor Cyan
}
Write-Host '[8/8] AARCH64 Raspberry Pi 4 P1 build complete.' -ForegroundColor Green
Write-Host "      boot tree: $(Join-Path $OutputDirectory 'boot')" -ForegroundColor Cyan
Write-Host '      physical outcome remains pending until a real BCM2711 board is tested.' -ForegroundColor Yellow
