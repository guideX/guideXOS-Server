[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$outputDirectory = Join-Path $repoRoot "out/validation"
$testBinary = Join-Path $outputDirectory "native-elf-validator-host-test.exe"

New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null

$gxx = (Get-Command g++ -ErrorAction Stop).Source
$sources = @(
    (Join-Path $repoRoot "tests/native_elf_validator_host_test.cpp"),
    (Join-Path $repoRoot "kernel/core/compiler/elf_writer.cpp"),
    (Join-Path $repoRoot "kernel/core/native_elf/native_elf_validator.cpp")
)

& $gxx -std=c++14 -Wall -Wextra -O2 -DGXOS_BARE_METAL `
    "-I$repoRoot" "-I$(Join-Path $repoRoot 'kernel')" `
    "-I$(Join-Path $repoRoot 'kernel/core')" `
    "-iquote$(Join-Path $repoRoot 'kernel/core/include')" `
    @sources -o $testBinary
if ($LASTEXITCODE -ne 0) { throw "NativeElf validator host test build failed" }

& $testBinary
if ($LASTEXITCODE -ne 0) { throw "NativeElf validator host test failed" }
