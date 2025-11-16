#include "console_window.h"
#include "gui_protocol.h"
#include "logger.h"
#include <sstream>
#include <algorithm>

namespace gxos { namespace apps {
    
    using namespace gxos::gui;
    
    // Static member initialization
    uint64_t ConsoleWindow::s_windowId = 0;
    std::vector<std::string> ConsoleWindow::s_outputLines;
    std::string ConsoleWindow::s_currentInput = "";
    std::vector<std::string> ConsoleWindow::s_commandHistory;
    int ConsoleWindow::s_historyIndex = -1;
    int ConsoleWindow::s_scrollOffset = 0;
    int ConsoleWindow::s_lastKeyCode = 0;
    bool ConsoleWindow::s_keyDown = false;
    bool ConsoleWindow::s_shiftPressed = false;
    
    uint64_t ConsoleWindow::Launch() {
        ProcessSpec spec{"console_window", ConsoleWindow::main};
        return ProcessTable::spawn(spec, {"console_window"});
    }
    
    int ConsoleWindow::main(int argc, char** argv) {
        try {
            Logger::write(LogLevel::Info, "ConsoleWindow starting...");
            
            // Initialize state
            s_windowId = 0;
            s_outputLines.clear();
            s_outputLines.push_back("guideXOS Console");
            s_outputLines.push_back("Type commands and press Enter");
            s_outputLines.push_back("Use Up/Down arrows for command history");
            s_outputLines.push_back("");
            s_currentInput = "";
            s_commandHistory.clear();
            s_historyIndex = -1;
            s_scrollOffset = 0;
            s_lastKeyCode = 0;
            s_keyDown = false;
            s_shiftPressed = false;
            
            // Subscribe to IPC channels
            const char* kGuiChanIn = "gui.input";
            const char* kGuiChanOut = "gui.output";
            const char* kConsoleChanIn = "console.input";
            const char* kConsoleChanOut = "console.output";
            
            ipc::Bus::ensure(kGuiChanIn);
            ipc::Bus::ensure(kGuiChanOut);
            ipc::Bus::ensure(kConsoleChanIn);
            ipc::Bus::ensure(kConsoleChanOut);
            
            // Create window (640x400)
            ipc::Message createMsg;
            createMsg.type = (uint32_t)MsgType::MT_Create;
            std::ostringstream oss;
            oss << "Console|640|400";
            std::string payload = oss.str();
            createMsg.data.assign(payload.begin(), payload.end());
            ipc::Bus::publish(kGuiChanIn, std::move(createMsg), false);
            
            // Main event loop
            bool running = true;
            while (running) {
                // Check for GUI events
                ipc::Message guiMsg;
                if (ipc::Bus::pop(kGuiChanOut, guiMsg, 50)) {
                    MsgType msgType = (MsgType)guiMsg.type;
                    
                    switch (msgType) {
                        case MsgType::MT_Create: {
                            // Window created - extract window ID
                            std::string payload(guiMsg.data.begin(), guiMsg.data.end());
                            size_t sep = payload.find('|');
                            if (sep != std::string::npos && sep > 0) {
                                try {
                                    std::string idStr = payload.substr(0, sep);
                                    s_windowId = std::stoull(idStr);
                                    Logger::write(LogLevel::Info, std::string("ConsoleWindow created: ") + std::to_string(s_windowId));
                                    
                                    // Draw initial display
                                    updateDisplay();
                                    updateInputLine();
                                } catch (const std::exception& e) {
                                    Logger::write(LogLevel::Error, std::string("ConsoleWindow: Failed to parse window ID: ") + e.what());
                                }
                            }
                            break;
                        }
                        
                        case MsgType::MT_Close: {
                            // Window closed
                            std::string payload(guiMsg.data.begin(), guiMsg.data.end());
                            if (!payload.empty()) {
                                try {
                                    uint64_t closedId = std::stoull(payload);
                                    if (closedId == s_windowId) {
                                        Logger::write(LogLevel::Info, "ConsoleWindow closing...");
                                        running = false;
                                    }
                                } catch (const std::exception& e) {
                                    Logger::write(LogLevel::Error, std::string("ConsoleWindow: Failed to parse close ID: ") + e.what());
                                }
                            }
                            break;
                        }
                        
                        case MsgType::MT_InputKey: {
                            // Keyboard input
                            std::string payload(guiMsg.data.begin(), guiMsg.data.end());
                            size_t sep = payload.find('|');
                            if (sep != std::string::npos && sep > 0) {
                                try {
                                    int keyCode = std::stoi(payload.substr(0, sep));
                                    std::string action = payload.substr(sep + 1);
                                    
                                    // Key debouncing
                                    if (action == "down") {
                                        if (s_keyDown && keyCode == s_lastKeyCode) {
                                            break;
                                        }
                                        s_keyDown = true;
                                        s_lastKeyCode = keyCode;
                                        
                                        // Track shift
                                        if (keyCode == 16) {
                                            s_shiftPressed = true;
                                        } else {
                                            handleKeyPress(keyCode);
                                        }
                                    } else {
                                        s_keyDown = false;
                                        s_lastKeyCode = 0;
                                        if (keyCode == 16) {
                                            s_shiftPressed = false;
                                        }
                                    }
                                } catch (const std::exception& e) {
                                    Logger::write(LogLevel::Error, std::string("ConsoleWindow: Failed to parse key code: ") + e.what());
                                }
                            }
                            break;
                        }
                        
                        default:
                            break;
                    }
                }
                
                // Check for console output
                ipc::Message consoleMsg;
                if (ipc::Bus::pop(kConsoleChanOut, consoleMsg, 10)) {
                    std::string output(consoleMsg.data.begin(), consoleMsg.data.end());
                    handleConsoleOutput(output);
                }
            }
            
            Logger::write(LogLevel::Info, "ConsoleWindow stopped");
            return 0;
            
        } catch (const std::exception& e) {
            Logger::write(LogLevel::Error, std::string("ConsoleWindow EXCEPTION: ") + e.what());
            return -1;
        } catch (...) {
            Logger::write(LogLevel::Error, "ConsoleWindow UNKNOWN EXCEPTION");
            return -1;
        }
    }
    
    void ConsoleWindow::sendCommand(const std::string& cmd) {
        if (cmd.empty()) return;
        
        // Add to output display
        s_outputLines.push_back("> " + cmd);
        
        // Add to command history
        s_commandHistory.push_back(cmd);
        if (s_commandHistory.size() > 50) {
            // Keep only last 50 commands
            s_commandHistory.erase(s_commandHistory.begin());
        }
        s_historyIndex = -1;  // Reset history navigation
        
        // Send to console service
        ipc::Message msg;
        msg.srcPid = 0;
        msg.type = 10;
        msg.data.assign(cmd.begin(), cmd.end());
        ipc::Bus::publish("console.input", std::move(msg), false);
        
        // Clear input
        s_currentInput = "";
        
        // Update display
        updateDisplay();
        updateInputLine();
        scrollToBottom();
        
        Logger::write(LogLevel::Info, std::string("ConsoleWindow: Sent command: ") + cmd);
    }
    
    void ConsoleWindow::handleConsoleOutput(const std::string& output) {
        // Add output to display buffer
        s_outputLines.push_back(output);
        
        // Limit buffer size
        if (s_outputLines.size() > 1000) {
            s_outputLines.erase(s_outputLines.begin());
        }
        
        // Update display
        updateDisplay();
        scrollToBottom();
        
        Logger::write(LogLevel::Info, std::string("ConsoleWindow: Received output: ") + output);
    }
    
    void ConsoleWindow::handleKeyPress(int keyCode) {
        // Enter - send command
        if (keyCode == 13) {
            sendCommand(s_currentInput);
            return;
        }
        
        // Backspace - delete character
        if (keyCode == 8) {
            if (!s_currentInput.empty()) {
                s_currentInput = s_currentInput.substr(0, s_currentInput.length() - 1);
                updateInputLine();
            }
            return;
        }
        
        // Up arrow - navigate history up
        if (keyCode == 38) {
            navigateHistoryUp();
            return;
        }
        
        // Down arrow - navigate history down
        if (keyCode == 40) {
            navigateHistoryDown();
            return;
        }
        
        // Escape - clear input
        if (keyCode == 27) {
            s_currentInput = "";
            s_historyIndex = -1;
            updateInputLine();
            return;
        }
        
        // Page Up - scroll up
        if (keyCode == 33) {
            if (s_scrollOffset > 0) {
                s_scrollOffset -= 10;
                if (s_scrollOffset < 0) s_scrollOffset = 0;
                updateDisplay();
            }
            return;
        }
        
        // Page Down - scroll down
        if (keyCode == 34) {
            int maxScroll = (int)s_outputLines.size() - 20;
            if (maxScroll < 0) maxScroll = 0;
            s_scrollOffset += 10;
            if (s_scrollOffset > maxScroll) s_scrollOffset = maxScroll;
            updateDisplay();
            return;
        }
        
        // Printable characters (A-Z, 0-9, space, symbols)
        if (keyCode >= 32 && keyCode < 127) {
            char ch = (char)keyCode;
            
            // Convert uppercase letters to lowercase unless shift is pressed
            if (keyCode >= 65 && keyCode <= 90) {
                if (!s_shiftPressed) {
                    ch = ch + 32;  // Convert to lowercase
                }
            }
            
            // Limit input length
            if (s_currentInput.length() < 100) {
                s_currentInput += ch;
                updateInputLine();
            }
        }
    }
    
    void ConsoleWindow::navigateHistoryUp() {
        if (s_commandHistory.empty()) return;
        
        if (s_historyIndex == -1) {
            // Start from most recent
            s_historyIndex = (int)s_commandHistory.size() - 1;
        } else if (s_historyIndex > 0) {
            // Move to older command
            s_historyIndex--;
        }
        
        if (s_historyIndex >= 0 && s_historyIndex < (int)s_commandHistory.size()) {
            s_currentInput = s_commandHistory[s_historyIndex];
            updateInputLine();
        }
    }
    
    void ConsoleWindow::navigateHistoryDown() {
        if (s_commandHistory.empty() || s_historyIndex == -1) return;
        
        if (s_historyIndex < (int)s_commandHistory.size() - 1) {
            // Move to newer command
            s_historyIndex++;
            s_currentInput = s_commandHistory[s_historyIndex];
        } else {
            // At newest, clear input
            s_historyIndex = -1;
            s_currentInput = "";
        }
        
        updateInputLine();
    }
    
    void ConsoleWindow::updateDisplay() {
        const char* kGuiChanIn = "gui.input";
        
        // Calculate visible lines (20 lines for output)
        int visibleLines = 20;
        int startLine = s_scrollOffset;
        int endLine = std::min((int)s_outputLines.size(), startLine + visibleLines);
        
        // Draw output lines
        for (int i = startLine; i < endLine; i++) {
            ipc::Message msg;
            msg.type = (uint32_t)MsgType::MT_DrawText;
            
            std::ostringstream oss;
            oss << s_windowId << "|" << s_outputLines[i];
            std::string payload = oss.str();
            msg.data.assign(payload.begin(), payload.end());
            ipc::Bus::publish(kGuiChanIn, std::move(msg), false);
        }
    }
    
    void ConsoleWindow::updateInputLine() {
        const char* kGuiChanIn = "gui.input";
        
        ipc::Message msg;
        msg.type = (uint32_t)MsgType::MT_DrawText;
        
        std::ostringstream oss;
        oss << s_windowId << "|> " << s_currentInput << "_";  // Show cursor as underscore
        std::string payload = oss.str();
        msg.data.assign(payload.begin(), payload.end());
        ipc::Bus::publish(kGuiChanIn, std::move(msg), false);
    }
    
    void ConsoleWindow::scrollToBottom() {
        // Auto-scroll to bottom when new output arrives
        int maxScroll = (int)s_outputLines.size() - 20;
        if (maxScroll < 0) maxScroll = 0;
        s_scrollOffset = maxScroll;
    }
    
}} // namespace gxos::apps
