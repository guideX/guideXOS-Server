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
$navigator = Join-Path $Root "navigator.cpp"
$clock = Join-Path $Root "clock.cpp"
$fileExplorer = Join-Path $Root "file_explorer.cpp"
$fileExplorerHeader = Join-Path $Root "file_explorer.h"
$imageViewer = Join-Path $Root "image_viewer.cpp"
$taskManager = Join-Path $Root "task_manager.cpp"
$compositor = Join-Path $Root "compositor.cpp"
$kernelDesktop = Join-Path $Root "kernel\core\desktop.cpp"
$kernelApps = Join-Path $Root "kernel\core\kernel_apps.cpp"
$kernelCompositor = Join-Path $Root "kernel\core\kernel_compositor.cpp"
$diskManager = Join-Path $Root "disk_manager.cpp"
$diskManagerHeader = Join-Path $Root "disk_manager.h"
$trash = Join-Path $Root "trash.cpp"
$windowRenderer = Join-Path $Root "window_renderer.h"
$startupSync = Join-Path $Root "scripts\smoke-desktop-startup-sync.ps1"
$controlThemeHeader = Join-Path $Root "desktop_control_theme.h"
$controlThemeTest = Join-Path $Root "tests\desktop_control_theme_test.cpp"
$planDoc = Join-Path $Root "docs\theme-system-plan.md"
$smokeScriptPath = $MyInvocation.MyCommand.Path
$desktopControlRolesMatch = Find-FirstMatch $controlThemeHeader 'struct DesktopControlTheme'
$desktopControlTokenMatch = Find-RawMatch $controlThemeHeader 'controlBackground.*?inputBackground.*?scrollbarTrack'
$desktopControlStateMatch = Find-RawMatch $controlThemeHeader 'enum class DesktopControlState.*?DesktopControlFillColor.*?DesktopControlBorderColor.*?DesktopControlTextColor'
$hostedSharedButtonMatch = Find-RawMatch $compositor 'hostedDefaultWidgetFillColor.*?DesktopControlFillColor.*?hostedDefaultWidgetBorderColor.*?DesktopControlBorderColor.*?hostedDefaultWidgetTextColor.*?DesktopControlTextColor'
$kernelSharedWidgetMatch = Find-RawMatch $kernelCompositor 'DesktopControlFillColor.*?DesktopControlBorderColor.*?DesktopControlTextColor.*?inputBackground'
$fileExplorerControlConsumerMatch = Find-FirstMatch $fileExplorer 'GetDesktopControlTheme|DesktopControl|scrollbarTrack'
$notepadControlConsumerMatch = Find-FirstMatch $notepad 'GetDesktopControlTheme|DesktopControl|inputBackground'
$displayOptionsControlConsumerMatch = Find-FirstMatch $displayOptions 'GetDesktopControlTheme|DesktopControl|controlFocusBorder'
$controlThemeTestMatch = Find-FirstMatch $controlThemeTest 'DesktopControlState::Hover|TryParseDesktopThemeId'
$startupThemeOverrideMatch = Find-FirstMatch $startupSync 'DesktopThemeId|staged desktop theme override'
$phase10AHeadingMatch = Find-FirstMatch $planDoc '## Phase 10A'
$phase10AArchitectureMatch = Find-FirstMatch $planDoc 'DesktopControlTheme'
$phase10AClassicMatch = Find-FirstMatch $planDoc 'Classic remains the default and compatible'
$phase10ALimitationsMatch = Find-FirstMatch $planDoc '### Known limitations and deferred work'
$phase10BHeadingMatch = Find-FirstMatch $planDoc '## Phase 10B - Navigator Interior Sci-Fi Theming'
$phase10BOwnershipMatch = Find-RawMatch $planDoc '### Starting architecture and ownership audit.*?hosted Navigator application in `navigator\.cpp`.*?bare-metal `NavigatorApp`'
$phase10BDocumentBoundaryMatch = Find-RawMatch $planDoc '### Browser chrome versus web content.*?Document backgrounds, authored CSS colors.*?HTML/CSS scrollbar behavior is not globally overridden'
$phase10BValidationMatch = Find-RawMatch $planDoc '### Validation and evidence.*?build\.bat.*?smoke-navigator-hosted\.ps1.*?build\.ps1 -Arch amd64'
$phase10BLimitationsMatch = Find-RawMatch $planDoc '### Limitations and deferred Navigator visual work.*?no separate keyboard-focus publication.*?Deferred work includes icon-system replacement'
$phase10CHeadingMatch = Find-FirstMatch $planDoc '## Phase 10C - Task Manager Interior Sci-Fi Theming'
$phase10COwnershipMatch = Find-RawMatch $planDoc '### Starting architecture and ownership audit.*?Hosted Task Manager is .*?`task_manager\.cpp`/`task_manager\.h`.*?Bare-metal Task Manager is `kernel::apps::TaskManagerApp`'
$phase10CSurfacesMatch = Find-RawMatch $planDoc '### Application-owned surfaces themed.*?process/task table headers.*?existing footer/status/summary text hierarchy'
$phase10CSharedRolesMatch = Find-RawMatch $planDoc '### Shared roles and reusable additions.*?GetDesktopControlTheme.*?tableHeaderBackground.*?tableHeaderText.*?No Task Manager-specific Sci-Fi palette'
$phase10CClassicMatch = Find-RawMatch $planDoc '### Classic and Sci-Fi behavior.*?Classic remains the default.*?Sci-Fi is opt-in.*?action semantics are unchanged'
$phase10CValidationMatch = Find-RawMatch $planDoc '### Validation and evidence.*?focused control-theme test.*?AMD64 validation uses `build\.ps1 -Arch amd64`'
$phase10CBoundaryMatch = Find-RawMatch $planDoc 'Visual theming completed now:.*?Task Manager functionality that does not yet exist:.*?deliberately deferred product work, not theme defects'
$phase10DHeadingMatch = Find-FirstMatch $planDoc '## Phase 10D - Device Manager Interior Sci-Fi Theming'
$phase10DOwnershipMatch = Find-RawMatch $planDoc 'The repository has no hosted Device Manager implementation.*?bare-metal embedded modal owned by\s*`kernel/core/desktop\.cpp`'
$phase10DModelMatch = Find-FirstMatch $planDoc 'fixed, flat three-column table'
$phase10DModelLimitMatch = Find-FirstMatch $planDoc 'category header'
$phase10DSurfaceMatch = Find-RawMatch $kernelDesktop 'static void draw_device_manager\(\).*?GetBareMetalControlTheme.*?tableHeaderBackground.*?DesktopSelectionColor.*?DesktopControlFillColor'
$phase10DStatusMatch = Find-RawMatch $kernelDesktop 'device_manager_status_color\(.*?roles\.statusWarning.*?device_manager_status_text_color'
$phase10DGuardMatch = Find-RawMatch $kernelDesktop 'device_manager_config_available\(.*?device\.isNetwork.*?device\.statusColor == 0xFF5FB878.*?if \(device_manager_config_available'
$phase10DSafetyMatch = Find-FirstMatch $planDoc 'No device was disabled'
$phase10DSafetyOwnershipMatch = Find-FirstMatch $planDoc 'Driver, PCI'
$phase10DNoNewRoleMatch = Find-FirstMatch $planDoc 'No new reusable role or API was necessary'
$phase10DNoLocalPaletteMatch = Find-FirstMatch $planDoc 'No Device Manager-specific Sci-Fi'
$phase10EHeadingMatch = Find-FirstMatch $planDoc '## Phase 10E - Disk Manager Interior Sci-Fi Theming'
$phase10EOwnershipMatch = Find-RawMatch $planDoc 'Hosted Disk Manager is\s+.*?`disk_manager\.cpp`.*?Bare-metal\s+Disk\s+Manager is\s+`kernel::apps::DiskManagerApp`'
$phase10EModelMatch = Find-RawMatch $planDoc 'left disk\s+list.*?Volumes table.*?partition map'
$phase10ESafetyMatch = Find-RawMatch $planDoc 'does not format disks.*?Create Partition.*?no destructive action is needed'
$phase10ELimitationsMatch = Find-RawMatch $planDoc 'Neither implementation has a hosted or bare-metal scrollbar.*?context\s+menu.*?per-partition selection model'
$diskHostedAccessorMatch = Find-FirstMatch $diskManager 'GetDesktopControlTheme\(GetCurrentDesktopTheme\(\)\)'
$diskHostedHeaderMatch = Find-FirstMatch $diskManager 'tableHeaderBackground|tableHeaderText'
$diskHostedSelectionMatch = Find-FirstMatch $diskManager 'DesktopSelectionColor'
$diskHostedButtonMatch = Find-FirstMatch $diskManager 'DesktopControlFillColor|DesktopControlBorderColor|DesktopControlTextColor'
$diskHostedWarningMatch = Find-FirstMatch $diskManager 'roles\.statusWarning'
$diskHostedSurfaceMatch = Find-RawMatch $diskManager 'drawVolumesGrid\(.*?drawMountsSection\(.*?drawPartitionMap\(.*?drawActions\('
$diskHostedStatusMatch = Find-RawMatch $diskManager 'diskManagerStatusTextColor.*?roles\.statusWarning.*?roles\.controlHoverBorder'
$diskHostedReadOnlyMatch = Find-RawMatch $diskManager 'tryFormatFAT\(\).*?Format is disabled.*?tryCreatePartitionLargestFree\(\).*?MBR writes are read-only'
$diskBareMetalRolesMatch = Find-RawMatch $kernelApps 'DiskManagerApp::draw\(.*?GetBareMetalControlTheme.*?DesktopSelectionColor.*?tableHeaderBackground.*?roles\.separator'
$diskClassicFallbackMatch = Find-RawMatch $diskManager 'const uint32_t classicColor = hover \? 0xFF3A3A3Au : 0xFF323232u.*?0xFF262626u'
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
$phase4bHeadingMatch = Find-FirstMatch $planDoc '## Phase 4B'
$phase4bBodyMatch = Find-RawMatch $planDoc '## Phase 4B.*?Classic remains the default theme and Sci Fi remains opt-in for this inventory pass\..*?The current background selection is global, not theme-scoped\..*?DisplayOptionsStore persists one wallpaperId and one backgroundScaleMode; desktop\.json also keeps the legacy wallpaper path and desktop\.wallpaper\.id for compatibility\..*?Compositor save/load keeps the same selection in both display-options\.cfg and desktop\.json, and DesktopService::SaveState preserves existing wallpaper fields when it refreshes pinned and recent entries\..*?DesktopTheme\.desktopBackground is a color fallback, not a wallpaper asset binding\..*?On a fresh config, the default background is still the built-in image wallpaper, so Sci Fi does not currently own a separate wallpaper\..*?The dedicated background asset folder is assets/Backgrounds\..*?It currently contains 15 image wallpapers with matching thumbnails:.*?wallpaper_registry\.cpp also defines 8 built-in gradient backgrounds:.*?The registry and UI support BackgroundKind::SolidColor, but there are no built-in solid-color backgrounds today\..*?The hosted compositor uses DesktopWallpaper::DrawGradient and drawBackgroundImageToHdc for its desktop backdrop\..*?The bare-metal framebuffer path uses drawBackgroundGradientToPixels and drawBackgroundImageToPixels\..*?The hosted Sci Fi no-image fallback uses the theme desktopBackground color\..*?Bare-metal does not consult the Sci Fi desktopBackground field and stays on the procedural background state\..*?The current Sci Fi desktop therefore is not a separate wallpaper track; it is the shared global background selection plus hosted Sci Fi color fallback behavior when no image is present\..*?Keep Classic and Sci Fi color-only for now\..*?Allow Sci Fi to suggest a bundled wallpaper while Classic remains unchanged\..*?Add a theme-specific default wallpaper later, still opt-in only, with explicit override rules for user wallpaper\..*?Keep user-selected wallpaper independent of theme; that is already the current global model\..*?Add a Display Options wallpaper UI later\..*?Keep bare-metal wallpaper parity deferred\..*?This pass does not promote a new default wallpaper\..*?No defaults changed\..*?No assets were added, removed, moved, or renamed\..*?No per-effect controls were introduced\..*?Wallpaper selection remains deferred until an implementation phase\.'
$phase4cHeadingMatch = Find-FirstMatch $planDoc '## Phase 4C'
$phase4cBodyMatch = Find-RawMatch $planDoc '## Phase 4C.*?Phase 4C adds a small opt-in Sci Fi wallpaper recommendation note in Display Options\..*?It is read-only and does not change the wallpaper automatically, the default wallpaper, the default theme, or the global wallpaper model\..*?The Theme tab now shows a read-only recommendation when Sci Fi is selected\..*?The recommendation points to existing bundled backgrounds only, and the user still has to choose a wallpaper through the existing Background tab controls\..*?No theme-scoped wallpaper persistence was added\..*?Classic remains the default theme\..*?Sci Fi remains opt-in\..*?No default wallpaper changed\..*?No assets were added, removed, moved, or renamed\..*?Bare-metal wallpaper parity remains deferred\.'
$phase4c1HeadingMatch = Find-FirstMatch $planDoc '## Phase 4C\.1'
$phase4c1BodyMatch = Find-RawMatch $planDoc '## Phase 4C\.1.*?stabilization-only review pass for the Phase 4C Sci Fi wallpaper recommendation note\..*?read-only and opt-in\..*?No wallpaper/default/persistence/assets changed, and no automatic wallpaper switching was added\..*?Theme tab recommendation stays gated to Sci Fi selection and remains read-only\..*?Tiny copy fix: the note now says the recommendation is optional and tells users to pick any wallpaper on the Background tab\..*?Placement was reviewed and kept below the theme cards and above the footer action text\..*?No theme-scoped wallpaper persistence was added\..*?Classic remains the default theme\..*?Sci Fi remains opt-in\..*?No default wallpaper changed\..*?No assets were added, removed, moved, or renamed\..*?No per-effect controls were introduced\.'
$phase4dHeadingMatch = Find-FirstMatch $planDoc '## Phase 4D'
$phase4dBodyMatch = Find-RawMatch $planDoc '## Phase 4D.*?Phase 4D is the next app-surface pilot for the guideXOS Server theme system\..*?It applies conservative Sci Fi toolbar, address, content, border, and text polish to Navigator while keeping Classic visually close to the current look\..*?It does not change Navigator behavior, navigation logic, page loading, URL/path handling, history, bookmarks, persistence, App Model behavior, theme IDs, persistence keys, or theme metrics\..*?It does not add new effects, blur, glass, animations, rounded clipping, rounded hit-testing, or per-effect controls\..*?\* Navigator is the next app-surface pilot\..*?\* Sci Fi gets conservative toolbar, address, content, border, and text polish so Navigator fits the shell more cleanly\..*?\* Classic is preserved and stays visually close to the current Navigator look\..*?\* No Navigator behavior changed: navigation logic, page loading, URL/path handling, history, bookmarks, and persistence all remain the same\..*?\* No App Model behavior changed\..*?\* No new effects were added\..*?\* Broad app redesign remains deferred\..*?\* Future app polish should continue one app at a time\.'
$phase4d1HeadingMatch = Find-FirstMatch $planDoc '## Phase 4D\.1'
$phase4d1BodyMatch = Find-RawMatch $planDoc '## Phase 4D\.1.*?Phase 4D\.1 is a stabilization-only review pass for the Navigator app-surface pilot\..*?The Navigator pilot was reviewed and stabilized\..*?The shared compositor widget guard was reviewed and tightened to match Navigator windows only\..*?Classic preservation was confirmed; the previous Navigator feel remains close to the current look\..*?Authored page background colors remain respected when present\..*?No Navigator behavior, navigation logic, page loading, URL/path handling, history, bookmarks, persistence, or App Model behavior changed\..*?No new effects, blur, glass, animation, rounded clipping, rounded hit-testing, or per-effect controls were added\..*?Tiny cleanup made: the shared compositor Navigator widget guard now uses a tighter title match to avoid accidental spillover\..*?Tiny readability fix made: Navigator''s default page-body text now tracks the content surface in Sci Fi so dark content stays legible while authored colors still win\..*?No layout or hit-test change was needed in this pass\.'
$phase4e1HeadingMatch = Find-FirstMatch $planDoc '## Phase 4E\.1'
$phase4e1BodyMatch = Find-RawMatch $planDoc '## Phase 4E\.1.*?stabilization-only review pass for the Task Manager app-surface pilot\..*?\* The Task Manager pilot was reviewed and stabilized\..*?\* Readability and graph/status safety were reviewed\..*?\* Row/selection/End Task safety were reviewed\..*?\* Classic preservation stayed close to the prior Task Manager look\..*?\* No monitoring, enumeration, End Task, or App Model behavior changed\..*?\* No new effects were added\..*?\* Tiny readability/layout fix made: none\.'
$phase4eHeadingMatch = Find-FirstMatch $planDoc '## Phase 4E'
$phase4eBodyMatch = Find-RawMatch $planDoc '## Phase 4E.*?Phase 4E is the next app-surface pilot for the guideXOS Server theme system\..*?It applies conservative Sci Fi body, header, list, selection, border, and text polish to Task Manager while keeping Classic visually close to the current look\..*?Task Manager is the next app-surface pilot\..*?Sci Fi gets conservative body, header, list, selection, border, and text polish so Task Manager fits the shell more cleanly\..*?Classic is preserved and stays visually close to the current Task Manager look\..*?No Task Manager behavior changed: process/app enumeration, refresh cadence, kill/close/end-task behavior, and monitoring logic all remain the same\..*?No App Model behavior changed\..*?No new effects were added\..*?Broad app redesign remains deferred\..*?Future app polish should continue one app at a time\.'
$phase4fHeadingMatch = Find-FirstMatch $planDoc '## Phase 4F'
$phase4fBodyMatch = Find-RawMatch $planDoc '## Phase 4F.*?documentation, inventory, and smoke-hardening pass\..*?No per-effect controls were introduced\..*?Classic remains the default theme\..*?Sci Fi remains opt-in\..*?Missing or invalid theme config falls back to Classic\..*?Wallpaper selection remains global, not theme-scoped\..*?The Sci Fi wallpaper recommendation in Display Options remains read-only and optional\..*?No automatic wallpaper switching exists\..*?No theme-scoped wallpaper persistence exists\..*?No App Model gate/status dependency was introduced\..*?No new effects were added\.'
$phase4fInventoryMatch = Find-RawMatch $planDoc '### App-Surface Inventory.*?Display Options: Phase 3A / 3A\.1, plus Phase 4C / 4C\.1; stabilization status: complete in 3A\.1 and 4C\.1; Sci Fi polish covers theme selection copy, the optional read-only wallpaper recommendation note, and conservative panel/card/accent treatment; Classic preservation: default look stays intact and the neutral card feel remains; Behavior changes: No; Safety fix / guard: the Sci Fi recommendation stays gated and read-only, and the earlier fallback tweak preserved the neutral card feel\..*?Control Panel: Phase 3B / 3B\.1; stabilization status: complete in 3B\.1; Sci Fi polish covers conservative panel/card/accent treatment; Classic preservation: look stays close to the prior Control Panel surface; Behavior changes: No; Safety fix / guard: shared grid top offset stays synchronized for drawing and hit-testing\..*?Notepad: Phase 3C / 3C\.1; stabilization status: complete in 3C\.1; Sci Fi polish covers conservative editor/body/border/text treatment; Classic preservation: look stays close to the prior Notepad surface; Behavior changes: No; Safety fix / guard: surface helpers stayed centralized and no visible fallback tweak was needed\..*?Calculator: Phase 3D / 3D\.1; stabilization status: complete in 3D\.1; Sci Fi polish covers conservative body/display/button/accent treatment; Classic preservation: look stays close to the prior Calculator surface; Behavior changes: No; Safety fix / guard: shared compositor widget guard stayed narrow to Calculator and Sci Fi\..*?File Explorer: Phase 3E / 3E\.1; stabilization status: complete in 3E\.1; Sci Fi polish covers conservative body/list/toolbar/address/selection/hover/border/status treatment; Classic preservation: look stays close to the prior File Explorer surface; Behavior changes: No; Safety fix / guard: Sci Fi hover clears on scroll and offset changes so hover stays visual-only\..*?Clock: Phase 3F / 3F\.1; stabilization status: complete in 3F\.1; Sci Fi polish covers conservative body/readout/accent treatment and clear repaint handling; Classic preservation: look stays close to the prior Clock surface; Behavior changes: No; Safety fix / guard: clear-then-redraw removes stale stacked text without changing timekeeping cadence\..*?Navigator: Phase 4D / 4D\.1; stabilization status: complete in 4D\.1; Sci Fi polish covers conservative toolbar/address/content/border/text treatment; Classic preservation: previous Navigator feel remains close to the current look; Behavior changes: No; Safety fix / guard: the shared compositor widget guard now matches Navigator windows only, and authored page colors still win when present\..*?Task Manager: Phase 4E / 4E\.1; stabilization status: complete in 4E\.1; Sci Fi polish covers conservative body/header/list/selection/border/text treatment; Classic preservation: stays close to the prior Task Manager look; Behavior changes: No; Safety fix / guard: readability and graph/status safety were reviewed, and row/selection/End Task safety stayed intact\.'
$phase4fBoundaryMatch = Find-RawMatch $planDoc '### Wallpaper / Default Boundaries.*?Classic remains the default theme\..*?Sci Fi remains opt-in\..*?Missing or invalid theme config falls back to Classic\..*?Wallpaper selection remains global and not theme-scoped\..*?The Sci Fi wallpaper recommendation in Display Options is read-only and optional\..*?No automatic wallpaper switching exists\..*?No theme-scoped wallpaper persistence exists\.'
$phase4fDeferredMatch = Find-RawMatch $planDoc '### Deferred Boundaries.*?Image Viewer stabilization/readability re-check\..*?Other future app-surface polish\..*?Bare-metal theme parity\..*?High-DPI/scaling\..*?Hosted shadow/taskbar polish\..*?Rounded client clipping\..*?Rounded hit-testing\..*?Blur/glass\..*?Animations\..*?Per-effect controls\..*?Theme-scoped wallpaper persistence\..*?Broader app redesign\.'
$phase4fImageViewerMatch = Find-RawMatch $planDoc '### Image Viewer Caution.*?Image Viewer may be a future theme target, but it should be approached carefully because prior large-image, memory, and repaint concerns make it a higher-risk surface for theme changes\..*?This pass does not inspect or modify Image Viewer\.'
$phase4gHeadingMatch = Find-FirstMatch $planDoc '## Phase 4G'
$phase4gBoundaryMatch = Find-RawMatch $planDoc '## Phase 4G.*?Classic remains the default theme\..*?Sci Fi remains opt-in\..*?Missing or invalid theme config falls back to Classic\..*?No per-effect controls were introduced\..*?No Image Viewer runtime behavior changed in this pass\..*?No Image Viewer styling was implemented in this pass\.'
$phase4gInventoryMatch = Find-RawMatch $planDoc '### Readiness Inventory.*?Image Viewer readiness inventory was performed before deciding whether to style the surface\..*?The inventory is hosted-app focused and is meant to guide a later Phase 4H styling pass, not replace it\.'
$phase4gDrawingMatch = Find-RawMatch $planDoc '### Drawing / Repaint Path.*?Hosted Image Viewer paints through its own app-local publish helpers: `MT_DrawText`, `MT_DrawTextAt`, `MT_DrawTextAtColor`, `MT_DrawRect`, `MT_DrawImage`, and `MT_WidgetAdd`\..*?`updateDisplay\(\)` starts each repaint by clearing the compositor''s text, positioned text, rect, and image layers with a form-feed `MT_DrawText` payload, then repaints the window body and content\..*?The image preview itself is drawn through `MT_DrawImage`\..*?The bottom button rows and status text are compositor widgets and positioned text, not a custom Image Viewer skin system\..*?The hosted and bare-metal image viewer paths are separate: hosted Image Viewer uses the compositor IPC path and `gui::ImagePtr`, while bare-metal `ImageViewerApp` uses framebuffer drawing and kernel-side image loading\..*?Stale image/text artifacts are less likely because the repaint path clears the compositor''s draw layers before republishing, but the widget rows persist until rebuilt on resize or initial layout\.'
$phase4gMemoryMatch = Find-RawMatch $planDoc '### Image Loading / Memory Ownership.*?`gui::ImageAdapter::LoadFromFile` handles the hosted app load path and only accepts PNG input in this version\..*?`ImageAdapter` decodes through `PngLoader::LoadFromMemory`, and `PngLoader` allocates a `gui::Image` then copies decoded RGBA pixels into `Image::Pixels`\..*?`Image::~Image` frees the heap pixel buffer with `delete\[\]`\..*?`s_image` is a `std::shared_ptr<gui::Image>`, so replacing it on open or reload releases the previous decoded image once no other references remain\..*?Successful loads also snapshot full pixel data into `HistorySnapshot::pixels`, which means undo/redo and original-state tracking can temporarily duplicate large images\..*?On close, the app exits after sending `MT_Close`; there is no special close-time image cleanup beyond normal shared_ptr/vector destruction\..*?The large-image/freezing concern remains visible in code because the app still keeps full decoded images, full-pixel snapshots, and compositor-side image loads\.'
$phase4gSafeTargetsMatch = Find-RawMatch $planDoc '### Safe Future Styling Targets.*?Window body/background outside the image area\..*?Bottom status strip and separator bands\..*?Empty-state, error, and notice text colors\..*?Border and separator colors around the preview area\..*?Existing compositor widget button fill, border, and text colors, if they are only restyled through the shared widget theme path and do not change widget behavior\..*?Small chrome-color tweaks around the image area, so long as they stay outside the decode, scaling, and buffer lifecycle paths\.'
$phase4gRiskyMatch = Find-RawMatch $planDoc '### Risky / Deferred Areas.*?Image decode/render path\..*?Large-image scaling path\..*?Cached bitmap ownership and preview-buffer ownership\..*?Close/reopen lifecycle\..*?Bare-metal framebuffer and kernel ImageAdapter paths\..*?Any new effects over the image area\..*?Blur/glass, animations, rounded client clipping, rounded hit-testing, and per-pixel image filters\..*?New retained buffers or theme-side image caches\.'
$phase4gPrecautionsMatch = Find-RawMatch $planDoc '### Blockers / Precautions Before Styling.*?Run a manual large-image open/close/reopen check before approving styling\..*?Verify the app releases its preview and snapshot state when switching images and on close\..*?Keep theme helpers outside image buffer ownership and decode paths\..*?Avoid new effects, retained buffers, and per-pixel processing over the image area\..*?Keep bare-metal validation separate; the kernel ImageViewer is a different app and remains out of scope for this pass\..*?Do not alter image loading, image decoding, memory ownership, large-image handling, file open behavior, or preview/window close behavior\.'
$phase4g1HeadingMatch = Find-FirstMatch $planDoc '## Phase 4G\.1'
$phase4g1LifecycleMatch = Find-RawMatch $planDoc '## Phase 4G\.1.*?Hosted lifecycle validation was performed with `desktop\.open` and `gui\.close`; `gui\.start` was unreliable in this environment, so the hosted compositor was bootstrapped through the open path\..*?Images used: `assets/Backgrounds/redflower_thumb\.png`, `assets/Backgrounds/ameoba\.png`, and `assets/Backgrounds/blueflower\.png`\..*?Open, close, reopen, and alternate-image cycles succeeded in one hosted session\..*?The large `ameoba\.png` image opened twice in the same session without an obvious stale surface or repaint artifact\..*?No stale image surfaces, stale text, or stale button chrome were observed in the open/close screenshots\..*?No freeze or crash was observed\..*?Built-in `mem` output stayed at `0 KB peak=0 KB` throughout the run, so no obvious runaway was visible in the harness output\..*?Image Viewer styling is conditionally ready next but only for surrounding chrome, status text, widget fills/borders, and other non-image chrome outside decode, scaling, ownership, and close/reopen lifecycle paths\.'
$phase4g1OffLimitsMatch = Find-RawMatch $planDoc '## Phase 4G\.1.*?Memory and lifecycle risk still remains in the implementation because the app continues to use full decoded images and snapshot buffers, so Image Viewer styling is conditionally ready next but only for surrounding chrome, status text, widget fills/borders, and other non-image chrome outside decode, scaling, ownership, and close/reopen lifecycle paths\.'
$phase4hInventoryMatch = Find-RawMatch $planDoc '### App-Surface Inventory.*?Image Viewer: Phase 4H; stabilization status: chrome-only hosted pilot complete; Sci Fi polish covers body/background, preview border/separators, status strip, and widget chrome outside the image area; Classic preservation: stays close to the current Image Viewer look; Behavior changes: No; Safety fix / guard: styling stayed outside decode/render/scaling/ownership/lifecycle paths, no bare-metal `ImageViewerApp` changes were made, and the shared compositor widget guard is narrow to Image Viewer and Sci Fi\.'
$phase4hHeadingMatch = Find-FirstMatch $planDoc '## Phase 4H'
$phase4hChromeOnlyMatch = Find-RawMatch $planDoc '## Phase 4H.*?chrome-only: the body/background outside the image area, the preview border and separators, the bottom status strip, and the existing widget/button chrome were styled, while image decode/render/scaling/ownership/lifecycle paths stayed untouched\.'
$phase4hOffLimitsMatch = Find-RawMatch $planDoc '## Phase 4H.*?Styling stays outside image decode, render, scaling, ownership, preview-buffer, snapshot/history, and close/reopen lifecycle code\..*?No large-image lifecycle code changed\..*?No bare-metal `ImageViewerApp` changes were made\.'
$phase4hClassicDefaultMatch = Find-RawMatch $planDoc '## Phase 4H.*?Classic remains the default theme, Sci Fi remains opt-in, and no theme IDs, persistence keys, theme metrics, or App Model behavior changed\.'
$phase4hNoPerEffectMatch = Find-RawMatch $planDoc '## Phase 4H.*?No per-effect controls were introduced\.'
$phase4hNoAppModelMatch = Find-RawMatch $planDoc '## Phase 4H.*?No App Model gate/status dependency was introduced\.'
$phase4hImageViewerThemeMatch = Find-FirstMatch $imageViewer 'ImageViewerBodyColor|ImageViewerPanelColor|ImageViewerStatusColor|ImageViewerPreviewBorderColor|ImageViewerSeparatorColor|ImageViewerTextColor|ImageViewerMutedTextColor|ImageViewerAccentColor'
$phase4hImageViewerCompositorMatch = Find-FirstMatch $compositor 'isImageViewerWindow|imageViewerClassicWidgetFillColor|imageViewerSciFiWidgetFillColor|imageViewerSciFiWidgetBorderColor|imageViewerSciFiWidgetTextColor'
$navigatorThemeHelperMatch = Find-FirstMatch $navigator 'NavigatorBodyColor|NavigatorToolbarColor|NavigatorAddressFillColor|NavigatorAddressFocusedBorderColor|NavigatorDocumentDefaultColor|NavigatorContentTextColor|NavigatorContentBorderColor|NavigatorScrollTrackColor|NavigatorScrollThumbColor|NavigatorStatusBarColor|NavigatorStatusBarBorderColor|NavigatorTextColor|NavigatorMutedTextColor|NavigatorAccentColor|NavigatorSelectionColor|NavigatorFindHighlightColor|NavigatorFieldFillColor|NavigatorFieldBorderColor|NavigatorFieldTextColor|NavigatorFieldMutedTextColor|NavigatorButtonFillColor|NavigatorButtonBorderColor|NavigatorButtonTextColor'
$navigatorThemeFieldMatch = Find-FirstMatch $navigator 'windowBackground|windowBorder|accent|mutedAccent|taskbarBackground|taskbarBorder|titleBarText'
$navigatorNoPerEffectMatch = Find-FirstMatch $navigator 'Visual Effects|per-effect'
$taskManagerThemeHelperMatch = Find-FirstMatch $taskManager 'TaskManagerBodyColor|TaskManagerPanelColor|TaskManagerHeaderColor|TaskManagerListColor|TaskManagerRowColor|TaskManagerRowSelectedColor|TaskManagerBorderColor|TaskManagerTextColor|TaskManagerHeaderTextColor|TaskManagerValueTextColor|TaskManagerMutedTextColor|TaskManagerAccentColor|TaskManagerIndicatorColor|GetCurrentDesktopThemeId|GetCurrentDesktopTheme|DesktopThemeId::SciFi'
$taskManagerThemeFieldMatch = Find-FirstMatch $taskManager 'windowBackground|windowBorder|accent|mutedAccent|taskbarBackground|taskbarBorder|titleBarText'
$taskManagerGraphThemeMatch = Find-RawMatch $taskManager 'drawGraphBox\(.*?TaskManagerIndicatorColor\('
$taskManagerStatusThemeMatch = Find-RawMatch $taskManager 'updateStatusBar\(\).*?TaskManagerMutedTextColor\('
$taskManagerNoPerEffectMatch = Find-FirstMatch $taskManager 'Visual Effects|per-effect'
$taskManagerSharedRolesMatch = Find-RawMatch $taskManager 'GetDesktopControlTheme.*?tableHeaderBackground.*?DesktopSelectionColor'
$taskManagerMetricRolesMatch = Find-RawMatch $taskManager 'TaskManagerIndicatorColor\(int metricIndex\).*?controlHoverBorder.*?controlBorder.*?secondaryText'
$taskManagerWarningRoleMatch = Find-RawMatch $taskManager 'TaskManagerStatusWarningColor\(\).*?statusWarning'
$taskManagerEnabledStateMatch = Find-RawMatch $taskManager 'MT_WidgetSetEnabled.*?packWidgetSetEnabled.*?updateActionButtonStates'
$taskManagerActiveTabMatch = Find-RawMatch $taskManager 'isSciFiThemeActive\(\).*?controlFocusBorder'
$kernelTaskManagerSharedRolesMatch = Find-RawMatch $kernelApps 'kernelTaskManagerControlTheme.*?tableHeaderBackground.*?DesktopSelectionColor'
$kernelTaskManagerEnabledStateMatch = Find-RawMatch $kernelApps 'TaskManagerApp::refreshList\(\).*?setWidgetEnabled\(m_endTaskBtnId'
$taskManagerControlRoleMatch = Find-RawMatch $controlThemeHeader 'tableHeaderBackground.*?tableHeaderText'
$navigatorSharedRolesMatch = Find-RawMatch $navigator 'GetDesktopControlTheme.*?inputBackground.*?scrollbarTrack.*?selectionActive'
$navigatorDocumentBoundaryMatch = Find-RawMatch $navigator 'NavigatorDocumentDefaultColor\(\).*?245,\s*247,\s*250.*?bodyStyle\.backgroundColor'
$navigatorEnabledProtocolMatch = Find-RawMatch $navigator 'MT_WidgetSetEnabled.*?packWidgetSetEnabled'
$navigatorCompositorSharedWidgetMatch = Find-RawMatch $compositor 'hostedDefaultWidgetFillColor.*?DesktopControlFillColor'
$navigatorCompositorDisabledMatch = Find-RawMatch $compositor 'hostedWidgetState.*?DesktopControlState::Disabled'
$navigatorCompositorSpecificMatch = Find-FirstMatch $compositor 'isNavigatorWindow|navigatorSciFiWidgetFillColor|navigatorSciFiWidgetBorderColor|navigatorSciFiWidgetTextColor'
$kernelNavigatorThemeMatch = Find-RawMatch $kernelApps 'kernelNavigatorClientColor.*?GetDesktopControlTheme.*?kernelNavigatorAddressFillColor.*?inputBackground.*?kernelNavigatorScrollbarTrackColor.*?scrollbarTrack.*?kernelNavigatorStatusColor'
$kernelNavigatorEnabledMatch = Find-RawMatch $kernelApps 'updateButtons\(\).*?setWidgetEnabled\(m_backBtnId.*?setWidgetEnabled\(m_forwardBtnId'
$kernelNavigatorViewportMatch = Find-FirstMatch $kernelApps 'kernelNavigatorViewportBorderColor|CONTENT_X, contentTop'
$themeRecommendationGateMatch = Find-FirstMatch $displayOptions 's_selectedThemeId == DesktopThemeId::SciFi'
$themeRecommendationTextMatch = Find-RawMatch $displayOptions 'Optional recommendation for Sci Fi: guideXOS Space, guideXOS Space 2, Tron Porsche, CPU\..*?Choose any wallpaper on the Background tab\.'
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
# Keep the phase-heading probes ASCII-tolerant because Windows PowerShell 5.1
# may decode a UTF-8 documentation file without a BOM using the active code
# page, which can change the em dash while leaving the heading text intact.
$phase8aHeadingMatch = Find-FirstMatch $planDoc '## Phase 8A .*Bare-Metal Core Application Surface Consistency'
$kernelNotepadThemeMatch = Find-FirstMatch $kernelApps 'kernelNotepadEditorColor|kernelNotepadSelectionColor|kernelNotepadCaretColor|kernelNotepadMenuSurfaceColor|kernelSciFiThemeActive'
$kernelCalculatorThemeMatch = Find-FirstMatch $kernelApps 'kernelCalculatorBodyColor|kernelCalculatorDisplayColor|kernelCalculatorDisplayBorderColor|kernelCalculatorDisplayTextColor|kernelSciFiThemeActive'
$kernelCalculatorWidgetGuardMatch = Find-RawMatch $kernelCompositor 'calculatorWindow.*sciFiTheme|window->owner->getName().*Calculator'
$phase8aClassicBoundaryMatch = Find-RawMatch $planDoc 'Classic branches retain the prior literal Calculator and Notepad client colors.*?No Calculator or Notepad window/client bounds.*?expected geometry result is \*\*no change\*\*'
$kernelFileExplorerThemeMatch = Find-FirstMatch $kernelApps 'kernelFileExplorerClientColor|kernelFileExplorerToolbarColor|kernelFileExplorerSelectionColor|kernelFileExplorerScrollbarThumbColor|kernelFileExplorerMenuColor|kernelFileExplorerDialogColor'
$kernelFileExplorerWidgetGuardMatch = Find-RawMatch $kernelCompositor 'fileExplorerWindow.*sciFiTheme|owner->getName\(\).*Files|owner->getName\(\).*FileExplorer'
$phase8bHeadingMatch = Find-FirstMatch $planDoc '## Phase 8B .*Bare-Metal File Explorer Surface Consistency'
$phase8bBoundaryMatch = Find-RawMatch $planDoc 'Classic branches retain the prior File Explorer literals.*?No client, toolbar, address, navigation, list, row, icon, scrollbar, footer, overlay, or widget geometry changed'
$phase10FHeadingMatch = Find-FirstMatch $planDoc '## Phase 10F - Network Utilities Sci-Fi Interior Theming'
$phase10FOwnershipMatch = Find-RawMatch $planDoc 'The repository has no hosted Network Settings application.*?bare-metal embedded desktop state.*?Network Adapters.*?TCP/IPv4 Properties'
$phase10FBoundaryMatch = Find-RawMatch $planDoc 'network visual theming.*?network feature/driver\s+development.*?separate networking and Navigator branches'
$phase10FValidationMatch = Find-RawMatch $planDoc 'Hosted validation consists.*?Bare-metal validation uses.*?harness/proof limitation'
$phase10FLimitationsMatch = Find-RawMatch $planDoc 'does not currently publish.*?RX/TX.*?Deferred network UI work'
$networkWidgetThemeMatch = Find-RawMatch $kernelDesktop 'static void draw_network_widget\(TaskbarWidget& widget\).*?roles\.statusWarning'
$networkAdaptersThemeMatch = Find-RawMatch $kernelDesktop 'static void draw_network_adapters\(\).*?GetBareMetalControlTheme\(theme\).*?DesktopSelectionColor\(roles, true\).*?roles\.separator'
$networkConfigThemeMatch = Find-RawMatch $kernelDesktop 'static void draw_network_config\(\).*?roles\.inputBackground.*?roles\.inputBorder.*?roles\.controlDisabled.*?DesktopControlFillColor\(roles, okState\).*?DesktopControlBorderColor\(roles, okState\)'
$networkStatusRolesMatch = Find-RawMatch $kernelDesktop 'network_adapter_status_color\(.*?theme\.accent.*?roles\.statusWarning.*?network_adapter_status_text_color'
$networkClassicFallbackMatch = Find-RawMatch $kernelDesktop 'sciFiTheme \? roles\.panelBackground : rgb\(35, 35, 45\).*?sciFiTheme \? theme\.titleBarBackground : rgb\(50, 70, 110\)'
$networkDataBoundaryMatch = Find-RawMatch $kernelDesktop 'Intel Ethernet Adapter.*?Realtek PCIe GbE Controller.*?TCP/IPv4 Properties.*?Configuration applied'
$phase10GHeadingMatch = Find-FirstMatch $planDoc '## Phase 10G - Remaining System Utility Sci-Fi Theming'
$phase10GInventoryMatch = Find-RawMatch $planDoc '### Remaining GUI utility inventory.*?Trash.*?Native App Debug Viewer.*?HDInstaller'
$phase10GOwnershipMatch = Find-RawMatch $planDoc 'Trash.*?Hosted.*?bare-metal.*?application owns client paint while the compositor owns outer chrome'
$phase10GRationaleMatch = Find-RawMatch $planDoc '### Candidate selection and rationale.*?Trash is the best bounded target'
$trashHostedThemeMatch = Find-FirstMatch $trash 'currentTrashSurfaceColors'
$trashHostedRolesMatch = Find-RawMatch $trash 'GetDesktopControlTheme.*?DesktopSelectionColor.*?roles\.statusWarning'
$trashHostedSurfaceMatch = Find-RawMatch $trash 'Trash::render\(.*?drawSurfaceRect.*?headerBackground.*?propertiesPanel.*?confirmPanel'
$trashHostedButtonMatch = Find-FirstMatch $trash 'addButton\(windowId, 210'
$trashBareMetalRolesMatch = Find-RawMatch $kernelApps 'TrashApp::draw\(.*?GetBareMetalControlTheme.*?DesktopSelectionColor.*?roles\.tableHeaderBackground.*?roles\.separator.*?roles\.statusWarning'
$trashBehaviorMatch = Find-RawMatch $trash 'restoreEntry\(.*?deleteEntryPermanently\(.*?purgeContents\('
$phase10GNoNewRoleMatch = Find-RawMatch $planDoc 'Phase 10G.*?No new shared theme roles or APIs were required'
$phase10GNoLocalPaletteMatch = Find-RawMatch $planDoc 'No Trash-specific Sci-Fi palette'
$phase10HHeadingMatch = Find-FirstMatch $planDoc '## Phase 10H .*Sci-Fi Visual Consistency and High-Visibility Polish'
$phase10HAuditMatch = Find-RawMatch $planDoc '### Starting audit and prioritized findings.*?bare-metal framebuffer compositor.*?generic keyboard-focus publication'
$phase10HHostedRolesMatch = Find-RawMatch $compositor 'const DesktopControlTheme controlRoles = GetDesktopControlTheme\(theme\).*?DesktopSelectionColor\(controlRoles, true\).*?DesktopControlFillColor\(controlRoles, DesktopControlState::Hover\)'
$phase10HBareMetalChromeMatch = Find-RawMatch $compositor 'bareMetalWindowSurfaceColor.*?bareMetalTitleBarColor.*?bareMetalTitleTextColor.*?bareMetalWindowBorderColor.*?bareMetalCloseButtonFillColor'
$phase10HBareMetalStartMatch = Find-RawMatch $compositor 'controlRoles\.panelBackground.*?DesktopSelectionColor\(controlRoles, true\).*?controlRoles\.raisedPanel.*?DesktopControlBorderColor\(controlRoles, DesktopControlState::Normal\)'
$phase10HTextParityMatch = Find-RawMatch $compositor 'sciFiTheme \? controlRoles\.primaryText.*?controlRoles\.secondaryText'
$phase10HClassicBoundaryMatch = Find-RawMatch $planDoc 'Classic branches.*?Sci-Fi remains opt-in.*?[Nn]o geometry.*?no input'
$phase10HFocusLimitMatch = Find-RawMatch $planDoc 'generic widget model.*?universal keyboard-focus field.*?documented limitation'
$phase10HDeferredEffectsMatch = Find-RawMatch $planDoc 'blur.*?glass.*?animation.*?icon redesign'
$iconThemeManager = Join-Path $Root "icon_theme_manager.cpp"
$iconManifestMatch = Find-RawMatch $iconThemeManager 'm_manifest.emplace\("app\.notepad".*?m_manifest.emplace\("trash\.full"'
$iconConsumerMatch = Find-RawMatch $compositor 'drawStartMenuIcon.*?IconThemeManager.*?drawDesktopThemedIcon.*?IconThemeManager'
$fileExplorerIconConsumerMatch = Find-RawMatch $fileExplorer 'void FileExplorer::drawIcon.*?IconThemeManager.*?ResolveIconPath'
$phase10IHeadingMatch = Find-FirstMatch $planDoc '## Phase 10I - Icon Legibility and Visual Evidence Closeout'
$phase10IAuditMatch = Find-RawMatch $planDoc '## Phase 10I.*?### Starting audit.*?Desktop icons.*?Window close.*?found no release-significant icon defect'
$phase10IDefectsMatch = Find-RawMatch $planDoc '### Icon audit result.*?No targeted visual fix was required.*?made no production rendering or asset changes'
$phase10IEvidenceMatch = Find-RawMatch $planDoc '### Evidence and screenshot set.*?phase7c-qemu.*?phase8b-qemu.*?No production screenshot framework was added'
$phase10IClassicMatch = Find-RawMatch $planDoc '### Classic comparison.*?Classic remains the default.*?Classic rendering is unchanged'
$phase10IReleaseMatch = Find-RawMatch $planDoc '### Release freeze decision.*?Freeze A.*?Sci-Fi development should now be frozen'
$phase10INoParallelIconMatch = Find-FirstMatch $planDoc 'did not add a `SciFiIconTheme`'

$checks = @(
    [pscustomobject]@{ Name = "theme header exists"; Pass = (Test-Path -LiteralPath $themeHeader); Match = $null },
    [pscustomobject]@{ Name = "theme source exists"; Pass = (Test-Path -LiteralPath $themeSource); Match = $null },
    [pscustomobject]@{ Name = "classic identifier exists"; Pass = $null -ne $classicMatch; Match = $classicMatch },
    [pscustomobject]@{ Name = "sci fi identifier exists"; Pass = $null -ne $sciFiMatch; Match = $sciFiMatch },
    [pscustomobject]@{ Name = "classic default is represented"; Pass = $null -ne $defaultMatch; Match = $defaultMatch },
    [pscustomobject]@{ Name = "classic remains default"; Pass = $null -ne $defaultMatch; Match = $defaultMatch },
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
    [pscustomobject]@{ Name = "sci fi remains opt-in"; Pass = $null -ne $sciFiOptionMatch; Match = $sciFiOptionMatch },
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
    [pscustomobject]@{ Name = "phase 4b docs heading"; Pass = $null -ne $phase4bHeadingMatch; Match = $phase4bHeadingMatch },
    [pscustomobject]@{ Name = "phase 4b docs body"; Pass = $null -ne $phase4bBodyMatch; Match = $phase4bBodyMatch },
    [pscustomobject]@{ Name = "phase 4c docs heading"; Pass = $null -ne $phase4cHeadingMatch; Match = $phase4cHeadingMatch },
    [pscustomobject]@{ Name = "phase 4c docs body"; Pass = $null -ne $phase4cBodyMatch; Match = $phase4cBodyMatch },
    [pscustomobject]@{ Name = "phase 4c.1 docs heading"; Pass = $null -ne $phase4c1HeadingMatch; Match = $phase4c1HeadingMatch },
    [pscustomobject]@{ Name = "phase 4c.1 docs body"; Pass = $null -ne $phase4c1BodyMatch; Match = $phase4c1BodyMatch },
    [pscustomobject]@{ Name = "phase 4d docs heading"; Pass = $null -ne $phase4dHeadingMatch; Match = $phase4dHeadingMatch },
    [pscustomobject]@{ Name = "phase 4d docs body"; Pass = $null -ne $phase4dBodyMatch; Match = $phase4dBodyMatch },
    [pscustomobject]@{ Name = "phase 4d.1 docs heading"; Pass = $null -ne $phase4d1HeadingMatch; Match = $phase4d1HeadingMatch },
    [pscustomobject]@{ Name = "phase 4d.1 docs body"; Pass = $null -ne $phase4d1BodyMatch; Match = $phase4d1BodyMatch },
    [pscustomobject]@{ Name = "phase 4e.1 docs heading"; Pass = $null -ne $phase4e1HeadingMatch; Match = $phase4e1HeadingMatch },
    [pscustomobject]@{ Name = "phase 4e.1 docs body"; Pass = $null -ne $phase4e1BodyMatch; Match = $phase4e1BodyMatch },
    [pscustomobject]@{ Name = "phase 4e docs heading"; Pass = $null -ne $phase4eHeadingMatch; Match = $phase4eHeadingMatch },
    [pscustomobject]@{ Name = "phase 4e docs body"; Pass = $null -ne $phase4eBodyMatch; Match = $phase4eBodyMatch },
    [pscustomobject]@{ Name = "phase 4e classic remains default"; Pass = $null -ne $defaultMatch; Match = $defaultMatch },
    [pscustomobject]@{ Name = "phase 4e sci fi remains opt-in"; Pass = $null -ne $sciFiMatch; Match = $sciFiMatch },
    [pscustomobject]@{ Name = "phase 4e task manager theme helpers wired"; Pass = $null -ne $taskManagerThemeHelperMatch -or $null -ne $taskManagerThemeFieldMatch; Match = $(if ($null -ne $taskManagerThemeHelperMatch) { $taskManagerThemeHelperMatch } else { $taskManagerThemeFieldMatch }) },
    [pscustomobject]@{ Name = "phase 4e.1 graph/status theme paths wired"; Pass = $null -ne $taskManagerGraphThemeMatch -and $null -ne $taskManagerStatusThemeMatch; Match = $(if ($null -ne $taskManagerGraphThemeMatch) { $taskManagerGraphThemeMatch } else { $taskManagerStatusThemeMatch }) },
    [pscustomobject]@{ Name = "phase 4e no per-effect controls"; Pass = $null -eq $taskManagerNoPerEffectMatch; Match = $taskManagerNoPerEffectMatch },
    [pscustomobject]@{ Name = "phase 10c Task Manager shared roles consumed"; Pass = $null -ne $taskManagerSharedRolesMatch -and $null -ne $kernelTaskManagerSharedRolesMatch; Match = $(if ($null -ne $taskManagerSharedRolesMatch) { $taskManagerSharedRolesMatch } else { $kernelTaskManagerSharedRolesMatch }) },
    [pscustomobject]@{ Name = "phase 10c Task Manager metric roles consumed"; Pass = $null -ne $taskManagerMetricRolesMatch; Match = $taskManagerMetricRolesMatch },
    [pscustomobject]@{ Name = "phase 10c Task Manager warning role consumed"; Pass = $null -ne $taskManagerWarningRoleMatch; Match = $taskManagerWarningRoleMatch },
    [pscustomobject]@{ Name = "phase 10c Task Manager enabled state published"; Pass = $null -ne $taskManagerEnabledStateMatch -and $null -ne $kernelTaskManagerEnabledStateMatch; Match = $(if ($null -ne $taskManagerEnabledStateMatch) { $taskManagerEnabledStateMatch } else { $kernelTaskManagerEnabledStateMatch }) },
    [pscustomobject]@{ Name = "phase 10c Task Manager Sci-Fi active tab state"; Pass = $null -ne $taskManagerActiveTabMatch; Match = $taskManagerActiveTabMatch },
    [pscustomobject]@{ Name = "phase 10c shared table header roles exist"; Pass = $null -ne $taskManagerControlRoleMatch; Match = $taskManagerControlRoleMatch },
    [pscustomobject]@{ Name = "phase 4e no app model status gate dependency"; Pass = $null -eq $smokeNoAppModelStatusMatch; Match = $smokeNoAppModelStatusMatch },
    [pscustomobject]@{ Name = "phase 10d docs heading"; Pass = $null -ne $phase10DHeadingMatch; Match = $phase10DHeadingMatch },
    [pscustomobject]@{ Name = "phase 10d hosted and bare-metal ownership documented"; Pass = $null -ne $phase10DOwnershipMatch; Match = $phase10DOwnershipMatch },
    [pscustomobject]@{ Name = "phase 10d flat Device Manager model documented"; Pass = $null -ne $phase10DModelMatch -and $null -ne $phase10DModelLimitMatch; Match = $(if ($null -ne $phase10DModelMatch) { $phase10DModelMatch } else { $phase10DModelLimitMatch }) },
    [pscustomobject]@{ Name = "phase 10d bare-metal shared roles consumed"; Pass = $null -ne $phase10DSurfaceMatch -and $null -ne $phase10DStatusMatch; Match = $(if ($null -ne $phase10DSurfaceMatch) { $phase10DSurfaceMatch } else { $phase10DStatusMatch }) },
    [pscustomobject]@{ Name = "phase 10d existing Config guard preserved"; Pass = $null -ne $phase10DGuardMatch; Match = $phase10DGuardMatch },
    [pscustomobject]@{ Name = "phase 10d hardware-safety boundary documented"; Pass = $null -ne $phase10DSafetyMatch -and $null -ne $phase10DSafetyOwnershipMatch; Match = $(if ($null -ne $phase10DSafetyMatch) { $phase10DSafetyMatch } else { $phase10DSafetyOwnershipMatch }) },
    [pscustomobject]@{ Name = "phase 10d no new theme role or local palette"; Pass = $null -ne $phase10DNoNewRoleMatch -and $null -ne $phase10DNoLocalPaletteMatch; Match = $(if ($null -ne $phase10DNoNewRoleMatch) { $phase10DNoNewRoleMatch } else { $phase10DNoLocalPaletteMatch }) },
    [pscustomobject]@{ Name = "phase 4f docs heading"; Pass = $null -ne $phase4fHeadingMatch; Match = $phase4fHeadingMatch },
    [pscustomobject]@{ Name = "phase 4f docs body"; Pass = $null -ne $phase4fBodyMatch; Match = $phase4fBodyMatch },
    [pscustomobject]@{ Name = "phase 4f inventory complete"; Pass = $null -ne $phase4fInventoryMatch; Match = $phase4fInventoryMatch },
    [pscustomobject]@{ Name = "phase 4f wallpaper/default boundaries documented"; Pass = $null -ne $phase4fBoundaryMatch; Match = $phase4fBoundaryMatch },
    [pscustomobject]@{ Name = "phase 4f deferred boundaries documented"; Pass = $null -ne $phase4fDeferredMatch; Match = $phase4fDeferredMatch },
    [pscustomobject]@{ Name = "phase 4f image viewer caution documented"; Pass = $null -ne $phase4fImageViewerMatch; Match = $phase4fImageViewerMatch },
    [pscustomobject]@{ Name = "phase 4g docs heading"; Pass = $null -ne $phase4gHeadingMatch; Match = $phase4gHeadingMatch },
    [pscustomobject]@{ Name = "phase 4g default and opt-in boundary documented"; Pass = $null -ne $phase4gBoundaryMatch; Match = $phase4gBoundaryMatch },
    [pscustomobject]@{ Name = "phase 4g readiness inventory documented"; Pass = $null -ne $phase4gInventoryMatch; Match = $phase4gInventoryMatch },
    [pscustomobject]@{ Name = "phase 4g drawing path documented"; Pass = $null -ne $phase4gDrawingMatch; Match = $phase4gDrawingMatch },
    [pscustomobject]@{ Name = "phase 4g loading and memory documented"; Pass = $null -ne $phase4gMemoryMatch; Match = $phase4gMemoryMatch },
    [pscustomobject]@{ Name = "phase 4g safe styling targets documented"; Pass = $null -ne $phase4gSafeTargetsMatch; Match = $phase4gSafeTargetsMatch },
    [pscustomobject]@{ Name = "phase 4g risky deferred areas documented"; Pass = $null -ne $phase4gRiskyMatch; Match = $phase4gRiskyMatch },
    [pscustomobject]@{ Name = "phase 4g blockers precautions documented"; Pass = $null -ne $phase4gPrecautionsMatch; Match = $phase4gPrecautionsMatch },
    [pscustomobject]@{ Name = "phase 4g.1 heading documented"; Pass = $null -ne $phase4g1HeadingMatch; Match = $phase4g1HeadingMatch },
    [pscustomobject]@{ Name = "phase 4g.1 lifecycle result documented"; Pass = $null -ne $phase4g1LifecycleMatch; Match = $phase4g1LifecycleMatch },
    [pscustomobject]@{ Name = "phase 4g.1 chrome-only boundary preserved"; Pass = $null -ne $phase4g1OffLimitsMatch; Match = $phase4g1OffLimitsMatch },
    [pscustomobject]@{ Name = "phase 4h docs heading"; Pass = $null -ne $phase4hHeadingMatch; Match = $phase4hHeadingMatch },
    [pscustomobject]@{ Name = "phase 4h inventory line documented"; Pass = $null -ne $phase4hInventoryMatch; Match = $phase4hInventoryMatch },
    [pscustomobject]@{ Name = "phase 4h chrome-only styling documented"; Pass = $null -ne $phase4hChromeOnlyMatch; Match = $phase4hChromeOnlyMatch },
    [pscustomobject]@{ Name = "phase 4h off-limits boundary documented"; Pass = $null -ne $phase4hOffLimitsMatch; Match = $phase4hOffLimitsMatch },
    [pscustomobject]@{ Name = "phase 4h classic remains default"; Pass = $null -ne $phase4hClassicDefaultMatch; Match = $phase4hClassicDefaultMatch },
    [pscustomobject]@{ Name = "phase 4h sci fi remains opt-in"; Pass = $null -ne $sciFiMatch; Match = $sciFiMatch },
    [pscustomobject]@{ Name = "phase 4h no per-effect controls"; Pass = $null -ne $phase4hNoPerEffectMatch; Match = $phase4hNoPerEffectMatch },
    [pscustomobject]@{ Name = "phase 4h no app model gate/status dependency"; Pass = $null -ne $phase4hNoAppModelMatch; Match = $phase4hNoAppModelMatch },
    [pscustomobject]@{ Name = "phase 4h image viewer theme helpers wired"; Pass = $null -ne $phase4hImageViewerThemeMatch; Match = $phase4hImageViewerThemeMatch },
    [pscustomobject]@{ Name = "phase 4h compositor widget guard wired"; Pass = $null -ne $phase4hImageViewerCompositorMatch; Match = $phase4hImageViewerCompositorMatch },
    [pscustomobject]@{ Name = "manual validation runbook heading"; Pass = $null -ne $manualRunbookHeadingMatch; Match = $manualRunbookHeadingMatch },
    [pscustomobject]@{ Name = "manual validation classic-first note"; Pass = $null -ne $manualRunbookClassicFirstMatch; Match = $manualRunbookClassicFirstMatch },
    [pscustomobject]@{ Name = "manual validation displayoptions launcher"; Pass = $null -ne $manualRunbookLauncherMatch; Match = $manualRunbookLauncherMatch },
    [pscustomobject]@{ Name = "manual validation persisted token note"; Pass = $null -ne $manualRunbookPersistedTokensMatch; Match = $manualRunbookPersistedTokensMatch },
    [pscustomobject]@{ Name = "manual validation screenshot artifact note"; Pass = $null -ne $manualRunbookArtifactMatch; Match = $manualRunbookArtifactMatch },
    [pscustomobject]@{ Name = "display options theme helpers wired"; Pass = $null -ne $displayOptionsThemeHelperMatch; Match = $displayOptionsThemeHelperMatch },
    [pscustomobject]@{ Name = "display options theme fields wired"; Pass = $null -ne $displayOptionsThemeFieldMatch; Match = $displayOptionsThemeFieldMatch },
    [pscustomobject]@{ Name = "display options text color support"; Pass = $null -ne $displayOptionsTextColorMatch; Match = $displayOptionsTextColorMatch },
    [pscustomobject]@{ Name = "display options sci fi wallpaper recommendation gate"; Pass = $null -ne $themeRecommendationGateMatch; Match = $themeRecommendationGateMatch },
    [pscustomobject]@{ Name = "display options sci fi wallpaper recommendation text"; Pass = $null -ne $themeRecommendationTextMatch; Match = $themeRecommendationTextMatch },
    [pscustomobject]@{ Name = "display options effect placeholders remain"; Pass = $null -ne $effectPlaceholderMatch -and $null -ne $phase3aNoEffectsMatch; Match = $(if ($null -ne $phase3aNoEffectsMatch) { $phase3aNoEffectsMatch } else { $effectPlaceholderMatch }) },
    [pscustomobject]@{ Name = "navigator theme helpers wired"; Pass = $null -ne $navigatorThemeHelperMatch -or $null -ne $navigatorThemeFieldMatch; Match = $(if ($null -ne $navigatorThemeHelperMatch) { $navigatorThemeHelperMatch } else { $navigatorThemeFieldMatch }) },
    [pscustomobject]@{ Name = "navigator uses shared compositor widget theming"; Pass = $null -ne $navigatorCompositorSharedWidgetMatch; Match = $navigatorCompositorSharedWidgetMatch },
    [pscustomobject]@{ Name = "navigator has no compositor-specific widget palette"; Pass = $null -eq $navigatorCompositorSpecificMatch; Match = $navigatorCompositorSpecificMatch },
    [pscustomobject]@{ Name = "navigator no per-effect controls"; Pass = $null -eq $navigatorNoPerEffectMatch; Match = $navigatorNoPerEffectMatch },
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
    [pscustomobject]@{ Name = "phase 8a docs heading"; Pass = $null -ne $phase8aHeadingMatch; Match = $phase8aHeadingMatch },
    [pscustomobject]@{ Name = "phase 8a bare-metal Notepad theme path wired"; Pass = $null -ne $kernelNotepadThemeMatch; Match = $kernelNotepadThemeMatch },
    [pscustomobject]@{ Name = "phase 8a bare-metal Calculator theme path wired"; Pass = $null -ne $kernelCalculatorThemeMatch; Match = $kernelCalculatorThemeMatch },
    [pscustomobject]@{ Name = "phase 8a Calculator widget guard scoped"; Pass = $null -ne $kernelCalculatorWidgetGuardMatch; Match = $kernelCalculatorWidgetGuardMatch },
    [pscustomobject]@{ Name = "phase 8a Classic and geometry boundaries documented"; Pass = $null -ne $phase8aClassicBoundaryMatch; Match = $phase8aClassicBoundaryMatch },
    [pscustomobject]@{ Name = "phase 8b bare-metal File Explorer theme path wired"; Pass = $null -ne $kernelFileExplorerThemeMatch; Match = $kernelFileExplorerThemeMatch },
    [pscustomobject]@{ Name = "phase 8b File Explorer widget guard scoped"; Pass = $null -ne $kernelFileExplorerWidgetGuardMatch; Match = $kernelFileExplorerWidgetGuardMatch },
    [pscustomobject]@{ Name = "phase 8b File Explorer Classic and geometry boundaries documented"; Pass = $null -ne $phase8bHeadingMatch -and $null -ne $phase8bBoundaryMatch; Match = $(if ($null -ne $phase8bBoundaryMatch) { $phase8bBoundaryMatch } else { $phase8bHeadingMatch }) },
    [pscustomobject]@{ Name = "compositor start button rect helper wired"; Pass = $null -ne $compositorStartButtonRectMatch; Match = $compositorStartButtonRectMatch },
    [pscustomobject]@{ Name = "phase 10a shared control role structure"; Pass = $null -ne $desktopControlRolesMatch -and $null -ne $desktopControlTokenMatch; Match = $(if ($null -ne $desktopControlTokenMatch) { $desktopControlTokenMatch } else { $desktopControlRolesMatch }) },
    [pscustomobject]@{ Name = "phase 10a control state APIs"; Pass = $null -ne $desktopControlStateMatch; Match = $desktopControlStateMatch },
    [pscustomobject]@{ Name = "phase 10a hosted generic button route"; Pass = $null -ne $hostedSharedButtonMatch; Match = $hostedSharedButtonMatch },
    [pscustomobject]@{ Name = "phase 10a bare-metal shared widget route"; Pass = $null -ne $kernelSharedWidgetMatch; Match = $kernelSharedWidgetMatch },
    [pscustomobject]@{ Name = "phase 10a File Explorer control consumer"; Pass = $null -ne $fileExplorerControlConsumerMatch; Match = $fileExplorerControlConsumerMatch },
    [pscustomobject]@{ Name = "phase 10a Notepad control consumer"; Pass = $null -ne $notepadControlConsumerMatch; Match = $notepadControlConsumerMatch },
    [pscustomobject]@{ Name = "phase 10a Display Options control consumer"; Pass = $null -ne $displayOptionsControlConsumerMatch; Match = $displayOptionsControlConsumerMatch },
    [pscustomobject]@{ Name = "phase 10a focused control test present"; Pass = $null -ne $controlThemeTestMatch; Match = $controlThemeTestMatch },
    [pscustomobject]@{ Name = "phase 10a staged Classic/Sci-Fi boot override"; Pass = $null -ne $startupThemeOverrideMatch; Match = $startupThemeOverrideMatch },
    [pscustomobject]@{ Name = "phase 10a docs heading"; Pass = $null -ne $phase10AHeadingMatch; Match = $phase10AHeadingMatch },
    [pscustomobject]@{ Name = "phase 10a architecture documented"; Pass = $null -ne $phase10AArchitectureMatch; Match = $phase10AArchitectureMatch },
    [pscustomobject]@{ Name = "phase 10a Classic compatibility documented"; Pass = $null -ne $phase10AClassicMatch; Match = $phase10AClassicMatch },
    [pscustomobject]@{ Name = "phase 10a limitations documented"; Pass = $null -ne $phase10ALimitationsMatch; Match = $phase10ALimitationsMatch },
    [pscustomobject]@{ Name = "phase 10b docs heading"; Pass = $null -ne $phase10BHeadingMatch; Match = $phase10BHeadingMatch },
    [pscustomobject]@{ Name = "phase 10b Navigator ownership documented"; Pass = $null -ne $phase10BOwnershipMatch; Match = $phase10BOwnershipMatch },
    [pscustomobject]@{ Name = "phase 10b page/chrome boundary documented"; Pass = $null -ne $phase10BDocumentBoundaryMatch; Match = $phase10BDocumentBoundaryMatch },
    [pscustomobject]@{ Name = "phase 10b validation paths documented"; Pass = $null -ne $phase10BValidationMatch; Match = $phase10BValidationMatch },
    [pscustomobject]@{ Name = "phase 10b limitations documented"; Pass = $null -ne $phase10BLimitationsMatch; Match = $phase10BLimitationsMatch },
    [pscustomobject]@{ Name = "phase 10b Navigator theme helpers wired"; Pass = $null -ne $navigatorThemeHelperMatch; Match = $navigatorThemeHelperMatch },
    [pscustomobject]@{ Name = "phase 10b Navigator shared roles consumed"; Pass = $null -ne $navigatorSharedRolesMatch; Match = $navigatorSharedRolesMatch },
    [pscustomobject]@{ Name = "phase 10b document/chrome boundary preserved"; Pass = $null -ne $navigatorDocumentBoundaryMatch; Match = $navigatorDocumentBoundaryMatch },
    [pscustomobject]@{ Name = "phase 10b hosted enabled state protocol wired"; Pass = $null -ne $navigatorEnabledProtocolMatch; Match = $navigatorEnabledProtocolMatch },
    [pscustomobject]@{ Name = "phase 10b hosted Navigator uses shared widget path"; Pass = $null -ne $navigatorCompositorSharedWidgetMatch; Match = $navigatorCompositorSharedWidgetMatch },
    [pscustomobject]@{ Name = "phase 10b hosted disabled state uses shared lookup"; Pass = $null -ne $navigatorCompositorDisabledMatch; Match = $navigatorCompositorDisabledMatch },
    [pscustomobject]@{ Name = "phase 10b no Navigator-specific compositor palette"; Pass = $null -eq $navigatorCompositorSpecificMatch; Match = $navigatorCompositorSpecificMatch },
    [pscustomobject]@{ Name = "phase 10b bare-metal Navigator shared roles consumed"; Pass = $null -ne $kernelNavigatorThemeMatch; Match = $kernelNavigatorThemeMatch },
    [pscustomobject]@{ Name = "phase 10b bare-metal navigation state wired"; Pass = $null -ne $kernelNavigatorEnabledMatch; Match = $kernelNavigatorEnabledMatch },
    [pscustomobject]@{ Name = "phase 10b bare-metal viewport boundary wired"; Pass = $null -ne $kernelNavigatorViewportMatch; Match = $kernelNavigatorViewportMatch },
    [pscustomobject]@{ Name = "phase 10c docs heading"; Pass = $null -ne $phase10CHeadingMatch; Match = $phase10CHeadingMatch },
    [pscustomobject]@{ Name = "phase 10c Task Manager ownership documented"; Pass = $null -ne $phase10COwnershipMatch; Match = $phase10COwnershipMatch },
    [pscustomobject]@{ Name = "phase 10c Task Manager surfaces documented"; Pass = $null -ne $phase10CSurfacesMatch; Match = $phase10CSurfacesMatch },
    [pscustomobject]@{ Name = "phase 10c shared roles documented"; Pass = $null -ne $phase10CSharedRolesMatch; Match = $phase10CSharedRolesMatch },
    [pscustomobject]@{ Name = "phase 10c Classic and Sci-Fi boundaries documented"; Pass = $null -ne $phase10CClassicMatch; Match = $phase10CClassicMatch },
    [pscustomobject]@{ Name = "phase 10c validation paths documented"; Pass = $null -ne $phase10CValidationMatch; Match = $phase10CValidationMatch },
    [pscustomobject]@{ Name = "phase 10c feature boundaries documented"; Pass = $null -ne $phase10CBoundaryMatch; Match = $phase10CBoundaryMatch },
    [pscustomobject]@{ Name = "phase 10d Device Manager heading"; Pass = $null -ne $phase10DHeadingMatch; Match = $phase10DHeadingMatch },
    [pscustomobject]@{ Name = "phase 10d Device Manager ownership documented"; Pass = $null -ne $phase10DOwnershipMatch; Match = $phase10DOwnershipMatch },
    [pscustomobject]@{ Name = "phase 10d Device Manager table model documented"; Pass = $null -ne $phase10DModelMatch; Match = $phase10DModelMatch },
    [pscustomobject]@{ Name = "phase 10d Device Manager category boundary documented"; Pass = $null -ne $phase10DModelLimitMatch; Match = $phase10DModelLimitMatch },
    [pscustomobject]@{ Name = "phase 10d Device Manager shared surface route"; Pass = $null -ne $phase10DSurfaceMatch; Match = $phase10DSurfaceMatch },
    [pscustomobject]@{ Name = "phase 10d Device Manager status route"; Pass = $null -ne $phase10DStatusMatch; Match = $phase10DStatusMatch },
    [pscustomobject]@{ Name = "phase 10d Device Manager action guard documented"; Pass = $null -ne $phase10DGuardMatch; Match = $phase10DGuardMatch },
    [pscustomobject]@{ Name = "phase 10d Device Manager safety documented"; Pass = $null -ne $phase10DSafetyMatch -and $null -ne $phase10DSafetyOwnershipMatch; Match = $(if ($null -ne $phase10DSafetyMatch) { $phase10DSafetyMatch } else { $phase10DSafetyOwnershipMatch }) },
    [pscustomobject]@{ Name = "phase 10d no new role documented"; Pass = $null -ne $phase10DNoNewRoleMatch; Match = $phase10DNoNewRoleMatch },
    [pscustomobject]@{ Name = "phase 10d no local palette documented"; Pass = $null -ne $phase10DNoLocalPaletteMatch; Match = $phase10DNoLocalPaletteMatch },
    [pscustomobject]@{ Name = "phase 10e Disk Manager heading"; Pass = $null -ne $phase10EHeadingMatch; Match = $phase10EHeadingMatch },
    [pscustomobject]@{ Name = "phase 10e Disk Manager ownership documented"; Pass = $null -ne $phase10EOwnershipMatch; Match = $phase10EOwnershipMatch },
    [pscustomobject]@{ Name = "phase 10e Disk Manager presentation model documented"; Pass = $null -ne $phase10EModelMatch; Match = $phase10EModelMatch },
    [pscustomobject]@{ Name = "phase 10e storage safety boundary documented"; Pass = $null -ne $phase10ESafetyMatch; Match = $phase10ESafetyMatch },
    [pscustomobject]@{ Name = "phase 10e absent surfaces documented"; Pass = $null -ne $phase10ELimitationsMatch; Match = $phase10ELimitationsMatch },
    [pscustomobject]@{ Name = "phase 10e hosted Disk Manager shared roles consumed"; Pass = $null -ne $diskHostedAccessorMatch -and $null -ne $diskHostedHeaderMatch -and $null -ne $diskHostedSelectionMatch -and $null -ne $diskHostedButtonMatch -and $null -ne $diskHostedWarningMatch; Match = $(if ($null -ne $diskHostedHeaderMatch) { $diskHostedHeaderMatch } elseif ($null -ne $diskHostedButtonMatch) { $diskHostedButtonMatch } else { $diskHostedAccessorMatch }) },
    [pscustomobject]@{ Name = "phase 10e hosted Disk Manager surfaces retained"; Pass = $null -ne $diskHostedSurfaceMatch; Match = $diskHostedSurfaceMatch },
    [pscustomobject]@{ Name = "phase 10e hosted Disk Manager status roles consumed"; Pass = $null -ne $diskHostedStatusMatch; Match = $diskHostedStatusMatch },
    [pscustomobject]@{ Name = "phase 10e hosted Disk Manager remains read-only"; Pass = $null -ne $diskHostedReadOnlyMatch; Match = $diskHostedReadOnlyMatch },
    [pscustomobject]@{ Name = "phase 10e bare-metal Disk Manager shared roles consumed"; Pass = $null -ne $diskBareMetalRolesMatch; Match = $diskBareMetalRolesMatch },
    [pscustomobject]@{ Name = "phase 10e hosted Classic branch preserved"; Pass = $null -ne $diskClassicFallbackMatch; Match = $diskClassicFallbackMatch },
    [pscustomobject]@{ Name = "phase 10e Disk Manager header exists"; Pass = Test-Path -LiteralPath $diskManagerHeader; Match = $null },
    [pscustomobject]@{ Name = "phase 10f Network Utilities heading"; Pass = $null -ne $phase10FHeadingMatch; Match = $phase10FHeadingMatch },
    [pscustomobject]@{ Name = "phase 10f network ownership documented"; Pass = $null -ne $phase10FOwnershipMatch; Match = $phase10FOwnershipMatch },
    [pscustomobject]@{ Name = "phase 10f visual/feature boundary documented"; Pass = $null -ne $phase10FBoundaryMatch; Match = $phase10FBoundaryMatch },
    [pscustomobject]@{ Name = "phase 10f validation and harness limits documented"; Pass = $null -ne $phase10FValidationMatch; Match = $phase10FValidationMatch },
    [pscustomobject]@{ Name = "phase 10f deferred network UI documented"; Pass = $null -ne $phase10FLimitationsMatch; Match = $phase10FLimitationsMatch },
    [pscustomobject]@{ Name = "phase 10f taskbar network widget consumes shared status roles"; Pass = $null -ne $networkWidgetThemeMatch; Match = $networkWidgetThemeMatch },
    [pscustomobject]@{ Name = "phase 10f adapter list consumes shared selection and separator roles"; Pass = $null -ne $networkAdaptersThemeMatch; Match = $networkAdaptersThemeMatch },
    [pscustomobject]@{ Name = "phase 10f IPv4 form consumes shared input and control roles"; Pass = $null -ne $networkConfigThemeMatch; Match = $networkConfigThemeMatch },
    [pscustomobject]@{ Name = "phase 10f status semantics consume shared roles"; Pass = $null -ne $networkStatusRolesMatch; Match = $networkStatusRolesMatch },
    [pscustomobject]@{ Name = "phase 10f Classic network fallback remains present"; Pass = $null -ne $networkClassicFallbackMatch; Match = $networkClassicFallbackMatch },
    [pscustomobject]@{ Name = "phase 10f network data and action strings remain unchanged"; Pass = $null -ne $networkDataBoundaryMatch; Match = $networkDataBoundaryMatch },
    [pscustomobject]@{ Name = "phase 10g heading"; Pass = $null -ne $phase10GHeadingMatch; Match = $phase10GHeadingMatch },
    [pscustomobject]@{ Name = "phase 10g remaining utility inventory documented"; Pass = $null -ne $phase10GInventoryMatch; Match = $phase10GInventoryMatch },
    [pscustomobject]@{ Name = "phase 10g Trash ownership documented"; Pass = $null -ne $phase10GOwnershipMatch; Match = $phase10GOwnershipMatch },
    [pscustomobject]@{ Name = "phase 10g target rationale documented"; Pass = $null -ne $phase10GRationaleMatch; Match = $phase10GRationaleMatch },
    [pscustomobject]@{ Name = "phase 10g hosted Trash theme helper exists"; Pass = $null -ne $trashHostedThemeMatch; Match = $trashHostedThemeMatch },
    [pscustomobject]@{ Name = "phase 10g hosted Trash consumes shared roles"; Pass = $null -ne $trashHostedRolesMatch; Match = $trashHostedRolesMatch },
    [pscustomobject]@{ Name = "phase 10g hosted Trash surfaces themed"; Pass = $null -ne $trashHostedSurfaceMatch; Match = $trashHostedSurfaceMatch },
    [pscustomobject]@{ Name = "phase 10g hosted Trash action controls retained"; Pass = $null -ne $trashHostedButtonMatch; Match = $trashHostedButtonMatch },
    [pscustomobject]@{ Name = "phase 10g bare-metal Trash consumes shared roles"; Pass = $null -ne $trashBareMetalRolesMatch; Match = $trashBareMetalRolesMatch },
    [pscustomobject]@{ Name = "phase 10g Trash behavior boundaries retained"; Pass = $null -ne $trashBehaviorMatch; Match = $trashBehaviorMatch },
    [pscustomobject]@{ Name = "phase 10g no new role documented"; Pass = $null -ne $phase10GNoNewRoleMatch; Match = $phase10GNoNewRoleMatch },
    [pscustomobject]@{ Name = "phase 10g no local palette documented"; Pass = $null -ne $phase10GNoLocalPaletteMatch; Match = $phase10GNoLocalPaletteMatch },
    [pscustomobject]@{ Name = "phase 10h heading"; Pass = $null -ne $phase10HHeadingMatch; Match = $phase10HHeadingMatch },
    [pscustomobject]@{ Name = "phase 10h starting audit documented"; Pass = $null -ne $phase10HAuditMatch; Match = $phase10HAuditMatch },
    [pscustomobject]@{ Name = "phase 10h hosted shared control roles consumed"; Pass = $null -ne $phase10HHostedRolesMatch; Match = $phase10HHostedRolesMatch },
    [pscustomobject]@{ Name = "phase 10h bare-metal chrome roles consumed"; Pass = $null -ne $phase10HBareMetalChromeMatch; Match = $phase10HBareMetalChromeMatch },
    [pscustomobject]@{ Name = "phase 10h bare-metal Start roles consumed"; Pass = $null -ne $phase10HBareMetalStartMatch; Match = $phase10HBareMetalStartMatch },
    [pscustomobject]@{ Name = "phase 10h shell text parity documented"; Pass = $null -ne $phase10HTextParityMatch; Match = $phase10HTextParityMatch },
    [pscustomobject]@{ Name = "phase 10h Classic compatibility boundary documented"; Pass = $null -ne $phase10HClassicBoundaryMatch; Match = $phase10HClassicBoundaryMatch },
    [pscustomobject]@{ Name = "phase 10h focus limitation documented"; Pass = $null -ne $phase10HFocusLimitMatch; Match = $phase10HFocusLimitMatch },
    [pscustomobject]@{ Name = "phase 10h high-cost effects deferred"; Pass = $null -ne $phase10HDeferredEffectsMatch; Match = $phase10HDeferredEffectsMatch },
    [pscustomobject]@{ Name = "phase 10i shared icon manifest remains bounded"; Pass = $null -ne $iconManifestMatch; Match = $iconManifestMatch },
    [pscustomobject]@{ Name = "phase 10i desktop and Start icon consumers remain shared"; Pass = $null -ne $iconConsumerMatch; Match = $iconConsumerMatch },
    [pscustomobject]@{ Name = "phase 10i File Explorer icon consumer remains shared"; Pass = $null -ne $fileExplorerIconConsumerMatch; Match = $fileExplorerIconConsumerMatch },
    [pscustomobject]@{ Name = "phase 10i heading"; Pass = $null -ne $phase10IHeadingMatch; Match = $phase10IHeadingMatch },
    [pscustomobject]@{ Name = "phase 10i icon audit documented"; Pass = $null -ne $phase10IAuditMatch; Match = $phase10IAuditMatch },
    [pscustomobject]@{ Name = "phase 10i icon result and scope documented"; Pass = $null -ne $phase10IDefectsMatch; Match = $phase10IDefectsMatch },
    [pscustomobject]@{ Name = "phase 10i screenshot evidence documented"; Pass = $null -ne $phase10IEvidenceMatch; Match = $phase10IEvidenceMatch },
    [pscustomobject]@{ Name = "phase 10i Classic comparison documented"; Pass = $null -ne $phase10IClassicMatch; Match = $phase10IClassicMatch },
    [pscustomobject]@{ Name = "phase 10i release freeze decision documented"; Pass = $null -ne $phase10IReleaseMatch; Match = $phase10IReleaseMatch },
    [pscustomobject]@{ Name = "phase 10i no parallel icon subsystem documented"; Pass = $null -ne $phase10INoParallelIconMatch; Match = $phase10INoParallelIconMatch }
)

$failures = 0
foreach ($check in $checks) {
    if (-not $check.Pass) {
        $failures++
        Write-Error ("FAILED CHECK: {0}" -f $check.Name)
    }
    Emit-Check -Name $check.Name -Pass $check.Pass -Match $check.Match
}

if ($failures -gt 0) {
    throw "$failures theme smoke check(s) failed."
}

Write-Host ""
Write-Host "Theme system smoke passed."
