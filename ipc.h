#pragma once
#include <algorithm>
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
        size_t size() const { std::lock_guard<std::mutex> lk(_mu); return _q.size() + _priorityQ.size(); }
        void push(Message&& m){
            std::unique_lock<std::mutex> lk(_mu);
            _cv.wait(lk, [&]{ return _q.size() < _cap; });
            _q.emplace_back(std::move(m));
            lk.unlock(); _cv.notify_all();
        }
        // Input and window-lifecycle messages use a separate small FIFO lane.
        // A compositor or hosted app can be busy publishing a bounded render
        // burst, but real input must not wait behind that normal mailbox.
        bool try_push_priority(Message&& m){
            std::lock_guard<std::mutex> lk(_mu);
            if (_priorityQ.size() >= kPriorityCap) return false;
            _priorityQ.emplace_back(std::move(m));
            _cv.notify_all();
            return true;
        }
        bool pop(Message& out, uint64_t timeoutMs){
            std::unique_lock<std::mutex> lk(_mu);
            if(!_cv.wait_for(lk, std::chrono::milliseconds(timeoutMs), [&]{ return !_priorityQ.empty() || !_q.empty(); })) return false;
            if (!_priorityQ.empty()) {
                out = std::move(_priorityQ.front()); _priorityQ.pop_front();
            } else {
                out = std::move(_q.front()); _q.pop_front();
            }
            lk.unlock(); _cv.notify_all();
            return true;
        }
        bool try_pop(Message& out){
            std::lock_guard<std::mutex> lk(_mu);
            if (_priorityQ.empty() && _q.empty()) return false;
            if (!_priorityQ.empty()) {
                out = std::move(_priorityQ.front());
                _priorityQ.pop_front();
            } else {
                out = std::move(_q.front());
                _q.pop_front();
            }
            _cv.notify_all();
            return true;
        }
        bool try_pop_type(uint32_t type, Message& out){
            std::lock_guard<std::mutex> lk(_mu);
            auto priorityIt = std::find_if(_priorityQ.begin(), _priorityQ.end(), [type](const Message& message) {
                return message.type == type;
            });
            if (priorityIt != _priorityQ.end()) {
                out = std::move(*priorityIt);
                _priorityQ.erase(priorityIt);
                _cv.notify_all();
                return true;
            }
            auto it = std::find_if(_q.begin(), _q.end(), [type](const Message& message) {
                return message.type == type;
            });
            if (it == _q.end()) return false;
            out = std::move(*it);
            _q.erase(it);
            _cv.notify_all();
            return true;
        }
    private:
        static constexpr size_t kPriorityCap = 256;
        mutable std::mutex _mu;
        std::condition_variable _cv;
        std::deque<Message> _q;
        std::deque<Message> _priorityQ;
        size_t _cap;
    };
} }
