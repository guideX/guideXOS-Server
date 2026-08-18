param(
    [Parameter(Mandatory = $true)][string]$BundlePath,
    [Parameter(Mandatory = $true)]
    [ValidateSet(
        "smoke-fixture",
        "validated-fixture",
        "user-dev",
        "production-source",
        "production-public-source",
        "production-public-probe-merged",
        "shipped-public",
        "shipped-root-candidate"
    )]
    [string]$BundleType,
    [string]$OutputManifestPath,
    [string]$SourceDescription,
    [string]$RotationId,
    [string]$GeneratedUtc,
    [ValidateSet("auto", "yes", "no")]
    [string]$ProductionReady = "auto"
)

$ErrorActionPreference = "Stop"
$NavigatorCaBundleSizeCapBytes = 512KB

function Get-NavigatorCaBundleProfile {
    param([Parameter(Mandatory = $true)][string]$Type)

    switch ($Type) {
        "smoke-fixture" {
            return [pscustomobject]@{
                ProductionReady = "no"
                TestOnly = "yes"
            }
        }
        "validated-fixture" {
            return [pscustomobject]@{
                ProductionReady = "no"
                TestOnly = "yes"
            }
        }
        "user-dev" {
            return [pscustomobject]@{
                ProductionReady = "no"
                TestOnly = "no"
            }
        }
        "production-source" {
            return [pscustomobject]@{
                ProductionReady = "no"
                TestOnly = "no"
            }
        }
        "production-public-source" {
            return [pscustomobject]@{
                ProductionReady = "yes"
                TestOnly = "no"
            }
        }
        "production-public-probe-merged" {
            return [pscustomobject]@{
                ProductionReady = "yes"
                TestOnly = "no"
            }
        }
        "shipped-public" {
            return [pscustomobject]@{
                ProductionReady = "yes"
                TestOnly = "no"
            }
        }
        "shipped-root-candidate" {
            return [pscustomobject]@{
                ProductionReady = "no"
                TestOnly = "no"
            }
        }
        default {
            throw "Unsupported bundle type: $Type"
        }
    }
}

function Get-NavigatorManifestOutputPath {
    param([Parameter(Mandatory = $true)][string]$LiteralPath)

    $directory = Split-Path -Parent $LiteralPath
    $filename = [System.IO.Path]::GetFileName($LiteralPath)

    if ($filename.EndsWith(".pem", [System.StringComparison]::OrdinalIgnoreCase)) {
        $manifestName = $filename.Substring(0, $filename.Length - 4) + ".manifest"
    } else {
        $manifestName = $filename + ".manifest"
    }

    return Join-Path $directory $manifestName
}

function ConvertTo-NavigatorSubjectSummary {
    param([Parameter(Mandatory = $true)][string[]]$Subjects)

    $uniqueSubjects = New-Object System.Collections.Generic.List[string]
    foreach ($subject in $Subjects) {
        if ([string]::IsNullOrWhiteSpace($subject)) {
            continue
        }
        if (-not $uniqueSubjects.Contains($subject)) {
            $null = $uniqueSubjects.Add($subject)
        }
    }

    if ($uniqueSubjects.Count -le 0) {
        return $null
    }

    $maxSubjects = 4
    $selected = @($uniqueSubjects | Select-Object -First $maxSubjects)
    $summary = ($selected -join " | ")
    if ($uniqueSubjects.Count -gt $maxSubjects) {
        $summary += " | +$($uniqueSubjects.Count - $maxSubjects) more"
    }
    return $summary
}

function Get-NavigatorCaBundleManifest {
    param(
        [Parameter(Mandatory = $true)][string]$LiteralPath,
        [Parameter(Mandatory = $true)][string]$Type,
        [Parameter(Mandatory = $true)][string]$Source,
        [AllowNull()][string]$ManifestGeneratedUtc
    )

    if (-not (Test-Path -LiteralPath $LiteralPath -PathType Leaf)) {
        throw "CA bundle not found: $LiteralPath"
    }

    $item = Get-Item -LiteralPath $LiteralPath
    if ($item.Length -le 0) {
        throw "CA bundle is empty: $LiteralPath"
    }
    if ($item.Length -gt $NavigatorCaBundleSizeCapBytes) {
        throw "CA bundle exceeds the 512 KiB safety cap: $LiteralPath"
    }

    $text = [System.IO.File]::ReadAllText($item.FullName, [System.Text.Encoding]::ASCII)
    $beginCount = ([regex]::Matches($text, '-----BEGIN CERTIFICATE-----')).Count
    $endCount = ([regex]::Matches($text, '-----END CERTIFICATE-----')).Count
    if ($beginCount -ne $endCount) {
        throw "CA bundle contains mismatched PEM certificate boundaries: $LiteralPath"
    }

    $matches = [regex]::Matches(
        $text,
        '-----BEGIN CERTIFICATE-----(?<body>[\s\S]*?)-----END CERTIFICATE-----',
        [System.Text.RegularExpressions.RegexOptions]::Singleline)
    if ($matches.Count -le 0) {
        throw "CA bundle does not contain any PEM certificates: $LiteralPath"
    }

    $subjects = New-Object System.Collections.Generic.List[string]
    $notBeforeValues = New-Object System.Collections.Generic.List[datetime]
    $notAfterValues = New-Object System.Collections.Generic.List[datetime]

    foreach ($match in $matches) {
        $base64 = ($match.Groups["body"].Value -replace '\s', '')
        if ([string]::IsNullOrWhiteSpace($base64)) {
            throw "CA bundle contains an empty PEM certificate block: $LiteralPath"
        }

        try {
            $bytes = [Convert]::FromBase64String($base64)
            $cert = [System.Security.Cryptography.X509Certificates.X509Certificate2]::new($bytes)
        } catch {
            throw "CA bundle contains a malformed PEM certificate: $LiteralPath"
        }

        try {
            if (-not [string]::IsNullOrWhiteSpace($cert.Subject)) {
                $null = $subjects.Add($cert.Subject)
            }
            $null = $notBeforeValues.Add($cert.NotBefore.ToUniversalTime())
            $null = $notAfterValues.Add($cert.NotAfter.ToUniversalTime())
        } finally {
            $cert.Dispose()
        }
    }

    $hash = [System.Security.Cryptography.SHA256]::Create()
    try {
        $sha256 = ([BitConverter]::ToString($hash.ComputeHash([System.IO.File]::ReadAllBytes($item.FullName)))).Replace("-", "").ToLowerInvariant()
    } finally {
        $hash.Dispose()
    }

    $profile = Get-NavigatorCaBundleProfile -Type $Type
    $generatedUtcText = [datetime]::UtcNow.ToString("o")
    if (-not [string]::IsNullOrWhiteSpace($ManifestGeneratedUtc)) {
        try {
            $generatedUtcText = ([datetime]::Parse(
                    $ManifestGeneratedUtc.Trim(),
                    [Globalization.CultureInfo]::InvariantCulture,
                    [Globalization.DateTimeStyles]::AssumeUniversal -bor [Globalization.DateTimeStyles]::AdjustToUniversal)).ToString("o")
        } catch {
            throw "GeneratedUtc must be a valid UTC timestamp: $ManifestGeneratedUtc"
        }
    }
    if ($ProductionReady -ne "auto") {
        if ($Type -eq "shipped-root-candidate") {
            $profile.ProductionReady = $ProductionReady
        } elseif ($profile.ProductionReady -ne $ProductionReady) {
            throw "Bundle type '$Type' has a fixed production_ready=$($profile.ProductionReady) contract; refusing override to '$ProductionReady'."
        }
    }
    $subjectSummary = ConvertTo-NavigatorSubjectSummary -Subjects @($subjects)
    $notBeforeMin = $null
    if ($notBeforeValues.Count -gt 0) {
        $notBeforeMin = ($notBeforeValues | Measure-Object -Minimum).Minimum.ToString("o")
    }
    $notAfterMax = $null
    if ($notAfterValues.Count -gt 0) {
        $notAfterMax = ($notAfterValues | Measure-Object -Maximum).Maximum.ToString("o")
    }

    return [ordered]@{
        schema_version = "guidexos.navigator.ca-bundle-manifest.v0.1"
        bundle_type = $Type
        source = $Source
        generated_utc = $generatedUtcText
        root_count = [int]$matches.Count
        pem_bytes = [int64]$item.Length
        sha256 = $sha256
        subject_summary = $subjectSummary
        not_before_min = $notBeforeMin
        not_after_max = $notAfterMax
        production_ready = $profile.ProductionReady
        test_only = $profile.TestOnly
        rotation_id = $(if ([string]::IsNullOrWhiteSpace($RotationId)) { "sha256:$sha256" } else { $RotationId.Trim() })
    }
}

if ([string]::IsNullOrWhiteSpace($OutputManifestPath)) {
    $OutputManifestPath = Get-NavigatorManifestOutputPath -LiteralPath $BundlePath
}
if ([string]::IsNullOrWhiteSpace($SourceDescription)) {
    $SourceDescription = "file:" + [System.IO.Path]::GetFileName($BundlePath)
}

$bundleFullPath = [System.IO.Path]::GetFullPath($BundlePath)
$manifestFullPath = [System.IO.Path]::GetFullPath($OutputManifestPath)
$manifest = Get-NavigatorCaBundleManifest -LiteralPath $bundleFullPath -Type $BundleType -Source $SourceDescription.Trim() -ManifestGeneratedUtc $GeneratedUtc

$manifestDirectory = Split-Path -Parent $manifestFullPath
if (-not [string]::IsNullOrWhiteSpace($manifestDirectory)) {
    New-Item -ItemType Directory -Force -Path $manifestDirectory | Out-Null
}

$json = $manifest | ConvertTo-Json -Depth 4
[System.IO.File]::WriteAllText($manifestFullPath, $json + [Environment]::NewLine, [System.Text.Encoding]::ASCII)

Write-Output "Navigator CA bundle manifest generated:"
Write-Output "  bundle: $bundleFullPath"
Write-Output "  manifest: $manifestFullPath"
Write-Output "  bundle_type: $($manifest["bundle_type"])"
Write-Output "  source: $($manifest["source"])"
Write-Output "  root_count: $($manifest["root_count"])"
Write-Output "  sha256: $($manifest["sha256"])"
Write-Output "  production_ready: $($manifest["production_ready"])"
Write-Output "  test_only: $($manifest["test_only"])"
