#Requires -Version 5.1
[CmdletBinding()]
param(
	[string]$OutputPath = "out\font11-validation\font11_cache_workload_test.exe"
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
	"-std=c++17", "-Wall", "-Wextra", "-O1", "-iquote", ".",
	"-idirafter", "kernel/core/include",
	"tests/font11_glyph_cache_workload_test.cpp",
	"kernel/core/font_contract.cpp",
	"kernel/core/font_text_layout.cpp",
	"kernel/core/font_glyph_cache.cpp",
	"kernel/core/font_bitmap_backend.cpp",
	"kernel/core/font_truetype.cpp",
	"kernel/core/font_truetype_loader.cpp",
	"kernel/core/font_truetype_raster.cpp",
	"-o", $Output
)

Write-Host ("Running: {0} {1}" -f $Compiler.Source, ($Arguments -join " "))
& $Compiler.Source @Arguments
if ($LASTEXITCODE -ne 0) { throw "FONT11 cache workload build failed with exit code $LASTEXITCODE." }

& $Output
if ($LASTEXITCODE -ne 0) { throw "FONT11 cache workload failed with exit code $LASTEXITCODE." }
Write-Host "FONT11 cache workload PASS."
