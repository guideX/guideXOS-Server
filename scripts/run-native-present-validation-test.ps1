#Requires -Version 5.1
[CmdletBinding()]
param(
    [string]$OutputPath = "out\validation\native_present_validation_test.exe"
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location -LiteralPath $Root

$compiler = Get-Command g++.exe -ErrorAction SilentlyContinue
if (-not $compiler -and (Test-Path -LiteralPath "C:\mingw64\bin\g++.exe")) {
    $compiler = Get-Item -LiteralPath "C:\mingw64\bin\g++.exe"
}
if (-not $compiler) { throw "g++ was not found." }

$output = [IO.Path]::GetFullPath($OutputPath)
$outputDirectory = Split-Path -Parent $output
New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null

# Single-TU build: the test includes the freestanding kernel validation
# header by quoted relative path (repo convention), so the kernel include
# directory (with its hosted-incompatible stdio.h/stdlib.h stubs) stays
# off the host include path.
$arguments = @(
    "-std=c++17", "-Wall", "-Wextra", "-O2",
    "tests/native_present_validation_test.cpp",
    "-o", $output
)

Write-Host ("Running: {0} {1}" -f $compiler.Source, ($arguments -join " "))
& $compiler.Source @arguments
if ($LASTEXITCODE -ne 0) { throw "Native present validation build failed with exit code $LASTEXITCODE." }

& $output
if ($LASTEXITCODE -ne 0) { throw "Native present validation test failed with exit code $LASTEXITCODE." }
Write-Host "Native present validation build and test PASS."
