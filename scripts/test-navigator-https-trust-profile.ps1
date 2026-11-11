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
Assert-Equal "candidate profile syntax" $result.Valid $true
Assert-Equal "candidate profile authorization" $result.Authorized $false
Assert-Equal "missing metadata" (Test-NavigatorTrustProfileMetadata -Profile $null -ExpectedBundleSha256 $sha).Reason "missing"

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
