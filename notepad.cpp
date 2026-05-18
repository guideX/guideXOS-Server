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

    namespace {
        constexpr int kEditorX = 8;
        constexpr int kEditorY = 32;
        constexpr int kEditorWidth = 624;
        constexpr int kEditorHeight = 384;
        constexpr int kEditorCharWidth = 6;
        constexpr int kEditorLineHeight = 16;
        constexpr int kEditorVisibleLines = kEditorHeight / kEditorLineHeight;
        constexpr int kEditorMaxDisplayCols = 100;
        constexpr int kStatusY = kEditorY + kEditorHeight + 8;

        void publishDrawTextAt(uint64_t windowId, int x, int y, const std::string& text) {
            ipc::Message msg;
            msg.type = (uint32_t)MsgType::MT_DrawTextAt;
            std::ostringstream oss;
            oss << windowId << "|" << x << "|" << y << "|" << text;
            std::string payload = oss.str();
            msg.data.assign(payload.begin(), payload.end());
            ipc::Bus::publish("gui.input", std::move(msg), false);
        }

        void publishDrawRect(uint64_t windowId, int x, int y, int w, int h, int r, int g, int b) {
            ipc::Message msg;
            msg.type = (uint32_t)MsgType::MT_DrawRect;
            std::ostringstream oss;
            oss << windowId << "|" << x << "|" << y << "|" << w << "|" << h << "|" << r << "|" << g << "|" << b;
            std::string payload = oss.str();
            msg.data.assign(payload.begin(), payload.end());
            ipc::Bus::publish("gui.input", std::move(msg), false);
        }
    }
    
    // Static member initialization
    uint64_t Notepad::s_windowId = 0;
    std::string Notepad::s_filePath = "";
    std::vector<std::string> Notepad::s_lines;
    int Notepad::s_cursorLine = 0;
    int Notepad::s_cursorCol = 0;
    int Notepad::s_selectionAnchorIndex = 0;
    int Notepad::s_selectionActiveEndIndex = 0;
    bool Notepad::s_mouseSelecting = false;
    bool Notepad::s_modified = false;
    int Notepad::s_scrollOffset = 0;
    bool Notepad::s_wrapText = true;
    bool Notepad::s_shiftPressed = false;
    bool Notepad::s_ctrlPressed = false;
    bool Notepad::s_capsLockOn = false;
    int Notepad::s_lastKeyCode = 0;
    bool Notepad::s_keyDown = false;
    bool Notepad::s_pendingClose = false;
    int Notepad::s_pendingModalLaunches = 0;
    std::vector<uint64_t> Notepad::s_modalDialogWindowIds;
    bool Notepad::s_contextMenuVisible = false;
    int Notepad::s_contextMenuX = 0;
    int Notepad::s_contextMenuY = 0;
    int Notepad::s_contextMenuHoverIndex = -1;
    bool Notepad::s_fileMenuVisible = false;
    int Notepad::s_fileMenuX = 4;
    int Notepad::s_fileMenuY = 28;
    int Notepad::s_fileMenuHoverIndex = -1;
    bool Notepad::s_editMenuVisible = false;
    int Notepad::s_editMenuX = 56;
    int Notepad::s_editMenuY = 28;
    int Notepad::s_editMenuHoverIndex = -1;
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
            s_selectionAnchorIndex = 0;
            s_selectionActiveEndIndex = 0;
            s_mouseSelecting = false;
            s_modified = false;
            s_scrollOffset = 0;
            s_wrapText = true;
            s_shiftPressed = false;
            s_ctrlPressed = false;
            s_capsLockOn = false;
            s_undoStack.clear();
            s_redoStack.clear();
            s_pendingModalLaunches = 0;
            s_modalDialogWindowIds.clear();
            s_fileMenuVisible = false;
            s_fileMenuX = 4;
            s_fileMenuY = 28;
            s_fileMenuHoverIndex = -1;
            s_editMenuVisible = false;
            s_editMenuX = 56;
            s_editMenuY = 28;
            s_editMenuHoverIndex = -1;

            runSelectionSelfTest();
            
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
                                    uint64_t createdId = std::stoull(idStr);

                                    if (s_windowId == 0) {
                                        s_windowId = createdId;
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
                                        addButton(1, 4, 4, 48, 20, "File");
                                        addButton(8, 56, 4, 48, 20, "Edit");
                                        addButton(2, 108, 4, 84, 20, "New File");
                                        addButton(3, 196, 4, 60, 20, "Save");
                                        addButton(4, 260, 4, 80, 20, "Save As");
                                        addButton(5, 344, 4, 64, 20, s_wrapText ? "Wrap" : "NoWrap");
                                        addButton(6, 412, 4, 60, 20, "Undo");
                                        addButton(7, 476, 4, 60, 20, "Redo");

                                        // Draw initial content
                                        redrawContent();
                                        updateStatusBar();
                                    } else if (s_pendingModalLaunches > 0 && createdId != s_windowId) {
                                        s_modalDialogWindowIds.push_back(createdId);
                                        s_pendingModalLaunches--;
                                        s_keyDown = false;
                                        s_lastKeyCode = 0;
                                        Logger::write(LogLevel::Info, std::string("Notepad: Registered modal dialog window ") + std::to_string(createdId));
                                    }
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
                                    } else {
                                        auto it = std::find(s_modalDialogWindowIds.begin(), s_modalDialogWindowIds.end(), closedId);
                                        if (it != s_modalDialogWindowIds.end()) {
                                            s_modalDialogWindowIds.erase(it);
                                            s_keyDown = false;
                                            s_lastKeyCode = 0;
                                            Logger::write(LogLevel::Info, std::string("Notepad: Modal dialog closed: ") + std::to_string(closedId));
                                        }
                                    }
                                } catch (const std::exception& e) {
                                    Logger::write(LogLevel::Error, std::string("Notepad: Failed to parse close ID: ") + e.what());
                                }
                            }
                            break;
                        }
                        
                        case MsgType::MT_InputKey: {
                            if (s_pendingModalLaunches > 0 || !s_modalDialogWindowIds.empty()) {
                                break;
                            }

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
                                            clearSelection();
                                            redrawContent();
                                            updateStatusBar();
                                        }
                                        // End - end of line
                                        else if (keyCode == 35) {
                                            if (s_cursorLine < (int)s_lines.size()) {
                                                s_cursorCol = (int)s_lines[s_cursorLine].size();
                                                clearSelection();
                                                redrawContent();
                                                updateStatusBar();
                                            }
                                        }
                                        // Tab key - insert 4 spaces
                                        else if (keyCode == 9) {
                                            insertText("    ");
                                        }
                                        // Backspace
                                        else if (keyCode == 8) {
                                            if (hasSelection()) {
                                                deleteSelection();
                                            } else if (s_cursorCol > 0 && s_cursorLine < (int)s_lines.size()) {
                                                pushUndo();
                                                std::string temp = s_lines[s_cursorLine];
                                                temp.erase(s_cursorCol - 1, 1);
                                                s_lines[s_cursorLine] = temp;
                                                s_cursorCol--;
                                                clearSelection();
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
                                                clearSelection();
                                                s_modified = true;
                                                redrawContent();
                                                updateStatusBar();
                                                updateTitle();
                                            }
                                        }
                                        // Enter (VK_RETURN=13, also accept '\n'=10)
                                        else if (keyCode == 13 || keyCode == 10) {
                                            insertText("\n");
                                        }
                                        // Delete key (forward delete, VK_DELETE=46)
                                        else if (keyCode == 46) {
                                            deleteChar();
                                        }
                                        // Handle printable characters with shift support
                                        else if (keyCode >= 32 && keyCode <= 126) {
                                            char ch = mapKeyToChar(keyCode);
                                            if (ch != '\0') {
                                                insertText(std::string(1, ch));
                                            }
                                        }
                                        // Arrow keys
                                        else if (keyCode == 37) { // Left
                                            if (s_cursorCol > 0) {
                                                s_cursorCol--;
                                                clearSelection();
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
                                                clearSelection();
                                                redrawContent();
                                                updateStatusBar();
                                            }
                                        }
                                        else if (keyCode == 39) { // Right
                                            if (s_cursorLine < (int)s_lines.size() && s_cursorCol < (int)s_lines[s_cursorLine].size()) {
                                                s_cursorCol++;
                                                clearSelection();
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
                                                clearSelection();
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
                            if (s_pendingModalLaunches > 0 || !s_modalDialogWindowIds.empty()) {
                                break;
                            }

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
                                            case 1: toggleFileMenu(); break;
                                            case 2: newFile(); break;
                                            case 3: saveFile(); break;
                                            case 4: saveFileAs(); break;
                                            case 5: toggleWrap(); break;
                                            case 6: performUndo(); break;
                                            case 7: performRedo(); break;
                                            case 8: toggleEditMenu(); break;
                                        }
                                    }
                                } catch (const std::exception& e) {
                                    Logger::write(LogLevel::Error, std::string("Notepad: Failed to parse widget event: ") + e.what());
                                }
                            }
                            break;
                        }
                        
                        case MsgType::MT_InputMouse: {
                            if (s_pendingModalLaunches > 0 || !s_modalDialogWindowIds.empty()) {
                                break;
                            }

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
                                    
                                    if (button == 0 && action == "move") {
                                        if (s_mouseSelecting) {
                                            updateMouseSelection(mx, my);
                                        } else if (updateMenuHover(mx, my)) {
                                            redrawContent();
                                        }
                                    }
                                    else if (button == 1 && action == "up") {
                                        if (s_mouseSelecting) {
                                            endMouseSelection(mx, my);
                                        }
                                    }
                                    // Button 2 = right click
                                    else if (button == 2 && action == "down") {
                                        // Show context menu at mouse position
                                        showContextMenu(mx, my);
                                        redrawContent();
                                    }
                                    // Left click dismisses context menu
                                    else if (button == 1 && action == "down") {
                                        if (s_fileMenuVisible) {
                                            if (!handleFileMenuClick(mx, my)) {
                                                hideFileMenu();
                                                redrawContent();
                                            }
                                        } else if (s_editMenuVisible) {
                                            if (!handleEditMenuClick(mx, my)) {
                                                hideEditMenu();
                                                redrawContent();
                                            }
                                        } else if (s_contextMenuVisible) {
                                            if (!handleContextMenuClick(mx, my)) {
                                                hideContextMenu();
                                                redrawContent();
                                            }
                                        } else if (isPointInTextArea(mx, my)) {
                                            beginMouseSelection(mx, my);
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

    int Notepad::documentLength() {
        if (s_lines.empty()) return 0;
        int length = 0;
        for (size_t i = 0; i < s_lines.size(); ++i) {
            length += (int)s_lines[i].size();
            if (i + 1 < s_lines.size()) length += 1;
        }
        return length;
    }

    int Notepad::lineColToTextIndex(int line, int col) {
        if (s_lines.empty()) return 0;
        if (line < 0) line = 0;
        if (line >= (int)s_lines.size()) line = (int)s_lines.size() - 1;
        col = std::max(0, std::min(col, (int)s_lines[line].size()));

        int index = 0;
        for (int i = 0; i < line; ++i) {
            index += (int)s_lines[i].size() + 1;
        }
        return index + col;
    }

    int Notepad::cursorTextIndex() {
        return lineColToTextIndex(s_cursorLine, s_cursorCol);
    }

    void Notepad::textIndexToLineCol(int index, int& line, int& col) {
        if (s_lines.empty()) {
            line = 0;
            col = 0;
            return;
        }

        index = std::max(0, std::min(index, documentLength()));
        int running = 0;
        for (int i = 0; i < (int)s_lines.size(); ++i) {
            int lineLen = (int)s_lines[i].size();
            int lineEnd = running + lineLen;
            if (index <= lineEnd || i == (int)s_lines.size() - 1) {
                line = i;
                col = std::max(0, std::min(index - running, lineLen));
                return;
            }
            running = lineEnd + 1;
        }

        line = (int)s_lines.size() - 1;
        col = (int)s_lines.back().size();
    }

    void Notepad::setCursorFromTextIndex(int index) {
        textIndexToLineCol(index, s_cursorLine, s_cursorCol);
    }

    bool Notepad::hasSelection() {
        return s_selectionAnchorIndex != s_selectionActiveEndIndex;
    }

    int Notepad::getSelectionStart() {
        return std::min(s_selectionAnchorIndex, s_selectionActiveEndIndex);
    }

    int Notepad::getSelectionEnd() {
        return std::max(s_selectionAnchorIndex, s_selectionActiveEndIndex);
    }

    void Notepad::clearSelection() {
        int caret = cursorTextIndex();
        s_selectionAnchorIndex = caret;
        s_selectionActiveEndIndex = caret;
    }

    bool Notepad::isPointInTextArea(int x, int y) {
        return x >= kEditorX && x < kEditorX + kEditorWidth &&
               y >= kEditorY && y < kEditorY + kEditorHeight;
    }

    int Notepad::pointToTextIndex(int x, int y) {
        if (s_lines.empty()) return 0;

        int row = (y - kEditorY) / kEditorLineHeight;
        if (y < kEditorY) row = 0;
        if (row < 0) row = 0;
        if (row >= kEditorVisibleLines) row = kEditorVisibleLines - 1;

        int line = s_scrollOffset + row;
        if (line < 0) line = 0;
        if (line >= (int)s_lines.size()) line = (int)s_lines.size() - 1;

        int col = (x - kEditorX + (kEditorCharWidth / 2)) / kEditorCharWidth;
        if (x < kEditorX) col = 0;
        col = std::max(0, std::min(col, (int)s_lines[line].size()));
        return lineColToTextIndex(line, col);
    }

    void Notepad::beginMouseSelection(int x, int y) {
        int index = pointToTextIndex(x, y);
        s_mouseSelecting = true;
        s_selectionAnchorIndex = index;
        s_selectionActiveEndIndex = index;
        setCursorFromTextIndex(index);
        hideContextMenu();
        hideFileMenu();
        hideEditMenu();
        if (s_windowId != 0) {
            redrawContent();
        }
    }

    void Notepad::updateMouseSelection(int x, int y) {
        if (!s_mouseSelecting) return;
        int index = pointToTextIndex(x, y);
        s_selectionActiveEndIndex = index;
        setCursorFromTextIndex(index);
        if (s_windowId != 0) {
            redrawContent();
        }
    }

    void Notepad::endMouseSelection(int x, int y) {
        if (!s_mouseSelecting) return;
        updateMouseSelection(x, y);
        s_mouseSelecting = false;
        if (!hasSelection()) clearSelection();
        if (s_windowId != 0) {
            redrawContent();
        }
    }
    
    void Notepad::insertText(const std::string& text) {
        if (text.empty() || s_cursorLine >= (int)s_lines.size()) return;
        pushUndo();
        if (hasSelection()) {
            deleteSelectionWithoutUndo();
        }
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
        clearSelection();
        s_modified = true;
        redrawContent();
        updateStatusBar();
        updateTitle();
    }
    
    void Notepad::deleteChar() {
        if (s_cursorLine >= (int)s_lines.size()) return;
        if (hasSelection()) {
            deleteSelection();
            return;
        }
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
        if (!hasSelection()) return;
        pushUndo();
        deleteSelectionWithoutUndo();
        s_modified = true;
        if (s_windowId != 0) {
            redrawContent();
            updateTitle();
        }
    }

    void Notepad::deleteSelectionWithoutUndo() {
        if (!hasSelection() || s_lines.empty()) return;

        int start = getSelectionStart();
        int end = getSelectionEnd();
        int startLine = 0, startCol = 0, endLine = 0, endCol = 0;
        textIndexToLineCol(start, startLine, startCol);
        textIndexToLineCol(end, endLine, endCol);

        if (startLine == endLine) {
            s_lines[startLine].erase(startCol, endCol - startCol);
        } else {
            std::string merged = s_lines[startLine].substr(0, startCol) + s_lines[endLine].substr(endCol);
            s_lines[startLine] = merged;
            s_lines.erase(s_lines.begin() + startLine + 1, s_lines.begin() + endLine + 1);
        }

        if (s_lines.empty()) s_lines.push_back("");
        setCursorFromTextIndex(start);
        clearSelection();
    }
    
    void Notepad::copy() {
        // TODO: Wire this to a shared clipboard service when one exists.
        // The selected range is tracked in buffer offsets by getSelectionStart()/getSelectionEnd().
        Logger::write(LogLevel::Info, "Notepad: Copy (not implemented)");
    }
    
    void Notepad::paste() {
        Logger::write(LogLevel::Info, "Notepad: Paste (not implemented)");
    }
    
    void Notepad::selectAll() {
        int end = documentLength();
        s_selectionAnchorIndex = 0;
        s_selectionActiveEndIndex = end;
        setCursorFromTextIndex(end);
        Logger::write(LogLevel::Info, "Notepad: Select All");
        if (s_windowId != 0) {
            redrawContent();
        }
    }

    bool Notepad::runSelectionSelfTest() {
        std::vector<std::string> savedLines = s_lines;
        int savedCursorLine = s_cursorLine;
        int savedCursorCol = s_cursorCol;
        int savedScrollOffset = s_scrollOffset;
        int savedAnchor = s_selectionAnchorIndex;
        int savedActive = s_selectionActiveEndIndex;
        bool savedMouseSelecting = s_mouseSelecting;
        bool savedModified = s_modified;
        std::vector<TextSnapshot> savedUndo = s_undoStack;
        std::vector<TextSnapshot> savedRedo = s_redoStack;
        uint64_t savedWindowId = s_windowId;
        s_windowId = 0;

        bool ok = true;
        s_lines = {"abcd", "efgh"};
        s_cursorLine = 0;
        s_cursorCol = 0;
        clearSelection();

        selectAll();
        ok = ok && hasSelection() && getSelectionStart() == 0 && getSelectionEnd() == documentLength();
        ok = ok && s_cursorLine == 1 && s_cursorCol == 4;

        s_lines = {"abcd", "efgh"};
        s_cursorLine = 0;
        s_cursorCol = 0;
        s_selectionAnchorIndex = 1;
        s_selectionActiveEndIndex = 6;
        deleteSelectionWithoutUndo();
        ok = ok && s_lines.size() == 1 && s_lines[0] == "agh" && s_cursorLine == 0 && s_cursorCol == 1 && !hasSelection();

        s_lines = {"abcd", "efgh"};
        s_scrollOffset = 0;
        beginMouseSelection(kEditorX + kEditorCharWidth, kEditorY + 2);
        updateMouseSelection(kEditorX + (3 * kEditorCharWidth), kEditorY + 2);
        ok = ok && getSelectionStart() == 1 && getSelectionEnd() == 3;
        endMouseSelection(kEditorX + (3 * kEditorCharWidth), kEditorY + 2);

        beginMouseSelection(kEditorX + (3 * kEditorCharWidth), kEditorY + 2);
        updateMouseSelection(kEditorX + kEditorCharWidth, kEditorY + 2);
        ok = ok && getSelectionStart() == 1 && getSelectionEnd() == 3;
        endMouseSelection(kEditorX + kEditorCharWidth, kEditorY + 2);

        s_lines = savedLines;
        s_cursorLine = savedCursorLine;
        s_cursorCol = savedCursorCol;
        s_scrollOffset = savedScrollOffset;
        s_selectionAnchorIndex = savedAnchor;
        s_selectionActiveEndIndex = savedActive;
        s_mouseSelecting = savedMouseSelecting;
        s_modified = savedModified;
        s_undoStack = savedUndo;
        s_redoStack = savedRedo;
        s_windowId = savedWindowId;

        if (!ok) {
            Logger::write(LogLevel::Error, "Notepad: selection self-test failed");
        } else {
            Logger::write(LogLevel::Info, "Notepad: selection self-test passed");
        }
        return ok;
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
        clearSelection();
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
        clearSelection();
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
        clearSelection();
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
        s_pendingModalLaunches++;
        s_keyDown = false;
        s_lastKeyCode = 0;
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
        clearSelection();
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
        s_pendingModalLaunches++;
        s_keyDown = false;
        s_lastKeyCode = 0;
        
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
        
        SaveDialog::Show(ownerX, ownerY, "drives", fileName,
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
        s_pendingModalLaunches++;
        s_keyDown = false;
        s_lastKeyCode = 0;
        
        SaveChangesDialog::Show(ownerX, ownerY,
            []() {
                // Save clicked
                Logger::write(LogLevel::Info, "SaveChangesDialog: User chose Save");
                if (s_filePath.empty()) {
                    // Need to show SaveDialog first
                    s_pendingModalLaunches++;
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

        ipc::Message clearMsg;
        clearMsg.type = (uint32_t)MsgType::MT_DrawText;
        std::string clearPayload = std::to_string(s_windowId) + "|\f";
        clearMsg.data.assign(clearPayload.begin(), clearPayload.end());
        ipc::Bus::publish(kGuiChanIn, std::move(clearMsg), false);
        
        // Calculate visible window - support scrolling
        int visibleLines = kEditorVisibleLines;
        if (s_cursorLine < s_scrollOffset) {
            s_scrollOffset = s_cursorLine;
        } else if (s_cursorLine >= s_scrollOffset + visibleLines) {
            s_scrollOffset = s_cursorLine - visibleLines + 1;
        }
        if (s_scrollOffset < 0) s_scrollOffset = 0;

        int startLine = s_scrollOffset;
        int endLine = std::min((int)s_lines.size(), startLine + visibleLines);

        publishDrawRect(s_windowId, kEditorX - 2, kEditorY - 2, kEditorWidth + 4, kEditorHeight + 4, 36, 38, 44);
        publishDrawRect(s_windowId, kEditorX - 1, kEditorY - 1, kEditorWidth + 2, kEditorHeight + 2, 18, 20, 24);

        int selectionStart = getSelectionStart();
        int selectionEnd = getSelectionEnd();
        
        // Draw selection highlights first so text remains readable on top.
        for (int i = startLine; i < endLine; i++) {
            const std::string& sourceLine = s_lines[i];
            int lineStart = lineColToTextIndex(i, 0);
            int lineEnd = lineStart + (int)sourceLine.size();

            if (selectionStart != selectionEnd && selectionEnd >= lineStart && selectionStart <= lineEnd + 1) {
                int highlightStart = std::max(selectionStart, lineStart);
                int highlightEnd = std::min(selectionEnd, lineEnd);
                int startCol = std::max(0, highlightStart - lineStart);
                int endCol = std::max(startCol, highlightEnd - lineStart);
                bool includesLineBreak = (selectionEnd > lineEnd && selectionStart <= lineEnd && i + 1 < (int)s_lines.size());
                if (includesLineBreak && endCol < kEditorMaxDisplayCols) {
                    endCol++;
                }
                startCol = std::min(startCol, kEditorMaxDisplayCols);
                endCol = std::min(endCol, kEditorMaxDisplayCols);
                if (endCol > startCol) {
                    int hx = kEditorX + startCol * kEditorCharWidth;
                    int hy = kEditorY + (i - startLine) * kEditorLineHeight;
                    int hw = std::max(kEditorCharWidth, (endCol - startCol) * kEditorCharWidth);
                    publishDrawRect(s_windowId, hx, hy, hw, kEditorLineHeight, 42, 92, 160);
                }
            }
        }

        // Draw visible lines using positioned text so mouse hit-testing and highlights share one grid.
        for (int i = startLine; i < endLine; i++) {
            std::string displayLine = s_lines[i];
            if (s_wrapText && displayLine.length() > kEditorMaxDisplayCols) {
                displayLine = displayLine.substr(0, kEditorMaxDisplayCols);
            }
            int y = kEditorY + (i - startLine) * kEditorLineHeight + 3;
            publishDrawTextAt(s_windowId, kEditorX, y, displayLine);
        }

        if (s_cursorLine >= startLine && s_cursorLine < endLine) {
            int caretCol = std::min(s_cursorCol, kEditorMaxDisplayCols);
            int caretX = kEditorX + caretCol * kEditorCharWidth;
            int caretY = kEditorY + (s_cursorLine - startLine) * kEditorLineHeight + 3;
            publishDrawTextAt(s_windowId, caretX, caretY, "|");
        }

        std::ostringstream status;
        status << "Line " << (s_cursorLine + 1) << ", Col " << (s_cursorCol + 1);
        if (hasSelection()) status << "  Selection " << (getSelectionEnd() - getSelectionStart()) << " chars";
        if (s_modified) status << " (Modified)";
        if (s_capsLockOn) status << " [CAPS]";
        if (s_shiftPressed) status << " [SHIFT]";
        if (s_ctrlPressed) status << " [CTRL]";
        publishDrawRect(s_windowId, kEditorX - 2, kStatusY - 2, kEditorWidth + 4, kEditorLineHeight + 4, 48, 50, 58);
        publishDrawTextAt(s_windowId, kEditorX, kStatusY, status.str());
        
        // Draw menus if visible
        drawContextMenu();
        drawFileMenu();
        drawEditMenu();
    }
    
    void Notepad::updateStatusBar() {
        redrawContent();
    }
    
    void Notepad::toggleWrap() {
        s_wrapText = !s_wrapText;
        Logger::write(LogLevel::Info, std::string("Notepad: Text wrapping ") + (s_wrapText ? "enabled" : "disabled"));
        
        // Recreate button with updated text
        ipc::Message msg;
        msg.type = (uint32_t)MsgType::MT_WidgetAdd;
        std::ostringstream oss;
        oss << s_windowId << "|1|5|344|4|64|20|" << (s_wrapText ? "Wrap" : "NoWrap");
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
        addButton(1, 4, 4, 48, 20, "File");
        addButton(8, 56, 4, 48, 20, "Edit");
        addButton(2, 108, 4, 84, 20, "New File");
        addButton(3, 196, 4, 60, 20, "Save");
        addButton(4, 260, 4, 80, 20, "Save As");
        addButton(5, 344, 4, 64, 20, s_wrapText ? "Wrap" : "NoWrap");
        addButton(6, 412, 4, 60, 20, "Undo");
        addButton(7, 476, 4, 60, 20, "Redo");
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
        hideFileMenu();
        hideEditMenu();
        s_contextMenuX = x;
        s_contextMenuY = y;
        s_contextMenuHoverIndex = -1;
        Logger::write(LogLevel::Info, std::string("Notepad: Context menu shown at ") + std::to_string(x) + "," + std::to_string(y));
    }
    
    void Notepad::hideContextMenu() {
        s_contextMenuVisible = false;
        s_contextMenuHoverIndex = -1;
    }

    void Notepad::toggleFileMenu() {
        s_fileMenuVisible = !s_fileMenuVisible;
        s_fileMenuHoverIndex = -1;
        hideEditMenu();
        hideContextMenu();
        redrawContent();
    }

    void Notepad::hideFileMenu() {
        s_fileMenuVisible = false;
        s_fileMenuHoverIndex = -1;
    }

    void Notepad::toggleEditMenu() {
        s_editMenuVisible = !s_editMenuVisible;
        s_editMenuHoverIndex = -1;
        hideFileMenu();
        hideContextMenu();
        redrawContent();
    }

    void Notepad::hideEditMenu() {
        s_editMenuVisible = false;
        s_editMenuHoverIndex = -1;
    }

    bool Notepad::handleFileMenuClick(int mx, int my) {
        if (!s_fileMenuVisible) return false;

        const int menuWidth = 120;
        const int itemHeight = 24;
        const int itemCount = 5;
        const int menuHeight = itemHeight * itemCount;

        if (mx >= s_fileMenuX && mx < s_fileMenuX + menuWidth &&
            my >= s_fileMenuY && my < s_fileMenuY + menuHeight) {
            int itemIndex = (my - s_fileMenuY) / itemHeight;
            hideFileMenu();

            switch (itemIndex) {
                case 0:
                    newFile();
                    break;
                case 1:
                    openFileDialog();
                    break;
                case 2:
                    saveFile();
                    break;
                case 3:
                    saveFileAs();
                    break;
                case 4:
                    closeWithPrompt();
                    break;
                default:
                    return false;
            }

            redrawContent();
            return true;
        }

        return false;
    }

    bool Notepad::handleEditMenuClick(int mx, int my) {
        if (!s_editMenuVisible) return false;

        const int menuWidth = 140;
        const int itemHeight = 24;
        const int itemCount = 6;
        const int menuHeight = itemHeight * itemCount;

        if (mx >= s_editMenuX && mx < s_editMenuX + menuWidth &&
            my >= s_editMenuY && my < s_editMenuY + menuHeight) {
            int itemIndex = (my - s_editMenuY) / itemHeight;
            hideEditMenu();

            switch (itemIndex) {
                case 0:
                    // TODO: Copy getSelectionStart()/getSelectionEnd() to a shared clipboard before deleting.
                    Logger::write(LogLevel::Info, "Notepad: Cut (not implemented)");
                    break;
                case 1:
                    copy();
                    break;
                case 2:
                    paste();
                    break;
                case 3:
                    performUndo();
                    break;
                case 4:
                    performRedo();
                    break;
                case 5:
                    selectAll();
                    break;
                default:
                    return false;
            }

            redrawContent();
            return true;
        }

        return false;
    }

    bool Notepad::updateMenuHover(int mx, int my) {
        bool changed = false;

        if (s_fileMenuVisible) {
            const int menuWidth = 120;
            const int itemHeight = 24;
            const int itemCount = 5;
            int hoverIndex = -1;

            if (mx >= s_fileMenuX && mx < s_fileMenuX + menuWidth &&
                my >= s_fileMenuY && my < s_fileMenuY + (itemHeight * itemCount)) {
                hoverIndex = (my - s_fileMenuY) / itemHeight;
            }

            if (s_fileMenuHoverIndex != hoverIndex) {
                s_fileMenuHoverIndex = hoverIndex;
                changed = true;
            }
        }

        if (s_editMenuVisible) {
            const int menuWidth = 140;
            const int itemHeight = 24;
            const int itemCount = 6;
            int hoverIndex = -1;

            if (mx >= s_editMenuX && mx < s_editMenuX + menuWidth &&
                my >= s_editMenuY && my < s_editMenuY + (itemHeight * itemCount)) {
                hoverIndex = (my - s_editMenuY) / itemHeight;
            }

            if (s_editMenuHoverIndex != hoverIndex) {
                s_editMenuHoverIndex = hoverIndex;
                changed = true;
            }
        }

        if (s_contextMenuVisible) {
            const int menuWidth = 140;
            const int itemHeight = 24;
            const int itemCount = 6;
            int hoverIndex = -1;

            if (mx >= s_contextMenuX && mx < s_contextMenuX + menuWidth &&
                my >= s_contextMenuY && my < s_contextMenuY + (itemHeight * itemCount)) {
                hoverIndex = (my - s_contextMenuY) / itemHeight;
            }

            if (s_contextMenuHoverIndex != hoverIndex) {
                s_contextMenuHoverIndex = hoverIndex;
                changed = true;
            }
        }

        return changed;
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
                        // TODO: Copy getSelectionStart()/getSelectionEnd() to a shared clipboard before deleting.
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
            
            publishDrawTextAt(s_windowId, s_contextMenuX + 8, itemY + 7, menuItems[i]);
        }
    }

    void Notepad::drawFileMenu() {
        if (!s_fileMenuVisible) return;

        const char* kGuiChanIn = "gui.input";
        const int menuWidth = 120;
        const int itemHeight = 24;
        const char* menuItems[] = { "New", "Open", "Save", "Save As", "Exit" };
        const int itemCount = 5;

        ipc::Message bgMsg;
        bgMsg.type = (uint32_t)MsgType::MT_DrawRect;
        std::ostringstream bgOss;
        bgOss << s_windowId << "|" << s_fileMenuX << "|" << s_fileMenuY << "|" << menuWidth << "|" << (itemHeight * itemCount) << "|80|80|90";
        std::string bgPayload = bgOss.str();
        bgMsg.data.assign(bgPayload.begin(), bgPayload.end());
        ipc::Bus::publish(kGuiChanIn, std::move(bgMsg), false);

        for (int i = 0; i < itemCount; i++) {
            int itemY = s_fileMenuY + (i * itemHeight);

            if (i == s_fileMenuHoverIndex) {
                ipc::Message itemBgMsg;
                itemBgMsg.type = (uint32_t)MsgType::MT_DrawRect;
                std::ostringstream itemBgOss;
                itemBgOss << s_windowId << "|" << s_fileMenuX << "|" << itemY << "|" << menuWidth << "|" << itemHeight << "|100|120|140";
                std::string itemBgPayload = itemBgOss.str();
                itemBgMsg.data.assign(itemBgPayload.begin(), itemBgPayload.end());
                ipc::Bus::publish(kGuiChanIn, std::move(itemBgMsg), false);
            }

            publishDrawTextAt(s_windowId, s_fileMenuX + 8, itemY + 7, menuItems[i]);
        }
    }

    void Notepad::drawEditMenu() {
        if (!s_editMenuVisible) return;

        const char* kGuiChanIn = "gui.input";
        const int menuWidth = 140;
        const int itemHeight = 24;
        const char* menuItems[] = { "Cut", "Copy", "Paste", "Undo", "Redo", "Select All" };
        const int itemCount = 6;

        ipc::Message bgMsg;
        bgMsg.type = (uint32_t)MsgType::MT_DrawRect;
        std::ostringstream bgOss;
        bgOss << s_windowId << "|" << s_editMenuX << "|" << s_editMenuY << "|" << menuWidth << "|" << (itemHeight * itemCount) << "|80|80|90";
        std::string bgPayload = bgOss.str();
        bgMsg.data.assign(bgPayload.begin(), bgPayload.end());
        ipc::Bus::publish(kGuiChanIn, std::move(bgMsg), false);

        for (int i = 0; i < itemCount; i++) {
            int itemY = s_editMenuY + (i * itemHeight);

            if (i == s_editMenuHoverIndex) {
                ipc::Message itemBgMsg;
                itemBgMsg.type = (uint32_t)MsgType::MT_DrawRect;
                std::ostringstream itemBgOss;
                itemBgOss << s_windowId << "|" << s_editMenuX << "|" << itemY << "|" << menuWidth << "|" << itemHeight << "|100|120|140";
                std::string itemBgPayload = itemBgOss.str();
                itemBgMsg.data.assign(itemBgPayload.begin(), itemBgPayload.end());
                ipc::Bus::publish(kGuiChanIn, std::move(itemBgMsg), false);
            }

            publishDrawTextAt(s_windowId, s_editMenuX + 8, itemY + 7, menuItems[i]);
        }
    }
    
}} // namespace gxos::apps
