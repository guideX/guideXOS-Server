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
                        std::function<void(const std::string&)> onSave,
                        std::function<void()> onClosed = {});
        
    private:
        // Main entry point for dialog process
        static int main(int argc, char** argv);
        
        // Navigation
        static void navigate(const std::string& path);
        static void goUp();
        static void refresh();
        static void selectEntry(int visibleIndex);
        
        // Actions
        static bool saveAction();
        static void handleKeyPress(int keyCode);
        
        // UI update
        static void redraw();
        
        // State
        static thread_local uint64_t s_windowId;
        static thread_local std::string s_currentPath;
        static thread_local std::string s_fileName;
        static thread_local std::vector<VfsEntryInfo> s_entries;
        static thread_local int s_selectedIndex;
        static thread_local int s_scrollOffset;
        static thread_local bool s_fileNameFocus;
        static thread_local bool s_showingDrives;
        static thread_local std::function<void(const std::string&)> s_onSave;
        static thread_local int s_lastKeyCode;
        static thread_local bool s_keyDown;
    };
    
}} // namespace gxos::dialogs
