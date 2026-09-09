#pragma once

#include <stdint.h>

#include "application_event_service.h"
#include "application_handle.h"

namespace kernel {
namespace application {

enum class LifecycleState : uint8_t {
    Starting = 0,
    Running = 1,
    Waiting = 2,
    Closing = 3,
    Exited = 4,
    Cleaned = 5
};

struct ApplicationQuotas {
    uint32_t windows;
    uint32_t widgets;
    uint32_t eventEntries;
    uint32_t mappedPages;
    uint32_t stackPages;
    uint32_t stringBytes;
};

struct ResourceAccounting {
    uint32_t windows;
    uint32_t widgets;
    uint32_t eventEntries;
    uint32_t mappedPages;
    uint32_t stackPages;
    uint32_t stringBytes;
    uint32_t eventHighWater;
};

class ApplicationRuntime {
public:
    ApplicationRuntime();

    bool initialize(const char* packageId, const char* architecture,
                    uint8_t runtimeId, uint16_t generation,
                    const ApplicationQuotas& quotas);
    bool attach_task(scheduler::Task* task);
    bool transition(LifecycleState next);
    bool begin_closing();
    bool mark_exited();
    bool cleanup();

    bool reserve_windows(uint32_t count = 1);
    bool reserve_widgets(uint32_t count = 1);
    bool reserve_pages(uint32_t count);
    bool reserve_stack_pages(uint32_t count);
    bool reserve_strings(uint32_t bytes);
    void release_windows(uint32_t count = 1);
    void release_widgets(uint32_t count = 1);
    void release_pages(uint32_t count);
    void release_stack_pages(uint32_t count);
    void release_strings(uint32_t bytes);
    void note_event_high_water(uint32_t value);

    bool can_create_objects() const;
    LifecycleState state() const { return m_state; }
    uint8_t runtime_id() const { return m_runtimeId; }
    uint16_t generation() const { return m_generation; }
    const char* package_id() const { return m_packageId; }
    const char* architecture() const { return m_architecture; }
    scheduler::Task* task() const { return m_task; }
    ApplicationEventService& events() { return m_events; }
    const ApplicationEventService& events() const { return m_events; }
    const ApplicationQuotas& quotas() const { return m_quotas; }
    const ResourceAccounting& resources() const { return m_resources; }

private:
    static bool copy_text(char* destination, uint32_t capacity, const char* source);
    static uint32_t subtract_saturating(uint32_t value, uint32_t amount);

    char m_packageId[80];
    char m_architecture[16];
    uint8_t m_runtimeId;
    uint16_t m_generation;
    LifecycleState m_state;
    scheduler::Task* m_task;
    ApplicationQuotas m_quotas;
    ResourceAccounting m_resources;
    ApplicationEventService m_events;
};

} // namespace application
} // namespace kernel
