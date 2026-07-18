#include "guidexos_mutex.h"

#if defined(GXOS_BARE_METAL)

namespace gxos {
namespace runtime {

namespace {
    MutexPlatformHooks g_hooks = { nullptr, nullptr };

    bool validMode(MutexMode mode) {
        return mode == MutexMode::NonRecursive || mode == MutexMode::Recursive;
    }

    MutexResult wakeReasonToResult(scheduler_wait::WakeReason reason) {
        switch (reason) {
        case scheduler_wait::WakeReason::Signaled:
            return MutexResult::Acquired;
        case scheduler_wait::WakeReason::Destroyed:
            return MutexResult::Destroyed;
        case scheduler_wait::WakeReason::None:
            return MutexResult::Interrupted;
        default:
            return MutexResult::Interrupted;
        }
    }
}

void installMutexPlatformHooks(const MutexPlatformHooks* hooks) {
    g_hooks = hooks == nullptr ? MutexPlatformHooks{ nullptr, nullptr } : *hooks;
}

MutexOwnerIdentity Mutex::currentOwner() {
    return g_hooks.currentOwner == nullptr
        ? MutexOwnerIdentity{ 0, 0 }
        : g_hooks.currentOwner(g_hooks.context);
}

bool Mutex::ownerMatches(const MutexOwnerIdentity& left,
                         const MutexOwnerIdentity& right) {
    return left.isValid() && right.isValid() && left == right;
}

Mutex::Mutex() noexcept
    : state_{}, initialized_(false), closed_(false) {
}

Mutex::Mutex(MutexMode mode) noexcept
    : state_{}, initialized_(false), closed_(false) {
    (void)initialize(mode);
}

Mutex::~Mutex() noexcept {
    (void)destroy();
}

bool Mutex::initialize(MutexMode mode) {
    if (initialized_ || closed_ || !validMode(mode)) {
        return false;
    }
    state_ = State{};
    state_.mode = mode;
    scheduler_wait::initializeWaitQueue(&state_.waiters);
    initialized_ = true;
    return true;
}

bool Mutex::isInitialized() const noexcept {
    return initialized_ && !closed_ && !state_.destroyed;
}

MutexResult Mutex::lock() {
    if (!initialized_) {
        return MutexResult::Invalid;
    }
    if (!scheduler_wait::schedulerWaitAvailable()) {
        return MutexResult::Invalid;
    }

    const MutexOwnerIdentity owner = currentOwner();
    if (!owner.isValid()) {
        return MutexResult::Invalid;
    }

    for (;;) {
        void* token = scheduler_wait::enterCritical();
        if (token == nullptr) {
            return MutexResult::Invalid;
        }

        if (state_.destroyed) {
            scheduler_wait::leaveCritical(token);
            return MutexResult::Destroyed;
        }
        if (state_.locked && ownerMatches(state_.owner, owner)) {
            if (state_.mode == MutexMode::NonRecursive) {
                scheduler_wait::leaveCritical(token);
                return MutexResult::AlreadyOwned;
            }
            if (state_.recursion >= kMutexMaximumRecursion) {
                scheduler_wait::leaveCritical(token);
                return MutexResult::RecursionLimit;
            }
            ++state_.recursion;
            scheduler_wait::leaveCritical(token);
            return MutexResult::Acquired;
        }
        if (!state_.locked && scheduler_wait::waitQueueEmpty(&state_.waiters)) {
            state_.locked = true;
            state_.owner = owner;
            state_.recursion = 1;
            scheduler_wait::leaveCritical(token);
            return MutexResult::Acquired;
        }

        scheduler_wait::WaitNode* node = nullptr;
        const scheduler_wait::WaitDuration infinite = { true, 0 };
        if (!scheduler_wait::prepareWait(&state_.waiters, infinite, &node) ||
            node == nullptr) {
            scheduler_wait::leaveCritical(token);
            return MutexResult::Invalid;
        }
        node->handoff_owner = owner.value;
        node->handoff_generation = owner.generation;
        scheduler_wait::leaveCritical(token);

        const scheduler_wait::WakeReason reason = scheduler_wait::parkWait(node);
        if (reason != scheduler_wait::WakeReason::Signaled) {
            return wakeReasonToResult(reason);
        }

        token = scheduler_wait::enterCritical();
        if (token == nullptr) {
            return MutexResult::Invalid;
        }
        const bool handedOff = state_.locked && ownerMatches(state_.owner, owner);
        const bool destroyed = state_.destroyed;
        scheduler_wait::leaveCritical(token);
        if (handedOff) {
            return MutexResult::Acquired;
        }
        return destroyed ? MutexResult::Destroyed : MutexResult::Interrupted;
    }
}

MutexResult Mutex::tryLock() {
    if (!initialized_) {
        return MutexResult::Invalid;
    }
    if (!scheduler_wait::schedulerWaitCriticalAvailable()) {
        return MutexResult::Invalid;
    }

    const MutexOwnerIdentity owner = currentOwner();
    if (!owner.isValid()) {
        return MutexResult::Invalid;
    }
    void* token = scheduler_wait::enterCritical();
    if (token == nullptr) {
        return MutexResult::Invalid;
    }
    if (state_.destroyed) {
        scheduler_wait::leaveCritical(token);
        return MutexResult::Destroyed;
    }
    if (state_.locked && ownerMatches(state_.owner, owner)) {
        if (state_.mode == MutexMode::NonRecursive) {
            scheduler_wait::leaveCritical(token);
            return MutexResult::AlreadyOwned;
        }
        if (state_.recursion >= kMutexMaximumRecursion) {
            scheduler_wait::leaveCritical(token);
            return MutexResult::RecursionLimit;
        }
        ++state_.recursion;
        scheduler_wait::leaveCritical(token);
        return MutexResult::Acquired;
    }
    if (state_.locked || !scheduler_wait::waitQueueEmpty(&state_.waiters)) {
        scheduler_wait::leaveCritical(token);
        return MutexResult::WouldBlock;
    }
    state_.locked = true;
    state_.owner = owner;
    state_.recursion = 1;
    scheduler_wait::leaveCritical(token);
    return MutexResult::Acquired;
}

MutexResult Mutex::unlock() {
    if (!initialized_) {
        return MutexResult::Invalid;
    }
    if (!scheduler_wait::schedulerWaitCriticalAvailable()) {
        return MutexResult::Invalid;
    }

    const MutexOwnerIdentity owner = currentOwner();
    if (!owner.isValid()) {
        return MutexResult::Invalid;
    }
    void* token = scheduler_wait::enterCritical();
    if (token == nullptr) {
        return MutexResult::Invalid;
    }
    if (state_.destroyed) {
        scheduler_wait::leaveCritical(token);
        return MutexResult::Destroyed;
    }
    if (!state_.locked || !ownerMatches(state_.owner, owner)) {
        scheduler_wait::leaveCritical(token);
        return MutexResult::NotOwner;
    }
    if (state_.recursion > 1) {
        --state_.recursion;
        scheduler_wait::leaveCritical(token);
        return MutexResult::Released;
    }

    scheduler_wait::WaitNode* next = state_.waiters.head;
    if (next != nullptr) {
        const MutexOwnerIdentity nextOwner = {
            next->handoff_owner, next->handoff_generation
        };
        if (!nextOwner.isValid()) {
            scheduler_wait::leaveCritical(token);
            return MutexResult::Invalid;
        }
        // Direct handoff is published before wakeOne removes the waiter from
        // the queue.  A new contender therefore cannot steal this ownership.
        state_.locked = true;
        state_.owner = nextOwner;
        state_.recursion = 1;
        if (!scheduler_wait::wakeOne(&state_.waiters,
                                     scheduler_wait::WakeReason::Signaled)) {
            scheduler_wait::leaveCritical(token);
            return MutexResult::Invalid;
        }
    }
    else {
        state_.locked = false;
        state_.owner = MutexOwnerIdentity{ 0, 0 };
        state_.recursion = 0;
    }
    scheduler_wait::leaveCritical(token);
    return MutexResult::Released;
}

MutexStatus Mutex::destroy() {
    if (!initialized_) {
        return MutexStatus::Invalid;
    }
    if (!scheduler_wait::schedulerWaitCriticalAvailable()) {
        return MutexStatus::Invalid;
    }
    void* token = scheduler_wait::enterCritical();
    if (token == nullptr) {
        return MutexStatus::Invalid;
    }
    if (state_.destroyed) {
        scheduler_wait::leaveCritical(token);
        return MutexStatus::Destroyed;
    }
    if (state_.locked || !scheduler_wait::waitQueueEmpty(&state_.waiters)) {
        scheduler_wait::leaveCritical(token);
        return MutexStatus::Busy;
    }
    state_.destroyed = true;
    closed_ = true;
    scheduler_wait::leaveCritical(token);
    return MutexStatus::Ok;
}

MutexStatus Mutex::notifyOwnerExit() {
    if (!initialized_) {
        return MutexStatus::Invalid;
    }
    if (!scheduler_wait::schedulerWaitCriticalAvailable()) {
        return MutexStatus::Invalid;
    }
    const MutexOwnerIdentity owner = currentOwner();
    if (!owner.isValid()) {
        return MutexStatus::Invalid;
    }
    void* token = scheduler_wait::enterCritical();
    if (token == nullptr) {
        return MutexStatus::Invalid;
    }
    if (state_.destroyed) {
        scheduler_wait::leaveCritical(token);
        return MutexStatus::Destroyed;
    }
    if (state_.locked && ownerMatches(state_.owner, owner)) {
        state_.owner_exit_violation = true;
        scheduler_wait::leaveCritical(token);
        return MutexStatus::OwnerExitViolation;
    }
    scheduler_wait::leaveCritical(token);
    return MutexStatus::Ok;
}

} // namespace runtime
} // namespace gxos

#endif // GXOS_BARE_METAL
