param(
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)

function Find-FirstMatch {
    param(
        [string]$LiteralPath,
        [string]$Pattern
    )

    if (-not (Test-Path -LiteralPath $LiteralPath)) {
        return $null
    }

    $match = Select-String -LiteralPath $LiteralPath -Pattern $Pattern | Select-Object -First 1
    if ($null -eq $match) {
        return $null
    }

    [pscustomobject]@{
        Path = $LiteralPath
        LineNumber = $match.LineNumber
        Line = $match.Line.Trim()
    }
}

function Emit-Check {
    param(
        [string]$Name,
        [bool]$Pass,
        [object]$Match
    )

    $status = if ($Pass) { "PASS" } else { "FAIL" }
    Write-Host ("{0}: {1}" -f $Name, $status)
    if ($null -ne $Match) {
        $relative = $Match.Path.Substring($Root.Length).TrimStart('\', '/')
        Write-Host ("  evidence: {0}:{1} {2}" -f $relative, $Match.LineNumber, $Match.Line)
    }
}

if (-not $SkipBuild) {
    & (Join-Path $Root "build.bat")
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

$repoRoot = (Resolve-Path -LiteralPath $Root).Path
Write-Host "[ClockTimeSettingsSmoke]"
Write-Host "repo: $repoRoot"
Write-Host "head: $(git -C $repoRoot rev-parse HEAD)"
Write-Host ""

$helper = Join-Path $Root "clock_time_settings.h"
$store = Join-Path $Root "display_options_store.h"
$config = Join-Path $Root "desktop_config.h"
$displayOptions = Join-Path $Root "display_options.cpp"
$compositor = Join-Path $Root "compositor.cpp"
$clock = Join-Path $Root "clock.cpp"

$helperDstMatch = Find-FirstMatch $helper 'Pacific Time \(US & Canada\)|observesUsDst|dstStartUtc|dstEndUtc|formatTimeOfDay|formatLongDate'
$helperFallbackMatch = Find-FirstMatch $helper 'kDefaultTimeZoneId|NormalizeTimeZoneId'
$storeSettingsMatch = Find-FirstMatch $store 'timeZoneId|use24HourTime|NormalizeTimeZoneId'
$configSettingsMatch = Find-FirstMatch $config 'desktop.clock.timeZoneId|desktop.clock.use24HourTime|desktop.clock.timeZone|desktop.clock.use24Hour'
$displayOptionsTabMatch = Find-FirstMatch $displayOptions 'Region/Time|Use 24-hour time|Time Zone:'
$displayOptionsSaveMatch = Find-FirstMatch $displayOptions 'saveClockSettings|applySelectedRegionTime|DisplayOptions applied clock settings'
$compositorTimeMatch = Find-FirstMatch $compositor 'formatTimeOfDay\(now, g_clockDisplaySettings, false\)|formatShortDate\(now, g_clockDisplaySettings'
$clockAppTimeMatch = Find-FirstMatch $clock 'loadClockSettings|formatTimeOfDay\(now_c, g_clockSettings, true\)|formatLongDate\(now_c, g_clockSettings'

$checks = @(
    [pscustomobject]@{ Name = "shared clock helper exists"; Pass = $null -ne $helperDstMatch; Match = $helperDstMatch },
    [pscustomobject]@{ Name = "Pacific fallback and normalization exist"; Pass = $null -ne $helperFallbackMatch; Match = $helperFallbackMatch },
    [pscustomobject]@{ Name = "display-options store persists clock settings"; Pass = $null -ne $storeSettingsMatch; Match = $storeSettingsMatch },
    [pscustomobject]@{ Name = "desktop config persists clock settings"; Pass = $null -ne $configSettingsMatch; Match = $configSettingsMatch },
    [pscustomobject]@{ Name = "display options region/time tab wired"; Pass = $null -ne $displayOptionsTabMatch -and $null -ne $displayOptionsSaveMatch; Match = $(if ($null -ne $displayOptionsTabMatch) { $displayOptionsTabMatch } else { $displayOptionsSaveMatch }) },
    [pscustomobject]@{ Name = "compositor uses shared clock formatting"; Pass = $null -ne $compositorTimeMatch; Match = $compositorTimeMatch },
    [pscustomobject]@{ Name = "clock app uses shared clock formatting"; Pass = $null -ne $clockAppTimeMatch; Match = $clockAppTimeMatch }
)

$failures = 0
foreach ($check in $checks) {
    if (-not $check.Pass) {
        $failures++
    }
    Emit-Check -Name $check.Name -Pass $check.Pass -Match $check.Match
}

if ($failures -gt 0) {
    throw "$failures clock time smoke check(s) failed."
}

Write-Host ""
Write-Host "Clock time settings smoke passed."

