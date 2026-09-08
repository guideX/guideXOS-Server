param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$compiler = Get-Command g++.exe -ErrorAction Stop

# Preserve the complete Phase 11-15 regression gate.
& powershell.exe -NoProfile -ExecutionPolicy Bypass `
    -File (Join-Path $PSScriptRoot 'run-network-phase15-tests.ps1')
if ($LASTEXITCODE -ne 0) { throw 'Phase 11-15 network regression tests failed' }

$testDir = Join-Path ([System.IO.Path]::GetTempPath()) 'guidex-phase16-network-tests'
New-Item -ItemType Directory -Path $testDir -Force | Out-Null
$placementExe = Join-Path $testDir 'network_tx_phase16_dma_placement_test.exe'
& $compiler.Source -std=c++14 -O2 -Wall -Wextra `
    -I (Join-Path $root 'kernel/core/include') `
    -I (Join-Path $root 'kernel/arch/amd64/include') `
    (Join-Path $root 'tests/network_tx_phase16_dma_placement_test.cpp') -o $placementExe
if ($LASTEXITCODE -ne 0) { throw 'network_tx_phase16_dma_placement_test compile failed' }
& $placementExe
if ($LASTEXITCODE -ne 0) { throw 'network_tx_phase16_dma_placement_test failed' }

$headerPath = Join-Path $root 'kernel/core/include/kernel/nic.h'
$headerSource = Get-Content -LiteralPath $headerPath -Raw
$nicSource = Get-Content -LiteralPath (Join-Path $root 'kernel/core/nic.cpp') -Raw
$loaderSource = Get-Content -LiteralPath (Join-Path $root 'guideXOSBootLoader/main.cpp') -Raw
$bootInfoSource = Get-Content -LiteralPath (Join-Path $root 'guideXOSBootLoader/guidexOSBootInfo.h') -Raw
$buildSource = Get-Content -LiteralPath (Join-Path $root 'build.ps1') -Raw
$projectSource = Get-Content -LiteralPath (Join-Path $root 'guideXOSBootLoader/guideXOSBootLoader.vcxproj') -Raw

function Require([string]$text, [string]$pattern, [string]$message) {
    if ($text -notmatch $pattern) { throw $message }
}

Require $headerSource 'TX_DMA_REGION_SIZE\s*=\s*0x2000' 'two-page DMA region contract is missing'
Require $headerSource 'TX_DMA_REGION_MAX_EXCLUSIVE\s*=\s*0x100000000' 'below-4G bound is missing'
Require $headerSource 'tx_dma_region_owned_by_loader_memory_map' 'memory-map ownership proof helper is missing'
Require $headerSource 'tx_dma_region_handoff_valid' 'fail-closed handoff validator is missing'
Require $nicSource 'GXOS_I219_TX_DMA_PLACEMENT_EXPERIMENT' 'Phase 16 compile-time gate is missing'
Require $nicSource 'TxDmaMode::ConstrainedLow' 'constrained-low storage selection is missing'
Require $nicSource 's_rxDescs\[NUM_RX_DESC\]' 'RX static control placement disappeared'
Require $nicSource 'dma_publish_barrier' 'descriptor publication barrier changed or disappeared'
Require $nicSource 'dma_completion_barrier' 'completion polling barrier changed or disappeared'
Require $loaderSource 'AllocateMaxAddress' 'loader does not use a bounded max-address allocation'
Require $loaderSource 'EfiLoaderData' 'loader ownership type is missing'
Require $loaderSource 'MemoryMapContainsLoaderData' 'final memory-map ownership reconciliation is missing'
Require $bootInfoSource 'TxDmaRegionDescriptor' 'BootInfo TX DMA handoff is missing'
Require $buildSource 'I219TxDmaPlacementExperiment' 'canonical build switch is missing'
Require $projectSource 'GXOS_I219_TX_DMA_PLACEMENT_EXPERIMENT' 'bootloader project does not receive Phase 16 switch'

# Structural host check for the one-screen command. Count source-level
# newline emission sites in the normal and no-device branches.
$shellSource = Get-Content -LiteralPath (Join-Path $root 'kernel/core/shell.cpp') -Raw
$briefStart = $shellSource.IndexOf('static void cmd_nicinfo_tx_brief()')
$briefEnd = $shellSource.IndexOf('static void cmd_nicinfo_brief()', $briefStart)
if ($briefStart -lt 0 -or $briefEnd -le $briefStart) {
    throw 'nicinfo tx brief implementation was not found'
}
$briefSource = $shellSource.Substring($briefStart, $briefEnd - $briefStart)
$lineCount = ([regex]::Matches($briefSource, 'output_string\("[^"\r\n]*\\n"\)')).Count
if ($lineCount -gt 20) { throw "nicinfo tx brief source emits $lineCount newline sites; maximum is 20" }

Write-Host "Phase 16 DMA-placement tests PASS (brief newline sites=$lineCount)." -ForegroundColor Green
