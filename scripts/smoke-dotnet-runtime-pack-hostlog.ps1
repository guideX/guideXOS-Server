param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path,
    [string]$RuntimePackRoot = "",
    [string]$OutputRoot = "",
    [string]$StageRoot = "",
    [string]$ServerExe = "",
    [switch]$SkipBuild,
    [switch]$SkipFailureProbe
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repoRoot = [System.IO.Path]::GetFullPath($RepoRoot)
if ([string]::IsNullOrWhiteSpace($RuntimePackRoot)) { $RuntimePackRoot = Join-Path $repoRoot "tools\dotnet\runtime-pack" }
if ([string]::IsNullOrWhiteSpace($OutputRoot)) { $OutputRoot = Join-Path $repoRoot "out\dotnet\managed-hostlog" }
if ([string]::IsNullOrWhiteSpace($StageRoot)) { $StageRoot = Join-Path $OutputRoot "stage-managed-hostlog-proof" }
if ([string]::IsNullOrWhiteSpace($ServerExe)) { $ServerExe = Join-Path $repoRoot "guideXOSServer.experimental.exe" }
$executionScript = Join-Path $repoRoot "scripts\smoke-dotnet-managed-hostlog-execution.ps1"

$firstArguments = @(
    "-RepoRoot", $repoRoot,
    "-RuntimePackRoot", $RuntimePackRoot,
    "-OutputRoot", $OutputRoot,
    "-StageRoot", $StageRoot,
    "-ServerExe", $ServerExe,
    "-UseGuideXosRuntimePack",
    "-SkipFailureProbe"
)
if ($SkipBuild) { $firstArguments += "-SkipBuild" }
& powershell -ExecutionPolicy Bypass -File $executionScript @firstArguments
if ($LASTEXITCODE -ne 0) { throw "First custom runtime-pack Server smoke failed with exit code $LASTEXITCODE" }

$secondArguments = @(
    "-RepoRoot", $repoRoot,
    "-RuntimePackRoot", $RuntimePackRoot,
    "-OutputRoot", $OutputRoot,
    "-StageRoot", $StageRoot,
    "-ServerExe", $ServerExe,
    "-UseGuideXosRuntimePack",
    "-SkipBuild",
    "-SkipFailureProbe"
)
& powershell -ExecutionPolicy Bypass -File $executionScript @secondArguments
if ($LASTEXITCODE -ne 0) { throw "Second custom runtime-pack Server smoke failed with exit code $LASTEXITCODE" }

Write-Host "Runtime-pack HostLogProof smoke PASS" -ForegroundColor Green
Write-Host "Same-process repeat: PASS" -ForegroundColor Cyan
Write-Host "Separate Server process repeat: PASS" -ForegroundColor Cyan
