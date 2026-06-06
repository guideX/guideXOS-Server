param(
    [Parameter(Mandatory = $true)][string]$BundlePath,
    [string]$CandidateId,
    [string]$RotationId,
    [Parameter(Mandatory = $true)][string]$SourceDescription,
    [string]$OutputDir,
    [string]$Reviewer,
    [switch]$MarkReviewed,
    [string[]]$Notes = @()
)

$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$ValidateScript = Join-Path $Root "scripts\validate-navigator-ca-bundle.ps1"

function Resolve-NavigatorCandidatePath {
    param([Parameter(Mandatory = $true)][string]$PathValue)

    $candidate = $PathValue.Trim()
    if (-not [System.IO.Path]::IsPathRooted($candidate)) {
        $candidate = Join-Path $Root $candidate
    }
    return [System.IO.Path]::GetFullPath($candidate)
}

function Test-NavigatorCandidateToken {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [AllowNull()][string]$Value
    )

    if ([string]::IsNullOrWhiteSpace($Value)) {
        return $null
    }

    $trimmed = $Value.Trim()
    if (-not [regex]::IsMatch($trimmed, '^[A-Za-z0-9._:-]{3,120}$')) {
        throw "$Name must match ^[A-Za-z0-9._:-]{3,120}$."
    }
    return $trimmed
}

function Test-NavigatorSafeSourceDescription {
    param([Parameter(Mandatory = $true)][string]$Value)

    $trimmed = $Value.Trim()
    if ([string]::IsNullOrWhiteSpace($trimmed)) {
        throw "SourceDescription is required."
    }
    if ($trimmed.Length -gt 160) {
        throw "SourceDescription must be 160 characters or fewer."
    }
    if ([System.IO.Path]::IsPathRooted($trimmed) -or $trimmed -match '^[A-Za-z]:[\\/]' -or $trimmed -match '^\\\\') {
        throw "SourceDescription must be a safe descriptive label, not a rooted local path."
    }
    if ($trimmed.Contains("`r") -or $trimmed.Contains("`n")) {
        throw "SourceDescription must be a single line."
    }
    return $trimmed
}

function Get-NavigatorCandidateDefaultOutputDir {
    param([Parameter(Mandatory = $true)][string]$EffectiveCandidateId)

    return Join-Path $Root ("logs\navigator-shipped-root-candidates\" + $EffectiveCandidateId)
}

$bundleFullPath = Resolve-NavigatorCandidatePath -PathValue $BundlePath
if (-not (Test-Path -LiteralPath $bundleFullPath -PathType Leaf)) {
    throw "Candidate bundle not found: $bundleFullPath"
}

$normalizedCandidateId = Test-NavigatorCandidateToken -Name "CandidateId" -Value $CandidateId
$normalizedRotationId = Test-NavigatorCandidateToken -Name "RotationId" -Value $RotationId
if (-not $normalizedCandidateId -and -not $normalizedRotationId) {
    throw "Provide CandidateId, RotationId, or both."
}
if (-not $normalizedCandidateId) {
    $normalizedCandidateId = $normalizedRotationId
}
if (-not $normalizedRotationId) {
    $normalizedRotationId = $normalizedCandidateId
}

$safeSourceDescription = Test-NavigatorSafeSourceDescription -Value $SourceDescription
if ($MarkReviewed -and [string]::IsNullOrWhiteSpace($Reviewer)) {
    throw "MarkReviewed requires Reviewer."
}

$candidateOutputDir = if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    Get-NavigatorCandidateDefaultOutputDir -EffectiveCandidateId $normalizedCandidateId
} else {
    Resolve-NavigatorCandidatePath -PathValue $OutputDir
}
New-Item -ItemType Directory -Force -Path $candidateOutputDir | Out-Null

$manifestPath = Join-Path $candidateOutputDir "ca-bundle.manifest"
$metadataPath = Join-Path $candidateOutputDir ($normalizedCandidateId + ".candidate.json")

$validateArgs = @(
    "-NoProfile",
    "-ExecutionPolicy", "Bypass",
    "-File", $ValidateScript,
    "-BundlePath", $bundleFullPath,
    "-BundleType", "shipped-root-candidate",
    "-OutputManifestPath", $manifestPath,
    "-SourceDescription", $safeSourceDescription,
    "-RotationId", $normalizedRotationId
)
if ($MarkReviewed) {
    $validateArgs += @("-ProductionReady", "yes")
}

$validateOutput = @(& powershell @validateArgs 2>&1)
if ($LASTEXITCODE -ne 0) {
    foreach ($line in $validateOutput) {
        Write-Output "$line"
    }
    throw "Navigator shipped-root candidate preparation failed."
}

$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if (-not [string]::Equals([string]$manifest.bundle_type, "shipped-root-candidate", [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Candidate manifest bundle_type must be shipped-root-candidate."
}

$defaultNotes = @(
    "Candidate metadata does not authorize default public HTTPS browsing.",
    "Archive dedicated public HTTPS PASS evidence before any shipped-root enablement decision."
)
$allNotes = @($defaultNotes + @($Notes | Where-Object { -not [string]::IsNullOrWhiteSpace($_) }))

$reviewedUtc = $null
$reviewerValue = "(pending-review)"
if ($MarkReviewed) {
    $reviewedUtc = [datetime]::UtcNow.ToString("o")
    $reviewerValue = $Reviewer.Trim()
}

$metadata = [ordered]@{
    schema_version = "guidexos.navigator.shipped-root-candidate.v0.1"
    generated_utc = ([datetime]::UtcNow.ToString("o"))
    candidate_id = $normalizedCandidateId
    rotation_id = $normalizedRotationId
    bundle_sha256 = [string]$manifest.sha256
    root_count = [int]$manifest.root_count
    pem_bytes = [int64]$manifest.pem_bytes
    bundle_type = [string]$manifest.bundle_type
    production_ready = $(if ($MarkReviewed) { "yes" } else { "no" })
    test_only = "no"
    proposed_utc = ([datetime]::UtcNow.ToString("o"))
    reviewed_utc = $reviewedUtc
    reviewer = $reviewerValue
    source_description = $safeSourceDescription
    evidence_required = "yes"
    evidence_status = "pending-public-proof"
    last_public_probe_target = $null
    last_public_probe_utc = $null
    last_public_probe_result = "not-run"
    manifest_schema_version = [string]$manifest.schema_version
    manifest_filename = [System.IO.Path]::GetFileName($manifestPath)
    notes = @($allNotes)
}

$metadataJson = $metadata | ConvertTo-Json -Depth 5
[System.IO.File]::WriteAllText($metadataPath, $metadataJson + [Environment]::NewLine, [System.Text.Encoding]::ASCII)

foreach ($line in $validateOutput) {
    Write-Output "$line"
}
Write-Output "Navigator shipped-root candidate prepared:"
Write-Output "  candidate_id: $normalizedCandidateId"
Write-Output "  rotation_id: $normalizedRotationId"
Write-Output "  bundle: $bundleFullPath"
Write-Output "  manifest: $manifestPath"
Write-Output "  metadata: $metadataPath"
Write-Output "  production_ready: $($metadata.production_ready)"
Write-Output "  evidence_status: $($metadata.evidence_status)"
