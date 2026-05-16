#include "console_service.h"
#include "desktop_service.h"
#include "native_app_process_table.h"
#include "process.h"
#include "logger.h"
#include "scheduler.h"
#include <sstream>

namespace gxos { namespace svc {
    using namespace gxos;
    static const char* kInputChan = "console.input";
    static const char* kOutputChan = "console.output";

    static std::string trim(const std::string& s){ size_t a = s.find_first_not_of(" \t\r\n"); if(a==std::string::npos) return {}; size_t b = s.find_last_not_of(" \t\r\n"); return s.substr(a, b-a+1); }

    static bool startsWith(const std::string& value, const std::string& prefix) {
        return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
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
                << " exitCode=" << process.exitCode
                << " failureReason=" << process.failureReason
                << " experimentalExecutionEnabled=" << (process.experimentalExecutionEnabled ? "true" : "false")
                << " hostArchitecture=" << process.hostArchitecture
                << "\n";
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
            if(line=="desktop.apps.verbose") { publishOutput(gxos::gui::DesktopService::GetRegisteredAppsVerboseDiagnostic()); continue; }
            if(startsWith(line, "nativeapp.inspect ")) { publishOutput(gxos::gui::DesktopService::InspectNativeAppPipeline(trim(line.substr(18)))); continue; }
            if(startsWith(line, "nativeapp.smoketest ")) { publishOutput(gxos::gui::DesktopService::NativeAppPipelineSmokeTest(trim(line.substr(20)))); continue; }
            if(line=="nativeapp.processes") { publishOutput(nativeAppProcessesDiagnostic()); continue; }
            // Basic demo: echo and simple commands
            std::string resp = "[console] " + line;
            publishOutput(resp);
        }
        Logger::write(LogLevel::Info, "ConsoleService stopped");
        return 0;
    }

    uint64_t ConsoleService::start(){ ProcessSpec spec{"console", ConsoleService::main}; return ProcessTable::spawn(spec, {"console"}); }
} }
