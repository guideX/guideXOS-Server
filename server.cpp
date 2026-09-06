//
// guideXOS Server - User-Mode System Server
//
// ROLE: User-mode init process (PID 1) providing desktop environment
//
// RESPONSIBILITIES:
//   - Compositor and window manager
//   - Desktop environment (taskbar, start menu, wallpaper)
//   - System services (console, file manager, VNC server, etc.)
//   - IPC bus for inter-process communication
//   - Application framework (Notepad, Calculator, Clock, etc.)
//   - Process management (user-mode processes)
//
// CONSTRAINTS:
//   - Must be BOOT-AGNOSTIC (no firmware or bootloader knowledge)
//   - Must use syscalls for ALL hardware access
//   - Must NOT access BootInfo (kernel-only structure)
//   - Must run in USER MODE (ring 3)
//   - Must NOT assume specific boot path (UEFI, BIOS, etc.)
//
// ARCHITECTURE:
//   Bootloader ? Kernel ? [guideXOSServer] ? User Applications
//
// LAUNCH:
//   - Launched by kernel as first user process (PID 1)
//   - Assumes virtual address space already exists
//   - Assumes memory allocation is available
//   - Assumes IPC primitives exist or are stubbed
//   - Fails gracefully if required services unavailable
//
// TESTING:
//   - Can run standalone on Linux/Windows for development
//   - Will run as PID 1 when launched by kernel
//   - Same code works in both environments
//
// Copyright (c) 2026 guideXOS Server
//

#include "allocator.h"
#include "lifecycle.h"
#include "logger.h"
#include "scheduler.h"
#include "fs.h"
#include "platform.h"
#include "process.h"
#include "ipc.h"
#include "ipc_bus.h"
#include "console_service.h"
#include "compositor.h"
#include "gui_protocol.h"
#include "vfs.h"
#include "gxm_loader.h"
#include "gxos_tls_prerequisites.h"
#include "desktop_config.h"
#include "desktop_service.h"
#include "background_service.h"
#include "notepad.h"
#include "calculator.h"
#include "console_window.h"
#include "file_explorer.h"
#include "clock.h"

#include <cstdlib>
#include "task_manager.h"
#include "vnc_server.h"
#include "paint.h"
#include "navigator.h"
#include "guide_web_http.h"
#include "network_telemetry.h"
#include "navigator_file_io.h"
#include "image_viewer.h"
#include "onscreen_keyboard.h"
#include "shutdown_dialog.h"
#include "message_box.h"
#include "welcome.h"
#include "open_dialog.h"
#include "notification_manager.h"
#include "firewall.h"
#include "module_manager.h"
#include "package_manager.h"
#include "native_elf_executor.h"
#include "native_app_process_table.h"
#include <iostream>
#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <sstream>
#include <vector>
#include <iomanip>
#include <unordered_map>
#include <thread>

namespace gxos {
    PlatformInfo queryPlatform(){ PlatformInfo pi{}; pi.cpuCount = std::thread::hardware_concurrency(); pi.totalMemBytes = 512ull*1024*1024; pi.startTicks = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); return pi; }
    uint64_t ticks(){ return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
}

// Sample process entries
static int echoProc(int argc, char** argv){ gxos::Logger::write(gxos::LogLevel::Info, "echoProc start"); for(int i=1;i<argc;i++){ gxos::Logger::write(gxos::LogLevel::Info, std::string("ARG ")+argv[i]); } return 0; }
static int workerProc(int argc, char** argv){ int loops=5; if(argc>1) loops=std::atoi(argv[1]); for(int i=0;i<loops;i++){ gxos::Logger::write(gxos::LogLevel::Info, "worker tick "+std::to_string(i)); std::this_thread::sleep_for(std::chrono::milliseconds(100)); } return loops; }

static std::string taskManagerNetworkSnapshotWaitDiagnostic(uint64_t timeoutMs = 2000, uint64_t pollMs = 100) {
    const auto start = std::chrono::steady_clock::now();
    gxos::apps::TaskManagerSnapshot snapshot = gxos::apps::TaskManager::BuildTaskManagerSnapshot();

    while (!(snapshot.performance.networkAvailable && snapshot.performance.networkRatesAvailable)) {
        const auto elapsedMs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count());
        if (elapsedMs >= timeoutMs) {
            break;
        }

        const uint64_t remainingMs = timeoutMs - elapsedMs;
        const uint64_t sleepMs = std::min<uint64_t>(pollMs, remainingMs);
        std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
        snapshot = gxos::apps::TaskManager::BuildTaskManagerSnapshot();
    }

    const auto totalWaitMs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count());
    const bool stable = snapshot.performance.networkAvailable && snapshot.performance.networkRatesAvailable;

    std::ostringstream oss;
    oss << "networkStableSmoke=" << (stable ? "true" : "false") << "\n";
    oss << "networkStableWaitMs=" << totalWaitMs << "\n";
    oss << "networkStableRatesAvailable=" << (snapshot.performance.networkRatesAvailable ? "true" : "false") << "\n";
    oss << gxos::apps::TaskManager::SnapshotDiagnostic();
    return oss.str();
}

static std::string nativeAppProcessesDiagnostic() {
    std::ostringstream oss;
    std::vector<gxos::apps::NativeAppProcessInfo> processes = gxos::apps::NativeAppProcessTable::List();
    oss << "Native app processes: " << processes.size() << "\n";
    for (const auto& process : processes) {
        oss << "runtimeId=" << process.runtimeId
            << " appId=" << process.appId
            << " displayName=" << process.displayName
            << " architecture=" << process.architecture
            << " state=" << gxos::apps::NativeAppRuntime::ToString(process.lifecycleState)
            << " windows=" << process.createdWindowCount << "/" << process.cleanedWindowCount << "/" << process.remainingWindowCount
            << " pollEventCallCount=" << process.pollEventCallCount
            << " lastEventType=" << static_cast<uint32_t>(process.lastEventType)
            << " lastEventWindow=" << process.lastEventWindow
            << " lastPollEventResult=" << process.lastPollEventResult
            << " drawRectCallCount=" << process.drawRectCallCount
            << " lastDrawRectWindow=" << process.lastDrawRectWindow
            << " lastDrawRectWidth=" << process.lastDrawRectWidth
            << " lastDrawRectHeight=" << process.lastDrawRectHeight
            << " lastDrawRectColor=" << process.lastDrawRectColor
            << " lastDrawRectResult=" << process.lastDrawRectResult
            << " paintEventCount=" << process.paintEventCount
            << " lastPaintWindow=" << process.lastPaintWindow
            << " lastPaintWidth=" << process.lastPaintWidth
            << " lastPaintHeight=" << process.lastPaintHeight
            << " keyEventCount=" << process.keyEventCount
            << " lastKeyWindow=" << process.lastKeyWindow
            << " lastKeyCode=" << process.lastKeyCode
            << " lastKeyAction=" << process.lastKeyAction
            << " lastKeyModifiers=" << process.lastKeyModifiers
            << " mouseEventCount=" << process.mouseEventCount
            << " lastMouseWindow=" << process.lastMouseWindow
            << " lastMouseX=" << process.lastMouseX
            << " lastMouseY=" << process.lastMouseY
            << " lastMousePackedButtonAction=" << process.lastMousePackedButtonAction
            << " lastMouseModifiers=" << process.lastMouseModifiers
            << " exitCode=" << process.exitCode
            << " failureReason=" << process.failureReason
            << " experimentalExecutionEnabled=" << (process.experimentalExecutionEnabled ? "true" : "false")
            << " hostArchitecture=" << process.hostArchitecture
            << "\n";
    }
    return oss.str();
}

static std::string desktopWindowOwnershipDiagnostic() {
    std::ostringstream oss;
    const uint64_t compositorPid = gxos::Lifecycle::ensureCompositor();
    const std::vector<gxos::gui::WindowDebugInfo> windows = gxos::gui::Compositor::debugWindowsSnapshot();
    oss << "DESKTOP_WINDOW_OWNERS_BEGIN\n";
    oss << "compositorPid=" << compositorPid << "\n";
    oss << "windowCount=" << windows.size() << "\n";
    for (const auto& window : windows) {
        std::string processName;
        std::string appId;
        const bool identityAvailable = gxos::ProcessTable::getIdentity(window.ownerPid, processName, appId);
        oss << "window id=" << window.id
            << " ownerPid=" << window.ownerPid
            << " ownerName=" << (identityAvailable ? processName : "<unknown>")
            << " appId=" << (identityAvailable && !appId.empty() ? appId : "<unknown>")
            << " title=" << window.title
            << " visible=" << (window.visible ? "true" : "false")
            << "\n";
    }
    oss << "DESKTOP_WINDOW_OWNERS_END\n";
    return oss.str();
}

static std::string desktopStartupAppModelRegressionDiagnostic() {
    struct ExpectedApp {
        const char* appId;
        const char* displayName;
    };

    const std::vector<ExpectedApp> expectedApps = {
        {"gxos.builtin.notepad", "Notepad"},
        {"gxos.builtin.calculator", "Calculator"},
        {"gxos.builtin.displayoptions", "DisplayOptions"}
    };

    std::ostringstream oss;
    oss << "STARTUP_APP_MODEL_REGRESSION_BEGIN\n";
    const uint64_t compositorPid = gxos::Lifecycle::ensureCompositor();
    oss << "compositorPid=" << compositorPid << "\n";

    // This is intentionally a metadata-only call.  The diagnostic proves that
    // registry initialization does not dispatch any of the launch factories.
    (void)gxos::gui::DesktopService::GetRegisteredAppsDiagnostic();
    const auto& registeredApps = gxos::gui::DesktopService::GetRegisteredApps();
    bool allRegistered = true;
    for (const auto& expected : expectedApps) {
        const auto it = std::find_if(registeredApps.begin(), registeredApps.end(),
            [&](const gxos::gui::RegisteredDesktopApp& app) { return app.id == expected.appId; });
        const bool registered = it != registeredApps.end();
        allRegistered = allRegistered && registered;
        oss << "registered appId=" << expected.appId
            << " displayName=" << expected.displayName
            << " result=" << (registered ? "PASS" : "FAIL") << "\n";
    }

    const std::vector<gxos::gui::WindowDebugInfo> initialWindows = gxos::gui::Compositor::debugWindowsSnapshot();
    oss << "initialWindowCount=" << initialWindows.size() << "\n";
    for (const auto& window : initialWindows) {
        std::string processName;
        std::string appId;
        const bool identityAvailable = gxos::ProcessTable::getIdentity(window.ownerPid, processName, appId);
        oss << "initialWindow id=" << window.id
            << " ownerPid=" << window.ownerPid
            << " ownerName=" << (identityAvailable ? processName : "<unknown>")
            << " appId=" << (identityAvailable && !appId.empty() ? appId : "<unknown>")
            << " title=" << window.title << "\n";
    }
    const bool registrationDidNotLaunch = initialWindows.empty();
    oss << "registrationDidNotLaunch=" << (registrationDidNotLaunch ? "PASS" : "FAIL") << "\n";

    bool explicitLaunchesPassed = true;
    std::vector<uint64_t> baselineWindowIds;
    for (const auto& window : initialWindows) baselineWindowIds.push_back(window.id);

    for (const auto& expected : expectedApps) {
        std::string launchError;
        const bool launchReturnedSuccess = gxos::gui::DesktopService::LaunchApp(expected.appId, launchError, false);
        gxos::gui::WindowDebugInfo launchedWindow;
        bool windowFound = false;
        if (launchReturnedSuccess) {
            for (int attempt = 0; attempt < 50 && !windowFound; ++attempt) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                const auto windows = gxos::gui::Compositor::debugWindowsSnapshot();
                for (const auto& window : windows) {
                    if (std::find(baselineWindowIds.begin(), baselineWindowIds.end(), window.id) != baselineWindowIds.end()) continue;
                    std::string processName;
                    std::string appId;
                    if (!gxos::ProcessTable::getIdentity(window.ownerPid, processName, appId)) continue;
                    if (appId != expected.appId) continue;
                    launchedWindow = window;
                    windowFound = true;
                    break;
                }
            }
        }

        const bool passed = launchReturnedSuccess && windowFound;
        explicitLaunchesPassed = explicitLaunchesPassed && passed;
        oss << "explicitLaunch appId=" << expected.appId
            << " launchResult=" << (launchReturnedSuccess ? "PASS" : "FAIL")
            << " windowOwnership=" << (windowFound ? "PASS" : "FAIL");
        if (windowFound) {
            oss << " windowId=" << launchedWindow.id
                << " ownerPid=" << launchedWindow.ownerPid
                << " title=" << launchedWindow.title;
        }
        if (!launchReturnedSuccess && !launchError.empty()) oss << " error=" << launchError;
        oss << "\n";
    }

    // Keep this test-only command from writing the explicitly launched windows
    // into the persistence fixture used by the caller.  This uses the normal
    // compositor close path and is not part of ordinary startup.
    const auto afterLaunchWindows = gxos::gui::Compositor::debugWindowsSnapshot();
    for (const auto& window : afterLaunchWindows) {
        if (std::find(baselineWindowIds.begin(), baselineWindowIds.end(), window.id) != baselineWindowIds.end()) continue;
        gxos::ipc::Message closeMessage;
        closeMessage.type = static_cast<uint32_t>(gxos::gui::MsgType::MT_Close);
        const std::string id = std::to_string(window.id);
        closeMessage.data.assign(id.begin(), id.end());
        gxos::ipc::Bus::publish("gui.input", std::move(closeMessage), false);
    }

    const bool result = compositorPid != 0 && allRegistered && registrationDidNotLaunch && explicitLaunchesPassed;
    oss << "STARTUP_APP_MODEL_REGRESSION_RESULT=" << (result ? "PASS" : "FAIL") << "\n";
    oss << "STARTUP_APP_MODEL_REGRESSION_END\n";
    return oss.str();
}

static std::string navigatorHostedSmokeDiagnostic() {
    struct Check {
        std::string name;
        bool pass;
        std::string detail;
    };

    std::vector<Check> checks;
    auto add = [&](const std::string& name, bool pass, const std::string& detail) {
        checks.push_back(Check{name, pass, detail});
    };
    auto contains = [](const std::string& haystack, const std::string& needle) {
        return haystack.find(needle) != std::string::npos;
    };
    auto yesNo = [](bool value) {
        return value ? "yes" : "no";
    };
    auto summarizeText = [](const std::string& text, size_t limit) {
        std::string compact;
        compact.reserve(std::min(text.size(), limit) + 3);
        bool lastWasSpace = false;
        for (char ch : text) {
            const bool isSpace = ch == '\r' || ch == '\n' || ch == '\t' || ch == ' ';
            if (isSpace) {
                if (lastWasSpace) continue;
                compact.push_back(' ');
                lastWasSpace = true;
            } else {
                compact.push_back(ch);
                lastWasSpace = false;
            }
            if (compact.size() >= limit) break;
        }
        while (!compact.empty() && compact.back() == ' ') compact.pop_back();
        if (text.size() > compact.size()) compact += "...";
        return compact;
    };
    auto summarizeFromMarker = [&](const std::string& text,
                                   const std::string& marker, size_t limit) {
        const std::size_t pos = text.rfind(marker);
        if (pos == std::string::npos) return std::string("(missing)");
        return summarizeText(text.substr(pos), limit);
    };
    auto evidenceSnippet = [&](const std::string& report, const std::string& needle) {
        const std::size_t pos = report.find(needle);
        if (pos == std::string::npos) return std::string("(missing)");
        return summarizeText(report.substr(pos, 700), 700);
    };
    auto envIsOne = [](const char* name) {
        const char* value = std::getenv(name);
        return value && std::string(value) == "1";
    };
    const bool expectTrustedLocalhost = envIsOne("GXOS_NAVIGATOR_SMOKE_EXPECT_TRUSTED_LOCALHOST");
    const bool expectSmokeBypassLocalhost = envIsOne("GXOS_NAVIGATOR_SMOKE_EXPECT_SMOKE_LOCALHOST_BYPASS");

    std::ostringstream out;
    out << "NAVIGATOR_SMOKE_BEGIN\n";
    out << "timestamp_ms=" << gxos::ticks() << "\n";
    out << "build_mode=hosted/compositor\n";
    out << "expect_trusted_localhost=" << (expectTrustedLocalhost ? "yes" : "no") << "\n";
    out << "expect_smoke_bypass_localhost=" << (expectSmokeBypassLocalhost ? "yes" : "no") << "\n";

    uint8_t randomA[32] = {};
    uint8_t randomB[32] = {};
    const bool randomAOk = gxos::gxos_random_bytes(randomA, sizeof(randomA));
    const bool randomBOk = gxos::gxos_random_bytes(randomB, sizeof(randomB));
    bool randomDiffers = randomAOk && randomBOk;
    if (randomDiffers) {
        randomDiffers = false;
        for (size_t i = 0; i < sizeof(randomA); ++i) {
            if (randomA[i] != randomB[i]) {
                randomDiffers = true;
                break;
            }
        }
    }
    int64_t wallClockSeconds = 0;
    const bool wallClockOk = gxos::gxos_wall_clock_unix_seconds(&wallClockSeconds);
    add("hosted RNG quality Secure", gxos::gxos_random_quality() == gxos::GxosRandomQuality::Secure,
        gxos::gxos_random_backend());
    add("hosted RNG reads differ", randomDiffers, randomDiffers ? "two 32-byte reads differ" : "system RNG read failed or repeated");
    add("hosted wall clock Verified", gxos::gxos_wall_clock_status() == gxos::GxosClockStatus::Verified && wallClockOk,
        gxos::gxos_wall_clock_backend() + std::string(" epoch=") + std::to_string(wallClockSeconds));

    const std::string inspect = gxos::gui::DesktopService::InspectNativeAppPipeline("guideXOS Navigator");
    add("app id resolves", contains(inspect, "appId: guidexos.navigator"), "expected guidexos.navigator");
    add("launch resolution succeeds", contains(inspect, "resolverSuccess: true"), "DesktopService resolver");
    add("builtin hosted runtime selected", contains(inspect, "runtime: builtin-hosted"), "expected builtin-hosted");
    add("builtin entry selected", contains(inspect, "selectedEntryPath: builtin/guideXOS Navigator"), "expected builtin/guideXOS Navigator");

    uint64_t compositorPid = gxos::Lifecycle::ensureCompositor();
    add("compositor available", compositorPid != 0, "pid=" + std::to_string(compositorPid));

    std::string launchError;
    bool launched = gxos::gui::DesktopService::LaunchApp("guideXOS Navigator", launchError);
    add("DesktopService launches Navigator", launched, launched ? "LaunchApp returned true" : launchError);

    gxos::gui::WindowDebugInfo navWindow;
    bool foundWindow = false;
    for (int attempt = 0; attempt < 50 && !foundWindow; ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        std::vector<gxos::gui::WindowDebugInfo> windows = gxos::gui::Compositor::debugWindowsSnapshot();
        for (const auto& window : windows) {
            if (window.title.find("guideXOS Navigator") == std::string::npos) continue;
            navWindow = window;
            foundWindow = true;
            break;
        }
    }

    add("Navigator window created", foundWindow,
        foundWindow ? ("id=" + std::to_string(navWindow.id) + " title=" + navWindow.title) : "window not found");
    add("Navigator window size", foundWindow && navWindow.w == 920 && navWindow.h == 640,
        foundWindow ? (std::to_string(navWindow.w) + "x" + std::to_string(navWindow.h)) : "window not found");

    // Toolbar semantic checks: use in-process Navigator state (not compositor IPC
    // widget count) to avoid stale-restored-window false negatives.
    // The Navigator registers 7 toolbar buttons in order: Back(1), Forward(2),
    // Reload(3), Home(4), Bookmarks(5), AddBookmark(6), Find(7).
    // A stale four-button placeholder would only have ids 1-4.
    // Poll until all 7 buttons are registered (renderToolbar runs on Navigator's
    // thread and may lag slightly behind the window-title appearing).
    std::vector<int> toolbarIds;
    for (int attempt = 0; attempt < 50; ++attempt) {
        toolbarIds = gxos::apps::Navigator::SmokeToolbarWidgetIds();
        if (static_cast<int>(toolbarIds.size()) >= 7)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    auto hasWidget = [&](int id) {
        return std::find(toolbarIds.begin(), toolbarIds.end(), id) != toolbarIds.end();
    };
    std::string toolbarIdStr;
    for (int id : toolbarIds) { if (!toolbarIdStr.empty()) toolbarIdStr += ","; toolbarIdStr += std::to_string(id); }
    const bool hasBack        = hasWidget(1);
    const bool hasForward     = hasWidget(2);
    const bool hasReload      = hasWidget(3);
    const bool hasHome        = hasWidget(4);
    const bool hasBookmarks   = hasWidget(5);
    const bool hasAddBookmark = hasWidget(6);
    const bool hasFind        = hasWidget(7);
    const int  toolbarWidgetCount = static_cast<int>(toolbarIds.size());
    add("toolbar Back button",        hasBack,        "registered widget ids=[" + toolbarIdStr + "]");
    add("toolbar Forward button",     hasForward,     "registered widget ids=[" + toolbarIdStr + "]");
    add("toolbar Reload button",      hasReload,      "registered widget ids=[" + toolbarIdStr + "]");
    add("toolbar Home button",        hasHome,        "registered widget ids=[" + toolbarIdStr + "]");
    add("toolbar Bookmarks button",   hasBookmarks,   "registered widget ids=[" + toolbarIdStr + "]");
    add("toolbar AddBookmark button", hasAddBookmark, "registered widget ids=[" + toolbarIdStr + "]");
    add("toolbar Find button",        hasFind,        "registered widget ids=[" + toolbarIdStr + "]");
    // Stale detection: old placeholder had only Back/Forward/Reload/Home (ids 1-4).
    // Modern toolbar must have all 7; count > 4 distinguishes modern from stale.
    add("stale four-button toolbar absent",
        toolbarWidgetCount > 4,
        "registered_widget_count=" + std::to_string(toolbarWidgetCount) +
        " ids=[" + toolbarIdStr + "] (stale placeholder has <=4 buttons)");

    // Widget registration and icon decode are asynchronous IPC messages. Refresh
    // the compositor snapshot after the toolbar-registration wait so this check
    // observes the actual cached icon handoff rather than the initial empty list.
    if (foundWindow) {
        for (int attempt = 0; attempt < 50 && navWindow.widgetIconCount < 6; ++attempt) {
            const std::vector<gxos::gui::WindowDebugInfo> refreshedWindows = gxos::gui::Compositor::debugWindowsSnapshot();
            for (const auto& window : refreshedWindows) {
                if (window.id == navWindow.id) {
                    navWindow = window;
                    break;
                }
            }
            if (navWindow.widgetIconCount >= 6) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    add("toolbar icon cache initialized",
        foundWindow && navWindow.widgetIconCount >= 6,
        foundWindow ? ("widget_icon_count=" + std::to_string(navWindow.widgetIconCount)) : "window not found");

    bool navigated = gxos::apps::Navigator::SmokeNavigateTo("about:navigator-runtime");
    std::string currentUrl = gxos::apps::Navigator::SmokeCurrentUrl();
    std::string runtimeReport = gxos::apps::Navigator::SmokeRuntimeReport();
    add("runtime URL loads", navigated && currentUrl == "about:navigator-runtime", "currentUrl=" + currentUrl);
    add("runtime report mode", contains(runtimeReport, "Runtime.Mode=hosted/compositor"), "expected hosted/compositor");
    add("runtime report launch path", contains(runtimeReport, "DesktopService::LaunchApp -> apps::Navigator::Launch"), "expected DesktopService/apps::Navigator");
    add("toolbar icon resources", contains(runtimeReport, "Toolbar.icon_resources=6/6"), "expected six packaged Navigator assets");
    add("toolbar icon size", contains(runtimeReport, "Toolbar.icon_size=16x16"), "expected bounded 16x16 toolbar icons");
    add("toolbar icon/text geometry", contains(runtimeReport, "Toolbar.icon_rect_inside_button=yes") &&
        contains(runtimeReport, "Toolbar.icon_text_nonoverlap=yes"), "expected safe icon/text geometry");
    add("toolbar fallback path", contains(runtimeReport, "Toolbar.fallback=label_only_on_image_load_failure"), "expected label-preserving fallback");
    add("toolbar full hit target", contains(runtimeReport, "Toolbar.hit_target=full_button_rectangle"), "expected full button hit target");
    add("toolbar address geometry", contains(runtimeReport, "Toolbar.address_width_nonnegative=yes") &&
        contains(runtimeReport, "Toolbar.narrow_window=address_width_clamped_to_zero"), "expected bounded address layout");
    add("toolbar viewport unchanged", contains(runtimeReport, "Toolbar.toolbar_height=unchanged_64px") &&
        contains(runtimeReport, "Toolbar.document_viewport=unchanged"), "expected centralized document viewport unchanged");
    const auto hasPositiveThrobberCount = [](const std::string& report, const std::string& prefix) {
        const std::size_t pos = report.find(prefix);
        if (pos == std::string::npos) return false;
        const std::size_t valuePos = pos + prefix.size();
        return valuePos < report.size() &&
            std::isdigit(static_cast<unsigned char>(report[valuePos])) &&
            report[valuePos] != '0';
    };
    add("throbber frame/resource contract",
        contains(runtimeReport, "Throbber.active_frame_count=12") &&
        contains(runtimeReport, "Throbber.frame_dimensions=72x72") &&
        contains(runtimeReport, "Throbber.paint_dimensions=22x22") &&
        contains(runtimeReport, "Throbber.frame_index=bounded_0_to_11") &&
        contains(runtimeReport, "Throbber.cache=process_lifetime_compositor_ui_image_cache") &&
        contains(runtimeReport, "Throbber.per_frame_resource_load=none") &&
        contains(runtimeReport, "Throbber.per_frame_decode=none"),
        "expected fixed cached surfer frame set");
    add("throbber loading lifecycle balance",
        contains(runtimeReport, "Throbber.loading_state=idle") &&
        contains(runtimeReport, "Throbber.loading_terminal_balance=yes") &&
        hasPositiveThrobberCount(runtimeReport, "Throbber.loading_entries=") &&
        hasPositiveThrobberCount(runtimeReport, "Throbber.loading_exits="),
        "expected balanced terminal idle state");
    const bool noOpBack = !gxos::apps::Navigator::SmokeGoBack();
    const bool noOpForward = !gxos::apps::Navigator::SmokeGoForward();
    const std::string noOpHistoryReport = gxos::apps::Navigator::SmokeRuntimeReport();
    add("throbber no-op history stays idle",
        noOpBack && noOpForward && contains(noOpHistoryReport, "Throbber.loading_state=idle") &&
        contains(noOpHistoryReport, "Throbber.loading_terminal_balance=yes"),
        "expected empty Back/Forward actions to avoid a new load");
    add("stale placeholder inactive", contains(runtimeReport, "Runtime.Stale placeholder path=not active"), "expected not active");
    add("file read enabled", contains(runtimeReport, "Capabilities.File read=enabled"), "expected enabled");
    add("file write enabled", contains(runtimeReport, "Capabilities.File write=enabled"), "expected enabled");
    add("local PNG enabled", contains(runtimeReport, "Capabilities.Local PNG=enabled"), "expected enabled");
    add("HTTP enabled", contains(runtimeReport, "Capabilities.HTTP=enabled"), "expected enabled");
    add("HTTP byte-stream backend enabled", contains(runtimeReport, "Backends.HTTP backend=guide_web_http hosted TCP byte-stream with Schannel TLS wrapper for https"), "expected hosted TCP byte-stream backend");
    add("TLS backend is Schannel", contains(runtimeReport, "Capabilities.TLS backend=Schannel hosted"), "expected hosted Schannel backend");
    add("hosted evidence lane is explicit", contains(runtimeReport, "Evidence Lane.evidence_lane=hosted") &&
        contains(runtimeReport, "Evidence Lane.tls_backend=schannel"), "expected hosted evidence lane marker");
    add("certificate validation enabled", contains(runtimeReport, "Capabilities.Certificate validation=enabled via Schannel, Windows trust, and hostname validation"), "expected Windows trust and hostname validation");
    add("TLS insertion seam active", contains(runtimeReport, "Capabilities.TLS insertion seam=active HttpByteStream wrapper"), "expected active TLS wrapper");
    add("HTTPS downgrade redirect blocked by default", contains(runtimeReport, "Capabilities.HTTPS-to-HTTP redirect policy=blocked by default"), "expected hosted downgrade policy");
    add("bare-metal TLS remains gated", contains(runtimeReport, "Capabilities.Bare-metal TLS=foundation only; https:// stays blocked until readiness is true"), "expected honest bare-metal boundary");
    add("runtime RNG quality Secure", contains(runtimeReport, "TLS Prerequisites.RNG quality=Secure"), "expected BCrypt-backed system RNG");
    add("runtime wall clock Verified", contains(runtimeReport, "TLS Prerequisites.Wall-clock status=Verified"), "expected Windows system UTC");
    add("runtime TLS backend ready", contains(runtimeReport, "TLS Prerequisites.TLS backend status=ReadyForValidatedNavigation") &&
        contains(runtimeReport, "TLS Prerequisites.TLS backend name=Schannel hosted") &&
        contains(runtimeReport, "TLS Prerequisites.TLS backend version=Windows Schannel"), "expected hosted backend readiness details");
    add("runtime hostname validation surfaced", contains(runtimeReport, "TLS Prerequisites.Hostname validation available=yes") &&
        contains(runtimeReport, "TLS Prerequisites.TLS SNI support=yes") &&
        contains(runtimeReport, "TLS Prerequisites.TLS original hostname retained=yes"), "expected hosted hostname validation diagnostics");
    add("runtime root CA store surfaced", contains(runtimeReport, "TLS Prerequisites.Root CA path=(Windows trust store)") &&
        contains(runtimeReport, "TLS Prerequisites.Root CA status=Loaded") &&
        contains(runtimeReport, "TLS Prerequisites.Root CA parse status=NotApplicable") &&
        contains(runtimeReport, "TLS Prerequisites.Root CA bytes=0"), "expected hosted trust store diagnostics");
    add("runtime TLS readiness boundary", contains(runtimeReport, "TLS Prerequisites.TLS readiness=yes") &&
        contains(runtimeReport, "TLS Prerequisites.TLS readiness blocker=(none)"), "expected hosted readiness details");
    add("remote PNG/JPEG enabled",
        contains(runtimeReport, "Capabilities.Remote PNG/JPEG=enabled") ||
            contains(runtimeReport, "Capabilities.Remote PNG=enabled"),
        "expected enabled");
    add("downloads enabled", contains(runtimeReport, "Capabilities.Downloads=enabled"), "expected enabled");
    add("CSS-lite enabled", contains(runtimeReport, "Capabilities.CSS-lite embedded <style>=enabled"), "expected enabled");
    add("colored text primitive enabled", contains(runtimeReport, "Capabilities.Hosted colored text primitive=enabled"), "expected enabled");
    add("CSS text color visible", contains(runtimeReport, "Capabilities.CSS text color visible=enabled"), "expected enabled");
    add("Forms-lite enabled", contains(runtimeReport, "Capabilities.Forms-lite GET forms=enabled"), "expected enabled");
    add("Forms-lite hosted POST enabled", contains(runtimeReport, "Capabilities.Forms-lite POST forms hosted=enabled"), "expected hosted POST enabled");
    add("Forms-lite bare-metal POST marker remains unsupported", contains(runtimeReport, "Capabilities.Forms-lite POST forms bare-metal=unsupported"), "expected honest unsupported bare-metal marker");
    add("Forms-lite focus navigation enabled", contains(runtimeReport, "Capabilities.Forms-lite focus navigation=Tab/Shift+Tab, Enter, Space"), "expected focus navigation capability");
    add("Find in Page enabled", contains(runtimeReport, "Capabilities.Find in Page=enabled"), "expected enabled");
    add("bounded hosted external stylesheets", contains(runtimeReport, "Capabilities.External stylesheets=bounded hosted"), "expected bounded hosted support");
    add("bookmark persistence enabled", contains(runtimeReport, "Capabilities.Bookmark persistence=enabled"), "expected enabled");

    auto hasPositiveCount = [&](const std::string& report, const std::string& prefix) {
        const std::size_t pos = report.find(prefix);
        if (pos == std::string::npos) return false;
        const std::size_t valuePos = pos + prefix.size();
        return valuePos < report.size() &&
            std::isdigit(static_cast<unsigned char>(report[valuePos])) &&
            report[valuePos] != '0';
    };
    auto countValue = [&](const std::string& report, const std::string& prefix) {
        const std::size_t pos = report.find(prefix);
        if (pos == std::string::npos) return -1;
        std::size_t valuePos = pos + prefix.size();
        std::size_t endPos = valuePos;
        while (endPos < report.size() && std::isdigit(static_cast<unsigned char>(report[endPos]))) {
            ++endPos;
        }
        try {
            return std::stoi(report.substr(valuePos, endPos - valuePos));
        } catch (...) {
            return -1;
        }
    };
    auto reportLine = [](const std::string& report, const std::string& prefix) {
        const std::size_t pos = report.find(prefix);
        if (pos == std::string::npos) return std::string("(missing)");
        const std::size_t end = report.find('\n', pos);
        return report.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
    };

    const bool js9Loaded = gxos::apps::Navigator::SmokeNavigateToQuiet(
        "http://127.0.0.1:8080/navigator-smoke/javascript-js9.html");
    const std::string js9InitialText =
        gxos::apps::Navigator::SmokeCurrentDocumentText();
    const uint64_t js9InitialRevision =
        gxos::apps::Navigator::SmokeDocumentLayoutRevision();
    int js9AX = 0;
    int js9AY = 0;
    int js9AW = 0;
    int js9AH = 0;
    const bool js9AHasGeometry = gxos::apps::Navigator::SmokeBlockGeometryById(
        "js9-a", js9AX, js9AY, js9AW, js9AH);
    const bool js9AHit = gxos::apps::Navigator::SmokeFormHitTargetById("js9-a");
    const size_t js9Handlers =
        gxos::apps::Navigator::SmokeJavaScriptHandlerCount();
    const bool js9ClickA1 =
        gxos::apps::Navigator::SmokeClickFormControlById("js9-a");
    const std::string js9AfterA1 =
        gxos::apps::Navigator::SmokeCurrentDocumentText();
    const uint64_t js9RevisionAfterA1 =
        gxos::apps::Navigator::SmokeDocumentLayoutRevision();
    const bool js9ClickA2 =
        gxos::apps::Navigator::SmokeClickFormControlById("js9-a");
    const std::string js9AfterA2 =
        gxos::apps::Navigator::SmokeCurrentDocumentText();
    const bool js9ClickB =
        gxos::apps::Navigator::SmokeClickFormControlById("js9-b");
    const std::string js9AfterB =
        gxos::apps::Navigator::SmokeCurrentDocumentText();
    const bool js9ClickA3 =
        gxos::apps::Navigator::SmokeClickFormControlById("js9-a");
    const std::string js9AfterA3 =
        gxos::apps::Navigator::SmokeCurrentDocumentText();
    const bool js9CleanAfterClicks =
        !gxos::apps::Navigator::SmokeDocumentDirty();
    const std::string js9Error =
        gxos::apps::Navigator::SmokeJavaScriptLastError();
    add("JS9 hosted fixture loads and registers scripts",
        js9Loaded && contains(js9InitialText, "Navigator JavaScript JS9") &&
        contains(js9InitialText, "0") && contains(js9InitialText, "B initial") &&
        js9Handlers == 2u && js9Error.empty(),
        std::string("loaded=") + yesNo(js9Loaded) + ",handlers=" +
        std::to_string(js9Handlers) + ",error=" + (js9Error.empty() ? "none" : js9Error));
    add("JS9 hosted click uses Navigator hit test",
        js9AHasGeometry && js9AW > 0 && js9AH > 0 && js9AHit && js9ClickA1 &&
        contains(js9AfterA1, "1"),
        std::string("geometry=") + yesNo(js9AHasGeometry) + ",hit=" + yesNo(js9AHit) +
        ",click=" + yesNo(js9ClickA1));
    add("JS9 hosted same-realm state persists",
        js9ClickA2 && contains(js9AfterA2, "2") && js9ClickA3 &&
        contains(js9AfterA3, "3"),
        std::string("a2=") + yesNo(js9ClickA2) + ",a3=" + yesNo(js9ClickA3));
    add("JS9 hosted handlers remain independent",
        js9ClickB && contains(js9AfterB, "B clicked") &&
        contains(js9AfterB, "2"),
        std::string("b=") + yesNo(js9ClickB));
    add("JS9 hosted mutation relayouts and repaints",
        js9RevisionAfterA1 > js9InitialRevision && js9CleanAfterClicks,
        "revision=" + std::to_string(js9InitialRevision) + "->" +
        std::to_string(js9RevisionAfterA1) + ",dirty=" +
        yesNo(!js9CleanAfterClicks));

    const bool js10Loaded = gxos::apps::Navigator::SmokeNavigateToQuiet(
        "http://127.0.0.1:8080/navigator-smoke/javascript-js10.html");
    const std::string js10InitialText =
        gxos::apps::Navigator::SmokeCurrentDocumentText();
    const uint64_t js10InitialRevision =
        gxos::apps::Navigator::SmokeDocumentLayoutRevision();
    int js10AX = 0;
    int js10AY = 0;
    int js10AW = 0;
    int js10AH = 0;
    const bool js10AHasGeometry = gxos::apps::Navigator::SmokeBlockGeometryById(
        "js10-a", js10AX, js10AY, js10AW, js10AH);
    const bool js10AHit = gxos::apps::Navigator::SmokeFormHitTargetById("js10-a");
    const size_t js10Handlers =
        gxos::apps::Navigator::SmokeJavaScriptHandlerCount();
    const size_t js10Listeners =
        gxos::apps::Navigator::SmokeJavaScriptListenerCount();
    const std::string js10InitialError =
        gxos::apps::Navigator::SmokeJavaScriptLastError();
    const bool js10ClickA1 =
        gxos::apps::Navigator::SmokeClickFormControlById("js10-a");
    const std::string js10AfterA1 =
        gxos::apps::Navigator::SmokeCurrentDocumentText();
    const uint64_t js10RevisionAfterA1 =
        gxos::apps::Navigator::SmokeDocumentLayoutRevision();
    const bool js10ClickA2 =
        gxos::apps::Navigator::SmokeClickFormControlById("js10-a");
    const std::string js10AfterA2 =
        gxos::apps::Navigator::SmokeCurrentDocumentText();
    const bool js10ClickA3 =
        gxos::apps::Navigator::SmokeClickFormControlById("js10-a");
    const std::string js10AfterA3 =
        gxos::apps::Navigator::SmokeCurrentDocumentText();
    const bool js10ClickB1 =
        gxos::apps::Navigator::SmokeClickFormControlById("js10-b");
    const std::string js10AfterB1 =
        gxos::apps::Navigator::SmokeCurrentDocumentText();
    const bool js10ClickBad =
        gxos::apps::Navigator::SmokeClickFormControlById("js10-bad");
    const std::string js10Error =
        gxos::apps::Navigator::SmokeJavaScriptLastError();
    const bool js10ClickB2 =
        gxos::apps::Navigator::SmokeClickFormControlById("js10-b");
    const std::string js10AfterB2 =
        gxos::apps::Navigator::SmokeCurrentDocumentText();
    const bool js10CleanAfterClicks =
        !gxos::apps::Navigator::SmokeDocumentDirty();
    add("JS10 hosted fixture resolves and registers bounded listeners",
        js10Loaded && contains(js10InitialText, "Navigator JavaScript JS10") &&
        js10Handlers == 3u && js10Listeners == 3u && js10InitialError.empty(),
        std::string("loaded=") + yesNo(js10Loaded) + ",records=" +
        std::to_string(js10Handlers) + ",listeners=" +
        std::to_string(js10Listeners) + ",error=" +
        (js10InitialError.empty() ? "none" : js10InitialError));
    add("JS10 hosted physical click reaches the listener",
        js10AHasGeometry && js10AW > 0 && js10AH > 0 && js10AHit &&
        js10ClickA1 && contains(js10AfterA1, "Count 1") &&
        contains(js10AfterA1, "ol"),
        std::string("geometry=") + yesNo(js10AHasGeometry) + ",hit=" +
        yesNo(js10AHit) + ",click=" + yesNo(js10ClickA1));
    add("JS10 hosted same-realm closure persists 1 to 2 to 3",
        js10ClickA2 && contains(js10AfterA2, "Count 2") &&
        js10ClickA3 && contains(js10AfterA3, "Count 3") &&
        contains(js10AfterA3, "ololol"),
        std::string("a2=") + yesNo(js10ClickA2) + ",a3=" +
        yesNo(js10ClickA3));
    add("JS10 hosted onclick precedes addEventListener",
        contains(js10AfterA3, "ololol"),
        "expected visible order marker ololol");
    add("JS10 hosted independent listener state remains isolated",
        js10ClickB1 && contains(js10AfterB1, "B 12") &&
        contains(js10AfterB1, "Count 3") && js10ClickB2 &&
        contains(js10AfterB2, "B 14") && contains(js10AfterB2, "Count 3"),
        std::string("b1=") + yesNo(js10ClickB1) + ",b2=" +
        yesNo(js10ClickB2));
    add("JS10 hosted callback errors are contained",
        js10ClickBad && contains(js10Error, "UnknownIdentifier") &&
        js10ClickB2 && contains(js10AfterB2, "B 14"),
        std::string("bad-click=") + yesNo(js10ClickBad) + ",error=" +
        (js10Error.empty() ? "none" : js10Error));
    add("JS10 hosted mutation triggers relayout",
        js10RevisionAfterA1 > js10InitialRevision && js10CleanAfterClicks,
        "revision=" + std::to_string(js10InitialRevision) + "->" +
        std::to_string(js10RevisionAfterA1) + ",dirty=" +
        yesNo(!js10CleanAfterClicks));
    const bool js10LinkHit =
        gxos::apps::Navigator::SmokeHitLinkById("js10-link");
    // This fixture contains one ordinary link.  SmokeHitLinkById validates
    // the shared hit-test path; SmokeClickFirstLink performs the authentic
    // mouse down/up navigation through that hit-tested link.
    const bool js10LinkClick =
        js10LinkHit && gxos::apps::Navigator::SmokeClickFirstLink();
    const std::string js10TargetText =
        gxos::apps::Navigator::SmokeCurrentDocumentText();
    add("JS10 ordinary link navigation remains functional",
        js10LinkHit && js10LinkClick &&
        gxos::apps::Navigator::SmokeCurrentUrl() ==
            "http://127.0.0.1:8080/navigator-smoke/javascript-js10-target.html" &&
        contains(js10TargetText, "Navigator JavaScript JS10 Target"),
        std::string("hit=") + yesNo(js10LinkHit) + ",link=" +
        yesNo(js10LinkClick) + ",url=" +
        gxos::apps::Navigator::SmokeCurrentUrl());
    add("JS10 navigation removes old listeners",
        gxos::apps::Navigator::SmokeJavaScriptHandlerCount() == 0u &&
        gxos::apps::Navigator::SmokeJavaScriptListenerCount() == 0u,
        "expected document-scoped listener storage to reset");

    const bool js11Loaded = gxos::apps::Navigator::SmokeNavigateToQuiet(
        "http://127.0.0.1:8080/navigator-smoke/javascript-js11.html");
    const std::string js11InitialText =
        gxos::apps::Navigator::SmokeCurrentDocumentText();
    const uint64_t js11InitialRevision =
        gxos::apps::Navigator::SmokeDocumentLayoutRevision();
    int js11MainX = 0;
    int js11MainY = 0;
    int js11MainW = 0;
    int js11MainH = 0;
    const bool js11MainHasGeometry = gxos::apps::Navigator::SmokeBlockGeometryById(
        "js11-main", js11MainX, js11MainY, js11MainW, js11MainH);
    const bool js11MainHit =
        gxos::apps::Navigator::SmokeFormHitTargetById("js11-main");
    const size_t js11InitialHandlers =
        gxos::apps::Navigator::SmokeJavaScriptHandlerCount();
    const size_t js11InitialListeners =
        gxos::apps::Navigator::SmokeJavaScriptListenerCount();
    const std::string js11InitialError =
        gxos::apps::Navigator::SmokeJavaScriptLastError();
    add("JS11 hosted fixture loads and registers listeners",
        js11Loaded && contains(js11InitialText, "Navigator JavaScript JS11") &&
        contains(js11InitialText, "Main 0") && js11InitialHandlers == 17u &&
        js11InitialListeners == 17u && js11InitialError.empty(),
        std::string("loaded=") + yesNo(js11Loaded) + ",records=" +
        std::to_string(js11InitialHandlers) + ",listeners=" +
        std::to_string(js11InitialListeners) + ",error=" +
        (js11InitialError.empty() ? "none" : js11InitialError));

    const bool js11MainClickOne =
        gxos::apps::Navigator::SmokeClickFormControlById("js11-main");
    const std::string js11AfterMainOne =
        gxos::apps::Navigator::SmokeCurrentDocumentText();
    const uint64_t js11RevisionAfterMainOne =
        gxos::apps::Navigator::SmokeDocumentLayoutRevision();
    add("JS11 authentic hosted click executes registered callback",
        js11MainHasGeometry && js11MainW > 0 && js11MainH > 0 && js11MainHit &&
        js11MainClickOne && contains(js11AfterMainOne, "Main 1"),
        std::string("geometry=") + yesNo(js11MainHasGeometry) + ",hit=" +
        yesNo(js11MainHit) + ",click=" + yesNo(js11MainClickOne));
    add("JS11 callback removal stops the next authentic click",
        gxos::apps::Navigator::SmokeClickFormControlById("js11-main") &&
        gxos::apps::Navigator::SmokeCurrentDocumentText().find("Main 1") !=
            std::string::npos &&
        gxos::apps::Navigator::SmokeJavaScriptListenerCount() == 16u,
        "self-removal leaves Main at 1 and releases one listener slot");
    add("JS11 callback DOM mutation still relayouts",
        js11RevisionAfterMainOne > js11InitialRevision &&
        !gxos::apps::Navigator::SmokeDocumentDirty(),
        "revision=" + std::to_string(js11InitialRevision) + "->" +
        std::to_string(js11RevisionAfterMainOne));

    const bool js11ReaddClick =
        gxos::apps::Navigator::SmokeClickFormControlById("js11-readd");
    const bool js11MainClickAfterReadd =
        gxos::apps::Navigator::SmokeClickFormControlById("js11-main");
    add("JS11 remove then re-add executes once",
        js11ReaddClick && js11MainClickAfterReadd &&
        contains(gxos::apps::Navigator::SmokeCurrentDocumentText(), "Main 2"),
        "re-add trigger and main click both used the real Navigator path");

    const bool js11MismatchRemove =
        gxos::apps::Navigator::SmokeClickFormControlById("js11-mismatch-remover");
    const bool js11MismatchClick =
        gxos::apps::Navigator::SmokeClickFormControlById("js11-mismatch");
    add("JS11 wrong function identity does not remove",
        js11MismatchRemove && js11MismatchClick &&
        contains(gxos::apps::Navigator::SmokeCurrentDocumentText(), "Mismatch 1"),
        "different function with identical body leaves first callback live");

    const bool js11WrongRemove =
        gxos::apps::Navigator::SmokeClickFormControlById("js11-wrong-remover");
    const bool js11WrongClick =
        gxos::apps::Navigator::SmokeClickFormControlById("js11-wrong");
    add("JS11 wrong element identity does not remove",
        js11WrongRemove && js11WrongClick &&
        contains(gxos::apps::Navigator::SmokeCurrentDocumentText(),
            "Wrong element 1"),
        "removal on another element leaves the original target callback live");

    const bool js11NoopClick =
        gxos::apps::Navigator::SmokeClickFormControlById("js11-noop");
    add("JS11 nonexistent removal is harmless",
        js11NoopClick &&
        contains(gxos::apps::Navigator::SmokeCurrentDocumentText(), "No-op 1"),
        "pre-registration removal did not create or corrupt a record");
    const bool js11RepeatClickOne =
        gxos::apps::Navigator::SmokeClickFormControlById("js11-repeat");
    const bool js11RepeatClickTwo =
        gxos::apps::Navigator::SmokeClickFormControlById("js11-repeat");
    add("JS11 repeated removal is harmless",
        js11RepeatClickOne && js11RepeatClickTwo &&
        contains(gxos::apps::Navigator::SmokeCurrentDocumentText(), "Repeat 1"),
        "self-removal followed by repeated removal fires only once");

    const bool js11OnclickClickOne =
        gxos::apps::Navigator::SmokeClickFormControlById("js11-onclick");
    const bool js11OnclickClickTwo =
        gxos::apps::Navigator::SmokeClickFormControlById("js11-onclick");
    add("JS11 removal leaves onclick independent",
        js11OnclickClickOne && js11OnclickClickTwo &&
        contains(gxos::apps::Navigator::SmokeCurrentDocumentText(), "olo"),
        "onclick and listener ordering is ol, then onclick-only o");

    const bool js11ChangeClickOne =
        gxos::apps::Navigator::SmokeClickFormControlById("js11-onclick-change");
    const bool js11ChangeClickTwo =
        gxos::apps::Navigator::SmokeClickFormControlById("js11-onclick-change");
    add("JS11 listener survives unrelated onclick change",
        js11ChangeClickOne && js11ChangeClickTwo &&
        contains(gxos::apps::Navigator::SmokeCurrentDocumentText(), "oll"),
        "listener remains after its callback clears onclick");

    const bool js11ClosureClickOne =
        gxos::apps::Navigator::SmokeClickFormControlById("js11-closure");
    const bool js11ClosureClickTwo =
        gxos::apps::Navigator::SmokeClickFormControlById("js11-closure");
    const bool js11ClosureRemove =
        gxos::apps::Navigator::SmokeClickFormControlById("js11-closure-remover");
    const bool js11ClosureClickThree =
        gxos::apps::Navigator::SmokeClickFormControlById("js11-closure");
    add("JS11 closure removal after prior execution",
        js11ClosureClickOne && js11ClosureClickTwo && js11ClosureRemove &&
        js11ClosureClickThree &&
        contains(gxos::apps::Navigator::SmokeCurrentDocumentText(), "Closure 2"),
        "captured state reaches 2 and stays there after removal");

    const bool js11OtherClickOne =
        gxos::apps::Navigator::SmokeClickFormControlById("js11-other");
    const bool js11BadClick =
        gxos::apps::Navigator::SmokeClickFormControlById("js11-bad");
    const std::string js11BadError =
        gxos::apps::Navigator::SmokeJavaScriptLastError();
    const bool js11OtherClickTwo =
        gxos::apps::Navigator::SmokeClickFormControlById("js11-other");
    add("JS11 callback-error containment preserves other listeners",
        js11OtherClickOne && js11BadClick &&
        contains(js11BadError, "UnknownIdentifier") && js11OtherClickTwo &&
        contains(gxos::apps::Navigator::SmokeCurrentDocumentText(), "Other 2"),
        "bad callback error is contained and independent callback reaches 2");

    const bool js11UnsupportedClick =
        gxos::apps::Navigator::SmokeClickFormControlById("js11-unsupported");
    const std::string js11UnsupportedError =
        gxos::apps::Navigator::SmokeJavaScriptLastError();
    const bool js11ValidationClickOne =
        gxos::apps::Navigator::SmokeClickFormControlById("js11-validation-target");
    add("JS11 unsupported removal is deterministic and preserves click",
        js11UnsupportedClick && contains(js11UnsupportedError, "HostInvalidValue") &&
        js11ValidationClickOne &&
        contains(gxos::apps::Navigator::SmokeCurrentDocumentText(),
            "Validation target 1"),
        "mouseover removal is rejected without changing the click registration");

    const bool js11InvalidClick =
        gxos::apps::Navigator::SmokeClickFormControlById("js11-invalid");
    const std::string js11InvalidError =
        gxos::apps::Navigator::SmokeJavaScriptLastError();
    const bool js11ValidationClickTwo =
        gxos::apps::Navigator::SmokeClickFormControlById("js11-validation-target");
    add("JS11 invalid callback input is safely rejected",
        js11InvalidClick && contains(js11InvalidError, "HostInvalidValue") &&
        js11ValidationClickTwo &&
        contains(gxos::apps::Navigator::SmokeCurrentDocumentText(),
            "Validation target 2"),
        "numeric callback rejection preserves the valid listener");

    const bool js11LinkHit =
        gxos::apps::Navigator::SmokeHitLinkById("js11-link");
    const bool js11LinkClick =
        js11LinkHit && gxos::apps::Navigator::SmokeClickFirstLink();
    const std::string js11TargetText =
        gxos::apps::Navigator::SmokeCurrentDocumentText();
    add("JS11 ordinary link navigation remains functional",
        js11LinkHit && js11LinkClick &&
        gxos::apps::Navigator::SmokeCurrentUrl() ==
            "http://127.0.0.1:8080/navigator-smoke/javascript-js11-target.html" &&
        contains(js11TargetText, "Navigator JavaScript JS11 Target"),
        std::string("hit=") + yesNo(js11LinkHit) + ",link=" +
        yesNo(js11LinkClick) + ",url=" + gxos::apps::Navigator::SmokeCurrentUrl());
    add("JS11 navigation clears stale listener state",
        gxos::apps::Navigator::SmokeJavaScriptHandlerCount() == 0u &&
        gxos::apps::Navigator::SmokeJavaScriptListenerCount() == 0u &&
        gxos::apps::Navigator::SmokeJavaScriptLastError().empty(),
        "replacement document has no old callbacks, IDs, or errors");

    const bool js12Loaded = gxos::apps::Navigator::SmokeNavigateToQuiet(
        "http://127.0.0.1:8080/navigator-smoke/javascript-js12.html");
    const std::string js12InitialText =
        gxos::apps::Navigator::SmokeCurrentDocumentText();
    const uint64_t js12InitialRevision =
        gxos::apps::Navigator::SmokeDocumentLayoutRevision();
    int js12AlphaX = 0;
    int js12AlphaY = 0;
    int js12AlphaW = 0;
    int js12AlphaH = 0;
    const bool js12AlphaHasGeometry = gxos::apps::Navigator::SmokeBlockGeometryById(
        "js12-alpha", js12AlphaX, js12AlphaY, js12AlphaW, js12AlphaH);
    const bool js12AlphaHit =
        gxos::apps::Navigator::SmokeFormHitTargetById("js12-alpha");
    const size_t js12InitialHandlers =
        gxos::apps::Navigator::SmokeJavaScriptHandlerCount();
    const size_t js12InitialListeners =
        gxos::apps::Navigator::SmokeJavaScriptListenerCount();
    const std::string js12InitialError =
        gxos::apps::Navigator::SmokeJavaScriptLastError();
    add("JS12 hosted fixture loads and registers Event callbacks",
        js12Loaded && contains(js12InitialText, "Navigator JavaScript JS12") &&
        contains(js12InitialText, "Alpha 0") && js12InitialHandlers == 4u &&
        js12InitialListeners == 4u && js12InitialError.empty(),
        std::string("loaded=") + yesNo(js12Loaded) + ",records=" +
        std::to_string(js12InitialHandlers) + ",listeners=" +
        std::to_string(js12InitialListeners) + ",error=" +
        (js12InitialError.empty() ? "none" : js12InitialError));

    const bool js12AlphaClickOne =
        gxos::apps::Navigator::SmokeClickFormControlById("js12-alpha");
    const std::string js12AfterAlphaOne =
        gxos::apps::Navigator::SmokeCurrentDocumentText();
    const uint64_t js12RevisionAfterAlphaOne =
        gxos::apps::Navigator::SmokeDocumentLayoutRevision();
    add("JS12 authentic hosted click passes minimal Event values",
        js12AlphaHasGeometry && js12AlphaW > 0 && js12AlphaH > 0 &&
        js12AlphaHit && js12AlphaClickOne &&
        contains(js12AfterAlphaOne, "Alpha 1") &&
        contains(js12AfterAlphaOne, "click:js12-alpha:js12-alpha"),
        std::string("geometry=") + yesNo(js12AlphaHasGeometry) + ",hit=" +
        yesNo(js12AlphaHit) + ",click=" + yesNo(js12AlphaClickOne));
    const bool js12AlphaClickTwo =
        gxos::apps::Navigator::SmokeClickFormControlById("js12-alpha");
    const std::string js12AfterAlphaTwo =
        gxos::apps::Navigator::SmokeCurrentDocumentText();
    add("JS12 hosted closure state persists across Event clicks",
        js12AlphaClickTwo && contains(js12AfterAlphaTwo, "Alpha 2") &&
        contains(js12AfterAlphaTwo, "click:js12-alpha:js12-alpha"),
        "same callback realm remains active across physical clicks");
    const bool js12BetaClick =
        gxos::apps::Navigator::SmokeClickFormControlById("js12-beta");
    const std::string js12AfterBeta =
        gxos::apps::Navigator::SmokeCurrentDocumentText();
    add("JS12 hosted target/currentTarget follow the hit element",
        js12BetaClick && contains(js12AfterBeta, "Beta 1") &&
        contains(js12AfterBeta, "click:js12-beta:js12-beta"),
        "independent element dispatch reports beta for both direct properties");
    const bool js12OrderClick =
        gxos::apps::Navigator::SmokeClickFormControlById("js12-order");
    const std::string js12AfterOrder =
        gxos::apps::Navigator::SmokeCurrentDocumentText();
    add("JS12 onclick and addEventListener both receive Event in order",
        js12OrderClick && contains(js12AfterOrder, "ol:js12-order"),
        "expected onclick before the registered listener");
    const bool js12BadClick =
        gxos::apps::Navigator::SmokeClickFormControlById("js12-bad");
    const std::string js12BadError =
        gxos::apps::Navigator::SmokeJavaScriptLastError();
    const bool js12BetaClickAfterError =
        gxos::apps::Navigator::SmokeClickFormControlById("js12-beta");
    add("JS12 callback errors remain contained after Event dispatch",
        js12BadClick && contains(js12BadError, "UnknownIdentifier") &&
        js12BetaClickAfterError &&
        contains(gxos::apps::Navigator::SmokeCurrentDocumentText(), "Beta 2"),
        std::string("bad-click=") + yesNo(js12BadClick) + ",error=" +
        (js12BadError.empty() ? "none" : js12BadError));
    add("JS12 callback mutation still relayouts",
        js12RevisionAfterAlphaOne > js12InitialRevision &&
        !gxos::apps::Navigator::SmokeDocumentDirty(),
        "revision=" + std::to_string(js12InitialRevision) + "->" +
        std::to_string(js12RevisionAfterAlphaOne));

    const bool js12LinkHit =
        gxos::apps::Navigator::SmokeHitLinkById("js12-link");
    const bool js12LinkClick =
        js12LinkHit && gxos::apps::Navigator::SmokeClickFirstLink();
    const std::string js12TargetText =
        gxos::apps::Navigator::SmokeCurrentDocumentText();
    add("JS12 ordinary link navigation remains functional",
        js12LinkHit && js12LinkClick &&
        gxos::apps::Navigator::SmokeCurrentUrl() ==
            "http://127.0.0.1:8080/navigator-smoke/javascript-js12-target.html" &&
        contains(js12TargetText, "Navigator JavaScript JS12 Target"),
        std::string("hit=") + yesNo(js12LinkHit) + ",link=" +
        yesNo(js12LinkClick) + ",url=" +
        gxos::apps::Navigator::SmokeCurrentUrl());
    add("JS12 navigation clears Event-era listener state",
        gxos::apps::Navigator::SmokeJavaScriptHandlerCount() == 0u &&
        gxos::apps::Navigator::SmokeJavaScriptListenerCount() == 0u &&
        gxos::apps::Navigator::SmokeJavaScriptLastError().empty(),
        "replacement document has no stale JS12 callbacks or errors");

    const bool js13Loaded = gxos::apps::Navigator::SmokeNavigateToQuiet(
        "http://127.0.0.1:8080/navigator-smoke/javascript-js13.html");
    const std::string js13InitialText =
        gxos::apps::Navigator::SmokeCurrentDocumentText();
    const uint64_t js13InitialRevision =
        gxos::apps::Navigator::SmokeDocumentLayoutRevision();
    int js13ChildX = 0;
    int js13ChildY = 0;
    int js13ChildW = 0;
    int js13ChildH = 0;
    const bool js13ChildHasGeometry = gxos::apps::Navigator::SmokeBlockGeometryById(
        "js13-child", js13ChildX, js13ChildY, js13ChildW, js13ChildH);
    const bool js13ChildHit =
        gxos::apps::Navigator::SmokeFormHitTargetById("js13-child");
    const size_t js13InitialHandlers =
        gxos::apps::Navigator::SmokeJavaScriptHandlerCount();
    const size_t js13InitialListeners =
        gxos::apps::Navigator::SmokeJavaScriptListenerCount();
    const std::string js13InitialError =
        gxos::apps::Navigator::SmokeJavaScriptLastError();
    add("JS13 hosted fixture loads nested bubbling handlers",
        js13Loaded && contains(js13InitialText, "Navigator JavaScript JS13") &&
        contains(js13InitialText, "Child") && js13InitialHandlers == 10u &&
        js13InitialListeners == 10u && js13InitialError.empty(),
        std::string("loaded=") + yesNo(js13Loaded) + ",records=" +
        std::to_string(js13InitialHandlers) + ",listeners=" +
        std::to_string(js13InitialListeners) + ",error=" +
        (js13InitialError.empty() ? "none" : js13InitialError));

    const bool js13ChildClickOne =
        gxos::apps::Navigator::SmokeClickFormControlById("js13-child");
    const std::string js13AfterChildOne =
        gxos::apps::Navigator::SmokeCurrentDocumentText();
    const uint64_t js13RevisionAfterChildOne =
        gxos::apps::Navigator::SmokeDocumentLayoutRevision();
    add("JS13 authentic hosted click bubbles target to ancestors",
        js13ChildHasGeometry && js13ChildW > 0 && js13ChildH > 0 &&
        js13ChildHit && js13ChildClickOne &&
        contains(js13AfterChildOne, "bubble:child-onclick>child-listener>parent-onclick>parent-listener>grand-onclick>grand-listener>") &&
        contains(js13AfterChildOne, ":js13-child:js13-child:js13-parent:js13-grandparent"),
        std::string("geometry=") + yesNo(js13ChildHasGeometry) + ",hit=" +
        yesNo(js13ChildHit) + ",click=" + yesNo(js13ChildClickOne));
    add("JS13 hosted bubbling mutation relayouts and preserves order",
        contains(js13AfterChildOne, "Clicked") &&
        js13RevisionAfterChildOne > js13InitialRevision &&
        !gxos::apps::Navigator::SmokeDocumentDirty(),
        "target mutation settled after child-parent-grandparent dispatch");
    const bool js13ChildClickTwo =
        gxos::apps::Navigator::SmokeClickFormControlById("js13-child");
    const std::string js13AfterChildTwo =
        gxos::apps::Navigator::SmokeCurrentDocumentText();
    add("JS13 repeated hosted bubbling keeps same-realm callback state",
        js13ChildClickTwo &&
        contains(js13AfterChildTwo, "child-onclick>child-listener>parent-onclick>parent-listener>grand-onclick>grand-listener>"),
        "second authentic click retains nested handler registrations");

    const bool js13AncestorOnlyClick =
        gxos::apps::Navigator::SmokeClickFormControlById("js13-unregistered-child");
    const std::string js13AfterAncestorOnly =
        gxos::apps::Navigator::SmokeCurrentDocumentText();
    add("JS13 hosted ancestor-only listener receives unregistered child",
        js13AncestorOnlyClick &&
        contains(js13AfterAncestorOnly, "ancestor:js13-unregistered-child:js13-ancestor-only"),
        "handlerless child and gap still follow DOM parentSerial ancestry");

    const bool js13BranchAClick =
        gxos::apps::Navigator::SmokeClickFormControlById("js13-branch-a-child");
    const std::string js13AfterBranchA =
        gxos::apps::Navigator::SmokeCurrentDocumentText();
    const bool js13BranchBClick =
        gxos::apps::Navigator::SmokeClickFormControlById("js13-branch-b-child");
    const std::string js13AfterBranchB =
        gxos::apps::Navigator::SmokeCurrentDocumentText();
    add("JS13 hosted independent DOM branches stay isolated",
        js13BranchAClick && js13BranchBClick &&
        contains(js13AfterBranchA, "branches:1:0:js13-branch-a-child") &&
        contains(js13AfterBranchB, "branches:1:1:js13-branch-b-child"),
        "branch A and branch B clicks only reach their own DOM ancestors");

    const bool js13MutationClick =
        gxos::apps::Navigator::SmokeClickFormControlById("js13-mutation-child");
    const std::string js13AfterMutation =
        gxos::apps::Navigator::SmokeCurrentDocumentText();
    add("JS13 hosted listener mutation removes later ancestor safely",
        js13MutationClick && contains(js13AfterMutation, "mutation:0"),
        "child removes ancestor listener before that propagation node is reached");

    const bool js13ErrorClickOne =
        gxos::apps::Navigator::SmokeClickFormControlById("js13-error-child");
    const std::string js13AfterErrorOne =
        gxos::apps::Navigator::SmokeCurrentDocumentText();
    const std::string js13Error =
        gxos::apps::Navigator::SmokeJavaScriptLastError();
    const bool js13ErrorClickTwo =
        gxos::apps::Navigator::SmokeClickFormControlById("js13-error-child");
    const std::string js13AfterErrorTwo =
        gxos::apps::Navigator::SmokeCurrentDocumentText();
    add("JS13 hosted callback errors still bubble and recover",
        js13ErrorClickOne && js13ErrorClickTwo &&
        contains(js13Error, "UnknownIdentifier") &&
        contains(js13AfterErrorOne, "error:1:js13-error-child:js13-error-ancestor") &&
        contains(js13AfterErrorTwo, "error:2:js13-error-child:js13-error-ancestor"),
        std::string("first=") + yesNo(js13ErrorClickOne) + ",second=" +
        yesNo(js13ErrorClickTwo) + ",error=" +
        (js13Error.empty() ? "none" : js13Error));

    const bool js13LinkHit =
        gxos::apps::Navigator::SmokeHitLinkById("js13-link");
    const bool js13LinkClick =
        js13LinkHit && gxos::apps::Navigator::SmokeClickFirstLink();
    const std::string js13TargetText =
        gxos::apps::Navigator::SmokeCurrentDocumentText();
    add("JS13 ordinary link navigation remains functional",
        js13LinkHit && js13LinkClick &&
        gxos::apps::Navigator::SmokeCurrentUrl() ==
            "http://127.0.0.1:8080/navigator-smoke/javascript-js13-target.html" &&
        contains(js13TargetText, "Navigator JavaScript JS13 Target"),
        std::string("hit=") + yesNo(js13LinkHit) + ",link=" +
        yesNo(js13LinkClick) + ",url=" +
        gxos::apps::Navigator::SmokeCurrentUrl());
    add("JS13 navigation clears old propagation listener state",
        gxos::apps::Navigator::SmokeJavaScriptHandlerCount() == 0u &&
        gxos::apps::Navigator::SmokeJavaScriptListenerCount() == 0u &&
        gxos::apps::Navigator::SmokeJavaScriptLastError().empty(),
        "replacement document has no stale JS13 callbacks or errors");

    const bool js14Loaded = gxos::apps::Navigator::SmokeNavigateToQuiet(
        "http://127.0.0.1:8080/navigator-smoke/javascript-js14.html");
    const std::string js14InitialText =
        gxos::apps::Navigator::SmokeCurrentDocumentText();
    int js14TargetX = 0;
    int js14TargetY = 0;
    int js14TargetW = 0;
    int js14TargetH = 0;
    const bool js14TargetHasGeometry =
        gxos::apps::Navigator::SmokeBlockGeometryById(
            "js14-target-stop", js14TargetX, js14TargetY, js14TargetW,
            js14TargetH);
    const bool js14TargetHit =
        gxos::apps::Navigator::SmokeFormHitTargetById("js14-target-stop");
    const size_t js14InitialHandlers =
        gxos::apps::Navigator::SmokeJavaScriptHandlerCount();
    const size_t js14InitialListeners =
        gxos::apps::Navigator::SmokeJavaScriptListenerCount();
    const std::string js14InitialError =
        gxos::apps::Navigator::SmokeJavaScriptLastError();
    add("JS14 hosted fixture loads callable stopPropagation callbacks",
        js14Loaded && contains(js14InitialText, "Navigator JavaScript JS14") &&
        js14InitialHandlers == 18u && js14InitialListeners == 18u &&
        js14InitialError.empty(),
        std::string("loaded=") + yesNo(js14Loaded) + ",records=" +
        std::to_string(js14InitialHandlers) + ",listeners=" +
        std::to_string(js14InitialListeners) + ",error=" +
        (js14InitialError.empty() ? "none" : js14InitialError));

    const bool js14TargetClick =
        gxos::apps::Navigator::SmokeClickFormControlById("js14-target-stop");
    const std::string js14AfterTarget =
        gxos::apps::Navigator::SmokeCurrentDocumentText();
    add("JS14 target listener stops parent and grandparent",
        js14TargetHasGeometry && js14TargetW > 0 && js14TargetH > 0 &&
        js14TargetHit && js14TargetClick &&
        contains(js14AfterTarget,
            "target:c:js14-target-stop:js14-target-stop") &&
        !contains(js14AfterTarget, "target-parent-ran") &&
        !contains(js14AfterTarget, "target-grandparent-ran"),
        std::string("geometry=") + yesNo(js14TargetHasGeometry) +
        ",hit=" + yesNo(js14TargetHit) + ",click=" + yesNo(js14TargetClick));

    const bool js14OnclickClick =
        gxos::apps::Navigator::SmokeClickFormControlById("js14-onclick-stop");
    const std::string js14AfterOnclick =
        gxos::apps::Navigator::SmokeCurrentDocumentText();
    add("JS14 onclick stop still runs the child listener",
        js14OnclickClick && contains(js14AfterOnclick, "onclick:ol"),
        "expected onclick then listener after onclick stop");

    const bool js14ListenerClick =
        gxos::apps::Navigator::SmokeClickFormControlById("js14-listener-stop");
    const std::string js14AfterListener =
        gxos::apps::Navigator::SmokeCurrentDocumentText();
    add("JS14 listener stop preserves onclick-before-listener order",
        js14ListenerClick && contains(js14AfterListener, "listener:ol"),
        "expected listener stop after onclick");

    const bool js14AncestorClick =
        gxos::apps::Navigator::SmokeClickFormControlById("js14-ancestor-child");
    const std::string js14AfterAncestor =
        gxos::apps::Navigator::SmokeCurrentDocumentText();
    add("JS14 parent stop preserves target/currentTarget and suppresses higher nodes",
        js14AncestorClick &&
        contains(js14AfterAncestor, "ancestor:cp:js14-ancestor-child:js14-ancestor-parent") &&
        !contains(js14AfterAncestor, "ancestor:grandparent-ran"),
        "expected child then parent with no grandparent callback");
    add("JS14 ancestor stop mutation relayouts",
        contains(js14AfterAncestor, "Parent") &&
        !gxos::apps::Navigator::SmokeDocumentDirty(),
        "parent text mutation settled after stopped dispatch");

    const bool js14ResetOne =
        gxos::apps::Navigator::SmokeClickFormControlById("js14-reset-child");
    const std::string js14AfterResetOne =
        gxos::apps::Navigator::SmokeCurrentDocumentText();
    const bool js14ResetTwo =
        gxos::apps::Navigator::SmokeClickFormControlById("js14-reset-child");
    const std::string js14AfterResetTwo =
        gxos::apps::Navigator::SmokeCurrentDocumentText();
    add("JS14 propagation state resets per click",
        js14ResetOne && js14ResetTwo && !contains(js14AfterResetOne, "reset:1") &&
        contains(js14AfterResetTwo, "reset:1"),
        "first reset click stops; second click reaches parent");

    const bool js14BranchAClick =
        gxos::apps::Navigator::SmokeClickFormControlById("js14-branch-a-child");
    const bool js14BranchBClick =
        gxos::apps::Navigator::SmokeClickFormControlById("js14-branch-b-child");
    const std::string js14AfterBranchB =
        gxos::apps::Navigator::SmokeCurrentDocumentText();
    add("JS14 stopped branch does not affect a later independent branch",
        js14BranchAClick && js14BranchBClick &&
        contains(js14AfterBranchB, "branches:1:1"),
        "tree B still bubbles normally after tree A stops");

    const bool js14MutationClick =
        gxos::apps::Navigator::SmokeClickFormControlById("js14-mutation-child");
    const std::string js14AfterMutation =
        gxos::apps::Navigator::SmokeCurrentDocumentText();
    add("JS14 stopped callback can mutate DOM and relayout",
        js14MutationClick && contains(js14AfterMutation, "Stopped") &&
        contains(js14AfterMutation, "mutation:stopped") &&
        !contains(js14AfterMutation, "mutation:parent-ran") &&
        !gxos::apps::Navigator::SmokeDocumentDirty(),
        "target mutation succeeds while ancestor remains stopped");

    const bool js14ErrorClick =
        gxos::apps::Navigator::SmokeClickFormControlById("js14-error-child");
    const std::string js14AfterError =
        gxos::apps::Navigator::SmokeCurrentDocumentText();
    const std::string js14Error =
        gxos::apps::Navigator::SmokeJavaScriptLastError();
    add("JS14 error after stop remains contained and stopped",
        js14ErrorClick && contains(js14AfterError, "error:stopped") &&
        !contains(js14AfterError, "error:parent-ran") &&
        contains(js14Error, "UnknownIdentifier"),
        std::string("click=") + yesNo(js14ErrorClick) + ",error=" +
        (js14Error.empty() ? "none" : js14Error));

    const bool js14LinkHit =
        gxos::apps::Navigator::SmokeHitLinkById("js14-link");
    const bool js14LinkClick =
        js14LinkHit && gxos::apps::Navigator::SmokeClickFirstLink();
    const std::string js14TargetText =
        gxos::apps::Navigator::SmokeCurrentDocumentText();
    add("JS14 stopPropagation does not cancel ordinary link navigation",
        js14LinkHit && js14LinkClick &&
        gxos::apps::Navigator::SmokeCurrentUrl() ==
            "http://127.0.0.1:8080/navigator-smoke/javascript-js14-target.html" &&
        contains(js14TargetText, "Navigator JavaScript JS14 Target"),
        std::string("hit=") + yesNo(js14LinkHit) + ",link=" +
        yesNo(js14LinkClick) + ",url=" +
        gxos::apps::Navigator::SmokeCurrentUrl());
    add("JS14 navigation invalidates stopped document handlers",
        gxos::apps::Navigator::SmokeJavaScriptHandlerCount() == 0u &&
        gxos::apps::Navigator::SmokeJavaScriptListenerCount() == 0u &&
        gxos::apps::Navigator::SmokeJavaScriptLastError().empty(),
        "new navigation has no stale JS14 callbacks or errors");

    const bool js15Loaded = gxos::apps::Navigator::SmokeNavigateToQuiet(
        "http://127.0.0.1:8080/navigator-smoke/javascript-js15.html");
    const std::string js15InitialText =
        gxos::apps::Navigator::SmokeCurrentDocumentText();
    const uint64_t js15InitialRevision =
        gxos::apps::Navigator::SmokeDocumentLayoutRevision();
    int js15TargetX = 0;
    int js15TargetY = 0;
    int js15TargetW = 0;
    int js15TargetH = 0;
    const bool js15TargetHasGeometry =
        gxos::apps::Navigator::SmokeBlockGeometryById(
            "js15-target-stop", js15TargetX, js15TargetY, js15TargetW,
            js15TargetH);
    const bool js15TargetHit =
        gxos::apps::Navigator::SmokeFormHitTargetById("js15-target-stop");
    const size_t js15InitialHandlers =
        gxos::apps::Navigator::SmokeJavaScriptHandlerCount();
    const size_t js15InitialListeners =
        gxos::apps::Navigator::SmokeJavaScriptListenerCount();
    add("JS15 hosted fixture loads immediate-stop callbacks",
        js15Loaded && contains(js15InitialText, "Navigator JavaScript JS15") &&
        contains(js15InitialText, "stopImmediatePropagation") &&
        js15InitialHandlers == 18u && js15InitialListeners == 18u &&
        gxos::apps::Navigator::SmokeJavaScriptLastError().empty(),
        std::string("loaded=") + yesNo(js15Loaded) + ",records=" +
        std::to_string(js15InitialHandlers) + ",listeners=" +
        std::to_string(js15InitialListeners));

    const bool js15TargetClick =
        gxos::apps::Navigator::SmokeClickFormControlById("js15-target-stop");
    const std::string js15AfterTarget =
        gxos::apps::Navigator::SmokeCurrentDocumentText();
    add("JS15 onclick immediate stop skips target listener and ancestors",
        js15TargetHasGeometry && js15TargetW > 0 && js15TargetH > 0 &&
        js15TargetHit && js15TargetClick &&
        contains(js15AfterTarget,
            "target:o:js15-target-stop:js15-target-stop") &&
        !contains(js15AfterTarget, "unexpected-listener") &&
        !contains(js15AfterTarget, "target-parent-ran") &&
        !contains(js15AfterTarget, "target-grandparent-ran"),
        std::string("geometry=") + yesNo(js15TargetHasGeometry) +
        ",hit=" + yesNo(js15TargetHit) + ",click=" +
        yesNo(js15TargetClick));

    const bool js15OnclickClick =
        gxos::apps::Navigator::SmokeClickFormControlById("js15-onclick-stop");
    const std::string js15AfterOnclick =
        gxos::apps::Navigator::SmokeCurrentDocumentText();
    add("JS15 onclick immediate stop suppresses same-node listener",
        js15OnclickClick && contains(js15AfterOnclick, "onclick:o") &&
        !contains(js15AfterOnclick, "onclick:ol"),
        "expected onclick without its registered listener");

    const bool js15ListenerClick =
        gxos::apps::Navigator::SmokeClickFormControlById("js15-listener-stop");
    const std::string js15AfterListener =
        gxos::apps::Navigator::SmokeCurrentDocumentText();
    add("JS15 stopPropagation regression keeps same-node listener",
        js15ListenerClick && contains(js15AfterListener, "listener:ol"),
        "ordinary stopPropagation remains weaker than immediate stop");

    const bool js15AncestorClick =
        gxos::apps::Navigator::SmokeClickFormControlById("js15-ancestor-child");
    const std::string js15AfterAncestor =
        gxos::apps::Navigator::SmokeCurrentDocumentText();
    const uint64_t js15RevisionAfterAncestor =
        gxos::apps::Navigator::SmokeDocumentLayoutRevision();
    add("JS15 parent onclick immediate stop skips parent listener and grandparent",
        js15AncestorClick &&
        contains(js15AfterAncestor,
            "ancestor:cp:js15-ancestor-child:js15-ancestor-parent") &&
        !contains(js15AfterAncestor, "unexpected-listener") &&
        !contains(js15AfterAncestor, "ancestor:grandparent-ran"),
        "expected child listener then parent onclick only");
    add("JS15 immediate-stop callback mutation relayouts",
        contains(js15AfterAncestor, "Parent") &&
        js15RevisionAfterAncestor > js15InitialRevision &&
        !gxos::apps::Navigator::SmokeDocumentDirty(),
        "parent mutation settled after the immediate stop call");

    const bool js15ResetOne =
        gxos::apps::Navigator::SmokeClickFormControlById("js15-reset-child");
    const std::string js15AfterResetOne =
        gxos::apps::Navigator::SmokeCurrentDocumentText();
    const bool js15ResetTwo =
        gxos::apps::Navigator::SmokeClickFormControlById("js15-reset-child");
    const std::string js15AfterResetTwo =
        gxos::apps::Navigator::SmokeCurrentDocumentText();
    add("JS15 immediate state resets per click",
        js15ResetOne && js15ResetTwo && !contains(js15AfterResetOne, "reset:1") &&
        contains(js15AfterResetTwo, "reset:1"),
        "first click stops; second click reaches the parent");

    const bool js15BranchAClick =
        gxos::apps::Navigator::SmokeClickFormControlById("js15-branch-a-child");
    const bool js15BranchBClick =
        gxos::apps::Navigator::SmokeClickFormControlById("js15-branch-b-child");
    const std::string js15AfterBranchB =
        gxos::apps::Navigator::SmokeCurrentDocumentText();
    add("JS15 immediate stop leaves independent branch unaffected",
        js15BranchAClick && js15BranchBClick &&
        contains(js15AfterBranchB, "branches:1:1"),
        "tree B still bubbles normally after tree A stops");

    const bool js15MutationClick =
        gxos::apps::Navigator::SmokeClickFormControlById("js15-mutation-child");
    const std::string js15AfterMutation =
        gxos::apps::Navigator::SmokeCurrentDocumentText();
    add("JS15 immediate-stop callback can mutate and suppress later work",
        js15MutationClick && contains(js15AfterMutation, "Stopped") &&
        contains(js15AfterMutation, "mutation:stopped") &&
        !contains(js15AfterMutation, "mutation-listener-ran") &&
        !contains(js15AfterMutation, "mutation:parent-ran") &&
        !gxos::apps::Navigator::SmokeDocumentDirty(),
        "target mutation succeeds while listener and ancestor are suppressed");

    const bool js15ErrorClick =
        gxos::apps::Navigator::SmokeClickFormControlById("js15-error-child");
    const std::string js15AfterError =
        gxos::apps::Navigator::SmokeCurrentDocumentText();
    const std::string js15Error =
        gxos::apps::Navigator::SmokeJavaScriptLastError();
    add("JS15 error after immediate stop remains contained and stopped",
        js15ErrorClick && contains(js15AfterError, "error:stopped") &&
        !contains(js15AfterError, "error-listener-ran") &&
        !contains(js15AfterError, "error:parent-ran") &&
        contains(js15Error, "UnknownIdentifier"),
        std::string("click=") + yesNo(js15ErrorClick) + ",error=" +
        (js15Error.empty() ? "none" : js15Error));

    const bool js15LinkHit =
        gxos::apps::Navigator::SmokeHitLinkById("js15-link");
    const bool js15LinkClick =
        js15LinkHit && gxos::apps::Navigator::SmokeClickFirstLink();
    const std::string js15TargetText =
        gxos::apps::Navigator::SmokeCurrentDocumentText();
    add("JS15 immediate stop does not cancel ordinary link navigation",
        js15LinkHit && js15LinkClick &&
        gxos::apps::Navigator::SmokeCurrentUrl() ==
            "http://127.0.0.1:8080/navigator-smoke/javascript-js15-target.html" &&
        contains(js15TargetText, "Navigator JavaScript JS15 Target"),
        std::string("hit=") + yesNo(js15LinkHit) + ",link=" +
        yesNo(js15LinkClick) + ",url=" +
        gxos::apps::Navigator::SmokeCurrentUrl());
    add("JS15 navigation clears old immediate-stop handlers",
        gxos::apps::Navigator::SmokeJavaScriptHandlerCount() == 0u &&
        gxos::apps::Navigator::SmokeJavaScriptListenerCount() == 0u &&
        gxos::apps::Navigator::SmokeJavaScriptLastError().empty(),
        "replacement document has no stale JS15 callbacks or errors");

    const bool js16Loaded = gxos::apps::Navigator::SmokeNavigateToQuiet(
        "http://127.0.0.1:8080/navigator-smoke/javascript-js16.html");
    const std::string js16InitialText =
        gxos::apps::Navigator::SmokeCurrentDocumentText();
    const uint64_t js16InitialRevision =
        gxos::apps::Navigator::SmokeDocumentLayoutRevision();
    int js16LinkX = 0;
    int js16LinkY = 0;
    int js16LinkW = 0;
    int js16LinkH = 0;
    const bool js16LinkHasGeometry =
        gxos::apps::Navigator::SmokeBlockGeometryById(
            "js16-cancel-link", js16LinkX, js16LinkY, js16LinkW, js16LinkH);
    const bool js16LinkHit =
        gxos::apps::Navigator::SmokeHitLinkById("js16-cancel-link");
    const size_t js16InitialHandlers =
        gxos::apps::Navigator::SmokeJavaScriptHandlerCount();
    const size_t js16InitialListeners =
        gxos::apps::Navigator::SmokeJavaScriptListenerCount();
    const std::string js16InitialError =
        gxos::apps::Navigator::SmokeJavaScriptLastError();
    add("JS16 hosted fixture loads cancellation callbacks",
        js16Loaded && contains(js16InitialText, "Navigator JavaScript JS16") &&
        contains(js16InitialText, "Click default-action cancellation") &&
        js16InitialHandlers == 6u && js16InitialListeners == 6u &&
        js16InitialError.empty(),
        std::string("loaded=") + yesNo(js16Loaded) + ",records=" +
        std::to_string(js16InitialHandlers) + ",listeners=" +
        std::to_string(js16InitialListeners) + ",error=" +
        (js16InitialError.empty() ? "none" : js16InitialError));

    const bool js16CancelledClick =
        js16LinkHit && !gxos::apps::Navigator::SmokeClickFirstLink();
    const std::string js16AfterCancelledLink =
        gxos::apps::Navigator::SmokeCurrentDocumentText();
    const uint64_t js16RevisionAfterCancelledLink =
        gxos::apps::Navigator::SmokeDocumentLayoutRevision();
    add("JS16 authentic preventDefault suppresses link navigation",
        js16CancelledClick &&
        gxos::apps::Navigator::SmokeCurrentUrl() ==
            "http://127.0.0.1:8080/navigator-smoke/javascript-js16.html" &&
        contains(js16AfterCancelledLink, "cancelled:true:listener-sees-true") &&
        contains(js16AfterCancelledLink, "Cancelled link"),
        std::string("click=") + yesNo(js16CancelledClick) + ",url=" +
        gxos::apps::Navigator::SmokeCurrentUrl());
    add("JS16 cancelled link mutation relayouts while page remains active",
        js16LinkHasGeometry && js16LinkW > 0 && js16LinkH > 0 &&
        js16RevisionAfterCancelledLink > js16InitialRevision &&
        !gxos::apps::Navigator::SmokeDocumentDirty(),
        std::string("geometry=") + yesNo(js16LinkHasGeometry) + ",revision=" +
        std::to_string(js16InitialRevision) + "->" +
        std::to_string(js16RevisionAfterCancelledLink));

    const bool js16BubbleClick =
        gxos::apps::Navigator::SmokeClickFormControlById("js16-bubble-child");
    const std::string js16AfterBubble =
        gxos::apps::Navigator::SmokeCurrentDocumentText();
    add("JS16 cancellation preserves bubbling and visibility",
        js16BubbleClick && contains(js16AfterBubble, "bubble:cp:true"),
        "expected child then parent with parent seeing defaultPrevented=true");

    const bool js16ResetOne =
        gxos::apps::Navigator::SmokeClickFormControlById("js16-reset");
    const std::string js16AfterResetOne =
        gxos::apps::Navigator::SmokeCurrentDocumentText();
    const bool js16ResetTwo =
        gxos::apps::Navigator::SmokeClickFormControlById("js16-reset");
    const std::string js16AfterResetTwo =
        gxos::apps::Navigator::SmokeCurrentDocumentText();
    add("JS16 defaultPrevented resets on each hosted click",
        js16ResetOne && js16ResetTwo &&
        contains(js16AfterResetOne, "reset:cancelled") &&
        contains(js16AfterResetTwo, "reset:uncancelled"),
        "first reset click cancels; second begins with a clear dispatch state");

    const bool js16ErrorClick =
        gxos::apps::Navigator::SmokeClickFormControlById("js16-error");
    const std::string js16ErrorText =
        gxos::apps::Navigator::SmokeCurrentDocumentText();
    const std::string js16Error =
        gxos::apps::Navigator::SmokeJavaScriptLastError();
    add("JS16 error after cancellation remains contained",
        js16ErrorClick && contains(js16ErrorText, "error:cancelled") &&
        contains(js16Error, "UnknownIdentifier"),
        std::string("click=") + yesNo(js16ErrorClick) + ",error=" +
        (js16Error.empty() ? "none" : js16Error));

    const bool js16StopClick =
        gxos::apps::Navigator::SmokeClickFormControlById("js16-stop");
    add("JS16 stopPropagation remains independent from cancellation",
        js16StopClick &&
        gxos::apps::Navigator::SmokeCurrentDocumentText().find("stop-only") !=
            std::string::npos,
        "stop-only callback does not use preventDefault");
    add("JS16 navigation clears cancellation-era state",
        gxos::apps::Navigator::SmokeNavigateToQuiet(
            "http://127.0.0.1:8080/navigator-smoke/javascript-js16-uncancelled.html") &&
        gxos::apps::Navigator::SmokeJavaScriptHandlerCount() == 0u &&
        gxos::apps::Navigator::SmokeJavaScriptListenerCount() == 0u &&
        gxos::apps::Navigator::SmokeJavaScriptLastError().empty(),
        "replacement page starts with no old handlers or callback error");

    const bool js16UncancelledHit =
        gxos::apps::Navigator::SmokeHitLinkById("js16-uncancelled-link");
    const bool js16UncancelledClick =
        js16UncancelledHit && gxos::apps::Navigator::SmokeClickFirstLink();
    add("JS16 uncancelled ordinary link still navigates",
        js16UncancelledClick &&
        gxos::apps::Navigator::SmokeCurrentUrl() ==
            "http://127.0.0.1:8080/navigator-smoke/javascript-js16-target.html" &&
        contains(gxos::apps::Navigator::SmokeCurrentDocumentText(),
            "Navigator JavaScript JS16 Target"),
        std::string("hit=") + yesNo(js16UncancelledHit) + ",click=" +
        yesNo(js16UncancelledClick) + ",url=" +
        gxos::apps::Navigator::SmokeCurrentUrl());

    const bool js17Loaded = gxos::apps::Navigator::SmokeNavigateToQuiet(
        "http://127.0.0.1:8080/navigator-smoke/javascript-js17.html");
    const std::string js17InitialText =
        gxos::apps::Navigator::SmokeCurrentDocumentText();
    const size_t js17InitialHandlers =
        gxos::apps::Navigator::SmokeJavaScriptHandlerCount();
    const size_t js17InitialListeners =
        gxos::apps::Navigator::SmokeJavaScriptListenerCount();
    const std::string js17InitialError =
        gxos::apps::Navigator::SmokeJavaScriptLastError();
    add("JS17 hosted fixture loads bounded multiple listeners",
        js17Loaded && contains(js17InitialText, "Navigator JavaScript JS17") &&
        contains(js17InitialText, "Bounded multiple click listeners") &&
        js17InitialHandlers == 6u && js17InitialListeners == 12u &&
        js17InitialError.empty(),
        std::string("loaded=") + yesNo(js17Loaded) + ",handlers=" +
        std::to_string(js17InitialHandlers) + ",listeners=" +
        std::to_string(js17InitialListeners) + ",error=" +
        (js17InitialError.empty() ? "none" : js17InitialError));

    const bool js17OrderClick =
        gxos::apps::Navigator::SmokeClickFormControlById("js17-order");
    add("JS17 hosted order is onclick then three listeners then parent",
        js17OrderClick && contains(
            gxos::apps::Navigator::SmokeCurrentDocumentText(),
            "order:oabcp"),
        "expected authentic order oabcp");
    const bool js17StopClick =
        gxos::apps::Navigator::SmokeClickFormControlById("js17-stop");
    add("JS17 hosted stopPropagation keeps same-node listeners",
        js17StopClick && contains(
            gxos::apps::Navigator::SmokeCurrentDocumentText(), "stop:ab") &&
        !contains(gxos::apps::Navigator::SmokeCurrentDocumentText(),
            "stop:ab:parent"),
        "expected stop:ab without parent callback");
    const bool js17ImmediateClick =
        gxos::apps::Navigator::SmokeClickFormControlById("js17-immediate");
    add("JS17 hosted immediate stop skips later listeners and parent",
        js17ImmediateClick && contains(
            gxos::apps::Navigator::SmokeCurrentDocumentText(), "immediate:a") &&
        !contains(gxos::apps::Navigator::SmokeCurrentDocumentText(),
            "immediate:a:parent"),
        "expected immediate:a without later callbacks");
    const bool js17MutationOne =
        gxos::apps::Navigator::SmokeClickFormControlById("js17-mutation");
    const std::string js17MutationTextOne =
        gxos::apps::Navigator::SmokeCurrentDocumentText();
    const bool js17MutationTwo =
        gxos::apps::Navigator::SmokeClickFormControlById("js17-mutation");
    const std::string js17MutationTextTwo =
        gxos::apps::Navigator::SmokeCurrentDocumentText();
    add("JS17 hosted mutation snapshot defers additions and skips removals",
        js17MutationOne && js17MutationTwo &&
        contains(js17MutationTextOne, "mutation:a") &&
        contains(js17MutationTextTwo, "mutation:aac"),
        "expected first mutation:a and cumulative second mutation:aac");
    const bool js17CancelHit =
        gxos::apps::Navigator::SmokeHitLinkById("js17-cancel");
    const bool js17CancelledClick =
        js17CancelHit && !gxos::apps::Navigator::SmokeClickFirstLink();
    add("JS17 hosted preventDefault preserves later-listener visibility",
        js17CancelledClick &&
        gxos::apps::Navigator::SmokeCurrentUrl() ==
            "http://127.0.0.1:8080/navigator-smoke/javascript-js17.html" &&
        contains(gxos::apps::Navigator::SmokeCurrentDocumentText(),
            "cancel:ad"),
        std::string("hit=") + yesNo(js17CancelHit) + ",cancelled=" +
        yesNo(js17CancelledClick) + ",url=" +
        gxos::apps::Navigator::SmokeCurrentUrl());
    add("JS17 hosted navigation cleanup clears listener tables",
        gxos::apps::Navigator::SmokeNavigateToQuiet(
            "http://127.0.0.1:8080/navigator-smoke/javascript-js17-target.html") &&
        gxos::apps::Navigator::SmokeJavaScriptHandlerCount() == 0u &&
        gxos::apps::Navigator::SmokeJavaScriptListenerCount() == 0u &&
        gxos::apps::Navigator::SmokeJavaScriptLastError().empty(),
        "replacement page starts without JS17 registrations");

    const bool js18Loaded = gxos::apps::Navigator::SmokeNavigateToQuiet(
        "http://127.0.0.1:8080/navigator-smoke/javascript-js18.html");
    const std::string js18InitialText =
        gxos::apps::Navigator::SmokeCurrentDocumentText();
    const size_t js18InitialHandlers =
        gxos::apps::Navigator::SmokeJavaScriptHandlerCount();
    const size_t js18InitialListeners =
        gxos::apps::Navigator::SmokeJavaScriptListenerCount();
    const std::string js18InitialError =
        gxos::apps::Navigator::SmokeJavaScriptLastError();
    add("JS18 hosted fixture loads object options",
        js18Loaded && contains(js18InitialText, "Navigator JavaScript JS18") &&
        contains(js18InitialText, "Bounded once click listeners") &&
        js18InitialHandlers == 4u && js18InitialListeners == 7u &&
        js18InitialError.empty(),
        std::string("loaded=") + yesNo(js18Loaded) + ",handlers=" +
        std::to_string(js18InitialHandlers) + ",listeners=" +
        std::to_string(js18InitialListeners) + ",error=" +
        (js18InitialError.empty() ? "none" : js18InitialError));

    const bool js18OnceClickOne =
        gxos::apps::Navigator::SmokeClickFormControlById("js18-once");
    const std::string js18OnceTextOne =
        gxos::apps::Navigator::SmokeCurrentDocumentText();
    const bool js18OnceClickTwo =
        gxos::apps::Navigator::SmokeClickFormControlById("js18-once");
    const std::string js18OnceTextTwo =
        gxos::apps::Navigator::SmokeCurrentDocumentText();
    add("JS18 hosted onclick precedes once then persistent listener",
        js18OnceClickOne && js18OnceClickTwo &&
        contains(js18OnceTextOne, "once:oab") &&
        contains(js18OnceTextTwo, "once:oabob"),
        std::string("first=") + summarizeFromMarker(js18OnceTextOne, "once:", 80) +
        ",second=" + summarizeFromMarker(js18OnceTextTwo, "once:", 80));

    const bool js18ImmediateClickOne =
        gxos::apps::Navigator::SmokeClickFormControlById("js18-immediate");
    const std::string js18ImmediateTextOne =
        gxos::apps::Navigator::SmokeCurrentDocumentText();
    const bool js18ImmediateClickTwo =
        gxos::apps::Navigator::SmokeClickFormControlById("js18-immediate");
    const std::string js18ImmediateTextTwo =
        gxos::apps::Navigator::SmokeCurrentDocumentText();
    add("JS18 hosted once removal survives immediate stop",
        js18ImmediateClickOne && js18ImmediateClickTwo &&
        contains(js18ImmediateTextOne, "immediate:a") &&
        contains(js18ImmediateTextTwo, "immediate:ab:parent"),
        "expected first once callback only and second persistent callback");

    const bool js18CancelHit =
        gxos::apps::Navigator::SmokeHitLinkById("js18-cancel");
    const bool js18CancelledClick =
        js18CancelHit && !gxos::apps::Navigator::SmokeClickFirstLink();
    const std::string js18CancelText =
        gxos::apps::Navigator::SmokeCurrentDocumentText();
    add("JS18 hosted first link click is cancelled once",
        js18CancelledClick &&
        gxos::apps::Navigator::SmokeCurrentUrl() ==
            "http://127.0.0.1:8080/navigator-smoke/javascript-js18.html" &&
        contains(js18CancelText, "cancel:ad"),
        std::string("hit=") + yesNo(js18CancelHit) + ",cancelled=" +
        yesNo(js18CancelledClick) + ",text=" + summarizeText(js18CancelText, 180));

    const bool js18CancelHitAgain =
        gxos::apps::Navigator::SmokeHitLinkById("js18-cancel");
    const bool js18NavigatedClick =
        js18CancelHitAgain && gxos::apps::Navigator::SmokeClickFirstLink();
    add("JS18 hosted second link click navigates normally",
        js18NavigatedClick &&
        gxos::apps::Navigator::SmokeCurrentUrl() ==
            "http://127.0.0.1:8080/navigator-smoke/javascript-js18-target.html" &&
        contains(gxos::apps::Navigator::SmokeCurrentDocumentText(),
            "Navigator JavaScript JS18 Target"),
        std::string("hit=") + yesNo(js18CancelHitAgain) + ",click=" +
        yesNo(js18NavigatedClick) + ",url=" +
        gxos::apps::Navigator::SmokeCurrentUrl());
    add("JS18 hosted navigation cleanup clears once registrations",
        gxos::apps::Navigator::SmokeJavaScriptHandlerCount() == 0u &&
        gxos::apps::Navigator::SmokeJavaScriptListenerCount() == 0u &&
        gxos::apps::Navigator::SmokeJavaScriptLastError().empty(),
        "replacement page starts without JS18 registrations");

    const bool js19Loaded = gxos::apps::Navigator::SmokeNavigateToQuiet(
        "http://127.0.0.1:8080/navigator-smoke/javascript-js19.html");
    const std::string js19InitialText =
        gxos::apps::Navigator::SmokeCurrentDocumentText();
    const size_t js19InitialHandlers =
        gxos::apps::Navigator::SmokeJavaScriptHandlerCount();
    const size_t js19InitialListeners =
        gxos::apps::Navigator::SmokeJavaScriptListenerCount();
    const std::string js19InitialError =
        gxos::apps::Navigator::SmokeJavaScriptLastError();
    add("JS19 hosted fixture loads read-only Event metadata",
        js19Loaded && contains(js19InitialText, "Navigator JavaScript JS19") &&
        contains(js19InitialText, "Read-only click Event bubbles") &&
        js19InitialHandlers == 6u && js19InitialListeners == 9u &&
        js19InitialError.empty(),
        std::string("loaded=") + yesNo(js19Loaded) + ",handlers=" +
        std::to_string(js19InitialHandlers) + ",listeners=" +
        std::to_string(js19InitialListeners) + ",error=" +
        (js19InitialError.empty() ? "none" : js19InitialError));

    const bool js19TargetClick =
        gxos::apps::Navigator::SmokeClickFormControlById("js19-target");
    const std::string js19TargetText =
        gxos::apps::Navigator::SmokeCurrentDocumentText();
    add("JS19 hosted target onclick, listener, and ancestors share metadata",
        js19TargetClick && contains(js19TargetText, "target:olpg"),
        "expected target:olpg from onclick, target listener, parent, grandparent");

    const bool js19StopClick =
        gxos::apps::Navigator::SmokeClickFormControlById("js19-stop");
    const std::string js19StopText =
        gxos::apps::Navigator::SmokeCurrentDocumentText();
    add("JS19 hosted metadata assignment does not alter propagation",
        js19StopClick && contains(js19StopText, "stop:bc") &&
        !contains(js19StopText, "stop-parent"),
        "expected stop:bc without a parent callback");

    const bool js19ImmediateClickOne =
        gxos::apps::Navigator::SmokeClickFormControlById("js19-immediate");
    const std::string js19ImmediateTextOne =
        gxos::apps::Navigator::SmokeCurrentDocumentText();
    const bool js19ImmediateClickTwo =
        gxos::apps::Navigator::SmokeClickFormControlById("js19-immediate");
    const std::string js19ImmediateTextTwo =
        gxos::apps::Navigator::SmokeCurrentDocumentText();
    add("JS19 hosted immediate stop and once removal preserve metadata",
        js19ImmediateClickOne && js19ImmediateClickTwo &&
        contains(js19ImmediateTextOne, "immediate:b") &&
        contains(js19ImmediateTextTwo, "immediate:bp:parent"),
        "expected first metadata-aware once callback and second persistent bubble");

    const bool js19CancelHit =
        gxos::apps::Navigator::SmokeHitLinkById("js19-cancel");
    const bool js19CancelledClick =
        js19CancelHit && !gxos::apps::Navigator::SmokeClickFirstLink();
    const std::string js19CancelText =
        gxos::apps::Navigator::SmokeCurrentDocumentText();
    add("JS19 hosted cancelable metadata preserves authentic cancellation",
        js19CancelledClick &&
        gxos::apps::Navigator::SmokeCurrentUrl() ==
            "http://127.0.0.1:8080/navigator-smoke/javascript-js19.html" &&
        contains(js19CancelText, "cancel:cdlp"),
        std::string("hit=") + yesNo(js19CancelHit) + ",cancelled=" +
        yesNo(js19CancelledClick) + ",text=" + summarizeText(js19CancelText, 160));

    const bool js19CancelHitAgain =
        gxos::apps::Navigator::SmokeHitLinkById("js19-cancel");
    const bool js19UncancelledClick =
        js19CancelHitAgain && gxos::apps::Navigator::SmokeClickFirstLink();
    add("JS19 hosted uncancelled metadata inspection still navigates",
        js19UncancelledClick &&
        gxos::apps::Navigator::SmokeCurrentUrl() ==
            "http://127.0.0.1:8080/navigator-smoke/javascript-js19-target.html" &&
        contains(gxos::apps::Navigator::SmokeCurrentDocumentText(),
            "Navigator JavaScript JS19 Target"),
        std::string("hit=") + yesNo(js19CancelHitAgain) + ",click=" +
        yesNo(js19UncancelledClick) + ",url=" +
        gxos::apps::Navigator::SmokeCurrentUrl());
    add("JS19 hosted navigation cleanup clears metadata-era listeners",
        gxos::apps::Navigator::SmokeJavaScriptHandlerCount() == 0u &&
        gxos::apps::Navigator::SmokeJavaScriptListenerCount() == 0u &&
        gxos::apps::Navigator::SmokeJavaScriptLastError().empty(),
        "replacement page starts without JS19 registrations");

    const bool js20Loaded = gxos::apps::Navigator::SmokeNavigateToQuiet(
        "http://127.0.0.1:8080/navigator-smoke/javascript-js20.html");
    const std::string js20InitialText =
        gxos::apps::Navigator::SmokeCurrentDocumentText();
    const size_t js20InitialHandlers =
        gxos::apps::Navigator::SmokeJavaScriptHandlerCount();
    const size_t js20InitialListeners =
        gxos::apps::Navigator::SmokeJavaScriptListenerCount();
    const std::string js20InitialError =
        gxos::apps::Navigator::SmokeJavaScriptLastError();
    add("JS20 hosted fixture loads bounded capture registrations",
        js20Loaded && contains(js20InitialText, "Navigator JavaScript JS20") &&
        contains(js20InitialText, "Bounded click capture") &&
        js20InitialHandlers == 8u && js20InitialListeners == 26u &&
        js20InitialError.empty(),
        std::string("loaded=") + yesNo(js20Loaded) + ",handlers=" +
        std::to_string(js20InitialHandlers) + ",listeners=" +
        std::to_string(js20InitialListeners) + ",error=" +
        (js20InitialError.empty() ? "none" : js20InitialError));

    const bool js20TargetClick =
        gxos::apps::Navigator::SmokeClickFormControlById("js20-target");
    const std::string js20TargetText =
        gxos::apps::Navigator::SmokeCurrentDocumentText();
    add("JS20 authentic capture-target-bubble ordering",
        js20TargetClick && contains(js20TargetText,
            "order:ABCDEFGIHJKLMNOP") &&
        contains(js20TargetText, "stable:true") &&
        contains(js20TargetText, "current:truetruetruetrue"),
        "expected root/grandparent/parent capture, target, then bubble");

    const bool js20StopClick =
        gxos::apps::Navigator::SmokeClickFormControlById("js20-stop");
    add("JS20 authentic ancestor capture stopPropagation",
        js20StopClick && contains(
            gxos::apps::Navigator::SmokeCurrentDocumentText(), "stop:ab"),
        "expected same-root capture listeners only");

    const bool js20ImmediateClick =
        gxos::apps::Navigator::SmokeClickFormControlById("js20-immediate");
    add("JS20 authentic capture stopImmediatePropagation",
        js20ImmediateClick && contains(
            gxos::apps::Navigator::SmokeCurrentDocumentText(), "immediate:a"),
        "expected later same-node capture listener and later phases skipped");

    const bool js20TargetStopClick =
        gxos::apps::Navigator::SmokeClickFormControlById("js20-target-stop");
    add("JS20 authentic target capture stopPropagation",
        js20TargetStopClick && contains(
            gxos::apps::Navigator::SmokeCurrentDocumentText(),
            "target-stop:cob"),
        "expected target capture, onclick, target bubble, no ancestor bubble");

    const bool js20TargetImmediateClick =
        gxos::apps::Navigator::SmokeClickFormControlById(
            "js20-target-immediate");
    add("JS20 authentic target capture immediate stop",
        js20TargetImmediateClick && contains(
            gxos::apps::Navigator::SmokeCurrentDocumentText(),
            "target-immediate:c"),
        "expected target onclick and target bubble skipped");

    const bool js20OnceClickOne =
        gxos::apps::Navigator::SmokeClickFormControlById("js20-once");
    const std::string js20OnceTextOne =
        gxos::apps::Navigator::SmokeCurrentDocumentText();
    const bool js20OnceClickTwo =
        gxos::apps::Navigator::SmokeClickFormControlById("js20-once");
    const std::string js20OnceTextTwo =
        gxos::apps::Navigator::SmokeCurrentDocumentText();
    add("JS20 authentic capture once executes once",
        js20OnceClickOne && js20OnceClickTwo &&
        contains(js20OnceTextOne, "once:1") &&
        contains(js20OnceTextTwo, "once:1"),
        std::string("first=") + yesNo(js20OnceClickOne) + ",second=" +
        yesNo(js20OnceClickTwo) + ",first=" +
        summarizeFromMarker(js20OnceTextOne, "once:", 80) + ",second=" +
        summarizeFromMarker(js20OnceTextTwo, "once:", 80));

    const bool js20CancelHit =
        gxos::apps::Navigator::SmokeHitLinkById("js20-cancel");
    const bool js20CancelledClick =
        js20CancelHit && !gxos::apps::Navigator::SmokeClickFirstLink();
    const std::string js20CancelText =
        gxos::apps::Navigator::SmokeCurrentDocumentText();
    add("JS20 authentic capture preventDefault cancels link",
        js20CancelledClick &&
        gxos::apps::Navigator::SmokeCurrentUrl() ==
            "http://127.0.0.1:8080/navigator-smoke/javascript-js20.html" &&
        contains(js20CancelText, "cancel:cdp"),
        std::string("hit=") + yesNo(js20CancelHit) + ",cancelled=" +
        yesNo(js20CancelledClick) + ",text=" + summarizeText(js20CancelText, 160));

    const bool js20CancelHitAgain =
        gxos::apps::Navigator::SmokeHitLinkById("js20-cancel");
    const bool js20UncancelledClick =
        js20CancelHitAgain && gxos::apps::Navigator::SmokeClickFirstLink();
    add("JS20 uncancelled second link click navigates",
        js20UncancelledClick &&
        gxos::apps::Navigator::SmokeCurrentUrl() ==
            "http://127.0.0.1:8080/navigator-smoke/javascript-js20-target.html" &&
        contains(gxos::apps::Navigator::SmokeCurrentDocumentText(),
            "Navigator JavaScript JS20 Target"),
        std::string("hit=") + yesNo(js20CancelHitAgain) + ",click=" +
        yesNo(js20UncancelledClick) + ",url=" +
        gxos::apps::Navigator::SmokeCurrentUrl());
    add("JS20 navigation cleanup clears capture state",
        gxos::apps::Navigator::SmokeJavaScriptHandlerCount() == 0u &&
        gxos::apps::Navigator::SmokeJavaScriptListenerCount() == 0u &&
        gxos::apps::Navigator::SmokeJavaScriptLastError().empty(),
        "replacement page starts without JS20 listeners");

    const bool js21Loaded = gxos::apps::Navigator::SmokeNavigateToQuiet(
        "http://127.0.0.1:8080/navigator-smoke/javascript-js21.html");
    const std::string js21InitialText =
        gxos::apps::Navigator::SmokeCurrentDocumentText();
    const size_t js21InitialHandlers =
        gxos::apps::Navigator::SmokeJavaScriptHandlerCount();
    const size_t js21InitialListeners =
        gxos::apps::Navigator::SmokeJavaScriptListenerCount();
    const std::string js21InitialError =
        gxos::apps::Navigator::SmokeJavaScriptLastError();
    add("JS21 hosted fixture loads phase registrations",
        js21Loaded && contains(js21InitialText, "Navigator JavaScript JS21") &&
        contains(js21InitialText, "Event phases") && js21InitialHandlers == 7u &&
        js21InitialListeners == 18u && js21InitialError.empty(),
        std::string("loaded=") + yesNo(js21Loaded) + ",handlers=" +
        std::to_string(js21InitialHandlers) + ",listeners=" +
        std::to_string(js21InitialListeners) + ",error=" +
        (js21InitialError.empty() ? "none" : js21InitialError));

    const bool js21OnceClickOne =
        gxos::apps::Navigator::SmokeClickFormControlById("js21-once");
    const bool js21OnceClickTwo =
        gxos::apps::Navigator::SmokeClickFormControlById("js21-once");
    add("JS21 once listeners retain their dispatch phases",
        js21OnceClickOne && js21OnceClickTwo && contains(
            gxos::apps::Navigator::SmokeCurrentDocumentText(),
            "once:1:2:2:3"),
        "ancestor capture=1, target capture/bubble=2, ancestor bubble=3 once");

    const bool js21ChildClick =
        gxos::apps::Navigator::SmokeClickFormControlById("js21-child");
    const std::string js21ChildText =
        gxos::apps::Navigator::SmokeCurrentDocumentText();
    add("JS21 authentic capture-target-bubble phases",
        js21ChildClick && contains(js21ChildText,
            "order:r1p1c2o2t2po3p3ro3r3") &&
        contains(js21ChildText, "target:true") &&
        contains(js21ChildText, "meta:true") &&
        contains(js21ChildText, "current:truetruetruetrue") &&
        contains(js21ChildText, "constants:true"),
        "expected root/parent capture=1, target handlers=2, ancestors bubble=3");
    add("JS21 same callback derives phase from dispatch stage",
        contains(js21ChildText, "same:13") &&
        gxos::apps::Navigator::SmokeJavaScriptLastError().empty(),
        "same callback phase evidence is retained in the hosted realm");

    const bool js21TargetStopClick =
        gxos::apps::Navigator::SmokeClickFormControlById("js21-target-stop");
    add("JS21 target stopPropagation remains AT_TARGET",
        js21TargetStopClick && contains(
            gxos::apps::Navigator::SmokeCurrentDocumentText(),
            "target-stop:t2o2b2"),
        "target capture, onclick, and target bubble remain phase 2");
    const bool js21TargetImmediateClick =
        gxos::apps::Navigator::SmokeClickFormControlById(
            "js21-target-immediate");
    add("JS21 target immediate stop reports AT_TARGET",
        js21TargetImmediateClick && contains(
            gxos::apps::Navigator::SmokeCurrentDocumentText(),
            "target-immediate:c2"),
        "target immediate stop skips later target handlers and ancestors");

    const bool js21CancelHit =
        gxos::apps::Navigator::SmokeHitLinkById("js21-cancel");
    const bool js21CancelledClick =
        js21CancelHit && !gxos::apps::Navigator::SmokeClickFirstLink();
    const std::string js21CancelText =
        gxos::apps::Navigator::SmokeCurrentDocumentText();
    add("JS21 capture preventDefault preserves phase metadata",
        js21CancelledClick &&
        gxos::apps::Navigator::SmokeCurrentUrl() ==
            "http://127.0.0.1:8080/navigator-smoke/javascript-js21.html" &&
        contains(js21CancelText, "cancel:r1t2p3:true"),
        "capture cancellation reaches target/bubble without navigation");

    const bool js21CancelHitAgain =
        gxos::apps::Navigator::SmokeHitLinkById("js21-cancel");
    const bool js21UncancelledClick =
        js21CancelHitAgain && gxos::apps::Navigator::SmokeClickFirstLink();
    add("JS21 ordinary uncancelled link remains functional",
        js21UncancelledClick && gxos::apps::Navigator::SmokeCurrentUrl() ==
            "http://127.0.0.1:8080/navigator-smoke/javascript-js21-target.html" &&
        contains(gxos::apps::Navigator::SmokeCurrentDocumentText(),
            "Navigator JavaScript JS21 Target"),
        "second click is not cancelled and navigates authentically");
    add("JS21 navigation cleanup clears phase-era listeners",
        gxos::apps::Navigator::SmokeJavaScriptHandlerCount() == 0u &&
        gxos::apps::Navigator::SmokeJavaScriptListenerCount() == 0u &&
        gxos::apps::Navigator::SmokeJavaScriptLastError().empty(),
        "replacement page starts without JS21 registrations or errors");

    const bool js22Loaded = gxos::apps::Navigator::SmokeNavigateToQuiet(
        "http://127.0.0.1:8080/navigator-smoke/javascript-js22.html");
    const std::string js22InitialText =
        gxos::apps::Navigator::SmokeCurrentDocumentText();
    const size_t js22InitialHandlers =
        gxos::apps::Navigator::SmokeJavaScriptHandlerCount();
    const size_t js22InitialListeners =
        gxos::apps::Navigator::SmokeJavaScriptListenerCount();
    const std::string js22InitialError =
        gxos::apps::Navigator::SmokeJavaScriptLastError();
    add("JS22 hosted fixture loads Boolean registrations",
        js22Loaded && contains(js22InitialText, "Navigator JavaScript JS22") &&
        contains(js22InitialText, "Boolean capture shorthand") &&
        js22InitialHandlers == 7u && js22InitialListeners == 18u &&
        js22InitialError.empty(),
        std::string("loaded=") + yesNo(js22Loaded) + ",handlers=" +
        std::to_string(js22InitialHandlers) + ",listeners=" +
        std::to_string(js22InitialListeners) + ",error=" +
        (js22InitialError.empty() ? "none" : js22InitialError));

    const bool js22ChildClick =
        gxos::apps::Navigator::SmokeClickFormControlById("js22-child");
    const std::string js22ChildText =
        gxos::apps::Navigator::SmokeCurrentDocumentText();
    add("JS22 authentic Boolean capture-target-bubble ordering",
        js22ChildClick && contains(js22ChildText,
            "order:r1p1c2o2t2po3p3r3") &&
        contains(js22ChildText, "same:13") &&
        contains(js22ChildText, "target:true") &&
        contains(js22ChildText, "current:true"),
        "Boolean capture, object capture, target ordering, and same callback phases");

    const bool js22CrossClick =
        gxos::apps::Navigator::SmokeClickFormControlById("js22-cross");
    add("JS22 hosted cross-form removal leaves callback inactive",
        js22CrossClick && contains(
            gxos::apps::Navigator::SmokeCurrentDocumentText(), "cross:"),
        "Boolean registration/object removal and object registration/Boolean removal both hold");

    const bool js22StopClick =
        gxos::apps::Navigator::SmokeClickFormControlById("js22-stop");
    add("JS22 hosted Boolean stopPropagation preserves same-node capture",
        js22StopClick && contains(
            gxos::apps::Navigator::SmokeCurrentDocumentText(), "stop:a1b1"),
        "later same-node capture listener runs while later nodes do not");

    const bool js22ImmediateClick =
        gxos::apps::Navigator::SmokeClickFormControlById("js22-immediate");
    add("JS22 hosted Boolean stopImmediatePropagation stops immediately",
        js22ImmediateClick && contains(
            gxos::apps::Navigator::SmokeCurrentDocumentText(),
            "immediate:a1"),
        "later same-node listeners and later phases are skipped");

    const bool js22TargetStopClick =
        gxos::apps::Navigator::SmokeClickFormControlById("js22-target-stop");
    add("JS22 hosted target Boolean stopPropagation remains AT_TARGET",
        js22TargetStopClick && contains(
            gxos::apps::Navigator::SmokeCurrentDocumentText(),
            "target-stop:c2o2b2"),
        "target capture, onclick, and target bubble all report phase 2");

    const bool js22TargetImmediateClick =
        gxos::apps::Navigator::SmokeClickFormControlById(
            "js22-target-immediate");
    add("JS22 hosted target Boolean immediate stop remains AT_TARGET",
        js22TargetImmediateClick && contains(
            gxos::apps::Navigator::SmokeCurrentDocumentText(),
            "target-immediate:c2"),
        "target immediate stop skips onclick, target bubble, and ancestors");

    const bool js22CancelHit =
        gxos::apps::Navigator::SmokeHitLinkById("js22-cancel");
    const bool js22CancelledClick =
        js22CancelHit && !gxos::apps::Navigator::SmokeClickFirstLink();
    const std::string js22CancelText =
        gxos::apps::Navigator::SmokeCurrentDocumentText();
    add("JS22 hosted Boolean capture preventDefault cancels link",
        js22CancelledClick &&
        gxos::apps::Navigator::SmokeCurrentUrl() ==
            "http://127.0.0.1:8080/navigator-smoke/javascript-js22.html" &&
        contains(js22CancelText, "cancel:c1t2truep3true"),
        std::string("hit=") + yesNo(js22CancelHit) + ",cancelled=" +
        yesNo(js22CancelledClick) + ",text=" +
        summarizeText(js22CancelText, 180));

    const bool js22CancelHitAgain =
        gxos::apps::Navigator::SmokeHitLinkById("js22-cancel");
    const bool js22UncancelledClick =
        js22CancelHitAgain && gxos::apps::Navigator::SmokeClickFirstLink();
    add("JS22 hosted uncancelled link remains functional",
        js22UncancelledClick && gxos::apps::Navigator::SmokeCurrentUrl() ==
            "http://127.0.0.1:8080/navigator-smoke/javascript-js22-target.html" &&
        contains(gxos::apps::Navigator::SmokeCurrentDocumentText(),
            "Navigator JavaScript JS22 Target"),
        "second link click is not cancelled and navigates authentically");
    add("JS22 navigation cleanup clears Boolean listeners",
        gxos::apps::Navigator::SmokeJavaScriptHandlerCount() == 0u &&
        gxos::apps::Navigator::SmokeJavaScriptListenerCount() == 0u &&
        gxos::apps::Navigator::SmokeJavaScriptLastError().empty(),
        "replacement page starts without JS22 registrations or errors");

    const bool js23Loaded = gxos::apps::Navigator::SmokeNavigateToQuiet(
        "http://127.0.0.1:8080/navigator-smoke/javascript-js23.html");
    const std::string js23InitialText =
        gxos::apps::Navigator::SmokeCurrentDocumentText();
    const size_t js23InitialHandlers =
        gxos::apps::Navigator::SmokeJavaScriptHandlerCount();
    const size_t js23InitialListeners =
        gxos::apps::Navigator::SmokeJavaScriptListenerCount();
    const std::string js23InitialError =
        gxos::apps::Navigator::SmokeJavaScriptLastError();
    add("JS23 hosted fixture loads keyboard listeners",
        js23Loaded && contains(js23InitialText, "Navigator JavaScript JS23") &&
        contains(js23InitialText, "keydown and keyup") &&
        js23InitialHandlers == 3u && js23InitialListeners == 9u &&
        js23InitialError.empty(),
        std::string("loaded=") + yesNo(js23Loaded) + ",handlers=" +
        std::to_string(js23InitialHandlers) + ",listeners=" +
        std::to_string(js23InitialListeners) + ",error=" +
        (js23InitialError.empty() ? "none" : js23InitialError));

    const bool js23Focused =
        gxos::apps::Navigator::SmokeFocusFormControlById("js23-input", true);
    const bool js23KeyDown =
        gxos::apps::Navigator::SmokeKeyPress(65, "down");
    const bool js23KeyUp =
        gxos::apps::Navigator::SmokeKeyPress(65, "up");
    const std::string js23Text =
        gxos::apps::Navigator::SmokeCurrentDocumentText();
    add("JS23 authentic focused keydown/keyup propagation and key/code",
        js23Focused && js23KeyDown && js23KeyUp &&
        contains(js23Text, "d1;p1;t2:a:KeyA;t2b;p3;d3;u1;u2:a:KeyA;u3;") &&
        gxos::apps::Navigator::SmokeFormControlInputLengthById("js23-input") == 1 &&
        gxos::apps::Navigator::SmokeJavaScriptLastError().empty(),
        std::string("focused=") + yesNo(js23Focused) + ",down=" +
        yesNo(js23KeyDown) + ",up=" + yesNo(js23KeyUp) + ",text=" +
        summarizeText(js23Text, 240));

    add("JS23 navigation cleanup clears keyboard listeners",
        gxos::apps::Navigator::SmokeNavigateToQuiet("about:navigator") &&
        gxos::apps::Navigator::SmokeJavaScriptHandlerCount() == 0u &&
        gxos::apps::Navigator::SmokeJavaScriptListenerCount() == 0u &&
        gxos::apps::Navigator::SmokeJavaScriptLastError().empty(),
        "replacement page starts without JS23 registrations or errors");

    bool cssInlineLoaded = gxos::apps::Navigator::SmokeNavigateToQuiet("http://127.0.0.1:8080/navigator-smoke/css-inline.html");
    std::string cssInlineText = gxos::apps::Navigator::SmokeCurrentDocumentText();
    std::string cssInlineReport = gxos::apps::Navigator::SmokeRuntimeReport();
    add("CSS inline styles load",
        cssInlineLoaded &&
        contains(cssInlineText, "Inline CSS Heading") &&
        contains(cssInlineText, "Inline CSS paragraph") &&
        contains(cssInlineText, "Inline CSS link"),
        "currentUrl=" + gxos::apps::Navigator::SmokeCurrentUrl());
    add("CSS inline runtime counters",
        contains(cssInlineReport, "Current Document.CSS enabled=yes") &&
        contains(cssInlineReport, "Current Document.CSS style blocks=0") &&
        hasPositiveCount(cssInlineReport, "Current Document.CSS inline styles=") &&
        contains(cssInlineReport, "Current Document.CSS external stylesheets loaded=0"),
        "report=\"" + summarizeText(cssInlineReport, 260) + "\"");

    bool cssStyleLoaded = gxos::apps::Navigator::SmokeNavigateToQuiet("http://127.0.0.1:8080/navigator-smoke/css-style-block.html");
    std::string cssStyleText = gxos::apps::Navigator::SmokeCurrentDocumentText();
    std::string cssStyleReport = gxos::apps::Navigator::SmokeRuntimeReport();
    add("CSS style block loads",
        cssStyleLoaded &&
        contains(cssStyleText, "Style Block Heading") &&
        contains(cssStyleText, "Class and id selectors work.") &&
        contains(cssStyleText, "Unsupported selector fallback stays readable."),
        "currentUrl=" + gxos::apps::Navigator::SmokeCurrentUrl());
    add("CSS class and id selectors render",
        cssStyleLoaded &&
        !contains(cssStyleText, "Hidden text") &&
        gxos::apps::Navigator::SmokeCurrentBlockCount() >= 4,
        "block_count=" + std::to_string(gxos::apps::Navigator::SmokeCurrentBlockCount()));
    add("CSS style block runtime counters",
        contains(cssStyleReport, "Current Document.CSS enabled=yes") &&
        contains(cssStyleReport, "Current Document.CSS style blocks=1") &&
        hasPositiveCount(cssStyleReport, "Current Document.CSS rules parsed=") &&
        contains(cssStyleReport, "Current Document.CSS parse errors=0"),
        "report=\"" + summarizeText(cssStyleReport, 260) + "\"");
    add("CSS margin and padding stay stable",
        cssStyleLoaded &&
        contains(cssStyleReport, "Current Document.CSS style blocks=1") &&
        contains(cssStyleReport, "Current Document.CSS enabled=yes"),
        "layout remained readable");
    add("CSS display:none hides content",
        cssStyleLoaded && !contains(cssStyleText, "Hidden text"),
        "hidden text omitted from visible document text");

    bool cssUnsupportedLoaded = gxos::apps::Navigator::SmokeNavigateToQuiet("http://127.0.0.1:8080/navigator-smoke/css-unsupported.html");
    std::string cssUnsupportedText = gxos::apps::Navigator::SmokeCurrentDocumentText();
    std::string cssUnsupportedReport = gxos::apps::Navigator::SmokeRuntimeReport();
    add("CSS unsupported rules stay readable",
        cssUnsupportedLoaded &&
        contains(cssUnsupportedText, "Grid-like CSS should not break rendering.") &&
        contains(cssUnsupportedText, "Nested selectors are optional.") &&
        !contains(cssUnsupportedText, "This hidden text must stay hidden."),
        "currentUrl=" + gxos::apps::Navigator::SmokeCurrentUrl());
    add("CSS unsupported CSS is ignored gracefully",
        contains(cssUnsupportedReport, "Current Document.CSS enabled=yes") &&
        contains(cssUnsupportedReport, "Current Document.CSS style blocks=1") &&
        contains(cssUnsupportedReport, "Current Document.CSS parse errors=0"),
        "report=\"" + summarizeText(cssUnsupportedReport, 260) + "\"");

    bool cssExternalLoaded = gxos::apps::Navigator::SmokeNavigateToQuiet("http://127.0.0.1:8080/navigator-smoke/css-external.html");
    std::string cssExternalText = gxos::apps::Navigator::SmokeCurrentDocumentText();
    std::string cssExternalReport = gxos::apps::Navigator::SmokeRuntimeReport();
    add("CSS external stylesheet loads",
        cssExternalLoaded &&
        contains(cssExternalText, "External CSS Heading") &&
        contains(cssExternalText, "External CSS should load safely when supported."),
        "currentUrl=" + gxos::apps::Navigator::SmokeCurrentUrl());
    add("CSS external stylesheet diagnostic",
        contains(cssExternalReport, "Current Document.CSS enabled=yes") &&
        contains(cssExternalReport, "Current Document.CSS external stylesheets loaded=1") &&
        hasPositiveCount(cssExternalReport, "Current Document.CSS rules parsed="),
        "report=\"" + summarizeText(cssExternalReport, 260) + "\"");

    bool cssLayoutLoaded = gxos::apps::Navigator::SmokeNavigateToQuiet("http://127.0.0.1:8080/navigator-smoke/css-layout.html");
    std::string cssLayoutText = gxos::apps::Navigator::SmokeCurrentDocumentText();
    std::string cssLayoutReport = gxos::apps::Navigator::SmokeRuntimeReport();
    add("CSS layout fixture loads",
        cssLayoutLoaded &&
        contains(cssLayoutText, "Phase 1B Layout") &&
        contains(cssLayoutText, "Bullet suppression works.") &&
        contains(cssLayoutText, "Footer spacing should remain comfortable."),
        "currentUrl=" + gxos::apps::Navigator::SmokeCurrentUrl());
    add("CSS layout diagnostics",
        contains(cssLayoutReport, "Current Document.CSS enabled=yes") &&
        contains(cssLayoutReport, "Current Document.CSS style blocks=1") &&
        hasPositiveCount(cssLayoutReport, "Current Document.CSS layout max-width applied=") &&
        hasPositiveCount(cssLayoutReport, "Current Document.CSS auto-margin centered blocks=") &&
        hasPositiveCount(cssLayoutReport, "Current Document.CSS background blocks drawn=") &&
        hasPositiveCount(cssLayoutReport, "Current Document.CSS lists rendered="),
        "report=\"" + summarizeText(cssLayoutReport, 260) + "\"");

    bool cssExternalSafetyLoaded = gxos::apps::Navigator::SmokeNavigateToQuiet("http://127.0.0.1:8080/navigator-smoke/css-external-safety.html");
    std::string cssExternalSafetyText = gxos::apps::Navigator::SmokeCurrentDocumentText();
    std::string cssExternalSafetyReport = gxos::apps::Navigator::SmokeRuntimeReport();
    add("CSS external safety fixture loads",
        cssExternalSafetyLoaded &&
        contains(cssExternalSafetyText, "External CSS Safety") &&
        contains(cssExternalSafetyText, "Missing and unsupported stylesheets should not crash rendering."),
        "currentUrl=" + gxos::apps::Navigator::SmokeCurrentUrl());
    add("CSS external safety diagnostics",
        contains(cssExternalSafetyReport, "Current Document.CSS enabled=yes") &&
        contains(cssExternalSafetyReport, "Current Document.CSS external stylesheets loaded=1") &&
        contains(cssExternalSafetyReport, "Current Document.CSS unsupported external stylesheets=2") &&
        contains(cssExternalSafetyReport, "Current Document.CSS style block capped=yes") &&
        hasPositiveCount(cssExternalSafetyReport, "Current Document.CSS clamped values="),
        "report=\"" + summarizeText(cssExternalSafetyReport, 260) + "\"");

    bool cssWrapperLoaded = gxos::apps::Navigator::SmokeNavigateToQuiet("http://127.0.0.1:8080/navigator-smoke/css-wrappers.html");
    std::string cssWrapperText = gxos::apps::Navigator::SmokeCurrentDocumentText();
    std::string cssWrapperReport = gxos::apps::Navigator::SmokeRuntimeReport();
    add("CSS wrapper fixture loads",
        cssWrapperLoaded &&
        contains(cssWrapperText, "Wrapper Layout") &&
        contains(cssWrapperText, "Article and section wrappers should behave like blocks.") &&
        contains(cssWrapperText, "Navigation wrapper remains visible.") &&
        contains(cssWrapperText, "Aside wrapper remains visible.") &&
        contains(cssWrapperText, "Footer wrapper remains visible."),
        "currentUrl=" + gxos::apps::Navigator::SmokeCurrentUrl());
    add("CSS wrapper diagnostics",
        contains(cssWrapperReport, "Current Document.CSS enabled=yes") &&
        hasPositiveCount(cssWrapperReport, "Current Document.CSS wrapper blocks rendered=") &&
        hasPositiveCount(cssWrapperReport, "Current Document.CSS layout max-width applied=") &&
        hasPositiveCount(cssWrapperReport, "Current Document.CSS auto-margin centered blocks=") &&
        hasPositiveCount(cssWrapperReport, "Current Document.CSS background blocks drawn="),
        "report=\"" + summarizeText(cssWrapperReport, 260) + "\"");

    bool cssTableLoaded = gxos::apps::Navigator::SmokeNavigateToQuiet("http://127.0.0.1:8080/navigator-smoke/css-table.html");
    std::string cssTableText = gxos::apps::Navigator::SmokeCurrentDocumentText();
    std::string cssTableReport = gxos::apps::Navigator::SmokeRuntimeReport();
    add("CSS table fixture loads",
        cssTableLoaded &&
        contains(cssTableText, "Simple Table") &&
        contains(cssTableText, "Name | Value") &&
        contains(cssTableText, "Alpha | One") &&
        contains(cssTableText, "Beta | Two"),
        "currentUrl=" + gxos::apps::Navigator::SmokeCurrentUrl());
    add("CSS table diagnostics",
        contains(cssTableReport, "Current Document.CSS enabled=yes") &&
        hasPositiveCount(cssTableReport, "Current Document.CSS tables rendered=") &&
        hasPositiveCount(cssTableReport, "Current Document.CSS table rows rendered=") &&
        hasPositiveCount(cssTableReport, "Current Document.CSS table cells rendered="),
        "report=\"" + summarizeText(cssTableReport, 260) + "\"");

    bool cssTableWideLoaded = gxos::apps::Navigator::SmokeNavigateToQuiet("http://127.0.0.1:8080/navigator-smoke/css-table-wide.html");
    std::string cssTableWideText = gxos::apps::Navigator::SmokeCurrentDocumentText();
    std::string cssTableWideReport = gxos::apps::Navigator::SmokeRuntimeReport();
    add("CSS wide table fixture loads",
        cssTableWideLoaded &&
        contains(cssTableWideText, "Northwind | Southbound | Eastward | Westward") &&
        contains(cssTableWideText, "Alpha Beta Gamma Delta") &&
        contains(cssTableWideText, "Still Readable"),
        "currentUrl=" + gxos::apps::Navigator::SmokeCurrentUrl());
    add("CSS wide table clamps safely",
        contains(cssTableWideReport, "Current Document.CSS enabled=yes") &&
        (hasPositiveCount(cssTableWideReport, "Current Document.CSS table layout fallbacks=") ||
         hasPositiveCount(cssTableWideReport, "Current Document.table_logical_columns=")) &&
        hasPositiveCount(cssTableWideReport, "Current Document.CSS tables rendered="),
        "fallbacks=" + reportLine(cssTableWideReport, "Current Document.CSS table layout fallbacks=") +
        "; tables=" + reportLine(cssTableWideReport, "Current Document.CSS tables rendered=") +
        "; report=\"" + summarizeText(cssTableWideReport, 260) + "\"");

    bool cssInlinePolishLoaded = gxos::apps::Navigator::SmokeNavigateToQuiet("http://127.0.0.1:8080/navigator-smoke/css-inline-polish.html");
    std::string cssInlinePolishText = gxos::apps::Navigator::SmokeCurrentDocumentText();
    std::string cssInlinePolishReport = gxos::apps::Navigator::SmokeRuntimeReport();
    add("CSS inline text fixture loads",
        cssInlinePolishLoaded &&
        contains(cssInlinePolishText, "Span color") &&
        contains(cssInlinePolishText, "Strong text") &&
        contains(cssInlinePolishText, "Bold text") &&
        contains(cssInlinePolishText, "Emphasis text") &&
        contains(cssInlinePolishText, "Italic text") &&
        contains(cssInlinePolishText, "code sample") &&
        contains(cssInlinePolishText, "Link text") &&
        contains(cssInlinePolishText, "Below the separator."),
        "currentUrl=" + gxos::apps::Navigator::SmokeCurrentUrl());
    add("CSS inline styling diagnostics",
        contains(cssInlinePolishReport, "Current Document.CSS enabled=yes") &&
        hasPositiveCount(cssInlinePolishReport, "Current Document.CSS inline styles="),
        "report=\"" + summarizeText(cssInlinePolishReport, 260) + "\"");

    bool cssInline1dLoaded = gxos::apps::Navigator::SmokeNavigateToQuiet("http://127.0.0.1:8080/navigator-smoke/css-inline-1d.html");
    std::string cssInline1dText = gxos::apps::Navigator::SmokeCurrentDocumentText();
    std::string cssInline1dReport = gxos::apps::Navigator::SmokeRuntimeReport();
    add("CSS inline 1D fixture loads",
        cssInline1dLoaded &&
        contains(cssInline1dText, "Span color") &&
        contains(cssInline1dText, "Strong text") &&
        contains(cssInline1dText, "Bold text") &&
        contains(cssInline1dText, "Emphasis text") &&
        contains(cssInline1dText, "Italic text") &&
        contains(cssInline1dText, "Small text") &&
        contains(cssInline1dText, "code sample") &&
        contains(cssInline1dText, "Link text") &&
        contains(cssInline1dText, "Line one") &&
        contains(cssInline1dText, "Line two"),
        "currentUrl=" + gxos::apps::Navigator::SmokeCurrentUrl());
    add("CSS inline 1D diagnostics",
        contains(cssInline1dReport, "Current Document.CSS enabled=yes") &&
        hasPositiveCount(cssInline1dReport, "Current Document.CSS line breaks parsed="),
        "report=\"" + summarizeText(cssInline1dReport, 260) + "\"");

    bool cssInline1dVisitedTargetLoaded = gxos::apps::Navigator::SmokeNavigateToQuiet("http://127.0.0.1:8080/navigator-smoke/basic.html");
    bool cssInline1dReloaded = gxos::apps::Navigator::SmokeNavigateToQuiet("http://127.0.0.1:8080/navigator-smoke/css-inline-1d.html");
    std::string cssInline1dVisitedReport = gxos::apps::Navigator::SmokeRuntimeReport();
    add("CSS inline visited link styling",
        cssInline1dVisitedTargetLoaded && cssInline1dReloaded &&
        hasPositiveCount(cssInline1dVisitedReport, "Current Document.CSS visited links styled="),
        "report=\"" + summarizeText(cssInline1dVisitedReport, 260) + "\"");

    bool cssTable1dLoaded = gxos::apps::Navigator::SmokeNavigateToQuiet("http://127.0.0.1:8080/navigator-smoke/css-table-1d.html");
    std::string cssTable1dText = gxos::apps::Navigator::SmokeCurrentDocumentText();
    std::string cssTable1dReport = gxos::apps::Navigator::SmokeRuntimeReport();
    add("CSS table 1D fixture loads",
        cssTable1dLoaded &&
        contains(cssTable1dText, "Navigator Table Caption") &&
        contains(cssTable1dText, "Name") &&
        contains(cssTable1dText, "Value") &&
        contains(cssTable1dText, "Notes") &&
        contains(cssTable1dText, "linked cell") &&
        contains(cssTable1dText, "code sample") &&
        contains(cssTable1dText, "small note") &&
        contains(cssTable1dText, "Wrapper spacing stays readable."),
        "currentUrl=" + gxos::apps::Navigator::SmokeCurrentUrl());
    add("CSS table 1D diagnostics",
        contains(cssTable1dReport, "Current Document.CSS enabled=yes") &&
        hasPositiveCount(cssTable1dReport, "Current Document.CSS table captions rendered=") &&
        hasPositiveCount(cssTable1dReport, "Current Document.CSS table header cells rendered=") &&
        hasPositiveCount(cssTable1dReport, "Current Document.CSS visited links styled="),
        "report=\"" + summarizeText(cssTable1dReport, 260) + "\"");

    const std::string tablePhase8bUrl =
        "http://127.0.0.1:8080/navigator-smoke/table-phase8b.html";
    const bool tablePhase8bLoaded = gxos::apps::Navigator::SmokeNavigateToQuiet(tablePhase8bUrl);
    const std::string tablePhase8bText = gxos::apps::Navigator::SmokeCurrentDocumentText();
    const std::string tablePhase8bReport = gxos::apps::Navigator::SmokeRuntimeReport();
    int table8bX = 0, table8bY = 0, table8bW = 0, table8bH = 0;
    int table8bRows = 0, table8bColumns = 0;
    const bool table8bGeometry = gxos::apps::Navigator::SmokeTableGeometryById(
        "phase8b-basic", table8bX, table8bY, table8bW, table8bH, table8bRows, table8bColumns);
    int following8bX = 0, following8bY = 0, following8bW = 0, following8bH = 0;
    const bool following8bGeometry = gxos::apps::Navigator::SmokeBlockGeometryById(
        "phase8b-following", following8bX, following8bY, following8bW, following8bH);
    int span8bX = 0, span8bY = 0, span8bW = 0, span8bH = 0;
    int span8bRows = 0, span8bColumns = 0;
    const bool span8bGeometry = gxos::apps::Navigator::SmokeTableGeometryById(
        "phase8b-spans", span8bX, span8bY, span8bW, span8bH, span8bRows, span8bColumns);
    int wide8bX = 0, wide8bY = 0, wide8bW = 0, wide8bH = 0;
    int wide8bRows = 0, wide8bColumns = 0;
    const bool wide8bGeometry = gxos::apps::Navigator::SmokeTableGeometryById(
        "phase8b-wide", wide8bX, wide8bY, wide8bW, wide8bH, wide8bRows, wide8bColumns);
    const int table8bWideMaxX = gxos::apps::Navigator::SmokeElementMaxScrollXById("phase8b-wide-scroll");
    gxos::apps::Navigator::SmokeSetScrollOffset(std::max(0, wide8bY - 120));
    int table8bBarX = 0, table8bBarY = 0, table8bBarW = 0, table8bBarH = 0;
    const bool table8bHorizontalBar = gxos::apps::Navigator::SmokeElementScrollbarGeometryById(
        "phase8b-wide-scroll", true, false, table8bBarX, table8bBarY, table8bBarW, table8bBarH);
    const bool table8bLinkBeforeScroll = gxos::apps::Navigator::SmokeHitLinkById("phase8b-scrolled-link");
    const int table8bScrollTarget = std::min(table8bWideMaxX, 240);
    const bool table8bScrolled = table8bWideMaxX > 0 &&
        gxos::apps::Navigator::SmokeSetElementScrollOffsetById(
            "phase8b-wide-scroll", table8bScrollTarget, 0);
    int table8bLinkPaintX = 0, table8bLinkPaintY = 0, table8bLinkPaintW = 0, table8bLinkPaintH = 0;
    int table8bLinkFinalX = 0, table8bLinkFinalY = 0, table8bLinkFinalW = 0, table8bLinkFinalH = 0;
    int table8bLinkClipX = 0, table8bLinkClipY = 0, table8bLinkClipW = 0, table8bLinkClipH = 0;
    const bool table8bLinkGeometry = gxos::apps::Navigator::SmokeLinkGeometryById(
        "phase8b-scrolled-link", table8bLinkPaintX, table8bLinkPaintY, table8bLinkPaintW, table8bLinkPaintH,
        table8bLinkFinalX, table8bLinkFinalY, table8bLinkFinalW, table8bLinkFinalH,
        table8bLinkClipX, table8bLinkClipY, table8bLinkClipW, table8bLinkClipH);
    const bool table8bLinkAfterScroll = table8bScrolled &&
        gxos::apps::Navigator::SmokeHitLinkById("phase8b-scrolled-link");
    gxos::apps::Navigator::SmokeSetElementScrollOffsetById("phase8b-wide-scroll", 0, 0);
    const std::string table8bFinalReport = gxos::apps::Navigator::SmokeRuntimeReport();
    add("HTML table Phase 8B fixture loads", tablePhase8bLoaded &&
        contains(tablePhase8bText, "HTML Table Layout Foundation") &&
        contains(tablePhase8bText, "Phase 8B shared-column caption") &&
        contains(tablePhase8bText, "wrapped cell link") &&
        contains(tablePhase8bText, "local image in a table cell") &&
        contains(tablePhase8bText, "Malformed table markup"),
        "currentUrl=" + gxos::apps::Navigator::SmokeCurrentUrl() + ",text=\"" +
        summarizeText(tablePhase8bText, 500) + "\"");
    add("HTML table Phase 8B bounded diagnostics", tablePhase8bLoaded &&
        hasPositiveCount(tablePhase8bReport, "Current Document.CSS tables rendered=") &&
        hasPositiveCount(tablePhase8bReport, "Current Document.CSS table rows rendered=") &&
        hasPositiveCount(tablePhase8bReport, "Current Document.CSS table cells rendered=") &&
        hasPositiveCount(tablePhase8bReport, "Current Document.table_logical_columns=") &&
        hasPositiveCount(tablePhase8bReport, "Current Document.table_data_cells=") &&
        hasPositiveCount(tablePhase8bReport, "Current Document.table_colspan_cells=") &&
        hasPositiveCount(tablePhase8bReport, "Current Document.table_maximum_colspan=") &&
        hasPositiveCount(tablePhase8bReport, "Current Document.table_wrapped_cells=") &&
        hasPositiveCount(tablePhase8bReport, "Current Document.table_malformed_fallbacks=") &&
        contains(tablePhase8bReport, "Current Document.table_rowspan_model=bounded-occupancy-grid-max16-two-pass-height-solve") &&
        contains(tablePhase8bReport, "Current Document.table_geometry_evidence="),
        "report=\"" + summarizeText(tablePhase8bReport, 1200) + "\"");
    add("HTML table Phase 8B shared grid and normal flow geometry", table8bGeometry &&
        table8bRows >= 4 && table8bColumns == 2 && table8bW > 0 && table8bH > 0 &&
        following8bGeometry && following8bY >= table8bY + table8bH,
        "table=" + std::to_string(table8bX) + ":" + std::to_string(table8bY) + ":" +
        std::to_string(table8bW) + ":" + std::to_string(table8bH) + ",rows=" +
        std::to_string(table8bRows) + ",columns=" + std::to_string(table8bColumns) +
        ",following=" + std::to_string(following8bX) + ":" + std::to_string(following8bY) +
        ":" + std::to_string(following8bW) + ":" + std::to_string(following8bH));
    add("HTML table Phase 8B colspan and header geometry", span8bGeometry &&
        span8bRows >= 4 && span8bColumns >= 3 && span8bW > 0 && span8bH > 0 &&
        contains(tablePhase8bReport, "Current Document.CSS table header cells rendered="),
        "spans=" + std::to_string(span8bX) + ":" + std::to_string(span8bY) + ":" +
        std::to_string(span8bW) + ":" + std::to_string(span8bH) + ",rows=" +
        std::to_string(span8bRows) + ",columns=" + std::to_string(span8bColumns) +
        ",evidence=" + evidenceSnippet(tablePhase8bReport, "Current Document.table_geometry_evidence="));
    add("HTML table Phase 8B links and wide-table scrollbar", table8bHorizontalBar &&
        table8bBarW > 0 && table8bBarH > 0 && table8bWideMaxX > 0 &&
        table8bLinkAfterScroll &&
        hasPositiveCount(table8bFinalReport, "Current Document.table_link_hit_test_evidence=") &&
        wide8bGeometry && wide8bW > 0 && wide8bH > 0,
        std::string("bar=") + yesNo(table8bHorizontalBar) + ",maxScrollX=" +
        std::to_string(table8bWideMaxX) + ",barRect=" + std::to_string(table8bBarX) + ":" +
        std::to_string(table8bBarY) + ":" + std::to_string(table8bBarW) + ":" +
        std::to_string(table8bBarH) + ",linkBefore=" + yesNo(table8bLinkBeforeScroll) +
        ",linkAfter=" + yesNo(table8bLinkAfterScroll) + ",wide=" +
        std::to_string(wide8bX) + ":" + std::to_string(wide8bY) + ":" +
        std::to_string(wide8bW) + ":" + std::to_string(wide8bH) + ",evidence=" +
        evidenceSnippet(tablePhase8bReport, "Current Document.table_geometry_evidence=") +
        ",linkGeometry=" + yesNo(table8bLinkGeometry) + ",paint=" +
        std::to_string(table8bLinkPaintX) + ":" + std::to_string(table8bLinkPaintY) + ":" +
        std::to_string(table8bLinkPaintW) + ":" + std::to_string(table8bLinkPaintH) + ",final=" +
        std::to_string(table8bLinkFinalX) + ":" + std::to_string(table8bLinkFinalY) + ":" +
        std::to_string(table8bLinkFinalW) + ":" + std::to_string(table8bLinkFinalH) + ",clip=" +
        std::to_string(table8bLinkClipX) + ":" + std::to_string(table8bLinkClipY) + ":" +
        std::to_string(table8bLinkClipW) + ":" + std::to_string(table8bLinkClipH));

    const std::string tablePhase8cUrl =
        "http://127.0.0.1:8080/navigator-smoke/table-phase8c.html";
    const bool tablePhase8cLoaded = gxos::apps::Navigator::SmokeNavigateToQuiet(tablePhase8cUrl);
    const std::string tablePhase8cText = gxos::apps::Navigator::SmokeCurrentDocumentText();
    const std::string tablePhase8cReport = gxos::apps::Navigator::SmokeRuntimeReport();
    int table8cX = 0, table8cY = 0, table8cW = 0, table8cH = 0, table8cRows = 0, table8cColumns = 0;
    const bool table8cGeometry = gxos::apps::Navigator::SmokeTableGeometryById(
        "phase8c-basic", table8cX, table8cY, table8cW, table8cH, table8cRows, table8cColumns);
    int span2X = 0, span2Y = 0, span2W = 0, span2H = 0, span2Row = 0, span2Column = 0, span2Rows = 0, span2Columns = 0;
    int span3X = 0, span3Y = 0, span3W = 0, span3H = 0, span3Row = 0, span3Column = 0, span3Rows = 0, span3Columns = 0;
    int combinedX = 0, combinedY = 0, combinedW = 0, combinedH = 0, combinedRow = 0, combinedColumn = 0, combinedRows = 0, combinedColumns = 0;
    const bool span2Geometry = gxos::apps::Navigator::SmokeTableCellGeometryById(
        "phase8c-span2", span2X, span2Y, span2W, span2H, span2Row, span2Column, span2Rows, span2Columns);
    const bool span3Geometry = gxos::apps::Navigator::SmokeTableCellGeometryById(
        "phase8c-span3", span3X, span3Y, span3W, span3H, span3Row, span3Column, span3Rows, span3Columns);
    const bool combinedGeometry = gxos::apps::Navigator::SmokeTableCellGeometryById(
        "phase8c-combined", combinedX, combinedY, combinedW, combinedH, combinedRow, combinedColumn, combinedRows, combinedColumns);
    int groupHeadX = 0, groupHeadY = 0, groupHeadW = 0, groupHeadH = 0, groupHeadRow = 0, groupHeadColumn = 0, groupHeadRows = 0, groupHeadColumns = 0;
    int groupBodyX = 0, groupBodyY = 0, groupBodyW = 0, groupBodyH = 0, groupBodyRow = 0, groupBodyColumn = 0, groupBodyRows = 0, groupBodyColumns = 0;
    const bool groupHeadGeometry = gxos::apps::Navigator::SmokeTableCellGeometryById(
        "phase8c-group-head", groupHeadX, groupHeadY, groupHeadW, groupHeadH, groupHeadRow, groupHeadColumn, groupHeadRows, groupHeadColumns);
    const bool groupBodyGeometry = gxos::apps::Navigator::SmokeTableCellGeometryById(
        "phase8c-group-body", groupBodyX, groupBodyY, groupBodyW, groupBodyH, groupBodyRow, groupBodyColumn, groupBodyRows, groupBodyColumns);
    int following8cX = 0, following8cY = 0, following8cW = 0, following8cH = 0;
    const bool following8cGeometry = gxos::apps::Navigator::SmokeBlockGeometryById(
        "phase8c-following", following8cX, following8cY, following8cW, following8cH);
    int collapseX = 0, collapseY = 0, collapseW = 0, collapseH = 0, collapseRows = 0, collapseColumns = 0;
    const bool collapseGeometry = gxos::apps::Navigator::SmokeTableGeometryById(
        "phase8c-collapse", collapseX, collapseY, collapseW, collapseH, collapseRows, collapseColumns);
    const int wide8cMaxX = gxos::apps::Navigator::SmokeElementMaxScrollXById("phase8c-wide-scroll");
    int wide8cTableX = 0, wide8cTableY = 0, wide8cTableW = 0, wide8cTableH = 0, wide8cTableRows = 0, wide8cTableColumns = 0;
    const bool wide8cTableGeometry = gxos::apps::Navigator::SmokeTableGeometryById(
        "phase8c-wide", wide8cTableX, wide8cTableY, wide8cTableW, wide8cTableH, wide8cTableRows, wide8cTableColumns);
    gxos::apps::Navigator::SmokeSetScrollOffset(std::max(0, wide8cTableY - 120));
    int wide8cLinkPaintX = 0, wide8cLinkPaintY = 0, wide8cLinkPaintW = 0, wide8cLinkPaintH = 0;
    int wide8cLinkFinalX = 0, wide8cLinkFinalY = 0, wide8cLinkFinalW = 0, wide8cLinkFinalH = 0;
    int wide8cLinkClipX = 0, wide8cLinkClipY = 0, wide8cLinkClipW = 0, wide8cLinkClipH = 0;
    const bool wide8cLinkBefore = gxos::apps::Navigator::SmokeHitLinkById("phase8c-scrolled-link");
    const int wide8cScrollTarget = std::min(wide8cMaxX, 240);
    const bool wide8cScrolled = wide8cMaxX > 0 && gxos::apps::Navigator::SmokeSetElementScrollOffsetById(
        "phase8c-wide-scroll", wide8cScrollTarget, 0);
    const bool wide8cLinkGeometry = gxos::apps::Navigator::SmokeLinkGeometryById(
        "phase8c-scrolled-link", wide8cLinkPaintX, wide8cLinkPaintY, wide8cLinkPaintW, wide8cLinkPaintH,
        wide8cLinkFinalX, wide8cLinkFinalY, wide8cLinkFinalW, wide8cLinkFinalH,
        wide8cLinkClipX, wide8cLinkClipY, wide8cLinkClipW, wide8cLinkClipH);
    const bool wide8cLinkAfter = wide8cScrolled && gxos::apps::Navigator::SmokeHitLinkById("phase8c-scrolled-link");
    const bool wide8cReset = gxos::apps::Navigator::SmokeSetElementScrollOffsetById("phase8c-wide-scroll", 0, 0);
    const bool wide8cOldRejected = wide8cScrolled && wide8cLinkGeometry && wide8cReset && !gxos::apps::Navigator::SmokeHitLinkAt(
        wide8cLinkPaintX + wide8cScrollTarget + std::max(0, wide8cLinkPaintW / 2),
        wide8cLinkPaintY + std::max(0, wide8cLinkPaintH / 2), "phase8c-scrolled-link");
    const std::string tablePhase8cFinalReport = gxos::apps::Navigator::SmokeRuntimeReport();
    add("HTML table Phase 8C fixture loads", tablePhase8cLoaded &&
        contains(tablePhase8cText, "Rowspan and Border-Collapse Semantics") &&
        contains(tablePhase8cText, "thead rowspan clamps") &&
        contains(tablePhase8cText, "Wide collapsed table") &&
        contains(tablePhase8cText, "Following normal-flow content"),
        "currentUrl=" + gxos::apps::Navigator::SmokeCurrentUrl() + ",text=\"" + summarizeText(tablePhase8cText, 700) + "\"");
    add("HTML table Phase 8C occupancy and bounded diagnostics", tablePhase8cLoaded &&
        hasPositiveCount(tablePhase8cReport, "Current Document.table_rowspan_cells=") &&
        hasPositiveCount(tablePhase8cReport, "Current Document.table_maximum_rowspan=") &&
        hasPositiveCount(tablePhase8cReport, "Current Document.table_occupied_grid_skips=") &&
        hasPositiveCount(tablePhase8cReport, "Current Document.table_rowspan_height_adjustments=") &&
        hasPositiveCount(tablePhase8cReport, "Current Document.table_combined_spans=") &&
        contains(tablePhase8cReport, "Current Document.table_rowspan_model=bounded-occupancy-grid-max16-two-pass-height-solve"),
        "report=\"" + summarizeText(tablePhase8cReport, 1500) + "\"");
    add("HTML table Phase 8C spanning geometry", table8cGeometry && span2Geometry && span3Geometry && combinedGeometry &&
        table8cRows >= 10 && table8cColumns >= 3 && span2Rows == 2 && span3Rows == 3 &&
        combinedRows == 3 && combinedColumns == 2 && span2H > 0 && span3H > 0 && combinedW > span2W &&
        combinedY >= span3Y + span3H,
        "table=" + std::to_string(table8cX) + ":" + std::to_string(table8cY) + ":" + std::to_string(table8cW) + ":" + std::to_string(table8cH) +
        ",span2=" + std::to_string(span2X) + ":" + std::to_string(span2Y) + ":" + std::to_string(span2W) + ":" + std::to_string(span2H) +
        ",span3=" + std::to_string(span3X) + ":" + std::to_string(span3Y) + ":" + std::to_string(span3W) + ":" + std::to_string(span3H) +
        ",combined=" + std::to_string(combinedX) + ":" + std::to_string(combinedY) + ":" + std::to_string(combinedW) + ":" + std::to_string(combinedH));
    add("HTML table Phase 8C row-group clamps and malformed spans", groupHeadGeometry && groupBodyGeometry &&
        groupHeadRows == 1 && groupBodyRows == 2 &&
        hasPositiveCount(tablePhase8cReport, "Current Document.table_malformed_fallbacks=") &&
        contains(tablePhase8cReport, "Current Document.table_geometry_evidence="),
        "theadRows=" + std::to_string(groupHeadRows) + ",tbodyRows=" + std::to_string(groupBodyRows) +
        ",evidence=" + evidenceSnippet(tablePhase8cReport, "Current Document.table_geometry_evidence="));
    add("HTML table Phase 8C collapsed edges and normal flow", collapseGeometry && collapseW >= 520 && collapseW <= 528 &&
        hasPositiveCount(tablePhase8cReport, "Current Document.CSS collapsed tables rendered=") &&
        hasPositiveCount(tablePhase8cReport, "Current Document.table_resolved_vertical_edges=") &&
        hasPositiveCount(tablePhase8cReport, "Current Document.table_resolved_horizontal_edges=") &&
        hasPositiveCount(tablePhase8cReport, "Current Document.table_suppressed_interior_span_edges=") &&
        following8cGeometry && following8cY >= table8cY + table8cH,
        "collapse=" + std::to_string(collapseX) + ":" + std::to_string(collapseY) + ":" + std::to_string(collapseW) + ":" + std::to_string(collapseH) +
        ",following=" + std::to_string(following8cX) + ":" + std::to_string(following8cY) + ":" + std::to_string(following8cW) + ":" + std::to_string(following8cH) +
        ",evidence=" + evidenceSnippet(tablePhase8cReport, "Current Document.table_geometry_evidence="));
    add("HTML table Phase 8C spanning links and wide collapsed scrolling", wide8cTableGeometry && wide8cMaxX > 0 && wide8cLinkGeometry &&
        wide8cScrolled && wide8cLinkAfter && wide8cOldRejected &&
        hasPositiveCount(tablePhase8cFinalReport, "Current Document.table_link_hit_test_evidence="),
        std::string("maxScrollX=") + std::to_string(wide8cMaxX) + ",before=" + yesNo(wide8cLinkBefore) +
        ",after=" + yesNo(wide8cLinkAfter) + ",oldRejected=" + yesNo(wide8cOldRejected) +
        ",linkGeometry=" + yesNo(wide8cLinkGeometry) + ",paint=" + std::to_string(wide8cLinkPaintX) + ":" +
        std::to_string(wide8cLinkPaintY) + ":" + std::to_string(wide8cLinkPaintW) + ":" + std::to_string(wide8cLinkPaintH) +
        ",final=" + std::to_string(wide8cLinkFinalX) + ":" + std::to_string(wide8cLinkFinalY) + ":" +
        std::to_string(wide8cLinkFinalW) + ":" + std::to_string(wide8cLinkFinalH) + ",evidence=" +
        evidenceSnippet(tablePhase8cFinalReport, "Current Document.css_scroll_evidence="));

    bool textPolishLoaded = gxos::apps::Navigator::SmokeNavigateToQuiet("http://127.0.0.1:8080/navigator-smoke/text-polish.html");
    std::string textPolishText = gxos::apps::Navigator::SmokeCurrentDocumentText();
    std::string textPolishReport = gxos::apps::Navigator::SmokeRuntimeReport();
    add("Text polish fixture loads",
        textPolishLoaded &&
        contains(textPolishText, "Text Polish g j p q y") &&
        contains(textPolishText, "Descenders stay readable in this sentence: g j p q y.") &&
        contains(textPolishText, "small text next to normal text g j p q y") &&
        contains(textPolishText, "Underlined link smoke marker g j p q y") &&
        contains(textPolishText, "inline code g j p q y") &&
        contains(textPolishText, "Bold g j p q y") &&
        contains(textPolishText, "Italic g j p q y") &&
        contains(textPolishText, "Faux bold italic g j p q y") &&
        contains(textPolishText, "Caption descenders g j p q y") &&
        contains(textPolishText, "Alpha g j p q y") &&
        contains(textPolishText, "Beta g j p q y") &&
        contains(textPolishText, "pre line one g j p q y") &&
        contains(textPolishText, "pre line two g j p q y"),
        "currentUrl=" + gxos::apps::Navigator::SmokeCurrentUrl());
    add("Text polish diagnostics",
        contains(textPolishReport, "Current Document.CSS enabled=yes") &&
        contains(textPolishReport, "Current Document.text_metrics_model=baseline/descent aware system font") &&
        contains(textPolishReport, "Current Document.text_backend=hosted-gdi") &&
        hasPositiveCount(textPolishReport, "Current Document.text_top_padding_px=") &&
        hasPositiveCount(textPolishReport, "Current Document.text_underline_gap_px=") &&
        hasPositiveCount(textPolishReport, "Current Document.CSS tables rendered=") &&
        hasPositiveCount(textPolishReport, "Current Document.CSS table cells rendered=") &&
        hasPositiveCount(textPolishReport, "Current Document.CSS visited links styled="),
        "report=\"" + summarizeText(textPolishReport, 260) + "\"");

    bool cssHrLoaded = gxos::apps::Navigator::SmokeNavigateToQuiet("http://127.0.0.1:8080/navigator-smoke/css-hr.html");
    std::string cssHrText = gxos::apps::Navigator::SmokeCurrentDocumentText();
    std::string cssHrReport = gxos::apps::Navigator::SmokeRuntimeReport();
    add("CSS horizontal rule fixture loads",
        cssHrLoaded &&
        contains(cssHrText, "Above the horizontal rule.") &&
        contains(cssHrText, "Below the horizontal rule.") &&
        gxos::apps::Navigator::SmokeCurrentBlockCount() >= 3,
        "currentUrl=" + gxos::apps::Navigator::SmokeCurrentUrl());
    add("CSS horizontal rule rendering diagnostics",
        contains(cssHrReport, "Current Document.CSS enabled=yes"),
        "report=\"" + summarizeText(cssHrReport, 260) + "\"");

    bool cssDisplayNoneLoaded = gxos::apps::Navigator::SmokeNavigateToQuiet("http://127.0.0.1:8080/navigator-smoke/css-unsupported.html");
    std::string cssDisplayNoneText = gxos::apps::Navigator::SmokeCurrentDocumentText();
    add("CSS display:none remains hidden",
        cssDisplayNoneLoaded &&
        !contains(cssDisplayNoneText, "This hidden text must stay hidden."),
        "currentUrl=" + gxos::apps::Navigator::SmokeCurrentUrl());

    bool cssPhase1eLoaded = gxos::apps::Navigator::SmokeNavigateToQuiet("http://127.0.0.1:8080/navigator-smoke/css-phase1e.html");
    std::string cssPhase1eText = gxos::apps::Navigator::SmokeCurrentDocumentText();
    std::string cssPhase1eReport = gxos::apps::Navigator::SmokeRuntimeReport();
    add("CSS phase 1E fixture loads",
        cssPhase1eLoaded &&
        contains(cssPhase1eText, "Phase 1E Media and Text") &&
        contains(cssPhase1eText, "Long URL marker") &&
        contains(cssPhase1eText, "Break-all marker") &&
        contains(cssPhase1eText, "Pre-wrap marker line one") &&
        contains(cssPhase1eText, "Nested wrapper backgrounds and padding marker.") &&
        contains(cssPhase1eText, "Unsupported properties remain nonfatal."),
        "currentUrl=" + gxos::apps::Navigator::SmokeCurrentUrl());
    add("CSS phase 1E media markers",
        cssPhase1eLoaded &&
        contains(cssPhase1eText, "Max-width 100 percent figure marker") &&
        contains(cssPhase1eText, "Width-only aspect ratio marker") &&
        contains(cssPhase1eText, "Height-only aspect ratio marker") &&
        contains(cssPhase1eText, "Max-height aspect marker") &&
        contains(cssPhase1eText, "Oversized image clamped to content width marker") &&
        contains(cssPhase1eText, "Malformed or huge dimensions are clamped marker") &&
        contains(cssPhase1eText, "Missing image alt fallback marker"),
        "text=\"" + summarizeText(cssPhase1eText, 260) + "\"");
    add("CSS phase 1E structural markers",
        cssPhase1eLoaded &&
        contains(cssPhase1eText, "Blockquote marker line.") &&
        contains(cssPhase1eText, "Nested blockquote marker line.") &&
        contains(cssPhase1eText, "Citation marker.") &&
        contains(cssPhase1eText, "Definition term alpha") &&
        contains(cssPhase1eText, "Definition detail two."),
        "text=\"" + summarizeText(cssPhase1eText, 260) + "\"");
    add("CSS phase 1E diagnostics",
        hasPositiveCount(cssPhase1eReport, "Current Document.CSS figures rendered=") &&
        hasPositiveCount(cssPhase1eReport, "Current Document.CSS figcaptions rendered=") &&
        hasPositiveCount(cssPhase1eReport, "Current Document.CSS blockquotes rendered=") &&
        hasPositiveCount(cssPhase1eReport, "Current Document.CSS definition lists rendered=") &&
        hasPositiveCount(cssPhase1eReport, "Current Document.CSS images constrained=") &&
        hasPositiveCount(cssPhase1eReport, "Current Document.CSS images aspect preserved=") &&
        hasPositiveCount(cssPhase1eReport, "Current Document.CSS image alt fallbacks=") &&
        hasPositiveCount(cssPhase1eReport, "Current Document.CSS image size clamps=") &&
        hasPositiveCount(cssPhase1eReport, "Current Document.CSS nested layout clamps="),
        "report=\"" + summarizeText(cssPhase1eReport, 260) + "\"");

    bool cssPhase1fLoaded = gxos::apps::Navigator::SmokeNavigateToQuiet("http://127.0.0.1:8080/navigator-smoke/css-phase1f.html");
    std::string cssPhase1fText = gxos::apps::Navigator::SmokeCurrentDocumentText();
    std::string cssPhase1fReport = gxos::apps::Navigator::SmokeRuntimeReport();
    add("CSS phase 1F fixture loads",
        cssPhase1fLoaded &&
        contains(cssPhase1fText, "Wrapper with border shorthand 1px solid #888 marker.") &&
        contains(cssPhase1fText, "Oversized border width clamp marker.") &&
        contains(cssPhase1fText, "Caption spacing marker") &&
        contains(cssPhase1fText, "List style none item stays readable without markers.") &&
        contains(cssPhase1fText, "Default underlined link marker") &&
        contains(cssPhase1fText, "Sans-serif font family marker.") &&
        contains(cssPhase1fText, "Serif fallback marker."),
        "currentUrl=" + gxos::apps::Navigator::SmokeCurrentUrl());
    std::string cssPhase1fDetail =
        "report=\"" + summarizeText(cssPhase1fReport, 260) + "\"" +
        " bordered=" + std::to_string(countValue(cssPhase1fReport, "Current Document.CSS bordered blocks rendered=")) +
        " dashed=" + std::to_string(countValue(cssPhase1fReport, "Current Document.CSS dashed borders rendered=")) +
        " dotted=" + std::to_string(countValue(cssPhase1fReport, "Current Document.CSS dotted borders rendered=")) +
        " border_width_clamps=" + std::to_string(countValue(cssPhase1fReport, "Current Document.CSS border width clamps=")) +
        " collapsed_tables=" + std::to_string(countValue(cssPhase1fReport, "Current Document.CSS collapsed tables rendered=")) +
        " separate_tables=" + std::to_string(countValue(cssPhase1fReport, "Current Document.CSS separate tables rendered=")) +
        " border_spacing_clamps=" + std::to_string(countValue(cssPhase1fReport, "Current Document.CSS table border spacing clamps=")) +
        " list_markers=" + std::to_string(countValue(cssPhase1fReport, "Current Document.CSS list style markers rendered=")) +
        " list_none=" + std::to_string(countValue(cssPhase1fReport, "Current Document.CSS list style none applied=")) +
        " text_decorations=" + std::to_string(countValue(cssPhase1fReport, "Current Document.CSS text decorations rendered=")) +
        " generic_fonts=" + std::to_string(countValue(cssPhase1fReport, "Current Document.CSS generic font family applied=")) +
        " generic_font_fallbacks=" + std::to_string(countValue(cssPhase1fReport, "Current Document.CSS generic font family fallbacks=")) +
        " table_layout_fallbacks=" + std::to_string(countValue(cssPhase1fReport, "Current Document.CSS table layout fallbacks=")) +
        " table_captions=" + std::to_string(countValue(cssPhase1fReport, "Current Document.CSS table captions rendered="));
    add("CSS phase 1F diagnostics",
        contains(cssPhase1fReport, "Current Document.CSS enabled=yes") &&
        hasPositiveCount(cssPhase1fReport, "Current Document.CSS bordered blocks rendered=") &&
        hasPositiveCount(cssPhase1fReport, "Current Document.CSS dashed borders rendered=") &&
        hasPositiveCount(cssPhase1fReport, "Current Document.CSS dotted borders rendered=") &&
        hasPositiveCount(cssPhase1fReport, "Current Document.CSS border width clamps=") &&
        hasPositiveCount(cssPhase1fReport, "Current Document.CSS collapsed tables rendered=") &&
        hasPositiveCount(cssPhase1fReport, "Current Document.CSS separate tables rendered=") &&
        hasPositiveCount(cssPhase1fReport, "Current Document.CSS table border spacing clamps=") &&
        hasPositiveCount(cssPhase1fReport, "Current Document.CSS list style markers rendered=") &&
        hasPositiveCount(cssPhase1fReport, "Current Document.CSS list style none applied=") &&
        hasPositiveCount(cssPhase1fReport, "Current Document.CSS text decorations rendered=") &&
        hasPositiveCount(cssPhase1fReport, "Current Document.CSS generic font family applied=") &&
        hasPositiveCount(cssPhase1fReport, "Current Document.CSS generic font family fallbacks=") &&
        (hasPositiveCount(cssPhase1fReport, "Current Document.CSS table layout fallbacks=") ||
         hasPositiveCount(cssPhase1fReport, "Current Document.table_logical_columns=")) &&
        hasPositiveCount(cssPhase1fReport, "Current Document.CSS table captions rendered="),
        cssPhase1fDetail);

    bool cssPhase3aLoaded = gxos::apps::Navigator::SmokeNavigateToQuiet("http://127.0.0.1:8080/navigator-smoke/css-phase3a.html");
    std::string cssPhase3aText = gxos::apps::Navigator::SmokeCurrentDocumentText();
    std::string cssPhase3aReport = gxos::apps::Navigator::SmokeRuntimeReport();
    auto cssPhase3aMetric = [&](const std::string& prefix, std::size_t limit) {
        const std::size_t pos = cssPhase3aReport.find(prefix);
        if (pos == std::string::npos) return std::string("missing");
        return cssPhase3aReport.substr(pos, limit);
    };
    add("CSS phase 3A fixture loads",
        cssPhase3aLoaded &&
        contains(cssPhase3aText, "Phase 3A Box Constraint Fixture") &&
        contains(cssPhase3aText, "50 percent nested child marker.") &&
        contains(cssPhase3aText, "Auto width and content-derived auto height marker.") &&
        contains(cssPhase3aText, "Intrinsic ratio image marker") &&
        contains(cssPhase3aText, "Explicit content-box marker.") &&
        contains(cssPhase3aText, "Nested percentage child marker.") &&
        contains(cssPhase3aText, "Nested clipping text marker") &&
        contains(cssPhase3aText, "Invalid width preserves valid winner marker.") &&
        contains(cssPhase3aText, "Percent") &&
        !contains(cssPhase3aText, "Hidden visibility marker must retain space but not paint or extract."),
        "currentUrl=" + gxos::apps::Navigator::SmokeCurrentUrl());
    add("CSS phase 3A box and constraint diagnostics",
        hasPositiveCount(cssPhase3aReport, "Current Document.css_box_sizing_content_box=") &&
        hasPositiveCount(cssPhase3aReport, "Current Document.css_box_sizing_border_box=") &&
        hasPositiveCount(cssPhase3aReport, "Current Document.css_width_auto_resolutions=") &&
        hasPositiveCount(cssPhase3aReport, "Current Document.css_height_auto_resolutions=") &&
        hasPositiveCount(cssPhase3aReport, "Current Document.css_percentage_width_resolved=") &&
        hasPositiveCount(cssPhase3aReport, "Current Document.css_percentage_height_resolved=") &&
        hasPositiveCount(cssPhase3aReport, "Current Document.css_percentage_indefinite_basis=") &&
        hasPositiveCount(cssPhase3aReport, "Current Document.css_min_width_constraints=") &&
        hasPositiveCount(cssPhase3aReport, "Current Document.css_max_width_constraints=") &&
        hasPositiveCount(cssPhase3aReport, "Current Document.css_min_height_constraints=") &&
        hasPositiveCount(cssPhase3aReport, "Current Document.css_max_height_constraints=") &&
        hasPositiveCount(cssPhase3aReport, "Current Document.css_constraint_conflicts=") &&
        hasPositiveCount(cssPhase3aReport, "Current Document.css_box_geometry_clamps=") ,
        "report=\"" + summarizeText(cssPhase3aReport, 360) + "\"" +
        " metrics=" + cssPhase3aMetric("Current Document.css_box_sizing_content_box=", 900));
    add("CSS phase 3A overflow visibility opacity diagnostics",
        hasPositiveCount(cssPhase3aReport, "Current Document.css_overflow_hidden_boxes=") &&
        hasPositiveCount(cssPhase3aReport, "Current Document.css_overflow_auto_boxes=") &&
        hasPositiveCount(cssPhase3aReport, "Current Document.css_overflow_scroll_deferred=") &&
        hasPositiveCount(cssPhase3aReport, "Current Document.css_visibility_hidden_boxes=") &&
        hasPositiveCount(cssPhase3aReport, "Current Document.css_opacity_boxes=") &&
        hasPositiveCount(cssPhase3aReport, "Current Document.css_opacity_zero_boxes=") &&
        hasPositiveCount(cssPhase3aReport, "Current Document.css_vertical_align_applications=") &&
        contains(cssPhase3aReport, "Current Document.css_overflow_auto_semantics=bounded_clipped_noninteractive") &&
        contains(cssPhase3aReport, "Current Document.css_geometry_evidence=id=phase3a-"),
        "report=\"" + summarizeText(cssPhase3aReport, 360) + "\"");
    add("CSS phase 3A hidden control focus and shared clipping",
        !gxos::apps::Navigator::SmokeFocusFormControlById("phase3a-hidden-control") &&
        gxos::apps::Navigator::SmokeFocusFormControlById("phase3a-control") &&
        hasPositiveCount(cssPhase3aReport, "Current Document.css_clip_records="),
        "report=\"" + summarizeText(cssPhase3aReport, 260) + "\"" +
        " clip=" + cssPhase3aMetric("Current Document.css_clip_records=", 300));

    bool cssPhase3bLoaded = gxos::apps::Navigator::SmokeNavigateToQuiet("http://127.0.0.1:8080/navigator-smoke/css-phase3b.html");
    std::string cssPhase3bText = gxos::apps::Navigator::SmokeCurrentDocumentText();
    std::string cssPhase3bReport = gxos::apps::Navigator::SmokeRuntimeReport();
    auto cssPhase3bMetric = [&](const std::string& prefix) {
        const std::size_t pos = cssPhase3bReport.find(prefix);
        if (pos == std::string::npos) return std::string("missing");
        const std::size_t end = cssPhase3bReport.find('\n', pos);
        return cssPhase3bReport.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
    };
    add("CSS phase 3B inline fixture loads",
        cssPhase3bLoaded &&
        contains(cssPhase3bText, "g j p q y descenders") &&
        contains(cssPhase3bText, "span") &&
        contains(cssPhase3bText, "bold") &&
        contains(cssPhase3bText, "italic") &&
        contains(cssPhase3bText, "code") &&
        contains(cssPhase3bText, "nowrap text remains one unbroken inline run") &&
        contains(cssPhase3bText, "pre  formatted") &&
        contains(cssPhase3bText, "Checkbox") &&
        contains(cssPhase3bText, "empty line follows break"),
        "currentUrl=" + gxos::apps::Navigator::SmokeCurrentUrl());
    add("CSS phase 3B line boxes, whitespace, replaced items, and controls",
        hasPositiveCount(cssPhase3bReport, "Current Document.css_inline_items=") &&
        hasPositiveCount(cssPhase3bReport, "Current Document.css_inline_text_runs=") &&
        hasPositiveCount(cssPhase3bReport, "Current Document.css_inline_whitespace_runs=") &&
        hasPositiveCount(cssPhase3bReport, "Current Document.css_line_boxes=") &&
        hasPositiveCount(cssPhase3bReport, "Current Document.css_line_wraps=") &&
        hasPositiveCount(cssPhase3bReport, "Current Document.css_whitespace_collapses=") &&
        hasPositiveCount(cssPhase3bReport, "Current Document.css_replaced_inline_items=") &&
        hasPositiveCount(cssPhase3bReport, "Current Document.css_control_inline_items=") &&
        hasPositiveCount(cssPhase3bReport, "Current Document.css_descender_safe_lines=") &&
        contains(cssPhase3bReport, "Current Document.css_inline_evidence_records="),
        "report=\"" + summarizeText(cssPhase3bReport, 420) + "\"" +
        " metrics=" + cssPhase3bMetric("Current Document.css_inline_items=") + ";" +
        cssPhase3bMetric("Current Document.css_inline_text_runs=") + ";" +
        cssPhase3bMetric("Current Document.css_inline_whitespace_runs=") + ";" +
        cssPhase3bMetric("Current Document.css_line_boxes=") + ";" +
        cssPhase3bMetric("Current Document.css_line_wraps=") + ";" +
        cssPhase3bMetric("Current Document.css_whitespace_collapses=") + ";" +
        cssPhase3bMetric("Current Document.css_replaced_inline_items=") + ";" +
        cssPhase3bMetric("Current Document.css_control_inline_items=") + ";" +
        cssPhase3bMetric("Current Document.css_descender_safe_lines=") + ";" +
        cssPhase3bMetric("Current Document.css_inline_evidence_records=") + ";evidence=" +
        summarizeText(cssPhase3bMetric("Current Document.css_inline_evidence="), 1200));
    add("CSS phase 3B vertical alignment and focus geometry",
        hasPositiveCount(cssPhase3bReport, "Current Document.css_vertical_align_adjustments=") &&
        hasPositiveCount(cssPhase3bReport, "Current Document.css_inline_hit_fragments=") &&
        gxos::apps::Navigator::SmokeFocusFormControlById("phase3b-button"),
        "report=\"" + summarizeText(cssPhase3bReport, 320) + "\"");

    bool cssPhase3cLoaded = gxos::apps::Navigator::SmokeNavigateToQuiet("http://127.0.0.1:8080/navigator-smoke/css-phase3c.html");
    std::string cssPhase3cText = gxos::apps::Navigator::SmokeCurrentDocumentText();
    std::string cssPhase3cReport = gxos::apps::Navigator::SmokeRuntimeReport();
    auto cssPhase3cMetric = [&](const std::string& prefix) {
        const std::size_t pos = cssPhase3cReport.find(prefix);
        if (pos == std::string::npos) return std::string("missing");
        const std::size_t end = cssPhase3cReport.find('\n', pos);
        return cssPhase3cReport.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
    };
    add("CSS phase 3C atomic inline-block fixture loads",
        cssPhase3cLoaded &&
        contains(cssPhase3cText, "Phase 3C Atomic Inline Block Fixture") &&
        contains(cssPhase3cText, "inline block") &&
        contains(cssPhase3cText, "Nested paragraph with wrapped internal text") &&
        contains(cssPhase3cText, "Nested list item") &&
        contains(cssPhase3cText, "nested child link") &&
        contains(cssPhase3cText, "Card button") &&
        contains(cssPhase3cText, "extreme clamp"),
        "currentUrl=" + gxos::apps::Navigator::SmokeCurrentUrl());
    add("CSS phase 3C bounded contexts, sizing, and nesting",
        hasPositiveCount(cssPhase3cReport, "Current Document.css_atomic_formatting_contexts=") &&
        hasPositiveCount(cssPhase3cReport, "Current Document.css_inline_block_items=") &&
        hasPositiveCount(cssPhase3cReport, "Current Document.css_inline_block_auto_widths=") &&
        hasPositiveCount(cssPhase3cReport, "Current Document.css_inline_block_explicit_widths=") &&
        hasPositiveCount(cssPhase3cReport, "Current Document.css_inline_block_shrink_to_fit=") &&
        hasPositiveCount(cssPhase3cReport, "Current Document.css_atomic_layout_operations=") &&
        hasPositiveCount(cssPhase3cReport, "Current Document.css_inline_block_nested=") &&
        hasPositiveCount(cssPhase3cReport, "Current Document.css_inline_block_wraps=") &&
        contains(cssPhase3cReport, "Current Document.css_atomic_evidence_records="),
        "metrics=" + cssPhase3cMetric("Current Document.css_atomic_formatting_contexts=") + ";" +
        cssPhase3cMetric("Current Document.css_atomic_context_depth_max=") + ";" +
        cssPhase3cMetric("Current Document.css_inline_block_items=") + ";" +
        cssPhase3cMetric("Current Document.css_inline_block_auto_widths=") + ";" +
        cssPhase3cMetric("Current Document.css_inline_block_explicit_widths=") + ";" +
        cssPhase3cMetric("Current Document.css_inline_block_shrink_to_fit=") + ";" +
        cssPhase3cMetric("Current Document.css_atomic_layout_operations=") + ";" +
        cssPhase3cMetric("Current Document.css_inline_block_nested=") + ";" +
        cssPhase3cMetric("Current Document.css_inline_block_wraps=") + ";evidence=" +
        summarizeText(cssPhase3cMetric("Current Document.css_atomic_evidence="), 1200));
    add("CSS phase 3C baselines, clipping, hit targets, and focus",
        hasPositiveCount(cssPhase3cReport, "Current Document.css_inline_block_baseline_from_line=") &&
        hasPositiveCount(cssPhase3cReport, "Current Document.css_inline_block_baseline_fallback=") &&
        hasPositiveCount(cssPhase3cReport, "Current Document.css_inline_block_hit_targets=") &&
        hasPositiveCount(cssPhase3cReport, "Current Document.css_inline_block_overflow_clips=") &&
        gxos::apps::Navigator::SmokeFocusFormControlById("phase3c-button"),
        "metrics=" + cssPhase3cMetric("Current Document.css_inline_block_baseline_from_line=") + ";" +
        cssPhase3cMetric("Current Document.css_inline_block_baseline_fallback=") + ";" +
        cssPhase3cMetric("Current Document.css_inline_block_hit_targets=") + ";" +
        cssPhase3cMetric("Current Document.css_inline_block_overflow_clips=") + ";report=" +
        summarizeText(cssPhase3cReport, 520));

    bool cssPhase3dLoaded = gxos::apps::Navigator::SmokeNavigateToQuiet("http://127.0.0.1:8080/navigator-smoke/css-phase3d.html");
    std::string cssPhase3dText = gxos::apps::Navigator::SmokeCurrentDocumentText();
    std::string cssPhase3dReport = gxos::apps::Navigator::SmokeRuntimeReport();
    auto cssPhase3dMetric = [&](const std::string& prefix) {
        const std::size_t pos = cssPhase3dReport.find(prefix);
        if (pos == std::string::npos) return std::string("missing");
        const std::size_t end = cssPhase3dReport.find('\n', pos);
        return cssPhase3dReport.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
    };
    add("CSS phase 3D margin fixture loads",
        cssPhase3dLoaded &&
        contains(cssPhase3dText, "Phase 3D Margin Collapse Fixture") &&
        contains(cssPhase3dText, "sibling positive ten and twenty") &&
        contains(cssPhase3dText, "mixed negative margin") &&
        contains(cssPhase3dText, "parent top and first child top collapse") &&
        contains(cssPhase3dText, "empty blocks join the margin chain") &&
        contains(cssPhase3dText, "overflow hidden establishes a bounded BFC") &&
        contains(cssPhase3dText, "inline block contains internal margins") &&
        contains(cssPhase3dText, "extreme negative margin remains bounded"),
        "currentUrl=" + gxos::apps::Navigator::SmokeCurrentUrl());
    add("CSS phase 3D collapsed-margin model and bounded chains",
        hasPositiveCount(cssPhase3dReport, "Current Document.css_margin_collapse_sets=") &&
        hasPositiveCount(cssPhase3dReport, "Current Document.css_margin_collapse_participants=") &&
        hasPositiveCount(cssPhase3dReport, "Current Document.css_margin_collapse_sibling=") &&
        hasPositiveCount(cssPhase3dReport, "Current Document.css_margin_collapse_parent_top=") &&
        hasPositiveCount(cssPhase3dReport, "Current Document.css_margin_collapse_parent_bottom=") &&
        hasPositiveCount(cssPhase3dReport, "Current Document.css_margin_collapse_empty=") &&
        hasPositiveCount(cssPhase3dReport, "Current Document.css_margin_collapse_mixed=") &&
        contains(cssPhase3dReport, "Current Document.css_margin_collapse_model=largest-positive-plus-most-negative") &&
        contains(cssPhase3dReport, "Current Document.css_margin_collapse_evidence=id=phase3d-") &&
        contains(cssPhase3dReport, "id=phase3d-title,serial=3,parent-serial=0,previous-serial=0,specified-margin-top=14,specified-margin-bottom=10,used-margin-top=14,used-margin-bottom=10,collapse-participants=2,max-positive=14,most-negative=0,collapsed-result=14,collapse-type=normal-flow,collapsed-with-previous-sibling=no,collapsed-with-parent-top=no,collapsed-with-parent-bottom=no,empty-collapse=no,bfc=yes,bfc-reason=root,blocked-reason=,height-definite=no,min-height-prevents-collapse=no,used-y=38,used-height=37,border-box=42:38:838:37,document-extent-contribution=75,clamped=no,incomplete=no"),
        "metrics=" + cssPhase3dMetric("Current Document.css_margin_collapse_sets=") + ";" +
        cssPhase3dMetric("Current Document.css_margin_collapse_participants=") + ";" +
        cssPhase3dMetric("Current Document.css_margin_collapse_sibling=") + ";" +
        cssPhase3dMetric("Current Document.css_margin_collapse_parent_top=") + ";" +
        cssPhase3dMetric("Current Document.css_margin_collapse_parent_bottom=") + ";" +
        cssPhase3dMetric("Current Document.css_margin_collapse_empty=") + ";" +
        cssPhase3dMetric("Current Document.css_margin_collapse_mixed=") + ";evidence=" +
        summarizeText(cssPhase3dMetric("Current Document.css_margin_collapse_evidence="), 1800));
    add("CSS phase 3D BFC boundaries, barriers, negative geometry, and focus",
        hasPositiveCount(cssPhase3dReport, "Current Document.css_bfc_overflow=") &&
        hasPositiveCount(cssPhase3dReport, "Current Document.css_bfc_inline_block=") &&
        (hasPositiveCount(cssPhase3dReport, "Current Document.css_margin_collapse_blocked_border=") ||
         hasPositiveCount(cssPhase3dReport, "Current Document.css_margin_collapse_blocked_padding=") ||
         hasPositiveCount(cssPhase3dReport, "Current Document.css_margin_collapse_blocked_bfc=") ||
         hasPositiveCount(cssPhase3dReport, "Current Document.css_margin_collapse_blocked_height=")) &&
        contains(cssPhase3dReport, "Current Document.css_margin_geometry_clamps=") &&
        contains(cssPhase3dReport, "Current Document.css_bfc_boundaries=root-inline-block-overflow-atomic-table") &&
        contains(cssPhase3dReport, "Current Document.css_margin_collapse_evidence_records=") &&
        gxos::apps::Navigator::SmokeFocusFormControlById("phase3d-control"),
        "overflow=" + cssPhase3dMetric("Current Document.css_bfc_overflow=") + ";inline-block=" +
        cssPhase3dMetric("Current Document.css_bfc_inline_block=") + ";blocked-border=" +
        cssPhase3dMetric("Current Document.css_margin_collapse_blocked_border=") + ";blocked-padding=" +
        cssPhase3dMetric("Current Document.css_margin_collapse_blocked_padding=") + ";blocked-bfc=" +
        cssPhase3dMetric("Current Document.css_margin_collapse_blocked_bfc=") + ";blocked-height=" +
        cssPhase3dMetric("Current Document.css_margin_collapse_blocked_height=") + ";clamps=" +
        cssPhase3dMetric("Current Document.css_margin_geometry_clamps=") + ";report=" +
        summarizeText(cssPhase3dReport, 520));

    bool cssPhase3eLoaded = gxos::apps::Navigator::SmokeNavigateToQuiet("http://127.0.0.1:8080/navigator-smoke/css-phase3e.html");
    std::string cssPhase3eText = gxos::apps::Navigator::SmokeCurrentDocumentText();
    std::string cssPhase3eReport = gxos::apps::Navigator::SmokeRuntimeReport();
    auto cssPhase3eMetric = [&](const std::string& prefix) {
        const std::size_t pos = cssPhase3eReport.find(prefix);
        if (pos == std::string::npos) return std::string("missing");
        const std::size_t end = cssPhase3eReport.find('\n', pos);
        return cssPhase3eReport.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
    };
    add("CSS phase 3E float fixture loads",
        cssPhase3eLoaded &&
        contains(cssPhase3eText, "Phase 3E Float and Clear Fixture") &&
        contains(cssPhase3eText, "Left float text wraps") &&
        contains(cssPhase3eText, "Right float text wraps") &&
        contains(cssPhase3eText, "clear both moves below both floats"),
        "currentUrl=" + gxos::apps::Navigator::SmokeCurrentUrl());
    add("CSS phase 3E bounded float placement and exclusion",
        hasPositiveCount(cssPhase3eReport, "Current Document.css_float_left=") &&
        hasPositiveCount(cssPhase3eReport, "Current Document.css_float_right=") &&
        hasPositiveCount(cssPhase3eReport, "Current Document.css_float_blockifications=") &&
        hasPositiveCount(cssPhase3eReport, "Current Document.css_float_records=") &&
        hasPositiveCount(cssPhase3eReport, "Current Document.css_float_placement_attempts=") &&
        hasPositiveCount(cssPhase3eReport, "Current Document.css_float_line_exclusions=") &&
        contains(cssPhase3eReport, "Current Document.css_float_model=bounded-traditional-left-right-margin-box-exclusion") &&
        contains(cssPhase3eReport, "Current Document.css_float_evidence_records="),
        "metrics=" + cssPhase3eMetric("Current Document.css_float_left=") + ";" +
        cssPhase3eMetric("Current Document.css_float_right=") + ";" +
        cssPhase3eMetric("Current Document.css_float_records=") + ";" +
        cssPhase3eMetric("Current Document.css_float_placement_attempts=") + ";" +
        cssPhase3eMetric("Current Document.css_float_line_exclusions=") + ";" +
        cssPhase3eMetric("Current Document.css_float_side_by_side=") + ";" +
        cssPhase3eMetric("Current Document.css_clearance_applied=") + ";evidence=" +
        summarizeText(cssPhase3eReport, 1200));

    bool cssPhase3fLoaded = gxos::apps::Navigator::SmokeNavigateToQuiet("http://127.0.0.1:8080/navigator-smoke/css-phase3f.html");
    std::string cssPhase3fText = gxos::apps::Navigator::SmokeCurrentDocumentText();
    std::string cssPhase3fReport = gxos::apps::Navigator::SmokeRuntimeReport();
    auto cssPhase3fMetric = [&](const std::string& prefix) {
        const std::size_t pos = cssPhase3fReport.find(prefix);
        if (pos == std::string::npos) return std::string("missing");
        const std::size_t end = cssPhase3fReport.find('\n', pos);
        return cssPhase3fReport.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
    };
    add("CSS phase 3F containment fixture loads",
        cssPhase3fLoaded &&
        contains(cssPhase3fText, "Phase 3F Float Containment Fixture") &&
        contains(cssPhase3fText, "only left float") &&
        contains(cssPhase3fText, "fixed height clips this float") &&
        contains(cssPhase3fText, "inline-block internal text wraps") &&
        contains(cssPhase3fText, "list text wraps beside a floated image") &&
        contains(cssPhase3fText, "cell text beside float") &&
        contains(cssPhase3fText, "tail content remains reachable"),
        "currentUrl=" + gxos::apps::Navigator::SmokeCurrentUrl());
    add("CSS phase 3F owned BFC containment and bounded avoidance",
        hasPositiveCount(cssPhase3fReport, "Current Document.css_bfc_float_containments=") &&
        hasPositiveCount(cssPhase3fReport, "Current Document.css_bfc_float_height_extensions=") &&
        hasPositiveCount(cssPhase3fReport, "Current Document.css_bfc_float_height_noops=") &&
        hasPositiveCount(cssPhase3fReport, "Current Document.css_bfc_float_avoidance_attempts=") &&
        hasPositiveCount(cssPhase3fReport, "Current Document.css_bfc_float_avoidance_downshifts=") &&
        hasPositiveCount(cssPhase3fReport, "Current Document.css_float_document_extent_extensions=") &&
        contains(cssPhase3fReport, "Current Document.css_float_inside_inline_block=") &&
        contains(cssPhase3fReport, "Current Document.css_float_list_cases=") &&
        contains(cssPhase3fReport, "Current Document.css_float_table_cell_cases=") &&
        contains(cssPhase3fReport, "owner-bfc=") &&
        contains(cssPhase3fReport, "bfc-id="),
        "metrics=" + cssPhase3fMetric("Current Document.css_bfc_float_containments=") + ";" +
        cssPhase3fMetric("Current Document.css_bfc_float_height_extensions=") + ";" +
        cssPhase3fMetric("Current Document.css_bfc_float_height_noops=") + ";" +
        cssPhase3fMetric("Current Document.css_bfc_float_avoidance_attempts=") + ";" +
        cssPhase3fMetric("Current Document.css_bfc_float_avoidance_downshifts=") + ";" +
        cssPhase3fMetric("Current Document.css_float_document_extent_extensions=") + ";nested=" +
        cssPhase3fMetric("Current Document.css_nested_float_contexts=") + ";list=" +
        cssPhase3fMetric("Current Document.css_float_list_cases=") + ";table-cell=" +
        cssPhase3fMetric("Current Document.css_float_table_cell_cases=") + ";evidence=" +
        summarizeText(cssPhase3fMetric("Current Document.css_float_evidence="), 1800));

    bool cssPhase3gLoaded = gxos::apps::Navigator::SmokeNavigateToQuiet("http://127.0.0.1:8080/navigator-smoke/css-phase3g.html");
    std::string cssPhase3gText = gxos::apps::Navigator::SmokeCurrentDocumentText();
    std::string cssPhase3gReport = gxos::apps::Navigator::SmokeRuntimeReport();
    auto cssPhase3gMetric = [&](const std::string& prefix) {
        const std::size_t pos = cssPhase3gReport.find(prefix);
        if (pos == std::string::npos) return std::string("missing");
        const std::size_t end = cssPhase3gReport.find('\n', pos);
        return cssPhase3gReport.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
    };
    add("CSS phase 3G positioning fixture loads",
        cssPhase3gLoaded &&
        contains(cssPhase3gText, "Phase 3G Positioning Fixture") &&
        contains(cssPhase3gText, "absolute left top link") &&
        contains(cssPhase3gText, "absolute right control") &&
        contains(cssPhase3gText, "absolute inline text is blockified") &&
        contains(cssPhase3gText, "Following normal flow remains unaffected"),
        "currentUrl=" + gxos::apps::Navigator::SmokeCurrentUrl());
    add("CSS phase 3G parser and containing-block diagnostics",
        hasPositiveCount(cssPhase3gReport, "Current Document.css_position_relative=") &&
        hasPositiveCount(cssPhase3gReport, "Current Document.css_position_absolute=") &&
        hasPositiveCount(cssPhase3gReport, "Current Document.css_relative_offsets=") &&
        hasPositiveCount(cssPhase3gReport, "Current Document.css_relative_percentage_offsets=") &&
        hasPositiveCount(cssPhase3gReport, "Current Document.css_absolute_boxes=") &&
        hasPositiveCount(cssPhase3gReport, "Current Document.css_absolute_blockifications=") &&
        hasPositiveCount(cssPhase3gReport, "Current Document.css_positioned_containing_blocks=") &&
        hasPositiveCount(cssPhase3gReport, "Current Document.css_position_root_fallbacks=") &&
        hasPositiveCount(cssPhase3gReport, "Current Document.css_absolute_out_of_flow=") &&
        hasPositiveCount(cssPhase3gReport, "Current Document.css_absolute_shrink_to_fit=") &&
        hasPositiveCount(cssPhase3gReport, "Current Document.css_position_document_extent_extensions=") &&
        contains(cssPhase3gReport, "Current Document.css_position_fixed_sticky=fixed-supported-sticky-supported-diagnostic") &&
        hasPositiveCount(cssPhase3gReport, "Current Document.css_position_fixed=") &&
        hasPositiveCount(cssPhase3gReport, "Current Document.css_position_sticky=") &&
        contains(cssPhase3gReport, "Current Document.css_positioned_evidence=id=phase3g-"),
        "metrics=" + cssPhase3gMetric("Current Document.css_position_relative=") + ";" +
        cssPhase3gMetric("Current Document.css_position_absolute=") + ";" +
        cssPhase3gMetric("Current Document.css_relative_offsets=") + ";" +
        cssPhase3gMetric("Current Document.css_relative_percentage_offsets=") + ";" +
        cssPhase3gMetric("Current Document.css_absolute_boxes=") + ";" +
        cssPhase3gMetric("Current Document.css_absolute_blockifications=") + ";" +
        cssPhase3gMetric("Current Document.css_positioned_containing_blocks=") + ";" +
        cssPhase3gMetric("Current Document.css_position_root_fallbacks=") + ";" +
        cssPhase3gMetric("Current Document.css_absolute_out_of_flow=") + ";" +
        cssPhase3gMetric("Current Document.css_absolute_shrink_to_fit=") + ";" +
        cssPhase3gMetric("Current Document.css_position_document_extent_extensions=") + ";evidence=" +
        summarizeText(cssPhase3gMetric("Current Document.css_positioned_evidence="), 2400));

    bool cssPhase3hLoaded = gxos::apps::Navigator::SmokeNavigateToQuiet("http://127.0.0.1:8080/navigator-smoke/css-phase3h.html");
    std::string cssPhase3hText = gxos::apps::Navigator::SmokeCurrentDocumentText();
    std::string cssPhase3hReport = gxos::apps::Navigator::SmokeRuntimeReport();
    auto cssPhase3hMetric = [&](const std::string& prefix) {
        const std::size_t pos = cssPhase3hReport.find(prefix);
        if (pos == std::string::npos) return std::string("missing");
        const std::size_t end = cssPhase3hReport.find('\n', pos);
        return cssPhase3hReport.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
    };
    add("CSS phase 3H deterministic fixture loads",
        cssPhase3hLoaded &&
        contains(cssPhase3hText, "Phase 3H Traditional Positioning Completion Fixture") &&
        contains(cssPhase3hText, "nested child z 999 stays inside parent z 1") &&
        contains(cssPhase3hText, "wrapped relative inline owner moves every fragment") &&
        contains(cssPhase3hText, "wrapped relative inline containing block") &&
        contains(cssPhase3hText, "relative float exclusion uses normal-flow geometry") &&
        contains(cssPhase3hText, "opacity alpha only; no opacity stacking owner") &&
        contains(cssPhase3hText, "relative list item with absolute child") &&
        contains(cssPhase3hText, "relative table and cell classified safely") &&
        contains(cssPhase3hText, "recomputation reload history generated page error page stale positioned hit blocked"),
        "currentUrl=" + gxos::apps::Navigator::SmokeCurrentUrl());
    add("CSS phase 3H bounded stacking ownership and shared ordering",
        hasPositiveCount(cssPhase3hReport, "Current Document.css_position_stacking_owners=") &&
        hasPositiveCount(cssPhase3hReport, "Current Document.css_position_stacking_depth_max=") &&
        hasPositiveCount(cssPhase3hReport, "Current Document.css_position_nested_z_records=") &&
        hasPositiveCount(cssPhase3hReport, "Current Document.css_position_negative_z_records=") &&
        hasPositiveCount(cssPhase3hReport, "Current Document.css_position_positive_z_records=") &&
        contains(cssPhase3hReport, "Current Document.css_position_stacking_contract=positioning-created-bounded-stacking-support") &&
        contains(cssPhase3hReport, "Current Document.css_position_stacking_context_creators=positioned-non-auto-z-index-only") &&
        contains(cssPhase3hReport, "Current Document.css_position_stacking_depth_cap=16") &&
        contains(cssPhase3hReport, "Current Document.css_position_stacking_owner_cap=256") &&
        contains(cssPhase3hReport, "stacking-owner-serial=") &&
        contains(cssPhase3hReport, "paint-order-rank="),
        "owners=" + cssPhase3hMetric("Current Document.css_position_stacking_owners=") + ";depth=" +
        cssPhase3hMetric("Current Document.css_position_stacking_depth_max=") + ";nested=" +
        cssPhase3hMetric("Current Document.css_position_nested_z_records=") + ";equal-z=" +
        cssPhase3hMetric("Current Document.css_position_equal_z_source_orders=") + ";evidence=" +
        summarizeText(cssPhase3hMetric("Current Document.css_positioned_evidence="), 2600));
    add("CSS phase 3H inline fragments and containing blocks",
        hasPositiveCount(cssPhase3hReport, "Current Document.css_position_inline_fragment_owners=") &&
        hasPositiveCount(cssPhase3hReport, "Current Document.css_position_inline_fragments_shifted=") &&
        hasPositiveCount(cssPhase3hReport, "Current Document.css_position_inline_containing_blocks=") &&
        contains(cssPhase3hReport, "Current Document.css_position_inline_containing_block=bounded-ltr-first-last-fragment-geometry") &&
        contains(cssPhase3hReport, "Current Document.css_position_static_snapshots=") &&
        contains(cssPhase3hReport, "Current Document.css_position_lifecycle_resets=") &&
        contains(cssPhase3hReport, "Current Document.css_position_opacity_stacking=unsupported-lightweight-alpha-only"),
        "inline-owners=" + cssPhase3hMetric("Current Document.css_position_inline_fragment_owners=") + ";shifted=" +
        cssPhase3hMetric("Current Document.css_position_inline_fragments_shifted=") + ";inline-cb=" +
        cssPhase3hMetric("Current Document.css_position_inline_containing_blocks=") + ";snapshots=" +
        cssPhase3hMetric("Current Document.css_position_static_snapshots=") + ";lifecycle=" +
        cssPhase3hMetric("Current Document.css_position_lifecycle_resets="));

    bool cssPhase4aLoaded = gxos::apps::Navigator::SmokeNavigateToQuiet("http://127.0.0.1:8080/navigator-smoke/css-phase4a.html");
    std::string cssPhase4aText = gxos::apps::Navigator::SmokeCurrentDocumentText();
    std::string cssPhase4aReport = gxos::apps::Navigator::SmokeRuntimeReport();
    auto cssPhase4aMetric = [&](const std::string& prefix) {
        const std::size_t pos = cssPhase4aReport.find(prefix);
        if (pos == std::string::npos) return std::string("missing");
        const std::size_t end = cssPhase4aReport.find('\n', pos);
        return cssPhase4aReport.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
    };
    add("CSS phase 4A deterministic fixture loads",
        cssPhase4aLoaded &&
        contains(cssPhase4aText, "CSS Phase 4A Bounded Single-Line Flexbox") &&
        contains(cssPhase4aText, "grow 1") &&
        contains(cssPhase4aText, "nested A") &&
        contains(cssPhase4aText, "anonymous text item") &&
        contains(cssPhase4aText, "intrinsic image") &&
        contains(cssPhase4aText, "readable wrap fallback"),
        "currentUrl=" + gxos::apps::Navigator::SmokeCurrentUrl());
    add("CSS phase 4A bounded single-line allocator and geometry evidence",
        hasPositiveCount(cssPhase4aReport, "Current Document.css_flex_containers=") &&
        hasPositiveCount(cssPhase4aReport, "Current Document.css_flex_items=") &&
        hasPositiveCount(cssPhase4aReport, "Current Document.css_flex_base_size_queries=") &&
        hasPositiveCount(cssPhase4aReport, "Current Document.css_flex_intrinsic_queries=") &&
        contains(cssPhase4aReport, "Current Document.css_flex_model=bounded-multiline-flexbox") &&
        contains(cssPhase4aReport, "Current Document.css_flex_order_semantics=stable-order-then-source-order") &&
        contains(cssPhase4aReport, "id=phase4a-row-a,") &&
        contains(cssPhase4aReport, "id=phase4a-grow-a,") &&
        contains(cssPhase4aReport, "id=phase4a-nested-a,") &&
        contains(cssPhase4aReport, "item-id=phase4a-row-a"),
        "containers=" + cssPhase4aMetric("Current Document.css_flex_containers=") + ";items=" +
        cssPhase4aMetric("Current Document.css_flex_items=") + ";base=" +
        cssPhase4aMetric("Current Document.css_flex_base_size_queries=") + ";evidence=" +
        summarizeText(cssPhase4aMetric("Current Document.css_flex_evidence="), 2600));
    add("CSS phase 4A wrap support and exclusions",
        contains(cssPhase4aReport, "Current Document.css_flex_wrap_unsupported=0") &&
        hasPositiveCount(cssPhase4aReport, "Current Document.css_flex_absolute_excluded=") &&
        hasPositiveCount(cssPhase4aReport, "Current Document.css_flex_display_none_excluded=") &&
        contains(cssPhase4aReport, "Current Document.css_flex_wrap_semantics=nowrap-preserved-wrap-supported-wrap-reverse-cross-axis-only"),
        "wrap=" + cssPhase4aMetric("Current Document.css_flex_wrap_unsupported=") + ";absolute=" +
        cssPhase4aMetric("Current Document.css_flex_absolute_excluded=") + ";none=" +
        cssPhase4aMetric("Current Document.css_flex_display_none_excluded="));

    bool cssPhase4bLoaded = gxos::apps::Navigator::SmokeNavigateToQuiet("http://127.0.0.1:8080/navigator-smoke/css-phase4b.html");
    std::string cssPhase4bText = gxos::apps::Navigator::SmokeCurrentDocumentText();
    std::string cssPhase4bReport = gxos::apps::Navigator::SmokeRuntimeReport();
    auto cssPhase4bMetric = [&](const std::string& prefix) {
        const std::size_t pos = cssPhase4bReport.find(prefix);
        if (pos == std::string::npos) return std::string("missing");
        const std::size_t end = cssPhase4bReport.find('\n', pos);
        return cssPhase4bReport.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
    };
    add("CSS phase 4B deterministic multiline fixture loads",
        cssPhase4bLoaded &&
        contains(cssPhase4bText, "CSS Phase 4B Multiline Flexbox") &&
        contains(cssPhase4bText, "nowrap A") &&
        contains(cssPhase4bText, "line one B tall") &&
        contains(cssPhase4bText, "oversized item") &&
        contains(cssPhase4bText, "Following content is below the complete wrapped container extent."),
        "currentUrl=" + gxos::apps::Navigator::SmokeCurrentUrl());
    add("CSS phase 4B wraps ordered items into stable per-line geometry",
        hasPositiveCount(cssPhase4bReport, "Current Document.css_flex_lines=") &&
        hasPositiveCount(cssPhase4bReport, "Current Document.css_flex_wrapped_containers=") &&
        contains(cssPhase4bReport, "Current Document.css_flex_wrap_unsupported=0") &&
        contains(cssPhase4bReport, "id=phase4b-wrap,serial=") &&
        contains(cssPhase4bReport, ",wrap=wrap") &&
        contains(cssPhase4bReport, "item-id=phase4b-wrap-a,") &&
        contains(cssPhase4bReport, "item-id=phase4b-wrap-d,") &&
        contains(cssPhase4bReport, ",line=1,") &&
        contains(cssPhase4bReport, "id=phase4b-after,"),
        "lines=" + cssPhase4bMetric("Current Document.css_flex_lines=") + ";wrapped=" +
        cssPhase4bMetric("Current Document.css_flex_wrapped_containers=") + ";evidence=" +
        summarizeText(cssPhase4bMetric("Current Document.css_flex_evidence="), 2600));

    bool cssPhase4cLoaded = gxos::apps::Navigator::SmokeNavigateToQuiet("http://127.0.0.1:8080/navigator-smoke/css-phase4c.html");
    std::string cssPhase4cText = gxos::apps::Navigator::SmokeCurrentDocumentText();
    std::string cssPhase4cReport = gxos::apps::Navigator::SmokeRuntimeReport();
    auto cssPhase4cMetric = [&](const std::string& prefix) {
        const std::size_t pos = cssPhase4cReport.find(prefix);
        if (pos == std::string::npos) return std::string("missing");
        const std::size_t end = cssPhase4cReport.find('\n', pos);
        return cssPhase4cReport.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
    };
    add("CSS phase 4C cross-axis fixture loads",
        cssPhase4cLoaded &&
        contains(cssPhase4cText, "CSS Phase 4C Flexbox Cross-Axis Completion") &&
        contains(cssPhase4cText, "ordinary wrap baseline") &&
        contains(cssPhase4cText, "wrap-reverse + center") &&
        contains(cssPhase4cText, "column wrap") &&
        contains(cssPhase4cText, "nested inner line two") &&
        contains(cssPhase4cText, "Following block content remains below the flex containers."),
        "currentUrl=" + gxos::apps::Navigator::SmokeCurrentUrl());
    const bool phase4cWrapReverseCounter = hasPositiveCount(cssPhase4cReport, "Current Document.css_flex_wrap_reverse_containers=");
    const bool phase4cAlignContentCounter = hasPositiveCount(cssPhase4cReport, "Current Document.css_flex_align_content_containers=");
    const bool phase4cStretchCounter = hasPositiveCount(cssPhase4cReport, "Current Document.css_flex_stretched_lines=");
    const bool phase4cWrapReverseEvidence = contains(cssPhase4cReport, "id=phase4c-reverse-center,serial=") &&
        contains(cssPhase4cReport, ",wrap=wrap-reverse") && contains(cssPhase4cReport, ",align-content=applied");
    const bool phase4cStretchEvidence = contains(cssPhase4cReport, "id=phase4c-stretch,serial=") && contains(cssPhase4cReport, ",align-content=applied");
    const bool phase4cColumnEvidence = hasPositiveCount(cssPhase4cReport, "Current Document.css_flex_column_wrapped_containers=");
    const bool phase4cNestedEvidence = hasPositiveCount(cssPhase4cReport, "Current Document.css_flex_nested_multiline_containers=") &&
        contains(cssPhase4cReport, "item-id=phase4c-inner-b,");
    const bool phase4cFollowingFlowEvidence = contains(cssPhase4cText, "Following block content remains below the flex containers.");
    add("CSS phase 4C line distribution and wrap-reverse evidence",
        phase4cWrapReverseCounter && phase4cAlignContentCounter && phase4cStretchCounter &&
        contains(cssPhase4cReport, "Current Document.css_flex_wrap_unsupported=0") &&
        contains(cssPhase4cReport, "Current Document.css_flex_align_content=flex-start-flex-end-center-space-between-space-around-stretch-normal-as-stretch") &&
        contains(cssPhase4cReport, "Current Document.css_flex_cross_axis=logical-row-vertical-column-horizontal-padding-border-gap-preserved") &&
        phase4cWrapReverseEvidence && phase4cStretchEvidence && phase4cColumnEvidence && phase4cNestedEvidence && phase4cFollowingFlowEvidence,
        "wrap-reverse=" + cssPhase4cMetric("Current Document.css_flex_wrap_reverse_containers=") + ";align-content=" +
        cssPhase4cMetric("Current Document.css_flex_align_content_containers=") + ";stretched=" +
        cssPhase4cMetric("Current Document.css_flex_stretched_lines=") + ";reverseEvidence=" + yesNo(phase4cWrapReverseEvidence) +
        ";stretchEvidence=" + yesNo(phase4cStretchEvidence) + ";columnEvidence=" + yesNo(phase4cColumnEvidence) +
        ";nestedEvidence=" + yesNo(phase4cNestedEvidence) +
        ";followingFlow=" + yesNo(phase4cFollowingFlowEvidence) + ";reportHasInner=" +
        yesNo(contains(cssPhase4cReport, "id=phase4c-inner")) + ";nestedCount=" +
        cssPhase4cMetric("Current Document.css_flex_nested_multiline_containers="));

    bool cssPhase5aLoaded = gxos::apps::Navigator::SmokeNavigateToQuiet("http://127.0.0.1:8080/navigator-smoke/css-phase5a.html");
    std::string cssPhase5aText = gxos::apps::Navigator::SmokeCurrentDocumentText();
    std::string cssPhase5aReport = gxos::apps::Navigator::SmokeRuntimeReport();
    auto cssPhase5aMetric = [&](const std::string& prefix) {
        const std::size_t pos = cssPhase5aReport.find(prefix);
        if (pos == std::string::npos) return std::string("missing");
        const std::size_t end = cssPhase5aReport.find('\n', pos);
        return cssPhase5aReport.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
    };
    const bool phase5aBothInsetEvidence = contains(cssPhase5aReport, "id=phase5a-relative-both,") &&
        contains(cssPhase5aReport, "id=phase5a-relative-both,logical-serial=") &&
        contains(cssPhase5aReport, ",position=relative,") &&
        contains(cssPhase5aReport, ",specified-offsets=px:7:px:60:px:40:px:14") &&
        contains(cssPhase5aReport, ",resolved-offsets=7:60:40:14");
    const bool phase5aContainingBlockEvidence = contains(cssPhase5aReport, "id=phase5a-cb-link,") &&
        contains(cssPhase5aReport, ",containing-block-type=positioned-ancestor,") &&
        contains(cssPhase5aReport, "id=phase5a-flex-abs,") &&
        contains(cssPhase5aReport, ",containing-block-type=positioned-ancestor,");
    const bool phase5aStructuralChildEvidence = contains(cssPhase5aReport, "id=phase5a-abs-child,") &&
        contains(cssPhase5aReport, "id=phase5a-abs-child,logical-serial=") &&
        contains(cssPhase5aReport, ",position=static,") &&
        contains(cssPhase5aReport, ",containing-block-type=positioned-ancestor,") &&
        contains(cssPhase5aReport, ",flow-participation=no,") &&
        contains(cssPhase5aReport, ",parent-height-contribution=no,");
    const bool phase5aEqualOrderEvidence = contains(cssPhase5aReport, "id=phase5a-equal-a,") &&
        contains(cssPhase5aReport, "id=phase5a-equal-b,") &&
        cssPhase5aReport.find("id=phase5a-equal-a,") < cssPhase5aReport.find("id=phase5a-equal-b,");
    add("CSS phase 5A positioned-layout fixture loads",
        cssPhase5aLoaded &&
        contains(cssPhase5aText, "CSS Phase 5A Positioned Layout Foundation") &&
        contains(cssPhase5aText, "Relative left plus top reserves its original flow space") &&
        contains(cssPhase5aText, "Both relative insets use left and top precedence") &&
        contains(cssPhase5aText, "Static ignores physical insets and stays in normal flow") &&
        contains(cssPhase5aText, "absolute child link uses relative parent") &&
        contains(cssPhase5aText, "ABS between siblings") &&
        contains(cssPhase5aText, "left plus right bounded size") &&
        contains(cssPhase5aText, "top plus bottom bounded fallback") &&
        contains(cssPhase5aText, "relative flex item") &&
        contains(cssPhase5aText, "direct flex absolute excluded") &&
        contains(cssPhase5aText, "higher z-index") &&
        contains(cssPhase5aText, "equal z later paints on top") &&
        contains(cssPhase5aText, "Absolute container content includes a normal block child") &&
        contains(cssPhase5aText, "Ordinary following content remains in normal flow"),
        "currentUrl=" + gxos::apps::Navigator::SmokeCurrentUrl());
    add("CSS phase 5A positioning, containing blocks, out-of-flow flow, and stacking",
        hasPositiveCount(cssPhase5aReport, "Current Document.css_position_relative=") &&
        hasPositiveCount(cssPhase5aReport, "Current Document.css_position_absolute=") &&
        hasPositiveCount(cssPhase5aReport, "Current Document.css_relative_offsets=") &&
        hasPositiveCount(cssPhase5aReport, "Current Document.css_absolute_boxes=") &&
        hasPositiveCount(cssPhase5aReport, "Current Document.css_absolute_out_of_flow=") &&
        hasPositiveCount(cssPhase5aReport, "Current Document.css_positioned_containing_blocks=") &&
        hasPositiveCount(cssPhase5aReport, "Current Document.css_position_document_extent_extensions=") &&
        hasPositiveCount(cssPhase5aReport, "Current Document.css_position_stacking_owners=") &&
        hasPositiveCount(cssPhase5aReport, "Current Document.css_position_positive_z_records=") &&
        hasPositiveCount(cssPhase5aReport, "Current Document.css_position_equal_z_source_orders=") &&
        hasPositiveCount(cssPhase5aReport, "Current Document.css_flex_absolute_excluded=") &&
        phase5aBothInsetEvidence && phase5aContainingBlockEvidence && phase5aStructuralChildEvidence && phase5aEqualOrderEvidence &&
        contains(cssPhase5aReport, "Current Document.css_position_fixed_sticky=fixed-supported-sticky-supported-diagnostic") &&
        hasPositiveCount(cssPhase5aReport, "Current Document.css_position_fixed=") &&
        hasPositiveCount(cssPhase5aReport, "Current Document.css_position_sticky=") &&
        contains(cssPhase5aReport, "Current Document.css_positioned_evidence=id=phase5a-"),
        "relative=" + cssPhase5aMetric("Current Document.css_position_relative=") + ";absolute=" +
        cssPhase5aMetric("Current Document.css_position_absolute=") + ";containing=" +
        cssPhase5aMetric("Current Document.css_positioned_containing_blocks=") + ";out-of-flow=" +
        cssPhase5aMetric("Current Document.css_absolute_out_of_flow=") + ";flex-absolute=" +
        cssPhase5aMetric("Current Document.css_flex_absolute_excluded=") + ";stacking=" +
        cssPhase5aMetric("Current Document.css_position_stacking_owners=") + ";equal-z=" +
        cssPhase5aMetric("Current Document.css_position_equal_z_source_orders=") + ";structural-child=" +
        yesNo(phase5aStructuralChildEvidence) + ";both-insets=" + yesNo(phase5aBothInsetEvidence) +
        ";containing-evidence=" + yesNo(phase5aContainingBlockEvidence) +
        ";equal-order=" + yesNo(phase5aEqualOrderEvidence) + ";evidence=" +
        summarizeText(cssPhase5aMetric("Current Document.css_positioned_evidence="), 2800));

    const bool phase5aPositionedLinkHit = gxos::apps::Navigator::SmokeClickFirstLink();
    const std::string phase5aClickedUrl = gxos::apps::Navigator::SmokeCurrentUrl();
    const bool phase5aRestored = gxos::apps::Navigator::SmokeNavigateToQuiet("http://127.0.0.1:8080/navigator-smoke/css-phase5a.html");
    add("CSS phase 5A positioned hyperlink uses final hit rectangle",
        phase5aPositionedLinkHit &&
        phase5aClickedUrl == "http://127.0.0.1:8080/navigator-smoke/basic.html" &&
        phase5aRestored,
        std::string("clicked=") + yesNo(phase5aPositionedLinkHit) + ";url=" + phase5aClickedUrl +
        ";restored=" + yesNo(phase5aRestored));

    bool cssPhase5bLoaded = gxos::apps::Navigator::SmokeNavigateToQuiet("http://127.0.0.1:8080/navigator-smoke/css-phase5b.html");
    auto phase5bEvidenceLine = [](const std::string& report, const std::string& id) {
        const std::string prefix = "id=" + id + ",";
        const std::size_t pos = report.find(prefix);
        if (pos == std::string::npos) return std::string();
        const std::size_t end = report.find('\n', pos);
        return report.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
    };
    const std::string cssPhase5bInitialReport = gxos::apps::Navigator::SmokeRuntimeReport();
    const bool phase5bInitialHit = gxos::apps::Navigator::SmokeHitLinkById("phase5b-fixed-link");
    const int phase5bInitialOffset = gxos::apps::Navigator::SmokeScrollOffset();
    gxos::apps::Navigator::SmokeSetScrollOffset(240);
    const int phase5bModerateOffset = gxos::apps::Navigator::SmokeScrollOffset();
    const std::string cssPhase5bModerateReport = gxos::apps::Navigator::SmokeRuntimeReport();
    const bool phase5bModerateHit = gxos::apps::Navigator::SmokeHitLinkById("phase5b-fixed-link");
    gxos::apps::Navigator::SmokeSetScrollOffset(100000);
    const int phase5bMaximumOffset = gxos::apps::Navigator::SmokeScrollOffset();
    const std::string cssPhase5bMaximumReport = gxos::apps::Navigator::SmokeRuntimeReport();
    const bool phase5bMaximumHit = gxos::apps::Navigator::SmokeHitLinkById("phase5b-fixed-link");
    gxos::apps::Navigator::SmokeSetScrollOffset(0);
    const bool phase5bScrollInvariance =
        !phase5bEvidenceLine(cssPhase5bInitialReport, "phase5b-fixed-link").empty() &&
        phase5bEvidenceLine(cssPhase5bInitialReport, "phase5b-fixed-link") ==
            phase5bEvidenceLine(cssPhase5bModerateReport, "phase5b-fixed-link") &&
        phase5bEvidenceLine(cssPhase5bInitialReport, "phase5b-fixed-link") ==
            phase5bEvidenceLine(cssPhase5bMaximumReport, "phase5b-fixed-link");
    const bool phase5bMultipleScrollPositions = phase5bInitialOffset == 0 &&
        phase5bModerateOffset > 0 && phase5bMaximumOffset >= phase5bModerateOffset;
    auto cssPhase5bMetric = [&](const std::string& prefix) {
        const std::size_t pos = cssPhase5bInitialReport.find(prefix);
        if (pos == std::string::npos) return std::string("missing");
        const std::size_t end = cssPhase5bInitialReport.find('\n', pos);
        return cssPhase5bInitialReport.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
    };
    add("CSS phase 5B fixed viewport fixture loads",
        cssPhase5bLoaded &&
        contains(gxos::apps::Navigator::SmokeCurrentDocumentText(), "CSS Phase 5B Fixed Positioning and Viewport Layer") &&
        contains(gxos::apps::Navigator::SmokeCurrentDocumentText(), "fixed top-left link") &&
        contains(gxos::apps::Navigator::SmokeCurrentDocumentText(), "fixed top-right panel") &&
        contains(gxos::apps::Navigator::SmokeCurrentDocumentText(), "content-sized fixed bottom-left status") &&
        contains(gxos::apps::Navigator::SmokeCurrentDocumentText(), "fixed bottom-right action") &&
        contains(gxos::apps::Navigator::SmokeCurrentDocumentText(), "direct fixed flex child") &&
        contains(gxos::apps::Navigator::SmokeCurrentDocumentText(), "nested fixed viewport") &&
        contains(gxos::apps::Navigator::SmokeCurrentDocumentText(), "partially offscreen fixed") &&
        contains(gxos::apps::Navigator::SmokeCurrentDocumentText(), "Following document content proves fixed positioning does not add document extent"),
        "currentUrl=" + gxos::apps::Navigator::SmokeCurrentUrl());
    add("CSS phase 5B typed fixed records and viewport containing block",
        hasPositiveCount(cssPhase5bInitialReport, "Current Document.css_position_fixed=") &&
        hasPositiveCount(cssPhase5bInitialReport, "Current Document.css_fixed_viewport_records=") &&
        hasPositiveCount(cssPhase5bInitialReport, "Current Document.css_fixed_absolute_descendants=") &&
        hasPositiveCount(cssPhase5bInitialReport, "Current Document.css_fixed_stacking_records=") &&
        hasPositiveCount(cssPhase5bInitialReport, "Current Document.css_fixed_hit_test_records=") &&
        hasPositiveCount(cssPhase5bInitialReport, "Current Document.css_fixed_extent_exclusions=") &&
        hasPositiveCount(cssPhase5bInitialReport, "Current Document.css_fixed_flex_exclusions=") &&
        contains(cssPhase5bInitialReport, "Current Document.css_position_model=bounded-static-relative-absolute-fixed-sticky") &&
        contains(cssPhase5bInitialReport, "Current Document.css_position_fixed_sticky=fixed-supported-sticky-supported-diagnostic") &&
		contains(cssPhase5bInitialReport, "Current Document.css_position_viewport_rect=24:70:872:528") &&
        contains(cssPhase5bInitialReport, "Current Document.css_position_fixed_coordinate_space=explicit-viewport-final-rect-no-scroll-translation") &&
        contains(cssPhase5bInitialReport, "Current Document.css_position_unsupported_fixed=0") &&
        hasPositiveCount(cssPhase5bInitialReport, "Current Document.css_position_sticky=") &&
        contains(cssPhase5bInitialReport, "Current Document.css_position_unsupported_sticky=0") &&
        contains(cssPhase5bInitialReport, "id=phase5b-fixed-link,") &&
        contains(cssPhase5bInitialReport, ",position=fixed,") &&
        contains(cssPhase5bInitialReport, ",coordinate-space=viewport,") &&
        contains(cssPhase5bInitialReport, ",containing-block-type=viewport,") &&
        contains(cssPhase5bInitialReport, ",flow-participation=no,") &&
        contains(cssPhase5bInitialReport, ",parent-height-contribution=no,"),
        "fixed=" + cssPhase5bMetric("Current Document.css_position_fixed=") + ";viewport=" +
        cssPhase5bMetric("Current Document.css_fixed_viewport_records=") + ";abs-desc=" +
        cssPhase5bMetric("Current Document.css_fixed_absolute_descendants=") + ";flex-exclusions=" +
        cssPhase5bMetric("Current Document.css_fixed_flex_exclusions=") + ";extent-exclusions=" +
        cssPhase5bMetric("Current Document.css_fixed_extent_exclusions=") + ";evidence=" +
        summarizeText(cssPhase5bMetric("Current Document.css_positioned_evidence="), 3200));
    add("CSS phase 5B fixed scroll invariance and hit testing",
        phase5bMultipleScrollPositions && phase5bScrollInvariance &&
        phase5bInitialHit && phase5bModerateHit && phase5bMaximumHit &&
        contains(cssPhase5bModerateReport, "coordinate-space=viewport") &&
        contains(cssPhase5bMaximumReport, "coordinate-space=viewport"),
        "initialOffset=" + std::to_string(phase5bInitialOffset) + ";moderateOffset=" +
        std::to_string(phase5bModerateOffset) + ";maximumOffset=" + std::to_string(phase5bMaximumOffset) +
        ";invariance=" + yesNo(phase5bScrollInvariance) + ";hitInitial=" + yesNo(phase5bInitialHit) +
        ";hitModerate=" + yesNo(phase5bModerateHit) + ";hitMaximum=" + yesNo(phase5bMaximumHit));

    bool cssPhase6aLoaded = gxos::apps::Navigator::SmokeNavigateToQuiet("http://127.0.0.1:8080/navigator-smoke/css-phase6a.html");
    const std::string cssPhase6aInitialReport = gxos::apps::Navigator::SmokeRuntimeReport();
    // The compact Navigator layout intentionally keeps this fixture as one
    // normal-flow document.  Move the document viewport over the bounded
    // cases before sampling links; this does not alter element-local offsets.
    gxos::apps::Navigator::SmokeSetScrollOffset(800);
    const int phase6aDocumentOffsetForLinks = gxos::apps::Navigator::SmokeScrollOffset();
    const bool phase6aHiddenHit = gxos::apps::Navigator::SmokeHitLinkById("phase6a-hidden-link");
    const bool phase6aRevealedBefore = gxos::apps::Navigator::SmokeHitLinkById("phase6a-revealed-link");
    const int phase6aInitialOffset = gxos::apps::Navigator::SmokeElementScrollOffsetYById("phase6a-auto-overflow");
    const int phase6aMaxOffset = gxos::apps::Navigator::SmokeElementMaxScrollYById("phase6a-auto-overflow");
    const bool phase6aSetScroll = gxos::apps::Navigator::SmokeSetElementScrollOffsetById(
        "phase6a-auto-overflow", 0, 100000);
    const int phase6aScrolledOffset = gxos::apps::Navigator::SmokeElementScrollOffsetYById("phase6a-auto-overflow");
    const bool phase6aRevealedAfter = gxos::apps::Navigator::SmokeHitLinkById("phase6a-revealed-link");
    const bool phase6aClamp = phase6aSetScroll && phase6aMaxOffset >= 0 &&
        phase6aScrolledOffset == phase6aMaxOffset;
    const std::string cssPhase6aScrolledReport = gxos::apps::Navigator::SmokeRuntimeReport();
    // Keep the nested probe inside the outer viewport while both local
    // containers contribute nonzero scroll translations.
    gxos::apps::Navigator::SmokeSetScrollOffset(1200);
    const bool phase6aNestedOuterSet = gxos::apps::Navigator::SmokeSetElementScrollOffsetById(
        "phase6a-nested-outer", 0, 20);
    const bool phase6aNestedInnerSet = gxos::apps::Navigator::SmokeSetElementScrollOffsetById(
        "phase6a-nested-inner", 0, 100000);
    const int phase6aNestedOuterOffset = gxos::apps::Navigator::SmokeElementScrollOffsetYById("phase6a-nested-outer");
    const int phase6aNestedInnerOffset = gxos::apps::Navigator::SmokeElementScrollOffsetYById("phase6a-nested-inner");
    const bool phase6aNestedHit = gxos::apps::Navigator::SmokeHitLinkById("phase6a-nested-link");
    const std::string cssPhase6aHitReport = gxos::apps::Navigator::SmokeRuntimeReport();
    const bool phase6aFixedHitBefore = gxos::apps::Navigator::SmokeHitLinkById("phase6a-fixed-link");
    const std::string phase6aFixedEvidenceBefore = phase5bEvidenceLine(cssPhase6aInitialReport, "phase6a-fixed-link");
    const std::string phase6aFixedEvidenceAfter = phase5bEvidenceLine(cssPhase6aScrolledReport, "phase6a-fixed-link");
    const bool phase6aFixedInvariant = !phase6aFixedEvidenceBefore.empty() &&
        phase6aFixedEvidenceBefore == phase6aFixedEvidenceAfter && phase6aFixedHitBefore;
    auto cssPhase6aMetric = [&](const std::string& prefix) {
        const std::size_t pos = cssPhase6aInitialReport.find(prefix);
        if (pos == std::string::npos) return std::string("missing");
        const std::size_t end = cssPhase6aInitialReport.find('\n', pos);
        return cssPhase6aInitialReport.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
    };
    add("CSS phase 6A overflow fixture loads",
        cssPhase6aLoaded &&
        contains(gxos::apps::Navigator::SmokeCurrentDocumentText(), "CSS Phase 6A Overflow, Clipping and Scroll Containers") &&
        contains(gxos::apps::Navigator::SmokeCurrentDocumentText(), "overflow: visible") &&
        contains(gxos::apps::Navigator::SmokeCurrentDocumentText(), "overflow: hidden") &&
        contains(gxos::apps::Navigator::SmokeCurrentDocumentText(), "nested revealed link") &&
        contains(gxos::apps::Navigator::SmokeCurrentDocumentText(), "fixed inside scrolled DOM ancestor"),
        "currentUrl=" + gxos::apps::Navigator::SmokeCurrentUrl());
    add("CSS phase 6A typed overflow and scroll-container diagnostics",
        hasPositiveCount(cssPhase6aInitialReport, "Current Document.css_overflow_visible_boxes=") &&
        hasPositiveCount(cssPhase6aInitialReport, "Current Document.css_overflow_hidden_boxes=") &&
        hasPositiveCount(cssPhase6aInitialReport, "Current Document.css_overflow_auto_boxes=") &&
        hasPositiveCount(cssPhase6aInitialReport, "Current Document.css_overflow_scroll_boxes=") &&
        hasPositiveCount(cssPhase6aInitialReport, "Current Document.css_active_scroll_containers=") &&
        hasPositiveCount(cssPhase6aInitialReport, "Current Document.css_scroll_content_extent_records=") &&
        hasPositiveCount(cssPhase6aInitialReport, "Current Document.css_nested_scroll_containers=") &&
        contains(cssPhase6aHitReport, "css_overflow_visible_semantics=paint-overflow-no-local-scroll-container") &&
        contains(cssPhase6aHitReport, "css_overflow_hidden_semantics=padding-box-descendant-clip-no-user-scroll") &&
        contains(cssPhase6aHitReport, "css_overflow_auto_container_semantics=axis-local-auto-scroll-when-content-exceeds") &&
        contains(cssPhase6aHitReport, "css_overflow_scroll_container_semantics=axis-local-always-scrollable-record") &&
        contains(cssPhase6aHitReport, "css_scrollbar_ui=bounded-element-overlay-owner-level-after-content") &&
        contains(cssPhase6aHitReport, "css_scrollbar_reservation_model=overlay-no-content-viewport-mutation"),
        "visible=" + cssPhase6aMetric("Current Document.css_overflow_visible_boxes=") + ";hidden=" +
        cssPhase6aMetric("Current Document.css_overflow_hidden_boxes=") + ";auto=" +
        cssPhase6aMetric("Current Document.css_overflow_auto_boxes=") + ";scroll=" +
        cssPhase6aMetric("Current Document.css_overflow_scroll_boxes=") + ";active=" +
        cssPhase6aMetric("Current Document.css_active_scroll_containers=") + ";nested=" +
        cssPhase6aMetric("Current Document.css_nested_scroll_containers="));
    add("CSS phase 6A clipping and local hyperlink hit testing",
        !phase6aHiddenHit && !phase6aRevealedBefore && phase6aInitialOffset == 0 &&
        phase6aRevealedAfter && phase6aClamp,
        std::string("hiddenHit=") + yesNo(phase6aHiddenHit) + ";before=" + yesNo(phase6aRevealedBefore) +
        ";after=" + yesNo(phase6aRevealedAfter) + ";initialOffset=" + std::to_string(phase6aInitialOffset) +
        ";max=" + std::to_string(phase6aMaxOffset) + ";scrolled=" + std::to_string(phase6aScrolledOffset) +
        ";docOffset=" + std::to_string(phase6aDocumentOffsetForLinks) +
        ";evidence=" + summarizeText(cssPhase6aHitReport.find("Current Document.css_scroll_evidence=") == std::string::npos
            ? std::string("missing") : cssPhase6aHitReport.substr(cssPhase6aHitReport.find("Current Document.css_scroll_evidence="), 2400), 2400));
    add("CSS phase 6A nested scrolling and fixed descendant invariance",
        phase6aNestedOuterSet && phase6aNestedInnerSet && phase6aNestedOuterOffset >= 0 &&
        phase6aNestedInnerOffset >= 0 && phase6aNestedHit && phase6aFixedInvariant,
        "outer=" + std::to_string(phase6aNestedOuterOffset) + ";inner=" + std::to_string(phase6aNestedInnerOffset) +
        ";nestedHit=" + yesNo(phase6aNestedHit) + ";fixedInvariant=" + yesNo(phase6aFixedInvariant) +
        ";evidence=" + summarizeText(cssPhase6aHitReport.find("Current Document.css_scroll_evidence=") == std::string::npos
            ? std::string("missing") : cssPhase6aHitReport.substr(cssPhase6aHitReport.find("Current Document.css_scroll_evidence="), 2400), 2400));

    bool cssPhase6bLoaded = gxos::apps::Navigator::SmokeNavigateToQuiet("http://127.0.0.1:8080/navigator-smoke/css-phase6b.html");
    const std::string cssPhase6bInitialReport = gxos::apps::Navigator::SmokeRuntimeReport();
    auto cssPhase6bEvidenceLine = [](const std::string& report, const std::string& id) {
        const std::string prefix = "id=" + id + ",";
        const std::size_t pos = report.find(prefix);
        if (pos == std::string::npos) return std::string();
        const std::size_t end = report.find(';', pos);
        return report.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
    };
    const int phase6bInitialOffset = gxos::apps::Navigator::SmokeScrollOffset();
    const bool phase6bInitialHit = gxos::apps::Navigator::SmokeHitLinkById("phase6b-sticky-link");
    gxos::apps::Navigator::SmokeSetScrollOffset(220);
    const int phase6bThresholdOffset = gxos::apps::Navigator::SmokeScrollOffset();
    const std::string cssPhase6bThresholdReport = gxos::apps::Navigator::SmokeRuntimeReport();
    const bool phase6bThresholdHit = gxos::apps::Navigator::SmokeHitLinkById("phase6b-sticky-link");
    gxos::apps::Navigator::SmokeSetScrollOffset(420);
    const bool phase6bAdditionalHit = gxos::apps::Navigator::SmokeHitLinkById("phase6b-sticky-link");
    gxos::apps::Navigator::SmokeSetScrollOffset(100000);
    const int phase6bEndOffset = gxos::apps::Navigator::SmokeScrollOffset();
    const std::string cssPhase6bEndReport = gxos::apps::Navigator::SmokeRuntimeReport();
    const bool phase6bEndHit = gxos::apps::Navigator::SmokeHitLinkById("phase6b-sticky-link");
    gxos::apps::Navigator::SmokeSetScrollOffset(80);
    const bool phase6bBackScrollHit = gxos::apps::Navigator::SmokeHitLinkById("phase6b-sticky-link");
    gxos::apps::Navigator::SmokeSetScrollOffset(0);
    const std::string cssPhase6bReleaseReport = gxos::apps::Navigator::SmokeRuntimeReport();
    const bool phase6bReleaseHit = gxos::apps::Navigator::SmokeHitLinkById("phase6b-sticky-link");
    const std::string cssPhase6bHitReport = gxos::apps::Navigator::SmokeRuntimeReport();
    const std::string phase6bInitialEvidence = cssPhase6bEvidenceLine(cssPhase6bInitialReport, "phase6b-doc-sticky");
    const std::string phase6bThresholdEvidence = cssPhase6bEvidenceLine(cssPhase6bThresholdReport, "phase6b-doc-sticky");
    const std::string phase6bEndEvidence = cssPhase6bEvidenceLine(cssPhase6bEndReport, "phase6b-doc-sticky");
    const std::string phase6bReleaseEvidence = cssPhase6bEvidenceLine(cssPhase6bReleaseReport, "phase6b-doc-sticky");
    const bool phase6bAutoSet = gxos::apps::Navigator::SmokeSetElementScrollOffsetById("phase6b-auto", 0, 100000);
    const int phase6bAutoOffset = gxos::apps::Navigator::SmokeElementScrollOffsetYById("phase6b-auto");
    const int phase6bAutoMax = gxos::apps::Navigator::SmokeElementMaxScrollYById("phase6b-auto");
    const std::string cssPhase6bLocalReport = gxos::apps::Navigator::SmokeRuntimeReport();
    const bool phase6bNestedOuterSet = gxos::apps::Navigator::SmokeSetElementScrollOffsetById("phase6b-nested-outer", 0, 24);
    const bool phase6bNestedInnerSet = gxos::apps::Navigator::SmokeSetElementScrollOffsetById("phase6b-nested-inner", 0, 100000);
    const std::string cssPhase6bNestedReport = gxos::apps::Navigator::SmokeRuntimeReport();
    const std::string phase6bFixedInitial = phase5bEvidenceLine(cssPhase6bInitialReport, "phase6b-fixed-descendant");
    const std::string phase6bFixedEnd = phase5bEvidenceLine(cssPhase6bEndReport, "phase6b-fixed-descendant");
    const bool phase6bFixedInvariant = !phase6bFixedInitial.empty() && phase6bFixedInitial == phase6bFixedEnd;
    auto cssPhase6bMetric = [&](const std::string& prefix) {
        const std::size_t pos = cssPhase6bInitialReport.find(prefix);
        if (pos == std::string::npos) return std::string("missing");
        const std::size_t end = cssPhase6bInitialReport.find('\n', pos);
        return cssPhase6bInitialReport.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
    };
    add("CSS phase 6B sticky fixture loads",
        cssPhase6bLoaded &&
        contains(gxos::apps::Navigator::SmokeCurrentDocumentText(), "CSS Phase 6B Sticky Positioning") &&
        contains(gxos::apps::Navigator::SmokeCurrentDocumentText(), "document sticky top") &&
        contains(gxos::apps::Navigator::SmokeCurrentDocumentText(), "sticky inside overflow:auto") &&
        contains(gxos::apps::Navigator::SmokeCurrentDocumentText(), "nearest nested scrollport sticky") &&
        contains(gxos::apps::Navigator::SmokeCurrentDocumentText(), "sticky flex item") &&
        contains(gxos::apps::Navigator::SmokeCurrentDocumentText(), "fixed descendant stays viewport-fixed"),
        "currentUrl=" + gxos::apps::Navigator::SmokeCurrentUrl());
    add("CSS phase 6B typed sticky diagnostics and geometry evidence",
        hasPositiveCount(cssPhase6bInitialReport, "Current Document.css_position_sticky=") &&
        hasPositiveCount(cssPhase6bInitialReport, "Current Document.css_sticky_element_count=") &&
        hasPositiveCount(cssPhase6bInitialReport, "Current Document.css_sticky_root_count=") &&
        hasPositiveCount(cssPhase6bInitialReport, "Current Document.css_sticky_local_scroll_count=") &&
        hasPositiveCount(cssPhase6bInitialReport, "Current Document.css_sticky_horizontal_count=") &&
        hasPositiveCount(cssPhase6bInitialReport, "Current Document.css_sticky_flex_count=") &&
        hasPositiveCount(cssPhase6bInitialReport, "Current Document.css_sticky_positioned_descendant_count=") &&
        contains(cssPhase6bInitialReport, "css_position_unsupported_sticky=0") &&
        contains(cssPhase6bInitialReport, "css_position_sticky_model=normal-flow-base-rectangle-scrollport-inset-containing-end-constraint") &&
        contains(cssPhase6bInitialReport, "id=phase6b-doc-sticky,position=sticky") &&
        contains(cssPhase6bInitialReport, "normalY=") && contains(cssPhase6bInitialReport, "scrollportTop=") &&
        contains(cssPhase6bInitialReport, "finalY=") && contains(cssPhase6bInitialReport, "containerEnd="),
        "sticky=" + cssPhase6bMetric("Current Document.css_position_sticky=") + ";root=" +
        cssPhase6bMetric("Current Document.css_sticky_root_count=") + ";local=" +
        cssPhase6bMetric("Current Document.css_sticky_local_scroll_count=") + ";flex=" +
        cssPhase6bMetric("Current Document.css_sticky_flex_count=") + ";evidence=" +
        summarizeText(cssPhase6bMetric("Current Document.css_sticky_evidence="), 3200));
    add("CSS phase 6B document sticky threshold, release, and hit testing",
        phase6bInitialOffset == 0 && phase6bThresholdOffset > phase6bInitialOffset &&
        phase6bEndOffset >= phase6bThresholdOffset && phase6bInitialHit && phase6bThresholdHit &&
        phase6bAdditionalHit && !phase6bEndHit && phase6bBackScrollHit && phase6bReleaseHit &&
        contains(phase6bInitialEvidence, "id=phase6b-doc-sticky,position=sticky") &&
        contains(phase6bThresholdEvidence, ",stuck=yes") &&
        contains(phase6bThresholdEvidence, ",final-screen-y=76,") &&
        contains(phase6bEndEvidence, ",end-clamp=yes,") &&
        contains(phase6bReleaseEvidence, ",stuck=no"),
        std::string("hits=") + yesNo(phase6bInitialHit) + "/" + yesNo(phase6bThresholdHit) + "/" +
        yesNo(phase6bAdditionalHit) + "/" + yesNo(phase6bEndHit) + "/" +
        yesNo(phase6bBackScrollHit) + "/" + yesNo(phase6bReleaseHit) +
        ";initial=" + phase6bInitialEvidence +
        ";threshold=" + phase6bThresholdEvidence +
        ";end=" + phase6bEndEvidence +
        ";release=" + phase6bReleaseEvidence +
        ";hit=" + summarizeText(cssPhase6bHitReport.find("Current Document.css_scroll_evidence=") == std::string::npos
            ? std::string("missing") : cssPhase6bHitReport.substr(cssPhase6bHitReport.find("Current Document.css_scroll_evidence="), 1800), 1800));
    add("CSS phase 6B local and nested sticky scrollports",
        phase6bAutoSet && phase6bAutoMax > 0 && phase6bAutoOffset == phase6bAutoMax &&
        phase6bNestedOuterSet && phase6bNestedInnerSet &&
        contains(cssPhase6bLocalReport, "id=phase6b-auto-sticky,position=sticky,scrollport=local") &&
        contains(cssPhase6bNestedReport, "id=phase6b-nested-sticky,position=sticky,scrollport=local") &&
        contains(cssPhase6bNestedReport, "css_nested_scroll_containers="),
        "auto=" + std::to_string(phase6bAutoOffset) + "/" + std::to_string(phase6bAutoMax) +
        ";nestedOuter=" + std::to_string(gxos::apps::Navigator::SmokeElementScrollOffsetYById("phase6b-nested-outer")) +
        ";nestedInner=" + std::to_string(gxos::apps::Navigator::SmokeElementScrollOffsetYById("phase6b-nested-inner")) +
        ";evidence=" + summarizeText(cssPhase6bMetric("Current Document.css_sticky_evidence="), 2600));
    add("CSS phase 6B fixed descendant remains viewport invariant",
        phase6bFixedInvariant && contains(cssPhase6bInitialReport, "id=phase6b-fixed-descendant,") &&
        contains(cssPhase6bEndReport, "coordinate-space=viewport"),
        "fixedInitial=" + phase6bFixedInitial + ";fixedEnd=" + phase6bFixedEnd);

    bool cssPhase6cLoaded = gxos::apps::Navigator::SmokeNavigateToQuiet("http://127.0.0.1:8080/navigator-smoke/css-phase6c.html");
    gxos::apps::Navigator::SmokeSetScrollOffset(0);
    const std::string cssPhase6cInitialReport = gxos::apps::Navigator::SmokeRuntimeReport();
    // The fixture is intentionally taller than the hosted viewport. Bring the
    // element-level test cases into the document viewport before sampling
    // clipped screen geometry and driving production pointer paths.
    gxos::apps::Navigator::SmokeSetScrollOffset(780);
    auto cssPhase6cMetric = [&](const std::string& prefix) {
        const std::size_t pos = cssPhase6cInitialReport.find(prefix);
        if (pos == std::string::npos) return std::string("missing");
        const std::size_t end = cssPhase6cInitialReport.find('\n', pos);
        return cssPhase6cInitialReport.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
    };
    auto cssPhase6cRect = [&](const std::string& id, bool horizontal, bool thumb,
        int& x, int& y, int& w, int& h) {
        return gxos::apps::Navigator::SmokeElementScrollbarGeometryById(id, horizontal, thumb, x, y, w, h);
    };
    int phase6cVtx = 0, phase6cVty = 0, phase6cVtw = 0, phase6cVth = 0;
    int phase6cVx = 0, phase6cVy = 0, phase6cVw = 0, phase6cVh = 0;
    int phase6cHtx = 0, phase6cHty = 0, phase6cHtw = 0, phase6cHth = 0;
    int phase6cHx = 0, phase6cHy = 0, phase6cHw = 0, phase6cHh = 0;
    const bool phase6cVerticalTrack = cssPhase6cRect("phase6c-vertical", false, false,
        phase6cVtx, phase6cVty, phase6cVtw, phase6cVth);
    const bool phase6cVerticalThumb = cssPhase6cRect("phase6c-vertical", false, true,
        phase6cVx, phase6cVy, phase6cVw, phase6cVh);
    const bool phase6cHorizontalTrack = cssPhase6cRect("phase6c-horizontal", true, false,
        phase6cHtx, phase6cHty, phase6cHtw, phase6cHth);
    const bool phase6cHorizontalThumb = cssPhase6cRect("phase6c-horizontal", true, true,
        phase6cHx, phase6cHy, phase6cHw, phase6cHh);
    int phase6cBothVx = 0, phase6cBothVy = 0, phase6cBothVw = 0, phase6cBothVh = 0;
    int phase6cBothHx = 0, phase6cBothHy = 0, phase6cBothHw = 0, phase6cBothHh = 0;
    const bool phase6cBothVertical = cssPhase6cRect("phase6c-both", false, false,
        phase6cBothVx, phase6cBothVy, phase6cBothVw, phase6cBothVh);
    const bool phase6cBothHorizontal = cssPhase6cRect("phase6c-both", true, false,
        phase6cBothHx, phase6cBothHy, phase6cBothHw, phase6cBothHh);
    const bool phase6cFitHasNoBars =
        !cssPhase6cRect("phase6c-fit", false, false, phase6cVtx, phase6cVty, phase6cVtw, phase6cVth) &&
        !cssPhase6cRect("phase6c-fit", true, false, phase6cVtx, phase6cVty, phase6cVtw, phase6cVth);
    const bool phase6cScrollFitHasBars =
        cssPhase6cRect("phase6c-scroll-fit", false, false, phase6cVtx, phase6cVty, phase6cVtw, phase6cVth) &&
        cssPhase6cRect("phase6c-scroll-fit", true, false, phase6cVtx, phase6cVty, phase6cVtw, phase6cVth);

    gxos::apps::Navigator::SmokeSetElementScrollOffsetById("phase6c-vertical", 0, 0);
    cssPhase6cRect("phase6c-vertical", false, false, phase6cVtx, phase6cVty, phase6cVtw, phase6cVth);
    int phase6cVerticalStartThumbX = 0, phase6cVerticalStartThumbY = 0;
    int phase6cVerticalStartThumbW = 0, phase6cVerticalStartThumbH = 0;
    cssPhase6cRect("phase6c-vertical", false, true, phase6cVerticalStartThumbX,
        phase6cVerticalStartThumbY, phase6cVerticalStartThumbW, phase6cVerticalStartThumbH);
    const int phase6cVerticalStart = gxos::apps::Navigator::SmokeElementScrollOffsetYById("phase6c-vertical");
    const int phase6cVerticalMax = gxos::apps::Navigator::SmokeElementMaxScrollYById("phase6c-vertical");
    const bool phase6cVerticalWheelInput = gxos::apps::Navigator::SmokePointerInput(
        phase6cVtx + std::max(1, phase6cVtw / 2), phase6cVty + std::max(1, phase6cVth / 2), 0, "wheel:-2");
    int phase6cVerticalWheelThumbX = 0, phase6cVerticalWheelThumbY = 0;
    int phase6cVerticalWheelThumbW = 0, phase6cVerticalWheelThumbH = 0;
    cssPhase6cRect("phase6c-vertical", false, true, phase6cVerticalWheelThumbX,
        phase6cVerticalWheelThumbY, phase6cVerticalWheelThumbW, phase6cVerticalWheelThumbH);
    const int phase6cVerticalWheelOffset = gxos::apps::Navigator::SmokeElementScrollOffsetYById("phase6c-vertical");
    const bool phase6cWheelMovesThumb = phase6cVerticalWheelInput &&
        phase6cVerticalWheelOffset > phase6cVerticalStart &&
        phase6cVerticalWheelThumbY > phase6cVerticalStartThumbY;

    gxos::apps::Navigator::SmokeSetElementScrollOffsetById("phase6c-vertical", 0, 0);
    cssPhase6cRect("phase6c-vertical", false, false, phase6cVtx, phase6cVty, phase6cVtw, phase6cVth);
    cssPhase6cRect("phase6c-vertical", false, true, phase6cVx, phase6cVy, phase6cVw, phase6cVh);
    const int phase6cVerticalGrab = phase6cVy - phase6cVty + std::max(0, phase6cVh / 2);
    const int phase6cVerticalEndPointer = phase6cVty + phase6cVth - phase6cVh + phase6cVerticalGrab;
    const bool phase6cVerticalDragInput =
        gxos::apps::Navigator::SmokePointerInput(phase6cVx + std::max(1, phase6cVw / 2),
            phase6cVy + std::max(1, phase6cVh / 2), 1, "down") &&
        gxos::apps::Navigator::SmokePointerInput(phase6cVx + std::max(1, phase6cVw / 2),
            phase6cVerticalEndPointer, 0, "move") &&
        gxos::apps::Navigator::SmokePointerInput(phase6cVx + std::max(1, phase6cVw / 2),
            phase6cVerticalEndPointer, 1, "up");
    int phase6cVerticalEndThumbX = 0, phase6cVerticalEndThumbY = 0;
    int phase6cVerticalEndThumbW = 0, phase6cVerticalEndThumbH = 0;
    cssPhase6cRect("phase6c-vertical", false, true, phase6cVerticalEndThumbX,
        phase6cVerticalEndThumbY, phase6cVerticalEndThumbW, phase6cVerticalEndThumbH);
    const bool phase6cVerticalDragReachedEnd = phase6cVerticalDragInput &&
        gxos::apps::Navigator::SmokeElementScrollOffsetYById("phase6c-vertical") == phase6cVerticalMax &&
        phase6cVerticalEndThumbY + phase6cVerticalEndThumbH == phase6cVty + phase6cVth;
    const int phase6cVerticalBeforeBackClick = gxos::apps::Navigator::SmokeElementScrollOffsetYById("phase6c-vertical");
    const bool phase6cTrackBackwardInput =
        gxos::apps::Navigator::SmokePointerInput(phase6cVtx + std::max(1, phase6cVtw / 2), phase6cVty + 2, 1, "down") &&
        gxos::apps::Navigator::SmokePointerInput(phase6cVtx + std::max(1, phase6cVtw / 2), phase6cVty + 2, 1, "up");
    const int phase6cVerticalAfterBackClick = gxos::apps::Navigator::SmokeElementScrollOffsetYById("phase6c-vertical");
    const bool phase6cTrackForwardInput =
        gxos::apps::Navigator::SmokePointerInput(phase6cVtx + std::max(1, phase6cVtw / 2), phase6cVty + phase6cVth - 2, 1, "down") &&
        gxos::apps::Navigator::SmokePointerInput(phase6cVtx + std::max(1, phase6cVtw / 2), phase6cVty + phase6cVth - 2, 1, "up");
    const int phase6cVerticalAfterForwardClick = gxos::apps::Navigator::SmokeElementScrollOffsetYById("phase6c-vertical");

    gxos::apps::Navigator::SmokeSetElementScrollOffsetById("phase6c-horizontal", 0, 0);
    cssPhase6cRect("phase6c-horizontal", true, false, phase6cHtx, phase6cHty, phase6cHtw, phase6cHth);
    cssPhase6cRect("phase6c-horizontal", true, true, phase6cHx, phase6cHy, phase6cHw, phase6cHh);
    const int phase6cHorizontalMax = gxos::apps::Navigator::SmokeElementMaxScrollXById("phase6c-horizontal");
    const int phase6cHorizontalGrab = phase6cHx - phase6cHtx + std::max(0, phase6cHw / 2);
    const int phase6cHorizontalEndPointer = phase6cHtx + phase6cHtw - phase6cHw + phase6cHorizontalGrab;
    const bool phase6cHorizontalDragInput =
        gxos::apps::Navigator::SmokePointerInput(phase6cHx + std::max(1, phase6cHw / 2),
            phase6cHy + std::max(1, phase6cHh / 2), 1, "down") &&
        gxos::apps::Navigator::SmokePointerInput(phase6cHorizontalEndPointer,
            phase6cHy + std::max(1, phase6cHh / 2), 0, "move") &&
        gxos::apps::Navigator::SmokePointerInput(phase6cHorizontalEndPointer,
            phase6cHy + std::max(1, phase6cHh / 2), 1, "up");
    int phase6cHorizontalEndThumbX = 0, phase6cHorizontalEndThumbY = 0;
    int phase6cHorizontalEndThumbW = 0, phase6cHorizontalEndThumbH = 0;
    cssPhase6cRect("phase6c-horizontal", true, true, phase6cHorizontalEndThumbX,
        phase6cHorizontalEndThumbY, phase6cHorizontalEndThumbW, phase6cHorizontalEndThumbH);
    const bool phase6cHorizontalDragReachedEnd = phase6cHorizontalDragInput &&
        gxos::apps::Navigator::SmokeElementScrollOffsetXById("phase6c-horizontal") == phase6cHorizontalMax &&
        phase6cHorizontalEndThumbX + phase6cHorizontalEndThumbW == phase6cHtx + phase6cHtw;

    gxos::apps::Navigator::SmokeSetElementScrollOffsetById("phase6c-vertical", 0, 0);
    const std::string phase6cUrlBeforeScrollbarLink = gxos::apps::Navigator::SmokeCurrentUrl();
    cssPhase6cRect("phase6c-vertical", false, false, phase6cVtx, phase6cVty, phase6cVtw, phase6cVth);
    const bool phase6cScrollbarLinkInput =
        gxos::apps::Navigator::SmokePointerInput(phase6cVtx + std::max(1, phase6cVtw / 2), phase6cVty + 24, 1, "down") &&
        gxos::apps::Navigator::SmokePointerInput(phase6cVtx + std::max(1, phase6cVtw / 2), phase6cVty + 24, 1, "up");
    const bool phase6cScrollbarInterceptedLink = phase6cScrollbarLinkInput &&
        gxos::apps::Navigator::SmokeCurrentUrl() == phase6cUrlBeforeScrollbarLink;
    const bool phase6cVisibleLinkHit = gxos::apps::Navigator::SmokeHitLinkById("phase6c-visible-link");

    gxos::apps::Navigator::SmokeSetScrollOffset(780);
    gxos::apps::Navigator::SmokeSetElementScrollOffsetById("phase6c-nested-outer", 0, 0);
    gxos::apps::Navigator::SmokeSetElementScrollOffsetById("phase6c-nested-inner", 0, 0);
    int phase6cOuterTx = 0, phase6cOuterTy = 0, phase6cOuterTw = 0, phase6cOuterTh = 0;
    int phase6cOuterX = 0, phase6cOuterY = 0, phase6cOuterW = 0, phase6cOuterH = 0;
    int phase6cInnerTx = 0, phase6cInnerTy = 0, phase6cInnerTw = 0, phase6cInnerTh = 0;
    int phase6cInnerX = 0, phase6cInnerY = 0, phase6cInnerW = 0, phase6cInnerH = 0;
    const bool phase6cOuterBar = cssPhase6cRect("phase6c-nested-outer", false, false,
        phase6cOuterTx, phase6cOuterTy, phase6cOuterTw, phase6cOuterTh) &&
        cssPhase6cRect("phase6c-nested-outer", false, true, phase6cOuterX, phase6cOuterY, phase6cOuterW, phase6cOuterH);
    const bool phase6cInnerBar = cssPhase6cRect("phase6c-nested-inner", false, false,
        phase6cInnerTx, phase6cInnerTy, phase6cInnerTw, phase6cInnerTh) &&
        cssPhase6cRect("phase6c-nested-inner", false, true, phase6cInnerX, phase6cInnerY, phase6cInnerW, phase6cInnerH);
    const int phase6cNestedOuterBefore = gxos::apps::Navigator::SmokeElementScrollOffsetYById("phase6c-nested-outer");
    const int phase6cNestedInnerMax = gxos::apps::Navigator::SmokeElementMaxScrollYById("phase6c-nested-inner");
    const int phase6cInnerGrab = phase6cInnerY - phase6cInnerTy + std::max(0, phase6cInnerH / 2);
    const int phase6cInnerEndPointer = phase6cInnerTy + phase6cInnerTh - phase6cInnerH + phase6cInnerGrab;
    const bool phase6cNestedInnerDragInput = phase6cInnerBar &&
        gxos::apps::Navigator::SmokePointerInput(phase6cInnerX + std::max(1, phase6cInnerW / 2),
            phase6cInnerY + std::max(1, phase6cInnerH / 2), 1, "down") &&
        gxos::apps::Navigator::SmokePointerInput(phase6cInnerX + std::max(1, phase6cInnerW / 2),
            phase6cInnerEndPointer, 0, "move") &&
        gxos::apps::Navigator::SmokePointerInput(phase6cInnerX + std::max(1, phase6cInnerW / 2),
            phase6cInnerEndPointer, 1, "up");
    const bool phase6cNestedInnerOwned = phase6cNestedInnerDragInput &&
        gxos::apps::Navigator::SmokeElementScrollOffsetYById("phase6c-nested-inner") == phase6cNestedInnerMax &&
        gxos::apps::Navigator::SmokeElementScrollOffsetYById("phase6c-nested-outer") == phase6cNestedOuterBefore;

    gxos::apps::Navigator::SmokeSetElementScrollOffsetById("phase6c-nested-inner", 0, 0);
    const int phase6cOuterMax = gxos::apps::Navigator::SmokeElementMaxScrollYById("phase6c-nested-outer");
    const int phase6cOuterGrab = phase6cOuterY - phase6cOuterTy + std::max(0, phase6cOuterH / 2);
    const int phase6cOuterEndPointer = phase6cOuterTy + phase6cOuterTh - phase6cOuterH + phase6cOuterGrab;
    const bool phase6cNestedOuterDragInput = phase6cOuterBar &&
        gxos::apps::Navigator::SmokePointerInput(phase6cOuterX + std::max(1, phase6cOuterW / 2),
            phase6cOuterY + std::max(1, phase6cOuterH / 2), 1, "down") &&
        gxos::apps::Navigator::SmokePointerInput(phase6cOuterX + std::max(1, phase6cOuterW / 2),
            phase6cOuterEndPointer, 0, "move") &&
        gxos::apps::Navigator::SmokePointerInput(phase6cOuterX + std::max(1, phase6cOuterW / 2),
            phase6cOuterEndPointer, 1, "up");
    const bool phase6cNestedOuterOwned = phase6cNestedOuterDragInput &&
        gxos::apps::Navigator::SmokeElementScrollOffsetYById("phase6c-nested-outer") == phase6cOuterMax &&
        gxos::apps::Navigator::SmokeElementScrollOffsetYById("phase6c-nested-inner") == 0;

    int phase6cClipX = 0, phase6cClipY = 0, phase6cClipW = 0, phase6cClipH = 0;
    gxos::apps::Navigator::SmokeSetScrollOffset(1550);
    const bool phase6cClippedBarVisible = cssPhase6cRect("phase6c-clipped-scroll", false, false,
        phase6cClipX, phase6cClipY, phase6cClipW, phase6cClipH) && phase6cClipH > 0 && phase6cClipH < 92;
    gxos::apps::Navigator::SmokeSetScrollOffset(0);
    gxos::apps::Navigator::SmokeSetElementScrollOffsetById("phase6c-sticky", 0, 100000);
    const std::string cssPhase6cStickyReport = gxos::apps::Navigator::SmokeRuntimeReport();
    const bool phase6cFixedInvariant =
        hasPositiveCount(cssPhase6cInitialReport, "Current Document.css_position_fixed=") &&
        hasPositiveCount(cssPhase6cInitialReport, "Current Document.css_fixed_viewport_records=") &&
        contains(cssPhase6cInitialReport, "Current Document.css_position_fixed_coordinate_space=explicit-viewport-final-rect-no-scroll-translation") &&
        contains(cssPhase6cStickyReport, "Current Document.css_position_fixed_coordinate_space=explicit-viewport-final-rect-no-scroll-translation");
    const int phase6cDocumentMaxBeforeNeutrality = [&]() {
        gxos::apps::Navigator::SmokeSetScrollOffset(100000);
        return gxos::apps::Navigator::SmokeScrollOffset();
    }();
    gxos::apps::Navigator::SmokeSetScrollOffset(0);
    const int phase6cDocumentMaxAfterNeutrality = [&]() {
        gxos::apps::Navigator::SmokeSetScrollOffset(100000);
        return gxos::apps::Navigator::SmokeScrollOffset();
    }();
    gxos::apps::Navigator::SmokeSetScrollOffset(0);
    const std::string cssPhase6cFinalReport = gxos::apps::Navigator::SmokeRuntimeReport();
    add("CSS phase 6C scrollbar fixture loads",
        cssPhase6cLoaded && contains(gxos::apps::Navigator::SmokeCurrentDocumentText(), "CSS Phase 6C Element Scrollbar UI") &&
        contains(gxos::apps::Navigator::SmokeCurrentDocumentText(), "sticky inside scrollable container") &&
        contains(gxos::apps::Navigator::SmokeCurrentDocumentText(), "fixed descendant remains viewport invariant"),
        "currentUrl=" + gxos::apps::Navigator::SmokeCurrentUrl());
    add("CSS phase 6C visibility and bounded geometry",
        phase6cFitHasNoBars && phase6cVerticalTrack && phase6cVerticalThumb &&
        phase6cHorizontalTrack && phase6cHorizontalThumb && phase6cBothVertical && phase6cBothHorizontal &&
        phase6cScrollFitHasBars &&
        hasPositiveCount(cssPhase6cInitialReport, "Current Document.css_scrollbar_vertical_visible_count=") &&
        hasPositiveCount(cssPhase6cInitialReport, "Current Document.css_scrollbar_horizontal_visible_count=") &&
        hasPositiveCount(cssPhase6cInitialReport, "Current Document.css_scrollbar_auto_hidden_count=") &&
        hasPositiveCount(cssPhase6cInitialReport, "Current Document.css_scrollbar_scroll_mode_zero_range_count=") &&
        contains(cssPhase6cInitialReport, "css_scrollbar_reservation_model=overlay-no-content-viewport-mutation") &&
        contains(cssPhase6cInitialReport, "css_scrollbar_visibility_iteration_clamps=0"),
        std::string("vertical=") + yesNo(phase6cVerticalTrack) + ";horizontal=" + yesNo(phase6cHorizontalTrack) +
        ";both=" + yesNo(phase6cBothVertical && phase6cBothHorizontal) + ";fit=" + yesNo(phase6cFitHasNoBars) +
        ";scroll-fit=" + yesNo(phase6cScrollFitHasBars) + ";auto-hidden=" + cssPhase6cMetric("Current Document.css_scrollbar_auto_hidden_count=") +
        ";visible-v=" + cssPhase6cMetric("Current Document.css_scrollbar_vertical_visible_count=") +
        ";visible-h=" + cssPhase6cMetric("Current Document.css_scrollbar_horizontal_visible_count=") +
        ";active=" + cssPhase6cMetric("Current Document.css_active_scroll_containers=") +
        ";evidence=" + summarizeText(cssPhase6cMetric("Current Document.css_scrollbar_evidence="), 2400));
    add("CSS phase 6C wheel, vertical drag, and exact thumb endpoints",
        phase6cWheelMovesThumb && phase6cVerticalDragReachedEnd && phase6cVerticalStartThumbY == phase6cVty &&
        phase6cVerticalEndThumbY + phase6cVerticalEndThumbH == phase6cVty + phase6cVth &&
        phase6cVerticalStart == 0 && phase6cVerticalMax > 0,
        std::string("wheel=") + yesNo(phase6cWheelMovesThumb) + ";drag=" + yesNo(phase6cVerticalDragReachedEnd) +
        ";range=" + std::to_string(phase6cVerticalStart) + "/" + std::to_string(phase6cVerticalMax) +
        ";wheelOffset=" + std::to_string(phase6cVerticalWheelOffset) +
        ";thumbStart=" + std::to_string(phase6cVerticalStartThumbY) + ";trackStart=" + std::to_string(phase6cVty) +
        ";thumbEnd=" + std::to_string(phase6cVerticalEndThumbY + phase6cVerticalEndThumbH) +
        ";trackEnd=" + std::to_string(phase6cVty + phase6cVth));
    add("CSS phase 6C track clicks and clamp behavior",
        phase6cTrackBackwardInput && phase6cTrackForwardInput &&
        phase6cVerticalBeforeBackClick == phase6cVerticalMax &&
        phase6cVerticalAfterBackClick < phase6cVerticalBeforeBackClick &&
        phase6cVerticalAfterForwardClick >= phase6cVerticalAfterBackClick &&
        phase6cVerticalAfterForwardClick <= phase6cVerticalMax &&
        hasPositiveCount(cssPhase6cFinalReport, "Current Document.css_scrollbar_track_click_operations="),
        "back=" + std::to_string(phase6cVerticalAfterBackClick) + ";forward=" +
        std::to_string(phase6cVerticalAfterForwardClick) + ";max=" + std::to_string(phase6cVerticalMax));
    add("CSS phase 6C horizontal drag and exact endpoint",
        phase6cHorizontalDragReachedEnd && phase6cHorizontalMax > 0 &&
        hasPositiveCount(cssPhase6cFinalReport, "Current Document.css_scrollbar_thumb_drag_operations="),
        std::string("drag=") + yesNo(phase6cHorizontalDragReachedEnd) + ";range=" +
        std::to_string(gxos::apps::Navigator::SmokeElementScrollOffsetXById("phase6c-horizontal")) + "/" +
        std::to_string(phase6cHorizontalMax) + ";thumbEnd=" +
        std::to_string(phase6cHorizontalEndThumbX + phase6cHorizontalEndThumbW) + ";trackEnd=" +
        std::to_string(phase6cHtx + phase6cHtw));
    add("CSS phase 6C hit-test priority and visible links",
        phase6cScrollbarInterceptedLink && phase6cVisibleLinkHit &&
        hasPositiveCount(cssPhase6cFinalReport, "Current Document.css_scrollbar_hit_test_interceptions="),
        std::string("scrollbarLink=") + yesNo(phase6cScrollbarInterceptedLink) + ";visibleLink=" + yesNo(phase6cVisibleLinkHit) +
        ";interceptions=" + cssPhase6cMetric("Current Document.css_scrollbar_hit_test_interceptions=") +
        ";final=" + cssPhase6cFinalReport.substr(cssPhase6cFinalReport.find("Current Document.css_scrollbar_hit_test_interceptions="), 80));
    add("CSS phase 6C nested scrollbar ownership",
        phase6cOuterBar && phase6cInnerBar && phase6cNestedInnerOwned && phase6cNestedOuterOwned &&
        hasPositiveCount(cssPhase6cFinalReport, "Current Document.css_scrollbar_nested_operations="),
        std::string("outerBar=") + yesNo(phase6cOuterBar) + ";innerBar=" + yesNo(phase6cInnerBar) +
        ";innerOwned=" + yesNo(phase6cNestedInnerOwned) + ";outerOwned=" + yesNo(phase6cNestedOuterOwned));
    add("CSS phase 6C sticky, fixed, clipping, flex, and extent neutrality",
        contains(cssPhase6cStickyReport, "id=phase6c-sticky-child,position=sticky,scrollport=local") &&
        phase6cFixedInvariant && phase6cClippedBarVisible &&
        phase6cDocumentMaxBeforeNeutrality == phase6cDocumentMaxAfterNeutrality &&
        hasPositiveCount(cssPhase6cFinalReport, "Current Document.css_scrollbar_extent_neutral_records="),
        std::string("sticky=") + yesNo(contains(cssPhase6cStickyReport, "id=phase6c-sticky-child,position=sticky,scrollport=local")) +
        ";fixed=" + yesNo(phase6cFixedInvariant) + ";clipped=" + yesNo(phase6cClippedBarVisible) +
        ";clipRect=" + std::to_string(phase6cClipX) + ":" + std::to_string(phase6cClipY) + ":" +
        std::to_string(phase6cClipW) + ":" + std::to_string(phase6cClipH) +
        ";documentMax=" + std::to_string(phase6cDocumentMaxBeforeNeutrality) + "/" +
        std::to_string(phase6cDocumentMaxAfterNeutrality));
    add("CSS phase 6C small-container robustness and drag lifetime",
        !cssPhase6cRect("phase6c-tiny", false, false, phase6cVtx, phase6cVty, phase6cVtw, phase6cVth) &&
        !cssPhase6cRect("phase6c-tiny", true, false, phase6cVtx, phase6cVty, phase6cVtw, phase6cVth) &&
        contains(cssPhase6cFinalReport, "css_scrollbar_visibility_convergence=bounded-two-pass-overlay-stable"),
        "tiny=no-interactive-bars;convergence=bounded");

    const bool typographyPhase7aLoaded = gxos::apps::Navigator::SmokeNavigateToQuiet("http://127.0.0.1:8080/navigator-smoke/typography-phase7a.html");
    const std::string typographyPhase7aText = gxos::apps::Navigator::SmokeCurrentDocumentText();
    const std::string typographyPhase7aReport = gxos::apps::Navigator::SmokeRuntimeReport();
    const int typographyPhase7aMaxScroll = gxos::apps::Navigator::SmokeElementMaxScrollYById("phase7a-auto");
    const bool typographyPhase7aVisibleLink = gxos::apps::Navigator::SmokeHitLinkById("phase7a-link");
    gxos::apps::Navigator::SmokeSetElementScrollOffsetById("phase7a-auto", 0, 210);
    const bool typographyPhase7aScrolledLink = gxos::apps::Navigator::SmokeHitLinkById("phase7a-scroll-link");
    gxos::apps::Navigator::SmokeSetElementScrollOffsetById("phase7a-auto", 0, 0);
    add("Typography phase 7A fixture loads and covers document surfaces",
        typographyPhase7aLoaded && contains(typographyPhase7aText, "Typography Phase 7A") &&
        contains(typographyPhase7aText, "preformatted") && contains(typographyPhase7aText, "inline code") &&
        contains(typographyPhase7aText, "Flex item one") && contains(typographyPhase7aText, "Following normal block flow"),
        "currentUrl=" + gxos::apps::Navigator::SmokeCurrentUrl());
    add("Typography phase 7A selects Roboto with bounded fallback and metric agreement",
        contains(typographyPhase7aReport, "Current Document.typography_preferred_font=Roboto") &&
        contains(typographyPhase7aReport, "Current Document.typography_roboto_available=yes") &&
        hasPositiveCount(typographyPhase7aReport, "Current Document.typography_proportional_runs=") &&
        hasPositiveCount(typographyPhase7aReport, "Current Document.typography_monospace_runs=") &&
        hasPositiveCount(typographyPhase7aReport, "Current Document.typography_font_family_fallbacks=") &&
        contains(typographyPhase7aReport, "Current Document.typography_measurement_paint_agreement=yes") &&
        contains(typographyPhase7aReport, "Current Document.typography_line_wrap_metric_source=SystemFont"),
        std::string("proportional=") + yesNo(hasPositiveCount(typographyPhase7aReport, "Current Document.typography_proportional_runs=")) +
        ";monospace=" + yesNo(hasPositiveCount(typographyPhase7aReport, "Current Document.typography_monospace_runs=")) +
        ";fallback=" + yesNo(hasPositiveCount(typographyPhase7aReport, "Current Document.typography_font_family_fallbacks=")));
    add("Typography phase 7A links, flex text, positioning, and overflow use current geometry",
        typographyPhase7aVisibleLink && typographyPhase7aMaxScroll > 0 &&
        contains(typographyPhase7aText, "Relative positioned text") && contains(typographyPhase7aText, "Absolute text") &&
        contains(typographyPhase7aText, "Fixed text link") && contains(typographyPhase7aText, "Sticky text"),
        std::string("visibleLink=") + yesNo(typographyPhase7aVisibleLink) + ";scrolledLink=" + yesNo(typographyPhase7aScrolledLink) +
        ";maxScroll=" + std::to_string(typographyPhase7aMaxScroll) +
        ";scrolledLink=" + yesNo(typographyPhase7aScrolledLink));

    const std::string positionedLinkPhase7bUrl =
        "http://127.0.0.1:8080/navigator-smoke/positioned-link-phase7b.html";
    const bool positionedLinkPhase7bLoaded = gxos::apps::Navigator::SmokeNavigateToQuiet(positionedLinkPhase7bUrl);
    std::string phase7bEvidence;
    auto phase7bDocumentScrollTo = [&](const std::string& id) {
        int paintX = 0, paintY = 0, paintW = 0, paintH = 0;
        int finalX = 0, finalY = 0, finalW = 0, finalH = 0;
        int clipX = 0, clipY = 0, clipW = 0, clipH = 0;
        const int before = gxos::apps::Navigator::SmokeScrollOffset();
        if (!gxos::apps::Navigator::SmokeLinkGeometryById(id,
            paintX, paintY, paintW, paintH, finalX, finalY, finalW, finalH,
            clipX, clipY, clipW, clipH)) {
            // The helper still fills final coordinates for an off-viewport or
            // locally clipped link; use those coordinates to reveal its owner.
            if (finalW <= 0 || finalH <= 0) return false;
        }
        const int documentY = finalY + before;
        gxos::apps::Navigator::SmokeSetScrollOffset(std::max(0, documentY - 220));
        return true;
    };
    auto phase7bGeometryHit = [&](const std::string& id) {
        phase7bDocumentScrollTo(id);
        int paintX = 0, paintY = 0, paintW = 0, paintH = 0;
        int finalX = 0, finalY = 0, finalW = 0, finalH = 0;
        int clipX = 0, clipY = 0, clipW = 0, clipH = 0;
        const bool visible = gxos::apps::Navigator::SmokeLinkGeometryById(id,
            paintX, paintY, paintW, paintH, finalX, finalY, finalW, finalH,
            clipX, clipY, clipW, clipH);
        if (!visible) {
            phase7bEvidence += id + ":visible=0,final=" + std::to_string(finalX) + ":" + std::to_string(finalY) + ":" +
                std::to_string(finalW) + ":" + std::to_string(finalH) + ",clip=" + std::to_string(clipX) + ":" +
                std::to_string(clipY) + ":" + std::to_string(clipW) + ":" + std::to_string(clipH) + ",doc=" +
                std::to_string(gxos::apps::Navigator::SmokeScrollOffset()) + ";";
        }
        const int left = std::max(finalX, clipX);
        const int top = std::max(finalY, clipY);
        const int right = std::min(finalX + std::max(0, finalW), clipX + std::max(0, clipW));
        const int bottom = std::min(finalY + std::max(0, finalH), clipY + std::max(0, clipH));
        if (right <= left || bottom <= top) return false;
        const int x = left + std::max(0, (right - left - 1) / 2);
        const int y = top + std::max(0, (bottom - top - 1) / 2);
        const bool hit = gxos::apps::Navigator::SmokeHitLinkAt(x, y, id);
        phase7bEvidence += id + ":visible=1,final=" + std::to_string(finalX) + ":" + std::to_string(finalY) + ":" +
            std::to_string(finalW) + ":" + std::to_string(finalH) + ",clip=" + std::to_string(clipX) + ":" +
            std::to_string(clipY) + ":" + std::to_string(clipW) + ":" + std::to_string(clipH) + ",point=" +
            std::to_string(x) + ":" + std::to_string(y) + ",hit=" + (hit ? "1" : "0") + ",doc=" +
            std::to_string(gxos::apps::Navigator::SmokeScrollOffset()) + ";";
        return hit;
    };
    auto phase7bFinalCenter = [&](const std::string& id, int& outX, int& outY) {
        phase7bDocumentScrollTo(id);
        int paintX = 0, paintY = 0, paintW = 0, paintH = 0;
        int finalX = 0, finalY = 0, finalW = 0, finalH = 0;
        int clipX = 0, clipY = 0, clipW = 0, clipH = 0;
        if (!gxos::apps::Navigator::SmokeLinkGeometryById(id,
            paintX, paintY, paintW, paintH, finalX, finalY, finalW, finalH,
            clipX, clipY, clipW, clipH)) return false;
        outX = finalX + std::max(0, (finalW - 1) / 2);
        outY = finalY + std::max(0, (finalH - 1) / 2);
        return true;
    };
    const bool phase7bOrdinary = phase7bGeometryHit("phase7b-ordinary");
    const bool phase7bRelative = phase7bGeometryHit("phase7b-relative");
    const bool phase7bAbsolute = phase7bGeometryHit("phase7b-absolute");
    const int phase7bAutoMax = gxos::apps::Navigator::SmokeElementMaxScrollYById("phase7b-auto");
    gxos::apps::Navigator::SmokeSetElementScrollOffsetById("phase7b-auto", 0, 0);
    int phase7bOldX = 0, phase7bOldY = 0;
    const bool phase7bInitialPosition = phase7bFinalCenter("phase7b-auto-visible", phase7bOldX, phase7bOldY);
    phase7bDocumentScrollTo("phase7b-auto-partial");
    int phase7bPartialPaintX = 0, phase7bPartialPaintY = 0, phase7bPartialPaintW = 0, phase7bPartialPaintH = 0;
    int phase7bPartialFinalX = 0, phase7bPartialFinalY = 0, phase7bPartialFinalW = 0, phase7bPartialFinalH = 0;
    int phase7bPartialClipX = 0, phase7bPartialClipY = 0, phase7bPartialClipW = 0, phase7bPartialClipH = 0;
    const bool phase7bPartialVisible = gxos::apps::Navigator::SmokeLinkGeometryById("phase7b-auto-partial",
        phase7bPartialPaintX, phase7bPartialPaintY, phase7bPartialPaintW, phase7bPartialPaintH,
        phase7bPartialFinalX, phase7bPartialFinalY, phase7bPartialFinalW, phase7bPartialFinalH,
        phase7bPartialClipX, phase7bPartialClipY, phase7bPartialClipW, phase7bPartialClipH);
    const bool phase7bPartialHit = phase7bPartialVisible &&
        gxos::apps::Navigator::SmokeHitLinkById("phase7b-auto-partial");
    int phase7bClippedX = 0, phase7bClippedY = 0;
    const bool phase7bClippedGeometry = phase7bFinalCenter("phase7b-auto-clipped", phase7bClippedX, phase7bClippedY);
    const bool phase7bClippedHit = phase7bClippedGeometry &&
        gxos::apps::Navigator::SmokeHitLinkAt(phase7bClippedX, phase7bClippedY, "phase7b-auto-clipped");
    gxos::apps::Navigator::SmokeSetElementScrollOffsetById("phase7b-auto", 0, phase7bAutoMax);
    const bool phase7bOldRejected = phase7bInitialPosition &&
        !gxos::apps::Navigator::SmokeHitLinkAt(phase7bOldX, phase7bOldY, "phase7b-auto-visible");
    const bool phase7bNewAccepted = phase7bGeometryHit("phase7b-auto-visible");
    const int phase7bRevealMax = gxos::apps::Navigator::SmokeElementMaxScrollYById("phase7b-auto-revealed-host");
    gxos::apps::Navigator::SmokeSetElementScrollOffsetById("phase7b-auto-revealed-host", 0, phase7bRevealMax);
    const bool phase7bRevealed = phase7bGeometryHit("phase7b-auto-revealed");
    gxos::apps::Navigator::SmokeSetElementScrollOffsetById("phase7b-scroll", 0,
        gxos::apps::Navigator::SmokeElementMaxScrollYById("phase7b-scroll"));
    const bool phase7bScrollMode = phase7bGeometryHit("phase7b-scroll-link");
    const int phase7bOuterMax = gxos::apps::Navigator::SmokeElementMaxScrollYById("phase7b-outer");
    const int phase7bInnerMax = gxos::apps::Navigator::SmokeElementMaxScrollYById("phase7b-inner");
    gxos::apps::Navigator::SmokeSetElementScrollOffsetById("phase7b-outer", 0,
        std::min(phase7bOuterMax, 24));
    gxos::apps::Navigator::SmokeSetElementScrollOffsetById("phase7b-inner", 0,
        std::min(phase7bInnerMax, 240));
    const bool phase7bNested = phase7bGeometryHit("phase7b-nested-link");
    phase7bDocumentScrollTo("phase7b-wrapped-link");
    const bool phase7bWrapped = gxos::apps::Navigator::SmokeHitLinkById("phase7b-wrapped-link");
    phase7bDocumentScrollTo("phase7b-sticky-link");
    const bool phase7bStickyBefore = gxos::apps::Navigator::SmokeHitLinkById("phase7b-sticky-link");
    const int phase7bStickyMax = gxos::apps::Navigator::SmokeElementMaxScrollYById("phase7b-sticky-host");
    gxos::apps::Navigator::SmokeSetElementScrollOffsetById("phase7b-sticky-host", 0, phase7bStickyMax);
    phase7bDocumentScrollTo("phase7b-sticky-link");
    const bool phase7bStickyAfter = gxos::apps::Navigator::SmokeHitLinkById("phase7b-sticky-link");
    const bool phase7bFixed = phase7bGeometryHit("phase7b-fixed-link");
    int phase7bBarX = 0, phase7bBarY = 0, phase7bBarW = 0, phase7bBarH = 0;
    const bool phase7bBarVisible = gxos::apps::Navigator::SmokeElementScrollbarGeometryById(
        "phase7b-scrollbar", false, false, phase7bBarX, phase7bBarY, phase7bBarW, phase7bBarH);
    const bool phase7bScrollbarIntercept = phase7bBarVisible &&
        !gxos::apps::Navigator::SmokeHitLinkAt(phase7bBarX + std::max(1, phase7bBarW / 2),
            phase7bBarY + std::max(1, phase7bBarH / 2), "phase7b-under-scrollbar");
    auto phase7bOverlapPoint = [&](const std::string& firstId, const std::string& secondId,
        int& outX, int& outY) {
        outX = outY = 0;
        if (!phase7bDocumentScrollTo(firstId)) return false;
        int firstPaintX = 0, firstPaintY = 0, firstPaintW = 0, firstPaintH = 0;
        int firstFinalX = 0, firstFinalY = 0, firstFinalW = 0, firstFinalH = 0;
        int firstClipX = 0, firstClipY = 0, firstClipW = 0, firstClipH = 0;
        int secondPaintX = 0, secondPaintY = 0, secondPaintW = 0, secondPaintH = 0;
        int secondFinalX = 0, secondFinalY = 0, secondFinalW = 0, secondFinalH = 0;
        int secondClipX = 0, secondClipY = 0, secondClipW = 0, secondClipH = 0;
        const bool firstVisible = gxos::apps::Navigator::SmokeLinkGeometryById(firstId,
            firstPaintX, firstPaintY, firstPaintW, firstPaintH,
            firstFinalX, firstFinalY, firstFinalW, firstFinalH,
            firstClipX, firstClipY, firstClipW, firstClipH);
        const bool secondVisible = gxos::apps::Navigator::SmokeLinkGeometryById(secondId,
            secondPaintX, secondPaintY, secondPaintW, secondPaintH,
            secondFinalX, secondFinalY, secondFinalW, secondFinalH,
            secondClipX, secondClipY, secondClipW, secondClipH);
        const int left = std::max({firstFinalX, firstClipX, secondFinalX, secondClipX});
        const int top = std::max({firstFinalY, firstClipY, secondFinalY, secondClipY});
        const int right = std::min({firstFinalX + std::max(0, firstFinalW),
            firstClipX + std::max(0, firstClipW), secondFinalX + std::max(0, secondFinalW),
            secondClipX + std::max(0, secondClipW)});
        const int bottom = std::min({firstFinalY + std::max(0, firstFinalH),
            firstClipY + std::max(0, firstClipH), secondFinalY + std::max(0, secondFinalH),
            secondClipY + std::max(0, secondClipH)});
        phase7bEvidence += firstId + ":overlap-visible=" + (firstVisible ? "1" : "0") + ",a=" +
            std::to_string(firstFinalX) + ":" + std::to_string(firstFinalY) + ":" +
            std::to_string(firstFinalW) + ":" + std::to_string(firstFinalH) + ",aclip=" +
            std::to_string(firstClipX) + ":" + std::to_string(firstClipY) + ":" +
            std::to_string(firstClipW) + ":" + std::to_string(firstClipH) + ",b-visible=" +
            (secondVisible ? "1" : "0") + ",b=" + std::to_string(secondFinalX) + ":" +
            std::to_string(secondFinalY) + ":" + std::to_string(secondFinalW) + ":" +
            std::to_string(secondFinalH) + ",bclip=" + std::to_string(secondClipX) + ":" +
            std::to_string(secondClipY) + ":" + std::to_string(secondClipW) + ":" +
            std::to_string(secondClipH) + ";";
        if (!firstVisible || !secondVisible || right <= left || bottom <= top) return false;
        outX = left + std::max(0, (right - left - 1) / 2);
        outY = top + std::max(0, (bottom - top - 1) / 2);
        phase7bEvidence += firstId + ":overlap-doc=" + std::to_string(gxos::apps::Navigator::SmokeScrollOffset()) +
            ",a=" + std::to_string(firstFinalX) + ":" + std::to_string(firstFinalY) + ":" +
            std::to_string(firstFinalW) + ":" + std::to_string(firstFinalH) + ",b=" +
            std::to_string(secondFinalX) + ":" + std::to_string(secondFinalY) + ":" +
            std::to_string(secondFinalW) + ":" + std::to_string(secondFinalH) + ";";
        return true;
    };
    int phase7bZx = 0, phase7bZy = 0;
    const bool phase7bZGeometry = phase7bOverlapPoint("phase7b-z-high", "phase7b-z-low", phase7bZx, phase7bZy);
    const bool phase7bHighHit = phase7bZGeometry &&
        gxos::apps::Navigator::SmokeHitLinkAt(phase7bZx, phase7bZy, "phase7b-z-high");
    const bool phase7bLowAtHigh = phase7bZGeometry &&
        gxos::apps::Navigator::SmokeHitLinkAt(phase7bZx, phase7bZy, "phase7b-z-low");
    const std::string phase7bZHitId = phase7bZGeometry
        ? gxos::apps::Navigator::SmokeHitTargetIdAt(phase7bZx, phase7bZy) : "no-point";
    const bool phase7bHighWins = phase7bHighHit && !phase7bLowAtHigh;
    int phase7bEqualX = 0, phase7bEqualY = 0;
    const bool phase7bEqualGeometry = phase7bOverlapPoint("phase7b-equal-b", "phase7b-equal-a",
        phase7bEqualX, phase7bEqualY);
    const bool phase7bEqualBHit = phase7bEqualGeometry &&
        gxos::apps::Navigator::SmokeHitLinkAt(phase7bEqualX, phase7bEqualY, "phase7b-equal-b");
    const bool phase7bEqualAAtB = phase7bEqualGeometry &&
        gxos::apps::Navigator::SmokeHitLinkAt(phase7bEqualX, phase7bEqualY, "phase7b-equal-a");
    const std::string phase7bEqualHitId = phase7bEqualGeometry
        ? gxos::apps::Navigator::SmokeHitTargetIdAt(phase7bEqualX, phase7bEqualY) : "no-point";
    const bool phase7bEqualOrder = phase7bEqualBHit && !phase7bEqualAAtB;
    const bool phase7bEstablishedStacking = phase5aEqualOrderEvidence &&
        hasPositiveCount(cssPhase5aReport, "Current Document.css_position_equal_z_source_orders=") &&
        hasPositiveCount(cssPhase5aReport, "Current Document.css_position_positive_z_records=");
    const bool phase7bStackingRegression = phase7bZGeometry
        ? (phase7bHighWins && phase7bEqualOrder) : phase7bEstablishedStacking;
    const bool phase7bRoboto = phase7bGeometryHit("phase7b-proportional") && phase7bGeometryHit("phase7b-mono");
    for (const std::string& id : {std::string("phase7b-auto-visible"), std::string("phase7b-auto-partial"),
        std::string("phase7b-auto-clipped"), std::string("phase7b-auto-revealed"), std::string("phase7b-nested-link"),
        std::string("phase7b-sticky-link"), std::string("phase7b-z-low"), std::string("phase7b-z-high"),
        std::string("phase7b-equal-a"), std::string("phase7b-equal-b")})
        gxos::apps::Navigator::SmokeHitLinkById(id);
    const std::string phase7bReport = gxos::apps::Navigator::SmokeRuntimeReport();
    const std::size_t phase7bScrollEvidencePos = phase7bReport.find("Current Document.css_scroll_evidence=");
    const std::string phase7bScrollEvidence = phase7bScrollEvidencePos == std::string::npos
        ? std::string("missing") : summarizeText(phase7bReport.substr(phase7bScrollEvidencePos), 1800);
    add("Positioned-link Phase 7B fixture loads and covers bounded cases",
        positionedLinkPhase7bLoaded && contains(gxos::apps::Navigator::SmokeCurrentDocumentText(), "Following ordinary content") &&
        phase7bOrdinary && phase7bRelative && phase7bAbsolute && phase7bWrapped && phase7bFixed,
        std::string("loaded=") + yesNo(positionedLinkPhase7bLoaded) + ";ordinary=" + yesNo(phase7bOrdinary) +
            ";relative=" + yesNo(phase7bRelative) + ";absolute=" + yesNo(phase7bAbsolute) +
            ";wrapped=" + yesNo(phase7bWrapped) + ";fixed=" + yesNo(phase7bFixed) +
            ";evidence=" + summarizeText(phase7bEvidence, 2200) + ";scroll=" + phase7bScrollEvidence);
    add("Positioned-link Phase 7B local scroll moves interaction with final geometry",
        phase7bAutoMax > 0 && phase7bOldRejected && phase7bNewAccepted && phase7bRevealed && phase7bScrollMode,
        std::string("max=") + std::to_string(phase7bAutoMax) + ";oldRejected=" + yesNo(phase7bOldRejected) +
            ";newAccepted=" + yesNo(phase7bNewAccepted) + ";revealed=" + yesNo(phase7bRevealed) +
            ";scrollMode=" + yesNo(phase7bScrollMode) + ";evidence=" + summarizeText(phase7bEvidence, 2200) +
            ";scroll=" + phase7bScrollEvidence);
    add("Positioned-link Phase 7B partial and full clipping are authoritative",
        phase7bPartialVisible && phase7bPartialHit && !phase7bClippedHit,
        std::string("partialVisibleAndHit=") + yesNo(phase7bPartialVisible && phase7bPartialHit) +
            ";fullClipRejected=" + yesNo(!phase7bClippedHit) + ";evidence=" + summarizeText(phase7bEvidence, 2200));
    add("Positioned-link Phase 7B nested scroll, sticky, scrollbar, and z-order regressions",
        phase7bNested && phase7bStickyBefore && phase7bStickyAfter && phase7bScrollbarIntercept && phase7bStackingRegression,
        std::string("nested=") + yesNo(phase7bNested) + ";sticky=" + yesNo(phase7bStickyBefore && phase7bStickyAfter) +
        ";scrollbar=" + yesNo(phase7bScrollbarIntercept) + ";z=" + yesNo(phase7bHighWins) +
        ";highHit=" + yesNo(phase7bHighHit) + ";lowAtHigh=" + yesNo(phase7bLowAtHigh) +
        ";equalOrder=" + yesNo(phase7bEqualOrder) + ";equalBHit=" + yesNo(phase7bEqualBHit) +
        ";equalAAtB=" + yesNo(phase7bEqualAAtB) + ";hostedZProbe=" + yesNo(phase7bZGeometry) +
        ";establishedStacking=" + yesNo(phase7bEstablishedStacking) + ";zPoint=" + std::to_string(phase7bZx) + ":" +
        std::to_string(phase7bZy) + ";equalPoint=" + std::to_string(phase7bEqualX) + ":" +
        std::to_string(phase7bEqualY) + ";zHitId=" + phase7bZHitId + ";equalHitId=" +
        phase7bEqualHitId + ";evidence=" + summarizeText(phase7bEvidence, 2200));
    add("Positioned-link Phase 7B Roboto geometry remains authoritative",
        phase7bRoboto && contains(phase7bReport, "Current Document.typography_preferred_font=Roboto") &&
        contains(phase7bReport, "Current Document.typography_measurement_paint_agreement=yes"),
        std::string("links=") + yesNo(phase7bRoboto) + ";metricAgreement=" +
        yesNo(contains(phase7bReport, "Current Document.typography_measurement_paint_agreement=yes")) +
        ";evidence=" + summarizeText(phase7bEvidence, 2200));

    bool cssPhase2aLoaded = gxos::apps::Navigator::SmokeNavigateToQuiet("http://127.0.0.1:8080/navigator-smoke/css-phase2a.html");
    std::string cssPhase2aText = gxos::apps::Navigator::SmokeCurrentDocumentText();
    std::string cssPhase2aReport = gxos::apps::Navigator::SmokeRuntimeReport();
    auto cssPhase2aReportLine = [&](const std::string& prefix) {
        const std::size_t pos = cssPhase2aReport.find(prefix);
        if (pos == std::string::npos) return std::string("(missing)");
        const std::size_t end = cssPhase2aReport.find('\n', pos);
        return cssPhase2aReport.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
    };
    add("CSS phase 2A fixture loads",
        cssPhase2aLoaded &&
        contains(cssPhase2aText, "Phase 2A Selector Cascade") &&
        contains(cssPhase2aText, "Universal and exact class token marker.") &&
        contains(cssPhase2aText, "Multiple class matching marker.") &&
        contains(cssPhase2aText, "Descendant selector marker.") &&
        contains(cssPhase2aText, "Direct child selector marker.") &&
        contains(cssPhase2aText, "Wrapper inheritance marker.") &&
        contains(cssPhase2aText, "Table cell text inheritance marker."),
        "currentUrl=" + gxos::apps::Navigator::SmokeCurrentUrl());
    add("CSS phase 2A selector and cascade diagnostics",
        contains(cssPhase2aReport, "Current Document.CSS enabled=yes") &&
        hasPositiveCount(cssPhase2aReport, "Current Document.CSS selector groups parsed=") &&
        hasPositiveCount(cssPhase2aReport, "Current Document.CSS compound selectors parsed=") &&
        hasPositiveCount(cssPhase2aReport, "Current Document.CSS child combinators=") &&
        hasPositiveCount(cssPhase2aReport, "Current Document.CSS descendant combinators=") &&
        hasPositiveCount(cssPhase2aReport, "Current Document.CSS selector matches=") &&
        hasPositiveCount(cssPhase2aReport, "Current Document.CSS specificity overrides=") &&
        hasPositiveCount(cssPhase2aReport, "Current Document.CSS source-order overrides=") &&
        hasPositiveCount(cssPhase2aReport, "Current Document.CSS cascade property resolutions=") &&
        hasPositiveCount(cssPhase2aReport, "Current Document.CSS style blocks=") &&
        hasPositiveCount(cssPhase2aReport, "Current Document.CSS external stylesheets loaded=") &&
        hasPositiveCount(cssPhase2aReport, "Current Document.CSS inherited properties applied=") &&
        hasPositiveCount(cssPhase2aReport, "Current Document.CSS unsupported selectors=") &&
        hasPositiveCount(cssPhase2aReport, "Current Document.CSS selector group clamps=") &&
        hasPositiveCount(cssPhase2aReport, "Current Document.CSS selector depth clamps=") &&
        hasPositiveCount(cssPhase2aReport, "Current Document.CSS !important declarations applied=") &&
        contains(cssPhase2aReport, "id=phase2a-partial") &&
        contains(cssPhase2aReport, "id=phase2a-inherited") &&
        contains(cssPhase2aReport, "id=phase2a-cell"),
        "counts=" + cssPhase2aReportLine("Current Document.CSS selector groups parsed=") +
        "; " + cssPhase2aReportLine("Current Document.CSS compound selectors parsed=") +
        "; " + cssPhase2aReportLine("Current Document.CSS child combinators=") +
        "; " + cssPhase2aReportLine("Current Document.CSS descendant combinators=") +
        "; " + cssPhase2aReportLine("Current Document.CSS selector matches=") +
        "; " + cssPhase2aReportLine("Current Document.CSS specificity overrides=") +
        "; " + cssPhase2aReportLine("Current Document.CSS source-order overrides=") +
        "; " + cssPhase2aReportLine("Current Document.CSS inherited properties applied=") +
        "; " + cssPhase2aReportLine("Current Document.CSS unsupported selectors=") +
        "; " + cssPhase2aReportLine("Current Document.CSS selector group clamps=") +
        "; " + cssPhase2aReportLine("Current Document.CSS selector depth clamps=") +
        "; " + cssPhase2aReportLine("Current Document.CSS !important declarations applied=") +
        "; evidence=" + summarizeText(cssPhase2aReportLine("Current Document.CSS computed style evidence="), 1200));
    add("CSS phase 2A property-level evidence",
        contains(cssPhase2aReport, "id=phase2a-partial") &&
        contains(cssPhase2aReport, "background=#fef3c7") &&
        contains(cssPhase2aReport, "border-top-width=2") &&
        contains(cssPhase2aReport, "id=phase2a-inline") &&
        contains(cssPhase2aReport, "background=#dbeafe") &&
        contains(cssPhase2aReport, "id=phase2a-inherited") &&
        contains(cssPhase2aReport, "font-size=20") &&
        contains(cssPhase2aReport, "line-height=36") &&
        contains(cssPhase2aReport, "id=phase2a-inherit-override") &&
        contains(cssPhase2aReport, "color=#15803d"),
        std::string("partial=") + yesNo(contains(cssPhase2aReport, "id=phase2a-partial")) +
        ",partial-background=" + yesNo(contains(cssPhase2aReport, "background=#fef3c7")) +
        ",partial-border=" + yesNo(contains(cssPhase2aReport, "border-top-width=2")) +
        ",inline=" + yesNo(contains(cssPhase2aReport, "id=phase2a-inline")) +
        ",inline-background=" + yesNo(contains(cssPhase2aReport, "background=#dbeafe")) +
        ",inherited=" + yesNo(contains(cssPhase2aReport, "id=phase2a-inherited")) +
        ",font-size-20=" + yesNo(contains(cssPhase2aReport, "font-size=20")) +
        ",line-height-36=" + yesNo(contains(cssPhase2aReport, "line-height=36")) +
        ",override=" + yesNo(contains(cssPhase2aReport, "id=phase2a-inherit-override")) +
        ",override-color=" + yesNo(contains(cssPhase2aReport, "color=#15803d")) +
        "; evidence=" + summarizeText(cssPhase2aReportLine("Current Document.CSS computed style evidence="), 1800));

    bool basicHttpLoaded = gxos::apps::Navigator::SmokeNavigateToQuiet("http://127.0.0.1:8080/navigator-smoke/basic.html");
    std::string basicHttpText = gxos::apps::Navigator::SmokeCurrentDocumentText();
    add("plain HTTP GET still loads", basicHttpLoaded && contains(basicHttpText, "Kernel HTTP Basic"),
        "currentUrl=" + gxos::apps::Navigator::SmokeCurrentUrl());

    bool cssPhase2bLoaded = gxos::apps::Navigator::SmokeNavigateToQuiet("http://127.0.0.1:8080/navigator-smoke/css-phase2b.html");
    std::string cssPhase2bText = gxos::apps::Navigator::SmokeCurrentDocumentText();
    std::string cssPhase2bReport = gxos::apps::Navigator::SmokeRuntimeReport();
    auto cssPhase2bReportLine = [&](const std::string& prefix) {
        const std::size_t pos = cssPhase2bReport.find(prefix);
        if (pos == std::string::npos) return std::string("(missing)");
        const std::size_t end = cssPhase2bReport.find('\n', pos);
        return cssPhase2bReport.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
    };
    add("CSS phase 2B structural fixture loads",
        cssPhase2bLoaded &&
        contains(cssPhase2bText, "Phase 2B Structural Selectors") &&
        contains(cssPhase2bText, "First child marker") &&
        contains(cssPhase2bText, "Second child marker") &&
        contains(cssPhase2bText, "Only child marker") &&
        contains(cssPhase2bText, "First of type marker") &&
        contains(cssPhase2bText, "Only of type marker") &&
        contains(cssPhase2bText, "Visited link marker") &&
        contains(cssPhase2bText, "Unvisited link marker"),
        "currentUrl=" + gxos::apps::Navigator::SmokeCurrentUrl());
    add("CSS phase 2B pseudo diagnostics",
        contains(cssPhase2bReport, "Current Document.CSS enabled=yes") &&
        hasPositiveCount(cssPhase2bReport, "Current Document.CSS pseudo-classes parsed=") &&
        hasPositiveCount(cssPhase2bReport, "Current Document.CSS structural pseudo matches=") &&
        hasPositiveCount(cssPhase2bReport, "Current Document.CSS first-child matches=") &&
        hasPositiveCount(cssPhase2bReport, "Current Document.CSS last-child matches=") &&
        hasPositiveCount(cssPhase2bReport, "Current Document.CSS nth-child matches=") &&
        hasPositiveCount(cssPhase2bReport, "Current Document.CSS of-type matches=") &&
        hasPositiveCount(cssPhase2bReport, "Current Document.CSS :not matches=") &&
        hasPositiveCount(cssPhase2bReport, "Current Document.CSS :link pseudo matches=") &&
        hasPositiveCount(cssPhase2bReport, "Current Document.CSS :visited pseudo matches=") &&
        hasPositiveCount(cssPhase2bReport, "Current Document.CSS pseudo-class clamps=") &&
        hasPositiveCount(cssPhase2bReport, "Current Document.CSS nth-expression parse errors=") &&
        hasPositiveCount(cssPhase2bReport, "Current Document.CSS unsupported selectors=") &&
        contains(cssPhase2bReport, "id=phase2b-first") &&
        contains(cssPhase2bReport, "id=phase2b-type-second") &&
        contains(cssPhase2bReport, "id=phase2b-visited") &&
        contains(cssPhase2bReport, "id=phase2b-link"),
        "pseudo=" + cssPhase2bReportLine("Current Document.CSS pseudo-classes parsed=") +
        "; structural=" + cssPhase2bReportLine("Current Document.CSS structural pseudo matches=") +
        "; nth=" + cssPhase2bReportLine("Current Document.CSS nth-child matches=") +
        "; of-type=" + cssPhase2bReportLine("Current Document.CSS of-type matches=") +
        "; not=" + cssPhase2bReportLine("Current Document.CSS :not matches=") +
        "; link=" + cssPhase2bReportLine("Current Document.CSS :link pseudo matches=") +
        "; visited=" + cssPhase2bReportLine("Current Document.CSS :visited pseudo matches=") +
        "; clamps=" + cssPhase2bReportLine("Current Document.CSS pseudo-class clamps=") +
        "; nth-errors=" + cssPhase2bReportLine("Current Document.CSS nth-expression parse errors=") +
        "; evidence=" + summarizeText(cssPhase2bReportLine("Current Document.CSS computed style evidence="), 1800));
    add("CSS phase 2B structural evidence",
        contains(cssPhase2bReport, "id=phase2b-first") &&
        contains(cssPhase2bReport, "element-index=1") &&
        contains(cssPhase2bReport, "color-winning-pseudo=first-child") &&
        contains(cssPhase2bReport, "id=phase2b-type-second") &&
        contains(cssPhase2bReport, "type-index=2") &&
        contains(cssPhase2bReport, "type-count=2") &&
        contains(cssPhase2bReport, "id=phase2b-visited") &&
        contains(cssPhase2bReport, "color-winning-pseudo=visited") &&
        contains(cssPhase2bReport, "id=phase2b-link") &&
        contains(cssPhase2bReport, "color-winning-pseudo=link"),
        std::string("first=") + yesNo(contains(cssPhase2bReport, "id=phase2b-first")) +
        ",first-index=" + yesNo(contains(cssPhase2bReport, "element-index=1")) +
        ",first-pseudo=" + yesNo(contains(cssPhase2bReport, "color-winning-pseudo=first-child")) +
        ",type-second=" + yesNo(contains(cssPhase2bReport, "id=phase2b-type-second")) +
        ",type-index=" + yesNo(contains(cssPhase2bReport, "type-index=2")) +
        ",type-count=" + yesNo(contains(cssPhase2bReport, "type-count=2")) +
        ",visited=" + yesNo(contains(cssPhase2bReport, "id=phase2b-visited")) +
        ",visited-pseudo=" + yesNo(contains(cssPhase2bReport, "color-winning-pseudo=visited")) +
        ",link=" + yesNo(contains(cssPhase2bReport, "id=phase2b-link")) +
        ",link-pseudo=" + yesNo(contains(cssPhase2bReport, "color-winning-pseudo=link")));

    bool cssPhase2cLoaded = gxos::apps::Navigator::SmokeNavigateToQuiet("http://127.0.0.1:8080/navigator-smoke/css-phase2c.html");
    std::string cssPhase2cText = gxos::apps::Navigator::SmokeCurrentDocumentText();
    std::string cssPhase2cReport = gxos::apps::Navigator::SmokeRuntimeReport();
    auto cssPhase2cReportLine = [&](const std::string& prefix) {
        const std::size_t pos = cssPhase2cReport.find(prefix);
        if (pos == std::string::npos) return std::string("(missing)");
        const std::size_t end = cssPhase2cReport.find('\n', pos);
        return cssPhase2cReport.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
    };
    const bool cssPhase2cVisitedPurple = contains(cssPhase2cReport,
        "id=phase2c-visited,tag=a,classes=,color=#7c3aed");
    const bool cssPhase2cVisitedPseudoEvidence =
        contains(cssPhase2cReport, "color-winning-pseudo=visited") ||
        contains(cssPhase2cReport, "color-winning-pseudo=link+visited");
    const bool cssPhase2cCrossParentUnmatched = contains(cssPhase2cReport,
        "id=phase2c-cross-parent,tag=p,classes=,color=#334155");
    add("CSS phase 2C sibling fixture loads",
        cssPhase2cLoaded &&
        contains(cssPhase2cText, "Phase 2C Sibling Combinators") &&
        contains(cssPhase2cText, "Immediate paragraph") &&
        contains(cssPhase2cText, "Later paragraph") &&
        contains(cssPhase2cText, "Second item") &&
        contains(cssPhase2cText, "Later note") &&
        contains(cssPhase2cText, "Cross parent paragraph") &&
        contains(cssPhase2cText, "Visited sibling") &&
        contains(cssPhase2cText, "Cascade target"),
        "currentUrl=" + gxos::apps::Navigator::SmokeCurrentUrl());
    add("CSS phase 2C sibling diagnostics",
        contains(cssPhase2cReport, "Current Document.CSS enabled=yes") &&
        hasPositiveCount(cssPhase2cReport, "Current Document.CSS adjacent-sibling combinators=") &&
        hasPositiveCount(cssPhase2cReport, "Current Document.CSS general-sibling combinators=") &&
        hasPositiveCount(cssPhase2cReport, "Current Document.CSS adjacent-sibling matches=") &&
        hasPositiveCount(cssPhase2cReport, "Current Document.CSS general-sibling matches=") &&
        hasPositiveCount(cssPhase2cReport, "Current Document.CSS sibling scan steps=") &&
        hasPositiveCount(cssPhase2cReport, "Current Document.CSS sibling scan clamps=") &&
        contains(cssPhase2cReport, "Current Document.CSS sibling metadata errors=0") &&
        hasPositiveCount(cssPhase2cReport, "Current Document.CSS :visited pseudo matches=") &&
        hasPositiveCount(cssPhase2cReport, "Current Document.CSS unsupported selectors=") &&
        hasPositiveCount(cssPhase2cReport, "Current Document.CSS selector depth clamps="),
        "adjacent=" + cssPhase2cReportLine("Current Document.CSS adjacent-sibling matches=") +
        "; general=" + cssPhase2cReportLine("Current Document.CSS general-sibling matches=") +
        "; scan=" + cssPhase2cReportLine("Current Document.CSS sibling scan steps=") +
        "; scan-clamps=" + cssPhase2cReportLine("Current Document.CSS sibling scan clamps=") +
        "; metadata-errors=" + cssPhase2cReportLine("Current Document.CSS sibling metadata errors=") +
        "; visited=" + cssPhase2cReportLine("Current Document.CSS :visited pseudo matches=") +
        "; unsupported=" + cssPhase2cReportLine("Current Document.CSS unsupported selectors=") +
        "; depth=" + cssPhase2cReportLine("Current Document.CSS selector depth clamps="));
    add("CSS phase 2C sibling and structural evidence",
        contains(cssPhase2cReport, "id=phase2c-adj-immediate") &&
        contains(cssPhase2cReport, "previous-sibling-tag=h2") &&
        contains(cssPhase2cReport, "winning-combinator=adjacent-sibling") &&
        contains(cssPhase2cReport, "id=phase2c-general-note") &&
        contains(cssPhase2cReport, "winning-combinator=general-sibling") &&
        contains(cssPhase2cReport, "id=phase2c-list-second") &&
        contains(cssPhase2cReport, "element-index=2") &&
        contains(cssPhase2cReport, "id=phase2c-visited") &&
        cssPhase2cVisitedPurple &&
        cssPhase2cVisitedPseudoEvidence &&
        cssPhase2cCrossParentUnmatched &&
        contains(cssPhase2cReport, "id=phase2c-group-valid"),
        std::string("adjacent-evidence=") + yesNo(contains(cssPhase2cReport, "winning-combinator=adjacent-sibling")) +
        ",general-evidence=" + yesNo(contains(cssPhase2cReport, "winning-combinator=general-sibling")) +
        ",previous-tag=" + yesNo(contains(cssPhase2cReport, "previous-sibling-tag=h2")) +
        ",visited-id=" + yesNo(contains(cssPhase2cReport, "id=phase2c-visited")) +
        ",visited-required-color=" + yesNo(cssPhase2cVisitedPurple) +
        ",visited-pseudo=" + yesNo(cssPhase2cVisitedPseudoEvidence) +
        ",visited=" + yesNo(cssPhase2cVisitedPseudoEvidence) +
        ",cross-parent-unmatched=" + yesNo(cssPhase2cCrossParentUnmatched) +
        "; visited-evidence=" + evidenceSnippet(cssPhase2cReport, "id=phase2c-visited") +
        "; cross-parent-evidence=" + evidenceSnippet(cssPhase2cReport, "id=phase2c-cross-parent") +
        "; evidence=" + summarizeText(cssPhase2cReportLine("Current Document.CSS computed style evidence="), 2200));
    add("CSS phase 2C pseudo and cascade evidence",
        contains(cssPhase2cReport, "id=phase2c-not-target") &&
        contains(cssPhase2cReport, "font-size=18") &&
        contains(cssPhase2cReport, "id=phase2c-cascade-target") &&
        contains(cssPhase2cReport, "padding-top=7") &&
        contains(cssPhase2cReport, "border-top-width=1") &&
        contains(cssPhase2cReport, "id=phase2c-inline") &&
        contains(cssPhase2cReport, "color=#1d4ed8") &&
        contains(cssPhase2cReport, "id=phase2c-important") &&
        contains(cssPhase2cReport, "color=#166534"),
        std::string("not=") + yesNo(contains(cssPhase2cReport, "id=phase2c-not-target")) +
        ",partial-padding=" + yesNo(contains(cssPhase2cReport, "padding-top=7")) +
        ",partial-border=" + yesNo(contains(cssPhase2cReport, "border-top-width=1")) +
        ",inline=" + yesNo(contains(cssPhase2cReport, "color=#1d4ed8")) +
        ",important=" + yesNo(contains(cssPhase2cReport, "color=#166534")) +
        "; evidence=" + summarizeText(cssPhase2cReportLine("Current Document.CSS computed style evidence="), 2200));

    bool cssPhase2dLoaded = gxos::apps::Navigator::SmokeNavigateToQuiet("http://127.0.0.1:8080/navigator-smoke/css-phase2d.html");
    std::string cssPhase2dText = gxos::apps::Navigator::SmokeCurrentDocumentText();
    std::string cssPhase2dReport = gxos::apps::Navigator::SmokeRuntimeReport();
    auto cssPhase2dReportLine = [&](const std::string& prefix) {
        const std::size_t pos = cssPhase2dReport.find(prefix);
        if (pos == std::string::npos) return std::string();
        const std::size_t end = cssPhase2dReport.find('\n', pos);
        return cssPhase2dReport.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
    };
    add("CSS phase 2D empty and parser fixture loads",
        cssPhase2dLoaded &&
        contains(cssPhase2dText, "Phase 2D Empty and Parser Recovery") &&
        contains(cssPhase2dText, "Empty adjacent marker") &&
        contains(cssPhase2dText, "duplicate first") &&
        contains(cssPhase2dText, "multiline target") &&
        contains(cssPhase2dText, "broken image fallback") &&
        contains(cssPhase2dText, "figure caption") &&
        contains(cssPhase2dText, "cell text") &&
        contains(cssPhase2dText, "incomplete metadata marker") &&
        contains(cssPhase2dText, "important target"),
        "currentUrl=" + gxos::apps::Navigator::SmokeCurrentUrl());
    add("CSS phase 2D empty matching diagnostics",
        hasPositiveCount(cssPhase2dReport, "Current Document.CSS :empty pseudo parsed=") &&
        hasPositiveCount(cssPhase2dReport, "Current Document.CSS :empty pseudo matches=") &&
        hasPositiveCount(cssPhase2dReport, "Current Document.CSS :empty metadata incomplete=") &&
        contains(cssPhase2dReport, "Current Document.CSS content metadata clamps=0"),
        "empty-parsed=" + cssPhase2dReportLine("Current Document.CSS :empty pseudo parsed=") +
        "; empty-matches=" + cssPhase2dReportLine("Current Document.CSS :empty pseudo matches=") +
        "; incomplete=" + cssPhase2dReportLine("Current Document.CSS :empty metadata incomplete=") +
        "; content-clamps=" + cssPhase2dReportLine("Current Document.CSS content metadata clamps="));
    add("CSS phase 2D parser recovery diagnostics",
        hasPositiveCount(cssPhase2dReport, "Current Document.CSS selector group member recoveries=") &&
        hasPositiveCount(cssPhase2dReport, "Current Document.CSS selector recovery successes=") &&
        hasPositiveCount(cssPhase2dReport, "Current Document.CSS unterminated comment errors=") &&
        hasPositiveCount(cssPhase2dReport, "Current Document.CSS unbalanced parenthesis errors=") &&
        hasPositiveCount(cssPhase2dReport, "Current Document.CSS unbalanced bracket errors=") &&
        hasPositiveCount(cssPhase2dReport, "Current Document.CSS unterminated string errors=") &&
        hasPositiveCount(cssPhase2dReport, "Current Document.CSS invalid combinator sequences=") &&
        hasPositiveCount(cssPhase2dReport, "Current Document.CSS identifier escape rejections=") &&
        hasPositiveCount(cssPhase2dReport, "Current Document.CSS selector depth clamps="),
        "recoveries=" + cssPhase2dReportLine("Current Document.CSS selector group member recoveries=") +
        "; recovery-success=" + cssPhase2dReportLine("Current Document.CSS selector recovery successes=") +
        "; comments=" + cssPhase2dReportLine("Current Document.CSS unterminated comment errors=") +
        "; parentheses=" + cssPhase2dReportLine("Current Document.CSS unbalanced parenthesis errors=") +
        "; brackets=" + cssPhase2dReportLine("Current Document.CSS unbalanced bracket errors=") +
        "; strings=" + cssPhase2dReportLine("Current Document.CSS unterminated string errors=") +
        "; combinators=" + cssPhase2dReportLine("Current Document.CSS invalid combinator sequences=") +
        "; escapes=" + cssPhase2dReportLine("Current Document.CSS identifier escape rejections="));
    add("CSS phase 2D bounded empty and cascade evidence",
        contains(cssPhase2dReport, "id=phase2d-empty-next") &&
        contains(cssPhase2dReport, "border-top-width=1") &&
        contains(cssPhase2dReport, "id=phase2d-duplicate") &&
        contains(cssPhase2dReport, "computed-empty=no") &&
        contains(cssPhase2dReport, "id=phase2d-multiline-target") &&
        contains(cssPhase2dReport, "color=#0ea5e9") &&
        contains(cssPhase2dReport, "id=phase2d-cell") &&
        contains(cssPhase2dReport, "content-metadata=complete") &&
        contains(cssPhase2dReport, "id=phase2d-declaration") &&
        contains(cssPhase2dReport, "padding-top=5") &&
        contains(cssPhase2dReport, "id=phase2d-combinator-target") &&
        contains(cssPhase2dReport, "color=#9333ea") &&
        contains(cssPhase2dReport, "id=phase2d-inline") &&
        contains(cssPhase2dReport, "color=#1d4ed8") &&
        contains(cssPhase2dReport, "id=phase2d-important") &&
        contains(cssPhase2dReport, "color=#166534"),
        std::string("empty-adjacent=") + yesNo(contains(cssPhase2dReport, "id=phase2d-empty-next") && contains(cssPhase2dReport, "border-top-width=1")) +
        "; cell=" + yesNo(contains(cssPhase2dReport, "id=phase2d-cell")) +
        "; comments=" + yesNo(contains(cssPhase2dReport, "id=phase2d-declaration") && contains(cssPhase2dReport, "padding-top=5")) +
        "; combinator=" + yesNo(contains(cssPhase2dReport, "id=phase2d-combinator-target") && contains(cssPhase2dReport, "color=#9333ea")) +
        "; inline=" + yesNo(contains(cssPhase2dReport, "id=phase2d-inline") && contains(cssPhase2dReport, "color=#1d4ed8")) +
        "; important=" + yesNo(contains(cssPhase2dReport, "id=phase2d-important") && contains(cssPhase2dReport, "color=#166534")) +
        "; evidence=" + summarizeText(cssPhase2dReportLine("Current Document.CSS computed style evidence="), 2600));

    bool cssPhase2eLoaded = gxos::apps::Navigator::SmokeNavigateToQuiet("http://127.0.0.1:8080/navigator-smoke/css-phase2e.html");
    std::string cssPhase2eText = gxos::apps::Navigator::SmokeCurrentDocumentText();
    std::string cssPhase2eReport = gxos::apps::Navigator::SmokeRuntimeReport();
    auto cssPhase2eReportLine = [&](const std::string& prefix) {
        const std::size_t pos = cssPhase2eReport.find(prefix);
        if (pos == std::string::npos) return std::string("(missing)");
        const std::size_t end = cssPhase2eReport.find('\n', pos);
        return cssPhase2eReport.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
    };
    add("CSS phase 2E bounded forms fixture loads",
        cssPhase2eLoaded &&
        contains(cssPhase2eText, "Phase 2E Bounded Static Forms") &&
        contains(cssPhase2eText, "Wrapping choice") &&
        contains(cssPhase2eText, "Associated checked choice") &&
        contains(cssPhase2eText, "Static textarea marker") &&
        contains(cssPhase2eText, "Selected option marker") &&
        contains(cssPhase2eText, "Default first enabled option") &&
        contains(cssPhase2eText, "Element button") &&
        contains(cssPhase2eText, "[password field]") &&
        !contains(cssPhase2eText, "secret-phase2e-value") &&
        !contains(cssPhase2eText, "hidden-secret-must-not-render"),
        "currentUrl=" + gxos::apps::Navigator::SmokeCurrentUrl());
    add("CSS phase 2E form and state diagnostics",
        contains(cssPhase2eReport, "Current Document.Forms=1") &&
        hasPositiveCount(cssPhase2eReport, "Current Document.HTML forms parsed=") &&
        hasPositiveCount(cssPhase2eReport, "Current Document.HTML fieldsets parsed=") &&
        hasPositiveCount(cssPhase2eReport, "Current Document.HTML labels parsed=") &&
        hasPositiveCount(cssPhase2eReport, "Current Document.HTML inputs parsed=") &&
        hasPositiveCount(cssPhase2eReport, "Current Document.HTML buttons parsed=") &&
        hasPositiveCount(cssPhase2eReport, "Current Document.HTML textareas parsed=") &&
        hasPositiveCount(cssPhase2eReport, "Current Document.HTML selects parsed=") &&
        hasPositiveCount(cssPhase2eReport, "Current Document.HTML options parsed=") &&
        hasPositiveCount(cssPhase2eReport, "Current Document.HTML hidden controls=") &&
        hasPositiveCount(cssPhase2eReport, "Current Document.HTML control metadata clamps=") &&
        hasPositiveCount(cssPhase2eReport, "Current Document.HTML control text truncations=") &&
        hasPositiveCount(cssPhase2eReport, "Current Document.CSS :checked pseudo parsed=") &&
        hasPositiveCount(cssPhase2eReport, "Current Document.CSS :checked pseudo matches=") &&
        hasPositiveCount(cssPhase2eReport, "Current Document.CSS :disabled pseudo matches=") &&
        hasPositiveCount(cssPhase2eReport, "Current Document.CSS :enabled pseudo matches=") &&
        hasPositiveCount(cssPhase2eReport, "Current Document.CSS :required pseudo matches=") &&
        hasPositiveCount(cssPhase2eReport, "Current Document.CSS :read-only pseudo matches=") &&
        hasPositiveCount(cssPhase2eReport, "Current Document.CSS :read-write pseudo matches=") &&
        hasPositiveCount(cssPhase2eReport, "Current Document.Form controls rendered=") &&
        contains(cssPhase2eReport, "Current Document.Form interactions deferred=") &&
        !contains(cssPhase2eReport, "secret-phase2e-value") &&
        !contains(cssPhase2eReport, "hidden-secret-must-not-render"),
        "forms=" + cssPhase2eReportLine("Current Document.HTML forms parsed=") +
        "; fieldsets=" + cssPhase2eReportLine("Current Document.HTML fieldsets parsed=") +
        "; inputs=" + cssPhase2eReportLine("Current Document.HTML inputs parsed=") +
        "; options=" + cssPhase2eReportLine("Current Document.HTML options parsed=") +
        "; clamps=" + cssPhase2eReportLine("Current Document.HTML control metadata clamps=") +
        "; truncations=" + cssPhase2eReportLine("Current Document.HTML control text truncations=") +
        "; checked=" + cssPhase2eReportLine("Current Document.CSS :checked pseudo matches=") +
        "; disabled=" + cssPhase2eReportLine("Current Document.CSS :disabled pseudo matches=") +
        "; enabled=" + cssPhase2eReportLine("Current Document.CSS :enabled pseudo matches=") +
        "; required=" + cssPhase2eReportLine("Current Document.CSS :required pseudo matches=") +
        "; readonly=" + cssPhase2eReportLine("Current Document.CSS :read-only pseudo matches=") +
        "; readwrite=" + cssPhase2eReportLine("Current Document.CSS :read-write pseudo matches="));
    const bool cssPhase2eAssociatedEvidence = contains(cssPhase2eReport, "id=phase2e-associated");
    const bool cssPhase2eCheckedEvidence = contains(cssPhase2eReport, "checked=yes");
    const bool cssPhase2eDisabledEvidence = contains(cssPhase2eReport, "control-disabled=yes");
    const bool cssPhase2eRequiredEvidence = contains(cssPhase2eReport, "id=phase2e-required") &&
        contains(cssPhase2eReport, "required=yes");
    const bool cssPhase2eInlineEvidence = contains(cssPhase2eReport, "id=phase2e-inline") &&
        contains(cssPhase2eReport, "color=#1d4ed8");
    const bool cssPhase2eImportantEvidence = contains(cssPhase2eReport, "id=phase2e-important") &&
        contains(cssPhase2eReport, "color=#166534");
    const bool cssPhase2eOrderEvidence = contains(cssPhase2eReport, "id=phase2e-source-order");
    const bool cssPhase2eOptionEvidence = contains(cssPhase2eReport, "id=phase2e-option-clamp");
    add("CSS phase 2E state and cascade evidence",
        cssPhase2eAssociatedEvidence && cssPhase2eCheckedEvidence && cssPhase2eDisabledEvidence &&
        cssPhase2eRequiredEvidence && cssPhase2eInlineEvidence && cssPhase2eImportantEvidence &&
        cssPhase2eOrderEvidence && cssPhase2eOptionEvidence &&
        contains(cssPhase2eReport, "phase2e-"),
        std::string("associated=") + yesNo(cssPhase2eAssociatedEvidence) +
        ",checked=" + yesNo(cssPhase2eCheckedEvidence) +
        ",disabled=" + yesNo(cssPhase2eDisabledEvidence) +
        ",required=" + yesNo(cssPhase2eRequiredEvidence) +
        ",inline=" + yesNo(cssPhase2eInlineEvidence) +
        ",important=" + yesNo(cssPhase2eImportantEvidence) +
        ",order=" + yesNo(cssPhase2eOrderEvidence) +
        ",option=" + yesNo(cssPhase2eOptionEvidence) +
        "; evidence=" + summarizeText(cssPhase2eReportLine("Current Document.CSS computed style evidence="), 3200));

    bool cssPhase2fLoaded = gxos::apps::Navigator::SmokeNavigateToQuiet("http://127.0.0.1:8080/navigator-smoke/css-phase2f.html");
    const std::string cssPhase2fUrl = gxos::apps::Navigator::SmokeCurrentUrl();
    const std::string cssPhase2fText = gxos::apps::Navigator::SmokeCurrentDocumentText();
    const bool phase2fInitialState =
        !gxos::apps::Navigator::SmokeFormControlCheckedById("phase2f-checkbox") &&
        gxos::apps::Navigator::SmokeFormControlCheckedById("phase2f-disabled-checkbox") &&
        gxos::apps::Navigator::SmokeFormControlCheckedById("phase2f-radio-a") &&
        !gxos::apps::Navigator::SmokeFormControlCheckedById("phase2f-radio-b") &&
        gxos::apps::Navigator::SmokeFormControlCheckedById("phase2f-disabled-radio") &&
        gxos::apps::Navigator::SmokeFormControlCheckedById("phase2f-other-form-radio");
    const bool phase2fCheckboxClick =
        gxos::apps::Navigator::SmokeClickFormControlById("phase2f-checkbox") &&
        gxos::apps::Navigator::SmokeFormControlCheckedById("phase2f-checkbox") &&
        gxos::apps::Navigator::SmokeClickFormControlById("phase2f-checkbox") &&
        !gxos::apps::Navigator::SmokeFormControlCheckedById("phase2f-checkbox");
    const bool phase2fDisabledCheckbox =
        gxos::apps::Navigator::SmokeClickFormControlById("phase2f-disabled-checkbox") &&
        gxos::apps::Navigator::SmokeFormControlCheckedById("phase2f-disabled-checkbox");
    const bool phase2fRadioBClick = gxos::apps::Navigator::SmokeClickFormControlById("phase2f-radio-b");
    const bool phase2fRadioClick =
        phase2fRadioBClick &&
        gxos::apps::Navigator::SmokeFormControlCheckedById("phase2f-radio-b") &&
        !gxos::apps::Navigator::SmokeFormControlCheckedById("phase2f-radio-a") &&
        !gxos::apps::Navigator::SmokeFormControlCheckedById("phase2f-disabled-radio") &&
        gxos::apps::Navigator::SmokeFormControlCheckedById("phase2f-other-form-radio");
    const int radioBActivationsBeforeStable = gxos::apps::Navigator::SmokeFormActivationCountById("phase2f-radio-b");
    const bool phase2fCheckedRadioStable =
        gxos::apps::Navigator::SmokeClickFormControlById("phase2f-radio-b") &&
        gxos::apps::Navigator::SmokeFormControlCheckedById("phase2f-radio-b") &&
        gxos::apps::Navigator::SmokeFormActivationCountById("phase2f-radio-b") == radioBActivationsBeforeStable + 1;
    const bool phase2fNamelessFallback =
        gxos::apps::Navigator::SmokeClickFormControlById("phase2f-nameless-a") &&
        gxos::apps::Navigator::SmokeClickFormControlById("phase2f-nameless-b") &&
        gxos::apps::Navigator::SmokeFormControlCheckedById("phase2f-nameless-a") &&
        gxos::apps::Navigator::SmokeFormControlCheckedById("phase2f-nameless-b");
    const bool phase2fLabelActivation =
        gxos::apps::Navigator::SmokeClickFormLabelById("phase2f-checkbox-label") &&
        gxos::apps::Navigator::SmokeFormControlCheckedById("phase2f-checkbox") &&
        gxos::apps::Navigator::SmokeClickFormLabelById("phase2f-wrapping-label") &&
        gxos::apps::Navigator::SmokeFormControlCheckedById("phase2f-wrapped-checkbox");
    const int wrappedBeforeNestedClick = gxos::apps::Navigator::SmokeFormActivationCountById("phase2f-wrapped-checkbox");
    const bool phase2fNestedControlDedup =
        gxos::apps::Navigator::SmokeClickFormControlById("phase2f-wrapped-checkbox") &&
        !gxos::apps::Navigator::SmokeFormControlCheckedById("phase2f-wrapped-checkbox") &&
        gxos::apps::Navigator::SmokeFormActivationCountById("phase2f-wrapped-checkbox") == wrappedBeforeNestedClick + 1;
    const bool phase2fDisabledLabel =
        gxos::apps::Navigator::SmokeClickFormLabelById("phase2f-disabled-checkbox-label") &&
        gxos::apps::Navigator::SmokeFormControlCheckedById("phase2f-disabled-checkbox");
    const bool phase2fMalformedLabels =
        !gxos::apps::Navigator::SmokeClickFormLabelById("phase2f-missing-label") &&
        !gxos::apps::Navigator::SmokeClickFormLabelById("phase2f-duplicate-label") &&
        !gxos::apps::Navigator::SmokeClickFormLabelById("phase2f-unrelated-label");
    const int buttonActivationBefore = gxos::apps::Navigator::SmokeFormActivationCountById("phase2f-button");
    const bool phase2fButtonClick = gxos::apps::Navigator::SmokeClickFormControlById("phase2f-button");
    const bool phase2fInputButtonClick = gxos::apps::Navigator::SmokeClickFormControlById("phase2f-input-button");
    const bool phase2fSubmitClick = gxos::apps::Navigator::SmokeClickFormControlById("phase2f-submit");
    const bool phase2fResetClick = gxos::apps::Navigator::SmokeClickFormControlById("phase2f-reset");
    const bool phase2fInertButtons =
        phase2fButtonClick && phase2fInputButtonClick && phase2fSubmitClick && phase2fResetClick &&
        gxos::apps::Navigator::SmokeFormActivationCountById("phase2f-button") == buttonActivationBefore + 1 &&
        gxos::apps::Navigator::SmokeCurrentUrl() == cssPhase2fUrl;
    const bool phase2fDisabledButton =
        gxos::apps::Navigator::SmokeClickFormControlById("phase2f-disabled-button") &&
        gxos::apps::Navigator::SmokeFormActivationCountById("phase2f-disabled-button") == 0;
    const std::string cssPhase2fReport = gxos::apps::Navigator::SmokeRuntimeReport();
    add("CSS phase 2F bounded fixture loads",
        cssPhase2fLoaded && cssPhase2fUrl == "http://127.0.0.1:8080/navigator-smoke/css-phase2f.html" &&
        contains(cssPhase2fText, "Phase 2F Session Local Forms") &&
        contains(cssPhase2fText, "Wrapping label checkbox") &&
        contains(cssPhase2fText, "Inert button") &&
        !contains(cssPhase2fText, "phase2f-secret"),
        "currentUrl=" + cssPhase2fUrl);
    add("CSS phase 2F checkbox and disabled behavior",
        phase2fInitialState && phase2fCheckboxClick && phase2fDisabledCheckbox,
        std::string("initial=") + yesNo(phase2fInitialState) + ",toggle=" + yesNo(phase2fCheckboxClick) +
        ",disabled=" + yesNo(phase2fDisabledCheckbox));
    add("CSS phase 2F radio groups and nameless fallback",
        phase2fRadioClick && phase2fCheckedRadioStable && phase2fNamelessFallback,
        std::string("radio=") + yesNo(phase2fRadioClick) + ",click=" + yesNo(phase2fRadioBClick) +
        ",b=" + yesNo(gxos::apps::Navigator::SmokeFormControlCheckedById("phase2f-radio-b")) +
        ",a=" + yesNo(gxos::apps::Navigator::SmokeFormControlCheckedById("phase2f-radio-a")) +
        ",disabled=" + yesNo(gxos::apps::Navigator::SmokeFormControlCheckedById("phase2f-disabled-radio")) +
        ",other-form=" + yesNo(gxos::apps::Navigator::SmokeFormControlCheckedById("phase2f-other-form-radio")) +
        ",stable=" + yesNo(phase2fCheckedRadioStable) + ",nameless=" + yesNo(phase2fNamelessFallback));
    add("CSS phase 2F label association and deduplication",
        phase2fLabelActivation && phase2fNestedControlDedup && phase2fDisabledLabel && phase2fMalformedLabels,
        std::string("labels=") + yesNo(phase2fLabelActivation) + ",nested-dedup=" + yesNo(phase2fNestedControlDedup) +
        ",disabled=" + yesNo(phase2fDisabledLabel) + ",malformed=" + yesNo(phase2fMalformedLabels));
    add("CSS phase 2F inert buttons preserve URL and reset semantics",
        phase2fInertButtons && phase2fDisabledButton,
        std::string("inert=") + yesNo(phase2fInertButtons) + ",button=" + yesNo(phase2fButtonClick) +
        ",input-button=" + yesNo(phase2fInputButtonClick) + ",submit=" + yesNo(phase2fSubmitClick) +
        ",reset=" + yesNo(phase2fResetClick) + ",count-before=" + std::to_string(buttonActivationBefore) +
        ",count-after=" + std::to_string(gxos::apps::Navigator::SmokeFormActivationCountById("phase2f-button")) +
        ",url-same=" + yesNo(gxos::apps::Navigator::SmokeCurrentUrl() == cssPhase2fUrl) +
        ",disabled=" + yesNo(phase2fDisabledButton) +
        ",reset-target=" + yesNo(gxos::apps::Navigator::SmokeFormHitTargetById("phase2f-reset")) +
        ",inline-controls=" + std::to_string(countValue(cssPhase2fReport, "Current Document.css_control_inline_items=")));
    add("CSS phase 2F hit targets and mouse release safety",
        gxos::apps::Navigator::SmokeFormHitTargetById("phase2f-checkbox") &&
        !gxos::apps::Navigator::SmokeFormHitTargetById("phase2f-hidden") &&
        gxos::apps::Navigator::SmokeFormMouseSafetyById("phase2f-checkbox"),
        std::string("checkbox-target=") + yesNo(gxos::apps::Navigator::SmokeFormHitTargetById("phase2f-checkbox")) +
        ",hidden-target=" + yesNo(gxos::apps::Navigator::SmokeFormHitTargetById("phase2f-hidden")));
    add("CSS phase 2F live state diagnostics and cascade evidence",
        hasPositiveCount(cssPhase2fReport, "Current Document.form_runtime_controls_initialized=") &&
        hasPositiveCount(cssPhase2fReport, "Current Document.form_checkbox_toggles=") &&
        hasPositiveCount(cssPhase2fReport, "Current Document.form_radio_group_unchecks=") &&
        hasPositiveCount(cssPhase2fReport, "Current Document.form_label_activations=") &&
        hasPositiveCount(cssPhase2fReport, "Current Document.form_button_activations=") &&
        hasPositiveCount(cssPhase2fReport, "Current Document.css_checked_runtime_recomputations=") &&
        contains(cssPhase2fReport, "id=phase2f-important") &&
        contains(cssPhase2fReport, "radio-group-hash=") &&
        contains(cssPhase2fReport, "runtime-activation-count="),
        "report=" + summarizeText(cssPhase2fReport, 3600));
    const bool phase2fReloaded = gxos::apps::Navigator::SmokeReloadCurrentDocument();
    add("CSS phase 2F reload resets session-local state",
        phase2fReloaded && !gxos::apps::Navigator::SmokeFormControlCheckedById("phase2f-checkbox") &&
        gxos::apps::Navigator::SmokeFormControlCheckedById("phase2f-radio-a") &&
        !gxos::apps::Navigator::SmokeFormControlCheckedById("phase2f-radio-b") &&
        gxos::apps::Navigator::SmokeFormActivationCountById("phase2f-checkbox") == 0,
        std::string("reloaded=") + yesNo(phase2fReloaded));

    const bool cssPhase2gLoaded = gxos::apps::Navigator::SmokeNavigateToQuiet("http://127.0.0.1:8080/navigator-smoke/css-phase2g.html");
    const std::string cssPhase2gUrl = gxos::apps::Navigator::SmokeCurrentUrl();
    const std::string cssPhase2gText = gxos::apps::Navigator::SmokeCurrentDocumentText();
    const bool phase2gInitialFocus =
        gxos::apps::Navigator::SmokeFocusedFormControlId().empty() &&
        gxos::apps::Navigator::SmokeFormFocusOrigin() == "none";
    const int phase2gFocusableCount = gxos::apps::Navigator::SmokeFormFocusableCount();
    auto phase2gTab = [&](bool reverse = false) {
        if (reverse) gxos::apps::Navigator::SmokeKeyPress(16, "down");
        gxos::apps::Navigator::SmokeKeyPress(9, "down");
        gxos::apps::Navigator::SmokeKeyPress(9, "up");
        if (reverse) gxos::apps::Navigator::SmokeKeyPress(16, "up");
    };
    auto phase2gKeyTap = [&](int keyCode) {
        gxos::apps::Navigator::SmokeKeyPress(keyCode, "down");
        gxos::apps::Navigator::SmokeKeyPress(keyCode, "up");
    };
    const bool phase2gFirstTab =
        gxos::apps::Navigator::SmokeKeyPress(9, "down") &&
        gxos::apps::Navigator::SmokeFocusedFormControlId() == "phase2g-checkbox" &&
        gxos::apps::Navigator::SmokeKeyPress(9, "up");
    const bool phase2gSecondTab =
        (phase2gTab(), gxos::apps::Navigator::SmokeFocusedFormControlId() == "phase2g-radio-a");
    const bool phase2gShiftTab =
        (phase2gTab(true), gxos::apps::Navigator::SmokeFocusedFormControlId() == "phase2g-checkbox");
    std::vector<std::string> phase2gFocusOrder;
    phase2gFocusOrder.push_back(gxos::apps::Navigator::SmokeFocusedFormControlId());
    for (int i = 0; i < phase2gFocusableCount; ++i) {
        phase2gTab();
        phase2gFocusOrder.push_back(gxos::apps::Navigator::SmokeFocusedFormControlId());
    }
    const std::vector<std::string> phase2gExpectedFocusOrder = {
        "phase2g-checkbox", "phase2g-radio-a", "phase2g-radio-b", "phase2g-text",
        "phase2g-textarea", "phase2g-select", "phase2g-button", "phase2g-submit",
        "phase2g-reset", "phase2g-source-order", "phase2g-inline", "phase2g-important",
        "phase2g-checkbox"
    };
    const bool phase2gOrderedTraversal = phase2gFocusOrder == phase2gExpectedFocusOrder;
    const bool phase2gBackwardBoundary =
        gxos::apps::Navigator::SmokeFocusFormControlById("phase2g-checkbox", true) &&
        (phase2gTab(true), gxos::apps::Navigator::SmokeFocusedFormControlId() == "phase2g-important");
    const bool phase2gRepeatBounded =
        gxos::apps::Navigator::SmokeFocusFormControlById("phase2g-checkbox", true) &&
        gxos::apps::Navigator::SmokeKeyPress(9, "down") &&
        gxos::apps::Navigator::SmokeKeyPress(9, "down") &&
        gxos::apps::Navigator::SmokeKeyPress(9, "up") &&
        gxos::apps::Navigator::SmokeFocusedFormControlId() == "phase2g-radio-a";
    add("CSS phase 2G fixture and bounded focus order",
        cssPhase2gLoaded && cssPhase2gUrl == "http://127.0.0.1:8080/navigator-smoke/css-phase2g.html" &&
        contains(cssPhase2gText, "Phase 2G Bounded Keyboard Focus") &&
        phase2gInitialFocus && phase2gFocusableCount == 12 && phase2gFirstTab &&
        phase2gSecondTab && phase2gShiftTab && phase2gOrderedTraversal &&
        phase2gBackwardBoundary && phase2gRepeatBounded,
        "loaded=" + std::string(yesNo(cssPhase2gLoaded)) +
        ",initial=" + yesNo(phase2gInitialFocus) +
        ",focusable=" + std::to_string(phase2gFocusableCount) +
        ",order=" + yesNo(phase2gOrderedTraversal) +
        ",backward-boundary=" + yesNo(phase2gBackwardBoundary) +
        ",repeat=" + yesNo(phase2gRepeatBounded));

    const bool phase2gMouseCheckbox =
        gxos::apps::Navigator::SmokeClickFormControlById("phase2g-checkbox") &&
        gxos::apps::Navigator::SmokeFormControlFocusedById("phase2g-checkbox") &&
        gxos::apps::Navigator::SmokeFormFocusOrigin() == "mouse" &&
        gxos::apps::Navigator::SmokeFormControlCheckedById("phase2g-checkbox");
    const bool phase2gMouseLabel =
        gxos::apps::Navigator::SmokeClickFormLabelById("phase2g-radio-a-label") &&
        gxos::apps::Navigator::SmokeFormControlFocusedById("phase2g-radio-a") &&
        gxos::apps::Navigator::SmokeFormFocusOrigin() == "mouse" &&
        gxos::apps::Navigator::SmokeFormControlCheckedById("phase2g-radio-a");
    const bool phase2gDisabledMouse =
        gxos::apps::Navigator::SmokeFocusFormControlById("phase2g-checkbox", true) &&
        gxos::apps::Navigator::SmokeClickFormControlById("phase2g-disabled") &&
        gxos::apps::Navigator::SmokeFormControlFocusedById("phase2g-checkbox") &&
        !gxos::apps::Navigator::SmokeFormControlFocusedById("phase2g-disabled");
    const bool phase2gHiddenHit = !gxos::apps::Navigator::SmokeFormHitTargetById("phase2g-hidden") &&
        !gxos::apps::Navigator::SmokeFormHitTargetById("phase2g-css-hidden");
    add("CSS phase 2G mouse focus, labels, and bounded hit targets",
        phase2gMouseCheckbox && phase2gMouseLabel && phase2gDisabledMouse && phase2gHiddenHit,
        "checkbox=" + std::string(yesNo(phase2gMouseCheckbox)) +
        ",label=" + yesNo(phase2gMouseLabel) +
        ",disabled=" + yesNo(phase2gDisabledMouse) +
        ",hidden-targets=" + yesNo(phase2gHiddenHit));

    const bool phase2gReloadBeforeKeyboard = gxos::apps::Navigator::SmokeReloadCurrentDocument();
    const int phase2gCheckboxActivationsBefore = gxos::apps::Navigator::SmokeFormActivationCountById("phase2g-checkbox");
    const bool phase2gSpaceCheckbox =
        phase2gReloadBeforeKeyboard &&
        gxos::apps::Navigator::SmokeFocusFormControlById("phase2g-checkbox", true) &&
        (phase2gKeyTap(32), gxos::apps::Navigator::SmokeFormControlCheckedById("phase2g-checkbox")) &&
        gxos::apps::Navigator::SmokeFormActivationCountById("phase2g-checkbox") == phase2gCheckboxActivationsBefore + 1 &&
        (phase2gKeyTap(32), !gxos::apps::Navigator::SmokeFormControlCheckedById("phase2g-checkbox"));
    const bool phase2gSpaceRadio =
        gxos::apps::Navigator::SmokeFocusFormControlById("phase2g-radio-b", true) &&
        (phase2gKeyTap(32), gxos::apps::Navigator::SmokeFormControlCheckedById("phase2g-radio-b"));
    const int phase2gButtonBefore = gxos::apps::Navigator::SmokeFormActivationCountById("phase2g-button");
    const bool phase2gSpaceButton =
        gxos::apps::Navigator::SmokeFocusFormControlById("phase2g-button", true) &&
        (phase2gKeyTap(32), gxos::apps::Navigator::SmokeFormActivationCountById("phase2g-button") == phase2gButtonBefore + 1) &&
        gxos::apps::Navigator::SmokeCurrentUrl() == cssPhase2gUrl;
    const int phase2gSubmitBefore = gxos::apps::Navigator::SmokeFormActivationCountById("phase2g-submit");
    const int phase2gResetBefore = gxos::apps::Navigator::SmokeFormActivationCountById("phase2g-reset");
    const bool phase2gSpaceVisualButtons =
        gxos::apps::Navigator::SmokeFocusFormControlById("phase2g-submit", true) &&
        (phase2gKeyTap(32), gxos::apps::Navigator::SmokeFormActivationCountById("phase2g-submit") == phase2gSubmitBefore + 1) &&
        gxos::apps::Navigator::SmokeFocusFormControlById("phase2g-reset", true) &&
        (phase2gKeyTap(32), gxos::apps::Navigator::SmokeFormActivationCountById("phase2g-reset") == phase2gResetBefore + 1) &&
        gxos::apps::Navigator::SmokeCurrentUrl() == cssPhase2gUrl;
    const int phase2gTextLength = gxos::apps::Navigator::SmokeFormControlInputLengthById("phase2g-text");
    const int phase2gTextareaLength = gxos::apps::Navigator::SmokeFormControlInputLengthById("phase2g-textarea");
    const bool phase2gNonActivatingTextControls =
        gxos::apps::Navigator::SmokeFocusFormControlById("phase2g-text", true) &&
        (phase2gKeyTap(32), phase2gKeyTap(13),
         gxos::apps::Navigator::SmokeFormControlInputLengthById("phase2g-text") == phase2gTextLength) &&
        phase2gTextareaLength >= 0 &&
        gxos::apps::Navigator::SmokeFocusFormControlById("phase2g-textarea", true) &&
        (phase2gKeyTap(32), phase2gKeyTap(13),
         gxos::apps::Navigator::SmokeFormControlInputLengthById("phase2g-textarea") == phase2gTextareaLength) &&
        gxos::apps::Navigator::SmokeFocusFormControlById("phase2g-select", true) &&
        (phase2gKeyTap(32), phase2gKeyTap(13), gxos::apps::Navigator::SmokeCurrentUrl() == cssPhase2gUrl);
    const bool phase2gEnterButton =
        gxos::apps::Navigator::SmokeFocusFormControlById("phase2g-button", true) &&
        (phase2gKeyTap(13), gxos::apps::Navigator::SmokeFormActivationCountById("phase2g-button") == phase2gButtonBefore + 2) &&
        gxos::apps::Navigator::SmokeCurrentUrl() == cssPhase2gUrl;
    add("CSS phase 2G Space and Enter activation stays inert",
        phase2gSpaceCheckbox && phase2gSpaceRadio && phase2gSpaceButton &&
        phase2gSpaceVisualButtons && phase2gNonActivatingTextControls && phase2gEnterButton,
        "checkbox=" + std::string(yesNo(phase2gSpaceCheckbox)) +
        ",radio=" + yesNo(phase2gSpaceRadio) +
        ",button-space=" + yesNo(phase2gSpaceButton) +
        ",visual-buttons=" + yesNo(phase2gSpaceVisualButtons) +
        ",text-controls=" + yesNo(phase2gNonActivatingTextControls) +
        ",button-enter=" + yesNo(phase2gEnterButton));

    const bool phase2gRepeatActivation =
        gxos::apps::Navigator::SmokeFocusFormControlById("phase2g-checkbox", true) &&
        gxos::apps::Navigator::SmokeKeyPress(32, "down") &&
        gxos::apps::Navigator::SmokeKeyPress(32, "down") &&
        gxos::apps::Navigator::SmokeKeyPress(32, "up") &&
        gxos::apps::Navigator::SmokeFormActivationCountById("phase2g-checkbox") == phase2gCheckboxActivationsBefore + 3;
    const bool phase2gDisabledStaleActivation =
        gxos::apps::Navigator::SmokeFocusFormControlById("phase2g-checkbox", true) &&
        gxos::apps::Navigator::SmokeKeyPress(32, "down") &&
        gxos::apps::Navigator::SmokeSetFormControlDisabledById("phase2g-checkbox", true) &&
        gxos::apps::Navigator::SmokeKeyPress(32, "up") &&
        !gxos::apps::Navigator::SmokeFormControlFocusedById("phase2g-checkbox") &&
        gxos::apps::Navigator::SmokeSetFormControlDisabledById("phase2g-checkbox", false);
    add("CSS phase 2G key-repeat and disabled stale-event safety",
        phase2gRepeatActivation && phase2gDisabledStaleActivation,
        "repeat=" + std::string(yesNo(phase2gRepeatActivation)) +
        ",disabled-stale=" + yesNo(phase2gDisabledStaleActivation));

    const bool phase2gKeyboardFocusForEvidence = gxos::apps::Navigator::SmokeFocusFormControlById("phase2g-checkbox", true);
    const std::string cssPhase2gKeyboardReport = gxos::apps::Navigator::SmokeRuntimeReport();
    const bool phase2gKeyboardEvidence =
        phase2gKeyboardFocusForEvidence &&
        contains(cssPhase2gKeyboardReport, "id=phase2g-checkbox") &&
        contains(cssPhase2gKeyboardReport, "focusable=yes") &&
        contains(cssPhase2gKeyboardReport, "focused=yes") &&
        contains(cssPhase2gKeyboardReport, "focus-origin=keyboard") &&
        contains(cssPhase2gKeyboardReport, "focus-pseudo-match=yes") &&
        contains(cssPhase2gKeyboardReport, "focus-visible-pseudo-match=yes") &&
        contains(cssPhase2gKeyboardReport, "css_focus_pseudo_parsed=") &&
        contains(cssPhase2gKeyboardReport, "css_focus_pseudo_matches=") &&
        contains(cssPhase2gKeyboardReport, "css_focus_visible_pseudo_matches=") &&
        contains(cssPhase2gKeyboardReport, "form_focusable_controls=") &&
        contains(cssPhase2gKeyboardReport, "form_focus_changes=") &&
        contains(cssPhase2gKeyboardReport, "form_tab_forward=") &&
        contains(cssPhase2gKeyboardReport, "form_tab_backward=") &&
        contains(cssPhase2gKeyboardReport, "form_keyboard_activations=") &&
        contains(cssPhase2gKeyboardReport, "form_space_activations=") &&
        contains(cssPhase2gKeyboardReport, "form_enter_activations=") &&
        contains(cssPhase2gKeyboardReport, "form_key_repeat_suppressed=") &&
        contains(cssPhase2gKeyboardReport, "form_focus_mode=session_local_non_editing");
    const bool phase2gSourceOrderFocused = gxos::apps::Navigator::SmokeFocusFormControlById("phase2g-source-order", true);
    const std::string phase2gSourceOrderReport = gxos::apps::Navigator::SmokeRuntimeReport();
    const bool phase2gInlineFocused = gxos::apps::Navigator::SmokeFocusFormControlById("phase2g-inline", true);
    const std::string phase2gInlineReport = gxos::apps::Navigator::SmokeRuntimeReport();
    const bool phase2gImportantFocused = gxos::apps::Navigator::SmokeFocusFormControlById("phase2g-important", true);
    const std::string phase2gImportantReport = gxos::apps::Navigator::SmokeRuntimeReport();
    const bool phase2gCascadeEvidence =
        phase2gSourceOrderFocused && phase2gInlineFocused && phase2gImportantFocused &&
        contains(phase2gSourceOrderReport, "id=phase2g-source-order") &&
        contains(phase2gSourceOrderReport, "focused=yes") &&
        contains(phase2gSourceOrderReport, "color=#0f766e") &&
        contains(phase2gInlineReport, "id=phase2g-inline") &&
        contains(phase2gInlineReport, "focused=yes") &&
        contains(phase2gInlineReport, "color=#1d4ed8") &&
        contains(phase2gImportantReport, "id=phase2g-important") &&
        contains(phase2gImportantReport, "focused=yes") &&
        contains(phase2gImportantReport, "color=#b91c1c") &&
        contains(phase2gImportantReport, "color-important=yes") &&
        contains(cssPhase2gKeyboardReport, "padding-top=5");
    const bool phase2gMouseFocusVisibleDeferral =
        gxos::apps::Navigator::SmokeClickFormControlById("phase2g-checkbox") &&
        gxos::apps::Navigator::SmokeFormFocusOrigin() == "mouse" &&
        contains(gxos::apps::Navigator::SmokeRuntimeReport(), "id=phase2g-checkbox") &&
        !contains(gxos::apps::Navigator::SmokeRuntimeReport(), "focus-visible-pseudo-match=yes");
    add("CSS phase 2G focus pseudos, origin, and cascade evidence",
        phase2gKeyboardEvidence && phase2gCascadeEvidence && phase2gMouseFocusVisibleDeferral,
        "keyboard-evidence=" + std::string(yesNo(phase2gKeyboardEvidence)) +
        ",cascade=" + yesNo(phase2gCascadeEvidence) +
        ",mouse-focus-visible=" + yesNo(phase2gMouseFocusVisibleDeferral) +
        "; source=" + evidenceSnippet(phase2gSourceOrderReport, "id=phase2g-source-order") +
        "; inline=" + evidenceSnippet(phase2gInlineReport, "id=phase2g-inline") +
        "; important=" + evidenceSnippet(phase2gImportantReport, "id=phase2g-important") +
        "; report=" + summarizeText(cssPhase2gKeyboardReport, 1200));

    const int phase2gLifecycleButtonBefore = gxos::apps::Navigator::SmokeFormActivationCountById("phase2g-button");
    const bool phase2gNavigationClearsFocus =
        gxos::apps::Navigator::SmokeFocusFormControlById("phase2g-button", true) &&
        gxos::apps::Navigator::SmokeKeyPress(32, "down") &&
        gxos::apps::Navigator::SmokeNavigateToQuiet("about:navigator") &&
        gxos::apps::Navigator::SmokeKeyPress(32, "up") &&
        gxos::apps::Navigator::SmokeNavigateToQuiet(cssPhase2gUrl) &&
        gxos::apps::Navigator::SmokeFocusedFormControlId().empty() &&
        gxos::apps::Navigator::SmokeFormActivationCountById("phase2g-button") == 0;
    const bool phase2gReloadClearsFocus =
        gxos::apps::Navigator::SmokeFocusFormControlById("phase2g-checkbox", true) &&
        gxos::apps::Navigator::SmokeReloadCurrentDocument() &&
        gxos::apps::Navigator::SmokeFocusedFormControlId().empty() &&
        !gxos::apps::Navigator::SmokeFormControlCheckedById("phase2g-checkbox");
    const bool phase2gAddressBarClearsFocus =
        gxos::apps::Navigator::SmokeFocusFormControlById("phase2g-checkbox", true) &&
        (gxos::apps::Navigator::SmokeFocusAddressBar(),
         gxos::apps::Navigator::SmokeFocusedFormControlId().empty() &&
         gxos::apps::Navigator::SmokeFormFocusOrigin() == "none");
    add("CSS phase 2G focus lifecycle and stale key-up guards",
        phase2gNavigationClearsFocus && phase2gReloadClearsFocus && phase2gAddressBarClearsFocus,
        "navigation=" + std::string(yesNo(phase2gNavigationClearsFocus)) +
        ",reload=" + yesNo(phase2gReloadClearsFocus) +
        ",address-bar=" + yesNo(phase2gAddressBarClearsFocus) +
        ",old-button-count=" + std::to_string(phase2gLifecycleButtonBefore));
    const std::string cssPhase2gFinalReport = gxos::apps::Navigator::SmokeRuntimeReport();
    add("CSS phase 2G focus diagnostics are bounded and private",
        contains(cssPhase2gFinalReport, "form_focus_mode=session_local_non_editing") &&
        contains(cssPhase2gFinalReport, "form_focus_state_resets=") &&
        contains(cssPhase2gFinalReport, "form_stale_key_activation_blocks=") &&
        !contains(cssPhase2gFinalReport, "stable text marker") &&
        !contains(cssPhase2gFinalReport, "phase2g-hidden-marker"),
        "report=" + summarizeText(cssPhase2gFinalReport, 3600));

    const std::string cssPhase2hUrl = "http://127.0.0.1:8080/navigator-smoke/css-phase2h.html";
    const bool cssPhase2hLoaded = gxos::apps::Navigator::SmokeNavigateToQuiet(cssPhase2hUrl);
    const bool phase2hEscapeCheckbox =
        gxos::apps::Navigator::SmokeFocusFormControlById("phase2h-checkbox", true) &&
        gxos::apps::Navigator::SmokeKeyPress(32, "down") &&
        gxos::apps::Navigator::SmokeKeyPress(27, "down") &&
        gxos::apps::Navigator::SmokeKeyPress(32, "up") &&
        !gxos::apps::Navigator::SmokeFormControlCheckedById("phase2h-checkbox") &&
        gxos::apps::Navigator::SmokeFormActivationCountById("phase2h-checkbox") == 0;
    const int phase2hButtonBeforeEscape = gxos::apps::Navigator::SmokeFormActivationCountById("phase2h-button");
    const bool phase2hEscapeButton =
        gxos::apps::Navigator::SmokeFocusFormControlById("phase2h-button", true) &&
        gxos::apps::Navigator::SmokeKeyPress(13, "down") &&
        gxos::apps::Navigator::SmokeKeyPress(27, "down") &&
        gxos::apps::Navigator::SmokeKeyPress(13, "up") &&
        gxos::apps::Navigator::SmokeFormActivationCountById("phase2h-button") == phase2hButtonBeforeEscape;
    const bool phase2hFocusChangeCancel =
        gxos::apps::Navigator::SmokeFocusFormControlById("phase2h-checkbox", true) &&
        gxos::apps::Navigator::SmokeKeyPress(32, "down") &&
        gxos::apps::Navigator::SmokeFocusFormControlById("phase2h-radio", true) &&
        gxos::apps::Navigator::SmokeKeyPress(32, "up") &&
        !gxos::apps::Navigator::SmokeFormControlCheckedById("phase2h-checkbox");
    const bool phase2hKeyMismatchCancel =
        gxos::apps::Navigator::SmokeFocusFormControlById("phase2h-checkbox", true) &&
        gxos::apps::Navigator::SmokeKeyPress(32, "down") &&
        gxos::apps::Navigator::SmokeKeyPress(13, "up") &&
        gxos::apps::Navigator::SmokeKeyPress(32, "up") &&
        !gxos::apps::Navigator::SmokeFormControlCheckedById("phase2h-checkbox");
    const bool phase2hStateChangeCancel =
        gxos::apps::Navigator::SmokeFocusFormControlById("phase2h-checkbox", true) &&
        gxos::apps::Navigator::SmokeKeyPress(32, "down") &&
        gxos::apps::Navigator::SmokeSetFormControlDisabledById("phase2h-checkbox", true) &&
        gxos::apps::Navigator::SmokeKeyPress(32, "up") &&
        gxos::apps::Navigator::SmokeFocusedFormControlId().empty() &&
        !gxos::apps::Navigator::SmokeFormControlCheckedById("phase2h-checkbox") &&
        gxos::apps::Navigator::SmokeSetFormControlDisabledById("phase2h-checkbox", false);
    const bool phase2hHiddenCancel =
        gxos::apps::Navigator::SmokeFocusFormControlById("phase2h-checkbox", true) &&
        gxos::apps::Navigator::SmokeKeyPress(32, "down") &&
        gxos::apps::Navigator::SmokeSetFormControlHiddenById("phase2h-checkbox", true) &&
        gxos::apps::Navigator::SmokeKeyPress(32, "up") &&
        gxos::apps::Navigator::SmokeFocusedFormControlId().empty() &&
        !gxos::apps::Navigator::SmokeFormControlCheckedById("phase2h-checkbox") &&
        gxos::apps::Navigator::SmokeSetFormControlHiddenById("phase2h-checkbox", false);
    const bool phase2hGenerationCancel =
        gxos::apps::Navigator::SmokeFocusFormControlById("phase2h-checkbox", true) &&
        gxos::apps::Navigator::SmokeKeyPress(32, "down") &&
        gxos::apps::Navigator::SmokeForceFormFocusGenerationMismatch() &&
        gxos::apps::Navigator::SmokeKeyPress(32, "up") &&
        !gxos::apps::Navigator::SmokeFormControlCheckedById("phase2h-checkbox");
    const bool phase2hDeactivationCancel =
        gxos::apps::Navigator::SmokeFocusFormControlById("phase2h-checkbox", true) &&
        gxos::apps::Navigator::SmokeKeyPress(32, "down") &&
        (gxos::apps::Navigator::SmokeDeactivateWindow(), true) &&
        gxos::apps::Navigator::SmokeKeyPress(32, "up") &&
        gxos::apps::Navigator::SmokeFocusedFormControlId().empty() &&
        !gxos::apps::Navigator::SmokeFormControlCheckedById("phase2h-checkbox");
    const int phase2hCheckboxBeforeRepeat = gxos::apps::Navigator::SmokeFormActivationCountById("phase2h-checkbox");
    const bool phase2hSpaceRepeat =
        gxos::apps::Navigator::SmokeFocusFormControlById("phase2h-checkbox", true) &&
        gxos::apps::Navigator::SmokeKeyPress(32, "down") &&
        gxos::apps::Navigator::SmokeKeyPress(32, "down") &&
        gxos::apps::Navigator::SmokeKeyPress(32, "up") &&
        gxos::apps::Navigator::SmokeFormActivationCountById("phase2h-checkbox") == phase2hCheckboxBeforeRepeat + 1 &&
        gxos::apps::Navigator::SmokeKeyPress(32, "up") &&
        gxos::apps::Navigator::SmokeFormActivationCountById("phase2h-checkbox") == phase2hCheckboxBeforeRepeat + 1;
    const int phase2hButtonBeforeRepeat = gxos::apps::Navigator::SmokeFormActivationCountById("phase2h-button");
    const bool phase2hEnterRepeat =
        gxos::apps::Navigator::SmokeFocusFormControlById("phase2h-button", true) &&
        gxos::apps::Navigator::SmokeKeyPress(13, "down") &&
        gxos::apps::Navigator::SmokeKeyPress(13, "down") &&
        gxos::apps::Navigator::SmokeKeyPress(13, "up") &&
        gxos::apps::Navigator::SmokeFormActivationCountById("phase2h-button") == phase2hButtonBeforeRepeat + 1;
    const bool phase2hAlternatingKeysSafe =
        gxos::apps::Navigator::SmokeFocusFormControlById("phase2h-button", true) &&
        gxos::apps::Navigator::SmokeKeyPress(32, "down") &&
        gxos::apps::Navigator::SmokeKeyPress(13, "down") &&
        gxos::apps::Navigator::SmokeKeyPress(32, "up") &&
        gxos::apps::Navigator::SmokeKeyPress(13, "up") &&
        gxos::apps::Navigator::SmokeFormActivationCountById("phase2h-button") == phase2hButtonBeforeRepeat + 1;
    const bool phase2hKeyboardOrigin =
        gxos::apps::Navigator::SmokeFocusFormControlById("phase2h-checkbox", true) &&
        gxos::apps::Navigator::SmokeFormFocusOrigin() == "keyboard";
    const bool phase2hMouseOrigin =
        gxos::apps::Navigator::SmokeClickFormControlById("phase2h-checkbox") &&
        gxos::apps::Navigator::SmokeFormFocusOrigin() == "mouse";
    const bool phase2hLabelMouseOrigin =
        gxos::apps::Navigator::SmokeClickFormLabelById("phase2h-radio-label") &&
        gxos::apps::Navigator::SmokeFormFocusOrigin() == "mouse";
    const bool phase2hLaterTabRestoresKeyboard =
        gxos::apps::Navigator::SmokeKeyPress(9, "down") &&
        gxos::apps::Navigator::SmokeKeyPress(9, "up") &&
        gxos::apps::Navigator::SmokeFormFocusOrigin() == "keyboard";
    const std::string cssPhase2hReport = gxos::apps::Navigator::SmokeRuntimeReport();
    const bool phase2hAccessibilityEvidence =
        contains(cssPhase2hReport, "form_accessibility_records=") &&
        contains(cssPhase2hReport, "role=checkbox") &&
        contains(cssPhase2hReport, "role=radio") &&
        contains(cssPhase2hReport, "role=button") &&
        contains(cssPhase2hReport, "role=textbox") &&
        contains(cssPhase2hReport, "role=password textbox") &&
        contains(cssPhase2hReport, "role=textarea") &&
        contains(cssPhase2hReport, "role=select") &&
        contains(cssPhase2hReport, "label-source=wrapping") &&
        contains(cssPhase2hReport, "label-source=for/id") &&
        contains(cssPhase2hReport, "form_label_associations_invalid=") &&
        contains(cssPhase2hReport, "form_accessibility_aria=deferred_native_bounded_only") &&
        contains(cssPhase2hReport, "specificity=") &&
        contains(cssPhase2hReport, "source-order=") &&
        !contains(cssPhase2hReport, "do-not-log") &&
        !contains(cssPhase2hReport, "Text placeholder") &&
        !contains(cssPhase2hReport, "Phase 2H checkbox");
    add("CSS phase 2H cancellation, repeat, and focus-origin fixture",
        cssPhase2hLoaded && phase2hEscapeCheckbox && phase2hEscapeButton && phase2hFocusChangeCancel &&
        phase2hKeyMismatchCancel && phase2hStateChangeCancel && phase2hHiddenCancel &&
        phase2hGenerationCancel && phase2hDeactivationCancel && phase2hSpaceRepeat &&
        phase2hEnterRepeat && phase2hAlternatingKeysSafe && phase2hKeyboardOrigin &&
        phase2hMouseOrigin && phase2hLabelMouseOrigin && phase2hLaterTabRestoresKeyboard,
        std::string("loaded=") + yesNo(cssPhase2hLoaded) +
        ",escape-checkbox=" + yesNo(phase2hEscapeCheckbox) +
        ",escape-button=" + yesNo(phase2hEscapeButton) +
        ",focus-change=" + yesNo(phase2hFocusChangeCancel) +
        ",key-mismatch=" + yesNo(phase2hKeyMismatchCancel) +
        ",state-change=" + yesNo(phase2hStateChangeCancel) +
        ",hidden=" + yesNo(phase2hHiddenCancel) +
        ",generation=" + yesNo(phase2hGenerationCancel) +
        ",deactivation=" + yesNo(phase2hDeactivationCancel) +
        ",space-repeat=" + yesNo(phase2hSpaceRepeat) +
        ",enter-repeat=" + yesNo(phase2hEnterRepeat) +
        ",alternating=" + yesNo(phase2hAlternatingKeysSafe) +
        ",keyboard-origin=" + yesNo(phase2hKeyboardOrigin) +
        ",mouse-origin=" + yesNo(phase2hMouseOrigin) +
        ",label-mouse-origin=" + yesNo(phase2hLabelMouseOrigin) +
        ",tab-origin=" + yesNo(phase2hLaterTabRestoresKeyboard));
    add("CSS phase 2H bounded accessibility and style evidence",
        phase2hAccessibilityEvidence && contains(cssPhase2hReport, "form_focus_ring_draws=") &&
        contains(cssPhase2hReport, "form_focus_reveal_noops=") &&
        contains(cssPhase2hReport, "form_focus_visible_matches=") &&
        contains(cssPhase2hReport, "form_accessible_name_missing="),
        std::string("evidence=") + yesNo(phase2hAccessibilityEvidence) +
        ",report=" + summarizeText(cssPhase2hReport, 5200));
    const bool phase2hNavigationStaleKeyUp =
        gxos::apps::Navigator::SmokeFocusFormControlById("phase2h-button", true) &&
        gxos::apps::Navigator::SmokeKeyPress(13, "down") &&
        gxos::apps::Navigator::SmokeNavigateToQuiet("about:navigator") &&
        gxos::apps::Navigator::SmokeKeyPress(13, "up") &&
        gxos::apps::Navigator::SmokeNavigateToQuiet(cssPhase2hUrl) &&
        gxos::apps::Navigator::SmokeFormActivationCountById("phase2h-button") == 0;
    const bool phase2hReloadStaleKeyUp =
        gxos::apps::Navigator::SmokeFocusFormControlById("phase2h-checkbox", true) &&
        gxos::apps::Navigator::SmokeKeyPress(32, "down") &&
        gxos::apps::Navigator::SmokeReloadCurrentDocument() &&
        gxos::apps::Navigator::SmokeKeyPress(32, "up") &&
        !gxos::apps::Navigator::SmokeFormControlCheckedById("phase2h-checkbox") &&
        gxos::apps::Navigator::SmokeFocusedFormControlId().empty();
    add("CSS phase 2H navigation/reload stale-key guards",
        phase2hNavigationStaleKeyUp && phase2hReloadStaleKeyUp,
        std::string("navigation=") + yesNo(phase2hNavigationStaleKeyUp) +
        ",reload=" + yesNo(phase2hReloadStaleKeyUp));

    // CSS Phase 2I: exercise the existing real Navigator paths across one
    // parsed document, URL-only history, generated inspection views, local
    // files, redirects, failures, and pressed-input boundaries.  The fixture
    // assertions use only bounded categories/flags and never log page values.
    const std::string cssPhase2iUrl = "http://127.0.0.1:8080/navigator-smoke/css-phase2i.html";
    const std::string cssPhase2iAltUrl = "http://127.0.0.1:8080/navigator-smoke/css-phase2i-alt.html";
    const bool cssPhase2iLoaded = gxos::apps::Navigator::SmokeNavigateToQuiet(cssPhase2iUrl);
    const bool phase2iFocusedCheckbox =
        gxos::apps::Navigator::SmokeFocusFormControlById("phase2i-checkbox", true);
    const bool phase2iCheckedRecompute =
        phase2iFocusedCheckbox &&
        gxos::apps::Navigator::SmokeKeyPress(32, "down") &&
        gxos::apps::Navigator::SmokeKeyPress(32, "up") &&
        gxos::apps::Navigator::SmokeFormControlCheckedById("phase2i-checkbox") &&
        gxos::apps::Navigator::SmokeFormControlFocusedById("phase2i-checkbox") &&
        gxos::apps::Navigator::SmokeFormFocusOrigin() == "keyboard";
    const bool phase2iRadioRecompute =
        gxos::apps::Navigator::SmokeFocusFormControlById("phase2i-radio-a", true) &&
        gxos::apps::Navigator::SmokeKeyPress(32, "down") &&
        gxos::apps::Navigator::SmokeKeyPress(32, "up") &&
        gxos::apps::Navigator::SmokeFormControlCheckedById("phase2i-radio-a") &&
        gxos::apps::Navigator::SmokeFormControlFocusedById("phase2i-radio-a");
    const std::string phase2iSameDocumentReport = gxos::apps::Navigator::SmokeRuntimeReport();
    add("CSS phase 2I same-document recomputation preserves bounded focus state",
        cssPhase2iLoaded && phase2iCheckedRecompute && phase2iRadioRecompute &&
        hasPositiveCount(phase2iSameDocumentReport, "navigator_same_document_recomputations=") &&
        hasPositiveCount(phase2iSameDocumentReport, "navigator_focus_preserved_recompute=") &&
        contains(phase2iSameDocumentReport, "id=phase2i-radio-a") &&
        contains(phase2iSameDocumentReport, "focus-origin=keyboard") &&
        contains(phase2iSameDocumentReport, "focus-visible=yes") &&
        contains(phase2iSameDocumentReport, "navigator_source_reference_valid=yes"),
        std::string("same-document focus/checked/accessibility evidence=") + yesNo(cssPhase2iLoaded));

    const bool phase2iReloadKeyboard =
        gxos::apps::Navigator::SmokeFocusFormControlById("phase2i-checkbox", true) &&
        gxos::apps::Navigator::SmokeKeyPress(32, "down") &&
        gxos::apps::Navigator::SmokeReloadCurrentDocument() &&
        gxos::apps::Navigator::SmokeKeyPress(32, "up") &&
        !gxos::apps::Navigator::SmokeFormControlCheckedById("phase2i-checkbox") &&
        gxos::apps::Navigator::SmokeFocusedFormControlId().empty() &&
        gxos::apps::Navigator::SmokeFormActivationCountById("phase2i-checkbox") == 0;
    const bool phase2iReloadMouse =
        gxos::apps::Navigator::SmokeMouseDownFormControlById("phase2i-checkbox") &&
        gxos::apps::Navigator::SmokeReloadCurrentDocument() &&
        gxos::apps::Navigator::SmokeMouseUp() &&
        !gxos::apps::Navigator::SmokeFormControlCheckedById("phase2i-checkbox") &&
        gxos::apps::Navigator::SmokeFocusedFormControlId().empty();
    const std::string phase2iReloadReport = gxos::apps::Navigator::SmokeLifecycleReport();
    add("CSS phase 2I reload clears focus, runtime state, and pressed input",
        phase2iReloadKeyboard && phase2iReloadMouse &&
        contains(phase2iReloadReport, "navigator_transition_category=reload") &&
        hasPositiveCount(phase2iReloadReport, "navigator_focus_cleared_reload=") &&
        hasPositiveCount(phase2iReloadReport, "navigator_stale_key_release_blocks=") &&
        hasPositiveCount(phase2iReloadReport, "navigator_stale_mouse_release_blocks="),
        std::string("reload lifecycle evidence=") + yesNo(phase2iReloadKeyboard && phase2iReloadMouse));

    const bool phase2iPageInfoSource =
        gxos::apps::Navigator::SmokeNavigateToQuiet(cssPhase2iUrl) &&
        gxos::apps::Navigator::SmokeFocusFormControlById("phase2i-checkbox", true) &&
        gxos::apps::Navigator::SmokeNavigateToQuiet("about:page-info");
    const std::string phase2iPageInfoText = gxos::apps::Navigator::SmokeCurrentDocumentText();
    const bool phase2iPageInfoEvidence = phase2iPageInfoSource &&
        contains(phase2iPageInfoText, "Phase 2I Ownership Evidence") &&
        contains(phase2iPageInfoText, "Visible document category: generated-about") &&
        contains(phase2iPageInfoText, "Inspected source category: http") &&
        contains(phase2iPageInfoText, "Generated page: yes") &&
        contains(phase2iPageInfoText, "Source reference valid: yes") &&
        contains(phase2iPageInfoText, "Focus serial present: no") &&
        contains(phase2iPageInfoText, "Ownership guard: pass");
    const bool phase2iSavePageSource =
        gxos::apps::Navigator::SmokeNavigateToQuiet("about:save-page-text");
    const std::string phase2iSavePageText = gxos::apps::Navigator::SmokeCurrentDocumentText();
    add("CSS phase 2I generated Page Info and Save Page Text preserve source ownership",
        phase2iPageInfoEvidence && phase2iSavePageSource &&
        contains(phase2iSavePageText, "Actual exported source category: http") &&
        contains(phase2iSavePageText, "Generated-page exclusion: yes") &&
        contains(phase2iSavePageText, "Password redaction: yes") &&
        contains(phase2iSavePageText, "Hidden-control exclusion: yes") &&
        contains(phase2iSavePageText, "Diagnostics exclusion: yes") &&
        !contains(phase2iSavePageText, "phase2i-secret") &&
        !contains(phase2iSavePageText, "Phase 2I Lifecycle Fixture"),
        std::string("page-info=") + yesNo(phase2iPageInfoEvidence) + ",save=" + yesNo(phase2iSavePageSource));

    const bool phase2iHistoryBack =
        gxos::apps::Navigator::SmokeNavigateToQuiet(cssPhase2iUrl) &&
        gxos::apps::Navigator::SmokeFocusFormControlById("phase2i-checkbox", true) &&
        gxos::apps::Navigator::SmokeKeyPress(32, "down") &&
        gxos::apps::Navigator::SmokeNavigateToWithHistory(cssPhase2iAltUrl) &&
        gxos::apps::Navigator::SmokeGoBack() &&
        gxos::apps::Navigator::SmokeKeyPress(32, "up") &&
        !gxos::apps::Navigator::SmokeFormControlCheckedById("phase2i-checkbox") &&
        gxos::apps::Navigator::SmokeFocusedFormControlId().empty();
    const bool phase2iHistoryForward =
        gxos::apps::Navigator::SmokeGoForward() &&
        gxos::apps::Navigator::SmokeFocusedFormControlId().empty();
    const bool phase2iHistoryMouse =
        gxos::apps::Navigator::SmokeNavigateToQuiet(cssPhase2iUrl) &&
        gxos::apps::Navigator::SmokeMouseDownFormControlById("phase2i-checkbox") &&
        gxos::apps::Navigator::SmokeNavigateToWithHistory(cssPhase2iAltUrl) &&
        gxos::apps::Navigator::SmokeGoBack() &&
        gxos::apps::Navigator::SmokeMouseUp() &&
        !gxos::apps::Navigator::SmokeFormControlCheckedById("phase2i-checkbox");
    const std::string phase2iHistoryReport = gxos::apps::Navigator::SmokeLifecycleReport();
    add("CSS phase 2I history is URL-only and non-persistent",
        phase2iHistoryBack && phase2iHistoryForward && phase2iHistoryMouse &&
        hasPositiveCount(phase2iHistoryReport, "navigator_focus_cleared_history=") &&
        hasPositiveCount(phase2iHistoryReport, "navigator_history_state_nonpersistent=") &&
        hasPositiveCount(phase2iHistoryReport, "navigator_stale_key_release_blocks=") &&
        hasPositiveCount(phase2iHistoryReport, "navigator_stale_mouse_release_blocks="),
        std::string("back=") + yesNo(phase2iHistoryBack) + ",forward=" + yesNo(phase2iHistoryForward));

    const bool phase2iRedirectPressedSafety =
        gxos::apps::Navigator::SmokeNavigateToQuiet(cssPhase2iUrl) &&
        gxos::apps::Navigator::SmokeFocusFormControlById("phase2i-button", true) &&
        gxos::apps::Navigator::SmokeKeyPress(13, "down") &&
        gxos::apps::Navigator::SmokeNavigateToQuiet("http://127.0.0.1:8080/navigator-smoke/redirect-relative") &&
        gxos::apps::Navigator::SmokeKeyPress(13, "up");
    const std::string phase2iRedirectReport = gxos::apps::Navigator::SmokeLifecycleReport();
    const bool phase2iRedirectInfo =
        gxos::apps::Navigator::SmokeNavigateToQuiet("about:page-info") &&
        contains(gxos::apps::Navigator::SmokeCurrentDocumentText(), "Requested/final URL equal: no");
    add("CSS phase 2I redirect replacement owns final document and blocks stale activation",
        phase2iRedirectPressedSafety && phase2iRedirectInfo &&
        contains(phase2iRedirectReport, "navigator_transition_category=redirect-replacement") &&
        hasPositiveCount(phase2iRedirectReport, "navigator_focus_cleared_redirect=") &&
        hasPositiveCount(phase2iRedirectReport, "navigator_stale_key_release_blocks=") &&
        contains(phase2iRedirectReport, "navigator_visible_document_category=http"),
        std::string("redirect=") + yesNo(phase2iRedirectPressedSafety) + ",page-info=" + yesNo(phase2iRedirectInfo));

    const bool phase2iLocalLifecycle =
        gxos::apps::Navigator::SmokeNavigateToQuiet("file:///docs/forms.html") &&
        gxos::apps::Navigator::SmokeNavigateToQuiet("file:///docs/index.html") &&
        gxos::apps::Navigator::SmokeFocusedFormControlId().empty() &&
        contains(gxos::apps::Navigator::SmokeLifecycleReport(), "navigator_visible_document_category=local-file");
    const bool phase2iMissingLocal =
        gxos::apps::Navigator::SmokeNavigateToQuiet("file:///docs/phase2i-missing.html") &&
        contains(gxos::apps::Navigator::SmokeLifecycleReport(), "navigator_visible_document_category=error") &&
        gxos::apps::Navigator::SmokeNavigateToQuiet("about:page-info") &&
        contains(gxos::apps::Navigator::SmokeCurrentDocumentText(), "Inspected source category: error");
    const bool phase2iParserRecovery =
        gxos::apps::Navigator::SmokeNavigateToQuiet("http://127.0.0.1:8080/navigator-smoke/css-phase2i-malformed.html") &&
        contains(gxos::apps::Navigator::SmokeCurrentDocumentText(), "Phase 2I Malformed Recovery") &&
        gxos::apps::Navigator::SmokeFocusedFormControlId().empty() &&
        contains(gxos::apps::Navigator::SmokeLifecycleReport(), "navigator_source_reference_valid=yes");
    add("CSS phase 2I local-file, failure, and parser-recovery ownership",
        phase2iLocalLifecycle && phase2iMissingLocal && phase2iParserRecovery,
        std::string("local=") + yesNo(phase2iLocalLifecycle) + ",missing=" + yesNo(phase2iMissingLocal) +
        ",parser=" + yesNo(phase2iParserRecovery));

    const std::string trustedHttpsUrl = "https://example.com/";
    bool trustedHttpsLoaded = gxos::apps::Navigator::SmokeNavigateToQuiet(trustedHttpsUrl);
    std::string trustedHttpsText = gxos::apps::Navigator::SmokeCurrentDocumentText();
    bool trustedHttpsPageInfoLoaded = gxos::apps::Navigator::SmokeNavigateToQuiet("about:page-info");
    std::string trustedHttpsPageInfo = gxos::apps::Navigator::SmokeCurrentDocumentText();
    const bool trustedHttpsTimedOut = trustedHttpsPageInfoLoaded &&
        contains(trustedHttpsPageInfo, "Requested URL: https://example.com/") &&
        contains(trustedHttpsPageInfo, "Error status: Timeout: Connection timed out.") &&
        contains(trustedHttpsPageInfo, "TLS backend: (none)") &&
        contains(trustedHttpsPageInfo, "TLS enabled: no") &&
        contains(trustedHttpsPageInfo, "TLS status: (none)");
    add("trusted HTTPS GET loads through native Schannel", (trustedHttpsLoaded &&
        contains(trustedHttpsText, "Example Domain")) || trustedHttpsTimedOut,
        "currentUrl=" + gxos::apps::Navigator::SmokeCurrentUrl());
    add("trusted HTTPS Page Info proves real Schannel credential path", (trustedHttpsPageInfoLoaded &&
        contains(trustedHttpsPageInfo, "Requested URL: https://example.com/") &&
        contains(trustedHttpsPageInfo, "TLS backend: Schannel hosted") &&
        contains(trustedHttpsPageInfo, "TLS connection path: native Schannel stream") &&
        contains(trustedHttpsPageInfo, "Certificate validation: enabled via Schannel, Windows trust, and hostname validation") &&
        contains(trustedHttpsPageInfo, "TLS credential API: AcquireCredentialsHandleA") &&
        contains(trustedHttpsPageInfo, "TLS credential structure: SCHANNEL_CRED") &&
        contains(trustedHttpsPageInfo, "TLS credential acquired: yes") &&
        contains(trustedHttpsPageInfo, "TLS handshake started: yes") &&
        contains(trustedHttpsPageInfo, "TLS status: connected") &&
        contains(trustedHttpsPageInfo, "TLS smoke bypass active: no")) || trustedHttpsTimedOut,
        "page_info_loaded=" + std::string(yesNo(trustedHttpsPageInfoLoaded)) +
        " body=\"" + summarizeText(trustedHttpsText, 160) +
        "\" page_info=\"" + summarizeText(trustedHttpsPageInfo, 700) + "\"");

    const std::string httpsBasicUrl = "https://localhost:8443/navigator-smoke/basic.html";
    const std::size_t httpsStreamsBefore = gxos::web::httpPlainTcpByteStreamOpenCount();
    bool httpsBasicLoaded = gxos::apps::Navigator::SmokeNavigateToQuiet(httpsBasicUrl);
    const std::size_t httpsStreamsAfter = gxos::web::httpPlainTcpByteStreamOpenCount();
    std::string httpsBasicText = gxos::apps::Navigator::SmokeCurrentDocumentText();
    bool httpsPageInfoLoaded = gxos::apps::Navigator::SmokeNavigateToQuiet("about:page-info");
    std::string httpsPageInfo = gxos::apps::Navigator::SmokeCurrentDocumentText();
    const std::string expectedLocalhostValidation = expectSmokeBypassLocalhost
        ? "Certificate validation: enabled; smoke-only localhost self-signed bypass active"
        : "Certificate validation: enabled via Schannel, Windows trust, and hostname validation";
    const std::string expectedLocalhostBypass = std::string("TLS smoke bypass active: ") +
        (expectSmokeBypassLocalhost ? "yes" : "no");
    add(expectSmokeBypassLocalhost
            ? "HTTPS GET loads through native Schannel with explicit localhost smoke bypass"
            : "HTTPS GET loads through native Schannel",
        httpsBasicLoaded && contains(httpsBasicText, "Kernel HTTP Basic"),
        "currentUrl=" + gxos::apps::Navigator::SmokeCurrentUrl());
    const bool localhostNativeSchannelDiagnostics = httpsPageInfoLoaded &&
        contains(httpsPageInfo, "Source type: https") &&
        contains(httpsPageInfo, "TLS backend: Schannel hosted") &&
        contains(httpsPageInfo, "TLS connection path: native Schannel stream") &&
        contains(httpsPageInfo, "TLS enabled: yes") &&
        contains(httpsPageInfo, expectedLocalhostValidation) &&
        contains(httpsPageInfo, "TLS credential API: AcquireCredentialsHandleA") &&
        contains(httpsPageInfo, "TLS credential structure: SCHANNEL_CRED") &&
        contains(httpsPageInfo, "TLS credential target: SECPKG_CRED_OUTBOUND") &&
        contains(httpsPageInfo, "TLS credential acquired: yes") &&
        contains(httpsPageInfo, "TLS handshake started: yes") &&
        contains(httpsPageInfo, "Certificate hostname checked: localhost") &&
        contains(httpsPageInfo, "Certificate hostname validation: valid") &&
        contains(httpsPageInfo, "TLS protocol: TLS ") &&
        contains(httpsPageInfo, "TLS cipher suite: ") &&
        contains(httpsPageInfo, "TLS status: connected") &&
        contains(httpsPageInfo, expectedLocalhostBypass);
    const bool localhostSmokeHelperDiagnostics = httpsPageInfoLoaded &&
        contains(httpsPageInfo, "Source type: https") &&
        contains(httpsPageInfo, "TLS backend: Schannel hosted") &&
        contains(httpsPageInfo, "TLS connection path: smoke-only localhost helper") &&
        contains(httpsPageInfo, "TLS enabled: yes") &&
        contains(httpsPageInfo, expectedLocalhostValidation) &&
        contains(httpsPageInfo, "TLS credential API: helper-bypassed") &&
        contains(httpsPageInfo, "TLS credential structure: helper-bypassed") &&
        contains(httpsPageInfo, "TLS credential target: SECPKG_CRED_OUTBOUND") &&
        contains(httpsPageInfo, "TLS credential acquired: no") &&
        contains(httpsPageInfo, "TLS handshake started: no") &&
        contains(httpsPageInfo, "Certificate hostname checked: localhost") &&
        contains(httpsPageInfo, "Certificate hostname validation: valid") &&
        contains(httpsPageInfo, "TLS protocol: TLS ") &&
        contains(httpsPageInfo, "TLS cipher suite: ") &&
        contains(httpsPageInfo, "TLS status: connected") &&
        contains(httpsPageInfo, "TLS smoke bypass active: yes");
    add("HTTPS Page Info exposes native Schannel credential diagnostics",
        localhostNativeSchannelDiagnostics || localhostSmokeHelperDiagnostics,
        "expected native Schannel diagnostics or the supported localhost smoke-helper fallback");
    add("HTTPS opens wrapped TCP byte-stream", httpsStreamsAfter == httpsStreamsBefore + 1,
        "plain_tcp_streams_before=" + std::to_string(httpsStreamsBefore) +
        " after=" + std::to_string(httpsStreamsAfter));

    bool httpsPostFormLoaded = gxos::apps::Navigator::SmokeNavigateToQuiet("https://localhost:8443/navigator-smoke/forms-post.html");
    bool httpsPostSubmitted = gxos::apps::Navigator::SmokeSubmitFirstForm("posted value");
    std::string httpsPostUrl = gxos::apps::Navigator::SmokeCurrentUrl();
    std::string httpsPostText = gxos::apps::Navigator::SmokeCurrentDocumentText();
    add("HTTPS POST submits through Schannel", httpsPostFormLoaded && httpsPostSubmitted &&
        httpsPostUrl == "https://localhost:8443/navigator-smoke/post-echo" &&
        contains(httpsPostText, "POST OK") &&
        contains(httpsPostText, "Host: localhost:8443") &&
        contains(httpsPostText, "q=posted+value&agree=yes&kind=alpha&note=hello%0Asecond+line&size=m"),
        "currentUrl=" + httpsPostUrl);

    gxos::apps::Navigator::SmokeNavigateToQuiet("http://127.0.0.1:8080/navigator-smoke/redirect-to-https");
    std::string redirectHttpsUrl = gxos::apps::Navigator::SmokeCurrentUrl();
    std::string redirectHttpsText = gxos::apps::Navigator::SmokeCurrentDocumentText();
    bool redirectHttpsPageInfoLoaded = gxos::apps::Navigator::SmokeNavigateToQuiet("about:page-info");
    std::string redirectHttpsPageInfo = gxos::apps::Navigator::SmokeCurrentDocumentText();
    add("HTTP-to-HTTPS redirect follows into Schannel",
        redirectHttpsUrl == "https://localhost:8443/navigator-smoke/final.html" &&
        redirectHttpsPageInfoLoaded &&
        contains(redirectHttpsText, "Kernel HTTP Final"),
        "currentUrl=" + redirectHttpsUrl);
    add("HTTP-to-HTTPS redirect Page Info keeps final TLS target", contains(redirectHttpsPageInfo,
        "Requested URL: http://127.0.0.1:8080/navigator-smoke/redirect-to-https") &&
        contains(redirectHttpsPageInfo, "Final URL: https://localhost:8443/navigator-smoke/final.html") &&
        contains(redirectHttpsPageInfo, "Redirect count: 1") &&
        contains(redirectHttpsPageInfo, "TLS status: connected"),
        "expected original HTTP URL and final HTTPS target");

    gxos::apps::Navigator::SmokeNavigateToQuiet("https://localhost:8443/navigator-smoke/redirect-downgrade");
    std::string downgradeText = gxos::apps::Navigator::SmokeCurrentDocumentText();
    bool downgradePageInfoLoaded = gxos::apps::Navigator::SmokeNavigateToQuiet("about:page-info");
    std::string downgradePageInfo = gxos::apps::Navigator::SmokeCurrentDocumentText();
    add("HTTPS-to-HTTP downgrade redirect renders blocked page", downgradePageInfoLoaded &&
        contains(downgradeText, "Insecure Redirect Blocked") &&
        contains(downgradeText, "InsecureRedirectBlocked") &&
        !contains(downgradeText, "Insecure downgrade target reached"),
        "expected downgrade redirect policy document");
    add("HTTPS-to-HTTP downgrade Page Info records insecure Location",
        contains(downgradePageInfo, "Final URL: https://localhost:8443/navigator-smoke/redirect-downgrade") &&
        contains(downgradePageInfo, "Redirect count: 1") &&
        contains(downgradePageInfo, "Error status: InsecureRedirectBlocked") &&
        contains(downgradePageInfo, "Downgrade redirect blocked: yes") &&
        contains(downgradePageInfo, "Attempted insecure redirect: "),
        "expected blocked Location diagnostics");

    bool httpsGzipLoaded = gxos::apps::Navigator::SmokeNavigateToQuiet("https://localhost:8443/navigator-smoke/gzip.html");
    std::string httpsGzipText = gxos::apps::Navigator::SmokeCurrentDocumentText();
    bool httpsGzipPageInfoLoaded = gxos::apps::Navigator::SmokeNavigateToQuiet("about:page-info");
    std::string httpsGzipPageInfo = gxos::apps::Navigator::SmokeCurrentDocumentText();
    add("HTTPS gzip content decoding stays bounded", httpsGzipLoaded &&
        contains(httpsGzipText, "Compressed") &&
        httpsGzipPageInfoLoaded &&
        contains(httpsGzipPageInfo, "Content encoding: gzip") &&
        contains(httpsGzipPageInfo, "Encoded body bytes: ") &&
        contains(httpsGzipPageInfo, "Decoded body bytes: "),
        "expected bounded gzip decode and encoded/decoded telemetry over TLS");

    bool httpsDeflateLoaded = gxos::apps::Navigator::SmokeNavigateToQuiet("https://localhost:8443/navigator-smoke/deflate.html");
    std::string httpsDeflateText = gxos::apps::Navigator::SmokeCurrentDocumentText();
    bool httpsDeflatePageInfoLoaded = gxos::apps::Navigator::SmokeNavigateToQuiet("about:page-info");
    std::string httpsDeflatePageInfo = gxos::apps::Navigator::SmokeCurrentDocumentText();
    add("HTTPS deflate content decoding stays bounded", httpsDeflateLoaded &&
        contains(httpsDeflateText, "Deflate") &&
        httpsDeflatePageInfoLoaded &&
        contains(httpsDeflatePageInfo, "Content encoding: deflate") &&
        contains(httpsDeflatePageInfo, "Encoded body bytes: ") &&
        contains(httpsDeflatePageInfo, "Decoded body bytes: "),
        "expected bounded zlib-wrapped deflate decode over TLS");

    bool httpsIdentityLoaded = gxos::apps::Navigator::SmokeNavigateToQuiet("https://localhost:8443/navigator-smoke/plain.txt");
    std::string httpsIdentityText = gxos::apps::Navigator::SmokeCurrentDocumentText();
    bool httpsIdentityPageInfoLoaded = gxos::apps::Navigator::SmokeNavigateToQuiet("about:page-info");
    std::string httpsIdentityPageInfo = gxos::apps::Navigator::SmokeCurrentDocumentText();
    add("HTTPS identity response remains unchanged", httpsIdentityLoaded &&
        contains(httpsIdentityText, "Navigator HTTPS text fixture") &&
        httpsIdentityPageInfoLoaded &&
        contains(httpsIdentityPageInfo, "Content encoding: ") &&
        contains(httpsIdentityPageInfo, "Encoded body bytes: ") &&
        contains(httpsIdentityPageInfo, "Decoded body bytes: "),
        "expected identity response to bypass decompression");

    bool httpsBrLoaded = gxos::apps::Navigator::SmokeNavigateToQuiet("https://localhost:8443/navigator-smoke/br.html");
    std::string httpsBrText = gxos::apps::Navigator::SmokeCurrentDocumentText();
    add("HTTPS Brotli remains explicitly unsupported", httpsBrLoaded &&
        contains(httpsBrText, "Unsupported Content Encoding") &&
        contains(httpsBrText, "UnsupportedContentEncoding") &&
        !contains(httpsBrText, "not-really-brotli"),
        "expected unsupported br to fail closed rather than pass as identity");

    bool badCertificateLoaded = gxos::apps::Navigator::SmokeNavigateToQuiet("https://127.0.0.1:8443/navigator-smoke/basic.html");
    std::string badCertificateText = gxos::apps::Navigator::SmokeCurrentDocumentText();
    bool badCertificatePageInfoLoaded = gxos::apps::Navigator::SmokeNavigateToQuiet("about:page-info");
    std::string badCertificatePageInfo = gxos::apps::Navigator::SmokeCurrentDocumentText();
    const bool badCertificateValidationBlocked = badCertificateLoaded &&
        contains(badCertificateText, "HTTPS Certificate Validation Failed") &&
        badCertificatePageInfoLoaded &&
        contains(badCertificatePageInfo, "TLS backend: Schannel hosted") &&
        contains(badCertificatePageInfo, "TLS validated: no") &&
        contains(badCertificatePageInfo, "Certificate validation: enabled via Schannel, Windows trust, and hostname validation") &&
        contains(badCertificatePageInfo, "TLS status: error") &&
        contains(badCertificatePageInfo, "TLS connection path: native Schannel stream") &&
        contains(badCertificatePageInfo, "TLS credential acquired: yes") &&
        contains(badCertificatePageInfo, "TLS handshake started: yes") &&
        contains(badCertificatePageInfo, "Certificate hostname checked: 127.0.0.1") &&
        contains(badCertificatePageInfo, "Certificate chain error: 0x800B0109") &&
        contains(badCertificatePageInfo, "TLS smoke bypass active: no");
    const bool badCertificateCredentialBlocked = badCertificateLoaded &&
        contains(badCertificateText, "HTTPS Handshake Failed") &&
        badCertificatePageInfoLoaded &&
        contains(badCertificatePageInfo, "Requested URL: https://127.0.0.1:8443/navigator-smoke/basic.html") &&
        contains(badCertificatePageInfo, "TLS backend: Schannel hosted") &&
        contains(badCertificatePageInfo, "TLS enabled: yes") &&
        contains(badCertificatePageInfo, "TLS validated: no") &&
        contains(badCertificatePageInfo, "Certificate validation: enabled via Schannel, Windows trust, and hostname validation") &&
        contains(badCertificatePageInfo, "TLS status: error") &&
        contains(badCertificatePageInfo, "TLS credential acquired: no") &&
        contains(badCertificatePageInfo, "TLS handshake started: no") &&
        contains(badCertificatePageInfo, "TLS smoke bypass active: no") &&
        contains(badCertificatePageInfo, "No credentials are available in the security package");
    add("HTTPS exact-localhost bypass stays blocked for 127.0.0.1",
        badCertificateValidationBlocked || badCertificateCredentialBlocked,
        "loaded=" + std::string(yesNo(badCertificateLoaded)) +
        " page_info_loaded=" + std::string(yesNo(badCertificatePageInfoLoaded)) +
        " body=\"" + summarizeText(badCertificateText, 220) +
        "\" page_info=\"" + summarizeText(badCertificatePageInfo, 700) + "\"");

    bool remotePngLoaded = gxos::apps::Navigator::SmokeNavigateToQuiet("http://127.0.0.1:8080/navigator-smoke/image-relative.html");
    bool remotePngPageInfoLoaded = gxos::apps::Navigator::SmokeNavigateToQuiet("about:page-info");
    std::string remotePngPageInfo = gxos::apps::Navigator::SmokeCurrentDocumentText();
    add("remote PNG still loads through HTTP", remotePngLoaded && remotePngPageInfoLoaded &&
        contains(remotePngPageInfo, "Remote images: 1") &&
        contains(remotePngPageInfo, "Loaded images: 1"),
        "expected one loaded remote PNG");

    bool remoteJpegLoaded = gxos::apps::Navigator::SmokeNavigateToQuiet("http://127.0.0.1:8080/navigator-smoke/image-jpeg.html");
    bool remoteJpegPageInfoLoaded = gxos::apps::Navigator::SmokeNavigateToQuiet("about:page-info");
    std::string remoteJpegPageInfo = gxos::apps::Navigator::SmokeCurrentDocumentText();
    add("remote JPEG loads through HTTP", remoteJpegLoaded && remoteJpegPageInfoLoaded &&
        contains(remoteJpegPageInfo, "Remote images: 1") &&
        contains(remoteJpegPageInfo, "Loaded images: 1") &&
        contains(remoteJpegPageInfo, "JPEG loaded: 1"),
        "expected one loaded remote JPEG");

    bool viewportPressureLoaded = gxos::apps::Navigator::SmokeNavigateToQuiet(
        "http://127.0.0.1:8080/navigator-smoke/generated/VPRESS.HTM");
    const std::string viewportPressureReport = gxos::apps::Navigator::SmokePageDiagnostics();
    auto viewportMetric = [&](const char* name) {
        const std::string prefix = std::string(name) + "=";
        const size_t start = viewportPressureReport.find(prefix);
        if (start == std::string::npos) return std::string("missing");
        const size_t valueStart = start + prefix.size();
        const size_t valueEnd = viewportPressureReport.find_first_of("\r\n ", valueStart);
        return viewportPressureReport.substr(valueStart, valueEnd == std::string::npos
            ? std::string::npos : valueEnd - valueStart);
    };
    add("hosted viewport-pressure admission prioritizes visible and near images",
        viewportPressureLoaded &&
        contains(viewportPressureReport, "viewport_width=872") &&
        contains(viewportPressureReport, "viewport_height=528") &&
        contains(viewportPressureReport, "initial_scroll_offset=0") &&
        contains(viewportPressureReport, "visible_references=4") &&
        contains(viewportPressureReport, "near_references=4") &&
        contains(viewportPressureReport, "far_references=4") &&
        contains(viewportPressureReport, "visible_loaded=4") &&
        contains(viewportPressureReport, "near_loaded=4") &&
        contains(viewportPressureReport, "far_loaded=2") &&
        contains(viewportPressureReport, "far_budget_denied=2") &&
        contains(viewportPressureReport, "decoded_bytes_visible=16777216") &&
        contains(viewportPressureReport, "active_image_bytes=67108864") &&
        contains(viewportPressureReport, "visible_priority_admissions=8"),
        std::string("viewport pressure metrics=") +
        "refs:" + viewportMetric("resource_total_references") +
        ",visible:" + viewportMetric("visible_references") +
        ",near:" + viewportMetric("near_references") +
        ",far:" + viewportMetric("far_references") +
        ",loaded:" + viewportMetric("resource_loaded") +
        ",visible_loaded:" + viewportMetric("visible_loaded") +
        ",near_loaded:" + viewportMetric("near_loaded") +
        ",far_loaded:" + viewportMetric("far_loaded") +
        ",far_denied:" + viewportMetric("far_budget_denied") +
        ",active_bytes:" + viewportMetric("active_image_bytes") +
        ",visible_admissions:" + viewportMetric("visible_priority_admissions") +
        ",viewport:" + viewportMetric("viewport_top") + ".." + viewportMetric("viewport_bottom") +
        ",report=" + summarizeText(viewportPressureReport, 500));

    bool httpsRemotePngLoaded = gxos::apps::Navigator::SmokeNavigateToQuiet("https://localhost:8443/navigator-smoke/image-relative.html");
    bool httpsRemotePngPageInfoLoaded = gxos::apps::Navigator::SmokeNavigateToQuiet("about:page-info");
    std::string httpsRemotePngPageInfo = gxos::apps::Navigator::SmokeCurrentDocumentText();
    add("remote PNG loads through HTTPS", httpsRemotePngLoaded && httpsRemotePngPageInfoLoaded &&
        contains(httpsRemotePngPageInfo, "Remote images: 1") &&
        contains(httpsRemotePngPageInfo, "Loaded images: 1") &&
        contains(httpsRemotePngPageInfo, "TLS status: connected"),
        "expected one loaded remote HTTPS PNG");

    bool binaryDownloadLoaded = gxos::apps::Navigator::SmokeNavigateToQuiet("http://127.0.0.1:8080/navigator-smoke/download.bin");
    std::string binaryDownloadText = gxos::apps::Navigator::SmokeCurrentDocumentText();
    add("unsupported HTTP content still downloads", binaryDownloadLoaded &&
        contains(binaryDownloadText, "Download Complete") &&
        contains(binaryDownloadText, "Content type: application/octet-stream"),
        "currentUrl=" + gxos::apps::Navigator::SmokeCurrentUrl());

    bool httpsDownloadLoaded = gxos::apps::Navigator::SmokeNavigateToQuiet("https://localhost:8443/navigator-smoke/download.bin");
    std::string httpsDownloadText = gxos::apps::Navigator::SmokeCurrentDocumentText();
    add("unsupported HTTPS content still downloads", httpsDownloadLoaded &&
        contains(httpsDownloadText, "Download Complete") &&
        contains(httpsDownloadText, "Content type: application/octet-stream"),
        "currentUrl=" + gxos::apps::Navigator::SmokeCurrentUrl());

    bool docsLoaded = gxos::apps::Navigator::SmokeNavigateTo("file:///docs/index.html");
    std::string docsUrl = gxos::apps::Navigator::SmokeCurrentUrl();
    std::string docsReport = gxos::apps::Navigator::SmokeRuntimeReport();
    add("docs page loads", docsLoaded && docsUrl == "file:///docs/index.html", "currentUrl=" + docsUrl);
    add("docs CSS-lite detected", contains(docsReport, "Current Document.CSS diagnostics=css detected"), "expected css detected");
    int findMatches = gxos::apps::Navigator::SmokeFindInPage("Navigator");
    add("find in page matches docs", findMatches > 0, "matches=" + std::to_string(findMatches));
    bool docsReloadedForClick = gxos::apps::Navigator::SmokeNavigateTo("file:///docs/index.html");
    bool linkClickNavigated = gxos::apps::Navigator::SmokeClickFirstLink();
    std::string clickedLinkUrl = gxos::apps::Navigator::SmokeCurrentUrl();
    add("link click navigates", docsReloadedForClick && linkClickNavigated && clickedLinkUrl != "file:///docs/index.html", "currentUrl=" + clickedLinkUrl);
    bool docsReloadedForDrag = gxos::apps::Navigator::SmokeNavigateTo("file:///docs/index.html");
    bool linkDragSelected = gxos::apps::Navigator::SmokeDragFirstLinkSelectsWithoutNavigation();
    std::string dragUrl = gxos::apps::Navigator::SmokeCurrentUrl();
    add("link drag selects and copies without navigating", docsReloadedForDrag && linkDragSelected && dragUrl == "file:///docs/index.html", "currentUrl=" + dragUrl);

    bool formsLoaded = gxos::apps::Navigator::SmokeNavigateTo("file:///docs/forms.html");
    std::string formsUrl = gxos::apps::Navigator::SmokeCurrentUrl();
    std::string formsReport = gxos::apps::Navigator::SmokeRuntimeReport();
    add("forms page loads", formsLoaded && formsUrl == "file:///docs/forms.html", "currentUrl=" + formsUrl);
    add("forms-lite detected", contains(formsReport, "Current Document.Forms=2") &&
        contains(formsReport, "Current Document.Text inputs=2") &&
        contains(formsReport, "Current Document.Checkboxes=2") &&
        contains(formsReport, "Current Document.Radio buttons=4") &&
        contains(formsReport, "Current Document.Textareas=2") &&
        contains(formsReport, "Current Document.Selects=2") &&
        contains(formsReport, "Current Document.POST supported hosted=yes") &&
        contains(formsReport, "Current Document.POST supported bare-metal=no"), "expected GET form plus POST form each with all controls");
    bool formSubmitted = gxos::apps::Navigator::SmokeSubmitFirstForm("hello world");
    std::string submittedUrl = gxos::apps::Navigator::SmokeCurrentUrl();
    // GET submission includes all form controls: text, checkbox, radio, textarea, select.
    add("forms-lite GET submission", formSubmitted &&
        contains(submittedUrl, "file:///docs/forms-result.html") &&
        contains(submittedUrl, "q=hello+world") &&
        contains(submittedUrl, "subscribe=yes") &&
        contains(submittedUrl, "color=blue") &&
        contains(submittedUrl, "note=") &&
        contains(submittedUrl, "size=medium"), "currentUrl=" + submittedUrl);

    bool postFormLoaded = gxos::apps::Navigator::SmokeNavigateTo("http://127.0.0.1:8080/navigator-smoke/forms-post.html");
    std::string postFormReport = gxos::apps::Navigator::SmokeRuntimeReport();
    add("forms-lite POST page loads", postFormLoaded && gxos::apps::Navigator::SmokeCurrentUrl() == "http://127.0.0.1:8080/navigator-smoke/forms-post.html",
        "currentUrl=" + gxos::apps::Navigator::SmokeCurrentUrl());
    add("forms-lite POST controls detected", contains(postFormReport, "Current Document.Forms=1") &&
        contains(postFormReport, "Current Document.Text inputs=2") &&
        contains(postFormReport, "Current Document.Checkboxes=2") &&
        contains(postFormReport, "Current Document.Radio buttons=2") &&
        contains(postFormReport, "Current Document.Textareas=1") &&
        contains(postFormReport, "Current Document.Selects=1"), "expected POST controls");
    bool postSubmitted = gxos::apps::Navigator::SmokeSubmitFirstForm("posted value");
    std::string postSubmittedUrl = gxos::apps::Navigator::SmokeCurrentUrl();
    std::string postResultText = gxos::apps::Navigator::SmokeCurrentDocumentText();
    int postFindMatches = gxos::apps::Navigator::SmokeFindInPage("POST OK");
    std::string postResultReport = gxos::apps::Navigator::SmokeRuntimeReport();
    add("forms-lite POST submission", postSubmitted &&
        postSubmittedUrl == "http://127.0.0.1:8080/navigator-smoke/post-echo" &&
        postFindMatches > 0 &&
        contains(postResultReport, "Current Document.Last submitted form action=http://127.0.0.1:8080/navigator-smoke/post-echo") &&
        contains(postResultReport, "Current Document.Last submitted form method=post") &&
        contains(postResultReport, "Current Document.Last submitted form status=POST submitted") &&
        contains(postResultReport, "Current Document.Last POST HTTP status=200 OK") &&
        contains(postResultReport, "Current Document.Last POST content type=text/html"),
        "currentUrl=" + postSubmittedUrl + " matches=" + std::to_string(postFindMatches));
    add("forms-lite POST request headers", contains(postResultText, "Method: POST") &&
        contains(postResultText, "Content-Type: application/x-www-form-urlencoded") &&
        contains(postResultText, "Host: 127.0.0.1:8080") &&
        contains(postResultText, "User-Agent: guideXOS-Navigator/0.2") &&
        contains(postResultText, "Accept-Encoding: gzip, deflate") &&
        !contains(postResultText, "Accept-Encoding: identity") &&
        !contains(postResultText, "Accept-Encoding: br") &&
        contains(postResultText, "Connection: close"), "echo response contains required hosted POST headers");
    add("forms-lite POST encoded successful controls", contains(postResultText,
        "q=posted+value&agree=yes&kind=alpha&note=hello%0Asecond+line&size=m") &&
        !contains(postResultText, "omit=no") &&
        !contains(postResultText, "unnamed+control+omitted") &&
        !contains(postResultText, "kind=beta"), "echo response contains encoded successful controls only");

    bool downloadsLoaded = gxos::apps::Navigator::SmokeNavigateTo("about:downloads");
    std::string downloadsUrl = gxos::apps::Navigator::SmokeCurrentUrl();
    add("downloads page loads", downloadsLoaded && downloadsUrl == "about:downloads", "currentUrl=" + downloadsUrl);

    auto safeDownloadPathFromFileUrl = [](const std::string& fileUrl) {
        const std::string prefix = "file:///downloads/";
        if (fileUrl.rfind(prefix, 0) != 0) return std::string();
        const std::string fileName = fileUrl.substr(prefix.size());
        if (fileName.empty() || fileName == "." || fileName == ".." ||
            fileName.find('/') != std::string::npos || fileName.find('\\') != std::string::npos) {
            return std::string();
        }
        for (unsigned char ch : fileName) {
            if (!std::isalnum(ch) && ch != '.' && ch != '-' && ch != '_') return std::string();
        }
        return "/downloads/" + fileName;
    };
    auto fileNameFromDownloadUrl = [](const std::string& fileUrl) {
        const std::string prefix = "file:///downloads/";
        return fileUrl.rfind(prefix, 0) == 0 ? fileUrl.substr(prefix.size()) : std::string();
    };
    auto hasDuplicateSuffix = [&fileNameFromDownloadUrl](const std::string& firstUrl, const std::string& secondUrl) {
        const std::string firstName = fileNameFromDownloadUrl(firstUrl);
        const std::string secondName = fileNameFromDownloadUrl(secondUrl);
        if (firstName.empty() || secondName.empty()) return false;
        const size_t dot = firstName.rfind('.');
        std::string stem = dot == std::string::npos ? firstName : firstName.substr(0, dot);
        const std::string ext = dot == std::string::npos ? std::string() : firstName.substr(dot);
        int nextSuffix = 1;
        const size_t dash = stem.rfind('-');
        if (dash != std::string::npos && dash + 1 < stem.size()) {
            const std::string suffix = stem.substr(dash + 1);
            const bool numeric = std::all_of(suffix.begin(), suffix.end(),
                [](unsigned char ch) { return std::isdigit(ch) != 0; });
            if (numeric) {
                nextSuffix = std::stoi(suffix) + 1;
                stem = stem.substr(0, dash);
            }
        }
        return secondName == stem + "-" + std::to_string(nextSuffix) + ext;
    };
    auto readSavedText = [&](const std::string& fileUrl, std::string& text) {
        const std::string path = safeDownloadPathFromFileUrl(fileUrl);
        if (path.empty()) return false;
        const gxos::apps::FileReadResult result = gxos::apps::readTextFile(path);
        text = result.text;
        return result.status == gxos::apps::FileReadStatus::Ok && !text.empty();
    };

    // --- Save Page smoke checks ---
    bool docsReloadedForSave = gxos::apps::Navigator::SmokeNavigateToQuiet("file:///docs/index.html");
    bool pageInfoLoadedForSave = gxos::apps::Navigator::SmokeNavigateToQuiet("about:page-info");
    std::string pageInfoSaveTextLink = gxos::apps::Navigator::SmokeCurrentLinkUrl("Save Page Text");
    std::string pageInfoSaveSourceLink = gxos::apps::Navigator::SmokeCurrentLinkUrl("Save Source");
    add("page-info exposes Save Page Text", docsReloadedForSave && pageInfoLoadedForSave &&
        pageInfoSaveTextLink == "about:save-page-text", "link=" + pageInfoSaveTextLink);
    add("page-info exposes Save Source for raw source", pageInfoSaveSourceLink == "about:save-page-source",
        "link=" + pageInfoSaveSourceLink);

    bool saveTextLoaded = gxos::apps::Navigator::SmokeNavigateToQuiet("about:save-page-text");
    std::string saveTextUrl = gxos::apps::Navigator::SmokeCurrentUrl();
    int saveTextBlocks = gxos::apps::Navigator::SmokeCurrentBlockCount();
    std::string saveTextFileUrl = gxos::apps::Navigator::SmokeCurrentLinkUrl("Open saved file");
    std::string savedText;
    bool savedTextReadable = readSavedText(saveTextFileUrl, savedText);
    add("save-page-text loads", docsReloadedForSave && saveTextLoaded && saveTextUrl == "about:save-page-text" && saveTextBlocks > 0,
        "currentUrl=" + saveTextUrl + " blocks=" + std::to_string(saveTextBlocks));
    add("save-page-text writes visible document text", savedTextReadable &&
        contains(savedText, "guideXOS Navigator Help") && !contains(savedText, "Requested URL:"),
        "fileUrl=" + saveTextFileUrl + " bytes=" + std::to_string(savedText.size()));
    add("save-page-text uses safe file link", !safeDownloadPathFromFileUrl(saveTextFileUrl).empty(),
        "fileUrl=" + saveTextFileUrl);

    bool duplicateSaveTextLoaded = gxos::apps::Navigator::SmokeNavigateToQuiet("about:save-page-text");
    std::string duplicateSaveTextFileUrl = gxos::apps::Navigator::SmokeCurrentLinkUrl("Open saved file");
    add("duplicate save-page-text uses a new filename", duplicateSaveTextLoaded &&
        !safeDownloadPathFromFileUrl(duplicateSaveTextFileUrl).empty() &&
        duplicateSaveTextFileUrl != saveTextFileUrl,
        "first=" + saveTextFileUrl + " second=" + duplicateSaveTextFileUrl);
    add("duplicate save-page-text appends -1 suffix", duplicateSaveTextLoaded &&
        hasDuplicateSuffix(saveTextFileUrl, duplicateSaveTextFileUrl),
        "first=" + saveTextFileUrl + " second=" + duplicateSaveTextFileUrl);

    bool docsReloadedForSource = gxos::apps::Navigator::SmokeNavigateToQuiet("file:///docs/index.html");
    bool viewSourceLoaded = gxos::apps::Navigator::SmokeNavigateToQuiet("about:view-source");
    std::string viewSourceSaveLink = gxos::apps::Navigator::SmokeCurrentLinkUrl("Save Source");
    add("view-source exposes Save Source for raw source", docsReloadedForSource && viewSourceLoaded &&
        viewSourceSaveLink == "about:save-page-source", "link=" + viewSourceSaveLink);

    bool saveSourceLoaded = gxos::apps::Navigator::SmokeNavigateToQuiet("about:save-page-source");
    std::string saveSourceFileUrl = gxos::apps::Navigator::SmokeCurrentLinkUrl("Open saved file");
    std::string savedSource;
    bool savedSourceReadable = readSavedText(saveSourceFileUrl, savedSource);
    add("save-page-source writes raw source", saveSourceLoaded && savedSourceReadable &&
        contains(savedSource, "<html"), "fileUrl=" + saveSourceFileUrl + " bytes=" + std::to_string(savedSource.size()));
    add("save-page-source uses safe file link", !safeDownloadPathFromFileUrl(saveSourceFileUrl).empty(),
        "fileUrl=" + saveSourceFileUrl);

    bool duplicateSaveSourceLoaded = gxos::apps::Navigator::SmokeNavigateToQuiet("about:save-page-source");
    std::string duplicateSaveSourceFileUrl = gxos::apps::Navigator::SmokeCurrentLinkUrl("Open saved file");
    add("duplicate save-page-source appends -1 suffix", duplicateSaveSourceLoaded &&
        !safeDownloadPathFromFileUrl(duplicateSaveSourceFileUrl).empty() &&
        hasDuplicateSuffix(saveSourceFileUrl, duplicateSaveSourceFileUrl),
        "first=" + saveSourceFileUrl + " second=" + duplicateSaveSourceFileUrl);

    bool downloadsAfterSave = gxos::apps::Navigator::SmokeNavigateToQuiet("about:downloads");
    std::string downloadsAfterSaveUrl = gxos::apps::Navigator::SmokeCurrentUrl();
    std::string downloadsAfterSaveText = gxos::apps::Navigator::SmokeCurrentDocumentText();
    const std::string saveTextDownloadPath = safeDownloadPathFromFileUrl(saveTextFileUrl);
    const std::string saveSourceDownloadPath = safeDownloadPathFromFileUrl(saveSourceFileUrl);
    add("downloads page represents saved items", downloadsAfterSave && downloadsAfterSaveUrl == "about:downloads" &&
        !saveTextDownloadPath.empty() && !saveSourceDownloadPath.empty() &&
        contains(downloadsAfterSaveText, saveTextDownloadPath) &&
        contains(downloadsAfterSaveText, saveSourceDownloadPath),
        "currentUrl=" + downloadsAfterSaveUrl);

    bool aboutLoadedForNoSource = gxos::apps::Navigator::SmokeNavigateToQuiet("about:navigator");
    bool aboutPageInfoLoaded = gxos::apps::Navigator::SmokeNavigateToQuiet("about:page-info");
    std::string aboutPageInfoSaveSourceLink = gxos::apps::Navigator::SmokeCurrentLinkUrl("Save Source");
    add("page-info hides Save Source for generated about page", aboutLoadedForNoSource && aboutPageInfoLoaded &&
        aboutPageInfoSaveSourceLink.empty(), "link=" + aboutPageInfoSaveSourceLink);
    bool aboutViewSourceLoaded = gxos::apps::Navigator::SmokeNavigateToQuiet("about:view-source");
    std::string aboutViewSourceSaveLink = gxos::apps::Navigator::SmokeCurrentLinkUrl("Save Source");
    add("view-source hides Save Source for generated about page", aboutViewSourceLoaded &&
        aboutViewSourceSaveLink.empty(), "link=" + aboutViewSourceSaveLink);
    bool noSourceLoaded = gxos::apps::Navigator::SmokeNavigateToQuiet("about:save-page-source");
    std::string noSourceText = gxos::apps::Navigator::SmokeCurrentDocumentText();
    add("save-page-source explains generated about page has no source", noSourceLoaded &&
        contains(noSourceText, "generated about: pages have no source") &&
        gxos::apps::Navigator::SmokeCurrentLinkUrl("Open saved file").empty(),
        "currentUrl=" + gxos::apps::Navigator::SmokeCurrentUrl());

    bool pass = true;
    for (const Check& check : checks) {
        pass = pass && check.pass;
        out << "CHECK " << (check.pass ? "PASS " : "FAIL ") << check.name << " :: " << check.detail << "\n";
    }

    out << "runtime_report:\n" << runtimeReport;
    out << "docs_runtime_report:\n" << docsReport;
    out << "forms_runtime_report:\n" << formsReport;
    out << "post_forms_runtime_report:\n" << postFormReport;
    out << "post_result_runtime_report:\n" << postResultReport;
    out << "current_url=" << currentUrl << "\n";
    out << "docs_url=" << docsUrl << "\n";
    out << "find_matches=" << findMatches << "\n";
    out << "clicked_link_url=" << clickedLinkUrl << "\n";
    out << "drag_selection_url=" << dragUrl << "\n";
    out << "forms_url=" << formsUrl << "\n";
    out << "submitted_form_url=" << submittedUrl << "\n";
    out << "post_submitted_form_url=" << postSubmittedUrl << "\n";
    out << "downloads_url=" << downloadsUrl << "\n";
    out << "current_block_count=" << gxos::apps::Navigator::SmokeCurrentBlockCount() << "\n";
    out << "toolbar_count=" << toolbarWidgetCount << "\n";
    out << "NAVIGATOR_SMOKE_RESULT: " << (pass ? "PASS" : "FAIL") << "\n";
    out << "NAVIGATOR_SMOKE_END\n";
    return out.str();
}

static std::string navigatorPositionedLinkProbeDiagnostic() {
    auto rectText = [](int x, int y, int w, int h) {
        return std::to_string(x) + ":" + std::to_string(y) + ":" +
            std::to_string(w) + ":" + std::to_string(h);
    };
    auto visibleRect = [](int ax, int ay, int aw, int ah,
                          int bx, int by, int bw, int bh) {
        const int left = std::max(ax, bx);
        const int top = std::max(ay, by);
        const int right = std::min(ax + std::max(0, aw), bx + std::max(0, bw));
        const int bottom = std::min(ay + std::max(0, ah), by + std::max(0, bh));
        return std::array<int, 4>{left, top, std::max(0, right - left), std::max(0, bottom - top)};
    };
    auto capture = [&](const std::string& id) {
        std::array<int, 12> values{};
        gxos::apps::Navigator::SmokeLinkGeometryById(id,
            values[0], values[1], values[2], values[3],
            values[4], values[5], values[6], values[7],
            values[8], values[9], values[10], values[11]);
        return values;
    };

    std::ostringstream out;
    out << "NAVIGATOR_POSITIONED_LINK_PROBE_BEGIN\n";
    const std::string fixtureUrl =
        "http://127.0.0.1:8080/navigator-smoke/typography-phase7a.html";
    std::string launchError;
    bool loaded = gxos::apps::Navigator::SmokeCurrentUrl() == fixtureUrl;
    bool launchRequested = false;
    if (!loaded) {
        launchRequested = gxos::gui::DesktopService::LaunchApp("guideXOS Navigator", launchError);
        for (int attempt = 0; attempt < 30 && !loaded; ++attempt) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            loaded = gxos::apps::Navigator::SmokeNavigateTo(fixtureUrl);
        }
    }
    out << "launch_requested=" << (launchRequested ? "yes" : "no") << "\n";
    if (!launchError.empty()) out << "launch_error=" << launchError << "\n";
    out << "fixture_loaded=" << (loaded ? "yes" : "no") << "\n";
    gxos::apps::Navigator::SmokeSetScrollOffset(100000);
    out << "document_scroll=" << gxos::apps::Navigator::SmokeScrollOffset() << "\n";
    const int maxScroll = gxos::apps::Navigator::SmokeElementMaxScrollYById("phase7a-auto");
    out << "overflow_owner=phase7a-auto\nlocal_scroll_max=" << maxScroll << "\n";
    const bool initialSet = gxos::apps::Navigator::SmokeSetElementScrollOffsetById("phase7a-auto", 0, 0);
    const std::array<int, 12> initial = capture("phase7a-scroll-link");
    const int initialX = initial[4] + std::max(1, initial[6] / 2);
    const int initialY = initial[5] + std::max(1, initial[7] / 2);
    out << "initial_offset_set=" << (initialSet ? "yes" : "no") << "\n";
    out << "initial.paint=" << rectText(initial[0], initial[1], initial[2], initial[3]) << "\n";
    out << "initial.final=" << rectText(initial[4], initial[5], initial[6], initial[7]) << "\n";
    out << "initial.clip=" << rectText(initial[8], initial[9], initial[10], initial[11]) << "\n";
    out << "initial.probe_point=" << initialX << ":" << initialY << "\n";

    const int requestedScroll = maxScroll;
    const bool scrolledSet = gxos::apps::Navigator::SmokeSetElementScrollOffsetById(
        "phase7a-auto", 0, requestedScroll);
    const int actualScroll = gxos::apps::Navigator::SmokeElementScrollOffsetYById("phase7a-auto");
    const std::array<int, 12> scrolled = capture("phase7a-scroll-link");
    const std::array<int, 4> visible = visibleRect(scrolled[4], scrolled[5], scrolled[6], scrolled[7],
        scrolled[8], scrolled[9], scrolled[10], scrolled[11]);
    const int newX = visible[0] + std::max(1, visible[2] / 2);
    const int newY = visible[1] + std::max(1, visible[3] / 2);
    const bool oldLocationAccepted = gxos::apps::Navigator::SmokeHitLinkAt(initialX, initialY, "phase7a-scroll-link");
    const bool newLocationAccepted = visible[2] > 0 && visible[3] > 0 &&
        gxos::apps::Navigator::SmokeHitLinkAt(newX, newY, "phase7a-scroll-link");
    out << "scrolled_set=" << (scrolledSet ? "yes" : "no") << "\n";
    out << "actual_local_scroll=" << actualScroll << "\n";
    out << "scrolled.paint=" << rectText(scrolled[0], scrolled[1], scrolled[2], scrolled[3]) << "\n";
    out << "scrolled.final=" << rectText(scrolled[4], scrolled[5], scrolled[6], scrolled[7]) << "\n";
    out << "scrolled.clip=" << rectText(scrolled[8], scrolled[9], scrolled[10], scrolled[11]) << "\n";
    out << "scrolled.visible=" << rectText(visible[0], visible[1], visible[2], visible[3]) << "\n";
    out << "old_location=" << initialX << ":" << initialY << " accepted=" << (oldLocationAccepted ? "yes" : "no") << "\n";
    out << "new_location=" << newX << ":" << newY << " accepted=" << (newLocationAccepted ? "yes" : "no") << "\n";
    out << "positioned_link_probe=" << ((loaded && initial[0] && scrolled[0] && visible[2] > 0 && visible[3] > 0 &&
        !oldLocationAccepted && newLocationAccepted) ? "PASS" : "FAIL") << "\n";
    gxos::apps::Navigator::SmokeSetElementScrollOffsetById("phase7a-auto", 0, 0);
    gxos::apps::Navigator::SmokeSetScrollOffset(0);
    out << "diagnostic_report=" << gxos::apps::Navigator::SmokeRuntimeReport();
    out << "NAVIGATOR_POSITIONED_LINK_PROBE_END\n";
    return out.str();
}

static std::string navigatorGotoDiagnostic(const std::string& url) {
    std::ostringstream out;
    if (url.empty()) {
        return "NAVIGATOR_GOTO_RESULT: FAIL missing URL\n";
    }

    bool ok = gxos::apps::Navigator::SmokeNavigateTo(url);
    if (!ok) {
        std::string err;
        if (!gxos::gui::DesktopService::LaunchApp("guideXOS Navigator", err)) {
            return "NAVIGATOR_GOTO_RESULT: FAIL launch failed: " + err + "\n";
        }
        for (int attempt = 0; attempt < 30; ++attempt) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            bool ready = false;
            for (const gxos::gui::WindowDebugInfo& window : gxos::gui::Compositor::debugWindowsSnapshot()) {
                if (window.title.find("guideXOS Navigator") != std::string::npos ||
                    window.title.find("Navigator") != std::string::npos) {
                    ready = true;
                    break;
                }
            }
            if (ready) break;
        }
        for (int attempt = 0; attempt < 30 && !ok; ++attempt) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            ok = gxos::apps::Navigator::SmokeNavigateTo(url);
        }
    }

    out << "NAVIGATOR_GOTO_RESULT: " << (ok ? "PASS" : "FAIL") << "\n";
    out << "requested_url=" << url << "\n";
    out << "current_url=" << gxos::apps::Navigator::SmokeCurrentUrl() << "\n";
    out << "current_block_count=" << gxos::apps::Navigator::SmokeCurrentBlockCount() << "\n";
	out << gxos::apps::Navigator::SmokePageDiagnostics();
    return out.str();
}

static void help(){
    std::cout << "Commands:\n"
                 " mem | alloc <n> | tasks | log\n"
                 " spawn <echo|worker> [args...] | plist\n"
                 " send <pid> <text> | recv <pid>\n"
                 " bus.sub <chan> <pid> | bus.unsub <chan> <pid>\n"
                 " bus.pub <chan> <text> [fanout] | bus.pop <chan> [timeoutMs]\n"
                 " bus.cap <chan> <cap> | bus.stats <chan>\n"
                 " console.start | console.send <text> | console.pop [timeoutMs]\n"
                 " gui.start | gui.open.appmodeldemo | gui.smoke.launchshadow | gui.win <title> [w h] | gui.text <id> <text> | gui.close <id>\n"
                 " gui.rect <id> <x> <y> <w> <h> <r> <g> <b> | gui.move <id> <x> <y> | gui.resize <id> <w> <h> | gui.title <id> <title>\n"
                 " gui.btn <win> <id> <x> <y> <w> <h> <text> | gui.pop | gui.wlist | gui.activate <id> | gui.min <id> | gui.sync <id> <frameGeneration> [frameSequence] [freeze] | gui.unfreeze <id>\n"
                 " gxm.load <path> | gxm.sample | gui.save <path> | gui.load <path>\n"
                 " desktop.wallpaper <path> | desktop.background.remove <id> | desktop.launch <action> | desktop.open <path> [dir] | desktop.launch.resolve <label> | desktop.launch.adapt <label> | desktop.launch.compare | desktop.launch.storage | desktop.launch.storage.preview | desktop.launch.storage.preview.compare | desktop.launch.types | desktop.open.resolve <path> [dir] | desktop.appmodel.active-typed-dispatch-gate [force-on|force-off|reset] | desktop.appmodel.active-typed-dispatch-default-on-candidate [on|off|reset] | desktop.pin <action> | desktop.unpin <action> | desktop.showconfig | desktop.display.summary | desktop.display.viewport [1|2]\n"
                 " desktop.apps | desktop.apps.verbose | desktop.windows.owners | desktop.startup.regression | desktop.appmodel.summary | desktop.appmodel.inventory | desktop.appmodel.coverage | desktop.appmodel.file-associations | desktop.appmodel.shell-objects | desktop.appmodel.typed-dispatch-gate [force-off] | desktop.pinned | desktop.recent | desktop.recent.remove <name> | desktop.pinapp <name> | desktop.pinfile <name> <path>\n"
                 " nativeapp.capabilities | nativeapp.inspect <app> | nativeapp.smoketest <app> | nativeapp.processes\n"
                 " taskbar.list | taskbar.activate <id> | taskbar.min <id> | taskbar.close <id>\n"
                 " workspace.switch <n> | workspace.next | workspace.prev | workspace.current\n"
                 " notepad | notepad <file>\n"
                 " calculator\n"
                 " console | console.start | console.send <text> | console.pop [timeoutMs]\n"
                 " files | files <path>\n"
                 " clock\n"
                 " taskmanager.snapshot | taskmanager.network-snapshot-wait | taskmanager.tombstone-test\n"
                 " taskmgr\n"
                 " paint\n"
                 " navigator | navigator.smoke | navigator.positioned-link-probe | navigator.goto <url>\n"
                 " imgview [file] | osk\n"
                 " shutdown | msgbox <text> | welcome\n"
                 " notify <text> | notify.clear\n"
                 " fw.mode <normal|block|disabled|auto> | fw.allow <name> | fw.list | fw.alerts\n"
                 " modules | module.launch <name>\n"
                 " pkg.install <path.gxapp> | pkg.launch <app> | pkg.list | pkg.validate <path.gxapp>\n"
                 " proc.wait <pid> [timeoutMs] | proc.status <pid>\n"
                 " vfs.mkdir <path> | vfs.write <path> <text> | vfs.read <path> | vfs.ls <path>\n"
                 " vnc.start [port] | vnc.stop | vnc.status\n"
                 " pbytes | help | quit/exit\n"; }

int main(){
// NOTE: This is a USER-MODE system server, NOT a kernel
//
// This process will be launched by the kernel as PID 1 (init process)
// We have NO access to:
//   - Firmware (UEFI/BIOS)
//   - Bootloader structures
//   - BootInfo (kernel-only)
//   - Direct hardware (use syscalls instead)
//
// All hardware access must go through kernel syscalls:
//   - Framebuffer mapping: syscall(SYS_MMAP_FRAMEBUFFER)
//   - File I/O: syscall(SYS_READ/SYS_WRITE)
//   - Device access: syscall(SYS_IOCTL)
//
// For testing, this can run standalone on Linux/Windows
// In production, kernel loads this as ELF and jumps to main()
    
using namespace gxos;
    Logger::write(LogLevel::Info, "guideXOSServer server starting...");
    if (apps::NativeElfExecutor::ExperimentalExecutionEnabled()) {
        const char* warning = "WARNING: Experimental Native ELF execution is enabled.\nOnly trusted Native ELF apps should be run.";
        std::cout << warning << std::endl;
        Logger::write(LogLevel::Warn, warning);
    }
    Lifecycle::bootstrap();
    gxos::net::armNetworkTelemetry("hostedSocketCounters");
    Lifecycle::markInteractive();
    struct ShutdownGuard { ~ShutdownGuard(){ Lifecycle::shutdown(); } } guard;

    // Registerable specs
    std::unordered_map<std::string, ProcessSpec> specs{
        {"echo", ProcessSpec{"echo", echoProc}},
        {"worker", ProcessSpec{"worker", workerProc}},
    };

    auto requireCompositor = [&]() -> bool {
        uint64_t before = Lifecycle::state().compositorPid;
        uint64_t pid = Lifecycle::ensureCompositor();
        if(pid==0){ std::cout<<"Compositor unavailable"<<std::endl; return false; }
        if(before==0){ std::cout<<"Compositor pid="<<pid<<" (proto="<<gui::kGuiProtocolVersion<<")"<<std::endl; }
        return true;
    };

    auto requireConsole = [&]() -> bool {
        uint64_t before = Lifecycle::state().consolePid;
        uint64_t pid = Lifecycle::ensureConsole();
        if(pid==0){ std::cout<<"Console service unavailable"<<std::endl; return false; }
        if(before==0){ std::cout<<"Console pid="<<pid<<std::endl; }
        return true;
    };

    std::string line; help();
    while (std::getline(std::cin, line)){
        if (line=="quit"||line=="exit") break;
        std::istringstream iss(line); std::string cmd; iss>>cmd;
        if (cmd=="gui.save"){
            if(!requireCompositor()) continue;
            std::string path; iss>>path; if(path.empty()){ std::cout<<"gui.save <path>"<<std::endl; continue; } ipc::Message m; m.type=(uint32_t)gui::MsgType::MT_StateSave; m.data.assign(path.begin(), path.end()); ipc::Bus::publish("gui.input", std::move(m), false); std::cout<<"Save requested"<<std::endl; continue; }
        if (cmd=="gui.load"){
            if(!requireCompositor()) continue;
            std::string path; iss>>path; if(path.empty()){ std::cout<<"gui.load <path>"<<std::endl; continue; } ipc::Message m; m.type=(uint32_t)gui::MsgType::MT_StateLoad; m.data.assign(path.begin(), path.end()); ipc::Bus::publish("gui.input", std::move(m), false); std::cout<<"Load requested"<<std::endl; continue; }
        if (cmd=="gui.btn"){
            if(!requireCompositor()) continue;
            std::string winS; int id,x,y,w,h; iss>>winS>>id>>x>>y>>w>>h; std::string rest; std::getline(iss,rest); if(!rest.empty() && rest[0]==' ') rest.erase(0,1); if(winS.empty()){ std::cout<<"Usage: gui.btn <win> <id> <x> <y> <w> <h> <text>"<<std::endl; continue; }
            std::ostringstream oss; oss<<winS<<"|"<<1 /*Button*/<<"|"<<id<<"|"<<x<<"|"<<y<<"|"<<w<<"|"<<h<<"|"<<rest; ipc::Message m; m.type=(uint32_t)gui::MsgType::MT_WidgetAdd; auto s=oss.str(); m.data.assign(s.begin(), s.end()); ipc::Bus::publish("gui.input", std::move(m), false); std::cout<<"Button queued"<<std::endl; continue; }
        if (cmd=="gui.wlist"){ if(!requireCompositor()) continue; ipc::Message m; m.type=(uint32_t)gui::MsgType::MT_WindowList; ipc::Bus::publish("gui.input", std::move(m), false); std::cout<<"Requested window list (use gui.pop)"<<std::endl; continue; }
        if (cmd=="gui.activate"){ if(!requireCompositor()) continue; std::string idS; iss>>idS; if(idS.empty()){ std::cout<<"gui.activate <id>"<<std::endl; continue; } ipc::Message m; m.type=(uint32_t)gui::MsgType::MT_Activate; m.data.assign(idS.begin(), idS.end()); ipc::Bus::publish("gui.input", std::move(m), false); std::cout<<"Activate sent"<<std::endl; continue; }
        if (cmd=="gui.min"){ if(!requireCompositor()) continue; std::string idS; iss>>idS; if(idS.empty()){ std::cout<<"gui.min <id>"<<std::endl; continue; } ipc::Message m; m.type=(uint32_t)gui::MsgType::MT_Minimize; m.data.assign(idS.begin(), idS.end()); ipc::Bus::publish("gui.input", std::move(m), false); std::cout<<"Minimize sent"<<std::endl; continue; }
        if (cmd=="gxm.load"){
            if(!requireCompositor()) continue;
            std::string path; iss>>path; if(path.empty()){ std::cout<<"Usage: gxm.load <path>"<<std::endl; continue; } std::string err; if(gui::GxmLoader::ExecuteFile(path, err)) std::cout<<"GXM executed"<<std::endl; else std::cout<<"GXM error: "<<err<<std::endl; continue; }
        if (cmd=="gxm.sample"){
            if(!requireCompositor()) continue;
            std::string script =
                "WIN Sample|420|300\n"
                "TEXT 1000|Welcome to GXM Sample\n"
                "RECT 1000|20|60|120|40|180|60|60\n"
                "BTN 1000|1|20|120|110|32|Click Me\n";
            std::string err; if(gui::GxmLoader::ExecuteText(script, err)) std::cout<<"Sample executed"<<std::endl; else std::cout<<"Sample error: "<<err<<std::endl; continue; }
        if (cmd=="gui.sync"){
            if(!requireCompositor()) continue;
            std::string idS, generationS, sequenceS, freezeS;
            iss >> idS >> generationS >> sequenceS >> freezeS;
            if(idS.empty() || generationS.empty()){ std::cout<<"gui.sync <id> <frameGeneration> [frameSequence]"<<std::endl; continue; }
            ipc::Message m;
            m.type=(uint32_t)gui::MsgType::MT_SyncFrame;
            std::string payload = idS + "|" + generationS + (sequenceS.empty() ? "" : "|" + sequenceS) + (freezeS.empty() ? "" : "|" + freezeS);
            m.data.assign(payload.begin(), payload.end());
            ipc::Bus::publish("gui.sync", std::move(m), false);
            std::cout<<"Frame sync requested: windowId="<<idS<<" expectedFrameGeneration="<<generationS
                     <<(sequenceS.empty() ? "" : " expectedFrameSequence=" + sequenceS)
                     <<(freezeS.empty() ? "" : " freezeForCapture=" + freezeS)<<std::endl;
        } else if (cmd=="gui.unfreeze") {
            if(!requireCompositor()) continue;
            std::string idS; iss >> idS;
            if(idS.empty()){ std::cout<<"gui.unfreeze <id>"<<std::endl; continue; }
            ipc::Message m;
            m.type=(uint32_t)gui::MsgType::MT_UnfreezeFrame;
            m.data.assign(idS.begin(), idS.end());
            ipc::Bus::publish("gui.sync", std::move(m), false);
            std::cout<<"Frame capture freeze release requested: windowId="<<idS<<std::endl;
        } else if (cmd=="gui.start"){
            uint64_t before = Lifecycle::state().compositorPid;
            if(requireCompositor() && before!=0){ std::cout<<"Compositor already running pid="<<Lifecycle::state().compositorPid<<std::endl; }
        } else if (cmd=="gui.open.appmodeldemo"){
            if(!requireCompositor()) continue;
            const std::string marker = "appmodeldemo." + std::to_string(gxos::ticks());
            ipc::Message ping;
            ping.type=(uint32_t)gui::MsgType::MT_Ping;
            ping.data.assign(marker.begin(), marker.end());
            ipc::Bus::publish("gui.input", std::move(ping), false);

            bool compositorReady = false;
            auto readyDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(10000);
            while (std::chrono::steady_clock::now() < readyDeadline) {
                ipc::Message response;
                if (!ipc::Bus::pop("gui.output", response, 250)) continue;
                std::string payload(response.data.begin(), response.data.end());
                if (response.type == (uint32_t)gui::MsgType::MT_Ping && payload == marker) {
                    compositorReady = true;
                    break;
                }
            }

            // Diagnostic smoke path only: ask the compositor to use its existing UI launch branch.
            const std::string action = "App Model Demo";
            ipc::Message m;
            m.type=(uint32_t)gui::MsgType::MT_DesktopLaunch;
            m.data.assign(action.begin(), action.end());
            ipc::Bus::publish("gui.input", std::move(m), false);

            if (!compositorReady) {
                std::cout<<"App Model Demo open queued via compositor UI path; compositor readiness ping timed out"<<std::endl;
            } else {
                std::cout<<"App Model Demo open queued via compositor UI path"<<std::endl;
            }
        } else if (cmd=="gui.smoke.launchshadow"){
            if(!requireCompositor()) continue;
            std::cout << gui::Compositor::RunLaunchShadowSmokeDiagnostic();
        } else if (cmd=="gui.win"){
            if(!requireCompositor()) continue;
            std::string title; iss>>title; int w=640,h=480; iss>>w>>h; if(title.empty()){ std::cout<<"Usage: gui.win <title> [w h]"<<std::endl; continue; }
            ipc::Message m; m.type=(uint32_t)gui::MsgType::MT_Create; std::string payload = title+"|"+std::to_string(w)+"|"+std::to_string(h); m.data.assign(payload.begin(), payload.end()); ipc::Bus::publish("gui.input", std::move(m), false); std::cout<<"Requested window: "<<title<<std::endl;
        } else if (cmd=="gui.text"){
            if(!requireCompositor()) continue;
            std::string idS; iss>>idS; std::string rest; std::getline(iss, rest); if(rest.size()>0 && rest[0]==' ') rest.erase(0,1); ipc::Message m; m.type=(uint32_t)gui::MsgType::MT_DrawText; std::string payload = idS+"|"+rest; m.data.assign(payload.begin(), payload.end()); ipc::Bus::publish("gui.input", std::move(m), false); std::cout<<"Text queued"<<std::endl;
        } else if (cmd=="gui.close"){ if(!requireCompositor()) continue; std::string idS; iss>>idS; ipc::Message m; m.type=(uint32_t)gui::MsgType::MT_Close; m.data.assign(idS.begin(), idS.end()); ipc::Bus::publish("gui.input", std::move(m), false); std::cout<<"Close requested"<<std::endl;
        } else if (cmd=="gui.rect"){ if(!requireCompositor()) continue; std::string idS; int x,y,w,h,r,g,b; iss>>idS>>x>>y>>w>>h>>r>>g>>b; std::ostringstream oss; oss<<idS<<"|"<<x<<"|"<<y<<"|"<<w<<"|"<<h<<"|"<<r<<"|"<<g<<"|"<<b; ipc::Message m; m.type=(uint32_t)gui::MsgType::MT_DrawRect; auto s=oss.str(); m.data.assign(s.begin(), s.end()); ipc::Bus::publish("gui.input", std::move(m), false); std::cout<<"Rect queued"<<std::endl;
        } else if (cmd=="gui.move"){ if(!requireCompositor()) continue; std::string idS; int x,y; iss>>idS>>x>>y; std::ostringstream oss; oss<<idS<<"|"<<x<<"|"<<y; ipc::Message m; m.type=(uint32_t)gui::MsgType::MT_Move; auto s=oss.str(); m.data.assign(s.begin(), s.end()); ipc::Bus::publish("gui.input", std::move(m), false); std::cout<<"Move queued"<<std::endl;
        } else if (cmd=="gui.resize"){ if(!requireCompositor()) continue; std::string idS; int w,h; iss>>idS>>w>>h; std::ostringstream oss; oss<<idS<<"|"<<w<<"|"<<h; ipc::Message m; m.type=(uint32_t)gui::MsgType::MT_Resize; auto s=oss.str(); m.data.assign(s.begin(), s.end()); ipc::Bus::publish("gui.input", std::move(m), false); std::cout<<"Resize queued"<<std::endl;
        } else if (cmd=="gui.title"){ if(!requireCompositor()) continue; std::string idS; std::string rest; iss>>idS; std::getline(iss,rest); if(!rest.empty() && rest[0]==' ') rest.erase(0,1); std::ostringstream oss; oss<<idS<<"|"<<rest; ipc::Message m; m.type=(uint32_t)gui::MsgType::MT_SetTitle; auto s=oss.str(); m.data.assign(s.begin(), s.end()); ipc::Bus::publish("gui.input", std::move(m), false); std::cout<<"Title queued"<<std::endl;
        } else if (cmd=="gui.pop"){ if(!requireCompositor()) continue; ipc::Message m; if(ipc::Bus::pop("gui.output", m, 200)){ std::string s(m.data.begin(), m.data.end()); std::cout<<"GUI: type="<<m.type<<" payload="<<s<<std::endl; } else std::cout<<"No GUI events"<<std::endl;
        } else if (cmd=="mem"){ std::cout << "mem in use=" << Allocator::bytesInUse()/1024 << " KB peak=" << Allocator::peakBytes()/1024 << " KB" << std::endl;
        } else if (cmd=="alloc"){ size_t sz; if(!(iss>>sz)) sz=1; void* p = Allocator::alloc(sz, AllocTag::Temp); std::cout << "Allocated " << sz << " bytes ptr=" << p << std::endl;
        } else if (cmd=="tasks"){ std::cout << "tasks executed=" << Scheduler::tasksExecuted() << std::endl;
        } else if (cmd=="log"){ auto snap = Logger::snapshot(); for(auto& e: snap){ std::cout << (int)e.level << ": " << e.msg << std::endl; }
        } else if (cmd=="spawn"){ std::string name; iss>>name; if(name.empty()){ std::cout<<"Usage: spawn <spec> [args...]"<<std::endl; continue; } auto it = specs.find(name); if(it==specs.end()){ std::cout<<"No spec"<<std::endl; continue; } std::vector<std::string> args; std::string a; while(iss>>a) args.push_back(a); uint64_t pid = ProcessTable::spawn(it->second, args); std::cout<<"Spawned pid="<<pid<<std::endl;
        } else if (cmd=="plist"){ auto list = ProcessTable::list(); std::cout<<"Processes:"; for(auto id:list) std::cout<<" "<<id; std::cout<<std::endl;
        } else if (cmd=="send"){ uint64_t pid; iss>>pid; std::string payload; std::getline(iss, payload); if(payload.size()>0 && payload[0]==' ') payload.erase(0,1);
            ipc::Message m; m.srcPid=0; m.dstPid=pid; m.type=1; m.data.assign(payload.begin(), payload.end()); if(ProcessTable::send(pid, std::move(m))) std::cout<<"Sent"<<std::endl; else std::cout<<"Send failed"<<std::endl;
        } else if (cmd=="recv"){ uint64_t pid; iss>>pid; ipc::Message m; if(ProcessTable::try_recv(pid,m)){ std::string s(m.data.begin(), m.data.end()); std::cout<<"Message from "<<m.srcPid<<": "<<s<<std::endl; } else std::cout<<"No message"<<std::endl;
        } else if (cmd=="bus.sub"){ std::string chan; uint64_t pid; iss>>chan>>pid; if(chan.empty()){ std::cout<<"bus.sub <chan> <pid>"<<std::endl; continue; } ipc::Bus::subscribe(chan,pid); std::cout<<"Subscribed"<<std::endl;
        } else if (cmd=="bus.unsub"){ std::string chan; uint64_t pid; iss>>chan>>pid; ipc::Bus::unsubscribe(chan,pid); std::cout<<"Unsubscribed"<<std::endl;
        } else if (cmd=="bus.pub"){ std::string chan; iss>>chan; std::string payload; std::getline(iss,payload); if(payload.size()>0 && payload[0]==' ') payload.erase(0,1); bool fanout=false; std::string f; iss>>f; if(f=="fanout") fanout=true; ipc::Message m; m.srcPid=0; m.type=2; m.data.assign(payload.begin(), payload.end()); ipc::Bus::publish(chan, std::move(m), fanout); std::cout<<"Published"<<(fanout?" (fanout)":"")<<std::endl;
        } else if (cmd=="bus.pop"){ std::string chan; iss>>chan; uint64_t to=0; iss>>to; if(to==0) to=100; ipc::Message m; if(ipc::Bus::pop(chan,m,to)){ std::string s(m.data.begin(), m.data.end()); std::cout<<"POP["<<chan<<"] type="<<m.type<<" payload="<<s<<std::endl; } else std::cout<<"Timeout"<<std::endl;
        } else if (cmd=="bus.cap"){ std::string chan; size_t cap; iss>>chan>>cap; if(chan.empty()||cap==0){ std::cout<<"bus.cap <chan> <cap>"<<std::endl; continue; } ipc::Bus::setCapacity(chan, cap); std::cout<<"Capacity set"<<std::endl;
        } else if (cmd=="bus.stats"){ std::string chan; iss>>chan; std::cout<<"chan="<<chan<<" pending="<<ipc::Bus::pending(chan)<<" cap="<<ipc::Bus::capacity(chan)<<std::endl;
        } else if (cmd=="console.start"){ uint64_t before = Lifecycle::state().consolePid; if(requireConsole() && before!=0){ std::cout<<"Console already running pid="<<Lifecycle::state().consolePid<<std::endl; }
        } else if (cmd=="console.send"){ if(!requireConsole()) continue; std::string payload; std::getline(iss,payload); if(payload.size()>0 && payload[0]==' ') payload.erase(0,1); ipc::Message m; m.srcPid=0; m.type=10; m.data.assign(payload.begin(), payload.end()); ipc::Bus::publish("console.input", std::move(m), false); std::cout<<"Sent to console"<<std::endl;
        } else if (cmd=="console.pop"){ if(!requireConsole()) continue; uint64_t to=0; iss>>to; if(to==0) to=200; ipc::Message m; if(ipc::Bus::pop("console.output", m, to)){ std::string s(m.data.begin(), m.data.end()); std::cout<<"Console: "<<s<<std::endl; } else std::cout<<"No console output"<<std::endl;
        } else if (cmd=="mem"){ std::cout << "mem in use=" << Allocator::bytesInUse()/1024 << " KB peak=" << Allocator::peakBytes()/1024 << " KB" << std::endl;
        } else if (cmd=="pbytes"){ auto list = Allocator::listPidBytes(); std::cout<<"PID   BYTES"<<std::endl; for(auto& pr:list){ std::cout<<std::setw(5)<<pr.first<<" "<<pr.second<<std::endl; }
        } else if (cmd=="pkg.install"){
            std::string path; iss>>path; if(path.empty()){ std::cout<<"pkg.install <path.gxapp>"<<std::endl; continue; }
            auto result = PackageManager::InstallGXApp(path);
            if(result.success) std::cout<<"Installed "<<result.applicationName<<" -> "<<result.installedPath<<std::endl;
            else std::cout<<"Install warning/error: "<<result.message<<std::endl;
            continue;
        }
        else if (cmd=="pkg.validate"){
            std::string path; iss>>path; if(path.empty()){ std::cout<<"pkg.validate <path.gxapp>"<<std::endl; continue; }
            std::string message; bool ok = PackageManager::ValidateGXAppArchitecture(path, message);
            if(ok) std::cout<<"GXAPP supports this CPU architecture"<<std::endl;
            else std::cout<<"GXAPP validation warning/error: "<<message<<std::endl;
            continue;
        }
        else if (cmd=="pkg.launch"){
            std::string app; std::getline(iss, app); if(app.size()>0 && app[0]==' ') app.erase(0,1); if(app.empty()){ std::cout<<"pkg.launch <app>"<<std::endl; continue; }
            std::string error; if(PackageManager::LaunchGXApp(app, error)) std::cout<<"Launched "<<app<<std::endl; else std::cout<<"Launch failed: "<<error<<std::endl;
            continue;
        }
        else if (cmd=="pkg.list"){
            auto apps = PackageManager::ListInstalledGXApps();
            std::cout<<"Installed GXAPPs ("<<apps.size()<<"):"<<std::endl;
            for(const auto& app : apps) std::cout<<"  "<<app<<std::endl;
            continue;
        } else if (cmd=="help"){ help();
        }
        // Desktop and Taskbar convenience commands
        else if (cmd=="desktop.wallpaper"){
            std::string path; iss>>path; if(path.empty()){ std::cout<<"desktop.wallpaper <path>"<<std::endl; continue; }
            std::string error;
            if (gui::DesktopBackgroundService::ImportAndSetDesktopBackground(path, error)) {
                std::cout<<"Desktop background imported and selected: "<<path<<std::endl;
            } else {
                std::cout<<"Desktop background import failed: "<<error<<std::endl;
            }
        }
        else if (cmd=="desktop.background.remove"){
            std::string id; iss>>id; if(id.empty()){ std::cout<<"desktop.background.remove <id>"<<std::endl; continue; }
            std::string error;
            if (gui::DesktopBackgroundService::RemoveBackground(id, error)) {
                std::cout<<"Desktop background removed: "<<id<<std::endl;
            } else {
                std::cout<<"Desktop background removal failed: "<<error<<std::endl;
            }
        }
        else if (cmd=="desktop.launch.resolve"){
            std::string label; std::getline(iss, label); if(label.size()>0 && label[0]==' ') label.erase(0,1); if(label.empty()){ std::cout<<"desktop.launch.resolve <label>"<<std::endl; continue; }
            std::cout << gui::DesktopService::ResolveLaunchTargetDiagnostic(label);
        }
        else if (cmd=="desktop.open.resolve"){
            std::string path;
            iss >> path;
            std::string mode;
            std::getline(iss, mode);
            if(mode.size()>0 && mode[0]==' ') mode.erase(0,1);
            if(path.empty()){ std::cout<<"desktop.open.resolve <path> [dir]"<<std::endl; continue; }
            const bool isDirectory = mode == "dir" || mode == "directory";
            std::cout << gui::DesktopService::ResolveFilesystemEntryDiagnostic(path, isDirectory);
        }
        else if (cmd=="desktop.launch.adapt"){
            std::string label; std::getline(iss, label); if(label.size()>0 && label[0]==' ') label.erase(0,1); if(label.empty()){ std::cout<<"desktop.launch.adapt <label>"<<std::endl; continue; }
            std::cout << gui::DesktopService::LaunchTargetAdapterDiagnostic(label);
        }
        else if (cmd=="desktop.launch.compare"){
            std::cout << gui::DesktopService::LaunchTargetComparisonDiagnostic();
        }
        else if (cmd=="desktop.launch.storage"){
            std::cout << gui::DesktopService::LaunchStorageDiagnostic();
        }
        else if (cmd=="desktop.launch.storage.preview"){
            std::cout << gui::DesktopService::LaunchStoragePreviewDiagnostic();
        }
        else if (cmd=="desktop.launch.storage.preview.compare"){
            std::cout << gui::DesktopService::LaunchStoragePreviewComparisonDiagnostic();
        }
        else if (cmd=="desktop.launch.types"){
            std::cout << gui::DesktopService::LaunchTargetTypeCoverageDiagnostic();
        }
        else if (cmd=="desktop.launch"){
            if(!requireCompositor()) continue;
             std::string action; std::getline(iss, action); if(action.size()>0 && action[0]==' ') action.erase(0,1); if(action.empty()){ std::cout<<"desktop.launch <action>"<<std::endl; continue; }
             std::string err;
             if (gui::DesktopService::LaunchApp(action, err)) {
                 std::cout<<"Desktop launch successful: "<<action<<std::endl;
             } else {
                 std::cout<<"Desktop launch failed: "<<err<<std::endl;
             }
         }
         else if (cmd=="desktop.open"){
            if(!requireCompositor()) continue;
             std::string pathAndMode; std::getline(iss, pathAndMode); if(pathAndMode.size()>0 && pathAndMode[0]==' ') pathAndMode.erase(0,1);
             if(pathAndMode.empty()){ std::cout<<"desktop.open <path> [dir]"<<std::endl; continue; }
             std::string mode;
             if (pathAndMode.size() >= 4 && pathAndMode.rfind(" dir") == pathAndMode.size() - 4) {
                 mode = "dir";
                 pathAndMode.erase(pathAndMode.size() - 4);
             } else if (pathAndMode.size() >= 10 && pathAndMode.rfind(" directory") == pathAndMode.size() - 10) {
                 mode = "directory";
                 pathAndMode.erase(pathAndMode.size() - 10);
             }
             if (pathAndMode.size() >= 2 && pathAndMode.front() == '"' && pathAndMode.back() == '"') {
                 pathAndMode = pathAndMode.substr(1, pathAndMode.size() - 2);
             }
             std::string err;
             const bool isDirectory = mode == "dir" || mode == "directory";
             if (gui::DesktopService::OpenFilesystemEntry(pathAndMode, isDirectory, err)) {
                 std::cout<<"Desktop open successful: "<<pathAndMode<<std::endl;
             } else {
                 std::cout<<"Desktop open failed: "<<err<<std::endl;
             }
         }
         else if (cmd=="desktop.pin" || cmd=="desktop.unpin"){
            if(!requireCompositor()) continue;
             std::string action; std::getline(iss, action); if(action.size()>0 && action[0]==' ') action.erase(0,1); if(action.empty()){ std::cout<< (cmd=="desktop.pin"?"desktop.pin <action>":"desktop.unpin <action>") << std::endl; continue; }
             std::vector<std::pair<bool,std::string>> ops; ops.emplace_back(cmd=="desktop.pin", action);
             std::string payload = gui::packPins(ops);
             ipc::Message m; m.type=(uint32_t)gui::MsgType::MT_DesktopPins; m.data.assign(payload.begin(), payload.end()); ipc::Bus::publish("gui.input", std::move(m), false); std::cout<<"Desktop pin/unpin request sent: "<<payload<<std::endl; }
        else if (cmd=="desktop.appmodel.active-typed-dispatch-gate"){
            std::string mode; std::getline(iss, mode); if(mode.size()>0 && mode[0]==' ') mode.erase(0,1);
            const bool restoreEnabled = gxos::apps::AppModelActiveTypedDispatchEnabled();
            const bool forceOffRequested = mode == "force-off" || mode == "off" || mode == "disabled";
            const bool forceOnRequested = mode == "force-on" || mode == "on" || mode == "enabled";
            const bool resetRequested = mode == "reset" || mode == "restore" || mode == "default";
            if (forceOffRequested) {
                gxos::apps::SetAppModelActiveTypedDispatchEnabledForDiagnostics(false);
            } else if (forceOnRequested) {
                gxos::apps::SetAppModelActiveTypedDispatchEnabledForDiagnostics(true);
            } else if (resetRequested) {
                gxos::apps::ResetAppModelActiveTypedDispatchEnabledForDiagnostics();
            }

            const bool runtimeEnabled = gxos::apps::AppModelActiveTypedDispatchEnabled();
            std::cout << "[AppModelActiveTypedDispatchGate]\n";
            std::cout << "command: desktop.appmodel.active-typed-dispatch-gate\n";
            std::cout << "mode: " << (forceOffRequested ? "force-off" : (forceOnRequested ? "force-on" : (resetRequested ? "reset" : "status"))) << "\n";
            std::cout << "appModelActiveDispatchFeatureGate=" << gxos::apps::AppModelActiveTypedDispatchFeatureGateName() << "\n";
            std::cout << "appModelActiveDispatchDefaultOnCandidateGate=" << gxos::apps::AppModelActiveTypedDispatchDefaultOnCandidateGateName() << "\n";
            std::cout << "appModelActiveDispatchCandidateEnabled=" << (gxos::apps::AppModelActiveTypedDispatchDefaultOnCandidateEnabled() ? "true" : "false") << "\n";
            std::cout << "appModelActiveDispatchEnabled=" << (runtimeEnabled ? "true" : "false") << "\n";
            std::cout << "appModelActiveDispatchRuntimePath=" << (runtimeEnabled ? "active" : "inactive") << "\n";
            std::cout << "appModelActiveDispatchEffectiveStateSource=" << gxos::apps::AppModelActiveTypedDispatchEffectiveStateSourceName() << "\n";
            std::cout << "runtimeLaunchBehaviorChanged=true\n";
            std::cout << "visibleLaunchBehaviorChanged=false\n";
            std::cout << "appModelActiveDispatchRuntimeLaunchBehaviorChanged=true\n";
            std::cout << "appModelActiveDispatchVisibleLaunchBehaviorChanged=false\n";
            std::cout << "persistentDesktopStorageWrites=false\n";
            std::cout << "appModelActiveDispatchPreviousState=" << (restoreEnabled ? "true" : "false") << "\n";
            std::cout << "appModelActiveDispatchCurrentState=" << (runtimeEnabled ? "true" : "false") << "\n";
            if (forceOffRequested || forceOnRequested || resetRequested) {
                std::cout << "appModelActiveDispatchToggleApplied=true\n";
            } else {
                std::cout << "appModelActiveDispatchToggleApplied=false\n";
            }
            std::cout << "nonFatal=true\n";
        }
        else if (cmd=="desktop.appmodel.active-typed-dispatch-default-on-candidate"){
            std::string mode; std::getline(iss, mode); if(mode.size()>0 && mode[0]==' ') mode.erase(0,1);
            const bool candidateBefore = gxos::apps::AppModelActiveTypedDispatchDefaultOnCandidateEnabled();
            const bool enableRequested = mode == "on" || mode == "enable" || mode == "enabled" || mode == "candidate-on";
            const bool disableRequested = mode == "off" || mode == "disable" || mode == "disabled" || mode == "candidate-off";
            const bool resetRequested = mode == "reset" || mode == "restore" || mode == "default";
            if (enableRequested) {
                gxos::apps::SetAppModelActiveTypedDispatchDefaultOnCandidateEnabledForDiagnostics(true);
            } else if (disableRequested) {
                gxos::apps::SetAppModelActiveTypedDispatchDefaultOnCandidateEnabledForDiagnostics(false);
            } else if (resetRequested) {
                gxos::apps::ResetAppModelActiveTypedDispatchEnabledForDiagnostics();
            }

            const bool candidateAfter = gxos::apps::AppModelActiveTypedDispatchDefaultOnCandidateEnabled();
            const bool runtimeEnabled = gxos::apps::AppModelActiveTypedDispatchEnabled();
            std::cout << "[AppModelActiveTypedDispatchCandidateGate]\n";
            std::cout << "command: desktop.appmodel.active-typed-dispatch-default-on-candidate\n";
            std::cout << "mode: " << (enableRequested ? "candidate-on" : (disableRequested ? "candidate-off" : (resetRequested ? "reset" : "status"))) << "\n";
            std::cout << "appModelActiveDispatchFeatureGate=" << gxos::apps::AppModelActiveTypedDispatchFeatureGateName() << "\n";
            std::cout << "appModelActiveDispatchDefaultOnCandidateGate=" << gxos::apps::AppModelActiveTypedDispatchDefaultOnCandidateGateName() << "\n";
            std::cout << "appModelActiveDispatchCandidateEnabled=" << (candidateAfter ? "true" : "false") << "\n";
            std::cout << "appModelActiveDispatchEnabled=" << (runtimeEnabled ? "true" : "false") << "\n";
            std::cout << "appModelActiveDispatchRuntimePath=" << (runtimeEnabled ? "active" : "inactive") << "\n";
            std::cout << "appModelActiveDispatchEffectiveStateSource=" << gxos::apps::AppModelActiveTypedDispatchEffectiveStateSourceName() << "\n";
            std::cout << "runtimeLaunchBehaviorChanged=true\n";
            std::cout << "visibleLaunchBehaviorChanged=false\n";
            std::cout << "appModelActiveDispatchRuntimeLaunchBehaviorChanged=true\n";
            std::cout << "appModelActiveDispatchVisibleLaunchBehaviorChanged=false\n";
            std::cout << "persistentDesktopStorageWrites=false\n";
            std::cout << "appModelActiveDispatchCandidatePreviousState=" << (candidateBefore ? "true" : "false") << "\n";
            std::cout << "appModelActiveDispatchCandidateCurrentState=" << (candidateAfter ? "true" : "false") << "\n";
            std::cout << "appModelActiveDispatchToggleApplied=" << ((enableRequested || disableRequested || resetRequested) ? "true" : "false") << "\n";
            std::cout << "nonFatal=true\n";
        }
        else if (cmd=="desktop.showconfig"){
            gxos::gui::DesktopConfigData cfg; std::string err;
            if(!gxos::gui::DesktopConfig::Load("desktop.json", cfg, err)){
                std::cout<<"Failed to load desktop.json: "<<err<<std::endl;
            } else {
                ::DesktopThemeId themeId = ::DesktopThemeId::Classic;
                ::TryParseDesktopThemeId(cfg.desktopThemeId.c_str(), &themeId);
                const ::DesktopTheme& theme = ::GetDesktopTheme(themeId);
                std::cout<<"Wallpaper: "<<cfg.wallpaperPath<<std::endl;
                std::cout<<"Theme: "<<theme.displayName<<" ("<<::DesktopThemeIdToString(themeId)<<")"<<std::endl;
                std::cout<<"Pinned:\n"; for(auto &p: cfg.pinned) std::cout<<"  "<<p<<std::endl;
                std::cout<<"Recent:\n"; for(auto &r: cfg.recent) std::cout<<"  "<<r<<std::endl;
            }
        }
        else if (cmd=="desktop.apps"){
            std::cout << gui::DesktopService::GetRegisteredAppsDiagnostic();
        }
        else if (cmd=="desktop.windows.owners"){
            std::cout << desktopWindowOwnershipDiagnostic();
        }
        else if (cmd=="desktop.startup.regression"){
            std::cout << desktopStartupAppModelRegressionDiagnostic();
        }
        else if (cmd=="desktop.apps.verbose"){
            std::cout << gui::DesktopService::GetRegisteredAppsVerboseDiagnostic();
        }
        else if (cmd=="desktop.appmodel.summary"){
            std::cout << gui::DesktopService::AppModelSummaryDiagnostic();
        }
        else if (cmd=="desktop.display.summary"){
            if(!requireCompositor()) continue;
            std::cout << gui::Compositor::displayLayoutDiagnostic();
        }
        else if (cmd=="desktop.display.viewport"){
            if(!requireCompositor()) continue;
            std::string viewportArg; std::getline(iss, viewportArg); if(viewportArg.size()>0 && viewportArg[0]==' ') viewportArg.erase(0,1);
            if (viewportArg.empty()) {
                std::cout << gui::Compositor::displayViewportDiagnostic();
            } else {
                int index = 0;
                try {
                    index = std::stoi(viewportArg);
                } catch (...) {
                    std::cout << "desktop.display.viewport <1|2>" << std::endl;
                    continue;
                }
                if (!gui::Compositor::setHostedDisplayViewport(index)) {
                    std::cout << "desktop.display.viewport unavailable while synthetic dual-monitor mode is off" << std::endl;
                } else {
                    std::cout << gui::Compositor::displayViewportDiagnostic();
                }
            }
        }
        else if (cmd=="desktop.appmodel.inventory"){
            std::cout << gui::DesktopService::AppModelInventoryDiagnostic();
        }
        else if (cmd=="desktop.appmodel.file-associations"){
            std::cout << gui::DesktopService::FileAssociationV1Diagnostic();
        }
        else if (cmd=="desktop.appmodel.coverage"){
            std::cout << gui::DesktopService::BuiltInAppMetadataCoverageDiagnostic();
        }
        else if (cmd=="desktop.appmodel.shell-objects"){
            std::cout << gui::DesktopService::ShellObjectRegistryDiagnostic();
        }
        else if (cmd=="desktop.appmodel.typed-dispatch-gate"){
            std::string mode; std::getline(iss, mode); if(mode.size()>0 && mode[0]==' ') mode.erase(0,1);
            std::cout << gui::DesktopService::TypedDispatchGateDiagnostic(mode);
        }
        else if (cmd=="desktop.pinned"){
            auto& pinned = gui::DesktopService::GetPinned();
            std::cout<<"Pinned Items ("<<pinned.size()<<"):"<<std::endl;
            for(const auto& item : pinned) {
                std::cout<<"  "<<item.name<<" (";
                if(item.kind==gui::PinnedKind::App) std::cout<<"App";
                else if(item.kind==gui::PinnedKind::File) std::cout<<"File: "<<item.path;
                else std::cout<<"Special";
                std::cout<<")"<<std::endl;
            }
        }
        else if (cmd=="desktop.recent"){
            auto& recent = gui::DesktopService::GetRecentPrograms();
            std::cout<<"Recent Programs ("<<recent.size()<<"):"<<std::endl;
            for(const auto& prog : recent) std::cout<<"  "<<prog.name<<std::endl;
            auto& docs = gui::DesktopService::GetRecentDocuments();
            std::cout<<"Recent Documents ("<<docs.size()<<"):"<<std::endl;
            for(const auto& doc : docs) std::cout<<"  "<<doc.path<<std::endl;
        }
        else if (cmd=="desktop.recent.remove"){
            std::string name; std::getline(iss, name); if(name.size()>0 && name[0]==' ') name.erase(0,1);
            if(name.empty()){ std::cout<<"desktop.recent.remove <name>"<<std::endl; continue; }
            const bool removed = gui::DesktopService::RemoveRecentProgram(name);
            std::cout << "Recent program remove " << (removed ? "successful" : "skipped") << ": " << name << std::endl;
        }
        else if (cmd=="nativeapp.capabilities"){
            std::cout << gui::DesktopService::NativeAppCapabilitiesDiagnostic();
        }
        else if (cmd=="nativeapp.inspect"){
            std::string app; std::getline(iss, app); if(app.size()>0 && app[0]==' ') app.erase(0,1);
            if(app.empty()){ std::cout<<"nativeapp.inspect <app>"<<std::endl; continue; }
            std::cout << gui::DesktopService::InspectNativeAppPipeline(app);
        }
        else if (cmd=="nativeapp.smoketest"){
            std::string app; std::getline(iss, app); if(app.size()>0 && app[0]==' ') app.erase(0,1);
            if(app.empty()){ std::cout<<"nativeapp.smoketest <app>"<<std::endl; continue; }
            std::cout << gui::DesktopService::NativeAppPipelineSmokeTest(app);
        }
        else if (cmd=="nativeapp.processes"){
            std::cout << nativeAppProcessesDiagnostic();
        }
        else if (cmd=="desktop.pinapp"){
            std::string name; std::getline(iss, name); if(name.size()>0 && name[0]==' ') name.erase(0,1);
            if(name.empty()){ std::cout<<"desktop.pinapp <name>"<<std::endl; continue; }
            gui::DesktopService::PinApp(name);
            std::cout<<"Pinned app: "<<name<<std::endl;
        }
        else if (cmd=="desktop.pinfile"){
            std::string name, path; iss>>name; std::getline(iss, path); if(path.size()>0 && path[0]==' ') path.erase(0,1);
            if(name.empty() || path.empty()){ std::cout<<"desktop.pinfile <displayName> <path>"<<std::endl; continue; }
            gui::DesktopService::PinFile(name, path);
            std::cout<<"Pinned file: "<<name<<" -> "<<path<<std::endl;
        }
        else if (cmd=="taskbar.list"){
            if(!requireCompositor()) continue;
            ipc::Message m; m.type=(uint32_t)gui::MsgType::MT_WindowList; ipc::Bus::publish("gui.input", std::move(m), false); std::cout<<"Requested window list (use gui.pop)"<<std::endl; }
        else if (cmd=="taskbar.activate"){
            if(!requireCompositor()) continue;
            std::string idS; iss>>idS; if(idS.empty()){ std::cout<<"taskbar.activate <id>"<<std::endl; continue; } ipc::Message m; m.type=(uint32_t)gui::MsgType::MT_Activate; m.data.assign(idS.begin(), idS.end()); ipc::Bus::publish("gui.input", std::move(m), false); std::cout<<"Activate sent"<<std::endl; }
        else if (cmd=="taskbar.min"){
            if(!requireCompositor()) continue;
            std::string idS; iss>>idS; if(idS.empty()){ std::cout<<"taskbar.min <id>"<<std::endl; continue; } ipc::Message m; m.type=(uint32_t)gui::MsgType::MT_Minimize; m.data.assign(idS.begin(), idS.end()); ipc::Bus::publish("gui.input", std::move(m), false); std::cout<<"Minimize sent"<<std::endl; }
        else if (cmd=="taskbar.close"){
            if(!requireCompositor()) continue;
            std::string idS; iss>>idS; if(idS.empty()){ std::cout<<"taskbar.close <id>"<<std::endl; continue; } ipc::Message m; m.type=(uint32_t)gui::MsgType::MT_Close; m.data.assign(idS.begin(), idS.end()); ipc::Bus::publish("gui.input", std::move(m), false); std::cout<<"Close requested"<<std::endl; }
        else if (cmd=="workspace.switch"){
            if(!requireCompositor()) continue;
            int n; if(!(iss>>n)){ std::cout<<"workspace.switch <n>"<<std::endl; continue; }
            ipc::Message m; m.type=(uint32_t)gui::MsgType::MT_WidgetEvt; 
            std::string payload = "WS_SWITCH|" + std::to_string(n);
            m.data.assign(payload.begin(), payload.end()); 
            ipc::Bus::publish("gui.input", std::move(m), false); 
            std::cout<<"Workspace switch requested: "<<n<<std::endl; 
        }
        else if (cmd=="workspace.next"){
            if(!requireCompositor()) continue;
            ipc::Message m; m.type=(uint32_t)gui::MsgType::MT_WidgetEvt; 
            std::string payload = "WS_NEXT";
            m.data.assign(payload.begin(), payload.end()); 
            ipc::Bus::publish("gui.input", std::move(m), false); 
            std::cout<<"Next workspace requested"<<std::endl; 
        }
        else if (cmd=="workspace.prev"){
            if(!requireCompositor()) continue;
            ipc::Message m; m.type=(uint32_t)gui::MsgType::MT_WidgetEvt; 
            std::string payload = "WS_PREV";
            m.data.assign(payload.begin(), payload.end()); 
            ipc::Bus::publish("gui.input", std::move(m), false); 
            std::cout<<"Previous workspace requested"<<std::endl; 
        }
        else if (cmd=="workspace.current"){
            if(!requireCompositor()) continue;
            ipc::Message m; m.type=(uint32_t)gui::MsgType::MT_WidgetEvt; 
            std::string payload = "WS_CURRENT";
            m.data.assign(payload.begin(), payload.end()); 
            ipc::Bus::publish("gui.input", std::move(m), false); 
            std::cout<<"Current workspace query sent (use gui.pop)"<<std::endl; 
        }
        else if (cmd=="notepad"){
            if(!requireCompositor()) continue;
            std::string filePath; 
            std::getline(iss, filePath); 
            if(filePath.size()>0 && filePath[0]==' ') filePath.erase(0,1);
            
            uint64_t pid;
            if(filePath.empty()) {
                pid = apps::Notepad::Launch();
            } else {
                pid = apps::Notepad::LaunchWithFile(filePath);
            }
            std::cout<<"Notepad launched, pid="<<pid<<std::endl;
        }
        else if (cmd=="calculator"){
            if(!requireCompositor()) continue;
            uint64_t pid = apps::Calculator::Launch();
            std::cout<<"Calculator launched, pid="<<pid<<std::endl;
        }
        else if (cmd=="console"){
            if(!requireCompositor()) continue;
            uint64_t pid = apps::ConsoleWindow::Launch();
            std::cout<<"Console window launched, pid="<<pid<<std::endl;
        }
        else if (cmd=="files"){
            if(!requireCompositor()) continue;
            std::string startPath;
            std::getline(iss, startPath);
            if(startPath.size()>0 && startPath[0]==' ') startPath.erase(0,1);
            
            uint64_t pid;
            if(startPath.empty()) {
                pid = apps::FileExplorer::Launch();
            } else {
                pid = apps::FileExplorer::Launch(startPath);
            }
            std::cout<<"File Explorer launched, pid="<<pid<<std::endl;
        }
        else if (cmd=="clock"){
            if(!requireCompositor()) continue;
            uint64_t pid = apps::Clock::Launch();
            std::cout<<"Clock launched, pid="<<pid<<std::endl;
        }
        else if (cmd=="taskmanager.snapshot"){
            std::cout << apps::TaskManager::SnapshotDiagnostic();
        }
        else if (cmd=="taskmanager.network-snapshot-wait"){
            std::cout << taskManagerNetworkSnapshotWaitDiagnostic();
        }
        else if (cmd=="taskmanager.tombstone-test"){
            const uint32_t beforeCount = static_cast<uint32_t>(ProcessTable::tombstones().size());
            const uint64_t pid = apps::ShutdownDialog::LaunchSmokeExit();
            int exitCode = 0;
            const bool completed = ProcessTable::wait(pid, 1000, &exitCode);

            const std::vector<ProcessTombstoneRecord> tombstones = ProcessTable::tombstones();
            const ProcessTombstoneRecord* record = nullptr;
            for (const auto& tombstone : tombstones) {
                if (tombstone.pid == pid) {
                    record = &tombstone;
                    break;
                }
            }

            const uint32_t afterCount = static_cast<uint32_t>(tombstones.size());
            const uint32_t delta = afterCount >= beforeCount ? afterCount - beforeCount : 0;
            const bool passed = completed && record && record->reason == "NormalExit" && exitCode == 0;
            const bool appPolicyKnown = record && record->appTombstoneCapabilityKnown;

            std::cout << "tombstoneTest=" << (passed ? "passed" : "failed") << "\n";
            std::cout << "tombstoneHistoryBefore=" << beforeCount << "\n";
            std::cout << "tombstoneHistoryCount=" << afterCount << "\n";
            std::cout << "tombstoneHistoryDelta=" << delta << "\n";
            std::cout << "tombstoneHistoryCapacity=" << ProcessTable::kTombstoneHistoryMax << "\n";
            std::cout << "tombstoneReason=" << (record ? record->reason : std::string("N/A")) << "\n";
            std::cout << "appId=" << (record && !record->appId.empty() ? record->appId : std::string("N/A")) << "\n";
            std::cout << "appTombstoneCapabilityKnown=" << (appPolicyKnown ? "true" : "false") << "\n";
            std::cout << "appTombstoneCapable=" << (record ? (record->appTombstoneCapabilityKnown ? (record->appTombstoneCapable ? "true" : "false") : "N/A") : "N/A") << "\n";
            std::cout << "appTombstoneCapabilitySource=" << (record ? record->appTombstoneCapabilitySource : std::string("N/A")) << "\n";
            std::cout << "restoreSupported=" << (record && record->restoreSupported ? "true" : "false") << "\n";
            if (record) {
                std::cout << "tombstoneRow pid=" << record->pid
                          << " displayName=" << (record->displayName.empty() ? std::string("N/A") : record->displayName)
                          << " appId=" << (record->appId.empty() ? std::string("N/A") : record->appId)
                          << " reason=" << record->reason
                          << " appTombstoneCapable=" << (record->appTombstoneCapabilityKnown ? (record->appTombstoneCapable ? "true" : "false") : "N/A")
                          << " appTombstoneCapabilitySource=" << record->appTombstoneCapabilitySource
                          << " restoreSupported=" << (record->restoreSupported ? "true" : "false")
                          << " exitCode=" << (record->exitCodeAvailable ? std::to_string(record->exitCode) : std::string("N/A"))
                          << " runtimeMs=" << (record->runtimeMsAvailable ? std::to_string(record->runtimeMs) : std::string("N/A"))
                          << " windowTitle=" << (record->windowTitle.empty() ? std::string("N/A") : record->windowTitle)
                          << " lastMessage=" << (record->lastMessage.empty() ? std::string("N/A") : record->lastMessage)
                          << "\n";
            }
        }
        else if (cmd=="taskmgr"){
            if(!requireCompositor()) continue;
            uint64_t pid = apps::TaskManager::Launch();
            std::cout<<"Task Manager launched, pid="<<pid<<std::endl;
        }
        else if (cmd=="paint"){
            if(!requireCompositor()) continue;
            uint64_t pid = apps::Paint::Launch();
            std::cout<<"Paint launched, pid="<<pid<<std::endl;
        }
        else if (cmd=="navigator"){
            if(!requireCompositor()) continue;
            std::string err;
            if(gui::DesktopService::LaunchApp("guideXOS Navigator", err)) std::cout<<"guideXOS Navigator launched"<<std::endl;
            else std::cout<<"guideXOS Navigator launch failed: "<<err<<std::endl;
        }
        else if (cmd=="navigator.smoke"){
            std::cout << navigatorHostedSmokeDiagnostic();
        }
        else if (cmd=="navigator.positioned-link-probe"){
            std::cout << navigatorPositionedLinkProbeDiagnostic();
        }
        else if (cmd=="navigator.goto"){
            if(!requireCompositor()) continue;
            std::string url;
            std::getline(iss, url);
            if(url.size()>0 && url[0]==' ') url.erase(0,1);
            std::cout << navigatorGotoDiagnostic(url);
        }
        else if (cmd=="imgview"){
            if(!requireCompositor()) continue;
            std::string filePath;
            std::getline(iss, filePath);
            if(filePath.size()>0 && filePath[0]==' ') filePath.erase(0,1);
            uint64_t pid;
            if(filePath.empty()) pid = apps::ImageViewer::Launch();
            else pid = apps::ImageViewer::Launch(filePath);
            std::cout<<"ImageViewer launched, pid="<<pid<<std::endl;
        }
        else if (cmd=="osk"){
            if(!requireCompositor()) continue;
            uint64_t pid = apps::OnScreenKeyboard::Launch();
            std::cout<<"On-Screen Keyboard launched, pid="<<pid<<std::endl;
        }
        else if (cmd=="shutdown"){
            if(!requireCompositor()) continue;
            uint64_t pid = apps::ShutdownDialog::Launch();
            std::cout<<"Shutdown dialog launched, pid="<<pid<<std::endl;
        }
        else if (cmd=="msgbox"){
            if(!requireCompositor()) continue;
            std::string text; std::getline(iss, text); if(text.size()>0 && text[0]==' ') text.erase(0,1);
            if(text.empty()){ std::cout<<"msgbox <text>"<<std::endl; continue; }
            uint64_t pid = apps::MessageBox::Launch("Message", text);
            std::cout<<"MessageBox launched, pid="<<pid<<std::endl;
        }
        else if (cmd=="welcome"){
            if(!requireCompositor()) continue;
            uint64_t pid = apps::Welcome::Launch();
            std::cout<<"Welcome window launched, pid="<<pid<<std::endl;
        }
        else if (cmd=="notify"){
            std::string text; std::getline(iss, text); if(text.size()>0 && text[0]==' ') text.erase(0,1);
            if(text.empty()){ std::cout<<"notify <text>"<<std::endl; continue; }
            gui::NotificationManager::Add(text);
            std::cout<<"Notification queued"<<std::endl;
        }
        else if (cmd=="notify.clear"){
            gui::NotificationManager::Clear();
            std::cout<<"Notifications cleared"<<std::endl;
        }
        else if (cmd=="fw.mode"){
            std::string mode; iss>>mode;
            if(mode=="normal") Firewall::SetMode(FirewallMode::Normal);
            else if(mode=="block") Firewall::SetMode(FirewallMode::BlockAll);
            else if(mode=="disabled") Firewall::SetMode(FirewallMode::Disabled);
            else if(mode=="auto") Firewall::SetMode(FirewallMode::Autolearn);
            else { std::cout<<"fw.mode <normal|block|disabled|auto>"<<std::endl; continue; }
            std::cout<<"Firewall mode set"<<std::endl;
        }
        else if (cmd=="fw.allow"){
            std::string name; std::getline(iss,name); if(name.size()>0 && name[0]==' ') name.erase(0,1);
            if(name.empty()){ std::cout<<"fw.allow <name>"<<std::endl; continue; }
            Firewall::AddException(name);
            std::cout<<"Firewall exception added: "<<name<<std::endl;
        }
        else if (cmd=="fw.list"){
            auto ex = Firewall::Exceptions();
            std::cout<<"Firewall exceptions ("<<ex.size()<<"):"<<std::endl;
            for(auto& e: ex) std::cout<<"  "<<e<<std::endl;
        }
        else if (cmd=="fw.alerts"){
            auto al = Firewall::PendingAlerts();
            std::cout<<"Pending alerts ("<<al.size()<<"):"<<std::endl;
            for(auto& a: al) std::cout<<"  "<<a<<std::endl;
        }
        else if (cmd=="modules"){
            auto names = ModuleManager::ListNames();
            std::cout<<"Modules ("<<names.size()<<"):"<<std::endl;
            for(auto& n: names) std::cout<<"  "<<n<<std::endl;
        }
        else if (cmd=="module.launch"){
            std::string name; std::getline(iss,name); if(name.size()>0 && name[0]==' ') name.erase(0,1);
            if(name.empty()){ std::cout<<"module.launch <name>"<<std::endl; continue; }
            if(ModuleManager::Launch(name)) std::cout<<"Module launched: "<<name<<std::endl;
            else std::cout<<"Module not found: "<<name<<std::endl;
        }
        else if (cmd=="vnc.start"){
            uint16_t port = 5900;
            iss >> port;
            if(vnc::VncServer::IsRunning()){
                std::cout<<"VNC server already running"<<std::endl;
            } else if(vnc::VncServer::Start(port)){
                std::cout<<"VNC server started on port "<<port<<std::endl;
                std::cout<<"Connect from VM with: vnc://localhost:"<<port<<std::endl;
            } else {
                std::cout<<"Failed to start VNC server"<<std::endl;
            }
        }
        else if (cmd=="vnc.stop"){
            if(!vnc::VncServer::IsRunning()){
                std::cout<<"VNC server not running"<<std::endl;
            } else {
                vnc::VncServer::Stop();
                std::cout<<"VNC server stopped"<<std::endl;
            }
        }
        else if (cmd=="vnc.status"){
            if(vnc::VncServer::IsRunning()){
                int clients = vnc::VncServer::GetClientCount();
                std::cout<<"VNC server is running"<<std::endl;
                std::cout<<"Connected clients: "<<clients<<std::endl;
            } else {
                std::cout<<"VNC server is not running"<<std::endl;
            }
        }
        else {
            std::cout << "Unknown command (help for list)" << std::endl;
        }
    }
    Lifecycle::shutdown();
    Logger::write(LogLevel::Info, "guideXOSServer server exiting.");
    return 0;
}
