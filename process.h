#pragma once
#include <cstdint>
#include <string>
#include <memory>
#include <vector>
#include <functional>
#include <unordered_map>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include "ipc.h"

namespace gxos {
    using ProcessFn = std::function<int(int,char**)>; // simplified entry point
    #if defined(_WIN32)
    using ProcessThreadHandle = void*;
    #else
    using ProcessThreadHandle = std::thread::native_handle_type;
    #endif

    struct ProcessSpec {
        std::string name;
        ProcessFn entry;
        std::string appId;
    };

    struct ProcessTombstoneRecord {
        uint64_t pid = 0;
        std::string displayName;
        std::string appId;
        std::string windowTitle;
        std::string reason;
        bool appTombstoneCapabilityKnown = false;
        bool appTombstoneCapable = false;
        std::string appTombstoneCapabilitySource = "N/A";
        bool exitCodeAvailable = false;
        int exitCode = 0;
        bool runtimeMsAvailable = false;
        uint64_t runtimeMs = 0;
        bool startedAtMsAvailable = false;
        uint64_t startedAtMs = 0;
        bool endedAtMsAvailable = false;
        uint64_t endedAtMs = 0;
        bool finalMemoryBytesAvailable = false;
        uint64_t finalMemoryBytes = 0;
        bool finalCpuPctAvailable = false;
        int finalCpuPct = 0;
        std::string lastMessage;
        bool restoreSupported = false;
        bool endSupported = false;
    };

    struct ProcessCpuTelemetry {
        bool available = false;
        bool running = false;
        uint64_t cpuMicros = 0;
        uint64_t startWallMicros = 0;
        const char* source = "processThreadCpuTime";
    };

    class Process {
    public:
        uint64_t pid; std::string name; std::string appId; ipc::Mailbox mbox; ProcessFn entry; std::atomic<bool> running{false}; int exitCode=0;
        // phase 1: completion signalling
        std::mutex mu; std::condition_variable cv; std::atomic<bool> finished{false};
        std::atomic<bool> tombstoneCaptured{false};
        std::atomic<bool> cpuSampleValid{false};
        std::atomic<uint64_t> cpuFinalMicros{0};
        ProcessThreadHandle cpuThreadHandle{};
        uint64_t startWallMicros = 0;
        Process(uint64_t id, const std::string& n, const std::string& a, ProcessFn fn): pid(id), name(n), appId(a), mbox(), entry(fn){}
        ~Process();
    };

    class ProcessTable {
    public:
        static uint64_t spawn(const ProcessSpec& spec, const std::vector<std::string>& args);
        static bool send(uint64_t dstPid, ipc::Message&& msg);
        static bool try_recv(uint64_t pid, ipc::Message& out);
        static bool try_recv_type(uint64_t pid, uint32_t type, ipc::Message& out);
        static bool wait_recv(uint64_t pid, ipc::Message& out, uint64_t timeoutMs);
        static bool terminate(uint64_t pid);
        static std::vector<uint64_t> list();
        // phase 1: join/wait and status
        static bool wait(uint64_t pid, uint64_t timeoutMs, int* exitCodeOut=nullptr);
        static bool getStatus(uint64_t pid, bool& runningOut, int& exitCodeOut);
        static bool cpuTelemetry(uint64_t pid, ProcessCpuTelemetry& out);
        static bool getIdentity(uint64_t pid, std::string& nameOut, std::string& appIdOut);
        static void recordTombstone(const ProcessTombstoneRecord& record);
        static std::vector<ProcessTombstoneRecord> tombstones();
        static bool claimTombstoneCapture(uint64_t pid);
        static constexpr uint32_t kTombstoneHistoryMax = 64;
    private:
        static std::unordered_map<uint64_t, std::shared_ptr<Process>> g_proc;
        static std::vector<ProcessTombstoneRecord> g_tombstones;
        static uint64_t g_nextPid;
        static std::mutex g_lock;
        static std::mutex g_tombstoneLock;
    };
}
