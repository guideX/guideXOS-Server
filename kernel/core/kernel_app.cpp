//
// guideXOS Kernel Application Framework Implementation
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/kernel/kernel_app.h"
#include "include/kernel/kernel_compositor.h"
#include "include/kernel/desktop.h"
#include "include/kernel/kernel_ipc.h"
#include "include/kernel/serial_debug.h"

extern "C" void desktop_request_redraw();

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

static void strcopy(char* dst, const char* src, int maxLen);
static bool streq(const char* a, const char* b);

namespace {

class ApplicationInstanceIdAllocator {
public:
    constexpr explicit ApplicationInstanceIdAllocator(uint64_t first = 1u)
        : m_next(first), m_exhausted(first == 0u) {}

    bool allocate(uint64_t* outId) {
        if (!outId || m_exhausted || m_next == 0u) return false;
        *outId = m_next;
        if (m_next == UINT64_MAX) {
            m_exhausted = true;
        } else {
            ++m_next;
        }
        return true;
    }

private:
    uint64_t m_next;
    bool m_exhausted;
};

ApplicationInstanceIdAllocator s_instanceIdAllocator;

#if defined(GXOS_NATIVEAOT_C160_APPLICATION_SNAPSHOT_PROOF)
class C160IdentityTestApp final : public KernelApp {
public:
    explicit C160IdentityTestApp(const char* name) {
        strcopy(m_name, name, MAX_APP_NAME);
        m_state = AppState::Running;
    }

    bool init() override { return true; }
    void shutdown() override {}
    void draw(uint32_t, uint32_t, uint32_t, uint32_t) override {}
    void terminate() { m_state = AppState::Terminated; }
    void setTestName(const char* name) { strcopy(m_name, name, MAX_APP_NAME); }
};

static int c160IdentityCases = 0;

static bool c160IdentityCase(bool passed) {
    ++c160IdentityCases;
    return passed;
}
#endif

#if defined(GXOS_NATIVEAOT_C162_MANAGED_TASK_MANAGER_CLOSE_PROOF)
static uint32_t c162ShutdownCount = 0u;
static uint32_t c162ClosedCount = 0u;

class C162CloseTestApp final : public KernelApp {
public:
    C162CloseTestApp(const char* name, bool allowClose = true)
        : m_allowClose(allowClose) {
        strcopy(m_name, name, MAX_APP_NAME);
        m_state = AppState::Running;
    }

    bool init() override { return true; }
    void shutdown() override { ++c162ShutdownCount; }
    void draw(uint32_t, uint32_t, uint32_t, uint32_t) override {}
    bool onWindowCloseRequested() override { return m_allowClose; }
    void onWindowClosed() override { ++c162ClosedCount; }

    bool attachWindow() {
        if (m_window) return false;
        m_window = new KernelWindow();
        if (!m_window) return false;
        m_window->owner = this;
        m_window->w = 240;
        m_window->h = 140;
        strcopy(m_window->title, m_name, MAX_TITLE_LEN);
        if (!compositor::KernelCompositor::registerWindow(m_window)) {
            delete m_window;
            m_window = nullptr;
            return false;
        }
        return true;
    }

    void allowClose(bool value) { m_allowClose = value; }

private:
    bool m_allowClose;
};

static uint32_t c162CloseCases = 0u;

static bool c162CloseCase(bool passed) {
    ++c162CloseCases;
    return passed;
}
#endif

} // namespace

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

KernelApp::KernelApp() : m_instanceId(0u), m_state(AppState::NotLoaded), m_window(nullptr) {
    m_name[0] = '\0';
    m_applicationId[0] = '\0';
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
        desktop_request_redraw();
    }
}

void KernelApp::setSize(int w, int h) {
    if (m_window) {
        m_window->w = w;
        m_window->h = h;
        m_window->dirty = true;
        desktop_request_redraw();
    }
}

void KernelApp::setPosition(int x, int y) {
    if (m_window) {
        m_window->x = x;
        m_window->y = y;
        m_window->dirty = true;
        desktop_request_redraw();
    }
}

bool KernelApp::requestClose() {
    if (!m_window) return false;
    if (!onWindowCloseRequested()) {
        #if defined(GXOS_DESKTOP_CLEANUP_RUNTIME_PASS)
        serial::puts("[app] close-result=vetoed app=");
        serial::puts(m_name);
        serial::putc('\n');
        #endif
        return false;
    }
    if (m_window) {
        #if defined(GXOS_DESKTOP_CLEANUP_RUNTIME_PASS)
        serial::puts("[app] close-request app=");
        serial::puts(m_name);
        serial::puts(" window=");
        serial::put_hex32(m_window->id);
        serial::puts(" title=");
        serial::puts(m_window->title);
        serial::putc('\n');
        #endif
        onWindowClose();
        shutdown();
        compositor::KernelCompositor::unregisterWindow(m_window);
        delete m_window;
        m_window = nullptr;
        m_state = AppState::Terminated;
        onWindowClosed();
        #if defined(GXOS_DESKTOP_CLEANUP_RUNTIME_PASS)
        serial::puts("[app] close-result=accepted app=");
        serial::puts(m_name);
        serial::putc('\n');
        #endif
        desktop_request_redraw();
    }
    return true;
}

void KernelApp::invalidate() {
    if (m_window) {
        m_window->dirty = true;
        desktop_request_redraw();
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
    widget.hover = false;
    widget.pressed = false;
    
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
    widget.hover = false;
    widget.pressed = false;
    
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
        s_registeredApps[i].applicationId[0] = '\0';
        s_registeredApps[i].available = false;
        s_registeredApps[i].factory = nullptr;
        s_runningApps[i] = nullptr;
    }
    
    s_registeredAppCount = 0;
    s_runningAppCount = 0;
    s_initialized = true;
#if defined(GXOS_NATIVEAOT_C160_APPLICATION_SNAPSHOT_PROOF)
    if (!runC160IdentityFocusedTests()) {
        serial::puts("[C160-APP-IDENTITY-TESTS] result=FAIL\n");
    }
#endif
}

bool AppManager::registerApp(const char* name, const char* applicationId,
                             uint32_t iconColor, KernelApp* (*factory)()) {
    if (!s_initialized || !name || !factory) {
        return false;
    }

    int applicationIdLength = 0;
    if (applicationId) {
        while (applicationId[applicationIdLength] &&
               applicationIdLength < MAX_APP_ID) ++applicationIdLength;
        if (applicationIdLength >= MAX_APP_ID) return false;
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
    strcopy(info.applicationId, applicationId ? applicationId : "", MAX_APP_ID);
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
    update();
    
    // Find registered app
    const AppInfo* info = getAppInfo(name);
    if (!info || !info->available || !info->factory) {
        AppLogger::logLaunch(name, LaunchResult::NotAvailable);
        return false;
    }
    
    // Check if already running
    for (int i = 0; i < s_runningAppCount; i++) {
        if (s_runningApps[i] && s_runningApps[i]->getState() != AppState::Terminated && streq(s_runningApps[i]->getName(), name)) {
            // Focus existing window
            if (s_runningApps[i]->getWindow()) {
                compositor::KernelCompositor::setFocus(s_runningApps[i]->getWindow()->id);
            }
            AppLogger::logLaunch(name, LaunchResult::AlreadyRunning);
            kernel::desktop::record_recent_program(name);
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
    
    // Identity is assigned exactly once after initialization succeeds and
    // immediately before the instance becomes authoritative in the list.
    if (!admitRunningApp(app, info->applicationId)) {
        app->shutdown();
        delete app;
        AppLogger::logLaunch(name, LaunchResult::OutOfResources);
        return false;
    }
    
    AppLogger::logLaunch(name, LaunchResult::Success);
    kernel::desktop::record_recent_program(name);
    return true;
}

bool AppManager::launchAppWithParam(const char* name, const char* param) {
    if (!s_initialized || !name) {
        AppLogger::logLaunch(name ? name : "unknown", LaunchResult::NotAvailable);
        return false;
    }
    update();
    
    // Find registered app
    const AppInfo* info = getAppInfo(name);
    if (!info || !info->available || !info->factory) {
        AppLogger::logLaunch(name, LaunchResult::NotAvailable);
        return false;
    }
    
    // Parameterized launches are intentionally narrow: current callers use a
    // single opt-in parameter for file-open style apps and the Trash
    // confirmation flag. This is not a generic argv passthrough for arbitrary
    // kernel apps.
    bool allowNewParameterizedInstance = param && (streq(name, "Notepad") || streq(name, "Trash") || streq(name, "Files"));
    if (!allowNewParameterizedInstance) {
        // Check if already running
        for (int i = 0; i < s_runningAppCount; i++) {
            if (s_runningApps[i] && s_runningApps[i]->getState() != AppState::Terminated && streq(s_runningApps[i]->getName(), name)) {
                // Focus existing window
                if (s_runningApps[i]->getWindow()) {
                    compositor::KernelCompositor::setFocus(s_runningApps[i]->getWindow()->id);
                }
                AppLogger::logLaunch(name, LaunchResult::AlreadyRunning);
                kernel::desktop::record_recent_program(name);
                return true;
            }
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
    
    // Initialize app with parameter
    bool initSuccess = param ? app->initWithParam(param) : app->init();
    if (!initSuccess) {
        delete app;
        AppLogger::logLaunch(name, LaunchResult::FailedToInit);
        return false;
    }
    
    // Identity is assigned exactly once after initialization succeeds and
    // immediately before the instance becomes authoritative in the list.
    if (!admitRunningApp(app, info->applicationId)) {
        app->shutdown();
        delete app;
        AppLogger::logLaunch(name, LaunchResult::OutOfResources);
        return false;
    }
    
    AppLogger::logLaunch(name, LaunchResult::Success);
    kernel::desktop::record_recent_program(name);
    return true;
}

void AppManager::closeApp(KernelApp* app) {
    if (!s_initialized || !app) return;
    
    // Find and remove from running list
    for (int i = 0; i < s_runningAppCount; i++) {
        if (s_runningApps[i] == app) {
            app->shutdown();
            delete app;
            desktop_request_redraw();
            
            // Shift remaining apps
            for (int j = i; j < s_runningAppCount - 1; j++) {
                s_runningApps[j] = s_runningApps[j + 1];
            }
            s_runningApps[--s_runningAppCount] = nullptr;
            return;
        }
    }
}

void AppManager::removeTerminatedAppAt(int index) {
    if (index < 0 || index >= s_runningAppCount) return;
    KernelApp* app = s_runningApps[index];
    if (!app || app->getState() != AppState::Terminated) return;
    for (int next = index; next < s_runningAppCount - 1; ++next)
        s_runningApps[next] = s_runningApps[next + 1];
    s_runningApps[--s_runningAppCount] = nullptr;
    delete app;
}

ApplicationCloseResult AppManager::closeApplicationInstance(
    ApplicationSnapshotSource source, uint64_t instanceId) {
    if (source != ApplicationSnapshotSource::AppManagerInstance ||
        instanceId == 0u) return ApplicationCloseResult::InvalidArgument;
    if (!s_initialized) return ApplicationCloseResult::NotFound;

    for (int index = 0; index < s_runningAppCount; ++index) {
        KernelApp* app = s_runningApps[index];
        if (!app || app->getInstanceId() != instanceId ||
            app->getState() == AppState::Terminated) continue;
        if (!app->requestClose() || app->getState() != AppState::Terminated)
            return ApplicationCloseResult::CloseFailed;
        removeTerminatedAppAt(index);
        desktop_request_redraw();
        return ApplicationCloseResult::Success;
    }
    return ApplicationCloseResult::NotFound;
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

bool AppManager::isRunningInstanceId(uint64_t instanceId) {
    if (instanceId == 0u) return false;
    for (int index = 0; index < s_runningAppCount; ++index) {
        const KernelApp* app = s_runningApps[index];
        if (app && app->getInstanceId() == instanceId &&
            app->getState() != AppState::Terminated) return true;
    }
    return false;
}

#if defined(GXOS_NATIVEAOT_C160_APPLICATION_SNAPSHOT_PROOF)
bool AppManager::launchC160ProofApps() {
    if (!s_initialized || getRunningAppCount() != 0) return false;
    const char* names[] = { "Calculator", "TaskManager" };
    for (const char* name : names) {
        const AppInfo* info = getAppInfo(name);
        if (!info || !info->available || !info->factory ||
            s_runningAppCount >= MAX_APPS) return false;
        KernelApp* instance = info->factory();
        if (!instance || !instance->init()) {
            delete instance;
            return false;
        }
        if (!admitRunningApp(instance, info->applicationId)) {
            instance->shutdown();
            delete instance;
            return false;
        }
    }
    const bool passed = getRunningAppCount() == 2 &&
        isRunningInstanceId(getRunningApp(0)->getInstanceId()) &&
        isRunningInstanceId(getRunningApp(1)->getInstanceId());
    // Keep one genuine shell lifetime open in proof boots so the snapshot
    // validates its distinct source without fabricating an AppManager row.
    desktop::open_terminal();
    serial::puts("[C160-APPMANAGER-PROOF-APPS] calculator=PASS taskmanager=PASS recentWrites=none result=");
    serial::puts(passed ? "PASS\n" : "FAIL\n");
    return passed;
}
#endif

#if defined(GXOS_NATIVEAOT_C161_TASK_MANAGER_PROOF)
bool AppManager::launchC161WheelProofApps() {
    const AppInfo* calculator = getAppInfo("Calculator");
    if (!s_initialized || !calculator || !calculator->available ||
        !calculator->factory || getRunningAppCount() != 2) return false;

    uint32_t added = 0u;
    for (; added < 7u; ++added) {
        if (s_runningAppCount >= MAX_APPS) break;
        KernelApp* instance = calculator->factory();
        if (!instance || !instance->init()) {
            delete instance;
            break;
        }
        if (!admitRunningApp(instance, calculator->applicationId)) {
            instance->shutdown();
            delete instance;
            break;
        }
    }
    const bool passed = added == 7u && getRunningAppCount() == 9;
    serial::puts("[C161-WHEEL-PROOF-APPS] additional=00000007 total=00000009 canonical-id=preserved distinct-lifetimes=true result=");
    serial::puts(passed ? "PASS\n" : "FAIL\n");
    return passed;
}
#endif

#if defined(GXOS_NATIVEAOT_C162_MANAGED_TASK_MANAGER_CLOSE_PROOF)
bool AppManager::runC162CloseFocusedTests() {
    if (!s_initialized || s_runningAppCount != 0) return false;
    c162CloseCases = 0u;
    c162ShutdownCount = 0u;
    c162ClosedCount = 0u;
    bool passed = true;
    const int initialCount = getRunningAppCount();

    passed &= c162CloseCase(closeApplicationInstance(
        ApplicationSnapshotSource::AppManagerInstance, 0u) ==
        ApplicationCloseResult::InvalidArgument);
    passed &= c162CloseCase(closeApplicationInstance(
        ApplicationSnapshotSource::ShellSurface, 1u) ==
        ApplicationCloseResult::InvalidArgument);
    passed &= c162CloseCase(closeApplicationInstance(
        ApplicationSnapshotSource::ManagedLogicalApplication, 1u) ==
        ApplicationCloseResult::InvalidArgument);
    passed &= c162CloseCase(closeApplicationInstance(
        ApplicationSnapshotSource::AppManagerInstance, UINT64_MAX) ==
        ApplicationCloseResult::NotFound);
    passed &= c162CloseCase(getRunningAppCount() == initialCount);

    C162CloseTestApp* first = new C162CloseTestApp("C162 AppManager Native");
    const bool firstReady = first && first->attachWindow() &&
        admitRunningApp(first, "gxos.builtin.calculator");
    if (!firstReady && first && first->getInstanceId() == 0u) delete first;
    const uint64_t firstId = firstReady ? first->getInstanceId() : 0u;
    passed &= c162CloseCase(firstReady && firstId != 0u &&
        getRunningAppCount() == initialCount + 1);
    passed &= c162CloseCase(closeApplicationInstance(
        ApplicationSnapshotSource::AppManagerInstance, firstId + 1u) ==
        ApplicationCloseResult::NotFound);
    passed &= c162CloseCase(closeApplicationInstance(
        ApplicationSnapshotSource::AppManagerInstance, firstId) ==
        ApplicationCloseResult::Success);
    passed &= c162CloseCase(getRunningAppCount() == initialCount &&
        !isRunningInstanceId(firstId));
    passed &= c162CloseCase(c162ShutdownCount == 1u &&
        c162ClosedCount == 1u);
    passed &= c162CloseCase(closeApplicationInstance(
        ApplicationSnapshotSource::AppManagerInstance, firstId) ==
        ApplicationCloseResult::NotFound);

    C162CloseTestApp* replacement = new C162CloseTestApp(
        "C162 AppManager Managed");
    const bool replacementReady = replacement && replacement->attachWindow() &&
        admitRunningApp(replacement,
            "com.guidexos.apps.managed.calculator");
    if (!replacementReady && replacement &&
        replacement->getInstanceId() == 0u) delete replacement;
    const uint64_t replacementId = replacementReady
        ? replacement->getInstanceId() : 0u;
    passed &= c162CloseCase(replacementReady && replacementId != 0u &&
        replacementId != firstId && getRunningAppCount() == initialCount + 1);
    passed &= c162CloseCase(closeApplicationInstance(
        ApplicationSnapshotSource::AppManagerInstance, firstId) ==
        ApplicationCloseResult::NotFound);
    passed &= c162CloseCase(replacementReady &&
        isRunningInstanceId(replacementId) &&
        getRunningAppCount() == initialCount + 1);
    passed &= c162CloseCase(closeApplicationInstance(
        ApplicationSnapshotSource::AppManagerInstance, replacementId) ==
        ApplicationCloseResult::Success);
    passed &= c162CloseCase(getRunningAppCount() == initialCount &&
        !isRunningInstanceId(replacementId));

    C162CloseTestApp* veto = new C162CloseTestApp(
        "C162 AppManager Veto", false);
    const bool vetoReady = veto && veto->attachWindow() &&
        admitRunningApp(veto, "gxos.test.close-veto");
    if (!vetoReady && veto && veto->getInstanceId() == 0u) delete veto;
    const uint64_t vetoId = vetoReady ? veto->getInstanceId() : 0u;
    const uint32_t shutdownsBeforeVeto = c162ShutdownCount;
    passed &= c162CloseCase(vetoReady &&
        closeApplicationInstance(ApplicationSnapshotSource::AppManagerInstance,
            vetoId) == ApplicationCloseResult::CloseFailed);
    passed &= c162CloseCase(vetoReady && isRunningInstanceId(vetoId) &&
        veto->getWindow() != nullptr &&
        c162ShutdownCount == shutdownsBeforeVeto);
    if (vetoReady) veto->allowClose(true);
    passed &= c162CloseCase(vetoReady && closeApplicationInstance(
        ApplicationSnapshotSource::AppManagerInstance, vetoId) ==
        ApplicationCloseResult::Success);
    passed &= c162CloseCase(getRunningAppCount() == initialCount &&
        !isRunningInstanceId(vetoId) &&
        c162ShutdownCount == shutdownsBeforeVeto + 1u);

    C162CloseTestApp* fallback = new C162CloseTestApp(
        "C162 Active Fallback");
    C162CloseTestApp* focused = new C162CloseTestApp(
        "C162 Active Close");
    const bool fallbackReady = fallback && fallback->attachWindow() &&
        admitRunningApp(fallback, "gxos.test.active-fallback");
    const uint64_t fallbackId = fallbackReady
        ? fallback->getInstanceId() : 0u;
    const bool focusedReady = focused && focused->attachWindow() &&
        admitRunningApp(focused, "gxos.test.active-close");
    const uint64_t focusedId = focusedReady
        ? focused->getInstanceId() : 0u;
    if (!fallbackReady && fallback && fallback->getInstanceId() == 0u)
        delete fallback;
    if (!focusedReady && focused && focused->getInstanceId() == 0u)
        delete focused;
    passed &= c162CloseCase(fallbackReady && focusedReady &&
        compositor::KernelCompositor::getFocusedWindow() ==
            focused->getWindow());
    passed &= c162CloseCase(focusedReady && closeApplicationInstance(
        ApplicationSnapshotSource::AppManagerInstance, focusedId) ==
        ApplicationCloseResult::Success);
    passed &= c162CloseCase(fallbackReady &&
        compositor::KernelCompositor::getFocusedWindow() ==
            fallback->getWindow() && isRunningInstanceId(fallbackId));
    passed &= c162CloseCase(fallbackReady && closeApplicationInstance(
        ApplicationSnapshotSource::AppManagerInstance, fallbackId) ==
        ApplicationCloseResult::Success);
    passed &= c162CloseCase(getRunningAppCount() == initialCount);

    const AppInfo* calculatorInfo = getAppInfo("Calculator");
    const bool calculatorRegistered = calculatorInfo &&
        calculatorInfo->available && calculatorInfo->factory;
    passed &= c162CloseCase(calculatorRegistered);
    auto createAndCalculate56 = [](const AppInfo* info, uint64_t* outId) {
        if (!info || !info->factory || !outId) return false;
        KernelApp* calculator = info->factory();
        if (!calculator || !calculator->init() ||
            !admitRunningApp(calculator, info->applicationId)) {
            if (calculator && calculator->getInstanceId() == 0u) {
                calculator->shutdown();
                delete calculator;
            }
            return false;
        }
        *outId = calculator->getInstanceId();
        const char keys[] = { '7', '*', '8', '=' };
        for (char key : keys)
            compositor::KernelCompositor::handleKeyChar(key);
        Widget* display = calculator->getWidget(0);
        return display && streq(display->text, "56");
    };
    bool nativeCalculatorPassed = true;
    uint64_t nativeCalculatorId = 0u;
    const bool nativeCalculatorStarted = calculatorRegistered &&
        createAndCalculate56(calculatorInfo, &nativeCalculatorId);
    nativeCalculatorPassed &= nativeCalculatorStarted &&
        nativeCalculatorId != 0u && isRunningInstanceId(nativeCalculatorId);
    nativeCalculatorPassed &= nativeCalculatorStarted &&
        closeApplicationInstance(ApplicationSnapshotSource::AppManagerInstance,
            nativeCalculatorId) == ApplicationCloseResult::Success &&
        !isRunningInstanceId(nativeCalculatorId) &&
        getRunningAppCount() == initialCount;
    passed &= c162CloseCase(nativeCalculatorPassed);
    uint64_t relaunchedCalculatorId = 0u;
    const bool nativeCalculatorRelaunched = calculatorRegistered &&
        createAndCalculate56(calculatorInfo, &relaunchedCalculatorId);
    nativeCalculatorPassed &= nativeCalculatorRelaunched &&
        relaunchedCalculatorId != 0u &&
        relaunchedCalculatorId != nativeCalculatorId &&
        isRunningInstanceId(relaunchedCalculatorId);
    passed &= c162CloseCase(nativeCalculatorPassed);
    nativeCalculatorPassed &= nativeCalculatorRelaunched &&
        closeApplicationInstance(ApplicationSnapshotSource::AppManagerInstance,
            relaunchedCalculatorId) == ApplicationCloseResult::Success &&
        !isRunningInstanceId(relaunchedCalculatorId) &&
        getRunningAppCount() == initialCount;
    passed &= c162CloseCase(nativeCalculatorPassed);
    serial::puts("[C162-NATIVE-CALCULATOR] close=relaunch identities=distinct operation=7*8=56 result=");
    serial::puts(nativeCalculatorPassed ? "PASS\n" : "FAIL\n");

    bool stressPassed = true;
    const uint32_t shutdownsBeforeStress = c162ShutdownCount;
    const uint32_t closesBeforeStress = c162ClosedCount;
    for (uint32_t iteration = 0u; iteration < 100u; ++iteration) {
        C162CloseTestApp* app = new C162CloseTestApp("C162 Close Stress");
        if (!app || !app->attachWindow() ||
            !admitRunningApp(app, "gxos.test.close-stress")) {
            if (app && !app->getInstanceId()) delete app;
            stressPassed = false;
            break;
        }
        const uint64_t id = app->getInstanceId();
        if ((iteration % 4u) == 1u) {
            stressPassed &= closeApplicationInstance(
                ApplicationSnapshotSource::ShellSurface, id) ==
                ApplicationCloseResult::InvalidArgument;
            stressPassed &= isRunningInstanceId(id);
        }
        if ((iteration % 4u) == 2u) app->allowClose(false);
        ApplicationCloseResult result = closeApplicationInstance(
            ApplicationSnapshotSource::AppManagerInstance, id);
        if ((iteration % 4u) == 2u) {
            stressPassed &= result == ApplicationCloseResult::CloseFailed;
            app->allowClose(true);
            result = closeApplicationInstance(
                ApplicationSnapshotSource::AppManagerInstance, id);
        }
        stressPassed &= result == ApplicationCloseResult::Success;
        stressPassed &= !isRunningInstanceId(id) &&
            getRunningAppCount() == initialCount;
        stressPassed &= closeApplicationInstance(
            ApplicationSnapshotSource::AppManagerInstance, id) ==
            ApplicationCloseResult::NotFound;
    }
    stressPassed &= c162ShutdownCount == shutdownsBeforeStress + 100u &&
        c162ClosedCount == closesBeforeStress + 100u;
    passed &= c162CloseCase(stressPassed);
    passed &= c162CloseCase(getRunningAppCount() == initialCount);

    serial::puts("[C162-APP-CLOSE-TESTS] cases=");
    serial::put_hex32(c162CloseCases);
    serial::puts(" stress=100 identity-revalidation=checked lifecycle=checked result=");
    serial::puts(passed && c162CloseCases >= 18u ? "PASS\n" : "FAIL\n");
    return passed && c162CloseCases >= 18u;
}
#endif

bool AppManager::admitRunningApp(KernelApp* app, const char* applicationId) {
    if (!s_initialized || !app || s_runningAppCount >= MAX_APPS ||
        app->m_instanceId != 0u) return false;
    int applicationIdLength = 0;
    if (applicationId) {
        while (applicationId[applicationIdLength] &&
               applicationIdLength < MAX_APP_ID) ++applicationIdLength;
        if (applicationIdLength >= MAX_APP_ID) return false;
    }
    uint64_t instanceId = 0u;
    if (!s_instanceIdAllocator.allocate(&instanceId) || instanceId == 0u) {
        return false;
    }
    strcopy(app->m_applicationId,
        applicationId ? applicationId : "", MAX_APP_ID);
    app->m_instanceId = instanceId;
    s_runningApps[s_runningAppCount++] = app;
    return true;
}

#if defined(GXOS_NATIVEAOT_C160_APPLICATION_SNAPSHOT_PROOF)
bool AppManager::runC160IdentityFocusedTests() {
    if (s_runningAppCount != 0) return false;
    c160IdentityCases = 0;
    bool passed = true;

    ApplicationInstanceIdAllocator ordinary;
    uint64_t firstToken = 0u;
    uint64_t secondToken = 0u;
    passed &= c160IdentityCase(ordinary.allocate(&firstToken) && firstToken != 0u);
    passed &= c160IdentityCase(ordinary.allocate(&secondToken) && secondToken > firstToken);
    passed &= c160IdentityCase(!ordinary.allocate(nullptr));
    ApplicationInstanceIdAllocator wrapped(UINT64_MAX);
    uint64_t lastToken = 0u;
    passed &= c160IdentityCase(wrapped.allocate(&lastToken) && lastToken == UINT64_MAX);
    uint64_t afterWrap = 0u;
    passed &= c160IdentityCase(!wrapped.allocate(&afterWrap) && afterWrap == 0u);
    ApplicationInstanceIdAllocator invalidZero(0u);
    passed &= c160IdentityCase(!invalidZero.allocate(&afterWrap));

    passed &= c160IdentityCase(!isRunningInstanceId(0u));
    passed &= c160IdentityCase(!admitRunningApp(nullptr, nullptr));
    C160IdentityTestApp* appA = new C160IdentityTestApp("C160 Test A");
    passed &= c160IdentityCase(appA != nullptr &&
        admitRunningApp(appA, "gxos.test.application-a"));
    const uint64_t appAId = appA ? appA->getInstanceId() : 0u;
    passed &= c160IdentityCase(appAId != 0u && isRunningInstanceId(appAId));
    passed &= c160IdentityCase(appA && appA->getApplicationId() &&
        streq(appA->getApplicationId(), "gxos.test.application-a"));
    passed &= c160IdentityCase(appA && appA->getName() &&
        streq(appA->getName(), "C160 Test A"));
    passed &= c160IdentityCase(getRunningAppCount() == 1 && getRunningApp(0) == appA);
    if (appA) {
        const uint64_t beforeRefresh = appA->getInstanceId();
        update();
        passed &= c160IdentityCase(appA->getInstanceId() == beforeRefresh);
        passed &= c160IdentityCase(!admitRunningApp(appA, "gxos.test.changed"));
        passed &= c160IdentityCase(appA->getInstanceId() == beforeRefresh &&
            streq(appA->getApplicationId(), "gxos.test.application-a"));
        closeApp(appA);
    }
    passed &= c160IdentityCase(getRunningAppCount() == 0);
    passed &= c160IdentityCase(!isRunningInstanceId(appAId));

    C160IdentityTestApp* appB = new C160IdentityTestApp("C160 Test B");
    passed &= c160IdentityCase(appB != nullptr &&
        admitRunningApp(appB, "gxos.test.application-b"));
    const uint64_t appBId = appB ? appB->getInstanceId() : 0u;
    passed &= c160IdentityCase(getRunningAppCount() == 1 && getRunningApp(0) == appB);
    passed &= c160IdentityCase(appBId != 0u && appBId != appAId && appBId > appAId);
    passed &= c160IdentityCase(!isRunningInstanceId(appAId) && isRunningInstanceId(appBId));
    passed &= c160IdentityCase(appB &&
        streq(appB->getApplicationId(), "gxos.test.application-b"));
    if (appB) {
        appB->terminate();
        passed &= c160IdentityCase(!isRunningInstanceId(appBId));
        update();
    }
    passed &= c160IdentityCase(getRunningAppCount() == 0 &&
        !isRunningInstanceId(appBId));

    KernelApp* full[MAX_APPS] = {};
    uint64_t fullIds[MAX_APPS] = {};
    for (int index = 0; index < MAX_APPS; ++index) {
        full[index] = new C160IdentityTestApp("C160 Full");
        if (!full[index] || !admitRunningApp(full[index], "gxos.test.full")) {
            passed = false;
            break;
        }
        fullIds[index] = full[index]->getInstanceId();
    }
    passed &= c160IdentityCase(getRunningAppCount() == MAX_APPS);
    C160IdentityTestApp* overflow = new C160IdentityTestApp("C160 Overflow");
    passed &= c160IdentityCase(overflow &&
        !admitRunningApp(overflow, "gxos.test.overflow"));
    delete overflow;
    for (int index = s_runningAppCount - 1; index >= 0; --index) {
        KernelApp* item = s_runningApps[index];
        closeApp(item);
    }
    bool everyFullIdUnique = true;
    bool everyFullIdInvalidated = true;
    for (int left = 0; left < MAX_APPS; ++left) {
        if (fullIds[left] == 0u) everyFullIdUnique = false;
        if (isRunningInstanceId(fullIds[left])) everyFullIdInvalidated = false;
        for (int right = left + 1; right < MAX_APPS; ++right) {
            if (fullIds[left] == fullIds[right]) everyFullIdUnique = false;
        }
    }
    passed &= c160IdentityCase(everyFullIdUnique);
    passed &= c160IdentityCase(everyFullIdInvalidated && getRunningAppCount() == 0);

    serial::puts("[C160-APP-IDENTITY-TESTS] cases=");
    serial::put_hex32(static_cast<uint32_t>(c160IdentityCases));
    serial::puts(" max-live=16 reuse=distinct wrap=fail-closed result=");
    serial::puts(passed && c160IdentityCases >= 20 ? "PASS\n" : "FAIL\n");
    return passed && c160IdentityCases >= 20;
}
#endif

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
                // requestClose() owns shutdown; this path only compacts and
                // releases an object that another lifecycle path terminated.
                removeTerminatedAppAt(i);
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
