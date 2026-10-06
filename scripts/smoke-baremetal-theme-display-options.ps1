param()

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$kernelAppsPath = Join-Path $Root "kernel\core\kernel_apps.cpp"
$kernelAppsHeaderPath = Join-Path $Root "kernel\core\include\kernel\kernel_apps.h"
$desktopPath = Join-Path $Root "kernel\core\desktop.cpp"
$desktopHeaderPath = Join-Path $Root "kernel\core\include\kernel\desktop.h"
$kernelApps = Get-Content -LiteralPath $kernelAppsPath -Raw
$kernelAppsHeader = Get-Content -LiteralPath $kernelAppsHeaderPath -Raw
$desktop = Get-Content -LiteralPath $desktopPath -Raw
$desktopHeader = Get-Content -LiteralPath $desktopHeaderPath -Raw

function Assert-Source {
    param([string]$Name, [string]$Text, [string]$Pattern)
    if (-not [regex]::IsMatch($Text, $Pattern, [System.Text.RegularExpressions.RegexOptions]::Singleline)) {
        throw "FAIL: $Name"
    }
    Write-Host "PASS: $Name"
}

Assert-Source "native app exposes Theme tab" $kernelApps 'tabLabel\[\].*?"Theme".*?m_activeTab == 4'
Assert-Source "native theme cards expose both choices" $kernelApps 'drawThemeTab\(.*?"Classic".*?"Sci-Fi"'
Assert-Source "native theme choices use canonical IDs" $kernelApps 'm_focusedThemeIndex == 0 \? "classic" : "scifi"'
Assert-Source "Tab traverses all native Display Options tabs" $kernelApps 'shell::KEY_TAB.*?\{ 0, 2, 1, 3, 4 \}.*?setActiveTab\(tabId\[m_focusedTabIndex\]\)'
Assert-Source "theme arrows focus Classic and Sci-Fi" $kernelApps 'm_activeTab == 4.*?KEY_LEFT \|\| key == shell::KEY_UP.*?m_focusedThemeIndex = 0.*?KEY_RIGHT \|\| key == shell::KEY_DOWN.*?m_focusedThemeIndex = 1'
Assert-Source "Enter activates focused native theme" $kernelApps 'm_activeTab == 4.*?key == ''\\r'' \|\| key == ''\\n''.*?set_desktop_theme_id\(themeId\)'
Assert-Source "Space activates focused native theme" $kernelApps 'm_activeTab == 4 && c == '' ''.*?set_desktop_theme_id\(themeId\)'
Assert-Source "native focus is visibly drawn" $kernelApps 'm_focusedThemeIndex == i\).*?GetCurrentDesktopTheme\(\)\.accent'
Assert-Source "native mouse selection remains available" $kernelApps 'm_activeTab == 4\).*?y >= 104.*?y >= 196.*?set_desktop_theme_id\(themeId\)'
Assert-Source "native apply persists, reloads, and redraws" $desktop 'bool set_desktop_theme_id\(const char\* themeId\).*?bare_metal_save_display_options\(store\).*?load_persisted_desktop_theme\(\).*?request_redraw\(\)'
Assert-Source "native API is part of the desktop interface" $desktopHeader 'bool set_desktop_theme_id\(const char\* themeId\)'
Assert-Source "invalid persisted theme still falls back to Classic" $desktop 'if \(!TryParseDesktopThemeId\(store\.desktopThemeId, &themeId\)\)\s*\{\s*themeId = DesktopThemeId::Classic;'

Write-Host "Bare-metal Display Options theme source smoke passed."
