#include "guidexos_event.h"
#include "guidexos_event_baremetal.h"

#if defined(GXOS_BARE_METAL)

namespace gxos {
namespace runtime {
namespace {
    baremetal::EventSchedulerHooks g_hooks = {};

    bool canBlock(EventMode mode) {
        if (g_hooks.enterCritical == nullptr ||
            g_hooks.leaveCritical == nullptr ||
            g_hooks.block == nullptr) {
            return false;
        }
        return mode == EventMode::ManualReset
            ? g_hooks.wakeAll != nullptr
            : g_hooks.wakeOne != nullptr;
    }

    struct CriticalScope {
        void* token;
        bool active;

        CriticalScope()
            : token(nullptr), active(false) {
            if (g_hooks.enterCritical != nullptr) {
                token = g_hooks.enterCritical(g_hooks.context);
                active = true;
            }
        }

        ~CriticalScope() {
            if (active && g_hooks.leaveCritical != nullptr) {
                g_hooks.leaveCritical(g_hooks.context, token);
            }
        }

        void release() {
            if (active && g_hooks.leaveCritical != nullptr) {
                g_hooks.leaveCritical(g_hooks.context, token);
            }
            active = false;
            token = nullptr;
        }
    };

    [[noreturn]] void bareMetalLifetimeFailure() {
#if defined(__GNUC__) || defined(__clang__)
        __builtin_trap();
#else
        for (;;) {
        }
#endif
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
    : state_{ EventMode::ManualReset, false, false, 0 }, initialized_(false), closed_(false) {
}

Event::Event(EventMode mode, bool initiallySignaled) noexcept
    : state_{ mode, initiallySignaled, false, 0 }, initialized_(true), closed_(false) {
}

Event::~Event() noexcept {
    if (state_.active_waiters != 0) {
        // The scheduler contract needed to wake and unregister active waiters
        // does not exist yet.  Destruction is therefore an asserted contract
        // violation in the current bare-metal build.
        bareMetalLifetimeFailure();
    }
    (void)close();
}

bool Event::initialize(EventMode mode, bool initiallySignaled) {
    CriticalScope lock;
    if (initialized_ || closed_) {
        return false;
    }
    state_ = State{ mode, initiallySignaled, false, 0 };
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
    if (state_.active_waiters != 0 &&
        ((state_.mode == EventMode::ManualReset && g_hooks.wakeAll == nullptr) ||
         (state_.mode == EventMode::AutoReset && g_hooks.wakeOne == nullptr))) {
        return EventStatus::Invalid;
    }
    if (state_.signaled) {
        return EventStatus::Ok;
    }
    state_.signaled = true;
    if (state_.mode == EventMode::ManualReset) {
        if (g_hooks.wakeAll != nullptr) {
            g_hooks.wakeAll(g_hooks.context, this);
        }
    }
    else if (g_hooks.wakeOne != nullptr) {
        g_hooks.wakeOne(g_hooks.context, this);
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
    if (state_.active_waiters != 0) {
        // Do not mark the object destroyed while a waiter may still retain
        // this object's address.  The caller must first establish quiescence.
        return EventStatus::Invalid;
    }
    state_.destroyed = true;
    closed_ = true;
    if (g_hooks.wakeAll != nullptr) {
        g_hooks.wakeAll(g_hooks.context, this);
    }
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
    const auto consumeIfSignaled = [&]() -> WaitResult {
        if (state_.destroyed) {
            return WaitResult::Destroyed;
        }
        if (!state_.signaled) {
            return WaitResult::TimedOut;
        }
        if (state_.mode == EventMode::AutoReset) {
            state_.signaled = false;
        }
        return WaitResult::Signaled;
    };

    WaitResult immediate = consumeIfSignaled();
    if (immediate == WaitResult::Signaled || timeout.nanoseconds == 0) {
        return immediate == WaitResult::Signaled ? immediate : WaitResult::TimedOut;
    }
    if (!canBlock(state_.mode)) {
        // No scheduler wait queue/timer is currently available.  Returning
        // Invalid is intentional; this path never polls or busy-spins.
        return WaitResult::Invalid;
    }

    state_.active_waiters += 1;
    lock.release();
    WaitResult result = g_hooks.block(g_hooks.context, this, timeout);
    CriticalScope afterWait;
    state_.active_waiters -= 1;

    if (state_.destroyed) {
        return WaitResult::Destroyed;
    }
    if (state_.signaled) {
        return consumeIfSignaled();
    }
    return result;
}

namespace baremetal {

void installEventSchedulerHooks(const EventSchedulerHooks* hooks) {
    g_hooks = hooks == nullptr ? EventSchedulerHooks{} : *hooks;
}

bool eventSchedulerHooksAvailable() {
    return canBlock(EventMode::ManualReset) && canBlock(EventMode::AutoReset);
}

} // namespace baremetal
} // namespace runtime
} // namespace gxos

#endif // GXOS_BARE_METAL
