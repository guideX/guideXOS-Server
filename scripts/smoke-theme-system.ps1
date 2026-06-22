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
Write-Host "[ThemeSystemSmoke]"
Write-Host "repo: $repoRoot"
Write-Host "head: $(git -C $repoRoot rev-parse HEAD)"
Write-Host "status: $(git -C $repoRoot status --short)"
Write-Host ""

$themeHeader = Join-Path $Root "desktop_theme.h"
$themeSource = Join-Path $Root "desktop_theme.cpp"
$desktopConfig = Join-Path $Root "desktop_config.h"
$server = Join-Path $Root "server.cpp"
$displayOptions = Join-Path $Root "display_options.cpp"
$compositor = Join-Path $Root "compositor.cpp"
$windowRenderer = Join-Path $Root "window_renderer.h"
$planDoc = Join-Path $Root "docs\theme-system-plan.md"

$classicMatch = Find-FirstMatch $themeSource 'DesktopThemeId::Classic'
$sciFiMatch = Find-FirstMatch $themeSource 'DesktopThemeId::SciFi'
$defaultMatch = Find-FirstMatch $themeSource 'g_currentDesktopThemeId = DesktopThemeId::Classic'
$themeDataMatch = Find-FirstMatch $themeSource '"Sci Fi"'
$themeMetricFieldsMatch = Find-FirstMatch $themeHeader 'windowCornerRadius|windowPadding|controlPadding|titleBarHeight|windowBorderThickness|titleTextInset|titleButtonGap|taskbarPadding|taskbarItemPadding'
$configThemeMatch = Find-FirstMatch $desktopConfig 'TryParseDesktopThemeId\(out\.desktopThemeId\.c_str\(\), &themeId\)|DesktopThemeIdToString\(themeId\)'
$showConfigThemeMatch = Find-FirstMatch $server 'Theme: .*DesktopThemeIdToString\(themeId\)'
$classicOptionMatch = Find-FirstMatch $displayOptions 'DesktopThemeId::Classic, GetDesktopTheme\(DesktopThemeId::Classic\)'
$sciFiOptionMatch = Find-FirstMatch $displayOptions 'DesktopThemeId::SciFi, GetDesktopTheme\(DesktopThemeId::SciFi\)'
$displayThemeMatch = Find-FirstMatch $displayOptions 'desktopThemeId|applySelectedTheme|Theme tab selected|DesktopThemeId::SciFi'
$compositorThemeMatch = Find-FirstMatch $compositor 'GetCurrentDesktopTheme|DesktopThemeIdToString|syncDesktopThemeFromConfig'
$compositorMetricMatch = Find-FirstMatch $compositor 'titleBarHeight|windowPadding|controlPadding|titleTextInset|titleButtonGap|taskbarPadding|taskbarItemPadding|windowBorderThickness'
$windowRendererThemeMatch = Find-FirstMatch $windowRenderer 'GetCurrentDesktopTheme'
$windowRendererMetricMatch = Find-FirstMatch $windowRenderer 'roundedWindows|windowCornerRadius|windowBorderThickness'
$chromeMatch = $compositorThemeMatch
if ($null -eq $chromeMatch) {
    $chromeMatch = $windowRendererThemeMatch
}
$phase2aDocMatch = Find-FirstMatch $planDoc 'Phase 2A|theme-aware chrome metrics|Classic metrics preserve the current guideXOS look'

$checks = @(
    [pscustomobject]@{ Name = "theme header exists"; Pass = (Test-Path -LiteralPath $themeHeader); Match = $null },
    [pscustomobject]@{ Name = "theme source exists"; Pass = (Test-Path -LiteralPath $themeSource); Match = $null },
    [pscustomobject]@{ Name = "classic identifier exists"; Pass = $null -ne $classicMatch; Match = $classicMatch },
    [pscustomobject]@{ Name = "sci fi identifier exists"; Pass = $null -ne $sciFiMatch; Match = $sciFiMatch },
    [pscustomobject]@{ Name = "classic default is represented"; Pass = $null -ne $defaultMatch; Match = $defaultMatch },
    [pscustomobject]@{ Name = "sci fi theme data exists"; Pass = $null -ne $themeDataMatch; Match = $themeDataMatch },
    [pscustomobject]@{ Name = "theme metric fields exist"; Pass = $null -ne $themeMetricFieldsMatch; Match = $themeMetricFieldsMatch },
    [pscustomobject]@{ Name = "config theme normalization"; Pass = $null -ne $configThemeMatch; Match = $configThemeMatch },
    [pscustomobject]@{ Name = "desktop.showconfig theme reporting"; Pass = $null -ne $showConfigThemeMatch; Match = $showConfigThemeMatch },
    [pscustomobject]@{ Name = "classic theme option visible"; Pass = $null -ne $classicOptionMatch; Match = $classicOptionMatch },
    [pscustomobject]@{ Name = "sci fi theme option visible"; Pass = $null -ne $sciFiOptionMatch; Match = $sciFiOptionMatch },
    [pscustomobject]@{ Name = "display options theme wiring"; Pass = $null -ne $displayThemeMatch; Match = $displayThemeMatch },
    [pscustomobject]@{ Name = "compositor chrome theme accessor"; Pass = $null -ne $chromeMatch; Match = $chromeMatch },
    [pscustomobject]@{ Name = "compositor theme metrics wired"; Pass = $null -ne $compositorMetricMatch; Match = $compositorMetricMatch },
    [pscustomobject]@{ Name = "window renderer theme metrics wired"; Pass = $null -ne $windowRendererMetricMatch; Match = $windowRendererMetricMatch },
    [pscustomobject]@{ Name = "phase 2a docs mention"; Pass = $null -ne $phase2aDocMatch; Match = $phase2aDocMatch }
)

$failures = 0
foreach ($check in $checks) {
    if (-not $check.Pass) {
        $failures++
    }
    Emit-Check -Name $check.Name -Pass $check.Pass -Match $check.Match
}

if ($failures -gt 0) {
    throw "$failures theme smoke check(s) failed."
}

Write-Host ""
Write-Host "Theme system smoke passed."
