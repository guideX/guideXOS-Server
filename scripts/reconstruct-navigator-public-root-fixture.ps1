param(
    [string]$SourceBundlePath,
    [string]$OutputBundlePath,
    [string]$OutputManifestPath,
    [string]$GeneratedUtc = "2026-08-13T03:12:01Z"
)

$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$AuthoritativeSourcePath = Join-Path $Root "assets\certs\mozilla-cacert-2026-08-13.pem"
$DefaultOutputBundlePath = Join-Path $Root "scripts\fixtures\public-roots\ca-bundle.pem.local"
$DefaultOutputManifestPath = Join-Path $Root "logs\navigator-public-root-fixture.manifest"
$ValidatorPath = Join-Path $Root "scripts\validate-navigator-ca-bundle.ps1"
$ExpectedBytes = 188900
$ExpectedCertificateCount = 121
$ExpectedSha256 = "f66dff1bdf8f96060b8177976f8b7d9254bc89bc4db933d769f7384d28480bc9"

if ([string]::IsNullOrWhiteSpace($SourceBundlePath)) {
    $SourceBundlePath = $AuthoritativeSourcePath
}
if ([string]::IsNullOrWhiteSpace($OutputBundlePath)) {
    $OutputBundlePath = $DefaultOutputBundlePath
}
if ([string]::IsNullOrWhiteSpace($OutputManifestPath)) {
    $OutputManifestPath = $DefaultOutputManifestPath
}

function Resolve-RepositoryPath {
    param([Parameter(Mandatory = $true)][string]$PathValue)

    if ([System.IO.Path]::IsPathRooted($PathValue)) {
        return [System.IO.Path]::GetFullPath($PathValue)
    }
    return [System.IO.Path]::GetFullPath((Join-Path $Root $PathValue))
}

$sourceFullPath = Resolve-RepositoryPath $SourceBundlePath
$outputFullPath = Resolve-RepositoryPath $OutputBundlePath
$manifestFullPath = Resolve-RepositoryPath $OutputManifestPath

if (-not (Test-Path -LiteralPath $sourceFullPath -PathType Leaf)) {
    throw "Authoritative production CA source is missing: $sourceFullPath"
}

$sourceItem = Get-Item -LiteralPath $sourceFullPath
$sourceHash = (Get-FileHash -LiteralPath $sourceFullPath -Algorithm SHA256).Hash.ToLowerInvariant()
if ($sourceItem.Length -ne $ExpectedBytes -or $sourceHash -ne $ExpectedSha256) {
    throw "Refusing to reconstruct the public fixture: source identity is not the reviewed Mozilla bundle (bytes=$($sourceItem.Length), sha256=$sourceHash)."
}

$outputDirectory = Split-Path -Parent $outputFullPath
if (-not [string]::IsNullOrWhiteSpace($outputDirectory)) {
    New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
}
Copy-Item -LiteralPath $sourceFullPath -Destination $outputFullPath -Force

$validatorArgs = @{
    BundlePath = $outputFullPath
    BundleType = "production-public-source"
    OutputManifestPath = $manifestFullPath
    SourceDescription = "tracked-curl-ca-extract-Mozilla-2026-08-13; source_sha256=$ExpectedSha256"
    RotationId = "mozilla-2026-08-13"
    GeneratedUtc = $GeneratedUtc
}
& $ValidatorPath @validatorArgs

$manifest = Get-Content -LiteralPath $manifestFullPath -Raw | ConvertFrom-Json
if ([int64]$manifest.pem_bytes -ne $ExpectedBytes -or
    [int]$manifest.root_count -ne $ExpectedCertificateCount -or
    $manifest.sha256 -ne $ExpectedSha256 -or
    $manifest.production_ready -ne "yes" -or
    $manifest.test_only -ne "no") {
    throw "Reconstructed public fixture manifest does not match the reviewed production identity."
}

Write-Output "Navigator public-root fixture reconstructed from the tracked Mozilla source."
Write-Output "source=$sourceFullPath"
Write-Output "output=$outputFullPath"
Write-Output "manifest=$manifestFullPath"
Write-Output "bytes=$($manifest.pem_bytes)"
Write-Output "certificates=$($manifest.root_count)"
Write-Output "sha256=$($manifest.sha256)"
Write-Output "production_ready=$($manifest.production_ready)"
Write-Output "test_only=$($manifest.test_only)"
