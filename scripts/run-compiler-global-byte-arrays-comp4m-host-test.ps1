param([string]$OutputDirectory = '')

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $repoRoot "out/comp4m-validation/global-byte-arrays-host" }
$outputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
$testBinary = Join-Path $outputDirectory "compiler-global-byte-arrays-comp4m-host-test.exe"
New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null

$gxx = (Get-Command g++ -ErrorAction Stop).Source
$sources = @(
    (Join-Path $repoRoot "tests/compiler_global_byte_arrays_comp4m_host_test.cpp"),
    (Join-Path $repoRoot "kernel/core/compiler/compiler_diagnostics.cpp"),
    (Join-Path $repoRoot "kernel/core/compiler/compiler_lexer.cpp"),
    (Join-Path $repoRoot "kernel/core/compiler/compiler_parser.cpp"),
    (Join-Path $repoRoot "kernel/core/compiler/compiler_module.cpp"),
    (Join-Path $repoRoot "kernel/core/compiler/compiler_linker.cpp"),
    (Join-Path $repoRoot "kernel/core/compiler/compiler_object.cpp"),
    (Join-Path $repoRoot "kernel/core/compiler/elf_writer.cpp"),
    (Join-Path $repoRoot "kernel/arch/amd64/compiler_backend.cpp"),
    (Join-Path $repoRoot "kernel/arch/amd64/arch.cpp")
)

& $gxx -std=c++14 -Wall -Wextra -O2 -DGXOS_BARE_METAL `
    "-I$repoRoot" "-I$(Join-Path $repoRoot 'kernel')" "-iquote$(Join-Path $repoRoot 'kernel/core/include')" `
    "-I$(Join-Path $repoRoot 'kernel/arch/amd64/include')" `
    @sources -o $testBinary
if ($LASTEXITCODE -ne 0) { throw "COMP4M global byte-array host test build failed" }

& $testBinary
if ($LASTEXITCODE -ne 0) { throw "COMP4M global byte-array host test failed" }
