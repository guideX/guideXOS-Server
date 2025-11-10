#pragma once
#include <functional>
#include <vector>
#include <thread>
#include <atomic>
#include <cstdint>
#include <mutex>
namespace gxos {
    struct Task { std::function<void()> fn; };
    class Scheduler {
    public:
        static void init(unsigned workers = 2);
        static void enqueue(const Task& t);
        static void shutdown();
        static uint64_t tasksExecuted();
    private:
        static std::vector<std::thread> g_threads;
        static std::vector<Task> g_queue;
        static std::mutex g_lock;
        static std::atomic<bool> g_stop;
        static std::atomic<uint64_t> g_executed;
        static void loop();
    };
}
