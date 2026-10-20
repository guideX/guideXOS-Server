param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$compiler = Get-Command g++.exe -ErrorAction Stop

& powershell.exe -NoProfile -ExecutionPolicy Bypass `
    -File (Join-Path $PSScriptRoot 'run-network-phase26r-tests.ps1')
if ($LASTEXITCODE -ne 0) { throw 'Phase 11-26R network regression tests failed' }

$testDir = Join-Path ([System.IO.Path]::GetTempPath()) 'guidex-phase27-network-tests'
New-Item -ItemType Directory -Path $testDir -Force | Out-Null
$testExe = Join-Path $testDir 'network_tx_phase27_test.exe'
& $compiler.Source -std=c++14 -O2 -Wall -Wextra `
    -I (Join-Path $root 'kernel/core/include') `
    -I (Join-Path $root 'kernel/arch/amd64/include') `
    (Join-Path $root 'tests/network_tx_phase27_test.cpp') -o $testExe
if ($LASTEXITCODE -ne 0) { throw 'network_tx_phase27_test compile failed' }
& $testExe
if ($LASTEXITCODE -ne 0) { throw 'network_tx_phase27_test failed' }

function Require([string]$text, [string]$pattern, [string]$message) {
    if ($text -notmatch $pattern) { throw $message }
}

$nic = Get-Content -LiteralPath (Join-Path $root 'kernel/core/nic.cpp') -Raw
$nicHeader = Get-Content -LiteralPath (Join-Path $root 'kernel/core/include/kernel/nic.h') -Raw
$shell = Get-Content -LiteralPath (Join-Path $root 'kernel/core/shell.cpp') -Raw
$shellHeader = Get-Content -LiteralPath (Join-Path $root 'kernel/core/include/kernel/shell.h') -Raw

$probeStart = $nic.IndexOf('bool run_i219_iommu_tx_observation()')
$probeEnd = $nic.IndexOf('// ================================================================', $probeStart)
if ($probeStart -lt 0 -or $probeEnd -le $probeStart) {
    throw 'Phase 27 TX probe source region was not found'
}
$probe = $nic.Substring($probeStart, $probeEnd - $probeStart)

Require $probe 'i219_phase27_gate_failure\(gates\)' `
    'the physical probe does not refuse failed pre-transmission gates'
Require $probe 'translation is active and the I219 context mapping is unknown' `
    'active VT-d translation with an unknown I219 context is not refused'
Require $probe 'i219_phase27_iommu_mapping_proven_safe' `
    'missing DMAR is not handled as unknown IOMMU translation state'
Require $probe 's_phase27TxProbeConsumed\s*=\s*true' `
    'the physical probe is not latched to one attempt per boot'
Require $probe 'send_raw_diagnostic_frame\(TxRawPath::Normal\)' `
    'the probe no longer reuses the established raw-TX submission path'
if (([regex]::Matches($probe, 'send_raw_diagnostic_frame\(TxRawPath::Normal\)')).Count -ne 1) {
    throw 'Phase 27 must have exactly one submission call and no retry'
}
if ($probe -match 'run_i219_reset_and_rearm|run_i219_post_reset_rearm|mmio_write32\([^\n]*E1000_STATUS|\bfree\s*\(|operator delete') {
    throw 'Phase 27 probe contains a reset/rearm action, STATUS write, or DMA-resource release'
}
Require $probe 'gates\.ringIdle\s*=\s*before\.valid' `
    'the probe does not check current TDH/TDT ring ownership'
Require $probe 'gates\.noCompetingTx\s*=\s*s_device\.stats\.txAttempted\s*==\s*0u' `
    'the probe does not refuse previously observed TX activity'
Require $probe 'validate_tx_dma_layout\(&dmaFailure\)' `
    'the probe does not revalidate the active DMA mapping before publication'
Require $probe 'descriptorDmaAddressValidBefore' `
    'the probe does not validate the intended descriptor DMA address before publication'
Require $probe 'bufferDmaAddressValidBefore' `
    'the probe does not validate the packet-buffer DMA address before publication'
Require $probe 'capture_tx_descriptor_raw\([\s\S]*?descriptorRaw0Before' `
    'the probe does not capture descriptor ownership before publication'
Require $probe 'dma_completion_barrier\(\);\s*s_device\.tx\.lastDescriptorStatus' `
    'the probe does not observe descriptor status after the bounded attempt'
Require $nic 'TX_COMPLETION_POLL_LIMIT\s*=\s*1000000u' `
    'the existing documented finite completion poll bound changed'
Require $shellHeader 'NICINFO_MODE_TX_IOMMU_ISOLATED' `
    'the explicit isolated-network confirmation command is missing'
Require $shell 'tx iommu: read-only Phase 27 preflight' `
    'the unconfirmed command is not read-only'
Require $shell 'tx iommu isolated: one bounded probe' `
    'the single transmission command is not documented in shell help'
Require $shell 'descriptor DMA preflight VA=' `
    'the pre-publication descriptor DMA address is not printed'
Require $shell 'buffer DMA preflight VA=' `
    'the pre-publication buffer DMA mapping is not printed'

$sendStart = $nic.IndexOf('Status send_frame(const uint8_t* data, uint16_t len)')
$sendEnd = $nic.IndexOf('Status send_raw_diagnostic_frame(', $sendStart)
if ($sendStart -lt 0 -or $sendEnd -le $sendStart) {
    throw 'production TX wrapper source region was not found'
}
$sendWrapper = $nic.Substring($sendStart, $sendEnd - $sendStart)
Require $sendWrapper 'return submit_frame\(data, len\);' `
    'Phase 27 unexpectedly changed the production send_frame wrapper'

Write-Host 'Phase 27 bounded TX gates, outcome logic, and hosted regressions PASS.' -ForegroundColor Green
