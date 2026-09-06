[CmdletBinding()]
param(
    [string]$LlvmRoot = 'C:\Program Files\LLVM\bin',
    [string]$OutputDirectory = ''
)

$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $repoRoot 'out\aarch64-phase2'
} else {
    $OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
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

function Read-U16([byte[]]$Bytes, [int]$Offset) {
    return ([int]$Bytes[$Offset] -bor ([int]$Bytes[$Offset + 1] -shl 8))
}
function Read-U32([byte[]]$Bytes, [int]$Offset) {
    return ([uint32]$Bytes[$Offset] -bor ([uint32]$Bytes[$Offset + 1] -shl 8) -bor
        ([uint32]$Bytes[$Offset + 2] -shl 16) -bor ([uint32]$Bytes[$Offset + 3] -shl 24))
}
function Test-PeArm64([string]$Path) {
    $bytes = [IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 64) { return $false }
    $peOffset = [int](Read-U32 $bytes 60)
    return $peOffset -ge 0 -and $peOffset + 6 -le $bytes.Length -and
        $bytes[$peOffset] -eq 0x50 -and $bytes[$peOffset + 1] -eq 0x45 -and
        $bytes[$peOffset + 2] -eq 0 -and $bytes[$peOffset + 3] -eq 0 -and
        (Read-U16 $bytes ($peOffset + 4)) -eq 0xaa64
}
function Test-ElfAarch64([string]$Path) {
    $bytes = [IO.File]::ReadAllBytes($Path)
    return $bytes.Length -ge 20 -and $bytes[0] -eq 0x7f -and $bytes[1] -eq 0x45 -and
        $bytes[2] -eq 0x4c -and $bytes[3] -eq 0x46 -and $bytes[4] -eq 2 -and
        $bytes[5] -eq 1 -and (Read-U16 $bytes 18) -eq 183
}

Write-Host '============================================' -ForegroundColor Cyan
Write-Host '  guideXOS AARCH64 Phase 2 kernel foundation' -ForegroundColor Cyan
Write-Host '============================================' -ForegroundColor Cyan
Write-Host "  LLVM root: $LlvmRoot"
Write-Host '  UEFI target: aarch64-pc-windows-msvc'
Write-Host '  Kernel target: aarch64-none-elf'
Write-Host '  QEMU platform: virt, GICv2, cortex-a53'
Write-Host "  Output: $OutputDirectory"

$resolvedRoot = [IO.Path]::GetFullPath($repoRoot).TrimEnd('\')
$resolvedOutput = [IO.Path]::GetFullPath($OutputDirectory).TrimEnd('\')
if (!$resolvedOutput.StartsWith($resolvedRoot + '\out\', [StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to remove output outside repository out/: $resolvedOutput"
}
if (Test-Path -LiteralPath $resolvedOutput) { Remove-Item -LiteralPath $resolvedOutput -Recurse -Force }
$null = New-Item -ItemType Directory -Path $OutputDirectory -Force
$null = New-Item -ItemType Directory -Path (Join-Path $OutputDirectory 'esp\EFI\BOOT') -Force

$loaderSource = Join-Path $repoRoot 'guideXOSBootLoader\aarch64\phase1_loader.cpp'
$loaderObject = Join-Path $OutputDirectory 'phase2_loader.obj'
$efiPath = Join-Path $OutputDirectory 'BOOTAA64.EFI'
$kernelSources = @(
    'phase2_entry.S', 'phase2_serial.cpp', 'phase2_platform.cpp', 'phase2_mmu.cpp',
    'phase2_memory.cpp', 'phase2_gic.cpp', 'phase2_timer.cpp', 'phase2_exceptions.cpp', 'phase2_main.cpp'
)
$kernelObjects = @()

Write-Host '[1/7] Compiling Phase 2 AArch64 UEFI loader...' -ForegroundColor Yellow
$loaderFlags = @('--target=aarch64-pc-windows-msvc', '-DGXOS_AARCH64_PHASE2', '-O2', '-ffreestanding', '-fno-builtin',
    '-fno-stack-protector', '-fno-exceptions', '-fno-rtti', '-fno-unwind-tables', '-fno-asynchronous-unwind-tables',
    '-fno-ident', '-I', (Join-Path $repoRoot 'guideXOSBootLoader'), '-I', $repoRoot, '-c', $loaderSource, '-o', $loaderObject)
Invoke-Checked $clang $loaderFlags

Write-Host '[2/7] Linking BOOTAA64.EFI and verifying PE machine 0xAA64...' -ForegroundColor Yellow
Invoke-Checked $lldLink @('/subsystem:efi_application', '/entry:efi_main', '/nodefaultlib', '/machine:arm64', '/timestamp:0',
    "/out:$efiPath", $loaderObject)
if (!(Test-PeArm64 $efiPath)) { throw 'BOOTAA64.EFI failed PE/COFF machine verification' }

$kernelFlags = @('--target=aarch64-none-elf', '-march=armv8-a', '-O2', '-ffreestanding', '-nostdlib', '-nostdinc++',
    '-fno-builtin', '-fno-stack-protector', '-fno-exceptions', '-fno-rtti', '-fno-unwind-tables',
    '-fno-asynchronous-unwind-tables', '-fno-pic', '-fno-pie', '-mcmodel=small', '-mgeneral-regs-only',
    '-mstrict-align', '-I', $repoRoot)
Write-Host '[3/7] Compiling guideXOS-owned AArch64 architecture services...' -ForegroundColor Yellow
foreach ($sourceName in $kernelSources) {
    $source = Join-Path $repoRoot ("kernel\arch\arm64\{0}" -f $sourceName)
    $object = Join-Path $OutputDirectory ($sourceName -replace '\.S$', '.o' -replace '\.cpp$', '.o')
    Invoke-Checked $clang ($kernelFlags + @('-c', $source, '-o', $object))
    $kernelObjects += $object
}

Write-Host '[4/7] Linking controlled Phase 2 kernel.elf...' -ForegroundColor Yellow
$kernelPath = Join-Path $OutputDirectory 'kernel.elf'
$linkerScript = Join-Path $repoRoot 'kernel\arch\arm64\phase2_linker.ld'
Invoke-Checked $ld (@('-m', 'aarch64elf', '-T', $linkerScript, '-o', $kernelPath) + $kernelObjects)
if (!(Test-ElfAarch64 $kernelPath)) { throw 'kernel.elf failed ELF64 EM_AARCH64 verification' }

Write-Host '[5/7] Running host-side malformed-input validation...' -ForegroundColor Yellow
$hostTest = Join-Path $OutputDirectory 'aarch64-phase2-host-tests.exe'
$hostSource = Join-Path $repoRoot 'tests\aarch64_phase2_host_tests.cpp'
$platformSource = Join-Path $repoRoot 'kernel\arch\arm64\phase2_platform.cpp'
Invoke-Checked $hostCxx @('-std=c++17', '-O2', '-I', $repoRoot, $hostSource, $platformSource, '-o', $hostTest)
& $hostTest
if ($LASTEXITCODE -ne 0) { throw 'Phase 2 host validation tests failed' }

Write-Host '[6/7] Staging EFI\BOOT\BOOTAA64.EFI and kernel.elf...' -ForegroundColor Yellow
Copy-Item -LiteralPath $efiPath -Destination (Join-Path $OutputDirectory 'esp\EFI\BOOT\BOOTAA64.EFI') -Force
Copy-Item -LiteralPath $kernelPath -Destination (Join-Path $OutputDirectory 'esp\kernel.elf') -Force
$readobj = & $llvmReadobj '--file-headers' '--program-headers' $kernelPath | Out-String
if ($LASTEXITCODE -ne 0) { throw 'llvm-readobj failed for Phase 2 kernel.elf' }
$readobj | Set-Content -LiteralPath (Join-Path $OutputDirectory 'kernel-llvm-readobj.txt') -Encoding ascii

Write-Host '[7/7] Artifact summary' -ForegroundColor Yellow
foreach ($artifact in @($efiPath, $kernelPath)) {
    $item = Get-Item -LiteralPath $artifact
    $hash = (Get-FileHash -LiteralPath $artifact -Algorithm SHA256).Hash.ToLowerInvariant()
    Write-Host "      $($item.Name): $($item.Length) bytes sha256=$hash" -ForegroundColor Cyan
}
Write-Host "      ESP: $([IO.Path]::GetFullPath((Join-Path $OutputDirectory 'esp')))." -ForegroundColor Cyan
Write-Host 'Build complete. Use scripts\test-aarch64-phase2.ps1 for three fresh boots.' -ForegroundColor Green
