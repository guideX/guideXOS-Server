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

    void Lifecycle::stopCompositor(){
        if(g_state.compositorPid==0) return;
        // Send a polite stop request; compositor interprets MT_Ping with "bye" as shutdown
        ipc::Message m; m.type = (uint32_t)gui::MsgType::MT_Ping; m.data.assign({'b','y','e'});
        ipc::Bus::publish("gui.input", std::move(m), false);
        ProcessTable::wait(g_state.compositorPid, 2000, nullptr);
        ProcessTable::terminate(g_state.compositorPid);
        g_state.compositorPid = 0;
    }

    void Lifecycle::shutdown(){
        if(g_state.phase == Phase::Stopped) return;
        setPhase(Phase::ShuttingDown);
        stopCompositor();
        if(g_state.consolePid!=0){
            Logger::write(LogLevel::Info, "[lifecycle] console stop is SHADOWED (no teardown hook yet)");
            g_state.consolePid = 0;
        }
        if(g_state.schedulerReady){
            Scheduler::shutdown();
            g_state.schedulerReady = false;
        }
        g_state.allocatorReady = false;
        setPhase(Phase::Stopped);
    }
}
