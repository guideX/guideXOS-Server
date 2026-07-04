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
$currentHead = (git -C $repoRoot rev-parse HEAD).Trim()
Write-Host "head: $currentHead"

function Get-LatestWriteTime {
    param(
        [string[]]$Paths
    )

    $latest = [DateTime]::MinValue
    foreach ($path in $Paths) {
        if (Test-Path -LiteralPath $path) {
            $item = Get-Item -LiteralPath $path
            if ($item.LastWriteTimeUtc -gt $latest) {
                $latest = $item.LastWriteTimeUtc
            }
        }
    }
    return $latest
}

$hostedDesktopLive = Find-FirstMatch -LiteralPath (Join-Path $Root "compositor.cpp") -Pattern "DesktopFolderResolver::Enumerate\(g_hostedDesktopDirectoryPath\)"
$hostedDesktopReservedFilter = Find-FirstMatch -LiteralPath (Join-Path $Root "compositor.cpp") -Pattern "IsReservedDesktopName|skipped reserved desktop name|skipped reserved desktop target"
$hostedDesktopStalePositionPrune = Find-FirstMatch -LiteralPath (Join-Path $Root "compositor.cpp") -Pattern "Pruned stale desktop icon position after refresh"
$hostedDesktopPathState = Find-FirstMatch -LiteralPath (Join-Path $Root "compositor.cpp") -Pattern "g_hostedDesktopDirectoryPath"
$hostedDesktopNav = Find-FirstMatch -LiteralPath (Join-Path $Root "compositor.cpp") -Pattern "makeSystemDesktopItem\(DesktopSystemObjectKind::DesktopBack|makeSystemDesktopItem\(DesktopSystemObjectKind::DesktopHome|hostedDesktopGoBack\(\)|hostedDesktopGoHome\(\)|desktop-nav:back|desktop-nav:home"
$kernelDesktopLive = Find-FirstMatch -LiteralPath (Join-Path $Root "kernel\core\desktop.cpp") -Pattern "enumerate_desktop_folder_items\("
$kernelDesktopReservedFilter = Find-FirstMatch -LiteralPath (Join-Path $Root "kernel\core\desktop.cpp") -Pattern "bare_metal_desktop_is_reserved_entry_name|skipped \(reserved desktop name\)|skipped \(duplicate path\)"
$hostedShellCdCommand = Find-FirstMatch -LiteralPath (Join-Path $Root "console_service.cpp") -Pattern 'if\(command=="cd"\)'
$hostedShellDesktopBridge = Find-FirstMatch -LiteralPath (Join-Path $Root "desktop_service.cpp") -Pattern "ShowFolderOnHostedDesktop\(|showFolderOnHostedDesktop\("
$bareMetalDesktopState = Find-FirstMatch -LiteralPath (Join-Path $Root "kernel\core\desktop.cpp") -Pattern "s_bareMetalDesktopCurrentPath|bare_metal_desktop_current_directory_path|bare_metal_desktop_home_directory_path"
$bareMetalDesktopHomePath = Find-FirstMatch -LiteralPath (Join-Path $Root "kernel\core\desktop.cpp") -Pattern 's_bareMetalDesktopHomePath\[vfs::VFS_MAX_PATH\] = "/Desktop"'
$bareMetalDesktopHomeCheck = Find-FirstMatch -LiteralPath (Join-Path $Root "kernel\core\desktop.cpp") -Pattern "bare_metal_desktop_is_home_directory"
$bareMetalDesktopRefresh = Find-FirstMatch -LiteralPath (Join-Path $Root "kernel\core\desktop.cpp") -Pattern "bare_metal_desktop_request_folder_refresh"
$bareMetalDesktopManualRefresh = Find-FirstMatch -LiteralPath (Join-Path $Root "kernel\core\desktop.cpp") -Pattern 'bare_metal_desktop_request_folder_refresh\("right-click refresh"\)'
$bareMetalDesktopStartupSyncApi = Find-FirstMatch -LiteralPath (Join-Path $Root "kernel\core\include\kernel\desktop.h") -Pattern "refresh_bare_metal_desktop_folders_after_vfs_ready"
$bareMetalDesktopStartupSyncCall = Find-FirstMatch -LiteralPath (Join-Path $Root "kernel\core\main.cpp") -Pattern "refresh_bare_metal_desktop_folders_after_vfs_ready\("
$bareMetalDesktopStartupSync = if ($null -ne $bareMetalDesktopStartupSyncApi -and $null -ne $bareMetalDesktopStartupSyncCall) { $bareMetalDesktopStartupSyncCall } else { $null }
$bareMetalDesktopIconInitMarkers = Find-FirstMatch -LiteralPath (Join-Path $Root "kernel\core\desktop.cpp") -Pattern "bare-metal desktop icon init starting|bare-metal desktop icon init completed"
$bareMetalDesktopBackingPathMarker = Find-FirstMatch -LiteralPath (Join-Path $Root "kernel\core\desktop.cpp") -Pattern "bare-metal desktop backing path chosen"
$bareMetalDesktopNavigation = Find-FirstMatch -LiteralPath (Join-Path $Root "kernel\core\desktop.cpp") -Pattern "bare_metal_desktop_set_current_directory|bare_metal_desktop_go_back|bare_metal_desktop_go_home|sync_live_directory_from_shell_cwd"
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
$kernelBareMetalDesktopConfigLoad = Find-FirstMatch -LiteralPath (Join-Path $Root "kernel\core\desktop.cpp") -Pattern "DesktopConfig::Load\(|DesktopConfig::Save\(|smallLiveDesktopFolderIcons\s*=\s*cfg\.smallLiveDesktopFolderIcons"
$rightClickIconSize = Find-FirstMatch -LiteralPath (Join-Path $Root "right_click_menu.cpp") -Pattern "Folder View Icon Size|setHostedDesktopPrefersCompactFolderIcons|hostedDesktopPrefersCompactFolderIcons"
$hostedNonRootCompactIcons = Find-FirstMatch -LiteralPath (Join-Path $Root "compositor.cpp") -Pattern "hostedDesktopUsesCompactIconLayout|desktopIconCellHeightForItem|desktopIconTopPadding"
$displayOptionsIconSize = Find-FirstMatch -LiteralPath (Join-Path $Root "display_options.cpp") -Pattern "smallLiveDesktopFolderIcons|Use smaller folder icons|folder icon size"
$desktopConfigIconSize = Find-FirstMatch -LiteralPath (Join-Path $Root "desktop_config.h") -Pattern "smallLiveDesktopFolderIcons"
$runtimeEvidencePath = Join-Path $Root "logs\live-directory-desktop-runtime.evidence.txt"
$runtimeEvidenceText = if (Test-Path -LiteralPath $runtimeEvidencePath) { Get-Content -LiteralPath $runtimeEvidencePath -Raw } else { $null }
$runtimeEvidenceHead = $null
$runtimeEvidenceResult = $null
$runtimeEvidencePathMode = $null
$runtimeEvidenceTargetPath = $null
$runtimeEvidenceNativePathAvailable = $null
$runtimeEvidenceCompact = $null
$runtimeEvidenceBack = $null
$runtimeEvidenceShell = $null
$runtimeEvidenceHome = $null
$runtimeEvidenceCleanup = $null
if ($runtimeEvidenceText) {
    if ($runtimeEvidenceText -match '(?m)^head=(.+)$') { $runtimeEvidenceHead = $Matches[1].Trim() }
    if ($runtimeEvidenceText -match '(?m)^result=(.+)$') { $runtimeEvidenceResult = $Matches[1].Trim() }
    if ($runtimeEvidenceText -match '(?m)^pathMode=(.+)$') { $runtimeEvidencePathMode = $Matches[1].Trim() }
    if ($runtimeEvidenceText -match '(?m)^targetPath=(.+)$') { $runtimeEvidenceTargetPath = $Matches[1].Trim() }
    if ($runtimeEvidenceText -match '(?m)^nativePathAvailable=(.+)$') { $runtimeEvidenceNativePathAvailable = $Matches[1].Trim() }
    if ($runtimeEvidenceText -match '(?m)^compactLayout=(.+)$') { $runtimeEvidenceCompact = $Matches[1].Trim() }
    if ($runtimeEvidenceText -match '(?m)^backNavigation=(.+)$') { $runtimeEvidenceBack = $Matches[1].Trim() }
    if ($runtimeEvidenceText -match '(?m)^shellCdSync=(.+)$') { $runtimeEvidenceShell = $Matches[1].Trim() }
    if ($runtimeEvidenceText -match '(?m)^goHome=(.+)$') { $runtimeEvidenceHome = $Matches[1].Trim() }
    if ($runtimeEvidenceText -match '(?m)^cleanup=(.+)$') { $runtimeEvidenceCleanup = $Matches[1].Trim() }
}
$runtimeEvidenceFresh = $false
if ($runtimeEvidenceText) {
    $relevantSourceWriteTime = Get-LatestWriteTime @(
        (Join-Path $Root "kernel\core\desktop.cpp"),
        (Join-Path $Root "kernel\core\main.cpp"),
        (Join-Path $Root "kernel\core\include\kernel\desktop.h"),
        (Join-Path $Root "scripts\smoke-live-directory-desktop-runtime.ps1")
    )
    $runtimeEvidenceFresh = (
        $runtimeEvidenceHead -eq $currentHead -and
        $runtimeEvidenceResult -eq "PASS" -and
        $runtimeEvidencePathMode -ne $null -and
        $runtimeEvidenceTargetPath -ne $null -and
        $runtimeEvidenceCompact -eq "PASS" -and
        $runtimeEvidenceBack -eq "PASS" -and
        $runtimeEvidenceShell -eq "PASS" -and
        $runtimeEvidenceHome -eq "PASS" -and
        $runtimeEvidenceCleanup -eq "PASS" -and
        (Get-Item -LiteralPath $runtimeEvidencePath).LastWriteTimeUtc -ge $relevantSourceWriteTime
    )
}

Write-Host ""
Emit-Check "hosted desktop folder enumeration" "present" $hostedDesktopLive
Emit-Check "hosted desktop reserved-name filtering" "present" $hostedDesktopReservedFilter
Emit-Check "hosted desktop stale-position pruning" "present" $hostedDesktopStalePositionPrune
Emit-Check "hosted desktop directory state" "present" $hostedDesktopPathState
Emit-Check "hosted desktop navigation controls" "present" $hostedDesktopNav
Emit-Check "bare-metal desktop folder enumeration" "present" $kernelDesktopLive
Emit-Check "bare-metal desktop reserved-name filtering" "present" $kernelDesktopReservedFilter
Emit-Check "hosted shell cd command" "present" $hostedShellCdCommand
Emit-Check "hosted shell desktop bridge" "present" $hostedShellDesktopBridge
Emit-Check "bare-metal shell cd / cwd state" "present" $bareMetalShellCdState
Emit-Check "bare-metal shell get_cwd exposure" "present" $bareMetalShellGetCwdState
Emit-Check "bare-metal desktop directory scaffold" "present" $bareMetalDesktopState
Emit-Check "bare-metal desktop home path" "present" $bareMetalDesktopHomePath
Emit-Check "bare-metal desktop home check" "present" $bareMetalDesktopHomeCheck
Emit-Check "bare-metal desktop backing-path marker" "present" $bareMetalDesktopBackingPathMarker
Emit-Check "bare-metal desktop icon-init markers" "present" $bareMetalDesktopIconInitMarkers
Emit-Check "bare-metal desktop refresh hook" "present" $bareMetalDesktopRefresh
Emit-Check "bare-metal desktop manual refresh hook" "present" $bareMetalDesktopManualRefresh
Emit-Check "bare-metal desktop startup sync API" "present" $bareMetalDesktopStartupSyncApi
Emit-Check "bare-metal desktop startup sync call" "present" $bareMetalDesktopStartupSyncCall
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

if ($null -eq $kernelBareMetalDesktopConfigLoad) {
    Write-Host "bare-metal hosted desktop.json config load: absent"
    Write-Host "    evidence: kernel\core\desktop.cpp has no DesktopConfig::Load/Save or smallLiveDesktopFolderIcons assignment"
} else {
    Emit-Check "bare-metal hosted desktop.json config load" "present" $kernelBareMetalDesktopConfigLoad
}

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

$hostedParityPresent = $null -ne $hostedDesktopLive -and $null -ne $hostedDesktopPathState -and $null -ne $hostedDesktopNav -and $null -ne $hostedShellCdCommand -and $null -ne $hostedShellDesktopBridge -and $null -ne $showOnDesktop -and $null -ne $hostedNonRootCompactIcons -and $null -ne $displayOptionsIconSize -and $null -ne $rightClickIconSize
$bareMetalSourceParityPresent = $null -ne $bareMetalDesktopState -and $null -ne $bareMetalDesktopHomePath -and $null -ne $bareMetalDesktopHomeCheck -and $null -ne $bareMetalDesktopBackingPathMarker -and $null -ne $bareMetalDesktopIconInitMarkers -and $null -ne $bareMetalDesktopManualRefresh -and $null -ne $bareMetalDesktopStartupSync -and $null -ne $bareMetalDesktopNavigation -and $null -ne $bareMetalDesktopBackHome -and $null -ne $bareMetalShellDesktopSync -and $null -ne $kernelBareMetalCompactIcons -and $null -eq $kernelBareMetalDesktopConfigLoad
$bareMetalParityPresent = if ($runtimeEvidenceFresh) { $true } else { $bareMetalSourceParityPresent }

$desktopSmokeScripts = @(
    "scripts\smoke-desktop-startup-sync.ps1",
    "scripts\smoke-appmodel-launchshadow.ps1",
    "scripts\smoke-appmodel-phase2-status.ps1",
    "scripts\smoke-appmodel-typed-dispatch-flags.ps1",
    "scripts\smoke-appmodel-phase3a-active-typed-dispatch.ps1",
    "scripts\smoke-appmodel-phase3b-active-typed-dispatch.ps1",
    "scripts\smoke-appmodel-phase3c-active-typed-dispatch.ps1",
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
Write-Host "  hosted-parity=$(if ($hostedParityPresent) { 'live-directory-present' } else { 'missing' })"
Write-Host "  live-directory-desktop-runtime-smoke=$(if ($runtimeEvidenceFresh) { 'present' } else { 'deferred' })"
Write-Host "  hosted-nonroot-smaller-icons=$(if ($null -ne $hostedNonRootCompactIcons) { 'present' } else { 'missing' })"
Write-Host "  display-options-right-click-icon-size=$(if ($null -ne $displayOptionsIconSize -and $null -ne $rightClickIconSize) { 'shared-setting' } else { 'missing' })"
Write-Host "  display-options-live-folder-icon-size=$(if ($null -ne $displayOptionsIconSize) { 'present' } else { 'missing' })"
Write-Host "  right-click-icon-size=$(if ($null -ne $rightClickIconSize) { 'live-folder-wired' } else { 'missing' })"
Write-Host "  bare-metal-desktop-directory-state=$(if ($null -ne $bareMetalDesktopState) { 'present' } else { 'missing' })"
Write-Host "  bare-metal-home-path=$(if ($null -ne $bareMetalDesktopHomePath) { '/Desktop' } else { 'missing' })"
Write-Host "  bare-metal-folder-navigation=$(if ($null -ne $bareMetalDesktopNavigation -and $null -ne $bareMetalDesktopHomeCheck) { 'present' } else { 'missing' })"
Write-Host "  bare-metal-back-go-desktop=$(if ($null -ne $bareMetalDesktopBackHome) { 'present' } else { 'missing' })"
Write-Host "  bare-metal-go-desktop-target=$(if ($null -ne $bareMetalDesktopHomePath) { '/Desktop' } else { 'missing' })"
Write-Host "  bare-metal-shell-cd-sync=$(if ($null -ne $bareMetalShellDesktopSync) { 'present' } else { 'missing' })"
Write-Host "  bare-metal-nonroot-smaller-icons=$(if ($null -ne $kernelBareMetalCompactIcons) { 'present' } else { 'missing' })"
Write-Host "  bare-metal-nonroot-icon-config=$(if ($null -eq $kernelBareMetalDesktopConfigLoad) { 'default-on-no-host-config' } else { 'host-config-loaded' })"
Write-Host "  bare-metal-parity=$(if ($bareMetalParityPresent) { if ($runtimeEvidenceFresh) { 'feature-present-runtime-evidence-partial' } else { 'feature-present-evidence-partial' } } else { 'missing' })"
Write-Host "  live-directory-desktop-parity=$(if ($hostedParityPresent -and $bareMetalParityPresent) { if ($runtimeEvidenceFresh) { 'hosted-present-baremetal-present-runtime-evidence' } else { 'hosted-present-baremetal-present-source-only' } } else { 'missing' })"
if ($runtimeEvidenceFresh) {
    Write-Host "  live-directory-desktop-runtime-path=$(if ($runtimeEvidencePathMode -eq 'native-desktop-live-smoke') { 'native-desktop-live-smoke' } elseif ($runtimeEvidencePathMode -eq 'alias-fallback') { 'alias-fallback' } else { $runtimeEvidencePathMode })"
    Write-Host "  live-directory-desktop-native-path=$(if ($runtimeEvidenceNativePathAvailable -eq 'PASS') { 'present' } elseif ($runtimeEvidencePathMode -eq 'alias-fallback') { 'blocked' } else { 'missing' })"
} else {
    Write-Host "  live-directory-desktop-runtime-path=deferred"
    Write-Host "  live-directory-desktop-native-path=deferred"
}
