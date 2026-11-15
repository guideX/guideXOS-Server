[CmdletBinding()]
param(
    [string]$OutputPath = "out\validation\managed_execution_lifecycle_test.exe"
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
    "-std=c++14", "-Wall", "-Wextra", "-O2", "-iquote", "kernel/core/include",
    "tests/managed_execution_lifecycle_test.cpp", "kernel/core/managed_execution.cpp",
    "kernel/core/identity.cpp", "kernel/core/identity_runtime.cpp",
    "kernel/core/authentication.cpp", "-o", $Output
)

Write-Host ("Running: {0} {1}" -f $Compiler.Source, ($Arguments -join " "))
& $Compiler.Source @Arguments
if ($LASTEXITCODE -ne 0) { throw "Managed execution lifecycle test build failed with exit code $LASTEXITCODE." }

& $Output
if ($LASTEXITCODE -ne 0) { throw "Managed execution lifecycle tests failed with exit code $LASTEXITCODE." }
Write-Host "Managed execution lifecycle build and test PASS."
