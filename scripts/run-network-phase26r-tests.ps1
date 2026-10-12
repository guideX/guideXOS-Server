param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path

# This includes the hosted Phase 11-26 chain, including the Phase 16 pure
# provisioning cases, registration-gate checks, and diagnostics-off build.
& powershell.exe -NoProfile -ExecutionPolicy Bypass `
    -File (Join-Path $PSScriptRoot 'run-network-phase26-tests.ps1')
if ($LASTEXITCODE -ne 0) { throw 'Phase 11-26 network regression tests failed' }

function Require([string]$text, [string]$pattern, [string]$message) {
    if ($text -notmatch $pattern) { throw $message }
}

$phase26Builder = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'create-phase26-aida-i219-laninit-diag-iso.ps1') -Raw
$phase26rBuilder = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'create-phase26r-aida-i219-tx-dma-provision-iso.ps1') -Raw
$kernel = Get-Content -LiteralPath (Join-Path $root 'kernel/core/nic.cpp') -Raw
$boot = Get-Content -LiteralPath (Join-Path $root 'guideXOSBootLoader/main.cpp') -Raw

if ($phase26Builder -match 'I219TxDmaPlacementExperiment') {
    throw 'The historical Phase 26 baseline no longer records its disabled placement setting'
}
Require $phase26rBuilder "'-I219TxDmaPlacementExperiment'" `
    'Phase 26R canonical build does not explicitly enable the gated DMA experiment'
Require $phase26rBuilder "'-I219LanInitDiagnostics'" `
    'Phase 26R canonical build does not enable the read-only LAN_INIT capture'
if (([regex]::Matches($phase26rBuilder, "'-I219TxDmaPlacementExperiment'")).Count -ne 2) {
    throw 'Phase 26R build and ISO manifest configuration do not both enable DMA placement'
}
if (([regex]::Matches($phase26rBuilder, "'-I219LanInitDiagnostics'")).Count -ne 2) {
    throw 'Phase 26R build and ISO manifest configuration do not both enable LAN_INIT diagnostics'
}
Require $kernel '#if GXOS_I219_TX_DMA_PLACEMENT_EXPERIMENT[\s\S]*?if \(i219\)' `
    'The placement behavior is no longer explicitly compile-time gated'
Require $boot 'AllocatePages\([\s\S]*?AllocateMaxAddress[\s\S]*?EfiLoaderData' `
    'The diagnostic allocation does not remain firmware-selected and loader-owned'
Require $boot 'txDmaMaxAddress \+ txDmaSize <= 0x100000000ULL' `
    'The low-address allocation bound was removed'

Write-Host 'Phase 26R provisioning qualification/source-gate tests PASS.' -ForegroundColor Green
