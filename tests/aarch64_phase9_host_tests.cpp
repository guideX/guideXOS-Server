#include <stdint.h>

#include <iostream>

#include "kernel/application_handle.h"
#include "kernel/application_runtime.h"

/* The common event-service controls run without an architecture backend.  A
 * real kernel supplies these symbols through arch_interface/common_scheduler;
 * this host binary supplies only the no-context stubs needed by queue tests. */
namespace kernel {
namespace arch {
interrupt_state_t irq_save() { return 0; }
void irq_restore(interrupt_state_t) {}
void irq_enable() {}
void irq_disable() {}
void idle() {}
void memory_barrier() {}
const char* interface_name() { return "host-test"; }
void* context_init(uint64_t, uint64_t, thread_entry_t, void*) { return nullptr; }
void context_switch(void**, void*) {}
bool interrupt_frame_save(void*, void*) { return false; }
bool context_to_interrupt(void*, void*) { return false; }
}
namespace scheduler {
Task* current_task() { return nullptr; }
bool block_current_locked() { return false; }
void wake_task_locked(Task*) {}
void wake_task(Task*) {}
}
}

static bool check(bool value, const char* message)
{
    if (!value) std::cerr << "FAIL: " << message << '\n';
    return value;
}

static gx_event event(gx_event_type type, gx_handle window = 0, int32_t p1 = 0)
{
    gx_event value{};
    value.size = sizeof(gx_event);
    value.type = type;
    value.window = window;
    value.param1 = p1;
    return value;
}

int main()
{
    using namespace kernel::application;
    const ApplicationQuotas quotas = { 2, 4, 32, 8, 4, 256 };
    ApplicationRuntime first;
    ApplicationRuntime second;
    bool pass = true;
    pass &= check(first.initialize("com.guidexos.phase9.appa", "AARCH64", 1, 7, quotas), "runtime A init");
    pass &= check(second.initialize("com.guidexos.phase9.appb", "AARCH64", 2, 7, quotas), "runtime B init");
    pass &= check(first.transition(LifecycleState::Running), "runtime A running");
    pass &= check(second.transition(LifecycleState::Running), "runtime B running");

    gx_handle window = make_handle(first.runtime_id(), first.generation(), HandleKind::Window, 1, 1);
    gx_handle widget = make_handle(first.runtime_id(), first.generation(), HandleKind::Widget, 1, 1);
    pass &= check(handle_matches(window, 1, 7, HandleKind::Window, 1, 1), "valid window handle");
    pass &= check(!handle_matches(window, 1, 8, HandleKind::Window, 1, 1), "stale generation rejected");
    pass &= check(!handle_matches(window, 2, 7, HandleKind::Window, 1, 1), "cross-app handle rejected");
    pass &= check(!handle_matches(window, 1, 7, HandleKind::Widget, 1, 1), "wrong-kind handle rejected");
    pass &= check(!handle_matches(window ^ UINT64_C(0x100), 1, 7, HandleKind::Window, 1, 1),
                  "forged handle rejected");
    pass &= check(handle_matches(widget, 1, 7, HandleKind::Widget, 1, 1), "valid widget handle");

    pass &= check(first.reserve_windows(2), "window quota reserve");
    pass &= check(!first.reserve_windows(1), "window quota +1 rejected");
    pass &= check(first.reserve_widgets(4), "widget quota reserve");
    pass &= check(!first.reserve_widgets(1), "widget quota +1 rejected");
    pass &= check(first.reserve_pages(8), "mapping quota reserve");
    pass &= check(!first.reserve_pages(1), "mapping quota +1 rejected");
    pass &= check(first.reserve_stack_pages(4), "stack quota reserve");
    pass &= check(!first.reserve_stack_pages(1), "stack quota +1 rejected");
    pass &= check(first.reserve_strings(256), "string quota reserve");
    pass &= check(!first.reserve_strings(1), "string quota +1 rejected");

    for (uint32_t i = 0; i < ApplicationEventService::kCapacity; ++i) {
        gx_event ordinary = event(GX_EVENT_MOUSE, window, static_cast<int32_t>(i));
        ordinary.param3 = GX_MOUSE_PACK(GX_MOUSE_BUTTON_LEFT, GX_MOUSE_ACTION_DOWN);
        pass &= check(first.events().enqueue(ordinary, false) == EventEnqueueResult::Accepted,
                      "ordinary event accepted");
    }
    pass &= check(first.events().enqueue(event(GX_EVENT_KEY, window, 1), false) == EventEnqueueResult::Dropped,
                  "event saturation drop-newest");
    pass &= check(first.events().enqueue(event(GX_EVENT_WINDOW_CLOSE, window), true) == EventEnqueueResult::Accepted,
                  "close reserved at saturation");
    gx_event observed{};
    bool sawClose = false;
    while (first.events().try_pop(&observed)) sawClose |= observed.type == GX_EVENT_WINDOW_CLOSE;
    pass &= check(sawClose, "close remains deliverable");
    pass &= check(first.events().high_water() == ApplicationEventService::kCapacity, "queue high-water bounded");
    pass &= check(first.events().dropped() >= 1, "queue drop count recorded");
    pass &= check(first.events().enqueue(event(GX_EVENT_WINDOW_CLOSE, window), true) == EventEnqueueResult::DuplicateLifecycle,
                  "duplicate close rejected");

    pass &= check(first.begin_closing(), "begin closing");
    pass &= check(!first.reserve_windows(1), "creation after closing rejected");
    pass &= check(first.mark_exited(), "mark exited");
    pass &= check(first.cleanup(), "cleanup once");
    pass &= check(first.cleanup(), "duplicate cleanup idempotent");
    pass &= check(first.events().enqueue(event(GX_EVENT_KEY), false) == EventEnqueueResult::Invalidated,
                  "event after exit rejected");

    if (!pass) return 1;
    std::cout << "AARCH64_PHASE9_HOST_CONTROLS_PASS\n";
    std::cout << "handles=forged,stale,cross-app,wrong-kind quotas=windows,widgets,mappings,stack,strings "
                  "saturation=close-guaranteed duplicate-cleanup=pass\n";
    return 0;
}
