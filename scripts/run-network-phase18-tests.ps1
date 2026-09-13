param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$compiler = Get-Command g++.exe -ErrorAction Stop

# Preserve the complete Phase 11-17 regression gate.
& powershell.exe -NoProfile -ExecutionPolicy Bypass `
    -File (Join-Path $PSScriptRoot 'run-network-phase17-tests.ps1')
if ($LASTEXITCODE -ne 0) { throw 'Phase 11-17 network regression tests failed' }

$testDir = Join-Path ([System.IO.Path]::GetTempPath()) 'guidex-phase18-network-tests'
New-Item -ItemType Directory -Path $testDir -Force | Out-Null
$controlExe = Join-Path $testDir 'network_tx_phase18_hw_control_test.exe'
& $compiler.Source -std=c++14 -O2 -Wall -Wextra `
    -I (Join-Path $root 'kernel/core/include') `
    -I (Join-Path $root 'kernel/arch/amd64/include') `
    (Join-Path $root 'tests/network_tx_phase18_hw_control_test.cpp') -o $controlExe
if ($LASTEXITCODE -ne 0) { throw 'network_tx_phase18_hw_control_test compile failed' }
& $controlExe
if ($LASTEXITCODE -ne 0) { throw 'network_tx_phase18_hw_control_test failed' }

function Require([string]$text, [string]$pattern, [string]$message) {
    if ($text -notmatch $pattern) { throw $message }
}

$headerSource = Get-Content -LiteralPath (Join-Path $root 'kernel/core/include/kernel/nic.h') -Raw
$shellHeaderSource = Get-Content -LiteralPath (Join-Path $root 'kernel/core/include/kernel/shell.h') -Raw
$nicSource = Get-Content -LiteralPath (Join-Path $root 'kernel/core/nic.cpp') -Raw
$shellSource = Get-Content -LiteralPath (Join-Path $root 'kernel/core/shell.cpp') -Raw
$testSource = Get-Content -LiteralPath (Join-Path $root 'tests/network_tx_phase17_raw_tx_test.cpp') -Raw

Require $headerSource 'PCI_DEVICE_I219_LM\s*=\s*0x156F' `
    'exact I219 SPT PCI identity is missing'
Require $headerSource 'E1000_CTRL_EXT_DRV_LOAD\s*=\s*0x10000000' `
    'CTRL_EXT.DRV_LOAD definition is missing'
Require $headerSource 'i219_spt_ctrl_ext_request' `
    'SPT ownership request helper is missing'
Require $headerSource 'i219_spt_ctrl_ext_unrelated_bits_preserved' `
    'CTRL_EXT unrelated-bit preservation helper is missing'
Require $headerSource 'I219_DRV_LOAD_READBACK_FAILED' `
    'ownership readback failure taxonomy is missing'
Require $headerSource 'i219_spt_ownership_path_selected' `
    'ownership lifecycle selector is missing'
Require $shellHeaderSource 'NICINFO_MODE_TX_OWNER' `
    'ownership command mode is missing'

Require $nicSource 'capture_i219_hw_control_initial' `
    'initial ownership snapshot is missing'
Require $nicSource 'capture_i219_hw_control_after_reset' `
    'post-reset ownership snapshot is missing'
Require $nicSource 'acquire_i219_hw_control' `
    'CTRL_EXT ownership write helper is missing'
Require $nicSource 'mmio_write32\(mmioBase, E1000_CTRL_EXT, requested\)' `
    'ownership write does not target CTRL_EXT'
Require $nicSource 'i219_spt_ctrl_ext_request\(before\)' `
    'ownership write does not preserve and add only DRV_LOAD'
Require $nicSource 'ownershipReadback = i219_spt_drv_load_readback_valid' `
    'DRV_LOAD readback validation is missing'
Require $nicSource 'record_i219_hw_control_tx_snapshot' `
    'bounded TX ownership snapshots are missing'
Require $nicSource 'verify_i219_hw_control_final' `
    'final ownership verification is missing'
Require $nicSource 'HwControlFailureReason::DrvLoadReadbackFailed' `
    'fail-closed ownership failure path is missing'
Require $nicSource 's_device\.hwControl\.failure' `
    'ownership failure is not retained separately from TX failure'
Require $nicSource 's_txPoisoned = true' `
    'Phase 17 timeout poisoning disappeared'
Require $nicSource 'TXDCTL1' `
    'Phase 17 SPT TX control path disappeared'
if ($nicSource -match 'E1000_SWSM|SWSM_DRV_LOAD') {
    throw 'Phase 18 must not write or seize the SWSM ownership mechanism'
}
if ($nicSource -match 'E1000_TXD_CMD_DEXT') {
    throw 'Phase 18 must not switch the raw fixture to advanced descriptors'
}

$initStart = $nicSource.IndexOf('static bool init_e1000(')
$initEnd = $nicSource.IndexOf('// ================================================================', $initStart + 1)
if ($initStart -lt 0 -or $initEnd -le $initStart) { throw 'init_e1000 source region was not found' }
$initSource = $nicSource.Substring($initStart, $initEnd - $initStart)
$initialCall = $initSource.IndexOf('capture_i219_hw_control_initial')
$resetCall = $initSource.IndexOf('i219_pch_reset(mmioBase)')
$afterResetCall = $initSource.IndexOf('capture_i219_hw_control_after_reset')
$acquireCall = $initSource.IndexOf('acquire_i219_hw_control')
$txCall = $initSource.IndexOf('init_tx(mmioBase)')
$finalCall = $initSource.IndexOf('verify_i219_hw_control_final')
if ($initialCall -lt 0 -or $resetCall -lt 0 -or $afterResetCall -lt 0 -or
    $acquireCall -lt 0 -or $txCall -lt 0 -or $finalCall -lt 0 -or
    -not ($initialCall -lt $resetCall -and $resetCall -lt $afterResetCall -and
          $afterResetCall -lt $acquireCall -and $acquireCall -lt $txCall -and
          $txCall -lt $finalCall)) {
    throw 'SPT ownership lifecycle ordering is not initial -> reset -> post-reset acquire -> TX -> final'
}

Require $shellSource 'cmd_nicinfo_tx_owner' 'nicinfo tx owner implementation is missing'
Require $shellSource 'tx owner: CTRL_EXT\.DRV_LOAD ownership evidence' `
    'ownership command help text is missing'
Require $shellSource 'NICINFO_MODE_TX_OWNER' 'ownership command dispatch is missing'

$ownerStart = $shellSource.IndexOf('static void cmd_nicinfo_tx_owner()')
$ownerEnd = $shellSource.IndexOf('static void cmd_nicinfo_tx_brief()', $ownerStart)
if ($ownerStart -lt 0 -or $ownerEnd -le $ownerStart) { throw 'ownership diagnostic region was not found' }
$ownerSource = $shellSource.Substring($ownerStart, $ownerEnd - $ownerStart)
$ownerLineCount = ([regex]::Matches($ownerSource, '\\n')).Count
if ($ownerLineCount -gt 20) { throw "nicinfo tx owner source emits $ownerLineCount newline escapes; maximum is 20" }

# Phase 17 fixture invariants remain explicit in the prior test and source.
Require $testSource 'TX_RAW_FRAME_LENGTH == 60u' 'Phase 17 raw frame length changed'
Require $testSource 'descriptor\.cmd == 0x0Bu' 'Phase 17 raw descriptor command changed'
Require $testSource 'TX_DMA_RING_LENGTH_BYTES == 1024u' 'Phase 17 ring length changed'
Require $nicSource 's_txDescs\[s_txCur\]\.cmd\s*=\s*E1000_TXD_CMD_EOP' `
    'Phase 17 legacy descriptor publication disappeared'
Require $nicSource 's_txPoisoned = true' 'Phase 17 safe poisoning disappeared'

Write-Host ("Phase 18 HW-control tests PASS " +
    "(owner newline escapes=$ownerLineCount).") -ForegroundColor Green
