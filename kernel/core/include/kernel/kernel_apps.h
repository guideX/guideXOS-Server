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
#include "kernel/desktop.h"

// Bare-metal Navigator is a capability-limited adapter.  It mirrors the small
// guideWeb CSS-lite value types it needs without including the hosted
// std::string/std::vector document model in the freestanding kernel build.
namespace gxos {
namespace web {
struct WebStyle {
    bool     hasColor = false;
    uint32_t color = 0;
    bool     hasBackgroundColor = false;
    uint32_t backgroundColor = 0;
    bool     bold = false;
    bool     underline = false;
    int      marginTop = -1;
    int      marginBottom = -1;
    int      marginLeft = -1;
    int      padding = -1;
    int      fontScaleOrSize = -1;
};

struct CssDiagnostics {
    bool cssDetected = false;
    int styleRuleCount = 0;
    int unsupportedExternalStylesheetCount = 0;
    int unsupportedDeclarationCount = 0;
    bool styleBlockCapped = false;
    unsigned long styleBytesProcessed = 0;
};
} // namespace web
} // namespace gxos

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
    kernel::desktop::SystemDesktopIconVisibility m_desktopIconVisibility;

    void loadSelection();
    void applySelected();
    int hitBackground(int x, int y) const;
    int hitWallpaper(int x, int y) const;
    int hitGradient(int x, int y) const;
    int hitDesktopIconCheckbox(int x, int y) const;
    void drawCheckbox(uint32_t x, uint32_t y, const char* label, bool checked);
    void toggleDesktopIconCheckbox(int index);
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
    static const int CONTEXT_MENU_W = 150;
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
    void pinSelectedToDesktop();
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

// Bare-metal Navigator adapter.
//
// The hosted/compositor Navigator in navigator.cpp is the authoritative full
// app-model implementation. This kernel-side app keeps the same user-facing
// shape where platform facilities exist, and reports honest unsupported
// capability documents/placeholders where they do not.
struct KernelHttpResponse;

class NavigatorApp : public app::KernelApp {
public:
    NavigatorApp();
    virtual ~NavigatorApp() override;

    virtual bool init() override;
    virtual void shutdown() override;
    virtual void draw(uint32_t x, uint32_t y, uint32_t w, uint32_t h) override;
    virtual void onMouseMove(int x, int y) override;
    virtual void onMouseDown(int x, int y, uint8_t button) override;
    virtual void onMouseUp(int x, int y, uint8_t button) override;
    virtual void onWidgetClick(int widgetId) override;
    virtual void onKeyDown(uint32_t key) override;
    virtual void onKeyUp(uint32_t key) override;
    virtual void onKeyChar(char c) override;

    static app::KernelApp* create() { return new NavigatorApp(); }
    static bool smokeHttpFetch(const char* url, int* statusCode, char* contentType,
                               int contentTypeLen, int* bodyBytes, int* parsedBlocks,
                               char* error, int errorLen, char* finalUrl = nullptr,
                               int finalUrlLen = 0, int* redirectCount = nullptr,
                               int* remoteImages = nullptr, int* loadedImages = nullptr,
                               int* failedImages = nullptr);
    static bool smokeFormsLitePost(const char* action, int* statusCode, char* contentType,
                                   int contentTypeLen, int* bodyBytes, int* parsedBlocks,
                                   char* error, int errorLen, char* finalUrl = nullptr,
                                   int finalUrlLen = 0, int* redirectCount = nullptr,
                                   int* submittedBodyBytes = nullptr);
    static bool smokeInteractiveFormsLitePost(const char* formUrl, int* statusCode,
                                              char* contentType, int contentTypeLen,
                                              int* bodyBytes, int* parsedBlocks,
                                              char* error, int errorLen,
                                              int* submittedBodyBytes = nullptr);
    static bool smokeInteractiveFormsLiteGet(const char* formUrl, char* finalUrl,
                                             int finalUrlLen, int* parsedBlocks,
                                             char* error, int errorLen);
    static bool smokeControlledLocalHttpsNavigation(const char* url,
                                                    int* statusCode, char* contentType,
                                                    int contentTypeLen, int* bodyBytes,
                                                    int* parsedBlocks, char* error,
                                                    int errorLen, char* finalUrl = nullptr,
                                                    int finalUrlLen = 0, int* redirectCount = nullptr,
                                                    int* plainTcpConnectAttempts = nullptr,
                                                    int* tlsTcpConnectAttempts = nullptr,
                                                    uint32_t* tlsVerifyFlags = nullptr,
                                                    char* tlsSniHost = nullptr,
                                                    int tlsSniHostLen = 0,
                                                    char* tlsProtocol = nullptr,
                                                    int tlsProtocolLen = 0,
                                                    char* tlsCipherSuite = nullptr,
                                                    int tlsCipherSuiteLen = 0,
                                                    char* transportSelection = nullptr,
                                                    int transportSelectionLen = 0,
                                                    char* tlsStatus = nullptr,
                                                    int tlsStatusLen = 0,
                                                    bool* tlsValidated = nullptr,
                                                    bool* tlsHostnameValidated = nullptr,
                                                    bool* tlsAllowlistLocalOnly = nullptr,
                                                    char* sourceType = nullptr,
                                                    int sourceTypeLen = 0);
    static bool smokeHttpsUnsupportedDocument(const char* url, const char* expectedFinalUrl,
                                              int expectedPlainTcpConnectAttempts,
                                              int expectedTlsTcpConnectAttempts,
                                              char* requestedUrl, int requestedUrlLen,
                                              char* finalUrl, int finalUrlLen,
                                              char* error, int errorLen,
                                              int* plainTcpConnectAttempts,
                                              int* tlsTcpConnectAttempts);
    static void smokeCaptureHttpsNavigation(const char* url,
                                            char* requestedUrl, int requestedUrlLen,
                                            int* statusCode,
                                            char* contentType, int contentTypeLen,
                                            int* bodyBytes,
                                            int* parsedBlocks,
                                            char* error, int errorLen,
                                            char* finalUrl, int finalUrlLen,
                                            int* redirectCount,
                                            int* plainTcpConnectAttempts,
                                            int* tlsTcpConnectAttempts,
                                            uint32_t* tlsVerifyFlags,
                                            char* tlsSniHost, int tlsSniHostLen,
                                            char* tlsProtocol, int tlsProtocolLen,
                                            char* tlsCipherSuite, int tlsCipherSuiteLen,
                                            char* transportSelection, int transportSelectionLen,
                                            char* tlsStatus, int tlsStatusLen,
                                            bool* tlsValidated,
                                            bool* tlsHostnameValidated,
                                            bool* tlsAllowlistLocalOnly,
                                            char* sourceType, int sourceTypeLen,
                                            char* contentEncoding = nullptr,
                                            int contentEncodingLen = 0,
                                            bool* dnsUsed = nullptr,
                                            char* dnsHost = nullptr,
                                            int dnsHostLen = 0,
                                            char* dnsResolvedIp = nullptr,
                                            int dnsResolvedIpLen = 0,
                                            char* dnsError = nullptr,
                                            int dnsErrorLen = 0,
                                            char* tlsBackend = nullptr,
                                            int tlsBackendLen = 0,
                                            char* transportPolicyReason = nullptr,
                                            int transportPolicyReasonLen = 0,
                                            char* unsupportedReason = nullptr,
                                            int unsupportedReasonLen = 0,
                                            bool* headerCapHit = nullptr,
                                            bool* bodyCapHit = nullptr,
                                            bool* downgradeRedirectBlocked = nullptr,
                                            bool* tlsSucceededBeforeContentFailure = nullptr,
                                            int* tlsHandshakeErrorCode = nullptr,
                                            int* tlsTransportErrorCode = nullptr,
                                            int* tlsRequestBytesWritten = nullptr,
                                            int* tlsResponseBytesRead = nullptr,
                                            int* tlsRetryCount = nullptr,
                                            char* tlsRetryReason = nullptr,
                                            int tlsRetryReasonLen = 0,
                                            int* tlsBytesWrittenBeforeRetry = nullptr,
                                            bool* tcpAbortUsed = nullptr,
                                            bool* redirectedHttpsRetryUsed = nullptr,
                                            int* redirectHopIndex = nullptr,
                                            char* redirectHopUrl = nullptr,
                                            int redirectHopUrlLen = 0);

private:
    enum NavigatorMouseMode {
        NAV_MOUSE_NONE = 0,
        NAV_MOUSE_POTENTIAL_LINK_CLICK,
        NAV_MOUSE_POTENTIAL_TEXT_SELECTION,
        NAV_MOUSE_SELECTING_TEXT,
        NAV_MOUSE_ADDRESS_BAR_INTERACTION
    };

    static const int MAX_STATUS_LEN = 128;
    static const int MAX_URL_LEN = 160;
    static const int MAX_TITLE_LEN_NAV = 96;
    static const int MAX_BLOCKS = 64;
    static const int MAX_BLOCK_TEXT = 320;
    static const int MAX_BOOKMARKS = 12;
    static const int MAX_SOURCE_PREVIEW = 2048;
    static const int MAX_FORM_OPTIONS = 8;
    static const int MAX_FORM_VALUE = 320;
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
        BLOCK_IMAGE,
        BLOCK_FORM_TEXT,
        BLOCK_FORM_CHECKBOX,
        BLOCK_FORM_RADIO,
        BLOCK_FORM_TEXTAREA,
        BLOCK_FORM_SELECT,
        BLOCK_FORM_SUBMIT
    };

    struct FormOption {
        char text[64];
        char value[64];
    };

    struct DocBlock {
        BlockKind kind;
        char text[MAX_BLOCK_TEXT];
        char url[MAX_URL_LEN];
        char src[MAX_URL_LEN];
        char alt[96];
        int width;
        int height;
        int naturalWidth;
        int naturalHeight;
        int imageStatus;
        const uint32_t* imagePixels;
        char imageError[128];
        gxos::web::WebStyle style;
        int formIndex;
        char formAction[MAX_URL_LEN];
        char formMethod[8];
        char formEncoding[48];
        char inputName[64];
        char inputValue[MAX_FORM_VALUE];
        char placeholder[96];
        char submitLabel[64];
        bool checked;
        bool disabled;
        bool formUnsupported;
        int visibleRows;
        FormOption options[MAX_FORM_OPTIONS];
        int optionCount;
        int selectedOption;
    };

    struct Bookmark {
        char title[64];
        char url[MAX_URL_LEN];
    };

    struct DownloadRecord {
        char url[MAX_URL_LEN];
        char finalUrl[MAX_URL_LEN];
        char suggestedFileName[vfs::VFS_MAX_FILENAME];
        char contentType[48];
        char savedPath[MAX_URL_LEN];
        int byteCount;
        bool success;
        char error[128];
    };

    struct SelectionPosition {
        int blockIndex;
        int offset;
    };

    char m_status[MAX_STATUS_LEN];
    char m_currentUrl[MAX_URL_LEN];
    char m_title[MAX_TITLE_LEN_NAV];
    DocBlock m_blocks[MAX_BLOCKS];
    int m_blockCount;
    Bookmark m_bookmarks[MAX_BOOKMARKS];
    int m_bookmarkCount;
    DownloadRecord m_recentDownloads[8];
    int m_recentDownloadCount;
    char m_backStack[12][MAX_URL_LEN];
    int m_backCount;
    char m_forwardStack[12][MAX_URL_LEN];
    int m_forwardCount;
    bool m_addressFocused;
    char m_addressBuffer[MAX_URL_LEN];
    int m_addressCaret;
    bool m_ctrlPressed;
    int m_scrollY;
    int m_hoverLinkIndex;
    bool m_selectionActive;
    bool m_selectionDragging;
    bool m_selectionMoved;
    bool m_mouseLeftDown;
    NavigatorMouseMode m_mouseMode;
    int m_mouseDownLinkIndex;
    int m_mouseDownX;
    int m_mouseDownY;
    bool m_mouseDragThresholdExceeded;
    SelectionPosition m_selectionAnchor;
    SelectionPosition m_selectionFocus;
    char m_clipboard[MAX_SOURCE_PREVIEW];
    char m_clipboardMode[48];
    int m_backBtnId;
    int m_forwardBtnId;
    int m_reloadBtnId;
    int m_homeBtnId;
    int m_bookmarksBtnId;
    int m_addBookmarkBtnId;
    char m_metaRequestedUrl[MAX_URL_LEN];
    char m_metaFinalUrl[MAX_URL_LEN];
    char m_metaSourceType[24];
    int m_metaHttpStatusCode;
    char m_metaHttpReason[48];
    char m_metaContentType[48];
    char m_metaContentEncoding[32];
    char m_metaUnsupportedReason[128];
    bool m_metaRedirected;
    int m_metaRedirectCount;
    char m_metaErrorStatus[128];
    bool m_metaHeaderCapHit;
    bool m_metaBodyCapHit;
    bool m_metaTlsSucceededBeforeContentFailure;
    bool m_metaDowngradeRedirectBlocked;
    char m_metaSourcePreview[MAX_SOURCE_PREVIEW];
    int m_metaSourceBytes;
    bool m_metaSourceTruncated;
    int m_metaDocumentBlocks;
    int m_metaImageBlocks;
    int m_metaLoadedImages;
    int m_metaFailedImages;
    int m_metaRemoteImages;
    int m_metaLocalImages;
    char m_metaLastImageError[128];
    char m_metaScheme[8];
    bool m_metaDnsUsed;
    char m_metaDnsHost[64];
    char m_metaDnsResolvedIp[16];
    char m_metaDnsError[64];
    bool m_metaTlsUsed;
    bool m_metaTlsValidated;
    bool m_metaTlsHostnameValidated;
    bool m_metaTlsAllowlistLocalOnly;
    uint32_t m_metaTlsVerifyFlags;
    int m_metaTlsHandshakeErrorCode;
    int m_metaTlsTransportErrorCode;
    int m_metaTlsRequestBytesWritten;
    int m_metaTlsResponseBytesRead;
    int m_metaTlsRetryCount;
    char m_metaTlsRetryReason[96];
    int m_metaTlsBytesWrittenBeforeRetry;
    bool m_metaTcpAbortUsed;
    bool m_metaRedirectedHttpsRetryUsed;
    int m_metaRedirectHopIndex;
    char m_metaRedirectHopUrl[MAX_URL_LEN];
    char m_metaTlsBackend[48];
    char m_metaTransportSelection[40];
    char m_metaTlsStatus[40];
    char m_metaTransportPolicyReason[128];
    char m_metaTlsHostname[64];
    char m_metaTlsSniHost[64];
    char m_metaTlsProtocol[32];
    char m_metaTlsCipherSuite[64];
    bool m_metaCssDetected;
    int m_metaStyleRuleCount;
    int m_metaUnsupportedExternalStylesheetCount;
    int m_metaUnsupportedCssDeclarationCount;
    bool m_metaCssStyleBlockCapped;
    int m_metaCssStyleBytesProcessed;
    bool m_metaDownloaded;
    char m_metaDownloadSavedPath[MAX_URL_LEN];
    int m_metaDownloadByteCount;
    char m_lastDownloadError[128];
    char m_lastSubmittedFormAction[MAX_URL_LEN];
    char m_lastSubmittedFormMethod[8];
    char m_lastSubmittedFormStatus[128];
    char m_lastPostHttpStatus[48];
    char m_lastPostContentType[48];
    int m_lastPostBodyBytes;
    char m_lastFormError[128];
    int m_metaFormCount;
    int m_metaTextInputCount;
    int m_metaCheckboxCount;
    int m_metaRadioCount;
    int m_metaTextareaCount;
    int m_metaSelectCount;
    int m_metaSubmitCount;
    int m_metaUnsupportedFormCount;
    int m_focusedFormBlock;
    int m_formCaret;
    gxos::web::WebStyle m_bodyStyle;

    void setStatus(const char* text);
    void updateButtons();
    void loadUrl(const char* url);
    void navigateTo(const char* url);
    void goBack();
    void goForward();
    void buildAboutNavigatorDocument();
    void buildBookmarksDocument();
    void buildPageInfoDocument();
    void buildViewSourceDocument();
    void buildRuntimeDocument();
    void buildDownloadsDocument();
    void buildDownloadResultDocument(const DownloadRecord& record);
    void buildErrorDocument(const char* url, const char* reason);
    void buildHttpsUnsupportedDocument(const char* url, bool redirected, const char* detail = nullptr);
    void buildUnsupportedContentEncodingDocument(const char* url, const char* encoding);
    void loadFileUrl(const char* url);
    void loadHttpUrl(const char* url);
    void loadHttpResponse(const char* url, KernelHttpResponse* response);
    void submitFormsLitePost(const char* action, const char* body, int bodyBytes,
                             const char* contentType = "application/x-www-form-urlencoded");
    void submitFormForBlock(int blockIndex);
    void activateFormControl(int blockIndex);
    bool setFormControlValue(const char* name, const char* value);
    bool selectFormRadio(const char* name, const char* value);
    bool selectFormOption(const char* name, const char* value);
    bool setFormCheckbox(const char* name, bool checked);
    void rememberPageMetadata(const char* requestedUrl, const char* finalUrl, const char* sourceType,
                              const char* contentType, const char* errorStatus,
                              const char* rawSource, int rawSourceBytes,
                              const gxos::web::CssDiagnostics* cssDiagnostics = nullptr,
                              const gxos::web::WebStyle* bodyStyle = nullptr,
                              int httpStatusCode = 0, const char* httpReason = "",
                              int redirectCount = 0,
                              const KernelHttpResponse* networkResponse = nullptr);
    void addBlock(BlockKind kind, const char* text, const char* url = "", const gxos::web::WebStyle* style = nullptr);
    void addImageBlock(const char* src, const char* alt, const char* resolvedUrl, int width, int height, const gxos::web::WebStyle* style = nullptr);
    void addBookmark(const char* title, const char* url);
    void loadDefaultBookmarks();
    void drawDocument(uint32_t x, uint32_t y, uint32_t w, uint32_t h);
    void drawWrappedText(uint32_t x, uint32_t y, const char* text, uint32_t color, int maxChars, int& outY) const;
    bool isSelectableBlock(const DocBlock& block) const;
    void clearSelection();
    bool hasSelection() const;
    void beginSelection(int x, int y);
    void updateSelection(int x, int y);
    void finalizeSelection(int x, int y);
    bool copySelectionToClipboard();
    void selectAllDocumentText();
    SelectionPosition textPositionFromPoint(int x, int y, bool clampToNearest) const;
    bool selectedText(char* out, int outSize) const;
    void blockTextForSelection(const DocBlock& block, char* out, int outSize) const;
    int blockHeight(const DocBlock& block, int maxChars) const;
    int blockY(int index, int maxChars) const;
    int hitLinkIndex(int x, int y) const;
    int hitFormBlockIndex(int x, int y) const;
    bool isFormBlock(const DocBlock& block) const;
    bool isFocusableFormBlock(const DocBlock& block) const;
    int formControlHeight(const DocBlock& block) const;
    void focusFormBlock(int blockIndex);
    void blurFormBlock();
    void focusNextFormBlock();
    void formControlRect(int blockIndex, int& x, int& y, int& w, int& h) const;
    bool hitAddressBar(int x, int y) const;
    void focusAddressBar();
    void blurAddressBar();
    void commitAddressBar();
    void normalizeUrl(const char* input, char* out, int outSize) const;
    void parseHtmlDocument(const char* url, const char* html,
                           const char* sourceType = "file",
                           const char* contentType = "text/html",
                           int httpStatusCode = 0,
                           const char* httpReason = "",
                           const char* requestedUrl = nullptr,
                           int redirectCount = 0,
                           const KernelHttpResponse* networkResponse = nullptr);
    void prepareImageResources();
    void resolveHref(const char* baseUrl, const char* href, char* out, int outSize) const;
    void rememberDownload(const DownloadRecord& record);
    void clearPageDownloadMetadata();
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
        char trashRoot[256];
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
void printNavigatorRuntimeSmokeReport();
void printNavigatorHttpRuntimeSmokeReport();

} // namespace apps
} // namespace kernel

#endif // KERNEL_KERNEL_APPS_H
