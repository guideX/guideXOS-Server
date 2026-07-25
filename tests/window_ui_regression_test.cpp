#include "window_interaction_policy.h"

#include <iostream>

namespace {

bool expect(bool value, const char* label) {
    if (!value) std::cerr << "FAIL: " << label << "\n";
    return value;
}

}

int main() {
    bool ok = true;

    gxos::gui::NormalWindowBounds oversized{-500, 900, 5000, 5000};
    gxos::gui::ClampNormalWindowBounds(oversized, {0, 0, 1024, 744});
    ok &= expect(oversized.x == 0 && oversized.y == 0 &&
        oversized.w == 1024 && oversized.h == 744,
        "oversized and off-screen bounds clamp to usable work area");

    gxos::gui::NormalWindowBounds minimum{-50, -50, 20, 20};
    gxos::gui::ClampNormalWindowBounds(minimum, {100, 40, 500, 300});
    ok &= expect(minimum.x == 100 && minimum.y == 40 &&
        minimum.w == 160 && minimum.h == 120,
        "restored bounds honor minimum size and work-area origin");

    gxos::gui::TitleBarDoubleClickTracker tracker;
    ok &= expect(!tracker.onDown(7, 1000, true, false, false),
        "first draggable title-bar click is not a double-click");
    ok &= expect(tracker.onDown(7, 1200, true, false, false),
        "second draggable title-bar click toggles maximize");
    ok &= expect(!tracker.onDown(7, 1300, false, false, false),
        "client-area double-click does not toggle maximize");
    ok &= expect(!tracker.onDown(7, 1400, true, true, false),
        "title-bar control click does not toggle maximize");
    ok &= expect(!tracker.onDown(7, 1500, true, false, true),
        "dragging title bar does not toggle maximize");
    ok &= expect(!tracker.onDown(7, 2100, true, false, false),
        "expired title-bar click sequence is discarded");

    const int windowX = 100;
    const int windowY = 80;
    const int windowWidth = 420;
    const int titleBarHeight = 32;
    const int padding = 4;
    const int gap = 4;
    const int buttonSize = 24;
    const int closeX = windowX + windowWidth - padding - buttonSize + 2;
    const int maxX = closeX - gap - buttonSize - gap - buttonSize + 2;
    ok &= expect(gxos::gui::IsTitleBarControlAt(windowX, windowY, windowWidth, true,
        titleBarHeight, padding, gap, closeX, windowY + padding + 2),
        "close control is excluded from draggable title-bar area");
    ok &= expect(gxos::gui::IsTitleBarControlAt(windowX, windowY, windowWidth, true,
        titleBarHeight, padding, gap, maxX, windowY + padding + 2),
        "maximize control is excluded from draggable title-bar area");
    ok &= expect(!gxos::gui::IsTitleBarControlAt(windowX, windowY, windowWidth, true,
        titleBarHeight, padding, gap, windowX + 40, windowY + 8),
        "draggable title-bar point is not a control");
    ok &= expect(!gxos::gui::IsTitleBarControlAt(windowX, windowY, windowWidth, true,
        titleBarHeight, padding, gap, windowX + 40, windowY + titleBarHeight + 8),
        "client-area point is not a control");

    std::cout << (ok ? "Window UI regression tests PASS\n" : "Window UI regression tests FAIL\n");
    return ok ? 0 : 1;
}
