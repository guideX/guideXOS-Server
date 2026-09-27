param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$compiler = Get-Command g++.exe -ErrorAction Stop

& powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'run-network-phase21-tests.ps1')
if ($LASTEXITCODE -ne 0) { throw 'Phase 11-21 network regression tests failed' }

$testDir = Join-Path ([System.IO.Path]::GetTempPath()) 'guidex-phase22-network-tests'
New-Item -ItemType Directory -Path $testDir -Force | Out-Null
$testExe = Join-Path $testDir 'network_tx_phase22_pch_init_test.exe'
& $compiler.Source -std=c++14 -O2 -Wall -Wextra `
    -I (Join-Path $root 'kernel/core/include') `
    -I (Join-Path $root 'kernel/arch/amd64/include') `
    (Join-Path $root 'tests/network_tx_phase22_pch_init_test.cpp') -o $testExe
if ($LASTEXITCODE -ne 0) { throw 'network_tx_phase22_pch_init_test compile failed' }
& $testExe
if ($LASTEXITCODE -ne 0) { throw 'network_tx_phase22_pch_init_test failed' }

function Require([string]$text, [string]$pattern, [string]$message) {
    if ($text -notmatch $pattern) { throw $message }
}

$header = Get-Content -LiteralPath (Join-Path $root 'kernel/core/include/kernel/nic.h') -Raw
$nic = Get-Content -LiteralPath (Join-Path $root 'kernel/core/nic.cpp') -Raw
$shell = Get-Content -LiteralPath (Join-Path $root 'kernel/core/shell.cpp') -Raw

Require $header 'E1000_TARC0_I219_INIT_BITS' 'I219 TARC0 initialization mask is missing'
Require $header 'E1000_TARC1_I219_INIT_BITS' 'I219 TARC1 initialization mask is missing'
Require $header 'E1000_RFCTL_NFSW_DIS' 'RFCTL NFS write-filter bit is missing'
Require $header 'E1000_RFCTL_NFSR_DIS' 'RFCTL NFS read-filter bit is missing'
Require $header 'struct I219TxTimeoutRegisters' 'I219 timeout snapshot is missing'

$txStart = $nic.IndexOf('static bool init_tx(')
$txEnd = $nic.IndexOf('void set_kernel_physical_base', $txStart)
if ($txStart -lt 0 -or $txEnd -le $txStart) { throw 'init_tx source region was not found' }
$txSource = $nic.Substring($txStart, $txEnd - $txStart)
$initCall = $txSource.IndexOf('apply_i219_pch_tx_initialization(mmioBase)')
$firstTctlWrite = $txSource.IndexOf('mmio_write32(mmioBase, E1000_TCTL, tctl)')
$lastTctlWrite = $txSource.LastIndexOf('mmio_write32(mmioBase, E1000_TCTL, tctl)')
if ($firstTctlWrite -lt 0 -or $initCall -lt 0 -or $lastTctlWrite -le $initCall) {
    throw 'I219 register initialization must run with TX disabled before TCTL enable'
}

$captureStart = $nic.IndexOf('static void capture_i219_tx_timeout_registers(')
$captureEnd = $nic.IndexOf('static bool configure_i219_spt_tx_descriptor_control', $captureStart)
if ($captureStart -lt 0 -or $captureEnd -le $captureStart) { throw 'I219 timeout capture region was not found' }
$capture = $nic.Substring($captureStart, $captureEnd - $captureStart)
foreach ($register in @('E1000_ICR', 'E1000_IMS', 'E1000_STATUS', 'E1000_CTRL', 'E1000_TARC0', 'E1000_TARC1', 'E1000_RFCTL')) {
    Require $capture "registers\.$($register.ToLowerInvariant().Replace('e1000_', ''))\s*=\s*mmio_read32\(mmioBase, $register\)" "timeout capture is missing $register"
}
if ([regex]::Matches($capture, 'mmio_read32\(mmioBase, E1000_ICR\)').Count -ne 1) {
    throw 'ICR must be read exactly once in the timeout capture'
}

foreach ($register in @('ICR', 'IMS', 'STATUS', 'CTRL', 'TARC0', 'TARC1', 'RFCTL')) {
    Require $shell "timeoutRegs\.$($register.ToLowerInvariant())" "shell timeout report is missing $register"
}
Require $shell 'timeout-MMIO=' 'raw status command does not label the timeout register snapshot'

Write-Host 'Phase 22 I219/PCH TX initialization tests PASS.' -ForegroundColor Green
