#include "guidexos_event.h"

#if !defined(GXOS_BARE_METAL)

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <limits>
#include <mutex>

namespace gxos {
namespace runtime {

namespace {
    constexpr gxos_event_uint64 kMaxSignedDuration = 0x7FFFFFFFFFFFFFFFULL;
    std::atomic<gxos_event_uint64> g_liveStates{0};

    bool hasFiniteDurationOverflow(gxos_event_uint64 value) {
        return value > kMaxSignedDuration;
    }

    std::chrono::steady_clock::time_point deadlineFor(const WaitTimeout& timeout) {
        const auto now = std::chrono::steady_clock::now();
        const auto delay = std::chrono::nanoseconds(
            static_cast<std::chrono::nanoseconds::rep>(timeout.nanoseconds));
        const auto remaining = std::chrono::steady_clock::time_point::max() - now;
        return delay > remaining ? std::chrono::steady_clock::time_point::max() : now + delay;
    }
}

struct Event::State {
    State(EventMode eventMode, bool initial)
        : mode(eventMode), signaled(initial), destroyed(false) {
        g_liveStates.fetch_add(1, std::memory_order_relaxed);
    }

    ~State() {
        g_liveStates.fetch_sub(1, std::memory_order_relaxed);
    }

    EventMode mode;
    bool signaled;
    bool destroyed;
    std::mutex mutex;
    std::condition_variable condition;
};

WaitTimeout WaitTimeout::finiteNanoseconds(gxos_event_uint64 value) {
    if (hasFiniteDurationOverflow(value)) {
        return invalid();
    }
    return { true, false, value };
}

WaitTimeout WaitTimeout::finiteMilliseconds(gxos_event_uint64 value) {
    if (value > kMaxSignedDuration / 1000000ULL) {
        return invalid();
    }
    return finiteNanoseconds(value * 1000000ULL);
}

WaitTimeout WaitTimeout::signedMilliseconds(gxos_event_int64 value) {
    if (value < 0) {
        return invalid();
    }
    return finiteMilliseconds(static_cast<gxos_event_uint64>(value));
}

Event::Event() noexcept
    : initialized_(false), closed_(false) {
}

Event::Event(EventMode mode, bool initiallySignaled) noexcept
    : initialized_(false), closed_(false) {
    (void)initialize(mode, initiallySignaled);
}

Event::~Event() noexcept {
    (void)close();
}

bool Event::initialize(EventMode mode, bool initiallySignaled) {
    std::shared_ptr<State> newState;
    try {
        newState = std::make_shared<State>(mode, initiallySignaled);
    }
    catch (...) {
        return false;
    }

    std::lock_guard<std::mutex> lifeLock(lifecycle_mutex_);
    if (initialized_ || closed_) {
        return false;
    }
    state_ = std::move(newState);
    initialized_ = true;
    return true;
}

bool Event::isInitialized() const noexcept {
    std::lock_guard<std::mutex> lifeLock(lifecycle_mutex_);
    return initialized_ && !closed_ && static_cast<bool>(state_);
}

std::shared_ptr<Event::State> Event::stateSnapshot() const {
    std::lock_guard<std::mutex> lifeLock(lifecycle_mutex_);
    return state_;
}

EventStatus Event::signal() {
    std::shared_ptr<State> state = stateSnapshot();
    if (!state) {
        return EventStatus::Invalid;
    }

    bool notify = false;
    bool notifyAll = false;
    {
        std::lock_guard<std::mutex> stateLock(state->mutex);
        if (state->destroyed) {
            return EventStatus::Destroyed;
        }
        if (!state->signaled) {
            state->signaled = true;
            notify = true;
            notifyAll = state->mode == EventMode::ManualReset;
        }
    }

    if (notifyAll) {
        state->condition.notify_all();
    }
    else if (notify) {
        state->condition.notify_one();
    }
    return EventStatus::Ok;
}

EventStatus Event::reset() {
    std::shared_ptr<State> state = stateSnapshot();
    if (!state) {
        return EventStatus::Invalid;
    }

    {
        std::lock_guard<std::mutex> stateLock(state->mutex);
        if (state->destroyed) {
            return EventStatus::Destroyed;
        }
        state->signaled = false;
    }
    return EventStatus::Ok;
}

EventStatus Event::close() {
    std::shared_ptr<State> state;
    {
        std::lock_guard<std::mutex> lifeLock(lifecycle_mutex_);
        if (!initialized_ || closed_ || !state_) {
            return EventStatus::Invalid;
        }
        closed_ = true;
        state.swap(state_);
    }

    {
        std::lock_guard<std::mutex> stateLock(state->mutex);
        state->destroyed = true;
    }
    state->condition.notify_all();
    return EventStatus::Ok;
}

WaitResult Event::wait() {
    return wait(WaitTimeout::infinite());
}

WaitResult Event::wait(const WaitTimeout& timeout) {
    if (!timeout.valid) {
        return WaitResult::Invalid;
    }

    std::shared_ptr<State> state = stateSnapshot();
    if (!state) {
        return WaitResult::Invalid;
    }

    const auto deadline = (!timeout.infinite_wait && timeout.nanoseconds != 0)
        ? deadlineFor(timeout)
        : std::chrono::steady_clock::time_point{};

    std::unique_lock<std::mutex> stateLock(state->mutex);
    const auto consumeIfSignaled = [&]() -> WaitResult {
        if (state->destroyed) {
            return WaitResult::Destroyed;
        }
        if (!state->signaled) {
            return WaitResult::TimedOut;
        }
        if (state->mode == EventMode::AutoReset) {
            state->signaled = false;
        }
        return WaitResult::Signaled;
    };

    for (;;) {
        if (state->destroyed) {
            return WaitResult::Destroyed;
        }
        if (state->signaled) {
            return consumeIfSignaled();
        }
        if (timeout.infinite_wait) {
            state->condition.wait(stateLock);
            continue;
        }
        if (timeout.nanoseconds == 0) {
            return WaitResult::TimedOut;
        }

        if (std::chrono::steady_clock::now() >= deadline) {
            return consumeIfSignaled() == WaitResult::Signaled
                ? WaitResult::Signaled
                : (state->destroyed ? WaitResult::Destroyed : WaitResult::TimedOut);
        }

        const std::cv_status status = state->condition.wait_until(stateLock, deadline);
        if (status == std::cv_status::timeout) {
            // Recheck while still holding the event lock.  A signal that won
            // the lock before this final check is reported as Signaled; a
            // later signal remains pending for a future wait.
            return consumeIfSignaled() == WaitResult::Signaled
                ? WaitResult::Signaled
                : (state->destroyed ? WaitResult::Destroyed : WaitResult::TimedOut);
        }
    }
}

} // namespace runtime
} // namespace gxos

namespace gxos {
namespace runtime {

extern "C" gxos_event_uint64 gxos_event_live_state_count_for_test() {
    // This diagnostic is used only by the bounded native regression harness;
    // it does not participate in event semantics.
    return g_liveStates.load(std::memory_order_relaxed);
}

} // namespace runtime
} // namespace gxos

#endif // !GXOS_BARE_METAL
