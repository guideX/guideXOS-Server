param(
    [switch]$Build,
    [int]$TimeoutSeconds = 40
)

$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$LogDir = Join-Path $Root "logs"
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

. (Join-Path $Root "scripts\process_environment.ps1")
Normalize-ProcessEnvironment
. (Join-Path $Root "scripts\navigator_smoke_repo_hygiene.ps1")

$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$dedicatedSerialLog = Join-Path $LogDir "navigator-public-https-$stamp.serial.log"
$dedicatedSummaryLog = Join-Path $LogDir "navigator-public-https-$stamp.summary.log"
$kernelScenarioName = "production_public_pilot_enabled"
$kernelSmokeScript = Join-Path $Root "scripts\smoke-navigator-kernel.ps1"
$publicLocalBundlePath = Join-Path $Root "scripts\fixtures\public-roots\ca-bundle.pem.local"
$publicExampleBundlePath = Join-Path $Root "scripts\fixtures\public-roots\ca-bundle.pem.example"
$publicProbeDefaultTarget = "https://sha256.badssl.com/"
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

$publicSmokeEnvNames = @(
    "GXOS_NAVIGATOR_SMOKE_ENABLE_REAL_PUBLIC_HTTPS",
    "GXOS_NAVIGATOR_SMOKE_REQUIRE_REAL_PUBLIC_HTTPS",
    "GXOS_NAVIGATOR_SMOKE_REAL_PUBLIC_HTTPS_URL",
    "GXOS_NAVIGATOR_SMOKE_REAL_PUBLIC_HTTPS_TARGET",
    "GXOS_NAVIGATOR_SMOKE_REAL_PUBLIC_HTTPS_CA_BUNDLE_SOURCE"
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

function Get-NavigatorRealPublicProbeTarget {
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

function Test-NavigatorRealPublicProbeTarget {
    param([Parameter(Mandatory = $true)][string]$Target)

    try {
        $uri = [System.Uri]$Target
    } catch {
        return [pscustomobject]@{
            Valid = $false
            Error = "Target is not a valid absolute URL."
            Host = $null
        }
    }

    if (-not $uri.IsAbsoluteUri) {
        return [pscustomobject]@{
            Valid = $false
            Error = "Target must be an absolute URL."
            Host = $null
        }
    }
    if (-not [string]::Equals($uri.Scheme, "https", [System.StringComparison]::OrdinalIgnoreCase)) {
        return [pscustomobject]@{
            Valid = $false
            Error = "Target must use https://."
            Host = $null
        }
    }
    if ([string]::IsNullOrWhiteSpace($uri.Host)) {
        return [pscustomobject]@{
            Valid = $false
            Error = "Target must include a DNS hostname."
            Host = $null
        }
    }

    $parsedIp = $null
    if ([System.Net.IPAddress]::TryParse($uri.Host, [ref]$parsedIp)) {
        return [pscustomobject]@{
            Valid = $false
            Error = "Target must use a DNS hostname, not a numeric IP literal."
            Host = $uri.Host
        }
    }

    return [pscustomobject]@{
        Valid = $true
        Error = $null
        Host = $uri.Host
    }
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
            Resolution = "ignored-local-file"
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

    return @(
        $Reason,
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

function Write-NavigatorPublicHttpsConsoleSummary {
    param(
        [Parameter(Mandatory = $true)][string]$FinalResult,
        [Parameter(Mandatory = $true)][int]$ExitCode,
        [Parameter(Mandatory = $true)][System.Collections.Specialized.OrderedDictionary]$Fields,
        [string[]]$Notes = @()
    )

    Write-Host "Dedicated real public HTTPS smoke summary:"
    Write-Host "  target: $($Fields["target_url"])"
    Write-Host "  CA source: $($Fields["public_ca_source_path"]) [$($Fields["public_ca_resolution"])]"
    Write-Host "  CA source marker: $($Fields["public_ca_source_marker"])"
    Write-Host "  CA bytes: $($Fields["public_ca_bytes"])"
    Write-Host "  parsed cert count: $($Fields["public_ca_parsed_certs"])"
    Write-Host "  DNS result: $($Fields["dns_result"])"
    Write-Host "  TCP result: $($Fields["tcp_result"])"
    Write-Host "  TLS result: $($Fields["tls_result"])"
    Write-Host "  certificate validation: $($Fields["certificate_validation_result"])"
    Write-Host "  hostname validation: $($Fields["hostname_validation_result"])"
    Write-Host "  HTTP status: $($Fields["http_status"])"
    Write-Host "  unsupported content reason: $($Fields["unsupported_reason"])"
    Write-Host "  plaintext_fallback: $($Fields["plaintext_fallback"])"
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

$exitCode = 1
$finalResult = "FAIL"
$kernelSerialPath = $null
$kernelSerialOutput = $null
$fields = [ordered]@{
    target_url = "(unknown)"
    target_host = "(unknown)"
    public_ca_resolution = "(unknown)"
    public_ca_source_marker = "(unknown)"
    public_ca_source_path = "(unknown)"
    public_ca_bytes = "(unknown)"
    public_ca_parsed_certs = "(unknown)"
    dns_result = "not-attempted"
    tcp_result = "not-attempted"
    tls_result = "not-attempted"
    certificate_validation_result = "not-attempted"
    hostname_validation_result = "not-attempted"
    verify_flags = "(not-attempted)"
    sni_host = "(not-attempted)"
    http_status = "(not-attempted)"
    content_type = "(not-attempted)"
    content_encoding = "(not-attempted)"
    unsupported_reason = "(not-attempted)"
    plaintext_fallback = "no"
    skip_reason = "(none)"
    failure_reason = "(none)"
    probe_result = "SKIP"
}

try {
    $target = Get-NavigatorRealPublicProbeTarget
    $targetValidation = Test-NavigatorRealPublicProbeTarget -Target $target
    $caResolution = Get-NavigatorRealPublicProbeCaBundleResolution

    if ($caResolution.EnvBundleProvided -and $caResolution.LocalBundleAvailable) {
        Write-Host "Using public CA bundle from GXOS_NAVIGATOR_SMOKE_REAL_PUBLIC_HTTPS_CA_BUNDLE_SOURCE and ignoring the local fallback file."
    }

    $fields["target_url"] = $target
    $fields["target_host"] = $(if ($targetValidation.Host) { $targetValidation.Host } else { "(none)" })
    $fields["public_ca_resolution"] = $caResolution.Resolution
    $fields["public_ca_source_marker"] = $caResolution.Resolution
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
        Write-NavigatorPublicHttpsConsoleSummary -FinalResult $finalResult -ExitCode $exitCode -Fields $fields -Notes $notes
        exit $exitCode
    }
    $fields["public_ca_source_path"] = $bundleInfo.Path
    $fields["public_ca_bytes"] = $bundleInfo.Bytes
    $fields["public_ca_parsed_certs"] = $bundleInfo.ParsedCertCount

    Write-Host "Dedicated real public HTTPS smoke target: $target"
    Write-Host "Dedicated real public HTTPS smoke CA bundle: $($bundleInfo.Path) [$($caResolution.Resolution)]"

    Set-ProcessEnvValue -Name "GXOS_NAVIGATOR_SMOKE_ENABLE_REAL_PUBLIC_HTTPS" -Value "1"
    Set-ProcessEnvValue -Name "GXOS_NAVIGATOR_SMOKE_REQUIRE_REAL_PUBLIC_HTTPS" -Value "1"
    Set-ProcessEnvValue -Name "GXOS_NAVIGATOR_SMOKE_REAL_PUBLIC_HTTPS_URL" -Value $target
    Set-ProcessEnvValue -Name "GXOS_NAVIGATOR_SMOKE_REAL_PUBLIC_HTTPS_TARGET" -Value $target
    Set-ProcessEnvValue -Name "GXOS_NAVIGATOR_SMOKE_REAL_PUBLIC_HTTPS_CA_BUNDLE_SOURCE" -Value $bundleInfo.Path

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
        "-ScenarioFilter", $kernelScenarioName
    )
    if ($Build) {
        $kernelArgs += "-Build"
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
        Write-NavigatorPublicHttpsConsoleSummary -FinalResult $finalResult -ExitCode $exitCode -Fields $fields -Notes $notes
        exit $exitCode
    }

    foreach ($fieldName in @(
        "public_ca_bundle_source",
        "public_ca_bytes",
        "public_ca_parsed_certs",
        "dns_result",
        "tcp_result",
        "tls_result",
        "certificate_validation_result",
        "hostname_validation_result",
        "verify_flags",
        "sni_host",
        "http_status",
        "content_type",
        "content_encoding",
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
    Write-NavigatorPublicHttpsConsoleSummary -FinalResult $finalResult -ExitCode $exitCode -Fields $fields -Notes $notes
    Write-Host "Dedicated real public HTTPS smoke FAIL. Summary log: $dedicatedSummaryLog" -ForegroundColor Red
    exit $exitCode
} finally {
    Restore-NavigatorPublicHttpsEnvironment
}
