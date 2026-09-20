param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$compiler = Get-Command g++.exe -ErrorAction Stop

# Preserve every Phase 11-20 regression before adding the Phase 21 checks.
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'run-network-phase20-tests.ps1')
if ($LASTEXITCODE -ne 0) { throw 'Phase 11-20 network regression tests failed' }

$testDir = Join-Path ([System.IO.Path]::GetTempPath()) 'guidex-phase21-network-tests'
New-Item -ItemType Directory -Path $testDir -Force | Out-Null
$testExe = Join-Path $testDir 'network_tx_phase21_vtd_test.exe'
& $compiler.Source -std=c++14 -O2 -Wall -Wextra `
    -I (Join-Path $root 'kernel/core/include') `
    -I (Join-Path $root 'kernel/arch/amd64/include') `
    (Join-Path $root 'tests/network_tx_phase21_vtd_test.cpp') -o $testExe
if ($LASTEXITCODE -ne 0) { throw 'network_tx_phase21_vtd_test compile failed' }
& $testExe
if ($LASTEXITCODE -ne 0) { throw 'network_tx_phase21_vtd_test failed' }

function Require([string]$text, [string]$pattern, [string]$message) {
    if ($text -notmatch $pattern) { throw $message }
}

$vtdHeader = Get-Content -LiteralPath (Join-Path $root 'kernel/core/include/kernel/vtd.h') -Raw
$vtdSource = Get-Content -LiteralPath (Join-Path $root 'kernel/core/vtd.cpp') -Raw
$nicHeader = Get-Content -LiteralPath (Join-Path $root 'kernel/core/include/kernel/nic.h') -Raw
$nicSource = Get-Content -LiteralPath (Join-Path $root 'kernel/core/nic.cpp') -Raw
$shellHeader = Get-Content -LiteralPath (Join-Path $root 'kernel/core/include/kernel/shell.h') -Raw
$shellSource = Get-Content -LiteralPath (Join-Path $root 'kernel/core/shell.cpp') -Raw
$loaderSource = Get-Content -LiteralPath (Join-Path $root 'guideXOSBootLoader/main.cpp') -Raw

Require $vtdHeader 'parse_dmar_table' 'bounded DMAR parser is missing'
Require $vtdHeader 'translation_enabled' 'GSTS.TES decoder is missing'
Require $vtdHeader 'fault_source_id' 'VT-d fault source decoder is missing'
Require $vtdHeader 'classify_fault_address' 'ring/buffer fault classifier is missing'
Require $vtdSource 'capture_unit' 'VT-d register capture is missing'
Require $vtdSource 'ACPI DMAR table is not present' 'DMAR absence classification is missing'
if ($vtdSource -match '(?i)\b(mmio_)?write(8|16|32|64)\b|\bpci_write\b') {
    throw 'VT-d audit source contains a write primitive'
}

Require $nicHeader 'struct I219IommuDiagnostics' 'I219 VT-d diagnostics state is missing'
Require $nicHeader 'run_i219_iommu_tx_observation' 'I219 VT-d observation API is missing'
Require $nicSource 'freshStateRequired' 'fresh reset/rearm prerequisite is missing'
Require $nicSource 's_device.resetDiagnostics.rearmCompleted' 'Phase 20 rearm prerequisite is missing'
Require $nicSource 's_txPoisoned' 'poison guard is missing from VT-d observation'
Require $nicSource 'send_raw_diagnostic_frame\(TxRawPath::Normal\)' 'normal raw-TX boundary is missing'
if ($nicSource -match 'run_i219_iommu_tx_observation\([\s\S]*send_raw_diagnostic_frame\(TxRawPath::Normal\)[\s\S]*send_raw_diagnostic_frame\(TxRawPath::Normal\)') {
    throw 'Phase 21 VT-d observation contains a retry'
}
if ($nicSource -match 'E1000_TXD_CMD_DEXT') {
    throw 'Phase 21 must remain on the Phase 20 legacy descriptor ABI'
}

Require $shellHeader 'NICINFO_MODE_DMA' 'nicinfo dma mode is missing'
Require $shellHeader 'NICINFO_MODE_DMA_BRIEF' 'nicinfo dma brief mode is missing'
Require $shellHeader 'NICINFO_MODE_TX_IOMMU' 'nicinfo tx iommu mode is missing'
Require $shellSource 'cmd_nicinfo_dma' 'nicinfo dma command is missing'
Require $shellSource 'cmd_nicinfo_dma_brief' 'nicinfo dma brief command is missing'
Require $shellSource 'cmd_nicinfo_tx_iommu' 'nicinfo tx iommu command is missing'
Require $shellSource 'no IOMMU writes' 'read-only VT-d help text is missing'
Require $shellSource 'never retries' 'single-attempt help text is missing'

$dmaStart = $shellSource.IndexOf('static void cmd_nicinfo_dma_report(bool brief)')
$dmaEnd = $shellSource.IndexOf('static void cmd_nicinfo_dma()', $dmaStart)
if ($dmaStart -lt 0 -or $dmaEnd -le $dmaStart) { throw 'DMA report source region was not found' }
$dmaSource = $shellSource.Substring($dmaStart, $dmaEnd - $dmaStart)
if ($dmaSource -match '(?i)mmio_write|pci_write|run_i219|send_raw') {
    throw 'nicinfo dma report is not read-only'
}

Require $loaderSource 'CollectAcpiIdentityMapPlan' 'loader ACPI mapping plan is missing'
Require $loaderSource 'MapUncachedIdentityRange' 'loader VT-d uncached mapping is missing'
Require $loaderSource 'vtdBases' 'loader does not retain DRHD register bases'

Write-Host 'Phase 21 VT-d / DMA audit tests PASS' -ForegroundColor Green
