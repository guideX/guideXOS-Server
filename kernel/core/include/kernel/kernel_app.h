//
// guideXOS Kernel Application Framework
//
// Provides base classes and infrastructure for running GUI applications
// directly in the kernel when the user-mode server is not available.
// This enables basic app functionality in UEFI/bare-metal mode.
//
// Copyright (c) 2026 guideXOS Server
//

#ifndef KERNEL_KERNEL_APP_H
#define KERNEL_KERNEL_APP_H

#include "kernel/types.h"

namespace kernel {
namespace app {

// ============================================================
// Constants
// ============================================================

static const int MAX_APPS = 16;
static const int MAX_APP_NAME = 32;
static const int MAX_WINDOWS = 8;
static const int MAX_TITLE_LEN = 64;

// ============================================================
// Forward declarations
// ============================================================

struct KernelWindow;
class KernelApp;

// ============================================================
// App State
// ============================================================

enum class AppState {
    NotLoaded,
    Running,
    Suspended,
    Terminated
};

// ============================================================
// Window flags
// ============================================================

enum WindowFlags : uint32_t {
    WF_NONE         = 0,
    WF_VISIBLE      = 1 << 0,
    WF_FOCUSED      = 1 << 1,
    WF_MINIMIZED    = 1 << 2,
    WF_MAXIMIZED    = 1 << 3,
    WF_RESIZABLE    = 1 << 4,
    WF_CLOSABLE     = 1 << 5,
    WF_TITLEBAR     = 1 << 6,
    WF_MODAL        = 1 << 7
};

// ============================================================
// UI Widget Types
// ============================================================

enum class WidgetType {
    None = 0,
    Label,
    Button,
    TextBox,
    ListBox,
    CheckBox,
    ProgressBar
};

// ============================================================
// Widget structure for UI elements
// ============================================================

struct Widget {
    int id;
    WidgetType type;
    int x, y, w, h;
    char text[64];
    uint32_t bgColor;
    uint32_t fgColor;
    bool enabled;
    bool visible;
    bool hover;
    bool pressed;
    int value;  // For checkboxes, progress bars, etc.
    
    Widget() : id(0), type(WidgetType::None), x(0), y(0), w(0), h(0),
               bgColor(0xFF404040), fgColor(0xFFFFFFFF), enabled(true),
               visible(true), hover(false), pressed(false), value(0) {
        text[0] = '\0';
    }
};

// ============================================================
// Kernel Window structure
// ============================================================

struct KernelWindow {
    uint32_t id;
    int x, y, w, h;
    char title[MAX_TITLE_LEN];
    uint32_t flags;
    KernelApp* owner;
    bool dirty;
    
    // Titlebar button states
    bool closeBtnHover;
    bool closeBtnPressed;
    bool maxBtnHover;
    bool maxBtnPressed;
    bool minBtnHover;
    bool minBtnPressed;
    
    // Saved position for restore from maximize
    int savedX, savedY, savedW, savedH;
    
    // Widgets in this window
    static const int MAX_WIDGETS = 32;
    Widget widgets[MAX_WIDGETS];
    int widgetCount;
    
    KernelWindow() : id(0), x(0), y(0), w(400), h(300), flags(WF_VISIBLE | WF_TITLEBAR | WF_CLOSABLE | WF_RESIZABLE),
                     owner(nullptr), dirty(true), closeBtnHover(false), closeBtnPressed(false),
                     maxBtnHover(false), maxBtnPressed(false), minBtnHover(false), minBtnPressed(false),
                     savedX(0), savedY(0), savedW(0), savedH(0), widgetCount(0) {
        title[0] = '\0';
    }
};

// ============================================================
// App Info structure (for registration)
// ============================================================

struct AppInfo {
    char name[MAX_APP_NAME];
    uint32_t iconColor;
    bool available;  // true if app can run in kernel mode
    KernelApp* (*factory)();  // Factory function to create app instance
    
    AppInfo() : iconColor(0xFF4690C8), available(false), factory(nullptr) {
        name[0] = '\0';
    }
};

// ============================================================
// Input Event types
// ============================================================

enum class InputEventType {
    None = 0,
    MouseMove,
    MouseDown,
    MouseUp,
    KeyDown,
    KeyUp,
    KeyChar,
    WindowClose,
    WindowFocus,
    WindowBlur,
    WidgetClick
};

// ============================================================
// Input Event structure
// ============================================================

struct InputEvent {
    InputEventType type;
    uint32_t windowId;
    int32_t mouseX;
    int32_t mouseY;
    uint8_t mouseButtons;
    uint32_t keyCode;
    char keyChar;
    int widgetId;
    
    InputEvent() : type(InputEventType::None), windowId(0), mouseX(0), mouseY(0),
                   mouseButtons(0), keyCode(0), keyChar(0), widgetId(0) {}
};

// ============================================================
// KernelApp base class
// ============================================================

class KernelApp {
public:
    KernelApp();
    virtual ~KernelApp();
    
    // Lifecycle methods (override in subclasses)
    virtual bool init() = 0;
    virtual void shutdown() = 0;
    virtual void update() {}  // Called each frame
    
    // Event handlers (override in subclasses)
    virtual void onMouseMove(int x, int y) {}
    virtual void onMouseDown(int x, int y, uint8_t button) {}
    virtual void onMouseUp(int x, int y, uint8_t button) {}
    virtual void onKeyDown(uint32_t key) {}
    virtual void onKeyUp(uint32_t key) {}
    virtual void onKeyChar(char c) {}
    virtual void onWidgetClick(int widgetId) {}
    virtual void onWindowClose() {}
    virtual void onWindowFocus() {}
    virtual void onWindowBlur() {}
    
    // Drawing (called by compositor to render app content)
    virtual void draw(uint32_t x, uint32_t y, uint32_t w, uint32_t h) = 0;
    
    // State
    AppState getState() const { return m_state; }
    const char* getName() const { return m_name; }
    KernelWindow* getWindow() { return m_window; }
    
    // Window management helpers
    void setTitle(const char* title);
    void setSize(int w, int h);
    void setPosition(int x, int y);
    void requestClose();
    void invalidate();
    
    // Widget helpers
    int addLabel(int x, int y, int w, int h, const char* text);
    int addButton(int x, int y, int w, int h, const char* text);
    int addTextBox(int x, int y, int w, int h, const char* text = "");
    int addCheckBox(int x, int y, int w, int h, const char* text, bool checked = false);
    int addProgressBar(int x, int y, int w, int h, int value = 0);
    void setWidgetText(int id, const char* text);
    void setWidgetValue(int id, int value);
    void setWidgetEnabled(int id, bool enabled);
    Widget* getWidget(int id);
    
protected:
    char m_name[MAX_APP_NAME];
    AppState m_state;
    KernelWindow* m_window;
    
    friend class AppManager;
};

// ============================================================
// App Manager (singleton)
// ============================================================

class AppManager {
public:
    // Initialize the app manager
    static void init();
    
    // Register an app that can run in kernel mode
    static bool registerApp(const char* name, uint32_t iconColor, KernelApp* (*factory)());
    
    // Check if an app is available in kernel mode
    static bool isAppAvailable(const char* name);
    
    // Launch an app by name
    static bool launchApp(const char* name);
    
    // Close an app
    static void closeApp(KernelApp* app);
    
    // Get running app count
    static int getRunningAppCount();
    
    // Get running app by index
    static KernelApp* getRunningApp(int index);
    
    // Get app info by name
    static const AppInfo* getAppInfo(const char* name);
    
    // Process input event for focused app
    static void processInput(const InputEvent& event);
    
    // Update all running apps
    static void update();
    
    // Check if we're running in bare-metal/UEFI mode
    static bool isBareMetal();
    
private:
    static AppInfo s_registeredApps[MAX_APPS];
    static int s_registeredAppCount;
    static KernelApp* s_runningApps[MAX_APPS];
    static int s_runningAppCount;
    static bool s_initialized;
};

// ============================================================
// App Launch Logging
// ============================================================

enum class LaunchResult {
    Success,
    NotAvailable,
    AlreadyRunning,
    FailedToInit,
    OutOfResources
};

struct AppLaunchLog {
    char appName[MAX_APP_NAME];
    LaunchResult result;
    uint32_t timestamp;
    
    AppLaunchLog() : result(LaunchResult::NotAvailable), timestamp(0) {
        appName[0] = '\0';
    }
};

class AppLogger {
public:
    static void init();
    static void logLaunch(const char* appName, LaunchResult result);
    static int getLogCount();
    static const AppLaunchLog* getLog(int index);
    static void clearLogs();
    
private:
    static const int MAX_LOGS = 64;
    static AppLaunchLog s_logs[MAX_LOGS];
    static int s_logCount;
    static int s_logHead;
    static uint32_t s_tickCounter;
};

} // namespace app
} // namespace kernel

#endif // KERNEL_KERNEL_APP_H
