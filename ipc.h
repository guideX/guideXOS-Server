#pragma once
#include <vector>
#include <cstdint>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <chrono>

namespace gxos { namespace ipc {
    struct Message {
        uint64_t srcPid{0};
        uint64_t dstPid{0};
        uint32_t type{0};
        std::vector<uint8_t> data; // opaque payload
    };

    class Mailbox {
    public:
        Mailbox(size_t cap=1024) : _cap(cap) {}
        void setCapacity(size_t cap){ std::lock_guard<std::mutex> lk(_mu); _cap = cap; _cv.notify_all(); }
        size_t capacity() const { return _cap; }
        size_t size() const { std::lock_guard<std::mutex> lk(_mu); return _q.size(); }
        void push(Message&& m){
            std::unique_lock<std::mutex> lk(_mu);
            _cv.wait(lk, [&]{ return _q.size() < _cap; });
            _q.emplace_back(std::move(m));
            lk.unlock(); _cv.notify_all();
        }
        bool pop(Message& out, uint64_t timeoutMs){
            std::unique_lock<std::mutex> lk(_mu);
            if(!_cv.wait_for(lk, std::chrono::milliseconds(timeoutMs), [&]{ return !_q.empty(); })) return false;
            out = std::move(_q.front()); _q.pop_front();
            lk.unlock(); _cv.notify_all();
            return true;
        }
        bool try_pop(Message& out){
            std::lock_guard<std::mutex> lk(_mu);
            if (_q.empty()) return false; out = std::move(_q.front()); _q.pop_front(); return true;
        }
    private:
        mutable std::mutex _mu; std::condition_variable _cv; std::deque<Message> _q; size_t _cap;
    };
} }
