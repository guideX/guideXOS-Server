$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$outputDirectory = Join-Path $repoRoot "out/validation"
$testBinary = Join-Path $outputDirectory "compiler-pointers-host-test.exe"
New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null

$gxx = (Get-Command g++ -ErrorAction Stop).Source
$sources = @(
    (Join-Path $repoRoot "tests/compiler_pointers_host_test.cpp"),
    (Join-Path $repoRoot "kernel/core/compiler/compiler_diagnostics.cpp"),
    (Join-Path $repoRoot "kernel/core/compiler/compiler_lexer.cpp"),
    (Join-Path $repoRoot "kernel/core/compiler/compiler_parser.cpp"),
    (Join-Path $repoRoot "kernel/core/compiler/compiler_module.cpp"),
    (Join-Path $repoRoot "kernel/core/compiler/compiler_linker.cpp"),
    (Join-Path $repoRoot "kernel/core/compiler/compiler_object.cpp"),
    (Join-Path $repoRoot "kernel/arch/amd64/compiler_backend.cpp"),
    (Join-Path $repoRoot "kernel/arch/amd64/arch.cpp")
)

& $gxx -std=c++14 -Wall -Wextra -O2 -DGXOS_BARE_METAL `
    "-I$repoRoot" "-I$(Join-Path $repoRoot 'kernel')" "-iquote$(Join-Path $repoRoot 'kernel/core/include')" `
    "-I$(Join-Path $repoRoot 'kernel/arch/amd64/include')" @sources -o $testBinary
if ($LASTEXITCODE -ne 0) { throw "compiler pointers host test build failed" }

& $testBinary
if ($LASTEXITCODE -ne 0) { throw "compiler pointers host test failed" }
