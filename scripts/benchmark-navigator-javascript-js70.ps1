$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $Root

$outputDir = Join-Path $env:TEMP "guidex-navigator-javascript-js70-benchmark"
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
$exe = Join-Path $outputDir "navigator_javascript_js70_benchmark.exe"
$compileOutput = & g++ -std=c++17 -O2 -Wall -Wextra -Werror -pedantic `
    -Wno-unused-parameter -Wno-missing-field-initializers -Wno-unused-function `
    -DGXOS_BARE_METAL -iquote . `
    tests/navigator_javascript_js70_benchmark.cpp `
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
    $compileOutput | Write-Output
    throw "Navigator JavaScript JS70 benchmark build failed."
}

& $exe
if ($LASTEXITCODE -ne 0) { throw "Navigator JavaScript JS70 benchmark workload failed." }
