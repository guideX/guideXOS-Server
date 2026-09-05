param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$compiler = Get-Command g++.exe -ErrorAction Stop

# Keep the complete Phase 11/12 regression gate in the Phase 13 gate.
& powershell.exe -NoProfile -ExecutionPolicy Bypass `
    -File (Join-Path $PSScriptRoot 'run-network-phase12-tests.ps1')
if ($LASTEXITCODE -ne 0) { throw 'Phase 11/12 network regression tests failed' }

$testDir = Join-Path ([System.IO.Path]::GetTempPath()) 'guidex-phase13-network-tests'
New-Item -ItemType Directory -Path $testDir -Force | Out-Null
$txExe = Join-Path $testDir 'network_tx_phase13_test.exe'
& $compiler.Source -std=c++14 -O2 -Wall -Wextra `
    -I (Join-Path $root 'kernel/core/include') `
    -I (Join-Path $root 'kernel/arch/amd64/include') `
    (Join-Path $root 'tests/network_tx_phase13_test.cpp') -o $txExe
if ($LASTEXITCODE -ne 0) { throw 'network_tx_phase13_test compile failed' }
& $txExe
if ($LASTEXITCODE -ne 0) { throw 'network_tx_phase13_test failed' }

Write-Host 'Phase 13 TX tests PASS.' -ForegroundColor Green
