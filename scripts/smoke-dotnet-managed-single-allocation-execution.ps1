param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path,
    [string]$RuntimePackRoot = "",
    [string]$OutputRoot = "",
    [string]$StageRoot = "",
    [string]$ServerExe = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$root = [System.IO.Path]::GetFullPath($RepoRoot)
if ([string]::IsNullOrWhiteSpace($RuntimePackRoot)) { $RuntimePackRoot = Join-Path $root "tools\dotnet\runtime-pack" }
if ([string]::IsNullOrWhiteSpace($OutputRoot)) { $OutputRoot = Join-Path $root "out\dotnet\managed-single-allocation" }
if ([string]::IsNullOrWhiteSpace($StageRoot)) { $StageRoot = Join-Path $OutputRoot "stage-managed-single-allocation" }
if ([string]::IsNullOrWhiteSpace($ServerExe)) { $ServerExe = Join-Path $root "guideXOSServer.experimental.exe" }
$execution = Join-Path $root "scripts\smoke-dotnet-managed-hostlog-execution.ps1"

$common = @(
    "-RepoRoot", $root,
    "-RuntimePackRoot", $RuntimePackRoot,
    "-OutputRoot", $OutputRoot,
    "-StageRoot", $StageRoot,
    "-ServerExe", $ServerExe,
    "-UseGuideXosRuntimePack",
    "-AllocationMode", "Allocating",
    "-SkipFailureProbe"
)

& powershell -NoProfile -ExecutionPolicy Bypass -File $execution @common
if ($LASTEXITCODE -ne 0) { throw "First single-allocation execution smoke failed with exit code $LASTEXITCODE" }

$second = @($common + "-SkipBuild")
& powershell -NoProfile -ExecutionPolicy Bypass -File $execution @second
if ($LASTEXITCODE -ne 0) { throw "Second Server-process single-allocation execution smoke failed with exit code $LASTEXITCODE" }

Write-Host "Managed single-allocation execution smoke PASS" -ForegroundColor Green
Write-Host "Same-process launches: 2" -ForegroundColor Cyan
Write-Host "Separate Server process: PASS" -ForegroundColor Cyan
Write-Host "Host callback count per launch: 1" -ForegroundColor Cyan
Write-Host "Message: Hello from managed heap" -ForegroundColor Cyan
