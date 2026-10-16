[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$OutputPath
)

$ErrorActionPreference = 'Stop'
$compiler = if ($env:CXX) { $env:CXX } else { 'C:\mingw64\bin\g++.exe' }
if (-not (Test-Path -LiteralPath $compiler)) { throw "MinGW compiler not found: $compiler" }
$OutputPath = [IO.Path]::GetFullPath($OutputPath)
$outputDirectory = Split-Path -Parent $OutputPath
New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
$source = Join-Path $PSScriptRoot 'process_thread_handle_host_test.cpp'
& $compiler -std=c++17 -Wall -Wextra -O2 $source -lwinpthread -o $OutputPath
if ($LASTEXITCODE -ne 0) { throw "Thread-handle regression build failed with exit code $LASTEXITCODE" }

$compilerDirectory = Split-Path -Parent $compiler
$previousPath = $env:PATH
try {
    $env:PATH = $compilerDirectory + ';' + $previousPath
    & $OutputPath
    if ($LASTEXITCODE -ne 0) { throw "Thread-handle regression failed with exit code $LASTEXITCODE" }
} finally {
    $env:PATH = $previousPath
}
