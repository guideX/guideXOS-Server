$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $Root

$outputDir = Join-Path $env:TEMP "guidex-navigator-javascript-js118-portable-runtime-core"
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
$exe = Join-Path $outputDir "navigator_javascript_portable_values_environment_test.exe"

$build = & g++ -std=c++17 -O2 -Wall -Wextra -Werror -pedantic -iquote . `
    tests/navigator_javascript_portable_values_environment_test.cpp `
    tests/navigator_javascript_js118_runtime_core_anchor.cpp `
    navigator_javascript/value.cpp `
    navigator_javascript/environment.cpp -o $exe 2>&1
if ($LASTEXITCODE -ne 0) {
    $build | Write-Output
    throw "JS118 portable Value and Environment test build failed."
}

& $exe
if ($LASTEXITCODE -ne 0) {
    throw "JS118 portable Value and Environment tests failed."
}
Write-Output "JS118 portable Value and Environment smoke PASS"
