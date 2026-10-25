param([string]$OutputDirectory = '')

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $repoRoot "out/comp3r4-validation" }
$outputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
$testBinary = Join-Path $outputDirectory "compiler-build-job-manager-host-test.exe"
$buildLog = Join-Path $outputDirectory "compiler-build-job-manager-host-test-build.log"

New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
$gxx = (Get-Command g++ -ErrorAction Stop).Source
$sources = @(
    (Join-Path $repoRoot "tests/compiler_build_job_manager_host_test.cpp"),
    (Join-Path $repoRoot "kernel/core/compiler/compiler_build_job_manager.cpp")
)

$buildOutput = & $gxx -std=c++17 -Wall -Wextra -O2 -pthread `
    "-iquote$repoRoot" "-iquote$(Join-Path $repoRoot 'kernel/core/include')" `
    "-iquote$(Join-Path $repoRoot 'kernel/arch/amd64/include')" `
    @sources -o $testBinary 2>&1
$buildExitCode = $LASTEXITCODE
$buildOutput | Set-Content -Path $buildLog
if ($buildExitCode -ne 0) {
    Get-Content -Path $buildLog -Tail 60
    throw "kernel build-job manager test build failed; see $buildLog"
}

$desktopSource = Get-Content -Raw -Path (Join-Path $repoRoot "kernel/core/desktop.cpp")
$desktopPumpPattern = '(?s)const bool nativeElfTargetActive\s*=\s*kernel::native_elf::NativeElfRunService::native_elf_scheduler_in_target\(\);\s*\(void\)kernel::compiler::BareMetalBuildService::pump_cooperative_test_step\(\);\s*if\s*\(!nativeElfTargetActive\)\s*\(void\)kernel::native_elf::NativeElfRunService::native_elf_scheduler_pump\(\);'
if (-not [regex]::IsMatch($desktopSource, $desktopPumpPattern)) {
    throw "desktop update boundary is not wired to the guarded build-job pump"
}
Write-Host "Desktop build-job pump wiring PASS"

& $testBinary
if ($LASTEXITCODE -ne 0) { throw "kernel build-job manager test failed" }
