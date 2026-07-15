#pragma once

// Runtime-neutral guideXOS event abstraction.
//
// The hosted implementation is backed by the C++ standard library.  The
// GXOS_BARE_METAL build uses the same state and result model but delegates
// blocking and wakeup to an explicitly installed scheduler hook set.

#if defined(GXOS_BARE_METAL)
#include <stdint.h>
using gxos_event_uint64 = uint64_t;
using gxos_event_int64 = int64_t;
#else
#include <cstdint>
#include <memory>
#include <mutex>
#include <utility>
using gxos_event_uint64 = std::uint64_t;
using gxos_event_int64 = std::int64_t;
#endif

namespace gxos {
namespace runtime {

enum class EventMode {
    ManualReset,
    AutoReset
};

enum class WaitResult {
    Signaled,
    TimedOut,
    Invalid,
    Interrupted,
    Destroyed
};

enum class EventStatus {
    Ok,
    Invalid,
    Destroyed
};

// Infinite is represented explicitly.  A finite duration is valid only when
// its nanosecond value can be represented by the implementation's signed
// monotonic-clock duration.  Invalid values are rejected by wait().
struct WaitTimeout {
    bool valid;
    bool infinite_wait;
    gxos_event_uint64 nanoseconds;

    static constexpr WaitTimeout infinite() {
        return { true, true, 0 };
    }

    static constexpr WaitTimeout zero() {
        return { true, false, 0 };
    }

    static constexpr WaitTimeout invalid() {
        return { false, false, 0 };
    }

    static WaitTimeout finiteNanoseconds(gxos_event_uint64 value);
    static WaitTimeout finiteMilliseconds(gxos_event_uint64 value);
    static WaitTimeout signedMilliseconds(gxos_event_int64 value);
};

class Event final {
public:
    Event() noexcept;
    Event(EventMode mode, bool initiallySignaled) noexcept;
    ~Event() noexcept;

    Event(const Event&) = delete;
    Event& operator=(const Event&) = delete;
    Event(Event&&) = delete;
    Event& operator=(Event&&) = delete;

    // Initialization is one-shot.  A closed Event cannot be reinitialized;
    // construct a new object instead.
    bool initialize(EventMode mode, bool initiallySignaled);
    bool isInitialized() const noexcept;

    EventStatus signal();
    EventStatus reset();
    EventStatus close();

    WaitResult wait();
    WaitResult wait(const WaitTimeout& timeout);

private:
#if defined(GXOS_BARE_METAL)
    struct State {
        EventMode mode;
        bool signaled;
        bool destroyed;
        gxos_event_uint64 active_waiters;
    };

    State state_;
    bool initialized_;
    bool closed_;
#else
    struct State;

    std::shared_ptr<State> state_;
    mutable std::mutex lifecycle_mutex_;
    bool initialized_;
    bool closed_;

    std::shared_ptr<State> stateSnapshot() const;
#endif
};

} // namespace runtime
} // namespace gxos
