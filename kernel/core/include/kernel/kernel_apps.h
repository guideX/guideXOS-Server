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
#include "kernel/block_device.h"

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
    virtual void onMouseMove(int x, int y) override;
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
    int m_hoveredMenuType;
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
    bool updateMenuHover(int x, int y);
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
// Display Options App
// ============================================================

class DisplayOptionsApp : public app::KernelApp {
public:
    DisplayOptionsApp();
    virtual ~DisplayOptionsApp() override;

    virtual bool init() override;
    virtual void shutdown() override;
    virtual void draw(uint32_t x, uint32_t y, uint32_t w, uint32_t h) override;
    virtual void onMouseDown(int x, int y, uint8_t button) override;
    virtual void onWidgetClick(int widgetId) override;

    static app::KernelApp* create() { return new DisplayOptionsApp(); }

private:
    int m_selectedIndex;
    int m_appliedIndex;
    int m_selectedBackgroundIndex;
    int m_appliedBackgroundIndex;
    int m_selectedGradientIndex;
    int m_appliedGradientIndex;
    int m_activeTab;
    int m_selectButtonId;

    void loadSelection();
    void applySelected();
    int hitBackground(int x, int y) const;
    int hitWallpaper(int x, int y) const;
    int hitGradient(int x, int y) const;
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
    virtual void onKeyChar(char c) override;
    virtual void onMouseMove(int x, int y) override;
    virtual void onMouseDown(int x, int y, uint8_t button) override;
    virtual void onWidgetClick(int widgetId) override;

    static app::KernelApp* create() { return new FileExplorerApp(); }
    static bool drawThemedIcon(uint32_t x, uint32_t y, uint32_t size, const char* logicalName);
    static void drawPlaceholderIcon(uint32_t x, uint32_t y, uint32_t size);

private:
    static const int MAX_PATH_LEN = 256;
    static const int MAX_ENTRIES = 128;
    static const int TOOLBAR_H = 30;
    static const int ADDRESS_H = 22;
    static const int LEFT_W = 150;
    static const int ROW_H = 16;
    static const int CONTEXT_MENU_W = 120;
    static const int CONTEXT_MENU_ITEM_H = 20;

    struct Entry {
        char name[vfs::VFS_MAX_FILENAME];
        bool isDir;
        uint64_t size;
    };

    enum class ClipboardOperation {
        None,
        Copy,
        Move,
    };

    struct ClipboardState {
        char sourcePath[MAX_PATH_LEN];
        char sourceName[vfs::VFS_MAX_FILENAME];
        char sourceMount[64];
        bool sourceIsDir;
        ClipboardOperation operation;
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
    int m_renameFileBtnId;
    int m_deleteFileBtnId;
    int m_renameFolderBtnId;
    int m_deleteFolderBtnId;
    int m_confirmDeleteBtnId;
    int m_cancelDeleteBtnId;
    bool m_renamePrompt;
    bool m_deleteConfirm;
    bool m_deleteTargetIsDir;
    char m_renameValue[vfs::VFS_MAX_FILENAME];
    char m_deleteTarget[MAX_PATH_LEN];
    char m_deleteTargetName[vfs::VFS_MAX_FILENAME];
    ClipboardState m_clipboard;
    bool m_contextMenuOpen;
    int m_contextMenuX;
    int m_contextMenuY;
    int m_contextMenuHover;
    bool m_propertiesOpen;
    bool m_propertiesIsDir;
    char m_propertiesName[vfs::VFS_MAX_FILENAME];
    char m_propertiesPath[MAX_PATH_LEN];
    char m_propertiesType[32];
    char m_propertiesSize[24];
    char m_propertiesModified[24];
    char m_propertiesIcon[32];

    void refresh();
    void navigate(const char* path);
    void openSelected();
    void goUp();
    void updateActionButtons();
    void beginRenameSelected();
    void commitRename();
    void cancelRename();
    void showDeleteConfirmation();
    void confirmDelete();
    void cancelDelete();
    void showPropertiesForSelected();
    void closeProperties();
    void beginCopySelected();
    void beginMoveSelected();
    void pasteClipboard();
    bool copyFileContents(const char* sourcePath, const char* destPath);
    int hitTestContextMenu(int x, int y) const;
    bool handleContextMenuClick(int x, int y);
    int hitTestEntryRow(int x, int y) const;
    void closeTransientUi();
    bool launchApplicationLikeFile(const char* fullPath, const Entry& entry);
    bool openDiskImage(const char* fullPath, const Entry& entry);
    void setStatus(const char* status);
    bool isTextFile(const char* name) const;
    void joinPath(const char* base, const char* name, char* out, int outSize) const;
    void parentPath(const char* path, char* out, int outSize) const;
    void formatSize(uint64_t size, char* out, int outSize) const;
    const char* fileType(const Entry& entry) const;
    const char* fileLogicalIcon(const Entry& entry) const;
    static bool textEquals(const char* a, const char* b);
    static bool endsWithIgnoreCase(const char* value, const char* suffix);
    static const uint32_t* getEmbeddedIconPixels(const char* logicalName);
    static bool drawArgbIconBuffer(const uint32_t* pixels, uint32_t srcW, uint32_t srcH, uint32_t x, uint32_t y, uint32_t width, uint32_t height);
};

class NavigatorApp : public app::KernelApp {
public:
    NavigatorApp();
    virtual ~NavigatorApp() override;

    virtual bool init() override;
    virtual void shutdown() override;
    virtual void draw(uint32_t x, uint32_t y, uint32_t w, uint32_t h) override;
    virtual void onMouseMove(int x, int y) override;
    virtual void onMouseDown(int x, int y, uint8_t button) override;
    virtual void onWidgetClick(int widgetId) override;
    virtual void onKeyDown(uint32_t key) override;
    virtual void onKeyChar(char c) override;

    static app::KernelApp* create() { return new NavigatorApp(); }

private:
    static const int MAX_STATUS_LEN = 128;
    static const int MAX_URL_LEN = 160;
    static const int MAX_TITLE_LEN_NAV = 96;
    static const int MAX_BLOCKS = 40;
    static const int MAX_BLOCK_TEXT = 320;
    static const int MAX_BOOKMARKS = 12;
    static const int TOOLBAR_H = 48;
    static const int STATUS_H = 24;
    static const int BUTTON_W = 64;
    static const int BUTTON_H = 22;
    static const int BUTTON_GAP = 6;
    static const int CONTENT_X = 16;
    static const int CONTENT_Y = 62;
    static const int ADDRESS_X = 452;
    static const int ADDRESS_Y = 12;
    static const int ADDRESS_H = 22;

    enum BlockKind {
        BLOCK_HEADING = 0,
        BLOCK_PARAGRAPH,
        BLOCK_LINK,
        BLOCK_LIST_ITEM,
        BLOCK_PREFORMATTED,
        BLOCK_IMAGE
    };

    struct DocBlock {
        BlockKind kind;
        char text[MAX_BLOCK_TEXT];
        char url[MAX_URL_LEN];
        char src[MAX_URL_LEN];
        char alt[96];
        int width;
        int height;
    };

    struct Bookmark {
        char title[64];
        char url[MAX_URL_LEN];
    };

    char m_status[MAX_STATUS_LEN];
    char m_currentUrl[MAX_URL_LEN];
    char m_title[MAX_TITLE_LEN_NAV];
    DocBlock m_blocks[MAX_BLOCKS];
    int m_blockCount;
    Bookmark m_bookmarks[MAX_BOOKMARKS];
    int m_bookmarkCount;
    char m_backStack[12][MAX_URL_LEN];
    int m_backCount;
    char m_forwardStack[12][MAX_URL_LEN];
    int m_forwardCount;
    bool m_addressFocused;
    char m_addressBuffer[MAX_URL_LEN];
    int m_addressCaret;
    int m_scrollY;
    int m_hoverLinkIndex;
    int m_backBtnId;
    int m_forwardBtnId;
    int m_reloadBtnId;
    int m_homeBtnId;
    int m_bookmarksBtnId;
    int m_addBookmarkBtnId;

    void setStatus(const char* text);
    void updateButtons();
    void loadUrl(const char* url);
    void navigateTo(const char* url);
    void goBack();
    void goForward();
    void buildAboutNavigatorDocument();
    void buildBookmarksDocument();
    void buildErrorDocument(const char* url, const char* reason);
    void loadFileUrl(const char* url);
    void addBlock(BlockKind kind, const char* text, const char* url = "");
    void addImageBlock(const char* src, const char* alt, const char* resolvedUrl, int width, int height);
    void addBookmark(const char* title, const char* url);
    void loadDefaultBookmarks();
    void drawDocument(uint32_t x, uint32_t y, uint32_t w, uint32_t h);
    void drawWrappedText(uint32_t x, uint32_t y, const char* text, uint32_t color, int maxChars, int& outY) const;
    int blockHeight(const DocBlock& block, int maxChars) const;
    int blockY(int index, int maxChars) const;
    int hitLinkIndex(int x, int y) const;
    bool hitAddressBar(int x, int y) const;
    void focusAddressBar();
    void blurAddressBar();
    void commitAddressBar();
    void normalizeUrl(const char* input, char* out, int outSize) const;
    void parseHtmlDocument(const char* url, const char* html);
    void resolveHref(const char* baseUrl, const char* href, char* out, int outSize) const;
    int maxScroll() const;
    void clampScroll();
};

// ============================================================
// Disk Manager App (baremetal)
// ============================================================

class DiskManagerApp : public app::KernelApp {
public:
    DiskManagerApp();
    virtual ~DiskManagerApp() override;

    virtual bool init() override;
    virtual void shutdown() override;
    virtual void draw(uint32_t x, uint32_t y, uint32_t w, uint32_t h) override;

    virtual void onMouseDown(int x, int y, uint8_t button) override;
    virtual void onWidgetClick(int widgetId) override;

    static app::KernelApp* create() { return new DiskManagerApp(); }

private:
    static const int MAX_DISKS = 16;
    static const int MAX_PARTS = 4;

    struct PartEntry {
        uint8_t  type;
        uint32_t lbaStart;
        uint32_t lbaCount;
        bool     bootable;
        char     fsLabel[16];  // "FAT", "EXT2", "TarFS", "Unknown"
    };

    struct DiskEntry {
        char     name[40];
        uint8_t  devIndex;
        uint64_t totalSectors;
        uint32_t sectorSize;
        bool     haveInfo;
        PartEntry parts[MAX_PARTS];
        int       partCount;
    };

    DiskEntry m_disks[MAX_DISKS];
    int       m_diskCount;
    int       m_selectedDisk;

    int m_refreshBtnId;

    void        scanDisks();
    void        readMBR(DiskEntry& disk);
    const char* detectFs(uint8_t devIndex, uint32_t lbaStart);
    void        formatSize(uint64_t bytes, char* out, int outSize) const;
};

class TrashApp : public app::KernelApp {
public:
    TrashApp();
    virtual ~TrashApp() override;

    virtual bool init() override;
    virtual void shutdown() override;
    virtual void draw(uint32_t x, uint32_t y, uint32_t w, uint32_t h) override;
    virtual void onWidgetClick(int widgetId) override;

    static app::KernelApp* create() { return new TrashApp(); }

private:
    static const int MAX_TRASH_ENTRIES = 32;

    struct TrashEntry {
        char name[vfs::VFS_MAX_FILENAME];
        bool isDir;
        char originalPath[256];
        char originalFolder[256];
        char type[32];
        char iconKey[32];
        char deletedText[32];
        uint64_t size;
    };

    TrashEntry m_entries[MAX_TRASH_ENTRIES];
    int m_entryCount;
    int m_selectedIndex;
    int m_emptyBtnId;
    int m_confirmEmptyBtnId;
    int m_cancelEmptyBtnId;
    int m_restoreBtnId;
    int m_restoreAllBtnId;
    int m_deletePermanentBtnId;
    int m_refreshBtnId;
    int m_propertiesBtnId;
    bool m_confirmEmpty;
    bool m_showProperties;
    char m_status[128];

    void refreshEntries();
    bool purgeContents(int* deletedCount);
    void updateButtons();
    void restoreSelected();
    void restoreAll();
    void deleteSelectedPermanently();
    bool restoreEntry(const TrashEntry& entry);
    bool deleteEntryPermanently(const TrashEntry& entry);
    void parentPathOf(const char* path, char* out, int outSize) const;
    void basenameOf(const char* path, char* out, int outSize) const;
    void makeUniqueRestorePath(const char* desiredPath, char* out, int outSize) const;
    void formatSize(uint64_t size, char* out, int outSize) const;
    const char* iconForEntry(const TrashEntry& entry) const;
    const char* typeForEntry(const TrashEntry& entry) const;
};

// ============================================================
// App Registration
// ============================================================

// Call this once to register all kernel GUI apps
void registerKernelApps();

} // namespace apps
} // namespace kernel

#endif // KERNEL_KERNEL_APPS_H
