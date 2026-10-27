param([string]$OutputDirectory = '')

$ErrorActionPreference = "Stop"
$PSNativeCommandUseErrorActionPreference = $false

$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $repoRoot "out/comp4b-validation" }
$outputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
$testBinary = Join-Path $outputDirectory "compiler-build-service-cooperative-host-test.exe"
$buildLog = Join-Path $outputDirectory "compiler-build-service-cooperative-host-test-build.log"

New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
$gxx = (Get-Command g++ -ErrorAction Stop).Source
$sources = @(
    (Join-Path $repoRoot "tests/compiler_build_service_cooperative_host_test.cpp"),
    (Join-Path $repoRoot "kernel/core/compiler/compiler_build_job_manager.cpp"),
    (Join-Path $repoRoot "kernel/core/compiler/compiler_build_service.cpp"),
    (Join-Path $repoRoot "kernel/core/compiler/compiler_driver.cpp"),
    (Join-Path $repoRoot "kernel/core/compiler/compiler_diagnostics.cpp"),
    (Join-Path $repoRoot "kernel/core/compiler/compiler_lexer.cpp"),
    (Join-Path $repoRoot "kernel/core/compiler/compiler_parser.cpp"),
    (Join-Path $repoRoot "kernel/core/compiler/compiler_module.cpp"),
    (Join-Path $repoRoot "kernel/core/compiler/compiler_object.cpp"),
    (Join-Path $repoRoot "kernel/core/compiler/compiler_linker.cpp"),
    (Join-Path $repoRoot "kernel/core/compiler/elf_writer.cpp"),
    (Join-Path $repoRoot "kernel/arch/amd64/compiler_backend.cpp"),
    (Join-Path $repoRoot "kernel/arch/amd64/arch.cpp")
)

$buildErrorActionPreference = $ErrorActionPreference
$ErrorActionPreference = "Continue"
$buildOutput = & $gxx -std=c++14 -Wall -Wextra -O2 -DGXOS_BARE_METAL `
    "-I$repoRoot" "-I$(Join-Path $repoRoot 'kernel')" `
    "-iquote$(Join-Path $repoRoot 'kernel/core/include')" `
    "-I$(Join-Path $repoRoot 'kernel/arch/amd64/include')" `
    "-include$(Join-Path $repoRoot 'tests/compiler_driver_host_arch_config.h')" `
    @sources -o $testBinary 2>&1
$ErrorActionPreference = $buildErrorActionPreference
$buildExitCode = $LASTEXITCODE
$buildOutput | Set-Content -Path $buildLog
if ($buildExitCode -ne 0) {
    Get-Content -Path $buildLog -Tail 70
    throw "cooperative guest Build service host test build failed; see $buildLog"
}

& $testBinary
if ($LASTEXITCODE -ne 0) { throw "cooperative guest Build service host test failed" }
