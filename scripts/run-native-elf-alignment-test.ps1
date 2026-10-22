#Requires -Version 5.1
[CmdletBinding()]
param(
    [string]$OutputPath = (Join-Path $env:TEMP ("native_elf_alignment_test_" + [Guid]::NewGuid().ToString("N") + ".exe"))
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $Root

$compiler = Get-Command g++.exe -ErrorAction SilentlyContinue
if (-not $compiler -and (Test-Path -LiteralPath "C:\mingw64\bin\g++.exe")) {
    $compiler = Get-Item -LiteralPath "C:\mingw64\bin\g++.exe"
}
if (-not $compiler) { throw "g++ was not found." }

$output = [IO.Path]::GetFullPath($OutputPath)
$outputDirectory = Split-Path -Parent $output
New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null

$arguments = @(
    "-std=c++11", "-Wall", "-Wextra", "-Werror", "-O2",
    "-Ikernel/core/include",
    "kernel/tests/native_elf_alignment_test.cpp",
    "-o", $output
)

Write-Host ("Running: {0} {1}" -f $compiler.Source, ($arguments -join " "))
& $compiler.Source @arguments
if ($LASTEXITCODE -ne 0) { throw "Native ELF alignment test build failed with exit code $LASTEXITCODE." }

& $output
if ($LASTEXITCODE -ne 0) { throw "Native ELF alignment test failed with exit code $LASTEXITCODE." }
Write-Host "Native ELF alignment build and test PASS."
