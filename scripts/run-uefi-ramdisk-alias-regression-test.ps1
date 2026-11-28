[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$OutputDirectory
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $repoRoot

$compiler = Get-Command g++.exe -ErrorAction SilentlyContinue
if (-not $compiler -and (Test-Path -LiteralPath 'C:\mingw64\bin\g++.exe')) {
    $compiler = Get-Item -LiteralPath 'C:\mingw64\bin\g++.exe'
}
if (-not $compiler) { throw 'g++ was not found.' }

$outputRoot = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $outputRoot) {
    throw "Output directory already exists; preserving it: $outputRoot"
}
New-Item -ItemType Directory -Path $outputRoot | Out-Null
$executable = Join-Path $outputRoot 'uefi_ramdisk_alias_regression_test.exe'
$arguments = @(
    '-std=c++14', '-O2', '-Wall', '-Wextra', '-Werror',
    '-I', 'guideXOSBootLoader',
    'tests/uefi_ramdisk_alias_regression_test.cpp',
    'guideXOSBootLoader/paging.cpp',
    '-o', $executable
)

Write-Host "Building the production UEFI paging source with $($compiler.Source)"
& $compiler.Source @arguments
if ($LASTEXITCODE -ne 0) { throw "UEFI RAMDISK alias regression build failed with exit code $LASTEXITCODE." }

& $executable
if ($LASTEXITCODE -ne 0) { throw "UEFI RAMDISK alias regression failed with exit code $LASTEXITCODE." }
Write-Output "TEST_EXE=$executable"
Write-Output "TEST_EXE_SHA256=$((Get-FileHash -LiteralPath $executable -Algorithm SHA256).Hash.ToLowerInvariant())"
