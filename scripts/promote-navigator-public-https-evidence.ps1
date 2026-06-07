param(
    [Parameter(Mandatory = $true)][string]$CandidateMetadataPath,
    [Parameter(Mandatory = $true)][string]$EvidencePath,
    [string]$OutputPath,
    [string[]]$ApprovedTarget = @()
)

$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
. (Join-Path $Root "scripts\navigator-public-https-reviewed-targets.ps1")

function Resolve-NavigatorPromotionPath {
    param([Parameter(Mandatory = $true)][string]$PathValue)

    return [System.IO.Path]::GetFullPath($PathValue)
}

function Read-NavigatorJsonFile {
    param([Parameter(Mandatory = $true)][string]$LiteralPath)

    if (-not (Test-Path -LiteralPath $LiteralPath -PathType Leaf)) {
        throw "JSON file not found: $LiteralPath"
    }
    return Get-Content -LiteralPath $LiteralPath -Raw | ConvertFrom-Json
}

function Test-NavigatorApprovedTarget {
    param(
        [Parameter(Mandatory = $true)][string]$TargetUrl,
        [Parameter(Mandatory = $true)][string[]]$Allowlist
    )

    foreach ($approved in $Allowlist) {
        if ([string]::Equals($approved, $TargetUrl, [System.StringComparison]::OrdinalIgnoreCase)) {
            return $true
        }
    }
    return $false
}

$candidateMetadataFullPath = Resolve-NavigatorPromotionPath -PathValue $CandidateMetadataPath
$evidenceFullPath = Resolve-NavigatorPromotionPath -PathValue $EvidencePath
$allowlistName = Get-NavigatorPublicHttpsReviewedAllowlistName
$allowlistVersion = Get-NavigatorPublicHttpsReviewedAllowlistVersion

if ($ApprovedTarget.Count -le 0) {
    $ApprovedTarget = @(Get-NavigatorPublicHttpsReviewedTargetUrls)
}

$candidate = Read-NavigatorJsonFile -LiteralPath $candidateMetadataFullPath
$evidence = Read-NavigatorJsonFile -LiteralPath $evidenceFullPath

if (-not [string]::Equals([string]$candidate.schema_version, "guidexos.navigator.shipped-root-candidate.v0.1", [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Unsupported candidate metadata schema_version: $($candidate.schema_version)"
}
if (-not [string]::Equals([string]$candidate.bundle_type, "shipped-root-candidate", [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Candidate metadata bundle_type must be shipped-root-candidate."
}
if (-not [string]::Equals([string]$candidate.evidence_required, "yes", [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Candidate metadata evidence_required must be yes."
}
if (-not [string]::Equals([string]$evidence.evidence_status, "PASS", [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Navigator public HTTPS evidence must have evidence_status=PASS."
}
if (-not [string]::Equals([string]$evidence.pass_contract_assertion_result, "PASS", [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Navigator public HTTPS evidence must have pass_contract_assertion_result=PASS."
}
if (-not [string]::Equals([string]$evidence.result_marker, "PASS", [System.StringComparison]::OrdinalIgnoreCase) -or
    -not [string]::Equals([string]$evidence.final_result, "PASS", [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Navigator public HTTPS evidence must report result_marker=PASS and final_result=PASS."
}
if (-not [string]::Equals([string]$evidence.reviewed_allowlist_name, $allowlistName, [System.StringComparison]::OrdinalIgnoreCase) -or
    -not [string]::Equals([string]$evidence.reviewed_allowlist_version, $allowlistVersion, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Navigator public HTTPS evidence must record reviewed allowlist $allowlistName $allowlistVersion."
}

$targetUrl = [string]$evidence.target_url
if (-not (Test-NavigatorApprovedTarget -TargetUrl $targetUrl -Allowlist $ApprovedTarget)) {
    throw "Evidence target_url '$targetUrl' is not in the approved allowlist."
}
$reviewedTarget = Find-NavigatorPublicHttpsReviewedTarget -TargetUrl $targetUrl

$trustBundleType = [string]$evidence.trust_bundle_type
if (@("production-public-probe-merged", "shipped-root-candidate") -notcontains $trustBundleType) {
    throw "Evidence trust_bundle_type '$trustBundleType' is not compatible with shipped-root candidate review."
}

$candidateBundleSha256 = [string]$candidate.bundle_sha256
$rotationId = [string]$candidate.rotation_id
$runtimeRotationId = [string]$evidence.runtime_manifest_rotation_id
$trustBundleSha256 = [string]$evidence.trust_bundle_sha256
$runtimeManifestSha256 = [string]$evidence.runtime_manifest_sha256

$candidateHashMatch = $false
$rotationIdMatch = -not [string]::IsNullOrWhiteSpace($rotationId) -and [string]::Equals($rotationId, $runtimeRotationId, [System.StringComparison]::Ordinal)
$lineageMode = $null
$candidateHashMatchStatus = "no"

switch ($trustBundleType) {
    "shipped-root-candidate" {
        $candidateHashMatch = [string]::Equals($candidateBundleSha256, $trustBundleSha256, [System.StringComparison]::OrdinalIgnoreCase)
        if (-not $candidateHashMatch) {
            throw "Candidate bundle_sha256 does not match evidence trust_bundle_sha256."
        }
        if (-not [string]::Equals([string]$evidence.runtime_manifest_bundle_type, "shipped-root-candidate", [System.StringComparison]::OrdinalIgnoreCase)) {
            throw "Expected runtime_manifest_bundle_type=shipped-root-candidate for direct candidate proof."
        }
        $lineageMode = "direct-candidate-hash-match"
        $candidateHashMatchStatus = "yes"
    }
    "production-public-probe-merged" {
        if (-not [string]::Equals([string]$evidence.runtime_manifest_bundle_type, "production-public-probe-merged", [System.StringComparison]::OrdinalIgnoreCase)) {
            throw "Expected runtime_manifest_bundle_type=production-public-probe-merged for merged candidate/public proof."
        }
        if (-not $rotationIdMatch) {
            throw "Candidate rotation_id does not match evidence runtime_manifest_rotation_id."
        }
        if ([string]::Equals($candidateBundleSha256, $trustBundleSha256, [System.StringComparison]::OrdinalIgnoreCase) -or
            [string]::Equals($candidateBundleSha256, $runtimeManifestSha256, [System.StringComparison]::OrdinalIgnoreCase)) {
            $candidateHashMatch = $true
            $candidateHashMatchStatus = "yes"
            $lineageMode = "merged-bundle-with-direct-hash-match"
        } else {
            $candidateHashMatchStatus = "not-directly-comparable-merged-bundle"
            $lineageMode = "rotation-id-linked-merged-bundle"
        }
    }
}

if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $candidateDirectory = Split-Path -Parent $candidateMetadataFullPath
    $candidateStem = [System.IO.Path]::GetFileNameWithoutExtension($candidateMetadataFullPath)
    $OutputPath = Join-Path $candidateDirectory ($candidateStem + ".evidence-link.json")
}
$outputFullPath = Resolve-NavigatorPromotionPath -PathValue $OutputPath
$outputDirectory = Split-Path -Parent $outputFullPath
if (-not [string]::IsNullOrWhiteSpace($outputDirectory)) {
    New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
}

$record = [ordered]@{
    schema_version = "guidexos.navigator.shipped-root-candidate-evidence.v0.1"
    generated_utc = ([datetime]::UtcNow.ToString("o"))
    reviewed_allowlist_name = $allowlistName
    reviewed_allowlist_version = $allowlistVersion
    candidate_id = [string]$candidate.candidate_id
    rotation_id = $rotationId
    candidate_bundle_type = [string]$candidate.bundle_type
    candidate_bundle_sha256 = $candidateBundleSha256
    candidate_root_count = [int]$candidate.root_count
    candidate_pem_bytes = [int64]$candidate.pem_bytes
    candidate_production_ready = [string]$candidate.production_ready
    candidate_test_only = [string]$candidate.test_only
    candidate_source_description = [string]$candidate.source_description
    candidate_metadata_filename = [System.IO.Path]::GetFileName($candidateMetadataFullPath)
    candidate_manifest_filename = [string]$candidate.manifest_filename
    approved_target_verified = "yes"
    evidence_status = [string]$evidence.evidence_status
    evidence_target_url = $targetUrl
    evidence_target_host = [string]$evidence.target_host
    evidence_target_allowlist = $allowlistName
    evidence_target_allowlist_version = $allowlistVersion
    evidence_target_reason = $(if ($null -ne $reviewedTarget) { [string]$reviewedTarget.Reason } else { "Approved through explicit caller allowlist." })
    evidence_result_marker = [string]$evidence.result_marker
    evidence_final_result = [string]$evidence.final_result
    evidence_trust_bundle_type = $trustBundleType
    evidence_trust_bundle_sha256 = $trustBundleSha256
    evidence_runtime_manifest_bundle_type = [string]$evidence.runtime_manifest_bundle_type
    evidence_runtime_manifest_rotation_id = $runtimeRotationId
    evidence_runtime_manifest_sha256 = $runtimeManifestSha256
    evidence_filename = [System.IO.Path]::GetFileName($evidenceFullPath)
    linkage_verification_mode = $lineageMode
    candidate_hash_match = $candidateHashMatchStatus
    rotation_id_match = $(if ($rotationIdMatch) { "yes" } else { "no" })
    promotion_result = "PASS"
    notes = @(
        "This review record does not authorize default public HTTPS browsing.",
        "Keep the source PEM out of git; archive only metadata, manifests, and reviewed evidence artifacts."
    )
}

$recordJson = $record | ConvertTo-Json -Depth 5
[System.IO.File]::WriteAllText($outputFullPath, $recordJson + [Environment]::NewLine, [System.Text.Encoding]::ASCII)

Write-Output "Navigator shipped-root candidate evidence linked:"
Write-Output "  candidate metadata: $candidateMetadataFullPath"
Write-Output "  public HTTPS evidence: $evidenceFullPath"
Write-Output "  output: $outputFullPath"
Write-Output "  linkage_verification_mode: $lineageMode"
Write-Output "  candidate_hash_match: $candidateHashMatchStatus"
Write-Output "  rotation_id_match: $(if ($rotationIdMatch) { 'yes' } else { 'no' })"
