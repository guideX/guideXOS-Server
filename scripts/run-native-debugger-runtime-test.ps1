[CmdletBinding()]
param(
    [string]$OutputPath = "out\validation\native-debugger-runtime-test.exe"
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location -LiteralPath $Root

$compiler = Get-Command g++.exe -ErrorAction SilentlyContinue
if (-not $compiler -and (Test-Path -LiteralPath "C:\mingw64\bin\g++.exe")) {
    $compiler = Get-Item -LiteralPath "C:\mingw64\bin\g++.exe"
}
if (-not $compiler) { throw "g++ was not found." }

$sources = @(
    "native_app_debugger.cpp",
    "executable_memory.cpp",
    "allocator.cpp",
    "logger.cpp"
)

$output = [IO.Path]::GetFullPath($OutputPath)
$outputDirectory = Split-Path -Parent $output
New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
$arguments = @(
    "-std=c++17", "-Wall", "-Wextra", "-O2", "-idirafter", ".",
    "-Ithird_party/mbedtls/include",
    "-Ithird_party/mbedtls/tf-psa-crypto/include",
    "-DGX_ENABLE_EXPERIMENTAL_NATIVE_ELF_EXECUTION",
    "tests/native_debugger_runtime_test.cpp"
) + $sources + @(
    "-lws2_32", "-lsecur32", "-lcrypt32", "-lbcrypt", "-lgdi32", "-luser32", "-lmsimg32",
    "-o", $output
)

Write-Host ("Running: {0} {1}" -f $compiler.Source, ($arguments -join " "))
& $compiler.Source @arguments
if ($LASTEXITCODE -ne 0) { throw "Native debugger runtime test build failed with exit code $LASTEXITCODE." }
& $output
if ($LASTEXITCODE -ne 0) { throw "Native debugger runtime test failed with exit code $LASTEXITCODE." }
Write-Host "Native debugger runtime build and test PASS."
