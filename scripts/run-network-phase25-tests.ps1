param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$compiler = Get-Command g++.exe -ErrorAction Stop

& powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'run-network-phase24-tests.ps1')
if ($LASTEXITCODE -ne 0) { throw 'Phase 11-24 network regression tests failed' }

$testDir = Join-Path ([System.IO.Path]::GetTempPath()) 'guidex-phase25-network-tests'
New-Item -ItemType Directory -Path $testDir -Force | Out-Null
$testExe = Join-Path $testDir 'network_tx_phase25_pcie_snoop_test.exe'
& $compiler.Source -std=c++14 -O2 -Wall -Wextra `
    -I (Join-Path $root 'kernel/core/include') `
    -I (Join-Path $root 'kernel/arch/amd64/include') `
    (Join-Path $root 'tests/network_tx_phase25_pcie_snoop_test.cpp') -o $testExe
if ($LASTEXITCODE -ne 0) { throw 'network_tx_phase25_pcie_snoop_test compile failed' }
& $testExe
if ($LASTEXITCODE -ne 0) { throw 'network_tx_phase25_pcie_snoop_test failed' }

function Require([string]$text, [string]$pattern, [string]$message) {
    if ($text -notmatch $pattern) { throw $message }
}

$nic = Get-Content -LiteralPath (Join-Path $root 'kernel/core/nic.cpp') -Raw
$header = Get-Content -LiteralPath (Join-Path $root 'kernel/core/include/kernel/nic.h') -Raw
$shell = Get-Content -LiteralPath (Join-Path $root 'kernel/core/shell.cpp') -Raw
$phase25Start = $nic.IndexOf('bool run_i219_spt_tx_snoop_experiment()')
$phase25End = $nic.IndexOf('// ================================================================', $phase25Start)
if ($phase25Start -lt 0 -or $phase25End -le $phase25Start) {
    throw 'Phase 25 experiment source region was not found'
}
$phase25 = $nic.Substring($phase25Start, $phase25End - $phase25Start)

Require $header 'E1000_GCR_TXD_NO_SNOOP\s*=\s*\(1u\s*<<\s*3\)' `
    'TX descriptor no-snoop control is missing'
Require $header 'E1000_GCR_TXDSCW_NO_SNOOP\s*=\s*\(1u\s*<<\s*4\)' `
    'TX descriptor write no-snoop control is missing'
Require $header 'E1000_GCR_TXDSCR_NO_SNOOP\s*=\s*\(1u\s*<<\s*5\)' `
    'TX descriptor/data no-snoop control is missing'
Require $phase25 'if\s*\(diagnostics\.started\)' `
    'Phase 25 does not guard against a second attempt'
Require $phase25 'if\s*\(!diagnostics\.candidateChanged\)' `
    'Phase 25 does not skip an already-compliant no-op experiment'
Require $phase25 'mmio_write32\(s_device\.mmioBase,\s*E1000_GCR,\s*diagnostics\.gcrRequested\)' `
    'Phase 25 GCR operation is missing'
Require $phase25 'send_raw_diagnostic_frame\(TxRawPath::Normal\)' `
    'Phase 25 does not use the existing bounded raw-TX boundary'
if ($phase25 -match 'vtd::discover|vtd::capture_registers') {
    throw 'Phase 25 unexpectedly depends on VT-d discovery/capture'
}
Require $nic 'mmio_write32\(s_device\.mmioBase, E1000_TDT, s_txCur\);\s*s_device\.tx\.immediateTdtReadback\s*=\s*static_cast<uint16_t>\(\s*mmio_read32\(s_device\.mmioBase, E1000_TDT\)' `
    'immediate TDT MMIO readback is not adjacent to the publication'
Require $shell 'descriptor-pre-TDT-decoded:' `
    'decoded legacy descriptor fields are not printed'
Require $shell 'TCTL_EXT=N/A-SPT-e1000e' `
    'non-applicable TCTL_EXT classification is missing'
Require $shell 'GCR old=0x' `
    'candidate old/new/readback values are not reported'
Require $shell 'OUTCOME_D_CANDIDATE_ALREADY_COMPLIANT' `
    'an already-compliant TX snoop policy is not classified'
Require $shell 'TIDV=0x' `
    'TX interrupt timer state is not captured'

Write-Host 'Phase 25 I219 TX snoop tests PASS.' -ForegroundColor Green
