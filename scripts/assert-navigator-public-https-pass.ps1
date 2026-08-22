param(
    [string]$SummaryPath,
    [switch]$SelfTest
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

function Add-NavigatorPublicHttpsAssertionCheck {
    param(
        [Parameter(Mandatory = $true)]$Checks,
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][bool]$Passed,
        [Parameter(Mandatory = $true)][string]$Detail
    )

    $Checks.Add([pscustomobject]@{
        Name = $Name
        Passed = $Passed
        Detail = $Detail
    })
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

function Test-NavigatorPublicHttpsPassContract {
    param([Parameter(Mandatory = $true)][string]$LiteralPath)

    $parsed = Get-NavigatorPublicHttpsSummaryFields -LiteralPath $LiteralPath
    $fields = $parsed.Fields
    $checks = New-Object 'System.Collections.Generic.List[object]'

    $resultMarker = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "result_marker"
    Add-NavigatorPublicHttpsAssertionCheck -Checks $checks -Name "result_marker" -Passed ([string]::Equals($resultMarker, "PASS", [System.StringComparison]::OrdinalIgnoreCase)) -Detail "Expected result_marker=PASS, got '$resultMarker'."

    $finalResult = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "final_result"
    Add-NavigatorPublicHttpsAssertionCheck -Checks $checks -Name "final_result" -Passed ([string]::Equals($finalResult, "PASS", [System.StringComparison]::OrdinalIgnoreCase)) -Detail "Expected final_result=PASS, got '$finalResult'."

    $targetUrl = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "target_url"
    $targetUri = $null
    $targetUrlValid = $false
    $targetUrlError = $null
    if ([string]::IsNullOrWhiteSpace($targetUrl)) {
        $targetUrlError = "target_url is missing."
    } else {
        try {
            $targetUri = [System.Uri]$targetUrl
            if (-not $targetUri.IsAbsoluteUri) {
                $targetUrlError = "target_url is not an absolute URL."
            } elseif (-not [string]::Equals($targetUri.Scheme, "https", [System.StringComparison]::OrdinalIgnoreCase)) {
                $targetUrlError = "target_url must use https://."
            } else {
                $targetUrlValid = $true
            }
        } catch {
            $targetUrlError = "target_url is not a valid URI."
        }
    }
    Add-NavigatorPublicHttpsAssertionCheck -Checks $checks -Name "target_url" -Passed $targetUrlValid -Detail $(if ($targetUrlValid) { "target_url uses HTTPS: $targetUrl" } else { $targetUrlError })

    $targetHost = $null
    $targetHostIsDns = $false
    if ($targetUrlValid) {
        $targetHost = $targetUri.Host
        $parsedIp = $null
        $targetHostIsDns = -not [System.Net.IPAddress]::TryParse($targetHost, [ref]$parsedIp)
    }
    Add-NavigatorPublicHttpsAssertionCheck -Checks $checks -Name "target_host" -Passed $targetHostIsDns -Detail $(if ($targetHostIsDns) { "Target hostname is DNS-based: $targetHost" } elseif ($targetUrlValid) { "Target hostname must not be a numeric IP literal: $targetHost" } else { "Target hostname could not be validated because target_url failed validation." })

    $publicTrustReady = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "public_trust_ready"
    Add-NavigatorPublicHttpsAssertionCheck -Checks $checks -Name "public_trust_ready" -Passed ([string]::Equals($publicTrustReady, "yes", [System.StringComparison]::OrdinalIgnoreCase)) -Detail "Expected public_trust_ready=yes, got '$publicTrustReady'."

    $publicCaSourceMarker = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "public_ca_source_marker"
    $disallowedSourceMarkers = @(
        "",
        "(none)",
        "(unknown)",
        "missing",
        "smoke-fixture",
        "deterministic-only",
        "ignored-local-file"
    )
    $caSourceMarkerPassed = -not [string]::IsNullOrWhiteSpace($publicCaSourceMarker) -and ($disallowedSourceMarkers -notcontains $publicCaSourceMarker)
    Add-NavigatorPublicHttpsAssertionCheck -Checks $checks -Name "public_ca_source_marker" -Passed $caSourceMarkerPassed -Detail $(if ($caSourceMarkerPassed) { "Explicit public CA source marker: $publicCaSourceMarker" } else { "public_ca_source_marker must name an explicit non-deterministic source, got '$publicCaSourceMarker'." })

    $parsedCertCount = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "public_ca_parsed_certs"
    $parsedCertCountValue = 0
    $parsedCertCountPassed = [int]::TryParse($parsedCertCount, [ref]$parsedCertCountValue) -and ($parsedCertCountValue -gt 0)
    Add-NavigatorPublicHttpsAssertionCheck -Checks $checks -Name "public_ca_parsed_certs" -Passed $parsedCertCountPassed -Detail $(if ($parsedCertCountPassed) { "Parsed public CA certificates: $parsedCertCountValue" } else { "public_ca_parsed_certs must be a positive integer, got '$parsedCertCount'." })

    $trustManifestPresent = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "trust_bundle_manifest_present"
    $trustManifestPresentPassed = [string]::Equals($trustManifestPresent, "yes", [System.StringComparison]::OrdinalIgnoreCase)
    Add-NavigatorPublicHttpsAssertionCheck -Checks $checks -Name "trust_bundle_manifest_present" -Passed $trustManifestPresentPassed -Detail "Expected trust_bundle_manifest_present=yes, got '$trustManifestPresent'."

    $trustBundleSha256 = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "trust_bundle_sha256"
    $trustBundleSha256Passed = -not [string]::IsNullOrWhiteSpace($trustBundleSha256) -and [regex]::IsMatch($trustBundleSha256, '^[0-9a-f]{64}$')
    Add-NavigatorPublicHttpsAssertionCheck -Checks $checks -Name "trust_bundle_sha256" -Passed $trustBundleSha256Passed -Detail $(if ($trustBundleSha256Passed) { "trust_bundle_sha256 is a 64-character lowercase hex digest." } else { "trust_bundle_sha256 must be a 64-character lowercase hex digest, got '$trustBundleSha256'." })

    $trustBundleType = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "trust_bundle_type"
    $trustBundleTypePassed = [string]::Equals($trustBundleType, "production-public-probe-merged", [System.StringComparison]::OrdinalIgnoreCase)
    Add-NavigatorPublicHttpsAssertionCheck -Checks $checks -Name "trust_bundle_type" -Passed $trustBundleTypePassed -Detail "Expected trust_bundle_type=production-public-probe-merged, got '$trustBundleType'."

    $trustBundleRootCount = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "trust_bundle_root_count"
    $trustBundleRootCountValue = 0
    $trustBundleRootCountPassed = [int]::TryParse($trustBundleRootCount, [ref]$trustBundleRootCountValue) -and ($trustBundleRootCountValue -gt 0)
    Add-NavigatorPublicHttpsAssertionCheck -Checks $checks -Name "trust_bundle_root_count" -Passed $trustBundleRootCountPassed -Detail $(if ($trustBundleRootCountPassed) { "trust_bundle_root_count is positive: $trustBundleRootCountValue" } else { "trust_bundle_root_count must be a positive integer, got '$trustBundleRootCount'." })

    $trustBundleProductionReady = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "trust_bundle_production_ready"
    $trustBundleProductionReadyPassed = [string]::Equals($trustBundleProductionReady, "yes", [System.StringComparison]::OrdinalIgnoreCase)
    Add-NavigatorPublicHttpsAssertionCheck -Checks $checks -Name "trust_bundle_production_ready" -Passed $trustBundleProductionReadyPassed -Detail "Expected trust_bundle_production_ready=yes, got '$trustBundleProductionReady'."

    $trustBundleTestOnly = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "trust_bundle_test_only"
    $trustBundleTestOnlyPassed = [string]::Equals($trustBundleTestOnly, "no", [System.StringComparison]::OrdinalIgnoreCase)
    Add-NavigatorPublicHttpsAssertionCheck -Checks $checks -Name "trust_bundle_test_only" -Passed $trustBundleTestOnlyPassed -Detail "Expected trust_bundle_test_only=no, got '$trustBundleTestOnly'."

    $runtimeManifestPresent = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "runtime_manifest_present"
    Add-NavigatorPublicHttpsAssertionCheck -Checks $checks -Name "runtime_manifest_present" -Passed ([string]::Equals($runtimeManifestPresent, "yes", [System.StringComparison]::OrdinalIgnoreCase)) -Detail "Expected runtime_manifest_present=yes, got '$runtimeManifestPresent'."

    $runtimeManifestHashMatch = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "runtime_manifest_hash_match"
    Add-NavigatorPublicHttpsAssertionCheck -Checks $checks -Name "runtime_manifest_hash_match" -Passed ([string]::Equals($runtimeManifestHashMatch, "yes", [System.StringComparison]::OrdinalIgnoreCase)) -Detail "Expected runtime_manifest_hash_match=yes, got '$runtimeManifestHashMatch'."

    $runtimeManifestBundleType = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "runtime_manifest_bundle_type"
    Add-NavigatorPublicHttpsAssertionCheck -Checks $checks -Name "runtime_manifest_bundle_type" -Passed ([string]::Equals($runtimeManifestBundleType, "production-public-probe-merged", [System.StringComparison]::OrdinalIgnoreCase)) -Detail "Expected runtime_manifest_bundle_type=production-public-probe-merged, got '$runtimeManifestBundleType'."

    $runtimeManifestProductionReady = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "runtime_manifest_production_ready"
    Add-NavigatorPublicHttpsAssertionCheck -Checks $checks -Name "runtime_manifest_production_ready" -Passed ([string]::Equals($runtimeManifestProductionReady, "yes", [System.StringComparison]::OrdinalIgnoreCase)) -Detail "Expected runtime_manifest_production_ready=yes, got '$runtimeManifestProductionReady'."

    $runtimeManifestTestOnly = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "runtime_manifest_test_only"
    Add-NavigatorPublicHttpsAssertionCheck -Checks $checks -Name "runtime_manifest_test_only" -Passed ([string]::Equals($runtimeManifestTestOnly, "no", [System.StringComparison]::OrdinalIgnoreCase)) -Detail "Expected runtime_manifest_test_only=no, got '$runtimeManifestTestOnly'."

    $runtimeManifestRootCount = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "runtime_manifest_root_count"
    $runtimeManifestRootCountValue = 0
    $runtimeManifestRootCountPassed = [int]::TryParse($runtimeManifestRootCount, [ref]$runtimeManifestRootCountValue) -and ($runtimeManifestRootCountValue -gt 0)
    Add-NavigatorPublicHttpsAssertionCheck -Checks $checks -Name "runtime_manifest_root_count" -Passed $runtimeManifestRootCountPassed -Detail $(if ($runtimeManifestRootCountPassed) { "runtime_manifest_root_count is positive: $runtimeManifestRootCountValue" } else { "runtime_manifest_root_count must be a positive integer, got '$runtimeManifestRootCount'." })

    $runtimeManifestSha256 = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "runtime_manifest_sha256"
    $runtimeManifestSha256Passed = -not [string]::IsNullOrWhiteSpace($runtimeManifestSha256) -and [regex]::IsMatch($runtimeManifestSha256, '^[0-9a-f]{64}$')
    Add-NavigatorPublicHttpsAssertionCheck -Checks $checks -Name "runtime_manifest_sha256" -Passed $runtimeManifestSha256Passed -Detail $(if ($runtimeManifestSha256Passed) { "runtime_manifest_sha256 is a 64-character lowercase hex digest." } else { "runtime_manifest_sha256 must be a 64-character lowercase hex digest, got '$runtimeManifestSha256'." })

    foreach ($fieldName in @(
        "dns_result",
        "tcp_result",
        "tls_result",
        "certificate_validation_result",
        "hostname_validation_result"
    )) {
        $value = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name $fieldName
        Add-NavigatorPublicHttpsAssertionCheck -Checks $checks -Name $fieldName -Passed ([string]::Equals($value, "PASS", [System.StringComparison]::OrdinalIgnoreCase)) -Detail "Expected $fieldName=PASS, got '$value'."
    }

    $dnsResolvedIp = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "dns_resolved_ip"
    $dnsResolvedIpPassed = -not [string]::IsNullOrWhiteSpace($dnsResolvedIp) -and $dnsResolvedIp -ne "(not-attempted)"
    Add-NavigatorPublicHttpsAssertionCheck -Checks $checks -Name "dns_resolved_ip" -Passed $dnsResolvedIpPassed -Detail $(if ($dnsResolvedIpPassed) { "Resolved IP recorded: $dnsResolvedIp" } else { "dns_resolved_ip must be present." })

    $tlsBackend = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "tls_backend"
    Add-NavigatorPublicHttpsAssertionCheck -Checks $checks -Name "tls_backend" -Passed ([string]::Equals($tlsBackend, "mbedtls", [System.StringComparison]::Ordinal)) -Detail "Expected tls_backend=mbedtls, got '$tlsBackend'."

    $evidenceLane = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "evidence_lane"
    Add-NavigatorPublicHttpsAssertionCheck -Checks $checks -Name "evidence_lane" -Passed ([string]::Equals($evidenceLane, "kernel_public_https", [System.StringComparison]::Ordinal)) -Detail "Expected evidence_lane=kernel_public_https, got '$evidenceLane'."
    foreach ($contractField in @("tls_suite_contract", "tls_suite_contract_installed", "tls_clienthello_scsv_only", "tls_clienthello_contract_match")) {
        $contractValue = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name $contractField
        $expectedValue = switch ($contractField) {
            "tls_suite_contract" { "explicit_bounded" }
            "tls_suite_contract_installed" { "yes" }
            "tls_clienthello_scsv_only" { "no" }
            default { "yes" }
        }
        Add-NavigatorPublicHttpsAssertionCheck -Checks $checks -Name $contractField -Passed ([string]::Equals($contractValue, $expectedValue, [System.StringComparison]::Ordinal)) -Detail "Expected $contractField=$expectedValue, got '$contractValue'."
    }
    foreach ($countField in @("tls_suite_contract_count", "tls_suite_contract_real_count", "tls_clienthello_real_suite_count")) {
        $countText = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name $countField
        $countValue = 0
        $countPassed = [int]::TryParse($countText, [ref]$countValue) -and $countValue -gt 0
        Add-NavigatorPublicHttpsAssertionCheck -Checks $checks -Name $countField -Passed $countPassed -Detail "Expected $countField to be positive, got '$countText'."
    }
    $negotiatedSuite = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "tls_negotiated_suite"
    Add-NavigatorPublicHttpsAssertionCheck -Checks $checks -Name "tls_negotiated_suite" -Passed (-not [string]::IsNullOrWhiteSpace($negotiatedSuite) -and $negotiatedSuite -ne "(none)") -Detail "Expected a negotiated suite, got '$negotiatedSuite'."

    $tlsProtocol = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "tls_protocol"
    $tlsProtocolPassed = -not [string]::IsNullOrWhiteSpace($tlsProtocol) -and $tlsProtocol -match '^TLSv1\.[23]$'
    Add-NavigatorPublicHttpsAssertionCheck -Checks $checks -Name "tls_protocol" -Passed $tlsProtocolPassed -Detail $(if ($tlsProtocolPassed) { "TLS version recorded: $tlsProtocol" } else { "Expected TLSv1.2 or TLSv1.3, got '$tlsProtocol'." })

    foreach ($fieldName in @("policy_enabled", "public_pilot_token_present", "public_proof_lane_active")) {
        $value = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name $fieldName
        Add-NavigatorPublicHttpsAssertionCheck -Checks $checks -Name $fieldName -Passed ([string]::Equals($value, "yes", [System.StringComparison]::OrdinalIgnoreCase)) -Detail "Expected $fieldName=yes for the explicit proof lane, got '$value'."
    }

    $verifyFlags = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "verify_flags"
    $verifyFlagsValue = -1
    $verifyFlagsPassed = [int]::TryParse($verifyFlags, [ref]$verifyFlagsValue) -and ($verifyFlagsValue -eq 0)
    Add-NavigatorPublicHttpsAssertionCheck -Checks $checks -Name "verify_flags" -Passed $verifyFlagsPassed -Detail $(if ($verifyFlagsPassed) { "verify_flags=0" } else { "verify_flags must be numeric 0, got '$verifyFlags'." })

    $sniHost = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "sni_host"
    $sniHostPassed = -not [string]::IsNullOrWhiteSpace($sniHost) -and ($sniHost -ne "(not-attempted)")
    Add-NavigatorPublicHttpsAssertionCheck -Checks $checks -Name "sni_host_presence" -Passed $sniHostPassed -Detail $(if ($sniHostPassed) { "sni_host is present: $sniHost" } else { "sni_host must be present and non-empty." })

    $sniMatchesTarget = $false
    if ($sniHostPassed -and $targetUrlValid) {
        $trimChars = [char[]]@('.')
        $normalizedSniHost = $sniHost.TrimEnd($trimChars)
        $normalizedTargetHost = $targetHost.TrimEnd($trimChars)
        $sniMatchesTarget = [string]::Equals($normalizedSniHost, $normalizedTargetHost, [System.StringComparison]::OrdinalIgnoreCase)
    }
    Add-NavigatorPublicHttpsAssertionCheck -Checks $checks -Name "sni_host_match" -Passed $sniMatchesTarget -Detail $(if ($sniMatchesTarget) { "sni_host matches the target hostname: $sniHost" } elseif ($targetUrlValid) { "sni_host '$sniHost' does not match target hostname '$targetHost'." } else { "sni_host match could not be validated because target_url failed validation." })

    $httpStatus = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "http_status"
    $httpStatusValue = 0
    $httpStatusPassed = [int]::TryParse($httpStatus, [ref]$httpStatusValue)
    Add-NavigatorPublicHttpsAssertionCheck -Checks $checks -Name "http_status" -Passed $httpStatusPassed -Detail $(if ($httpStatusPassed) { "HTTP status recorded: $httpStatusValue" } else { "http_status must be present and numeric, got '$httpStatus'." })

    $requestAcceptEncoding = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "request_accept_encoding"
    $requestAcceptEncodingPassed = [string]::Equals($requestAcceptEncoding, "gzip, deflate", [System.StringComparison]::OrdinalIgnoreCase)
    Add-NavigatorPublicHttpsAssertionCheck -Checks $checks -Name "request_accept_encoding" -Passed $requestAcceptEncodingPassed -Detail "Expected request_accept_encoding=gzip, deflate, got '$requestAcceptEncoding'."

    $plaintextFallback = Get-NavigatorPublicHttpsFieldValue -Fields $fields -Name "plaintext_fallback"
    Add-NavigatorPublicHttpsAssertionCheck -Checks $checks -Name "plaintext_fallback" -Passed ([string]::Equals($plaintextFallback, "no", [System.StringComparison]::OrdinalIgnoreCase)) -Detail "Expected plaintext_fallback=no, got '$plaintextFallback'."

    $failedChecks = @($checks | Where-Object { -not $_.Passed })
    $overallPassed = ($failedChecks.Count -eq 0)
    $checkArray = New-Object object[] $checks.Count
    $checks.CopyTo($checkArray, 0)
    $noteArray = [string[]]@($parsed.Notes)
    $result = [ordered]@{}
    $result["SummaryPath"] = [System.IO.Path]::GetFullPath($LiteralPath)
    $result["Passed"] = [bool]$overallPassed
    $result["Checks"] = $checkArray
    $result["Fields"] = $fields
    $result["Notes"] = $noteArray
    return $result
}

function Write-NavigatorPublicHttpsAssertionReport {
    param([Parameter(Mandatory = $true)]$Result)

    Write-Output "Navigator public HTTPS PASS artifact assertion"
    Write-Output "  summary: $($Result["SummaryPath"])"
    Write-Output "  outcome: $(if ($Result["Passed"]) { 'PASS' } else { 'FAIL' })"
    foreach ($check in $Result["Checks"]) {
        $marker = if ($check.Passed) { "[PASS]" } else { "[FAIL]" }
        Write-Output "  $marker $($check.Name): $($check.Detail)"
    }
}

function Invoke-NavigatorPublicHttpsAssertionSelfTest {
    $tempRoot = Join-Path $env:TEMP ("navigator-public-https-assert-selftest-" + [guid]::NewGuid().ToString("N"))
    New-Item -ItemType Directory -Path $tempRoot -Force | Out-Null

    try {
        $baseLines = @(
            "[NAVIGATOR-PUBLIC-HTTPS] final_result=PASS",
            "[NAVIGATOR-PUBLIC-HTTPS] exit_code=0",
            "[NAVIGATOR-PUBLIC-HTTPS] result_marker=PASS",
            "[NAVIGATOR-PUBLIC-HTTPS] target_url=https://sha256.badssl.com/",
            "[NAVIGATOR-PUBLIC-HTTPS] target_host=sha256.badssl.com",
            "[NAVIGATOR-PUBLIC-HTTPS] policy_enabled=yes",
            "[NAVIGATOR-PUBLIC-HTTPS] public_pilot_token_present=yes",
            "[NAVIGATOR-PUBLIC-HTTPS] public_proof_lane_active=yes",
            "[NAVIGATOR-PUBLIC-HTTPS] public_ca_source_marker=env-var",
            "[NAVIGATOR-PUBLIC-HTTPS] public_trust_ready=yes",
            "[NAVIGATOR-PUBLIC-HTTPS] public_ca_parsed_certs=2",
            "[NAVIGATOR-PUBLIC-HTTPS] trust_bundle_manifest_present=yes",
            "[NAVIGATOR-PUBLIC-HTTPS] trust_bundle_sha256=0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
            "[NAVIGATOR-PUBLIC-HTTPS] trust_bundle_type=production-public-probe-merged",
            "[NAVIGATOR-PUBLIC-HTTPS] trust_bundle_root_count=4",
            "[NAVIGATOR-PUBLIC-HTTPS] trust_bundle_production_ready=yes",
            "[NAVIGATOR-PUBLIC-HTTPS] trust_bundle_test_only=no",
            "[NAVIGATOR-PUBLIC-HTTPS] runtime_manifest_present=yes",
            "[NAVIGATOR-PUBLIC-HTTPS] runtime_manifest_hash_match=yes",
            "[NAVIGATOR-PUBLIC-HTTPS] runtime_manifest_bundle_type=production-public-probe-merged",
            "[NAVIGATOR-PUBLIC-HTTPS] runtime_manifest_rotation_id=sha256:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
            "[NAVIGATOR-PUBLIC-HTTPS] runtime_manifest_production_ready=yes",
            "[NAVIGATOR-PUBLIC-HTTPS] runtime_manifest_test_only=no",
            "[NAVIGATOR-PUBLIC-HTTPS] runtime_manifest_root_count=4",
            "[NAVIGATOR-PUBLIC-HTTPS] runtime_manifest_sha256=0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
            "[NAVIGATOR-PUBLIC-HTTPS] dns_result=PASS",
            "[NAVIGATOR-PUBLIC-HTTPS] dns_resolved_ip=104.154.89.105",
            "[NAVIGATOR-PUBLIC-HTTPS] tcp_result=PASS",
            "[NAVIGATOR-PUBLIC-HTTPS] tls_result=PASS",
            "[NAVIGATOR-PUBLIC-HTTPS] tls_backend=mbedtls",
            "[NAVIGATOR-PUBLIC-HTTPS] evidence_lane=kernel_public_https",
            "[NAVIGATOR-PUBLIC-HTTPS] tls_suite_contract=explicit_bounded",
            "[NAVIGATOR-PUBLIC-HTTPS] tls_suite_contract_count=4",
            "[NAVIGATOR-PUBLIC-HTTPS] tls_suite_contract_real_count=4",
            "[NAVIGATOR-PUBLIC-HTTPS] tls_suite_contract_installed=yes",
            "[NAVIGATOR-PUBLIC-HTTPS] tls_clienthello_real_suite_count=4",
            "[NAVIGATOR-PUBLIC-HTTPS] tls_clienthello_scsv_only=no",
            "[NAVIGATOR-PUBLIC-HTTPS] tls_clienthello_contract_match=yes",
            "[NAVIGATOR-PUBLIC-HTTPS] tls_negotiated_suite=TLS-ECDHE-RSA-WITH-AES-128-GCM-SHA256",
            "[NAVIGATOR-PUBLIC-HTTPS] tls_protocol=TLSv1.2",
            "[NAVIGATOR-PUBLIC-HTTPS] certificate_validation_result=PASS",
            "[NAVIGATOR-PUBLIC-HTTPS] hostname_validation_result=PASS",
            "[NAVIGATOR-PUBLIC-HTTPS] verify_flags=0",
            "[NAVIGATOR-PUBLIC-HTTPS] sni_host=sha256.badssl.com",
            "[NAVIGATOR-PUBLIC-HTTPS] http_status=200",
            "[NAVIGATOR-PUBLIC-HTTPS] request_accept_encoding=gzip, deflate",
            "[NAVIGATOR-PUBLIC-HTTPS] plaintext_fallback=no"
        )

        $cases = @(
            [pscustomobject]@{
                Name = "valid-pass"
                ExpectedPass = $true
                Mutator = {
                    param([string[]]$Lines)
                    return $Lines
                }
            },
            [pscustomobject]@{
                Name = "missing-result-marker"
                ExpectedPass = $false
                Mutator = {
                    param([string[]]$Lines)
                    return @($Lines | Where-Object { $_ -notmatch ' result_marker=' })
                }
            },
            [pscustomobject]@{
                Name = "verify-flags-nonzero"
                ExpectedPass = $false
                Mutator = {
                    param([string[]]$Lines)
                    return @($Lines | ForEach-Object {
                        if ($_ -match ' verify_flags=') { "[NAVIGATOR-PUBLIC-HTTPS] verify_flags=32" } else { $_ }
                    })
                }
            },
            [pscustomobject]@{
                Name = "plaintext-fallback-yes"
                ExpectedPass = $false
                Mutator = {
                    param([string[]]$Lines)
                    return @($Lines | ForEach-Object {
                        if ($_ -match ' plaintext_fallback=') { "[NAVIGATOR-PUBLIC-HTTPS] plaintext_fallback=yes" } else { $_ }
                    })
                }
            },
            [pscustomobject]@{
                Name = "sni-host-mismatch"
                ExpectedPass = $false
                Mutator = {
                    param([string[]]$Lines)
                    return @($Lines | ForEach-Object {
                        if ($_ -match ' sni_host=') { "[NAVIGATOR-PUBLIC-HTTPS] sni_host=example.com" } else { $_ }
                    })
                }
            },
            [pscustomobject]@{
                Name = "public-trust-not-ready"
                ExpectedPass = $false
                Mutator = {
                    param([string[]]$Lines)
                    return @($Lines | ForEach-Object {
                        if ($_ -match ' public_trust_ready=') { "[NAVIGATOR-PUBLIC-HTTPS] public_trust_ready=no" } else { $_ }
                    })
                }
            },
            [pscustomobject]@{
                Name = "trust-bundle-not-production-ready"
                ExpectedPass = $false
                Mutator = {
                    param([string[]]$Lines)
                    return @($Lines | ForEach-Object {
                        if ($_ -match ' trust_bundle_production_ready=') { "[NAVIGATOR-PUBLIC-HTTPS] trust_bundle_production_ready=no" } else { $_ }
                    })
                }
            }
        )

        $allPassed = $true
        foreach ($case in $cases) {
            $casePath = Join-Path $tempRoot ($case.Name + ".summary.log")
            $lines = & $case.Mutator $baseLines
            [System.IO.File]::WriteAllLines($casePath, $lines, [System.Text.Encoding]::ASCII)

            $result = Test-NavigatorPublicHttpsPassContract -LiteralPath $casePath
            $matchedExpectation = ($result["Passed"] -eq $case.ExpectedPass)
            if ($matchedExpectation) {
                Write-Output "[SELFTEST PASS] $($case.Name): expected $($case.ExpectedPass), got $($result["Passed"])."
            } else {
                $allPassed = $false
                Write-Output "[SELFTEST FAIL] $($case.Name): expected $($case.ExpectedPass), got $($result["Passed"])."
                Write-NavigatorPublicHttpsAssertionReport -Result $result
            }
        }

        if (-not $allPassed) {
            throw "Navigator public HTTPS PASS assertion self-test failed."
        }

        Write-Output "Navigator public HTTPS PASS assertion self-test PASS."
    } finally {
        Remove-Item -LiteralPath $tempRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
}

if ($SelfTest) {
    Invoke-NavigatorPublicHttpsAssertionSelfTest
    exit 0
}

if ([string]::IsNullOrWhiteSpace($SummaryPath)) {
    throw "Provide -SummaryPath <navigator-public-https-*.summary.log> or use -SelfTest."
}

$result = Test-NavigatorPublicHttpsPassContract -LiteralPath $SummaryPath
Write-NavigatorPublicHttpsAssertionReport -Result $result

if ($result["Passed"]) {
    exit 0
}

exit 1
