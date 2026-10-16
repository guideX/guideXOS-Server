#include "lifecycle.h"
#include <string>
#include "allocator.h"
#include "scheduler.h"
#include "logger.h"
#include "ipc_bus.h"
#include "desktop_service.h"
#include "console_service.h"
#include "compositor.h"
#include "gui_protocol.h"
#include "process.h"
#include "notification_manager.h"
#include "firewall.h"
#include "module_manager.h"
#include "native_app_process_table.h"
#include <utility>
#include <vector>

namespace gxos {
    static LifecycleState g_state;

    LifecycleState& Lifecycle::state(){ return g_state; }

    const char* Lifecycle::phaseName(Phase phase){
        switch(phase){
        case Phase::ColdStart: return "ColdStart";
        case Phase::PlatformDiscovery: return "PlatformDiscovery";
        case Phase::MemoryReady: return "MemoryReady";
        case Phase::SchedulerReady: return "SchedulerReady";
        case Phase::IpcReady: return "IpcReady";
        case Phase::DesktopStateReady: return "DesktopStateReady";
        case Phase::ServicesReady: return "ServicesReady";
        case Phase::Interactive: return "Interactive";
        case Phase::ShuttingDown: return "ShuttingDown";
        case Phase::Stopped: return "Stopped";
        }
        return "Unknown";
    }

    void Lifecycle::setPhase(Phase phase){
        g_state.phase = phase;
        Logger::write(LogLevel::Info, std::string("[lifecycle] phase -> ") + phaseName(phase));
    }

    void Lifecycle::ensureIpcChannels(){
        // Create well-known channels up-front to avoid implicit creation races
        ipc::Bus::ensure("gui.input");
        ipc::Bus::ensure("gui.output");
        ipc::Bus::ensure("console.input");
        ipc::Bus::ensure("console.output");
    }

    void Lifecycle::bootstrap(){
        if(g_state.phase != Phase::ColdStart) return;
        setPhase(Phase::PlatformDiscovery);
        g_state.platform = queryPlatform();

        setPhase(Phase::MemoryReady);
        // Allocate a deterministic heap sized from platform discovery (fallback to 512MiB if absent)
        uint64_t heapBytes = g_state.platform.totalMemBytes ? g_state.platform.totalMemBytes : 512ull * 1024 * 1024;
        Allocator::init(heapBytes);
        g_state.allocatorReady = true;

        setPhase(Phase::SchedulerReady);
        // Use a single worker for deterministic ordering; this avoids hidden fairness/preemption assumptions
        Scheduler::init(1);
        g_state.schedulerReady = true;

        setPhase(Phase::IpcReady);
        ensureIpcChannels();
        g_state.ipcReady = true;

        setPhase(Phase::DesktopStateReady);
        gui::DesktopService::LoadState();
        gui::NotificationManager::Initialize();
        Firewall::Initialize();
        ModuleManager::InitializeBuiltins();
        g_state.desktopStateLoaded = true;

        setPhase(Phase::ServicesReady);
    }

    uint64_t Lifecycle::ensureCompositor(){
        if(g_state.compositorPid!=0) return g_state.compositorPid;
        setPhase(Phase::ServicesReady);
        g_state.compositorPid = gui::Compositor::start();
        Logger::write(LogLevel::Info, "[lifecycle] compositor pid=" + std::to_string(g_state.compositorPid));
        return g_state.compositorPid;
    }

    uint64_t Lifecycle::ensureConsole(){
        if(g_state.consolePid!=0) return g_state.consolePid;
        setPhase(Phase::ServicesReady);
        g_state.consolePid = svc::ConsoleService::start();
        Logger::write(LogLevel::Info, "[lifecycle] console pid=" + std::to_string(g_state.consolePid));
        return g_state.consolePid;
    }

    void Lifecycle::markInteractive(){
        if(g_state.phase != Phase::Interactive){
            setPhase(Phase::Interactive);
        }
    }

    namespace {
        constexpr uint64_t kServiceShutdownTimeoutMs = 10000;

        bool waitAndReapProcess(uint64_t pid, const std::string& owner) {
            int exitCode = 0;
            bool completedWithinBound = ProcessTable::wait(pid, kServiceShutdownTimeoutMs, &exitCode);
            if (!completedWithinBound) {
                Logger::write(LogLevel::Error, "[lifecycle] shutdown exceeded " + std::to_string(kServiceShutdownTimeoutMs) +
                    "ms waiting for " + owner + "; retaining host resources until its entry returns process=" +
                    std::to_string(pid));
                if (!ProcessTable::wait(pid, UINT64_MAX, &exitCode)) {
                    Logger::write(LogLevel::Error, "[lifecycle] shutdown lost process-table ownership for " + owner +
                        " process=" + std::to_string(pid));
                    return false;
                }
            }

            bool workerExitedWithinBound = ProcessTable::waitForThreadExit(pid, kServiceShutdownTimeoutMs);
            if (!workerExitedWithinBound) {
                Logger::write(LogLevel::Error, "[lifecycle] shutdown exceeded " + std::to_string(kServiceShutdownTimeoutMs) +
                    "ms waiting for " + owner + " worker; retaining host resources until it exits process=" +
                    std::to_string(pid));
                if (!ProcessTable::waitForThreadExit(pid, UINT64_MAX)) {
                    Logger::write(LogLevel::Error, "[lifecycle] shutdown could not establish worker exit for " + owner +
                        " process=" + std::to_string(pid));
                    return false;
                }
            }
            ProcessTable::terminate(pid);
            Logger::write(LogLevel::Info, "[lifecycle] shutdown reaped " + owner +
                " process=" + std::to_string(pid) + " exitCode=" + std::to_string(exitCode) +
                " withinBound=" + ((completedWithinBound && workerExitedWithinBound) ? "true" : "false"));
            return completedWithinBound && workerExitedWithinBound;
        }

        bool stopNativeApps() {
            const std::vector<apps::NativeAppProcessInfo> processes = apps::NativeAppProcessTable::List();
            std::vector<std::pair<uint64_t, uint64_t>> active;
            for (const apps::NativeAppProcessInfo& process : processes) {
                if (process.processId == 0 ||
                    (process.lifecycleState != apps::NativeAppLifecycleState::Running &&
                     process.lifecycleState != apps::NativeAppLifecycleState::Closing)) {
                    continue;
                }

                bool running = false;
                int exitCode = 0;
                if (!ProcessTable::getStatus(process.processId, running, exitCode)) {
                    Logger::write(LogLevel::Error, "[lifecycle] active native app has no process-table owner runtimeId=" +
                        std::to_string(process.runtimeId) + " process=" + std::to_string(process.processId));
                    return false;
                }
                if (running) {
                    uint32_t requested = 0;
                    for (gx_handle window : process.createdWindowHandles) {
                        if (window == 0) continue;
                        ipc::Message close;
                        close.srcPid = process.processId;
                        close.type = static_cast<uint32_t>(gui::MsgType::MT_Close);
                        const std::string payload = std::to_string(window);
                        close.data.assign(payload.begin(), payload.end());
                        if (ipc::Bus::publish("gui.input", std::move(close), false)) ++requested;
                    }
                    Logger::write(LogLevel::Info, "[lifecycle] native app close requested runtimeId=" +
                        std::to_string(process.runtimeId) + " appId=" + process.appId + " process=" +
                        std::to_string(process.processId) + " windows=" + std::to_string(requested));
                }
                active.emplace_back(process.runtimeId, process.processId);
            }

            bool completedWithinBound = true;
            for (const auto& item : active) {
                if (!waitAndReapProcess(item.second, "native app runtime=" + std::to_string(item.first)))
                    completedWithinBound = false;
            }
            return completedWithinBound;
        }

        bool stopConsole(uint64_t pid) {
            if (pid == 0) return true;
            ipc::Message exitMessage;
            exitMessage.data = {'e', 'x', 'i', 't'};
            if (!ProcessTable::send(pid, std::move(exitMessage))) {
                Logger::write(LogLevel::Warn, "[lifecycle] console exit request was not accepted process=" + std::to_string(pid));
            }
            return waitAndReapProcess(pid, "console");
        }
    }

    bool Lifecycle::stopCompositor(){
        if(g_state.compositorPid==0) return true;
        // Send a polite stop request; compositor interprets MT_Ping with "bye" as shutdown
        ipc::Message m; m.type = (uint32_t)gui::MsgType::MT_Ping; m.data.assign({'b','y','e'});
        ipc::Bus::publish("gui.input", std::move(m), false);
        const uint64_t pid = g_state.compositorPid;
        const bool completedWithinBound = waitAndReapProcess(pid, "compositor");
        g_state.compositorPid = 0;
        return completedWithinBound;
    }

    bool Lifecycle::shutdown(){
        if(g_state.phase == Phase::Stopped) return true;
        setPhase(Phase::ShuttingDown);
        bool completedWithinBound = stopNativeApps();
        if(g_state.consolePid!=0){
            if (!stopConsole(g_state.consolePid)) completedWithinBound = false;
            g_state.consolePid = 0;
        }
        if (!stopCompositor()) completedWithinBound = false;
        if(g_state.schedulerReady){
            Scheduler::shutdown();
            g_state.schedulerReady = false;
        }
        g_state.allocatorReady = false;
        setPhase(Phase::Stopped);
        return completedWithinBound;
    }
}
