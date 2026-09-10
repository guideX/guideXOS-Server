$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$outputDirectory = Join-Path $repoRoot 'out/validation'
$testBinary = Join-Path $outputDirectory 'compiler-arm64-host-test.exe'
New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
$gxx = (Get-Command g++ -ErrorAction Stop).Source
$sources = @(
    (Join-Path $repoRoot 'tests/compiler_arm64_host_test.cpp'),
    (Join-Path $repoRoot 'kernel/core/compiler/compiler_diagnostics.cpp'),
    (Join-Path $repoRoot 'kernel/core/compiler/compiler_lexer.cpp'),
    (Join-Path $repoRoot 'kernel/core/compiler/compiler_parser.cpp'),
    (Join-Path $repoRoot 'kernel/core/compiler/compiler_target.cpp'),
    (Join-Path $repoRoot 'kernel/core/native_architecture.cpp'),
    (Join-Path $repoRoot 'kernel/core/compiler/elf_writer.cpp'),
    (Join-Path $repoRoot 'kernel/arch/arm64/compiler_backend.cpp'),
    (Join-Path $repoRoot 'kernel/arch/amd64/compiler_backend.cpp'),
    (Join-Path $repoRoot 'kernel/arch/amd64/arch.cpp')
)
& $gxx -std=c++14 -Wall -Wextra -O2 -DGXOS_BARE_METAL `
    "-I$repoRoot" "-I$(Join-Path $repoRoot 'kernel')" "-iquote$(Join-Path $repoRoot 'kernel/core/include')" "-I$(Join-Path $repoRoot 'kernel/arch/amd64/include')" `
    @sources -o $testBinary
if ($LASTEXITCODE -ne 0) { throw 'ARM64 compiler host test build failed' }
& $testBinary
if ($LASTEXITCODE -ne 0) { throw 'ARM64 compiler host test failed' }
