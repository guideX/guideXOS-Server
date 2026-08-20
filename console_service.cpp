#include "console_service.h"
#include "desktop_service.h"
#include "desktop_folder.h"
#include "native_app_debug_log.h"
#include "native_app_process_table.h"
#include "process.h"
#include "logger.h"
#include "scheduler.h"
#include <algorithm>
#include <filesystem>
#include <sstream>

namespace gxos { namespace svc {
    using namespace gxos;
    static const char* kInputChan = "console.input";
    static const char* kOutputChan = "console.output";
    static std::string s_cwd = gxos::gui::DesktopFolderResolver::VirtualPath();

    static std::string trim(const std::string& s){ size_t a = s.find_first_not_of(" \t\r\n"); if(a==std::string::npos) return {}; size_t b = s.find_last_not_of(" \t\r\n"); return s.substr(a, b-a+1); }

    static bool startsWith(const std::string& value, const std::string& prefix) {
        return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
    }

    static std::string resolveVirtualPath(const std::string& base, const std::string& raw) {
        if (raw.empty()) return base;

        std::string candidate = raw;
        std::replace(candidate.begin(), candidate.end(), '\\', '/');
        if (candidate == "~") return gxos::gui::DesktopFolderResolver::VirtualPath();

        const bool isAbsolute = !candidate.empty() && candidate.front() == '/';
        std::filesystem::path combined = isAbsolute ? std::filesystem::path(candidate) : (std::filesystem::path(base) / std::filesystem::path(candidate));
        return gxos::gui::DesktopFolderResolver::NormalizeVirtualPath(combined.generic_string());
    }

    static bool changeDirectory(const std::string& rawPath, std::string& error) {
        const std::string target = resolveVirtualPath(s_cwd, rawPath);
        std::string ensureError;
        const bool createIfMissing = target == gxos::gui::DesktopFolderResolver::VirtualPath();
        if (!gxos::gui::DesktopFolderResolver::EnsureExists(target, ensureError, createIfMissing)) {
            error = ensureError;
            return false;
        }

        std::string syncError;
        if (!gxos::gui::DesktopService::ShowFolderOnHostedDesktop(target, syncError)) {
            error = syncError.empty() ? std::string("Hosted desktop navigation failed for ") + target : syncError;
            return false;
        }

        s_cwd = target;
        return true;
    }

    static void publishOutput(const std::string& text) {
        ipc::Message out;
        out.type = 0;
        out.data.assign(text.begin(), text.end());
        ipc::Bus::publish(kOutputChan, std::move(out), false);
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
                << " shutdownStage=" << gxos::apps::NativeAppRuntime::HostedShutdownStageName(process.hostedShutdownStage)
                << " shutdownStageCode=" << process.hostedShutdownStage
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

    static std::string nativeAppDebugLogDiagnostic(const std::string& rawCount) {
        size_t requested = 24;
        if (!rawCount.empty()) {
            try {
                requested = static_cast<size_t>(std::stoul(rawCount));
            } catch (...) {
                requested = 24;
            }
        }
        requested = std::max<size_t>(1, std::min<size_t>(64, requested));

        std::ostringstream oss;
        const auto entries = gxos::apps::NativeAppDebugLog::Recent(requested);
        oss << "Native app debug log: " << entries.size() << "\n";
        for (const auto& entry : entries) {
            oss << "#" << entry.timestamp
                << " runtimeId=" << entry.runtimeId
                << " appId=" << entry.appId
                << " severity=" << entry.severity
                << " message=" << entry.message << "\n";
        }
        return oss.str();
    }

    int ConsoleService::main(int argc, char** argv){
        Logger::write(LogLevel::Info, "ConsoleService started");
        ipc::Bus::ensure(kInputChan); ipc::Bus::ensure(kOutputChan);
        // Subscribe so the service also gets fanout publish
        // Not required as we will read from Bus::pop which reads the queue
        while(true){
            ipc::Message m; if(!ipc::Bus::pop(kInputChan, m, 1000)){ continue; }
            std::string line(m.data.begin(), m.data.end()); line = trim(line);
            if(line=="exit"||line=="quit"){ publishOutput("bye"); break; }
            std::istringstream commandStream(line);
            std::string command;
            commandStream >> command;
            std::string remainder;
            std::getline(commandStream, remainder);
            remainder = trim(remainder);
            if(command=="cd") {
                if (remainder.empty()) {
                    publishOutput(s_cwd);
                    continue;
                }

                std::string error;
                if (!changeDirectory(remainder, error)) {
                    publishOutput("cd: " + remainder + ": " + error);
                }
                continue;
            }
            if(command=="pwd") {
                publishOutput(s_cwd);
                continue;
            }
            if(line=="desktop.apps") { publishOutput(gxos::gui::DesktopService::GetRegisteredAppsDiagnostic()); continue; }
            if(line=="desktop.apps.verbose") { publishOutput(gxos::gui::DesktopService::GetRegisteredAppsVerboseDiagnostic()); continue; }
            if(line=="desktop.launch.types") { publishOutput(gxos::gui::DesktopService::LaunchTargetTypeCoverageDiagnostic()); continue; }
            if(startsWith(line, "desktop.launch ")) { std::string err; std::string app = trim(line.substr(15)); if (gxos::gui::DesktopService::LaunchApp(app, err)) publishOutput("Desktop launch successful: " + app); else publishOutput("Desktop launch failed: " + err); continue; }
            if(line=="nativeapp.capabilities") { publishOutput(gxos::gui::DesktopService::NativeAppCapabilitiesDiagnostic()); continue; }
            if(startsWith(line, "nativeapp.inspect ")) { publishOutput(gxos::gui::DesktopService::InspectNativeAppPipeline(trim(line.substr(18)))); continue; }
            if(startsWith(line, "nativeapp.smoketest ")) { publishOutput(gxos::gui::DesktopService::NativeAppPipelineSmokeTest(trim(line.substr(20)))); continue; }
            if(line=="nativeapp.processes") { publishOutput(nativeAppProcessesDiagnostic()); continue; }
            if(line=="nativeapp.debuglog") { publishOutput(nativeAppDebugLogDiagnostic("")); continue; }
            if(startsWith(line, "nativeapp.debuglog ")) { publishOutput(nativeAppDebugLogDiagnostic(trim(line.substr(19)))); continue; }
            // Basic demo: echo and simple commands
            std::string resp = "[console] " + line;
            publishOutput(resp);
        }
        Logger::write(LogLevel::Info, "ConsoleService stopped");
        return 0;
    }

    uint64_t ConsoleService::start(){ ProcessSpec spec{"console", ConsoleService::main}; spec.appId = "gxos.system.console"; return ProcessTable::spawn(spec, {"console"}); }
} }
