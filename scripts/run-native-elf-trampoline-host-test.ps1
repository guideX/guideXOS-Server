$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$outputDirectory = Join-Path $repoRoot "out/validation"
$testBinary = Join-Path $outputDirectory "native-elf-trampoline-host-test.exe"
$trampolineObject = Join-Path $outputDirectory "native-elf-trampoline-host-test-trampoline.o"
$entryObject = Join-Path $outputDirectory "native-elf-trampoline-host-test-entry.o"
New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null

$gxx = (Get-Command g++ -ErrorAction Stop).Source
$nasm = (Get-Command nasm -ErrorAction Stop).Source
& $nasm -f win64 (Join-Path $repoRoot "kernel/arch/amd64/native_elf_trampoline.asm") -o $trampolineObject
if ($LASTEXITCODE -ne 0) { throw "NativeElf trampoline assembly failed" }
& $nasm -f win64 (Join-Path $repoRoot "tests/native_elf_trampoline_test_entry.asm") -o $entryObject
if ($LASTEXITCODE -ne 0) { throw "NativeElf trampoline test-entry assembly failed" }

& $gxx -std=c++14 -Wall -Wextra -O2 -DGXOS_BARE_METAL `
    "-I$repoRoot" "-I$(Join-Path $repoRoot 'kernel')" `
    "-iquote$(Join-Path $repoRoot 'kernel/core/include')" `
    "-I$(Join-Path $repoRoot 'kernel/arch/amd64/include')" `
    (Join-Path $repoRoot "tests/native_elf_trampoline_host_test.cpp") `
    $trampolineObject $entryObject -o $testBinary
if ($LASTEXITCODE -ne 0) { throw "NativeElf trampoline host test build failed" }

& $testBinary
if ($LASTEXITCODE -ne 0) { throw "NativeElf trampoline host test failed" }
