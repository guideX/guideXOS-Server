[CmdletBinding()]
param(
    [string]$OutputPath = "out\validation\settings-s20-modifier-focus-20261018\ps2keyboard_modifier_test.exe"
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location -LiteralPath $Root

$Compiler = Get-Command g++.exe -ErrorAction SilentlyContinue
if (-not $Compiler -and (Test-Path -LiteralPath "C:\mingw64\bin\g++.exe")) {
    $Compiler = Get-Item -LiteralPath "C:\mingw64\bin\g++.exe"
}
if (-not $Compiler) { throw "g++ was not found." }

$Output = [IO.Path]::GetFullPath($OutputPath)
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Output) | Out-Null
$Arguments = @(
    "-std=c++14", "-Wall", "-Wextra", "-O2", "-fno-exceptions", "-fno-rtti",
    "-DGXOS_BARE_METAL", "-iquote", ".", "-idirafter", "kernel/core/include",
    "-I", "kernel/arch/amd64/include", "tests/ps2keyboard_modifier_test.cpp",
    "kernel/core/ps2keyboard.cpp", "-o", $Output
)

Write-Host ("Running: {0} {1}" -f $Compiler.Source, ($Arguments -join " "))
& $Compiler.Source @Arguments
if ($LASTEXITCODE -ne 0) { throw "PS/2 keyboard modifier test build failed with exit code $LASTEXITCODE." }

& $Output
if ($LASTEXITCODE -ne 0) { throw "PS/2 keyboard modifier tests failed with exit code $LASTEXITCODE." }
Write-Host "PS/2 keyboard modifier build and test PASS."
