param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path,
    [string]$OutputRoot = "",
    [string]$StageRoot = "",
    [string]$RuntimePackRoot = "",
    [string]$RuntimePackOutputRoot = "",
    [ValidateSet("Primary64KiB", "Small4KiB")]
    [string]$HeapConfiguration = "Primary64KiB",
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$Root = [System.IO.Path]::GetFullPath($RepoRoot)
. (Join-Path $Root "scripts\dotnet\managed-hostlog-artifact-assertions.ps1")

function Assert-Contains([string]$Path, [string[]]$Patterns, [string]$Label) {
    $text = Get-Content -LiteralPath $Path -Raw
    foreach ($pattern in $Patterns) {
        if ($text -notmatch $pattern) { throw "$Label missing pattern: $pattern" }
    }
}

function Assert-NotContains([string]$Path, [string[]]$Patterns, [string]$Label) {
    $text = Get-Content -LiteralPath $Path -Raw
    foreach ($pattern in $Patterns) {
        if ($text -match $pattern) { throw "$Label unexpectedly contains: $pattern" }
    }
}

if ([string]::IsNullOrWhiteSpace($OutputRoot)) { $OutputRoot = Join-Path $Root "out\dotnet\repeated-allocation-comparison\repeated-allocation-static" }
if ([string]::IsNullOrWhiteSpace($StageRoot)) { $StageRoot = Join-Path $OutputRoot "stage" }
if ([string]::IsNullOrWhiteSpace($RuntimePackRoot)) { $RuntimePackRoot = Join-Path $Root "tools\dotnet\runtime-pack" }
if ([string]::IsNullOrWhiteSpace($RuntimePackOutputRoot)) { $RuntimePackOutputRoot = Join-Path $Root "out\dotnet\repeated-allocation-comparison\runtime-pack-repeated-static" }

$OutputRoot = [System.IO.Path]::GetFullPath($OutputRoot)
$StageRoot = [System.IO.Path]::GetFullPath($StageRoot)
$RuntimePackRoot = [System.IO.Path]::GetFullPath($RuntimePackRoot)
$RuntimePackOutputRoot = [System.IO.Path]::GetFullPath($RuntimePackOutputRoot)

$buildScript = Join-Path $Root "scripts\dotnet\build-managed-hostlog-proof.ps1"
$stageScript = Join-Path $Root "scripts\dotnet\stage-managed-hostlog-proof.ps1"
$powershell = (Get-Command powershell -ErrorAction Stop).Source

if (-not $SkipBuild) {
    & $powershell -ExecutionPolicy Bypass -File $buildScript `
        -RepoRoot $Root -OutputRoot $OutputRoot `
        -RuntimePackRoot $RuntimePackRoot -UseGuideXosRuntimePack `
        -AllocationMode Repeated -HeapConfiguration $HeapConfiguration `
        -RuntimePackOutputRoot $RuntimePackOutputRoot -Clean
    if ($LASTEXITCODE -ne 0) { throw "Repeated-allocation static build failed with exit code $LASTEXITCODE" }
}

& $powershell -ExecutionPolicy Bypass -File $stageScript `
    -RepoRoot $Root -OutputRoot $OutputRoot -StageRoot $StageRoot `
    -RuntimePackRoot $RuntimePackRoot -UseGuideXosRuntimePack `
    -AllocationMode Repeated -HeapConfiguration $HeapConfiguration `
    -RuntimePackOutputRoot $RuntimePackOutputRoot -SkipBuild
if ($LASTEXITCODE -ne 0) { throw "Repeated-allocation static staging failed with exit code $LASTEXITCODE" }

$artifactRoot = Join-Path $OutputRoot "artifacts"
$elfPath = Join-Path $artifactRoot "HostLogProof.elf"
$pePath = Join-Path $artifactRoot "HostLogProof.exe"
$mapPath = Join-Path $artifactRoot "HostLogProof.map"
$peDumpPath = Join-Path $artifactRoot "HostLogProof.pe.objdump.txt"
$elfDumpPath = Join-Path $artifactRoot "HostLogProof.elf.objdump.txt"
$elfReadelfPath = Join-Path $artifactRoot "HostLogProof.elf.readelf.txt"
$nativeDumpPath = Join-Path $artifactRoot "HostLogProof.native.objdump.txt"
$stageEnvelopePath = Join-Path $StageRoot "proof\proof-envelope.json"
$manifestPath = Join-Path $RuntimePackOutputRoot "runtime-pack.manifest.json"
$programPath = Join-Path $Root "samples\managed\HostLogProof\Program.cs"
$runtimeSourcePath = Join-Path $Root "tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_platform.cpp"

foreach ($path in @($elfPath, $pePath, $mapPath, $peDumpPath, $elfDumpPath, $elfReadelfPath, $nativeDumpPath, $stageEnvelopePath, $manifestPath)) {
    if (-not (Test-Path -LiteralPath $path)) { throw "Repeated-allocation static artifact missing: $path" }
}

$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$envelope = Get-Content -LiteralPath $stageEnvelopePath -Raw | ConvertFrom-Json
$expectedHeap = if ($HeapConfiguration -eq "Small4KiB") { 4096 } else { 65536 }
$expectedCount = [math]::Floor($expectedHeap / 280)
$expectedRemaining = $expectedHeap - ($expectedCount * 280)

if ([string]$manifest.identity -ne "guidexos-nativeaot-runtime-pack-amd64-hostlog-repeated-allocation-nocollection-v1") { throw "Repeated runtime-pack identity mismatch." }
if ([int]$manifest.managedHeapBytes -ne $expectedHeap) { throw "Runtime-pack heap size mismatch." }
if ([string]$manifest.managedHeapConfiguration -ne $HeapConfiguration) { throw "Runtime-pack heap configuration mismatch." }
if ([int]$manifest.managedArrayLength -ne 256) { throw "Runtime-pack array-length metadata mismatch." }
if ([int]$manifest.managedObjectSize -ne 280) { throw "Runtime-pack object-size metadata mismatch." }
if ([bool]$manifest.managedExceptions -or [string]$manifest.allocationStrategy -notmatch 'no-collection') { throw "Repeated runtime pack metadata does not describe the no-collection contract." }
if ([string]$envelope.allocationMode -ne "Repeated" -or -not [bool]$envelope.repeatedAllocation) { throw "Stage envelope is not repeated-allocation mode." }
if ([int]$envelope.heapBytes -ne $expectedHeap -or [int]$envelope.expectedObjectSize -ne 280) { throw "Stage envelope geometry mismatch." }

Assert-Contains $programPath @(
    'HOSTLOGPROOF_REPEATED_ALLOCATION',
    'new byte\[arrayLength\]',
    'guideXosManagedAllocationCanFit',
    'guideXosManagedAllocationValidateObject',
    'guideXosManagedAllocationReport',
    'while \(completed < maximumBoundedAllocations\)'
) "Repeated managed source"
Assert-Contains $runtimeSourcePath @(
    'RhpNewArray',
    'guideXosStockRhpNewArray',
    'RhpNewFast',
    'guideXosManagedAllocationCanFit',
    'guideXosManagedAllocationReport',
    'controlledOom'
) "Repeated runtime support"
Assert-NotContains $programPath @('GC\.Collect', 'OutOfMemoryException', 'throw\s+') "Repeated managed source"
Assert-Contains $mapPath @(
    'ManagedMain\s+[0-9A-Fa-f]{16}',
    'RhpNewArray\s+[0-9A-Fa-f]{16}',
    'guideXosStockRhpNewArray\s+[0-9A-Fa-f]{16}',
    'RhpNewFast',
    'guideXosManagedAllocationCanFit\s+[0-9A-Fa-f]{16}',
    'guideXosManagedAllocationValidateObject\s+[0-9A-Fa-f]{16}',
    'guideXosManagedAllocationRecordFailure\s+[0-9A-Fa-f]{16}',
    'guideXosManagedAllocationReport\s+[0-9A-Fa-f]{16}'
) "Repeated allocation map"
Assert-Contains $nativeDumpPath @('HostLogProof_HostLogProof_Program__ManagedMain>', 'guideXosManagedAllocationCanFit', 'guideXosManagedAllocationValidateObject', 'guideXosManagedAllocationReport') "Generated repeated call path"
Assert-NotContains $peDumpPath @('FlsGetValue', 'FlsSetValue', 'ucrtbase\.dll', 'msvcrt\.dll', 'ntdll\.dll') "Repeated PE live Windows imports"
Assert-NotContains $elfReadelfPath @('NEEDED', 'PT_INTERP', 'There are relocations in this file') "Repeated ELF dependency envelope"

$reverse = Assert-ManagedHostLogReversePInvokeChain $elfPath $mapPath $peDumpPath -GuideXosRuntimePack -ManagedAllocation -RepeatedAllocation
if ($null -eq $reverse) { throw "Reverse-P/Invoke allocation chain assertion returned no result." }

Write-Host "Managed repeated-allocation static smoke PASS" -ForegroundColor Green
Write-Host "Mode: bounded repeated managed allocation with collection disabled" -ForegroundColor Cyan
Write-Host "Heap bytes: $expectedHeap; object size: 280; alignment: 8; expected allocations: $expectedCount; remaining: $expectedRemaining" -ForegroundColor Cyan
Write-Host "Allocation path: ManagedMain -> RhpNewArray -> guideXosStockRhpNewArray -> RhpNewFast" -ForegroundColor Cyan
Write-Host "OOM helper: guideXosManagedAllocationCanFit -> proof-specific nonfatal result" -ForegroundColor Cyan
Write-Host "Generated-code bypass: none" -ForegroundColor Cyan
Write-Host "Live FLS/CRT imports: none; resolver-only imports remain statically present but unentered" -ForegroundColor Cyan
