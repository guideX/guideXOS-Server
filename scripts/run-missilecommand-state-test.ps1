[CmdletBinding()]
param(
    [string]$OutputPath = "out\validation\missilecommand_state_test.exe"
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
    "tests/missilecommand_state_test.cpp",
    "-o", $output
)

Write-Host ("Running: {0} {1}" -f $compiler.Source, ($arguments -join " "))
& $compiler.Source @arguments
if ($LASTEXITCODE -ne 0) { throw "MissileCommand state build failed with exit code $LASTEXITCODE." }

& $output
if ($LASTEXITCODE -ne 0) { throw "MissileCommand state test failed with exit code $LASTEXITCODE." }
Write-Host "MissileCommand state build and test PASS."
