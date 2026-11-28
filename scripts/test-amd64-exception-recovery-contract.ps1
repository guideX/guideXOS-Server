param(
    [Parameter(Mandatory = $true)]
    [string]$OutputDirectory
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$outputPath = [System.IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $outputPath -Force | Out-Null

$compiler = Get-Command g++.exe -ErrorAction SilentlyContinue
if (-not $compiler) { $compiler = Get-Command g++ -ErrorAction Stop }
$testExecutable = Join-Path $outputPath 'amd64_exception_recovery_contract_test.exe'
$testSource = Join-Path $repoRoot 'tests\amd64_exception_recovery_contract_test.cpp'

& $compiler.Source -std=c++14 -O2 -Wall -Wextra -fno-exceptions -fno-rtti `
    $testSource -o $testExecutable
if ($LASTEXITCODE -ne 0) {
    throw "AMD64 exception-recovery contract compile failed with exit code $LASTEXITCODE"
}

& $testExecutable
if ($LASTEXITCODE -ne 0) {
    throw "AMD64 exception-recovery contract regression failed with exit code $LASTEXITCODE"
}

Write-Output "TEST_EXE=$testExecutable"
Write-Output "TEST_EXE_SHA256=$((Get-FileHash -LiteralPath $testExecutable -Algorithm SHA256).Hash.ToLowerInvariant())"
