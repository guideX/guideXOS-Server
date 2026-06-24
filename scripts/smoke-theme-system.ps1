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

function Find-RawMatch {
    param(
        [string]$LiteralPath,
        [string]$Pattern
    )

    if (-not (Test-Path -LiteralPath $LiteralPath)) {
        return $null
    }

    $text = Get-Content -LiteralPath $LiteralPath -Raw
    $match = [regex]::Match($text, $Pattern, [System.Text.RegularExpressions.RegexOptions]::Singleline)
    if (-not $match.Success) {
        return $null
    }

    [pscustomobject]@{
        Path = $LiteralPath
        LineNumber = 1
        Line = ($match.Value -replace '\s+', ' ').Trim()
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
$themeFieldWindowPaddingMatch = Find-FirstMatch $themeHeader 'windowPadding'
$themeFieldRoundedWindowsMatch = Find-FirstMatch $themeHeader 'roundedWindows'
$themeFieldWindowCornerRadiusMatch = Find-FirstMatch $themeHeader 'windowCornerRadius'
$themeFieldControlPaddingMatch = Find-FirstMatch $themeHeader 'controlPadding'
$themeFieldTitleBarHeightMatch = Find-FirstMatch $themeHeader 'titleBarHeight'
$themeFieldWindowBorderMatch = Find-FirstMatch $themeHeader 'windowBorderThickness'
$themeFieldTitleTextInsetMatch = Find-FirstMatch $themeHeader 'titleTextInset'
$themeFieldTitleButtonGapMatch = Find-FirstMatch $themeHeader 'titleButtonGap'
$themeFieldTaskbarPaddingMatch = Find-FirstMatch $themeHeader 'taskbarPadding'
$themeFieldTaskbarItemPaddingMatch = Find-FirstMatch $themeHeader 'taskbarItemPadding'
$configThemeMatch = Find-FirstMatch $desktopConfig 'TryParseDesktopThemeId\(out\.desktopThemeId\.c_str\(\), &themeId\)|DesktopThemeIdToString\(themeId\)'
$showConfigThemeMatch = Find-FirstMatch $server 'Theme: .*DesktopThemeIdToString\(themeId\)'
$classicOptionMatch = Find-FirstMatch $displayOptions 'DesktopThemeId::Classic, GetDesktopTheme\(DesktopThemeId::Classic\)'
$sciFiOptionMatch = Find-FirstMatch $displayOptions 'DesktopThemeId::SciFi, GetDesktopTheme\(DesktopThemeId::SciFi\)'
$displayThemeMatch = Find-FirstMatch $displayOptions 'desktopThemeId|applySelectedTheme|Theme tab selected|DesktopThemeId::SciFi'
$compositorThemeMatch = Find-FirstMatch $compositor 'GetCurrentDesktopTheme|DesktopThemeIdToString|syncDesktopThemeFromConfig'
$classicRoundedIntentMatch = Find-RawMatch $themeSource 'const DesktopTheme kClassicTheme\{.*?false,\s*false,\s*false,\s*false\s*\};'
$sciFiRoundedIntentMatch = Find-RawMatch $themeSource 'const DesktopTheme kSciFiTheme\{.*?true,\s*true,\s*true,\s*true\s*\};'
$windowRendererRoundedGuardMatch = Find-FirstMatch $windowRenderer 'ShouldUseRoundedWindowChrome|GetWindowChromeCornerRadius'
$compositorRoundedGuardMatch = Find-FirstMatch $compositor 'GetWindowChromeCornerRadius|ShouldUseRoundedWindowChrome'
$compositorTitleBarMatch = Find-FirstMatch $compositor 'theme\.titleBarHeight'
$compositorTitleTextInsetMatch = Find-FirstMatch $compositor 'theme\.titleTextInset'
$compositorTitleButtonGapMatch = Find-FirstMatch $compositor 'theme\.titleButtonGap'
$compositorWindowPaddingMatch = Find-FirstMatch $compositor 'theme\.windowPadding'
$compositorWindowBorderMatch = Find-FirstMatch $compositor 'theme\.windowBorderThickness'
$compositorTaskbarPaddingMatch = Find-FirstMatch $compositor 'theme\.taskbarPadding'
$compositorTaskbarItemPaddingMatch = Find-FirstMatch $compositor 'theme\.taskbarItemPadding'
$compositorButtonHitMatch = Find-FirstMatch $compositor 'my >= btnY && my < btnY \+ btnSize'
$compositorWidgetInsetMatch = Find-FirstMatch $compositor 'wx = mx - topW->x - theme\.windowPadding|wy = my - topW->y - titleBarH - theme\.windowPadding'
$compositorTaskbarSpacingMatch = Find-FirstMatch $compositor 'taskbarItemPadding / 2'
$windowRendererThemeMatch = Find-FirstMatch $windowRenderer 'GetCurrentDesktopTheme'
$windowRendererMetricMatch = Find-FirstMatch $windowRenderer 'titleBarBackground|taskbarBackground|roundedWindows|windowCornerRadius|windowBorderThickness'
$chromeMatch = $compositorThemeMatch
if ($null -eq $chromeMatch) {
    $chromeMatch = $windowRendererThemeMatch
}
$phase2aDocMatch = Find-FirstMatch $planDoc 'Phase 2A added theme-aware chrome metrics|Phase 2A\.1|stabilization-only|rounded clipping remains deferred|shadows remain deferred|blur/glass remains deferred|animations remain deferred|high-DPI and scaling remain deferred'
$phase2bDocMatch = Find-FirstMatch $planDoc 'Phase 2B|guarded rounded-window drawing preview|rounded hit-testing is deferred|Shadows remain deferred|Blur/glass simulation remains deferred|Animations remain deferred|High-DPI and scaling remain deferred'
$phase2b1DocMatch = Find-FirstMatch $planDoc 'Phase 2B\.1|validates and stabilizes|full client background|Classic remains rectangular|bare-metal framebuffer rendering remains rectangular|Rounded hit-testing remains rectangular'

$checks = @(
    [pscustomobject]@{ Name = "theme header exists"; Pass = (Test-Path -LiteralPath $themeHeader); Match = $null },
    [pscustomobject]@{ Name = "theme source exists"; Pass = (Test-Path -LiteralPath $themeSource); Match = $null },
    [pscustomobject]@{ Name = "classic identifier exists"; Pass = $null -ne $classicMatch; Match = $classicMatch },
    [pscustomobject]@{ Name = "sci fi identifier exists"; Pass = $null -ne $sciFiMatch; Match = $sciFiMatch },
    [pscustomobject]@{ Name = "classic default is represented"; Pass = $null -ne $defaultMatch; Match = $defaultMatch },
    [pscustomobject]@{ Name = "sci fi theme data exists"; Pass = $null -ne $themeDataMatch; Match = $themeDataMatch },
    [pscustomobject]@{ Name = "theme field windowPadding exists"; Pass = $null -ne $themeFieldWindowPaddingMatch; Match = $themeFieldWindowPaddingMatch },
    [pscustomobject]@{ Name = "theme field roundedWindows exists"; Pass = $null -ne $themeFieldRoundedWindowsMatch; Match = $themeFieldRoundedWindowsMatch },
    [pscustomobject]@{ Name = "theme field windowCornerRadius exists"; Pass = $null -ne $themeFieldWindowCornerRadiusMatch; Match = $themeFieldWindowCornerRadiusMatch },
    [pscustomobject]@{ Name = "theme field controlPadding exists"; Pass = $null -ne $themeFieldControlPaddingMatch; Match = $themeFieldControlPaddingMatch },
    [pscustomobject]@{ Name = "theme field titleBarHeight exists"; Pass = $null -ne $themeFieldTitleBarHeightMatch; Match = $themeFieldTitleBarHeightMatch },
    [pscustomobject]@{ Name = "theme field windowBorderThickness exists"; Pass = $null -ne $themeFieldWindowBorderMatch; Match = $themeFieldWindowBorderMatch },
    [pscustomobject]@{ Name = "theme field titleTextInset exists"; Pass = $null -ne $themeFieldTitleTextInsetMatch; Match = $themeFieldTitleTextInsetMatch },
    [pscustomobject]@{ Name = "theme field titleButtonGap exists"; Pass = $null -ne $themeFieldTitleButtonGapMatch; Match = $themeFieldTitleButtonGapMatch },
    [pscustomobject]@{ Name = "theme field taskbarPadding exists"; Pass = $null -ne $themeFieldTaskbarPaddingMatch; Match = $themeFieldTaskbarPaddingMatch },
    [pscustomobject]@{ Name = "theme field taskbarItemPadding exists"; Pass = $null -ne $themeFieldTaskbarItemPaddingMatch; Match = $themeFieldTaskbarItemPaddingMatch },
    [pscustomobject]@{ Name = "config theme normalization"; Pass = $null -ne $configThemeMatch; Match = $configThemeMatch },
    [pscustomobject]@{ Name = "desktop.showconfig theme reporting"; Pass = $null -ne $showConfigThemeMatch; Match = $showConfigThemeMatch },
    [pscustomobject]@{ Name = "classic theme option visible"; Pass = $null -ne $classicOptionMatch; Match = $classicOptionMatch },
    [pscustomobject]@{ Name = "sci fi theme option visible"; Pass = $null -ne $sciFiOptionMatch; Match = $sciFiOptionMatch },
    [pscustomobject]@{ Name = "display options theme wiring"; Pass = $null -ne $displayThemeMatch; Match = $displayThemeMatch },
    [pscustomobject]@{ Name = "compositor chrome theme accessor"; Pass = $null -ne $chromeMatch; Match = $chromeMatch },
    [pscustomobject]@{ Name = "classic theme remains rectangular"; Pass = $null -ne $classicRoundedIntentMatch; Match = $classicRoundedIntentMatch },
    [pscustomobject]@{ Name = "sci fi theme intends rounded chrome"; Pass = $null -ne $sciFiRoundedIntentMatch; Match = $sciFiRoundedIntentMatch },
    [pscustomobject]@{ Name = "window renderer rounded helper wired"; Pass = $null -ne $windowRendererRoundedGuardMatch; Match = $windowRendererRoundedGuardMatch },
    [pscustomobject]@{ Name = "compositor rounded helper wired"; Pass = $null -ne $compositorRoundedGuardMatch; Match = $compositorRoundedGuardMatch },
    [pscustomobject]@{ Name = "compositor title bar metric wired"; Pass = $null -ne $compositorTitleBarMatch; Match = $compositorTitleBarMatch },
    [pscustomobject]@{ Name = "compositor title inset wired"; Pass = $null -ne $compositorTitleTextInsetMatch; Match = $compositorTitleTextInsetMatch },
    [pscustomobject]@{ Name = "compositor title button gap wired"; Pass = $null -ne $compositorTitleButtonGapMatch; Match = $compositorTitleButtonGapMatch },
    [pscustomobject]@{ Name = "compositor window padding wired"; Pass = $null -ne $compositorWindowPaddingMatch; Match = $compositorWindowPaddingMatch },
    [pscustomobject]@{ Name = "compositor window border wired"; Pass = $null -ne $compositorWindowBorderMatch; Match = $compositorWindowBorderMatch },
    [pscustomobject]@{ Name = "compositor taskbar padding wired"; Pass = $null -ne $compositorTaskbarPaddingMatch; Match = $compositorTaskbarPaddingMatch },
    [pscustomobject]@{ Name = "compositor taskbar item padding wired"; Pass = $null -ne $compositorTaskbarItemPaddingMatch; Match = $compositorTaskbarItemPaddingMatch },
    [pscustomobject]@{ Name = "compositor title button hit-test wired"; Pass = $null -ne $compositorButtonHitMatch; Match = $compositorButtonHitMatch },
    [pscustomobject]@{ Name = "compositor widget inset wired"; Pass = $null -ne $compositorWidgetInsetMatch; Match = $compositorWidgetInsetMatch },
    [pscustomobject]@{ Name = "compositor taskbar spacing wired"; Pass = $null -ne $compositorTaskbarSpacingMatch; Match = $compositorTaskbarSpacingMatch },
    [pscustomobject]@{ Name = "window renderer theme metrics wired"; Pass = $null -ne $windowRendererMetricMatch; Match = $windowRendererMetricMatch },
    [pscustomobject]@{ Name = "phase 2a docs mention"; Pass = $null -ne $phase2aDocMatch; Match = $phase2aDocMatch },
    [pscustomobject]@{ Name = "phase 2b docs mention"; Pass = $null -ne $phase2bDocMatch; Match = $phase2bDocMatch },
    [pscustomobject]@{ Name = "phase 2b1 docs mention"; Pass = $null -ne $phase2b1DocMatch; Match = $phase2b1DocMatch }
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
