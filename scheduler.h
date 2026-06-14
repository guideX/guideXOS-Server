#pragma once
#include <functional>
#include <vector>
#include <thread>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
namespace gxos {
    struct Task { std::function<void()> fn; };
    struct CpuTelemetrySnapshot {
        bool available = false;
        int utilizationPct = 0;
        uint64_t busyTimeMs = 0;
        uint64_t idleTimeMs = 0;
        uint64_t sampleWindowMs = 0;
        std::string source = "schedulerIdleBusy";
    };
    class Scheduler {
    public:
        static void init(unsigned workers = 2);
        static void enqueue(const Task& t);
        static void shutdown();
        static uint64_t tasksExecuted();
        static CpuTelemetrySnapshot cpuTelemetrySnapshot();
    private:
        static std::vector<std::thread> g_threads;
        static std::vector<Task> g_queue;
        static std::mutex g_lock;
        static std::atomic<bool> g_stop;
        static std::atomic<uint64_t> g_executed;
        static std::atomic<uint64_t> g_busyMicros;
        static std::atomic<uint64_t> g_idleMicros;
        static std::mutex g_telemetryLock;
        static uint64_t g_lastSampleBusyMicros;
        static uint64_t g_lastSampleIdleMicros;
        static uint64_t g_lastSampleWallMicros;
        static void loop();
    };
}
