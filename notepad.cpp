#include "notepad.h"
#include "gui_protocol.h"
#include "logger.h"
#include "vfs.h"
#include "desktop_service.h"
#include "save_dialog.h"
#include "save_changes_dialog.h"
#include "open_dialog.h"
#include <sstream>
#include <algorithm>
#include <cctype>

#ifndef _WIN32
#include "kernel/core/include/kernel/vfs.h"
#endif

namespace gxos { namespace apps {
    
    using namespace gxos::gui;
    using namespace gxos::dialogs;
    
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
    int Notepad::s_lastKeyCode = 0;
    bool Notepad::s_keyDown = false;
    bool Notepad::s_pendingClose = false;
    bool Notepad::s_contextMenuVisible = false;
    int Notepad::s_contextMenuX = 0;
    int Notepad::s_contextMenuY = 0;
    int Notepad::s_contextMenuHoverIndex = -1;
    std::vector<Notepad::TextSnapshot> Notepad::s_undoStack;
    std::vector<Notepad::TextSnapshot> Notepad::s_redoStack;
    
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
            s_lines.push_back("");
            s_cursorLine = 0;
            s_cursorCol = 0;
            s_modified = false;
            s_scrollOffset = 0;
            s_wrapText = true;
            s_shiftPressed = false;
            s_ctrlPressed = false;
            s_capsLockOn = false;
            s_undoStack.clear();
            s_redoStack.clear();
            
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
                                    
                                    // Add toolbar buttons matching Legacy Notepad
                                    addButton(1, 4, 4, 60, 20, "New");
                                    addButton(2, 68, 4, 60, 20, "Open");
                                    addButton(3, 132, 4, 60, 20, "Save");
                                    addButton(4, 196, 4, 80, 20, "Save As");
                                    addButton(5, 280, 4, 64, 20, s_wrapText ? "Wrap" : "NoWrap");
                                    addButton(6, 348, 4, 60, 20, "Undo");
                                    addButton(7, 412, 4, 60, 20, "Redo");
                                    
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
                            // Window closed - check for unsaved changes
                            std::string payload(msg.data.begin(), msg.data.end());
                            if (!payload.empty()) {
                                try {
                                    uint64_t closedId = std::stoull(payload);
                                    if (closedId == s_windowId) {
                                        if (s_modified && !s_pendingClose) {
                                            // Has unsaved changes - show dialog
                                            Logger::write(LogLevel::Info, "Notepad: Unsaved changes - showing dialog...");
                                            closeWithPrompt();
                                        } else {
                                            // No unsaved changes or already confirmed - close now
                                            Logger::write(LogLevel::Info, "Notepad closing...");
                                            running = false;
                                        }
                                    }
                                } catch (const std::exception& e) {
                                    Logger::write(LogLevel::Error, std::string("Notepad: Failed to parse close ID: ") + e.what());
                                }
                            }
                            break;
                        }
                        
                        case MsgType::MT_InputKey: {
                            // Improved keyboard input with modifier support and debouncing
                            std::string payload(msg.data.begin(), msg.data.end());
                            size_t sep = payload.find('|');
                            if (sep != std::string::npos && sep > 0) {
                                try {
                                    int keyCode = std::stoi(payload.substr(0, sep));
                                    std::string action = payload.substr(sep + 1);
                                    
                                    // Key debouncing - ignore repeated key down events
                                    if (action == "down") {
                                        if (s_keyDown && keyCode == s_lastKeyCode) {
                                            break; // Ignore repeat
                                        }
                                        s_keyDown = true;
                                        s_lastKeyCode = keyCode;
                                    } else {
                                        s_keyDown = false;
                                        s_lastKeyCode = 0;
                                    }
                                    
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
                                        // Ctrl+S - Save
                                        if (s_ctrlPressed && (keyCode == 83 || keyCode == 115)) {
                                            saveFile();
                                            break;
                                        }
                                        // Ctrl+N - New
                                        else if (s_ctrlPressed && (keyCode == 78 || keyCode == 110)) {
                                            newFile();
                                            break;
                                        }
                                        // Ctrl+O - Open
                                        else if (s_ctrlPressed && (keyCode == 79 || keyCode == 111)) {
                                            openFileDialog();
                                            break;
                                        }
                                        // Ctrl+Z - Undo
                                        else if (s_ctrlPressed && (keyCode == 90 || keyCode == 122)) {
                                            performUndo();
                                            break;
                                        }
                                        // Ctrl+Y - Redo
                                        else if (s_ctrlPressed && (keyCode == 89 || keyCode == 121)) {
                                            performRedo();
                                            break;
                                        }
                                        // Ctrl+A - Select All
                                        else if (s_ctrlPressed && (keyCode == 65 || keyCode == 97)) {
                                            selectAll();
                                            break;
                                        }
                                        // Escape key
                                        if (keyCode == 27) {
                                            Logger::write(LogLevel::Info, "Notepad: Escape pressed");
                                            break;
                                        }
                                        // Page Up - scroll up
                                        else if (keyCode == 33) {
                                            if (s_scrollOffset > 0) {
                                                s_scrollOffset -= 10;
                                                if (s_scrollOffset < 0) s_scrollOffset = 0;
                                                redrawContent();
                                            }
                                        }
                                        // Page Down - scroll down
                                        else if (keyCode == 34) {
                                            int maxScroll = (int)s_lines.size() - 25;
                                            if (maxScroll < 0) maxScroll = 0;
                                            s_scrollOffset += 10;
                                            if (s_scrollOffset > maxScroll) s_scrollOffset = maxScroll;
                                            redrawContent();
                                        }
                                        // Home - beginning of line
                                        else if (keyCode == 36) {
                                            s_cursorCol = 0;
                                            redrawContent();
                                            updateStatusBar();
                                        }
                                        // End - end of line
                                        else if (keyCode == 35) {
                                            if (s_cursorLine < (int)s_lines.size()) {
                                                s_cursorCol = (int)s_lines[s_cursorLine].size();
                                                redrawContent();
                                                updateStatusBar();
                                            }
                                        }
                                        // Tab key - insert 4 spaces
                                        else if (keyCode == 9) {
                                            if (s_cursorLine < (int)s_lines.size()) {
                                                pushUndo();
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
                                        // Backspace
                                        else if (keyCode == 8) {
                                            if (s_cursorCol > 0 && s_cursorLine < (int)s_lines.size()) {
                                                pushUndo();
                                                std::string temp = s_lines[s_cursorLine];
                                                temp.erase(s_cursorCol - 1, 1);
                                                s_lines[s_cursorLine] = temp;
                                                s_cursorCol--;
                                                s_modified = true;
                                                redrawContent();
                                                updateStatusBar();
                                                updateTitle();
                                            } else if (s_cursorCol == 0 && s_cursorLine > 0) {
                                                // Join with previous line (matching Legacy behavior)
                                                pushUndo();
                                                int prevLen = (int)s_lines[s_cursorLine - 1].size();
                                                s_lines[s_cursorLine - 1] += s_lines[s_cursorLine];
                                                s_lines.erase(s_lines.begin() + s_cursorLine);
                                                s_cursorLine--;
                                                s_cursorCol = prevLen;
                                                s_modified = true;
                                                redrawContent();
                                                updateStatusBar();
                                                updateTitle();
                                            }
                                        }
                                        // Enter (VK_RETURN=13, also accept '\n'=10)
                                        else if (keyCode == 13 || keyCode == 10) {
                                            if (s_cursorLine < (int)s_lines.size()) {
                                                pushUndo();
                                                std::string currentLine = s_lines[s_cursorLine];
                                                std::string remainder = currentLine.substr(s_cursorCol);
                                                std::string newCurrentLine = currentLine.substr(0, s_cursorCol);

                                                s_lines[s_cursorLine] = newCurrentLine;

                                                s_lines.insert(s_lines.begin() + s_cursorLine + 1, remainder);
                                                s_cursorLine++;
                                                s_cursorCol = 0;
                                                s_modified = true;
                                                redrawContent();
                                                updateStatusBar();
                                                updateTitle();
                                            }
                                        }
                                        // Delete key (forward delete, VK_DELETE=46)
                                        else if (keyCode == 46) {
                                            deleteChar();
                                        }
                                        // Handle printable characters with shift support
                                        else if (keyCode >= 32 && keyCode <= 126) {
                                            char ch = mapKeyToChar(keyCode);
                                            if (ch != '\0' && s_cursorLine < (int)s_lines.size()) {
                                                pushUndo();
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
                                            case 2: openFileDialog(); break;
                                            case 3: saveFile(); break;
                                            case 4: saveFileAs(); break;
                                            case 5: toggleWrap(); break;
                                            case 6: performUndo(); break;
                                            case 7: performRedo(); break;
                                        }
                                    }
                                } catch (const std::exception& e) {
                                    Logger::write(LogLevel::Error, std::string("Notepad: Failed to parse widget event: ") + e.what());
                                }
                            }
                            break;
                        }
                        
                        case MsgType::MT_InputMouse: {
                            // Handle mouse input
                            std::string payload(msg.data.begin(), msg.data.end());
                            // Parse: <x>|<y>|<button>|<action>
                            std::istringstream iss(payload);
                            std::string xStr, yStr, buttonStr, action;
                            std::getline(iss, xStr, '|');
                            std::getline(iss, yStr, '|');
                            std::getline(iss, buttonStr, '|');
                            std::getline(iss, action);
                            
                            if (!xStr.empty() && !yStr.empty() && !buttonStr.empty()) {
                                try {
                                    int mx = std::stoi(xStr);
                                    int my = std::stoi(yStr);
                                    int button = std::stoi(buttonStr);
                                    
                                    // Button 2 = right click
                                    if (button == 2 && action == "down") {
                                        // Show context menu at mouse position
                                        showContextMenu(mx, my);
                                        redrawContent();
                                    }
                                    // Left click dismisses context menu
                                    else if (button == 1 && action == "down" && s_contextMenuVisible) {
                                        if (!handleContextMenuClick(mx, my)) {
                                            hideContextMenu();
                                            redrawContent();
                                        }
                                    }
                                } catch (const std::exception& e) {
                                    Logger::write(LogLevel::Error, std::string("Notepad: Failed to parse mouse event: ") + e.what());
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
        if (text.empty() || s_cursorLine >= (int)s_lines.size()) return;
        pushUndo();
        for (char ch : text) {
            if (ch == '\n') {
                std::string remainder = s_lines[s_cursorLine].substr(s_cursorCol);
                s_lines[s_cursorLine] = s_lines[s_cursorLine].substr(0, s_cursorCol);
                s_lines.insert(s_lines.begin() + s_cursorLine + 1, remainder);
                s_cursorLine++;
                s_cursorCol = 0;
            } else {
                s_lines[s_cursorLine].insert(s_cursorCol, 1, ch);
                s_cursorCol++;
            }
        }
        s_modified = true;
        redrawContent();
        updateStatusBar();
        updateTitle();
    }
    
    void Notepad::deleteChar() {
        if (s_cursorLine >= (int)s_lines.size()) return;
        if (s_cursorCol < (int)s_lines[s_cursorLine].size()) {
            pushUndo();
            s_lines[s_cursorLine].erase(s_cursorCol, 1);
            s_modified = true;
            redrawContent();
            updateStatusBar();
            updateTitle();
        } else if (s_cursorLine < (int)s_lines.size() - 1) {
            // Join with next line
            pushUndo();
            s_lines[s_cursorLine] += s_lines[s_cursorLine + 1];
            s_lines.erase(s_lines.begin() + s_cursorLine + 1);
            s_modified = true;
            redrawContent();
            updateStatusBar();
            updateTitle();
        }
    }
    
    void Notepad::deleteSelection() {
        // TODO: Implement selection-based delete
    }
    
    void Notepad::copy() {
        Logger::write(LogLevel::Info, "Notepad: Copy (not implemented)");
    }
    
    void Notepad::paste() {
        Logger::write(LogLevel::Info, "Notepad: Paste (not implemented)");
    }
    
    void Notepad::selectAll() {
        // Select all text: move cursor to end of all content
        // In a full implementation this would set selection start/end markers
        // For now, position cursor at start and log
        if (!s_lines.empty()) {
            s_cursorLine = 0;
            s_cursorCol = 0;
            // Move cursor to the very end
            s_cursorLine = (int)s_lines.size() - 1;
            s_cursorCol = (int)s_lines[s_cursorLine].size();
            Logger::write(LogLevel::Info, "Notepad: Select All (cursor moved to end)");
            redrawContent();
            updateStatusBar();
        }
    }
    
    void Notepad::pushUndo() {
        if ((int)s_undoStack.size() >= kMaxUndo) {
            s_undoStack.erase(s_undoStack.begin());
        }
        TextSnapshot snap;
        snap.lines = s_lines;
        snap.cursorLine = s_cursorLine;
        snap.cursorCol = s_cursorCol;
        s_undoStack.push_back(snap);
        s_redoStack.clear();
    }
    
    void Notepad::performUndo() {
        if (s_undoStack.empty()) return;
        TextSnapshot redo;
        redo.lines = s_lines;
        redo.cursorLine = s_cursorLine;
        redo.cursorCol = s_cursorCol;
        s_redoStack.push_back(redo);
        
        TextSnapshot& snap = s_undoStack.back();
        s_lines = snap.lines;
        s_cursorLine = snap.cursorLine;
        s_cursorCol = snap.cursorCol;
        s_undoStack.pop_back();
        s_modified = true;
        redrawContent();
        updateStatusBar();
        updateTitle();
    }
    
    void Notepad::performRedo() {
        if (s_redoStack.empty()) return;
        TextSnapshot undo;
        undo.lines = s_lines;
        undo.cursorLine = s_cursorLine;
        undo.cursorCol = s_cursorCol;
        s_undoStack.push_back(undo);
        
        TextSnapshot& snap = s_redoStack.back();
        s_lines = snap.lines;
        s_cursorLine = snap.cursorLine;
        s_cursorCol = snap.cursorCol;
        s_redoStack.pop_back();
        s_modified = true;
        redrawContent();
        updateStatusBar();
        updateTitle();
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
        s_undoStack.clear();
        s_redoStack.clear();
        updateTitle();
        redrawContent();
        updateStatusBar();
    }
    
    void Notepad::openFile() {
        if (s_filePath.empty()) {
            openFileDialog();
            return;
        }
        loadFile(s_filePath);
    }
    
    void Notepad::openFileDialog() {
        Logger::write(LogLevel::Info, "Notepad: Opening file dialog...");
        int ownerX = 100;
        int ownerY = 100;
        OpenDialog::Show(ownerX, ownerY, "data/",
            [](const std::string& path) {
                loadFile(path);
            }
        );
    }
    
    void Notepad::loadFile(const std::string& path) {
        if (path.empty()) return;
        
        std::vector<uint8_t> data;
#ifndef _WIN32
        kernel::vfs::FileInfo info{};
        if (kernel::vfs::stat(path.c_str(), &info) != kernel::vfs::VFS_OK || info.type == kernel::vfs::FILE_TYPE_DIRECTORY) {
            Logger::write(LogLevel::Error, std::string("Notepad: Failed to stat file: ") + path);
            return;
        }

        data.resize(static_cast<size_t>(info.size));
        int32_t bytesRead = kernel::vfs::read_file(path.c_str(), data.data(), static_cast<uint32_t>(data.size()));
        if (bytesRead < 0) {
            Logger::write(LogLevel::Error, std::string("Notepad: Failed to read file: ") + path);
            return;
        }
        data.resize(static_cast<size_t>(bytesRead));
#else
        if (!Vfs::instance().readFile(path, data)) {
            Logger::write(LogLevel::Error, std::string("Notepad: Failed to read file: ") + path);
            return;
        }
#endif
        
        Logger::write(LogLevel::Info, std::string("Notepad: Loaded file: ") + path + " (" + std::to_string(data.size()) + " bytes)");
        
        // Parse file content into lines (matching Legacy OpenFile behavior)
        s_lines.clear();
        std::string currentLine;
        for (uint8_t byte : data) {
            if (byte == '\n') {
                s_lines.push_back(currentLine);
                currentLine.clear();
            } else if (byte >= 32 && byte < 127) {
                currentLine += (char)byte;
            } else if (byte == '\t') {
                currentLine += "    ";
            }
        }
        if (!currentLine.empty() || s_lines.empty()) {
            s_lines.push_back(currentLine);
        }
        
        s_filePath = path;
        s_cursorLine = 0;
        s_cursorCol = 0;
        s_modified = false;
        s_scrollOffset = 0;
        s_undoStack.clear();
        s_redoStack.clear();
        
        DesktopService::AddRecentDocument(path);
        
        updateTitle();
        redrawContent();
        updateStatusBar();
    }
    
    void Notepad::saveFile() {
        if (s_filePath.empty()) {
            saveFileAs();
            return;
        }
        
        // Serialize lines to byte vector
        std::vector<uint8_t> data;
        for (size_t i = 0; i < s_lines.size(); i++) {
            const std::string& line = s_lines[i];
            for (char ch : line) {
                data.push_back((uint8_t)ch);
            }
            // Add newline after each line except the last
            if (i < s_lines.size() - 1) {
                data.push_back((uint8_t)'\n');
            }
        }
        
#ifndef _WIN32
        int32_t bytesWritten = kernel::vfs::write_file(s_filePath.c_str(), data.data(), static_cast<uint32_t>(data.size()));
        if (bytesWritten < 0 || static_cast<size_t>(bytesWritten) != data.size()) {
            Logger::write(LogLevel::Error, std::string("Notepad: Failed to write file: ") + s_filePath);
            return;
        }
#else
        if (!Vfs::instance().writeFile(s_filePath, data)) {
            Logger::write(LogLevel::Error, std::string("Notepad: Failed to write file: ") + s_filePath);
            return;
        }
#endif
        
        Logger::write(LogLevel::Info, std::string("Notepad: Saved to ") + s_filePath + " (" + std::to_string(data.size()) + " bytes)");
        s_modified = false;
        
        // Add to recent documents
        DesktopService::AddRecentDocument(s_filePath);
        
        updateTitle();
        updateStatusBar();
    }
    
    void Notepad::saveFileAs() {
        // Show Save Dialog
        Logger::write(LogLevel::Info, "Notepad: Opening Save As dialog...");
        
        // TODO: Get window position - for now use defaults
        int ownerX = 100;
        int ownerY = 100;
        
        // Extract just the filename from path if we have one
        std::string fileName = "untitled.txt";
        if (!s_filePath.empty()) {
            size_t lastSlash = s_filePath.find_last_of('/');
            if (lastSlash != std::string::npos) {
                fileName = s_filePath.substr(lastSlash + 1);
            } else {
                fileName = s_filePath;
            }
        }
        
        SaveDialog::Show(ownerX, ownerY, "data/", fileName,
            [](const std::string& path) {
                // Save callback
                s_filePath = path;
                saveFile();
            }
        );
    }
    
    void Notepad::closeWithPrompt() {
        // Show SaveChangesDialog
        Logger::write(LogLevel::Info, "Notepad: Showing save changes dialog...");
        
        // TODO: Get window position - for now use defaults
        int ownerX = 100;
        int ownerY = 100;
        
        SaveChangesDialog::Show(ownerX, ownerY,
            []() {
                // Save clicked
                Logger::write(LogLevel::Info, "SaveChangesDialog: User chose Save");
                if (s_filePath.empty()) {
                    // Need to show SaveDialog first
                    SaveDialog::Show(100, 100, "data/", "untitled.txt",
                        [](const std::string& path) {
                            s_filePath = path;
                            saveFile();
                            s_pendingClose = true;
                            // Window will close after this
                        }
                    );
                } else {
                    // Save to existing file
                    saveFile();
                    s_pendingClose = true;
                    // Window will close after this
                }
            },
            []() {
                // Don't Save clicked
                Logger::write(LogLevel::Info, "SaveChangesDialog: User chose Don't Save");
                s_pendingClose = true;
                s_modified = false;  // Clear modified flag so close proceeds
                // Window will close after this
            },
            []() {
                // Cancel clicked
                Logger::write(LogLevel::Info, "SaveChangesDialog: User chose Cancel");
                // Do nothing - window stays open
            }
        );
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
        
        // Calculate visible window - support scrolling
        int visibleLines = 25; // Maximum lines to display
        int startLine = s_scrollOffset;
        int endLine = std::min((int)s_lines.size(), startLine + visibleLines);
        
        // Draw visible lines with cursor indicator
        for (int i = startLine; i < endLine; i++) {
            ipc::Message textMsg;
            textMsg.type = (uint32_t)MsgType::MT_DrawText;
            
            // Build the line text with cursor if needed
            std::string lineText;
            const std::string& sourceLine = s_lines[i];
            
            // Apply text wrapping if enabled
            std::string displayLine = sourceLine;
            if (s_wrapText && displayLine.length() > 80) {
                // Simple wrapping at 80 characters
                displayLine = displayLine.substr(0, 80);
            }
            
            if (i == s_cursorLine && s_cursorCol <= (int)sourceLine.size()) {
                // Build line with cursor using basic string operations
                if (s_cursorCol == 0) {
                    lineText = "|";
                    lineText += displayLine;
                } else if (s_cursorCol >= (int)displayLine.size()) {
                    lineText = displayLine;
                    lineText += "|";
                } else {
                    lineText.reserve(displayLine.size() + 1);
                    for (int j = 0; j < s_cursorCol; j++) {
                        lineText += displayLine[j];
                    }
                    lineText += '|';
                    for (size_t j = s_cursorCol; j < displayLine.size(); j++) {
                        lineText += displayLine[j];
                    }
                }
            } else {
                lineText = displayLine;
            }
            
            std::ostringstream oss;
            oss << s_windowId << "|" << lineText;
            std::string payload = oss.str();
            textMsg.data.assign(payload.begin(), payload.end());
            ipc::Bus::publish(kGuiChanIn, std::move(textMsg), false);
        }
        
        // Ensure cursor scrolls into view
        if (s_cursorLine < s_scrollOffset) {
            s_scrollOffset = s_cursorLine;
        } else if (s_cursorLine >= s_scrollOffset + visibleLines) {
            s_scrollOffset = s_cursorLine - visibleLines + 1;
        }
        
        // Draw context menu if visible
        drawContextMenu();
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
    
    void Notepad::rebuildToolbarButtons() {
        auto addButton = [](int id, int x, int y, int w, int h, const std::string& text) {
            ipc::Message msg;
            msg.type = (uint32_t)MsgType::MT_WidgetAdd;
            std::ostringstream oss;
            oss << s_windowId << "|1|" << id << "|" << x << "|" << y << "|" << w << "|" << h << "|" << text;
            std::string payload = oss.str();
            msg.data.assign(payload.begin(), payload.end());
            ipc::Bus::publish("gui.input", std::move(msg), false);
        };
        addButton(1, 4, 4, 60, 20, "New");
        addButton(2, 68, 4, 60, 20, "Open");
        addButton(3, 132, 4, 60, 20, "Save");
        addButton(4, 196, 4, 80, 20, "Save As");
        addButton(5, 280, 4, 64, 20, s_wrapText ? "Wrap" : "NoWrap");
        addButton(6, 348, 4, 60, 20, "Undo");
        addButton(7, 412, 4, 60, 20, "Redo");
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
    
    void Notepad::showContextMenu(int x, int y) {
        s_contextMenuVisible = true;
        s_contextMenuX = x;
        s_contextMenuY = y;
        s_contextMenuHoverIndex = -1;
        Logger::write(LogLevel::Info, std::string("Notepad: Context menu shown at ") + std::to_string(x) + "," + std::to_string(y));
    }
    
    void Notepad::hideContextMenu() {
        s_contextMenuVisible = false;
        s_contextMenuHoverIndex = -1;
    }
    
    bool Notepad::handleContextMenuClick(int mx, int my) {
        if (!s_contextMenuVisible) return false;
        
        // Context menu dimensions
        const int menuWidth = 140;
        const int itemHeight = 24;
        const char* menuItems[] = { "Cut", "Copy", "Paste", "Undo", "Redo", "Select All" };
        const int itemCount = 6;
        const int menuHeight = itemHeight * itemCount;
        
        // Check if click is within menu bounds
        if (mx >= s_contextMenuX && mx < s_contextMenuX + menuWidth &&
            my >= s_contextMenuY && my < s_contextMenuY + menuHeight) {
            
            // Determine which item was clicked
            int itemIndex = (my - s_contextMenuY) / itemHeight;
            if (itemIndex >= 0 && itemIndex < itemCount) {
                Logger::write(LogLevel::Info, std::string("Notepad: Context menu item clicked: ") + menuItems[itemIndex]);
                
                // Execute the corresponding action
                switch (itemIndex) {
                    case 0: // Cut
                        Logger::write(LogLevel::Info, "Notepad: Cut (not implemented)");
                        break;
                    case 1: // Copy
                        copy();
                        break;
                    case 2: // Paste
                        paste();
                        break;
                    case 3: // Undo
                        performUndo();
                        break;
                    case 4: // Redo
                        performRedo();
                        break;
                    case 5: // Select All
                        selectAll();
                        break;
                }
                
                hideContextMenu();
                redrawContent();
                return true;
            }
        }
        
        return false;
    }
    
    void Notepad::drawContextMenu() {
        if (!s_contextMenuVisible) return;
        
        const char* kGuiChanIn = "gui.input";
        const int menuWidth = 140;
        const int itemHeight = 24;
        const char* menuItems[] = { "Cut", "Copy", "Paste", "Undo", "Redo", "Select All" };
        const int itemCount = 6;
        
        // Draw menu background rectangle
        ipc::Message bgMsg;
        bgMsg.type = (uint32_t)MsgType::MT_DrawRect;
        std::ostringstream bgOss;
        bgOss << s_windowId << "|" << s_contextMenuX << "|" << s_contextMenuY << "|" << menuWidth << "|" << (itemHeight * itemCount) << "|80|80|90";
        std::string bgPayload = bgOss.str();
        bgMsg.data.assign(bgPayload.begin(), bgPayload.end());
        ipc::Bus::publish(kGuiChanIn, std::move(bgMsg), false);
        
        // Draw menu items
        for (int i = 0; i < itemCount; i++) {
            int itemY = s_contextMenuY + (i * itemHeight);
            
            // Draw item background (highlight on hover)
            if (i == s_contextMenuHoverIndex) {
                ipc::Message itemBgMsg;
                itemBgMsg.type = (uint32_t)MsgType::MT_DrawRect;
                std::ostringstream itemBgOss;
                itemBgOss << s_windowId << "|" << s_contextMenuX << "|" << itemY << "|" << menuWidth << "|" << itemHeight << "|100|120|140";
                std::string itemBgPayload = itemBgOss.str();
                itemBgMsg.data.assign(itemBgPayload.begin(), itemBgPayload.end());
                ipc::Bus::publish(kGuiChanIn, std::move(itemBgMsg), false);
            }
            
            // Draw item text
            ipc::Message itemTextMsg;
            itemTextMsg.type = (uint32_t)MsgType::MT_DrawText;
            std::ostringstream itemTextOss;
            itemTextOss << s_windowId << "|" << menuItems[i];
            std::string itemTextPayload = itemTextOss.str();
            itemTextMsg.data.assign(itemTextPayload.begin(), itemTextPayload.end());
            ipc::Bus::publish(kGuiChanIn, std::move(itemTextMsg), false);
        }
    }
    
}} // namespace gxos::apps
