param(
    [switch]$Build,
    [int]$TimeoutSeconds = 40,
    [string]$CandidateBundlePath,
    [string]$CandidateRotationId,
    [switch]$CandidateReviewed
)

$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$ProbeScript = Join-Path $Root "scripts\smoke-navigator-public-https.ps1"

. (Join-Path $Root "scripts\navigator-public-https-reviewed-targets.ps1")

if (-not (Test-Path -LiteralPath $ProbeScript -PathType Leaf)) {
    throw "Navigator public HTTPS probe script not found: $ProbeScript"
}

$reviewedTargets = @(Get-NavigatorPublicHttpsReviewedTargets)
if ($reviewedTargets.Count -le 0) {
    throw "Navigator reviewed public HTTPS allowlist is empty."
}

$results = New-Object System.Collections.Generic.List[object]

foreach ($target in $reviewedTargets) {
    Write-Host "Running reviewed public HTTPS target: $($target.Url)" -ForegroundColor Cyan
    Write-Host "  reason: $($target.Reason)" -ForegroundColor DarkGray

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

    & powershell @args
    $exitCode = $LASTEXITCODE

    $results.Add([pscustomobject]@{
        TargetUrl = [string]$target.Url
        ExitCode = [int]$exitCode
    })
}

Write-Host ""
Write-Host "Reviewed public HTTPS target matrix summary:"
foreach ($result in $results) {
    $marker = switch ($result.ExitCode) {
        0 { "PASS" }
        2 { "SETUP_BLOCKED" }
        3 { "SKIP" }
        default { "FAIL" }
    }
    Write-Host ("  {0}  {1}" -f $marker.PadRight(13), $result.TargetUrl)
}

$failures = @($results | Where-Object { $_.ExitCode -eq 1 })
if ($failures.Count -gt 0) {
    throw "One or more reviewed public HTTPS targets failed."
}

$setupBlocked = @($results | Where-Object { $_.ExitCode -eq 2 })
if ($setupBlocked.Count -gt 0) {
    exit 2
}

$skipped = @($results | Where-Object { $_.ExitCode -eq 3 })
if ($skipped.Count -gt 0) {
    exit 3
}

exit 0
