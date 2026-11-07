$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$outDir = Join-Path $env:TEMP 'guidex-navhttps3-dns-smoke'
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$exe = Join-Path $outDir 'dns_resolution_test.exe'

Push-Location $root
try {
    & g++ -std=c++14 -O2 -Wall -Wextra `
        -DGXOS_NAVIGATOR_GUEST_DNS_QUALIFICATION_ACTIVE `
        -I (Join-Path $root 'kernel\core\include') `
        -I (Join-Path $root 'kernel\arch\amd64\include') `
        -include (Join-Path $root 'tests\dns_serial_debug_stub.h') `
        (Join-Path $root 'tests\dns_resolution_test.cpp') `
        (Join-Path $root 'kernel\core\dns.cpp') -o $exe
    if ($LASTEXITCODE -ne 0) { throw 'DNS resolution test build failed.' }
    & $exe
    if ($LASTEXITCODE -ne 0) { throw "DNS resolution protocol tests failed with code $LASTEXITCODE." }
    $python = Get-Command python -ErrorAction SilentlyContinue
    if (-not $python) { throw 'Python is required for the controlled DNS fixture smoke.' }
    & $python.Source (Join-Path $root 'scripts\test_navigator_controlled_dns_fixture.py')
    if ($LASTEXITCODE -ne 0) { throw "Controlled DNS fixture tests failed with code $LASTEXITCODE." }
} finally {
    Pop-Location
}

Write-Output 'DNS resolution smoke PASS (13 protocol scenarios + 6 controlled fixture checks)'
