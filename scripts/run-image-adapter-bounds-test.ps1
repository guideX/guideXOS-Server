[CmdletBinding()]
param(
    [string]$OutputPath = (Join-Path ([IO.Path]::GetTempPath()) "guidexos_image_adapter_bounds_test.exe")
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
    "-std=c++17", "-Wall", "-Wextra", "-O2", "-iquote", ".",
    "tests/image_adapter_bounds_test.cpp",
    "image_adapter.cpp",
    "image.cpp",
    "image_renderer.cpp",
    "png_loader.cpp",
    "jpeg_loader.cpp",
    "vfs.cpp",
    "logger.cpp",
    "allocator.cpp",
    "-lgdi32", "-lmsimg32",
    "-o", $Output
)

Write-Host ("Running: {0} {1}" -f $Compiler.Source, ($Arguments -join " "))
& $Compiler.Source @Arguments
if ($LASTEXITCODE -ne 0) { throw "Image adapter bounds test build failed with exit code $LASTEXITCODE." }

& $Output
if ($LASTEXITCODE -ne 0) { throw "Image adapter bounds tests failed with exit code $LASTEXITCODE." }
Write-Host "Image adapter bounds build and test PASS."
