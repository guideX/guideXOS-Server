//
// guideXOS Kernel GUI Apps Implementation
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/kernel/kernel_apps.h"
#include "include/kernel/kernel_compositor.h"
#include "include/kernel/framebuffer.h"
#include "include/kernel/shell.h"
#include "include/kernel/ps2keyboard.h"
#include "include/kernel/vfs.h"
#include "include/kernel/pit.h"
#include "include/kernel/serial_debug.h"

namespace kernel {
namespace apps {

// ============================================================
// Helper: string copy
// ============================================================

static void strcopy(char* dst, const char* src, int maxLen) {
    int i = 0;
    while (src[i] && i < maxLen - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int strlen_local(const char* s) {
    int len = 0;
    while (s[len]) len++;
    return len;
}

// ============================================================
// Color helpers
// ============================================================

static uint32_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    return 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

// Bitmap font constants (same as compositor)
static const int kGlyphW = 5;
static const int kGlyphH = 7;
static const int kGlyphSpacing = 1;
static const int kGlyphCount = 95;

// Bitmap font glyph data (5x7, ASCII 32..126)
static const uint8_t s_glyphs[kGlyphCount][5] = {
    {0x00,0x00,0x00,0x00,0x00}, // 32 ' '
    {0x00,0x00,0x5F,0x00,0x00}, // 33 '!'
    {0x00,0x07,0x00,0x07,0x00}, // 34 '"'
    {0x14,0x7F,0x14,0x7F,0x14}, // 35 '#'
    {0x24,0x2A,0x7F,0x2A,0x12}, // 36 '$'
    {0x23,0x13,0x08,0x64,0x62}, // 37 '%'
    {0x36,0x49,0x55,0x22,0x50}, // 38 '&'
    {0x00,0x05,0x03,0x00,0x00}, // 39 '''
    {0x00,0x1C,0x22,0x41,0x00}, // 40 '('
    {0x00,0x41,0x22,0x1C,0x00}, // 41 ')'
    {0x14,0x08,0x3E,0x08,0x14}, // 42 '*'
    {0x08,0x08,0x3E,0x08,0x08}, // 43 '+'
    {0x00,0x50,0x30,0x00,0x00}, // 44 ','
    {0x08,0x08,0x08,0x08,0x08}, // 45 '-'
    {0x00,0x60,0x60,0x00,0x00}, // 46 '.'
    {0x20,0x10,0x08,0x04,0x02}, // 47 '/'
    {0x3E,0x51,0x49,0x45,0x3E}, // 48 '0'
    {0x00,0x42,0x7F,0x40,0x00}, // 49 '1'
    {0x42,0x61,0x51,0x49,0x46}, // 50 '2'
    {0x21,0x41,0x45,0x4B,0x31}, // 51 '3'
    {0x18,0x14,0x12,0x7F,0x10}, // 52 '4'
    {0x27,0x45,0x45,0x45,0x39}, // 53 '5'
    {0x3C,0x4A,0x49,0x49,0x30}, // 54 '6'
    {0x01,0x71,0x09,0x05,0x03}, // 55 '7'
    {0x36,0x49,0x49,0x49,0x36}, // 56 '8'
    {0x06,0x49,0x49,0x29,0x1E}, // 57 '9'
    {0x00,0x36,0x36,0x00,0x00}, // 58 ':'
    {0x00,0x56,0x36,0x00,0x00}, // 59 ';'
    {0x08,0x14,0x22,0x41,0x00}, // 60 '<'
    {0x14,0x14,0x14,0x14,0x14}, // 61 '='
    {0x00,0x41,0x22,0x14,0x08}, // 62 '>'
    {0x02,0x01,0x51,0x09,0x06}, // 63 '?'
    {0x32,0x49,0x79,0x41,0x3E}, // 64 '@'
    {0x7E,0x11,0x11,0x11,0x7E}, // 65 'A'
    {0x7F,0x49,0x49,0x49,0x36}, // 66 'B'
    {0x3E,0x41,0x41,0x41,0x22}, // 67 'C'
    {0x7F,0x41,0x41,0x22,0x1C}, // 68 'D'
    {0x7F,0x49,0x49,0x49,0x41}, // 69 'E'
    {0x7F,0x09,0x09,0x09,0x01}, // 70 'F'
    {0x3E,0x41,0x49,0x49,0x7A}, // 71 'G'
    {0x7F,0x08,0x08,0x08,0x7F}, // 72 'H'
    {0x00,0x41,0x7F,0x41,0x00}, // 73 'I'
    {0x20,0x40,0x41,0x3F,0x01}, // 74 'J'
    {0x7F,0x08,0x14,0x22,0x41}, // 75 'K'
    {0x7F,0x40,0x40,0x40,0x40}, // 76 'L'
    {0x7F,0x02,0x0C,0x02,0x7F}, // 77 'M'
    {0x7F,0x04,0x08,0x10,0x7F}, // 78 'N'
    {0x3E,0x41,0x41,0x41,0x3E}, // 79 'O'
    {0x7F,0x09,0x09,0x09,0x06}, // 80 'P'
    {0x3E,0x41,0x51,0x21,0x5E}, // 81 'Q'
    {0x7F,0x09,0x19,0x29,0x46}, // 82 'R'
    {0x46,0x49,0x49,0x49,0x31}, // 83 'S'
    {0x01,0x01,0x7F,0x01,0x01}, // 84 'T'
    {0x3F,0x40,0x40,0x40,0x3F}, // 85 'U'
    {0x1F,0x20,0x40,0x20,0x1F}, // 86 'V'
    {0x3F,0x40,0x38,0x40,0x3F}, // 87 'W'
    {0x63,0x14,0x08,0x14,0x63}, // 88 'X'
    {0x07,0x08,0x70,0x08,0x07}, // 89 'Y'
    {0x61,0x51,0x49,0x45,0x43}, // 90 'Z'
    {0x00,0x7F,0x41,0x41,0x00}, // 91 '['
    {0x02,0x04,0x08,0x10,0x20}, // 92 backslash
    {0x00,0x41,0x41,0x7F,0x00}, // 93 ']'
    {0x04,0x02,0x01,0x02,0x04}, // 94 '^'
    {0x40,0x40,0x40,0x40,0x40}, // 95 '_'
    {0x00,0x01,0x02,0x04,0x00}, // 96 '`'
    {0x20,0x54,0x54,0x54,0x78}, // 97 'a'
    {0x7F,0x48,0x44,0x44,0x38}, // 98 'b'
    {0x38,0x44,0x44,0x44,0x20}, // 99 'c'
    {0x38,0x44,0x44,0x48,0x7F}, //100 'd'
    {0x38,0x54,0x54,0x54,0x18}, //101 'e'
    {0x08,0x7E,0x09,0x01,0x02}, //102 'f'
    {0x0C,0x52,0x52,0x52,0x3E}, //103 'g'
    {0x7F,0x08,0x04,0x04,0x78}, //104 'h'
    {0x00,0x44,0x7D,0x40,0x00}, //105 'i'
    {0x20,0x40,0x44,0x3D,0x00}, //106 'j'
    {0x7F,0x10,0x28,0x44,0x00}, //107 'k'
    {0x00,0x41,0x7F,0x40,0x00}, //108 'l'
    {0x7C,0x04,0x18,0x04,0x78}, //109 'm'
    {0x7C,0x08,0x04,0x04,0x78}, //110 'n'
    {0x38,0x44,0x44,0x44,0x38}, //111 'o'
    {0x7C,0x14,0x14,0x14,0x08}, //112 'p'
    {0x08,0x14,0x14,0x18,0x7C}, //113 'q'
    {0x7C,0x08,0x04,0x04,0x08}, //114 'r'
    {0x48,0x54,0x54,0x54,0x20}, //115 's'
    {0x04,0x3F,0x44,0x40,0x20}, //116 't'
    {0x3C,0x40,0x40,0x20,0x7C}, //117 'u'
    {0x1C,0x20,0x40,0x20,0x1C}, //118 'v'
    {0x3C,0x40,0x30,0x40,0x3C}, //119 'w'
    {0x44,0x28,0x10,0x28,0x44}, //120 'x'
    {0x0C,0x50,0x50,0x50,0x3C}, //121 'y'
    {0x44,0x64,0x54,0x4C,0x44}, //122 'z'
    {0x00,0x08,0x36,0x41,0x00}, //123 '{'
    {0x00,0x00,0x7F,0x00,0x00}, //124 '|'
    {0x00,0x41,0x36,0x08,0x00}, //125 '}'
    {0x10,0x08,0x08,0x10,0x08}, //126 '~'
};

static const uint8_t* getGlyph(char c) {
    int idx = (int)(unsigned char)c - 32;
    if (idx < 0 || idx >= kGlyphCount) return nullptr;
    return s_glyphs[idx];
}

// Draw a single character using the bitmap font
static void drawChar(uint32_t px, uint32_t py, char c, uint32_t color) {
    const uint8_t* g = getGlyph(c);
    if (!g) return;
    for (int col = 0; col < kGlyphW; col++) {
        uint8_t bits = g[col];
        for (int row = 0; row < kGlyphH; row++) {
            if (bits & (1 << row)) {
                framebuffer::put_pixel(px + col, py + row, color);
            }
        }
    }
}

static void appDrawText(uint32_t x, uint32_t y, const char* text, uint32_t color) {
    uint32_t cx = x;
    while (text && *text) {
        drawChar(cx, y, *text, color);
        cx += kGlyphW + kGlyphSpacing;
        text++;
    }
}

// ============================================================
// NotepadApp Implementation
// ============================================================

// Static clipboard for cut/copy/paste
char NotepadApp::s_clipboard[MAX_TEXT_LENGTH] = {0};
int NotepadApp::s_clipboardLength = 0;

NotepadApp::NotepadApp() : m_textLength(0), m_cursorPos(0), m_scrollY(0), m_selectAll(false),
                           m_modified(false), m_ctrlPressed(false), m_showFileMenu(false),
                           m_showEditMenu(false), m_showContextMenu(false), m_contextMenuX(0),
                            m_contextMenuY(0), m_hoveredMenuItem(-1), m_hoveredMenuType(0),
                            m_selectionStart(-1), m_selectionEnd(-1) {
    strcopy(m_name, "Notepad", app::MAX_APP_NAME);
    m_text[0] = '\0';
    m_filePath[0] = '\0';
    m_showSaveDialog = false;
    m_saveDialogIsOpenMode = false;
    m_saveDialogShowingDrives = true;
    m_saveDialogFilenameFocused = true;
    m_saveDialogPath[0] = '\0';
    strcopy(m_saveDialogFilename, "untitled.txt", MAX_SAVE_FILENAME);
    m_saveDialogStatus[0] = '\0';
    m_saveEntryCount = 0;
    m_saveSelected = 0;
    m_saveScroll = 0;
}

NotepadApp::~NotepadApp() {
}

bool NotepadApp::init() {
    return initWithParam(nullptr);
}

bool NotepadApp::initWithParam(const char* filePath) {
    // Create window
    m_window = new app::KernelWindow();
    strcopy(m_window->title, "Notepad - Untitled", app::MAX_TITLE_LEN);
    m_window->x = 100;
    m_window->y = 50;
    m_window->w = 600;
    m_window->h = 400;
    m_window->flags = app::WF_VISIBLE | app::WF_TITLEBAR | app::WF_CLOSABLE | app::WF_RESIZABLE | app::WF_FOCUSED;
    m_window->owner = this;
    
    // Register with compositor
    if (!compositor::KernelCompositor::registerWindow(m_window)) {
        delete m_window;
        m_window = nullptr;
        return false;
    }
    
    // Load file if specified, otherwise show welcome
    if (filePath && filePath[0] != '\0') {
        if (!loadFile(filePath)) {
            // File load failed, start with empty document
            newFile();
        }
    } else {
        // Initialize with welcome message
        const char* welcome = "Welcome to guideXOS Notepad!\n\nFile/Edit menus available.\nRight-click for context menu.\nCtrl+S to save, Ctrl+O to open.\n\nType here...";
        strcopy(m_text, welcome, MAX_TEXT_LENGTH);
        m_textLength = strlen_local(m_text);
        m_cursorPos = m_textLength;
    }
    
    m_state = app::AppState::Running;
    return true;
}

void NotepadApp::shutdown() {
    m_state = app::AppState::Terminated;
}

void NotepadApp::draw(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    // Draw menu bar background
    framebuffer::fill_rect(x, y, w, MENU_BAR_HEIGHT, rgb(50, 50, 60));
    drawMenuBar(x, y, w);
    
    // Text editor background
    uint32_t textAreaY = y + MENU_BAR_HEIGHT;
    uint32_t textAreaH = h - MENU_BAR_HEIGHT;
    framebuffer::fill_rect(x + 4, textAreaY + 4, w - 8, textAreaH - 8, rgb(45, 45, 55));
    
    // Select-all highlight
    if (m_selectAll && m_textLength > 0) {
        framebuffer::fill_rect(x + 4, textAreaY + 4, w - 8, textAreaH - 8, rgb(42, 91, 154));
    }
    
    // Draw text
    uint32_t textX = x + 8;
    uint32_t textY = textAreaY + 8;
    uint32_t lineH = kGlyphH + 3;
    uint32_t maxY = y + h - 8;
    
    int line = 0;
    int col = 0;
    int charIdx = 0;
    
    while (charIdx <= m_textLength && textY + kGlyphH < maxY) {
        char c = (charIdx < m_textLength) ? m_text[charIdx] : '\0';
        
        // Draw cursor
        if (charIdx == m_cursorPos) {
            framebuffer::fill_rect(textX + col * (kGlyphW + kGlyphSpacing), textY,
                                   2, kGlyphH + 2, rgb(200, 200, 220));
        }
        
        if (c == '\n' || c == '\0') {
            // New line
            line++;
            col = 0;
            textY += lineH;
        } else if (c >= 32 && c < 127) {
            // Printable character
            uint32_t cx = textX + col * (kGlyphW + kGlyphSpacing);
            
            if (c != ' ') {
                drawChar(cx, textY, c, rgb(220, 220, 235));
            }
            col++;
            
            // Word wrap
            if (col * (kGlyphW + kGlyphSpacing) + kGlyphW > w - 16) {
                line++;
                col = 0;
                textY += lineH;
            }
        }
        
        charIdx++;
    }
    
    // Draw menus on top
    if (m_showFileMenu) drawFileMenu(x + 4, y + MENU_BAR_HEIGHT);
    if (m_showEditMenu) drawEditMenu(x + 50, y + MENU_BAR_HEIGHT);
    if (m_showContextMenu) drawContextMenu(x + m_contextMenuX, y + m_contextMenuY);
    if (m_showSaveDialog) drawSaveAsDialog(x, y, w, h);
}

void NotepadApp::onKeyChar(char c) {
    if (m_showSaveDialog) {
        handleSaveDialogChar(c);
        invalidate();
        return;
    }

    if (c >= 32 && c < 127) {
        if (m_selectAll) {
            clearText();
            m_selectAll = false;
        }
        insertChar(c);
        m_modified = true;
        invalidate();
    }
}

void NotepadApp::onKeyDown(uint32_t key) {
    if (m_showSaveDialog) {
        handleSaveDialogKey(key);
        invalidate();
        return;
    }

    bool ctrl = ps2keyboard::is_ctrl_down();
    m_ctrlPressed = ctrl;
    
    // Ctrl shortcuts
    if (ctrl) {
        if (key == 'a' || key == 'A') {
            selectAll();
            invalidate();
            return;
        }
        if (key == 'c' || key == 'C') {
            copy();
            return;
        }
        if (key == 'x' || key == 'X') {
            cut();
            invalidate();
            return;
        }
        if (key == 'v' || key == 'V') {
            paste();
            invalidate();
            return;
        }
        if (key == 's' || key == 'S') {
            saveFile();
            invalidate();
            return;
        }
        if (key == 'o' || key == 'O') {
            openOpenFileDialog();
            invalidate();
            return;
        }
        if (key == 'n' || key == 'N') {
            newFile();
            invalidate();
            return;
        }
    }
    
    switch (key) {
        case '\n':  // 10
        case '\r':  // 13
            if (m_selectAll) { clearText(); m_selectAll = false; }
            insertChar('\n');
            m_modified = true;
            break;
        case '\b':  // 8 (Backspace)
            if (m_selectAll) {
                clearText();
                m_selectAll = false;
            } else {
                deleteChar();
            }
            m_modified = true;
            break;
        case '\t':  // 9 (Tab)
            if (m_selectAll) { clearText(); m_selectAll = false; }
            insertChar(' '); insertChar(' '); insertChar(' '); insertChar(' ');
            m_modified = true;
            break;
        case 127:  // Delete (ASCII DEL)
        case 0x106:  // KEY_DELETE
            if (m_selectAll) {
                clearText();
                m_selectAll = false;
            } else {
                // Forward delete: remove char at cursor
                if (m_cursorPos < m_textLength) {
                    for (int i = m_cursorPos; i < m_textLength; i++) {
                        m_text[i] = m_text[i + 1];
                    }
                    m_textLength--;
                }
            }
            m_modified = true;
            break;
        case shell::KEY_LEFT:
            m_selectAll = false;
            moveCursor(-1);
            break;
        case shell::KEY_RIGHT:
            m_selectAll = false;
            moveCursor(1);
            break;
        case shell::KEY_HOME:
            m_selectAll = false;
            m_cursorPos = 0;
            break;
        case shell::KEY_END:
            m_selectAll = false;
            m_cursorPos = m_textLength;
            break;
        default:
            break;
    }
    invalidate();
}

void NotepadApp::onMouseMove(int x, int y) {
    if (updateMenuHover(x, y)) {
        invalidate();
    }
}

void NotepadApp::onMouseDown(int x, int y, uint8_t button) {
    // Debug: log all mouse clicks
    serial::puts("[NOTEPAD] Mouse down: button=");
    serial::put_hex8(button);
    serial::puts(" x=");
    serial::put_hex32(x);
    serial::puts(" y=");
    serial::put_hex32(y);
    serial::putc('\n');
    
    // Left click
    if (button == 1) {
        if (m_showSaveDialog) {
            if (handleSaveDialogClick(x, y)) {
                m_hoveredMenuItem = -1;
                m_hoveredMenuType = 0;
                invalidate();
                return;
            }
        }

        // Click on menu bar
        if (y < MENU_BAR_HEIGHT) {
            if (handleMenuClick(x, y)) {
                invalidate();
                return;
            }
        }
        // Click on dropdown menu
        else if (m_showFileMenu || m_showEditMenu) {
            if (handleMenuClick(x, y)) {
                invalidate();
                return;
            }
        }
        
        // Click elsewhere - close all menus
        m_showFileMenu = false;
        m_showEditMenu = false;
        m_showContextMenu = false;
        m_hoveredMenuItem = -1;
        m_hoveredMenuType = 0;
        invalidate();
        return;
    }
    
    // Right click - show context menu in text area
    if (button == 2) {
        serial::puts("[NOTEPAD] Right-click detected! Showing context menu\n");
        
        // Close dropdown menus
        m_showFileMenu = false;
        m_showEditMenu = false;
        m_hoveredMenuItem = -1;
        m_hoveredMenuType = 0;
        
        // Show context menu at mouse position
        m_showContextMenu = true;
        m_contextMenuX = x;
        m_contextMenuY = y;
        invalidate();
        return;
    }
}

void NotepadApp::onMouseUp(int x, int y, uint8_t button) {
    // Handle context menu clicks
    if (m_showContextMenu && button == 1) {
        if (handleContextMenuClick(x, y)) {
            m_showContextMenu = false;
            invalidate();
        }
    }
}

void NotepadApp::insertChar(char c) {
    if (m_textLength >= MAX_TEXT_LENGTH - 1) return;
    
    // Shift text after cursor
    for (int i = m_textLength; i > m_cursorPos; i--) {
        m_text[i] = m_text[i - 1];
    }
    
    m_text[m_cursorPos] = c;
    m_cursorPos++;
    m_textLength++;
    m_text[m_textLength] = '\0';
}

void NotepadApp::deleteChar() {
    if (m_cursorPos > 0 && m_textLength > 0) {
        // Shift text before cursor
        for (int i = m_cursorPos - 1; i < m_textLength; i++) {
            m_text[i] = m_text[i + 1];
        }
        m_cursorPos--;
        m_textLength--;
    }
}

void NotepadApp::clearText() {
    m_text[0] = '\0';
    m_textLength = 0;
    m_cursorPos = 0;
}

void NotepadApp::moveCursor(int delta) {
    m_cursorPos += delta;
    if (m_cursorPos < 0) m_cursorPos = 0;
    if (m_cursorPos > m_textLength) m_cursorPos = m_textLength;
}

int NotepadApp::getLineCount() const {
    int count = 1;
    for (int i = 0; i < m_textLength; i++) {
        if (m_text[i] == '\n') count++;
    }
    return count;
}

int NotepadApp::getLineStart(int lineIndex) const {
    if (lineIndex == 0) return 0;
    
    int line = 0;
    for (int i = 0; i < m_textLength; i++) {
        if (m_text[i] == '\n') {
            line++;
            if (line == lineIndex) return i + 1;
        }
    }
    return m_textLength;
}

// File operations
bool NotepadApp::loadFile(const char* path) {
    if (!path || path[0] == '\0') return false;
    
    uint8_t handle = vfs::open(path, vfs::OPEN_READ);
    if (handle == 0xFF) return false;
    
    int32_t bytesRead = vfs::read(handle, m_text, MAX_TEXT_LENGTH - 1);
    vfs::close(handle);
    
    if (bytesRead < 0) return false;
    
    m_text[bytesRead] = '\0';
    m_textLength = bytesRead;
    m_cursorPos = 0;
    m_modified = false;
    strcopy(m_filePath, path, MAX_PATH_LEN);
    updateTitle();
    return true;
}

bool NotepadApp::saveFile() {
    if (m_filePath[0] == '\0') {
        openSaveAsDialog();
        return false;
    }
    return saveFileAs(m_filePath);
}

bool NotepadApp::saveFileAs(const char* path) {
    if (!path || path[0] == '\0') return false;

    int32_t bytesWritten = vfs::write_file(path, m_text, static_cast<uint32_t>(m_textLength));
    if (bytesWritten != m_textLength) return false;

    m_modified = false;
    strcopy(m_filePath, path, MAX_PATH_LEN);
    updateTitle();
    return true;
}

void NotepadApp::newFile() {
    m_text[0] = '\0';
    m_textLength = 0;
    m_cursorPos = 0;
    m_modified = false;
    m_filePath[0] = '\0';
    updateTitle();
}

void NotepadApp::openSaveAsDialog() {
    m_showFileMenu = false;
    m_showEditMenu = false;
    m_showContextMenu = false;
    m_showSaveDialog = true;
    m_saveDialogIsOpenMode = false;
    m_saveDialogShowingDrives = true;
    m_saveDialogFilenameFocused = true;
    m_saveDialogPath[0] = '\0';
    m_saveSelected = 0;
    m_saveScroll = 0;
    if (m_filePath[0] != '\0') {
        const char* base = vfs::basename(m_filePath);
        if (base && base[0] != '\0') strcopy(m_saveDialogFilename, base, MAX_SAVE_FILENAME);
    } else {
        strcopy(m_saveDialogFilename, "untitled.txt", MAX_SAVE_FILENAME);
    }
    strcopy(m_saveDialogStatus, "Pick a drive or folder, then Save.", sizeof(m_saveDialogStatus));
    refreshSaveDialog();
    invalidate();
}

void NotepadApp::openOpenFileDialog() {
    m_showFileMenu = false;
    m_showEditMenu = false;
    m_showContextMenu = false;
    m_showSaveDialog = true;
    m_saveDialogIsOpenMode = true;
    m_saveDialogShowingDrives = true;
    m_saveDialogFilenameFocused = true;
    m_saveDialogPath[0] = '\0';
    m_saveSelected = 0;
    m_saveScroll = 0;
    strcopy(m_saveDialogFilename, "", MAX_SAVE_FILENAME);
    strcopy(m_saveDialogStatus, "Pick a drive, folder, or file to open.", sizeof(m_saveDialogStatus));
    refreshSaveDialog();
    invalidate();
}

void NotepadApp::refreshSaveDialog() {
    m_saveEntryCount = 0;
    if (m_saveDialogShowingDrives) {
        uint8_t count = vfs::mount_count();
        for (uint8_t i = 0; i < count && m_saveEntryCount < MAX_SAVE_ENTRIES; ++i) {
            const vfs::MountPoint* mount = vfs::get_mount_by_index(i);
            if (!mount || !mount->active) continue;
            SaveDialogEntry& entry = m_saveEntries[m_saveEntryCount++];
            strcopy(entry.name, mount->path, vfs::VFS_MAX_FILENAME);
            entry.isDir = true;
            entry.isDrive = true;
            entry.isFile = false;
        }
    } else {
        uint8_t dir = vfs::opendir(m_saveDialogPath);
        if (dir != 0xFF) {
            vfs::DirEntry de{};
            while (vfs::readdir(dir, &de) && m_saveEntryCount < MAX_SAVE_ENTRIES) {
                // Always show directories
                if (de.type == vfs::FILE_TYPE_DIRECTORY) {
                    SaveDialogEntry& entry = m_saveEntries[m_saveEntryCount++];
                    strcopy(entry.name, de.name, vfs::VFS_MAX_FILENAME);
                    entry.isDir = true;
                    entry.isDrive = false;
                    entry.isFile = false;
                }
                // Show files only in Open mode
                else if (m_saveDialogIsOpenMode && de.type == vfs::FILE_TYPE_REGULAR) {
                    SaveDialogEntry& entry = m_saveEntries[m_saveEntryCount++];
                    strcopy(entry.name, de.name, vfs::VFS_MAX_FILENAME);
                    entry.isDir = false;
                    entry.isDrive = false;
                    entry.isFile = true;
                }
            }
            vfs::closedir(dir);
        }
    }
    if (m_saveSelected >= m_saveEntryCount) m_saveSelected = m_saveEntryCount - 1;
    if (m_saveSelected < 0) m_saveSelected = 0;
}

void NotepadApp::drawSaveAsDialog(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    uint32_t dlgW = w > 440 ? 420 : w - 20;
    uint32_t dlgH = h > 300 ? 280 : h - 20;
    uint32_t dlgX = x + (w - dlgW) / 2;
    uint32_t dlgY = y + (h - dlgH) / 2;

    framebuffer::fill_rect(dlgX, dlgY, dlgW, dlgH, rgb(35, 35, 45));
    framebuffer::fill_rect(dlgX, dlgY, dlgW, 1, rgb(150, 150, 170));
    framebuffer::fill_rect(dlgX, dlgY + dlgH - 1, dlgW, 1, rgb(150, 150, 170));
    framebuffer::fill_rect(dlgX, dlgY, 1, dlgH, rgb(150, 150, 170));
    framebuffer::fill_rect(dlgX + dlgW - 1, dlgY, 1, dlgH, rgb(150, 150, 170));

    const char* title = m_saveDialogIsOpenMode ? "Open File" : "Save As";
    appDrawText(dlgX + 12, dlgY + 10, title, rgb(255, 255, 255));

    const char* locationLabel = m_saveDialogShowingDrives ? 
        (m_saveDialogIsOpenMode ? "Open from: Computer" : "Save in: Computer") : 
        (m_saveDialogIsOpenMode ? "Open from:" : "Save in:");
    appDrawText(dlgX + 12, dlgY + 32, locationLabel, rgb(220, 220, 230));
    if (!m_saveDialogShowingDrives) appDrawText(dlgX + 80, dlgY + 32, m_saveDialogPath, rgb(200, 220, 255));

    framebuffer::fill_rect(dlgX + 12, dlgY + 54, dlgW - 24, 130, rgb(25, 25, 32));
    const int rowH = 16;
    int rows = 8;
    for (int i = 0; i < rows; ++i) {
        int index = m_saveScroll + i;
        if (index >= m_saveEntryCount) break;
        uint32_t rowY = dlgY + 58 + i * rowH;
        if (index == m_saveSelected) framebuffer::fill_rect(dlgX + 14, rowY - 2, dlgW - 28, rowH, rgb(50, 90, 150));

        const char* typeLabel = m_saveEntries[index].isDrive ? "[DRIVE]" : 
                                 m_saveEntries[index].isDir ? "[DIR]" : "[FILE]";
        appDrawText(dlgX + 18, rowY, typeLabel, rgb(210, 210, 120));
        appDrawText(dlgX + 70, rowY, m_saveEntries[index].name, rgb(235, 235, 240));
    }
    if (m_saveEntryCount == 0) {
        const char* emptyMsg = m_saveDialogIsOpenMode ? "No drives, folders, or files found." : "No drives or folders found.";
        appDrawText(dlgX + 18, dlgY + 64, emptyMsg, rgb(240, 180, 120));
    }

    appDrawText(dlgX + 12, dlgY + 196, "File name:", rgb(220, 220, 230));
    framebuffer::fill_rect(dlgX + 86, dlgY + 190, dlgW - 110, 22, m_saveDialogFilenameFocused ? rgb(18, 28, 48) : rgb(20, 20, 28));
    if (m_saveDialogFilenameFocused) {
        framebuffer::fill_rect(dlgX + 86, dlgY + 190, dlgW - 110, 1, rgb(90, 140, 220));
        framebuffer::fill_rect(dlgX + 86, dlgY + 211, dlgW - 110, 1, rgb(90, 140, 220));
        framebuffer::fill_rect(dlgX + 86, dlgY + 190, 1, 22, rgb(90, 140, 220));
        framebuffer::fill_rect(dlgX + dlgW - 25, dlgY + 190, 1, 22, rgb(90, 140, 220));
    }
    appDrawText(dlgX + 92, dlgY + 197, m_saveDialogFilename, rgb(255, 255, 255));
    if (m_saveDialogFilenameFocused) {
        int len = strlen_local(m_saveDialogFilename);
        int caretX = dlgX + 92 + len * (kGlyphW + kGlyphSpacing);
        uint32_t rightLimit = dlgX + dlgW - 28;
        if ((uint32_t)caretX > rightLimit) caretX = rightLimit;
        framebuffer::fill_rect(caretX, dlgY + 196, 1, kGlyphH + 3, rgb(255, 255, 255));
    }

    framebuffer::fill_rect(dlgX + 12, dlgY + 226, 70, 24, rgb(65, 75, 95));
    appDrawText(dlgX + 28, dlgY + 234, "Drives", rgb(255, 255, 255));
    framebuffer::fill_rect(dlgX + 90, dlgY + 226, 55, 24, rgb(65, 75, 95));
    appDrawText(dlgX + 110, dlgY + 234, "Up", rgb(255, 255, 255));

    const char* actionButtonText = m_saveDialogIsOpenMode ? "Open" : "Save";
    framebuffer::fill_rect(dlgX + dlgW - 170, dlgY + 226, 70, 24, rgb(50, 110, 70));
    appDrawText(dlgX + dlgW - 147, dlgY + 234, actionButtonText, rgb(255, 255, 255));
    framebuffer::fill_rect(dlgX + dlgW - 90, dlgY + 226, 70, 24, rgb(110, 65, 65));
    appDrawText(dlgX + dlgW - 72, dlgY + 234, "Cancel", rgb(255, 255, 255));

    appDrawText(dlgX + 12, dlgY + 260, m_saveDialogStatus, rgb(210, 210, 210));
}

void NotepadApp::navigateSaveDialog(const char* path) {
    if (!path || path[0] == '\0') return;
    strcopy(m_saveDialogPath, path, MAX_PATH_LEN);
    m_saveDialogShowingDrives = false;
    m_saveSelected = 0;
    m_saveScroll = 0;
    refreshSaveDialog();
}

void NotepadApp::saveDialogGoUp() {
    if (m_saveDialogShowingDrives || m_saveDialogPath[0] == '\0' || (m_saveDialogPath[0] == '/' && m_saveDialogPath[1] == '\0')) {
        m_saveDialogShowingDrives = true;
        m_saveDialogPath[0] = '\0';
        refreshSaveDialog();
        return;
    }
    char parent[MAX_PATH_LEN];
    vfs::parent_path(m_saveDialogPath, parent, sizeof(parent));
    navigateSaveDialog(parent);
}

void NotepadApp::buildSavePath(char* out, int outSize) const {
    if (!out || outSize <= 0) return;
    int pos = 0;
    const char* path = m_saveDialogPath;
    while (*path && pos < outSize - 1) out[pos++] = *path++;
    if (pos > 0 && out[pos - 1] != '/' && pos < outSize - 1) out[pos++] = '/';
    const char* name = m_saveDialogFilename;
    bool hasDot = false;
    while (*name && pos < outSize - 1) {
        if (*name == '.') hasDot = true;
        out[pos++] = *name++;
    }
    if (!hasDot) {
        const char* ext = ".txt";
        while (*ext && pos < outSize - 1) out[pos++] = *ext++;
    }
    out[pos] = '\0';
}

bool NotepadApp::saveToDialogTarget() {
    if (m_saveDialogShowingDrives || m_saveDialogPath[0] == '\0') {
        strcopy(m_saveDialogStatus, "Select a drive or folder first.", sizeof(m_saveDialogStatus));
        return false;
    }
    char fullPath[MAX_PATH_LEN];
    buildSavePath(fullPath, sizeof(fullPath));
    if (!saveFileAs(fullPath)) {
        strcopy(m_saveDialogStatus, "Save failed. Use an 8.3 name like NOTE.TXT.", sizeof(m_saveDialogStatus));
        return false;
    }
    m_showSaveDialog = false;
    return true;
}

void NotepadApp::handleSaveDialogChar(char c) {
    if (!m_saveDialogFilenameFocused) return;
    if (c < 32 || c >= 127) return;

    int len = strlen_local(m_saveDialogFilename);
    if (len >= MAX_SAVE_FILENAME - 1) return;

    if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
        strcopy(m_saveDialogStatus, "Filename cannot contain / \\ : * ? \" < > |", sizeof(m_saveDialogStatus));
        return;
    }

    m_saveDialogFilename[len] = c;
    m_saveDialogFilename[len + 1] = '\0';
    strcopy(m_saveDialogStatus, "Type a filename, pick a folder, then Save.", sizeof(m_saveDialogStatus));
}

void NotepadApp::handleSaveDialogKey(uint32_t key) {
    if (key == 27) {
        m_showSaveDialog = false;
        return;
    }

    if (key == '\t' || key == shell::KEY_TAB) {
        m_saveDialogFilenameFocused = !m_saveDialogFilenameFocused;
        return;
    }

    if (m_saveDialogFilenameFocused) {
        if (key == '\b') {
            int len = strlen_local(m_saveDialogFilename);
            if (len > 0) m_saveDialogFilename[len - 1] = '\0';
            return;
        }
        if (key == '\n' || key == '\r') {
            saveToDialogTarget();
            return;
        }
        if (key == shell::KEY_UP || key == shell::KEY_DOWN) {
            m_saveDialogFilenameFocused = false;
            return;
        }
        return;
    }

    switch (key) {
        case shell::KEY_UP:
            if (m_saveSelected > 0) m_saveSelected--;
            break;
        case shell::KEY_DOWN:
            if (m_saveSelected < m_saveEntryCount - 1) m_saveSelected++;
            break;
        case '\b':
            saveDialogGoUp();
            break;
        case '\n':
        case '\r':
            if (m_saveSelected >= 0 && m_saveSelected < m_saveEntryCount) {
                if (m_saveEntries[m_saveSelected].isDrive) {
                    navigateSaveDialog(m_saveEntries[m_saveSelected].name);
                } else if (m_saveEntries[m_saveSelected].isDir) {
                    char child[MAX_PATH_LEN];
                    vfs::join_path(m_saveDialogPath, m_saveEntries[m_saveSelected].name, child, sizeof(child));
                    navigateSaveDialog(child);
                } else if (m_saveDialogIsOpenMode && m_saveEntries[m_saveSelected].isFile) {
                    // Open the selected file
                    char fullPath[MAX_PATH_LEN];
                    vfs::join_path(m_saveDialogPath, m_saveEntries[m_saveSelected].name, fullPath, sizeof(fullPath));
                    if (loadFile(fullPath)) {
                        m_showSaveDialog = false;
                    }
                }
            }
            break;
        default:
            break;
    }
}

bool NotepadApp::handleSaveDialogClick(int x, int y) {
    if (!m_window) return false;
    int w = m_window->w;
    int h = m_window->h - 24;
    int dlgW = w > 440 ? 420 : w - 20;
    int dlgH = h > 300 ? 280 : h - 20;
    int dlgX = (w - dlgW) / 2;
    int dlgY = (h - dlgH) / 2;

    if (x < dlgX || x >= dlgX + dlgW || y < dlgY || y >= dlgY + dlgH) return true;

    if (y >= dlgY + 58 && y < dlgY + 58 + 8 * 16 && x >= dlgX + 12 && x < dlgX + dlgW - 12) {
        m_saveDialogFilenameFocused = false;
        int row = (y - (dlgY + 58)) / 16;
        int index = m_saveScroll + row;
        if (index >= 0 && index < m_saveEntryCount) {
            m_saveSelected = index;
            if (m_saveEntries[index].isDrive) {
                navigateSaveDialog(m_saveEntries[index].name);
            } else if (m_saveEntries[index].isDir) {
                char child[MAX_PATH_LEN];
                vfs::join_path(m_saveDialogPath, m_saveEntries[index].name, child, sizeof(child));
                navigateSaveDialog(child);
            } else if (m_saveDialogIsOpenMode && m_saveEntries[index].isFile) {
                // In open mode, populate filename field with clicked file
                strcopy(m_saveDialogFilename, m_saveEntries[index].name, MAX_SAVE_FILENAME);
                strcopy(m_saveDialogStatus, "Click Open to open this file.", sizeof(m_saveDialogStatus));
            }
        }
        return true;
    }

    if (y >= dlgY + 190 && y < dlgY + 212 && x >= dlgX + 86 && x < dlgX + dlgW - 24) {
        m_saveDialogFilenameFocused = true;
        const char* statusMsg = m_saveDialogIsOpenMode ? 
            "Type a filename or select from list." : 
            "Type a filename, pick a folder, then Save.";
        strcopy(m_saveDialogStatus, statusMsg, sizeof(m_saveDialogStatus));
        return true;
    }

    if (y >= dlgY + 226 && y < dlgY + 250) {
        if (x >= dlgX + 12 && x < dlgX + 82) {
            m_saveDialogFilenameFocused = false;
            m_saveDialogShowingDrives = true;
            m_saveDialogPath[0] = '\0';
            refreshSaveDialog();
            return true;
        }
        if (x >= dlgX + 90 && x < dlgX + 145) {
            m_saveDialogFilenameFocused = false;
            saveDialogGoUp();
            return true;
        }
        if (x >= dlgX + dlgW - 170 && x < dlgX + dlgW - 100) {
            if (m_saveDialogIsOpenMode) {
                // Open mode: load the file
                if (m_saveDialogFilename[0] != '\0' && m_saveDialogPath[0] != '\0') {
                    char fullPath[MAX_PATH_LEN];
                    vfs::join_path(m_saveDialogPath, m_saveDialogFilename, fullPath, sizeof(fullPath));
                    if (loadFile(fullPath)) {
                        m_showSaveDialog = false;
                    } else {
                        strcopy(m_saveDialogStatus, "Failed to open file.", sizeof(m_saveDialogStatus));
                    }
                } else {
                    strcopy(m_saveDialogStatus, "Select a file or enter a filename.", sizeof(m_saveDialogStatus));
                }
            } else {
                // Save mode
                saveToDialogTarget();
            }
            return true;
        }
        if (x >= dlgX + dlgW - 90 && x < dlgX + dlgW - 20) {
            m_showSaveDialog = false;
            return true;
        }
    }

    return true;
}

void NotepadApp::updateTitle() {
    char title[app::MAX_TITLE_LEN];
    const char* filename = m_filePath[0] != '\0' ? m_filePath : "Untitled";
    
    // Build title: "filename - Notepad" or "*filename - Notepad" if modified
    int pos = 0;
    if (m_modified && pos < app::MAX_TITLE_LEN - 1) {
        title[pos++] = '*';
    }
    
    int i = 0;
    while (filename[i] && pos < app::MAX_TITLE_LEN - 12) {
        title[pos++] = filename[i++];
    }
    
    const char* suffix = " - Notepad";
    i = 0;
    while (suffix[i] && pos < app::MAX_TITLE_LEN - 1) {
        title[pos++] = suffix[i++];
    }
    title[pos] = '\0';
    
    strcopy(m_window->title, title, app::MAX_TITLE_LEN);
}

// Text editing operations
void NotepadApp::backspace() {
    deleteChar();
}

void NotepadApp::selectAll() {
    m_selectAll = true;
}

void NotepadApp::cut() {
    copy();
    if (m_selectAll && m_textLength > 0) {
        clearText();
        m_selectAll = false;
        m_modified = true;
    }
}

void NotepadApp::copy() {
    if (m_selectAll && m_textLength > 0) {
        int copyLen = m_textLength < MAX_TEXT_LENGTH - 1 ? m_textLength : MAX_TEXT_LENGTH - 1;
        for (int i = 0; i < copyLen; i++) {
            s_clipboard[i] = m_text[i];
        }
        s_clipboard[copyLen] = '\0';
        s_clipboardLength = copyLen;
    }
}

void NotepadApp::paste() {
    if (s_clipboardLength == 0) return;
    
    if (m_selectAll) {
        clearText();
        m_selectAll = false;
    }
    
    // Insert clipboard contents at cursor
    for (int i = 0; i < s_clipboardLength && m_textLength < MAX_TEXT_LENGTH - 1; i++) {
        insertChar(s_clipboard[i]);
    }
    m_modified = true;
}

// Menu and UI drawing
void NotepadApp::drawMenuBar(uint32_t x, uint32_t y, uint32_t w) {
    // Draw menu bar background
    framebuffer::fill_rect(x, y, w, MENU_BAR_HEIGHT, rgb(50, 50, 60));
    
    // Draw bottom separator line
    framebuffer::fill_rect(x, y + MENU_BAR_HEIGHT - 1, w, 1, rgb(70, 70, 80));
    
    // File menu item
    uint32_t fileX = x + 4;
    uint32_t fileW = 40;
    if (m_showFileMenu || (m_hoveredMenuType == 1 && m_hoveredMenuItem == -2)) {
        framebuffer::fill_rect(fileX, y + 2, fileW, MENU_BAR_HEIGHT - 4, rgb(70, 100, 150));
    }
    drawChar(fileX + 4, y + 6, 'F', rgb(220, 220, 230));
    drawChar(fileX + 10, y + 6, 'i', rgb(220, 220, 230));
    drawChar(fileX + 16, y + 6, 'l', rgb(220, 220, 230));
    drawChar(fileX + 22, y + 6, 'e', rgb(220, 220, 230));
    
    // Edit menu item
    uint32_t editX = fileX + fileW + 4;
    uint32_t editW = 40;
    if (m_showEditMenu || (m_hoveredMenuType == 2 && m_hoveredMenuItem == -2)) {
        framebuffer::fill_rect(editX, y + 2, editW, MENU_BAR_HEIGHT - 4, rgb(70, 100, 150));
    }
    drawChar(editX + 4, y + 6, 'E', rgb(220, 220, 230));
    drawChar(editX + 10, y + 6, 'd', rgb(220, 220, 230));
    drawChar(editX + 16, y + 6, 'i', rgb(220, 220, 230));
    drawChar(editX + 22, y + 6, 't', rgb(220, 220, 230));
}

void NotepadApp::drawFileMenu(uint32_t x, uint32_t y) {
    const char* items[] = {"New", "Open", "Save", "Save As", "Exit"};
    const int itemCount = 5;
    const int menuW = 120;
    const int itemH = 22;

    // Menu background
    framebuffer::fill_rect(x, y, menuW, itemCount * itemH + 2, rgb(240, 240, 245));

    // Border
    framebuffer::fill_rect(x, y, menuW, 1, rgb(160, 160, 170)); // Top
    framebuffer::fill_rect(x, y + itemCount * itemH + 1, menuW, 1, rgb(160, 160, 170)); // Bottom
    framebuffer::fill_rect(x, y, 1, itemCount * itemH + 2, rgb(160, 160, 170)); // Left
    framebuffer::fill_rect(x + menuW - 1, y, 1, itemCount * itemH + 2, rgb(160, 160, 170)); // Right

    for (int i = 0; i < itemCount; i++) {
        uint32_t itemY = y + 1 + i * itemH;

        if (m_hoveredMenuType == 1 && m_hoveredMenuItem == i) {
            framebuffer::fill_rect(x + 1, itemY, menuW - 2, itemH, rgb(45, 95, 180));
        }

        // Item text
        uint32_t textColor = (m_hoveredMenuType == 1 && m_hoveredMenuItem == i) ? rgb(255, 255, 255) : rgb(0, 0, 0);
        for (int j = 0; items[i][j]; j++) {
            drawChar(x + 8 + j * 6, itemY + 7, items[i][j], textColor);
        }
    }
}

void NotepadApp::drawEditMenu(uint32_t x, uint32_t y) {
    const char* items[] = {"Cut      Ctrl+X", "Copy     Ctrl+C", "Paste    Ctrl+V", "Select All  Ctrl+A"};
    const int itemCount = 4;
    const int menuW = 160;
    const int itemH = 22;
    
    // Menu background
    framebuffer::fill_rect(x, y, menuW, itemCount * itemH + 2, rgb(240, 240, 245));
    
    // Border
    framebuffer::fill_rect(x, y, menuW, 1, rgb(160, 160, 170));
    framebuffer::fill_rect(x, y + itemCount * itemH + 1, menuW, 1, rgb(160, 160, 170));
    framebuffer::fill_rect(x, y, 1, itemCount * itemH + 2, rgb(160, 160, 170));
    framebuffer::fill_rect(x + menuW - 1, y, 1, itemCount * itemH + 2, rgb(160, 160, 170));
    
    for (int i = 0; i < itemCount; i++) {
        uint32_t itemY = y + 1 + i * itemH;

        if (m_hoveredMenuType == 2 && m_hoveredMenuItem == i) {
            framebuffer::fill_rect(x + 1, itemY, menuW - 2, itemH, rgb(45, 95, 180));
        }
        
        // Item text
        uint32_t textColor = (m_hoveredMenuType == 2 && m_hoveredMenuItem == i) ? rgb(255, 255, 255) : rgb(0, 0, 0);
        for (int j = 0; items[i][j]; j++) {
            drawChar(x + 8 + j * 6, itemY + 7, items[i][j], textColor);
        }
    }
}

void NotepadApp::drawContextMenu(uint32_t x, uint32_t y) {
    const char* items[] = {"Cut", "Copy", "Paste", "Select All"};
    const int itemCount = 4;
    const int menuW = 130;
    const int itemH = 22;
    
    // Menu background
    framebuffer::fill_rect(x, y, menuW, itemCount * itemH + 2, rgb(240, 240, 245));
    
    // Border with shadow effect
    framebuffer::fill_rect(x, y, menuW, 1, rgb(160, 160, 170));
    framebuffer::fill_rect(x, y + itemCount * itemH + 1, menuW, 1, rgb(160, 160, 170));
    framebuffer::fill_rect(x, y, 1, itemCount * itemH + 2, rgb(160, 160, 170));
    framebuffer::fill_rect(x + menuW - 1, y, 1, itemCount * itemH + 2, rgb(160, 160, 170));
    
    // Shadow
    framebuffer::fill_rect(x + 2, y + itemCount * itemH + 2, menuW, 2, rgb(100, 100, 110));
    framebuffer::fill_rect(x + menuW, y + 2, 2, itemCount * itemH, rgb(100, 100, 110));
    
    for (int i = 0; i < itemCount; i++) {
        uint32_t itemY = y + 1 + i * itemH;

        if (m_hoveredMenuType == 3 && m_hoveredMenuItem == i) {
            framebuffer::fill_rect(x + 1, itemY, menuW - 2, itemH, rgb(45, 95, 180));
        }
        
        // Item text
        uint32_t textColor = (m_hoveredMenuType == 3 && m_hoveredMenuItem == i) ? rgb(255, 255, 255) : rgb(0, 0, 0);
        for (int j = 0; items[i][j]; j++) {
            drawChar(x + 8 + j * 6, itemY + 7, items[i][j], textColor);
        }
    }
}

bool NotepadApp::handleMenuClick(int x, int y) {
    const int fileX = 4;
    const int fileW = 40;
    const int editX = 48;
    const int editW = 40;
    
    // Click on menu bar
    if (y < MENU_BAR_HEIGHT) {
        // File menu toggle
        if (x >= fileX && x < fileX + fileW) {
            m_showFileMenu = !m_showFileMenu;
            m_showEditMenu = false;
            m_hoveredMenuItem = -1;
            m_hoveredMenuType = 0;
            return true;
        }
        // Edit menu toggle
        if (x >= editX && x < editX + editW) {
            m_showEditMenu = !m_showEditMenu;
            m_showFileMenu = false;
            m_hoveredMenuItem = -1;
            m_hoveredMenuType = 0;
            return true;
        }
    }
    
    // Handle File menu dropdown item clicks
    if (m_showFileMenu) {
        const int menuW = 120;
        const int itemH = 22;
        const int menuX = fileX;
        const int menuY = MENU_BAR_HEIGHT;

        if (x >= menuX && x < menuX + menuW && 
            y >= menuY && y < menuY + 5 * itemH + 2) {
            int item = (y - menuY - 1) / itemH;
            if (item >= 0 && item < 5) {
                m_showFileMenu = false;
                m_hoveredMenuItem = -1;
                m_hoveredMenuType = 0;
                switch (item) {
                    case 0: newFile(); break;
                    case 1: openOpenFileDialog(); break;
                    case 2: saveFile(); break;
                    case 3: openSaveAsDialog(); break;
                    case 4: requestClose(); break;
                }
                return true;
            }
        }
    }
    
    // Handle Edit menu dropdown item clicks
    if (m_showEditMenu) {
        const int menuW = 160;
        const int itemH = 22;
        const int menuX = editX;
        const int menuY = MENU_BAR_HEIGHT;
        
        if (x >= menuX && x < menuX + menuW && 
            y >= menuY && y < menuY + 4 * itemH + 2) {
            int item = (y - menuY - 1) / itemH;
            if (item >= 0 && item < 4) {
                m_showEditMenu = false;
                m_hoveredMenuItem = -1;
                m_hoveredMenuType = 0;
                switch (item) {
                    case 0: cut(); break;
                    case 1: copy(); break;
                    case 2: paste(); break;
                    case 3: selectAll(); break;
                }
                return true;
            }
        }
    }
    
    return false;
}

bool NotepadApp::handleContextMenuClick(int x, int y) {
    const int menuW = 130;
    const int itemH = 22;
    const int itemCount = 4;
    
    // Check if click is within context menu bounds
    if (x >= m_contextMenuX && x < m_contextMenuX + menuW &&
        y >= m_contextMenuY && y < m_contextMenuY + itemCount * itemH + 2) {
        
        int item = (y - m_contextMenuY - 1) / itemH;
        if (item >= 0 && item < itemCount) {
            switch (item) {
                case 0: cut(); break;
                case 1: copy(); break;
                case 2: paste(); break;
                case 3: selectAll(); break;
            }
            m_hoveredMenuItem = -1;
            m_hoveredMenuType = 0;
            return true;
        }
    }
    return false;
}

bool NotepadApp::updateMenuHover(int x, int y) {
    int newType = 0;
    int newItem = -1;

    const int fileX = 4;
    const int fileW = 40;
    const int editX = 48;
    const int editW = 40;

    if (y >= 0 && y < MENU_BAR_HEIGHT) {
        if (x >= fileX && x < fileX + fileW) {
            newType = 1;
            newItem = -2;
        } else if (x >= editX && x < editX + editW) {
            newType = 2;
            newItem = -2;
        }
    }

    if (m_showFileMenu) {
        const int menuX = fileX;
        const int menuY = MENU_BAR_HEIGHT;
        const int menuW = 120;
        const int itemH = 22;
        const int itemCount = 5;
        if (x >= menuX && x < menuX + menuW && y >= menuY + 1 && y < menuY + 1 + itemCount * itemH) {
            newType = 1;
            newItem = (y - menuY - 1) / itemH;
        }
    }

    if (m_showEditMenu) {
        const int menuX = editX;
        const int menuY = MENU_BAR_HEIGHT;
        const int menuW = 160;
        const int itemH = 22;
        const int itemCount = 4;
        if (x >= menuX && x < menuX + menuW && y >= menuY + 1 && y < menuY + 1 + itemCount * itemH) {
            newType = 2;
            newItem = (y - menuY - 1) / itemH;
        }
    }

    if (m_showContextMenu) {
        const int menuW = 130;
        const int itemH = 22;
        const int itemCount = 4;
        if (x >= m_contextMenuX && x < m_contextMenuX + menuW &&
            y >= m_contextMenuY + 1 && y < m_contextMenuY + 1 + itemCount * itemH) {
            newType = 3;
            newItem = (y - m_contextMenuY - 1) / itemH;
        }
    }

    if (newType != m_hoveredMenuType || newItem != m_hoveredMenuItem) {
        m_hoveredMenuType = newType;
        m_hoveredMenuItem = newItem;
        return true;
    }

    return false;
}

// ============================================================
// CalculatorApp Implementation
// ============================================================

CalculatorApp::CalculatorApp() 
    : m_accumulator(0), m_operand(0), m_operation('\0'), m_newNumber(true), m_displayId(-1) {
    strcopy(m_name, "Calculator", app::MAX_APP_NAME);
    m_display[0] = '0';
    m_display[1] = '\0';
    for (int i = 0; i < 20; i++) m_btnIds[i] = -1;
}

CalculatorApp::~CalculatorApp() {
}

bool CalculatorApp::init() {
    m_window = new app::KernelWindow();
    strcopy(m_window->title, "Calculator", app::MAX_TITLE_LEN);
    m_window->x = 200;
    m_window->y = 80;
    m_window->w = 220;
    m_window->h = 280;
    m_window->flags = app::WF_VISIBLE | app::WF_TITLEBAR | app::WF_CLOSABLE | app::WF_FOCUSED;
    m_window->owner = this;
    
    if (!compositor::KernelCompositor::registerWindow(m_window)) {
        delete m_window;
        m_window = nullptr;
        return false;
    }
    
    // Create display
    m_displayId = addLabel(10, 10, 200, 30, "0");
    
    // Create buttons (4x5 grid)
    const char* btnLabels[] = {
        "C", "CE", "%", "/",
        "7", "8", "9", "*",
        "4", "5", "6", "-",
        "1", "2", "3", "+",
        "+/-", "0", ".", "="
    };
    
    int btnW = 45;
    int btnH = 35;
    int startY = 50;
    int gap = 5;
    
    for (int row = 0; row < 5; row++) {
        for (int col = 0; col < 4; col++) {
            int idx = row * 4 + col;
            int bx = 10 + col * (btnW + gap);
            int by = startY + row * (btnH + gap);
            m_btnIds[idx] = addButton(bx, by, btnW, btnH, btnLabels[idx]);
        }
    }
    
    m_state = app::AppState::Running;
    return true;
}

void CalculatorApp::shutdown() {
    m_state = app::AppState::Terminated;
}

void CalculatorApp::draw(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    // Display background
    framebuffer::fill_rect(x + 10, y + 10, w - 20, 30, rgb(30, 35, 45));
    
    // Display border
    for (uint32_t i = 0; i < w - 20; i++) {
        framebuffer::put_pixel(x + 10 + i, y + 10, rgb(60, 70, 90));
        framebuffer::put_pixel(x + 10 + i, y + 39, rgb(60, 70, 90));
    }
    for (uint32_t i = 0; i < 30; i++) {
        framebuffer::put_pixel(x + 10, y + 10 + i, rgb(60, 70, 90));
        framebuffer::put_pixel(x + w - 11, y + 10 + i, rgb(60, 70, 90));
    }
    
    // Display text (right-aligned)
    int dispLen = strlen_local(m_display);
    uint32_t textW = dispLen * (kGlyphW + kGlyphSpacing);
    uint32_t textX = x + w - 15 - textW;
    uint32_t textY = y + 10 + (30 - kGlyphH) / 2;
    
    // Draw display digits
    for (int i = 0; i < dispLen; i++) {
        char c = m_display[i];
        uint32_t cx = textX + i * (kGlyphW + kGlyphSpacing);
        
        // Simple digit rendering
        if (c >= '0' && c <= '9') {
            for (int dy = 0; dy < kGlyphH; dy++) {
                for (int dx = 0; dx < kGlyphW; dx++) {
                    bool on = ((c - '0' + dx + dy) % 2 == 0);
                    if (on) {
                        framebuffer::put_pixel(cx + dx, textY + dy, rgb(200, 220, 255));
                    }
                }
            }
        } else if (c == '.') {
            framebuffer::put_pixel(cx + 2, textY + kGlyphH - 1, rgb(200, 220, 255));
            framebuffer::put_pixel(cx + 2, textY + kGlyphH - 2, rgb(200, 220, 255));
        } else if (c == '-') {
            for (int dx = 0; dx < kGlyphW; dx++) {
                framebuffer::put_pixel(cx + dx, textY + kGlyphH / 2, rgb(200, 220, 255));
            }
        }
    }
    
    (void)h;
}

void CalculatorApp::onWidgetClick(int widgetId) {
    const char* btnChars = "Cce/%789*456-123++/-0.=";
    
    for (int i = 0; i < 20; i++) {
        if (m_btnIds[i] == widgetId) {
            if (i == 0) handleButton('C');
            else if (i == 1) handleButton('E');  // CE
            else if (i == 2) handleButton('%');
            else if (i == 3) handleButton('/');
            else if (i >= 4 && i <= 6) handleButton('7' + (i - 4));
            else if (i == 7) handleButton('*');
            else if (i >= 8 && i <= 10) handleButton('4' + (i - 8));
            else if (i == 11) handleButton('-');
            else if (i >= 12 && i <= 14) handleButton('1' + (i - 12));
            else if (i == 15) handleButton('+');
            else if (i == 16) handleButton('N');  // +/-
            else if (i == 17) handleButton('0');
            else if (i == 18) handleButton('.');
            else if (i == 19) handleButton('=');
            break;
        }
    }
}

void CalculatorApp::onKeyChar(char c) {
    if ((c >= '0' && c <= '9') || c == '.' || c == '+' || c == '-' ||
        c == '*' || c == '/' || c == '=' || c == '\r' || c == '\n' ||
        c == 'c' || c == 'C') {
        if (c == '\r' || c == '\n') c = '=';
        handleButton(c);
    }
}

void CalculatorApp::handleButton(char btn) {
    if (btn >= '0' && btn <= '9') {
        if (m_newNumber) {
            m_display[0] = btn;
            m_display[1] = '\0';
            m_newNumber = false;
        } else {
            int len = strlen_local(m_display);
            if (len < 15) {
                m_display[len] = btn;
                m_display[len + 1] = '\0';
            }
        }
    } else if (btn == '.') {
        // Check if already has decimal point
        bool hasDot = false;
        for (int i = 0; m_display[i]; i++) {
            if (m_display[i] == '.') hasDot = true;
        }
        if (!hasDot) {
            int len = strlen_local(m_display);
            if (len < 15) {
                m_display[len] = '.';
                m_display[len + 1] = '\0';
            }
        }
        m_newNumber = false;
    } else if (btn == '+' || btn == '-' || btn == '*' || btn == '/') {
        // Parse current display
        double val = 0;
        double frac = 0;
        bool negative = false;
        bool afterDot = false;
        double fracDiv = 10;
        
        for (int i = 0; m_display[i]; i++) {
            char c = m_display[i];
            if (c == '-' && i == 0) negative = true;
            else if (c == '.') afterDot = true;
            else if (c >= '0' && c <= '9') {
                if (afterDot) {
                    frac += (c - '0') / fracDiv;
                    fracDiv *= 10;
                } else {
                    val = val * 10 + (c - '0');
                }
            }
        }
        val += frac;
        if (negative) val = -val;
        
        if (m_operation != '\0') {
            m_operand = val;
            calculate();
        } else {
            m_accumulator = val;
        }
        
        m_operation = btn;
        m_newNumber = true;
    } else if (btn == '=') {
        // Parse and calculate
        double val = 0;
        double frac = 0;
        bool negative = false;
        bool afterDot = false;
        double fracDiv = 10;
        
        for (int i = 0; m_display[i]; i++) {
            char c = m_display[i];
            if (c == '-' && i == 0) negative = true;
            else if (c == '.') afterDot = true;
            else if (c >= '0' && c <= '9') {
                if (afterDot) {
                    frac += (c - '0') / fracDiv;
                    fracDiv *= 10;
                } else {
                    val = val * 10 + (c - '0');
                }
            }
        }
        val += frac;
        if (negative) val = -val;
        
        m_operand = val;
        calculate();
        m_operation = '\0';
        m_newNumber = true;
    } else if (btn == 'C') {
        clear();
    } else if (btn == 'E') {
        clearEntry();
    } else if (btn == 'N') {
        // Negate
        if (m_display[0] == '-') {
            for (int i = 0; m_display[i]; i++) {
                m_display[i] = m_display[i + 1];
            }
        } else {
            int len = strlen_local(m_display);
            for (int i = len; i >= 0; i--) {
                m_display[i + 1] = m_display[i];
            }
            m_display[0] = '-';
        }
    }
    
    updateDisplay();
    invalidate();
}

void CalculatorApp::updateDisplay() {
    setWidgetText(m_displayId, m_display);
}

void CalculatorApp::calculate() {
    switch (m_operation) {
        case '+': m_accumulator = m_accumulator + m_operand; break;
        case '-': m_accumulator = m_accumulator - m_operand; break;
        case '*': m_accumulator = m_accumulator * m_operand; break;
        case '/': 
            if (m_operand != 0) {
                m_accumulator = m_accumulator / m_operand;
            } else {
                strcopy(m_display, "Error", 32);
                return;
            }
            break;
    }
    
    // Convert result to string
    int intPart = (int)m_accumulator;
    double fracPart = m_accumulator - intPart;
    if (fracPart < 0) fracPart = -fracPart;
    
    int idx = 0;
    if (m_accumulator < 0) {
        m_display[idx++] = '-';
        intPart = -intPart;
    }
    
    // Integer part
    if (intPart == 0) {
        m_display[idx++] = '0';
    } else {
        char temp[16];
        int ti = 0;
        while (intPart > 0) {
            temp[ti++] = '0' + (intPart % 10);
            intPart /= 10;
        }
        while (ti > 0) {
            m_display[idx++] = temp[--ti];
        }
    }
    
    // Fractional part (up to 6 digits)
    if (fracPart > 0.0000001) {
        m_display[idx++] = '.';
        for (int i = 0; i < 6 && fracPart > 0.0000001; i++) {
            fracPart *= 10;
            int digit = (int)fracPart;
            m_display[idx++] = '0' + digit;
            fracPart -= digit;
        }
    }
    
    m_display[idx] = '\0';
}

void CalculatorApp::clear() {
    m_accumulator = 0;
    m_operand = 0;
    m_operation = '\0';
    m_newNumber = true;
    m_display[0] = '0';
    m_display[1] = '\0';
}

void CalculatorApp::clearEntry() {
    m_display[0] = '0';
    m_display[1] = '\0';
    m_newNumber = true;
}

// ============================================================
// TaskManagerApp Implementation
// ============================================================

TaskManagerApp::TaskManagerApp() 
    : m_selectedApp(-1), m_refreshBtnId(-1), m_endTaskBtnId(-1), 
      m_lastUpdate(0), m_entryCount(0) {
    strcopy(m_name, "TaskManager", app::MAX_APP_NAME);
}

TaskManagerApp::~TaskManagerApp() {
}

bool TaskManagerApp::init() {
    m_window = new app::KernelWindow();
    strcopy(m_window->title, "Task Manager", app::MAX_TITLE_LEN);
    m_window->x = 150;
    m_window->y = 60;
    m_window->w = 350;
    m_window->h = 300;
    m_window->flags = app::WF_VISIBLE | app::WF_TITLEBAR | app::WF_CLOSABLE | app::WF_RESIZABLE | app::WF_FOCUSED;
    m_window->owner = this;
    
    if (!compositor::KernelCompositor::registerWindow(m_window)) {
        delete m_window;
        m_window = nullptr;
        return false;
    }
    
    // Create buttons
    m_refreshBtnId = addButton(10, 240, 80, 28, "Refresh");
    m_endTaskBtnId = addButton(100, 240, 80, 28, "End Task");
    
    refreshList();
    
    m_state = app::AppState::Running;
    return true;
}

void TaskManagerApp::shutdown() {
    m_state = app::AppState::Terminated;
}

void TaskManagerApp::update() {
    // Auto-refresh every 100 ticks
    m_lastUpdate++;
    if (m_lastUpdate >= 100) {
        refreshList();
        m_lastUpdate = 0;
        invalidate();
    }
}

void TaskManagerApp::draw(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    // Header
    framebuffer::fill_rect(x + 10, y + 10, w - 20, 24, rgb(40, 50, 65));
    
    // Column headers
    uint32_t headerY = y + 10 + (24 - kGlyphH) / 2;
    appDrawText(x + 15, headerY, "Application", rgb(220, 225, 240));
    appDrawText(x + w - 95, headerY, "Status", rgb(220, 225, 240));
    
    // List background
    uint32_t listY = y + 40;
    uint32_t listH = h - 90;
    framebuffer::fill_rect(x + 10, listY, w - 20, listH, rgb(30, 30, 38));
    
    // Draw entries
    uint32_t rowH = 24;
    for (int i = 0; i < m_entryCount && (uint32_t)i * rowH < listH - rowH; i++) {
        uint32_t rowY = listY + i * rowH;
        
        // Selection highlight
        if (i == m_selectedApp) {
            framebuffer::fill_rect(x + 11, rowY + 1, w - 22, rowH - 2, rgb(50, 70, 100));
        } else if (i % 2 == 0) {
            framebuffer::fill_rect(x + 11, rowY + 1, w - 22, rowH - 2, rgb(35, 35, 43));
        }
        
        // App name
        uint32_t textY = rowY + (rowH - kGlyphH) / 2;
        appDrawText(x + 15, textY, m_entries[i].name, rgb(235, 235, 245));
        
        // Status indicator
        uint32_t statusColor = m_entries[i].running ? rgb(80, 180, 100) : rgb(180, 80, 80);
        framebuffer::fill_rect(x + w - 80, textY, 8, 8, statusColor);
        
        // Status text
        const char* status = m_entries[i].running ? "Running" : "Stopped";
        appDrawText(x + w - 68, textY, status, rgb(210, 215, 225));
    }
}

void TaskManagerApp::onMouseDown(int localX, int localY, uint8_t button) {
    (void)button;
    
    // Check if clicked in list area
    if (localY >= 40 && localY < 240) {
        int row = (localY - 40) / 24;
        if (row >= 0 && row < m_entryCount) {
            m_selectedApp = row;
            invalidate();
        }
    }
}

void TaskManagerApp::onWidgetClick(int widgetId) {
    if (widgetId == m_refreshBtnId) {
        refreshList();
        invalidate();
    } else if (widgetId == m_endTaskBtnId) {
        if (m_selectedApp >= 0 && m_selectedApp < m_entryCount) {
            // Find and close the app
            app::KernelApp* app = app::AppManager::getRunningApp(m_selectedApp);
            if (app && app != this) {  // Don't close self
                app::AppManager::closeApp(app);
                m_selectedApp = -1;
                refreshList();
                invalidate();
            }
        }
    }
}

void TaskManagerApp::refreshList() {
    m_entryCount = 0;
    
    // Add running apps
    int count = app::AppManager::getRunningAppCount();
    for (int i = 0; i < count && m_entryCount < MAX_ENTRIES; i++) {
        app::KernelApp* runApp = app::AppManager::getRunningApp(i);
        if (runApp) {
            strcopy(m_entries[m_entryCount].name, runApp->getName(), app::MAX_APP_NAME);
            m_entries[m_entryCount].running = true;
            m_entries[m_entryCount].windowCount = 1;
            m_entryCount++;
        }
    }
    
    // Add shell if open
    if (shell::is_open() && m_entryCount < MAX_ENTRIES) {
        strcopy(m_entries[m_entryCount].name, "Terminal", app::MAX_APP_NAME);
        m_entries[m_entryCount].running = true;
        m_entries[m_entryCount].windowCount = 1;
        m_entryCount++;
    }
    
    // Validate selection
    if (m_selectedApp >= m_entryCount) {
        m_selectedApp = m_entryCount - 1;
    }
}

// ============================================================
// FileExplorerApp Implementation
// ============================================================

FileExplorerApp::FileExplorerApp()
    : m_entryCount(0), m_selected(0), m_scroll(0),
      m_lastClickIndex(-1), m_lastClickTick(0),
      m_backBtnId(-1), m_upBtnId(-1), m_refreshBtnId(-1), m_rootBtnId(-1),
      m_renameFileBtnId(-1), m_deleteFileBtnId(-1), m_renameFolderBtnId(-1), m_deleteFolderBtnId(-1),
      m_confirmDeleteBtnId(-1), m_cancelDeleteBtnId(-1), m_renamePrompt(false), m_deleteConfirm(false),
      m_deleteTargetIsDir(false) {
    strcopy(m_name, "Files", app::MAX_APP_NAME);
    strcopy(m_currentPath, "/", MAX_PATH_LEN);
    strcopy(m_status, "Ready", sizeof(m_status));
    m_renameValue[0] = '\0';
    m_deleteTarget[0] = '\0';
    m_deleteTargetName[0] = '\0';
}

FileExplorerApp::~FileExplorerApp() {
}

bool FileExplorerApp::init() {
    return initWithParam("/");
}

bool FileExplorerApp::initWithParam(const char* startPath) {
    m_window = new app::KernelWindow();
    strcopy(m_window->title, "File Explorer", app::MAX_TITLE_LEN);
    m_window->x = 80;
    m_window->y = 45;
    m_window->w = 760;
    m_window->h = 460;
    m_window->flags = app::WF_VISIBLE | app::WF_TITLEBAR | app::WF_CLOSABLE | app::WF_RESIZABLE | app::WF_FOCUSED;
    m_window->owner = this;

    if (!compositor::KernelCompositor::registerWindow(m_window)) {
        delete m_window;
        m_window = nullptr;
        return false;
    }

    m_backBtnId = addButton(8, 5, 52, 20, "Root");
    m_upBtnId = addButton(66, 5, 38, 20, "Up");
    m_refreshBtnId = addButton(108, 5, 58, 20, "Refresh");
    m_rootBtnId = addButton(170, 5, 70, 20, "Mounts");
    m_renameFileBtnId = addButton(248, 5, 82, 20, "Rename File");
    m_deleteFileBtnId = addButton(334, 5, 78, 20, "Delete File");
    m_renameFolderBtnId = addButton(248, 5, 92, 20, "Rename Dir");
    m_deleteFolderBtnId = addButton(344, 5, 84, 20, "Delete Dir");
    m_confirmDeleteBtnId = addButton(260, 205, 78, 20, "Delete");
    m_cancelDeleteBtnId = addButton(344, 205, 70, 20, "Cancel");

    strcopy(m_currentPath, startPath && startPath[0] ? startPath : "/", MAX_PATH_LEN);
    refresh();
    updateActionButtons();
    m_state = app::AppState::Running;
    return true;
}

void FileExplorerApp::shutdown() {
    m_state = app::AppState::Terminated;
}

void FileExplorerApp::draw(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    framebuffer::fill_rect(x, y, w, h, rgb(246, 246, 246));

    uint32_t addressY = y + TOOLBAR_H;
    framebuffer::fill_rect(x, addressY, w, ADDRESS_H, rgb(255, 255, 255));
    appDrawText(x + 8, addressY + 7, "Address:", rgb(70, 70, 70));
    appDrawText(x + 62, addressY + 7, m_currentPath, rgb(30, 30, 30));

    uint32_t bodyY = y + TOOLBAR_H + ADDRESS_H;
    uint32_t statusH = 22;
    uint32_t bodyH = h > TOOLBAR_H + ADDRESS_H + statusH ? h - TOOLBAR_H - ADDRESS_H - statusH : 0;

    framebuffer::fill_rect(x, bodyY, LEFT_W, bodyH, rgb(238, 238, 238));
    appDrawText(x + 8, bodyY + 10, "Navigation", rgb(40, 40, 40));
    appDrawText(x + 12, bodyY + 30, "Root", rgb(50, 70, 110));
    appDrawText(x + 12, bodyY + 46, "Mounted drives", rgb(50, 70, 110));

    uint8_t mountCount = vfs::mount_count();
    if (mountCount == 0) {
        appDrawText(x + 18, bodyY + 64, "No mounts", rgb(130, 60, 60));
    } else {
        int row = 0;
        for (uint8_t i = 0; i < vfs::VFS_MAX_MOUNTS && row < 8; ++i) {
            const vfs::MountPoint* mp = vfs::get_mount_by_index(i);
            if (!mp || !mp->active) continue;
            appDrawText(x + 18, bodyY + 64 + row * ROW_H, mp->path, rgb(30, 30, 30));
            row++;
        }
    }

    uint32_t mainX = x + LEFT_W;
    uint32_t mainW = w > LEFT_W ? w - LEFT_W : 0;
    framebuffer::fill_rect(mainX, bodyY, mainW, bodyH, rgb(255, 255, 255));
    framebuffer::fill_rect(mainX, bodyY, mainW, 22, rgb(230, 230, 230));
    appDrawText(mainX + 8, bodyY + 7, "Name", rgb(40, 40, 40));
    appDrawText(mainX + 250, bodyY + 7, "Size", rgb(40, 40, 40));
    appDrawText(mainX + 330, bodyY + 7, "Type", rgb(40, 40, 40));
    appDrawText(mainX + 430, bodyY + 7, "Modified", rgb(40, 40, 40));

    if (m_entryCount == 0) {
        appDrawText(mainX + 8, bodyY + 34, "Empty directory or unavailable path", rgb(120, 120, 120));
    }

    int visibleRows = bodyH > 30 ? (int)((bodyH - 30) / ROW_H) : 0;
    for (int i = 0; i < visibleRows && m_scroll + i < m_entryCount; ++i) {
        int entryIndex = m_scroll + i;
        Entry& e = m_entries[entryIndex];
        uint32_t rowY = bodyY + 24 + i * ROW_H;
        if (entryIndex == m_selected) {
            framebuffer::fill_rect(mainX + 1, rowY - 2, mainW - 2, ROW_H, rgb(200, 220, 245));
        }

        char sizeText[24];
        formatSize(e.size, sizeText, sizeof(sizeText));
        appDrawText(mainX + 8, rowY, e.isDir ? "[DIR]" : "[FILE]", rgb(80, 80, 95));
        appDrawText(mainX + 52, rowY, e.name, rgb(20, 20, 20));
        appDrawText(mainX + 250, rowY, e.isDir ? "" : sizeText, rgb(70, 70, 70));
        appDrawText(mainX + 330, rowY, fileType(e), rgb(70, 70, 70));
        appDrawText(mainX + 430, rowY, "--", rgb(110, 110, 110));
    }

    framebuffer::fill_rect(x, y + h - statusH, w, statusH, rgb(235, 235, 235));
    appDrawText(x + 8, y + h - 15, m_status, rgb(40, 40, 40));

    if (m_renamePrompt) {
        framebuffer::fill_rect(x + 220, y + 165, 360, 92, rgb(245, 245, 250));
        appDrawText(x + 232, y + 182, "Rename selected item", rgb(30, 30, 30));
        appDrawText(x + 232, y + 205, m_renameValue, rgb(20, 20, 20));
        appDrawText(x + 232, y + 230, "Enter=OK  Esc=Cancel  Backspace=Delete", rgb(80, 80, 80));
    } else if (m_deleteConfirm) {
        framebuffer::fill_rect(x + 220, y + 165, 390, 92, rgb(250, 245, 245));
        appDrawText(x + 232, y + 182, m_deleteTargetIsDir ? "Are you sure you wish to delete this folder?" : "Are you sure you wish to delete this file?", rgb(80, 30, 30));
        appDrawText(x + 232, y + 205, m_deleteTargetName, rgb(30, 30, 30));
        appDrawText(x + 232, y + 230, "This cannot be undone.", rgb(80, 80, 80));
    }
}

void FileExplorerApp::onKeyDown(uint32_t key) {
    if (m_renamePrompt) {
        if (key == '\n' || key == '\r') {
            commitRename();
        } else if (key == 27) {
            cancelRename();
        } else if (key == '\b') {
            int len = strlen_local(m_renameValue);
            if (len > 0) m_renameValue[len - 1] = '\0';
            invalidate();
        }
        return;
    }

    if (m_deleteConfirm && key == 27) {
        cancelDelete();
        return;
    }

    if (key == shell::KEY_UP) {
        if (m_selected > 0) {
            m_selected--;
            if (m_selected < m_scroll) m_scroll = m_selected;
            updateActionButtons();
            invalidate();
        }
    } else if (key == shell::KEY_DOWN) {
        if (m_selected < m_entryCount - 1) {
            m_selected++;
            if (m_selected >= m_scroll + 20) m_scroll = m_selected - 19;
            updateActionButtons();
            invalidate();
        }
    } else if (key == '\n' || key == '\r') {
        openSelected();
    } else if (key == '\b') {
        goUp();
    } else if (key == shell::KEY_PGUP) {
        m_selected -= 20;
        if (m_selected < 0) m_selected = 0;
        m_scroll -= 20;
        if (m_scroll < 0) m_scroll = 0;
        updateActionButtons();
        invalidate();
    } else if (key == shell::KEY_PGDN) {
        m_selected += 20;
        if (m_selected >= m_entryCount) m_selected = m_entryCount - 1;
        m_scroll += 20;
        if (m_scroll > m_selected) m_scroll = m_selected;
        updateActionButtons();
        invalidate();
    } else if (key == 'r' || key == 'R') {
        refresh();
        updateActionButtons();
        invalidate();
    }
}

void FileExplorerApp::onKeyChar(char c) {
    if (!m_renamePrompt) return;
    if (c >= 32 && c < 127) {
        int len = strlen_local(m_renameValue);
        if (len < (int)sizeof(m_renameValue) - 1 && c != '/') {
            m_renameValue[len] = c;
            m_renameValue[len + 1] = '\0';
        }
        invalidate();
    }
}

void FileExplorerApp::onMouseDown(int localX, int localY, uint8_t button) {
    (void)button;
    int bodyY = TOOLBAR_H + ADDRESS_H;
    if (localX < LEFT_W || localY < bodyY + 24) return;

    int row = (localY - bodyY - 24) / ROW_H;
    int index = m_scroll + row;
    if (index >= 0 && index < m_entryCount) {
        uint64_t now = pit::ticks();
        bool doubleClick = (index == m_lastClickIndex && now >= m_lastClickTick && now - m_lastClickTick <= 50);
        m_selected = index;
        m_lastClickIndex = index;
        m_lastClickTick = now;
        updateActionButtons();
        if (doubleClick) {
            openSelected();
            return;
        }
        invalidate();
    }
}

void FileExplorerApp::onWidgetClick(int widgetId) {
    if (widgetId == m_backBtnId || widgetId == m_rootBtnId) {
        navigate("/");
    } else if (widgetId == m_upBtnId) {
        goUp();
    } else if (widgetId == m_refreshBtnId) {
        refresh();
        updateActionButtons();
        invalidate();
    } else if (widgetId == m_renameFileBtnId || widgetId == m_renameFolderBtnId) {
        beginRenameSelected();
    } else if (widgetId == m_deleteFileBtnId || widgetId == m_deleteFolderBtnId) {
        showDeleteConfirmation();
    } else if (widgetId == m_confirmDeleteBtnId) {
        confirmDelete();
    } else if (widgetId == m_cancelDeleteBtnId) {
        cancelDelete();
    }
}

void FileExplorerApp::refresh() {
    m_entryCount = 0;
    uint8_t dir = vfs::opendir(m_currentPath);
    if (dir == 0xFF) {
        setStatus("Cannot open directory. Mount a filesystem with vfsmount if needed.");
        return;
    }

    vfs::DirEntry de{};
    while (m_entryCount < MAX_ENTRIES && vfs::readdir(dir, &de)) {
        if (de.name[0] == '.' && (de.name[1] == '\0' ||
            (de.name[1] == '.' && de.name[2] == '\0'))) {
            continue;
        }

        strcopy(m_entries[m_entryCount].name, de.name, vfs::VFS_MAX_FILENAME);
        m_entries[m_entryCount].isDir = (de.type == vfs::FILE_TYPE_DIRECTORY);
        m_entries[m_entryCount].size = de.size;
        m_entryCount++;
    }
    vfs::closedir(dir);

    if (m_selected >= m_entryCount) m_selected = m_entryCount - 1;
    if (m_selected < 0) m_selected = 0;
    if (m_scroll > m_selected) m_scroll = m_selected;
    if (m_entryCount == 0) {
        setStatus("Directory is empty");
    } else {
        setStatus("Ready");
    }
    updateActionButtons();
}

void FileExplorerApp::navigate(const char* path) {
    if (!path || !path[0]) return;
    vfs::FileInfo info{};
    if (vfs::stat(path, &info) != vfs::VFS_OK || info.type != vfs::FILE_TYPE_DIRECTORY) {
        setStatus("Path not found or not a directory");
        invalidate();
        return;
    }
    strcopy(m_currentPath, path, MAX_PATH_LEN);
    m_selected = 0;
    m_scroll = 0;
    m_lastClickIndex = -1;
    m_lastClickTick = 0;
    refresh();
    invalidate();
}

void FileExplorerApp::openSelected() {
    if (m_selected < 0 || m_selected >= m_entryCount) return;
    Entry& e = m_entries[m_selected];
    char full[MAX_PATH_LEN];
    joinPath(m_currentPath, e.name, full, sizeof(full));
    if (e.isDir) {
        navigate(full);
    } else if (isTextFile(e.name)) {
        if (app::AppManager::launchAppWithParam("Notepad", full)) {
            setStatus("Opened text file in Notepad");
        } else {
            setStatus("Unable to open text file in Notepad");
        }
        invalidate();
    } else {
        setStatus("Only .TXT and .TEXT files open in Notepad");
        invalidate();
    }
}

void FileExplorerApp::goUp() {
    char parent[MAX_PATH_LEN];
    parentPath(m_currentPath, parent, sizeof(parent));
    if (parent[0] && parent[0] != m_currentPath[0]) {
        navigate(parent);
    } else if (parent[0]) {
        bool different = false;
        for (int i = 0; parent[i] || m_currentPath[i]; ++i) {
            if (parent[i] != m_currentPath[i]) { different = true; break; }
        }
        if (different) navigate(parent);
    }
}

void FileExplorerApp::updateActionButtons() {
    bool hasSelection = m_selected >= 0 && m_selected < m_entryCount;
    bool isDir = hasSelection && m_entries[m_selected].isDir;

    app::Widget* renameFile = getWidget(m_renameFileBtnId);
    app::Widget* deleteFile = getWidget(m_deleteFileBtnId);
    app::Widget* renameFolder = getWidget(m_renameFolderBtnId);
    app::Widget* deleteFolder = getWidget(m_deleteFolderBtnId);
    app::Widget* confirmDelete = getWidget(m_confirmDeleteBtnId);
    app::Widget* cancelDelete = getWidget(m_cancelDeleteBtnId);

    if (renameFile) renameFile->visible = hasSelection && !isDir && !m_renamePrompt && !m_deleteConfirm;
    if (deleteFile) deleteFile->visible = hasSelection && !isDir && !m_renamePrompt && !m_deleteConfirm;
    if (renameFolder) renameFolder->visible = hasSelection && isDir && !m_renamePrompt && !m_deleteConfirm;
    if (deleteFolder) deleteFolder->visible = hasSelection && isDir && !m_renamePrompt && !m_deleteConfirm;
    if (confirmDelete) confirmDelete->visible = m_deleteConfirm;
    if (cancelDelete) cancelDelete->visible = m_deleteConfirm;
}

void FileExplorerApp::beginRenameSelected() {
    if (m_selected < 0 || m_selected >= m_entryCount) return;
    m_deleteConfirm = false;
    m_renamePrompt = true;
    strcopy(m_renameValue, m_entries[m_selected].name, sizeof(m_renameValue));
    setStatus("Type a new name, then press Enter.");
    updateActionButtons();
    invalidate();
}

void FileExplorerApp::commitRename() {
    if (m_selected < 0 || m_selected >= m_entryCount || !m_renameValue[0]) {
        cancelRename();
        return;
    }

    char oldPath[MAX_PATH_LEN];
    char newPath[MAX_PATH_LEN];
    joinPath(m_currentPath, m_entries[m_selected].name, oldPath, sizeof(oldPath));
    joinPath(m_currentPath, m_renameValue, newPath, sizeof(newPath));

    vfs::Status status = vfs::rename(oldPath, newPath);
    m_renamePrompt = false;
    if (status == vfs::VFS_OK) {
        setStatus("Renamed item");
    } else {
        setStatus("Rename failed");
    }
    refresh();
    updateActionButtons();
    invalidate();
}

void FileExplorerApp::cancelRename() {
    m_renamePrompt = false;
    setStatus("Rename cancelled");
    updateActionButtons();
    invalidate();
}

void FileExplorerApp::showDeleteConfirmation() {
    if (m_selected < 0 || m_selected >= m_entryCount) return;
    Entry& entry = m_entries[m_selected];
    joinPath(m_currentPath, entry.name, m_deleteTarget, sizeof(m_deleteTarget));
    strcopy(m_deleteTargetName, entry.name, sizeof(m_deleteTargetName));
    m_deleteTargetIsDir = entry.isDir;
    m_renamePrompt = false;
    m_deleteConfirm = true;
    setStatus("Confirm delete");
    updateActionButtons();
    invalidate();
}

void FileExplorerApp::confirmDelete() {
    if (!m_deleteConfirm || !m_deleteTarget[0]) return;
    vfs::Status status = m_deleteTargetIsDir ? vfs::rmdir(m_deleteTarget) : vfs::unlink(m_deleteTarget);
    m_deleteConfirm = false;
    if (status == vfs::VFS_OK) {
        setStatus("Deleted item");
    } else {
        setStatus("Delete failed");
    }
    refresh();
    updateActionButtons();
    invalidate();
}

void FileExplorerApp::cancelDelete() {
    m_deleteConfirm = false;
    setStatus("Delete cancelled");
    updateActionButtons();
    invalidate();
}

void FileExplorerApp::setStatus(const char* status) {
    strcopy(m_status, status ? status : "", sizeof(m_status));
}

bool FileExplorerApp::isTextFile(const char* name) const {
    if (!name) return false;

    const char* dot = nullptr;
    for (int i = 0; name[i]; ++i) {
        if (name[i] == '.') dot = &name[i];
    }

    if (!dot) return false;

    char ext[6];
    int len = 0;
    for (int i = 1; dot[i] && len < 5; ++i) {
        char c = dot[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        ext[len++] = c;
    }
    ext[len] = '\0';

    return (len == 3 && ext[0] == 't' && ext[1] == 'x' && ext[2] == 't') ||
           (len == 4 && ext[0] == 't' && ext[1] == 'e' && ext[2] == 'x' && ext[3] == 't');
}

void FileExplorerApp::joinPath(const char* base, const char* name, char* out, int outSize) const {
    if (!out || outSize <= 0) return;
    int pos = 0;
    if (!base || !base[0]) base = "/";
    while (base[pos] && pos < outSize - 1) {
        out[pos] = base[pos];
        pos++;
    }
    if (pos > 1 && out[pos - 1] != '/' && pos < outSize - 1) out[pos++] = '/';
    if (pos == 1 && out[0] == '/') {
        // root already has separator
    }
    for (int i = 0; name && name[i] && pos < outSize - 1; ++i) out[pos++] = name[i];
    out[pos] = '\0';
}

void FileExplorerApp::parentPath(const char* path, char* out, int outSize) const {
    if (!out || outSize <= 0) return;
    if (!path || !path[0] || (path[0] == '/' && path[1] == '\0')) {
        strcopy(out, "/", outSize);
        return;
    }

    int len = strlen_local(path);
    while (len > 1 && path[len - 1] == '/') len--;
    int slash = len - 1;
    while (slash > 0 && path[slash] != '/') slash--;
    int copyLen = slash == 0 ? 1 : slash;
    if (copyLen >= outSize) copyLen = outSize - 1;
    for (int i = 0; i < copyLen; ++i) out[i] = path[i];
    out[copyLen] = '\0';
}

void FileExplorerApp::formatSize(uint64_t size, char* out, int outSize) const {
    if (!out || outSize <= 0) return;
    uint64_t value = size;
    const char* suffix = " B";
    if (size >= 1024 * 1024) { value = size / (1024 * 1024); suffix = " MB"; }
    else if (size >= 1024) { value = size / 1024; suffix = " KB"; }

    char digits[24];
    int d = 0;
    if (value == 0) digits[d++] = '0';
    else {
        char tmp[24];
        int t = 0;
        while (value > 0 && t < 23) { tmp[t++] = '0' + (value % 10); value /= 10; }
        while (t > 0) digits[d++] = tmp[--t];
    }
    digits[d] = '\0';

    int pos = 0;
    for (int i = 0; digits[i] && pos < outSize - 1; ++i) out[pos++] = digits[i];
    for (int i = 0; suffix[i] && pos < outSize - 1; ++i) out[pos++] = suffix[i];
    out[pos] = '\0';
}

const char* FileExplorerApp::fileType(const Entry& entry) const {
    if (entry.isDir) return "File folder";
    const char* dot = nullptr;
    for (int i = 0; entry.name[i]; ++i) if (entry.name[i] == '.') dot = &entry.name[i];
    if (!dot || !dot[1]) return "File";
    if ((dot[1] == 't' || dot[1] == 'T') && (dot[2] == 'x' || dot[2] == 'X') && (dot[3] == 't' || dot[3] == 'T')) return "Text document";
    if ((dot[1] == 'e' || dot[1] == 'E') && (dot[2] == 'l' || dot[2] == 'L') && (dot[3] == 'f' || dot[3] == 'F')) return "Application";
    return "File";
}

// ============================================================
// DiskManagerApp Implementation
// ============================================================

DiskManagerApp::DiskManagerApp()
    : m_diskCount(0), m_selectedDisk(0), m_refreshBtnId(-1) {
    strcopy(m_name, "DiskManager", app::MAX_APP_NAME);
}

DiskManagerApp::~DiskManagerApp() {
}

bool DiskManagerApp::init() {
    serial::puts("[DISKMANAGER] Starting in baremetal mode\n");

    m_window = new app::KernelWindow();
    strcopy(m_window->title, "Disk Manager", app::MAX_TITLE_LEN);
    m_window->x = 120;
    m_window->y = 55;
    m_window->w = 700;
    m_window->h = 420;
    m_window->flags = app::WF_VISIBLE | app::WF_TITLEBAR | app::WF_CLOSABLE | app::WF_RESIZABLE | app::WF_FOCUSED;
    m_window->owner = this;

    if (!compositor::KernelCompositor::registerWindow(m_window)) {
        delete m_window;
        m_window = nullptr;
        serial::puts("[DISKMANAGER] Failed to register window\n");
        return false;
    }

    m_refreshBtnId = addButton(10, m_window->h - 44, 90, 28, "Refresh");

    scanDisks();

    m_state = app::AppState::Running;
    serial::puts("[DISKMANAGER] Init complete\n");
    return true;
}

void DiskManagerApp::shutdown() {
    m_state = app::AppState::Terminated;
}

void DiskManagerApp::scanDisks() {
    m_diskCount = 0;
    uint8_t total = kernel::block::device_count();
    serial::puts("[DISKMANAGER] Scanning block devices, count=");
    serial::put_hex8(total);
    serial::putc('\n');

    for (uint8_t i = 0; i < total && m_diskCount < MAX_DISKS; i++) {
        const kernel::block::BlockDevice* dev = kernel::block::get_device(i);
        if (!dev || !dev->active) continue;

        DiskEntry& e = m_disks[m_diskCount];
        e.devIndex = i;
        e.totalSectors = dev->totalSectors;
        e.sectorSize = dev->sectorSize;
        e.haveInfo = true;
        e.partCount = 0;

        // Build display name from device name + type
        const char* typeStr = "Disk";
        if (dev->type == kernel::block::BDEV_ATA_PIO || dev->type == kernel::block::BDEV_AHCI)
            typeStr = "System";
        else if (dev->type == kernel::block::BDEV_NVME)
            typeStr = "NVMe";
        else if (dev->type == kernel::block::BDEV_USB_MASS)
            typeStr = "USB";

        // name = "<dev->name> (<typeStr>)"
        int ni = 0;
        for (int j = 0; dev->name[j] && ni < 30; j++) e.name[ni++] = dev->name[j];
        e.name[ni++] = ' '; e.name[ni++] = '(';
        for (int j = 0; typeStr[j] && ni < 37; j++) e.name[ni++] = typeStr[j];
        e.name[ni++] = ')'; e.name[ni] = '\0';

        readMBR(e);
        m_diskCount++;
    }

    if (m_diskCount == 0) {
        serial::puts("[DISKMANAGER] No block devices found\n");
        DiskEntry& e = m_disks[0];
        strcopy(e.name, "No disks detected", 40);
        e.devIndex = 0;
        e.haveInfo = false;
        e.totalSectors = 0;
        e.sectorSize = 512;
        e.partCount = 0;
        m_diskCount = 1;
    }

    if (m_selectedDisk >= m_diskCount) m_selectedDisk = 0;
}

void DiskManagerApp::readMBR(DiskEntry& disk) {
    disk.partCount = 0;
    uint8_t mbr[512];
    kernel::block::Status st = kernel::block::read_sectors(disk.devIndex, 0, 1, mbr);
    if (st != kernel::block::BLOCK_OK) return;
    if (mbr[510] != 0x55 || mbr[511] != 0xAA) return;

    for (int i = 0; i < MAX_PARTS; i++) {
        int off = 446 + i * 16;
        uint8_t type = mbr[off + 4];
        if (type == 0) continue;

        PartEntry& p = disk.parts[disk.partCount];
        p.type     = type;
        p.bootable = (mbr[off + 0] == 0x80);
        p.lbaStart = (uint32_t)mbr[off + 8]  | ((uint32_t)mbr[off + 9]  << 8) |
                     ((uint32_t)mbr[off + 10] << 16) | ((uint32_t)mbr[off + 11] << 24);
        p.lbaCount = (uint32_t)mbr[off + 12] | ((uint32_t)mbr[off + 13] << 8) |
                     ((uint32_t)mbr[off + 14] << 16) | ((uint32_t)mbr[off + 15] << 24);
        const char* fs = detectFs(disk.devIndex, p.lbaStart);
        strcopy(p.fsLabel, fs, (int)sizeof(p.fsLabel));
        disk.partCount++;
    }
}

const char* DiskManagerApp::detectFs(uint8_t devIndex, uint32_t lbaStart) {
    if (lbaStart == 0) return "Unknown";
    uint8_t sec[512];
    kernel::block::Status st = kernel::block::read_sectors(devIndex, lbaStart, 1, sec);
    if (st != kernel::block::BLOCK_OK) return "Unknown";

    // TarFS magic at offset 257
    if (sec[257] == 'u' && sec[258] == 's' && sec[259] == 't' &&
        sec[260] == 'a' && sec[261] == 'r') return "TarFS";

    // FAT: boot signature + sane BPB
    if (sec[510] == 0x55 && sec[511] == 0xAA) {
        uint16_t bps = (uint16_t)sec[11] | ((uint16_t)sec[12] << 8);
        if ((bps == 512 || bps == 1024 || bps == 2048 || bps == 4096) && sec[13] != 0)
            return "FAT";
    }

    // EXT2/3/4: magic at superblock offset 0x438
    uint8_t sb[512];
    kernel::block::Status st2 = kernel::block::read_sectors(devIndex, lbaStart + 2, 1, sb);
    if (st2 == kernel::block::BLOCK_OK) {
        uint16_t ext_magic = (uint16_t)sb[0x38] | ((uint16_t)sb[0x39] << 8);
        if (ext_magic == 0xEF53) return "EXT2";
    }

    return "Unknown";
}

void DiskManagerApp::formatSize(uint64_t bytes, char* out, int outSize) const {
    // Simple size formatter: TB/GB/MB/KB/B
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    int u = 0;
    uint64_t val = bytes;
    while (val >= 1024 && u < 4) { val >>= 10; u++; }

    // Convert to decimal string (no printf available)
    char tmp[24];
    int ti = 0;
    if (val == 0) {
        tmp[ti++] = '0';
    } else {
        uint64_t v = val;
        char rev[20]; int ri = 0;
        while (v > 0) { rev[ri++] = '0' + (int)(v % 10); v /= 10; }
        for (int j = ri - 1; j >= 0; j--) tmp[ti++] = rev[j];
    }
    tmp[ti++] = ' ';
    for (int j = 0; units[u][j] && ti < 22; j++) tmp[ti++] = units[u][j];
    tmp[ti] = '\0';

    // Copy to out
    int i = 0;
    while (tmp[i] && i < outSize - 1) { out[i] = tmp[i]; i++; }
    out[i] = '\0';
}

void DiskManagerApp::draw(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    static const uint32_t kHeader   = 0xFF2C3E50;
    static const uint32_t kBg       = 0xFF1E2430;
    static const uint32_t kPanel    = 0xFF252D3B;
    static const uint32_t kRowSel   = 0xFF2E4A6E;
    static const uint32_t kRowAlt   = 0xFF222A36;
    static const uint32_t kText     = 0xFFDCE3F0;
    static const uint32_t kSubText  = 0xFF8A9AB0;
    static const uint32_t kAccent   = 0xFF4A9ECA;
    static const uint32_t kPartBar  = 0xFF3A7EAA;
    static const uint32_t kPartBarB = 0xFF2A5E80;
    (void)kAccent; (void)kPartBarB;

    // Background
    framebuffer::fill_rect(x, y, w, h, kBg);

    // Title bar stripe
    framebuffer::fill_rect(x, y, w, 22, kHeader);
    appDrawText(x + 10, y + 7, "Disk Manager  [baremetal mode]", kText);

    // Left pane: disk list
    const uint32_t leftW = 200;
    framebuffer::fill_rect(x, y + 22, leftW, h - 22, kPanel);
    appDrawText(x + 8, y + 28, "Disks", kSubText);

    uint32_t rowH = 28;
    for (int i = 0; i < m_diskCount; i++) {
        uint32_t ry = y + 46 + (uint32_t)i * rowH;
        uint32_t rowColor = (i == m_selectedDisk) ? kRowSel : ((i % 2 == 0) ? kPanel : kRowAlt);
        framebuffer::fill_rect(x + 2, ry, leftW - 4, rowH - 2, rowColor);
        appDrawText(x + 8, ry + (rowH - kGlyphH) / 2, m_disks[i].name, kText);
    }

    // Right pane: detail
    uint32_t rx = x + leftW + 4;
    uint32_t rw = (w > leftW + 8) ? (w - leftW - 8) : 0;
    framebuffer::fill_rect(rx, y + 22, rw, h - 22, kBg);

    if (m_selectedDisk >= 0 && m_selectedDisk < m_diskCount) {
        const DiskEntry& d = m_disks[m_selectedDisk];
        uint32_t dy = y + 28;

        // Disk header
        appDrawText(rx + 4, dy, d.name, kText);
        dy += kGlyphH + 6;

        if (d.haveInfo) {
            char szBuf[32];
            formatSize(d.totalSectors * (uint64_t)d.sectorSize, szBuf, sizeof(szBuf));
            appDrawText(rx + 4, dy, szBuf, kSubText);
            dy += kGlyphH + 10;

            // Partition table header
            appDrawText(rx + 4, dy, "# ", kSubText);
            appDrawText(rx + 20, dy, "Type  LBA Start    Sectors     FS       Boot", kSubText);
            dy += kGlyphH + 4;
            framebuffer::fill_rect(rx + 4, dy, rw - 8, 1, kPanel);
            dy += 3;

            if (d.partCount == 0) {
                appDrawText(rx + 4, dy, "No MBR partitions found", kSubText);
                dy += kGlyphH + 6;
            }

            for (int pi = 0; pi < d.partCount; pi++) {
                const PartEntry& p = d.parts[pi];
                uint32_t pry = dy + (uint32_t)pi * (kGlyphH + 6);

                // Small partition color bar
                framebuffer::fill_rect(rx + 4, pry, 4, kGlyphH, kPartBar);

                // Row text  (manual number → char)
                char numBuf[4] = {'0' + (char)(pi + 1), '\0', '\0', '\0'};
                appDrawText(rx + 10, pry, numBuf, kText);

                // Type hex
                char typeBuf[8];
                typeBuf[0] = '0'; typeBuf[1] = 'x';
                static const char hex[] = "0123456789ABCDEF";
                typeBuf[2] = hex[(p.type >> 4) & 0xF];
                typeBuf[3] = hex[p.type & 0xF];
                typeBuf[4] = '\0';
                appDrawText(rx + 22, pry, typeBuf, kSubText);

                // LBA start (decimal, hand-rolled)
                char lbaBuf[16]; int li = 0;
                if (p.lbaStart == 0) { lbaBuf[li++] = '0'; }
                else { uint32_t v = p.lbaStart; char rev[12]; int ri = 0;
                       while (v > 0) { rev[ri++] = '0' + (int)(v % 10); v /= 10; }
                       for (int j = ri - 1; j >= 0; j--) lbaBuf[li++] = rev[j]; }
                lbaBuf[li] = '\0';
                appDrawText(rx + 60, pry, lbaBuf, kSubText);

                // Sector count
                char scBuf[16]; int si = 0;
                if (p.lbaCount == 0) { scBuf[si++] = '0'; }
                else { uint32_t v = p.lbaCount; char rev[12]; int ri = 0;
                       while (v > 0) { rev[ri++] = '0' + (int)(v % 10); v /= 10; }
                       for (int j = ri - 1; j >= 0; j--) scBuf[si++] = rev[j]; }
                scBuf[si] = '\0';
                appDrawText(rx + 120, pry, scBuf, kSubText);

                // FS label
                appDrawText(rx + 190, pry, p.fsLabel, kText);

                // Boot flag
                if (p.bootable) appDrawText(rx + 240, pry, "*", kAccent);
            }

            // Partition bar at bottom
            if (d.partCount > 0 && d.totalSectors > 0) {
                uint32_t barY = y + h - 60;
                uint32_t barX = rx + 4;
                uint32_t barW = (rw > 16) ? rw - 16 : 0;
                framebuffer::fill_rect(barX, barY, barW, 18, kPanel);

                for (int pi = 0; pi < d.partCount; pi++) {
                    const PartEntry& p = d.parts[pi];
                    uint32_t pxOff = (uint32_t)((uint64_t)p.lbaStart * barW / d.totalSectors);
                    uint32_t pxW   = (uint32_t)((uint64_t)p.lbaCount * barW / d.totalSectors);
                    if (pxW < 2) pxW = 2;
                    uint32_t col = (pi % 2 == 0) ? kPartBar : kPartBarB;
                    framebuffer::fill_rect(barX + pxOff, barY, pxW, 18, col);
                }

                appDrawText(rx + 4, barY + 22, "Partition map", kSubText);
            }
        } else {
            appDrawText(rx + 4, dy, "No disk info available", kSubText);
        }
    }
}

void DiskManagerApp::onMouseDown(int localX, int localY, uint8_t button) {
    (void)button;
    const uint32_t leftW = 200;
    const uint32_t listTop = 46;
    const uint32_t rowH = 28;

    if ((uint32_t)localX < leftW && (uint32_t)localY >= listTop) {
        int idx = ((uint32_t)localY - listTop) / rowH;
        if (idx >= 0 && idx < m_diskCount) {
            m_selectedDisk = idx;
            invalidate();
        }
    }
}

void DiskManagerApp::onWidgetClick(int widgetId) {
    if (widgetId == m_refreshBtnId) {
        serial::puts("[DISKMANAGER] Manual refresh\n");
        scanDisks();
        invalidate();
    }
}

// ============================================================
// App Registration
// ============================================================

void registerKernelApps() {
    app::AppManager::init();
    app::AppLogger::init();
    
    // Register available kernel-mode apps
    app::AppManager::registerApp("Notepad", 0xFF78B450, NotepadApp::create);
    app::AppManager::registerApp("Calculator", 0xFF4690C8, CalculatorApp::create);
    app::AppManager::registerApp("TaskManager", 0xFFB44646, TaskManagerApp::create);
    app::AppManager::registerApp("Files", 0xFFC8B43C, FileExplorerApp::create);
    app::AppManager::registerApp("FileExplorer", 0xFFC8B43C, FileExplorerApp::create);
    app::AppManager::registerApp("DiskManager", 0xFF7050C0, DiskManagerApp::create);
}

} // namespace apps
} // namespace kernel
