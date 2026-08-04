#include "ipc_bus.h"


#include "ipc_bus.h"
#include "process.h"
#include "allocator.h"
#include "logger.h"
#include "gui_protocol.h"
#include <algorithm>
#include <thread>
#include <chrono>

namespace gxos {
    namespace ipc {
        static bool isGuiPresentationMessage(uint32_t type) {
            switch (static_cast<gxos::gui::MsgType>(type)) {
            case gxos::gui::MsgType::MT_SetTitle:
            case gxos::gui::MsgType::MT_DrawText:
            case gxos::gui::MsgType::MT_DrawRect:
            case gxos::gui::MsgType::MT_DrawImage:
            case gxos::gui::MsgType::MT_DrawTextAt:
            case gxos::gui::MsgType::MT_DrawTextAtColor:
            case gxos::gui::MsgType::MT_DrawImageAnimated:
                return true;
            default:
                return false;
            }
        }

        static bool guiWindowId(const Message& message, uint64_t& windowId) {
            if (!isGuiPresentationMessage(message.type)) return false;
            const std::string payload(message.data.begin(), message.data.end());
            const std::size_t separator = payload.find('|');
            if (separator == std::string::npos || separator == 0) return false;
            try {
                windowId = std::stoull(payload.substr(0, separator));
                return windowId != 0;
            } catch (...) {
                return false;
            }
        }

        static void coalesceQueuedGuiPaint(Channel& channel, const Message& message) {
            if (channel.name != "gui.input" ||
                message.type != static_cast<uint32_t>(gxos::gui::MsgType::MT_DrawText)) {
                return;
            }
            const std::string payload(message.data.begin(), message.data.end());
            const std::size_t separator = payload.find('|');
            if (separator == std::string::npos || payload.substr(separator + 1) != "\f") return;

            uint64_t windowId = 0;
            if (!guiWindowId(message, windowId)) return;
            for (auto it = channel.queue.begin(); it != channel.queue.end();) {
                uint64_t queuedWindowId = 0;
                if (guiWindowId(*it, queuedWindowId) && queuedWindowId == windowId) {
                    it = channel.queue.erase(it);
                } else {
                    ++it;
                }
            }
        }

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
                const uint64_t destination = msg.dstPid;
                ProcessTable::send(destination, std::move(msg));
                return;
            }

            Logger::write(LogLevel::Info, std::string("Bus::publish queueing msg type=") + std::to_string(msg.type) + " to channel=" + name);
            auto ch = get(name);
            std::unique_lock < std::mutex > lk(ch->mu);
            coalesceQueuedGuiPaint(*ch, msg);
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
                if (pid != 0) {
                    const bool received = ProcessTable::try_recv(pid, out);
                    if (received) {
                        return true;
                    }
                }
                
                // Check channel queue
                {
                    std::lock_guard<std::mutex> lk(ch->mu);
                    if (!ch->queue.empty()) {
                        out = std::move(ch->queue.front());
                        ch->queue.pop_front();
                        ch->cv.notify_all();
                        return true;
                    }
                }
                
                // Brief sleep to avoid busy spinning
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            
            // Final check before giving up
            if (pid != 0) {
                const bool received = ProcessTable::try_recv(pid, out);
                if (received) {
                    return true;
                }
            }
            {
                std::lock_guard<std::mutex> lk(ch->mu);
                if (!ch->queue.empty()) {
                    out = std::move(ch->queue.front());
                    ch->queue.pop_front();
                    ch->cv.notify_all();
                    return true;
                }
            }
            
            return false;
        }

        bool Bus::popType(const std::string& name, uint32_t type, Message& out, uint64_t timeoutMs) {
            const uint64_t pid = Allocator::currentPid();
            auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
            auto ch = get(name);
            while (true) {
                if (pid != 0 && ProcessTable::try_recv_type(pid, type, out)) {
                    return true;
                }
                {
                    std::lock_guard<std::mutex> lk(ch->mu);
                    auto it = std::find_if(ch->queue.begin(), ch->queue.end(), [type](const Message& message) {
                        return message.type == type;
                    });
                    if (it != ch->queue.end()) {
                        out = std::move(*it);
                        ch->queue.erase(it);
                        ch->cv.notify_all();
                        return true;
                    }
                }
                if (timeoutMs == 0 || std::chrono::steady_clock::now() >= deadline) return false;
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
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
