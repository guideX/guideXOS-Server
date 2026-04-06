//
// guideXOS Kernel GUI Apps Implementation
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/kernel/kernel_apps.h"
#include "include/kernel/kernel_compositor.h"
#include "include/kernel/framebuffer.h"
#include "include/kernel/shell.h"

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

// ============================================================
// NotepadApp Implementation
// ============================================================

NotepadApp::NotepadApp() : m_textLength(0), m_cursorPos(0), m_scrollY(0) {
    strcopy(m_name, "Notepad", app::MAX_APP_NAME);
    m_text[0] = '\0';
}

NotepadApp::~NotepadApp() {
}

bool NotepadApp::init() {
    // Create window
    m_window = new app::KernelWindow();
    strcopy(m_window->title, "Notepad - Untitled", app::MAX_TITLE_LEN);
    m_window->x = 100;
    m_window->y = 50;
    m_window->w = 500;
    m_window->h = 350;
    m_window->flags = app::WF_VISIBLE | app::WF_TITLEBAR | app::WF_CLOSABLE | app::WF_RESIZABLE | app::WF_FOCUSED;
    m_window->owner = this;
    
    // Register with compositor
    if (!compositor::KernelCompositor::registerWindow(m_window)) {
        delete m_window;
        m_window = nullptr;
        return false;
    }
    
    // Initialize text buffer with welcome message
    const char* welcome = "Welcome to guideXOS Notepad!\n\nThis is a simple text editor running\nin bare-metal/UEFI mode.\n\nType here...";
    strcopy(m_text, welcome, MAX_TEXT_LENGTH);
    m_textLength = strlen_local(m_text);
    m_cursorPos = m_textLength;
    
    m_state = app::AppState::Running;
    return true;
}

void NotepadApp::shutdown() {
    m_state = app::AppState::Terminated;
}

void NotepadApp::draw(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    // Text editor background
    framebuffer::fill_rect(x + 4, y + 4, w - 8, h - 8, rgb(45, 45, 55));
    
    // Draw text
    uint32_t textX = x + 8;
    uint32_t textY = y + 8;
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
            // Printable character - draw it
            // (Using simple pixel output for now, could use proper glyph rendering)
            uint32_t cx = textX + col * (kGlyphW + kGlyphSpacing);
            
            // Simple character rendering - just show as blocks for now
            // Full glyph rendering is in the compositor
            if (c != ' ') {
                for (int dy = 0; dy < kGlyphH; dy++) {
                    for (int dx = 0; dx < kGlyphW; dx++) {
                        // Simple pattern based on character
                        bool on = ((c + dx + dy) % 3 == 0) || (dx == 2 && dy >= 1 && dy <= 5);
                        if (on) {
                            framebuffer::put_pixel(cx + dx, textY + dy, rgb(220, 220, 235));
                        }
                    }
                }
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
}

void NotepadApp::onKeyChar(char c) {
    if (c >= 32 && c < 127) {
        insertChar(c);
        invalidate();
    }
}

void NotepadApp::onKeyDown(uint32_t key) {
    switch (key) {
        case '\n':
        case '\r':
            insertChar('\n');
            break;
        case '\b':
        case 127:  // Delete
            deleteChar();
            break;
        case shell::KEY_LEFT:
            moveCursor(-1);
            break;
        case shell::KEY_RIGHT:
            moveCursor(1);
            break;
        case shell::KEY_HOME:
            m_cursorPos = 0;
            break;
        case shell::KEY_END:
            m_cursorPos = m_textLength;
            break;
        default:
            break;
    }
    invalidate();
}

void NotepadApp::onMouseDown(int x, int y, uint8_t button) {
    // Could implement click-to-position cursor here
    (void)x; (void)y; (void)button;
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
    // Draw "Application" header
    for (int i = 0; i < 11; i++) {
        char c = "Application"[i];
        for (int dy = 0; dy < kGlyphH; dy++) {
            for (int dx = 0; dx < kGlyphW; dx++) {
                if (((c + dx + dy) % 3) == 0) {
                    framebuffer::put_pixel(x + 15 + i * (kGlyphW + kGlyphSpacing) + dx, 
                                           headerY + dy, rgb(180, 185, 200));
                }
            }
        }
    }
    
    // Draw "Status" header
    for (int i = 0; i < 6; i++) {
        char c = "Status"[i];
        for (int dy = 0; dy < kGlyphH; dy++) {
            for (int dx = 0; dx < kGlyphW; dx++) {
                if (((c + dx + dy) % 3) == 0) {
                    framebuffer::put_pixel(x + w - 90 + i * (kGlyphW + kGlyphSpacing) + dx,
                                           headerY + dy, rgb(180, 185, 200));
                }
            }
        }
    }
    
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
        int nameLen = strlen_local(m_entries[i].name);
        for (int j = 0; j < nameLen && j < 25; j++) {
            char c = m_entries[i].name[j];
            for (int dy = 0; dy < kGlyphH; dy++) {
                for (int dx = 0; dx < kGlyphW; dx++) {
                    if (((c + dx + dy) % 2) == 0) {
                        framebuffer::put_pixel(x + 15 + j * (kGlyphW + kGlyphSpacing) + dx,
                                               textY + dy, rgb(210, 210, 225));
                    }
                }
            }
        }
        
        // Status indicator
        uint32_t statusColor = m_entries[i].running ? rgb(80, 180, 100) : rgb(180, 80, 80);
        framebuffer::fill_rect(x + w - 80, textY, 8, 8, statusColor);
        
        // Status text
        const char* status = m_entries[i].running ? "Running" : "Stopped";
        int slen = strlen_local(status);
        for (int j = 0; j < slen; j++) {
            char c = status[j];
            for (int dy = 0; dy < kGlyphH; dy++) {
                for (int dx = 0; dx < kGlyphW; dx++) {
                    if (((c + dx + dy) % 3) == 0) {
                        framebuffer::put_pixel(x + w - 68 + j * (kGlyphW + kGlyphSpacing) + dx,
                                               textY + dy, rgb(160, 165, 180));
                    }
                }
            }
        }
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
// App Registration
// ============================================================

void registerKernelApps() {
    app::AppManager::init();
    app::AppLogger::init();
    
    // Register available kernel-mode apps
    app::AppManager::registerApp("Notepad", 0xFF78B450, NotepadApp::create);
    app::AppManager::registerApp("Calculator", 0xFF4690C8, CalculatorApp::create);
    app::AppManager::registerApp("TaskManager", 0xFFB44646, TaskManagerApp::create);
}

} // namespace apps
} // namespace kernel
