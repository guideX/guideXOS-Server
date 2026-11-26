[CmdletBinding()]
param(
    [string]$OutputDirectory = "out\validation\ramdisk-integrity-diagnostic"
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $repoRoot

$compiler = Get-Command g++.exe -ErrorAction SilentlyContinue
if (-not $compiler) { $compiler = Get-Command g++ -ErrorAction Stop }
$outputRoot = if ([IO.Path]::IsPathRooted($OutputDirectory)) {
    [IO.Path]::GetFullPath($OutputDirectory)
} else {
    [IO.Path]::GetFullPath((Join-Path $repoRoot $OutputDirectory))
}
New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null
$testExecutable = Join-Path $outputRoot 'ramdisk_integrity_diagnostic_test.exe'

$arguments = @(
    '-std=c++14', '-Wall', '-Wextra', '-Werror', '-O2',
    'tests/ramdisk_integrity_diagnostic_test.cpp',
    '-o', $testExecutable
)
Write-Host "Building RAMDISK integrity diagnostic regression with $($compiler.Source)"
& $compiler.Source @arguments
if ($LASTEXITCODE -ne 0) { throw "Hosted diagnostic test build failed with exit code $LASTEXITCODE" }

& $testExecutable
if ($LASTEXITCODE -ne 0) { throw "Hosted diagnostic test failed with exit code $LASTEXITCODE" }

Write-Output "TEST_EXE=$testExecutable"
Write-Output "TEST_EXE_SHA256=$((Get-FileHash -LiteralPath $testExecutable -Algorithm SHA256).Hash.ToLowerInvariant())"
