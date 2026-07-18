#pragma once

// Runtime-neutral mutex/critical-section contract.
//
// The public surface contains only fixed-width values and opaque state.  The
// hosted implementation uses a private condition-variable state; the
// GXOS_BARE_METAL implementation uses the existing scheduler wait queue and
// its single-CPU critical-section hooks.

#if defined(GXOS_BARE_METAL)
#include "guidexos_scheduler_wait.h"
#include <stdint.h>
using gxos_mutex_uint32 = uint32_t;
using gxos_mutex_uint64 = uint64_t;
#else
#include <cstdint>
#include <memory>
#include <mutex>
using gxos_mutex_uint32 = std::uint32_t;
using gxos_mutex_uint64 = std::uint64_t;
#endif

namespace gxos {
namespace runtime {

enum class MutexMode : gxos_mutex_uint32 {
    NonRecursive = 0,
    Recursive = 1
};

struct MutexOwnerIdentity {
    gxos_mutex_uint64 value;
    gxos_mutex_uint32 generation;

    constexpr bool isValid() const {
        return value != 0 && generation != 0;
    }
};

constexpr gxos_mutex_uint32 kMutexMaximumRecursion = 1024;

enum class MutexResult : gxos_mutex_uint32 {
    Acquired,
    Released,
    WouldBlock,
    Invalid,
    Destroyed,
    NotOwner,
    AlreadyOwned,
    RecursionLimit,
    Interrupted
};

enum class MutexStatus : gxos_mutex_uint32 {
    Ok,
    Invalid,
    Destroyed,
    Busy,
    OwnerExitViolation
};

inline bool operator==(const MutexOwnerIdentity& left, const MutexOwnerIdentity& right) {
    return left.value == right.value && left.generation == right.generation;
}

inline bool operator!=(const MutexOwnerIdentity& left, const MutexOwnerIdentity& right) {
    return !(left == right);
}

class Mutex final {
public:
    Mutex() noexcept;
    explicit Mutex(MutexMode mode) noexcept;
    ~Mutex() noexcept;

    Mutex(const Mutex&) = delete;
    Mutex& operator=(const Mutex&) = delete;
    Mutex(Mutex&&) = delete;
    Mutex& operator=(Mutex&&) = delete;

    // Initialization is one-shot.  A destroyed Mutex cannot be reinitialized;
    // construct a new object instead.
    bool initialize(MutexMode mode);
    bool isInitialized() const noexcept;

    // lock() has no timeout.  A contended call parks through the scheduler
    // wait contract on bare metal and a private condition variable when
    // hosted.
    MutexResult lock();
    MutexResult tryLock();
    MutexResult unlock();

    // Destruction is deliberately quiescent: an owned or contended Mutex is
    // reported Busy and remains usable.  No waiter is woken by destruction.
    MutexStatus destroy();

    // A thread that is about to exit while holding a known Mutex may report
    // the ownership violation.  This records a diagnostic and never releases
    // the lock.  There is intentionally no abandoned-owner recovery policy.
    MutexStatus notifyOwnerExit();

private:
#if defined(GXOS_BARE_METAL)
    struct State {
        MutexMode mode;
        bool locked;
        bool destroyed;
        bool owner_exit_violation;
        MutexOwnerIdentity owner;
        gxos_mutex_uint32 recursion;
        scheduler_wait::WaitQueue waiters;
    };

    State state_;
    bool initialized_;
    bool closed_;

    static bool ownerMatches(const MutexOwnerIdentity& left,
                             const MutexOwnerIdentity& right);
    static MutexOwnerIdentity currentOwner();
#else
    struct State;

    std::shared_ptr<State> state_;
    mutable std::mutex lifecycle_mutex_;
    bool initialized_;
    bool closed_;

    std::shared_ptr<State> stateSnapshot() const;
#endif
};

#if defined(GXOS_BARE_METAL)

struct MutexPlatformHooks {
    void* context;
    MutexOwnerIdentity (*currentOwner)(void* context);
};

void installMutexPlatformHooks(const MutexPlatformHooks* hooks);

#endif

} // namespace runtime
} // namespace gxos
