#include "scheduler.h"
#include "logger.h"
#include <condition_variable>
#include <chrono>
#include <algorithm>

namespace gxos {
    std::vector<std::thread> Scheduler::g_threads; 
    std::vector<Task> Scheduler::g_queue; 
    std::mutex Scheduler::g_lock; 
    std::atomic<bool> Scheduler::g_stop{false}; 
    std::atomic<uint64_t> Scheduler::g_executed{0};
    std::atomic<uint64_t> Scheduler::g_busyMicros{0};
    std::atomic<uint64_t> Scheduler::g_idleMicros{0};
    std::mutex Scheduler::g_telemetryLock;
    uint64_t Scheduler::g_lastSampleBusyMicros = 0;
    uint64_t Scheduler::g_lastSampleIdleMicros = 0;
    uint64_t Scheduler::g_lastSampleWallMicros = 0;
    static std::condition_variable g_cv;
    static uint64_t g_pendingSampleBusyMicros = 0;
    static uint64_t g_pendingSampleWindowMicros = 0;
    static bool g_hasStableCpuSample = false;
    static CpuTelemetrySnapshot g_lastStableCpuSample{};

    namespace {
        constexpr uint64_t kCpuTelemetryDisplayWindowMicros = 1000ULL * 1000ULL;

        static uint64_t durationMicros(const std::chrono::steady_clock::duration& duration) {
            return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(duration).count());
        }

        static uint64_t microsToMillisCeil(uint64_t micros) {
            return micros == 0 ? 0 : ((micros + 999) / 1000);
        }

        static uint64_t clampBusyMicros(uint64_t busyMicros, uint64_t windowMicros) {
            return busyMicros > windowMicros ? windowMicros : busyMicros;
        }
    }

    void Scheduler::init(unsigned workers){
        g_stop = false;
        g_executed = 0;
        g_busyMicros = 0;
        g_idleMicros = 0;
        g_lastSampleBusyMicros = 0;
        g_lastSampleIdleMicros = 0;
        g_lastSampleWallMicros = durationMicros(std::chrono::steady_clock::now().time_since_epoch());
        g_pendingSampleBusyMicros = 0;
        g_pendingSampleWindowMicros = 0;
        g_hasStableCpuSample = false;
        g_lastStableCpuSample = CpuTelemetrySnapshot{};
        {
            std::lock_guard<std::mutex> lk(g_lock);
            g_queue.clear();
        }
        for (unsigned i=0;i<workers;i++){
            g_threads.emplace_back(loop);
        }
    }
    void Scheduler::enqueue(const Task& t){ std::lock_guard<std::mutex> _g(g_lock); g_queue.push_back(t); g_cv.notify_one(); }
    void Scheduler::shutdown(){ g_stop=true; g_cv.notify_all(); for(auto& th: g_threads){ if(th.joinable()) th.join(); } g_threads.clear(); }
    uint64_t Scheduler::tasksExecuted(){ return g_executed.load(); }

    CpuTelemetrySnapshot Scheduler::cpuTelemetrySnapshot() {
        CpuTelemetrySnapshot snapshot;
        snapshot.source = "schedulerIdleBusyWarmup";

        const uint64_t busyTotalMicros = g_busyMicros.load(std::memory_order_relaxed);
        const uint64_t idleTotalMicros = g_idleMicros.load(std::memory_order_relaxed);
        const uint64_t nowMicros = durationMicros(std::chrono::steady_clock::now().time_since_epoch());

        std::lock_guard<std::mutex> lk(g_telemetryLock);
        const uint64_t busyDeltaMicros = busyTotalMicros - g_lastSampleBusyMicros;
        const uint64_t windowMicros = nowMicros - g_lastSampleWallMicros;
        g_lastSampleBusyMicros = busyTotalMicros;
        g_lastSampleIdleMicros = idleTotalMicros;
        g_lastSampleWallMicros = nowMicros;
        g_pendingSampleBusyMicros += busyDeltaMicros;
        g_pendingSampleWindowMicros += windowMicros;

        if (g_pendingSampleWindowMicros >= kCpuTelemetryDisplayWindowMicros) {
            const uint64_t displayWindowMicros = g_pendingSampleWindowMicros;
            const uint64_t displayBusyMicros = clampBusyMicros(g_pendingSampleBusyMicros, displayWindowMicros);
            const uint64_t displayIdleMicros = displayWindowMicros > displayBusyMicros ? displayWindowMicros - displayBusyMicros : 0;
            const uint64_t rawPct = displayWindowMicros == 0 ? 0 : ((displayBusyMicros * 100ULL) / displayWindowMicros);

            snapshot.available = true;
            snapshot.source = "schedulerIdleBusy";
            snapshot.utilizationPct = static_cast<int>(std::min<uint64_t>(100ULL, rawPct));
            snapshot.busyTimeMs = microsToMillisCeil(displayBusyMicros);
            snapshot.idleTimeMs = microsToMillisCeil(displayIdleMicros);
            snapshot.sampleWindowMs = microsToMillisCeil(displayWindowMicros);

            g_lastStableCpuSample = snapshot;
            g_hasStableCpuSample = true;
            g_pendingSampleBusyMicros = 0;
            g_pendingSampleWindowMicros = 0;
            return snapshot;
        }

        snapshot.busyTimeMs = microsToMillisCeil(g_pendingSampleBusyMicros);
        snapshot.idleTimeMs = microsToMillisCeil(g_pendingSampleWindowMicros > g_pendingSampleBusyMicros ? g_pendingSampleWindowMicros - g_pendingSampleBusyMicros : 0);
        snapshot.sampleWindowMs = microsToMillisCeil(g_pendingSampleWindowMicros);

        if (g_hasStableCpuSample) {
            return g_lastStableCpuSample;
        }

        return snapshot;
    }

    void Scheduler::loop(){
        std::unique_lock<std::mutex> lk(g_lock, std::defer_lock);
        while(!g_stop){
            lk.lock();
            const auto waitStart = std::chrono::steady_clock::now();
            g_cv.wait(lk, []{ return g_stop || !g_queue.empty(); });
            const auto waitEnd = std::chrono::steady_clock::now();
            g_idleMicros.fetch_add(durationMicros(waitEnd - waitStart), std::memory_order_relaxed);
            if (g_stop){ lk.unlock(); break; }
            Task t = g_queue.back(); g_queue.pop_back();
            lk.unlock();
            const auto runStart = std::chrono::steady_clock::now();
            try{ t.fn(); g_executed++; }
            catch(...){ Logger::write(LogLevel::Error, "Task threw exception"); }
            const auto runEnd = std::chrono::steady_clock::now();
            g_busyMicros.fetch_add(durationMicros(runEnd - runStart), std::memory_order_relaxed);
        }
    }
}
