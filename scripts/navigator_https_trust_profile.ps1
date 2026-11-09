function Assert-NavigatorHttpsPolicyOverrideAllowed {
    param([AllowNull()][string]$Value)
    if ([string]::IsNullOrWhiteSpace($Value)) { return }
    $allowed = @("disabled", "local-smoke-only", "user-trust-dev-mode", "controlled-qualification", "public-pilot-candidate")
    if ($allowed -notcontains $Value.Trim().ToLowerInvariant()) {
        throw "GXOS_NAVIGATOR_HTTPS_POLICY '$Value' is not an activation authorization. Use disabled, local-smoke-only, user-trust-dev-mode, controlled-qualification, or public-pilot-candidate. ProductionValidated requires an authenticated release trust anchor, which this build does not provide."
    }
}

function New-NavigatorTrustProfileDocument {
    param(
        [Parameter(Mandatory = $true)]$Manifest,
        [Parameter(Mandatory = $true)][ValidateSet("fixture-only", "candidate")][string]$ProfileClass,
        [Parameter(Mandatory = $true)][string]$SourceDate
    )
    $hash = ([string]$Manifest.sha256).ToLowerInvariant()
    if ($hash -notmatch '^[0-9a-f]{64}$') {
        throw "Cannot create Navigator trust profile without a valid bundle SHA-256."
    }
    if ([string]::IsNullOrWhiteSpace([string]$Manifest.bundle_type) -or
        [string]::IsNullOrWhiteSpace([string]$Manifest.rotation_id) -or
        [string]::IsNullOrWhiteSpace([string]$Manifest.source)) {
        throw "Cannot create Navigator trust profile without bundle identity and source provenance."
    }
    return [ordered]@{
        schema_version = "guidexos.navigator.trust-profile.v1"
        profile_id = "navigator-$ProfileClass-$([string]$Manifest.rotation_id)"
        profile_version = 1
        bundle_identity = "$([string]$Manifest.bundle_type):$([string]$Manifest.rotation_id)"
        bundle_sha256 = $hash
        source_provenance = [string]$Manifest.source
        source_checksum_sha256 = $hash
        source_date = $SourceDate
        trust_purpose = "tls-server-auth"
        supported_restrictions = @("chain-signatures", "validity-periods", "basic-constraints", "path-length", "leaf-server-auth-eku", "leaf-key-usage", "requested-hostname")
        unsupported_restrictions = @("name-constraints", "mozilla-trust-purpose-bits", "mozilla-domain-restrictions", "certificate-policy-tree", "root-distrust-after-policy")
        required_verifier_capabilities = @("strict-pem-accounting", "sha256-integrity-match", "hostname-verification", "server-auth-eku", "authenticated-time", "signed-approval-verification")
        trust_policy_version = 1
        clock_assurance = "authenticated-fresh-rollback-protected-required"
        revocation_policy = "owner-decision-pending-no-crl-ocsp-or-stapled-status-enforced"
        approval_state = "pending"
        authorization_evidence = "none"
        approval_authenticated = $false
        profile_class = $ProfileClass
    }
}

function Test-NavigatorTrustProfileMetadata {
    param([AllowNull()]$Profile, [Parameter(Mandatory = $true)][string]$ExpectedBundleSha256)
    if ($null -eq $Profile) { return [pscustomobject]@{ Valid = $false; Authorized = $false; Reason = "missing" } }
    if ([string]$Profile.schema_version -ne "guidexos.navigator.trust-profile.v1" -or [int]$Profile.profile_version -ne 1) {
        return [pscustomobject]@{ Valid = $false; Authorized = $false; Reason = "unsupported-version" }
    }
    if ([string]$Profile.bundle_sha256 -ne $ExpectedBundleSha256.ToLowerInvariant()) {
        return [pscustomobject]@{ Valid = $false; Authorized = $false; Reason = "bundle-hash-mismatch" }
    }
    foreach ($field in @("profile_id", "bundle_identity", "source_provenance", "source_date", "trust_purpose", "supported_restrictions", "unsupported_restrictions", "required_verifier_capabilities", "clock_assurance", "revocation_policy", "approval_state", "authorization_evidence", "profile_class")) {
        $hasField = if ($Profile -is [System.Collections.IDictionary]) { $Profile.Contains($field) } else { $null -ne $Profile.PSObject.Properties[$field] }
        if (-not $hasField) {
            return [pscustomobject]@{ Valid = $false; Authorized = $false; Reason = "missing-field:$field" }
        }
    }
    if ([string]$Profile.profile_class -eq "release-approved" -and
        ([string]$Profile.approval_state -ne "release-approved" -or [string]$Profile.authorization_evidence -eq "none")) {
        return [pscustomobject]@{ Valid = $false; Authorized = $false; Reason = "missing-approval-evidence" }
    }
    # This repository has no runtime signature verifier or independently rooted release key.
    return [pscustomobject]@{ Valid = $true; Authorized = $false; Reason = "approval-not-authenticated" }
}
