param([string]$OutputDirectory = '')

$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $repoRoot 'out/comp4p-validation' }
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
$testBinary = Join-Path $OutputDirectory 'compiler-comp4p-native-project-host-test.exe'
$buildLog = Join-Path $OutputDirectory 'compiler-comp4p-native-project-host-test-build.log'
$artifact = Join-Path $OutputDirectory 'hello-world.elf'
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null

$gxx = (Get-Command g++ -ErrorAction Stop).Source
$sources = @(
    (Join-Path $repoRoot 'tests/compiler_comp4p_native_project_host_test.cpp'),
    (Join-Path $repoRoot 'kernel/core/compiler/compiler_build_job_manager.cpp'),
    (Join-Path $repoRoot 'kernel/core/compiler/compiler_build_service.cpp'),
    (Join-Path $repoRoot 'kernel/core/compiler/compiler_driver.cpp'),
    (Join-Path $repoRoot 'kernel/core/compiler/compiler_diagnostics.cpp'),
    (Join-Path $repoRoot 'kernel/core/compiler/compiler_lexer.cpp'),
    (Join-Path $repoRoot 'kernel/core/compiler/compiler_parser.cpp'),
    (Join-Path $repoRoot 'kernel/core/compiler/compiler_module.cpp'),
    (Join-Path $repoRoot 'kernel/core/compiler/compiler_object.cpp'),
    (Join-Path $repoRoot 'kernel/core/compiler/compiler_linker.cpp'),
    (Join-Path $repoRoot 'kernel/core/compiler/elf_writer.cpp'),
    (Join-Path $repoRoot 'kernel/core/native_elf/native_elf_validator.cpp'),
    (Join-Path $repoRoot 'kernel/arch/amd64/compiler_backend.cpp'),
    (Join-Path $repoRoot 'kernel/arch/amd64/arch.cpp')
)

$savedPreference = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
$buildOutput = & $gxx -std=c++14 -Wall -Wextra -Wno-unused-function -O2 -DGXOS_BARE_METAL `
    "-I$repoRoot" "-I$(Join-Path $repoRoot 'kernel')" `
    "-iquote$(Join-Path $repoRoot 'kernel/core/include')" `
    "-I$(Join-Path $repoRoot 'kernel/arch/amd64/include')" `
    "-include$(Join-Path $repoRoot 'tests/compiler_driver_host_arch_config.h')" `
    @sources -o $testBinary 2>&1
$ErrorActionPreference = $savedPreference
$buildExitCode = $LASTEXITCODE
$buildOutput | Set-Content -Path $buildLog
if ($buildExitCode -ne 0) {
    Get-Content -Path $buildLog -Tail 70
    throw "COMP4P native project host test build failed; see $buildLog"
}

Push-Location $repoRoot
try {
    & $testBinary $artifact
    if ($LASTEXITCODE -ne 0) { throw 'COMP4P native project host test failed' }
} finally {
    Pop-Location
}
