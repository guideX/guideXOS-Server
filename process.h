#pragma once
#include <cstdint>
#include <string>
#include <memory>
#include <vector>
#include <functional>
#include <unordered_map>
#include <mutex>
#include <condition_variable>
#include "ipc.h"

namespace gxos {
    using ProcessFn = std::function<int(int,char**)>; // simplified entry point

    struct ProcessSpec { std::string name; ProcessFn entry; };

    class Process {
    public:
        uint64_t pid; std::string name; ipc::Mailbox mbox; ProcessFn entry; bool running=false; int exitCode=0;
        // phase 1: completion signalling
        std::mutex mu; std::condition_variable cv; bool finished=false;
        Process(uint64_t id, const std::string& n, ProcessFn fn): pid(id), name(n), mbox(), entry(fn){}
    };

    class ProcessTable {
    public:
        static uint64_t spawn(const ProcessSpec& spec, const std::vector<std::string>& args);
        static bool send(uint64_t dstPid, ipc::Message&& msg);
        static bool try_recv(uint64_t pid, ipc::Message& out);
        static bool wait_recv(uint64_t pid, ipc::Message& out, uint64_t timeoutMs);
        static bool terminate(uint64_t pid);
        static std::vector<uint64_t> list();
        // phase 1: join/wait and status
        static bool wait(uint64_t pid, uint64_t timeoutMs, int* exitCodeOut=nullptr);
        static bool getStatus(uint64_t pid, bool& runningOut, int& exitCodeOut);
    private:
        static std::unordered_map<uint64_t, std::shared_ptr<Process>> g_proc;
        static uint64_t g_nextPid;
        static std::mutex g_lock;
    };
}
