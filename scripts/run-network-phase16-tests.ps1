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

# Keep provisioning fail-closed and prove that the enabled experiment uses the
# loader-selected allocation, not a guessed physical address. On allocation
# or ownership failure the handoff must stay empty, which the kernel rejects.
$allocationStart = $loaderSource.IndexOf('// Phase 16 controlled TX DMA placement.')
$allocationEnd = $loaderSource.IndexOf('// --- Build identity-mapped page tables', $allocationStart)
if ($allocationStart -lt 0 -or $allocationEnd -le $allocationStart) {
    throw 'Phase 16 TX DMA allocator source boundary was not found'
}
$allocationSource = $loaderSource.Substring($allocationStart, $allocationEnd - $allocationStart)
Require $allocationSource 'GXOS_I219_TX_DMA_PLACEMENT_EXPERIMENT\s*!=\s*0' `
    'TX DMA allocation is not explicitly gated by the placement experiment'
Require $allocationSource 'AllocatePages\([\s\S]*?AllocateMaxAddress[\s\S]*?EfiLoaderData' `
    'TX DMA allocation is not firmware-selected EfiLoaderData below the bound'
Require $allocationSource 'if\s*\(txDmaReservationValid\)[\s\S]*?TxDmaRegion\.Base\s*=\s*txDmaMaxAddress' `
    'BootInfo does not receive the verified allocator-selected base'
Require $allocationSource 'else\s*\{[\s\S]*?FreePages[\s\S]*?SetMem\(&v1BootInfo->TxDmaRegion' `
    'failed/invalid allocation does not clear the DMA handoff safely'

$txStart = $nicSource.IndexOf('static bool init_tx(')
$txEnd = $nicSource.IndexOf('void set_kernel_physical_base', $txStart)
if ($txStart -lt 0 -or $txEnd -le $txStart) {
    throw 'TX initialization source boundary was not found'
}
$txSource = $nicSource.Substring($txStart, $txEnd - $txStart)
$experimentGate = $txSource.IndexOf('!tx_dma_experiment_active(s_txDmaMode)')
$layoutCheck = $txSource.IndexOf('validate_tx_dma_layout(&layoutFailure)')
$addressCheck = $txSource.IndexOf('dma_address_range(s_txDescs')
$ringProgram = $txSource.IndexOf('mmio_write32(mmioBase, E1000_TDBAL')
$ringReady = $txSource.IndexOf('s_device.txRingInitialized = true')
if ($experimentGate -lt 0 -or $layoutCheck -le $experimentGate -or
    $addressCheck -le $layoutCheck -or $ringProgram -le $addressCheck -or
    $ringReady -le $ringProgram) {
    throw 'TX ring programming/readiness no longer follows explicit DMA validation'
}

$initStart = $nicSource.IndexOf('static bool init_e1000(')
$initEnd = $nicSource.IndexOf('// PCI bus scan for network controllers', $initStart)
$bootStart = $nicSource.IndexOf('bool init_from_bootinfo(')
$bootEnd = $nicSource.IndexOf('// Public API', $bootStart)
if ($initStart -lt 0 -or $initEnd -le $initStart -or
    $bootStart -lt 0 -or $bootEnd -le $bootStart) {
    throw 'NIC initialization/registration source boundaries were not found'
}
$initSource = $nicSource.Substring($initStart, $initEnd - $initStart)
$bootSource = $nicSource.Substring($bootStart, $bootEnd - $bootStart)
Require $initSource 'if\s*\(!init_tx\(mmioBase\)\)[\s\S]*?return false;[\s\S]*?capture_i219_lan_init_post_init' `
    'failed TX initialization must return before the post-init checkpoint'
$initResult = $bootSource.IndexOf('if (!init_e1000(s_device.mmioBase))')
$completionGate = $bootSource.IndexOf('if (!hardware_init_complete(s_device))')
$registration = $bootSource.IndexOf('s_device.nicRegistered = true')
if ($initResult -lt 0 -or $completionGate -le $initResult -or
    $registration -le $completionGate) {
    throw 'NIC registration no longer requires successful hardware and TX initialization'
}

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

Write-Host "Phase 16 DMA-placement/provisioning tests PASS (brief newline sites=$lineCount)." -ForegroundColor Green
