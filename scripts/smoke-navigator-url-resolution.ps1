$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$outDir = Join-Path $env:TEMP 'guidex-phase8r-smoke'
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$exe = Join-Path $outDir 'navigator_url_resolution_test.exe'

g++ -std=c++17 -Wall -Wextra -DGXOS_BARE_METAL -iquote $root `
    (Join-Path $root 'tests/navigator_url_resolution_test.cpp') `
    (Join-Path $root 'guide_web_html_parser.cpp') -o $exe
if ($LASTEXITCODE -ne 0) { throw 'URL resolution test build failed.' }

& $exe
if ($LASTEXITCODE -ne 0) { throw 'URL resolution test failed.' }
Write-Output 'Navigator URL resolution smoke PASS'
