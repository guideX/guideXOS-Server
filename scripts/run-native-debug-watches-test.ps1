$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$outputDirectory = Join-Path $repoRoot "out/validation"
$testBinary = Join-Path $outputDirectory "native-debug-watches-test.exe"
New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null

$gxx = (Get-Command g++ -ErrorAction Stop).Source
& $gxx -std=c++14 -Wall -Wextra -Werror `
    "-idirafter$repoRoot" `
    "-I$(Join-Path $repoRoot 'sdk/include')" `
    (Join-Path $repoRoot "tests/native_debug_watches_test.cpp") `
    (Join-Path $repoRoot "kernel/core/native_elf/native_elf_debug_watches.cpp") `
    -o $testBinary
if ($LASTEXITCODE -ne 0) { throw "debug watch host test build failed" }
& $testBinary
if ($LASTEXITCODE -ne 0) { throw "debug watch host test failed" }
