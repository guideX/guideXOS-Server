param(
    [Parameter(Mandatory = $true)][string]$SummaryPath,
    [string]$OutputPath
)

$ErrorActionPreference = "Stop"

function Get-NavigatorPublicHttpsSummaryFields {
    param([Parameter(Mandatory = $true)][string]$LiteralPath)

    if (-not (Test-Path -LiteralPath $LiteralPath -PathType Leaf)) {
        throw "Summary log not found: $LiteralPath"
    }

    $fields = [ordered]@{}
    $notes = New-Object System.Collections.Generic.List[string]

    foreach ($line in (Get-Content -LiteralPath $LiteralPath)) {
        if ($line -match '^\[NAVIGATOR-PUBLIC-HTTPS\] (?<key>[^=]+)=(?<value>.*)$') {
            $key = $matches["key"].Trim()
            $value = $matches["value"]
            if ([string]::Equals($key, "note", [System.StringComparison]::OrdinalIgnoreCase)) {
                $notes.Add($value)
            } else {
                $fields[$key] = $value
            }
        }
    }

    return [pscustomobject]@{
        Fields = $fields
        Notes = @($notes)
    }
}

function Get-NavigatorPublicHttpsFieldValue {
    param(
        [Parameter(Mandatory = $true)][System.Collections.IDictionary]$Fields,
        [Parameter(Mandatory = $true)][string]$Name
    )

    if ($Fields.Contains($Name)) {
        return [string]$Fields[$Name]
    }
    return $null
}

function ConvertTo-NavigatorNullableInt {
    param([AllowNull()][string]$Value)

    $parsed = 0
    if ([int]::TryParse($Value, [ref]$parsed)) {
        return $parsed
    }
    return $null
}

function ConvertTo-NavigatorNullableLong {
    param([AllowNull()][string]$Value)

    $parsed = [int64]0
    if ([int64]::TryParse($Value, [ref]$parsed)) {
        return $parsed
    }
    return $null
}

function Get-NavigatorPublicHttpsEvidenceStatus {
    param([Parameter(Mandatory = $true)][System.Collections.IDictionary]$Fields)

    $resultMarker = Get-NavigatorPublicHttpsFieldValue -Fields $Fields -Name "result_marker"
    $finalResult = Get-NavigatorPublicHttpsFieldValue -Fields $Fields -Name "final_result"
    $assertionResult = Get-NavigatorPublicHttpsFieldValue -Fields $Fields -Name "pass_contract_assertion_result"

    if ([string]::Equals($resultMarker, "PASS", [System.StringComparison]::OrdinalIgnoreCase) -and
        [string]::Equals($finalResult, "PASS", [System.StringComparison]::OrdinalIgnoreCase) -and
        [string]::Equals($assertionResult, "PASS", [System.StringComparison]::OrdinalIgnoreCase)) {
        return "PASS"
    }
    if ([string]::Equals($resultMarker, "SETUP_BLOCKED", [System.StringComparison]::OrdinalIgnoreCase)) {
        return "SETUP_BLOCKED"
    }
    if ([string]::Equals($resultMarker, "SKIP", [System.StringComparison]::OrdinalIgnoreCase) -or
        [string]::Equals($finalResult, "SKIP", [System.StringComparison]::OrdinalIgnoreCase)) {
        return "SKIP"
    }
    return "FAIL"
}

if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    if ($SummaryPath.EndsWith(".summary.log", [System.StringComparison]::OrdinalIgnoreCase)) {
        $OutputPath = $SummaryPath.Substring(0, $SummaryPath.Length - ".summary.log".Length) + ".evidence.json"
    } else {
        $OutputPath = $SummaryPath + ".evidence.json"
    }
}

$summaryFullPath = [System.IO.Path]::GetFullPath($SummaryPath)
$outputFullPath = [System.IO.Path]::GetFullPath($OutputPath)
$parsed = Get-NavigatorPublicHttpsSummaryFields -LiteralPath $summaryFullPath
$fields = $parsed.Fields
$evidenceStatus = Get-NavigatorPublicHttpsEvidenceStatus -Fields $fields

$evidence = [ordered]@{
    schema_version = "guidexos.navigator.public-https-evidence.v0.3"
    generated_utc = ([datetime]::UtcNow.ToString("o"))
    source_summary = $summaryFullPath
    result_marker = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "result_marker"
    final_result = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "final_result"
    target_url = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "target_url"
    target_host = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "target_host"
    public_trust_ready = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "public_trust_ready"
    public_ca_source_marker = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "public_ca_source_marker"
    public_ca_bytes = ConvertTo-NavigatorNullableLong (Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "public_ca_bytes")
    public_ca_parsed_certs = ConvertTo-NavigatorNullableInt (Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "public_ca_parsed_certs")
    trust_bundle_manifest_present = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "trust_bundle_manifest_present"
    trust_bundle_sha256 = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "trust_bundle_sha256"
    trust_bundle_type = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "trust_bundle_type"
    trust_bundle_root_count = ConvertTo-NavigatorNullableInt (Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "trust_bundle_root_count")
    trust_bundle_production_ready = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "trust_bundle_production_ready"
    trust_bundle_test_only = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "trust_bundle_test_only"
    dns_result = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "dns_result"
    tcp_result = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "tcp_result"
    tls_result = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "tls_result"
    certificate_validation_result = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "certificate_validation_result"
    hostname_validation_result = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "hostname_validation_result"
    verify_flags = ConvertTo-NavigatorNullableInt (Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "verify_flags")
    sni_host = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "sni_host"
    http_status = ConvertTo-NavigatorNullableInt (Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "http_status")
    content_type = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "content_type"
    content_encoding = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "content_encoding"
    unsupported_reason = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "unsupported_reason"
    plaintext_fallback = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "plaintext_fallback"
    pass_contract_assertion_result = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "pass_contract_assertion_result"
    pass_contract_assertion_exit_code = ConvertTo-NavigatorNullableInt (Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "pass_contract_assertion_exit_code")
    evidence_status = $evidenceStatus
}

$outputDir = Split-Path -Parent $outputFullPath
if (-not [string]::IsNullOrWhiteSpace($outputDir)) {
    New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
}

$json = $evidence | ConvertTo-Json -Depth 4
[System.IO.File]::WriteAllText($outputFullPath, $json + [Environment]::NewLine, [System.Text.Encoding]::ASCII)

Write-Output "Navigator public HTTPS evidence JSON generated:"
Write-Output "  summary: $summaryFullPath"
Write-Output "  evidence: $outputFullPath"
Write-Output "  evidence_status: $evidenceStatus"
