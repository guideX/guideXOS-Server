[CmdletBinding()]
param(
    [string]$OutputDirectory = "out\validation\settings-s19-native-layout-20261017"
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location -LiteralPath $Root

$Compiler = Get-Command g++.exe -ErrorAction SilentlyContinue
if (-not $Compiler -and (Test-Path -LiteralPath "C:\mingw64\bin\g++.exe")) {
    $Compiler = Get-Item -LiteralPath "C:\mingw64\bin\g++.exe"
}
if (-not $Compiler) { throw "g++ was not found." }

$Output = [IO.Path]::GetFullPath($OutputDirectory)
$Executable = Join-Path $Output "settings_native_layout_test.exe"
if (Test-Path -LiteralPath $Executable) {
    throw "Layout test output already exists; preserving it: $Executable"
}
New-Item -ItemType Directory -Force -Path $Output | Out-Null

$Arguments = @(
    "-std=c++14", "-Wall", "-Wextra", "-Werror", "-O2", "-iquote", ".",
    "tests/settings_native_layout_test.cpp",
    "kernel/core/settings_native_layout.cpp",
    "settings_presentation_core.cpp",
    "-o", $Executable
)

Write-Host ("Running: {0} {1}" -f $Compiler.Source, ($Arguments -join " "))
& $Compiler.Source @Arguments
if ($LASTEXITCODE -ne 0) { throw "Settings native layout test build failed with exit code $LASTEXITCODE." }

& $Executable
if ($LASTEXITCODE -ne 0) { throw "Settings native layout tests failed with exit code $LASTEXITCODE." }
Write-Host "Settings native layout build and test PASS."
