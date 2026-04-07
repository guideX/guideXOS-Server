//
// guideXOS Kernel Application Framework Implementation
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/kernel/kernel_app.h"
#include "include/kernel/kernel_compositor.h"
#include "include/kernel/kernel_ipc.h"

namespace kernel {
namespace app {

// ============================================================
// Static member initialization
// ============================================================

AppInfo AppManager::s_registeredApps[MAX_APPS];
int AppManager::s_registeredAppCount = 0;
KernelApp* AppManager::s_runningApps[MAX_APPS];
int AppManager::s_runningAppCount = 0;
bool AppManager::s_initialized = false;

AppLaunchLog AppLogger::s_logs[AppLogger::MAX_LOGS];
int AppLogger::s_logCount = 0;
int AppLogger::s_logHead = 0;
uint32_t AppLogger::s_tickCounter = 0;

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

static bool streq(const char* a, const char* b) {
    while (*a && *b) {
        if (*a != *b) return false;
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

// ============================================================
// KernelApp implementation
// ============================================================

KernelApp::KernelApp() : m_state(AppState::NotLoaded), m_window(nullptr) {
    m_name[0] = '\0';
}

KernelApp::~KernelApp() {
    if (m_window) {
        compositor::KernelCompositor::unregisterWindow(m_window);
        delete m_window;
        m_window = nullptr;
    }
}

void KernelApp::setTitle(const char* title) {
    if (m_window && title) {
        strcopy(m_window->title, title, MAX_TITLE_LEN);
        m_window->dirty = true;
    }
}

void KernelApp::setSize(int w, int h) {
    if (m_window) {
        m_window->w = w;
        m_window->h = h;
        m_window->dirty = true;
    }
}

void KernelApp::setPosition(int x, int y) {
    if (m_window) {
        m_window->x = x;
        m_window->y = y;
        m_window->dirty = true;
    }
}

void KernelApp::requestClose() {
    if (m_window) {
        onWindowClose();
        compositor::KernelCompositor::unregisterWindow(m_window);
        delete m_window;
        m_window = nullptr;
        m_state = AppState::Terminated;
    }
}

void KernelApp::invalidate() {
    if (m_window) {
        m_window->dirty = true;
    }
}

int KernelApp::addLabel(int x, int y, int w, int h, const char* text) {
    if (!m_window || m_window->widgetCount >= KernelWindow::MAX_WIDGETS) {
        return -1;
    }
    
    int id = m_window->widgetCount;
    Widget& widget = m_window->widgets[m_window->widgetCount++];
    widget.id = id;
    widget.type = WidgetType::Label;
    widget.x = x;
    widget.y = y;
    widget.w = w;
    widget.h = h;
    strcopy(widget.text, text, 64);
    widget.bgColor = 0x00000000;  // Transparent
    widget.fgColor = 0xFFFFFFFF;
    widget.enabled = true;
    widget.visible = true;
    
    return id;
}

int KernelApp::addButton(int x, int y, int w, int h, const char* text) {
    if (!m_window || m_window->widgetCount >= KernelWindow::MAX_WIDGETS) {
        return -1;
    }
    
    int id = m_window->widgetCount;
    Widget& widget = m_window->widgets[m_window->widgetCount++];
    widget.id = id;
    widget.type = WidgetType::Button;
    widget.x = x;
    widget.y = y;
    widget.w = w;
    widget.h = h;
    strcopy(widget.text, text, 64);
    widget.bgColor = 0xFF505060;
    widget.fgColor = 0xFFFFFFFF;
    widget.enabled = true;
    widget.visible = true;
    
    return id;
}

int KernelApp::addTextBox(int x, int y, int w, int h, const char* text) {
    if (!m_window || m_window->widgetCount >= KernelWindow::MAX_WIDGETS) {
        return -1;
    }
    
    int id = m_window->widgetCount;
    Widget& widget = m_window->widgets[m_window->widgetCount++];
    widget.id = id;
    widget.type = WidgetType::TextBox;
    widget.x = x;
    widget.y = y;
    widget.w = w;
    widget.h = h;
    if (text) strcopy(widget.text, text, 64);
    else widget.text[0] = '\0';
    widget.bgColor = 0xFF2D2D37;
    widget.fgColor = 0xFFFFFFFF;
    widget.enabled = true;
    widget.visible = true;
    
    return id;
}

int KernelApp::addCheckBox(int x, int y, int w, int h, const char* text, bool checked) {
    if (!m_window || m_window->widgetCount >= KernelWindow::MAX_WIDGETS) {
        return -1;
    }
    
    int id = m_window->widgetCount;
    Widget& widget = m_window->widgets[m_window->widgetCount++];
    widget.id = id;
    widget.type = WidgetType::CheckBox;
    widget.x = x;
    widget.y = y;
    widget.w = w;
    widget.h = h;
    strcopy(widget.text, text, 64);
    widget.bgColor = 0xFF2D2D37;
    widget.fgColor = 0xFFFFFFFF;
    widget.enabled = true;
    widget.visible = true;
    widget.value = checked ? 1 : 0;
    
    return id;
}

int KernelApp::addProgressBar(int x, int y, int w, int h, int value) {
    if (!m_window || m_window->widgetCount >= KernelWindow::MAX_WIDGETS) {
        return -1;
    }
    
    int id = m_window->widgetCount;
    Widget& widget = m_window->widgets[m_window->widgetCount++];
    widget.id = id;
    widget.type = WidgetType::ProgressBar;
    widget.x = x;
    widget.y = y;
    widget.w = w;
    widget.h = h;
    widget.text[0] = '\0';
    widget.bgColor = 0xFF1E1E28;
    widget.fgColor = 0xFF4A9EFF;
    widget.enabled = true;
    widget.visible = true;
    widget.value = value;
    
    return id;
}

void KernelApp::setWidgetText(int id, const char* text) {
    Widget* widget = getWidget(id);
    if (widget && text) {
        strcopy(widget->text, text, 64);
        invalidate();
    }
}

void KernelApp::setWidgetValue(int id, int value) {
    Widget* widget = getWidget(id);
    if (widget) {
        widget->value = value;
        invalidate();
    }
}

void KernelApp::setWidgetEnabled(int id, bool enabled) {
    Widget* widget = getWidget(id);
    if (widget) {
        widget->enabled = enabled;
        invalidate();
    }
}

Widget* KernelApp::getWidget(int id) {
    if (!m_window || id < 0 || id >= m_window->widgetCount) {
        return nullptr;
    }
    return &m_window->widgets[id];
}

// ============================================================
// AppManager implementation
// ============================================================

void AppManager::init() {
    // Always reinitialize to handle cases where static init may not work
    // (e.g., kernel/UEFI environments where .bss might not be zeroed)
    for (int i = 0; i < MAX_APPS; i++) {
        s_registeredApps[i].name[0] = '\0';
        s_registeredApps[i].available = false;
        s_registeredApps[i].factory = nullptr;
        s_runningApps[i] = nullptr;
    }
    
    s_registeredAppCount = 0;
    s_runningAppCount = 0;
    s_initialized = true;
}

bool AppManager::registerApp(const char* name, uint32_t iconColor, KernelApp* (*factory)()) {
    if (!s_initialized || !name || !factory) {
        return false;
    }
    
    // Check if already registered
    for (int i = 0; i < s_registeredAppCount; i++) {
        if (streq(s_registeredApps[i].name, name)) {
            return false;  // Already registered
        }
    }
    
    if (s_registeredAppCount >= MAX_APPS) {
        return false;
    }
    
    AppInfo& info = s_registeredApps[s_registeredAppCount++];
    strcopy(info.name, name, MAX_APP_NAME);
    info.iconColor = iconColor;
    info.available = true;
    info.factory = factory;
    
    return true;
}

bool AppManager::isAppAvailable(const char* name) {
    if (!s_initialized || !name) {
        return false;
    }
    
    for (int i = 0; i < s_registeredAppCount; i++) {
        if (streq(s_registeredApps[i].name, name)) {
            return s_registeredApps[i].available;
        }
    }
    
    return false;
}

bool AppManager::launchApp(const char* name) {
    if (!s_initialized || !name) {
        AppLogger::logLaunch(name ? name : "unknown", LaunchResult::NotAvailable);
        return false;
    }
    
    // Find registered app
    const AppInfo* info = getAppInfo(name);
    if (!info || !info->available || !info->factory) {
        AppLogger::logLaunch(name, LaunchResult::NotAvailable);
        return false;
    }
    
    // Check if already running
    for (int i = 0; i < s_runningAppCount; i++) {
        if (s_runningApps[i] && streq(s_runningApps[i]->getName(), name)) {
            // Focus existing window
            if (s_runningApps[i]->getWindow()) {
                compositor::KernelCompositor::setFocus(s_runningApps[i]->getWindow()->id);
            }
            AppLogger::logLaunch(name, LaunchResult::AlreadyRunning);
            return true;
        }
    }
    
    // Check for available slot
    if (s_runningAppCount >= MAX_APPS) {
        AppLogger::logLaunch(name, LaunchResult::OutOfResources);
        return false;
    }
    
    // Create app instance
    KernelApp* app = info->factory();
    if (!app) {
        AppLogger::logLaunch(name, LaunchResult::OutOfResources);
        return false;
    }
    
    // Initialize app
    if (!app->init()) {
        delete app;
        AppLogger::logLaunch(name, LaunchResult::FailedToInit);
        return false;
    }
    
    // Add to running list
    s_runningApps[s_runningAppCount++] = app;
    
    AppLogger::logLaunch(name, LaunchResult::Success);
    return true;
}

void AppManager::closeApp(KernelApp* app) {
    if (!s_initialized || !app) return;
    
    // Find and remove from running list
    for (int i = 0; i < s_runningAppCount; i++) {
        if (s_runningApps[i] == app) {
            app->shutdown();
            delete app;
            
            // Shift remaining apps
            for (int j = i; j < s_runningAppCount - 1; j++) {
                s_runningApps[j] = s_runningApps[j + 1];
            }
            s_runningApps[--s_runningAppCount] = nullptr;
            return;
        }
    }
}

int AppManager::getRunningAppCount() {
    return s_runningAppCount;
}

KernelApp* AppManager::getRunningApp(int index) {
    if (index < 0 || index >= s_runningAppCount) {
        return nullptr;
    }
    return s_runningApps[index];
}

const AppInfo* AppManager::getAppInfo(const char* name) {
    if (!name) return nullptr;
    
    for (int i = 0; i < s_registeredAppCount; i++) {
        if (streq(s_registeredApps[i].name, name)) {
            return &s_registeredApps[i];
        }
    }
    return nullptr;
}

void AppManager::processInput(const InputEvent& event) {
    // Route to focused window's app
    compositor::KernelCompositor::handleKeyChar(event.keyChar);
}

void AppManager::update() {
    // Update all running apps and clean up terminated ones
    for (int i = s_runningAppCount - 1; i >= 0; i--) {
        if (s_runningApps[i]) {
            // Check if app has been terminated (window was closed)
            if (s_runningApps[i]->getState() == AppState::Terminated) {
                // Clean up the app
                KernelApp* app = s_runningApps[i];
                
                // Shift remaining apps
                for (int j = i; j < s_runningAppCount - 1; j++) {
                    s_runningApps[j] = s_runningApps[j + 1];
                }
                s_runningApps[--s_runningAppCount] = nullptr;
                
                // Delete the app object
                delete app;
            } else {
                s_runningApps[i]->update();
            }
        }
    }
}

bool AppManager::isBareMetal() {
    // In kernel mode, we're always in bare-metal/UEFI mode
    // This would return false if running under the guideXOSServer
    return true;
}

// ============================================================
// AppLogger implementation
// ============================================================

void AppLogger::init() {
    s_logCount = 0;
    s_logHead = 0;
    s_tickCounter = 0;
}

void AppLogger::logLaunch(const char* appName, LaunchResult result) {
    AppLaunchLog& log = s_logs[s_logHead];
    
    if (appName) {
        strcopy(log.appName, appName, MAX_APP_NAME);
    } else {
        log.appName[0] = '\0';
    }
    
    log.result = result;
    log.timestamp = s_tickCounter++;
    
    s_logHead = (s_logHead + 1) % MAX_LOGS;
    if (s_logCount < MAX_LOGS) {
        s_logCount++;
    }
}

int AppLogger::getLogCount() {
    return s_logCount;
}

const AppLaunchLog* AppLogger::getLog(int index) {
    if (index < 0 || index >= s_logCount) {
        return nullptr;
    }
    
    // Calculate actual index (circular buffer)
    int actualIndex;
    if (s_logCount < MAX_LOGS) {
        actualIndex = index;
    } else {
        actualIndex = (s_logHead + index) % MAX_LOGS;
    }
    
    return &s_logs[actualIndex];
}

void AppLogger::clearLogs() {
    s_logCount = 0;
    s_logHead = 0;
}

} // namespace app
} // namespace kernel
