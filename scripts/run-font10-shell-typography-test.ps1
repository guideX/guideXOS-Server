#Requires -Version 5.1
[CmdletBinding()]
param(
	[string]$OutputPath = "out\font10-validation\font10_shell_typography_test.exe"
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
	"-idirafter", "kernel/core/include", "-I", "kernel/arch/amd64/include",
	"tests/font10_shell_typography_test.cpp",
	"tests/font10_systemfont_stubs.cpp",
	"kernel/core/desktop_system_font.cpp",
	"kernel/core/desktop_font.cpp",
	"-o", $Output
)

Write-Host ("Running: {0} {1}" -f $Compiler.Source, ($Arguments -join " "))
& $Compiler.Source @Arguments
if ($LASTEXITCODE -ne 0) { throw "FONT10 shell typography test build failed with exit code $LASTEXITCODE." }

& $Output
if ($LASTEXITCODE -ne 0) { throw "FONT10 shell typography tests failed with exit code $LASTEXITCODE." }
Write-Host "FONT10 shell typography tests PASS."
