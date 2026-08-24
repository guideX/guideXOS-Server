param(
    [string]$Root = (Split-Path -Parent $PSScriptRoot)
)

$ErrorActionPreference = "Stop"
Set-Location -LiteralPath $Root

$outputDir = Join-Path $env:TEMP "guidex-navigator-javascript-lexer-test"
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
$exe = Join-Path $outputDir "navigator_javascript_lexer_test.exe"

$build = & g++ -std=c++17 -O2 -Wall -Wextra -pedantic -iquote . `
    tests/navigator_javascript_lexer_test.cpp `
    navigator_javascript/lexer.cpp -o $exe 2>&1
if ($LASTEXITCODE -ne 0) {
    $build | Write-Output
    throw "Navigator JavaScript lexer test build failed."
}

& $exe
if ($LASTEXITCODE -ne 0) {
    throw "Navigator JavaScript lexer tests failed."
}
Write-Output "Navigator JavaScript lexer smoke PASS"
