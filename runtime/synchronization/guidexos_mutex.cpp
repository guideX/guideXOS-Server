#include "guidexos_mutex.h"

#if !defined(GXOS_BARE_METAL)

#include <atomic>
#include <condition_variable>
#include <limits>
#include <mutex>

namespace gxos {
namespace runtime {

namespace {
    std::atomic<gxos_mutex_uint64> g_nextOwner{1};
    thread_local MutexOwnerIdentity g_currentOwner = { 0, 0 };

    MutexOwnerIdentity currentOwner() {
        if (!g_currentOwner.isValid()) {
            gxos_mutex_uint64 value = g_nextOwner.fetch_add(1, std::memory_order_relaxed);
            if (value == 0) {
                value = g_nextOwner.fetch_add(1, std::memory_order_relaxed);
            }
            g_currentOwner = { value, 1 };
        }
        return g_currentOwner;
    }
}

struct Mutex::State {
    explicit State(MutexMode mutexMode)
        : mode(mutexMode), locked(false), destroyed(false),
          owner_exit_violation(false), owner{0, 0}, recursion(0),
          next_ticket(0), serving_ticket(0), waiter_count(0) {
    }

    MutexMode mode;
    bool locked;
    bool destroyed;
    bool owner_exit_violation;
    MutexOwnerIdentity owner;
    gxos_mutex_uint32 recursion;
    gxos_mutex_uint64 next_ticket;
    gxos_mutex_uint64 serving_ticket;
    gxos_mutex_uint32 waiter_count;
    std::mutex mutex;
    std::condition_variable condition;
};

Mutex::Mutex() noexcept
    : initialized_(false), closed_(false) {
}

Mutex::Mutex(MutexMode mode) noexcept
    : initialized_(false), closed_(false) {
    (void)initialize(mode);
}

Mutex::~Mutex() noexcept {
    (void)destroy();
}

bool Mutex::initialize(MutexMode mode) {
    std::shared_ptr<State> newState;
    try {
        newState = std::make_shared<State>(mode);
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

bool Mutex::isInitialized() const noexcept {
    std::lock_guard<std::mutex> lifeLock(lifecycle_mutex_);
    return initialized_ && !closed_ && static_cast<bool>(state_);
}

std::shared_ptr<Mutex::State> Mutex::stateSnapshot() const {
    std::lock_guard<std::mutex> lifeLock(lifecycle_mutex_);
    return state_;
}

MutexResult Mutex::lock() {
    std::shared_ptr<State> state = stateSnapshot();
    if (!state) {
        return MutexResult::Invalid;
    }

    const MutexOwnerIdentity owner = currentOwner();
    std::unique_lock<std::mutex> stateLock(state->mutex);
    if (state->destroyed) {
        return MutexResult::Destroyed;
    }
    if (state->locked && state->owner == owner) {
        if (state->mode == MutexMode::NonRecursive) {
            return MutexResult::AlreadyOwned;
        }
        if (state->recursion >= kMutexMaximumRecursion) {
            return MutexResult::RecursionLimit;
        }
        ++state->recursion;
        return MutexResult::Acquired;
    }

    if (!state->locked && state->waiter_count == 0) {
        state->locked = true;
        state->owner = owner;
        state->recursion = 1;
        return MutexResult::Acquired;
    }

    if (state->next_ticket == std::numeric_limits<gxos_mutex_uint64>::max() ||
        state->waiter_count == std::numeric_limits<gxos_mutex_uint32>::max()) {
        return MutexResult::Invalid;
    }
    const gxos_mutex_uint64 ticket = state->next_ticket++;
    ++state->waiter_count;
    while (!state->destroyed && (state->locked || ticket != state->serving_ticket)) {
        state->condition.wait(stateLock);
    }

    if (state->destroyed) {
        --state->waiter_count;
        return MutexResult::Destroyed;
    }

    --state->waiter_count;
    state->locked = true;
    state->owner = owner;
    state->recursion = 1;
    ++state->serving_ticket;
    return MutexResult::Acquired;
}

MutexResult Mutex::tryLock() {
    std::shared_ptr<State> state = stateSnapshot();
    if (!state) {
        return MutexResult::Invalid;
    }

    const MutexOwnerIdentity owner = currentOwner();
    std::lock_guard<std::mutex> stateLock(state->mutex);
    if (state->destroyed) {
        return MutexResult::Destroyed;
    }
    if (state->locked && state->owner == owner) {
        if (state->mode == MutexMode::NonRecursive) {
            return MutexResult::AlreadyOwned;
        }
        if (state->recursion >= kMutexMaximumRecursion) {
            return MutexResult::RecursionLimit;
        }
        ++state->recursion;
        return MutexResult::Acquired;
    }
    if (state->locked || state->waiter_count != 0) {
        return MutexResult::WouldBlock;
    }
    state->locked = true;
    state->owner = owner;
    state->recursion = 1;
    return MutexResult::Acquired;
}

MutexResult Mutex::unlock() {
    std::shared_ptr<State> state = stateSnapshot();
    if (!state) {
        return MutexResult::Invalid;
    }

    const MutexOwnerIdentity owner = currentOwner();
    bool notify = false;
    {
        std::lock_guard<std::mutex> stateLock(state->mutex);
        if (state->destroyed) {
            return MutexResult::Destroyed;
        }
        if (!state->locked || state->owner != owner) {
            return MutexResult::NotOwner;
        }
        if (state->recursion > 1) {
            --state->recursion;
            return MutexResult::Released;
        }
        state->locked = false;
        state->owner = { 0, 0 };
        state->recursion = 0;
        notify = state->waiter_count != 0;
    }
    if (notify) {
        state->condition.notify_all();
    }
    return MutexResult::Released;
}

MutexStatus Mutex::destroy() {
    std::shared_ptr<State> state;
    {
        std::lock_guard<std::mutex> lifeLock(lifecycle_mutex_);
        if (!initialized_ || !state_) {
            return MutexStatus::Invalid;
        }
        state = state_;
    }

    {
        std::lock_guard<std::mutex> stateLock(state->mutex);
        if (state->destroyed) {
            return MutexStatus::Destroyed;
        }
        if (state->locked || state->waiter_count != 0) {
            return MutexStatus::Busy;
        }
        state->destroyed = true;
    }

    std::lock_guard<std::mutex> lifeLock(lifecycle_mutex_);
    closed_ = true;
    return MutexStatus::Ok;
}

MutexStatus Mutex::notifyOwnerExit() {
    std::shared_ptr<State> state = stateSnapshot();
    if (!state) {
        return MutexStatus::Invalid;
    }
    const MutexOwnerIdentity owner = currentOwner();
    std::lock_guard<std::mutex> stateLock(state->mutex);
    if (state->destroyed) {
        return MutexStatus::Destroyed;
    }
    if (state->locked && state->owner == owner) {
        state->owner_exit_violation = true;
        return MutexStatus::OwnerExitViolation;
    }
    return MutexStatus::Ok;
}

} // namespace runtime
} // namespace gxos

#endif // !GXOS_BARE_METAL
