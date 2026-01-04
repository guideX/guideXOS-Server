#pragma once
#include <cstdint>
#include "platform.h"

namespace gxos {
    enum class Phase {
        ColdStart,
        PlatformDiscovery,
        MemoryReady,
        SchedulerReady,
        IpcReady,
        DesktopStateReady,
        ServicesReady,
        Interactive,
        ShuttingDown,
        Stopped
    };

    struct LifecycleState {
        Phase phase{Phase::ColdStart};
        PlatformInfo platform{};
        bool allocatorReady{false};
        bool schedulerReady{false};
        bool ipcReady{false};
        bool desktopStateLoaded{false};
        uint64_t compositorPid{0};
        uint64_t consolePid{0};
    };

    class Lifecycle {
    public:
        static LifecycleState& state();
        // Run deterministic bootstrap (platform -> memory -> scheduler -> IPC -> desktop state)
        static void bootstrap();
        // Ensure services exist; idempotent
        static uint64_t ensureCompositor();
        static uint64_t ensureConsole();
        static void markInteractive();
        // Request orderly shutdown and block until core services are down
        static void shutdown();
    private:
        static void setPhase(Phase phase);
        static const char* phaseName(Phase phase);
        static void ensureIpcChannels();
        static void stopCompositor();
    };
}
