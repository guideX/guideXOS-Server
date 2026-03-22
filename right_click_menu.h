#pragma once
#pragma once
#include "process.h"
#include "ipc_bus.h"
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace gxos { namespace gui {

    /// RightClickMenu - Desktop right-click context menu
    /// Ported from guideXOS.Legacy RightMenu.cs
    /// Shows context menu items: Display Options, Performance Widget toggle,
    /// Save Settings, Up one level, Icon Size submenu.
    class RightClickMenu {
    public:
        /// Show the right-click menu at the given screen coordinates
        static void Show(int x, int y);

        /// Hide the menu
        static void Hide();

        /// Check if the menu is currently visible
        static bool IsVisible();

        /// Handle a mouse click at the given position
        /// @return true if click was consumed by the menu
        static bool HandleClick(int mx, int my);

        /// Draw the menu (called by compositor during paint)
        /// Uses Win32 GDI on Windows, text commands otherwise
#ifdef _WIN32
        static void Draw(HDC dc);
#else
        static void Draw();
#endif

    private:
        struct MenuItem {
            std::string label;
            bool hasSubmenu;
            bool checked;
        };

        static void buildItems();

        static bool s_visible;
        static int s_x;
        static int s_y;
        static std::vector<MenuItem> s_items;
        static bool s_iconSubmenuVisible;
        static int s_iconSubmenuIndex;

        static constexpr int kItemH    = 28;
        static constexpr int kMenuW    = 220;
        static constexpr int kSubMenuW = 160;
        static constexpr int kPadding  = 8;
    };

}} // namespace gxos::gui
