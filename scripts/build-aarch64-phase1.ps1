[CmdletBinding()]
param(
    [string]$LlvmRoot = 'C:\Program Files\LLVM\bin',
    [string]$OutputDirectory = ''
)

$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $repoRoot 'out\aarch64-phase1'
} else {
    $OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
}

$clang = Join-Path $LlvmRoot 'clang++.exe'
$lldLink = Join-Path $LlvmRoot 'lld-link.exe'
$ld = Join-Path $LlvmRoot 'ld.lld.exe'
$llvmObjcopy = Join-Path $LlvmRoot 'llvm-objcopy.exe'
$llvmReadobj = Join-Path $LlvmRoot 'llvm-readobj.exe'
foreach ($tool in @($clang, $lldLink, $ld, $llvmObjcopy, $llvmReadobj)) {
    if (!(Test-Path -LiteralPath $tool -PathType Leaf)) {
        throw "Required LLVM tool not found: $tool"
    }
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
    if ($peOffset -lt 0 -or $peOffset + 6 -gt $bytes.Length) { return $false }
    if ($bytes[$peOffset] -ne 0x50 -or $bytes[$peOffset + 1] -ne 0x45 -or
        $bytes[$peOffset + 2] -ne 0 -or $bytes[$peOffset + 3] -ne 0) { return $false }
    return (Read-U16 $bytes ($peOffset + 4)) -eq 0xaa64
}

function Test-ElfAarch64([string]$Path) {
    $bytes = [IO.File]::ReadAllBytes($Path)
    return $bytes.Length -ge 20 -and $bytes[0] -eq 0x7f -and $bytes[1] -eq 0x45 -and
        $bytes[2] -eq 0x4c -and $bytes[3] -eq 0x46 -and $bytes[4] -eq 2 -and
        $bytes[5] -eq 1 -and (Read-U16 $bytes 18) -eq 183
}

Write-Host '============================================' -ForegroundColor Cyan
Write-Host '  guideXOS AARCH64 Phase 1 native boot build' -ForegroundColor Cyan
Write-Host '============================================' -ForegroundColor Cyan
Write-Host "  LLVM root: $LlvmRoot"
Write-Host '  UEFI target: aarch64-pc-windows-msvc'
Write-Host '  Kernel target: aarch64-none-elf (LLVM normalizes to aarch64-unknown-none-elf)'
Write-Host "  Output: $OutputDirectory"

if (Test-Path -LiteralPath $OutputDirectory) {
    $resolvedRoot = [IO.Path]::GetFullPath($repoRoot).TrimEnd('\')
    $resolvedOutput = [IO.Path]::GetFullPath($OutputDirectory).TrimEnd('\')
    if (!$resolvedOutput.StartsWith($resolvedRoot + '\out\', [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to remove output outside repository out/: $resolvedOutput"
    }
    Remove-Item -LiteralPath $resolvedOutput -Recurse -Force
}
$null = New-Item -ItemType Directory -Path $OutputDirectory -Force
$null = New-Item -ItemType Directory -Path (Join-Path $OutputDirectory 'esp\EFI\BOOT') -Force

$loaderSource = Join-Path $repoRoot 'guideXOSBootLoader\aarch64\phase1_loader.cpp'
$loaderObject = Join-Path $OutputDirectory 'phase1_loader.obj'
$efiPath = Join-Path $OutputDirectory 'BOOTAA64.EFI'
$kernelEntrySource = Join-Path $repoRoot 'kernel\arch\arm64\phase1_entry.S'
$kernelSerialSource = Join-Path $repoRoot 'kernel\arch\arm64\phase1_serial.cpp'
$kernelMainSource = Join-Path $repoRoot 'kernel\arch\arm64\phase1.cpp'
$kernelEntryObject = Join-Path $OutputDirectory 'phase1_entry.o'
$kernelSerialObject = Join-Path $OutputDirectory 'phase1_serial.o'
$kernelMainObject = Join-Path $OutputDirectory 'phase1.o'
$kernelPath = Join-Path $OutputDirectory 'kernel.elf'
$linkerScript = Join-Path $repoRoot 'kernel\arch\arm64\phase1_linker.ld'

$loaderFlags = @(
    '--target=aarch64-pc-windows-msvc', '-O2', '-ffreestanding', '-fno-builtin',
    '-fno-stack-protector', '-fno-exceptions', '-fno-rtti', '-fno-unwind-tables',
    '-fno-asynchronous-unwind-tables', '-fno-ident', '-I', (Join-Path $repoRoot 'guideXOSBootLoader'),
    '-I', $repoRoot, '-c', $loaderSource, '-o', $loaderObject
)
Write-Host '[1/6] Compiling AArch64 UEFI loader...' -ForegroundColor Yellow
Invoke-Checked $clang $loaderFlags

Write-Host '[2/6] Linking BOOTAA64.EFI...' -ForegroundColor Yellow
$efiLinkFlags = @(
    '/subsystem:efi_application', '/entry:efi_main', '/nodefaultlib', '/machine:arm64', '/timestamp:0',
    "/out:$efiPath", $loaderObject
)
Invoke-Checked $lldLink $efiLinkFlags
if (!(Test-PeArm64 $efiPath)) { throw 'BOOTAA64.EFI failed PE/COFF machine verification (expected 0xAA64)' }
Write-Host '      PE/COFF machine: AArch64 (0xAA64)' -ForegroundColor Green

$kernelFlags = @(
    '--target=aarch64-none-elf', '-march=armv8-a', '-O2', '-ffreestanding', '-nostdlib',
    '-nostdinc++', '-fno-builtin', '-fno-stack-protector', '-fno-exceptions', '-fno-rtti',
    '-fno-unwind-tables', '-fno-asynchronous-unwind-tables', '-fno-pic', '-fno-pie',
    '-mcmodel=small', '-mgeneral-regs-only', '-mstrict-align', '-I', $repoRoot
)
Write-Host '[3/6] Compiling freestanding AArch64 kernel...' -ForegroundColor Yellow
Invoke-Checked $clang ($kernelFlags + @('-c', $kernelEntrySource, '-o', $kernelEntryObject))
Invoke-Checked $clang ($kernelFlags + @('-c', $kernelSerialSource, '-o', $kernelSerialObject))
Invoke-Checked $clang ($kernelFlags + @('-c', $kernelMainSource, '-o', $kernelMainObject))

Write-Host '[4/6] Linking kernel.elf at physical/virtual address 0x40000000...' -ForegroundColor Yellow
$kernelLinkFlags = @('-m', 'aarch64elf', '-T', $linkerScript, '-o', $kernelPath,
    $kernelEntryObject, $kernelSerialObject, $kernelMainObject)
Invoke-Checked $ld $kernelLinkFlags
if (!(Test-ElfAarch64 $kernelPath)) { throw 'kernel.elf failed ELF64 EM_AARCH64 verification' }
Write-Host '      ELF machine: AArch64 (EM_AARCH64 = 183)' -ForegroundColor Green

Write-Host '[5/6] Inspecting and staging the FAT ESP directory...' -ForegroundColor Yellow
$stagedEfi = Join-Path $OutputDirectory 'esp\EFI\BOOT\BOOTAA64.EFI'
$stagedKernel = Join-Path $OutputDirectory 'esp\kernel.elf'
Copy-Item -LiteralPath $efiPath -Destination $stagedEfi -Force
Copy-Item -LiteralPath $kernelPath -Destination $stagedKernel -Force
$readobj = & $llvmReadobj '--file-headers' '--program-headers' $kernelPath | Out-String
if ($LASTEXITCODE -ne 0) { throw 'llvm-readobj failed for kernel.elf' }
$readobj | Set-Content -LiteralPath (Join-Path $OutputDirectory 'kernel-llvm-readobj.txt') -Encoding ascii

Write-Host '[6/6] Artifact summary' -ForegroundColor Yellow
foreach ($artifact in @($efiPath, $kernelPath)) {
    $item = Get-Item -LiteralPath $artifact
    $hash = (Get-FileHash -LiteralPath $artifact -Algorithm SHA256).Hash.ToLowerInvariant()
    Write-Host "      $($item.Name): $($item.Length) bytes sha256=$hash" -ForegroundColor Cyan
}
Write-Host "      ESP: $([IO.Path]::GetFullPath((Join-Path $OutputDirectory 'esp')))" -ForegroundColor Cyan
Write-Host 'Build complete. Use scripts\run-aarch64-phase1.ps1 or scripts\test-aarch64-phase1.ps1.' -ForegroundColor Green
