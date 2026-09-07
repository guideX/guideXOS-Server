[CmdletBinding()]
param(
    [string]$LlvmRoot = 'C:\Program Files\LLVM\bin',
    [string]$OutputDirectory = ''
)

$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) { $OutputDirectory = Join-Path $repoRoot 'out\aarch64-phase4' }
else { $OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory) }
$resolvedRoot = $repoRoot.TrimEnd('\')
$resolvedOutput = $OutputDirectory.TrimEnd('\')
if (!$resolvedOutput.StartsWith($resolvedRoot + '\out\', [StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to remove output outside repository out/: $resolvedOutput"
}

$clang = Join-Path $LlvmRoot 'clang++.exe'
$hostCxx = 'C:\mingw64\bin\g++.exe'
if (!(Test-Path -LiteralPath $hostCxx -PathType Leaf)) { $hostCxx = $clang }
$lldLink = Join-Path $LlvmRoot 'lld-link.exe'
$ld = Join-Path $LlvmRoot 'ld.lld.exe'
$llvmReadobj = Join-Path $LlvmRoot 'llvm-readobj.exe'
foreach ($tool in @($clang, $lldLink, $ld, $llvmReadobj)) {
    if (!(Test-Path -LiteralPath $tool -PathType Leaf)) { throw "Required LLVM tool not found: $tool" }
}

function Invoke-Checked {
    param([string]$FilePath, [string[]]$Arguments)
    Write-Host "      $([IO.Path]::GetFileName($FilePath)) $($Arguments -join ' ')" -ForegroundColor DarkGray
    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) { throw "Command failed ($LASTEXITCODE): $FilePath" }
}
function Read-U16([byte[]]$Bytes, [int]$Offset) { return [int]$Bytes[$Offset] -bor ([int]$Bytes[$Offset + 1] -shl 8) }
function Read-U32([byte[]]$Bytes, [int]$Offset) {
    return [uint32]$Bytes[$Offset] -bor ([uint32]$Bytes[$Offset + 1] -shl 8) -bor
        ([uint32]$Bytes[$Offset + 2] -shl 16) -bor ([uint32]$Bytes[$Offset + 3] -shl 24)
}
function Test-PeArm64([string]$Path) {
    $b = [IO.File]::ReadAllBytes($Path)
    if ($b.Length -lt 64) { return $false }
    $p = [int](Read-U32 $b 60)
    return $p -ge 0 -and $p + 6 -le $b.Length -and $b[$p] -eq 0x50 -and $b[$p + 1] -eq 0x45 -and
        $b[$p + 2] -eq 0 -and $b[$p + 3] -eq 0 -and (Read-U16 $b ($p + 4)) -eq 0xaa64
}
function Test-ElfAarch64([string]$Path) {
    $b = [IO.File]::ReadAllBytes($Path)
    return $b.Length -ge 20 -and $b[0] -eq 0x7f -and $b[1] -eq 0x45 -and $b[2] -eq 0x4c -and
        $b[3] -eq 0x46 -and $b[4] -eq 2 -and $b[5] -eq 1 -and (Read-U16 $b 18) -eq 183
}

if (Test-Path -LiteralPath $resolvedOutput) { Remove-Item -LiteralPath $resolvedOutput -Recurse -Force }
$null = New-Item -ItemType Directory -Path (Join-Path $OutputDirectory 'esp\EFI\BOOT') -Force

$loaderObject = Join-Path $OutputDirectory 'phase4_loader.obj'
$efiPath = Join-Path $OutputDirectory 'BOOTAA64.EFI'
$loaderSource = Join-Path $repoRoot 'guideXOSBootLoader\aarch64\phase1_loader.cpp'
$loaderFlags = @('--target=aarch64-pc-windows-msvc', '-DGXOS_AARCH64_PHASE4', '-O2', '-ffreestanding', '-fno-builtin',
    '-fno-stack-protector', '-fno-exceptions', '-fno-rtti', '-fno-unwind-tables', '-fno-asynchronous-unwind-tables',
    '-fno-ident', '-I', (Join-Path $repoRoot 'guideXOSBootLoader'), '-I', $repoRoot, '-c', $loaderSource, '-o', $loaderObject)
Write-Host '[1/8] Compiling Phase 4 AArch64 UEFI loader...' -ForegroundColor Yellow
Invoke-Checked $clang $loaderFlags
Write-Host '[2/8] Linking BOOTAA64.EFI...' -ForegroundColor Yellow
Invoke-Checked $lldLink @('/subsystem:efi_application', '/entry:efi_main', '/nodefaultlib', '/machine:arm64', '/timestamp:0', "/out:$efiPath", $loaderObject)
if (!(Test-PeArm64 $efiPath)) { throw 'BOOTAA64.EFI failed PE/COFF machine verification' }

$kernelSources = @(
    'kernel\arch\arm64\phase3_entry.S', 'kernel\arch\arm64\phase3_context.S',
    'kernel\arch\arm64\phase3_register_test.S', 'kernel\arch\arm64\phase3_preemptive_worker.S',
    'kernel\arch\arm64\phase3_serial.cpp', 'kernel\arch\arm64\phase4_serial.cpp',
    'kernel\arch\arm64\phase2_platform.cpp', 'kernel\arch\arm64\phase2_mmu.cpp',
    'kernel\arch\arm64\phase2_gic.cpp', 'kernel\arch\arm64\phase3_timer.cpp',
    'kernel\arch\arm64\phase3_arch.cpp', 'kernel\arch\arm64\phase3_exceptions.cpp',
    'kernel\arch\arm64\phase4_main.cpp', 'kernel\core\common_scheduler.cpp',
    'kernel\core\common_kernel_entry.cpp', 'kernel\core\common_boot_info.cpp',
    'kernel\core\common_physical_allocator.cpp', 'kernel\core\irq_registry.cpp',
    'kernel\core\cxx_runtime.cpp', 'kernel\core\block_device.cpp',
    'kernel\core\ramdisk.cpp', 'kernel\core\vfs.cpp', 'kernel\core\fs_fat.cpp',
    'kernel\core\fs_ext4.cpp'
)
$kernelObjects = @()
$kernelFlags = @('--target=aarch64-none-elf', '-std=c++14', '-march=armv8-a', '-O2', '-ffreestanding', '-nostdlib', '-nostdinc++',
    '-fno-builtin', '-fno-stack-protector', '-fno-exceptions', '-fno-rtti', '-fno-unwind-tables',
    '-fno-asynchronous-unwind-tables', '-fno-pic', '-fno-pie', '-mcmodel=small', '-mgeneral-regs-only',
    '-mstrict-align', '-mno-outline-atomics', '-DGXOS_AARCH64_PHASE4', '-I', $repoRoot,
    '-I', (Join-Path $repoRoot 'kernel\core\include'), '-I', (Join-Path $repoRoot 'kernel\arch\arm64\include'))
Write-Host '[3/8] Compiling common memory, heap, VFS, and ARM64 services...' -ForegroundColor Yellow
foreach ($relativeSource in $kernelSources) {
    $source = Join-Path $repoRoot $relativeSource
    $leaf = [IO.Path]::GetFileName($relativeSource)
    $object = Join-Path $OutputDirectory (($leaf -replace '\.S$', '.o' -replace '\.cpp$', '.o'))
    Invoke-Checked $clang ($kernelFlags + @('-c', $source, '-o', $object))
    $kernelObjects += $object
}

Write-Host '[4/8] Linking Phase 4 kernel.elf...' -ForegroundColor Yellow
$kernelPath = Join-Path $OutputDirectory 'kernel.elf'
Invoke-Checked $ld (@('-m', 'aarch64elf', '-T', (Join-Path $repoRoot 'kernel\arch\arm64\phase3_linker.ld'), '-o', $kernelPath) + $kernelObjects)
if (!(Test-ElfAarch64 $kernelPath)) { throw 'kernel.elf failed ELF64 EM_AARCH64 verification' }

Write-Host '[5/8] Running common allocator/VFS negative controls...' -ForegroundColor Yellow
$hostTest = Join-Path $OutputDirectory 'aarch64-phase4-host-tests.exe'
Invoke-Checked $hostCxx @('-std=c++17', '-O2', '-iquote', (Join-Path $repoRoot 'kernel\core\include'),
    (Join-Path $repoRoot 'tests\aarch64_phase4_host_tests.cpp'),
    (Join-Path $repoRoot 'kernel\core\common_boot_info.cpp'),
    (Join-Path $repoRoot 'kernel\core\common_physical_allocator.cpp'), '-o', $hostTest)
& $hostTest
if ($LASTEXITCODE -ne 0) { throw 'Phase 4 host negative controls failed' }

Write-Host '[6/8] Staging EFI, kernel, and deterministic FAT32 ramdisk...' -ForegroundColor Yellow
Copy-Item -LiteralPath $efiPath -Destination (Join-Path $OutputDirectory 'esp\EFI\BOOT\BOOTAA64.EFI') -Force
Copy-Item -LiteralPath $kernelPath -Destination (Join-Path $OutputDirectory 'esp\kernel.elf') -Force
& (Join-Path $PSScriptRoot 'stage-aarch64-phase4-ramdisk.ps1') -SourceImage (Join-Path $repoRoot 'ESP\ramdisk.img') -OutputImage (Join-Path $OutputDirectory 'esp\ramdisk.img')
if ($LASTEXITCODE -ne 0) { throw 'Phase 4 ramdisk staging failed' }

Write-Host '[7/8] Recording ELF headers...' -ForegroundColor Yellow
& $llvmReadobj '--file-headers' '--program-headers' $kernelPath | Set-Content -LiteralPath (Join-Path $OutputDirectory 'kernel-llvm-readobj.txt') -Encoding ascii
if ($LASTEXITCODE -ne 0) { throw 'llvm-readobj failed for Phase 4 kernel.elf' }
Write-Host '[8/8] Phase 4 artifacts' -ForegroundColor Yellow
foreach ($artifact in @($efiPath, $kernelPath, (Join-Path $OutputDirectory 'esp\ramdisk.img'))) {
    $item = Get-Item -LiteralPath $artifact
    Write-Host "      $($item.Name): $($item.Length) bytes sha256=$((Get-FileHash -LiteralPath $artifact -Algorithm SHA256).Hash.ToLowerInvariant())" -ForegroundColor Cyan
}
Write-Host "      ESP: $([IO.Path]::GetFullPath((Join-Path $OutputDirectory 'esp')))." -ForegroundColor Cyan
Write-Host 'Build complete. Use scripts\test-aarch64-phase4.ps1 for three fresh boots.' -ForegroundColor Green
