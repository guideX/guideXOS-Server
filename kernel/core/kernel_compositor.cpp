//
// guideXOS Kernel Compositor Implementation
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/kernel/kernel_compositor.h"
#include "include/kernel/kernel_ipc.h"
#include "include/kernel/framebuffer.h"

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
DragState KernelCompositor::s_dragState;
uint32_t KernelCompositor::s_hoverWindowId = 0;
HitTestResult KernelCompositor::s_hoverResult = HitTestResult::None;
bool KernelCompositor::s_buttonPressActive = false;
bool KernelCompositor::s_initialized = false;

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
    s_initialized = true;
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
            s_windows[i].valid = false;
            s_windows[i].window = nullptr;
            s_windowCount--;
            
            // Clear focus if this was focused
            if (s_focusedWindowId == window->id) {
                s_focusedWindowId = 0;
                // Focus next window if available
                for (int j = MAX_WINDOWS - 1; j >= 0; j--) {
                    if (s_windows[j].valid && s_windows[j].window) {
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

void KernelCompositor::setFocus(uint32_t windowId) {
    // Clear focus flag on old window
    if (s_focusedWindowId != 0 && s_focusedWindowId != windowId) {
        app::KernelWindow* oldWin = getWindow(s_focusedWindowId);
        if (oldWin) {
            oldWin->flags &= ~app::WF_FOCUSED;
            oldWin->dirty = true;
        }
    }
    
    // Set focus flag on new window
    app::KernelWindow* newWin = getWindow(windowId);
    if (newWin) {
        newWin->flags |= app::WF_FOCUSED;
        newWin->dirty = true;
        s_focusedWindowId = windowId;
        bringToFront(windowId);
    }
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
            win->x = 0;
            win->y = 0;
            win->w = (int)s_screenW;
            win->h = (int)(s_screenH - s_taskbarH);
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

void KernelCompositor::closeWindow(uint32_t windowId) {
    app::KernelWindow* win = getWindow(windowId);
    if (win && win->owner) {
        // Directly request the app to close (calls onWindowClose and cleans up)
        win->owner->requestClose();
    }
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
                if (newW < MIN_WINDOW_WIDTH) newW = MIN_WINDOW_WIDTH;
                if (newH < MIN_WINDOW_HEIGHT) newH = MIN_WINDOW_HEIGHT;
                win->w = newW;
                win->h = newH;
            } else {
                win->x = mx - s_dragState.offsetX;
                win->y = my - s_dragState.offsetY;
                // Clamp to screen
                if (win->x < 0) win->x = 0;
                if (win->y < 0) win->y = 0;
                if (win->x + win->w > (int)s_screenW) win->x = (int)s_screenW - win->w;
                if (win->y + win->h > (int)(s_screenH - s_taskbarH)) 
                    win->y = (int)(s_screenH - s_taskbarH) - win->h;
            }
            win->dirty = true;
        }
        return;
    }
    
    // Update hover states
    updateHoverStates(mx, my);
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
            // Forward to app
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
        hitWin->owner->onMouseUp(localX, localY, button);
    }
}

void KernelCompositor::handleKeyDown(uint32_t keyCode) {
    app::KernelWindow* focused = getFocusedWindow();
    if (focused && focused->owner) {
        focused->owner->onKeyDown(keyCode);
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
    
    // Draw widgets
    for (int i = 0; i < window->widgetCount; i++) {
        app::Widget* widget = &window->widgets[i];
        if (widget->visible) {
            drawWidget(widget, x, clientY);
        }
    }
    
    // Let app draw custom content
    if (window->owner) {
        window->owner->draw(x, clientY, w, clientH);
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
            drawTextCentered(x, y, w, h, widget->text, widget->fgColor);
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

TaskbarButton TaskbarManager::s_buttons[MAX_TASKBAR_BUTTONS];
int TaskbarManager::s_buttonCount = 0;
uint32_t TaskbarManager::s_screenW = 0;
uint32_t TaskbarManager::s_screenH = 0;
uint32_t TaskbarManager::s_taskbarH = 0;
uint32_t TaskbarManager::s_startX = 0;

void TaskbarManager::init(uint32_t screenW, uint32_t screenH, uint32_t taskbarH, uint32_t startX) {
    s_screenW = screenW;
    s_screenH = screenH;
    s_taskbarH = taskbarH;
    s_startX = startX;
    s_buttonCount = 0;
}

void TaskbarManager::updateButtons() {
    s_buttonCount = 0;
    uint32_t btnX = s_startX;
    uint32_t btnY = s_screenH - s_taskbarH + 6;
    uint32_t btnH = s_taskbarH - 12;
    
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (!KernelCompositor::s_windows[i].valid) continue;
        app::KernelWindow* win = KernelCompositor::s_windows[i].window;
        if (!win) continue;
        
        TaskbarButton& btn = s_buttons[s_buttonCount];
        btn.windowId = win->id;
        
        // Copy title
        int j = 0;
        while (win->title[j] && j < app::MAX_TITLE_LEN - 1) {
            btn.title[j] = win->title[j];
            j++;
        }
        btn.title[j] = '\0';
        
        btn.active = (win->flags & app::WF_FOCUSED) != 0;
        btn.x = (int)btnX;
        btn.y = (int)btnY;
        btn.w = measureText(btn.title) + 24;
        btn.h = (int)btnH;
        
        btnX += btn.w + 4;
        s_buttonCount++;
        
        if (s_buttonCount >= MAX_TASKBAR_BUTTONS) break;
    }
}

void TaskbarManager::drawButtons() {
    for (int i = 0; i < s_buttonCount; i++) {
        TaskbarButton& btn = s_buttons[i];
        
        uint32_t bgColor = btn.active ? KernelCompositor::rgb(70, 100, 150) :
                           btn.hover ? KernelCompositor::rgb(60, 65, 75) :
                                       KernelCompositor::rgb(55, 58, 70);
        
        KernelCompositor::fillRect(btn.x, btn.y, btn.w, btn.h, bgColor);
        
        if (btn.active) {
            KernelCompositor::fillRect(btn.x + 2, btn.y + btn.h - 3, btn.w - 4, 2,
                                       KernelCompositor::rgb(100, 160, 240));
        }
        
        KernelCompositor::drawTextCentered(btn.x, btn.y, btn.w, btn.h, btn.title,
                                           KernelCompositor::rgb(230, 230, 240));
    }
}

bool TaskbarManager::handleClick(int32_t mx, int32_t my) {
    for (int i = 0; i < s_buttonCount; i++) {
        TaskbarButton& btn = s_buttons[i];
        if (mx >= btn.x && mx < btn.x + btn.w &&
            my >= btn.y && my < btn.y + btn.h) {
            
            app::KernelWindow* win = KernelCompositor::getWindow(btn.windowId);
            if (win) {
                if (win->flags & app::WF_MINIMIZED) {
                    KernelCompositor::restoreWindow(btn.windowId);
                } else if (win->flags & app::WF_FOCUSED) {
                    KernelCompositor::minimizeWindow(btn.windowId);
                } else {
                    KernelCompositor::setFocus(btn.windowId);
                }
            }
            return true;
        }
    }
    return false;
}

void TaskbarManager::updateHover(int32_t mx, int32_t my) {
    for (int i = 0; i < s_buttonCount; i++) {
        TaskbarButton& btn = s_buttons[i];
        btn.hover = (mx >= btn.x && mx < btn.x + btn.w &&
                     my >= btn.y && my < btn.y + btn.h);
    }
}

} // namespace compositor
} // namespace kernel
