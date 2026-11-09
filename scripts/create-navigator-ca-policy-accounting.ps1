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
foreach ($match in [regex]::Matches($bundleText, '-----BEGIN CERTIFICATE-----(?<body>[\s\S]*?)-----END CERTIFICATE-----')) {
    $der = [Convert]::FromBase64String(($match.Groups['body'].Value -replace '\s', ''))
    $cert = [System.Security.Cryptography.X509Certificates.X509Certificate2]::new($der)
    try {
        $fingerprint = Get-Sha256Hex $cert.RawData
        if ($fixtureHashes.ContainsKey($fingerprint)) { throw "Fixture-root fingerprint contamination found: $fingerprint" }
        $fingerprints.Add($fingerprint)
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
        reason = "Mozilla purpose/domain/distrust policy is not represented in the PEM and the bundled verifier cannot enforce name constraints or certificate policy processing."
    }
})
$report = [ordered]@{
    schema_version = "guidexos.navigator.ca-policy-accounting.v1"
    source_identity = $SourceIdentity
    source_date = $SourceDate
    source_checksum_sha256 = $sha
    toolchain_identity = $ToolchainIdentity
    generator_identity = "scripts/create-navigator-ca-policy-accounting.ps1 v1; deterministic all-reject policy"
    input_certificate_count = $fingerprints.Count
    included_count = 0
    included = @()
    excluded_count = $excluded.Count
    excluded = $excluded
    fixture_root_contamination = "none-detected"
    policy_result = "rejected-disabled-no-public-roots-included"
    integrity_vs_authenticity = "SHA-256 binds these accounting records to input bytes; it does not authenticate source provenance or approval."
}
$json = $report | ConvertTo-Json -Depth 8
$outputFull = [System.IO.Path]::GetFullPath($OutputPath)
$parent = Split-Path -Parent $outputFull
if ($parent) { New-Item -ItemType Directory -Force -Path $parent | Out-Null }
[System.IO.File]::WriteAllText($outputFull, $json + [Environment]::NewLine, [System.Text.Encoding]::ASCII)
Write-Output "Navigator CA policy accounting written: $outputFull"
Write-Output "input_roots=$($fingerprints.Count) included=0 excluded=$($excluded.Count) sha256=$sha"
