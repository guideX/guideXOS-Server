//
// guideXOS Kernel GUI Apps
//
// Sample GUI applications that can run in bare-metal/UEFI mode
// without the user-mode server.
//
// Copyright (c) 2026 guideXOS Server
//

#ifndef KERNEL_KERNEL_APPS_H
#define KERNEL_KERNEL_APPS_H

#include "kernel/kernel_app.h"
#include "kernel/vfs.h"

namespace kernel {
namespace apps {

// ============================================================
// Notepad App
// ============================================================

class NotepadApp : public app::KernelApp {
public:
    NotepadApp();
    virtual ~NotepadApp() override;
    
    virtual bool init() override;
    virtual bool initWithParam(const char* filePath) override;  // Load file on init
    virtual void shutdown() override;
    virtual void draw(uint32_t x, uint32_t y, uint32_t w, uint32_t h) override;
    
    virtual void onKeyChar(char c) override;
    virtual void onKeyDown(uint32_t key) override;
    virtual void onMouseDown(int x, int y, uint8_t button) override;
    virtual void onMouseUp(int x, int y, uint8_t button) override;
    
    static app::KernelApp* create() { return new NotepadApp(); }
    
private:
    static const int MAX_TEXT_LENGTH = 8192;
    static const int MAX_LINES = 200;
    static const int MAX_PATH_LEN = 256;
    static const int MAX_SAVE_ENTRIES = 32;
    static const int MAX_SAVE_FILENAME = 64;
    static const int MENU_BAR_HEIGHT = 20;
    static const int CONTEXT_MENU_WIDTH = 120;
    static const int CONTEXT_MENU_ITEM_HEIGHT = 20;

    struct SaveDialogEntry {
        char name[vfs::VFS_MAX_FILENAME];
        bool isDir;
        bool isDrive;
        bool isFile;
    };
    
    char m_text[MAX_TEXT_LENGTH];
    char m_filePath[MAX_PATH_LEN];
    int m_textLength;
    int m_cursorPos;
    int m_scrollY;
    bool m_selectAll;
    bool m_modified;
    bool m_ctrlPressed;
    
    // Menu state
    bool m_showFileMenu;
    bool m_showEditMenu;
    bool m_showContextMenu;
    bool m_showSaveDialog;
    bool m_saveDialogIsOpenMode;
    bool m_saveDialogShowingDrives;
    bool m_saveDialogFilenameFocused;
    int m_contextMenuX;
    int m_contextMenuY;
    int m_hoveredMenuItem;
    char m_saveDialogPath[MAX_PATH_LEN];
    char m_saveDialogFilename[MAX_SAVE_FILENAME];
    char m_saveDialogStatus[96];
    SaveDialogEntry m_saveEntries[MAX_SAVE_ENTRIES];
    int m_saveEntryCount;
    int m_saveSelected;
    int m_saveScroll;
    
    // Clipboard
    static char s_clipboard[MAX_TEXT_LENGTH];
    static int s_clipboardLength;
    int m_selectionStart;
    int m_selectionEnd;
    
    // File operations
    bool loadFile(const char* path);
    bool saveFile();
    bool saveFileAs(const char* path);
    void newFile();
    void updateTitle();
    void openSaveAsDialog();
    void openOpenFileDialog();
    void refreshSaveDialog();
    void drawSaveAsDialog(uint32_t x, uint32_t y, uint32_t w, uint32_t h);
    bool handleSaveDialogClick(int x, int y);
    void navigateSaveDialog(const char* path);
    void saveDialogGoUp();
    bool saveToDialogTarget();
    void buildSavePath(char* out, int outSize) const;
    void handleSaveDialogKey(uint32_t key);
    void handleSaveDialogChar(char c);
    
    // Text operations
    void insertChar(char c);
    void deleteChar();
    void backspace();
    void clearText();
    void moveCursor(int delta);
    void selectAll();
    void cut();
    void copy();
    void paste();
    
    // UI
    void drawMenuBar(uint32_t x, uint32_t y, uint32_t w);
    void drawFileMenu(uint32_t x, uint32_t y);
    void drawEditMenu(uint32_t x, uint32_t y);
    void drawContextMenu(uint32_t x, uint32_t y);
    bool handleMenuClick(int x, int y);
    bool handleContextMenuClick(int x, int y);
    int getLineCount() const;
    int getLineStart(int lineIndex) const;
};

// ============================================================
// Calculator App
// ============================================================

class CalculatorApp : public app::KernelApp {
public:
    CalculatorApp();
    virtual ~CalculatorApp() override;
    
    virtual bool init() override;
    virtual void shutdown() override;
    virtual void draw(uint32_t x, uint32_t y, uint32_t w, uint32_t h) override;
    
    virtual void onWidgetClick(int widgetId) override;
    virtual void onKeyChar(char c) override;
    
    static app::KernelApp* create() { return new CalculatorApp(); }
    
private:
    double m_accumulator;
    double m_operand;
    char m_operation;
    bool m_newNumber;
    char m_display[32];
    
    // Widget IDs
    int m_displayId;
    int m_btnIds[20];  // 0-9, +, -, *, /, =, C, CE, ., +/-, %
    
    void handleButton(char btn);
    void updateDisplay();
    void calculate();
    void clear();
    void clearEntry();
};

// ============================================================
// Task Manager App
// ============================================================

class TaskManagerApp : public app::KernelApp {
public:
    TaskManagerApp();
    virtual ~TaskManagerApp() override;
    
    virtual bool init() override;
    virtual void shutdown() override;
    virtual void update() override;
    virtual void draw(uint32_t x, uint32_t y, uint32_t w, uint32_t h) override;
    
    virtual void onMouseDown(int x, int y, uint8_t button) override;
    virtual void onWidgetClick(int widgetId) override;
    
    static app::KernelApp* create() { return new TaskManagerApp(); }
    
private:
    int m_selectedApp;
    int m_refreshBtnId;
    int m_endTaskBtnId;
    uint32_t m_lastUpdate;
    
    struct AppEntry {
        char name[app::MAX_APP_NAME];
        int windowCount;
        bool running;
    };
    static const int MAX_ENTRIES = 16;
    AppEntry m_entries[MAX_ENTRIES];
    int m_entryCount;
    
    void refreshList();
};

// ============================================================
// File Explorer App
// ============================================================

class FileExplorerApp : public app::KernelApp {
public:
    FileExplorerApp();
    virtual ~FileExplorerApp() override;

    virtual bool init() override;
    virtual bool initWithParam(const char* startPath) override;
    virtual void shutdown() override;
    virtual void draw(uint32_t x, uint32_t y, uint32_t w, uint32_t h) override;

    virtual void onKeyDown(uint32_t key) override;
    virtual void onMouseDown(int x, int y, uint8_t button) override;
    virtual void onWidgetClick(int widgetId) override;

    static app::KernelApp* create() { return new FileExplorerApp(); }

private:
    static const int MAX_PATH_LEN = 256;
    static const int MAX_ENTRIES = 128;
    static const int TOOLBAR_H = 30;
    static const int ADDRESS_H = 22;
    static const int LEFT_W = 150;
    static const int ROW_H = 16;

    struct Entry {
        char name[vfs::VFS_MAX_FILENAME];
        bool isDir;
        uint64_t size;
    };

    char m_currentPath[MAX_PATH_LEN];
    char m_status[96];
    Entry m_entries[MAX_ENTRIES];
    int m_entryCount;
    int m_selected;
    int m_scroll;
    int m_lastClickIndex;
    uint64_t m_lastClickTick;
    int m_backBtnId;
    int m_upBtnId;
    int m_refreshBtnId;
    int m_rootBtnId;

    void refresh();
    void navigate(const char* path);
    void openSelected();
    void goUp();
    void setStatus(const char* status);
    bool isTextFile(const char* name) const;
    void joinPath(const char* base, const char* name, char* out, int outSize) const;
    void parentPath(const char* path, char* out, int outSize) const;
    void formatSize(uint64_t size, char* out, int outSize) const;
    const char* fileType(const Entry& entry) const;
};

// ============================================================
// App Registration
// ============================================================

// Call this once to register all kernel GUI apps
void registerKernelApps();

} // namespace apps
} // namespace kernel

#endif // KERNEL_KERNEL_APPS_H
