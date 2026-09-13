param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$compiler = Get-Command g++.exe -ErrorAction Stop

# Preserve the complete Phase 11-18 regression gate.
& powershell.exe -NoProfile -ExecutionPolicy Bypass `
    -File (Join-Path $PSScriptRoot 'run-network-phase18-tests.ps1')
if ($LASTEXITCODE -ne 0) { throw 'Phase 11-18 network regression tests failed' }

$testDir = Join-Path ([System.IO.Path]::GetTempPath()) 'guidex-phase19-network-tests'
New-Item -ItemType Directory -Path $testDir -Force | Out-Null
$resetExe = Join-Path $testDir 'network_tx_phase19_reset_flush_test.exe'
& $compiler.Source -std=c++14 -O2 -Wall -Wextra `
    -I (Join-Path $root 'kernel/core/include') `
    -I (Join-Path $root 'kernel/arch/amd64/include') `
    (Join-Path $root 'tests/network_tx_phase19_reset_flush_test.cpp') -o $resetExe
if ($LASTEXITCODE -ne 0) { throw 'network_tx_phase19_reset_flush_test compile failed' }
& $resetExe
if ($LASTEXITCODE -ne 0) { throw 'network_tx_phase19_reset_flush_test failed' }

function Require([string]$text, [string]$pattern, [string]$message) {
    if ($text -notmatch $pattern) { throw $message }
}

$headerSource = Get-Content -LiteralPath (Join-Path $root 'kernel/core/include/kernel/nic.h') -Raw
$shellHeaderSource = Get-Content -LiteralPath (Join-Path $root 'kernel/core/include/kernel/shell.h') -Raw
$nicSource = Get-Content -LiteralPath (Join-Path $root 'kernel/core/nic.cpp') -Raw
$shellSource = Get-Content -LiteralPath (Join-Path $root 'kernel/core/shell.cpp') -Raw

Require $headerSource 'PCI_CONFIG_DESC_RING_STATUS\s*=\s*0x00E4' `
    'I219 descriptor-ring status config offset is missing'
Require $headerSource 'PCI_CONFIG_FLUSH_DESC_REQUIRED\s*=\s*0x0100' `
    'I219 descriptor-ring flush-required mask is missing'
Require $headerSource 'E1000_FEXTNVM11\s*=\s*0x5BBC' `
    'FEXTNVM11 register definition is missing'
Require $headerSource 'E1000_FEXTNVM11_DISABLE_MULR_FIX\s*=\s*0x00002000' `
    'FEXTNVM11 MULR workaround bit is missing'
Require $headerSource 'i219_spt_reset_flush_applies' `
    'exact I219 reset-flush gate is missing'
Require $headerSource 'enum class I219RingOwner' `
    'reset ring ownership taxonomy is missing'
Require $headerSource 'I219_TX_FLUSH_POLL_LIMIT\s*=\s*100000u' `
    'bounded TX flush limit is missing'
Require $headerSource 'I219_RX_FLUSH_POLL_LIMIT\s*=\s*100000u' `
    'bounded RX flush limit is missing'

Require $nicSource 'read_i219_reset_snapshot' `
    'reset-boundary register capture is missing'
Require $nicSource 'capture_i219_reset_boundary' `
    'pre-reset I219 audit capture is missing'
Require $nicSource 'refresh_i219_reset_boundary' `
    'post-reset I219 audit capture is missing'
Require $nicSource 'prepare_i219_reset' `
    'I219 reset preparation helper is missing'
Require $nicSource 'flush_i219_tx_ring' `
    'legacy I219 TX flush helper is missing'
Require $nicSource 'flush_i219_rx_ring' `
    'legacy I219 RX flush helper is missing'
Require $nicSource 'PCI_CONFIG_DESC_RING_STATUS' `
    'reset path does not read PCI config ring status'
Require $nicSource 'i219_spt_flush_needed' `
    'I219 flush-required decision is missing'
Require $nicSource 'i219_reset_status_transitioned' `
    'post-flush PCI status transition check is missing'
Require $nicSource 'E1000_FEXTNVM11_DISABLE_MULR_FIX' `
    'FEXTNVM11 correction is not wired into the reset path'
Require $nicSource 'descriptor\.length\s*=\s*512u' `
    'upstream 512-byte TX flush fixture is missing'
Require $nicSource 'descriptor\.cmd\s*=\s*E1000_TXD_CMD_IFCS' `
    'TX flush fixture is not legacy IFCS-only'
Require $nicSource 's_device\.resetDiagnostics' `
    'reset diagnostics are not retained in NIC state'
Require $nicSource 'strongerRecoveryRequired' `
    'unsafe reset recovery state is not retained'
Require $nicSource 'E1000_RXDCTL_THRESH_UNIT_DESC' `
    'upstream RX flush descriptor-granularity programming is missing'

if ($nicSource -match 'E1000_TXD_CMD_DEXT') {
    throw 'Phase 19 must not switch any TX fixture to advanced descriptors'
}
$resetStart = $nicSource.IndexOf('static bool i219_pch_reset(')
$resetEnd = $nicSource.IndexOf('static bool run_i219_phase6_micro_stage(', $resetStart)
if ($resetStart -lt 0 -or $resetEnd -le $resetStart) {
    throw 'production I219 reset source region was not found'
}
$resetSource = $nicSource.Substring($resetStart, $resetEnd - $resetStart)
$prepareCall = $resetSource.IndexOf('prepare_i219_reset(mmioBase)')
$ctrlWrite = $resetSource.IndexOf('mmio_write32(mmioBase, E1000_CTRL, s_device.resetCtrlRequest)')
$postCapture = $resetSource.IndexOf('refresh_i219_reset_boundary(mmioBase)')
if ($prepareCall -lt 0 -or $ctrlWrite -lt 0 -or $postCapture -lt 0 -or
    -not ($prepareCall -lt $ctrlWrite -and $ctrlWrite -lt $postCapture)) {
    throw 'production reset ordering is not audit/prepare -> CTRL.RST -> post-capture'
}

$initStart = $nicSource.IndexOf('static bool init_e1000(')
$initEnd = $nicSource.IndexOf('// ================================================================', $initStart + 1)
if ($initStart -lt 0 -or $initEnd -le $initStart) { throw 'init_e1000 source region was not found' }
$initSource = $nicSource.Substring($initStart, $initEnd - $initStart)
$rxCall = $initSource.IndexOf('init_rx(mmioBase)')
$txCall = $initSource.IndexOf('init_tx(mmioBase)')
if ($rxCall -lt 0 -or $txCall -lt 0 -or
    -not ($rxCall -lt $txCall)) {
    throw 'I219 lifecycle must initialize RX before TX'
}
$bootStart = $nicSource.IndexOf('bool init_from_bootinfo(')
$readyCall = $nicSource.IndexOf('s_device.driverReady = true', $bootStart)
$initCall = $nicSource.IndexOf('init_e1000(s_device.mmioBase)', $bootStart)
if ($bootStart -lt 0 -or $readyCall -lt 0 -or $initCall -lt 0 -or
    -not ($initCall -lt $readyCall)) {
    throw 'I219 lifecycle must publish ready only after init_e1000 succeeds'
}

Require $shellHeaderSource 'NICINFO_MODE_TX_RESET' `
    'nicinfo tx reset mode is missing'
Require $shellSource 'cmd_nicinfo_tx_reset' `
    'nicinfo tx reset command implementation is missing'
Require $shellSource 'tx reset: I219/SPT pre-reset flush audit' `
    'nicinfo tx reset help text is missing'
Require $shellSource 'NICINFO_MODE_TX_RESET' `
    'nicinfo tx reset dispatch is missing'

$resetCommandStart = $shellSource.IndexOf('static void cmd_nicinfo_tx_reset()')
$resetCommandEnd = $shellSource.IndexOf('static void cmd_nicinfo_tx_brief()', $resetCommandStart)
if ($resetCommandStart -lt 0 -or $resetCommandEnd -le $resetCommandStart) {
    throw 'nicinfo tx reset command region was not found'
}
$resetCommandSource = $shellSource.Substring($resetCommandStart,
                                               $resetCommandEnd - $resetCommandStart)
$resetLineCount = ([regex]::Matches(
    $resetCommandSource, 'output_string\("[^"\r\n]*\\n"\)')).Count
if ($resetLineCount -gt 20) {
    throw "nicinfo tx reset source emits $resetLineCount newline sites; maximum is 20"
}

# Phase 17 raw-TX and Phase 18 ownership contracts remain source-visible in
# the inherited regression scripts; retain the direct coupling guard here too.
if ($shellSource -match 'cmd_nicinfo_tx_raw[\s\S]{0,800}(dhcp|udp|arp|route)') {
    throw 'Phase 19 raw command is coupled to a higher network protocol'
}

Write-Host ("Phase 19 reset-flush tests PASS " +
    "(reset command newline sites=$resetLineCount).") -ForegroundColor Green
