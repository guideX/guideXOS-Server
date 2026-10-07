$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)

function Read-Source([string]$RelativePath) {
    return Get-Content -LiteralPath (Join-Path $Root $RelativePath) -Raw
}

function Assert-Contains([string]$Source, [string]$Expected, [string]$Behavior) {
    if (-not $Source.Contains($Expected)) {
        throw "[DesktopKeyboardOperabilitySmoke] Missing $Behavior"
    }
}

$keyboard = Read-Source 'kernel\core\ps2keyboard.cpp'
$desktop = Read-Source 'kernel\core\desktop.cpp'
$apps = Read-Source 'kernel\core\kernel_apps.cpp'
$compositor = Read-Source 'kernel\core\kernel_compositor.cpp'

Assert-Contains $keyboard 'scancode == 0x0D' 'PS/2 Tab decoding'
Assert-Contains $keyboard 'enqueue_event(shell::KEY_TAB, false)' 'Tab delivery to the desktop'
Assert-Contains $keyboard 'scancode == 0x76' 'PS/2 Escape decoding'
Assert-Contains $keyboard 'enqueue_event(KEY_ESCAPE, false)' 'Escape delivery to the desktop'
Assert-Contains $desktop 'handle_start_menu_key(key)' 'Start-menu keyboard navigation'
Assert-Contains $desktop 'show_start_menu_notification(label)' 'Start-menu Enter activation through the normal launch handler'
Assert-Contains $desktop 'key == 27 || key == ps2keyboard::KEY_EVENT_ESCAPE' 'Escape dismissal'
Assert-Contains $desktop 'cycleFocus(ps2keyboard::is_shift_down())' 'Alt+Tab window cycling'
Assert-Contains $desktop 'handle_alt_f4_shortcut(' 'Alt+F4 active-window close'
Assert-Contains $apps 'm_focusedTabIndex = (m_focusedTabIndex + 1) % 5' 'Display Options Tab navigation'
Assert-Contains $apps 'set_desktop_theme_id(themeId)' 'keyboard theme activation'
Assert-Contains $compositor 'setFocus(target)' 'focus change through the normal compositor'
Assert-Contains $desktop 'show_start_menu_notification(start_menu_left_item_label_for_row(leftHit))' 'existing mouse Start-menu activation path'

Write-Host '[DesktopKeyboardOperabilitySmoke] focused keyboard and mouse-path checks passed.'
