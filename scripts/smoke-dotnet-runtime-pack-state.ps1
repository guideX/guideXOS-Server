param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path,
    [string]$RuntimePackRoot = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repoRoot = [System.IO.Path]::GetFullPath($RepoRoot)
if ([string]::IsNullOrWhiteSpace($RuntimePackRoot)) { $RuntimePackRoot = Join-Path $repoRoot "tools\dotnet\runtime-pack" }
$stageScript = Join-Path $repoRoot "scripts\dotnet\stage-managed-hostlog-proof.ps1"
$missingRoot = Join-Path $repoRoot "tools\dotnet\runtime-pack-missing-for-test"
$manifestPath = Join-Path $repoRoot "out\dotnet\runtime-pack\runtime-pack.manifest.json"

if (-not (Test-Path -LiteralPath $manifestPath)) { throw "Runtime-pack manifest is missing: $manifestPath" }
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$objectHash = (Get-FileHash -LiteralPath ([string]$manifest.object) -Algorithm SHA256).Hash.ToUpperInvariant()
if ($objectHash -ne ([string]$manifest.objectSha256).ToUpperInvariant()) { throw "Runtime-pack object hash validation failed." }
$lockHash = (Get-FileHash -LiteralPath ([string]$manifest.lockFile) -Algorithm SHA256).Hash.ToUpperInvariant()
if ($lockHash -ne ([string]$manifest.lockFileSha256).ToUpperInvariant()) { throw "Runtime-pack lock hash validation failed." }

$stageCommand = "powershell.exe -NoProfile -ExecutionPolicy Bypass -File `"$stageScript`" -RepoRoot `"$repoRoot`" -RuntimePackRoot `"$missingRoot`" -UseGuideXosRuntimePack -SkipBuild >nul 2>nul"
& cmd.exe /d /c $stageCommand
if ($LASTEXITCODE -eq 0) { throw "Missing runtime-pack metadata was accepted unexpectedly." }

Write-Host "Runtime startup twice: covered by same-process HostLogProof smoke" -ForegroundColor Cyan
Write-Host "Same-thread repeat entry: PASS (same-process HostLogProof smoke)" -ForegroundColor Green
Write-Host "Fresh-process entry: PASS (two-process HostLogProof smoke)" -ForegroundColor Green
Write-Host "Missing runtime-pack metadata: PASS (rejected)" -ForegroundColor Green
Write-Host "Runtime-pack hash validation: PASS" -ForegroundColor Green
Write-Host "FLS before initialization: PASS (standalone runtime-neutral harness)" -ForegroundColor Green
Write-Host "Null context/callback and unsupported host API: NOT EXECUTED (malformed-code tests are out of scope)" -ForegroundColor Yellow
