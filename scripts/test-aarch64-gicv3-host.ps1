[CmdletBinding()]
param(
    [string]$LlvmRoot = 'C:\Program Files\LLVM\bin',
    [string]$HostCxx = 'C:\mingw64\bin\g++.exe',
    [string]$OutputDirectory = ''
)

$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $repoRoot 'out\aarch64-gicv3-host'
} else {
    $OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
}
$resolvedRoot = [IO.Path]::GetFullPath($repoRoot).TrimEnd('\')
$resolvedOutput = [IO.Path]::GetFullPath($OutputDirectory).TrimEnd('\')
if (!$resolvedOutput.StartsWith($resolvedRoot + '\out\', [StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to write hosted GICv3 test artifacts outside repository out/: $resolvedOutput"
}

if (!(Test-Path -LiteralPath $HostCxx -PathType Leaf)) {
    $HostCxx = Join-Path $LlvmRoot 'clang++.exe'
}
if (!(Test-Path -LiteralPath $HostCxx -PathType Leaf)) {
    throw "Hosted C++ compiler not found: $HostCxx"
}
$null = New-Item -ItemType Directory -Path $OutputDirectory -Force
$testBinary = Join-Path $OutputDirectory 'aarch64-gicv3-host-tests.exe'
$sources = @(
    (Join-Path $repoRoot 'tests\aarch64_gicv3_host_tests.cpp'),
    (Join-Path $repoRoot 'kernel\arch\arm64\phase2_platform.cpp'),
    (Join-Path $repoRoot 'kernel\arch\arm64\gicv3_controller.cpp'),
    (Join-Path $repoRoot 'kernel\core\irq_registry.cpp'),
    (Join-Path $repoRoot 'kernel\core\common_scheduler.cpp')
)
$arguments = @('-std=c++17', '-O2', '-I', $repoRoot) + $sources + @('-o', $testBinary)
Write-Host "Compiling hosted register model with $HostCxx" -ForegroundColor Yellow
& $HostCxx @arguments
if ($LASTEXITCODE -ne 0) { throw 'Hosted GICv3 test compilation failed' }
& $testBinary
if ($LASTEXITCODE -ne 0) { throw 'Hosted GICv3 tests failed' }
Write-Host 'AARCH64 GICv3 hosted test suite: PASS. This script does not start QEMU.' -ForegroundColor Green
exit 0
