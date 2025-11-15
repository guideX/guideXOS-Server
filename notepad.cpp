#include "notepad.h"
#include "gui_protocol.h"
#include "logger.h"
#include <sstream>
#include <algorithm>
#include <cctype>

namespace gxos { namespace apps {
    
    using namespace gxos::gui;
    
    // Static member initialization
    uint64_t Notepad::s_windowId = 0;
    std::string Notepad::s_filePath = "";
    std::vector<std::string> Notepad::s_lines;
    int Notepad::s_cursorLine = 0;
    int Notepad::s_cursorCol = 0;
    bool Notepad::s_modified = false;
    int Notepad::s_scrollOffset = 0;
    bool Notepad::s_wrapText = true;
    bool Notepad::s_shiftPressed = false;
    bool Notepad::s_ctrlPressed = false;
    bool Notepad::s_capsLockOn = false;
    
    uint64_t Notepad::Launch() {
        ProcessSpec spec{"notepad", Notepad::main};
        return ProcessTable::spawn(spec, {"notepad"});
    }
    
    uint64_t Notepad::LaunchWithFile(const std::string& filePath) {
        ProcessSpec spec{"notepad", Notepad::main};
        return ProcessTable::spawn(spec, {"notepad", filePath});
    }
    
    int Notepad::main(int argc, char** argv) {
        try {
            Logger::write(LogLevel::Info, "Notepad starting...");
            
            // Initialize state
            s_windowId = 0;
            s_filePath = "";
            s_lines.clear();
            s_lines.push_back("Welcome to Notepad!");
            s_lines.push_back("This is a simple text editor.");
            s_lines.push_back("");
            s_lines.push_back("Features:");
            s_lines.push_back("- Multi-line text editing");
            s_lines.push_back("- Tab support (4 spaces)");
            s_lines.push_back("- Text wrapping toggle");
            s_lines.push_back("- Special characters with Shift");
            s_lines.push_back("");
            s_lines.push_back("Type to edit text...");
            s_cursorLine = s_lines.size();
            s_lines.push_back(""); // Add blank line for typing
            s_cursorCol = 0;
            s_modified = false;
            s_scrollOffset = 0;
            s_wrapText = true;
            s_shiftPressed = false;
            s_ctrlPressed = false;
            s_capsLockOn = false;
            
            // Check if file path was provided
            if (argc > 1) {
                s_filePath = argv[1];
                Logger::write(LogLevel::Info, std::string("Notepad: Would load file ") + s_filePath);
            }
            
            // Subscribe to IPC channels
            const char* kGuiChanIn = "gui.input";
            const char* kGuiChanOut = "gui.output";
            ipc::Bus::ensure(kGuiChanIn);
            ipc::Bus::ensure(kGuiChanOut);
            
            // Create window
            ipc::Message createMsg;
            createMsg.type = (uint32_t)MsgType::MT_Create;
            std::string title = s_filePath.empty() ? "Untitled - Notepad" : s_filePath + " - Notepad";
            std::ostringstream oss;
            oss << title << "|640|480";
            std::string payload = oss.str();
            createMsg.data.assign(payload.begin(), payload.end());
            ipc::Bus::publish(kGuiChanIn, std::move(createMsg), false);
            
            // Main event loop
            bool running = true;
            while (running) {
                ipc::Message msg;
                if (ipc::Bus::pop(kGuiChanOut, msg, 100)) {
                    MsgType msgType = (MsgType)msg.type;
                    
                    switch (msgType) {
                        case MsgType::MT_Create: {
                            // Window created - extract window ID
                            std::string payload(msg.data.begin(), msg.data.end());
                            size_t sep = payload.find('|');
                            if (sep != std::string::npos && sep > 0) {
                                try {
                                    std::string idStr = payload.substr(0, sep);
                                    s_windowId = std::stoull(idStr);
                                    Logger::write(LogLevel::Info, std::string("Notepad window created: ") + std::to_string(s_windowId));
                                    
                                    // Add menu buttons
                                    const char* kGuiChanIn = "gui.input";
                                    
                                    // Helper lambda to add a button
                                    auto addButton = [](int id, int x, int y, int w, int h, const std::string& text) {
                                        ipc::Message msg;
                                        msg.type = (uint32_t)MsgType::MT_WidgetAdd;
                                        std::ostringstream oss;
                                        oss << s_windowId << "|1|" << id << "|" << x << "|" << y << "|" << w << "|" << h << "|" << text;
                                        std::string payload = oss.str();
                                        msg.data.assign(payload.begin(), payload.end());
                                        ipc::Bus::publish("gui.input", std::move(msg), false);
                                    };
                                    
                                    // Add menu buttons including Wrap toggle
                                    addButton(1, 4, 4, 60, 20, "New");
                                    addButton(2, 68, 4, 60, 20, "Open");
                                    addButton(3, 132, 4, 60, 20, "Save");
                                    addButton(4, 196, 4, 80, 20, "Save As");
                                    addButton(5, 280, 4, 64, 20, s_wrapText ? "Wrap" : "NoWrap");
                                    
                                    // Draw initial content
                                    redrawContent();
                                    updateStatusBar();
                                } catch (const std::exception& e) {
                                    Logger::write(LogLevel::Error, std::string("Notepad: Failed to parse window ID: ") + e.what() + " payload: " + payload);
                                }
                            } else {
                                Logger::write(LogLevel::Error, std::string("Notepad: Invalid MT_Create payload: ") + payload);
                            }
                            break;
                        }
                        
                        case MsgType::MT_Close: {
                            // Window closed
                            std::string payload(msg.data.begin(), msg.data.end());
                            if (!payload.empty()) {
                                try {
                                    uint64_t closedId = std::stoull(payload);
                                    if (closedId == s_windowId) {
                                        Logger::write(LogLevel::Info, "Notepad closing...");
                                        running = false;
                                    }
                                } catch (const std::exception& e) {
                                    Logger::write(LogLevel::Error, std::string("Notepad: Failed to parse close ID: ") + e.what());
                                }
                            }
                            break;
                        }
                        
                        case MsgType::MT_InputKey: {
                            // Improved keyboard input with modifier support
                            std::string payload(msg.data.begin(), msg.data.end());
                            size_t sep = payload.find('|');
                            if (sep != std::string::npos && sep > 0) {
                                try {
                                    int keyCode = std::stoi(payload.substr(0, sep));
                                    std::string action = payload.substr(sep + 1);
                                    
                                    // Track modifier keys
                                    if (keyCode == 16) { // Shift
                                        s_shiftPressed = (action == "down");
                                        updateStatusBar();
                                    } else if (keyCode == 17) { // Ctrl
                                        s_ctrlPressed = (action == "down");
                                        updateStatusBar();
                                    } else if (keyCode == 20) { // Caps Lock
                                        if (action == "down") {
                                            s_capsLockOn = !s_capsLockOn;
                                            updateStatusBar();
                                        }
                                    }
                                    
                                    if (action == "down") {
                                        // Tab key - insert 4 spaces
                                        if (keyCode == 9) {
                                            if (s_cursorLine < (int)s_lines.size()) {
                                                std::string temp = s_lines[s_cursorLine];
                                                temp.insert(s_cursorCol, "    ");
                                                s_lines[s_cursorLine] = temp;
                                                s_cursorCol += 4;
                                                s_modified = true;
                                                redrawContent();
                                                updateStatusBar();
                                                updateTitle();
                                            }
                                        }
                                        // Handle printable characters with shift support
                                        else if (keyCode >= 32 && keyCode <= 126) {
                                            char ch = mapKeyToChar(keyCode);
                                            if (ch != '\0' && s_cursorLine < (int)s_lines.size()) {
                                                std::string temp = s_lines[s_cursorLine];
                                                temp.insert(s_cursorCol, 1, ch);
                                                s_lines[s_cursorLine] = temp;
                                                s_cursorCol++;
                                                s_modified = true;
                                                redrawContent();
                                                updateStatusBar();
                                                updateTitle();
                                            }
                                        }
                                        // Backspace
                                        else if (keyCode == 8) {
                                            if (s_cursorCol > 0 && s_cursorLine < (int)s_lines.size()) {
                                                std::string temp = s_lines[s_cursorLine];
                                                temp.erase(s_cursorCol - 1, 1);
                                                s_lines[s_cursorLine] = temp;
                                                s_cursorCol--;
                                                s_modified = true;
                                                redrawContent();
                                                updateStatusBar();
                                                updateTitle();
                                            }
                                        }
                                        // Enter
                                        else if (keyCode == 13) {
                                            if (s_cursorLine < (int)s_lines.size()) {
                                                // Copy the current line to avoid iterator issues
                                                std::string currentLine = s_lines[s_cursorLine];
                                                std::string remainder = currentLine.substr(s_cursorCol);
                                                std::string newCurrentLine = currentLine.substr(0, s_cursorCol);
                                                
                                                // Update current line
                                                s_lines[s_cursorLine] = newCurrentLine;
                                                
                                                // Insert new line
                                                s_lines.insert(s_lines.begin() + s_cursorLine + 1, remainder);
                                                s_cursorLine++;
                                                s_cursorCol = 0;
                                                s_modified = true;
                                                redrawContent();
                                                updateStatusBar();
                                                updateTitle();
                                            }
                                        }
                                        // Arrow keys
                                        else if (keyCode == 37) { // Left
                                            if (s_cursorCol > 0) {
                                                s_cursorCol--;
                                                redrawContent();
                                                updateStatusBar();
                                            }
                                        }
                                        else if (keyCode == 38) { // Up
                                            if (s_cursorLine > 0) {
                                                s_cursorLine--;
                                                if (s_cursorCol > (int)s_lines[s_cursorLine].size()) {
                                                    s_cursorCol = (int)s_lines[s_cursorLine].size();
                                                }
                                                redrawContent();
                                                updateStatusBar();
                                            }
                                        }
                                        else if (keyCode == 39) { // Right
                                            if (s_cursorLine < (int)s_lines.size() && s_cursorCol < (int)s_lines[s_cursorLine].size()) {
                                                s_cursorCol++;
                                                redrawContent();
                                                updateStatusBar();
                                            }
                                        }
                                        else if (keyCode == 40) { // Down
                                            if (s_cursorLine < (int)s_lines.size() - 1) {
                                                s_cursorLine++;
                                                if (s_cursorCol > (int)s_lines[s_cursorLine].size()) {
                                                    s_cursorCol = (int)s_lines[s_cursorLine].size();
                                                }
                                                redrawContent();
                                                updateStatusBar();
                                            }
                                        }
                                    }
                                } catch (const std::exception& e) {
                                    Logger::write(LogLevel::Error, std::string("Notepad: Failed to parse key code: ") + e.what());
                                }
                            }
                            break;
                        }
                        
                        case MsgType::MT_WidgetEvt: {
                            // Widget event (button click)
                            std::string payload(msg.data.begin(), msg.data.end());
                            
                            // Parse: <winId>|<widgetId>|<event>|<value>
                            std::istringstream iss(payload);
                            std::string winIdStr, widgetIdStr, event, value;
                            std::getline(iss, winIdStr, '|');
                            std::getline(iss, widgetIdStr, '|');
                            std::getline(iss, event, '|');
                            std::getline(iss, value);
                            
                            if (!winIdStr.empty() && !widgetIdStr.empty()) {
                                try {
                                    uint64_t winId = std::stoull(winIdStr);
                                    if (winId == s_windowId && event == "click") {
                                        int widgetId = std::stoi(widgetIdStr);
                                        
                                        // Handle button clicks
                                        switch (widgetId) {
                                            case 1: newFile(); break;
                                            case 2: openFile(); break;
                                            case 3: saveFile(); break;
                                            case 4: saveFileAs(); break;
                                            case 5: toggleWrap(); break;
                                        }
                                    }
                                } catch (const std::exception& e) {
                                    Logger::write(LogLevel::Error, std::string("Notepad: Failed to parse widget event: ") + e.what());
                                }
                            }
                            break;
                        }
                        
                        default:
                            break;
                    }
                }
            }
            
            Logger::write(LogLevel::Info, "Notepad stopped");
            return 0;
            
        } catch (const std::exception& e) {
            Logger::write(LogLevel::Error, std::string("Notepad EXCEPTION: ") + e.what());
            return -1;
        } catch (...) {
            Logger::write(LogLevel::Error, "Notepad UNKNOWN EXCEPTION");
            return -1;
        }
    }
    
    void Notepad::handleMessage(const ipc::Message& m) {
        // Not used in this simple version
    }
    
    void Notepad::insertText(const std::string& text) {
        // TODO: Implement
    }
    
    void Notepad::deleteSelection() {
        // TODO: Implement
    }
    
    void Notepad::copy() {
        Logger::write(LogLevel::Info, "Notepad: Copy (not implemented)");
    }
    
    void Notepad::paste() {
        Logger::write(LogLevel::Info, "Notepad: Paste (not implemented)");
    }
    
    void Notepad::selectAll() {
        Logger::write(LogLevel::Info, "Notepad: Select All (not implemented)");
    }
    
    void Notepad::newFile() {
        Logger::write(LogLevel::Info, "Notepad: New file");
        s_filePath = "";
        s_lines.clear();
        s_lines.push_back("");
        s_cursorLine = 0;
        s_cursorCol = 0;
        s_modified = false;
        s_scrollOffset = 0;
        updateTitle();
        redrawContent();
        updateStatusBar();
    }
    
    void Notepad::openFile() {
        Logger::write(LogLevel::Info, "Notepad: Open file (not implemented - needs VFS integration)");
    }
    
    void Notepad::saveFile() {
        if (s_filePath.empty()) {
            saveFileAs();
        } else {
            Logger::write(LogLevel::Info, std::string("Notepad: Saving to ") + s_filePath);
            s_modified = false;
            updateTitle();
        }
    }
    
    void Notepad::saveFileAs() {
        Logger::write(LogLevel::Info, "Notepad: Save As (not implemented - needs VFS integration)");
    }
    
    void Notepad::updateTitle() {
        const char* kGuiChanIn = "gui.input";
        std::string title = s_filePath.empty() ? "Untitled" : s_filePath;
        if (s_modified) title += "*";
        title += " - Notepad";
        
        ipc::Message msg;
        msg.type = (uint32_t)MsgType::MT_SetTitle;
        std::ostringstream oss;
        oss << s_windowId << "|" << title;
        std::string payload = oss.str();
        msg.data.assign(payload.begin(), payload.end());
        ipc::Bus::publish(kGuiChanIn, std::move(msg), false);
    }
    
    void Notepad::redrawContent() {
        const char* kGuiChanIn = "gui.input";
        
        // Draw visible lines with cursor indicator
        for (size_t i = 0; i < s_lines.size() && i < 25; i++) {
            ipc::Message textMsg;
            textMsg.type = (uint32_t)MsgType::MT_DrawText;
            
            // Build the line text with cursor if needed
            std::string lineText;
            if ((int)i == s_cursorLine && s_cursorCol <= (int)s_lines[i].size()) {
                // Use simple string operations - no iterators, no character access
                const std::string& sourceLine = s_lines[i];
                
                // Build line with cursor using basic string operations
                if (s_cursorCol == 0) {
                    // Cursor at start: "|rest"
                    lineText = "|";
                    lineText += sourceLine;
                } else if (s_cursorCol >= (int)sourceLine.size()) {
                    // Cursor at end: "text|"
                    lineText = sourceLine;
                    lineText += "|";
                } else {
                    // Cursor in middle: manually build with + operator
                    // This avoids substr() which can trigger iterator issues
                    lineText.reserve(sourceLine.size() + 1);
                    
                    // Copy chars before cursor
                    for (int j = 0; j < s_cursorCol; j++) {
                        lineText += sourceLine[j];
                    }
                    
                    // Add cursor
                    lineText += '|';
                    
                    // Copy chars after cursor
                    for (size_t j = s_cursorCol; j < sourceLine.size(); j++) {
                        lineText += sourceLine[j];
                    }
                }
            } else {
                // No cursor on this line - direct copy
                lineText = s_lines[i];
            }
            
            std::ostringstream oss;
            oss << s_windowId << "|" << lineText;
            std::string payload = oss.str();
            textMsg.data.assign(payload.begin(), payload.end());
            ipc::Bus::publish(kGuiChanIn, std::move(textMsg), false);
        }
    }
    
    void Notepad::updateStatusBar() {
        const char* kGuiChanIn = "gui.input";
        
        ipc::Message msg;
        msg.type = (uint32_t)MsgType::MT_DrawText;
        
        std::ostringstream oss;
        oss << s_windowId << "|Line " << (s_cursorLine + 1) << ", Col " << (s_cursorCol + 1);
        if (s_modified) oss << " (Modified)";
        
        // Add modifier status
        if (s_capsLockOn) oss << " [CAPS]";
        if (s_shiftPressed) oss << " [SHIFT]";
        if (s_ctrlPressed) oss << " [CTRL]";
        
        std::string payload = oss.str();
        msg.data.assign(payload.begin(), payload.end());
        ipc::Bus::publish(kGuiChanIn, std::move(msg), false);
    }
    
    void Notepad::toggleWrap() {
        s_wrapText = !s_wrapText;
        Logger::write(LogLevel::Info, std::string("Notepad: Text wrapping ") + (s_wrapText ? "enabled" : "disabled"));
        
        // Recreate button with updated text
        ipc::Message msg;
        msg.type = (uint32_t)MsgType::MT_WidgetAdd;
        std::ostringstream oss;
        oss << s_windowId << "|1|5|280|4|64|20|" << (s_wrapText ? "Wrap" : "NoWrap");
        std::string payload = oss.str();
        msg.data.assign(payload.begin(), payload.end());
        ipc::Bus::publish("gui.input", std::move(msg), false);
        
        redrawContent();
    }
    
    char Notepad::mapKeyToChar(int keyCode) {
        // Handle letters (65-90 = A-Z)
        if (keyCode >= 65 && keyCode <= 90) {
            char c = 'a' + (keyCode - 65);
            // Apply shift/caps logic
            bool shouldUppercase = s_shiftPressed ^ s_capsLockOn;
            if (shouldUppercase) {
                c = 'A' + (keyCode - 65);
            }
            return c;
        }
        
        // Handle numbers and shifted symbols (48-57 = 0-9)
        if (keyCode >= 48 && keyCode <= 57) {
            if (s_shiftPressed) {
                // Shifted number symbols
                const char* symbols = ")!@#$%^&*(";
                return symbols[keyCode - 48];
            } else {
                return '0' + (keyCode - 48);
            }
        }
        
        // Handle special characters
        switch (keyCode) {
            case 32: return ' '; // Space
            case 186: return s_shiftPressed ? ':' : ';'; // OEM_1
            case 187: return s_shiftPressed ? '+' : '='; // OEM_PLUS
            case 188: return s_shiftPressed ? '<' : ','; // OEM_COMMA
            case 189: return s_shiftPressed ? '_' : '-'; // OEM_MINUS
            case 190: return s_shiftPressed ? '>' : '.'; // OEM_PERIOD
            case 191: return s_shiftPressed ? '?' : '/'; // OEM_2
            case 192: return s_shiftPressed ? '~' : '`'; // OEM_3
            case 219: return s_shiftPressed ? '{' : '['; // OEM_4
            case 220: return s_shiftPressed ? '|' : '\\'; // OEM_5
            case 221: return s_shiftPressed ? '}' : ']'; // OEM_6
            case 222: return s_shiftPressed ? '"' : '\''; // OEM_7
            default: return (char)keyCode; // Fallback
        }
    }
    
}} // namespace gxos::apps
