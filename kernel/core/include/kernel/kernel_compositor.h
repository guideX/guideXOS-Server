//
// guideXOS Kernel Compositor
//
// Manages window rendering and input routing for kernel-mode GUI applications.
// This compositor provides basic window management when running in UEFI/bare-metal
// mode without the full guideXOSServer compositor.
//
// Copyright (c) 2026 guideXOS Server
//

#ifndef KERNEL_KERNEL_COMPOSITOR_H
#define KERNEL_KERNEL_COMPOSITOR_H

#include "kernel/types.h"
#include "kernel/kernel_app.h"

namespace kernel {
namespace compositor {

// ============================================================
// Constants
// ============================================================

static const int MAX_WINDOWS = 16;
static const int TITLEBAR_HEIGHT = 24;
static const int BORDER_WIDTH = 1;
static const int BUTTON_SIZE = 16;
static const int BUTTON_GAP = 6;
static const int RESIZE_MARGIN = 8;
static const int MIN_WINDOW_WIDTH = 200;
static const int MIN_WINDOW_HEIGHT = 100;

// ============================================================
// Window Z-order management
// ============================================================

struct WindowEntry {
    app::KernelWindow* window;
    bool valid;
    
    WindowEntry() : window(nullptr), valid(false) {}
};

// ============================================================
// Hit test results
// ============================================================

enum class HitTestResult {
    None = 0,
    Client,
    Titlebar,
    CloseButton,
    MaximizeButton,
    MinimizeButton,
    ResizeCorner,
    Border
};

// ============================================================
// Drag/resize state
// ============================================================

struct DragState {
    bool active;
    uint32_t windowId;
    int startX, startY;
    int offsetX, offsetY;
    bool isResize;
    int resizeStartW, resizeStartH;
    
    DragState() : active(false), windowId(0), startX(0), startY(0),
                  offsetX(0), offsetY(0), isResize(false),
                  resizeStartW(0), resizeStartH(0) {}
};

// Forward declaration
class TaskbarManager;

// ============================================================
// Kernel Compositor (singleton)
// ============================================================

class KernelCompositor {
public:
    // Initialize the compositor
    static void init(uint32_t screenW, uint32_t screenH, uint32_t taskbarH);
    
    // Shutdown the compositor
    static void shutdown();
    
    // Register a window with the compositor
    static bool registerWindow(app::KernelWindow* window);
    
    // Unregister a window
    static void unregisterWindow(app::KernelWindow* window);
    
    // Get window by ID
    static app::KernelWindow* getWindow(uint32_t windowId);
    
    // Get focused window
    static app::KernelWindow* getFocusedWindow();
    
    // Set focus to a window
    static void setFocus(uint32_t windowId);
    
    // Bring window to front
    static void bringToFront(uint32_t windowId);
    
    // Window management
    static void minimizeWindow(uint32_t windowId);
    static void maximizeWindow(uint32_t windowId);
    static void restoreWindow(uint32_t windowId);
    static void closeWindow(uint32_t windowId);
    
    // Hit testing
    static HitTestResult hitTest(int x, int y, app::KernelWindow** outWindow);
    
    // Input handling (call from desktop.cpp)
    static void handleMouseMove(int32_t mx, int32_t my);
    static void handleMouseDown(int32_t mx, int32_t my, uint8_t button);
    static void handleMouseUp(int32_t mx, int32_t my, uint8_t button);
    static void handleKeyDown(uint32_t keyCode);
    static void handleKeyChar(char c);
    
    // Rendering
    static void drawAllWindows();
    static void drawWindow(app::KernelWindow* window);
    static void drawTitlebar(app::KernelWindow* window);
    static void drawWidget(app::Widget* widget, uint32_t winX, uint32_t winY);
    
    // Check if any windows are open
    static bool hasWindows();
    
    // Get window count
    static int getWindowCount();
    
    // Check if point is over any compositor window
    static bool isPointOverWindow(int32_t x, int32_t y);
    
    // Check if a button press is active (for routing mouse up events)
    static bool isButtonPressActive();
    
    // Generate unique window ID
    static uint32_t generateWindowId();
    
    // Allow TaskbarManager to access private members
    friend class TaskbarManager;
    
private:
    // Window list (z-ordered, back to front)
    static WindowEntry s_windows[MAX_WINDOWS];
    static int s_windowCount;
    static uint32_t s_focusedWindowId;
    static uint32_t s_nextWindowId;
    
    // Screen dimensions
    static uint32_t s_screenW;
    static uint32_t s_screenH;
    static uint32_t s_taskbarH;
    
    // Drag/resize state
    static DragState s_dragState;
    
    // Hover state
    static uint32_t s_hoverWindowId;
    static HitTestResult s_hoverResult;
    
    // Button press tracking (to ensure mouse up is sent even if mouse moved)
    static bool s_buttonPressActive;
    
    // Initialization flag
    static bool s_initialized;
    
    // Helper: find window index
    static int findWindowIndex(uint32_t windowId);
    
    // Helper: update window hover states
    static void updateHoverStates(int mx, int my);
    
    // Helper: clear all hover states for a window
    static void clearHoverStates(app::KernelWindow* window);
    
    // Drawing helpers
    static void drawButton(uint32_t x, uint32_t y, uint32_t w, uint32_t h, 
                           uint32_t bgColor, uint32_t borderColor, 
                           const char* text, uint32_t textColor);
    static void drawRect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
    static void fillRect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
    static void drawText(uint32_t x, uint32_t y, const char* text, uint32_t color, int scale = 1);
    static void drawTextCentered(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                                 const char* text, uint32_t color, int scale = 1);
    static void hline(uint32_t x, uint32_t y, uint32_t w, uint32_t color);
    static void vline(uint32_t x, uint32_t y, uint32_t h, uint32_t color);
    
    // Color helpers
    static uint32_t rgb(uint8_t r, uint8_t g, uint8_t b);
    static uint32_t blend(uint32_t c1, uint32_t c2, uint8_t alpha);
};

// ============================================================
// Taskbar integration
// ============================================================

struct TaskbarButton {
    uint32_t windowId;
    char title[app::MAX_TITLE_LEN];
    bool active;
    bool hover;
    int x, y, w, h;
    
    TaskbarButton() : windowId(0), active(false), hover(false),
                      x(0), y(0), w(0), h(0) {
        title[0] = '\0';
    }
};

class TaskbarManager {
public:
    static void init(uint32_t screenW, uint32_t screenH, uint32_t taskbarH, uint32_t startX);
    static void updateButtons();
    static void drawButtons();
    static bool handleClick(int32_t mx, int32_t my);
    static void updateHover(int32_t mx, int32_t my);
    
private:
    static const int MAX_TASKBAR_BUTTONS = 8;
    static TaskbarButton s_buttons[MAX_TASKBAR_BUTTONS];
    static int s_buttonCount;
    static uint32_t s_screenW;
    static uint32_t s_screenH;
    static uint32_t s_taskbarH;
    static uint32_t s_startX;
};

} // namespace compositor
} // namespace kernel

#endif // KERNEL_KERNEL_COMPOSITOR_H
