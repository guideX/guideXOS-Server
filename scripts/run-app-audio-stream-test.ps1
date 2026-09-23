[CmdletBinding()]
param(
    [string]$OutputPath = "out\validation\app_audio_stream_test.exe"
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
$objDirectory = Join-Path $outputDirectory "obj"
New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
New-Item -ItemType Directory -Force -Path $objDirectory | Out-Null

# Two-step build: the freestanding kernel module is compiled with the kernel
# include directory (it needs kernel/types.h via pci_audio.h), while the
# host test TU is compiled WITHOUT it — the kernel include dir ships
# hosted-incompatible stdio.h/stdlib.h stubs that would shadow the system
# headers inside <cstdio>/<string>. The test includes the module header by
# quoted relative path (repo convention).
$moduleObj = Join-Path $objDirectory "app_audio_stream.o"
$testObj = Join-Path $objDirectory "app_audio_stream_test.o"

$moduleArgs = @(
    "-std=c++17", "-Wall", "-Wextra", "-O2",
    "-I", "kernel/core/include",
    "-c", "kernel/core/app_audio_stream.cpp",
    "-o", $moduleObj
)
Write-Host ("Running: {0} {1}" -f $compiler.Source, ($moduleArgs -join " "))
& $compiler.Source @moduleArgs
if ($LASTEXITCODE -ne 0) { throw "App audio stream module build failed with exit code $LASTEXITCODE." }

$testArgs = @(
    "-std=c++17", "-Wall", "-Wextra", "-O2",
    "-c", "tests/app_audio_stream_test.cpp",
    "-o", $testObj
)
Write-Host ("Running: {0} {1}" -f $compiler.Source, ($testArgs -join " "))
& $compiler.Source @testArgs
if ($LASTEXITCODE -ne 0) { throw "App audio stream test build failed with exit code $LASTEXITCODE." }

$linkArgs = @($moduleObj, $testObj, "-o", $output)
Write-Host ("Running: {0} {1}" -f $compiler.Source, ($linkArgs -join " "))
& $compiler.Source @linkArgs
if ($LASTEXITCODE -ne 0) { throw "App audio stream link failed with exit code $LASTEXITCODE." }

& $output
if ($LASTEXITCODE -ne 0) { throw "App audio stream test failed with exit code $LASTEXITCODE." }
Write-Host "App audio stream build and test PASS."
