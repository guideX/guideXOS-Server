#include "right_click_menu.h"
#include "logger.h"
#include "desktop_service.h"
#include <cstring>

namespace gxos { namespace gui {

bool RightClickMenu::s_visible = false;
int RightClickMenu::s_x = 0;
int RightClickMenu::s_y = 0;
std::vector<RightClickMenu::MenuItem> RightClickMenu::s_items;
bool RightClickMenu::s_iconSubmenuVisible = false;
int RightClickMenu::s_iconSubmenuIndex = -1;

void RightClickMenu::Show(int x, int y) {
    s_x = x;
    s_y = y;
    s_visible = true;
    s_iconSubmenuVisible = false;
    buildItems();
    Logger::write(LogLevel::Info, "RightClickMenu shown");
}

void RightClickMenu::Hide() {
    s_visible = false;
    s_iconSubmenuVisible = false;
    s_items.clear();
}

bool RightClickMenu::IsVisible() {
    return s_visible;
}

void RightClickMenu::buildItems() {
    s_items.clear();
    s_items.push_back({"Display Options", false, false});
    s_items.push_back({"Performance Widget", false, false});
    s_items.push_back({"Save Settings", false, false});
    s_items.push_back({"Icon Size", true, false});
    s_iconSubmenuIndex = 3;
}

bool RightClickMenu::HandleClick(int mx, int my) {
    if (!s_visible) return false;

    int menuH = (int)s_items.size() * kItemH;

    // Check icon size submenu click
    if (s_iconSubmenuVisible && s_iconSubmenuIndex >= 0) {
        int subX = s_x + kMenuW;
        int subY = s_y + s_iconSubmenuIndex * kItemH;
        int subH = kItemH * 5;
        if (mx >= subX && mx <= subX + kSubMenuW && my >= subY && my <= subY + subH) {
            int idx = (my - subY) / kItemH;
            int sizes[] = {16, 24, 32, 48, 128};
            if (idx >= 0 && idx < 5) {
                Logger::write(LogLevel::Info, "Icon size selected: " + std::to_string(sizes[idx]));
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
            if (s_items[idx].label == "Display Options") {
                Logger::write(LogLevel::Info, "Display Options selected");
            } else if (s_items[idx].label == "Performance Widget") {
                Logger::write(LogLevel::Info, "Performance Widget toggled");
            } else if (s_items[idx].label == "Save Settings") {
                Logger::write(LogLevel::Info, "Save Settings selected");
                DesktopService::SaveState();
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
    SetTextColor(dc, RGB(220, 220, 220));
    HFONT font = (HFONT)GetStockObject(ANSI_VAR_FONT);
    SelectObject(dc, font);

    POINT cursor;
    GetCursorPos(&cursor);
    HWND hwnd = WindowFromDC(dc);
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

        TextOutA(dc, s_x + kPadding, iy + (kItemH / 2) - 7,
                 s_items[i].label.c_str(), (int)s_items[i].label.size());

        // Submenu arrow indicator
        if (s_items[i].hasSubmenu) {
            TextOutA(dc, s_x + kMenuW - 20, iy + (kItemH / 2) - 7, ">", 1);
        }
    }

    // Icon size submenu
    if (s_iconSubmenuVisible && s_iconSubmenuIndex >= 0) {
        int subX = s_x + kMenuW;
        int subY = s_y + s_iconSubmenuIndex * kItemH;
        int subH = kItemH * 5;

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

        const char* sizeLabels[] = {"16", "24", "32", "48", "128"};
        for (int i = 0; i < 5; i++) {
            int sy = subY + i * kItemH;
            RECT subItem = {subX, sy, subX + kSubMenuW, sy + kItemH};

            if (cursor.x >= subItem.left && cursor.x <= subItem.right &&
                cursor.y >= subItem.top && cursor.y <= subItem.bottom) {
                HBRUSH shov = CreateSolidBrush(RGB(49, 49, 49));
                FillRect(dc, &subItem, shov);
                DeleteObject(shov);
            }

            TextOutA(dc, subX + kPadding, sy + (kItemH / 2) - 7,
                     sizeLabels[i], (int)strlen(sizeLabels[i]));
        }
    }
}
#else
void RightClickMenu::Draw() {
    // Non-Windows: rendering handled by compositor via IPC
}
#endif

}} // namespace gxos::gui
