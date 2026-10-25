param([string]$OutputDirectory = '')

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $repoRoot "out/comp3-validation" }
$outputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
$testBinary = Join-Path $outputDirectory "native-build-service-responsiveness-test.exe"
$buildLog = Join-Path $outputDirectory "native-build-service-responsiveness-build.log"
$fixtureRoot = Join-Path $outputDirectory "hosted-service-fixture"

New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
$gxx = (Get-Command g++ -ErrorAction Stop).Source
$sources = @(
    (Join-Path $repoRoot "tests/native_build_service_responsiveness_test.cpp"),
    (Join-Path $repoRoot "native_build_service.cpp"),
    (Join-Path $repoRoot "kernel/core/compiler/elf_writer.cpp")
)

$buildOutput = & $gxx -std=c++17 -Wall -Wextra -O2 `
    "-iquote$repoRoot" "-iquote$(Join-Path $repoRoot 'kernel/core/include')" `
    "-iquote$(Join-Path $repoRoot 'kernel/arch/amd64/include')" `
    @sources -o $testBinary 2>&1
$buildExitCode = $LASTEXITCODE
$buildOutput | Set-Content -Path $buildLog
if ($buildExitCode -ne 0) {
    Get-Content -Path $buildLog -Tail 50
    throw "native build service responsiveness test build failed; see $buildLog"
}

& $testBinary $repoRoot $fixtureRoot
if ($LASTEXITCODE -ne 0) { throw "native build service responsiveness test failed" }
