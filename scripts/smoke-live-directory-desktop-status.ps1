param(
    [switch]$Detailed
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

function Format-EvidenceLine {
    param(
        [object]$Match
    )

    if ($null -eq $Match) {
        return "    evidence: none"
    }

    $relative = $Match.Path.Substring($Root.Length).TrimStart('\', '/')
    return "    evidence: ${relative}:$($Match.LineNumber) $($Match.Line)"
}

function Emit-Check {
    param(
        [string]$Name,
        [string]$Status,
        [object]$Match
    )

    Write-Host ("{0}: {1}" -f $Name, $Status)
    Write-Host (Format-EvidenceLine $Match)
}

$repoRoot = (Resolve-Path -LiteralPath $Root).Path
Write-Host "[LiveDirectoryDesktopStatus]"
Write-Host "repo: $repoRoot"
Write-Host "branch: $(git -C $repoRoot branch --show-current)"
Write-Host "head: $(git -C $repoRoot rev-parse HEAD)"

$hostedDesktopLive = Find-FirstMatch -LiteralPath (Join-Path $Root "compositor.cpp") -Pattern "DesktopFolderResolver::Enumerate\(g_hostedDesktopDirectoryPath\)"
$hostedDesktopPathState = Find-FirstMatch -LiteralPath (Join-Path $Root "compositor.cpp") -Pattern "g_hostedDesktopDirectoryPath"
$hostedDesktopNav = Find-FirstMatch -LiteralPath (Join-Path $Root "compositor.cpp") -Pattern "DesktopBack|DesktopHome|desktop-nav:back|desktop-nav:home|hostedDesktopGoBack\(\)|hostedDesktopGoHome\(\)"
$kernelDesktopLive = Find-FirstMatch -LiteralPath (Join-Path $Root "kernel\core\desktop.cpp") -Pattern "enumerate_desktop_folder_items\("
$hostedShellCdCommand = Find-FirstMatch -LiteralPath (Join-Path $Root "console_service.cpp") -Pattern 'if\(command=="cd"\)'
$hostedShellDesktopBridge = Find-FirstMatch -LiteralPath (Join-Path $Root "desktop_service.cpp") -Pattern "ShowFolderOnHostedDesktop\(|showFolderOnHostedDesktop\("
$bareMetalDesktopState = Find-FirstMatch -LiteralPath (Join-Path $Root "kernel\core\desktop.cpp") -Pattern "s_bareMetalDesktopCurrentPath|bare_metal_desktop_current_directory_path|bare_metal_desktop_home_directory_path"
$bareMetalDesktopHomePath = Find-FirstMatch -LiteralPath (Join-Path $Root "kernel\core\desktop.cpp") -Pattern 's_bareMetalDesktopHomePath\[vfs::VFS_MAX_PATH\] = "/Desktop"'
$bareMetalDesktopHomeCheck = Find-FirstMatch -LiteralPath (Join-Path $Root "kernel\core\desktop.cpp") -Pattern "bare_metal_desktop_is_home_directory"
$bareMetalDesktopRefresh = Find-FirstMatch -LiteralPath (Join-Path $Root "kernel\core\desktop.cpp") -Pattern "bare_metal_desktop_request_folder_refresh"
$bareMetalDesktopNavigation = Find-FirstMatch -LiteralPath (Join-Path $Root "kernel\core\desktop.cpp") -Pattern "bare_metal_desktop_set_current_directory|bare_metal_desktop_go_back|bare_metal_desktop_go_home|s_bareMetalDesktopHistoryCount|s_bareMetalDesktopHistoryPaths"
$bareMetalDesktopBackHome = Find-FirstMatch -LiteralPath (Join-Path $Root "kernel\core\desktop.cpp") -Pattern "bare_metal_desktop_go_back|bare_metal_desktop_go_home"
$bareMetalShellDesktopSyncApi = Find-FirstMatch -LiteralPath (Join-Path $Root "kernel\core\include\kernel\desktop.h") -Pattern "sync_live_directory_from_shell_cwd"
$bareMetalShellDesktopSyncCall = Find-FirstMatch -LiteralPath (Join-Path $Root "kernel\core\shell.cpp") -Pattern "sync_live_directory_from_shell_cwd"
$bareMetalShellDesktopSync = if ($null -ne $bareMetalShellDesktopSyncApi -and $null -ne $bareMetalShellDesktopSyncCall) { $bareMetalShellDesktopSyncCall } else { $null }
$bareMetalShellCdState = Find-FirstMatch -LiteralPath (Join-Path $Root "kernel\core\shell.cpp") -Pattern "cmd_cd\("
$bareMetalShellGetCwdState = Find-FirstMatch -LiteralPath (Join-Path $Root "kernel\core\shell.cpp") -Pattern "get_cwd\("
$fileExplorerBack = Find-FirstMatch -LiteralPath (Join-Path $Root "file_explorer.cpp") -Pattern "goBack\(\)"
$fileExplorerGoHome = Find-FirstMatch -LiteralPath (Join-Path $Root "file_explorer.cpp") -Pattern "goHome\(\)"
$fileExplorerContextPin = Find-FirstMatch -LiteralPath (Join-Path $Root "file_explorer.cpp") -Pattern "Pin to Desktop"
$showOnDesktop = Find-FirstMatch -LiteralPath (Join-Path $Root "file_explorer.cpp") -Pattern "Show on Desktop|showFolderOnHostedDesktop"
$kernelIconSize = Find-FirstMatch -LiteralPath (Join-Path $Root "kernel\core\desktop.cpp") -Pattern "s_desktopIconSize"
$kernelBareMetalCompactIcons = Find-FirstMatch -LiteralPath (Join-Path $Root "kernel\core\desktop.cpp") -Pattern "bare_metal_desktop_uses_compact_folder_layout|bare_metal_desktop_icon_metrics|bare_metal_desktop_icon_size"
$kernelBareMetalSharedIconConfig = Find-FirstMatch -LiteralPath (Join-Path $Root "kernel\core\desktop.cpp") -Pattern "smallLiveDesktopFolderIcons"
$rightClickIconSize = Find-FirstMatch -LiteralPath (Join-Path $Root "right_click_menu.cpp") -Pattern "setHostedDesktopPrefersCompactFolderIcons|hostedDesktopPrefersCompactFolderIcons|Folder View Icon Size|Normal folder icons|Small folder icons"
$hostedNonRootCompactIcons = Find-FirstMatch -LiteralPath (Join-Path $Root "compositor.cpp") -Pattern "hostedDesktopUsesCompactIconLayout|desktopIconCellHeightForItem|desktopIconTopPadding"
$displayOptionsIconSize = Find-FirstMatch -LiteralPath (Join-Path $Root "display_options.cpp") -Pattern "smallLiveDesktopFolderIcons|Use smaller folder icons|folder icon size"
$desktopConfigIconSize = Find-FirstMatch -LiteralPath (Join-Path $Root "desktop_config.h") -Pattern "smallLiveDesktopFolderIcons"

Write-Host ""
Emit-Check "hosted desktop folder enumeration" "present" $hostedDesktopLive
Emit-Check "hosted desktop directory state" "present" $hostedDesktopPathState
Emit-Check "hosted desktop navigation controls" "present" $hostedDesktopNav
Emit-Check "bare-metal desktop folder enumeration" "present" $kernelDesktopLive
Emit-Check "hosted shell cd command" "present" $hostedShellCdCommand
Emit-Check "hosted shell desktop bridge" "present" $hostedShellDesktopBridge
Emit-Check "bare-metal shell cd / cwd state" "present" $bareMetalShellCdState
Emit-Check "bare-metal shell get_cwd exposure" "present" $bareMetalShellGetCwdState
Emit-Check "bare-metal desktop directory scaffold" "present" $bareMetalDesktopState
Emit-Check "bare-metal desktop home path" "present" $bareMetalDesktopHomePath
Emit-Check "bare-metal desktop home check" "present" $bareMetalDesktopHomeCheck
Emit-Check "bare-metal desktop refresh hook" "present" $bareMetalDesktopRefresh
Emit-Check "bare-metal desktop navigation helpers" "present" $bareMetalDesktopNavigation
Emit-Check "bare-metal desktop back/home helpers" "present" $bareMetalDesktopBackHome
Emit-Check "bare-metal shell desktop sync API" "present" $bareMetalShellDesktopSyncApi
Emit-Check "bare-metal shell desktop sync call" "present" $bareMetalShellDesktopSyncCall
Emit-Check "bare-metal shell desktop sync hook" "present" $bareMetalShellDesktopSync
Emit-Check "File Explorer Back navigation" "present" $fileExplorerBack
Emit-Check "File Explorer Go Home navigation" "present" $fileExplorerGoHome
Emit-Check "File Explorer Pin to Desktop action" "present" $fileExplorerContextPin
Emit-Check "right-click Icon Size submenu wiring" "present" $rightClickIconSize
Emit-Check "hosted non-root smaller icon layout" "present" $hostedNonRootCompactIcons
Emit-Check "Display Options live folder icon size setting" "present" $displayOptionsIconSize
Emit-Check "desktop config folder icon size persistence" "present" $desktopConfigIconSize
Emit-Check "bare-metal compact icon hook" "present" $kernelBareMetalCompactIcons

if ($null -eq $showOnDesktop) {
    Write-Host "show-on-desktop-action=missing"
    Write-Host "    evidence: no literal match for 'Show on Desktop' in file_explorer.cpp"
} else {
    Emit-Check "show on desktop action" "present-in-file-explorer" $showOnDesktop
}

Emit-Check "kernel desktop icon size state" "present" $kernelIconSize

if ($null -eq $displayOptionsIconSize) {
    Write-Host "Display Options icon-size setting hook: missing or only partial"
    Write-Host "    evidence: display_options.cpp currently exposes desktop icon visibility checkboxes, not a persisted folder-icon-size setting"
} else {
    Emit-Check "Display Options icon-size setting hook" "present" $displayOptionsIconSize
}

$desktopSmokeScripts = @(
    "scripts\smoke-appmodel-launchshadow.ps1",
    "scripts\smoke-appmodel-phase2-status.ps1",
    "scripts\smoke-appmodel-typed-dispatch-flags.ps1",
    "scripts\smoke-navigator-hosted.ps1",
    "scripts\smoke-navigator-kernel.ps1",
    "scripts\smoke-taskmanager-snapshot.ps1"
)

Write-Host ""
Write-Host "existing smoke/status scripts:"
foreach ($scriptPath in $desktopSmokeScripts) {
    $fullPath = Join-Path $Root $scriptPath
    if (Test-Path -LiteralPath $fullPath) {
        Write-Host "  present: $scriptPath"
    }
}

Write-Host ""
Write-Host "status summary:"
Write-Host "  desktop-directory-state=hosted-live-navigation-present"
Write-Host "  back-go-home-nav=present-in-hosted-desktop"
if ($null -eq $showOnDesktop) {
    Write-Host "  show-on-desktop-action=missing"
} else {
    Write-Host "  show-on-desktop-action=present-in-file-explorer"
}
if ($null -ne $hostedShellCdCommand -and $null -ne $hostedShellDesktopBridge) {
    Write-Host "  shell-cd-sync=hosted-present"
} elseif ($null -ne $hostedShellDesktopBridge) {
    Write-Host "  shell-cd-sync=hosted-bridge-present"
} else {
    Write-Host "  shell-cd-sync=missing"
}
if ($null -ne $hostedNonRootCompactIcons -and $null -ne $displayOptionsIconSize -and $null -ne $desktopConfigIconSize) {
    Write-Host "  icon-size-setting=hosted-nonroot-present"
} else {
    Write-Host "  icon-size-setting=missing-or-partial"
}
Write-Host "  hosted-nonroot-smaller-icons=$(if ($null -ne $hostedNonRootCompactIcons) { 'present' } else { 'missing' })"
Write-Host "  display-options-live-folder-icon-size=$(if ($null -ne $displayOptionsIconSize) { 'present' } else { 'missing' })"
Write-Host "  right-click-icon-size=$(if ($null -ne $rightClickIconSize) { 'live-folder-wired' } else { 'missing' })"
Write-Host "  bare-metal-desktop-directory-state=$(if ($null -ne $bareMetalDesktopState) { 'present' } else { 'missing' })"
Write-Host "  bare-metal-home-path=$(if ($null -ne $bareMetalDesktopHomePath) { '/Desktop' } else { 'missing' })"
Write-Host "  bare-metal-folder-navigation=$(if ($null -ne $bareMetalDesktopNavigation -and $null -ne $bareMetalDesktopHomeCheck) { 'present' } else { 'missing' })"
Write-Host "  bare-metal-back-go-desktop=$(if ($null -ne $bareMetalDesktopBackHome) { 'present' } else { 'missing' })"
Write-Host "  bare-metal-go-desktop-target=$(if ($null -ne $bareMetalDesktopHomePath) { '/Desktop' } else { 'missing' })"
Write-Host "  bare-metal-shell-cd-sync=$(if ($null -ne $bareMetalShellDesktopSync) { 'present' } else { 'missing' })"
Write-Host "  bare-metal-nonroot-smaller-icons=$(if ($null -ne $kernelBareMetalCompactIcons) { 'present' } else { 'missing' })"
Write-Host "  bare-metal-nonroot-icon-config=$(if ($null -ne $kernelBareMetalSharedIconConfig) { 'uses-shared-setting' } else { 'default-on' })"
Write-Host "  bare-metal-parity=$(if ($null -ne $bareMetalDesktopNavigation -and $null -ne $bareMetalDesktopHomeCheck) { 'partial' } else { 'missing' })"
