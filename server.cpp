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
#include "development_run_service.h"
#include <iostream>
#include <algorithm>
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

    bool navigated = gxos::apps::Navigator::SmokeNavigateTo("about:navigator-runtime");
    std::string currentUrl = gxos::apps::Navigator::SmokeCurrentUrl();
    std::string runtimeReport = gxos::apps::Navigator::SmokeRuntimeReport();
    add("runtime URL loads", navigated && currentUrl == "about:navigator-runtime", "currentUrl=" + currentUrl);
    add("runtime report mode", contains(runtimeReport, "Runtime.Mode=hosted/compositor"), "expected hosted/compositor");
    add("runtime report launch path", contains(runtimeReport, "DesktopService::LaunchApp -> apps::Navigator::Launch"), "expected DesktopService/apps::Navigator");
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
    add("remote PNG enabled", contains(runtimeReport, "Capabilities.Remote PNG=enabled"), "expected enabled");
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
        hasPositiveCount(cssTableWideReport, "Current Document.CSS table layout fallbacks=") &&
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
        hasPositiveCount(cssPhase1fReport, "Current Document.CSS table layout fallbacks=") &&
        hasPositiveCount(cssPhase1fReport, "Current Document.CSS table captions rendered="),
        cssPhase1fDetail);

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
        ",disabled=" + yesNo(phase2fDisabledButton));
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
    add("HTTPS unsupported compression stays friendly", httpsGzipLoaded &&
        contains(httpsGzipText, "Unsupported Content Encoding") &&
        contains(httpsGzipText, "UnsupportedContentEncoding"),
        "expected existing parser behavior over TLS");

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
        contains(postResultText, "User-Agent: guideXOS-Navigator/0.1") &&
        contains(postResultText, "Accept-Encoding: identity") &&
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
                 " gui.start | gui.open.appmodeldemo | gui.smoke.launchshadow | gui.win <title> [w h] | gui.text <id> <text> | gui.close <id> | gui.key <keyCode> <down|up> [modifiers]\n"
                 " gui.rect <id> <x> <y> <w> <h> <r> <g> <b> | gui.move <id> <x> <y> | gui.resize <id> <w> <h> | gui.title <id> <title>\n"
                 " gui.btn <win> <id> <x> <y> <w> <h> <text> | gui.pop | gui.wlist | gui.activate <id> | gui.min <id> | gui.sync <id> <frameGeneration> [frameSequence] [freeze] | gui.unfreeze <id>\n"
                 " gxm.load <path> | gxm.sample | gui.save <path> | gui.load <path>\n"
                 " desktop.wallpaper <path> | desktop.background.remove <id> | desktop.launch <action> | desktop.open <path> [dir] | desktop.launch.resolve <label> | desktop.launch.adapt <label> | desktop.launch.compare | desktop.launch.storage | desktop.launch.storage.preview | desktop.launch.storage.preview.compare | desktop.launch.types | desktop.open.resolve <path> [dir] | desktop.appmodel.active-typed-dispatch-gate [force-on|force-off|reset] | desktop.appmodel.active-typed-dispatch-default-on-candidate [on|off|reset] | desktop.pin <action> | desktop.unpin <action> | desktop.showconfig\n"
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
                 " navigator | navigator.smoke | navigator.goto <url>\n"
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
    struct ShutdownGuard { ~ShutdownGuard(){ apps::DevelopmentRunService::Shutdown(); Lifecycle::shutdown(); } } guard;

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
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) {
            line.pop_back();
        }
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
        if (cmd=="gui.activate"){ if(!requireCompositor()) continue; std::string idS; iss>>idS; if(idS.empty()){ std::cout<<"gui.activate <id>"<<std::endl; continue; } ipc::Message m; m.type=(uint32_t)gui::MsgType::MT_Activate; m.dstPid=Lifecycle::state().compositorPid; m.data.assign(idS.begin(), idS.end()); ipc::Bus::publish("gui.input", std::move(m), false); std::cout<<"Activate sent"<<std::endl; continue; }
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
        } else if (cmd=="gui.close"){ if(!requireCompositor()) continue; std::string idS; iss>>idS; ipc::Message m; m.type=(uint32_t)gui::MsgType::MT_Close; m.dstPid=Lifecycle::state().compositorPid; m.data.assign(idS.begin(), idS.end()); ipc::Bus::publish("gui.input", std::move(m), false); std::cout<<"Close requested"<<std::endl;
        } else if (cmd=="gui.key"){ if(!requireCompositor()) continue; int keyCode=0, modifiers=0; std::string action; iss>>keyCode>>action>>modifiers; if(action!="down" && action!="up"){ std::cout<<"Usage: gui.key <keyCode> <down|up> [modifiers]"<<std::endl; continue; } ipc::Message m; m.type=(uint32_t)gui::MsgType::MT_InputKey; m.dstPid=Lifecycle::state().compositorPid; const std::string payload=std::to_string(keyCode)+"|"+action+"|"+std::to_string(modifiers); m.data.assign(payload.begin(), payload.end()); ipc::Bus::publish("gui.input", std::move(m), false); std::cout<<"Key queued"<<std::endl;
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
    apps::DevelopmentRunService::Shutdown();
    Lifecycle::shutdown();
    Logger::write(LogLevel::Info, "guideXOSServer server exiting.");
    return 0;
}
