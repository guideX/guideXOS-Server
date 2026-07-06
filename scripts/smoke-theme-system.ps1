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
$controlPanel = Join-Path $Root "control_panel.cpp"
$notepad = Join-Path $Root "notepad.cpp"
$calculator = Join-Path $Root "calculator.cpp"
$clock = Join-Path $Root "clock.cpp"
$fileExplorer = Join-Path $Root "file_explorer.cpp"
$fileExplorerHeader = Join-Path $Root "file_explorer.h"
$compositor = Join-Path $Root "compositor.cpp"
$windowRenderer = Join-Path $Root "window_renderer.h"
$planDoc = Join-Path $Root "docs\theme-system-plan.md"
$smokeScriptPath = $MyInvocation.MyCommand.Path
$displayOptionsThemeHelperMatch = Find-FirstMatch $displayOptions 'IsSciFiThemeActive|DisplayOptionsBodyColor|DisplayOptionsPanelColor|DisplayOptionsCardColor|DisplayOptionsButtonFillColor|DisplayOptionsButtonBorderColor|DisplayOptionsTextColor|DisplayOptionsMutedTextColor|DisplayOptionsAccentColor'
$displayOptionsThemeFieldMatch = Find-FirstMatch $displayOptions 'windowBackground|windowBorder|accent|mutedAccent|taskbarBackground|taskbarBorder|titleBarText'
$displayOptionsTextColorMatch = Find-FirstMatch $displayOptions 'MT_DrawTextAtColor|DisplayOptionsMutedTextColor\(\)|DisplayOptionsTextColor\(\)'
$effectPlaceholderMatch = Find-FirstMatch $displayOptions 'Choose Color|Visual Effects'
$phase3aNoEffectsMatch = Find-FirstMatch $planDoc 'No blur, glass, animation, rounded clipping, rounded hit-testing, or per-effect theme controls were added'

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
$phase3aHeadingMatch = Find-FirstMatch $planDoc '## Phase 3A'
$phase3aBodyMatch = Find-FirstMatch $planDoc 'first app-surface pilot|first app surface to receive Sci Fi polish|conservative panel/card/accent treatment|Classic is preserved and stays visually close to the current Display Options look|No blur, glass, animation, rounded clipping, rounded hit-testing, or per-effect theme controls were added|Broad app redesign remains deferred|Future app polish should proceed one app at a time'
$phase3a1HeadingMatch = Find-FirstMatch $planDoc '## Phase 3A\.1'
$phase3a1BodyMatch = Find-FirstMatch $planDoc 'stabilization-only review pass for the Display Options app-surface pilot|Display Options surface helpers were reviewed and kept centralized|Classic preservation was verified|tiny fallback tweak to restore the prior neutral card feel|No new effects or controls were added|Future app polish should remain one app at a time'
$manualRunbookHeadingMatch = Find-FirstMatch $planDoc '## Manual Validation Runbook'
$manualRunbookClassicFirstMatch = Find-FirstMatch $planDoc 'Validate Classic first'
$manualRunbookLauncherMatch = Find-FirstMatch $planDoc 'Launch Display Options with the registered app name `DisplayOptions`'
$manualRunbookPersistedTokensMatch = Find-FirstMatch $planDoc 'Persisted theme tokens are `classic` and `scifi`'
$manualRunbookArtifactMatch = Find-FirstMatch $planDoc 'theme-phase2g1-\*\.png.*local validation artifacts'
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
$controlPanelThemeHelperMatch = Find-FirstMatch $controlPanel 'ControlPanelBodyColor|ControlPanelPanelColor|ControlPanelCardColor|ControlPanelCardHoverColor|ControlPanelCardSelectedColor|ControlPanelBorderColor|ControlPanelTextColor|ControlPanelMutedTextColor|ControlPanelAccentColor|GetCurrentDesktopThemeId|GetCurrentDesktopTheme|DesktopThemeId::SciFi'
$controlPanelGridTopMatch = Find-RawMatch $controlPanel 'void ControlPanel::render\(\).*?int itemY = kGridTop \+ row \* \(ITEM_H \+ GAP\);.*?void ControlPanel::handleMouseDown\(int mx, int my\).*?int itemY = kGridTop \+ row \* \(ITEM_H \+ GAP\);'
$controlPanelPerEffectMatch = Find-FirstMatch $controlPanel 'Visual Effects|per-effect'
$phase3bHeadingMatch = Find-FirstMatch $planDoc '## Phase 3B'
$phase3bBodyMatch = Find-FirstMatch $planDoc 'second app-surface pilot|Control Panel is the second app surface to receive Sci Fi polish|conservative panel/card/accent treatment|Classic is preserved and stays visually close to the current Control Panel look|No new effects were added|No new per-effect theme controls were introduced|Broad app redesign remains deferred|Future app polish should continue one app at a time'
$phase3b1HeadingMatch = Find-FirstMatch $planDoc '## Phase 3B\.1'
$phase3b1BodyMatch = Find-FirstMatch $planDoc 'stabilization-only review pass for the Control Panel app-surface pilot|Control Panel surface helpers were reviewed and kept centralized|Classic preservation was confirmed|No new effects, controls, rounded behavior, or layout redesign were added|shared grid top offset remains in place|Future app polish should remain one app at a time'
$phase3cHeadingMatch = Find-FirstMatch $planDoc '## Phase 3C'
$phase3cBodyMatch = Find-FirstMatch $planDoc 'third app-surface pilot|Notepad is the third app surface to receive Sci Fi polish|conservative editor/body/border/text polish|Classic is preserved and stays visually close to the current Notepad look|No editing behavior changed|No new effects were added|No new per-effect theme controls were introduced|Broad app redesign remains deferred|Future app polish should continue one app at a time'
$phase3c1HeadingMatch = Find-FirstMatch $planDoc '## Phase 3C\.1'
$phase3c1BodyMatch = Find-FirstMatch $planDoc 'stabilization-only review pass for the Notepad app-surface pilot|Notepad surface helpers were reviewed and kept centralized|Classic preservation was confirmed|No editing behavior, file handling, keyboard input, persistence, layout geometry, hit-testing, or theme metrics changed|No new effects or controls were added|build\.bat wrapper behavior was classified by direct reruns as non-reproducible|Future app polish should remain one app at a time'
$phase3dHeadingMatch = Find-FirstMatch $planDoc '## Phase 3D'
$phase3dBodyMatch = Find-RawMatch $planDoc '## Phase 3D.*?Phase 3D is the fourth app-surface pilot for the guideXOS Server theme system\..*?Calculator is the fourth app surface to receive Sci Fi polish\..*?Sci Fi gets conservative body, display, button, and accent polish so Calculator feels a little more cohesive with the shell\..*?Classic is preserved and stays visually close to the current Calculator look\..*?No Calculator behavior changed\..*?No new effects were added\..*?No new per-effect theme controls were introduced\..*?Broad app redesign remains deferred\..*?Future app polish should continue one app at a time\.'
$phase3d1HeadingMatch = Find-FirstMatch $planDoc '## Phase 3D\.1'
$phase3d1BodyMatch = Find-RawMatch $planDoc '## Phase 3D\.1.*?Phase 3D\.1 is a stabilization-only review pass for the Calculator app-surface pilot\..*?Calculator surface helpers were reviewed and kept centralized\..*?The shared compositor widget guard was reviewed and kept narrow to Calculator and Sci Fi\..*?Classic preservation was confirmed, with no visible fallback changes needed to restore the prior familiar look\..*?No Calculator behavior, input handling, math evaluation, history, persistence, layout geometry, hit-testing, or theme metrics changed\..*?No new effects or controls were added\..*?No blur, glass, animation, rounded clipping, or rounded hit-testing was added\..*?Future app polish should remain one app at a time\.'
$phase3eHeadingMatch = Find-FirstMatch $planDoc '## Phase 3E'
$phase3eBodyMatch = Find-RawMatch $planDoc '## Phase 3E.*?Phase 3E is the next app-surface pilot for the guideXOS Server theme system\..*?File Explorer is the next app surface to receive Sci Fi polish\..*?Sci Fi gets conservative body, list, toolbar, address, selection, hover, border, and status polish so File Explorer feels more cohesive with the shell\..*?Classic is preserved and stays visually close to the current File Explorer look\..*?No File Explorer file listing behavior changed\..*?No File Explorer navigation behavior changed\..*?No File Explorer open or launch behavior changed\..*?No App Model behavior changed\..*?No new effects were added\..*?Broad app redesign remains deferred\..*?Future app polish should continue one app at a time\.'
$phase3e1HeadingMatch = Find-FirstMatch $planDoc '## Phase 3E\.1'
$phase3e1BodyMatch = Find-RawMatch $planDoc '## Phase 3E\.1.*?Phase 3E\.1 is a stabilization-only review pass for the File Explorer app-surface pilot\..*?File Explorer surface helpers were reviewed and kept centralized\..*?Row hover and selection safety were reviewed.*?hover stays visual-only\..*?The shared File Explorer render path is app-level renderer surface code; no bare-metal-specific theme parity work was added in this pass\..*?Classic preservation was confirmed, with no visible fallback changes needed to restore the prior familiar look\..*?No File Explorer file listing behavior, directory navigation, file open or launch behavior, App Model behavior, persistence, layout geometry, hit-testing, or theme metrics changed\..*?No new effects or controls were added\..*?No blur, glass, animation, rounded clipping, or rounded hit-testing was added\..*?Future app polish should remain one app at a time\.'
$phase3fHeadingMatch = Find-FirstMatch $planDoc '## Phase 3F'
$phase3fBodyMatch = Find-RawMatch $planDoc '## Phase 3F.*?Phase 3F is the next app-surface pilot for the guideXOS Server theme system\..*?Clock is the next app surface to receive Sci Fi polish\..*?Sci Fi gets conservative body, readout, and accent polish; Clock does not have dedicated mode tabs or button chrome in this pass, so there is no control-surface redesign to apply\..*?Classic is preserved and stays visually close to the current Clock look\..*?No Clock behavior changed\..*?No new effects were added\..*?Broad app redesign remains deferred\..*?Future app polish should continue one app at a time\.'
$phase3f1HeadingMatch = Find-FirstMatch $planDoc '## Phase 3F\.1'
$phase3f1BodyMatch = Find-RawMatch $planDoc '## Phase 3F\.1.*?Phase 3F\.1 is a stabilization-only review pass for the Clock app-surface pilot\..*?The Clock theme-aware repaint path was reviewed and kept local to Clock surfaces\..*?Repaint safety was reviewed: the clear-then-redraw path removes stale stacked text without changing timekeeping cadence or layout, and it still runs on the existing one-second update cadence\..*?Classic preservation was confirmed; the fallback remains visually close to the prior Clock look and no visible fallback tweak was needed\..*?Sci Fi readability was reviewed; the time/date readout stays readable on the darker body/face surfaces, and the muted date text remains legible\..*?No Clock timekeeping, timer, alarm, stopwatch, scheduling, persistence, layout geometry, hit-testing, or theme metric behavior changed\..*?No new effects or controls were added\..*?No blur, glass, animation, rounded clipping, or rounded hit-testing was added\..*?No additional readability or layout fix was needed in this pass\..*?Future app polish should remain one app at a time\.'
$phase3gHeadingMatch = Find-FirstMatch $planDoc '## Phase 3G'
$phase3gIntroMatch = Find-RawMatch $planDoc '## Phase 3G.*?Classic remains the default theme\..*?Sci Fi remains opt-in\..*?No per-effect controls were introduced\.'
$phase3gInventoryMatch = Find-RawMatch $planDoc '### App-Surface Inventory.*?Display Options: Phase 3A / 3A\.1;.*?Classic preservation: default look stays intact and the neutral card feel remains;.*?Behavior changes: No;.*?Stabilization: complete in 3A\.1;.*?Safety fix: tiny fallback tweak restored the neutral card feel\..*?Control Panel: Phase 3B / 3B\.1;.*?Classic preservation: look stays close to the prior Control Panel surface;.*?Behavior changes: No;.*?Stabilization: complete in 3B\.1;.*?Safety fix: shared grid top offset stayed synchronized for drawing and hit-testing\..*?Notepad: Phase 3C / 3C\.1;.*?Classic preservation: look stays close to the prior Notepad surface;.*?Behavior changes: No;.*?Stabilization: complete in 3C\.1;.*?Safety fix: surface helpers stayed centralized and no visible fallback tweak was needed\..*?Calculator: Phase 3D / 3D\.1;.*?Classic preservation: look stays close to the prior Calculator surface;.*?Behavior changes: No;.*?Stabilization: complete in 3D\.1;.*?Safety fix: shared compositor widget guard stayed narrow to Calculator and Sci Fi\..*?File Explorer: Phase 3E / 3E\.1;.*?Classic preservation: look stays close to the prior File Explorer surface;.*?Behavior changes: No;.*?Stabilization: complete in 3E\.1;.*?Safety fix: Sci Fi hover clears on scroll and offset changes so hover stays visual-only\..*?Clock: Phase 3F / 3F\.1;.*?Classic preservation: look stays close to the prior Clock surface;.*?Behavior changes: No;.*?Stabilization: complete in 3F\.1;.*?Safety fix: clear-then-redraw removes stale stacked text without changing timekeeping cadence\.'
$phase3gDeferredMatch = Find-RawMatch $planDoc '### Deferred Boundaries.*?Rounded client clipping remains deferred\..*?Rounded hit-testing remains deferred\..*?Blur/glass remains deferred\..*?Animations remain deferred\..*?High-DPI/scaling polish remains deferred\..*?Taskbar shadows remain deferred\..*?Theme wallpaper selection remains deferred\..*?Broad app redesign remains deferred\..*?Bare-metal theme parity remains deferred\..*?Per-effect controls remain deferred\..*?Navigator, Image Viewer, Task Manager, and other future app-surface polish remain deferred\.'
$phase3gValidationMatch = Find-RawMatch $planDoc '### Validation Checklist.*?\.\\build\.bat.*?\.\\scripts\\smoke-theme-system\.ps1 -SkipBuild.*?\.\\scripts\\smoke-live-directory-desktop-status\.ps1 -SkipBuild.*?git diff --check.*?Skip the optional appmodel smoke unless there is a specific reason to run it\..*?Treat `guideXOSServer\.exe`, `desktop\.json`, screenshots, generated EXEs, and disk images as validation artifacts unless intentionally changed\.'
$phase3hHeadingMatch = Find-FirstMatch $planDoc '## Phase 3H'
$phase3hBodyMatch = Find-RawMatch $planDoc '## Phase 3H.*?Hosted compositor validation was performed for Classic and Sci Fi\..*?Classic preservation held: the default theme stayed familiar, and no accidental Sci Fi colors, layout drift, or missing fills or borders were observed on the inspected surfaces\..*?Sci Fi readability and cohesion held on the shell and app pilots: titlebars, menus, taskbar, desktop surfaces, and app chrome remained readable and cohesive without looking overdone\..*?Inspected surfaces: desktop background, taskbar, Start menu, taskbar context menu, Display Options, Control Panel, Notepad, Calculator, File Explorer, and Clock\..*?Tiny fixes made: none in repo source; the pass stayed documentation-only, with only temporary local validation harness retries to stabilize screenshot capture\..*?Screenshots and artifacts captured: local PNGs were captured in temporary validation artifact folders and were not added to git\..*?Deferred boundaries remain unchanged: rounded client clipping, rounded hit-testing, blur/glass, animations, taskbar shadows, high-DPI/scaling, bare-metal theme parity, per-effect controls, and broad app redesign remain deferred\..*?Readiness: the Sci Fi app-surface milestone is ready for the next layer of theme work\.'
$phase4aHeadingMatch = Find-FirstMatch $planDoc '## Phase 4A'
$phase4aBodyMatch = Find-RawMatch $planDoc '## Phase 4A.*?readiness/defaults boundary checkpoint for the Sci Fi theme\..*?validated hosted opt-in theme milestone\..*?Classic remains the default and fallback-safe theme\..*?Hosted shell and app-surface coverage are broad enough to continue from a stable checkpoint\..*?Bare-metal parity remains deferred\..*?Sci Fi is not yet declared a universal/default OS theme\..*?Future work should continue behind opt-in gates unless explicitly promoted\..*?No per-effect controls were introduced\.'
$phase4aSafetyMatch = Find-RawMatch $planDoc '### Default Safety Boundary.*?Classic remains default\..*?Sci Fi remains opt-in\..*?Missing or invalid theme config falls back to Classic\..*?Theme switching should not create tracked config changes that imply a default change\..*?Validation artifacts like `guideXOSServer\.exe`, `desktop\.json`, `desktop\.state`, screenshots, temp harness files, and disk images are local-only and should not be committed unless a task explicitly changes defaults\.'
$phase4aNextLayerMatch = Find-RawMatch $planDoc '### Ready for Next Layer.*?Theme wallpaper selection\..*?Navigator, Image Viewer, and Task Manager app-surface pilots\..*?Higher-quality manual screenshot harness\..*?High-DPI/scaling review\..*?Hosted shadow and taskbar polish\..*?Bare-metal parity inventory\..*?Rounded clipping and hit-testing research, still not implementation\..*?Theme asset and icon polish\.'
$clockThemeHelperMatch = Find-FirstMatch $clock 'ClockBodyColor|ClockFaceColor|ClockBorderColor|ClockReadoutColor|ClockMutedTextColor|ClockAccentColor|paintClockSurface|drawClockText|GetCurrentDesktopThemeId|GetCurrentDesktopTheme|DesktopThemeId::SciFi'
$clockClearHelperMatch = Find-FirstMatch $clock 'clearWindowSurface\(uint64_t windowId\)'
$clockClearSentinelMatch = Find-FirstMatch $clock 'MsgType::MT_DrawText, "\\f"'
$clockThemeFieldMatch = Find-FirstMatch $clock 'windowBackground|windowBorder|accent|mutedAccent|taskbarBackground|taskbarBorder|titleBarText'
$clockPerEffectMatch = Find-FirstMatch $clock 'Visual Effects|per-effect'
$fileExplorerThemeHelperMatch = Find-FirstMatch $fileExplorer 'FileExplorerBodyColor|FileExplorerToolbarColor|FileExplorerPathBoxColor|FileExplorerPanelColor|FileExplorerListColor|FileExplorerHeaderColor|FileExplorerStatusColor|FileExplorerBorderColor|FileExplorerTextColor|FileExplorerMutedTextColor|FileExplorerAccentColor|FileExplorerSelectedRowColor|FileExplorerHoveredRowColor|FileExplorerContextMenuColor|FileExplorerContextMenuHoverColor|drawSurfaceTextAt'
$fileExplorerThemeFieldMatch = Find-FirstMatch $fileExplorer 'windowBackground|windowBorder|accent|mutedAccent|taskbarBackground|taskbarBorder|titleBarText'
$fileExplorerHoverMatch = Find-RawMatch $fileExplorer 'action == "move" && !s_draggingFileListScrollbar && isSciFiThemeActive\(\).*?hitTestEntryRow\(x, y\)|scrollFileListByRows\(int rows\).*?s_hoveredIndex = -1;|ensureSelectedFileVisible\(\).*?s_hoveredIndex = -1;'
$fileExplorerNoPerEffectMatch = Find-FirstMatch $fileExplorer 'Visual Effects|per-effect'
$appModelSmokePattern = ('smoke-' + 'appmodel' + '.*-(status|gate)')
$smokeNoAppModelStatusMatch = Find-FirstMatch $smokeScriptPath $appModelSmokePattern
$notepadThemeHelperMatch = Find-FirstMatch $notepad 'NotepadBodyColor|NotepadEditorColor|NotepadBorderColor|NotepadTextColor|NotepadMutedTextColor|NotepadAccentColor|NotepadSelectionColor|NotepadStatusColor|NotepadMenuColor|NotepadMenuHoverColor|GetCurrentDesktopThemeId|GetCurrentDesktopTheme|DesktopThemeId::SciFi'
$notepadColorTextMatch = Find-FirstMatch $notepad 'publishDrawTextAtColor'
$notepadThemeFieldMatch = Find-FirstMatch $notepad 'windowBackground|windowBorder|accent|mutedAccent|taskbarBackground|taskbarBorder|titleBarText'
$notepadPerEffectMatch = Find-FirstMatch $notepad 'Visual Effects|per-effect'
$calculatorThemeHelperMatch = Find-FirstMatch $calculator 'CalculatorBodyColor|CalculatorDisplayColor|CalculatorDisplayBorderColor|CalculatorDisplayTextColor|paintCalculatorSurface|calculatorWidgetFillColor|calculatorWidgetBorderColor|calculatorWidgetTextColor|isCalculatorWindow|GetCurrentDesktopThemeId|GetCurrentDesktopTheme|DesktopThemeId::SciFi'
$calculatorWidgetGuardMatch = Find-RawMatch $compositor 'static uint32_t calculatorWidgetFillColor\(const WinInfo& winfo, const Widget& widget, const DesktopTheme& theme\)\s*\{\s*if \(!isCalculatorWindow\(winfo\) \|\| theme\.id != DesktopThemeId::SciFi\) \{\s*return calculatorClassicWidgetFillColor\(widget\);'
$calculatorThemeFieldMatch = Find-FirstMatch $calculator 'windowBackground|windowBorder|accent|mutedAccent|taskbarBackground|taskbarBorder|titleBarText|windowPadding|titleBarHeight'
$calculatorPerEffectMatch = Find-FirstMatch $calculator 'Visual Effects|per-effect'

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
    [pscustomobject]@{ Name = "phase 3a docs heading"; Pass = $null -ne $phase3aHeadingMatch; Match = $phase3aHeadingMatch },
    [pscustomobject]@{ Name = "phase 3a docs body"; Pass = $null -ne $phase3aBodyMatch; Match = $phase3aBodyMatch },
    [pscustomobject]@{ Name = "phase 3a1 docs heading"; Pass = $null -ne $phase3a1HeadingMatch; Match = $phase3a1HeadingMatch },
    [pscustomobject]@{ Name = "phase 3a1 docs body"; Pass = $null -ne $phase3a1BodyMatch; Match = $phase3a1BodyMatch },
    [pscustomobject]@{ Name = "phase 3g docs heading"; Pass = $null -ne $phase3gHeadingMatch; Match = $phase3gHeadingMatch },
    [pscustomobject]@{ Name = "phase 3g intro restatement"; Pass = $null -ne $phase3gIntroMatch; Match = $phase3gIntroMatch },
    [pscustomobject]@{ Name = "phase 3g inventory complete"; Pass = $null -ne $phase3gInventoryMatch; Match = $phase3gInventoryMatch },
    [pscustomobject]@{ Name = "phase 3g deferred boundaries documented"; Pass = $null -ne $phase3gDeferredMatch; Match = $phase3gDeferredMatch },
    [pscustomobject]@{ Name = "phase 3g validation checklist documented"; Pass = $null -ne $phase3gValidationMatch; Match = $phase3gValidationMatch },
    [pscustomobject]@{ Name = "phase 3h docs heading"; Pass = $null -ne $phase3hHeadingMatch; Match = $phase3hHeadingMatch },
    [pscustomobject]@{ Name = "phase 3h docs body"; Pass = $null -ne $phase3hBodyMatch; Match = $phase3hBodyMatch },
    [pscustomobject]@{ Name = "phase 4a docs heading"; Pass = $null -ne $phase4aHeadingMatch; Match = $phase4aHeadingMatch },
    [pscustomobject]@{ Name = "phase 4a docs body"; Pass = $null -ne $phase4aBodyMatch; Match = $phase4aBodyMatch },
    [pscustomobject]@{ Name = "phase 4a default safety boundary documented"; Pass = $null -ne $phase4aSafetyMatch; Match = $phase4aSafetyMatch },
    [pscustomobject]@{ Name = "phase 4a ready-for-next-layer list documented"; Pass = $null -ne $phase4aNextLayerMatch; Match = $phase4aNextLayerMatch },
    [pscustomobject]@{ Name = "manual validation runbook heading"; Pass = $null -ne $manualRunbookHeadingMatch; Match = $manualRunbookHeadingMatch },
    [pscustomobject]@{ Name = "manual validation classic-first note"; Pass = $null -ne $manualRunbookClassicFirstMatch; Match = $manualRunbookClassicFirstMatch },
    [pscustomobject]@{ Name = "manual validation displayoptions launcher"; Pass = $null -ne $manualRunbookLauncherMatch; Match = $manualRunbookLauncherMatch },
    [pscustomobject]@{ Name = "manual validation persisted token note"; Pass = $null -ne $manualRunbookPersistedTokensMatch; Match = $manualRunbookPersistedTokensMatch },
    [pscustomobject]@{ Name = "manual validation screenshot artifact note"; Pass = $null -ne $manualRunbookArtifactMatch; Match = $manualRunbookArtifactMatch },
    [pscustomobject]@{ Name = "display options theme helpers wired"; Pass = $null -ne $displayOptionsThemeHelperMatch; Match = $displayOptionsThemeHelperMatch },
    [pscustomobject]@{ Name = "display options theme fields wired"; Pass = $null -ne $displayOptionsThemeFieldMatch; Match = $displayOptionsThemeFieldMatch },
    [pscustomobject]@{ Name = "display options text color support"; Pass = $null -ne $displayOptionsTextColorMatch; Match = $displayOptionsTextColorMatch },
    [pscustomobject]@{ Name = "display options effect placeholders remain"; Pass = $null -ne $effectPlaceholderMatch -and $null -ne $phase3aNoEffectsMatch; Match = $(if ($null -ne $phase3aNoEffectsMatch) { $phase3aNoEffectsMatch } else { $effectPlaceholderMatch }) },
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
    [pscustomobject]@{ Name = "control panel theme helpers wired"; Pass = $null -ne $controlPanelThemeHelperMatch; Match = $controlPanelThemeHelperMatch },
    [pscustomobject]@{ Name = "control panel grid top synchronized"; Pass = $null -ne $controlPanelGridTopMatch; Match = $controlPanelGridTopMatch },
    [pscustomobject]@{ Name = "control panel no per-effect controls"; Pass = $null -eq $controlPanelPerEffectMatch; Match = $controlPanelPerEffectMatch },
    [pscustomobject]@{ Name = "notepad theme helpers wired"; Pass = $null -ne $notepadThemeHelperMatch -or $null -ne $notepadThemeFieldMatch; Match = $(if ($null -ne $notepadThemeHelperMatch) { $notepadThemeHelperMatch } else { $notepadThemeFieldMatch }) },
    [pscustomobject]@{ Name = "notepad colored text path present"; Pass = $null -ne $notepadColorTextMatch; Match = $notepadColorTextMatch },
    [pscustomobject]@{ Name = "notepad no per-effect controls"; Pass = $null -eq $notepadPerEffectMatch; Match = $notepadPerEffectMatch },
    [pscustomobject]@{ Name = "phase 3b docs heading"; Pass = $null -ne $phase3bHeadingMatch; Match = $phase3bHeadingMatch },
    [pscustomobject]@{ Name = "phase 3b docs body"; Pass = $null -ne $phase3bBodyMatch; Match = $phase3bBodyMatch },
    [pscustomobject]@{ Name = "phase 3b1 docs heading"; Pass = $null -ne $phase3b1HeadingMatch; Match = $phase3b1HeadingMatch },
    [pscustomobject]@{ Name = "phase 3b1 docs body"; Pass = $null -ne $phase3b1BodyMatch; Match = $phase3b1BodyMatch },
    [pscustomobject]@{ Name = "phase 3c docs heading"; Pass = $null -ne $phase3cHeadingMatch; Match = $phase3cHeadingMatch },
    [pscustomobject]@{ Name = "phase 3c docs body"; Pass = $null -ne $phase3cBodyMatch; Match = $phase3cBodyMatch },
    [pscustomobject]@{ Name = "phase 3c.1 docs heading"; Pass = $null -ne $phase3c1HeadingMatch; Match = $phase3c1HeadingMatch },
    [pscustomobject]@{ Name = "phase 3c.1 docs body"; Pass = $null -ne $phase3c1BodyMatch; Match = $phase3c1BodyMatch },
    [pscustomobject]@{ Name = "phase 3d docs heading"; Pass = $null -ne $phase3dHeadingMatch; Match = $phase3dHeadingMatch },
    [pscustomobject]@{ Name = "phase 3d docs body"; Pass = $null -ne $phase3dBodyMatch; Match = $phase3dBodyMatch },
    [pscustomobject]@{ Name = "phase 3d.1 docs heading"; Pass = $null -ne $phase3d1HeadingMatch; Match = $phase3d1HeadingMatch },
    [pscustomobject]@{ Name = "phase 3d.1 docs body"; Pass = $null -ne $phase3d1BodyMatch; Match = $phase3d1BodyMatch },
    [pscustomobject]@{ Name = "phase 3e docs heading"; Pass = $null -ne $phase3eHeadingMatch; Match = $phase3eHeadingMatch },
    [pscustomobject]@{ Name = "phase 3e docs body"; Pass = $null -ne $phase3eBodyMatch; Match = $phase3eBodyMatch },
    [pscustomobject]@{ Name = "phase 3e.1 docs heading"; Pass = $null -ne $phase3e1HeadingMatch; Match = $phase3e1HeadingMatch },
    [pscustomobject]@{ Name = "phase 3e.1 docs body"; Pass = $null -ne $phase3e1BodyMatch; Match = $phase3e1BodyMatch },
    [pscustomobject]@{ Name = "phase 3f docs heading"; Pass = $null -ne $phase3fHeadingMatch; Match = $phase3fHeadingMatch },
    [pscustomobject]@{ Name = "phase 3f docs body"; Pass = $null -ne $phase3fBodyMatch; Match = $phase3fBodyMatch },
    [pscustomobject]@{ Name = "phase 3f.1 docs heading"; Pass = $null -ne $phase3f1HeadingMatch; Match = $phase3f1HeadingMatch },
    [pscustomobject]@{ Name = "phase 3f.1 docs body"; Pass = $null -ne $phase3f1BodyMatch; Match = $phase3f1BodyMatch },
    [pscustomobject]@{ Name = "clock theme helpers wired"; Pass = $null -ne $clockThemeHelperMatch -or $null -ne $clockThemeFieldMatch; Match = $(if ($null -ne $clockThemeHelperMatch) { $clockThemeHelperMatch } else { $clockThemeFieldMatch }) },
    [pscustomobject]@{ Name = "clock clear helper present"; Pass = $null -ne $clockClearHelperMatch; Match = $clockClearHelperMatch },
    [pscustomobject]@{ Name = "clock clear sentinel present"; Pass = $null -ne $clockClearSentinelMatch; Match = $clockClearSentinelMatch },
    [pscustomobject]@{ Name = "clock no per-effect controls"; Pass = $null -eq $clockPerEffectMatch; Match = $clockPerEffectMatch },
    [pscustomobject]@{ Name = "file explorer theme helpers wired"; Pass = $null -ne $fileExplorerThemeHelperMatch -or $null -ne $fileExplorerThemeFieldMatch -or $null -ne $fileExplorerHoverMatch; Match = $(if ($null -ne $fileExplorerThemeHelperMatch) { $fileExplorerThemeHelperMatch } elseif ($null -ne $fileExplorerThemeFieldMatch) { $fileExplorerThemeFieldMatch } else { $fileExplorerHoverMatch }) },
    [pscustomobject]@{ Name = "file explorer sci fi hover guarded"; Pass = $null -ne $fileExplorerHoverMatch; Match = $fileExplorerHoverMatch },
    [pscustomobject]@{ Name = "file explorer no per-effect controls"; Pass = $null -eq $fileExplorerNoPerEffectMatch; Match = $fileExplorerNoPerEffectMatch },
    [pscustomobject]@{ Name = "smoke script has no app model status gate dependency"; Pass = $null -eq $smokeNoAppModelStatusMatch; Match = $smokeNoAppModelStatusMatch },
    [pscustomobject]@{ Name = "calculator theme helpers wired"; Pass = $null -ne $calculatorThemeHelperMatch -or $null -ne $calculatorThemeFieldMatch; Match = $(if ($null -ne $calculatorThemeHelperMatch) { $calculatorThemeHelperMatch } else { $calculatorThemeFieldMatch }) },
    [pscustomobject]@{ Name = "calculator widget sci fi guard narrow"; Pass = $null -ne $calculatorWidgetGuardMatch; Match = $calculatorWidgetGuardMatch },
    [pscustomobject]@{ Name = "calculator no per-effect controls"; Pass = $null -eq $calculatorPerEffectMatch; Match = $calculatorPerEffectMatch },
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
