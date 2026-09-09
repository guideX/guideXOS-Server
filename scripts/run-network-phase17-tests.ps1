param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$compiler = Get-Command g++.exe -ErrorAction Stop

# Preserve the complete Phase 11-16 regression gate.
& powershell.exe -NoProfile -ExecutionPolicy Bypass `
    -File (Join-Path $PSScriptRoot 'run-network-phase16-tests.ps1')
if ($LASTEXITCODE -ne 0) { throw 'Phase 11-16 network regression tests failed' }

$testDir = Join-Path ([System.IO.Path]::GetTempPath()) 'guidex-phase17-network-tests'
New-Item -ItemType Directory -Path $testDir -Force | Out-Null
$rawExe = Join-Path $testDir 'network_tx_phase17_raw_tx_test.exe'
& $compiler.Source -std=c++14 -O2 -Wall -Wextra `
    -I (Join-Path $root 'kernel/core/include') `
    -I (Join-Path $root 'kernel/arch/amd64/include') `
    (Join-Path $root 'tests/network_tx_phase17_raw_tx_test.cpp') -o $rawExe
if ($LASTEXITCODE -ne 0) { throw 'network_tx_phase17_raw_tx_test compile failed' }
& $rawExe
if ($LASTEXITCODE -ne 0) { throw 'network_tx_phase17_raw_tx_test failed' }

function Require([string]$text, [string]$pattern, [string]$message) {
    if ($text -notmatch $pattern) { throw $message }
}

$headerPath = Join-Path $root 'kernel/core/include/kernel/nic.h'
$headerSource = Get-Content -LiteralPath $headerPath -Raw
$nicSource = Get-Content -LiteralPath (Join-Path $root 'kernel/core/nic.cpp') -Raw
$shellSource = Get-Content -LiteralPath (Join-Path $root 'kernel/core/shell.cpp') -Raw
$dhcpSource = Get-Content -LiteralPath (Join-Path $root 'kernel/core/dhcp.cpp') -Raw

Require $headerSource 'TX_RAW_ETHERTYPE\s*=\s*0x88B5' `
    'raw diagnostic EtherType is missing'
Require $headerSource 'GXOS-I219-P17' 'raw diagnostic marker is missing'
Require $headerSource 'build_raw_tx_frame' 'raw frame builder is missing'
Require $headerSource 'sizeof\(TxDescriptor\) == 16u' `
    'legacy descriptor ABI assertion is missing'
Require $headerSource 'tx_descriptor_raw_word1' `
    'raw descriptor encoding helper is missing'
Require $headerSource 'tx_dma_experiment_active' `
    'fail-closed DMA experiment predicate is missing'
Require $nicSource 'Status send_raw_diagnostic_frame\(TxRawPath path\)' `
    'raw TX entry point is missing'
Require $nicSource 'TxRawPath::Normal' 'normal raw path is missing'
Require $nicSource 'TxRawPath::Direct' 'direct raw path is missing'
Require $nicSource 'TxFailureReason::DmaExperimentNotActive' `
    'inactive DMA experiment failure is missing'
Require $nicSource 'TxFailureReason::RawFrameInvalid' `
    'raw frame failure classification is missing'
Require $nicSource 's_txPoisoned' 'poisoned-ring guard disappeared'
Require $nicSource 'dma_publish_barrier' 'publication barrier disappeared'
Require $nicSource 'dma_completion_barrier' 'completion barrier disappeared'
Require $nicSource 's_txDescs\[s_txCur\]\.cso\s*=\s*0u' `
    'checksum offset is not explicitly cleared'
Require $nicSource 's_txDescs\[s_txCur\]\.css\s*=\s*0u' `
    'checksum start is not explicitly cleared'
Require $nicSource 's_txDescs\[s_txCur\]\.special\s*=\s*0u' `
    'VLAN/special field is not explicitly cleared'
Require $shellSource 'nicinfo_mode_from_args' 'raw command parser is missing'
Require $shellSource 'cmd_nicinfo_tx_raw_status' 'raw status command is missing'
Require $shellSource 'tx raw direct' 'raw direct help text is missing'
Require $shellSource 'tx raw status' 'raw status help text is missing'
Require $dhcpSource 'nic::send_frame' 'normal DHCP TX path was removed'

# Both compact diagnostics must remain bounded at the source level. This
# catches accidental Console-scrollback growth without pretending to emulate
# the target shell in a hosted test.
function Count-LogicalLines([string]$source, [string]$startToken, [string]$endToken) {
    $start = $source.IndexOf($startToken)
    $end = $source.IndexOf($endToken, $start)
    if ($start -lt 0 -or $end -le $start) {
        throw "bounded command region was not found: $startToken"
    }
    $region = $source.Substring($start, $end - $start)
    return ([regex]::Matches($region, 'output_string\("[^"\r\n]*\\n"\)')).Count
}

$txBriefLines = Count-LogicalLines $shellSource `
    'static void cmd_nicinfo_tx_brief()' 'static void cmd_nicinfo_brief()'
$rawStatusLines = Count-LogicalLines $shellSource `
    'static void cmd_nicinfo_tx_raw_status()' 'static void cmd_nicinfo_tx_brief()'
if ($txBriefLines -gt 20) {
    throw "nicinfo tx brief source emits $txBriefLines newline sites; maximum is 20"
}
if ($rawStatusLines -gt 20) {
    throw "nicinfo tx raw status source emits $rawStatusLines newline sites; maximum is 20"
}

# The raw command must not be coupled to any protocol sender.
Require $nicSource 'build_raw_tx_frame' 'raw command does not build its own fixture'
if ($shellSource -match 'cmd_nicinfo_tx_raw[\s\S]{0,800}(dhcp|udp|arp|route)') {
    throw 'raw command is coupled to a higher network protocol'
}

Write-Host ("Phase 17 raw-TX tests PASS " +
    "(tx brief newline sites=$txBriefLines, raw status newline sites=$rawStatusLines).") `
    -ForegroundColor Green
