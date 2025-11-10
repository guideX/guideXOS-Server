#pragma once
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <deque>
#include <mutex>
#include <condition_variable>
#include "ipc.h"

namespace gxos { namespace ipc {
    struct Channel {
        std::string name;
        std::unordered_set<uint64_t> subs;
        std::deque<Message> queue;
        size_t cap{1024};
        std::mutex mu; std::condition_variable cv;
    };

    class Bus {
    public:
        static void ensure(const std::string& name);
        static bool subscribe(const std::string& name, uint64_t pid);
        static void unsubscribe(const std::string& name, uint64_t pid);
        static void publish(const std::string& name, Message&& msg, bool fanout=true);
        // For host/REPL to read from a named channel
        static bool pop(const std::string& name, Message& out, uint64_t timeoutMs);
        // New helpers for phase 1
        static bool setCapacity(const std::string& name, size_t cap);
        static size_t pending(const std::string& name);
        static size_t capacity(const std::string& name);
    private:
        static std::unordered_map<std::string, std::shared_ptr<Channel>> g;
        static std::mutex gmu;
        static std::shared_ptr<Channel> get(const std::string& name);
    };
} }
