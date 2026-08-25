param(
    [string]$Root = (Split-Path -Parent $PSScriptRoot)
)

$ErrorActionPreference = "Stop"
Set-Location -LiteralPath $Root

$outputDir = Join-Path $env:TEMP "guidex-navigator-javascript-js7-test"
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
$exe = Join-Path $outputDir "navigator_javascript_js7_test.exe"

$build = & g++ -std=c++17 -O2 -Wall -Wextra -Werror -pedantic -iquote . `
    tests/navigator_javascript_js7_test.cpp `
    navigator_javascript/value.cpp `
    navigator_javascript/host.cpp `
    navigator_javascript/environment.cpp `
    navigator_javascript/runtime.cpp `
    navigator_javascript/ast.cpp `
    navigator_javascript/lexer.cpp `
    navigator_javascript/parser.cpp -o $exe 2>&1
if ($LASTEXITCODE -ne 0) {
    $build | Write-Output
    throw "Navigator JavaScript JS7 test build failed."
}

& $exe
if ($LASTEXITCODE -ne 0) {
    throw "Navigator JavaScript JS7 tests failed."
}
Write-Output "Navigator JavaScript JS7 smoke PASS"
