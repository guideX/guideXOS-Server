#include "notepad.h"
#include "gui_protocol.h"
#include "logger.h"
#include <sstream>
#include <algorithm>

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
    
    uint64_t Notepad::Launch() {
        ProcessSpec spec{"notepad", Notepad::main};
        return ProcessTable::spawn(spec, {"notepad"});
    }
    
    uint64_t Notepad::LaunchWithFile(const std::string& filePath) {
        ProcessSpec spec{"notepad", Notepad::main};
        return ProcessTable::spawn(spec, {"notepad", filePath});
    }
    
    int Notepad::main(int argc, char** argv) {
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
        s_lines.push_back("- File operations (coming soon)");
        s_lines.push_back("- Copy/Paste (coming soon)");
        s_lines.push_back("");
        s_lines.push_back("Type to edit text...");
        s_cursorLine = s_lines.size();
        s_lines.push_back(""); // Add blank line for typing
        s_cursorCol = 0;
        s_modified = false;
        s_scrollOffset = 0;
        
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
        createMsg.data.assign(oss.str().begin(), oss.str().end());
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
                        if (sep != std::string::npos) {
                            s_windowId = std::stoull(payload.substr(0, sep));
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
                            
                            // Add File menu buttons
                            addButton(1, 4, 4, 60, 20, "New");
                            addButton(2, 68, 4, 60, 20, "Open");
                            addButton(3, 132, 4, 60, 20, "Save");
                            addButton(4, 196, 4, 80, 20, "Save As");
                            
                            // Draw initial content
                            redrawContent();
                            updateStatusBar();
                        }
                        break;
                    }
                    
                    case MsgType::MT_Close: {
                        // Window closed
                        std::string payload(msg.data.begin(), msg.data.end());
                        uint64_t closedId = std::stoull(payload);
                        if (closedId == s_windowId) {
                            Logger::write(LogLevel::Info, "Notepad closing...");
                            running = false;
                        }
                        break;
                    }
                    
                    case MsgType::MT_InputKey: {
                        // Keyboard input
                        std::string payload(msg.data.begin(), msg.data.end());
                        size_t sep = payload.find('|');
                        if (sep != std::string::npos) {
                            int keyCode = std::stoi(payload.substr(0, sep));
                            std::string action = payload.substr(sep + 1);
                            
                            if (action == "down") {
                                // Handle printable characters
                                if (keyCode >= 32 && keyCode <= 126) {
                                    char ch = (char)keyCode;
                                    if (s_cursorLine < (int)s_lines.size()) {
                                        s_lines[s_cursorLine].insert(s_cursorCol, 1, ch);
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
                                        s_lines[s_cursorLine].erase(s_cursorCol - 1, 1);
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
                                        std::string remainder = s_lines[s_cursorLine].substr(s_cursorCol);
                                        s_lines[s_cursorLine] = s_lines[s_cursorLine].substr(0, s_cursorCol);
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
                            uint64_t winId = std::stoull(winIdStr);
                            if (winId == s_windowId && event == "click") {
                                int widgetId = std::stoi(widgetIdStr);
                                
                                // Handle button clicks
                                switch (widgetId) {
                                    case 1: newFile(); break;      // New
                                    case 2: openFile(); break;     // Open
                                    case 3: saveFile(); break;     // Save
                                    case 4: saveFileAs(); break;   // Save As
                                }
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
        
        // Clear existing content first (send small delay to let compositor clear)
        
        // Draw visible lines with cursor indicator
        for (size_t i = 0; i < s_lines.size() && i < 25; i++) {
            ipc::Message textMsg;
            textMsg.type = (uint32_t)MsgType::MT_DrawText;
            
            std::string lineText = s_lines[i];
            
            // Add cursor indicator on current line
            if ((int)i == s_cursorLine && s_cursorCol <= (int)lineText.size()) {
                lineText.insert(s_cursorCol, "|");
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
        
        std::string payload = oss.str();
        msg.data.assign(payload.begin(), payload.end());
        ipc::Bus::publish(kGuiChanIn, std::move(msg), false);
    }
    
}} // namespace gxos::apps
