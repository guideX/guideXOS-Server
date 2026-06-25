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
$themeFieldSoftShadowIntentMatch = Find-FirstMatch $themeHeader 'softShadowIntent'
$themeFieldDesktopBackgroundMatch = Find-FirstMatch $themeHeader 'desktopBackground'
$configThemeMatch = Find-FirstMatch $desktopConfig 'TryParseDesktopThemeId\(out\.desktopThemeId\.c_str\(\), &themeId\)|DesktopThemeIdToString\(themeId\)'
$showConfigThemeMatch = Find-FirstMatch $server 'Theme: .*DesktopThemeIdToString\(themeId\)'
$classicOptionMatch = Find-FirstMatch $displayOptions 'DesktopThemeId::Classic, GetDesktopTheme\(DesktopThemeId::Classic\)'
$sciFiOptionMatch = Find-FirstMatch $displayOptions 'DesktopThemeId::SciFi, GetDesktopTheme\(DesktopThemeId::SciFi\)'
$displayThemeMatch = Find-FirstMatch $displayOptions 'desktopThemeId|applySelectedTheme|Theme tab selected|DesktopThemeId::SciFi'
$compositorThemeMatch = Find-FirstMatch $compositor 'GetCurrentDesktopTheme|DesktopThemeIdToString|syncDesktopThemeFromConfig'
$classicRoundedIntentMatch = Find-RawMatch $themeSource 'const DesktopTheme kClassicTheme\{.*?false,\s*false,\s*false,\s*false\s*\};'
$sciFiRoundedIntentMatch = Find-RawMatch $themeSource 'const DesktopTheme kSciFiTheme\{.*?true,\s*true,\s*true,\s*true\s*\};'
$classicShadowOffMatch = Find-RawMatch $themeSource 'const DesktopTheme kClassicTheme\{.*?false,\s*false,\s*false,\s*false\s*\};'
$sciFiShadowOnMatch = Find-RawMatch $themeSource 'const DesktopTheme kSciFiTheme\{.*?true,\s*true,\s*true,\s*true\s*\};'
$windowRendererRoundedGuardMatch = Find-FirstMatch $windowRenderer 'ShouldUseRoundedWindowChrome|GetWindowChromeCornerRadius'
$windowRendererShadowGuardMatch = Find-FirstMatch $windowRenderer 'ShouldDrawWindowShadow|GetWindowShadowOffset|GetWindowShadowColor|DrawWindowShadow'
$compositorRoundedGuardMatch = Find-FirstMatch $compositor 'GetWindowChromeCornerRadius|ShouldUseRoundedWindowChrome'
$compositorShadowCallMatch = Find-FirstMatch $compositor 'DrawWindowShadow'
$compositorTitleBarMatch = Find-FirstMatch $compositor 'theme\.titleBarHeight'
$compositorTitleTextInsetMatch = Find-FirstMatch $compositor 'theme\.titleTextInset'
$compositorTitleButtonGapMatch = Find-FirstMatch $compositor 'theme\.titleButtonGap'
$compositorWindowPaddingMatch = Find-FirstMatch $compositor 'theme\.windowPadding'
$compositorWindowBorderMatch = Find-FirstMatch $compositor 'theme\.windowBorderThickness'
$compositorTaskbarPaddingMatch = Find-FirstMatch $compositor 'theme\.taskbarPadding'
$compositorTaskbarItemPaddingMatch = Find-FirstMatch $compositor 'theme\.taskbarItemPadding'
$compositorTaskbarSciFiMatch = Find-FirstMatch $compositor 'theme\.id == DesktopThemeId::SciFi'
$compositorDesktopBackgroundMatch = Find-FirstMatch $compositor 'hostedDesktopTopColor|theme\.desktopBackground'
$compositorTaskbarSurfaceMatch = Find-FirstMatch $compositor 'hostedTaskbarSurfaceColor|hostedTaskbarHighlightColor|hostedTaskbarBorderColor'
$compositorPanelSurfaceMatch = Find-FirstMatch $compositor 'hostedPanelSurfaceColor|hostedPanelBorderColor'
$compositorTaskbarItemMatch = Find-FirstMatch $compositor 'hostedTaskbarItemFillColor|hostedTaskbarItemBorderColor'
$compositorStartButtonMatch = Find-FirstMatch $compositor 'hostedStartButtonFillColor|hostedStartButtonBorderColor'
$compositorButtonHitMatch = Find-FirstMatch $compositor 'my >= btnY && my < btnY \+ btnSize'
$compositorWidgetInsetMatch = Find-FirstMatch $compositor 'wx = mx - topW->x - theme\.windowPadding|wy = my - topW->y - titleBarH - theme\.windowPadding'
$compositorTaskbarSpacingMatch = Find-FirstMatch $compositor 'taskbarItemPadding / 2'
$windowRendererThemeMatch = Find-FirstMatch $windowRenderer 'GetCurrentDesktopTheme'
$windowRendererMetricMatch = Find-FirstMatch $windowRenderer 'titleBarBackground|taskbarBackground|roundedWindows|windowCornerRadius|windowBorderThickness'
$windowRendererBlendMatch = Find-FirstMatch $windowRenderer 'BlendThemeColor|ToColorRef'
$windowRendererTitleBarPolishMatch = Find-FirstMatch $windowRenderer 'BlendThemeColor\(theme\.titleBarBackground, theme\.accent, 48\)|BlendThemeColor\(theme\.taskbarBackground, theme\.mutedAccent, 28\)'
$windowRendererBorderPolishMatch = Find-FirstMatch $windowRenderer 'BlendThemeColor\(theme\.windowBorder, theme\.accent, 72\)|BlendThemeColor\(theme\.windowBorder, theme\.mutedAccent, 20\)'
$chromeMatch = $compositorThemeMatch
if ($null -eq $chromeMatch) {
    $chromeMatch = $windowRendererThemeMatch
}
$phase2aDocMatch = Find-FirstMatch $planDoc 'Phase 2A added theme-aware chrome metrics|Phase 2A\.1|stabilization-only|rounded clipping remains deferred|shadows remain deferred|blur/glass remains deferred|animations remain deferred|high-DPI and scaling remain deferred'
$phase2bDocMatch = Find-FirstMatch $planDoc 'Phase 2B|guarded rounded-window drawing preview|rounded hit-testing is deferred|Shadows remain deferred|Blur/glass simulation remains deferred|Animations remain deferred|High-DPI and scaling remain deferred'
$phase2b1DocMatch = Find-FirstMatch $planDoc 'Phase 2B\.1|validates and stabilizes|full client background|Classic remains rectangular|bare-metal framebuffer rendering remains rectangular|Rounded hit-testing remains rectangular'
$phase2cHeadingMatch = Find-FirstMatch $planDoc '## Phase 2C'
$phase2cBodyMatch = Find-FirstMatch $planDoc 'Phase 2C adds a conservative Sci Fi border and highlight polish layer without changing the core compositor model\.'
$phase2c1HeadingMatch = Find-FirstMatch $planDoc '## Phase 2C\.1'
$phase2c1BodyMatch = Find-FirstMatch $planDoc 'Phase 2C\.1 is a stabilization-only review pass for the Sci Fi chrome polish\.'
$phase2dHeadingMatch = Find-FirstMatch $planDoc '## Phase 2D'
$phase2dBodyMatch = Find-FirstMatch $planDoc 'hosted-only Sci Fi soft shadow preview|softShadowIntent|No blur or glass simulation was added|No animation was added|The hosted paint path redraws the full client frame before the window stack'
$phase2d1HeadingMatch = Find-FirstMatch $planDoc '## Phase 2D\.1'
$phase2d1BodyMatch = Find-FirstMatch $planDoc 'stabilization and review pass for the hosted Sci Fi shadow preview|full client frame before the window stack|Classic remains shadow-free|Bare-metal framebuffer rendering remains unchanged|rounded hit-testing and rounded client clipping remain rectangular and deferred|No blur, glass, animation, or high-DPI work was added|rounded chrome preview guard remains separate from the shadow guard'
$phase2eHeadingMatch = Find-FirstMatch $planDoc '## Phase 2E'
$phase2eBodyMatch = Find-FirstMatch $planDoc 'Phase 2E adds conservative Sci Fi desktop and taskbar surface polish|desktopBackground|taskbar surface|Classic remains the default theme|Classic remains rectangular and shadow-free|Bare-metal framebuffer rendering remains unchanged and rectangular|Rounded client clipping remains deferred|Rounded hit-testing remains deferred|Blur/glass remains deferred|Animations remain deferred|High-DPI and scaling remain deferred'
$phase2e1HeadingMatch = Find-FirstMatch $planDoc '## Phase 2E\.1'
$phase2e1BodyMatch = Find-FirstMatch $planDoc 'Phase 2E\.1 is a stabilization and review pass for the Phase 2E Sci Fi desktop and taskbar surface polish|surface helper set was reviewed and kept centralized|start-button hit-geometry cleanup|Wallpaper and theme asset selection remain deferred|desktopBackground wiring for the hosted no-wallpaper path remains in place'
$phase2fHeadingMatch = Find-FirstMatch $planDoc '## Phase 2F'
$phase2fBodyMatch = Find-FirstMatch $planDoc 'Display Options theme UX polish|The Theme section now explains the Classic and Sci Fi choices more clearly|Classic keeps a concise description|Sci Fi keeps a concise description|read-only feature summary|No per-effect sliders, checkboxes, or theme customization controls were added|No new rendering effects or compositor changes were added|Theme selection, persistence, and reload behavior remain unchanged'
$phase2gHeadingMatch = Find-FirstMatch $planDoc '## Phase 2G'
$manualChecklistHeadingMatch = Find-FirstMatch $planDoc '## Manual Validation Checklist'
$classicHostedChecklistMatch = Find-FirstMatch $planDoc '### Classic hosted checklist'
$sciFiHostedChecklistMatch = Find-FirstMatch $planDoc '### Sci Fi hosted checklist'
$bareMetalChecklistMatch = Find-FirstMatch $planDoc '### Bare-metal checklist'
$deferredQueueHeadingMatch = Find-FirstMatch $planDoc '## Deferred / Future Work Queue'
$recommendedNextPassHeadingMatch = Find-FirstMatch $planDoc '## Recommended Next Pass'
$compositorStartButtonRectMatch = Find-FirstMatch $compositor 'hostedStartButtonRect\(theme, tb\)|hostedStartButtonRect\(theme, tbWork\)'
$themeDescriptionClassicMatch = Find-FirstMatch $displayOptions 'Current guideXOS look\.'
$themeDescriptionSciFiMatch = Find-FirstMatch $displayOptions 'Dark futuristic hosted UI\.'
$themeFeatureSummaryMatch = Find-FirstMatch $displayOptions 'Legacy rectangular chrome|Classic taskbar styling|Minimal effects|Rounded hosted chrome|Accent highlights, shadows|Dark taskbar surfaces'
$themeSaveReloadMatch = Find-FirstMatch $displayOptions 'Selecting a theme saves immediately and reloads the compositor\.'
$themeIntroMatch = Find-FirstMatch $displayOptions 'Choose a desktop theme\. Classic is default; Sci Fi is opt-in\.'
$classicOptionMatch = Find-RawMatch $displayOptions 'drawThemeOption\(\s*kThemeOptionX,\s*kThemeOptionY,\s*DesktopThemeId::Classic,.*?Current guideXOS look\.'
$sciFiOptionMatch = Find-RawMatch $displayOptions 'drawThemeOption\(\s*kThemeOptionX,\s*kThemeOptionY \+ kThemeOptionH \+ kThemeOptionGap,\s*DesktopThemeId::SciFi,.*?Dark futuristic hosted UI\.'

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
    [pscustomobject]@{ Name = "theme field softShadowIntent exists"; Pass = $null -ne $themeFieldSoftShadowIntentMatch; Match = $themeFieldSoftShadowIntentMatch },
    [pscustomobject]@{ Name = "theme field desktopBackground exists"; Pass = $null -ne $themeFieldDesktopBackgroundMatch; Match = $themeFieldDesktopBackgroundMatch },
    [pscustomobject]@{ Name = "config theme normalization"; Pass = $null -ne $configThemeMatch; Match = $configThemeMatch },
    [pscustomobject]@{ Name = "desktop.showconfig theme reporting"; Pass = $null -ne $showConfigThemeMatch; Match = $showConfigThemeMatch },
    [pscustomobject]@{ Name = "classic theme option visible"; Pass = $null -ne $classicOptionMatch; Match = $classicOptionMatch },
    [pscustomobject]@{ Name = "sci fi theme option visible"; Pass = $null -ne $sciFiOptionMatch; Match = $sciFiOptionMatch },
    [pscustomobject]@{ Name = "display options theme wiring"; Pass = $null -ne $displayThemeMatch; Match = $displayThemeMatch },
    [pscustomobject]@{ Name = "display options theme intro"; Pass = $null -ne $themeIntroMatch; Match = $themeIntroMatch },
    [pscustomobject]@{ Name = "compositor chrome theme accessor"; Pass = $null -ne $chromeMatch; Match = $chromeMatch },
    [pscustomobject]@{ Name = "classic theme remains rectangular"; Pass = $null -ne $classicRoundedIntentMatch; Match = $classicRoundedIntentMatch },
    [pscustomobject]@{ Name = "sci fi theme intends rounded chrome"; Pass = $null -ne $sciFiRoundedIntentMatch; Match = $sciFiRoundedIntentMatch },
    [pscustomobject]@{ Name = "classic theme keeps shadow off"; Pass = $null -ne $classicShadowOffMatch; Match = $classicShadowOffMatch },
    [pscustomobject]@{ Name = "sci fi theme enables shadow"; Pass = $null -ne $sciFiShadowOnMatch; Match = $sciFiShadowOnMatch },
    [pscustomobject]@{ Name = "window renderer rounded helper wired"; Pass = $null -ne $windowRendererRoundedGuardMatch; Match = $windowRendererRoundedGuardMatch },
    [pscustomobject]@{ Name = "window renderer shadow helper wired"; Pass = $null -ne $windowRendererShadowGuardMatch; Match = $windowRendererShadowGuardMatch },
    [pscustomobject]@{ Name = "compositor rounded helper wired"; Pass = $null -ne $compositorRoundedGuardMatch; Match = $compositorRoundedGuardMatch },
    [pscustomobject]@{ Name = "compositor shadow call wired"; Pass = $null -ne $compositorShadowCallMatch; Match = $compositorShadowCallMatch },
    [pscustomobject]@{ Name = "compositor desktop background wired"; Pass = $null -ne $compositorDesktopBackgroundMatch; Match = $compositorDesktopBackgroundMatch },
    [pscustomobject]@{ Name = "compositor taskbar surface helpers wired"; Pass = $null -ne $compositorTaskbarSurfaceMatch; Match = $compositorTaskbarSurfaceMatch },
    [pscustomobject]@{ Name = "compositor panel surface helpers wired"; Pass = $null -ne $compositorPanelSurfaceMatch; Match = $compositorPanelSurfaceMatch },
    [pscustomobject]@{ Name = "compositor taskbar item helpers wired"; Pass = $null -ne $compositorTaskbarItemMatch; Match = $compositorTaskbarItemMatch },
    [pscustomobject]@{ Name = "compositor start button helpers wired"; Pass = $null -ne $compositorStartButtonMatch; Match = $compositorStartButtonMatch },
    [pscustomobject]@{ Name = "compositor title bar metric wired"; Pass = $null -ne $compositorTitleBarMatch; Match = $compositorTitleBarMatch },
    [pscustomobject]@{ Name = "compositor title inset wired"; Pass = $null -ne $compositorTitleTextInsetMatch; Match = $compositorTitleTextInsetMatch },
    [pscustomobject]@{ Name = "compositor title button gap wired"; Pass = $null -ne $compositorTitleButtonGapMatch; Match = $compositorTitleButtonGapMatch },
    [pscustomobject]@{ Name = "compositor window padding wired"; Pass = $null -ne $compositorWindowPaddingMatch; Match = $compositorWindowPaddingMatch },
    [pscustomobject]@{ Name = "compositor window border wired"; Pass = $null -ne $compositorWindowBorderMatch; Match = $compositorWindowBorderMatch },
    [pscustomobject]@{ Name = "compositor taskbar padding wired"; Pass = $null -ne $compositorTaskbarPaddingMatch; Match = $compositorTaskbarPaddingMatch },
    [pscustomobject]@{ Name = "compositor taskbar item padding wired"; Pass = $null -ne $compositorTaskbarItemPaddingMatch; Match = $compositorTaskbarItemPaddingMatch },
    [pscustomobject]@{ Name = "compositor sci fi taskbar polish wired"; Pass = $null -ne $compositorTaskbarSciFiMatch -and $null -ne $compositorTaskbarSurfaceMatch -and $null -ne $compositorPanelSurfaceMatch -and $null -ne $compositorTaskbarItemMatch -and $null -ne $compositorStartButtonMatch; Match = $(if ($null -ne $compositorTaskbarSurfaceMatch) { $compositorTaskbarSurfaceMatch } else { $compositorTaskbarSciFiMatch }) },
    [pscustomobject]@{ Name = "compositor title button hit-test wired"; Pass = $null -ne $compositorButtonHitMatch; Match = $compositorButtonHitMatch },
    [pscustomobject]@{ Name = "compositor widget inset wired"; Pass = $null -ne $compositorWidgetInsetMatch; Match = $compositorWidgetInsetMatch },
    [pscustomobject]@{ Name = "compositor taskbar spacing wired"; Pass = $null -ne $compositorTaskbarSpacingMatch; Match = $compositorTaskbarSpacingMatch },
    [pscustomobject]@{ Name = "window renderer theme metrics wired"; Pass = $null -ne $windowRendererMetricMatch; Match = $windowRendererMetricMatch },
    [pscustomobject]@{ Name = "window renderer color helper wired"; Pass = $null -ne $windowRendererBlendMatch; Match = $windowRendererBlendMatch },
    [pscustomobject]@{ Name = "window renderer titlebar polish wired"; Pass = $null -ne $windowRendererTitleBarPolishMatch; Match = $windowRendererTitleBarPolishMatch },
    [pscustomobject]@{ Name = "window renderer border polish wired"; Pass = $null -ne $windowRendererBorderPolishMatch; Match = $windowRendererBorderPolishMatch },
    [pscustomobject]@{ Name = "phase 2a docs mention"; Pass = $null -ne $phase2aDocMatch; Match = $phase2aDocMatch },
    [pscustomobject]@{ Name = "phase 2b docs mention"; Pass = $null -ne $phase2bDocMatch; Match = $phase2bDocMatch },
    [pscustomobject]@{ Name = "phase 2b1 docs mention"; Pass = $null -ne $phase2b1DocMatch; Match = $phase2b1DocMatch },
    [pscustomobject]@{ Name = "phase 2c docs mention"; Pass = $null -ne $phase2cHeadingMatch -and $null -ne $phase2cBodyMatch; Match = $(if ($null -ne $phase2cBodyMatch) { $phase2cBodyMatch } else { $phase2cHeadingMatch }) },
    [pscustomobject]@{ Name = "phase 2c1 docs mention"; Pass = $null -ne $phase2c1HeadingMatch -and $null -ne $phase2c1BodyMatch; Match = $(if ($null -ne $phase2c1BodyMatch) { $phase2c1BodyMatch } else { $phase2c1HeadingMatch }) },
    [pscustomobject]@{ Name = "phase 2d docs mention"; Pass = $null -ne $phase2dHeadingMatch -and $null -ne $phase2dBodyMatch; Match = $(if ($null -ne $phase2dBodyMatch) { $phase2dBodyMatch } else { $phase2dHeadingMatch }) },
    [pscustomobject]@{ Name = "phase 2d1 docs mention"; Pass = $null -ne $phase2d1HeadingMatch -and $null -ne $phase2d1BodyMatch; Match = $(if ($null -ne $phase2d1BodyMatch) { $phase2d1BodyMatch } else { $phase2d1HeadingMatch }) },
    [pscustomobject]@{ Name = "phase 2e docs mention"; Pass = $null -ne $phase2eHeadingMatch -and $null -ne $phase2eBodyMatch; Match = $(if ($null -ne $phase2eBodyMatch) { $phase2eBodyMatch } else { $phase2eHeadingMatch }) },
    [pscustomobject]@{ Name = "phase 2e1 docs mention"; Pass = $null -ne $phase2e1HeadingMatch -and $null -ne $phase2e1BodyMatch; Match = $(if ($null -ne $phase2e1BodyMatch) { $phase2e1BodyMatch } else { $phase2e1HeadingMatch }) },
    [pscustomobject]@{ Name = "phase 2f docs mention"; Pass = $null -ne $phase2fHeadingMatch -and $null -ne $phase2fBodyMatch; Match = $(if ($null -ne $phase2fBodyMatch) { $phase2fBodyMatch } else { $phase2fHeadingMatch }) },
    [pscustomobject]@{ Name = "phase 2g docs mention"; Pass = $null -ne $phase2gHeadingMatch; Match = $phase2gHeadingMatch },
    [pscustomobject]@{ Name = "manual validation checklist heading"; Pass = $null -ne $manualChecklistHeadingMatch; Match = $manualChecklistHeadingMatch },
    [pscustomobject]@{ Name = "classic hosted checklist heading"; Pass = $null -ne $classicHostedChecklistMatch; Match = $classicHostedChecklistMatch },
    [pscustomobject]@{ Name = "sci fi hosted checklist heading"; Pass = $null -ne $sciFiHostedChecklistMatch; Match = $sciFiHostedChecklistMatch },
    [pscustomobject]@{ Name = "bare-metal checklist heading"; Pass = $null -ne $bareMetalChecklistMatch; Match = $bareMetalChecklistMatch },
    [pscustomobject]@{ Name = "deferred future work queue heading"; Pass = $null -ne $deferredQueueHeadingMatch; Match = $deferredQueueHeadingMatch },
    [pscustomobject]@{ Name = "recommended next pass heading"; Pass = $null -ne $recommendedNextPassHeadingMatch; Match = $recommendedNextPassHeadingMatch },
    [pscustomobject]@{ Name = "display options classic description"; Pass = $null -ne $themeDescriptionClassicMatch; Match = $themeDescriptionClassicMatch },
    [pscustomobject]@{ Name = "display options sci fi description"; Pass = $null -ne $themeDescriptionSciFiMatch; Match = $themeDescriptionSciFiMatch },
    [pscustomobject]@{ Name = "display options feature summary"; Pass = $null -ne $themeFeatureSummaryMatch; Match = $themeFeatureSummaryMatch },
    [pscustomobject]@{ Name = "display options save reload note"; Pass = $null -ne $themeSaveReloadMatch; Match = $themeSaveReloadMatch },
    [pscustomobject]@{ Name = "compositor start button rect helper wired"; Pass = $null -ne $compositorStartButtonRectMatch; Match = $compositorStartButtonRectMatch }
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
