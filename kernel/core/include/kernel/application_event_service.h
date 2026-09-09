#pragma once

#include <stdint.h>

#include "common_scheduler.h"
#include "sdk/include/guidexos/abi.h"

namespace kernel {
namespace application {

enum class EventEnqueueResult : uint8_t {
    Accepted = 0,
    Dropped = 1,
    Invalidated = 2,
    DuplicateLifecycle = 3
};

/* Fixed-storage, per-runtime event service.  It is safe to call enqueue from
 * the input path because it never allocates and wakes a waiter only while the
 * scheduler interrupt lock is held. */
class ApplicationEventService {
public:
    static const uint32_t kCapacity = 32;
    static const uint32_t kLifecycleWindowCapacity = 8;

    ApplicationEventService();

    void initialize(uint8_t runtimeId, uint16_t generation);
    void invalidate();
    bool valid() const { return m_valid; }

    EventEnqueueResult enqueue(const gx_event& event, bool lifecycle);
    bool try_pop(gx_event* event);
    gx_result wait(gx_event* event);

    uint32_t size() const { return m_count; }
    uint32_t high_water() const { return m_highWater; }
    uint64_t dropped() const { return m_dropped; }
    uint64_t coalesced() const { return m_coalesced; }
    uint64_t waits() const { return m_waits; }
    uint64_t wakes() const { return m_wakes; }
    uint64_t spurious_wakes() const { return m_spuriousWakes; }

private:
    static uint32_t next_index(uint32_t index);
    static void copy_event(gx_event* destination, const gx_event& source);
    bool pop_locked(gx_event* event);
    bool coalesce_locked(const gx_event& event);

    uint8_t m_runtimeId;
    uint16_t m_generation;
    bool m_valid;
    gx_handle m_lifecycleWindows[kLifecycleWindowCapacity];
    uint32_t m_lifecycleWindowCount;
    gx_event m_events[kCapacity];
    uint32_t m_head;
    uint32_t m_tail;
    uint32_t m_count;
    uint32_t m_highWater;
    uint64_t m_dropped;
    uint64_t m_coalesced;
    uint64_t m_waits;
    uint64_t m_wakes;
    uint64_t m_spuriousWakes;
    scheduler::Task* m_waiter;
};

} // namespace application
} // namespace kernel
