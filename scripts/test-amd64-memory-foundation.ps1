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
$testExecutable = Join-Path $outputPath 'amd64_memory_foundation_test.exe'

$arguments = @(
    '-std=c++14', '-O2', '-Wall', '-Wextra',
    '-fno-exceptions', '-fno-rtti',
    (Join-Path $repoRoot 'tests\amd64_memory_foundation_test.cpp'),
    (Join-Path $repoRoot 'kernel\core\physical_frame_owner.cpp'),
    (Join-Path $repoRoot 'kernel\core\task_address_space.cpp'),
    '-o', $testExecutable
)
& $compiler.Source @arguments
if ($LASTEXITCODE -ne 0) { throw "AMD64 memory-foundation compile failed with exit code $LASTEXITCODE" }

& $testExecutable
if ($LASTEXITCODE -ne 0) { throw "AMD64 memory-foundation regression failed with exit code $LASTEXITCODE" }

Write-Output "TEST_EXE=$testExecutable"
Write-Output "TEST_EXE_SHA256=$((Get-FileHash -LiteralPath $testExecutable -Algorithm SHA256).Hash.ToLowerInvariant())"
