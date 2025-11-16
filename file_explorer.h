#pragma once
#include "process.h"
#include "ipc_bus.h"
#include "vfs.h"
#include <string>
#include <vector>

namespace gxos { namespace apps {
    
    /// <summary>
    /// FileExplorer - File browser for VFS filesystem
    /// Features: Browse directories, navigate folders, open files, file operations
    /// </summary>
    class FileExplorer {
    public:
        /// <summary>
        /// Launch a new FileExplorer instance
        /// </summary>
        /// <param name="startPath">Optional starting directory path</param>
        /// <returns>Process ID of the launched FileExplorer</returns>
        static uint64_t Launch(const std::string& startPath = "");
        
    private:
        // Main entry point for FileExplorer process
        static int main(int argc, char** argv);
        
        // Navigation
        static void navigate(const std::string& path);
        static void goUp();
        static void goHome();
        static void refresh();
        
        // Actions
        static void openSelected();
        static void deleteSelected();
        static void createFolder();
        
        // Keyboard handling
        static void handleKeyPress(int keyCode);
        
        // UI update
        static void updateDisplay();
        static void updateStatusBar();
        static void updatePathDisplay();
        
        // State
        static uint64_t s_windowId;
        static std::string s_currentPath;
        static std::vector<VfsEntryInfo> s_entries;
        static int s_selectedIndex;
        static int s_scrollOffset;
        static int s_lastKeyCode;
        static bool s_keyDown;
    };
    
}} // namespace gxos::apps
