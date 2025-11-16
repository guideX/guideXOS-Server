#pragma once
#include "process.h"
#include "ipc_bus.h"
#include "vfs.h"
#include <string>
#include <vector>
#include <functional>

namespace gxos { namespace dialogs {
    
    /// <summary>
    /// SaveDialog - File browser dialog for Save As
    /// Allows user to browse VFS and select/enter a filename
    /// </summary>
    class SaveDialog {
    public:
        /// <summary>
        /// Show the dialog
        /// </summary>
        /// <param name="ownerX">Owner window X position</param>
        /// <param name="ownerY">Owner window Y position</param>
        /// <param name="startPath">Initial directory path</param>
        /// <param name="defaultFileName">Default filename</param>
        /// <param name="onSave">Callback when Save clicked (receives full path)</param>
        static void Show(int ownerX, int ownerY,
                        const std::string& startPath,
                        const std::string& defaultFileName,
                        std::function<void(const std::string&)> onSave);
        
    private:
        // Main entry point for dialog process
        static int main(int argc, char** argv);
        
        // Navigation
        static void navigate(const std::string& path);
        static void goUp();
        static void refresh();
        
        // Actions
        static void saveAction();
        static void handleKeyPress(int keyCode);
        
        // UI update
        static void redraw();
        
        // State
        static uint64_t s_windowId;
        static std::string s_currentPath;
        static std::string s_fileName;
        static std::vector<VfsEntryInfo> s_entries;
        static int s_selectedIndex;
        static int s_scrollOffset;
        static bool s_fileNameFocus;
        static std::function<void(const std::string&)> s_onSave;
        static int s_lastKeyCode;
        static bool s_keyDown;
    };
    
}} // namespace gxos::dialogs
