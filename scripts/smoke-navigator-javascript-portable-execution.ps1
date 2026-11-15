$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $Root

$outputDir = Join-Path $env:TEMP "guidex-navigator-javascript-js120-portable-objects-arrays"
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
$exe = Join-Path $outputDir "navigator_javascript_portable_execution_test.exe"

$build = & g++ -std=c++17 -O2 -Wall -Wextra -Werror -pedantic -iquote . `
    tests/navigator_javascript_portable_execution_test.cpp `
    navigator_javascript/lexer.cpp `
    navigator_javascript/ast.cpp `
    navigator_javascript/parser.cpp `
    navigator_javascript/value.cpp `
    navigator_javascript/environment.cpp `
    navigator_javascript/object_storage.cpp `
    -o $exe 2>&1
if ($LASTEXITCODE -ne 0) {
    $build | Write-Output
    throw "Navigator JavaScript portable execution test build failed."
}

& $exe
if ($LASTEXITCODE -ne 0) {
    throw "Navigator JavaScript portable execution tests failed."
}
Write-Output "Navigator JavaScript portable execution smoke PASS"
