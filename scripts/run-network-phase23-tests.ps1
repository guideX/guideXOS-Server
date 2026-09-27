param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$compiler = Get-Command g++.exe -ErrorAction Stop

& powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'run-network-phase22-tests.ps1')
if ($LASTEXITCODE -ne 0) { throw 'Phase 11-22 network regression tests failed' }

$testDir = Join-Path ([System.IO.Path]::GetTempPath()) 'guidex-phase23-network-tests'
New-Item -ItemType Directory -Path $testDir -Force | Out-Null
$testExe = Join-Path $testDir 'network_tx_phase23_dma_audit_test.exe'
& $compiler.Source -std=c++14 -O2 -Wall -Wextra `
    -I (Join-Path $root 'kernel/core/include') `
    -I (Join-Path $root 'kernel/arch/amd64/include') `
    (Join-Path $root 'tests/network_tx_phase23_dma_audit_test.cpp') -o $testExe
if ($LASTEXITCODE -ne 0) { throw 'network_tx_phase23_dma_audit_test compile failed' }
& $testExe
if ($LASTEXITCODE -ne 0) { throw 'network_tx_phase23_dma_audit_test failed' }

function Require([string]$text, [string]$pattern, [string]$message) {
    if ($text -notmatch $pattern) { throw $message }
}

$nic = Get-Content -LiteralPath (Join-Path $root 'kernel/core/nic.cpp') -Raw
$header = Get-Content -LiteralPath (Join-Path $root 'kernel/core/include/kernel/nic.h') -Raw
$shell = Get-Content -LiteralPath (Join-Path $root 'kernel/core/shell.cpp') -Raw
$bootInfo = Get-Content -LiteralPath (Join-Path $root 'guideXOSBootLoader/main.cpp') -Raw
$paging = Get-Content -LiteralPath (Join-Path $root 'guideXOSBootLoader/paging.cpp') -Raw

Require $header 'tx_dma_region_memory_map_attributes' 'EFI memory attributes helper is missing'
Require $nic 'packetFirst32BeforeTdt' 'pre-TDT packet capture is missing'
Require $nic 'packetFirst32AfterTdt' 'post-TDT packet capture is missing'
Require $nic 'dma_publish_barrier\(\);\s*mmio_write32\(s_device\.mmioBase, E1000_TDT' `
    'DMA publication fence is not immediately before TDT'
Require $shell 'descriptor-pre-TDT=' 'pre-TDT raw descriptor output is missing'
Require $shell 'descriptor-post-TDT=' 'post-TDT raw descriptor output is missing'
Require $shell 'pre-TDT' 'pre-TDT register snapshot output is missing'
Require $shell 'post-TDT' 'post-TDT register snapshot output is missing'
Require $shell 'active-device-context=unknown' 'uninspected VT-d context is not clearly qualified'
Require $bootInfo 'MemoryMapContainsLoaderData' 'final-map ownership check is missing'
Require $paging 'PTE_PCD \| PTE_PWT' 'uncached MMIO mapping policy is missing'

Write-Host 'Phase 23 I219 DMA/fetch audit tests PASS.' -ForegroundColor Green
