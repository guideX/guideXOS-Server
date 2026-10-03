[CmdletBinding()]
param(
    [string]$OutputPath = (Join-Path ([IO.Path]::GetTempPath()) "guidexos_appmodel_uri_protocol_test.exe")
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
    "-std=c++17", "-Wall", "-Wextra", "-O2", "-iquote", ".", "-DGXOS_APPMODEL_TESTING",
    "tests/appmodel_uri_protocol_test.cpp",
    "app_registry.cpp",
    "app_default_handler_store.cpp",
    "app_manifest.cpp",
    "app_manifest_loader.cpp",
    "app_manifest_validator.cpp",
    "fs.cpp",
    "-o", $Output
)

Write-Host ("Running: {0} {1}" -f $Compiler.Source, ($Arguments -join " "))
& $Compiler.Source @Arguments
if ($LASTEXITCODE -ne 0) { throw "App Model URI/protocol test build failed with exit code $LASTEXITCODE." }

& $Output
if ($LASTEXITCODE -ne 0) { throw "App Model URI/protocol tests failed with exit code $LASTEXITCODE." }
Write-Host "App Model URI/protocol activation tests PASS."
