#include "include/kernel/application_runtime.h"

namespace kernel {
namespace application {

namespace {
static const ApplicationQuotas kZeroQuotas = { 0, 0, 0, 0, 0, 0 };
}

ApplicationRuntime::ApplicationRuntime()
    : m_packageId{}, m_architecture{}, m_runtimeId(0), m_generation(0),
      m_state(LifecycleState::Cleaned), m_task(nullptr), m_quotas(kZeroQuotas),
      m_resources{}, m_events() {}

bool ApplicationRuntime::copy_text(char* destination, uint32_t capacity, const char* source)
{
    if (!destination || capacity == 0 || !source) return false;
    uint32_t index = 0;
    while (source[index] && index + 1u < capacity) {
        destination[index] = source[index];
        ++index;
    }
    destination[index] = '\0';
    return source[index] == '\0';
}

uint32_t ApplicationRuntime::subtract_saturating(uint32_t value, uint32_t amount)
{
    return amount >= value ? 0u : value - amount;
}

bool ApplicationRuntime::initialize(const char* packageId, const char* architecture,
                                    uint8_t runtimeId, uint16_t generation,
                                    const ApplicationQuotas& quotas)
{
    if (!copy_text(m_packageId, sizeof(m_packageId), packageId) ||
        !copy_text(m_architecture, sizeof(m_architecture), architecture) ||
        runtimeId == 0 || generation == 0 || quotas.windows == 0 || quotas.widgets == 0 ||
        quotas.eventEntries == 0 || quotas.mappedPages == 0 || quotas.stackPages == 0) return false;
    m_runtimeId = runtimeId;
    m_generation = generation;
    m_state = LifecycleState::Starting;
    m_task = nullptr;
    m_quotas = quotas;
    m_resources = ResourceAccounting{};
    m_events.initialize(runtimeId, generation);
    return true;
}

bool ApplicationRuntime::attach_task(scheduler::Task* task)
{
    if (!task || m_state == LifecycleState::Closing || m_state == LifecycleState::Exited ||
        m_state == LifecycleState::Cleaned) return false;
    m_task = task;
    return true;
}

bool ApplicationRuntime::transition(LifecycleState next)
{
    bool allowed = false;
    switch (m_state) {
        case LifecycleState::Starting: allowed = next == LifecycleState::Running || next == LifecycleState::Closing; break;
        case LifecycleState::Running: allowed = next == LifecycleState::Waiting || next == LifecycleState::Closing || next == LifecycleState::Exited; break;
        case LifecycleState::Waiting: allowed = next == LifecycleState::Running || next == LifecycleState::Closing || next == LifecycleState::Exited; break;
        case LifecycleState::Closing: allowed = next == LifecycleState::Exited; break;
        case LifecycleState::Exited: allowed = next == LifecycleState::Cleaned; break;
        case LifecycleState::Cleaned: allowed = false; break;
    }
    if (!allowed) return false;
    m_state = next;
    return true;
}

bool ApplicationRuntime::begin_closing()
{
    if (m_state == LifecycleState::Closing) return true;
    if (m_state != LifecycleState::Starting && m_state != LifecycleState::Running &&
        m_state != LifecycleState::Waiting) return false;
    m_state = LifecycleState::Closing;
    return true;
}

bool ApplicationRuntime::mark_exited()
{
    if (m_state == LifecycleState::Exited) return true;
    if (m_state != LifecycleState::Closing && m_state != LifecycleState::Running &&
        m_state != LifecycleState::Waiting) return false;
    m_state = LifecycleState::Exited;
    return true;
}

bool ApplicationRuntime::cleanup()
{
    if (m_state == LifecycleState::Cleaned) return true;
    if (m_state != LifecycleState::Exited) return false;
    m_events.invalidate();
    m_task = nullptr;
    m_resources = ResourceAccounting{};
    m_state = LifecycleState::Cleaned;
    return true;
}

bool ApplicationRuntime::can_create_objects() const
{
    return m_state == LifecycleState::Starting || m_state == LifecycleState::Running;
}

bool ApplicationRuntime::reserve_windows(uint32_t count)
{
    if (!can_create_objects() || count > m_quotas.windows - (m_resources.windows <= m_quotas.windows ? m_resources.windows : m_quotas.windows)) return false;
    m_resources.windows += count;
    return true;
}

bool ApplicationRuntime::reserve_widgets(uint32_t count)
{
    if (!can_create_objects() || count > m_quotas.widgets - (m_resources.widgets <= m_quotas.widgets ? m_resources.widgets : m_quotas.widgets)) return false;
    m_resources.widgets += count;
    return true;
}

bool ApplicationRuntime::reserve_pages(uint32_t count)
{
    if (!can_create_objects() || count > m_quotas.mappedPages - (m_resources.mappedPages <= m_quotas.mappedPages ? m_resources.mappedPages : m_quotas.mappedPages)) return false;
    m_resources.mappedPages += count;
    return true;
}

bool ApplicationRuntime::reserve_stack_pages(uint32_t count)
{
    if (!can_create_objects() || count > m_quotas.stackPages - (m_resources.stackPages <= m_quotas.stackPages ? m_resources.stackPages : m_quotas.stackPages)) return false;
    m_resources.stackPages += count;
    return true;
}

bool ApplicationRuntime::reserve_strings(uint32_t bytes)
{
    if (!can_create_objects() || bytes > m_quotas.stringBytes - (m_resources.stringBytes <= m_quotas.stringBytes ? m_resources.stringBytes : m_quotas.stringBytes)) return false;
    m_resources.stringBytes += bytes;
    return true;
}

void ApplicationRuntime::release_windows(uint32_t count) { m_resources.windows = subtract_saturating(m_resources.windows, count); }
void ApplicationRuntime::release_widgets(uint32_t count) { m_resources.widgets = subtract_saturating(m_resources.widgets, count); }
void ApplicationRuntime::release_pages(uint32_t count) { m_resources.mappedPages = subtract_saturating(m_resources.mappedPages, count); }
void ApplicationRuntime::release_stack_pages(uint32_t count) { m_resources.stackPages = subtract_saturating(m_resources.stackPages, count); }
void ApplicationRuntime::release_strings(uint32_t bytes) { m_resources.stringBytes = subtract_saturating(m_resources.stringBytes, bytes); }
void ApplicationRuntime::note_event_high_water(uint32_t value) { if (value > m_resources.eventHighWater) m_resources.eventHighWater = value; }

} // namespace application
} // namespace kernel
