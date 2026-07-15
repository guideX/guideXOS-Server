param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path,
    [string]$RuntimePackRoot = "",
    [string]$OutputRoot = "",
    [string]$StockRuntimePackRoot = "",
    [string]$ExternalRuntimeRoot = "",
    [switch]$ManagedAllocation,
    [switch]$Clean
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

if ([string]::IsNullOrWhiteSpace($RuntimePackRoot)) {
    $RuntimePackRoot = Join-Path $RepoRoot "tools\dotnet\runtime-pack"
}
$build = Join-Path ([System.IO.Path]::GetFullPath($RuntimePackRoot)) "build-runtime-pack.ps1"
if (-not (Test-Path -LiteralPath $build)) { throw "Runtime-pack build script not found: $build" }

$arguments = @(
    '-ExecutionPolicy', 'Bypass',
    '-File', $build,
    '-RepoRoot', ([System.IO.Path]::GetFullPath($RepoRoot)),
    '-RuntimePackRoot', ([System.IO.Path]::GetFullPath($RuntimePackRoot))
)
if (-not [string]::IsNullOrWhiteSpace($OutputRoot)) {
    $arguments += @('-OutputRoot', ([System.IO.Path]::GetFullPath($OutputRoot)))
}
if (-not [string]::IsNullOrWhiteSpace($StockRuntimePackRoot)) {
    $arguments += @('-StockRuntimePackRoot', ([System.IO.Path]::GetFullPath($StockRuntimePackRoot)))
}
if (-not [string]::IsNullOrWhiteSpace($ExternalRuntimeRoot)) {
    $arguments += @('-ExternalRuntimeRoot', ([System.IO.Path]::GetFullPath($ExternalRuntimeRoot)))
}
if ($ManagedAllocation) { $arguments += '-ManagedAllocation' }
if ($Clean) { $arguments += '-Clean' }
& powershell @arguments
if ($LASTEXITCODE -ne 0) { throw "guideXOS NativeAOT runtime-pack build failed with exit code $LASTEXITCODE" }
