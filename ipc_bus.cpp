#include "ipc_bus.h"


#include "ipc_bus.h"
#include "process.h"
#include "allocator.h"
#include "logger.h"
#include <thread>
#include <chrono>

namespace gxos {
    namespace ipc {
        std::unordered_map < std::string, std::shared_ptr < Channel >> Bus::g;
        std::mutex Bus::gmu;

        void Bus::ensure(const std::string& name) {
            std::lock_guard < std::mutex > _g(gmu);
            if (!g.count(name)) g[name] = std::make_shared < Channel >();
            g[name]->name = name;
        }
        std::shared_ptr < Channel > Bus::get(const std::string& name) {
            ensure(name);
            std::lock_guard < std::mutex > _g(gmu);
            return g[name];
        }

        bool Bus::subscribe(const std::string& name, uint64_t pid) {
            auto ch = get(name);
            std::lock_guard < std::mutex > lk(ch->mu);
            ch->subs.insert(pid);
            return true;
        }
        void Bus::unsubscribe(const std::string& name, uint64_t pid) {
            auto ch = get(name);
            std::lock_guard < std::mutex > lk(ch->mu);
            ch->subs.erase(pid);
        }

        void Bus::publish(const std::string& name, Message&& msg, bool fanout) {
            if (msg.srcPid == 0) {
                msg.srcPid = Allocator::currentPid();
            }
            if (!fanout && msg.dstPid != 0) {
                Logger::write(LogLevel::Info, std::string("Bus::publish directing msg type=") + std::to_string(msg.type) + " to pid=" + std::to_string(msg.dstPid));
                ProcessTable::send(msg.dstPid, std::move(msg));
                return;
            }

            Logger::write(LogLevel::Info, std::string("Bus::publish queueing msg type=") + std::to_string(msg.type) + " to channel=" + name);
            auto ch = get(name);
            std::unique_lock < std::mutex > lk(ch->mu);
            ch->cv.wait(lk, [&] {
                return ch->queue.size() < ch->cap;
                });
            if (fanout) {
                for (auto pid : ch->subs) {
                    Message copy = msg;
                    copy.dstPid = pid;
                    ProcessTable::send(pid, std::move(copy));
                }
            }
            else {
                ch->queue.emplace_back(std::move(msg));
            }
            lk.unlock();
            ch->cv.notify_all();
        }

        bool Bus::pop(const std::string& name, Message& out, uint64_t timeoutMs) {
            uint64_t pid = Allocator::currentPid();
            
            // Poll both sources with short intervals until timeout
            auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
            auto ch = get(name);
            
            while (std::chrono::steady_clock::now() < deadline) {
                // Check process mailbox first (directed messages have priority)
                if (pid != 0 && ProcessTable::try_recv(pid, out)) {
                    return true;
                }
                
                // Check channel queue
                {
                    std::lock_guard<std::mutex> lk(ch->mu);
                    if (!ch->queue.empty()) {
                        out = std::move(ch->queue.front());
                        ch->queue.pop_front();
                        return true;
                    }
                }
                
                // Brief sleep to avoid busy spinning
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            
            // Final check before giving up
            if (pid != 0 && ProcessTable::try_recv(pid, out)) {
                return true;
            }
            {
                std::lock_guard<std::mutex> lk(ch->mu);
                if (!ch->queue.empty()) {
                    out = std::move(ch->queue.front());
                    ch->queue.pop_front();
                    return true;
                }
            }
            
            return false;
        }

        bool Bus::setCapacity(const std::string& name, size_t cap) {
            auto ch = get(name);
            std::lock_guard < std::mutex > lk(ch->mu);
            ch->cap = cap;
            ch->cv.notify_all();
            return true;
        }
        size_t Bus::pending(const std::string& name) {
            auto ch = get(name);
            std::lock_guard < std::mutex > lk(ch->mu);
            return ch->queue.size();
        }
        size_t Bus::capacity(const std::string& name) {
            auto ch = get(name);
            std::lock_guard < std::mutex > lk(ch->mu);
            return ch->cap;
        }
    }
}