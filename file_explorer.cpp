#include "file_explorer.h"
#include "gui_protocol.h"
#include "logger.h"
#include "notepad.h"
#include <sstream>
#include <algorithm>

namespace gxos { namespace apps {
    
    using namespace gxos::gui;
    
    // Static member initialization
    uint64_t FileExplorer::s_windowId = 0;
    std::string FileExplorer::s_currentPath = "";
    std::vector<VfsEntryInfo> FileExplorer::s_entries;
    int FileExplorer::s_selectedIndex = 0;
    int FileExplorer::s_scrollOffset = 0;
    int FileExplorer::s_lastKeyCode = 0;
    bool FileExplorer::s_keyDown = false;
    
    uint64_t FileExplorer::Launch(const std::string& startPath) {
        ProcessSpec spec{"file_explorer", FileExplorer::main};
        if (startPath.empty()) {
            return ProcessTable::spawn(spec, {"file_explorer"});
        } else {
            return ProcessTable::spawn(spec, {"file_explorer", startPath});
        }
    }
    
    int FileExplorer::main(int argc, char** argv) {
        try {
            Logger::write(LogLevel::Info, "FileExplorer starting...");
            
            // Initialize state
            s_windowId = 0;
            s_currentPath = (argc > 1) ? argv[1] : "data/";
            s_entries.clear();
            s_selectedIndex = 0;
            s_scrollOffset = 0;
            s_lastKeyCode = 0;
            s_keyDown = false;
            
            // Ensure path ends with /
            if (!s_currentPath.empty() && s_currentPath.back() != '/') {
                s_currentPath += '/';
            }
            
            // Load initial directory
            refresh();
            
            // Subscribe to IPC channels
            const char* kGuiChanIn = "gui.input";
            const char* kGuiChanOut = "gui.output";
            ipc::Bus::ensure(kGuiChanIn);
            ipc::Bus::ensure(kGuiChanOut);
            
            // Create window (640x480)
            ipc::Message createMsg;
            createMsg.type = (uint32_t)MsgType::MT_Create;
            std::ostringstream oss;
            oss << "File Explorer - " << s_currentPath << "|640|480";
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
                                    Logger::write(LogLevel::Info, std::string("FileExplorer window created: ") + std::to_string(s_windowId));
                                    
                                    // Add navigation buttons
                                    auto addButton = [](int id, int x, int y, int w, int h, const std::string& text) {
                                        ipc::Message msg;
                                        msg.type = (uint32_t)MsgType::MT_WidgetAdd;
                                        std::ostringstream oss;
                                        oss << s_windowId << "|1|" << id << "|" << x << "|" << y << "|" << w << "|" << h << "|" << text;
                                        std::string payload = oss.str();
                                        msg.data.assign(payload.begin(), payload.end());
                                        ipc::Bus::publish("gui.input", std::move(msg), false);
                                    };
                                    
                                    // Navigation buttons
                                    addButton(1, 4, 4, 60, 20, "Up");
                                    addButton(2, 68, 4, 60, 20, "Home");
                                    addButton(3, 132, 4, 80, 20, "Refresh");
                                    addButton(4, 216, 4, 80, 20, "New Folder");
                                    addButton(5, 300, 4, 60, 20, "Delete");
                                    
                                    // Draw initial content
                                    updatePathDisplay();
                                    updateDisplay();
                                    updateStatusBar();
                                } catch (const std::exception& e) {
                                    Logger::write(LogLevel::Error, std::string("FileExplorer: Failed to parse window ID: ") + e.what());
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
                                        Logger::write(LogLevel::Info, "FileExplorer closing...");
                                        running = false;
                                    }
                                } catch (const std::exception& e) {
                                    Logger::write(LogLevel::Error, std::string("FileExplorer: Failed to parse close ID: ") + e.what());
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
                                    Logger::write(LogLevel::Error, std::string("FileExplorer: Failed to parse key code: ") + e.what());
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
                                            case 1: goUp(); break;
                                            case 2: goHome(); break;
                                            case 3: refresh(); updateDisplay(); updateStatusBar(); break;
                                            case 4: createFolder(); break;
                                            case 5: deleteSelected(); break;
                                        }
                                    }
                                } catch (const std::exception& e) {
                                    Logger::write(LogLevel::Error, std::string("FileExplorer: Failed to parse widget event: ") + e.what());
                                }
                            }
                            break;
                        }
                        
                        default:
                            break;
                    }
                }
            }
            
            Logger::write(LogLevel::Info, "FileExplorer stopped");
            return 0;
            
        } catch (const std::exception& e) {
            Logger::write(LogLevel::Error, std::string("FileExplorer EXCEPTION: ") + e.what());
            return -1;
        } catch (...) {
            Logger::write(LogLevel::Error, "FileExplorer UNKNOWN EXCEPTION");
            return -1;
        }
    }
    
    void FileExplorer::navigate(const std::string& path) {
        s_currentPath = path;
        
        // Ensure path ends with /
        if (!s_currentPath.empty() && s_currentPath.back() != '/') {
            s_currentPath += '/';
        }
        
        refresh();
        s_selectedIndex = 0;
        s_scrollOffset = 0;
        
        updatePathDisplay();
        updateDisplay();
        updateStatusBar();
        
        Logger::write(LogLevel::Info, std::string("FileExplorer: Navigated to ") + s_currentPath);
    }
    
    void FileExplorer::goUp() {
        if (s_currentPath.empty() || s_currentPath == "/") {
            return;
        }
        
        // Remove trailing slash
        std::string path = s_currentPath;
        if (!path.empty() && path.back() == '/') {
            path = path.substr(0, path.length() - 1);
        }
        
        // Find last slash
        size_t lastSlash = path.find_last_of('/');
        if (lastSlash != std::string::npos) {
            navigate(path.substr(0, lastSlash + 1));
        }
    }
    
    void FileExplorer::goHome() {
        navigate("data/");
    }
    
    void FileExplorer::refresh() {
        s_entries = Vfs::instance().list(s_currentPath);
        
        // Sort: directories first, then alphabetically
        std::sort(s_entries.begin(), s_entries.end(), [](const VfsEntryInfo& a, const VfsEntryInfo& b) {
            if (a.isDir != b.isDir) return a.isDir;  // Directories first
            return a.name < b.name;  // Then alphabetically
        });
        
        // Ensure selected index is valid
        if (s_selectedIndex >= (int)s_entries.size()) {
            s_selectedIndex = (int)s_entries.size() - 1;
        }
        if (s_selectedIndex < 0) {
            s_selectedIndex = 0;
        }
        
        Logger::write(LogLevel::Info, std::string("FileExplorer: Loaded ") + std::to_string(s_entries.size()) + " entries");
    }
    
    void FileExplorer::openSelected() {
        if (s_selectedIndex < 0 || s_selectedIndex >= (int)s_entries.size()) {
            return;
        }
        
        const VfsEntryInfo& entry = s_entries[s_selectedIndex];
        
        if (entry.isDir) {
            // Navigate into directory
            navigate(s_currentPath + entry.name + "/");
        } else {
            // Open file - check extension
            std::string fullPath = s_currentPath + entry.name;
            
            // Check if it's a text file (.txt)
            if (entry.name.length() > 4 && entry.name.substr(entry.name.length() - 4) == ".txt") {
                // Launch Notepad with this file
                Logger::write(LogLevel::Info, std::string("FileExplorer: Opening file in Notepad: ") + fullPath);
                Notepad::LaunchWithFile(fullPath);
            } else {
                Logger::write(LogLevel::Info, std::string("FileExplorer: Cannot open file type: ") + entry.name);
            }
        }
    }
    
    void FileExplorer::deleteSelected() {
        if (s_selectedIndex < 0 || s_selectedIndex >= (int)s_entries.size()) {
            return;
        }
        
        const VfsEntryInfo& entry = s_entries[s_selectedIndex];
        Logger::write(LogLevel::Info, std::string("FileExplorer: Delete not yet implemented: ") + entry.name);
        
        // TODO: Implement file deletion when VFS supports it
        // For now, just log the action
    }
    
    void FileExplorer::createFolder() {
        // Create a new folder with a default name
        std::string newFolderPath = s_currentPath + "NewFolder/";
        
        if (Vfs::instance().mkdirs(newFolderPath)) {
            Logger::write(LogLevel::Info, std::string("FileExplorer: Created folder: ") + newFolderPath);
            refresh();
            updateDisplay();
            updateStatusBar();
        } else {
            Logger::write(LogLevel::Error, std::string("FileExplorer: Failed to create folder: ") + newFolderPath);
        }
    }
    
    void FileExplorer::handleKeyPress(int keyCode) {
        // Up arrow - move selection up
        if (keyCode == 38) {
            if (s_selectedIndex > 0) {
                s_selectedIndex--;
                
                // Scroll up if needed
                if (s_selectedIndex < s_scrollOffset) {
                    s_scrollOffset = s_selectedIndex;
                }
                
                updateDisplay();
                updateStatusBar();
            }
        }
        // Down arrow - move selection down
        else if (keyCode == 40) {
            if (s_selectedIndex < (int)s_entries.size() - 1) {
                s_selectedIndex++;
                
                // Scroll down if needed (show 15 items at a time)
                if (s_selectedIndex >= s_scrollOffset + 15) {
                    s_scrollOffset = s_selectedIndex - 14;
                }
                
                updateDisplay();
                updateStatusBar();
            }
        }
        // Enter - open selected item
        else if (keyCode == 13) {
            openSelected();
        }
        // Backspace - go up one directory
        else if (keyCode == 8) {
            goUp();
        }
        // Page Up - scroll up
        else if (keyCode == 33) {
            if (s_scrollOffset > 0) {
                s_scrollOffset -= 10;
                if (s_scrollOffset < 0) s_scrollOffset = 0;
                updateDisplay();
            }
        }
        // Page Down - scroll down
        else if (keyCode == 34) {
            int maxScroll = (int)s_entries.size() - 15;
            if (maxScroll < 0) maxScroll = 0;
            s_scrollOffset += 10;
            if (s_scrollOffset > maxScroll) s_scrollOffset = maxScroll;
            updateDisplay();
        }
        // Delete key - delete selected
        else if (keyCode == 46) {
            deleteSelected();
        }
        // F5 - refresh
        else if (keyCode == 116) {
            refresh();
            updateDisplay();
            updateStatusBar();
        }
    }
    
    void FileExplorer::updateDisplay() {
        const char* kGuiChanIn = "gui.input";
        
        // Calculate visible entries (15 visible items)
        int visibleCount = 15;
        int startIndex = s_scrollOffset;
        int endIndex = std::min((int)s_entries.size(), startIndex + visibleCount);
        
        // Draw entries
        for (int i = startIndex; i < endIndex; i++) {
            const VfsEntryInfo& entry = s_entries[i];
            
            ipc::Message msg;
            msg.type = (uint32_t)MsgType::MT_DrawText;
            
            std::ostringstream oss;
            oss << s_windowId << "|";
            
            // Selection indicator
            if (i == s_selectedIndex) {
                oss << "> ";
            } else {
                oss << "  ";
            }
            
            // Type indicator
            if (entry.isDir) {
                oss << "[DIR]  ";
            } else {
                oss << "[FILE] ";
            }
            
            // Name
            oss << entry.name;
            
            // Size for files
            if (!entry.isDir) {
                oss << "  (" << entry.size << " bytes)";
            }
            
            std::string payload = oss.str();
            msg.data.assign(payload.begin(), payload.end());
            ipc::Bus::publish(kGuiChanIn, std::move(msg), false);
        }
    }
    
    void FileExplorer::updateStatusBar() {
        const char* kGuiChanIn = "gui.input";
        
        ipc::Message msg;
        msg.type = (uint32_t)MsgType::MT_DrawText;
        
        std::ostringstream oss;
        oss << s_windowId << "|" << s_entries.size() << " items";
        
        if (s_selectedIndex >= 0 && s_selectedIndex < (int)s_entries.size()) {
            const VfsEntryInfo& entry = s_entries[s_selectedIndex];
            oss << " | Selected: " << entry.name;
        }
        
        std::string payload = oss.str();
        msg.data.assign(payload.begin(), payload.end());
        ipc::Bus::publish(kGuiChanIn, std::move(msg), false);
    }
    
    void FileExplorer::updatePathDisplay() {
        const char* kGuiChanIn = "gui.input";
        
        // Update window title
        ipc::Message titleMsg;
        titleMsg.type = (uint32_t)MsgType::MT_SetTitle;
        std::ostringstream titleOss;
        titleOss << s_windowId << "|File Explorer - " << s_currentPath;
        std::string titlePayload = titleOss.str();
        titleMsg.data.assign(titlePayload.begin(), titlePayload.end());
        ipc::Bus::publish(kGuiChanIn, std::move(titleMsg), false);
        
        // Draw path as text
        ipc::Message pathMsg;
        pathMsg.type = (uint32_t)MsgType::MT_DrawText;
        std::ostringstream pathOss;
        pathOss << s_windowId << "|Path: " << s_currentPath;
        std::string pathPayload = pathOss.str();
        pathMsg.data.assign(pathPayload.begin(), pathPayload.end());
        ipc::Bus::publish(kGuiChanIn, std::move(pathMsg), false);
    }
    
}} // namespace gxos::apps
