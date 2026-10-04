#pragma once

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>

namespace gxos { namespace apps {

// Navigator's hosted action path uses this bounded in-process completion table
// alongside the existing directed ProcessTable mailbox. It is intentionally
// app-local and does not provide a general request/reply transport.
class NavigatorAppActionDelivery {
public:
    static constexpr size_t kCapacity = 32;

    struct Request {
        uint64_t targetPid = 0;
        uint64_t registrationGeneration = 0;
        uint64_t token = 0;
    };

    enum class WaitResult {
        Consumed,
        Rejected,
        TimedOut,
        Missing
    };

    bool Reserve(uint64_t targetPid, uint64_t registrationGeneration, Request& out) {
        out = Request{};
        if (targetPid == 0 || registrationGeneration == 0) return false;

        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_tokensExhausted) return false;
        Slot* available = nullptr;
        for (Slot& slot : m_slots) {
            if (!slot.used) {
                available = &slot;
                break;
            }
        }
        if (!available) return false;

        const uint64_t token = m_nextToken;
        if (token == std::numeric_limits<uint64_t>::max()) m_tokensExhausted = true;
        else ++m_nextToken;

        available->used = true;
        available->request = Request{ targetPid, registrationGeneration, token };
        available->state = State::Queued;
        out = available->request;
        return true;
    }

    bool BeginConsumption(const Request& request) {
        std::lock_guard<std::mutex> lock(m_mutex);
        Slot* slot = Find(request);
        if (!slot || slot->state != State::Queued) return false;
        slot->state = State::Consuming;
        return true;
    }

    bool AcknowledgeConsumed(const Request& request) {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            Slot* slot = Find(request);
            if (!slot || slot->state != State::Consuming) return false;
            slot->state = State::Consumed;
        }
        m_changed.notify_all();
        return true;
    }

    bool Reject(const Request& request) {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            Slot* slot = Find(request);
            if (!slot || (slot->state != State::Queued && slot->state != State::Consuming)) return false;
            slot->state = State::Rejected;
        }
        m_changed.notify_all();
        return true;
    }

    bool Cancel(const Request& request) {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            Slot* slot = Find(request);
            if (!slot || slot->state == State::Consumed || slot->state == State::Rejected) return false;
            *slot = Slot{};
        }
        m_changed.notify_all();
        return true;
    }

    template <typename Rep, typename Period>
    WaitResult Wait(const Request& request, const std::chrono::duration<Rep, Period>& timeout) {
        std::unique_lock<std::mutex> lock(m_mutex);
        const bool signalled = m_changed.wait_for(lock, timeout, [&] {
            const Slot* slot = Find(request);
            return !slot || slot->state == State::Consumed || slot->state == State::Rejected;
        });

        Slot* slot = Find(request);
        if (!slot) return WaitResult::Missing;
        if (slot->state == State::Consumed) {
            *slot = Slot{};
            return WaitResult::Consumed;
        }
        if (slot->state == State::Rejected) {
            *slot = Slot{};
            return WaitResult::Rejected;
        }
        if (!signalled) {
            // A late consumer can no longer claim or acknowledge this token.
            *slot = Slot{};
            return WaitResult::TimedOut;
        }
        return WaitResult::Missing;
    }

private:
    enum class State { Free, Queued, Consuming, Consumed, Rejected };
    struct Slot {
        bool used = false;
        Request request;
        State state = State::Free;
    };

    Slot* Find(const Request& request) {
        for (Slot& slot : m_slots) {
            if (slot.used && slot.request.targetPid == request.targetPid &&
                slot.request.registrationGeneration == request.registrationGeneration &&
                slot.request.token == request.token) return &slot;
        }
        return nullptr;
    }

    const Slot* Find(const Request& request) const {
        for (const Slot& slot : m_slots) {
            if (slot.used && slot.request.targetPid == request.targetPid &&
                slot.request.registrationGeneration == request.registrationGeneration &&
                slot.request.token == request.token) return &slot;
        }
        return nullptr;
    }

    std::array<Slot, kCapacity> m_slots{};
    std::mutex m_mutex;
    std::condition_variable m_changed;
    uint64_t m_nextToken = 1;
    bool m_tokensExhausted = false;
};

}} // namespace gxos::apps
