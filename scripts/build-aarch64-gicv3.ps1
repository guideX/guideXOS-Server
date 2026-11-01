[CmdletBinding()]
param(
    [string]$LlvmRoot = 'C:\Program Files\LLVM\bin',
    [string]$OutputDirectory = ''
)

$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $repoRoot 'out\aarch64-gicv3'
} else {
    $OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
}

$resolvedRoot = [IO.Path]::GetFullPath($repoRoot).TrimEnd('\')
$resolvedOutput = [IO.Path]::GetFullPath($OutputDirectory).TrimEnd('\')
if (!$resolvedOutput.StartsWith($resolvedRoot + '\out\', [StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to write GICv3 build outside repository out/: $resolvedOutput"
}

Write-Host 'Building the Phase 4 ARM64 kernel, including the production GICv3 driver and common IRQ registry.' -ForegroundColor Cyan
& (Join-Path $PSScriptRoot 'build-aarch64-phase4.ps1') -LlvmRoot $LlvmRoot -OutputDirectory $OutputDirectory
if ($LASTEXITCODE -ne 0) { throw 'AArch64 GICv3 freestanding build failed' }

$kernel = Join-Path $OutputDirectory 'kernel.elf'
$driverObject = Join-Path $OutputDirectory 'gicv3_controller.o'
foreach ($required in @($kernel, $driverObject)) {
    if (!(Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "GICv3 build artifact is missing: $required"
    }
}

$kernelHash = (Get-FileHash -LiteralPath $kernel -Algorithm SHA256).Hash.ToLowerInvariant()
Write-Host "ARM64 freestanding link: PASS kernel.elf sha256=$kernelHash" -ForegroundColor Green
Write-Host 'GICv3 build complete. No QEMU guest was launched.' -ForegroundColor Green
exit 0
