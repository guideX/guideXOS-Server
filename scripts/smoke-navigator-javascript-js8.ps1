param(
    [string]$Root = (Split-Path -Parent $PSScriptRoot)
)

$ErrorActionPreference = "Stop"
Set-Location -LiteralPath $Root

$outputDir = Join-Path $env:TEMP "guidex-navigator-javascript-js8-test"
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
$exe = Join-Path $outputDir "navigator_javascript_js8_test.exe"

# The deterministic fixture uses the parser's local/bare-metal translation
# path so the proof does not pull network stylesheet loading into the test.
# These warnings are pre-existing in the large parser translation unit; the
# JS8 adapter/runtime itself is compiled separately with full -Werror below.
$build = & g++ -std=c++17 -O2 -Wall -Wextra -Werror -pedantic `
    -Wno-unused-parameter -Wno-missing-field-initializers -Wno-unused-function `
    -DGXOS_BARE_METAL -iquote . `
    tests/navigator_javascript_js8_test.cpp `
    guide_web_html_parser.cpp `
    navigator_javascript/value.cpp `
    navigator_javascript/host.cpp `
    navigator_javascript/environment.cpp `
    navigator_javascript/runtime.cpp `
    navigator_javascript/ast.cpp `
    navigator_javascript/lexer.cpp `
    navigator_javascript/parser.cpp `
    navigator_javascript/navigator_script_host.cpp -o $exe 2>&1
if ($LASTEXITCODE -ne 0) {
    $build | Write-Output
    throw "Navigator JavaScript JS8 test build failed."
}

& $exe
if ($LASTEXITCODE -ne 0) {
    throw "Navigator JavaScript JS8 tests failed."
}

$strict = & g++ -std=c++17 -Wall -Wextra -Werror -pedantic -iquote . `
    -fsyntax-only `
    navigator_javascript/value.cpp `
    navigator_javascript/host.cpp `
    navigator_javascript/environment.cpp `
    navigator_javascript/runtime.cpp `
    navigator_javascript/ast.cpp `
    navigator_javascript/lexer.cpp `
    navigator_javascript/parser.cpp `
    navigator_javascript/navigator_script_host.cpp 2>&1
if ($LASTEXITCODE -ne 0) {
    $strict | Write-Output
    throw "Navigator JavaScript JS8 strict adapter/runtime build failed."
}

Write-Output "Navigator JavaScript JS8 smoke PASS"
