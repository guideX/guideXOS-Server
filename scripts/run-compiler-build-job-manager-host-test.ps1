$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$outputDirectory = Join-Path $repoRoot "out/comp3r3-validation"
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

& $testBinary
if ($LASTEXITCODE -ne 0) { throw "kernel build-job manager test failed" }
