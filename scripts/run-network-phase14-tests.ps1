param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$compiler = Get-Command g++.exe -ErrorAction Stop

# Keep the complete Phase 11/12/13 regression gate in the Phase 14 gate.
& powershell.exe -NoProfile -ExecutionPolicy Bypass `
    -File (Join-Path $PSScriptRoot 'run-network-phase13-tests.ps1')
if ($LASTEXITCODE -ne 0) { throw 'Phase 11/12/13 network regression tests failed' }

$testDir = Join-Path ([System.IO.Path]::GetTempPath()) 'guidex-phase14-network-tests'
New-Item -ItemType Directory -Path $testDir -Force | Out-Null
$dmaExe = Join-Path $testDir 'network_tx_phase14_dma_fetch_test.exe'
& $compiler.Source -std=c++14 -O2 -Wall -Wextra `
    -I (Join-Path $root 'kernel/core/include') `
    -I (Join-Path $root 'kernel/arch/amd64/include') `
    (Join-Path $root 'tests/network_tx_phase14_dma_fetch_test.cpp') -o $dmaExe
if ($LASTEXITCODE -ne 0) { throw 'network_tx_phase14_dma_fetch_test compile failed' }
& $dmaExe
if ($LASTEXITCODE -ne 0) { throw 'network_tx_phase14_dma_fetch_test failed' }

Write-Host 'Phase 14 TX DMA/fetch provenance tests PASS.' -ForegroundColor Green
