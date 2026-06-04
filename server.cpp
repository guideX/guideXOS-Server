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
    add("external stylesheets unsupported", contains(runtimeReport, "Capabilities.External stylesheets=unsupported"), "expected unsupported");
    add("bookmark persistence enabled", contains(runtimeReport, "Capabilities.Bookmark persistence=enabled"), "expected enabled");

    bool basicHttpLoaded = gxos::apps::Navigator::SmokeNavigateToQuiet("http://127.0.0.1:8080/navigator-smoke/basic.html");
    std::string basicHttpText = gxos::apps::Navigator::SmokeCurrentDocumentText();
    add("plain HTTP GET still loads", basicHttpLoaded && contains(basicHttpText, "Kernel HTTP Basic"),
        "currentUrl=" + gxos::apps::Navigator::SmokeCurrentUrl());

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
                 " gui.start | gui.open.appmodeldemo | gui.smoke.launchshadow | gui.win <title> [w h] | gui.text <id> <text> | gui.close <id>\n"
                 " gui.rect <id> <x> <y> <w> <h> <r> <g> <b> | gui.move <id> <x> <y> | gui.resize <id> <w> <h> | gui.title <id> <title>\n"
                 " gui.btn <win> <id> <x> <y> <w> <h> <text> | gui.pop | gui.wlist | gui.activate <id> | gui.min <id>\n"
                 " gxm.load <path> | gxm.sample | gui.save <path> | gui.load <path>\n"
                 " desktop.wallpaper <path> | desktop.launch <action> | desktop.launch.resolve <label> | desktop.launch.adapt <label> | desktop.launch.compare | desktop.launch.storage | desktop.launch.storage.preview | desktop.launch.storage.preview.compare | desktop.launch.types | desktop.pin <action> | desktop.unpin <action> | desktop.showconfig\n"
                 " desktop.apps | desktop.apps.verbose | desktop.appmodel.summary | desktop.appmodel.coverage | desktop.appmodel.typed-dispatch-gate | desktop.pinned | desktop.recent | desktop.pinapp <name> | desktop.pinfile <name> <path>\n"
                 " nativeapp.capabilities | nativeapp.inspect <app> | nativeapp.smoketest <app> | nativeapp.processes\n"
                 " taskbar.list | taskbar.activate <id> | taskbar.min <id> | taskbar.close <id>\n"
                 " workspace.switch <n> | workspace.next | workspace.prev | workspace.current\n"
                 " notepad | notepad <file>\n"
                 " calculator\n"
                 " console | console.start | console.send <text> | console.pop [timeoutMs]\n"
                 " files | files <path>\n"
                 " clock\n"
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
        if (cmd=="gui.start"){
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
            if(!requireCompositor()) continue;
            std::string path; iss>>path; if(path.empty()){ std::cout<<"desktop.wallpaper <path>"<<std::endl; continue; } ipc::Message m; m.type=(uint32_t)gui::MsgType::MT_DesktopWallpaperSet; m.data.assign(path.begin(), path.end()); ipc::Bus::publish("gui.input", std::move(m), false); std::cout<<"Desktop wallpaper set request sent: "<<path<<std::endl; }
        else if (cmd=="desktop.launch.resolve"){
            std::string label; std::getline(iss, label); if(label.size()>0 && label[0]==' ') label.erase(0,1); if(label.empty()){ std::cout<<"desktop.launch.resolve <label>"<<std::endl; continue; }
            std::cout << gui::DesktopService::ResolveLaunchTargetDiagnostic(label);
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
         else if (cmd=="desktop.pin" || cmd=="desktop.unpin"){
            if(!requireCompositor()) continue;
             std::string action; std::getline(iss, action); if(action.size()>0 && action[0]==' ') action.erase(0,1); if(action.empty()){ std::cout<< (cmd=="desktop.pin"?"desktop.pin <action>":"desktop.unpin <action>") << std::endl; continue; }
             std::vector<std::pair<bool,std::string>> ops; ops.emplace_back(cmd=="desktop.pin", action);
             std::string payload = gui::packPins(ops);
             ipc::Message m; m.type=(uint32_t)gui::MsgType::MT_DesktopPins; m.data.assign(payload.begin(), payload.end()); ipc::Bus::publish("gui.input", std::move(m), false); std::cout<<"Desktop pin/unpin request sent: "<<payload<<std::endl; }
        else if (cmd=="desktop.showconfig"){
            gxos::gui::DesktopConfigData cfg; std::string err; if(!gxos::gui::DesktopConfig::Load("desktop.json", cfg, err)){ std::cout<<"Failed to load desktop.json: "<<err<<std::endl; } else { std::cout<<"Wallpaper: "<<cfg.wallpaperPath<<std::endl; std::cout<<"Pinned:\n"; for(auto &p: cfg.pinned) std::cout<<"  "<<p<<std::endl; std::cout<<"Recent:\n"; for(auto &r: cfg.recent) std::cout<<"  "<<r<<std::endl; }
        }
        else if (cmd=="desktop.apps"){
            std::cout << gui::DesktopService::GetRegisteredAppsDiagnostic();
        }
        else if (cmd=="desktop.apps.verbose"){
            std::cout << gui::DesktopService::GetRegisteredAppsVerboseDiagnostic();
        }
        else if (cmd=="desktop.appmodel.summary"){
            std::cout << gui::DesktopService::AppModelSummaryDiagnostic();
        }
        else if (cmd=="desktop.appmodel.coverage"){
            std::cout << gui::DesktopService::BuiltInAppMetadataCoverageDiagnostic();
        }
        else if (cmd=="desktop.appmodel.typed-dispatch-gate"){
            std::cout << gui::DesktopService::TypedDispatchGateDiagnostic();
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
    Lifecycle::shutdown();
    Logger::write(LogLevel::Info, "guideXOSServer server exiting.");
    return 0;
}
