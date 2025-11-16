#pragma once
#include "process.h"
#include "ipc_bus.h"
#include <string>
#include <vector>

namespace gxos { namespace apps {
    
    /// <summary>
    /// ConsoleWindow - GUI window for console_service
    /// Features: Command input, output display, command history, scrollback buffer
    /// </summary>
    class ConsoleWindow {
    public:
        /// <summary>
        /// Launch a new ConsoleWindow instance
        /// </summary>
        /// <returns>Process ID of the launched ConsoleWindow</returns>
        static uint64_t Launch();
        
    private:
        // Main entry point for ConsoleWindow process
        static int main(int argc, char** argv);
        
        // Command handling
        static void sendCommand(const std::string& cmd);
        static void handleConsoleOutput(const std::string& output);
        static void handleKeyPress(int keyCode);
        
        // History navigation
        static void navigateHistoryUp();
        static void navigateHistoryDown();
        
        // UI update
        static void updateDisplay();
        static void updateInputLine();
        static void scrollToBottom();
        
        // State
        static uint64_t s_windowId;
        static std::vector<std::string> s_outputLines;     // Output display buffer
        static std::string s_currentInput;                 // Current input line
        static std::vector<std::string> s_commandHistory;  // Command history
        static int s_historyIndex;                         // Current position in history
        static int s_scrollOffset;                         // Scroll position
        static int s_lastKeyCode;                          // For key debouncing
        static bool s_keyDown;                             // For key debouncing
        static bool s_shiftPressed;                        // Track shift key
    };
    
}} // namespace gxos::apps
