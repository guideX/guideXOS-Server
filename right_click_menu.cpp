#include "right_click_menu.h"
#include "logger.h"
#include "compositor.h"
#include "desktop_service.h"
#include "kernel/core/include/kernel/system_font.h"
#include <cstring>

namespace gxos { namespace gui {

namespace {
    constexpr int kFolderIconSizeOptionCount = 2;

    const char* folderIconSizeLabel(int index) {
        switch (index) {
        case 0: return "Normal folder icons";
        case 1: return "Small folder icons";
        default: return "";
        }
    }
}

bool RightClickMenu::s_visible = false;
int RightClickMenu::s_x = 0;
int RightClickMenu::s_y = 0;
std::vector<RightClickMenu::MenuItem> RightClickMenu::s_items;
int RightClickMenu::s_desktopItemIndex = -1;
std::string RightClickMenu::s_startMenuAppName;
bool RightClickMenu::s_iconSubmenuVisible = false;
int RightClickMenu::s_iconSubmenuIndex = -1;

void RightClickMenu::Show(int x, int y) {
    s_x = x;
    s_y = y;
    s_visible = true;
    s_desktopItemIndex = -1;
    s_startMenuAppName.clear();
    s_iconSubmenuVisible = false;
    buildItems();
    Logger::write(LogLevel::Info, "RightClickMenu shown");
}

void RightClickMenu::ShowForDesktopItem(int x, int y, int desktopItemIndex) {
    s_x = x;
    s_y = y;
    s_visible = true;
    s_desktopItemIndex = desktopItemIndex;
    s_startMenuAppName.clear();
    s_iconSubmenuVisible = false;
    buildItems();
    Logger::write(LogLevel::Info, "RightClickMenu shown for desktop item");
}

void RightClickMenu::ShowForStartMenuApp(int x, int y, const std::string& appName) {
    s_x = x;
    s_y = y;
    s_visible = true;
    s_desktopItemIndex = -1;
    s_startMenuAppName = appName;
    s_iconSubmenuVisible = false;
    buildItems();
    Logger::write(LogLevel::Info, "Start Menu context menu created for app: " + appName);
}

void RightClickMenu::Hide() {
    s_visible = false;
    s_desktopItemIndex = -1;
    s_startMenuAppName.clear();
    s_iconSubmenuVisible = false;
    s_items.clear();
}

bool RightClickMenu::IsVisible() {
    return s_visible;
}

bool RightClickMenu::IsStartMenuAppMenuVisible() {
    return s_visible && !s_startMenuAppName.empty();
}

int RightClickMenu::menuHeight() {
    return (int)s_items.size() * kItemH;
}

bool RightClickMenu::ContainsPoint(int mx, int my) {
    if (!s_visible) return false;
    int menuH = menuHeight();
    if (mx >= s_x && mx <= s_x + kMenuW && my >= s_y && my <= s_y + menuH) return true;
    if (s_iconSubmenuVisible && s_iconSubmenuIndex >= 0) {
        int subX = s_x + kMenuW;
        int subY = s_y + s_iconSubmenuIndex * kItemH;
        int subH = kItemH * kFolderIconSizeOptionCount;
        if (mx >= subX && mx <= subX + kSubMenuW && my >= subY && my <= subY + subH) return true;
    }
    return false;
}

void RightClickMenu::buildItems() {
    s_items.clear();
    if (!s_startMenuAppName.empty()) {
        s_items.push_back({"Open", false, false});
        s_items.push_back({Compositor::isStartMenuAppPinnedToDesktop(s_startMenuAppName) ? "Unpin from Desktop" : "Pin to Desktop", false, false});
        return;
    }
    if (s_desktopItemIndex >= 0) {
        s_items.push_back({"Open", false, false});
        if (s_desktopItemIndex < (int)Compositor::g_items.size() &&
            Compositor::g_items[s_desktopItemIndex].kind == DesktopItemKind::Shortcut) {
            const DesktopItem& item = Compositor::g_items[s_desktopItemIndex];
            if (item.shortcutType == "File" || item.shortcutType == "Folder") {
                s_items.push_back({"Open Target Location", false, false});
            }
            s_items.push_back({"Remove from Desktop", false, false});
        }
        return;
    }
    s_items.push_back({"Refresh", false, false});
    s_items.push_back({"Display Options", false, false});
    s_items.push_back({"Folder View Icon Size", true, false});
    s_iconSubmenuIndex = 2;
}

bool RightClickMenu::HandleClick(int mx, int my) {
    if (!s_visible) return false;

    int menuH = (int)s_items.size() * kItemH;

    // Check icon size submenu click
    if (s_iconSubmenuVisible && s_iconSubmenuIndex >= 0) {
        int subX = s_x + kMenuW;
        int subY = s_y + s_iconSubmenuIndex * kItemH;
        int subH = kItemH * kFolderIconSizeOptionCount;
        if (mx >= subX && mx <= subX + kSubMenuW && my >= subY && my <= subY + subH) {
            int idx = (my - subY) / kItemH;
            if (idx >= 0 && idx < kFolderIconSizeOptionCount) {
                const bool smallIcons = idx == 1;
                Logger::write(LogLevel::Info,
                    std::string("Folder view icon size selected: ") + (smallIcons ? "small" : "normal"));
                Compositor::setHostedDesktopPrefersCompactFolderIcons(smallIcons);
            }
            Hide();
            return true;
        }
    }

    // Check main menu click
    if (mx >= s_x && mx <= s_x + kMenuW && my >= s_y && my <= s_y + menuH) {
        int idx = (my - s_y) / kItemH;
        if (idx >= 0 && idx < (int)s_items.size()) {
            if (s_items[idx].hasSubmenu) {
                s_iconSubmenuVisible = !s_iconSubmenuVisible;
                return true;
            }
            if (s_items[idx].label == "Open" && s_desktopItemIndex >= 0) {
                Logger::write(LogLevel::Info, "Desktop item Open selected");
                Compositor::openDesktopItem(s_desktopItemIndex);
            } else if (s_items[idx].label == "Open Target Location" && s_desktopItemIndex >= 0) {
                Logger::write(LogLevel::Info, "Desktop shortcut Open Target Location selected");
                Compositor::openDesktopShortcutTargetLocation(s_desktopItemIndex);
            } else if (s_items[idx].label == "Remove from Desktop" && s_desktopItemIndex >= 0) {
                Logger::write(LogLevel::Info, "Desktop shortcut Remove from Desktop selected");
                Compositor::removeDesktopShortcut(s_desktopItemIndex);
            } else if (s_items[idx].label == "Open" && !s_startMenuAppName.empty()) {
                Logger::write(LogLevel::Info, "Start Menu app Open selected from context menu: " + s_startMenuAppName);
                Compositor::openStartMenuApp(s_startMenuAppName);
            } else if (s_items[idx].label == "Pin to Desktop" && !s_startMenuAppName.empty()) {
                Logger::write(LogLevel::Info, "Start Menu app Pin to Desktop selected: " + s_startMenuAppName);
                Compositor::pinStartMenuAppToDesktop(s_startMenuAppName);
            } else if (s_items[idx].label == "Unpin from Desktop" && !s_startMenuAppName.empty()) {
                Logger::write(LogLevel::Info, "Start Menu app Unpin from Desktop selected: " + s_startMenuAppName);
                Compositor::unpinStartMenuAppFromDesktop(s_startMenuAppName);
            } else if (s_items[idx].label == "Refresh") {
                Logger::write(LogLevel::Info, "Desktop Refresh selected");
                Compositor::requestDesktopRefresh();
            } else if (s_items[idx].label == "Display Options") {
                Logger::write(LogLevel::Info, "Display Options selected");
                std::string err;
                DesktopService::LaunchApp("DisplayOptions", err);
            }
            Hide();
            return true;
        }
    }

    // Click outside - dismiss
    Hide();
    return true;
}

#ifdef _WIN32
void RightClickMenu::Draw(HDC dc) {
    if (!s_visible) return;

    int menuH = (int)s_items.size() * kItemH;

    // Menu background (semi-transparent dark)
    RECT bgRect = {s_x, s_y, s_x + kMenuW, s_y + menuH};
    HBRUSH bgBrush = CreateSolidBrush(RGB(34, 34, 34));
    FillRect(dc, &bgRect, bgBrush);
    DeleteObject(bgBrush);

    // Border
    HPEN borderPen = CreatePen(PS_SOLID, 1, RGB(63, 63, 63));
    HGDIOBJ oldPen = SelectObject(dc, borderPen);
    HGDIOBJ oldBrush = SelectObject(dc, (HBRUSH)GetStockObject(NULL_BRUSH));
    Rectangle(dc, s_x, s_y, s_x + kMenuW, s_y + menuH);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(borderPen);

    SetBkMode(dc, TRANSPARENT);
    const int lineH = SystemFont::MeasureHeight(FontRole::Default);

    POINT cursor;
    GetCursorPos(&cursor);
    HWND hwnd = Compositor::g_hwnd ? Compositor::g_hwnd : WindowFromDC(dc);
    if (hwnd) ScreenToClient(hwnd, &cursor);

    for (int i = 0; i < (int)s_items.size(); i++) {
        int iy = s_y + i * kItemH;
        RECT itemRect = {s_x, iy, s_x + kMenuW, iy + kItemH};

        // Hover highlight
        if (cursor.x >= itemRect.left && cursor.x <= itemRect.right &&
            cursor.y >= itemRect.top && cursor.y <= itemRect.bottom) {
            HBRUSH hov = CreateSolidBrush(RGB(49, 49, 49));
            FillRect(dc, &itemRect, hov);
            DeleteObject(hov);
        }

        const int textY = iy + (kItemH > lineH ? (kItemH - lineH) / 2 : 0);
        SystemFont::DrawText(dc, s_x + kPadding, textY,
                             s_items[i].label.c_str(), (int)s_items[i].label.size(),
                             RGB(220, 220, 220), FontRole::Default);

        // Submenu arrow indicator
        if (s_items[i].hasSubmenu) {
            SystemFont::DrawText(dc, s_x + kMenuW - 20, textY, ">", 1,
                                 RGB(220, 220, 220), FontRole::Default);
        }
    }

    // Icon size submenu
    if (s_iconSubmenuVisible && s_iconSubmenuIndex >= 0) {
        int subX = s_x + kMenuW;
        int subY = s_y + s_iconSubmenuIndex * kItemH;
        int subH = kItemH * kFolderIconSizeOptionCount;

        RECT subBg = {subX, subY, subX + kSubMenuW, subY + subH};
        HBRUSH subBgBrush = CreateSolidBrush(RGB(34, 34, 34));
        FillRect(dc, &subBg, subBgBrush);
        DeleteObject(subBgBrush);

        HPEN subBorderPen = CreatePen(PS_SOLID, 1, RGB(63, 63, 63));
        HGDIOBJ oldSubPen = SelectObject(dc, subBorderPen);
        HGDIOBJ oldSubBrush2 = SelectObject(dc, (HBRUSH)GetStockObject(NULL_BRUSH));
        Rectangle(dc, subX, subY, subX + kSubMenuW, subY + subH);
        SelectObject(dc, oldSubPen);
        SelectObject(dc, oldSubBrush2);
        DeleteObject(subBorderPen);

        const bool smallIcons = Compositor::hostedDesktopPrefersCompactFolderIcons();
        for (int i = 0; i < kFolderIconSizeOptionCount; i++) {
            int sy = subY + i * kItemH;
            RECT subItem = {subX, sy, subX + kSubMenuW, sy + kItemH};

            if (cursor.x >= subItem.left && cursor.x <= subItem.right &&
                cursor.y >= subItem.top && cursor.y <= subItem.bottom) {
                HBRUSH shov = CreateSolidBrush(RGB(49, 49, 49));
                FillRect(dc, &subItem, shov);
                DeleteObject(shov);
            }

            const int subTextY = sy + (kItemH > lineH ? (kItemH - lineH) / 2 : 0);
            if ((i == 0 && !smallIcons) || (i == 1 && smallIcons)) {
                SystemFont::DrawText(dc, subX + kPadding, subTextY, "*", 1,
                                     RGB(220, 220, 220), FontRole::Default);
            }
            SystemFont::DrawText(dc, subX + kPadding + 14, subTextY,
                                 folderIconSizeLabel(i), (int)strlen(folderIconSizeLabel(i)),
                                 RGB(220, 220, 220), FontRole::Default);
        }
    }
}
#else
void RightClickMenu::Draw() {
    // Non-Windows: rendering handled by compositor via IPC
}
#endif

}} // namespace gxos::gui
