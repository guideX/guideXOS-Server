#pragma once
#include "process.h"
#include "ipc_bus.h"
#include <string>
#include <vector>

namespace gxos { namespace apps {
    
    /// <summary>
    /// Notepad - Simple text editor application
    /// Features: Multi-line text editing, Open/Save via VFS, Text wrapping, Special characters
    /// </summary>
    class Notepad {
    public:
        /// <summary>
        /// Launch a new Notepad instance
        /// </summary>
        /// <returns>Process ID of the launched Notepad</returns>
        static uint64_t Launch();
        
        /// <summary>
        /// Launch Notepad with a specific file loaded
        /// </summary>
        static uint64_t LaunchWithFile(const std::string& filePath);
        
    private:
        // Main entry point for Notepad process
        static int main(int argc, char** argv);
        
        // Message handling
        static void handleMessage(const ipc::Message& m);
        
        // Text editing operations
        static void insertText(const std::string& text);
        static void deleteSelection();
        static void copy();
        static void paste();
        static void selectAll();
        
        // File operations
        static void newFile();
        static void openFile();
        static void saveFile();
        static void saveFileAs();
        
        // UI operations
        static void toggleWrap();
        
        // UI update
        static void updateTitle();
        static void redrawContent();
        static void updateStatusBar();
        
        // Keyboard helpers
        static char mapKeyToChar(int keyCode);
        
        // State
        static uint64_t s_windowId;
        static std::string s_filePath;
        static std::vector<std::string> s_lines;
        static int s_cursorLine;
        static int s_cursorCol;
        static bool s_modified;
        static int s_scrollOffset;
        static bool s_wrapText;
        static bool s_shiftPressed;
        static bool s_ctrlPressed;
        static bool s_capsLockOn;
    };
    
}} // namespace gxos::apps
