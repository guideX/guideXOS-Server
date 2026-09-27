param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$compiler = Get-Command g++.exe -ErrorAction Stop

& powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'run-network-phase23-tests.ps1')
if ($LASTEXITCODE -ne 0) { throw 'Phase 11-23 network regression tests failed' }

$testDir = Join-Path ([System.IO.Path]::GetTempPath()) 'guidex-phase24-network-tests'
New-Item -ItemType Directory -Path $testDir -Force | Out-Null
$testExe = Join-Path $testDir 'network_tx_phase24_no_dmar_test.exe'
& $compiler.Source -std=c++14 -O2 -Wall -Wextra `
    -I (Join-Path $root 'kernel/core/include') `
    -I (Join-Path $root 'kernel/arch/amd64/include') `
    (Join-Path $root 'tests/network_tx_phase24_no_dmar_test.cpp') -o $testExe
if ($LASTEXITCODE -ne 0) { throw 'network_tx_phase24_no_dmar_test compile failed' }
& $testExe
if ($LASTEXITCODE -ne 0) { throw 'network_tx_phase24_no_dmar_test failed' }

function Require([string]$text, [string]$pattern, [string]$message) {
    if ($text -notmatch $pattern) { throw $message }
}

$nic = Get-Content -LiteralPath (Join-Path $root 'kernel/core/nic.cpp') -Raw
$header = Get-Content -LiteralPath (Join-Path $root 'kernel/core/include/kernel/nic.h') -Raw
$shell = Get-Content -LiteralPath (Join-Path $root 'kernel/core/shell.cpp') -Raw

Require $header 'bool\s+dmarStatusKnown;\s*bool\s+dmarPresent;\s*bool\s+iommuApplicable;' `
    'DMAR presence and IOMMU applicability state is missing'
Require $nic 'const bool vtdDiscovered = vtd::discover\(target\);' `
    'VT-d discovery result is not retained'
Require $nic 'diagnostics\.iommuApplicable = audit && audit->dmarTablePresent &&\s*audit->matchingDrhdFound;' `
    'IOMMU applicability is not classified from DMAR/DRHD scope'
Require $nic 'diagnostics\.afterValid = diagnostics\.iommuApplicable &&\s*vtd::capture_registers' `
    'post-attempt VT-d register capture is not limited to applicable units'
Require $nic 'diagnostics\.attempted = true;\s*diagnostics\.result = result;' `
    'bounded TX attempt no longer records its single result'
Require $nic 'dma_publish_barrier\(\);\s*mmio_write32\(s_device\.mmioBase, E1000_TDT' `
    'the explicit publication barrier is not adjacent to the TDT write'

$observationStart = $nic.IndexOf('bool run_i219_iommu_tx_observation()')
$observationEnd = $nic.IndexOf('// ================================================================', $observationStart)
if ($observationStart -lt 0 -or $observationEnd -le $observationStart) {
    throw 'IOMMU TX observation source region was not found'
}
$observation = $nic.Substring($observationStart, $observationEnd - $observationStart)
if ($observation -match 'if\s*\(\s*!vtdDiscovered\s*\)\s*\{[^}]*return\s+false') {
    throw 'unavailable VT-d diagnostics still gate the raw TX experiment'
}
Require $observation 'send_raw_diagnostic_frame\(TxRawPath::Normal\)' `
    'the observation no longer uses its one existing raw TX boundary'

Require $shell 'IOMMU DMAR=' 'DMAR presence is not printed'
Require $shell 'iommu-applicable=' 'IOMMU applicability is not printed'
Require $shell 'IOMMU evidence-unavailable:' 'unavailable IOMMU evidence has no reason'
Require $shell 'TDBA == TX_RING_DMA_ADDRESS:' 'exact TDBA/ring physical address comparison is missing'
Require $shell 'descriptor-pre-TDT=raw16:' 'pre-TDT descriptor bytes are missing'
Require $shell 'buffer-pre-TDT-first32=' 'pre-TDT packet bytes are missing'
Require $shell 'immediate-readback=' 'immediate TDT readback is missing'
Require $shell 'final-TX' 'post-poll TX register snapshot is missing'
Require $shell 'descriptor-final-DD=' 'final descriptor DD state is missing'
Require $shell 'A_TDBA_MISMATCH|B_TDT_PUBLISHED_TDH_STATIONARY_DD_CLEAR|C_DESCRIPTOR_DD_SET|D_DESCRIPTOR_CHANGED_HEAD_STATIONARY' `
    'the report does not distinguish the required TX outcomes'
Require $shell 'unavailable \(no readable matching VT-d unit\)' `
    'invalid VT-d snapshots could print zero placeholders as evidence'

Write-Host 'Phase 24 no-DMAR I219 DMA-fetch tests PASS.' -ForegroundColor Green
