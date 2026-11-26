param(
    [string]$OutputPath = "out\validation\taskbar_application_model_test.exe"
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
Set-Location $Root
$Output = Join-Path $Root $OutputPath
$OutputDirectory = Split-Path -Parent $Output
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null

$Compiler = Get-Command g++ -ErrorAction SilentlyContinue
if (-not $Compiler) { throw "g++ was not found on PATH." }

& $Compiler.Source -std=c++17 -O2 -Wall -Wextra -iquote . `
    tests\taskbar_application_model_test.cpp fs.cpp desktop_theme.cpp `
    -o $Output
if ($LASTEXITCODE -ne 0) { throw "Taskbar application model test build failed with exit code $LASTEXITCODE." }

& $Output
if ($LASTEXITCODE -ne 0) { throw "Taskbar application model test failed with exit code $LASTEXITCODE." }

Write-Host "Taskbar application model build and test PASS. Output: $Output"
