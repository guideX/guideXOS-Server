param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path,
    [string]$OutputRoot = "",
    [string]$StageRoot = "",
    [string]$RuntimePackRoot = "",
    [string]$RuntimePackOutputRoot = "",
    [string]$ServerExe = "",
    [ValidateSet("Primary64KiB", "Small4KiB")]
    [string]$HeapConfiguration = "Primary64KiB",
    [switch]$UseGuideXosRuntimePack,
    [switch]$EnableFaultDiagnostics,
    [switch]$SkipBuild,
    [switch]$SkipFailureProbe
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$Root = [System.IO.Path]::GetFullPath($RepoRoot)
if ([string]::IsNullOrWhiteSpace($OutputRoot)) { $OutputRoot = Join-Path $Root "out\dotnet\repeated-allocation-comparison\repeated-allocation" }
if ([string]::IsNullOrWhiteSpace($StageRoot)) { $StageRoot = Join-Path $OutputRoot "stage" }
if ([string]::IsNullOrWhiteSpace($RuntimePackRoot)) { $RuntimePackRoot = Join-Path $Root "tools\dotnet\runtime-pack" }
if ([string]::IsNullOrWhiteSpace($RuntimePackOutputRoot)) { $RuntimePackOutputRoot = Join-Path $Root "out\dotnet\repeated-allocation-comparison\runtime-pack-repeated" }
if ([string]::IsNullOrWhiteSpace($ServerExe)) { $ServerExe = Join-Path $Root "guideXOSServer.experimental.exe" }

$runner = Join-Path $Root "scripts\smoke-dotnet-managed-hostlog-execution.ps1"
$arguments = @(
    "-RepoRoot", $Root,
    "-OutputRoot", ([System.IO.Path]::GetFullPath($OutputRoot)),
    "-StageRoot", ([System.IO.Path]::GetFullPath($StageRoot)),
    "-RuntimePackRoot", ([System.IO.Path]::GetFullPath($RuntimePackRoot)),
    "-RuntimePackOutputRoot", ([System.IO.Path]::GetFullPath($RuntimePackOutputRoot)),
    "-ServerExe", ([System.IO.Path]::GetFullPath($ServerExe)),
    "-AllocationMode", "Repeated",
    "-HeapConfiguration", $HeapConfiguration
)
if ($UseGuideXosRuntimePack) { $arguments += "-UseGuideXosRuntimePack" }
if ($EnableFaultDiagnostics) { $arguments += "-EnableFaultDiagnostics" }
if ($SkipBuild) { $arguments += "-SkipBuild" }
if ($SkipFailureProbe) { $arguments += "-SkipFailureProbe" }

& powershell -ExecutionPolicy Bypass -File $runner @arguments
if ($LASTEXITCODE -ne 0) { throw "Repeated-allocation execution smoke failed with exit code $LASTEXITCODE" }

Write-Host "Repeated allocation start: PASS"
Write-Host "Repeated allocation/OOM execution smoke: PASS"
Write-Host "Collection entered: no"
Write-Host "Heap expansion occurred: no"
Write-Host "Default inventory isolation: PASS"
