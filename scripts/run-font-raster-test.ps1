#Requires -Version 5.1
[CmdletBinding()]
param(
	[string]$OutputPath = "out\validation\font_raster_test.exe",
	[switch]$Sanitize
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
	"tests/font_raster_test.cpp",
	"kernel/core/font_contract.cpp",
	"kernel/core/font_bitmap_backend.cpp",
	"kernel/core/font_truetype.cpp",
	"kernel/core/font_truetype_loader.cpp",
	"kernel/core/font_truetype_raster.cpp",
	"-o", $Output
)
if ($Sanitize) {
	$Arguments = @($Arguments[0..($Arguments.Length - 3)]) + @(
		"-fsanitize=address,undefined", "-fno-omit-frame-pointer") + @($Arguments[($Arguments.Length - 2)..($Arguments.Length - 1)])
}

Write-Host ("Running: {0} {1}" -f $Compiler.Source, ($Arguments -join " "))
& $Compiler.Source @Arguments
if ($LASTEXITCODE -ne 0) { throw "FONT4 raster test build failed with exit code $LASTEXITCODE." }

& $Output
if ($LASTEXITCODE -ne 0) { throw "FONT4 raster tests failed with exit code $LASTEXITCODE." }
Write-Host "FONT4 glyph rasterization build and test PASS."
