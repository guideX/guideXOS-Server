param(
    [Parameter(Mandatory = $true)][string]$SummaryPath,
    [string]$OutputPath
)

$ErrorActionPreference = "Stop"

. (Join-Path (Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)) "scripts\navigator-public-https-reviewed-targets.ps1")

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
    schema_version = "guidexos.navigator.public-https-evidence.v0.8"
    generated_utc = ([datetime]::UtcNow.ToString("o"))
    source_summary = $summaryFullPath
    reviewed_allowlist_name = Get-NavigatorPublicHttpsReviewedAllowlistName
    reviewed_allowlist_version = Get-NavigatorPublicHttpsReviewedAllowlistVersion
    result_marker = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "result_marker"
    final_result = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "final_result"
    target_url = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "target_url"
    target_host = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "target_host"
    reviewed_target_policy = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "reviewed_target_policy"
    reviewed_target_allowlist = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "reviewed_target_allowlist"
    reviewed_target_match = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "reviewed_target_match"
    reviewed_target_override = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "reviewed_target_override"
    reviewed_target_reason = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "reviewed_target_reason"
    policy_enabled = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "policy_enabled"
    public_pilot_token_present = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "public_pilot_token_present"
    public_pilot_token_value = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "public_pilot_token_value"
    public_proof_lane_active = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "public_proof_lane_active"
    public_trust_ready = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "public_trust_ready"
    public_ca_source_marker = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "public_ca_source_marker"
    public_trust_source_allowed = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "public_trust_source_allowed"
    public_trust_manifest_ready = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "public_trust_manifest_ready"
    public_trust_runtime_hash_match = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "public_trust_runtime_hash_match"
    public_trust_test_only = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "public_trust_test_only"
    public_trust_source_marker = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "public_trust_source_marker"
    public_trust_lane = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "public_trust_lane"
    public_trust_reason = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "public_trust_reason"
    public_trust_blocker = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "public_trust_blocker"
    public_ca_bytes = ConvertTo-NavigatorNullableLong (Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "public_ca_bytes")
    public_ca_parsed_certs = ConvertTo-NavigatorNullableInt (Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "public_ca_parsed_certs")
    trust_bundle_manifest_present = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "trust_bundle_manifest_present"
    trust_bundle_sha256 = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "trust_bundle_sha256"
    trust_bundle_type = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "trust_bundle_type"
    trust_bundle_root_count = ConvertTo-NavigatorNullableInt (Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "trust_bundle_root_count")
    trust_bundle_production_ready = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "trust_bundle_production_ready"
    trust_bundle_test_only = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "trust_bundle_test_only"
    runtime_manifest_present = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "runtime_manifest_present"
    runtime_manifest_hash_match = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "runtime_manifest_hash_match"
    runtime_manifest_bundle_type = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "runtime_manifest_bundle_type"
    runtime_manifest_rotation_id = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "runtime_manifest_rotation_id"
    runtime_manifest_production_ready = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "runtime_manifest_production_ready"
    runtime_manifest_test_only = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "runtime_manifest_test_only"
    runtime_manifest_root_count = ConvertTo-NavigatorNullableInt (Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "runtime_manifest_root_count")
    runtime_manifest_sha256 = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "runtime_manifest_sha256"
    dns_result = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "dns_result"
    dns_server = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "dns_server"
    dns_resolved_ip = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "dns_resolved_ip"
    dns_query_id = ConvertTo-NavigatorNullableInt (Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "dns_query_id")
    dns_source_port = ConvertTo-NavigatorNullableInt (Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "dns_source_port")
    dns_destination_port = ConvertTo-NavigatorNullableInt (Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "dns_destination_port")
    dns_query_bytes = ConvertTo-NavigatorNullableInt (Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "dns_query_bytes")
    dns_send_attempts = ConvertTo-NavigatorNullableInt (Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "dns_send_attempts")
    dns_last_send_result = ConvertTo-NavigatorNullableInt (Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "dns_last_send_result")
    dns_reply_bytes = ConvertTo-NavigatorNullableInt (Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "dns_reply_bytes")
    dns_reply_rcode = ConvertTo-NavigatorNullableInt (Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "dns_reply_rcode")
    dns_reply_answer_count = ConvertTo-NavigatorNullableInt (Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "dns_reply_answer_count")
    dns_ipv4_rx_packets = ConvertTo-NavigatorNullableInt (Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "dns_ipv4_rx_packets")
    dns_ipv4_rx_errors = ConvertTo-NavigatorNullableInt (Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "dns_ipv4_rx_errors")
    dns_ipv4_checksum_errors = ConvertTo-NavigatorNullableInt (Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "dns_ipv4_checksum_errors")
    dns_udp_rx_datagrams = ConvertTo-NavigatorNullableInt (Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "dns_udp_rx_datagrams")
    dns_udp_rx_errors = ConvertTo-NavigatorNullableInt (Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "dns_udp_rx_errors")
    dns_udp_checksum_errors = ConvertTo-NavigatorNullableInt (Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "dns_udp_checksum_errors")
    dns_udp_no_port_errors = ConvertTo-NavigatorNullableInt (Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "dns_udp_no_port_errors")
    tcp_result = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "tcp_result"
    tls_result = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "tls_result"
    transport_selection = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "transport_selection"
    transport_policy_reason = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "transport_policy_reason"
    tls_status = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "tls_status"
    tls_backend = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "tls_backend"
    tls_protocol = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "tls_protocol"
    tls_connect_attempts = ConvertTo-NavigatorNullableInt (Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "tls_connect_attempts")
    tls_retry_count = ConvertTo-NavigatorNullableInt (Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "tls_retry_count")
    tls_retry_reason = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "tls_retry_reason"
    tls_bytes_written_before_retry = ConvertTo-NavigatorNullableInt (Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "tls_bytes_written_before_retry")
    tls_handshake_error_code = ConvertTo-NavigatorNullableInt (Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "tls_handshake_error_code")
    tls_transport_error_code = ConvertTo-NavigatorNullableInt (Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "tls_transport_error_code")
    tls_request_bytes_written = ConvertTo-NavigatorNullableInt (Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "tls_request_bytes_written")
    tls_response_bytes_read = ConvertTo-NavigatorNullableInt (Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "tls_response_bytes_read")
    tls_failure_classification = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "tls_failure_classification"
    tcp_abort_used = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "tcp_abort_used"
    redirected_https_retry_used = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "redirected_https_retry_used"
    redirect_hop_index = ConvertTo-NavigatorNullableInt (Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "redirect_hop_index")
    redirect_hop_url = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "redirect_hop_url"
    certificate_validation_result = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "certificate_validation_result"
    hostname_validation_result = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "hostname_validation_result"
    verify_flags = ConvertTo-NavigatorNullableInt (Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "verify_flags")
    sni_host = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "sni_host"
    source_type = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "source_type"
    requested_url = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "requested_url"
    final_url = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "final_url"
    redirect_count = ConvertTo-NavigatorNullableInt (Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "redirect_count")
    http_status = ConvertTo-NavigatorNullableInt (Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "http_status")
    content_type = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "content_type"
    content_encoding = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "content_encoding"
    header_cap_hit = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "header_cap_hit"
    body_cap_hit = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "body_cap_hit"
    downgrade_blocked = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "downgrade_blocked"
    tls_succeeded_before_content_failure = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "tls_succeeded_before_content_failure"
    unsupported_reason = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "unsupported_reason"
    tls_transport_proof_result = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "tls_transport_proof_result"
    content_compatibility_result = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "content_compatibility_result"
    page_render_result = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "page_render_result"
    real_world_compatibility_note = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "real_world_compatibility_note"
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
