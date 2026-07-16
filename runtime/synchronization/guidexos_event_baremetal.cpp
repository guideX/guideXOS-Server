#include "guidexos_event.h"
#include "guidexos_event_baremetal.h"

#if defined(GXOS_BARE_METAL)

namespace gxos {
namespace runtime {
namespace {
    using scheduler_wait::WakeReason;

    struct CriticalScope {
        void* token;
        bool active;

        CriticalScope()
            : token(nullptr), active(scheduler_wait::schedulerWaitCriticalAvailable()) {
            if (active) {
                token = scheduler_wait::enterCritical();
            }
        }

        ~CriticalScope() {
            release();
        }

        void release() {
            if (active) {
                scheduler_wait::leaveCritical(token);
                active = false;
                token = nullptr;
            }
        }
    };

    WaitResult mapWakeReason(WakeReason reason) {
        switch (reason) {
        case WakeReason::Signaled:
            return WaitResult::Signaled;
        case WakeReason::TimedOut:
            return WaitResult::TimedOut;
        case WakeReason::Destroyed:
            return WaitResult::Destroyed;
        case WakeReason::Interrupted:
            return WaitResult::Interrupted;
        case WakeReason::Cancelled:
            return WaitResult::Interrupted;
        case WakeReason::None:
        default:
            return WaitResult::Interrupted;
        }
    }

    scheduler_wait::WaitDuration makeWaitDuration(const WaitTimeout& timeout) {
        return { timeout.infinite_wait, timeout.nanoseconds };
    }
}

WaitTimeout WaitTimeout::finiteNanoseconds(gxos_event_uint64 value) {
    return value > 0x7FFFFFFFFFFFFFFFULL ? invalid() : WaitTimeout{ true, false, value };
}

WaitTimeout WaitTimeout::finiteMilliseconds(gxos_event_uint64 value) {
    if (value > 0x7FFFFFFFFFFFFFFFULL / 1000000ULL) {
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
    : state_{ EventMode::ManualReset, false, false, { nullptr, nullptr, 0 } },
      initialized_(false),
      closed_(false) {
}

Event::Event(EventMode mode, bool initiallySignaled) noexcept
    : state_{ mode, initiallySignaled, false, { nullptr, nullptr, 0 } },
      initialized_(true),
      closed_(false) {
}

Event::~Event() noexcept {
    (void)close();
}

bool Event::initialize(EventMode mode, bool initiallySignaled) {
    CriticalScope lock;
    if (initialized_ || closed_) {
        return false;
    }
    state_ = State{ mode, initiallySignaled, false, { nullptr, nullptr, 0 } };
    initialized_ = true;
    return true;
}

bool Event::isInitialized() const noexcept {
    return initialized_ && !closed_;
}

EventStatus Event::signal() {
    CriticalScope lock;
    if (!initialized_ || closed_) {
        return EventStatus::Invalid;
    }
    if (state_.destroyed) {
        return EventStatus::Destroyed;
    }
    if (!scheduler_wait::waitQueueEmpty(&state_.waiters) &&
        !scheduler_wait::schedulerWaitAvailable()) {
        return EventStatus::Invalid;
    }

    if (state_.mode == EventMode::ManualReset) {
        state_.signaled = true;
        (void)scheduler_wait::wakeAll(&state_.waiters, WakeReason::Signaled);
        return EventStatus::Ok;
    }

    if (state_.signaled) {
        return EventStatus::Ok;
    }

    // For auto-reset, a successful wake reserves this signal for the selected
    // waiter before the critical section is released.  This prevents a later
    // waiter from consuming the same bit while the awakened thread is still
    // being scheduled.
    state_.signaled = true;
    if (scheduler_wait::wakeOne(&state_.waiters, WakeReason::Signaled)) {
        state_.signaled = false;
    }
    return EventStatus::Ok;
}

EventStatus Event::reset() {
    CriticalScope lock;
    if (!initialized_ || closed_) {
        return EventStatus::Invalid;
    }
    if (state_.destroyed) {
        return EventStatus::Destroyed;
    }
    state_.signaled = false;
    return EventStatus::Ok;
}

EventStatus Event::close() {
    CriticalScope lock;
    if (!initialized_ || closed_) {
        return EventStatus::Invalid;
    }
    if (!scheduler_wait::waitQueueEmpty(&state_.waiters) &&
        !scheduler_wait::schedulerWaitAvailable()) {
        return EventStatus::Invalid;
    }

    state_.destroyed = true;
    closed_ = true;
    (void)scheduler_wait::wakeAll(&state_.waiters, WakeReason::Destroyed);
    return EventStatus::Ok;
}

WaitResult Event::wait() {
    return wait(WaitTimeout::infinite());
}

WaitResult Event::wait(const WaitTimeout& timeout) {
    if (!timeout.valid) {
        return WaitResult::Invalid;
    }

    CriticalScope lock;
    if (!initialized_ || closed_) {
        return WaitResult::Invalid;
    }
    if (state_.destroyed) {
        return WaitResult::Destroyed;
    }

    if (state_.signaled) {
        if (state_.mode == EventMode::AutoReset) {
            state_.signaled = false;
        }
        return WaitResult::Signaled;
    }
    // Infinite waits intentionally carry zero nanoseconds.  Only a finite
    // zero-duration wait is a poll.
    if (!timeout.infinite_wait && timeout.nanoseconds == 0) {
        return WaitResult::TimedOut;
    }
    if (!scheduler_wait::schedulerWaitAvailable()) {
        return WaitResult::Invalid;
    }

    scheduler_wait::WaitNode* node = nullptr;
    if (!scheduler_wait::prepareWait(&state_.waiters,
                                     makeWaitDuration(timeout),
                                     &node)) {
        return WaitResult::Invalid;
    }

    // The node is fully published in the object queue and (for finite waits)
    // in the timer queue before this lock is released.  A signal or timeout
    // that wins here completes the node, so parkWait returns immediately and
    // cannot lose the wakeup.
    lock.release();
    return mapWakeReason(scheduler_wait::parkWait(node));
}

} // namespace runtime
} // namespace gxos

#endif // GXOS_BARE_METAL
