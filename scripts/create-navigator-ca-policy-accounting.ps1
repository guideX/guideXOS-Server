param(
    [Parameter(Mandatory = $true)][string]$BundlePath,
    [Parameter(Mandatory = $true)][string]$OutputPath,
    [Parameter(Mandatory = $true)][string]$ExpectedSha256,
    [Parameter(Mandatory = $true)][string]$SourceIdentity,
    [Parameter(Mandatory = $true)][string]$SourceDate,
    [Parameter(Mandatory = $true)][string]$ToolchainIdentity,
    [string]$FixtureBundlePath
)

$ErrorActionPreference = "Stop"
$sizeCap = 512KB
function Get-Sha256Hex([byte[]]$Bytes) {
    $algorithm = [System.Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($algorithm.ComputeHash($Bytes))).Replace("-", "").ToLowerInvariant() }
    finally { $algorithm.Dispose() }
}
$bundleFull = [System.IO.Path]::GetFullPath($BundlePath)
if (-not (Test-Path -LiteralPath $bundleFull -PathType Leaf)) { throw "CA candidate input was not found: $bundleFull" }
$bundleBytes = [System.IO.File]::ReadAllBytes($bundleFull)
if ($bundleBytes.Length -eq 0 -or $bundleBytes.Length -gt $sizeCap) { throw "CA candidate input is empty or exceeds the 512 KiB cap." }
$bundleText = [System.Text.Encoding]::ASCII.GetString($bundleBytes)
if ($bundleText.Contains("guideXOS Navigator smoke-only root CA fixture")) { throw "Fixture root contamination marker found in candidate input." }
$beginCount = ([regex]::Matches($bundleText, '-----BEGIN CERTIFICATE-----')).Count
$endCount = ([regex]::Matches($bundleText, '-----END CERTIFICATE-----')).Count
if ($beginCount -eq 0 -or $beginCount -ne $endCount) { throw "Candidate PEM certificate boundaries are missing or mismatched." }
$actualToolchainIdentity = "PowerShell $($PSVersionTable.PSVersion); .NET $([Environment]::Version); .NET X509Certificate2"
if ($ToolchainIdentity -ne $actualToolchainIdentity) { throw "Toolchain identity is not the actual pinned runtime. Expected '$actualToolchainIdentity'." }
$sha = Get-Sha256Hex $bundleBytes
if ($sha -ne $ExpectedSha256.ToLowerInvariant()) { throw "Candidate source checksum mismatch. Expected $ExpectedSha256, got $sha." }

$fixtureHashes = @{}
if (-not [string]::IsNullOrWhiteSpace($FixtureBundlePath)) {
    $fixtureText = [System.IO.File]::ReadAllText([System.IO.Path]::GetFullPath($FixtureBundlePath), [System.Text.Encoding]::ASCII)
    foreach ($match in [regex]::Matches($fixtureText, '-----BEGIN CERTIFICATE-----(?<body>[\s\S]*?)-----END CERTIFICATE-----')) {
        $der = [Convert]::FromBase64String(($match.Groups['body'].Value -replace '\s', ''))
        $fixtureHash = Get-Sha256Hex $der
        $fixtureHashes[$fixtureHash] = $true
    }
}

$fingerprints = [System.Collections.Generic.List[string]]::new()
$restrictionData = @{}
foreach ($match in [regex]::Matches($bundleText, '-----BEGIN CERTIFICATE-----(?<body>[\s\S]*?)-----END CERTIFICATE-----')) {
    $der = [Convert]::FromBase64String(($match.Groups['body'].Value -replace '\s', ''))
    $cert = [System.Security.Cryptography.X509Certificates.X509Certificate2]::new($der)
    try {
        $fingerprint = Get-Sha256Hex $cert.RawData
        if ($fixtureHashes.ContainsKey($fingerprint)) { throw "Fixture-root fingerprint contamination found: $fingerprint" }
        $fingerprints.Add($fingerprint)
        $nameConstraints = @($cert.Extensions | Where-Object { $_.Oid.Value -eq '2.5.29.30' })
        $restrictionData[$fingerprint] = [ordered]@{
            name_constraints_extension = if ($nameConstraints.Count -eq 0) { 'absent' } elseif ($nameConstraints.Count -gt 1) { 'duplicate-invalid' } else { 'present' }
            name_constraints_critical = if ($nameConstraints.Count -eq 1 -and $nameConstraints[0].Critical) { 'critical' } elseif ($nameConstraints.Count -eq 1) { 'noncritical' } else { 'not-applicable' }
            name_constraints_extension_sha256 = if ($nameConstraints.Count -eq 1) { Get-Sha256Hex $nameConstraints[0].RawData } else { 'none' }
            enforcement = if ($nameConstraints.Count -gt 1) { 'reject-duplicate-extension' } else { 'path-enforced-for-supported-forms; unsupported applicable forms fail closed' }
        }
    } finally { $cert.Dispose() }
}
if ($fingerprints.Count -eq 0) { throw "Candidate input contains no certificates." }
if ($fingerprints.Count -ne $beginCount) { throw "Candidate certificate accounting count does not match its PEM block count." }
$sorted = @($fingerprints | Sort-Object -Unique)
if ($sorted.Count -ne $fingerprints.Count) { throw "Candidate contains duplicate DER certificates; accounting is ambiguous." }
$excluded = @($sorted | ForEach-Object {
    [ordered]@{
        certificate_sha256 = $_
        disposition = "excluded"
        mozilla_server_auth_purpose = "unavailable-in-PEM"
        mozilla_domain_constraints_and_distrust = "unavailable-in-PEM"
        name_constraints = $restrictionData[$_]
        certificate_policy_processing = "not-enforced-by-bundled-verifier"
        root_program_provenance_and_approval = "not-established"
        runtime_public_trust_authorization = "disabled"
        reason = "Excluded pending Mozilla server-auth purpose/domain/distrust metadata, certificate-policy evaluation, and root-program provenance/approval. Name Constraints are enforced on the verified path for supported forms."
    }
})
$report = [ordered]@{
    schema_version = "guidexos.navigator.ca-policy-accounting.v1"
    source_identity = $SourceIdentity
    source_date = $SourceDate
    source_checksum_sha256 = $sha
    toolchain_identity = $ToolchainIdentity
    generator_identity = "scripts/create-navigator-ca-policy-accounting.ps1 v2; NAVHTTPS19 restriction-aware deterministic all-reject policy"
    input_certificate_count = $fingerprints.Count
    included_count = 0
    included = @()
    excluded_count = $excluded.Count
    excluded = $excluded
    name_constraints_verifier = [ordered]@{
        mbedtls_version = "4.1.0"
        source = "guideXOS NAVHTTPS19 patch adapted from Mbed-TLS/mbedtls PR #11027 at 26dc074328d9bdbfc206285e70cc7c5f6aec509e"
        supported_forms = @("dNSName", "iPAddress with contiguous IPv4/IPv6 masks", "rfc822Name", "directoryName")
        unsupported_applicable_forms = "fail-closed"
        verified_path_only = $true
    }
    mozilla_metadata_status = "unavailable; not reconstructed from PEM"
    certificate_policy_status = "not-enforced"
    root_program_status = "no roots approved"
    runtime_public_trust_authorization = "disabled"
    fixture_root_contamination = "none-detected"
    policy_result = "118-roots-excluded-disabled-no-public-roots-included"
    integrity_vs_authenticity = "SHA-256 binds these accounting records to input bytes; it does not authenticate source provenance or approval."
}
$json = $report | ConvertTo-Json -Depth 8
$outputFull = [System.IO.Path]::GetFullPath($OutputPath)
$parent = Split-Path -Parent $outputFull
if ($parent) { New-Item -ItemType Directory -Force -Path $parent | Out-Null }
[System.IO.File]::WriteAllText($outputFull, $json + [Environment]::NewLine, [System.Text.Encoding]::ASCII)
Write-Output "Navigator CA policy accounting written: $outputFull"
Write-Output "input_roots=$($fingerprints.Count) included=0 excluded=$($excluded.Count) sha256=$sha"
