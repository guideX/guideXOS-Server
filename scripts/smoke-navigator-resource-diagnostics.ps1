param(
    [string]$Root = (Split-Path -Parent $PSScriptRoot)
)

$ErrorActionPreference = "Stop"
Set-Location -LiteralPath $Root
$outputDir = Join-Path $env:TEMP "guidex-phase8r-resource-diagnostics-test"
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
$exe = Join-Path $outputDir "navigator_resource_diagnostics_test.exe"
$build = & cmd.exe /c "g++ -std=c++17 -O2 -Wall -Wextra -iquote . tests\navigator_resource_diagnostics_test.cpp -o `"$exe`" 2>&1"
if ($LASTEXITCODE -ne 0) {
    $build | Write-Output
    throw "Navigator resource diagnostics test build failed."
}
& $exe
if ($LASTEXITCODE -ne 0) {
    throw "Navigator resource diagnostics tests failed."
}
Write-Output "Navigator resource diagnostics smoke PASS"
