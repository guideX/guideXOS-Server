function Assert-NavigatorHttpsPolicyOverrideAllowed {
    param([AllowNull()][string]$Value)
    if ([string]::IsNullOrWhiteSpace($Value)) { return }
    $allowed = @("disabled", "local-smoke-only", "user-trust-dev-mode", "controlled-qualification", "public-pilot-candidate")
    if ($allowed -notcontains $Value.Trim().ToLowerInvariant()) {
        throw "GXOS_NAVIGATOR_HTTPS_POLICY '$Value' is not an activation authorization. Use disabled, local-smoke-only, user-trust-dev-mode, controlled-qualification, or public-pilot-candidate. ProductionValidated requires an authenticated release trust anchor, which this build does not provide."
    }
}

function Get-NavigatorTrustProfileFieldValue {
    param([AllowNull()]$Object, [Parameter(Mandatory = $true)][string]$Name)
    if ($null -eq $Object) { return $null }
    if ($Object -is [System.Collections.IDictionary]) {
        if ($Object.Contains($Name)) { return $Object[$Name] }
        return $null
    }
    $property = $Object.PSObject.Properties[$Name]
    if ($null -eq $property) { return $null }
    return $property.Value
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
        required_verifier_capabilities = if ($ProfileClass -eq "fixture-only") {
            @("strict-pem-accounting", "sha256-integrity-match", "hostname-verification", "server-auth-eku", "plausible-clock-fixture-only")
        } else {
            @("strict-pem-accounting", "sha256-integrity-match", "hostname-verification", "server-auth-eku", "authenticated-time", "signed-approval-verification")
        }
        trust_policy_version = 1
        clock_assurance = if ($ProfileClass -eq "fixture-only") {
            "plausible-allowed-controlled-fixtures-only"
        } else {
            "authenticated-fresh-rollback-protected-required"
        }
        revocation_policy = "owner-decision-pending-no-crl-ocsp-or-stapled-status-enforced"
        approval_state = "pending"
        authorization_evidence = "none"
        approval_authenticated = $false
        profile_class = $ProfileClass
        mozilla_trust_manifest_schema_version = Get-NavigatorTrustProfileFieldValue -Object $Manifest -Name "mozilla_trust_manifest_schema_version"
        mozilla_trust_manifest_sha256 = Get-NavigatorTrustProfileFieldValue -Object $Manifest -Name "mozilla_trust_manifest_sha256"
        mozilla_source_complete = Get-NavigatorTrustProfileFieldValue -Object $Manifest -Name "mozilla_source_complete"
        mozilla_technically_eligible_root_count = Get-NavigatorTrustProfileFieldValue -Object $Manifest -Name "mozilla_technically_eligible_root_count"
        mozilla_candidate_root_count = Get-NavigatorTrustProfileFieldValue -Object $Manifest -Name "mozilla_candidate_root_count"
        mozilla_production_approved_root_count = Get-NavigatorTrustProfileFieldValue -Object $Manifest -Name "mozilla_production_approved_root_count"
    }
}

function Test-NavigatorMozillaTrustManifestMetadata {
    param(
        [AllowNull()]$Manifest,
        [Parameter(Mandatory = $true)][string]$ExpectedBundleSha256,
        [Parameter(Mandatory = $true)][string]$ExpectedManifestSha256,
        [Parameter(Mandatory = $true)][string]$ActualManifestSha256
    )
    if ($null -eq $Manifest) { return [pscustomobject]@{ Valid = $false; Authorized = $false; Reason = "missing-mozilla-trust-metadata" } }
    if ([string](Get-NavigatorTrustProfileFieldValue $Manifest "schema_version") -ne "guidexos.navigator.mozilla-trust-manifest.v1") {
        return [pscustomobject]@{ Valid = $false; Authorized = $false; Reason = "unsupported-mozilla-metadata-version" }
    }
    foreach ($hash in @($ExpectedBundleSha256, $ExpectedManifestSha256, $ActualManifestSha256, [string](Get-NavigatorTrustProfileFieldValue $Manifest "source_lock_sha256"))) {
        if ($hash -notmatch '^[0-9a-fA-F]{64}$') {
            return [pscustomobject]@{ Valid = $false; Authorized = $false; Reason = "malformed-metadata-integrity-hash" }
        }
    }
    if (-not [string]::Equals($ExpectedManifestSha256, $ActualManifestSha256, [System.StringComparison]::OrdinalIgnoreCase)) {
        return [pscustomobject]@{ Valid = $false; Authorized = $false; Reason = "mozilla-metadata-hash-mismatch" }
    }
    if (-not [string]::Equals([string](Get-NavigatorTrustProfileFieldValue $Manifest "input_candidate_bundle_sha256"), $ExpectedBundleSha256, [System.StringComparison]::OrdinalIgnoreCase)) {
        return [pscustomobject]@{ Valid = $false; Authorized = $false; Reason = "mozilla-source-bundle-hash-mismatch" }
    }
    if ([bool](Get-NavigatorTrustProfileFieldValue $Manifest "external_policy_complete") -ne $true) {
        return [pscustomobject]@{ Valid = $false; Authorized = $false; Reason = "mozilla-external-policy-incomplete" }
    }
    if ([string](Get-NavigatorTrustProfileFieldValue $Manifest "fixture_root_contamination") -ne "none-detected") {
        return [pscustomobject]@{ Valid = $false; Authorized = $false; Reason = "fixture-root-contamination" }
    }
    $eligible = 0
    $candidateCount = 0
    $approved = -1
    if (-not [int]::TryParse([string](Get-NavigatorTrustProfileFieldValue $Manifest "technically_eligible_root_count"), [ref]$eligible) -or $eligible -le 0) {
        return [pscustomobject]@{ Valid = $false; Authorized = $false; Reason = "no-eligible-mozilla-roots" }
    }
    if (-not [int]::TryParse([string](Get-NavigatorTrustProfileFieldValue $Manifest "candidate_included_root_count"), [ref]$candidateCount) -or $candidateCount -ne $eligible) {
        return [pscustomobject]@{ Valid = $false; Authorized = $false; Reason = "candidate-inclusion-count-mismatch" }
    }
    if (-not [int]::TryParse([string](Get-NavigatorTrustProfileFieldValue $Manifest "production_approved_root_count"), [ref]$approved) -or $approved -ne 0) {
        return [pscustomobject]@{ Valid = $false; Authorized = $false; Reason = "candidate-approval-state-invalid" }
    }
    if ([bool](Get-NavigatorTrustProfileFieldValue $Manifest "public_https_enabled") -ne $false) {
        return [pscustomobject]@{ Valid = $false; Authorized = $false; Reason = "public-https-must-remain-disabled" }
    }
    $roots = @(Get-NavigatorTrustProfileFieldValue $Manifest "roots")
    if ($roots.Count -ne $candidateCount) {
        return [pscustomobject]@{ Valid = $false; Authorized = $false; Reason = "candidate-root-record-count-mismatch" }
    }
    foreach ($root in $roots) {
        $fingerprint = [string](Get-NavigatorTrustProfileFieldValue $root "certificate_sha256")
        if ($fingerprint -notmatch '^[0-9a-fA-F]{64}$' -or
            [string](Get-NavigatorTrustProfileFieldValue $root "technical_eligibility") -ne "eligible" -or
            -not [bool](Get-NavigatorTrustProfileFieldValue $root "identity_verified") -or
            [string](Get-NavigatorTrustProfileFieldValue $root "server_auth_trust_record") -ne "CKT_NSS_TRUSTED_DELEGATOR" -or
            [string](Get-NavigatorTrustProfileFieldValue $root "external_restrictions") -ne "complete" -or
            [string](Get-NavigatorTrustProfileFieldValue $root "domain_restrictions_status") -notin @("none", "enforced") -or
            [string](Get-NavigatorTrustProfileFieldValue $root "name_constraints_status") -notin @("none", "compatible") -or
            [string](Get-NavigatorTrustProfileFieldValue $root "certificate_policy_status") -notin @("not-required", "enforced") -or
            [string](Get-NavigatorTrustProfileFieldValue $root "path_validation_status") -ne "verified-by-mbedtls-4.1.0" -or
            [bool](Get-NavigatorTrustProfileFieldValue $root "candidate_included") -ne $true -or
            [bool](Get-NavigatorTrustProfileFieldValue $root "production_approved") -ne $false -or
            @(Get-NavigatorTrustProfileFieldValue $root "eligibility_rejection_reasons").Count -ne 0 -or
            @(Get-NavigatorTrustProfileFieldValue $root "unknown_nss_fields").Count -ne 0) {
            return [pscustomobject]@{ Valid = $false; Authorized = $false; Reason = "root-eligibility-record-invalid" }
        }
    }
    return [pscustomobject]@{ Valid = $true; Authorized = $false; Reason = "technically-eligible-not-production-approved" }
}

function Test-NavigatorTrustProfileMetadata {
    param(
        [AllowNull()]$Profile,
        [Parameter(Mandatory = $true)][string]$ExpectedBundleSha256,
        [AllowNull()]$MozillaTrustManifest,
        [AllowNull()][string]$ActualMozillaTrustManifestSha256
    )
    if ($null -eq $Profile) { return [pscustomobject]@{ Valid = $false; Authorized = $false; Reason = "missing" } }
    if ([string]$Profile.schema_version -ne "guidexos.navigator.trust-profile.v1" -or [int]$Profile.profile_version -ne 1) {
        return [pscustomobject]@{ Valid = $false; Authorized = $false; Reason = "unsupported-version" }
    }
    if ([string]$Profile.bundle_sha256 -ne $ExpectedBundleSha256.ToLowerInvariant()) {
        return [pscustomobject]@{ Valid = $false; Authorized = $false; Reason = "bundle-hash-mismatch" }
    }
    if ([string]$Profile.profile_class -eq "candidate") {
        if ($null -eq $MozillaTrustManifest -or [string]::IsNullOrWhiteSpace([string]$ActualMozillaTrustManifestSha256)) {
            return [pscustomobject]@{ Valid = $false; Authorized = $false; Reason = "missing-mozilla-trust-metadata" }
        }
        $profileMetadataSha = [string]$Profile.mozilla_trust_manifest_sha256
        if ($profileMetadataSha -notmatch '^[0-9a-fA-F]{64}$') {
            return [pscustomobject]@{ Valid = $false; Authorized = $false; Reason = "missing-mozilla-trust-metadata" }
        }
        $mozillaResult = Test-NavigatorMozillaTrustManifestMetadata -Manifest $MozillaTrustManifest `
            -ExpectedBundleSha256 $ExpectedBundleSha256 `
            -ExpectedManifestSha256 $profileMetadataSha `
            -ActualManifestSha256 $ActualMozillaTrustManifestSha256
        if (-not $mozillaResult.Valid) {
            return [pscustomobject]@{ Valid = $false; Authorized = $false; Reason = $mozillaResult.Reason }
        }
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
