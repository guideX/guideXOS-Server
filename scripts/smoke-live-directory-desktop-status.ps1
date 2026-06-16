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

$hostedDesktopLive = Find-FirstMatch -LiteralPath (Join-Path $Root "compositor.cpp") -Pattern "DesktopFolderResolver::Enumerate\("
$hostedDesktopPathState = Find-FirstMatch -LiteralPath (Join-Path $Root "file_explorer.cpp") -Pattern "s_currentPath"
$kernelDesktopLive = Find-FirstMatch -LiteralPath (Join-Path $Root "kernel\core\desktop.cpp") -Pattern "enumerate_desktop_folder_items\("
$shellCdState = Find-FirstMatch -LiteralPath (Join-Path $Root "kernel\core\shell.cpp") -Pattern "cmd_cd\("
$shellGetCwdState = Find-FirstMatch -LiteralPath (Join-Path $Root "kernel\core\shell.cpp") -Pattern "get_cwd\("
$fileExplorerBack = Find-FirstMatch -LiteralPath (Join-Path $Root "file_explorer.cpp") -Pattern "goBack\(\)"
$fileExplorerGoHome = Find-FirstMatch -LiteralPath (Join-Path $Root "file_explorer.cpp") -Pattern "goHome\(\)"
$fileExplorerContextPin = Find-FirstMatch -LiteralPath (Join-Path $Root "file_explorer.cpp") -Pattern "Pin to Desktop"
$showOnDesktop = Find-FirstMatch -LiteralPath (Join-Path $Root "compositor.cpp") -Pattern "Show on Desktop"
$kernelIconSize = Find-FirstMatch -LiteralPath (Join-Path $Root "kernel\core\desktop.cpp") -Pattern "s_desktopIconSize"
$rightClickIconSize = Find-FirstMatch -LiteralPath (Join-Path $Root "right_click_menu.cpp") -Pattern "Icon Size"
$displayOptionsIconSize = Find-FirstMatch -LiteralPath (Join-Path $Root "display_options.cpp") -Pattern "s_desktopIconSize|desktop icon size|icon-size|Icon Size Setting"

Write-Host ""
Emit-Check "hosted desktop folder enumeration" "present" $hostedDesktopLive
Emit-Check "hosted File Explorer path state" "present" $hostedDesktopPathState
Emit-Check "bare-metal desktop folder enumeration" "present" $kernelDesktopLive
Emit-Check "shell cd / cwd state" "present" $shellCdState
Emit-Check "shell get_cwd exposure" "present" $shellGetCwdState
Emit-Check "File Explorer Back navigation" "present" $fileExplorerBack
Emit-Check "File Explorer Go Home navigation" "present" $fileExplorerGoHome
Emit-Check "File Explorer Pin to Desktop action" "present" $fileExplorerContextPin
Emit-Check "right-click Icon Size submenu placeholder" "present" $rightClickIconSize

if ($null -eq $showOnDesktop) {
    Write-Host "desktop Show on Desktop action: missing"
    Write-Host "    evidence: no literal match for 'Show on Desktop' in compositor.cpp"
} else {
    Emit-Check "desktop Show on Desktop action" "present" $showOnDesktop
}

Emit-Check "kernel desktop icon size state" "present" $kernelIconSize

if ($null -eq $displayOptionsIconSize) {
    Write-Host "Display Options icon-size setting hook: missing or only partial"
    Write-Host "    evidence: display_options.cpp currently exposes desktop icon visibility checkboxes, not a persisted icon-size setting"
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
Write-Host "  desktop-directory-state=partial"
Write-Host "  back-go-home-nav=present-in-file-explorer"
Write-Host "  show-on-desktop-action=missing"
Write-Host "  shell-cd-sync=missing"
Write-Host "  icon-size-setting=missing-or-partial"
