[CmdletBinding()]
param(
    [string]$OutputPath = "out\validation\native-debug-output-contract-test.exe"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location -LiteralPath $root
$compiler = Get-Command g++.exe -ErrorAction SilentlyContinue
if (-not $compiler -and (Test-Path -LiteralPath "C:\mingw64\bin\g++.exe")) {
    $compiler = Get-Item -LiteralPath "C:\mingw64\bin\g++.exe"
}
if (-not $compiler) { throw "g++ was not found." }

$output = [IO.Path]::GetFullPath($OutputPath)
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $output) | Out-Null
$arguments = @(
    "-std=c++17", "-Wall", "-Wextra", "-O2", "-I.", "-Isdk/include",
    "tests/native_debug_output_contract_test.cpp",
    "kernel/core/native_elf/native_elf_debug_output.cpp", "-o", $output
)
Write-Host ("Running: {0} {1}" -f $compiler.Source, ($arguments -join " "))
& $compiler.Source @arguments
if ($LASTEXITCODE -ne 0) { throw "Native debug output contract build failed with exit code $LASTEXITCODE." }
& $output
if ($LASTEXITCODE -ne 0) { throw "Native debug output contract test failed with exit code $LASTEXITCODE." }
Write-Host "Native debug output contract build and test PASS."
