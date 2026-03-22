#pragma once
#include "process.h"
#include "ipc_bus.h"
#include "vfs.h"
#include <string>
#include <vector>
#include <functional>

namespace gxos { namespace dialogs {

    /// OpenDialog - File browser dialog for opening files.
    /// Ported from guideXOS.Legacy OpenDialog.cs
    /// Allows user to browse VFS directories and select a file to open.
    class OpenDialog {
    public:
        /// Show the open dialog
        /// @param ownerX Owner window X position (for dialog placement)
        /// @param ownerY Owner window Y position
        /// @param startPath Initial directory path in VFS
        /// @param onOpen Callback invoked with full file path when Open is clicked
        static void Show(int ownerX, int ownerY,
                         const std::string& startPath,
                         std::function<void(const std::string&)> onOpen);

    private:
        static int main(int argc, char** argv);

        // Navigation
        static void navigate(const std::string& path);
        static void goUp();
        static void refresh();

        // Actions
        static void openAction();
        static void handleKeyPress(int keyCode);

        // UI
        static void redraw();

        // State
        static uint64_t s_windowId;
        static std::string s_currentPath;
        static std::vector<VfsEntryInfo> s_entries;
        static int s_selectedIndex;
        static int s_scrollOffset;
        static std::function<void(const std::string&)> s_onOpen;
        static bool s_done;
        static int s_lastKeyCode;
        static bool s_keyDown;

        // Layout
        static constexpr int kDialogW = 440;
        static constexpr int kDialogH = 360;
        static constexpr int kPadding = 10;
        static constexpr int kRowH    = 24;
        static constexpr int kBtnW    = 80;
        static constexpr int kBtnH    = 28;
    };

}} // namespace gxos::dialogs
