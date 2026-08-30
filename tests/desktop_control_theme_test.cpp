#include "desktop_control_theme.h"

#include <iostream>

namespace {

bool expect(bool value, const char* label)
{
    if (!value) std::cerr << "FAIL: " << label << "\n";
    return value;
}

}

int main()
{
    const DesktopTheme& classic = GetDesktopTheme(DesktopThemeId::Classic);
    const DesktopTheme& sciFi = GetDesktopTheme(DesktopThemeId::SciFi);
    const DesktopControlTheme classicRoles = GetDesktopControlTheme(classic);
    const DesktopControlTheme sciFiRoles = GetDesktopControlTheme(sciFi);

    bool ok = true;
    ok &= expect(classic.id == DesktopThemeId::Classic, "Classic theme lookup remains available");
    ok &= expect(sciFi.id == DesktopThemeId::SciFi, "Sci-Fi theme lookup remains available");
    ok &= expect(classicRoles.panelBackground == classic.windowBackground,
        "control panel role follows the selected theme");
    ok &= expect(classicRoles.inputBackground == classicRoles.recessedField,
        "input background is derived from the shared recessed-field role");
    ok &= expect(sciFiRoles.tableHeaderBackground != sciFiRoles.panelBackground,
        "table header background is a distinct shared utility role");
    ok &= expect(sciFiRoles.tableHeaderText == sciFi.titleBarText,
        "table header text follows the selected theme title text");
    ok &= expect(sciFiRoles.statusWarning != sciFiRoles.panelBackground,
        "status warning is a distinct shared utility role");
    ok &= expect(DesktopControlFillColor(sciFiRoles, DesktopControlState::Normal) == sciFiRoles.controlBackground,
        "normal control state selects the normal fill");
    ok &= expect(DesktopControlFillColor(sciFiRoles, DesktopControlState::Hover) == sciFiRoles.controlHover,
        "hover control state selects the hover fill");
    ok &= expect(DesktopControlFillColor(sciFiRoles, DesktopControlState::Pressed) == sciFiRoles.controlPressed,
        "pressed control state selects the pressed fill");
    ok &= expect(DesktopControlBorderColor(sciFiRoles, DesktopControlState::Focused) == sciFiRoles.controlFocusBorder,
        "focused control state selects the focus border");
    ok &= expect(DesktopControlTextColor(sciFiRoles, DesktopControlState::Disabled) == sciFiRoles.controlDisabledText,
        "disabled control state selects disabled text");
    ok &= expect(sciFiRoles.controlBackground != classicRoles.controlBackground,
        "Classic and Sci-Fi control palettes remain distinct");

    DesktopThemeId parsed = DesktopThemeId::SciFi;
    ok &= expect(TryParseDesktopThemeId(nullptr, &parsed) && parsed == DesktopThemeId::Classic,
        "missing theme token falls back to Classic");
    parsed = DesktopThemeId::SciFi;
    ok &= expect(!TryParseDesktopThemeId("unsupported", &parsed) && parsed == DesktopThemeId::Classic,
        "invalid theme token falls back to Classic");

    SetCurrentDesktopTheme(DesktopThemeId::Classic);
    std::cout << (ok ? "Desktop control theme tests PASS\n" :
        "Desktop control theme tests FAIL\n");
    return ok ? 0 : 1;
}
