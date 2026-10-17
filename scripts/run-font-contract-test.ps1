#Requires -Version 5.1
[CmdletBinding()]
param(
	[string]$OutputPath = "out\validation\font_contract_test.exe"
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

$arguments = @(
	"-std=c++17", "-Wall", "-Wextra", "-O2",
	"-I.",
	"tests/font_contract_test.cpp",
	"kernel/core/font_contract.cpp",
	"kernel/core/font_glyph_cache.cpp",
	"kernel/core/font_bitmap_backend.cpp",
	"kernel/core/font_truetype_raster.cpp",
	"-o", $output
)

Write-Host ("Running: {0} {1}" -f $compiler.Source, ($arguments -join " "))
& $compiler.Source @arguments
if ($LASTEXITCODE -ne 0) { throw "Font contract test build failed with exit code $LASTEXITCODE." }

& $output
if ($LASTEXITCODE -ne 0) { throw "Font contract test failed with exit code $LASTEXITCODE." }
Write-Host "Font contract build and test PASS."
