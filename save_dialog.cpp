#include "save_dialog.h"
#include "gui_protocol.h"
#include "logger.h"
#include <sstream>
#include <algorithm>

namespace gxos { namespace dialogs {
    
    using namespace gxos::gui;
    
    // Static member initialization
    uint64_t SaveDialog::s_windowId = 0;
    std::string SaveDialog::s_currentPath = "";
    std::string SaveDialog::s_fileName = "";
    std::vector<VfsEntryInfo> SaveDialog::s_entries;
    int SaveDialog::s_selectedIndex = 0;
    int SaveDialog::s_scrollOffset = 0;
    bool SaveDialog::s_fileNameFocus = true;
    std::function<void(const std::string&)> SaveDialog::s_onSave = nullptr;
    int SaveDialog::s_lastKeyCode = 0;
    bool SaveDialog::s_keyDown = false;
    
    void SaveDialog::Show(int ownerX, int ownerY,
                         const std::string& startPath,
                         const std::string& defaultFileName,
                         std::function<void(const std::string&)> onSave) {
        s_currentPath = startPath.empty() ? "data/" : startPath;
        s_fileName = defaultFileName.empty() ? "untitled.txt" : defaultFileName;
        s_onSave = onSave;
        s_fileNameFocus = true;
        
        // Launch dialog as a new process
        ProcessSpec spec{"save_dialog", SaveDialog::main};
        std::string xStr = std::to_string(ownerX + 40);
        std::string yStr = std::to_string(ownerY + 40);
        ProcessTable::spawn(spec, {"save_dialog", xStr.c_str(), yStr.c_str()});
    }
    
    int SaveDialog::main(int argc, char** argv) {
        try {
            Logger::write(LogLevel::Info, "SaveDialog starting...");
            
            // Parse position from args
            int x = 100, y = 100;
            if (argc >= 3) {
                x = std::stoi(argv[1]);
                y = std::stoi(argv[2]);
            }
            
            // Load directory listing
            refresh();
            
            // Subscribe to IPC channels
            const char* kGuiChanIn = "gui.input";
            const char* kGuiChanOut = "gui.output";
            ipc::Bus::ensure(kGuiChanIn);
            ipc::Bus::ensure(kGuiChanOut);
            
            // Create window
            ipc::Message createMsg;
            createMsg.type = (uint32_t)MsgType::MT_Create;
            std::ostringstream oss;
            oss << "Save As|520|400|" << x << "|" << y;
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
                                    Logger::write(LogLevel::Info, std::string("SaveDialog window created: ") + std::to_string(s_windowId));
                                    
                                    // Add buttons
                                    auto addButton = [](int id, int x, int y, int w, int h, const std::string& text) {
                                        ipc::Message msg;
                                        msg.type = (uint32_t)MsgType::MT_WidgetAdd;
                                        std::ostringstream oss;
                                        oss << s_windowId << "|1|" << id << "|" << x << "|" << y << "|" << w << "|" << h << "|" << text;
                                        std::string payload = oss.str();
                                        msg.data.assign(payload.begin(), payload.end());
                                        ipc::Bus::publish("gui.input", std::move(msg), false);
                                    };
                                    
                                    // Add Up button at top
                                    addButton(1, 10, 30, 60, 22, "Up");
                                    
                                    // Add Save and Cancel buttons at bottom
                                    int btnY = 360;  // Near bottom of 400px window
                                    addButton(2, 320, btnY, 90, 28, "Save");
                                    addButton(3, 418, btnY, 90, 28, "Cancel");
                                    
                                    // Draw initial content
                                    redraw();
                                } catch (const std::exception& e) {
                                    Logger::write(LogLevel::Error, std::string("SaveDialog: Failed to parse window ID: ") + e.what());
                                }
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
                                        Logger::write(LogLevel::Info, "SaveDialog closing...");
                                        running = false;
                                    }
                                } catch (const std::exception& e) {
                                    Logger::write(LogLevel::Error, std::string("SaveDialog: Failed to parse close ID: ") + e.what());
                                }
                            }
                            break;
                        }
                        
                        case MsgType::MT_InputKey: {
                            // Keyboard input
                            std::string payload(msg.data.begin(), msg.data.end());
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
                                        handleKeyPress(keyCode);
                                    } else {
                                        s_keyDown = false;
                                        s_lastKeyCode = 0;
                                    }
                                } catch (const std::exception& e) {
                                    Logger::write(LogLevel::Error, std::string("SaveDialog: Failed to parse key code: ") + e.what());
                                }
                            }
                            break;
                        }
                        
                        case MsgType::MT_WidgetEvt: {
                            // Button click
                            std::string payload(msg.data.begin(), msg.data.end());
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
                                        
                                        switch (widgetId) {
                                            case 1: // Up
                                                goUp();
                                                break;
                                            case 2: // Save
                                                saveAction();
                                                running = false;
                                                break;
                                            case 3: // Cancel
                                                Logger::write(LogLevel::Info, "SaveDialog: Cancelled");
                                                running = false;
                                                break;
                                        }
                                    }
                                } catch (const std::exception& e) {
                                    Logger::write(LogLevel::Error, std::string("SaveDialog: Failed to parse widget event: ") + e.what());
                                }
                            }
                            break;
                        }
                        
                        default:
                            break;
                    }
                }
            }
            
            Logger::write(LogLevel::Info, "SaveDialog stopped");
            return 0;
            
        } catch (const std::exception& e) {
            Logger::write(LogLevel::Error, std::string("SaveDialog EXCEPTION: ") + e.what());
            return -1;
        } catch (...) {
            Logger::write(LogLevel::Error, "SaveDialog UNKNOWN EXCEPTION");
            return -1;
        }
    }
    
    void SaveDialog::navigate(const std::string& path) {
        s_currentPath = path;
        refresh();
        redraw();
    }
    
    void SaveDialog::goUp() {
        if (s_currentPath.empty() || s_currentPath == "/") {
            return;
        }
        
        // Remove trailing slash if present
        std::string path = s_currentPath;
        if (!path.empty() && path[path.length() - 1] == '/') {
            path = path.substr(0, path.length() - 1);
        }
        
        // Find last slash
        size_t lastSlash = path.find_last_of('/');
        if (lastSlash != std::string::npos) {
            navigate(path.substr(0, lastSlash + 1));
        }
    }
    
    void SaveDialog::refresh() {
        s_entries = Vfs::instance().list(s_currentPath);
        s_selectedIndex = std::min(s_selectedIndex, (int)s_entries.size() - 1);
        if (s_selectedIndex < 0) s_selectedIndex = 0;
        
        Logger::write(LogLevel::Info, std::string("SaveDialog: Loaded ") + std::to_string(s_entries.size()) + " entries from " + s_currentPath);
    }
    
    void SaveDialog::saveAction() {
        if (s_fileName.empty()) {
            Logger::write(LogLevel::Warn, "SaveDialog: No filename specified");
            return;
        }
        
        std::string fullPath = s_currentPath + s_fileName;
        Logger::write(LogLevel::Info, std::string("SaveDialog: Saving to ") + fullPath);
        
        if (s_onSave) {
            s_onSave(fullPath);
        }
    }
    
    void SaveDialog::handleKeyPress(int keyCode) {
        // Escape - cancel
        if (keyCode == 27) {
            Logger::write(LogLevel::Info, "SaveDialog: Escape pressed - cancelling");
            // TODO: Close dialog
            return;
        }
        
        // Enter - save
        if (keyCode == 13) {
            saveAction();
            return;
        }
        
        // Tab - toggle focus
        if (keyCode == 9) {
            s_fileNameFocus = !s_fileNameFocus;
            redraw();
            return;
        }
        
        if (s_fileNameFocus) {
            // Typing in filename field
            if (keyCode == 8) { // Backspace
                if (!s_fileName.empty()) {
                    s_fileName = s_fileName.substr(0, s_fileName.length() - 1);
                    redraw();
                }
            } else if (keyCode >= 32 && keyCode <= 126) {
                // Printable characters
                char ch = (char)keyCode;
                // Convert to lowercase for simplicity
                if (ch >= 'A' && ch <= 'Z') {
                    ch = ch - 'A' + 'a';
                }
                s_fileName += ch;
                redraw();
            }
        } else {
            // List navigation
            if (keyCode == 38) { // Up arrow
                if (s_selectedIndex > 0) {
                    s_selectedIndex--;
                    redraw();
                }
            } else if (keyCode == 40) { // Down arrow
                if (s_selectedIndex < (int)s_entries.size() - 1) {
                    s_selectedIndex++;
                    redraw();
                }
            } else if (keyCode == 13) { // Enter - navigate into folder
                if (s_selectedIndex >= 0 && s_selectedIndex < (int)s_entries.size()) {
                    const VfsEntryInfo& entry = s_entries[s_selectedIndex];
                    if (entry.isDir) {
                        navigate(s_currentPath + entry.name + "/");
                    }
                }
            }
        }
    }
    
    void SaveDialog::redraw() {
        const char* kGuiChanIn = "gui.input";
        
        // Draw current path
        ipc::Message pathMsg;
        pathMsg.type = (uint32_t)MsgType::MT_DrawText;
        std::ostringstream pathOss;
        pathOss << s_windowId << "|Path: " << s_currentPath;
        std::string pathPayload = pathOss.str();
        pathMsg.data.assign(pathPayload.begin(), pathPayload.end());
        ipc::Bus::publish(kGuiChanIn, std::move(pathMsg), false);
        
        // Draw file list (up to 10 entries)
        int visibleCount = std::min(10, (int)s_entries.size());
        for (int i = 0; i < visibleCount; i++) {
            int index = s_scrollOffset + i;
            if (index >= (int)s_entries.size()) break;
            
            const VfsEntryInfo& entry = s_entries[index];
            ipc::Message entryMsg;
            entryMsg.type = (uint32_t)MsgType::MT_DrawText;
            std::ostringstream entryOss;
            entryOss << s_windowId << "|";
            if (index == s_selectedIndex) entryOss << "> ";
            if (entry.isDir) entryOss << "[DIR] ";
            entryOss << entry.name;
            std::string entryPayload = entryOss.str();
            entryMsg.data.assign(entryPayload.begin(), entryPayload.end());
            ipc::Bus::publish(kGuiChanIn, std::move(entryMsg), false);
        }
        
        // Draw filename field
        ipc::Message fnMsg;
        fnMsg.type = (uint32_t)MsgType::MT_DrawText;
        std::ostringstream fnOss;
        fnOss << s_windowId << "|File name: ";
        if (s_fileNameFocus) fnOss << ">";
        fnOss << s_fileName;
        if (s_fileNameFocus) fnOss << "<";
        std::string fnPayload = fnOss.str();
        fnMsg.data.assign(fnPayload.begin(), fnPayload.end());
        ipc::Bus::publish(kGuiChanIn, std::move(fnMsg), false);
    }
    
}} // namespace gxos::dialogs
