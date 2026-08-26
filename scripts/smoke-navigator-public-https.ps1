param(
    [switch]$Build,
    [int]$TimeoutSeconds = 40,
    [string]$CandidateBundlePath,
    [string]$CandidateRotationId,
    [switch]$CandidateReviewed,
    [string]$CandidateMetadataPath,
    [string]$PromotionRecordPath,
    [string]$TargetUrl,
    [switch]$ReviewedTargetOverride
)

$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$LogDir = Join-Path $Root "logs"
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

. (Join-Path $Root "scripts\process_environment.ps1")
Normalize-ProcessEnvironment
. (Join-Path $Root "scripts\navigator_smoke_repo_hygiene.ps1")
. (Join-Path $Root "scripts\navigator-public-https-reviewed-targets.ps1")

$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$dedicatedSerialLog = Join-Path $LogDir "navigator-public-https-$stamp.serial.log"
$dedicatedSummaryLog = Join-Path $LogDir "navigator-public-https-$stamp.summary.log"
$dedicatedEvidenceLog = Join-Path $LogDir "navigator-public-https-$stamp.evidence.json"
$dedicatedTrustManifestLog = Join-Path $LogDir "navigator-public-https-$stamp.ca-bundle.manifest"
$dedicatedProofPackDir = Join-Path $LogDir "navigator-public-https-proof-pack-$stamp"
$kernelScenarioGroup = "PublicPilot"
$kernelScenarioName = "persistent_navigation_scheduler"
$kernelSmokeScript = Join-Path $Root "scripts\smoke-navigator-kernel.ps1"
$passAssertionScript = Join-Path $Root "scripts\assert-navigator-public-https-pass.ps1"
$evidenceExportScript = Join-Path $Root "scripts\export-navigator-public-https-evidence.ps1"
$caBundleValidationScript = Join-Path $Root "scripts\validate-navigator-ca-bundle.ps1"
$publicLocalBundlePath = Join-Path $Root "scripts\fixtures\public-roots\ca-bundle.pem.local"
$publicExampleBundlePath = Join-Path $Root "scripts\fixtures\public-roots\ca-bundle.pem.example"
$publicProbeDefaultTarget = Get-NavigatorPublicHttpsDefaultTarget
$publicProbeReviewedAllowlistName = Get-NavigatorPublicHttpsReviewedAllowlistName
$publicProbeBundleCapBytes = 512KB

function Set-ProcessEnvValue {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [AllowNull()][string]$Value
    )

    if ([string]::IsNullOrEmpty($Value)) {
        Remove-Item "Env:$Name" -ErrorAction SilentlyContinue
    } else {
        Set-Item "Env:$Name" $Value
    }
}

function Write-NavigatorPublicHttpsProofPack {
    param(
        [Parameter(Mandatory = $true)][string]$ProofPackDir,
        [Parameter(Mandatory = $true)][string]$FinalResult,
        [Parameter(Mandatory = $true)][int]$ExitCode,
        [Parameter(Mandatory = $true)][System.Collections.Specialized.OrderedDictionary]$Fields,
        [AllowNull()][string]$SummaryPath,
        [AllowNull()][string]$SerialPath,
        [AllowNull()][string]$EvidencePath,
        [AllowNull()][string]$CandidateMetadataPath,
        [AllowNull()][string]$ManifestPath,
        [AllowNull()][string]$PromotionRecordPath,
        [string[]]$Notes = @()
    )

    New-Item -ItemType Directory -Force -Path $ProofPackDir | Out-Null

    $proofSummary = [ordered]@{
        schema_version = "guidexos.navigator.public-https-proof-pack.v0.5"
        generated_utc = ([datetime]::UtcNow.ToString("o"))
        reviewed_allowlist_name = $publicProbeReviewedAllowlistName
        reviewed_allowlist_version = Get-NavigatorPublicHttpsReviewedAllowlistVersion
        target_url = [string]$Fields["target_url"]
        final_url = [string]$Fields["final_url"]
        final_result = $FinalResult
        exit_code = $ExitCode
        result_marker = Get-NavigatorPublicHttpsResultMarker -FinalResult $FinalResult -ExitCode $ExitCode
        dns_server = [string]$Fields["dns_server"]
        resolved_ip = [string]$Fields["dns_resolved_ip"]
        dns_result = [string]$Fields["dns_result"]
        tcp_result = [string]$Fields["tcp_result"]
        tls_result = [string]$Fields["tls_result"]
        tls_status = [string]$Fields["tls_status"]
        tls_backend = [string]$Fields["tls_backend"]
        evidence_lane = [string]$Fields["evidence_lane"]
        tls_suite_contract = [string]$Fields["tls_suite_contract"]
        tls_suite_contract_count = [string]$Fields["tls_suite_contract_count"]
        tls_suite_contract_real_count = [string]$Fields["tls_suite_contract_real_count"]
        tls_suite_contract_installed = [string]$Fields["tls_suite_contract_installed"]
        tls_clienthello_real_suite_count = [string]$Fields["tls_clienthello_real_suite_count"]
        tls_clienthello_scsv_only = [string]$Fields["tls_clienthello_scsv_only"]
        tls_clienthello_contract_match = [string]$Fields["tls_clienthello_contract_match"]
        tls_negotiated_suite = [string]$Fields["tls_negotiated_suite"]
        tls_version = [string]$Fields["tls_protocol"]
        certificate_validation_result = [string]$Fields["certificate_validation_result"]
        hostname_validation_result = [string]$Fields["hostname_validation_result"]
        verify_flags = [string]$Fields["verify_flags"]
        http_status = [string]$Fields["http_status"]
        content_type = [string]$Fields["content_type"]
        content_encoding = [string]$Fields["content_encoding"]
        request_accept_encoding = [string]$Fields["request_accept_encoding"]
        body_bytes = [string]$Fields["body_bytes"]
        encoded_body_bytes = [string]$Fields["encoded_body_bytes"]
        decoded_body_bytes = [string]$Fields["decoded_body_bytes"]
        decoded_body_cap = [string]$Fields["decoded_body_cap"]
        decoded_cap_headroom = [string]$Fields["decoded_cap_headroom"]
        page_render_result = [string]$Fields["page_render_result"]
        plaintext_fallback = [string]$Fields["plaintext_fallback"]
        public_https_opt_in = [string]$Fields["public_proof_lane_active"]
        policy_enabled = [string]$Fields["policy_enabled"]
        public_pilot_token_present = [string]$Fields["public_pilot_token_present"]
        reviewed_target_policy = [string]$Fields["reviewed_target_policy"]
        reviewed_target_match = [string]$Fields["reviewed_target_match"]
        trust_bundle_manifest_present = [string]$Fields["trust_bundle_manifest_present"]
        trust_bundle_manifest_hash_match = [string]$Fields["runtime_manifest_hash_match"]
        trust_bundle_type = [string]$Fields["trust_bundle_type"]
        trust_bundle_production_ready = [string]$Fields["trust_bundle_production_ready"]
        trust_bundle_test_only = [string]$Fields["trust_bundle_test_only"]
        summary_log = $(if ($SummaryPath) { [System.IO.Path]::GetFullPath($SummaryPath) } else { $null })
        serial_log = $(if ($SerialPath) { [System.IO.Path]::GetFullPath($SerialPath) } else { $null })
        evidence_json = $(if ($EvidencePath) { [System.IO.Path]::GetFullPath($EvidencePath) } else { $null })
        candidate_metadata = $(if ($CandidateMetadataPath) { [System.IO.Path]::GetFullPath($CandidateMetadataPath) } else { $null })
        ca_bundle_manifest = $(if ($ManifestPath) { [System.IO.Path]::GetFullPath($ManifestPath) } else { $null })
        promotion_record = $(if ($PromotionRecordPath) { [System.IO.Path]::GetFullPath($PromotionRecordPath) } else { $null })
        notes = @($Notes)
    }

    $proofSummaryPath = Join-Path $ProofPackDir "proof-pack-summary.json"
    [System.IO.File]::WriteAllText($proofSummaryPath, ($proofSummary | ConvertTo-Json -Depth 6) + [Environment]::NewLine, [System.Text.Encoding]::ASCII)

    if ($SummaryPath -and (Test-Path -LiteralPath $SummaryPath -PathType Leaf)) {
        Copy-Item -LiteralPath $SummaryPath -Destination (Join-Path $ProofPackDir (Split-Path -Leaf $SummaryPath)) -Force
    }
    if ($SerialPath -and (Test-Path -LiteralPath $SerialPath -PathType Leaf)) {
        Copy-Item -LiteralPath $SerialPath -Destination (Join-Path $ProofPackDir (Split-Path -Leaf $SerialPath)) -Force
    }
    if ($EvidencePath -and (Test-Path -LiteralPath $EvidencePath -PathType Leaf)) {
        Copy-Item -LiteralPath $EvidencePath -Destination (Join-Path $ProofPackDir (Split-Path -Leaf $EvidencePath)) -Force
    }
    if ($CandidateMetadataPath -and (Test-Path -LiteralPath $CandidateMetadataPath -PathType Leaf)) {
        Copy-Item -LiteralPath $CandidateMetadataPath -Destination (Join-Path $ProofPackDir (Split-Path -Leaf $CandidateMetadataPath)) -Force
    }
    if ($ManifestPath -and (Test-Path -LiteralPath $ManifestPath -PathType Leaf)) {
        Copy-Item -LiteralPath $ManifestPath -Destination (Join-Path $ProofPackDir (Split-Path -Leaf $ManifestPath)) -Force
    }
    if ($PromotionRecordPath -and (Test-Path -LiteralPath $PromotionRecordPath -PathType Leaf)) {
        Copy-Item -LiteralPath $PromotionRecordPath -Destination (Join-Path $ProofPackDir (Split-Path -Leaf $PromotionRecordPath)) -Force
    }

    return $proofSummaryPath
}

$publicSmokeEnvNames = @(
    "GXOS_NAVIGATOR_SMOKE_ENABLE_REAL_PUBLIC_HTTPS",
    "GXOS_NAVIGATOR_SMOKE_REQUIRE_REAL_PUBLIC_HTTPS",
    "GXOS_NAVIGATOR_SMOKE_REAL_PUBLIC_HTTPS_URL",
    "GXOS_NAVIGATOR_SMOKE_REAL_PUBLIC_HTTPS_TARGET",
    "GXOS_NAVIGATOR_SMOKE_REAL_PUBLIC_HTTPS_CA_BUNDLE_SOURCE",
    "GXOS_NAVIGATOR_SMOKE_REAL_PUBLIC_HTTPS_REVIEWED_OVERRIDE",
    "GXOS_NAVIGATOR_HTTPS_POLICY"
)
$publicSmokeEnvOriginal = @{}
foreach ($envName in $publicSmokeEnvNames) {
    $publicSmokeEnvOriginal[$envName] = [Environment]::GetEnvironmentVariable($envName, "Process")
}

function Restore-NavigatorPublicHttpsEnvironment {
    foreach ($envName in $publicSmokeEnvNames) {
        Set-ProcessEnvValue -Name $envName -Value $publicSmokeEnvOriginal[$envName]
    }
}

function Resolve-StagedSourcePath {
    param([AllowNull()][string]$PathValue)

    if ([string]::IsNullOrWhiteSpace($PathValue)) {
        return $null
    }

    $candidate = $PathValue.Trim()
    if (-not [System.IO.Path]::IsPathRooted($candidate)) {
        $candidate = Join-Path $Root $candidate
    }
    return [System.IO.Path]::GetFullPath($candidate)
}

function Test-NavigatorTruthyToken {
    param([AllowNull()][string]$Value)

    if ([string]::IsNullOrWhiteSpace($Value)) {
        return $false
    }

    switch ($Value.Trim().ToLowerInvariant()) {
        "1" { return $true }
        "true" { return $true }
        "yes" { return $true }
        "enabled" { return $true }
        "required" { return $true }
        default { return $false }
    }
}

function Get-NavigatorRealPublicProbeTarget {
    if (-not [string]::IsNullOrWhiteSpace($TargetUrl)) {
        return $TargetUrl.Trim()
    }

    $urlValue = [Environment]::GetEnvironmentVariable("GXOS_NAVIGATOR_SMOKE_REAL_PUBLIC_HTTPS_URL", "Process")
    if (-not [string]::IsNullOrWhiteSpace($urlValue)) {
        return $urlValue.Trim()
    }

    $targetValue = [Environment]::GetEnvironmentVariable("GXOS_NAVIGATOR_SMOKE_REAL_PUBLIC_HTTPS_TARGET", "Process")
    if (-not [string]::IsNullOrWhiteSpace($targetValue)) {
        return $targetValue.Trim()
    }

    return $publicProbeDefaultTarget
}

function Get-NavigatorRealPublicProbeReviewedOverrideEnabled {
    if ($ReviewedTargetOverride) {
        return $true
    }

    return Test-NavigatorTruthyToken ([Environment]::GetEnvironmentVariable(
            "GXOS_NAVIGATOR_SMOKE_REAL_PUBLIC_HTTPS_REVIEWED_OVERRIDE",
            "Process"))
}

function Test-NavigatorRealPublicProbeTarget {
    param(
        [Parameter(Mandatory = $true)][string]$Target,
        [bool]$ReviewedOverrideEnabled = $false
    )

    try {
        $uri = [System.Uri]$Target
    } catch {
        return [pscustomobject]@{
            Valid = $false
            Error = "Target is not a valid absolute URL."
            Host = $null
            CanonicalTarget = $Target
            ReviewedTargetPolicy = "rejected"
            ReviewedTargetMatch = "no"
            ReviewedTargetOverride = $(if ($ReviewedOverrideEnabled) { "yes" } else { "no" })
            ReviewedTargetAllowlist = $publicProbeReviewedAllowlistName
            ReviewedTargetReason = "The requested target could not be parsed as an absolute HTTPS URL."
        }
    }

    if (-not $uri.IsAbsoluteUri) {
        return [pscustomobject]@{
            Valid = $false
            Error = "Target must be an absolute URL."
            Host = $null
            CanonicalTarget = $Target
            ReviewedTargetPolicy = "rejected"
            ReviewedTargetMatch = "no"
            ReviewedTargetOverride = $(if ($ReviewedOverrideEnabled) { "yes" } else { "no" })
            ReviewedTargetAllowlist = $publicProbeReviewedAllowlistName
            ReviewedTargetReason = "The requested target must be an absolute HTTPS URL."
        }
    }
    if (-not [string]::Equals($uri.Scheme, "https", [System.StringComparison]::OrdinalIgnoreCase)) {
        return [pscustomobject]@{
            Valid = $false
            Error = "Target must use https://."
            Host = $null
            CanonicalTarget = $uri.AbsoluteUri
            ReviewedTargetPolicy = "rejected"
            ReviewedTargetMatch = "no"
            ReviewedTargetOverride = $(if ($ReviewedOverrideEnabled) { "yes" } else { "no" })
            ReviewedTargetAllowlist = $publicProbeReviewedAllowlistName
            ReviewedTargetReason = "Only HTTPS targets are eligible for the reviewed public probe."
        }
    }
    if ([string]::IsNullOrWhiteSpace($uri.Host)) {
        return [pscustomobject]@{
            Valid = $false
            Error = "Target must include a DNS hostname."
            Host = $null
            CanonicalTarget = $uri.AbsoluteUri
            ReviewedTargetPolicy = "rejected"
            ReviewedTargetMatch = "no"
            ReviewedTargetOverride = $(if ($ReviewedOverrideEnabled) { "yes" } else { "no" })
            ReviewedTargetAllowlist = $publicProbeReviewedAllowlistName
            ReviewedTargetReason = "The reviewed public probe requires an HTTPS DNS hostname."
        }
    }

    $parsedIp = $null
    if ([System.Net.IPAddress]::TryParse($uri.Host, [ref]$parsedIp)) {
        return [pscustomobject]@{
            Valid = $false
            Error = "Target must use a DNS hostname, not a numeric IP literal."
            Host = $uri.Host
            CanonicalTarget = $uri.AbsoluteUri
            ReviewedTargetPolicy = "rejected"
            ReviewedTargetMatch = "no"
            ReviewedTargetOverride = $(if ($ReviewedOverrideEnabled) { "yes" } else { "no" })
            ReviewedTargetAllowlist = $publicProbeReviewedAllowlistName
            ReviewedTargetReason = "Numeric IP literals are never approved for the reviewed public HTTPS probe."
        }
    }

    $canonicalTarget = $uri.AbsoluteUri
    $reviewedTarget = Find-NavigatorPublicHttpsReviewedTarget -TargetUrl $canonicalTarget
    if ($null -ne $reviewedTarget) {
        return [pscustomobject]@{
            Valid = $true
            Error = $null
            Host = $uri.Host
            CanonicalTarget = $canonicalTarget
            ReviewedTargetPolicy = "reviewed-allowlist"
            ReviewedTargetMatch = "yes"
            ReviewedTargetOverride = $(if ($ReviewedOverrideEnabled) { "yes" } else { "no" })
            ReviewedTargetAllowlist = $publicProbeReviewedAllowlistName
            ReviewedTargetReason = [string]$reviewedTarget.Reason
        }
    }

    if ($ReviewedOverrideEnabled) {
        return [pscustomobject]@{
            Valid = $true
            Error = $null
            Host = $uri.Host
            CanonicalTarget = $canonicalTarget
            ReviewedTargetPolicy = "explicit-reviewed-override"
            ReviewedTargetMatch = "no"
            ReviewedTargetOverride = "yes"
            ReviewedTargetAllowlist = $publicProbeReviewedAllowlistName
            ReviewedTargetReason = "Accepted only because an explicit reviewed target override was requested for this one-off public proof run."
        }
    }

    $approvedTargets = [string]::Join(", ", (Get-NavigatorPublicHttpsReviewedTargetUrls))
    return [pscustomobject]@{
        Valid = $false
        Error = "Target must match the reviewed public HTTPS allowlist ($approvedTargets) unless an explicit reviewed override path is used."
        Host = $uri.Host
        CanonicalTarget = $canonicalTarget
        ReviewedTargetPolicy = "rejected"
        ReviewedTargetMatch = "no"
        ReviewedTargetOverride = "no"
        ReviewedTargetAllowlist = $publicProbeReviewedAllowlistName
        ReviewedTargetReason = "The requested target is outside the reviewed public HTTPS allowlist for v0.7."
    }
}

function Test-NavigatorPublicHttpsTrustSourceAllowed {
    param([AllowNull()][string]$Marker)

    switch ($Marker) {
        "env-var" { return $true }
        "env-var-preferred-over-local" { return $true }
        "local-fallback-file" { return $true }
        default { return $false }
    }
}

function Get-NavigatorPublicHttpsTrustBlocker {
    param([Parameter(Mandatory = $true)][System.Collections.IDictionary]$Fields)

    if ([string]::Equals([string]$Fields["public_trust_ready"], "yes", [System.StringComparison]::OrdinalIgnoreCase)) {
        return "(none)"
    }

    if ([string]::Equals([string]$Fields["reviewed_target_match"], "no", [System.StringComparison]::OrdinalIgnoreCase) -and
        -not [string]::Equals([string]$Fields["reviewed_target_override"], "yes", [System.StringComparison]::OrdinalIgnoreCase)) {
        return "reviewed target is not on the reviewed public HTTPS allowlist."
    }

    if (-not (Test-NavigatorPublicHttpsTrustSourceAllowed -Marker ([string]$Fields["public_ca_source_marker"]))) {
        return "public CA source marker is not an explicit public proof source."
    }

    if (-not [string]::Equals([string]$Fields["trust_bundle_manifest_present"], "yes", [System.StringComparison]::OrdinalIgnoreCase)) {
        return "public trust manifest is missing."
    }

    if (-not [string]::Equals([string]$Fields["trust_bundle_type"], "production-public-probe-merged", [System.StringComparison]::OrdinalIgnoreCase)) {
        return "public trust manifest is not the merged production public probe bundle."
    }

    if (-not [string]::Equals([string]$Fields["trust_bundle_production_ready"], "yes", [System.StringComparison]::OrdinalIgnoreCase)) {
        return "public trust manifest is not marked production_ready=yes."
    }

    if (-not [string]::Equals([string]$Fields["trust_bundle_test_only"], "no", [System.StringComparison]::OrdinalIgnoreCase)) {
        return "public trust manifest is marked test_only=yes."
    }

    if (-not [string]::Equals([string]$Fields["runtime_manifest_present"], "yes", [System.StringComparison]::OrdinalIgnoreCase)) {
        return "runtime trust manifest is missing."
    }

    if (-not [string]::Equals([string]$Fields["runtime_manifest_hash_match"], "yes", [System.StringComparison]::OrdinalIgnoreCase)) {
        return "runtime trust manifest hash does not match the staged bundle."
    }

    $parsedCerts = 0
    if (-not [int]::TryParse([string]$Fields["public_ca_parsed_certs"], [ref]$parsedCerts) -or $parsedCerts -le 0) {
        return "public CA bundle parsed zero certificates."
    }

    return "public trust readiness is blocked by an unrecognized policy/setup condition."
}

function Get-NavigatorRealPublicProbeCaBundleResolution {
    $envSource = Resolve-StagedSourcePath ([Environment]::GetEnvironmentVariable("GXOS_NAVIGATOR_SMOKE_REAL_PUBLIC_HTTPS_CA_BUNDLE_SOURCE", "Process"))
    $localSource = [System.IO.Path]::GetFullPath($publicLocalBundlePath)
    $localExists = Test-Path -LiteralPath $localSource -PathType Leaf

    if ($envSource) {
        return [pscustomobject]@{
            SourcePath = $envSource
            Resolution = $(if ($localExists) { "env-var-preferred-over-local" } else { "env-var" })
            LocalBundleAvailable = $localExists
            EnvBundleProvided = $true
        }
    }
    if ($localExists) {
        return [pscustomobject]@{
            SourcePath = $localSource
            Resolution = "local-fallback-file"
            LocalBundleAvailable = $true
            EnvBundleProvided = $false
        }
    }
    return [pscustomobject]@{
        SourcePath = $null
        Resolution = "missing"
        LocalBundleAvailable = $false
        EnvBundleProvided = $false
    }
}

function Get-NavigatorPemBundleInfo {
    param(
        [Parameter(Mandatory = $true)][string]$LiteralPath,
        [Parameter(Mandatory = $true)][string]$Label
    )

    if (-not (Test-Path -LiteralPath $LiteralPath -PathType Leaf)) {
        throw "$Label source not found: $LiteralPath"
    }

    $item = Get-Item -LiteralPath $LiteralPath
    if ($item.Length -le 0) {
        throw "$Label is empty: $LiteralPath"
    }
    if ($item.Length -gt $publicProbeBundleCapBytes) {
        throw "$Label exceeds the 512 KiB safety cap: $LiteralPath"
    }

    $text = [System.IO.File]::ReadAllText($item.FullName, [System.Text.Encoding]::ASCII)
    $matches = [regex]::Matches(
        $text,
        '-----BEGIN CERTIFICATE-----(?<body>[\s\S]*?)-----END CERTIFICATE-----',
        [System.Text.RegularExpressions.RegexOptions]::Singleline)
    if ($matches.Count -le 0) {
        throw "$Label does not contain any PEM certificates: $LiteralPath"
    }

    $parsedCount = 0
    foreach ($match in $matches) {
        $base64 = ($match.Groups["body"].Value -replace '\s', '')
        if ([string]::IsNullOrWhiteSpace($base64)) {
            throw "$Label contains an empty PEM certificate block: $LiteralPath"
        }
        try {
            $bytes = [Convert]::FromBase64String($base64)
            $cert = [System.Security.Cryptography.X509Certificates.X509Certificate2]::new($bytes)
            $parsedCount++
            $cert.Dispose()
        } catch {
            throw "$Label contains a malformed PEM certificate that the smoke harness refused to stage: $LiteralPath"
        }
    }

    return [pscustomobject]@{
        Path = $item.FullName
        Bytes = [int64]$item.Length
        ParsedCertCount = [int]$parsedCount
    }
}

function Get-NavigatorPublicProbeValue {
    param(
        [Parameter(Mandatory = $true)][string]$Output,
        [Parameter(Mandatory = $true)][string]$Name
    )

    # Parse line-by-line so large serial captures still resolve the latest
    # real_public_probe field deterministically without relying on a broad regex scan.
    $prefix = "[NAVIGATOR-SMOKE] https.case.real_public_probe.$Name="
    $latestValue = $null
    foreach ($line in ($Output -split "`r?`n")) {
        if ($line.StartsWith($prefix, [System.StringComparison]::Ordinal)) {
            $latestValue = $line.Substring($prefix.Length).Trim()
        }
    }
    return $latestValue
}

function Invoke-NavigatorPublicHttpsPassAssertion {
    param([Parameter(Mandatory = $true)][string]$SummaryPath)

    if (-not (Test-Path -LiteralPath $passAssertionScript -PathType Leaf)) {
        throw "Navigator public HTTPS PASS assertion helper not found: $passAssertionScript"
    }

    $output = @(& powershell -NoProfile -ExecutionPolicy Bypass -File $passAssertionScript -SummaryPath $SummaryPath 2>&1)
    $exitCode = $LASTEXITCODE

    return [pscustomobject]@{
        ExitCode = $exitCode
        Output = @($output | ForEach-Object { "$_" })
    }
}

function Invoke-NavigatorPublicHttpsEvidenceExport {
    param(
        [Parameter(Mandatory = $true)][string]$SummaryPath,
        [Parameter(Mandatory = $true)][string]$OutputPath
    )

    if (-not (Test-Path -LiteralPath $evidenceExportScript -PathType Leaf)) {
        return [pscustomobject]@{
            ExitCode = 1
            Output = @("Navigator public HTTPS evidence export helper not found: $evidenceExportScript")
            EvidencePath = $OutputPath
        }
    }

    $output = @(& powershell -NoProfile -ExecutionPolicy Bypass -File $evidenceExportScript -SummaryPath $SummaryPath -OutputPath $OutputPath 2>&1)
    $exitCode = $LASTEXITCODE

    return [pscustomobject]@{
        ExitCode = $exitCode
        Output = @($output | ForEach-Object { "$_" })
        EvidencePath = $OutputPath
    }
}

function Invoke-NavigatorCaBundleValidation {
    param(
        [Parameter(Mandatory = $true)][string]$BundlePath,
        [Parameter(Mandatory = $true)][string]$BundleType,
        [Parameter(Mandatory = $true)][string]$OutputManifestPath,
        [Parameter(Mandatory = $true)][string]$SourceDescription
    )

    if (-not (Test-Path -LiteralPath $caBundleValidationScript -PathType Leaf)) {
        return [pscustomobject]@{
            ExitCode = 1
            Output = @("Navigator CA bundle validation helper not found: $caBundleValidationScript")
            ManifestPath = $OutputManifestPath
            Manifest = $null
        }
    }

    $output = @(& powershell -NoProfile -ExecutionPolicy Bypass -File $caBundleValidationScript `
        -BundlePath $BundlePath `
        -BundleType $BundleType `
        -OutputManifestPath $OutputManifestPath `
        -SourceDescription $SourceDescription 2>&1)
    $exitCode = $LASTEXITCODE
    $manifest = $null
    if ($exitCode -eq 0 -and (Test-Path -LiteralPath $OutputManifestPath -PathType Leaf)) {
        $manifest = Get-Content -LiteralPath $OutputManifestPath -Raw | ConvertFrom-Json
    }

    return [pscustomobject]@{
        ExitCode = $exitCode
        Output = @($output | ForEach-Object { "$_" })
        ManifestPath = $OutputManifestPath
        Manifest = $manifest
    }
}

function Find-NavigatorKernelScenarioSerialLog {
    param(
        [Parameter(Mandatory = $true)][hashtable]$ExistingLogs,
        [Parameter(Mandatory = $true)][datetime]$StartedAtUtc
    )

    $candidates = @(Get-ChildItem -LiteralPath $LogDir -Filter "navigator-kernel-smoke-*-$kernelScenarioName.serial.log" -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTimeUtc -Descending)
    if ($candidates.Count -le 0) {
        return $null
    }

    foreach ($candidate in $candidates) {
        if (-not $ExistingLogs.ContainsKey($candidate.FullName)) {
            return $candidate
        }
    }
    foreach ($candidate in $candidates) {
        if ($candidate.LastWriteTimeUtc -ge $StartedAtUtc.AddSeconds(-2)) {
            return $candidate
        }
    }
    return $candidates[0]
}

function Write-NavigatorPublicHttpsLogs {
    param(
        [Parameter(Mandatory = $true)][string]$SerialPath,
        [Parameter(Mandatory = $true)][string]$SummaryPath,
        [Parameter(Mandatory = $true)][string]$FinalResult,
        [Parameter(Mandatory = $true)][int]$ExitCode,
        [AllowNull()][string]$KernelSerialPath,
        [AllowNull()][string]$KernelSerialOutput,
        [Parameter(Mandatory = $true)][System.Collections.Specialized.OrderedDictionary]$Fields,
        [string[]]$Notes = @()
    )

    Set-NavigatorPublicHttpsDerivedClassification -FinalResult $FinalResult -ExitCode $ExitCode -Fields $Fields
    $resultMarker = Get-NavigatorPublicHttpsResultMarker -FinalResult $FinalResult -ExitCode $ExitCode

    if (-not (Test-Path -LiteralPath $SerialPath)) {
        $serialLines = @(
            "[NAVIGATOR-PUBLIC-HTTPS] final_result=$FinalResult",
            "[NAVIGATOR-PUBLIC-HTTPS] exit_code=$ExitCode",
            "[NAVIGATOR-PUBLIC-HTTPS] result_marker=$resultMarker"
        )
        foreach ($fieldKey in $Fields.Keys) {
            $serialLines += "[NAVIGATOR-PUBLIC-HTTPS] $fieldKey=$($Fields[$fieldKey])"
        }
        foreach ($note in $Notes) {
            $serialLines += "[NAVIGATOR-PUBLIC-HTTPS] note=$note"
        }
        [System.IO.File]::WriteAllLines($SerialPath, $serialLines, [System.Text.Encoding]::ASCII)
    }

    $summaryLines = @(
        "[NAVIGATOR-PUBLIC-HTTPS] final_result=$FinalResult",
        "[NAVIGATOR-PUBLIC-HTTPS] exit_code=$ExitCode",
        "[NAVIGATOR-PUBLIC-HTTPS] result_marker=$resultMarker",
        "[NAVIGATOR-PUBLIC-HTTPS] dedicated_serial_log=$SerialPath",
        "[NAVIGATOR-PUBLIC-HTTPS] dedicated_summary_log=$SummaryPath",
        "[NAVIGATOR-PUBLIC-HTTPS] kernel_serial_log=$(if ($KernelSerialPath) { $KernelSerialPath } else { '(none)' })"
    )
    foreach ($fieldKey in $Fields.Keys) {
        $summaryLines += "[NAVIGATOR-PUBLIC-HTTPS] $fieldKey=$($Fields[$fieldKey])"
    }
    foreach ($note in $Notes) {
        $summaryLines += "[NAVIGATOR-PUBLIC-HTTPS] note=$note"
    }

    if ($KernelSerialOutput) {
        $summaryLines += "[NAVIGATOR-PUBLIC-HTTPS] extracted_guest_probe_lines_begin"
        $probeLines = [regex]::Matches(
            $KernelSerialOutput,
            '(?m)^\[NAVIGATOR-SMOKE\] https\.case\.real_public_probe\..*$') | ForEach-Object { $_.Value }
        if ($probeLines.Count -gt 0) {
            $summaryLines += $probeLines
        } else {
            $summaryLines += "[NAVIGATOR-PUBLIC-HTTPS] extracted_guest_probe_lines=(none)"
        }
        $summaryLines += "[NAVIGATOR-PUBLIC-HTTPS] extracted_guest_probe_lines_end"
    }

    [System.IO.File]::WriteAllLines($SummaryPath, $summaryLines, [System.Text.Encoding]::ASCII)
}

function New-NavigatorPublicHttpsSetupNotes {
    param([Parameter(Mandatory = $true)][string]$Reason)

    $approvedTargets = [string]::Join(", ", (Get-NavigatorPublicHttpsReviewedTargetUrls))

    return @(
        $Reason,
        "Reviewed target allowlist ($publicProbeReviewedAllowlistName): $approvedTargets",
        "Provide a real public-root PEM bundle with one of these options:",
        "  1. Copy real roots to $publicLocalBundlePath",
        "  2. Set GXOS_NAVIGATOR_SMOKE_REAL_PUBLIC_HTTPS_CA_BUNDLE_SOURCE=C:\path\to\ca-bundle.pem",
        "The placeholder file remains instructions only: $publicExampleBundlePath",
        "Public HTTPS smoke stays opt-in and does not enable default public browsing."
    )
}

function Get-NavigatorPublicHttpsResultMarker {
    param(
        [Parameter(Mandatory = $true)][string]$FinalResult,
        [Parameter(Mandatory = $true)][int]$ExitCode
    )

    switch ($ExitCode) {
        0 { return "PASS" }
        2 { return "SETUP_BLOCKED" }
        3 { return "SKIP" }
        default {
            if ([string]::Equals($FinalResult, "PASS", [System.StringComparison]::OrdinalIgnoreCase)) {
                return "PASS"
            }
            if ([string]::Equals($FinalResult, "SKIP", [System.StringComparison]::OrdinalIgnoreCase)) {
                return "SKIP"
            }
            return "FAIL"
        }
    }
}

function Get-NavigatorFieldYesNo {
    param(
        [Parameter(Mandatory = $true)][System.Collections.IDictionary]$Fields,
        [Parameter(Mandatory = $true)][string]$Name
    )

    if (-not $Fields.Contains($Name)) {
        return $false
    }
    return [string]::Equals([string]$Fields[$Name], "yes", [System.StringComparison]::OrdinalIgnoreCase)
}

function Get-NavigatorFieldInt {
    param(
        [Parameter(Mandatory = $true)][System.Collections.IDictionary]$Fields,
        [Parameter(Mandatory = $true)][string]$Name
    )

    $parsed = 0
    if ($Fields.Contains($Name) -and [int]::TryParse([string]$Fields[$Name], [ref]$parsed)) {
        return $parsed
    }
    return $null
}

function Get-NavigatorPublicHttpsTlsFailureClassification {
    param(
        [Parameter(Mandatory = $true)][string]$ResultMarker,
        [Parameter(Mandatory = $true)][System.Collections.IDictionary]$Fields
    )

    switch ($ResultMarker) {
        "SETUP_BLOCKED" { return "POLICY_OR_SETUP_BLOCKED" }
        "SKIP" { return "ENVIRONMENT_UNAVAILABLE" }
    }

    $dnsResult = [string]$Fields["dns_result"]
    $tcpResult = [string]$Fields["tcp_result"]
    $tlsResult = [string]$Fields["tls_result"]
    $tlsStatus = [string]$Fields["tls_status"]
    $certificateValidationResult = [string]$Fields["certificate_validation_result"]

    if ([string]::Equals($dnsResult, "FAIL", [System.StringComparison]::OrdinalIgnoreCase)) {
        return "DNS_FAILURE"
    }
    if ([string]::Equals($tcpResult, "FAIL", [System.StringComparison]::OrdinalIgnoreCase)) {
        return "TCP_FAILURE"
    }
    if ([string]::Equals($tlsResult, "PASS", [System.StringComparison]::OrdinalIgnoreCase)) {
        return "NONE"
    }

    switch ($tlsStatus) {
        "PolicyBlocked" { return "POLICY_BLOCKED" }
        "RngUnavailable" { return "RNG_UNAVAILABLE" }
        "ClockUnavailable" { return "CLOCK_UNAVAILABLE" }
        "CaMissing" { return "CA_MISSING" }
        "CaParseFailed" { return "CA_PARSE_FAILED" }
        "TcpConnectFailed" { return "TCP_FAILURE" }
        "HostnameMismatch" { return "HOSTNAME_FAILURE" }
        "CertificateVerifyFailed" { return "CERTIFICATE_VERIFICATION_FAILURE" }
        "TlsWriteFailed" { return "TLS_WRITE_FAILURE" }
        "TlsReadFailed" { return "TLS_READ_FAILURE" }
        "ResponseTooLarge" { return "RESPONSE_CAP_HIT_AFTER_TLS" }
        "HandshakeFailed" { return "TLS_HANDSHAKE_FAILURE" }
    }

    if ([string]::Equals($certificateValidationResult, "FAIL", [System.StringComparison]::OrdinalIgnoreCase)) {
        return "CERTIFICATE_VERIFICATION_FAILURE"
    }
    if ([string]::Equals($tlsResult, "FAIL", [System.StringComparison]::OrdinalIgnoreCase)) {
        return "TLS_TRANSPORT_FAILURE"
    }

    return "UNKNOWN_FAILURE"
}

function Set-NavigatorPublicHttpsDerivedClassification {
    param(
        [Parameter(Mandatory = $true)][string]$FinalResult,
        [Parameter(Mandatory = $true)][int]$ExitCode,
        [Parameter(Mandatory = $true)][System.Collections.Specialized.OrderedDictionary]$Fields
    )

    $resultMarker = Get-NavigatorPublicHttpsResultMarker -FinalResult $FinalResult -ExitCode $ExitCode
    $tlsPass = [string]::Equals($Fields["tls_result"], "PASS", [System.StringComparison]::OrdinalIgnoreCase) -and
        [string]::Equals($Fields["certificate_validation_result"], "PASS", [System.StringComparison]::OrdinalIgnoreCase) -and
        [string]::Equals($Fields["hostname_validation_result"], "PASS", [System.StringComparison]::OrdinalIgnoreCase)
    $headerCapHit = Get-NavigatorFieldYesNo -Fields $Fields -Name "header_cap_hit"
    $bodyCapHit = Get-NavigatorFieldYesNo -Fields $Fields -Name "body_cap_hit"
    $downgradeBlocked = Get-NavigatorFieldYesNo -Fields $Fields -Name "downgrade_blocked"
    $tlsSucceededBeforeContentFailure = Get-NavigatorFieldYesNo -Fields $Fields -Name "tls_succeeded_before_content_failure"
    $unsupportedReason = [string]$Fields["unsupported_reason"]
    $contentEncoding = [string]$Fields["content_encoding"]
    $sourceType = [string]$Fields["source_type"]
    $publicTrustReady = [string]$Fields["public_trust_ready"]
    $publicTrustSourceAllowed = Test-NavigatorPublicHttpsTrustSourceAllowed -Marker ([string]$Fields["public_ca_source_marker"])
    $publicTrustManifestReady = [string]::Equals([string]$Fields["trust_bundle_manifest_present"], "yes", [System.StringComparison]::OrdinalIgnoreCase) -and
        [string]::Equals([string]$Fields["runtime_manifest_present"], "yes", [System.StringComparison]::OrdinalIgnoreCase) -and
        [string]::Equals([string]$Fields["runtime_manifest_hash_match"], "yes", [System.StringComparison]::OrdinalIgnoreCase)
    $publicTrustTestOnly = [string]$Fields["trust_bundle_test_only"]
    $publicTrustReason = if ([string]::Equals($publicTrustReady, "yes", [System.StringComparison]::OrdinalIgnoreCase)) {
        "Public trust readiness requirements were satisfied for the reviewed public proof lane."
    } else {
        Get-NavigatorPublicHttpsTrustBlocker -Fields $Fields
    }
    $publicTrustBlocker = if ([string]::Equals($publicTrustReady, "yes", [System.StringComparison]::OrdinalIgnoreCase)) { "(none)" } else { $publicTrustReason }
    $redirectCount = Get-NavigatorFieldInt -Fields $Fields -Name "redirect_count"
    $httpStatus = Get-NavigatorFieldInt -Fields $Fields -Name "http_status"
    $failureReason = [string]$Fields["failure_reason"]
    $skipReason = [string]$Fields["skip_reason"]
    $redirectPolicyBlocked = $false
    $failureClassification = Get-NavigatorPublicHttpsTlsFailureClassification -ResultMarker $resultMarker -Fields $Fields

    if (($failureReason -match "redirect" -or $skipReason -match "redirect") -and -not $downgradeBlocked) {
        $redirectPolicyBlocked = $true
    } elseif ($null -ne $redirectCount -and $redirectCount -ge 5 -and -not $downgradeBlocked -and -not $tlsPass) {
        $redirectPolicyBlocked = $true
    }

    $Fields["tls_failure_classification"] = $failureClassification
    $Fields["public_trust_source_allowed"] = $(if ($publicTrustSourceAllowed) { "yes" } else { "no" })
    $Fields["public_trust_manifest_ready"] = $(if ($publicTrustManifestReady) { "yes" } else { "no" })
    $Fields["public_trust_runtime_hash_match"] = [string]$Fields["runtime_manifest_hash_match"]
    $Fields["public_trust_test_only"] = [string]$publicTrustTestOnly
    $Fields["public_trust_source_marker"] = [string]$Fields["public_ca_source_marker"]
    $Fields["public_trust_lane"] = "dedicated-reviewed-public-proof"
    $Fields["public_trust_reason"] = $publicTrustReason
    $Fields["public_trust_blocker"] = $publicTrustBlocker

    switch ($resultMarker) {
        "SETUP_BLOCKED" {
            $Fields["tls_transport_proof_result"] = "POLICY_OR_SETUP_BLOCKED"
            $Fields["content_compatibility_result"] = "NOT_ATTEMPTED"
            $Fields["page_render_result"] = "NOT_ATTEMPTED_POLICY_OR_SETUP_BLOCKED"
            $Fields["real_world_compatibility_note"] = "The reviewed public HTTPS proof did not run because setup, policy, target review, or trust prerequisites blocked it before transport proof."
            if ([string]::Equals($Fields["public_trust_ready"], "no", [System.StringComparison]::OrdinalIgnoreCase)) {
                $Fields["public_trust_reason"] = $publicTrustBlocker
            }
            return
        }
        "SKIP" {
            $Fields["tls_transport_proof_result"] = "ENVIRONMENT_UNAVAILABLE"
            $Fields["content_compatibility_result"] = "NOT_ATTEMPTED"
            $Fields["page_render_result"] = "NOT_ATTEMPTED_ENVIRONMENT_BLOCKED"
            $Fields["real_world_compatibility_note"] = "The reviewed public HTTPS proof stayed opt-in but skipped because outbound environment prerequisites were unavailable for this run."
            return
        }
    }

    if ($tlsPass) {
        $Fields["tls_transport_proof_result"] = "PASS"

        if ($downgradeBlocked) {
            $Fields["content_compatibility_result"] = "DOWNGRADE_BLOCKED"
            $Fields["page_render_result"] = "NOT_RENDERED_DOWNGRADE_BLOCKED"
            $Fields["real_world_compatibility_note"] = "TLS transport succeeded, but Navigator blocked an HTTPS-to-HTTP downgrade redirect before rendering content."
        } elseif ($redirectPolicyBlocked) {
            $Fields["content_compatibility_result"] = "REDIRECT_POLICY_BLOCKED"
            $Fields["page_render_result"] = "NOT_RENDERED_REDIRECT_POLICY_BLOCKED"
            $Fields["real_world_compatibility_note"] = "TLS transport succeeded, but Navigator stopped at a redirect policy boundary instead of rendering the final page."
        } elseif ($headerCapHit -and $bodyCapHit) {
            $Fields["content_compatibility_result"] = "HEADER_AND_BODY_CAP_HIT_AFTER_TLS"
            $Fields["page_render_result"] = "NOT_RENDERED_HEADER_AND_BODY_CAP_HIT_AFTER_TLS"
            $Fields["real_world_compatibility_note"] = "TLS transport succeeded, but Navigator hit both header and body safety caps before full page rendering completed."
        } elseif ($headerCapHit) {
            $Fields["content_compatibility_result"] = "HEADER_CAP_HIT_AFTER_TLS"
            $Fields["page_render_result"] = "NOT_RENDERED_HEADER_CAP_HIT_AFTER_TLS"
            $Fields["real_world_compatibility_note"] = "TLS transport succeeded, but Navigator hit the response-header safety cap before full page rendering completed."
        } elseif ($bodyCapHit) {
            $Fields["content_compatibility_result"] = "BODY_CAP_HIT_AFTER_TLS"
            $Fields["page_render_result"] = "NOT_RENDERED_BODY_CAP_HIT_AFTER_TLS"
            $Fields["real_world_compatibility_note"] = "TLS transport succeeded, but Navigator hit the response-body safety cap before full page rendering completed."
        } elseif (-not [string]::IsNullOrWhiteSpace($unsupportedReason) -and $unsupportedReason -ne "(none)" -and $unsupportedReason -ne "(not-attempted)") {
            if (($unsupportedReason -match "ContentEncoding") -or
                ($contentEncoding -and $contentEncoding -ne "(none)" -and $contentEncoding -ne "(not-attempted)" -and $contentEncoding -ne "identity")) {
                $Fields["content_compatibility_result"] = "UNSUPPORTED_CONTENT_ENCODING_AFTER_TLS"
                $Fields["page_render_result"] = "NOT_RENDERED_UNSUPPORTED_CONTENT_ENCODING_AFTER_TLS"
                $Fields["real_world_compatibility_note"] = "TLS transport succeeded, but the page relied on unsupported content encoding after HTTPS validation completed."
            } elseif (($unsupportedReason -match "content type") -or
                ($unsupportedReason -match "download") -or
                ($sourceType -and $sourceType -eq "download")) {
                $Fields["content_compatibility_result"] = "UNSUPPORTED_CONTENT_TYPE_DOWNLOAD_AFTER_TLS"
                $Fields["page_render_result"] = "NOT_RENDERED_UNSUPPORTED_CONTENT_TYPE_DOWNLOAD_AFTER_TLS"
                $Fields["real_world_compatibility_note"] = "TLS transport succeeded, but the response was treated as an unsupported content type or download instead of a renderable page."
            } else {
                $Fields["content_compatibility_result"] = "UNSUPPORTED_CONTENT_AFTER_TLS"
                $Fields["page_render_result"] = "NOT_RENDERED_UNSUPPORTED_CONTENT_AFTER_TLS"
                $Fields["real_world_compatibility_note"] = "TLS transport succeeded, but Navigator does not yet support the returned content shape well enough to render the page."
            }
        } elseif ($null -ne $httpStatus -and $httpStatus -gt 0) {
            $Fields["content_compatibility_result"] = "PASS"
            $Fields["page_render_result"] = "RENDERED"
            $Fields["real_world_compatibility_note"] = "TLS transport succeeded and Navigator rendered a supported HTTPS response."
        } elseif ($tlsSucceededBeforeContentFailure) {
            $Fields["content_compatibility_result"] = "UNSUPPORTED_CONTENT_AFTER_TLS"
            $Fields["page_render_result"] = "NOT_RENDERED_UNSUPPORTED_CONTENT_AFTER_TLS"
            $Fields["real_world_compatibility_note"] = "TLS transport succeeded before Navigator stopped on a post-handshake content limitation."
        } else {
            $Fields["content_compatibility_result"] = "TLS_TRANSPORT_PROVED_NO_RENDER"
            $Fields["page_render_result"] = "NOT_RENDERED_AFTER_TLS_SUCCESS"
            $Fields["real_world_compatibility_note"] = "TLS transport succeeded, but the final page-render outcome was incomplete."
        }
        return
    }

    $Fields["tls_transport_proof_result"] = $failureClassification
    if ($downgradeBlocked) {
        $Fields["content_compatibility_result"] = "DOWNGRADE_BLOCKED"
        $Fields["page_render_result"] = "NOT_RENDERED_DOWNGRADE_BLOCKED"
        $Fields["real_world_compatibility_note"] = "Navigator blocked an HTTPS-to-HTTP downgrade and did not permit plaintext fallback."
    } elseif ($redirectPolicyBlocked) {
        $Fields["content_compatibility_result"] = "REDIRECT_POLICY_BLOCKED"
        $Fields["page_render_result"] = "NOT_RENDERED_REDIRECT_POLICY_BLOCKED"
        $Fields["real_world_compatibility_note"] = "The public HTTPS probe stopped at a redirect policy guard before successful page rendering."
    } else {
        $Fields["content_compatibility_result"] = "NOT_PROVED"
        $Fields["page_render_result"] = "NOT_RENDERED_$failureClassification"
        switch ($failureClassification) {
            "DNS_FAILURE" {
                $Fields["real_world_compatibility_note"] = "The real-world HTTPS proof failed during DNS resolution before Navigator could establish a validated TLS transport."
            }
            "TCP_FAILURE" {
                $Fields["real_world_compatibility_note"] = "The real-world HTTPS proof failed while opening the TCP transport for HTTPS."
            }
            "CERTIFICATE_VERIFICATION_FAILURE" {
                $Fields["real_world_compatibility_note"] = "Navigator reached the HTTPS endpoint, but certificate verification failed before proof could be accepted."
            }
            "HOSTNAME_FAILURE" {
                $Fields["real_world_compatibility_note"] = "Navigator reached the HTTPS endpoint, but hostname validation failed before proof could be accepted."
            }
            default {
                $Fields["real_world_compatibility_note"] = "The real-world HTTPS proof failed before Navigator established a fully validated TLS transport."
            }
        }
    }
}

function Write-NavigatorPublicHttpsConsoleSummary {
    param(
        [Parameter(Mandatory = $true)][string]$FinalResult,
        [Parameter(Mandatory = $true)][int]$ExitCode,
        [Parameter(Mandatory = $true)][System.Collections.Specialized.OrderedDictionary]$Fields,
        [string[]]$Notes = @()
    )

    Write-Host "Dedicated real public HTTPS smoke summary:"
    Write-Host "  target: $($Fields["target_url"])"
    Write-Host "  reviewed target policy: $($Fields["reviewed_target_policy"])"
    Write-Host "  reviewed target allowlist: $($Fields["reviewed_target_allowlist"])"
    Write-Host "  reviewed target match: $($Fields["reviewed_target_match"])"
    Write-Host "  reviewed override: $($Fields["reviewed_target_override"])"
    Write-Host "  reviewed target reason: $($Fields["reviewed_target_reason"])"
    Write-Host "  CA source: $($Fields["public_ca_source_path"]) [$($Fields["public_ca_resolution"])]"
    Write-Host "  CA source marker: $($Fields["public_ca_source_marker"])"
    Write-Host "  public trust ready: $($Fields["public_trust_ready"])"
    Write-Host "  CA bytes: $($Fields["public_ca_bytes"])"
    Write-Host "  parsed cert count: $($Fields["public_ca_parsed_certs"])"
    Write-Host "  trust manifest present: $($Fields["trust_bundle_manifest_present"])"
    Write-Host "  trust bundle type: $($Fields["trust_bundle_type"])"
    Write-Host "  trust bundle root count: $($Fields["trust_bundle_root_count"])"
    Write-Host "  trust bundle sha256: $($Fields["trust_bundle_sha256"])"
    Write-Host "  trust bundle production_ready: $($Fields["trust_bundle_production_ready"])"
    Write-Host "  trust bundle test_only: $($Fields["trust_bundle_test_only"])"
    Write-Host "  runtime manifest present: $($Fields["runtime_manifest_present"])"
    Write-Host "  runtime manifest hash match: $($Fields["runtime_manifest_hash_match"])"
    Write-Host "  runtime manifest bundle type: $($Fields["runtime_manifest_bundle_type"])"
    Write-Host "  runtime manifest rotation_id: $($Fields["runtime_manifest_rotation_id"])"
    Write-Host "  runtime manifest root count: $($Fields["runtime_manifest_root_count"])"
    Write-Host "  runtime manifest sha256: $($Fields["runtime_manifest_sha256"])"
    Write-Host "  DNS result: $($Fields["dns_result"])"
    Write-Host "  DNS server: $($Fields["dns_server"])"
    Write-Host "  resolved IP: $($Fields["dns_resolved_ip"])"
    Write-Host "  TCP result: $($Fields["tcp_result"])"
    Write-Host "  TLS result: $($Fields["tls_result"])"
    Write-Host "  TLS backend: $($Fields["tls_backend"])"
    Write-Host "  TLS version: $($Fields["tls_protocol"])"
    Write-Host "  TLS connect attempts: $($Fields["tls_connect_attempts"])"
    Write-Host "  TLS retry count: $($Fields["tls_retry_count"])"
    Write-Host "  TLS retry reason: $($Fields["tls_retry_reason"])"
    Write-Host "  TLS bytes written before retry: $($Fields["tls_bytes_written_before_retry"])"
    Write-Host "  TLS handshake error code: $($Fields["tls_handshake_error_code"])"
    Write-Host "  TLS transport error code: $($Fields["tls_transport_error_code"])"
    Write-Host "  TLS request bytes written: $($Fields["tls_request_bytes_written"])"
    Write-Host "  TLS response bytes read: $($Fields["tls_response_bytes_read"])"
    Write-Host "  redirect hop index: $($Fields["redirect_hop_index"])"
    Write-Host "  redirect hop URL: $($Fields["redirect_hop_url"])"
    Write-Host "  redirected HTTPS retry used: $($Fields["redirected_https_retry_used"])"
    Write-Host "  tcp_abort_used: $($Fields["tcp_abort_used"])"
    Write-Host "  certificate validation: $($Fields["certificate_validation_result"])"
    Write-Host "  hostname validation: $($Fields["hostname_validation_result"])"
    Write-Host "  HTTP status: $($Fields["http_status"])"
    Write-Host "  final URL: $($Fields["final_url"])"
    Write-Host "  redirect count: $($Fields["redirect_count"])"
    Write-Host "  header cap hit: $($Fields["header_cap_hit"])"
    Write-Host "  body cap hit: $($Fields["body_cap_hit"])"
    Write-Host "  downgrade blocked: $($Fields["downgrade_blocked"])"
    Write-Host "  TLS succeeded before content failure: $($Fields["tls_succeeded_before_content_failure"])"
    Write-Host "  unsupported content reason: $($Fields["unsupported_reason"])"
    Write-Host "  TLS failure classification: $($Fields["tls_failure_classification"])"
    Write-Host "  TLS transport proof: $($Fields["tls_transport_proof_result"])"
    Write-Host "  content compatibility: $($Fields["content_compatibility_result"])"
    Write-Host "  page render result: $($Fields["page_render_result"])"
    Write-Host "  compatibility note: $($Fields["real_world_compatibility_note"])"
    Write-Host "  plaintext_fallback: $($Fields["plaintext_fallback"])"
    if ($Fields.Contains("pass_contract_assertion_result")) {
        Write-Host "  pass contract assertion: $($Fields["pass_contract_assertion_result"])"
    }
    if ($Fields.Contains("skip_reason") -and -not [string]::IsNullOrWhiteSpace($Fields["skip_reason"]) -and $Fields["skip_reason"] -ne "(none)") {
        Write-Host "  skip reason: $($Fields["skip_reason"])"
    }
    if ($Fields.Contains("failure_reason") -and -not [string]::IsNullOrWhiteSpace($Fields["failure_reason"]) -and $Fields["failure_reason"] -ne "(none)") {
        Write-Host "  failure reason: $($Fields["failure_reason"])"
    }
    foreach ($note in $Notes) {
        Write-Host "  note: $note"
    }

    $finalLine = "  final result: $FinalResult (exit $ExitCode)"
    $resultMarker = Get-NavigatorPublicHttpsResultMarker -FinalResult $FinalResult -ExitCode $ExitCode
    Write-Host "  result marker: $resultMarker"
    switch ($FinalResult) {
        "PASS" { Write-Host $finalLine -ForegroundColor Green }
        "SKIP" { Write-Host $finalLine -ForegroundColor Yellow }
        default { Write-Host $finalLine -ForegroundColor Red }
    }
}

function Write-NavigatorPublicHttpsEvidenceReport {
    param([Parameter(Mandatory = $true)]$EvidenceResult)

    foreach ($evidenceLine in $EvidenceResult.Output) {
        Write-Host $evidenceLine
    }
    if ($EvidenceResult.ExitCode -eq 0) {
        Write-Host "Dedicated real public HTTPS evidence JSON: $($EvidenceResult.EvidencePath)"
    } else {
        Write-Host "Dedicated real public HTTPS evidence export failed for $($EvidenceResult.EvidencePath)" -ForegroundColor Yellow
    }
}

$exitCode = 1
$finalResult = "FAIL"
$kernelSerialPath = $null
$kernelSerialOutput = $null
$evidenceOutputPath = $null
$fields = [ordered]@{
    target_url = "(unknown)"
    target_host = "(unknown)"
    reviewed_allowlist_name = $publicProbeReviewedAllowlistName
    reviewed_allowlist_version = Get-NavigatorPublicHttpsReviewedAllowlistVersion
    reviewed_target_policy = "(unknown)"
    reviewed_target_allowlist = $publicProbeReviewedAllowlistName
    reviewed_target_match = "no"
    reviewed_target_override = "no"
    reviewed_target_reason = "(none)"
    policy_enabled = "no"
    public_pilot_token_present = "no"
    public_pilot_token_value = "(none)"
    public_proof_lane_active = "no"
    public_ca_resolution = "(unknown)"
    public_ca_source_marker = "(unknown)"
    public_trust_ready = "(unknown)"
    public_trust_source_allowed = "(unknown)"
    public_trust_manifest_ready = "(unknown)"
    public_trust_runtime_hash_match = "(unknown)"
    public_trust_test_only = "(unknown)"
    public_trust_source_marker = "(unknown)"
    public_trust_lane = "(unknown)"
    public_trust_reason = "(none)"
    public_trust_blocker = "(none)"
    public_ca_source_path = "(unknown)"
    public_ca_bytes = "(unknown)"
    public_ca_parsed_certs = "(unknown)"
    trust_bundle_manifest_present = "no"
    trust_bundle_sha256 = "(not-available)"
    trust_bundle_type = "(not-available)"
    trust_bundle_root_count = "(not-available)"
    trust_bundle_production_ready = "no"
    trust_bundle_test_only = "(not-available)"
    runtime_manifest_present = "no"
    runtime_manifest_hash_match = "no"
    runtime_manifest_bundle_type = "(not-available)"
    runtime_manifest_rotation_id = "(not-available)"
    runtime_manifest_production_ready = "no"
    runtime_manifest_test_only = "(not-available)"
    runtime_manifest_root_count = "(not-available)"
    runtime_manifest_sha256 = "(not-available)"
    dns_result = "not-attempted"
    dns_server = "(not-attempted)"
    dns_resolved_ip = "(not-attempted)"
    dns_query_id = "0"
    dns_source_port = "0"
    dns_destination_port = "0"
    dns_query_bytes = "0"
    dns_send_attempts = "0"
    dns_last_send_result = "(not-attempted)"
    dns_reply_bytes = "0"
    dns_reply_rcode = "0"
    dns_reply_answer_count = "0"
    dns_ipv4_rx_packets = "0"
    dns_ipv4_rx_errors = "0"
    dns_ipv4_checksum_errors = "0"
    dns_udp_rx_datagrams = "0"
    dns_udp_rx_errors = "0"
    dns_udp_checksum_errors = "0"
    dns_udp_no_port_errors = "0"
    tcp_result = "not-attempted"
    tls_result = "not-attempted"
    transport_selection = "(not-attempted)"
    transport_policy_reason = "(not-attempted)"
    tls_status = "(not-attempted)"
    tls_backend = "(not-attempted)"
    evidence_lane = "kernel_public_https"
    tls_suite_contract = "explicit_bounded"
    tls_suite_contract_count = "0"
    tls_suite_contract_real_count = "0"
    tls_suite_contract_installed = "no"
    tls_clienthello_real_suite_count = "0"
    tls_clienthello_scsv_only = "no"
    tls_clienthello_contract_match = "no"
    tls_negotiated_suite = "(not-attempted)"
    tls_protocol = "(not-attempted)"
    tls_connect_attempts = "0"
    tls_retry_count = "0"
    tls_retry_reason = "(none)"
    tls_bytes_written_before_retry = "0"
    tls_handshake_error_code = "(not-attempted)"
    tls_transport_error_code = "(not-attempted)"
    tls_request_bytes_written = "0"
    tls_response_bytes_read = "0"
    tls_failure_classification = "NOT_ATTEMPTED"
    tcp_abort_used = "no"
    redirected_https_retry_used = "no"
    redirect_hop_index = "0"
    redirect_hop_url = "(not-attempted)"
    certificate_validation_result = "not-attempted"
    hostname_validation_result = "not-attempted"
    verify_flags = "(not-attempted)"
    sni_host = "(not-attempted)"
    source_type = "(not-attempted)"
    http_status = "(not-attempted)"
    requested_url = "(not-attempted)"
    final_url = "(not-attempted)"
    redirect_count = "0"
    content_type = "(not-attempted)"
    content_encoding = "(not-attempted)"
    request_accept_encoding = "(not-attempted)"
    body_bytes = "0"
    encoded_body_bytes = "0"
    decoded_body_bytes = "0"
    decoded_body_cap = "262144"
    decoded_cap_headroom = "262144"
    header_cap_hit = "no"
    body_cap_hit = "no"
    downgrade_blocked = "no"
    tls_succeeded_before_content_failure = "no"
    unsupported_reason = "(not-attempted)"
    tls_transport_proof_result = "NOT_ATTEMPTED"
    content_compatibility_result = "NOT_ATTEMPTED"
    page_render_result = "NOT_ATTEMPTED"
    real_world_compatibility_note = "The reviewed public HTTPS proof has not run yet."
    plaintext_fallback = "no"
    pass_contract_assertion_result = "not-run"
    pass_contract_assertion_exit_code = "(not-run)"
    skip_reason = "(none)"
    failure_reason = "(none)"
    probe_result = "SKIP"
}

try {
    $target = Get-NavigatorRealPublicProbeTarget
    $reviewedOverrideEnabled = Get-NavigatorRealPublicProbeReviewedOverrideEnabled
    $targetValidation = Test-NavigatorRealPublicProbeTarget -Target $target -ReviewedOverrideEnabled:$reviewedOverrideEnabled
    $caResolution = Get-NavigatorRealPublicProbeCaBundleResolution

    if ($caResolution.EnvBundleProvided -and $caResolution.LocalBundleAvailable) {
        Write-Host "Using public CA bundle from GXOS_NAVIGATOR_SMOKE_REAL_PUBLIC_HTTPS_CA_BUNDLE_SOURCE and ignoring the local fallback file."
    }

    $fields["target_url"] = $(if ($targetValidation.CanonicalTarget) { $targetValidation.CanonicalTarget } else { $target })
    $fields["target_host"] = $(if ($targetValidation.Host) { $targetValidation.Host } else { "(none)" })
    $fields["reviewed_target_policy"] = $targetValidation.ReviewedTargetPolicy
    $fields["reviewed_target_allowlist"] = $targetValidation.ReviewedTargetAllowlist
    $fields["reviewed_target_match"] = $targetValidation.ReviewedTargetMatch
    $fields["reviewed_target_override"] = $targetValidation.ReviewedTargetOverride
    $fields["reviewed_target_reason"] = $targetValidation.ReviewedTargetReason
    $fields["public_ca_resolution"] = $caResolution.Resolution
    $fields["public_ca_source_marker"] = $caResolution.Resolution
    $fields["public_trust_ready"] = $(if ($caResolution.SourcePath) { "pending-guest-check" } else { "no" })
    $fields["public_ca_source_path"] = $(if ($caResolution.SourcePath) { $caResolution.SourcePath } else { "(none)" })
    $fields["public_ca_bytes"] = "(not-validated)"
    $fields["public_ca_parsed_certs"] = "(not-validated)"

    if (-not $targetValidation.Valid) {
        $finalResult = "SKIP"
        $exitCode = 2
        $notes = New-NavigatorPublicHttpsSetupNotes -Reason "Refused to start the dedicated real public HTTPS smoke: $($targetValidation.Error)"
        Write-Host $notes[0] -ForegroundColor Yellow
        Write-NavigatorPublicHttpsLogs `
            -SerialPath $dedicatedSerialLog `
            -SummaryPath $dedicatedSummaryLog `
            -FinalResult $finalResult `
            -ExitCode $exitCode `
            -KernelSerialPath $null `
            -KernelSerialOutput $null `
            -Fields $fields `
            -Notes $notes
        $evidenceResult = Invoke-NavigatorPublicHttpsEvidenceExport -SummaryPath $dedicatedSummaryLog -OutputPath $dedicatedEvidenceLog
        $evidenceOutputPath = $evidenceResult.EvidencePath
        Write-NavigatorPublicHttpsEvidenceReport -EvidenceResult $evidenceResult
        $proofPackSummaryPath = Write-NavigatorPublicHttpsProofPack `
            -ProofPackDir $dedicatedProofPackDir `
            -FinalResult $finalResult `
            -ExitCode $exitCode `
            -Fields $fields `
            -SummaryPath $dedicatedSummaryLog `
            -SerialPath $dedicatedSerialLog `
            -EvidencePath $dedicatedEvidenceLog `
            -CandidateMetadataPath $(if ($CandidateMetadataPath) { [System.IO.Path]::GetFullPath($CandidateMetadataPath) } else { $null }) `
            -ManifestPath $null `
            -PromotionRecordPath $(if ($PromotionRecordPath) { [System.IO.Path]::GetFullPath($PromotionRecordPath) } else { $null }) `
            -Notes @($notes + "Proof pack retains the blocked real-root state for review.")
        Write-Host "Dedicated real public HTTPS proof pack summary: $proofPackSummaryPath"
        Write-NavigatorPublicHttpsConsoleSummary -FinalResult $finalResult -ExitCode $exitCode -Fields $fields -Notes $notes
        exit $exitCode
    }

    if (-not $caResolution.SourcePath) {
        $finalResult = "SKIP"
        $exitCode = 2
        $notes = New-NavigatorPublicHttpsSetupNotes -Reason "Refused to start the dedicated real public HTTPS smoke: no explicit public-root CA bundle was found."
        Write-Host $notes[0] -ForegroundColor Yellow
        Write-NavigatorPublicHttpsLogs `
            -SerialPath $dedicatedSerialLog `
            -SummaryPath $dedicatedSummaryLog `
            -FinalResult $finalResult `
            -ExitCode $exitCode `
            -KernelSerialPath $null `
            -KernelSerialOutput $null `
            -Fields $fields `
            -Notes $notes
        $evidenceResult = Invoke-NavigatorPublicHttpsEvidenceExport -SummaryPath $dedicatedSummaryLog -OutputPath $dedicatedEvidenceLog
        $evidenceOutputPath = $evidenceResult.EvidencePath
        Write-NavigatorPublicHttpsEvidenceReport -EvidenceResult $evidenceResult
        $proofPackSummaryPath = Write-NavigatorPublicHttpsProofPack `
            -ProofPackDir $dedicatedProofPackDir `
            -FinalResult $finalResult `
            -ExitCode $exitCode `
            -Fields $fields `
            -SummaryPath $dedicatedSummaryLog `
            -SerialPath $dedicatedSerialLog `
            -EvidencePath $dedicatedEvidenceLog `
            -CandidateMetadataPath $(if ($CandidateMetadataPath) { [System.IO.Path]::GetFullPath($CandidateMetadataPath) } else { $null }) `
            -ManifestPath $null `
            -PromotionRecordPath $(if ($PromotionRecordPath) { [System.IO.Path]::GetFullPath($PromotionRecordPath) } else { $null }) `
            -Notes @($notes + "Proof pack retains the missing-root blocked state for review.")
        Write-Host "Dedicated real public HTTPS proof pack summary: $proofPackSummaryPath"
        Write-NavigatorPublicHttpsConsoleSummary -FinalResult $finalResult -ExitCode $exitCode -Fields $fields -Notes $notes
        exit $exitCode
    }

    try {
        $bundleInfo = Get-NavigatorPemBundleInfo -LiteralPath $caResolution.SourcePath -Label "Dedicated real public HTTPS smoke CA bundle"
    } catch {
        $finalResult = "SKIP"
        $exitCode = 2
        $notes = New-NavigatorPublicHttpsSetupNotes -Reason $_.Exception.Message
        Write-Host $notes[0] -ForegroundColor Yellow
        Write-NavigatorPublicHttpsLogs `
            -SerialPath $dedicatedSerialLog `
            -SummaryPath $dedicatedSummaryLog `
            -FinalResult $finalResult `
            -ExitCode $exitCode `
            -KernelSerialPath $null `
            -KernelSerialOutput $null `
            -Fields $fields `
            -Notes $notes
        $evidenceResult = Invoke-NavigatorPublicHttpsEvidenceExport -SummaryPath $dedicatedSummaryLog -OutputPath $dedicatedEvidenceLog
        $evidenceOutputPath = $evidenceResult.EvidencePath
        Write-NavigatorPublicHttpsEvidenceReport -EvidenceResult $evidenceResult
        $proofPackSummaryPath = Write-NavigatorPublicHttpsProofPack `
            -ProofPackDir $dedicatedProofPackDir `
            -FinalResult $finalResult `
            -ExitCode $exitCode `
            -Fields $fields `
            -SummaryPath $dedicatedSummaryLog `
            -SerialPath $dedicatedSerialLog `
            -EvidencePath $dedicatedEvidenceLog `
            -CandidateMetadataPath $(if ($CandidateMetadataPath) { [System.IO.Path]::GetFullPath($CandidateMetadataPath) } else { $null }) `
            -ManifestPath $(if ($manifestValidation.Manifest) { $dedicatedTrustManifestLog } else { $null }) `
            -PromotionRecordPath $(if ($PromotionRecordPath) { [System.IO.Path]::GetFullPath($PromotionRecordPath) } else { $null }) `
            -Notes @($notes + "Proof pack retains the setup-blocked state for review.")
        Write-Host "Dedicated real public HTTPS proof pack summary: $proofPackSummaryPath"
        Write-NavigatorPublicHttpsConsoleSummary -FinalResult $finalResult -ExitCode $exitCode -Fields $fields -Notes $notes
        exit $exitCode
    }
    $fields["public_ca_source_path"] = $bundleInfo.Path
    $fields["public_ca_bytes"] = $bundleInfo.Bytes
    $fields["public_ca_parsed_certs"] = $bundleInfo.ParsedCertCount

    $manifestValidation = Invoke-NavigatorCaBundleValidation `
        -BundlePath $bundleInfo.Path `
        -BundleType "production-public-source" `
        -OutputManifestPath $dedicatedTrustManifestLog `
        -SourceDescription "explicit-public-root-input"
    if ($manifestValidation.ExitCode -ne 0 -or $null -eq $manifestValidation.Manifest) {
        $finalResult = "SKIP"
        $exitCode = 2
        $notes = New-NavigatorPublicHttpsSetupNotes -Reason "Refused to start the dedicated real public HTTPS smoke: the public-root manifest contract could not be generated."
        foreach ($line in $manifestValidation.Output) {
            Write-Host $line -ForegroundColor Yellow
            $notes += "manifest: $line"
        }
        Write-NavigatorPublicHttpsLogs `
            -SerialPath $dedicatedSerialLog `
            -SummaryPath $dedicatedSummaryLog `
            -FinalResult $finalResult `
            -ExitCode $exitCode `
            -KernelSerialPath $null `
            -KernelSerialOutput $null `
            -Fields $fields `
            -Notes $notes
        $evidenceResult = Invoke-NavigatorPublicHttpsEvidenceExport -SummaryPath $dedicatedSummaryLog -OutputPath $dedicatedEvidenceLog
        $evidenceOutputPath = $evidenceResult.EvidencePath
        Write-NavigatorPublicHttpsEvidenceReport -EvidenceResult $evidenceResult
        Write-NavigatorPublicHttpsConsoleSummary -FinalResult $finalResult -ExitCode $exitCode -Fields $fields -Notes $notes
        exit $exitCode
    }
    foreach ($line in $manifestValidation.Output) {
        Write-Host $line
    }
    $fields["trust_bundle_manifest_present"] = "yes"
    $fields["trust_bundle_sha256"] = $manifestValidation.Manifest.sha256
    $fields["trust_bundle_type"] = $manifestValidation.Manifest.bundle_type
    $fields["trust_bundle_root_count"] = $manifestValidation.Manifest.root_count
    $fields["trust_bundle_production_ready"] = $manifestValidation.Manifest.production_ready
    $fields["trust_bundle_test_only"] = $manifestValidation.Manifest.test_only
    $fields["public_trust_source_allowed"] = $(if (Test-NavigatorPublicHttpsTrustSourceAllowed -Marker $caResolution.Resolution) { "yes" } else { "no" })
    $fields["public_trust_manifest_ready"] = $(if ([string]::Equals($manifestValidation.Manifest.production_ready, "yes", [System.StringComparison]::OrdinalIgnoreCase) -and [string]::Equals($manifestValidation.Manifest.test_only, "no", [System.StringComparison]::OrdinalIgnoreCase)) { "yes" } else { "no" })
    $fields["public_trust_runtime_hash_match"] = "(pending-kernel-check)"
    $fields["public_trust_test_only"] = $manifestValidation.Manifest.test_only
    $fields["public_trust_source_marker"] = $caResolution.Resolution
    $fields["public_trust_lane"] = "dedicated-reviewed-public-proof"
    $fields["public_trust_reason"] = "Public trust readiness is pending the kernel-side trust policy check."
    $fields["public_trust_blocker"] = $(if ($fields["public_trust_source_allowed"] -eq "no") { "public CA source marker is not an explicit public proof source." } elseif ($fields["public_trust_manifest_ready"] -eq "no") { "public trust manifest is not production-ready." } else { "(pending-kernel-check)" })

    Write-Host "Dedicated real public HTTPS smoke target: $($fields["target_url"])"
    Write-Host "Dedicated real public HTTPS smoke CA bundle: $($bundleInfo.Path) [$($caResolution.Resolution)]"

    Set-ProcessEnvValue -Name "GXOS_NAVIGATOR_SMOKE_ENABLE_REAL_PUBLIC_HTTPS" -Value "1"
    Set-ProcessEnvValue -Name "GXOS_NAVIGATOR_SMOKE_REQUIRE_REAL_PUBLIC_HTTPS" -Value "1"
    Set-ProcessEnvValue -Name "GXOS_NAVIGATOR_SMOKE_REAL_PUBLIC_HTTPS_URL" -Value $fields["target_url"]
    Set-ProcessEnvValue -Name "GXOS_NAVIGATOR_SMOKE_REAL_PUBLIC_HTTPS_TARGET" -Value $fields["target_url"]
    Set-ProcessEnvValue -Name "GXOS_NAVIGATOR_SMOKE_REAL_PUBLIC_HTTPS_CA_BUNDLE_SOURCE" -Value $bundleInfo.Path
    Set-ProcessEnvValue -Name "GXOS_NAVIGATOR_SMOKE_REAL_PUBLIC_HTTPS_REVIEWED_OVERRIDE" -Value $(if ($reviewedOverrideEnabled) { "1" } else { $null })
    Set-ProcessEnvValue -Name "GXOS_NAVIGATOR_HTTPS_POLICY" -Value "production-validated`npublic-https-pilot=enabled"

    $existingKernelSerialLogs = @{}
    Get-ChildItem -LiteralPath $LogDir -Filter "navigator-kernel-smoke-*-$kernelScenarioName.serial.log" -ErrorAction SilentlyContinue | ForEach-Object {
        $existingKernelSerialLogs[$_.FullName] = $true
    }

    $kernelStartUtc = [datetime]::UtcNow
    $kernelArgs = @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", $kernelSmokeScript,
        "-TimeoutSeconds", $TimeoutSeconds.ToString(),
        "-ScenarioGroup", $kernelScenarioGroup,
        "-ScenarioFilter", $kernelScenarioName
    )
    if ($Build) {
        $kernelArgs += "-Build"
    }
    if (-not [string]::IsNullOrWhiteSpace($CandidateBundlePath)) {
        $kernelArgs += @("-CandidateBundlePath", $CandidateBundlePath)
    }
    if (-not [string]::IsNullOrWhiteSpace($CandidateRotationId)) {
        $kernelArgs += @("-CandidateRotationId", $CandidateRotationId)
    }
    if ($CandidateReviewed) {
        $kernelArgs += "-CandidateReviewed"
    }

    & powershell @kernelArgs
    $kernelExitCode = $LASTEXITCODE

    $kernelSerialLog = Find-NavigatorKernelScenarioSerialLog -ExistingLogs $existingKernelSerialLogs -StartedAtUtc $kernelStartUtc
    if ($kernelSerialLog) {
        $kernelSerialPath = $kernelSerialLog.FullName
        Copy-Item -LiteralPath $kernelSerialPath -Destination $dedicatedSerialLog -Force
        $kernelSerialOutput = Get-Content -LiteralPath $kernelSerialPath -Raw
    } else {
        $kernelSerialOutput = ""
    }

    if ([string]::IsNullOrEmpty($kernelSerialOutput)) {
        $finalResult = "FAIL"
        $exitCode = 1
        $notes = @("The dedicated public HTTPS smoke could not locate a scenario serial log from smoke-navigator-kernel.ps1.")
        Write-NavigatorPublicHttpsLogs `
            -SerialPath $dedicatedSerialLog `
            -SummaryPath $dedicatedSummaryLog `
            -FinalResult $finalResult `
            -ExitCode $exitCode `
            -KernelSerialPath $kernelSerialPath `
            -KernelSerialOutput $null `
            -Fields $fields `
            -Notes $notes
        $evidenceResult = Invoke-NavigatorPublicHttpsEvidenceExport -SummaryPath $dedicatedSummaryLog -OutputPath $dedicatedEvidenceLog
        $evidenceOutputPath = $evidenceResult.EvidencePath
        Write-NavigatorPublicHttpsEvidenceReport -EvidenceResult $evidenceResult
        Write-NavigatorPublicHttpsConsoleSummary -FinalResult $finalResult -ExitCode $exitCode -Fields $fields -Notes $notes
        exit $exitCode
    }

    foreach ($fieldName in @(
        "reviewed_target_policy",
        "reviewed_target_allowlist",
        "reviewed_target_match",
        "reviewed_target_override",
        "reviewed_target_reason",
        "policy_enabled",
        "policy_blocker",
        "policy_config_path",
        "policy_config_source",
        "public_pilot_token_present",
        "public_pilot_token_path",
        "public_pilot_token_value",
        "public_proof_lane_active",
        "scenario_group",
        "scenario_name",
        "public_trust_ready",
        "public_ca_bundle_source",
        "public_ca_bytes",
        "public_ca_parsed_certs",
        "trust_bundle_manifest_present",
        "trust_bundle_sha256",
        "trust_bundle_type",
        "trust_bundle_root_count",
        "trust_bundle_production_ready",
        "trust_bundle_test_only",
        "runtime_manifest_present",
        "runtime_manifest_hash_match",
        "runtime_manifest_bundle_type",
        "runtime_manifest_rotation_id",
        "runtime_manifest_production_ready",
        "runtime_manifest_test_only",
        "runtime_manifest_root_count",
        "runtime_manifest_sha256",
        "dns_result",
        "dns_server",
        "dns_resolved_ip",
        "dns_query_id",
        "dns_source_port",
        "dns_destination_port",
        "dns_query_bytes",
        "dns_send_attempts",
        "dns_last_send_result",
        "dns_reply_bytes",
        "dns_reply_rcode",
        "dns_reply_answer_count",
        "dns_ipv4_rx_packets",
        "dns_ipv4_rx_errors",
        "dns_ipv4_checksum_errors",
        "dns_udp_rx_datagrams",
        "dns_udp_rx_errors",
        "dns_udp_checksum_errors",
        "dns_udp_no_port_errors",
        "tcp_result",
        "tls_result",
        "transport_selection",
        "transport_policy_reason",
        "tls_status",
        "tls_backend",
        "evidence_lane",
        "tls_suite_contract",
        "tls_suite_contract_count",
        "tls_suite_contract_real_count",
        "tls_suite_contract_installed",
        "tls_clienthello_real_suite_count",
        "tls_clienthello_scsv_only",
        "tls_clienthello_contract_match",
        "tls_negotiated_suite",
        "protocol",
        "tls_protocol",
        "tls_connect_attempts",
        "tls_retry_count",
        "tls_retry_reason",
        "tls_bytes_written_before_retry",
        "tls_handshake_error_code",
        "tls_transport_error_code",
        "tls_request_bytes_written",
        "tls_response_bytes_read",
        "tcp_abort_used",
        "redirected_https_retry_used",
        "redirect_hop_index",
        "redirect_hop_url",
        "certificate_validation_result",
        "hostname_validation_result",
        "verify_flags",
        "sni_host",
        "source_type",
        "requested_url",
        "final_url",
        "redirect_count",
        "http_status",
        "content_type",
        "content_encoding",
        "request_accept_encoding",
        "body_bytes",
        "encoded_body_bytes",
        "decoded_body_bytes",
        "decoded_body_cap",
        "decoded_cap_headroom",
        "header_cap_hit",
        "body_cap_hit",
        "downgrade_blocked",
        "tls_succeeded_before_content_failure",
        "unsupported_reason",
        "plaintext_fallback",
        "skip_reason",
        "error",
        "result"
    )) {
        $value = Get-NavigatorPublicProbeValue -Output $kernelSerialOutput -Name $fieldName
        if ($null -ne $value) {
            switch ($fieldName) {
                "public_ca_bundle_source" { $fields["public_ca_source_path"] = $value }
                "reviewed_target_policy" { $fields["reviewed_target_policy"] = $value }
                "reviewed_target_allowlist" { $fields["reviewed_target_allowlist"] = $value }
                "reviewed_target_match" { $fields["reviewed_target_match"] = $value }
                "reviewed_target_override" { $fields["reviewed_target_override"] = $value }
                "reviewed_target_reason" { $fields["reviewed_target_reason"] = $value }
                "policy_enabled" { $fields["policy_enabled"] = $value }
                "policy_blocker" { $fields["policy_blocker"] = $value }
                "policy_config_path" { $fields["policy_config_path"] = $value }
                "policy_config_source" { $fields["policy_config_source"] = $value }
                "public_pilot_token_present" { $fields["public_pilot_token_present"] = $value }
                "public_pilot_token_path" { $fields["public_pilot_token_path"] = $value }
                "public_pilot_token_value" { $fields["public_pilot_token_value"] = $value }
                "public_proof_lane_active" { $fields["public_proof_lane_active"] = $value }
                "protocol" { $fields["tls_protocol"] = $value }
                "scenario_group" { $fields["scenario_group"] = $value }
                "scenario_name" { $fields["scenario_name"] = $value }
                "error" { $fields["failure_reason"] = $value }
                "result" { $fields["probe_result"] = $value }
                default { $fields[$fieldName] = $value }
            }
        }
    }

    $parsedTarget = Get-NavigatorPublicProbeValue -Output $kernelSerialOutput -Name "target"
    if ($parsedTarget) {
        $fields["target_url"] = $parsedTarget
    }
    $parsedTargetHost = Get-NavigatorPublicProbeValue -Output $kernelSerialOutput -Name "dns_host"
    if ($parsedTargetHost) {
        $fields["target_host"] = $parsedTargetHost
    }

    $guestProbeResult = $fields["probe_result"]
    $notes = @("Kernel smoke exit code: $kernelExitCode")

    switch ($guestProbeResult) {
        "PASS" {
            if ($kernelExitCode -eq 0) {
                $finalResult = "PASS"
                $exitCode = 0
            } else {
                $finalResult = "FAIL"
                $exitCode = 1
                $notes += "Guest probe reported PASS but the kernel smoke wrapper exited nonzero."
            }
        }
        "SKIP" {
            $finalResult = "SKIP"
            $exitCode = 3
            $skipReason = $fields["skip_reason"]
            if ($skipReason) {
                $notes += "Guest skip reason: $skipReason"
            }
        }
        default {
            $finalResult = "FAIL"
            $exitCode = 1
            $failureReason = $fields["failure_reason"]
            if ($failureReason -and $failureReason -ne "(none)") {
                $notes += "Guest failure reason: $failureReason"
            }
        }
    }

    Write-NavigatorPublicHttpsLogs `
        -SerialPath $dedicatedSerialLog `
        -SummaryPath $dedicatedSummaryLog `
        -FinalResult $finalResult `
        -ExitCode $exitCode `
        -KernelSerialPath $kernelSerialPath `
        -KernelSerialOutput $kernelSerialOutput `
        -Fields $fields `
        -Notes $notes

    if ($finalResult -eq "PASS") {
        $assertionResult = Invoke-NavigatorPublicHttpsPassAssertion -SummaryPath $dedicatedSummaryLog
        $fields["pass_contract_assertion_result"] = $(if ($assertionResult.ExitCode -eq 0) { "PASS" } else { "FAIL" })
        $fields["pass_contract_assertion_exit_code"] = $assertionResult.ExitCode

        foreach ($assertionLine in $assertionResult.Output) {
            Write-Host $assertionLine
        }

        if ($assertionResult.ExitCode -ne 0) {
            $finalResult = "FAIL"
            $exitCode = 1
            $fields["failure_reason"] = "PASS artifact assertion failed"
            $notes += "PASS artifact assertion failed."
            foreach ($assertionLine in $assertionResult.Output) {
                $notes += "assertion: $assertionLine"
            }
        }

        Write-NavigatorPublicHttpsLogs `
            -SerialPath $dedicatedSerialLog `
            -SummaryPath $dedicatedSummaryLog `
            -FinalResult $finalResult `
            -ExitCode $exitCode `
            -KernelSerialPath $kernelSerialPath `
            -KernelSerialOutput $kernelSerialOutput `
            -Fields $fields `
            -Notes $notes
    }

    $evidenceResult = Invoke-NavigatorPublicHttpsEvidenceExport -SummaryPath $dedicatedSummaryLog -OutputPath $dedicatedEvidenceLog
    $evidenceOutputPath = $evidenceResult.EvidencePath
    Write-NavigatorPublicHttpsEvidenceReport -EvidenceResult $evidenceResult

    $proofPackNotes = @($notes)
    $proofPackNotes += "Proof pack keeps the dedicated public HTTPS lane separate from deterministic smoke."
    $proofPackNotes += "Default public HTTPS browsing remains off."
    $proofPackManifestPath = $(if ($fields.Contains("trust_bundle_manifest_present") -and $fields["trust_bundle_manifest_present"] -eq "yes") { $dedicatedTrustManifestLog } else { $null })
    $proofPackSummaryPath = Write-NavigatorPublicHttpsProofPack `
        -ProofPackDir $dedicatedProofPackDir `
        -FinalResult $finalResult `
        -ExitCode $exitCode `
        -Fields $fields `
        -SummaryPath $dedicatedSummaryLog `
        -SerialPath $dedicatedSerialLog `
        -EvidencePath $dedicatedEvidenceLog `
        -CandidateMetadataPath $(if ($CandidateMetadataPath) { [System.IO.Path]::GetFullPath($CandidateMetadataPath) } else { $null }) `
        -ManifestPath $proofPackManifestPath `
        -PromotionRecordPath $(if ($PromotionRecordPath) { [System.IO.Path]::GetFullPath($PromotionRecordPath) } else { $null }) `
        -Notes $proofPackNotes

    Write-Host "Dedicated real public HTTPS proof pack summary: $proofPackSummaryPath"

    Write-NavigatorPublicHttpsConsoleSummary -FinalResult $finalResult -ExitCode $exitCode -Fields $fields -Notes $notes

    if ($finalResult -eq "PASS") {
        Write-Host "Dedicated real public HTTPS smoke PASS. Serial log: $dedicatedSerialLog"
    } elseif ($finalResult -eq "SKIP") {
        Write-Host "Dedicated real public HTTPS smoke SKIP. Summary log: $dedicatedSummaryLog" -ForegroundColor Yellow
    } else {
        Write-Host "Dedicated real public HTTPS smoke FAIL. Summary log: $dedicatedSummaryLog" -ForegroundColor Red
    }

    exit $exitCode
} catch {
    $finalResult = "FAIL"
    $exitCode = 1
    $notes = @($_.Exception.Message)
    Write-NavigatorPublicHttpsLogs `
        -SerialPath $dedicatedSerialLog `
        -SummaryPath $dedicatedSummaryLog `
        -FinalResult $finalResult `
        -ExitCode $exitCode `
        -KernelSerialPath $kernelSerialPath `
        -KernelSerialOutput $kernelSerialOutput `
        -Fields $fields `
        -Notes $notes
    $evidenceResult = Invoke-NavigatorPublicHttpsEvidenceExport -SummaryPath $dedicatedSummaryLog -OutputPath $dedicatedEvidenceLog
    $evidenceOutputPath = $evidenceResult.EvidencePath
    Write-NavigatorPublicHttpsEvidenceReport -EvidenceResult $evidenceResult
    Write-NavigatorPublicHttpsConsoleSummary -FinalResult $finalResult -ExitCode $exitCode -Fields $fields -Notes $notes
    Write-Host "Dedicated real public HTTPS smoke FAIL. Summary log: $dedicatedSummaryLog" -ForegroundColor Red
    exit $exitCode
} finally {
    Restore-NavigatorPublicHttpsEnvironment
}
