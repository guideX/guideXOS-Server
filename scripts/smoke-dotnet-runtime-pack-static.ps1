param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path,
    [string]$RuntimePackRoot = "",
    [ValidateSet("NonAllocating", "Allocating")]
    [string]$AllocationMode = "NonAllocating",
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repoRoot = [System.IO.Path]::GetFullPath($RepoRoot)
if ([string]::IsNullOrWhiteSpace($RuntimePackRoot)) {
    $RuntimePackRoot = Join-Path $repoRoot "tools\dotnet\runtime-pack"
}
$RuntimePackRoot = [System.IO.Path]::GetFullPath($RuntimePackRoot)
$buildPack = Join-Path $repoRoot "scripts\dotnet\build-guidexos-nativeaot-runtime-pack.ps1"
$buildProof = Join-Path $repoRoot "scripts\dotnet\build-managed-hostlog-proof.ps1"
$manifestPath = Join-Path $repoRoot "out\dotnet\runtime-pack\runtime-pack.manifest.json"
$artifactRoot = Join-Path $repoRoot "out\dotnet\managed-hostlog\artifacts"
$peDump = Join-Path $artifactRoot "HostLogProof.pe.objdump.txt"
$linkRsp = Join-Path $artifactRoot "HostLogProof.link.rsp"

if (-not $SkipBuild) {
    & powershell -ExecutionPolicy Bypass -File $buildProof -RepoRoot $repoRoot -RuntimePackRoot $RuntimePackRoot -UseGuideXosRuntimePack -AllocationMode $AllocationMode -Clean
    if ($LASTEXITCODE -ne 0) { throw "Custom managed proof build failed with exit code $LASTEXITCODE" }
} else {
    if (-not (Test-Path -LiteralPath $manifestPath)) { throw "Runtime-pack manifest is missing: $manifestPath" }
}

if (-not (Test-Path -LiteralPath $manifestPath)) { throw "Runtime-pack manifest is missing: $manifestPath" }
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$expectedIdentity = if ($AllocationMode -eq "Allocating") { "guidexos-nativeaot-runtime-pack-amd64-hostlog-allocating-nocollection-v1" } else { "guidexos-nativeaot-runtime-pack-amd64-hostlog-nonallocating-v1" }
if ($manifest.identity -ne $expectedIdentity) { throw "Runtime-pack identity mismatch." }
if ([bool]$manifest.managedAllocation -ne ($AllocationMode -eq "Allocating")) { throw "Runtime-pack allocation-mode metadata mismatch." }
$objectHash = (Get-FileHash -LiteralPath ([string]$manifest.object) -Algorithm SHA256).Hash.ToUpperInvariant()
if ($objectHash -ne ([string]$manifest.objectSha256).ToUpperInvariant()) { throw "Runtime-pack object hash mismatch." }
$adaptedHash = (Get-FileHash -LiteralPath ([string]$manifest.adaptedRuntimeLibrary) -Algorithm SHA256).Hash.ToUpperInvariant()
if ($adaptedHash -ne ([string]$manifest.adaptedRuntimeLibrarySha256).ToUpperInvariant()) { throw "Adapted runtime library hash mismatch." }

$peText = Get-Content -LiteralPath $peDump -Raw
foreach ($name in @("FlsGetValue", "FlsSetValue")) {
    if ($peText -match "\b$name\b") { throw "Custom PE still contains a live $name import." }
}
$linkText = Get-Content -LiteralPath $linkRsp -Raw
if ($linkText -notmatch [regex]::Escape([string]$manifest.adaptedRuntimeLibrary)) { throw "Link response did not consume the adapted runtime library." }
if ($linkText -notmatch "guidexos_nativeaot_platform\.obj") { throw "Link response did not consume the guideXOS platform object." }
if ($AllocationMode -eq "Allocating") {
    $mapText = Get-Content -LiteralPath (Join-Path $artifactRoot "HostLogProof.map") -Raw
    if ($mapText -notmatch "RhpNewArray" -or $mapText -notmatch "guideXosStockRhpNewArray" -or $mapText -notmatch "g_guideXosManagedHeap" -or $mapText -notmatch "g_guideXosAllocationDiagnostics") { throw "Allocation helper/runtime-pack evidence is incomplete." }
}

Write-Host "Runtime-pack static smoke PASS" -ForegroundColor Green
Write-Host "Runtime-pack identity: $($manifest.identity)" -ForegroundColor Cyan
Write-Host "Platform object SHA256: $objectHash" -ForegroundColor Cyan
Write-Host "Adapted runtime library SHA256: $adaptedHash" -ForegroundColor Cyan
Write-Host "Windows live FLS imports: none" -ForegroundColor Cyan
Write-Host "Allocation mode: $AllocationMode" -ForegroundColor Cyan
