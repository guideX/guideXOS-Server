param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$compiler = Get-Command g++.exe -ErrorAction Stop

# Preserve every Phase 11-19 regression before adding the Phase 20 checks.
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'run-network-phase19-tests.ps1')
if ($LASTEXITCODE -ne 0) { throw 'Phase 11-19 network regression tests failed' }

$testDir = Join-Path ([System.IO.Path]::GetTempPath()) 'guidex-phase20-network-tests'
New-Item -ItemType Directory -Path $testDir -Force | Out-Null
$testExe = Join-Path $testDir 'network_tx_phase20_lifecycle_test.exe'
& $compiler.Source -std=c++14 -O2 -Wall -Wextra -I (Join-Path $root 'kernel/core/include') -I (Join-Path $root 'kernel/arch/amd64/include') (Join-Path $root 'tests/network_tx_phase20_lifecycle_test.cpp') -o $testExe
if ($LASTEXITCODE -ne 0) { throw 'network_tx_phase20_lifecycle_test compile failed' }
& $testExe
if ($LASTEXITCODE -ne 0) { throw 'network_tx_phase20_lifecycle_test failed' }

function Require([string]$text, [string]$pattern, [string]$message) {
    if ($text -notmatch $pattern) { throw $message }
}

$headerSource = Get-Content -LiteralPath (Join-Path $root 'kernel/core/include/kernel/nic.h') -Raw
$shellHeaderSource = Get-Content -LiteralPath (Join-Path $root 'kernel/core/include/kernel/shell.h') -Raw
$nicSource = Get-Content -LiteralPath (Join-Path $root 'kernel/core/nic.cpp') -Raw
$shellSource = Get-Content -LiteralPath (Join-Path $root 'kernel/core/shell.cpp') -Raw

Require $headerSource 'PCI_CONFIG_DESC_RING_STATUS\s*=\s*0x00E4' 'PCI 0xE4 definition is missing'
Require $headerSource 'PCI_CONFIG_FLUSH_DESC_REQUIRED\s*=\s*0x0100' 'flush-required bit definition is missing'
Require $headerSource 'E1000_CTRL_GIO_MASTER_DISABLE\s*=\s*0x00000004' 'CTRL.GIO_MASTER_DISABLE definition is missing'
Require $headerSource 'E1000_STATUS_GIO_MASTER_ENABLE\s*=\s*0x00080000' 'STATUS.GIO master definition is missing'
Require $headerSource 'enum class I219RearmFailureReason' 'post-reset rearm failure taxonomy is missing'
Require $headerSource 'bool run_i219_reset_and_rearm\(\)' 'explicit destructive reset/rearm API is missing'
Require $headerSource 'bool run_i219_post_reset_rearm\(\)' 'explicit post-reset rearm API is missing'

Require $nicSource 'snapshot\.status\s*=\s*mmio_read32' 'STATUS/GIO master snapshot is missing'
Require $nicSource 'static bool rearm_i219_after_reset' 'bounded post-reset rearm helper is missing'
Require $nicSource 'run_i219_reset_and_rearm' 'destructive reset/rearm implementation is missing'
Require $nicSource 'run_i219_post_reset_rearm' 'no-reset rearm implementation is missing'
Require $nicSource 's_txDmaRegionHandoffValid' 'constrained DMA handoff is not required by rearm'
Require $nicSource 'rearmTxDisabledBeforeBuild' 'TX disable-before-build evidence is missing'
Require $nicSource 'rearmRegisterReadback' 'post-rearm register readback evidence is missing'
Require $shellSource 'E1000_STATUS_GIO_MASTER_ENABLE' 'GIO master status definition is not available to diagnostics'

if ($nicSource -match 'mmio_write32\([^;]*E1000_PBA') { throw 'Phase 20 must not speculatively write PBA' }
if ($nicSource -match 'mmio_write32\([^;]*E1000_FWSM|mmio_write32\([^;]*E1000_SWSM') { throw 'Phase 20 must not write firmware/SWSM ownership state' }
if ($nicSource -match 'E1000_TXD_CMD_DEXT') { throw 'Phase 20 must remain on the legacy descriptor ABI' }

$initStart = $nicSource.IndexOf('static bool init_e1000(')
$initEnd = $nicSource.IndexOf('// ================================================================', $initStart + 1)
if ($initStart -lt 0 -or $initEnd -le $initStart) { throw 'init_e1000 source region was not found' }
$initSource = $nicSource.Substring($initStart, $initEnd - $initStart)
$initialCall = $initSource.IndexOf('capture_i219_hw_control_initial')
$resetCall = $initSource.IndexOf('i219_pch_reset(mmioBase)')
$afterResetCall = $initSource.IndexOf('capture_i219_hw_control_after_reset')
$acquireCall = $initSource.IndexOf('acquire_i219_hw_control')
$rxCall = $initSource.IndexOf('init_rx(mmioBase)')
$txCall = $initSource.IndexOf('init_tx(mmioBase)')
$finalCall = $initSource.IndexOf('verify_i219_hw_control_final')
if ($initialCall -lt 0 -or $resetCall -lt 0 -or $afterResetCall -lt 0 -or $acquireCall -lt 0 -or $rxCall -lt 0 -or $txCall -lt 0 -or $finalCall -lt 0 -or -not ($initialCall -lt $resetCall -and $resetCall -lt $afterResetCall -and $afterResetCall -lt $acquireCall -and $acquireCall -lt $rxCall -and $rxCall -lt $txCall -and $txCall -lt $finalCall)) {
    throw 'boot I219 lifecycle ordering changed'
}

$rearmForward = $nicSource.IndexOf('static bool rearm_i219_after_reset')
$rearmStart = $nicSource.IndexOf('static bool rearm_i219_after_reset',
                                  $rearmForward + 1)
$rearmEnd = $nicSource.IndexOf('bool run_i219_post_reset_rearm()', $rearmStart)
if ($rearmStart -lt 0 -or $rearmEnd -le $rearmStart) { throw 'post-reset rearm source region was not found' }
$rearmSource = $nicSource.Substring($rearmStart, $rearmEnd - $rearmStart)
$pciCheck = $rearmSource.IndexOf('pci_dma_access_enabled')
$ownerAcquire = $rearmSource.IndexOf('acquire_i219_hw_control')
$rxRebuild = $rearmSource.IndexOf('init_rx(mmioBase)')
$txRebuild = $rearmSource.IndexOf('init_tx(mmioBase, true)')
$ownerFinal = $rearmSource.IndexOf('verify_i219_hw_control_final')
if ($pciCheck -lt 0 -or $ownerAcquire -lt 0 -or $rxRebuild -lt 0 -or $txRebuild -lt 0 -or $ownerFinal -lt 0 -or -not ($pciCheck -lt $ownerAcquire -and $ownerAcquire -lt $rxRebuild -and $rxRebuild -lt $txRebuild -and $txRebuild -lt $ownerFinal)) {
    throw 'post-reset rearm ordering is not PCI -> ownership -> RX -> TX -> final'
}

$txStart = $nicSource.IndexOf('static bool init_tx(')
$txEnd = $nicSource.IndexOf('void set_kernel_physical_base', $txStart)
if ($txStart -lt 0 -or $txEnd -le $txStart) { throw 'TX initialization source region was not found' }
$txSource = $nicSource.Substring($txStart, $txEnd - $txStart)
$disable = $txSource.IndexOf('rearmTxDisabledBeforeBuild')
$tdbal = $txSource.IndexOf('E1000_TDBAL')
$txEnable = $txSource.IndexOf('E1000_TCTL, tctl')
$readback = $txSource.IndexOf('rearmRegisterReadback')
if ($disable -lt 0 -or $tdbal -lt 0 -or $txEnable -lt 0 -or $readback -lt 0 -or -not ($disable -lt $tdbal -and $tdbal -lt $txEnable -and $txEnable -lt $readback)) {
    throw 'TX rearm does not disable, rebuild, enable, and read back in order'
}

$resetRunStart = $nicSource.IndexOf('bool run_i219_reset_and_rearm()')
$resetRunEnd = $nicSource.IndexOf('#if !ARCH_HAS_PORT_IO', $resetRunStart)
$resetRunSource = $nicSource.Substring($resetRunStart, $resetRunEnd - $resetRunStart)
$resetCall = $resetRunSource.IndexOf('i219_pch_reset(s_device.mmioBase)')
$postCapture = $resetRunSource.IndexOf('capture_i219_hw_control_after_reset')
$rearmCall = $resetRunSource.IndexOf('rearm_i219_after_reset')
if ($resetCall -lt 0 -or $postCapture -lt 0 -or $rearmCall -lt 0 -or -not ($resetCall -lt $postCapture -and $postCapture -lt $rearmCall)) {
    throw 'explicit reset/rearm ordering is not reset -> capture/ownership -> rearm'
}

Require $shellHeaderSource 'NICINFO_MODE_TX_RESET_BRIEF' 'reset brief shell mode is missing'
Require $shellHeaderSource 'NICINFO_MODE_TX_RESET_RUN' 'explicit reset run shell mode is missing'
Require $shellHeaderSource 'NICINFO_MODE_TX_REARM' 'rearm shell mode is missing'
Require $shellHeaderSource 'NICINFO_MODE_TX_LIFECYCLE' 'lifecycle shell mode is missing'
Require $shellSource 'cmd_nicinfo_tx_reset_brief' 'reset brief command implementation is missing'
Require $shellSource 'cmd_nicinfo_tx_reset_run' 'explicit destructive reset command is missing'
Require $shellSource 'cmd_nicinfo_tx_rearm' 'rearm command implementation is missing'
Require $shellSource 'cmd_nicinfo_tx_lifecycle' 'lifecycle command implementation is missing'
Require $shellSource 'READ-ONLY; does not reset' 'read-only reset semantics are not prominent'
Require $shellSource 'EXPLICIT destructive reset' 'destructive reset syntax is not prominent'

$briefStart = $shellSource.IndexOf('static void cmd_nicinfo_tx_reset_brief()')
$briefEnd = $shellSource.IndexOf('static const nic::TxRegisterSnapshot*', $briefStart)
$briefSource = $shellSource.Substring($briefStart, $briefEnd - $briefStart)
$briefLines = ([regex]::Matches($briefSource, '\\n')).Count
if ($briefLines -gt 12) { throw "reset brief emits $briefLines newline sites; maximum is 12" }

$lifecycleStart = $shellSource.IndexOf('static void cmd_nicinfo_tx_lifecycle()')
$lifecycleEnd = $shellSource.IndexOf('static void cmd_nicinfo_tx_reset_run()', $lifecycleStart)
$lifecycleSource = $shellSource.Substring($lifecycleStart, $lifecycleEnd - $lifecycleStart)
$lifecycleLines = ([regex]::Matches($lifecycleSource, '\\n')).Count
if ($lifecycleLines -gt 20) { throw "lifecycle emits $lifecycleLines newline sites; maximum is 20" }

if ($shellSource -match 'cmd_nicinfo_tx_raw[\s\S]{0,800}(dhcp|udp|arp|route)') { throw 'Phase 20 raw command is coupled to a higher network protocol' }

Write-Host ("Phase 20 lifecycle tests PASS " + "(reset brief newline sites=$briefLines, lifecycle newline sites=$lifecycleLines).") -ForegroundColor Green
