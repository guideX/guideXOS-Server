param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$compiler = Get-Command g++.exe -ErrorAction Stop

& powershell.exe -NoProfile -ExecutionPolicy Bypass `
    -File (Join-Path $PSScriptRoot 'run-network-phase25-tests.ps1')
if ($LASTEXITCODE -ne 0) { throw 'Phase 11-25 network regression tests failed' }

$testDir = Join-Path ([System.IO.Path]::GetTempPath()) 'guidex-phase26-network-tests'
New-Item -ItemType Directory -Path $testDir -Force | Out-Null
$testSource = Join-Path $root 'tests/network_tx_phase26_lan_init_test.cpp'
$includeDir = Join-Path $root 'kernel/core/include'
$archIncludeDir = Join-Path $root 'kernel/arch/amd64/include'
$enabledExe = Join-Path $testDir 'network_tx_phase26_lan_init_enabled.exe'
$disabledExe = Join-Path $testDir 'network_tx_phase26_lan_init_disabled.exe'

& $compiler.Source -std=c++14 -O2 -Wall -Wextra `
    -DGXOS_I219_LAN_INIT_DIAGNOSTICS=1 `
    -I $includeDir -I $archIncludeDir $testSource -o $enabledExe
if ($LASTEXITCODE -ne 0) { throw 'Phase 26 enabled helper test compile failed' }
& $enabledExe
if ($LASTEXITCODE -ne 0) { throw 'Phase 26 enabled helper test failed' }

& $compiler.Source -std=c++14 -O2 -Wall -Wextra `
    -DGXOS_I219_LAN_INIT_DIAGNOSTICS=0 `
    -I $includeDir -I $archIncludeDir $testSource -o $disabledExe
if ($LASTEXITCODE -ne 0) { throw 'Phase 26 disabled helper test compile failed' }
& $disabledExe
if ($LASTEXITCODE -ne 0) { throw 'Phase 26 disabled helper test failed' }

function Require([string]$text, [string]$pattern, [string]$message) {
    if ($text -notmatch $pattern) { throw $message }
}

$nicPath = Join-Path $root 'kernel/core/nic.cpp'
$nicHeaderPath = Join-Path $root 'kernel/core/include/kernel/nic.h'
$helperPath = Join-Path $root 'kernel/core/include/kernel/i219_lan_init_diagnostics.h'
$shellPath = Join-Path $root 'kernel/core/shell.cpp'
$shellHeaderPath = Join-Path $root 'kernel/core/include/kernel/shell.h'
$nic = Get-Content -LiteralPath $nicPath -Raw
$nicHeader = Get-Content -LiteralPath $nicHeaderPath -Raw
$helper = Get-Content -LiteralPath $helperPath -Raw
$shell = Get-Content -LiteralPath $shellPath -Raw
$shellHeader = Get-Content -LiteralPath $shellHeaderPath -Raw

Require $nicHeader 'E1000_STATUS_LAN_INIT_DONE\s*=\s*0x00000200' `
    'Phase 26 LAN_INIT_DONE mask does not match Linux'
Require $nicHeader 'GXOS_I219_LAN_INIT_DIAGNOSTICS\s+0' `
    'production default does not disable Phase 26 diagnostics'
Require $helper 'I219_LAN_INIT_SAMPLE_LIMIT' `
    'bounded sample limit is not used by the capture helper'
Require $helper 'requestedCount < I219_LAN_INIT_SAMPLE_LIMIT' `
    'sample count is not clamped to the configured bound'
Require $helper 'readRegister\(E1000_STATUS\)[\s\S]*?readRegister\(E1000_CTRL\)' `
    'capture does not read STATUS and CTRL through the supplied read-only accessor'
if ($helper -match 'writeRegister|mmio_write32|pci_write|E1000_TDT|TxDescriptor|RxDescriptor') {
    throw 'Phase 26 capture helper contains a writer or descriptor operation'
}
if ($nic -match 'mmio_write32\([^;]*E1000_STATUS') {
    throw 'Phase 26 introduced a STATUS write'
}

$preStart = $nic.IndexOf('static void capture_i219_lan_init_pre_phy')
$postStart = $nic.IndexOf('static void capture_i219_lan_init_post_init', $preStart)
$resetStart = $nic.IndexOf('static bool i219_pch_reset(')
$resetEnd = $nic.IndexOf('static bool run_i219_phase6_micro_stage', $resetStart)
$initStart = $nic.IndexOf('static bool init_e1000(')
$initEnd = $nic.IndexOf('// PCI bus scan for network controllers', $initStart)
if ($preStart -lt 0 -or $postStart -le $preStart -or
    $resetStart -lt 0 -or $resetEnd -le $resetStart -or
    $initStart -lt 0 -or $initEnd -le $initStart) {
    throw 'Phase 26 diagnostic source boundaries were not found'
}
$pre = $nic.Substring($preStart, $postStart - $preStart)
$reset = $nic.Substring($resetStart, $resetEnd - $resetStart)
$init = $nic.Substring($initStart, $initEnd - $initStart)
if ($pre -match 'mmio_write32|pci_write|E1000_TDT|TxDescriptor|RxDescriptor|send_raw_diagnostic_frame') {
    throw 'pre-PHY diagnostic capture modifies device state or TX descriptors'
}
if ($reset.IndexOf('s_device.resetCompleted = true;') -ge
    $reset.IndexOf('capture_i219_lan_init_pre_phy(mmioBase);') -or
    $reset.IndexOf('capture_i219_lan_init_pre_phy(mmioBase);') -ge
    $reset.IndexOf('refresh_i219_reset_boundary(mmioBase)')) {
    throw 'pre-PHY capture is not after reset completion and before the existing reset-boundary refresh'
}
if ($init.IndexOf('capture_i219_lan_init_post_init(mmioBase);') -le
    $init.IndexOf('init_tx(mmioBase)') -or
    $init.IndexOf('capture_i219_lan_init_post_init(mmioBase);') -le
    $init.IndexOf('refresh_i219_link_state(mmioBase')) {
    throw 'post-init capture precedes existing TX-ring or link initialization'
}
Require $reset '#if GXOS_I219_LAN_INIT_DIAGNOSTICS[\s\S]*?capture_i219_lan_init_pre_phy' `
    'pre-PHY hardware reads are not compile-time gated'
Require $init '#if GXOS_I219_LAN_INIT_DIAGNOSTICS[\s\S]*?capture_i219_lan_init_post_init' `
    'post-init hardware reads are not compile-time gated'
Require $shellHeader 'NICINFO_MODE_LANINIT' `
    'nicinfo laninit parser mode is missing'
Require $shell 'static void cmd_nicinfo_laninit\(\)[\s\S]*?get_i219_lan_init_diagnostics\(\)[\s\S]*?STATUS-writes=none' `
    'console output does not expose the stored read-only diagnostics'
$commandStart = $shell.LastIndexOf('static void cmd_nicinfo_laninit()')
$commandEnd = $shell.IndexOf('static void cmd_nicinfo()', $commandStart)
$command = $shell.Substring($commandStart, $commandEnd - $commandStart)
if ($command -match 'mmio_read32|pci_read|mdic_read|send_raw_diagnostic_frame|run_i219_') {
    throw 'nicinfo laninit performs hardware access or a TX/reset operation'
}

Write-Host 'Phase 26 bounded LAN_INIT_DONE diagnostics tests PASS (simulated MMIO only).' -ForegroundColor Green
