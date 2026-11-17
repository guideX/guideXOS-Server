$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $ScriptDir "navigator_https_trust_profile.ps1")

function Assert-Equal {
    param([string]$Name, $Actual, $Expected)
    if ($Actual -ne $Expected) { throw "$Name expected '$Expected', got '$Actual'." }
}

$allowed = @("disabled", "local-smoke-only", "user-trust-dev-mode", "controlled-qualification", "public-pilot-candidate")
foreach ($token in $allowed) { Assert-NavigatorHttpsPolicyOverrideAllowed -Value $token }
foreach ($token in @("production-validated", "ProductionValidated", "arbitrary-public")) {
    $rejected = $false
    try { Assert-NavigatorHttpsPolicyOverrideAllowed -Value $token } catch { $rejected = $true }
    Assert-Equal "policy override $token" $rejected $true
}

$sha = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
$manifest = @{
    sha256 = $sha
    bundle_type = "production-source"
    rotation_id = "mozilla-2026-11-05-review"
    source = "curl CA Extract; Mozilla roots 2026-11-05; review candidate not activated"
    production_ready = "yes"
}
$profile = New-NavigatorTrustProfileDocument -Manifest $manifest -ProfileClass "candidate" -SourceDate "2026-11-05"
Assert-Equal "profile schema" $profile.schema_version "guidexos.navigator.trust-profile.v1"
Assert-Equal "profile class" $profile.profile_class "candidate"
Assert-Equal "profile hash" $profile.bundle_sha256 $sha
Assert-Equal "legacy readiness ignored" $profile.approval_state "pending"
Assert-Equal "no generated authorization evidence" $profile.authorization_evidence "none"
Assert-Equal "candidate requires authenticated time" $profile.clock_assurance "authenticated-fresh-rollback-protected-required"
$fixtureProfile = New-NavigatorTrustProfileDocument -Manifest $manifest -ProfileClass "fixture-only" -SourceDate "2026-11-05"
Assert-Equal "fixture permits plausible time only in fixture lane" $fixtureProfile.clock_assurance "plausible-allowed-controlled-fixtures-only"
Assert-Equal "fixture does not claim authenticated-time capability" ($fixtureProfile.required_verifier_capabilities -contains "authenticated-time") $false

$result = Test-NavigatorTrustProfileMetadata -Profile $profile -ExpectedBundleSha256 $sha
Assert-Equal "candidate without Mozilla metadata is rejected" $result.Valid $false
Assert-Equal "missing candidate metadata reason" $result.Reason "missing-mozilla-trust-metadata"
Assert-Equal "candidate profile authorization" $result.Authorized $false
Assert-Equal "missing metadata" (Test-NavigatorTrustProfileMetadata -Profile $null -ExpectedBundleSha256 $sha).Reason "missing"
$fixtureValidation = Test-NavigatorTrustProfileMetadata -Profile $fixtureProfile -ExpectedBundleSha256 $sha
Assert-Equal "fixture profile remains valid" $fixtureValidation.Valid $true

$mozillaHash = "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789"
$eligibleMozillaManifest = [ordered]@{
    schema_version = "guidexos.navigator.mozilla-trust-manifest.v1"
    source_lock_sha256 = $sha
    input_candidate_bundle_sha256 = $sha
    external_policy_complete = $true
    fixture_root_contamination = "none-detected"
    technically_eligible_root_count = 1
    candidate_included_root_count = 1
    production_approved_root_count = 0
    public_https_enabled = $false
    roots = @([ordered]@{
        certificate_sha256 = $sha
        identity_verified = $true
        server_auth_trust_record = "CKT_NSS_TRUSTED_DELEGATOR"
        external_restrictions = "complete"
        domain_restrictions_status = "none"
        name_constraints_status = "none"
        certificate_policy_status = "not-required"
        path_validation_status = "verified-by-mbedtls-4.1.0"
        technical_eligibility = "eligible"
        candidate_included = $true
        production_approved = $false
        eligibility_rejection_reasons = @()
        unknown_nss_fields = @()
    })
}
$profileWithMetadata = [ordered]@{}
foreach ($entry in $profile.GetEnumerator()) { $profileWithMetadata[$entry.Key] = $entry.Value }
$profileWithMetadata.mozilla_trust_manifest_sha256 = $mozillaHash
$profileWithMetadata.mozilla_trust_manifest_schema_version = "guidexos.navigator.mozilla-trust-manifest.v1"
$eligibleMetadataResult = Test-NavigatorTrustProfileMetadata -Profile $profileWithMetadata `
    -ExpectedBundleSha256 $sha -MozillaTrustManifest $eligibleMozillaManifest `
    -ActualMozillaTrustManifestSha256 $mozillaHash
Assert-Equal "technically eligible metadata validates" $eligibleMetadataResult.Valid $true
Assert-Equal "technical candidate does not imply production authorization" $eligibleMetadataResult.Authorized $false
Assert-Equal "candidate stays pending production approval" $eligibleMetadataResult.Reason "approval-not-authenticated"

$tamperedMetadata = Test-NavigatorTrustProfileMetadata -Profile $profileWithMetadata `
    -ExpectedBundleSha256 $sha -MozillaTrustManifest $eligibleMozillaManifest `
    -ActualMozillaTrustManifestSha256 ("f" + $mozillaHash.Substring(1))
Assert-Equal "tampered metadata hash is rejected" $tamperedMetadata.Reason "mozilla-metadata-hash-mismatch"
$missingMetadata = Test-NavigatorTrustProfileMetadata -Profile $profileWithMetadata -ExpectedBundleSha256 $sha
Assert-Equal "candidate without manifest input is rejected" $missingMetadata.Reason "missing-mozilla-trust-metadata"
$unsupportedMozilla = [ordered]@{}
foreach ($entry in $eligibleMozillaManifest.GetEnumerator()) { $unsupportedMozilla[$entry.Key] = $entry.Value }
$unsupportedMozilla.schema_version = "guidexos.navigator.mozilla-trust-manifest.v99"
Assert-Equal "unknown Mozilla metadata version is rejected" `
    (Test-NavigatorMozillaTrustManifestMetadata -Manifest $unsupportedMozilla -ExpectedBundleSha256 $sha `
        -ExpectedManifestSha256 $mozillaHash -ActualManifestSha256 $mozillaHash).Reason `
    "unsupported-mozilla-metadata-version"
$tamperedPolicy = [ordered]@{}
foreach ($entry in $eligibleMozillaManifest.GetEnumerator()) { $tamperedPolicy[$entry.Key] = $entry.Value }
$tamperedPolicy.input_candidate_bundle_sha256 = "f" + $sha.Substring(1)
Assert-Equal "tampered input fingerprint is rejected" `
    (Test-NavigatorMozillaTrustManifestMetadata -Manifest $tamperedPolicy -ExpectedBundleSha256 $sha `
        -ExpectedManifestSha256 $mozillaHash -ActualManifestSha256 $mozillaHash).Reason `
    "mozilla-source-bundle-hash-mismatch"
$incompletePolicy = [ordered]@{}
foreach ($entry in $eligibleMozillaManifest.GetEnumerator()) { $incompletePolicy[$entry.Key] = $entry.Value }
$incompletePolicy.external_policy_complete = $false
Assert-Equal "missing external restrictions are rejected" `
    (Test-NavigatorMozillaTrustManifestMetadata -Manifest $incompletePolicy -ExpectedBundleSha256 $sha `
        -ExpectedManifestSha256 $mozillaHash -ActualManifestSha256 $mozillaHash).Reason `
    "mozilla-external-policy-incomplete"
$wrongPurpose = [ordered]@{}
foreach ($entry in $eligibleMozillaManifest.roots[0].GetEnumerator()) { $wrongPurpose[$entry.Key] = $entry.Value }
$wrongPurpose.server_auth_trust_record = "CKT_NSS_MUST_VERIFY_TRUST"
$wrongPurposeManifest = [ordered]@{}
foreach ($entry in $eligibleMozillaManifest.GetEnumerator()) { $wrongPurposeManifest[$entry.Key] = $entry.Value }
$wrongPurposeManifest.roots = @($wrongPurpose)
Assert-Equal "wrong server-auth purpose is rejected" `
    (Test-NavigatorMozillaTrustManifestMetadata -Manifest $wrongPurposeManifest -ExpectedBundleSha256 $sha `
        -ExpectedManifestSha256 $mozillaHash -ActualManifestSha256 $mozillaHash).Reason `
    "root-eligibility-record-invalid"
$distrusted = [ordered]@{}
foreach ($entry in $eligibleMozillaManifest.roots[0].GetEnumerator()) { $distrusted[$entry.Key] = $entry.Value }
$distrusted.server_auth_trust_record = "CKT_NSS_NOT_TRUSTED"
$distrustedManifest = [ordered]@{}
foreach ($entry in $eligibleMozillaManifest.GetEnumerator()) { $distrustedManifest[$entry.Key] = $entry.Value }
$distrustedManifest.roots = @($distrusted)
Assert-Equal "distrusted root is rejected" `
    (Test-NavigatorMozillaTrustManifestMetadata -Manifest $distrustedManifest -ExpectedBundleSha256 $sha `
        -ExpectedManifestSha256 $mozillaHash -ActualManifestSha256 $mozillaHash).Reason `
    "root-eligibility-record-invalid"
$fixtureContaminated = [ordered]@{}
foreach ($entry in $eligibleMozillaManifest.GetEnumerator()) { $fixtureContaminated[$entry.Key] = $entry.Value }
$fixtureContaminated.fixture_root_contamination = "detected"
Assert-Equal "fixture root contamination is rejected" `
    (Test-NavigatorMozillaTrustManifestMetadata -Manifest $fixtureContaminated -ExpectedBundleSha256 $sha `
        -ExpectedManifestSha256 $mozillaHash -ActualManifestSha256 $mozillaHash).Reason `
    "fixture-root-contamination"
$bypass = [ordered]@{}
foreach ($entry in $eligibleMozillaManifest.GetEnumerator()) { $bypass[$entry.Key] = $entry.Value }
$bypass.production_approved_root_count = 1
$bypass.production_ready = "yes"
Assert-Equal "legacy production_ready cannot bypass owner approval" `
    (Test-NavigatorMozillaTrustManifestMetadata -Manifest $bypass -ExpectedBundleSha256 $sha `
        -ExpectedManifestSha256 $mozillaHash -ActualManifestSha256 $mozillaHash).Reason `
    "candidate-approval-state-invalid"

$repoRoot = (Resolve-Path -LiteralPath (Join-Path $ScriptDir "..")).Path
$importedManifestPath = Join-Path $repoRoot "assets\certs\candidates\mozilla-nss-2026-11-13.navhttps20-trust-manifest.json"
$importedManifest = Get-Content -LiteralPath $importedManifestPath -Raw | ConvertFrom-Json
$importedManifestHash = (Get-FileHash -LiteralPath $importedManifestPath -Algorithm SHA256).Hash.ToLowerInvariant()
$importedMetadataCheck = Test-NavigatorMozillaTrustManifestMetadata -Manifest $importedManifest `
    -ExpectedBundleSha256 ([string]$importedManifest.input_candidate_bundle_sha256) `
    -ExpectedManifestSha256 $importedManifestHash -ActualManifestSha256 $importedManifestHash
Assert-Equal "pinned 118-root snapshot remains blocked by missing external metadata" $importedMetadataCheck.Valid $false
Assert-Equal "incomplete source policy remains a hard gate" $importedMetadataCheck.Reason "mozilla-external-policy-incomplete"
Assert-Equal "public HTTPS stays disabled in imported metadata" $importedManifest.public_https_enabled $false

$mismatch = [ordered]@{}
foreach ($entry in $profile.GetEnumerator()) { $mismatch[$entry.Key] = $entry.Value }
$mismatch.bundle_sha256 = "f" + $sha.Substring(1)
Assert-Equal "modified bundle identity" (Test-NavigatorTrustProfileMetadata -Profile $mismatch -ExpectedBundleSha256 $sha).Reason "bundle-hash-mismatch"

$unsupported = [ordered]@{}
foreach ($entry in $profile.GetEnumerator()) { $unsupported[$entry.Key] = $entry.Value }
$unsupported.schema_version = "guidexos.navigator.trust-profile.v99"
Assert-Equal "unsupported profile version" (Test-NavigatorTrustProfileMetadata -Profile $unsupported -ExpectedBundleSha256 $sha).Reason "unsupported-version"

$withoutApproval = [ordered]@{}
foreach ($entry in $profile.GetEnumerator()) { $withoutApproval[$entry.Key] = $entry.Value }
$withoutApproval.profile_class = "release-approved"
$withoutApproval.approval_state = "release-approved"
$withoutApproval.authorization_evidence = "none"
$withoutApproval.approval_authenticated = $true
Assert-Equal "missing approval evidence" (Test-NavigatorTrustProfileMetadata -Profile $withoutApproval -ExpectedBundleSha256 $sha).Reason "missing-approval-evidence"

$claimedApproval = [ordered]@{}
foreach ($entry in $profile.GetEnumerator()) { $claimedApproval[$entry.Key] = $entry.Value }
$claimedApproval.profile_class = "release-approved"
$claimedApproval.approval_state = "release-approved"
$claimedApproval.authorization_evidence = "developer-config-claims-approved"
$claimedApproval.approval_authenticated = $true
$claimedResult = Test-NavigatorTrustProfileMetadata -Profile $claimedApproval -ExpectedBundleSha256 $sha
Assert-Equal "self-asserted approval does not authorize" $claimedResult.Authorized $false
Assert-Equal "missing signature verifier" $claimedResult.Reason "approval-not-authenticated"

Write-Output "NAVIGATOR_HTTPS_TRUST_PROFILE_RESULT: PASS"
Write-Output "Assertions: policy override rejection, versioned metadata, integrity binding, unsupported version, missing evidence, and non-self-authorizing approval."
