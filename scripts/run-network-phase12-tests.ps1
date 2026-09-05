param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$compiler = Get-Command g++.exe -ErrorAction Stop

# Keep every Phase 11 DHCP/provenance/configuration test in the Phase 12
# gate, then add the register-only link decision tests below.
& powershell.exe -NoProfile -ExecutionPolicy Bypass `
    -File (Join-Path $PSScriptRoot 'run-network-phase11-tests.ps1')
if ($LASTEXITCODE -ne 0) { throw 'Phase 11 network regression tests failed' }

$testDir = Join-Path ([System.IO.Path]::GetTempPath()) 'guidex-phase12-network-tests'
New-Item -ItemType Directory -Path $testDir -Force | Out-Null
$linkExe = Join-Path $testDir 'network_link_state_test.exe'
& $compiler.Source -std=c++14 -O2 -Wall -Wextra `
    -I (Join-Path $root 'kernel/core/include') `
    -I (Join-Path $root 'kernel/arch/amd64/include') `
    (Join-Path $root 'tests/network_link_state_test.cpp') -o $linkExe
if ($LASTEXITCODE -ne 0) { throw 'network_link_state_test compile failed' }
& $linkExe
if ($LASTEXITCODE -ne 0) { throw 'network_link_state_test failed' }

Write-Host 'Phase 12 network/link tests PASS.' -ForegroundColor Green
