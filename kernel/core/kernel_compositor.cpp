//
// guideXOS Kernel Compositor Implementation
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/kernel/kernel_compositor.h"
#include "include/kernel/kernel_ipc.h"
#include "include/kernel/framebuffer.h"
#include "include/kernel/image_adapter.h"
#include "include/kernel/serial_debug.h"
#include "include/kernel/vfs.h"
#include "../../taskbar_application_model_core.h"

namespace kernel {
namespace compositor {

// ============================================================
// Static member initialization
// ============================================================

WindowEntry KernelCompositor::s_windows[MAX_WINDOWS];
int KernelCompositor::s_windowCount = 0;
uint32_t KernelCompositor::s_focusedWindowId = 0;
uint32_t KernelCompositor::s_nextWindowId = 1000;
uint32_t KernelCompositor::s_screenW = 0;
uint32_t KernelCompositor::s_screenH = 0;
uint32_t KernelCompositor::s_taskbarH = 0;
uint32_t KernelCompositor::s_workX = 0;
uint32_t KernelCompositor::s_workY = 0;
uint32_t KernelCompositor::s_workW = 0;
uint32_t KernelCompositor::s_workH = 0;
DragState KernelCompositor::s_dragState;
uint32_t KernelCompositor::s_hoverWindowId = 0;
HitTestResult KernelCompositor::s_hoverResult = HitTestResult::None;
bool KernelCompositor::s_buttonPressActive = false;
bool KernelCompositor::s_initialized = false;
uint32_t KernelCompositor::s_showDesktopMinimized[MAX_WINDOWS];
int KernelCompositor::s_showDesktopMinimizedCount = 0;

// Bitmap font (5x7) - same as desktop.cpp
static const int kGlyphW = 5;
static const int kGlyphH = 7;
static const int kGlyphSpacing = 1;
static const int kGlyphCount = 95;

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

static int measureText(const char* str) {
    int len = 0;
    while (str[len]) len++;
    if (len == 0) return 0;
    return len * (kGlyphW + kGlyphSpacing) - kGlyphSpacing;
}

// ============================================================
// KernelCompositor implementation
// ============================================================

void KernelCompositor::init(uint32_t screenW, uint32_t screenH, uint32_t taskbarH) {
    // Always reinitialize to handle cases where static init may not work
    // (e.g., kernel/UEFI environments where .bss might not be zeroed)
    s_screenW = screenW;
    s_screenH = screenH;
    s_taskbarH = taskbarH;
    setWorkArea(0, 0, screenW, screenH > taskbarH ? screenH - taskbarH : screenH);
    
    for (int i = 0; i < MAX_WINDOWS; i++) {
        s_windows[i].valid = false;
        s_windows[i].window = nullptr;
    }
    
    s_windowCount = 0;
    s_focusedWindowId = 0;
    s_nextWindowId = 1000;
    s_hoverWindowId = 0;
    s_hoverResult = HitTestResult::None;
    s_dragState = DragState();
    s_buttonPressActive = false;  // Ensure button press tracking is reset
    s_showDesktopMinimizedCount = 0;
    s_initialized = true;
}

void KernelCompositor::setWorkArea(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    const bool changed = s_workX != x || s_workY != y || s_workW != w || s_workH != h;
    s_workX = x;
    s_workY = y;
    s_workW = w;
    s_workH = h;
    if (!changed) return;
    for (int i = 0; i < MAX_WINDOWS; ++i) {
        if (!s_windows[i].valid || !s_windows[i].window) continue;
        app::KernelWindow* window = s_windows[i].window;
        if (!(window->flags & app::WF_MAXIMIZED)) continue;
        if (window->x == static_cast<int>(s_workX) && window->y == static_cast<int>(s_workY) &&
            window->w == static_cast<int>(s_workW) && window->h == static_cast<int>(s_workH)) continue;
        window->x = static_cast<int>(s_workX);
        window->y = static_cast<int>(s_workY);
        window->w = static_cast<int>(s_workW);
        window->h = static_cast<int>(s_workH);
        window->dirty = true;
    }
}

void KernelCompositor::reconcileDisplayTopology(uint32_t screenW, uint32_t screenH) {
    if (screenW == 0u || screenH == 0u) return;
    s_screenW = screenW;
    s_screenH = screenH;
    const uint32_t workH = screenH > s_taskbarH ? screenH - s_taskbarH : screenH;
    setWorkArea(0u, 0u, screenW, workH);

    // A topology change invalidates an in-flight drag/capture target.  Keep
    // the window alive and make its title bar reachable in the surviving
    // monitor work area.
    s_dragState = DragState();
    s_buttonPressActive = false;
    for (int i = 0; i < MAX_WINDOWS; ++i) {
        if (!s_windows[i].valid || s_windows[i].window == nullptr) continue;
        app::KernelWindow* window = s_windows[i].window;
        if (window->w < MIN_WINDOW_WIDTH) window->w = MIN_WINDOW_WIDTH;
        if (window->h < MIN_WINDOW_HEIGHT) window->h = MIN_WINDOW_HEIGHT;
        if ((uint32_t)window->w > s_workW) window->w = (int)s_workW;
        if ((uint32_t)window->h > s_workH) window->h = (int)s_workH;
        const int maxX = (int)(s_workX + s_workW) - window->w;
        const int maxY = (int)(s_workY + s_workH) - window->h;
        if (window->x < (int)s_workX) window->x = (int)s_workX;
        if (window->y < (int)s_workY) window->y = (int)s_workY;
        if (window->x > maxX) window->x = maxX >= (int)s_workX ? maxX : (int)s_workX;
        if (window->y > maxY) window->y = maxY >= (int)s_workY ? maxY : (int)s_workY;
        window->dirty = true;
    }

    TaskbarManager::setLayout(0u, workH, screenW, s_taskbarH, false, 8u, workH + 6u);
    TaskbarManager::updateButtons();
}

void KernelCompositor::shutdown() {
    for (int i = 0; i < MAX_WINDOWS; i++) {
        s_windows[i].valid = false;
        s_windows[i].window = nullptr;
    }
    s_windowCount = 0;
    s_initialized = false;
}

bool KernelCompositor::registerWindow(app::KernelWindow* window) {
    if (!s_initialized || !window) {
        return false;
    }
    
    // Assign ID if not set
    if (window->id == 0) {
        window->id = generateWindowId();
    }
    
    // Find empty slot
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (!s_windows[i].valid) {
            s_windows[i].valid = true;
            s_windows[i].window = window;
            s_windowCount++;
            
            // Focus new window
            setFocus(window->id);
            return true;
        }
    }
    
    return false;
}

void KernelCompositor::unregisterWindow(app::KernelWindow* window) {
    if (!s_initialized || !window) {
        return;
    }
    
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (s_windows[i].valid && s_windows[i].window == window) {
            TaskbarManager::endFileOperationActivity(window->id);
            TaskbarManager::clearActivity(nullptr, window->id);
            s_windows[i].valid = false;
            s_windows[i].window = nullptr;
            s_windowCount--;
            
            // Clear focus if this was focused
            if (s_focusedWindowId == window->id) {
                if (window->owner) window->owner->onWindowBlur();
                window->flags &= ~app::WF_FOCUSED;
                s_focusedWindowId = 0;
                // Focus next window if available
                for (int j = MAX_WINDOWS - 1; j >= 0; j--) {
                    if (s_windows[j].valid && s_windows[j].window &&
                        !(s_windows[j].window->flags & app::WF_MINIMIZED)) {
                        setFocus(s_windows[j].window->id);
                        break;
                    }
                }
            }
            return;
        }
    }
}

app::KernelWindow* KernelCompositor::getWindow(uint32_t windowId) {
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (s_windows[i].valid && s_windows[i].window && s_windows[i].window->id == windowId) {
            return s_windows[i].window;
        }
    }
    return nullptr;
}

app::KernelWindow* KernelCompositor::getFocusedWindow() {
    return getWindow(s_focusedWindowId);
}

#if defined(GXOS_DESKTOP_CLEANUP_RUNTIME_PASS)
static void log_close_target(const char* prefix, const app::KernelWindow* win)
{
    serial::puts(prefix);
    serial::puts(" window=");
    if (win) {
        serial::put_hex32(win->id);
        serial::puts(" title=");
        serial::puts(win->title);
        serial::puts(" app=");
        serial::puts(win->owner ? win->owner->getName() : "(no-owner)");
        serial::puts(" flags=0x");
        serial::put_hex32(win->flags);
    } else {
        serial::puts("(null)");
    }
    serial::putc('\n');
}
#endif

void KernelCompositor::setFocus(uint32_t windowId) {
    app::KernelWindow* newWin = getWindow(windowId);
    if (!newWin) return;

    // Focusing is an activation operation. A taskbar selection must make a
    // minimized window visible before it can receive focus.
    newWin->flags &= ~app::WF_MINIMIZED;
    TaskbarManager::acknowledgeActivity(newWin->owner ? newWin->owner->getRuntimeAppId() : "", windowId);

    if (s_focusedWindowId == windowId) {
        newWin->flags |= app::WF_FOCUSED;
        return;
    }

    // Clear focus flag on old window
    if (s_focusedWindowId != 0 && s_focusedWindowId != windowId) {
        app::KernelWindow* oldWin = getWindow(s_focusedWindowId);
        if (oldWin) {
            oldWin->flags &= ~app::WF_FOCUSED;
            oldWin->dirty = true;
            if (oldWin->owner) oldWin->owner->onWindowBlur();
        }
    }
    
    // Set focus flag on new window
    newWin->flags |= app::WF_FOCUSED;
    newWin->dirty = true;
    s_focusedWindowId = windowId;
    bringToFront(windowId);
    if (newWin->owner) newWin->owner->onWindowFocus();
}

void KernelCompositor::bringToFront(uint32_t windowId) {
    int idx = findWindowIndex(windowId);
    if (idx < 0 || idx >= s_windowCount - 1) {
        return;  // Already at front or not found
    }
    
    // Swap with last valid entry
    WindowEntry temp = s_windows[idx];
    for (int i = idx; i < MAX_WINDOWS - 1; i++) {
        s_windows[i] = s_windows[i + 1];
    }
    
    // Find last valid position and insert there
    for (int i = MAX_WINDOWS - 1; i >= 0; i--) {
        if (!s_windows[i].valid || i == MAX_WINDOWS - 1) {
            s_windows[i] = temp;
            break;
        }
    }
}

void KernelCompositor::minimizeWindow(uint32_t windowId) {
    app::KernelWindow* win = getWindow(windowId);
    if (win) {
        win->flags |= app::WF_MINIMIZED;
        win->dirty = true;
        if (s_focusedWindowId == windowId) {
            win->flags &= ~app::WF_FOCUSED;
            if (win->owner) win->owner->onWindowBlur();
            s_focusedWindowId = 0;
            // Keep a useful active window after minimizing the focused one.
            for (int i = MAX_WINDOWS - 1; i >= 0; --i) {
                if (!s_windows[i].valid || !s_windows[i].window) continue;
                app::KernelWindow* candidate = s_windows[i].window;
                if (candidate->id == windowId || !(candidate->flags & app::WF_VISIBLE) ||
                    (candidate->flags & app::WF_MINIMIZED)) continue;
                setFocus(candidate->id);
                break;
            }
        }
    }
}

void KernelCompositor::maximizeWindow(uint32_t windowId) {
    app::KernelWindow* win = getWindow(windowId);
    if (win) {
        if (win->flags & app::WF_MAXIMIZED) {
            // Restore
            win->flags &= ~app::WF_MAXIMIZED;
            win->x = win->savedX;
            win->y = win->savedY;
            win->w = win->savedW;
            win->h = win->savedH;
        } else {
            // Maximize
            win->savedX = win->x;
            win->savedY = win->y;
            win->savedW = win->w;
            win->savedH = win->h;
            win->x = (int)s_workX;
            win->y = (int)s_workY;
            win->w = (int)s_workW;
            win->h = (int)s_workH;
            win->flags |= app::WF_MAXIMIZED;
        }
        win->dirty = true;
    }
}

void KernelCompositor::restoreWindow(uint32_t windowId) {
    app::KernelWindow* win = getWindow(windowId);
    if (win) {
        win->flags &= ~(app::WF_MINIMIZED | app::WF_MAXIMIZED);
        win->dirty = true;
    }
}

void KernelCompositor::minimizeWindowsForShowDesktop() {
    s_showDesktopMinimizedCount = 0;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (!s_windows[i].valid || !s_windows[i].window) continue;
        app::KernelWindow* win = s_windows[i].window;
        if ((win->flags & app::WF_VISIBLE) && !(win->flags & app::WF_MINIMIZED)) {
            s_showDesktopMinimized[s_showDesktopMinimizedCount++] = win->id;
            win->flags |= app::WF_MINIMIZED;
            if (win->flags & app::WF_FOCUSED) {
                win->flags &= ~app::WF_FOCUSED;
                if (win->owner) win->owner->onWindowBlur();
                if (s_focusedWindowId == win->id) s_focusedWindowId = 0;
            }
            win->dirty = true;
            if (s_showDesktopMinimizedCount >= MAX_WINDOWS) break;
        }
    }
}

void KernelCompositor::restoreWindowsFromShowDesktop() {
    for (int i = 0; i < s_showDesktopMinimizedCount; i++) {
        app::KernelWindow* win = getWindow(s_showDesktopMinimized[i]);
        if (win) {
            win->flags &= ~app::WF_MINIMIZED;
            win->dirty = true;
        }
    }

    if (s_showDesktopMinimizedCount > 0) {
        setFocus(s_showDesktopMinimized[s_showDesktopMinimizedCount - 1]);
    }
    s_showDesktopMinimizedCount = 0;
}

bool KernelCompositor::requestCloseWindow(uint32_t windowId) {
    app::KernelWindow* win = getWindow(windowId);
    #if defined(GXOS_DESKTOP_CLEANUP_RUNTIME_PASS)
    log_close_target("[compositor] requestCloseWindow", win);
    #endif
    if (!win || !win->owner) {
        #if defined(GXOS_DESKTOP_CLEANUP_RUNTIME_PASS)
        serial::puts("[compositor] close-result=rejected reason=no-owner\n");
        #endif
        return false;
    }
    if (!(win->flags & app::WF_VISIBLE)) {
        #if defined(GXOS_DESKTOP_CLEANUP_RUNTIME_PASS)
        serial::puts("[compositor] close-result=rejected reason=not-visible\n");
        #endif
        return false;
    }
    if (win->flags & app::WF_MINIMIZED) {
        #if defined(GXOS_DESKTOP_CLEANUP_RUNTIME_PASS)
        serial::puts("[compositor] close-result=rejected reason=minimized\n");
        #endif
        return false;
    }
    if (!(win->flags & app::WF_CLOSABLE)) {
        #if defined(GXOS_DESKTOP_CLEANUP_RUNTIME_PASS)
        serial::puts("[compositor] close-result=rejected reason=not-closable\n");
        #endif
        return false;
    }

    #if defined(GXOS_DESKTOP_CLEANUP_RUNTIME_PASS)
    serial::puts("[compositor] close-result=accepted window=");
    serial::put_hex32(win->id);
    serial::putc('\n');
    #endif
    win->owner->requestClose();
    return true;
}

void KernelCompositor::closeWindow(uint32_t windowId) {
    requestCloseWindow(windowId);
}

int KernelCompositor::findWindowIndex(uint32_t windowId) {
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (s_windows[i].valid && s_windows[i].window && s_windows[i].window->id == windowId) {
            return i;
        }
    }
    return -1;
}

HitTestResult KernelCompositor::hitTest(int x, int y, app::KernelWindow** outWindow) {
    *outWindow = nullptr;
    
    // Test windows from front to back
    for (int i = MAX_WINDOWS - 1; i >= 0; i--) {
        if (!s_windows[i].valid || !s_windows[i].window) continue;
        
        app::KernelWindow* win = s_windows[i].window;
        if (win->flags & app::WF_MINIMIZED) continue;
        if (!(win->flags & app::WF_VISIBLE)) continue;
        
        // Check bounds
        if (x < win->x || x >= win->x + win->w || y < win->y || y >= win->y + win->h) {
            continue;
        }
        
        *outWindow = win;
        
        // Check titlebar buttons (if has titlebar)
        if (win->flags & app::WF_TITLEBAR) {
            int titlebarY = win->y;
            int titlebarH = TITLEBAR_HEIGHT;
            
            if (y >= titlebarY && y < titlebarY + titlebarH) {
                // Close button
                int closeBtnX = win->x + win->w - BUTTON_GAP - BUTTON_SIZE;
                if (x >= closeBtnX && x < closeBtnX + BUTTON_SIZE &&
                    y >= titlebarY + 4 && y < titlebarY + 4 + BUTTON_SIZE) {
                    return HitTestResult::CloseButton;
                }
                
                // Maximize button
                int maxBtnX = closeBtnX - BUTTON_GAP - BUTTON_SIZE;
                if (x >= maxBtnX && x < maxBtnX + BUTTON_SIZE &&
                    y >= titlebarY + 4 && y < titlebarY + 4 + BUTTON_SIZE) {
                    return HitTestResult::MaximizeButton;
                }
                
                // Minimize button
                int minBtnX = maxBtnX - BUTTON_GAP - BUTTON_SIZE;
                if (x >= minBtnX && x < minBtnX + BUTTON_SIZE &&
                    y >= titlebarY + 4 && y < titlebarY + 4 + BUTTON_SIZE) {
                    return HitTestResult::MinimizeButton;
                }
                
                return HitTestResult::Titlebar;
            }
        }
        
        // Check resize corner
        if ((win->flags & app::WF_RESIZABLE) && !(win->flags & app::WF_MAXIMIZED)) {
            int resizeX = win->x + win->w - RESIZE_MARGIN;
            int resizeY = win->y + win->h - RESIZE_MARGIN;
            if (x >= resizeX && y >= resizeY) {
                return HitTestResult::ResizeCorner;
            }
        }
        
        return HitTestResult::Client;
    }
    
    return HitTestResult::None;
}

void KernelCompositor::handleMouseMove(int32_t mx, int32_t my) {
    if (!s_initialized) return;
    
    // Handle drag
    if (s_dragState.active) {
        app::KernelWindow* win = getWindow(s_dragState.windowId);
        if (win) {
            if (s_dragState.isResize) {
                int newW = s_dragState.resizeStartW + (mx - s_dragState.startX);
                int newH = s_dragState.resizeStartH + (my - s_dragState.startY);
                int minimumWidth = win->minimumWidth > MIN_WINDOW_WIDTH
                    ? win->minimumWidth : MIN_WINDOW_WIDTH;
                int minimumHeight = win->minimumHeight > MIN_WINDOW_HEIGHT
                    ? win->minimumHeight : MIN_WINDOW_HEIGHT;
                int maximumWidth = win->maximumWidth;
                int maximumHeight = win->maximumHeight;
                const int availableWidth = static_cast<int>(s_workX + s_workW) - win->x;
                const int availableHeight = static_cast<int>(s_workY + s_workH) - win->y;
                if (maximumWidth > 0 && availableWidth >= minimumWidth &&
                    maximumWidth > availableWidth) maximumWidth = availableWidth;
                if (maximumHeight > 0 && availableHeight >= minimumHeight &&
                    maximumHeight > availableHeight) maximumHeight = availableHeight;
                if (maximumWidth > 0 && maximumWidth < minimumWidth)
                    maximumWidth = minimumWidth;
                if (maximumHeight > 0 && maximumHeight < minimumHeight)
                    maximumHeight = minimumHeight;

                if (newW < minimumWidth) newW = minimumWidth;
                if (newH < minimumHeight) newH = minimumHeight;
                if (maximumWidth > 0 && newW > maximumWidth) newW = maximumWidth;
                if (maximumHeight > 0 && newH > maximumHeight) newH = maximumHeight;
                win->w = newW;
                win->h = newH;
            } else {
                win->x = mx - s_dragState.offsetX;
                win->y = my - s_dragState.offsetY;
                // Clamp to screen
                if (win->x < (int)s_workX) win->x = (int)s_workX;
                if (win->y < (int)s_workY) win->y = (int)s_workY;
                if (win->x + win->w > (int)(s_workX + s_workW)) win->x = (int)(s_workX + s_workW) - win->w;
                if (win->y + win->h > (int)(s_workY + s_workH)) 
                    win->y = (int)(s_workY + s_workH) - win->h;
            }
            win->dirty = true;
        }
        return;
    }
    
    // Update hover states
    updateHoverStates(mx, my);

    app::KernelWindow* hitWin = nullptr;
    HitTestResult hit = hitTest(mx, my, &hitWin);
    if (hit == HitTestResult::Client && hitWin && hitWin->owner) {
        int localX = mx - hitWin->x;
        int localY = my - hitWin->y - TITLEBAR_HEIGHT;
        hitWin->owner->onMouseMove(localX, localY);
    }
}

void KernelCompositor::handleMouseDown(int32_t mx, int32_t my, uint8_t button) {
    if (!s_initialized) return;
    
    // Track that we have an active button press
    s_buttonPressActive = true;
    
    app::KernelWindow* hitWin = nullptr;
    HitTestResult hit = hitTest(mx, my, &hitWin);
    
    if (hit == HitTestResult::None) {
        return;
    }
    
    if (!hitWin) return;
    
    // Focus this window
    setFocus(hitWin->id);
    
    switch (hit) {
        case HitTestResult::CloseButton:
            hitWin->closeBtnPressed = true;
            hitWin->dirty = true;
            break;
            
        case HitTestResult::MaximizeButton:
            hitWin->maxBtnPressed = true;
            hitWin->dirty = true;
            break;
            
        case HitTestResult::MinimizeButton:
            hitWin->minBtnPressed = true;
            hitWin->dirty = true;
            break;
            
        case HitTestResult::Titlebar:
            if (!(hitWin->flags & app::WF_MAXIMIZED)) {
                s_dragState.active = true;
                s_dragState.windowId = hitWin->id;
                s_dragState.startX = mx;
                s_dragState.startY = my;
                s_dragState.offsetX = mx - hitWin->x;
                s_dragState.offsetY = my - hitWin->y;
                s_dragState.isResize = false;
            }
            break;
            
        case HitTestResult::ResizeCorner:
            s_dragState.active = true;
            s_dragState.windowId = hitWin->id;
            s_dragState.startX = mx;
            s_dragState.startY = my;
            s_dragState.resizeStartW = hitWin->w;
            s_dragState.resizeStartH = hitWin->h;
            s_dragState.isResize = true;
            break;
            
        case HitTestResult::Client:
            // Check widgets first; bare-metal apps use these for toolbar buttons.
            for (int i = hitWin->widgetCount - 1; i >= 0; --i) {
                app::Widget* widget = &hitWin->widgets[i];
                if (!widget->visible || !widget->enabled) continue;
                int localX = mx - hitWin->x;
                int localY = my - hitWin->y - TITLEBAR_HEIGHT;
                if (localX >= widget->x && localX < widget->x + widget->w &&
                    localY >= widget->y && localY < widget->y + widget->h) {
                    widget->pressed = true;
                    hitWin->dirty = true;
                    return;
                }
            }

            // Forward to app content
            if (hitWin->owner) {
                int localX = mx - hitWin->x;
                int localY = my - hitWin->y - TITLEBAR_HEIGHT;
                hitWin->owner->onMouseDown(localX, localY, button);
            }
            break;
            
        default:
            break;
    }
}

void KernelCompositor::handleMouseUp(int32_t mx, int32_t my, uint8_t button) {
    if (!s_initialized) return;
    
    // Clear button press tracking
    s_buttonPressActive = false;
    
    // End drag
    if (s_dragState.active) {
        s_dragState.active = false;
    }
    
    // Check button releases
    app::KernelWindow* hitWin = nullptr;
    HitTestResult hit = hitTest(mx, my, &hitWin);
    
    // Process button clicks
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (!s_windows[i].valid || !s_windows[i].window) continue;
        app::KernelWindow* win = s_windows[i].window;
        
        if (win->closeBtnPressed) {
            win->closeBtnPressed = false;
            win->dirty = true;
            if (hitWin == win && hit == HitTestResult::CloseButton) {
                #if defined(GXOS_DESKTOP_CLEANUP_RUNTIME_PASS)
                log_close_target("[compositor] titlebar close-request", win);
                #endif
                closeWindow(win->id);
                return;  // Window was closed, stop processing
            }
        }
        
        if (win->maxBtnPressed) {
            win->maxBtnPressed = false;
            win->dirty = true;
            if (hitWin == win && hit == HitTestResult::MaximizeButton) {
                maximizeWindow(win->id);
            }
        }
        
        if (win->minBtnPressed) {
            win->minBtnPressed = false;
            win->dirty = true;
            if (hitWin == win && hit == HitTestResult::MinimizeButton) {
                minimizeWindow(win->id);
            }
        }
    }
    
    // Forward to app
    if (hit == HitTestResult::Client && hitWin && hitWin->owner) {
        int localX = mx - hitWin->x;
        int localY = my - hitWin->y - TITLEBAR_HEIGHT;
        for (int i = 0; i < hitWin->widgetCount; ++i) {
            app::Widget* widget = &hitWin->widgets[i];
            if (!widget->pressed) continue;
            widget->pressed = false;
            hitWin->dirty = true;
            if (widget->visible && widget->enabled &&
                localX >= widget->x && localX < widget->x + widget->w &&
                localY >= widget->y && localY < widget->y + widget->h) {
                hitWin->owner->onWidgetClick(widget->id);
                return;
            }
        }
        hitWin->owner->onMouseUp(localX, localY, button);
    }
}

void KernelCompositor::handleMouseWheel(int32_t mx, int32_t my, int8_t wheelDelta) {
    if (!s_initialized || wheelDelta == 0) return;

    app::KernelWindow* hitWin = nullptr;
    HitTestResult hit = hitTest(mx, my, &hitWin);
    if (hit != HitTestResult::Client || !hitWin || !hitWin->owner) {
        return;
    }

    int localX = mx - hitWin->x;
    int localY = my - hitWin->y - TITLEBAR_HEIGHT;
    hitWin->owner->onMouseWheel(localX, localY, wheelDelta);
}

void KernelCompositor::handleKeyDown(uint32_t keyCode) {
    handleKeyDownWithModifiers(keyCode, 0u);
}

void KernelCompositor::handleKeyDownWithModifiers(uint32_t keyCode, uint32_t modifiers) {
    app::KernelWindow* focused = getFocusedWindow();
    if (focused && focused->owner) {
        focused->owner->onKeyDownWithModifiers(keyCode, modifiers);
    }
}

void KernelCompositor::handleKeyChar(char c) {
    app::KernelWindow* focused = getFocusedWindow();
    if (focused && focused->owner) {
        focused->owner->onKeyChar(c);
    }
}

void KernelCompositor::updateHoverStates(int mx, int my) {
    app::KernelWindow* hitWin = nullptr;
    HitTestResult hit = hitTest(mx, my, &hitWin);
    
    // Clear old hover states
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (s_windows[i].valid && s_windows[i].window) {
            clearHoverStates(s_windows[i].window);
        }
    }
    
    // Set new hover states
    if (hitWin) {
        switch (hit) {
            case HitTestResult::CloseButton:
                hitWin->closeBtnHover = true;
                break;
            case HitTestResult::MaximizeButton:
                hitWin->maxBtnHover = true;
                break;
            case HitTestResult::MinimizeButton:
                hitWin->minBtnHover = true;
                break;
            case HitTestResult::Client: {
                int localX = mx - hitWin->x;
                int localY = my - hitWin->y - TITLEBAR_HEIGHT;
                for (int i = 0; i < hitWin->widgetCount; ++i) {
                    app::Widget* widget = &hitWin->widgets[i];
                    if (!widget->visible || !widget->enabled) continue;
                    widget->hover = localX >= widget->x && localX < widget->x + widget->w &&
                                    localY >= widget->y && localY < widget->y + widget->h;
                }
                break;
            }
            default:
                break;
        }
        hitWin->dirty = true;
    }
    
    s_hoverWindowId = hitWin ? hitWin->id : 0;
    s_hoverResult = hit;
}

void KernelCompositor::clearHoverStates(app::KernelWindow* window) {
    if (window) {
        window->closeBtnHover = false;
        window->maxBtnHover = false;
        window->minBtnHover = false;
        for (int i = 0; i < window->widgetCount; ++i) {
            window->widgets[i].hover = false;
        }
    }
}

void KernelCompositor::drawAllWindows() {
    if (!s_initialized) return;
    
    // Draw windows from back to front
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (s_windows[i].valid && s_windows[i].window) {
            app::KernelWindow* win = s_windows[i].window;
            if (!(win->flags & app::WF_MINIMIZED) && (win->flags & app::WF_VISIBLE)) {
                drawWindow(win);
            }
        }
    }
}

void KernelCompositor::drawWindow(app::KernelWindow* window) {
    if (!window) return;
    
    uint32_t x = (uint32_t)window->x;
    uint32_t y = (uint32_t)window->y;
    uint32_t w = (uint32_t)window->w;
    uint32_t h = (uint32_t)window->h;
    bool focused = (window->flags & app::WF_FOCUSED) != 0;
    
    // Window shadow
    fillRect(x + 3, y + 3, w, h, rgb(15, 15, 20));
    
    // Window background
    fillRect(x, y, w, h, rgb(34, 34, 34));
    
    // Border
    uint32_t borderColor = focused ? rgb(85, 136, 170) : rgb(51, 51, 51);
    drawRect(x, y, w, h, borderColor);
    
    // Titlebar
    if (window->flags & app::WF_TITLEBAR) {
        drawTitlebar(window);
    }
    
    // Client area
    uint32_t clientY = y + TITLEBAR_HEIGHT;
    uint32_t clientH = h - TITLEBAR_HEIGHT;
    
    // Let app draw custom content
    if (window->owner) {
        window->owner->draw(x, clientY, w, clientH);
    }

    // Draw widgets over the app content
    for (int i = 0; i < window->widgetCount; i++) {
        app::Widget* widget = &window->widgets[i];
        if (widget->visible) {
            drawWidget(widget, x, clientY);
        }
    }
    
    // Resize grip
    if ((window->flags & app::WF_RESIZABLE) && !(window->flags & app::WF_MAXIMIZED)) {
        uint32_t gripX = x + w - 14;
        uint32_t gripY = y + h - 14;
        uint32_t gripColor = rgb(119, 119, 119);
        for (int line = 0; line < 3; line++) {
            int off = line * 4;
            framebuffer::put_pixel(gripX + 12 - off, gripY + 11, gripColor);
            framebuffer::put_pixel(gripX + 11 - off, gripY + 10, gripColor);
            framebuffer::put_pixel(gripX + 11, gripY + 11 - off, gripColor);
            framebuffer::put_pixel(gripX + 10, gripY + 10 - off, gripColor);
        }
    }
    
    window->dirty = false;
}

void KernelCompositor::drawTitlebar(app::KernelWindow* window) {
    uint32_t x = (uint32_t)window->x;
    uint32_t y = (uint32_t)window->y;
    uint32_t w = (uint32_t)window->w;
    bool focused = (window->flags & app::WF_FOCUSED) != 0;
    
    // Titlebar background
    uint32_t titlebarColor = focused ? rgb(43, 80, 111) : rgb(17, 17, 17);
    fillRect(x + 1, y + 1, w - 2, TITLEBAR_HEIGHT - 1, titlebarColor);
    
    // Title text
    int titleW = measureText(window->title);
    uint32_t titleX = x + (w - titleW) / 2;
    uint32_t titleColor = focused ? rgb(240, 240, 240) : rgb(150, 150, 160);
    drawText(titleX, y + 7, window->title, titleColor);
    
    // Buttons
    uint32_t btnY = y + 4;
    
    // Close button
    if (window->flags & app::WF_CLOSABLE) {
        uint32_t closeBtnX = x + w - BUTTON_GAP - BUTTON_SIZE;
        uint32_t closeColor = window->closeBtnPressed ? rgb(200, 50, 50) :
                              window->closeBtnHover ? rgb(170, 64, 64) : rgb(120, 40, 40);
        fillRect(closeBtnX, btnY, BUTTON_SIZE, BUTTON_SIZE, closeColor);
        drawRect(closeBtnX, btnY, BUTTON_SIZE, BUTTON_SIZE, rgb(80, 80, 80));
        
        // X icon
        uint32_t iconColor = rgb(250, 250, 250);
        for (int i = 0; i < 8; i++) {
            framebuffer::put_pixel(closeBtnX + 4 + i, btnY + 4 + i, iconColor);
            framebuffer::put_pixel(closeBtnX + 4 + i + 1, btnY + 4 + i, iconColor);
            framebuffer::put_pixel(closeBtnX + BUTTON_SIZE - 5 - i, btnY + 4 + i, iconColor);
            framebuffer::put_pixel(closeBtnX + BUTTON_SIZE - 4 - i, btnY + 4 + i, iconColor);
        }
    }
    
    // Maximize button
    uint32_t maxBtnX = x + w - BUTTON_GAP * 2 - BUTTON_SIZE * 2;
    uint32_t maxColor = window->maxBtnPressed ? rgb(70, 70, 80) :
                        window->maxBtnHover ? rgb(60, 60, 70) : rgb(46, 46, 46);
    fillRect(maxBtnX, btnY, BUTTON_SIZE, BUTTON_SIZE, maxColor);
    drawRect(maxBtnX, btnY, BUTTON_SIZE, BUTTON_SIZE, rgb(80, 80, 80));
    
    // Max/restore icon
    uint32_t iconColor = rgb(250, 250, 250);
    if (window->flags & app::WF_MAXIMIZED) {
        drawRect(maxBtnX + 5, btnY + 3, 7, 7, iconColor);
        drawRect(maxBtnX + 3, btnY + 5, 7, 7, iconColor);
    } else {
        drawRect(maxBtnX + 4, btnY + 4, 8, 8, iconColor);
    }
    
    // Minimize button
    uint32_t minBtnX = x + w - BUTTON_GAP * 3 - BUTTON_SIZE * 3;
    uint32_t minColor = window->minBtnPressed ? rgb(70, 70, 80) :
                        window->minBtnHover ? rgb(60, 60, 70) : rgb(46, 46, 46);
    fillRect(minBtnX, btnY, BUTTON_SIZE, BUTTON_SIZE, minColor);
    drawRect(minBtnX, btnY, BUTTON_SIZE, BUTTON_SIZE, rgb(80, 80, 80));
    hline(minBtnX + 4, btnY + BUTTON_SIZE - 5, 8, iconColor);
}

void KernelCompositor::drawWidget(app::Widget* widget, uint32_t winX, uint32_t winY) {
    if (!widget || !widget->visible) return;
    
    uint32_t x = winX + widget->x;
    uint32_t y = winY + widget->y;
    uint32_t w = (uint32_t)widget->w;
    uint32_t h = (uint32_t)widget->h;
    
    switch (widget->type) {
        case app::WidgetType::Label:
            drawText(x, y + (h - kGlyphH) / 2, widget->text, widget->fgColor);
            break;
            
        case app::WidgetType::Button: {
            uint32_t bgColor = widget->pressed ? rgb(70, 100, 150) :
                               widget->hover ? rgb(60, 80, 120) : widget->bgColor;
            fillRect(x, y, w, h, bgColor);
            drawRect(x, y, w, h, rgb(80, 100, 140));
            if (widget->iconPixels && widget->iconWidth > 0 && widget->iconHeight > 0) {
                gxos::gui::ImageBitmap icon{gxos::gui::ImageLoadStatus::Ok, widget->iconPixels, widget->iconWidth, widget->iconHeight};
                gxos::gui::ImageAdapter::DrawToFramebuffer(icon, x + 4, y + (h - 16) / 2, 16, 16);
                drawText(x + 24, y + (h - kGlyphH) / 2, widget->text, widget->fgColor);
            } else {
                drawTextCentered(x, y, w, h, widget->text, widget->fgColor);
            }
            break;
        }
        
        case app::WidgetType::TextBox:
            fillRect(x, y, w, h, rgb(45, 45, 55));
            drawRect(x, y, w, h, rgb(70, 80, 100));
            drawText(x + 4, y + (h - kGlyphH) / 2, widget->text, widget->fgColor);
            break;
            
        case app::WidgetType::CheckBox: {
            // Box
            fillRect(x, y + (h - 12) / 2, 12, 12, widget->value ? rgb(74, 158, 255) : rgb(45, 45, 55));
            drawRect(x, y + (h - 12) / 2, 12, 12, rgb(80, 100, 140));
            // Check mark
            if (widget->value) {
                uint32_t checkColor = rgb(255, 255, 255);
                framebuffer::put_pixel(x + 3, y + (h - 12) / 2 + 6, checkColor);
                framebuffer::put_pixel(x + 4, y + (h - 12) / 2 + 7, checkColor);
                framebuffer::put_pixel(x + 5, y + (h - 12) / 2 + 8, checkColor);
                framebuffer::put_pixel(x + 6, y + (h - 12) / 2 + 7, checkColor);
                framebuffer::put_pixel(x + 7, y + (h - 12) / 2 + 6, checkColor);
                framebuffer::put_pixel(x + 8, y + (h - 12) / 2 + 5, checkColor);
                framebuffer::put_pixel(x + 9, y + (h - 12) / 2 + 4, checkColor);
            }
            // Label
            drawText(x + 18, y + (h - kGlyphH) / 2, widget->text, widget->fgColor);
            break;
        }
        
        case app::WidgetType::ProgressBar:
            fillRect(x, y, w, h, rgb(30, 30, 40));
            drawRect(x, y, w, h, rgb(60, 70, 90));
            if (widget->value > 0) {
                uint32_t fillW = (w - 4) * (uint32_t)widget->value / 100;
                fillRect(x + 2, y + 2, fillW, h - 4, rgb(74, 158, 255));
            }
            break;
            
        default:
            break;
    }
}

bool KernelCompositor::hasWindows() {
    return s_windowCount > 0;
}

int KernelCompositor::getWindowCount() {
    return s_windowCount;
}

bool KernelCompositor::isPointOverWindow(int32_t x, int32_t y) {
    app::KernelWindow* win = nullptr;
    return hitTest(x, y, &win) != HitTestResult::None;
}

bool KernelCompositor::isButtonPressActive() {
    return s_buttonPressActive;
}

uint32_t KernelCompositor::generateWindowId() {
    return s_nextWindowId++;
}

// ============================================================
// Drawing helpers
// ============================================================

uint32_t KernelCompositor::rgb(uint8_t r, uint8_t g, uint8_t b) {
    return 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

void KernelCompositor::fillRect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    framebuffer::fill_rect(x, y, w, h, color);
}

void KernelCompositor::drawRect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    hline(x, y, w, color);
    hline(x, y + h - 1, w, color);
    vline(x, y, h, color);
    vline(x + w - 1, y, h, color);
}

void KernelCompositor::hline(uint32_t x, uint32_t y, uint32_t w, uint32_t color) {
    for (uint32_t i = 0; i < w; i++) {
        framebuffer::put_pixel(x + i, y, color);
    }
}

void KernelCompositor::vline(uint32_t x, uint32_t y, uint32_t h, uint32_t color) {
    for (uint32_t i = 0; i < h; i++) {
        framebuffer::put_pixel(x, y + i, color);
    }
}

void KernelCompositor::drawText(uint32_t x, uint32_t y, const char* text, uint32_t color, int scale) {
    uint32_t cx = x;
    while (*text) {
        const uint8_t* g = getGlyph(*text);
        if (g) {
            for (int col = 0; col < kGlyphW; col++) {
                uint8_t bits = g[col];
                for (int row = 0; row < kGlyphH; row++) {
                    if (bits & (1 << row)) {
                        for (int sy = 0; sy < scale; sy++) {
                            for (int sx = 0; sx < scale; sx++) {
                                framebuffer::put_pixel(cx + col * scale + sx, y + row * scale + sy, color);
                            }
                        }
                    }
                }
            }
        }
        cx += (kGlyphW + kGlyphSpacing) * scale;
        text++;
    }
}

void KernelCompositor::drawTextCentered(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                                         const char* text, uint32_t color, int scale) {
    int tw = measureText(text) * scale;
    int th = kGlyphH * scale;
    uint32_t tx = x + (w > (uint32_t)tw ? (w - tw) / 2 : 0);
    uint32_t ty = y + (h > (uint32_t)th ? (h - th) / 2 : 0);
    drawText(tx, ty, text, color, scale);
}

// ============================================================
// TaskbarManager implementation
// ============================================================

static const char* kTaskbarPinStorePath = "/desktop.taskbar.pins";
static const int kTaskbarButtonMaxWidth = 176;
static const int kTaskbarMenuWidth = 220;
static const int kTaskbarMenuRowHeight = 20;

static int taskbarTextLength(const char* value) {
    int length = 0;
    if (!value) return 0;
    while (value[length]) ++length;
    return length;
}

static void copyTaskbarText(char* out, int outSize, const char* value) {
    if (!out || outSize <= 0) return;
    int i = 0;
    if (value) {
        while (value[i] && i + 1 < outSize) {
            out[i] = value[i];
            ++i;
        }
    }
    out[i] = '\0';
}

static bool validTaskbarAppId(const char* value) {
    if (!gxos::gui::HasCanonicalTaskbarAppIdentity(value)) return false;
    int length = 0;
    while (value[length]) {
        const unsigned char c = static_cast<unsigned char>(value[length]);
        if (length >= kernel::app::MAX_APP_ID - 1 || c < 32u || c == 127u || c == '=') return false;
        ++length;
    }
    return true;
}

static bool taskbarStringsEqual(const char* left, const char* right) {
    if (!left || !right) return false;
    while (*left && *right && *left == *right) {
        ++left;
        ++right;
    }
    return *left == *right;
}

static void makeTaskbarFitText(char* out, int outSize, const char* value, int maxChars) {
    if (!out || outSize <= 0) return;
    const int length = taskbarTextLength(value);
    if (maxChars <= 0) {
        out[0] = '\0';
        return;
    }
    if (length <= maxChars) {
        copyTaskbarText(out, outSize, value);
        return;
    }
    if (maxChars < 4) {
        int dots = maxChars < outSize - 1 ? maxChars : outSize - 1;
        for (int i = 0; i < dots; ++i) out[i] = '.';
        out[dots] = '\0';
        return;
    }
    int copied = maxChars - 3;
    if (copied + 4 > outSize) copied = outSize - 4;
    if (copied < 0) copied = 0;
    int i = 0;
    while (i < copied && value[i]) {
        out[i] = value[i];
        ++i;
    }
    out[i++] = '.';
    out[i++] = '.';
    out[i++] = '.';
    out[i] = '\0';
}

TaskbarButton TaskbarManager::s_buttons[MAX_TASKBAR_DISPLAY_BUTTONS];
int TaskbarManager::s_buttonCount = 0;
int TaskbarManager::s_visibleButtonCount = 0;
int TaskbarManager::s_overflowFirstIndex = 0;
int TaskbarManager::s_previewButtonIndex = -1;
uint32_t TaskbarManager::s_previewWindowIds[4] = {};
int TaskbarManager::s_previewWindowCount = 0;
int TaskbarManager::s_previewX = 0;
int TaskbarManager::s_previewY = 0;
int TaskbarManager::s_previewW = 0;
int TaskbarManager::s_previewH = 0;
int TaskbarManager::s_previewCardX[4] = {};
int TaskbarManager::s_previewCardY[4] = {};
int TaskbarManager::s_previewCardW[4] = {};
int TaskbarManager::s_previewCardH[4] = {};
uint32_t TaskbarManager::s_screenW = 0;
uint32_t TaskbarManager::s_screenH = 0;
uint32_t TaskbarManager::s_taskbarH = 0;
uint32_t TaskbarManager::s_startX = 0;
uint32_t TaskbarManager::s_taskbarX = 0;
uint32_t TaskbarManager::s_taskbarY = 0;
uint32_t TaskbarManager::s_taskbarW = 0;
bool TaskbarManager::s_vertical = false;
gxos::gui::TaskbarSizeMode TaskbarManager::s_sizeMode = gxos::gui::TaskbarSizeMode::Standard;
bool TaskbarManager::s_showLabels = true;
bool TaskbarManager::s_showActivity = true;
bool TaskbarManager::s_showPreviews = true;
bool TaskbarManager::s_taskbarVisible = true;
uint32_t TaskbarManager::s_startY = 0;
int32_t TaskbarManager::s_mouseX = -1;
int32_t TaskbarManager::s_mouseY = -1;
char TaskbarManager::s_pinnedAppIds[MAX_TASKBAR_PINS][app::MAX_APP_ID] = {};
int TaskbarManager::s_pinCount = 0;
TaskbarPinPersistenceState TaskbarManager::s_pinPersistence = TaskbarPinPersistenceState::Unsupported;
bool TaskbarManager::s_pinStoreCorrupt = false;
gxos::gui::TaskbarActivityStore TaskbarManager::s_activityStore;
uint32_t TaskbarManager::s_fileOperationOwnerWindowId = 0;
TaskbarManager::MenuItem TaskbarManager::s_menuItems[MAX_TASKBAR_MENU_ITEMS];
int TaskbarManager::s_menuItemCount = 0;
int TaskbarManager::s_menuHover = -1;
int TaskbarManager::s_menuScroll = 0;
int TaskbarManager::s_menuX = 0;
int TaskbarManager::s_menuY = 0;
int TaskbarManager::s_menuW = 0;
int TaskbarManager::s_menuH = 0;
bool TaskbarManager::s_menuOpen = false;
char TaskbarManager::s_menuAppId[app::MAX_APP_ID] = {};

void TaskbarManager::init(uint32_t screenW, uint32_t screenH, uint32_t taskbarH, uint32_t startX) {
    s_screenW = screenW;
    s_screenH = screenH;
    s_taskbarH = taskbarH < screenH ? taskbarH : screenH;
    s_startX = startX;
    s_taskbarX = 0;
    s_taskbarY = screenH - s_taskbarH;
    s_taskbarW = screenW;
    s_vertical = false;
    s_startY = s_taskbarY + 6;
    s_buttonCount = 0;
    s_mouseX = -1;
    s_mouseY = -1;
    s_menuOpen = false;
    s_menuItemCount = 0;
    s_pinCount = 0;
    s_pinStoreCorrupt = false;
    s_fileOperationOwnerWindowId = 0;
    loadPinnedApplications();
}

void TaskbarManager::setLayout(uint32_t taskbarX, uint32_t taskbarY, uint32_t taskbarW, uint32_t taskbarH,
                               bool vertical, uint32_t buttonStartX, uint32_t buttonStartY) {
    s_taskbarX = taskbarX;
    s_taskbarY = taskbarY;
    s_taskbarW = taskbarW;
    s_taskbarH = taskbarH;
    s_vertical = vertical;
    s_startX = buttonStartX;
    s_startY = buttonStartY;
}

void TaskbarManager::setPresentation(gxos::gui::TaskbarSizeMode size, bool showLabels,
                                     bool showActivity, bool showPreviews, bool taskbarVisible) {
    s_sizeMode = size;
    s_showLabels = showLabels;
    s_showActivity = showActivity;
    s_showPreviews = showPreviews;
    s_taskbarVisible = taskbarVisible;
    if (!s_showPreviews) closePreview();
}

void TaskbarManager::updateButtons() {
    s_buttonCount = 0;
    for (int i = 0; i < s_pinCount && s_buttonCount < MAX_TASKBAR_BUTTONS; ++i) {
        TaskbarButton& button = s_buttons[s_buttonCount++];
        button = TaskbarButton();
        copyTaskbarText(button.appId, app::MAX_APP_ID, s_pinnedAppIds[i]);
        button.pinned = true;
        const app::AppInfo* info = app::AppManager::getAppInfoById(button.appId);
        button.relaunchable = info && info->available && info->factory;
        button.missingApplication = !button.relaunchable;
        if (info) {
            copyTaskbarText(button.title, app::MAX_TITLE_LEN, info->name);
            button.iconColor = info->iconColor;
        } else {
            copyTaskbarText(button.title, app::MAX_TITLE_LEN, "Missing app");
        }
    }

    // The compositor's current windows remain the source of truth. Canonical
    // identities are grouped; windows without one receive their own entry.
    for (int i = 0; i < MAX_WINDOWS; ++i) {
        if (!KernelCompositor::s_windows[i].valid) continue;
        app::KernelWindow* window = KernelCompositor::s_windows[i].window;
        if (!window) continue;
        const char* appId = window->owner ? window->owner->getRuntimeAppId() : "";
        int buttonIndex = -1;
        if (gxos::gui::HasCanonicalTaskbarAppIdentity(appId)) {
            for (int j = 0; j < s_buttonCount; ++j) {
                if (gxos::gui::SameCanonicalTaskbarAppIdentity(s_buttons[j].appId, appId)) {
                    buttonIndex = j;
                    break;
                }
            }
        }
        if (buttonIndex < 0) {
            if (s_buttonCount >= MAX_TASKBAR_BUTTONS) break;
            buttonIndex = s_buttonCount++;
            TaskbarButton& button = s_buttons[buttonIndex];
            button = TaskbarButton();
            if (gxos::gui::HasCanonicalTaskbarAppIdentity(appId)) {
                copyTaskbarText(button.appId, app::MAX_APP_ID, appId);
                const app::AppInfo* info = app::AppManager::getAppInfoById(appId);
                if (info) {
                    copyTaskbarText(button.title, app::MAX_TITLE_LEN, info->name);
                    button.iconColor = info->iconColor;
                    button.relaunchable = info->available && info->factory;
                } else {
                    copyTaskbarText(button.title, app::MAX_TITLE_LEN,
                                    window->title[0] ? window->title : appId);
                }
            } else {
                // No identity means no grouping, even when titles match.
                copyTaskbarText(button.title, app::MAX_TITLE_LEN,
                                window->title[0] ? window->title : "Window");
            }
        }

        TaskbarButton& button = s_buttons[buttonIndex];
        if (button.windowCount >= MAX_WINDOWS) continue;
        const int member = button.windowCount++;
        button.windowIds[member] = window->id;
        copyTaskbarText(button.windowTitles[member], app::MAX_TITLE_LEN,
                        window->title[0] ? window->title : button.title);
        button.running = true;
        if (window->flags & app::WF_FOCUSED) {
            button.active = true;
            button.windowId = window->id;
        } else if (button.windowId == 0 && !(window->flags & app::WF_MINIMIZED)) {
            button.windowId = window->id;
        } else if (button.windowId == 0) {
            button.windowId = window->id;
        }
    }

    for (int i = 0; i < s_buttonCount; ++i) {
        TaskbarButton& button = s_buttons[i];
        button.minimized = button.windowCount > 0;
        for (int j = 0; j < button.windowCount; ++j) {
            app::KernelWindow* window = KernelCompositor::getWindow(button.windowIds[j]);
            if (window && !(window->flags & app::WF_MINIMIZED)) button.minimized = false;
            if (window && (window->flags & app::WF_FOCUSED)) {
                button.active = true;
                button.windowId = window->id;
            }
        }
        button.activity = s_showActivity ? s_activityStore.resolve(button.appId, button.windowIds,
            static_cast<size_t>(button.windowCount)) : gxos::gui::TaskbarActivity{};
    }

    // Preserve application order while the focused window and z-order change.
    for (int i = s_pinCount + 1; i < s_buttonCount; ++i) {
        TaskbarButton entry = s_buttons[i];
        uint32_t orderKey = 0xFFFFFFFFu;
        for (int w = 0; w < entry.windowCount; ++w) {
            if (entry.windowIds[w] < orderKey) orderKey = entry.windowIds[w];
        }
        int j = i;
        while (j > s_pinCount) {
            uint32_t previousKey = 0xFFFFFFFFu;
            for (int w = 0; w < s_buttons[j - 1].windowCount; ++w) {
                if (s_buttons[j - 1].windowIds[w] < previousKey) previousKey = s_buttons[j - 1].windowIds[w];
            }
            if (previousKey <= orderKey) break;
            s_buttons[j] = s_buttons[j - 1];
            --j;
        }
        s_buttons[j] = entry;
    }

    int x = static_cast<int>(s_startX);
    int y = static_cast<int>(s_startY);
    const int maxX = static_cast<int>(s_taskbarX + s_taskbarW);
    const int maxY = static_cast<int>(s_taskbarY + s_taskbarH);
    const int taskbarThickness = s_vertical ? static_cast<int>(s_taskbarW) : static_cast<int>(s_taskbarH);
    const int buttonHeight = s_vertical
        ? (taskbarThickness > 8 ? taskbarThickness - 8 : taskbarThickness)
        : (taskbarThickness > 12 ? taskbarThickness - 12 : taskbarThickness);
    int totalPreferred = 0;
    for (int i = 0; i < s_buttonCount; ++i) {
        const int desired = s_showLabels ? measureText(s_buttons[i].title) + 34 : taskbarThickness;
        const int minimum = s_showLabels ? 58 : (taskbarThickness < 36 ? 36 : taskbarThickness);
        totalPreferred += desired < minimum ? minimum : (desired > kTaskbarButtonMaxWidth ? kTaskbarButtonMaxWidth : desired);
        if (i > 0) totalPreferred += 4;
    }
    const int horizontalAvailable = maxX - x;
    const bool needsOverflow = s_vertical
        ? static_cast<int>(s_buttonCount) > ((maxY - y + 4) / (buttonHeight + 4))
        : totalPreferred > horizontalAvailable;
    const int overflowSpace = needsOverflow ? (s_vertical ? buttonHeight : (horizontalAvailable < 40 ? horizontalAvailable : 40)) : 0;
    const int appLimit = s_vertical ? (maxY - y - overflowSpace) : (maxX - x - overflowSpace - (needsOverflow ? 4 : 0));
    int used = 0;
    s_visibleButtonCount = 0;
    s_overflowFirstIndex = s_buttonCount;
    for (int i = 0; i < s_buttonCount; ++i) {
        TaskbarButton& button = s_buttons[i];
        const int available = s_vertical ? static_cast<int>(s_taskbarW > 8 ? s_taskbarW - 8 : s_taskbarW) : appLimit - used;
        if (available <= 0) break;
        const int desiredW = s_vertical ? available : (s_showLabels ? measureText(button.title) + 34 : taskbarThickness);
        const int minimum = s_showLabels ? 58 : (taskbarThickness < 36 ? 36 : taskbarThickness);
        const int width = s_vertical ? available : (desiredW < minimum ? minimum : (desiredW > kTaskbarButtonMaxWidth ? kTaskbarButtonMaxWidth : desiredW));
        if (needsOverflow && used > 0 && width + 4 > available) break;
        const int boundedW = width < available ? width : available;
        const int availableH = maxY - y;
        if (availableH <= 0) break;
        const int desiredH = buttonHeight;
        const int height = desiredH < availableH ? desiredH : availableH;
        if (boundedW <= 0 || height <= 0) break;
        button.x = x;
        button.y = y;
        button.w = boundedW;
        button.h = height;
        button.hover = s_mouseX >= button.x && s_mouseX < button.x + button.w &&
            s_mouseY >= button.y && s_mouseY < button.y + button.h;
        ++s_visibleButtonCount;
        used += boundedW + (s_vertical ? 4 : 4);
        if (s_vertical) y += button.h + 4;
        else x += button.w + 4;
    }
    s_overflowFirstIndex = s_visibleButtonCount;
    if (needsOverflow && s_buttonCount > s_visibleButtonCount && appLimit + overflowSpace >= 0) {
        TaskbarButton& overflow = s_buttons[s_buttonCount];
        overflow = TaskbarButton();
        overflow.overflowEntry = true;
        copyTaskbarText(overflow.title, app::MAX_TITLE_LEN, "More");
        if (s_vertical) {
            const int height = maxY - y < buttonHeight ? maxY - y : buttonHeight;
            if (height > 0) {
                overflow.x = static_cast<int>(s_taskbarX + 4);
                overflow.y = y;
                overflow.w = static_cast<int>(s_taskbarW > 8 ? s_taskbarW - 8 : s_taskbarW);
                overflow.h = height;
            }
        } else {
            const int width = overflowSpace < maxX - x ? overflowSpace : maxX - x;
            const int height = maxY - static_cast<int>(s_taskbarY) - 12;
            if (width > 0 && height > 0) {
                overflow.x = x;
                overflow.y = static_cast<int>(s_taskbarY) + 6;
                overflow.w = width;
                overflow.h = height;
            }
        }
        overflow.hover = s_mouseX >= overflow.x && s_mouseX < overflow.x + overflow.w &&
            s_mouseY >= overflow.y && s_mouseY < overflow.y + overflow.h;
    }
    if (s_previewButtonIndex >= s_visibleButtonCount || s_previewButtonIndex < 0 ||
        s_buttons[s_previewButtonIndex].windowCount == 0) closePreview();
    updatePreviewLayout();
}

void TaskbarManager::drawButtons() {
    if (!s_taskbarVisible) return;
    updateButtons();

    const int drawCount = s_visibleButtonCount +
        ((s_buttonCount > s_visibleButtonCount && s_buttons[s_buttonCount].overflowEntry) ? 1 : 0);
    for (int i = 0; i < drawCount; i++) {
        TaskbarButton& btn = s_buttons[i];
        
        uint32_t bgColor = btn.overflowEntry ? KernelCompositor::rgb(48, 62, 82) : btn.missingApplication ? KernelCompositor::rgb(86, 48, 50) :
                           btn.active ? KernelCompositor::rgb(70, 100, 150) :
                           btn.hover ? KernelCompositor::rgb(60, 65, 75) :
                                       KernelCompositor::rgb(55, 58, 70);
        
        KernelCompositor::fillRect(btn.x, btn.y, btn.w, btn.h, bgColor);
        if (btn.w >= 18 && btn.h >= 10) {
            const int iconX = s_vertical || !s_showLabels ? btn.x + (btn.w - 8) / 2 : btn.x + 5;
            const int iconY = s_vertical && s_showLabels ? btn.y + 2 : btn.y + (btn.h - 8) / 2;
            KernelCompositor::fillRect(iconX, iconY, 8, 8, btn.missingApplication
                ? KernelCompositor::rgb(230, 105, 96) : btn.iconColor);
        }
        if ((btn.active || btn.running) && btn.w > 4 && btn.h >= 3) {
            KernelCompositor::fillRect(btn.x + 2, btn.y + btn.h - 3, btn.w - 4, 2,
                btn.active ? KernelCompositor::rgb(100, 160, 240) : KernelCompositor::rgb(132, 140, 154));
        }

        if (s_showLabels) {
            char label[app::MAX_TITLE_LEN];
            const int textWidth = s_vertical ? btn.w - 2 : (btn.w > 16 ? btn.w - 16 : btn.w);
            const int labelChars = textWidth > 0 ? textWidth / 6 : 0;
            makeTaskbarFitText(label, sizeof(label), btn.title, labelChars);
            if (s_vertical) {
                KernelCompositor::drawTextCentered(btn.x + 1, btn.y + 10,
                    btn.w > 2 ? btn.w - 2 : btn.w, btn.h > 10 ? btn.h - 10 : 1, label,
                    KernelCompositor::rgb(230, 230, 240));
            } else {
                KernelCompositor::drawTextCentered(btn.x + (btn.w >= 22 ? 14 : 2), btn.y,
                    btn.w > 16 ? btn.w - 16 : btn.w, btn.h, label,
                    KernelCompositor::rgb(230, 230, 240));
            }
        }
        if (btn.windowCount > 1 && btn.w >= 32) {
            char countLabel[4];
            countLabel[0] = static_cast<char>('0' + (btn.windowCount > 9 ? 9 : btn.windowCount));
            countLabel[1] = '\0';
            KernelCompositor::fillRect(btn.x + btn.w - 16, btn.y + 2, 14, 14,
                                       KernelCompositor::rgb(85, 110, 145));
            KernelCompositor::drawTextCentered(btn.x + btn.w - 16, btn.y + 2, 14, 14,
                                               countLabel, KernelCompositor::rgb(245, 245, 250));
        } else if (btn.pinned && btn.windowCount == 0 && btn.w >= 18) {
            KernelCompositor::fillRect(btn.x + btn.w - 7, btn.y + btn.h / 2 - 2, 4, 4,
                                       KernelCompositor::rgb(155, 190, 240));
        }
        if (s_showActivity && btn.activity.attention && btn.w >= 24 && btn.h >= 14) {
            KernelCompositor::fillRect(btn.x + btn.w - 10, btn.y + 3, 6, 6,
                KernelCompositor::rgb(236, 112, 91));
        }
        if (s_showActivity && btn.activity.progress != gxos::gui::TaskbarProgressState::None && btn.w > 4 && btn.h >= 4) {
            const uint32_t color = btn.activity.progress == gxos::gui::TaskbarProgressState::Failed
                ? KernelCompositor::rgb(238, 102, 91)
                : btn.activity.progress == gxos::gui::TaskbarProgressState::Completed
                    ? KernelCompositor::rgb(93, 190, 133) : KernelCompositor::rgb(105, 176, 238);
            const int barW = btn.activity.progress == gxos::gui::TaskbarProgressState::Determinate ||
                    btn.activity.progress == gxos::gui::TaskbarProgressState::Completed
                ? (btn.w - 4) * btn.activity.percent / 100
                : (btn.w - 4) / 3;
            if (barW > 0) KernelCompositor::fillRect(btn.x + 2, btn.y + btn.h - 3, barW, 2, color);
        }
    }

    if (s_menuOpen) {
        KernelCompositor::fillRect(s_menuX + 2, s_menuY + 2, s_menuW, s_menuH,
                                   KernelCompositor::rgb(20, 20, 25));
        KernelCompositor::fillRect(s_menuX, s_menuY, s_menuW, s_menuH,
                                   KernelCompositor::rgb(44, 46, 55));
        KernelCompositor::drawRect(s_menuX, s_menuY, s_menuW, s_menuH,
                                   KernelCompositor::rgb(100, 112, 135));
        const int visibleRows = menuVisibleRowCount();
        for (int row = 0; row < visibleRows; ++row) {
            const int itemIndex = s_menuScroll + row;
            if (itemIndex >= s_menuItemCount) break;
            const int rowY = s_menuY + 2 + row * kTaskbarMenuRowHeight;
            if (rowY + kTaskbarMenuRowHeight > s_menuY + s_menuH) break;
            if (itemIndex == s_menuHover) {
                KernelCompositor::fillRect(s_menuX + 1, rowY, s_menuW - 2, kTaskbarMenuRowHeight,
                                           KernelCompositor::rgb(58, 88, 136));
            }
            const uint32_t textColor = s_menuItems[itemIndex].enabled
                ? KernelCompositor::rgb(235, 236, 242)
                : KernelCompositor::rgb(145, 149, 158);
            KernelCompositor::drawText(s_menuX + 8, rowY + 6,
                                       s_menuItems[itemIndex].label, textColor);
        }
    }
    if (s_previewButtonIndex >= 0 && s_previewW > 0 && s_previewH > 0) {
        KernelCompositor::fillRect(s_previewX + 2, s_previewY + 2, s_previewW, s_previewH,
            KernelCompositor::rgb(16, 18, 24));
        KernelCompositor::fillRect(s_previewX, s_previewY, s_previewW, s_previewH,
            KernelCompositor::rgb(38, 42, 52));
        KernelCompositor::drawRect(s_previewX, s_previewY, s_previewW, s_previewH,
            KernelCompositor::rgb(102, 124, 158));
        const TaskbarButton& button = s_buttons[s_previewButtonIndex];
        for (int card = 0; card < s_previewWindowCount; ++card) {
            const int cx = s_previewCardX[card];
            const int cy = s_previewCardY[card];
            const int cw = s_previewCardW[card];
            const int ch = s_previewCardH[card];
            const uint32_t id = s_previewWindowIds[card];
            app::KernelWindow* window = KernelCompositor::getWindow(id);
            if (!window) continue;
            const bool minimized = (window->flags & app::WF_MINIMIZED) != 0;
            const bool active = (window->flags & app::WF_FOCUSED) != 0;
            KernelCompositor::fillRect(cx, cy, cw, ch,
                minimized ? KernelCompositor::rgb(42, 43, 50) : KernelCompositor::rgb(26, 29, 36));
            KernelCompositor::drawRect(cx, cy, cw, ch,
                active ? KernelCompositor::rgb(106, 158, 232) : KernelCompositor::rgb(76, 84, 100));
            KernelCompositor::fillRect(cx + 7, cy + 7, 10, 10, button.iconColor);
            char title[app::MAX_TITLE_LEN];
            copyTaskbarText(title, sizeof(title), window->title[0] ? window->title : button.title);
            makeTaskbarFitText(title, sizeof(title), title, (cw - 28) / 6);
            KernelCompositor::drawText(cx + 22, cy + 8, title,
                KernelCompositor::rgb(231, 234, 241));
            const char* state = minimized ? "Minimized" : active ? "Active" : "Running";
            KernelCompositor::drawText(cx + 8, cy + ch - 18, state,
                minimized ? KernelCompositor::rgb(163, 166, 176) :
                    active ? KernelCompositor::rgb(127, 177, 240) : KernelCompositor::rgb(184, 190, 202));
        }
    }
}

bool TaskbarManager::handleClick(int32_t mx, int32_t my) {
    updateButtons();
    const int index = findButtonAt(mx, my);
    if (index < 0) return false;
    if (index == s_buttonCount && s_buttons[index].overflowEntry) {
        openOverflowMenu(mx, my);
        return true;
    }
    TaskbarButton& button = s_buttons[index];
    bool focused = false;
    if (button.windowCount > 0) {
        app::KernelWindow* window = KernelCompositor::getWindow(button.windowId);
        focused = window && (window->flags & app::WF_FOCUSED);
    }
    const gxos::gui::TaskbarCoreClickAction action = gxos::gui::DecideTaskbarCoreClick(
        static_cast<unsigned int>(button.windowCount), focused, button.minimized, button.relaunchable);
    switch (action) {
    case gxos::gui::TaskbarCoreClickAction::ShowWindowChooser:
        openMenuForButton(index, mx, my);
        return true;
    case gxos::gui::TaskbarCoreClickAction::Launch:
        app::AppManager::launchAppById(button.appId);
        return true;
    case gxos::gui::TaskbarCoreClickAction::Minimize:
        KernelCompositor::minimizeWindow(button.windowId);
        return true;
    case gxos::gui::TaskbarCoreClickAction::Activate:
        activateWindow(button.windowId);
        return true;
    case gxos::gui::TaskbarCoreClickAction::None:
    default:
        return true;
    }
}

void TaskbarManager::updateHover(int32_t mx, int32_t my) {
    s_mouseX = mx;
    s_mouseY = my;
    if (!s_showPreviews) {
        closePreview();
        return;
    }
    updateButtons();
    gxos::gui::TaskbarPreviewHoverState currentState;
    if (s_previewButtonIndex >= 0)
        currentState.entryIndex = static_cast<size_t>(s_previewButtonIndex);
    const int index = findButtonAt(mx, my);
    const bool hoveredEntryRunning = index >= 0 && index < s_buttonCount && s_buttons[index].windowCount > 0;
    const gxos::gui::TaskbarPreviewHoverState nextState = gxos::gui::DecideTaskbarPreviewHoverTransition(
        currentState, index >= 0 ? static_cast<size_t>(index) : gxos::gui::kTaskbarNoPreviewEntry,
        static_cast<size_t>(s_buttonCount), hoveredEntryRunning,
        currentState.visible() && previewContainsPoint(mx, my), s_menuOpen);
    if (!nextState.visible()) {
        closePreview();
        return;
    }
    if (nextState.entryIndex != currentState.entryIndex) {
        s_previewButtonIndex = static_cast<int>(nextState.entryIndex);
        updatePreviewLayout();
    }
}

bool TaskbarManager::handlePointerDown(int32_t mx, int32_t my, uint8_t button) {
    updateButtons();
    if (s_menuOpen) {
        const int item = menuItemAt(mx, my);
        if (button == 1 && item >= 0) dispatchMenuItem(item);
        closeMenu();
        return true;
    }
    if (button == 1 && previewContainsPoint(mx, my)) {
        const int card = previewCardAt(mx, my);
        if (card >= 0 && card < s_previewWindowCount) {
            const uint32_t windowId = s_previewWindowIds[card];
            closePreview();
            activateWindow(windowId);
            return true;
        }
        return true;
    }
    if (s_previewButtonIndex >= 0) closePreview();
    const int index = findButtonAt(mx, my);
    if (button == 2 && index >= 0) {
        if (index == s_buttonCount && s_buttons[index].overflowEntry) {
            openOverflowMenu(mx, my);
            return true;
        }
        openMenuForButton(index, mx, my);
        return true;
    }
    if (button == 1 && index >= 0) return handleClick(mx, my);
    return false;
}

bool TaskbarManager::handleKeyDown(uint32_t keyCode) {
    if (!s_menuOpen) {
        if (keyCode == 27u && s_previewButtonIndex >= 0) { closePreview(); return true; }
        return false;
    }
    if (keyCode == 27u) {
        closeMenu();
        return true;
    }
    const int visibleRows = menuVisibleRowCount();
    int selected = s_menuHover >= 0 ? s_menuHover : s_menuScroll;
    if (keyCode == 0x100u || keyCode == 0x101u) {
        if (keyCode == 0x100u) --selected;
        else ++selected;
        if (selected < 0) selected = 0;
        if (selected >= s_menuItemCount) selected = s_menuItemCount - 1;
        if (selected < s_menuScroll) s_menuScroll = selected;
        if (selected >= s_menuScroll + visibleRows) s_menuScroll = selected - visibleRows + 1;
        s_menuHover = selected;
        return true;
    }
    if (keyCode == 10u || keyCode == 13u) {
        if (selected >= 0 && selected < s_menuItemCount) dispatchMenuItem(selected);
        closeMenu();
        return true;
    }
    return true;
}

bool TaskbarManager::handleMenuHover(int32_t mx, int32_t my) {
    if (!s_menuOpen) {
        const int oldPreview = s_previewButtonIndex;
        updateHover(mx, my);
        return oldPreview != s_previewButtonIndex;
    }
    const int next = menuItemAt(mx, my);
    if (next == s_menuHover) return false;
    s_menuHover = next;
    return true;
}

bool TaskbarManager::handleWheel(int32_t mx, int32_t my, int8_t wheelDelta) {
    if (!s_menuOpen || !menuContainsPoint(mx, my) || wheelDelta == 0) return false;
    const int visibleRows = menuVisibleRowCount();
    const int maximumScroll = s_menuItemCount > visibleRows ? s_menuItemCount - visibleRows : 0;
    if (wheelDelta > 0 && s_menuScroll > 0) --s_menuScroll;
    else if (wheelDelta < 0 && s_menuScroll < maximumScroll) ++s_menuScroll;
    else return false;
    s_menuHover = -1;
    return true;
}

bool TaskbarManager::menuVisible() { return s_menuOpen || s_previewButtonIndex >= 0; }

bool TaskbarManager::menuContainsPoint(int32_t mx, int32_t my) {
    return (s_menuOpen && mx >= s_menuX && mx < s_menuX + s_menuW &&
        my >= s_menuY && my < s_menuY + s_menuH) || previewContainsPoint(mx, my);
}

bool TaskbarManager::isTaskbarButtonAt(int32_t mx, int32_t my) {
    updateButtons();
    return findButtonAt(mx, my) >= 0;
}

void TaskbarManager::closeMenu() {
    s_menuOpen = false;
    s_menuItemCount = 0;
    s_menuHover = -1;
    s_menuScroll = 0;
    s_menuAppId[0] = '\0';
}

int TaskbarManager::findButtonAt(int32_t mx, int32_t my) {
    for (int i = 0; i < s_visibleButtonCount; ++i) {
        const TaskbarButton& button = s_buttons[i];
        if (mx >= button.x && mx < button.x + button.w &&
            my >= button.y && my < button.y + button.h) return i;
    }
    if (s_buttonCount > s_visibleButtonCount && s_buttons[s_buttonCount].overflowEntry) {
        const TaskbarButton& button = s_buttons[s_buttonCount];
        if (mx >= button.x && mx < button.x + button.w && my >= button.y && my < button.y + button.h)
            return s_buttonCount;
    }
    return -1;
}

bool TaskbarManager::setActivity(const char* appId, uint32_t windowId,
                                gxos::gui::TaskbarProgressState progress, uint8_t percent, bool attention) {
    if (windowId != 0 && !KernelCompositor::getWindow(windowId)) return false;
    return s_activityStore.set(appId, windowId, progress, percent, attention);
}

bool TaskbarManager::clearActivity(const char* appId, uint32_t windowId) {
    return s_activityStore.clear(appId, windowId);
}

void TaskbarManager::acknowledgeActivity(const char* appId, uint32_t windowId) {
    s_activityStore.acknowledgeWindow(appId, windowId);
}

bool TaskbarManager::beginFileOperationActivity(uint32_t ownerWindowId) {
    app::KernelWindow* ownerWindow = KernelCompositor::getWindow(ownerWindowId);
    if (!ownerWindow || !ownerWindow->owner ||
        !gxos::gui::HasCanonicalTaskbarAppIdentity(ownerWindow->owner->getRuntimeAppId())) {
        return false;
    }
    if (s_fileOperationOwnerWindowId != 0) {
        s_activityStore.clear(nullptr, s_fileOperationOwnerWindowId);
    }
    s_fileOperationOwnerWindowId = ownerWindowId;
    return true;
}

bool TaskbarManager::updateFileOperationActivity(
    gxos::gui::TaskbarProgressState progress, uint8_t percent) {
    const uint32_t ownerWindowId = s_fileOperationOwnerWindowId;
    if (ownerWindowId == 0) return false;
    app::KernelWindow* ownerWindow = KernelCompositor::getWindow(ownerWindowId);
    if (!ownerWindow || !ownerWindow->owner ||
        !gxos::gui::HasCanonicalTaskbarAppIdentity(ownerWindow->owner->getRuntimeAppId())) {
        endFileOperationActivity(ownerWindowId);
        return false;
    }
    return s_activityStore.set(nullptr, ownerWindowId, progress, percent, false);
}

void TaskbarManager::endFileOperationActivity(uint32_t ownerWindowId) {
    if (ownerWindowId == 0 || s_fileOperationOwnerWindowId != ownerWindowId) return;
    s_activityStore.clear(nullptr, ownerWindowId);
    s_fileOperationOwnerWindowId = 0;
}

void TaskbarManager::closePreview() {
    s_previewButtonIndex = -1;
    s_previewWindowCount = 0;
    s_previewW = s_previewH = 0;
}

void TaskbarManager::updatePreviewLayout() {
    s_previewWindowCount = 0;
    s_previewW = s_previewH = 0;
    if (s_previewButtonIndex < 0 || s_previewButtonIndex >= s_buttonCount || s_menuOpen) return;
    const TaskbarButton& button = s_buttons[s_previewButtonIndex];
    if (button.windowCount <= 0) return;
    gxos::gui::TaskbarDockEdge edge = gxos::gui::TaskbarDockEdge::Bottom;
    if (s_vertical) edge = s_taskbarX + s_taskbarW / 2 < s_screenW / 2
        ? gxos::gui::TaskbarDockEdge::Left : gxos::gui::TaskbarDockEdge::Right;
    const gxos::gui::TaskbarRect anchor{button.x, button.y, button.x + button.w, button.y + button.h};
    const gxos::gui::TaskbarRect bounds{0, 0, static_cast<int>(s_screenW), static_cast<int>(s_screenH)};
    const int requestedWidth = button.windowCount > 1 ? 660 : 270;
    const gxos::gui::TaskbarRect popup = gxos::gui::PositionTaskbarPopup(anchor, bounds,
        requestedWidth, 138, edge, 0);
    if (popup.empty()) return;
    s_previewX = popup.left;
    s_previewY = popup.top;
    s_previewW = popup.right - popup.left;
    s_previewH = popup.bottom - popup.top;
    const int inset = 8;
    const int gap = 8;
    const int cardW = button.windowCount > 1
        ? (s_previewW - inset * 2 - gap * 3) / 4 : s_previewW - inset * 2;
    if (cardW <= 0) return;
    const int capacity = button.windowCount > 1
        ? ((s_previewW - inset * 2 + gap) / (cardW + gap)) : 1;
    const int count = button.windowCount < capacity ? button.windowCount : capacity;
    s_previewWindowCount = count > 4 ? 4 : count;
    for (int i = 0; i < s_previewWindowCount; ++i) {
        const uint32_t id = button.windowIds[i];
        app::KernelWindow* window = KernelCompositor::getWindow(id);
        if (!window) continue;
        const int width = button.windowCount > 1
            ? (s_previewW - inset * 2 - gap * (s_previewWindowCount - 1)) / s_previewWindowCount
            : s_previewW - inset * 2;
        s_previewCardX[i] = s_previewX + inset + i * (width + gap);
        s_previewCardY[i] = s_previewY + inset;
        s_previewCardW[i] = width;
        s_previewCardH[i] = s_previewH - inset * 2;
        s_previewWindowIds[i] = id;
    }
}

bool TaskbarManager::previewContainsPoint(int32_t mx, int32_t my) {
    return s_previewButtonIndex >= 0 && s_previewW > 0 && s_previewH > 0 &&
        mx >= s_previewX && mx < s_previewX + s_previewW &&
        my >= s_previewY && my < s_previewY + s_previewH;
}

int TaskbarManager::previewCardAt(int32_t mx, int32_t my) {
    for (int i = 0; i < s_previewWindowCount; ++i) {
        if (mx >= s_previewCardX[i] && mx < s_previewCardX[i] + s_previewCardW[i] &&
            my >= s_previewCardY[i] && my < s_previewCardY[i] + s_previewCardH[i]) return i;
    }
    return -1;
}

void TaskbarManager::activateWindow(uint32_t windowId) {
    if (!KernelCompositor::getWindow(windowId)) return;
    KernelCompositor::restoreWindow(windowId);
    KernelCompositor::setFocus(windowId);
}

void TaskbarManager::appendMenuItem(const char* label, MenuCommand command, uint32_t windowId, bool enabled) {
    if (s_menuItemCount >= MAX_TASKBAR_MENU_ITEMS) return;
    MenuItem& item = s_menuItems[s_menuItemCount++];
    makeTaskbarFitText(item.label, sizeof(item.label), label, 33);
    item.command = command;
    item.windowId = windowId;
    item.enabled = enabled;
    item.appId[0] = '\0';
}

void TaskbarManager::openOverflowMenu(int32_t mx, int32_t my) {
    if (s_buttonCount <= s_visibleButtonCount) return;
    closePreview();
    closeMenu();
    s_menuOpen = true;
    for (int i = s_visibleButtonCount; i < s_buttonCount; ++i) {
        const TaskbarButton& button = s_buttons[i];
        if (button.overflowEntry) continue;
        uint32_t targetWindow = button.windowCount > 0 ? button.windowId : 0;
        const bool enabled = targetWindow != 0 || button.relaunchable;
        appendMenuItem(button.title[0] ? button.title : "Application",
            MenuCommand::SelectOverflowEntry, targetWindow, enabled);
        if (s_menuItemCount > 0) {
            copyTaskbarText(s_menuItems[s_menuItemCount - 1].appId,
                sizeof(s_menuItems[s_menuItemCount - 1].appId), button.appId);
        }
    }
    TaskbarButton& overflow = s_buttons[s_buttonCount];
    layoutMenu(mx >= 0 ? mx : overflow.x + overflow.w / 2,
        my >= 0 ? my : overflow.y);
}

void TaskbarManager::openMenuForButton(int buttonIndex, int32_t mx, int32_t my) {
    closePreview();
    if (buttonIndex < 0 || buttonIndex >= s_buttonCount) return;
    const TaskbarButton& button = s_buttons[buttonIndex];
    closeMenu();
    s_menuOpen = true;
    copyTaskbarText(s_menuAppId, sizeof(s_menuAppId), button.appId);

    for (int i = 0; i < button.windowCount; ++i) {
        char label[64];
        const app::KernelWindow* window = KernelCompositor::getWindow(button.windowIds[i]);
        const char* prefix = window && (window->flags & app::WF_MINIMIZED) ? "Restore: " : "Activate: ";
        copyTaskbarText(label, sizeof(label), prefix);
        int position = taskbarTextLength(label);
        const char* title = button.windowTitles[i][0] ? button.windowTitles[i] : button.title;
        int j = 0;
        while (title[j] && position + 1 < (int)sizeof(label)) label[position++] = title[j++];
        label[position] = '\0';
        appendMenuItem(label, MenuCommand::ActivateWindow, button.windowIds[i]);
    }

    int primaryIndex = -1;
    for (int i = 0; i < button.windowCount; ++i) {
        app::KernelWindow* window = KernelCompositor::getWindow(button.windowIds[i]);
        if (window && (window->flags & app::WF_FOCUSED)) { primaryIndex = i; break; }
    }
    if (primaryIndex < 0) {
        for (int i = 0; i < button.windowCount; ++i) {
            app::KernelWindow* window = KernelCompositor::getWindow(button.windowIds[i]);
            if (window && !(window->flags & app::WF_MINIMIZED)) { primaryIndex = i; break; }
        }
    }
    if (primaryIndex < 0 && button.windowCount > 0) primaryIndex = 0;
    app::KernelWindow* primary = primaryIndex >= 0
        ? KernelCompositor::getWindow(button.windowIds[primaryIndex]) : nullptr;
    if (primary) {
        if (!(primary->flags & app::WF_MINIMIZED)) appendMenuItem("Minimize", MenuCommand::MinimizeWindow, primary->id);
        if (primary->flags & app::WF_RESIZABLE) {
            appendMenuItem((primary->flags & app::WF_MAXIMIZED) ? "Restore Down" : "Maximize",
                           MenuCommand::ToggleMaximizeWindow, primary->id);
        }
        if (primary->flags & app::WF_CLOSABLE) appendMenuItem("Close Window", MenuCommand::CloseWindow, primary->id);
        if (button.windowCount > 1) appendMenuItem("Close All Windows", MenuCommand::CloseAllWindows);
    } else if (button.relaunchable) {
        appendMenuItem("Launch", MenuCommand::LaunchApp);
    } else if (button.pinned) {
        char unavailable[64];
        copyTaskbarText(unavailable, sizeof(unavailable), "Unavailable: ");
        int position = taskbarTextLength(unavailable);
        int i = 0;
        while (button.appId[i] && position + 1 < (int)sizeof(unavailable)) unavailable[position++] = button.appId[i++];
        unavailable[position] = '\0';
        appendMenuItem(unavailable, MenuCommand::None, 0, false);
    }

    if (button.appId[0] && button.relaunchable) {
        if (button.pinned) appendMenuItem("Unpin from Taskbar", MenuCommand::UnpinApp);
        else if (s_pinCount >= MAX_TASKBAR_PINS) appendMenuItem("Pin limit reached", MenuCommand::None, 0, false);
        else appendMenuItem("Pin to Taskbar", MenuCommand::PinApp);
        const char* persistence = s_pinPersistence == TaskbarPinPersistenceState::Ready
            ? "Pin storage: available"
            : s_pinPersistence == TaskbarPinPersistenceState::Unsupported
                ? "Pin storage: unavailable"
                : "Pin storage: write/read error";
        appendMenuItem(persistence, MenuCommand::None, 0, false);
    } else if (button.pinned) {
        appendMenuItem("Unpin from Taskbar", MenuCommand::UnpinApp);
        const char* persistence = s_pinPersistence == TaskbarPinPersistenceState::Ready
            ? "Pin storage: available"
            : s_pinPersistence == TaskbarPinPersistenceState::Unsupported
                ? "Pin storage: unavailable"
                : "Pin storage: write/read error";
        appendMenuItem(persistence, MenuCommand::None, 0, false);
    }

    if (s_vertical) layoutMenu(button.x + button.w, button.y);
    else layoutMenu(button.x, button.y - 4);
    if (s_menuItemCount == 0) {
        (void)mx;
        (void)my;
        closeMenu();
    }
}

void TaskbarManager::layoutMenu(int32_t anchorX, int32_t anchorY) {
    if (s_screenW == 0 || s_screenH == 0) {
        s_menuX = s_menuY = s_menuW = s_menuH = 0;
        return;
    }
    s_menuW = static_cast<int>(s_screenW < (uint32_t)kTaskbarMenuWidth ? s_screenW : (uint32_t)kTaskbarMenuWidth);
    const int requestedHeight = s_menuItemCount * kTaskbarMenuRowHeight + 4;
    s_menuH = requestedHeight < (int)s_screenH ? requestedHeight : (int)s_screenH;
    if (s_menuW < 1) s_menuW = 1;
    if (s_menuH < 1) s_menuH = 1;
    s_menuX = anchorX;
    s_menuY = anchorY;
    if (!s_vertical && anchorY > (int)s_screenH / 2) s_menuY -= s_menuH;
    if (s_menuX + s_menuW > (int)s_screenW) s_menuX = (int)s_screenW - s_menuW;
    if (s_menuY + s_menuH > (int)s_screenH) s_menuY = (int)s_screenH - s_menuH;
    if (s_menuX < 0) s_menuX = 0;
    if (s_menuY < 0) s_menuY = 0;
    s_menuScroll = 0;
    s_menuHover = -1;
}

int TaskbarManager::menuVisibleRowCount() {
    int rows = (s_menuH - 4) / kTaskbarMenuRowHeight;
    return rows > 0 ? rows : 1;
}

int TaskbarManager::menuItemAt(int32_t mx, int32_t my) {
    if (!menuContainsPoint(mx, my)) return -1;
    const int row = (my - s_menuY - 2) / kTaskbarMenuRowHeight;
    if (row < 0 || row >= menuVisibleRowCount()) return -1;
    const int index = s_menuScroll + row;
    return index >= 0 && index < s_menuItemCount ? index : -1;
}

void TaskbarManager::dispatchMenuItem(int index) {
    if (index < 0 || index >= s_menuItemCount) return;
    const MenuItem item = s_menuItems[index];
    if (!item.enabled) return;
    switch (item.command) {
    case MenuCommand::ActivateWindow:
        activateWindow(item.windowId);
        break;
    case MenuCommand::MinimizeWindow:
        KernelCompositor::minimizeWindow(item.windowId);
        break;
    case MenuCommand::ToggleMaximizeWindow:
        KernelCompositor::maximizeWindow(item.windowId);
        break;
    case MenuCommand::CloseWindow: {
        app::KernelWindow* window = KernelCompositor::getWindow(item.windowId);
        if (window && (window->flags & app::WF_MINIMIZED)) KernelCompositor::restoreWindow(item.windowId);
        KernelCompositor::requestCloseWindow(item.windowId);
        break;
    }
    case MenuCommand::CloseAllWindows: {
        uint32_t targets[MAX_WINDOWS];
        int count = 0;
        for (int i = 0; i < MAX_WINDOWS; ++i) {
            if (!KernelCompositor::s_windows[i].valid || !KernelCompositor::s_windows[i].window) continue;
            app::KernelWindow* window = KernelCompositor::s_windows[i].window;
            const char* appId = window->owner ? window->owner->getRuntimeAppId() : "";
            if (gxos::gui::SameCanonicalTaskbarAppIdentity(s_menuAppId, appId) && count < MAX_WINDOWS) {
                targets[count++] = window->id;
            }
        }
        for (int i = 0; i < count; ++i) {
            app::KernelWindow* window = KernelCompositor::getWindow(targets[i]);
            if (window && (window->flags & app::WF_MINIMIZED)) KernelCompositor::restoreWindow(targets[i]);
            KernelCompositor::requestCloseWindow(targets[i]);
        }
        break;
    }
    case MenuCommand::PinApp:
        pinApplication(s_menuAppId);
        break;
    case MenuCommand::UnpinApp:
        unpinApplication(s_menuAppId);
        break;
    case MenuCommand::LaunchApp:
        app::AppManager::launchAppById(s_menuAppId);
        break;
    case MenuCommand::SelectOverflowEntry:
        if (item.windowId != 0) activateWindow(item.windowId);
        else if (item.appId[0] != '\0') app::AppManager::launchAppById(item.appId);
        break;
    case MenuCommand::None:
    default:
        break;
    }
}

void TaskbarManager::loadPinnedApplications() {
    s_pinCount = 0;
    const vfs::MountPoint* mount = vfs::get_mount(kTaskbarPinStorePath);
    if (!mount || mount->readOnly) {
        s_pinPersistence = TaskbarPinPersistenceState::Unsupported;
        serial::puts("[taskbar] pin persistence=unsupported reason=");
        serial::puts(!mount ? "settings-volume-unmounted\n" : "settings-volume-read-only\n");
        return;
    }
    vfs::FileInfo info{};
    const vfs::Status statStatus = vfs::stat(kTaskbarPinStorePath, &info);
    if (statStatus == vfs::VFS_ERR_NOT_FOUND) {
        s_pinPersistence = TaskbarPinPersistenceState::Ready;
        return;
    }
    if (statStatus != vfs::VFS_OK || info.type != vfs::FILE_TYPE_REGULAR ||
        info.size == 0 || info.size > 512u) {
        s_pinStoreCorrupt = true;
        s_pinPersistence = TaskbarPinPersistenceState::StorageFailure;
        serial::puts("[taskbar] pin persistence=storage-failure reason=invalid-store\n");
        return;
    }
    char text[513];
    const int32_t count = vfs::read_file(kTaskbarPinStorePath, text, 512u);
    if (count <= 0 || static_cast<uint64_t>(count) != info.size) {
        s_pinStoreCorrupt = true;
        s_pinPersistence = TaskbarPinPersistenceState::StorageFailure;
        serial::puts("[taskbar] pin persistence=storage-failure reason=read\n");
        return;
    }
    text[count] = '\0';
    bool sawVersion = false;
    bool malformed = false;
    char* cursor = text;
    while (*cursor) {
        char* line = cursor;
        while (*cursor && *cursor != '\n' && *cursor != '\r') ++cursor;
        if (*cursor) {
            *cursor++ = '\0';
            while (*cursor == '\n' || *cursor == '\r') ++cursor;
        }
        if (taskbarStringsEqual(line, "version=1")) {
            sawVersion = true;
            continue;
        }
        if (line[0] == '\0') continue;
        if (line[0] != 'a' || line[1] != 'p' || line[2] != 'p' || line[3] != 'I' ||
            line[4] != 'd' || line[5] != '=') {
            malformed = true;
            continue;
        }
        const char* appId = line + 6;
        if (!validTaskbarAppId(appId)) {
            malformed = true;
            continue;
        }
        bool duplicate = false;
        for (int i = 0; i < s_pinCount; ++i) {
            if (taskbarStringsEqual(s_pinnedAppIds[i], appId)) duplicate = true;
        }
        if (duplicate) continue;
        if (s_pinCount >= MAX_TASKBAR_PINS) {
            malformed = true;
            continue;
        }
        copyTaskbarText(s_pinnedAppIds[s_pinCount++], app::MAX_APP_ID, appId);
    }
    if (!sawVersion) malformed = true;
    s_pinStoreCorrupt = malformed;
    s_pinPersistence = malformed ? TaskbarPinPersistenceState::StorageFailure : TaskbarPinPersistenceState::Ready;
    serial::puts(malformed ? "[taskbar] pin persistence=storage-failure reason=invalid-record\n"
                           : "[taskbar] pin persistence=loaded\n");
}

bool TaskbarManager::savePinnedApplications() {
    const vfs::MountPoint* mount = vfs::get_mount(kTaskbarPinStorePath);
    if (!mount || mount->readOnly) {
        s_pinPersistence = TaskbarPinPersistenceState::Unsupported;
        serial::puts("[taskbar] pin persistence=unsupported reason=");
        serial::puts(!mount ? "settings-volume-unmounted\n" : "settings-volume-read-only\n");
        return false;
    }
    if (s_pinStoreCorrupt) {
        s_pinPersistence = TaskbarPinPersistenceState::StorageFailure;
        serial::puts("[taskbar] pin persistence=storage-failure reason=preserving-invalid-store\n");
        return false;
    }
    char text[512];
    int position = 0;
    const char* header = "version=1\n";
    for (int i = 0; header[i] && position + 1 < (int)sizeof(text); ++i) text[position++] = header[i];
    for (int pin = 0; pin < s_pinCount; ++pin) {
        const char* prefix = "appId=";
        for (int i = 0; prefix[i] && position + 1 < (int)sizeof(text); ++i) text[position++] = prefix[i];
        for (int i = 0; s_pinnedAppIds[pin][i] && position + 1 < (int)sizeof(text); ++i) text[position++] = s_pinnedAppIds[pin][i];
        text[position++] = '\n';
    }
    text[position] = '\0';
    const int32_t written = vfs::write_file(kTaskbarPinStorePath, text, static_cast<uint32_t>(position));
    if (written != position) {
        s_pinPersistence = TaskbarPinPersistenceState::StorageFailure;
        serial::puts("[taskbar] pin persistence=storage-failure reason=write\n");
        return false;
    }
    char readback[512];
    const int32_t read = vfs::read_file(kTaskbarPinStorePath, readback, sizeof(readback));
    bool matches = read == position;
    if (matches) {
        for (int i = 0; i < position; ++i) if (readback[i] != text[i]) matches = false;
    }
    if (!matches) {
        s_pinPersistence = TaskbarPinPersistenceState::StorageFailure;
        serial::puts("[taskbar] pin persistence=storage-failure reason=readback\n");
        return false;
    }
    s_pinPersistence = TaskbarPinPersistenceState::Ready;
    serial::puts("[taskbar] pin persistence=saved path=/desktop.taskbar.pins pins=");
    serial::put_hex32(static_cast<uint32_t>(s_pinCount));
    serial::putc('\n');
    return true;
}

bool TaskbarManager::pinApplication(const char* appId) {
    if (!validTaskbarAppId(appId)) return false;
    const app::AppInfo* info = app::AppManager::getAppInfoById(appId);
    if (!info || !info->available || !info->factory) return false;
    for (int i = 0; i < s_pinCount; ++i) if (taskbarStringsEqual(s_pinnedAppIds[i], appId)) return false;
    if (s_pinCount >= MAX_TASKBAR_PINS) return false;
    copyTaskbarText(s_pinnedAppIds[s_pinCount++], app::MAX_APP_ID, appId);
    savePinnedApplications();
    return true;
}

bool TaskbarManager::unpinApplication(const char* appId) {
    if (!gxos::gui::HasCanonicalTaskbarAppIdentity(appId)) return false;
    int found = -1;
    for (int i = 0; i < s_pinCount; ++i) {
        if (taskbarStringsEqual(s_pinnedAppIds[i], appId)) { found = i; break; }
    }
    if (found < 0) return false;
    for (int i = found; i + 1 < s_pinCount; ++i) {
        copyTaskbarText(s_pinnedAppIds[i], app::MAX_APP_ID, s_pinnedAppIds[i + 1]);
    }
    s_pinnedAppIds[--s_pinCount][0] = '\0';
    savePinnedApplications();
    return true;
}

} // namespace compositor
} // namespace kernel
