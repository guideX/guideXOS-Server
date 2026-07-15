#pragma once

// Runtime-neutral scheduler wait contract.
//
// The wait queue owns no storage.  A synchronization object embeds one
// WaitQueue, while the scheduler embeds one reusable WaitNode in each thread
// control block.  The platform hooks provide the current thread, the actual
// park/context-switch operation, and runnable-queue insertion.

#if defined(GXOS_BARE_METAL)
#include <stdint.h>
using gxos_wait_uint8 = uint8_t;
using gxos_wait_uint32 = uint32_t;
using gxos_wait_uint64 = uint64_t;
#else
#include <cstdint>
using gxos_wait_uint8 = std::uint8_t;
using gxos_wait_uint32 = std::uint32_t;
using gxos_wait_uint64 = std::uint64_t;
#endif

namespace gxos {
namespace runtime {
namespace scheduler_wait {

enum class WakeReason : gxos_wait_uint8 {
    None = 0,
    Signaled,
    TimedOut,
    Cancelled,
    Destroyed,
    Interrupted
};

enum class WaitNodeState : gxos_wait_uint8 {
    Idle = 0,
    Waiting,
    Completed
};

struct WaitNode;

struct WaitQueue {
    WaitNode* head;
    WaitNode* tail;
    gxos_wait_uint32 count;
};

struct WaitNode {
    void* owner_thread;
    WaitQueue* owner_queue;
    WaitNode* queue_previous;
    WaitNode* queue_next;
    WaitNode* timer_previous;
    WaitNode* timer_next;
    gxos_wait_uint64 deadline;
    gxos_wait_uint32 generation;
    WakeReason reason;
    WaitNodeState state;
    bool timed;
    bool queue_linked;
    bool timer_linked;
};

struct WaitDuration {
    bool infinite;
    gxos_wait_uint64 nanoseconds;
};

using WaitCriticalEnter = void* (*)(void* context);
using WaitCriticalLeave = void (*)(void* context, void* token);
using WaitCurrentNode = WaitNode* (*)(void* context);
using WaitNowTicks = gxos_wait_uint64 (*)(void* context);
using WaitDurationToTicks = gxos_wait_uint64 (*)(void* context,
                                                 gxos_wait_uint64 nanoseconds,
                                                 bool* valid);
using WaitPark = WakeReason (*)(void* context, WaitNode* node);
using WaitMakeRunnable = void (*)(void* context, WaitNode* node);

struct SchedulerWaitHooks {
    void* context;
    WaitCriticalEnter enterCritical;
    WaitCriticalLeave leaveCritical;
    WaitCurrentNode currentNode;
    WaitNowTicks nowTicks;
    WaitDurationToTicks durationToTicks;
    WaitPark park;
    WaitMakeRunnable makeRunnable;
};

void initializeWaitQueue(WaitQueue* queue);
void installSchedulerWaitHooks(const SchedulerWaitHooks* hooks);
bool schedulerWaitAvailable();
bool schedulerWaitCriticalAvailable();

void* enterCritical();
void leaveCritical(void* token);

// The caller must hold the scheduler/event critical section while preparing
// or waking a queue.  prepareWait publishes the node before the caller
// releases that section, which closes the signal-versus-park lost-wakeup
// window.
bool prepareWait(WaitQueue* queue, const WaitDuration& duration, WaitNode** nodeOut);
WakeReason parkWait(WaitNode* node);

bool wakeOne(WaitQueue* queue, WakeReason reason);
gxos_wait_uint32 wakeAll(WaitQueue* queue, WakeReason reason);
bool cancelWait(WaitNode* node, WakeReason reason);

// Thread teardown must unlink without reinserting the terminated thread into
// the runnable queue.  It is safe to call for an already completed node.
void abandonWait(WaitNode* node);

// Called by the monotonic timer interrupt.  The budget bounds work per tick;
// the timer list is deadline ordered, so unrelated non-expired timers are not
// inspected after the first future deadline.
gxos_wait_uint32 processExpired(gxos_wait_uint64 nowTicks,
                                gxos_wait_uint32 budget);

bool waitQueueEmpty(const WaitQueue* queue);
gxos_wait_uint32 waitQueueCount(const WaitQueue* queue);
bool waitNodeIsWaiting(const WaitNode* node);
bool waitNodeIsCompleted(const WaitNode* node);
bool waitNodeTimerRegistered(const WaitNode* node);

} // namespace scheduler_wait
} // namespace runtime
} // namespace gxos
