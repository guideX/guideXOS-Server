#include "include/kernel/application_event_service.h"

#include "include/kernel/arch_interface.h"

namespace kernel {
namespace application {

ApplicationEventService::ApplicationEventService()
    : m_runtimeId(0), m_generation(0), m_valid(false), m_lifecycleWindows{},
      m_lifecycleWindowCount(0),
      m_events{}, m_head(0), m_tail(0), m_count(0), m_highWater(0), m_dropped(0),
      m_coalesced(0), m_waits(0), m_wakes(0), m_spuriousWakes(0), m_waiter(nullptr) {}

uint32_t ApplicationEventService::next_index(uint32_t index)
{
    return index + 1u == kCapacity ? 0u : index + 1u;
}

void ApplicationEventService::copy_event(gx_event* destination, const gx_event& source)
{
    if (!destination) return;
    destination->size = source.size;
    destination->type = source.type;
    destination->window = source.window;
    destination->param1 = source.param1;
    destination->param2 = source.param2;
    destination->param3 = source.param3;
    destination->param4 = source.param4;
}

void ApplicationEventService::initialize(uint8_t runtimeId, uint16_t generation)
{
    m_runtimeId = runtimeId;
    m_generation = generation;
    m_valid = runtimeId != 0 && generation != 0;
    m_lifecycleWindowCount = 0;
    for (uint32_t i = 0; i < kLifecycleWindowCapacity; ++i) m_lifecycleWindows[i] = 0;
    m_head = m_tail = m_count = m_highWater = 0;
    m_dropped = m_coalesced = m_waits = m_wakes = m_spuriousWakes = 0;
    m_waiter = nullptr;
}

void ApplicationEventService::invalidate()
{
    const arch::interrupt_state_t state = arch::irq_save();
    m_valid = false;
    m_count = 0;
    if (m_waiter) {
        scheduler::wake_task_locked(m_waiter);
        m_waiter = nullptr;
        ++m_wakes;
    }
    arch::irq_restore(state);
}

bool ApplicationEventService::coalesce_locked(const gx_event& event)
{
    if (m_count == 0 || event.type != GX_EVENT_MOUSE ||
        GX_MOUSE_ACTION(event.param3) != GX_MOUSE_ACTION_MOVE) return false;
    const uint32_t previous = m_tail == 0 ? kCapacity - 1u : m_tail - 1u;
    gx_event& last = m_events[previous];
    if (last.type != GX_EVENT_MOUSE || GX_MOUSE_ACTION(last.param3) != GX_MOUSE_ACTION_MOVE ||
        last.window != event.window) return false;
    last.param1 = event.param1;
    last.param2 = event.param2;
    last.param4 = event.param4;
    ++m_coalesced;
    return true;
}

EventEnqueueResult ApplicationEventService::enqueue(const gx_event& event, bool lifecycle)
{
    const arch::interrupt_state_t state = arch::irq_save();
    if (!m_valid) {
        arch::irq_restore(state);
        return EventEnqueueResult::Invalidated;
    }
    if (lifecycle && event.type == GX_EVENT_WINDOW_CLOSE) {
        for (uint32_t i = 0; i < m_lifecycleWindowCount; ++i) {
            if (m_lifecycleWindows[i] == event.window) {
                arch::irq_restore(state);
                return EventEnqueueResult::DuplicateLifecycle;
            }
        }
        if (m_lifecycleWindowCount < kLifecycleWindowCapacity)
            m_lifecycleWindows[m_lifecycleWindowCount++] = event.window;
    }
    if (coalesce_locked(event)) {
        arch::irq_restore(state);
        return EventEnqueueResult::Accepted;
    }
    if (m_count == kCapacity) {
        if (!lifecycle) {
            ++m_dropped;
            arch::irq_restore(state);
            return EventEnqueueResult::Dropped;
        }
        /* Lifecycle delivery is guaranteed by evicting the oldest ordinary
         * event.  The queue remains bounded and the close path is ordered
         * after the retained history. */
        m_tail = next_index(m_tail);
        --m_count;
        ++m_dropped;
    }
    gx_event& destination = m_events[m_head];
    copy_event(&destination, event);
    m_head = next_index(m_head);
    ++m_count;
    if (m_count > m_highWater) m_highWater = m_count;
    if (m_waiter) {
        scheduler::wake_task_locked(m_waiter);
        m_waiter = nullptr;
        ++m_wakes;
    }
    arch::irq_restore(state);
    return EventEnqueueResult::Accepted;
}

bool ApplicationEventService::pop_locked(gx_event* event)
{
    if (!event || m_count == 0) return false;
    copy_event(event, m_events[m_tail]);
    m_tail = next_index(m_tail);
    --m_count;
    return true;
}

bool ApplicationEventService::try_pop(gx_event* event)
{
    const arch::interrupt_state_t state = arch::irq_save();
    const bool result = pop_locked(event);
    arch::irq_restore(state);
    return result;
}

gx_result ApplicationEventService::wait(gx_event* event)
{
    if (!event) return GX_ERROR_INVALID_ARGUMENT;
    for (;;) {
        const arch::interrupt_state_t state = arch::irq_save();
        if (!m_valid) {
            arch::irq_restore(state);
            return GX_ERROR_FAILED;
        }
        if (pop_locked(event)) {
            arch::irq_restore(state);
            return GX_OK;
        }
        scheduler::Task* current = scheduler::current_task();
        if (!current || m_waiter) {
            ++m_spuriousWakes;
            arch::irq_restore(state);
            return GX_ERROR_BUSY;
        }
        /* Queue check, waiter registration, and TASK_BLOCKED transition are
         * all inside one IRQ-masked critical section.  enqueue() uses the
         * same section, preventing the lost-wakeup boundary race. */
        m_waiter = current;
        ++m_waits;
        if (!scheduler::block_current_locked()) {
            m_waiter = nullptr;
            ++m_spuriousWakes;
            arch::irq_restore(state);
            return GX_ERROR_BUSY;
        }
        arch::irq_restore(state);
        /* We resume here only after wake_task_locked().  Re-check the queue
         * because teardown may have invalidated the service. */
    }
}

} // namespace application
} // namespace kernel
