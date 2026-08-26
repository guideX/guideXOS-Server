param(
    [string]$Root = (Split-Path -Parent $PSScriptRoot)
)

$ErrorActionPreference = "Stop"
Set-Location -LiteralPath $Root
$outputDir = Join-Path $env:TEMP "guidex-phase8t-resource-scheduler-test"
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
$exe = Join-Path $outputDir "navigator_resource_scheduler_test.exe"

$build = & g++ -std=c++17 -O2 -Wall -Wextra -iquote . `
    tests/navigator_resource_scheduler_test.cpp -o $exe 2>&1
if ($LASTEXITCODE -ne 0) {
    $build | Write-Output
    throw "Navigator viewport-priority scheduler test build failed."
}

& $exe
if ($LASTEXITCODE -ne 0) {
    throw "Navigator viewport-priority scheduler tests failed."
}
Write-Output "Navigator viewport-priority scheduler smoke PASS"
