param(
    [switch]$Build,
    [int]$TimeoutSeconds = 40,
    [string]$CandidateBundlePath,
    [string]$CandidateRotationId,
    [switch]$CandidateReviewed,
    [string]$TargetUrl
)

$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$LogDir = Join-Path $Root "logs"
$ProbeScript = Join-Path $Root "scripts\smoke-navigator-public-https.ps1"
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

. (Join-Path $Root "scripts\navigator-public-https-reviewed-targets.ps1")

function Get-NavigatorPublicHttpsSummaryFields {
    param([Parameter(Mandatory = $true)][string]$LiteralPath)

    $fields = [ordered]@{}
    if (-not (Test-Path -LiteralPath $LiteralPath -PathType Leaf)) {
        return $fields
    }

    foreach ($line in (Get-Content -LiteralPath $LiteralPath)) {
        if ($line -match '^\[NAVIGATOR-PUBLIC-HTTPS\] (?<key>[^=]+)=(?<value>.*)$') {
            $fields[$matches["key"].Trim()] = $matches["value"]
        }
    }
    return $fields
}

function Find-NewNavigatorArtifact {
    param(
        [Parameter(Mandatory = $true)][hashtable]$Existing,
        [Parameter(Mandatory = $true)][string]$Filter,
        [Parameter(Mandatory = $true)][datetime]$StartedAtUtc
    )

    $candidates = @(Get-ChildItem -LiteralPath $LogDir -Filter $Filter -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTimeUtc -Descending)
    if ($candidates.Count -le 0) {
        return $null
    }

    foreach ($candidate in $candidates) {
        if (-not $Existing.ContainsKey($candidate.FullName)) {
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

function Get-NavigatorPublicHttpsTargetSlug {
    param([Parameter(Mandatory = $true)][string]$Url)

    try {
        $uri = [System.Uri]$Url
        $base = ($uri.Host + $uri.AbsolutePath).ToLowerInvariant()
    } catch {
        $base = $Url.ToLowerInvariant()
    }

    $slug = ($base -replace '[^a-z0-9]+', '-').Trim('-')
    if ([string]::IsNullOrWhiteSpace($slug)) {
        return "target"
    }
    return $slug
}

function Get-NavigatorPublicHttpsTargetMatrix {
    if ([string]::IsNullOrWhiteSpace($TargetUrl)) {
        return @(Get-NavigatorPublicHttpsReviewedTargets)
    }

    $requested = $TargetUrl.Trim()
    $match = Find-NavigatorPublicHttpsReviewedTarget -TargetUrl $requested
    if ($null -eq $match) {
        $approvedTargets = [string]::Join(", ", (Get-NavigatorPublicHttpsReviewedTargetUrls))
        throw "Requested -TargetUrl '$requested' is not in the reviewed public HTTPS allowlist ($approvedTargets)."
    }

    return @($match)
}

function Write-NavigatorMatrixOutputLine {
    param([Parameter(Mandatory = $true)][AllowEmptyString()][string]$Line)

    if ($Line -match '^\s*CA source:\s+') {
        Write-Host "  CA source: (redacted in matrix wrapper)"
        return
    }
    if ($Line -match '^Dedicated real public HTTPS smoke CA bundle:\s+') {
        Write-Host "Dedicated real public HTTPS smoke CA bundle: (redacted in matrix wrapper)"
        return
    }
    if ($Line -match 'public_ca_source_path=') {
        return
    }
    Write-Host $Line
}

if (-not (Test-Path -LiteralPath $ProbeScript -PathType Leaf)) {
    throw "Navigator public HTTPS probe script not found: $ProbeScript"
}

$reviewedTargets = @(Get-NavigatorPublicHttpsTargetMatrix)
if ($reviewedTargets.Count -le 0) {
    throw "Navigator reviewed public HTTPS allowlist is empty."
}

$allowlistName = Get-NavigatorPublicHttpsReviewedAllowlistName
$matrixStamp = Get-Date -Format "yyyyMMdd-HHmmss"
$matrixSummaryLog = Join-Path $LogDir "navigator-public-https-allowlist-$matrixStamp.summary.log"
$results = New-Object System.Collections.Generic.List[object]

foreach ($target in $reviewedTargets) {
    Write-Host "Running reviewed public HTTPS target: $($target.Url)" -ForegroundColor Cyan
    Write-Host "  allowlist: $allowlistName" -ForegroundColor DarkGray
    Write-Host "  reason: $($target.Reason)" -ForegroundColor DarkGray

    $existingSummaries = @{}
    $existingEvidence = @{}
    Get-ChildItem -LiteralPath $LogDir -Filter "navigator-public-https-*.summary.log" -ErrorAction SilentlyContinue | ForEach-Object {
        $existingSummaries[$_.FullName] = $true
    }
    Get-ChildItem -LiteralPath $LogDir -Filter "navigator-public-https-*.evidence.json" -ErrorAction SilentlyContinue | ForEach-Object {
        $existingEvidence[$_.FullName] = $true
    }

    $args = @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", $ProbeScript,
        "-TimeoutSeconds", $TimeoutSeconds.ToString(),
        "-TargetUrl", $target.Url
    )
    if ($Build) {
        $args += "-Build"
    }
    if (-not [string]::IsNullOrWhiteSpace($CandidateBundlePath)) {
        $args += @("-CandidateBundlePath", $CandidateBundlePath)
    }
    if (-not [string]::IsNullOrWhiteSpace($CandidateRotationId)) {
        $args += @("-CandidateRotationId", $CandidateRotationId)
    }
    if ($CandidateReviewed) {
        $args += "-CandidateReviewed"
    }

    $startedAtUtc = [datetime]::UtcNow
    $savedErrorActionPreference = $ErrorActionPreference
    try {
        # The probe can emit successful build warnings on stderr. Capture them and
        # use the native exit code to classify the result.
        $ErrorActionPreference = "Continue"
        $probeOutput = @(& powershell.exe @args 2>&1)
        $exitCode = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $savedErrorActionPreference
    }

    foreach ($line in $probeOutput) {
        Write-NavigatorMatrixOutputLine -Line "$line"
    }

    $summaryArtifact = Find-NewNavigatorArtifact -Existing $existingSummaries -Filter "navigator-public-https-*.summary.log" -StartedAtUtc $startedAtUtc
    $evidenceArtifact = Find-NewNavigatorArtifact -Existing $existingEvidence -Filter "navigator-public-https-*.evidence.json" -StartedAtUtc $startedAtUtc
    $summaryFields = if ($summaryArtifact) { Get-NavigatorPublicHttpsSummaryFields -LiteralPath $summaryArtifact.FullName } else { [ordered]@{} }

    $resultMarker = switch ($exitCode) {
        0 { "PASS" }
        2 { "SETUP_BLOCKED" }
        3 { "SKIP" }
        default { "FAIL" }
    }
    if ($summaryFields.Contains("result_marker") -and -not [string]::IsNullOrWhiteSpace([string]$summaryFields["result_marker"])) {
        $resultMarker = [string]$summaryFields["result_marker"]
    }

    $slug = Get-NavigatorPublicHttpsTargetSlug -Url $target.Url
    $copiedSummaryPath = $null
    $copiedEvidencePath = $null
    if ($summaryArtifact) {
        $copiedSummaryPath = Join-Path $LogDir "navigator-public-https-allowlist-$matrixStamp-$slug.summary.log"
        Copy-Item -LiteralPath $summaryArtifact.FullName -Destination $copiedSummaryPath -Force
    }
    if ($evidenceArtifact) {
        $copiedEvidencePath = Join-Path $LogDir "navigator-public-https-allowlist-$matrixStamp-$slug.evidence.json"
        Copy-Item -LiteralPath $evidenceArtifact.FullName -Destination $copiedEvidencePath -Force
    }

    $results.Add([pscustomobject]@{
        TargetUrl = [string]$target.Url
        Reason = [string]$target.Reason
        ExitCode = [int]$exitCode
        ResultMarker = [string]$resultMarker
        SummaryPath = $copiedSummaryPath
        EvidencePath = $copiedEvidencePath
        Allowlist = $allowlistName
    })

    Write-Host "  target result: $resultMarker"
    if ($copiedSummaryPath) {
        Write-Host "  summary artifact: $copiedSummaryPath"
    } else {
        Write-Host "  summary artifact: (not found)"
    }
    if ($copiedEvidencePath) {
        Write-Host "  evidence artifact: $copiedEvidencePath"
    } else {
        Write-Host "  evidence artifact: (not found)"
    }
    Write-Host ""
}

$aggregateResult = "PASS"
if (@($results | Where-Object { $_.ResultMarker -eq "FAIL" -or $_.ExitCode -eq 1 }).Count -gt 0) {
    $aggregateResult = "FAIL"
} elseif (@($results | Where-Object { $_.ResultMarker -eq "SETUP_BLOCKED" -or $_.ExitCode -eq 2 }).Count -gt 0) {
    $aggregateResult = "SETUP_BLOCKED"
} elseif (@($results | Where-Object { $_.ResultMarker -eq "SKIP" -or $_.ExitCode -eq 3 }).Count -gt 0) {
    $aggregateResult = "SKIP"
}

$summaryLines = @(
    "[NAVIGATOR-PUBLIC-HTTPS-ALLOWLIST] reviewed_target_allowlist=$allowlistName",
    "[NAVIGATOR-PUBLIC-HTTPS-ALLOWLIST] matrix_target_count=$($results.Count)",
    "[NAVIGATOR-PUBLIC-HTTPS-ALLOWLIST] aggregate_result=$aggregateResult"
)
foreach ($result in $results) {
    $summaryLines += "[NAVIGATOR-PUBLIC-HTTPS-ALLOWLIST] target_url=$($result.TargetUrl)"
    $summaryLines += "[NAVIGATOR-PUBLIC-HTTPS-ALLOWLIST] target_result=$($result.ResultMarker)"
    $summaryLines += "[NAVIGATOR-PUBLIC-HTTPS-ALLOWLIST] target_summary_log=$(if ($result.SummaryPath) { $result.SummaryPath } else { '(not-found)' })"
    $summaryLines += "[NAVIGATOR-PUBLIC-HTTPS-ALLOWLIST] target_evidence_json=$(if ($result.EvidencePath) { $result.EvidencePath } else { '(not-found)' })"
}
[System.IO.File]::WriteAllLines($matrixSummaryLog, $summaryLines, [System.Text.Encoding]::ASCII)

Write-Host "Reviewed public HTTPS target matrix summary:"
foreach ($result in $results) {
    Write-Host ("  {0}  {1}" -f $result.ResultMarker.PadRight(13), $result.TargetUrl)
}
Write-Host "  aggregate result: $aggregateResult"
Write-Host "  allowlist summary log: $matrixSummaryLog"

switch ($aggregateResult) {
    "FAIL" { exit 1 }
    "SETUP_BLOCKED" { exit 2 }
    "SKIP" { exit 3 }
    default { exit 0 }
}
