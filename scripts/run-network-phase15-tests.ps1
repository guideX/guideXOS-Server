param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$compiler = Get-Command g++.exe -ErrorAction Stop

# Preserve the complete Phase 11-14 regression gate.
& powershell.exe -NoProfile -ExecutionPolicy Bypass `
    -File (Join-Path $PSScriptRoot 'run-network-phase14-tests.ps1')
if ($LASTEXITCODE -ne 0) { throw 'Phase 11-14 network regression tests failed' }

$testDir = Join-Path ([System.IO.Path]::GetTempPath()) 'guidex-phase15-network-tests'
New-Item -ItemType Directory -Path $testDir -Force | Out-Null
$engineExe = Join-Path $testDir 'network_tx_phase15_i219_engine_test.exe'
& $compiler.Source -std=c++14 -O2 -Wall -Wextra `
    -I (Join-Path $root 'kernel/core/include') `
    -I (Join-Path $root 'kernel/arch/amd64/include') `
    (Join-Path $root 'tests/network_tx_phase15_i219_engine_test.cpp') -o $engineExe
if ($LASTEXITCODE -ne 0) { throw 'network_tx_phase15_i219_engine_test compile failed' }
& $engineExe
if ($LASTEXITCODE -ne 0) { throw 'network_tx_phase15_i219_engine_test failed' }

# Structural host check for the physical one-screen command. Count the
# source-level newline emission sites in the normal and no-device branches;
# the documented contract allows at most 20 logical lines.
$shellSource = Get-Content -LiteralPath (Join-Path $root 'kernel/core/shell.cpp') -Raw
$briefStart = $shellSource.IndexOf('static void cmd_nicinfo_tx_brief()')
$briefEnd = $shellSource.IndexOf('static void cmd_nicinfo_brief()', $briefStart)
if ($briefStart -lt 0 -or $briefEnd -le $briefStart) {
    throw 'nicinfo tx brief implementation was not found'
}
$briefSource = $shellSource.Substring($briefStart, $briefEnd - $briefStart)
$lineCount = ([regex]::Matches($briefSource, 'output_string\("[^"\r\n]*\\n"\)')).Count
if ($lineCount -gt 20) {
    throw "nicinfo tx brief source emits $lineCount newline sites; maximum is 20"
}

$headerSource = Get-Content -LiteralPath (Join-Path $root 'kernel/core/include/kernel/nic.h') -Raw
if ($headerSource -notmatch 'i219_spt_txdctl_configuration_valid') {
    throw 'I219 TXDCTL semantic validation helper is missing'
}
$nicSource = Get-Content -LiteralPath (Join-Path $root 'kernel/core/nic.cpp') -Raw
if ($nicSource -notmatch 'if \(!is_i219_device\(s_device\.deviceId\)\) return true;') {
    throw 'I219-only guard for TXDCTL correction is missing'
}
$txdctlIndex = $nicSource.IndexOf('if (!configure_i219_spt_tx_descriptor_control(mmioBase))')
$tipgIndex = $nicSource.IndexOf('mmio_write32(mmioBase, E1000_TIPG', $txdctlIndex)
$tctlIndex = $nicSource.IndexOf('mmio_write32(mmioBase, E1000_TCTL', $tipgIndex)
if ($txdctlIndex -lt 0 -or $tipgIndex -le $txdctlIndex -or $tctlIndex -le $tipgIndex) {
    throw 'I219 TXDCTL correction is not ordered before TIPG and TCTL enablement'
}

Write-Host "Phase 15 I219 TX-engine tests PASS (brief newline sites=$lineCount)." -ForegroundColor Green
