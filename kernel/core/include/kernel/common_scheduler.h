// Architecture-neutral, freestanding kernel scheduler.

#pragma once

#include <stdint.h>

#include "arch_interface.h"

namespace kernel {
namespace scheduler {

typedef void (*page_allocate_fn)(uint64_t pages, uint64_t* base);
typedef bool (*page_release_fn)(uint64_t base, uint64_t pages);

enum Phase : uint8_t {
    PHASE_COOPERATIVE = 0,
    PHASE_PREEMPTIVE = 1,
    PHASE_IDLE = 2,
    // Long-lived, ordinary scheduler operation.  Unlike the bounded Phase-3
    // durability proof, this mode does not hand off at a fixed tick count.
    PHASE_SCHEDULED = 3
};

enum TaskState : uint8_t {
    TASK_UNUSED = 0,
    TASK_RUNNABLE = 1,
    TASK_BLOCKED = 2,
    TASK_FINISHED = 3
};

// EL0 children use the normal scheduler task and stack.  Their user register
// context and address-space owner are opaque to the common scheduler; ARM64
// exception code interprets those objects when it materializes a return.
enum ExecutionKind : uint8_t {
    EXECUTION_KERNEL = 0,
    EXECUTION_EL0_CHILD = 1
};

enum ExecutionState : uint8_t {
    EXECUTION_NONE = 0,
    EXECUTION_CREATED = 1,
    EXECUTION_READY = 2,
    EXECUTION_RUNNING = 3,
    EXECUTION_PREEMPTED = 4,
    EXECUTION_RETURNING = 5,
    EXECUTION_COMPLETED = 6,
    EXECUTION_FAULTED = 7,
    EXECUTION_TIMED_OUT = 8,
    EXECUTION_CANCELLED = 9,
    EXECUTION_START_FAILED = 10,
    EXECUTION_REAPED = 11
};

enum ExecutionResultKind : uint8_t {
    EXECUTION_RESULT_NONE = 0,
    EXECUTION_RESULT_EXIT = 1,
    EXECUTION_RESULT_FAULT = 2,
    EXECUTION_RESULT_TIMEOUT = 3,
    EXECUTION_RESULT_CANCEL = 4,
    EXECUTION_RESULT_START_FAILURE = 5
};

struct Task {
    uint32_t id;
    const char* name;
    kernel::arch::thread_entry_t entry;
    void* argument;
    uint64_t stack_base;
    uint64_t stack_size;
    uint64_t stack_top;
    uint64_t low_canary;
    uint64_t high_canary;
    void* context;
    uint64_t interrupt_context[kernel::arch::kInterruptContextWords];
    uint8_t interrupt_context_valid;
    uint8_t enabled;
    uint8_t state;
    uint8_t reserved;
    uint64_t execution_count;
    uint64_t preemption_count;
    uint64_t el0_preemption_count;
    uint64_t el0_entry_count;
    uint64_t el0_resume_count;
    uint64_t el0_timer_interrupt_count;
    uint64_t wait_count;
    uint64_t wake_count;
    uint64_t execution_id;
    uint64_t owner_id;
    uint64_t execution_budget;
    uint64_t execution_consumed;
    uint64_t execution_started_at;
    uint64_t execution_result;
    void* execution_context;
    void* execution_address_space;
    uint8_t execution_kind;
    uint8_t execution_state;
    uint8_t execution_result_kind;
    uint8_t cancel_requested;
    uint8_t resources_cleaned;
    uint8_t reserved_execution[3];
};

struct Config {
    page_allocate_fn allocate_pages;
    uint64_t stack_pages;
    uint64_t cooperative_switch_target;
    uint64_t preemption_target;
    page_release_fn release_pages;
};

struct Stats {
    uint64_t timer_ticks;
    uint64_t context_switches;
    uint64_t cooperative_switches;
    uint64_t preemptions;
    uint64_t idle_wake_ticks;
    uint32_t task_count;
    uint32_t unexpected_irqs;
    uint32_t last_unexpected_irq;
    uint64_t waits;
    uint64_t wakes;
};

bool initialize(const Config& config);
Task* create_task(uint32_t id, const char* name, kernel::arch::thread_entry_t entry,
                  void* argument, bool enabled);
bool reset_task(Task* task, kernel::arch::thread_entry_t entry, void* argument, bool enabled);
bool configure_el0_task(Task* task, uint64_t execution_id, uint64_t owner_id,
                        uint64_t budget, void* execution_context,
                        void* address_space);
bool start_task(Task* task, uint64_t now);
bool mark_el0_running(Task* task, uint64_t now);
bool mark_el0_resumed(Task* task, uint64_t now);
bool mark_el0_returning(Task* task, uint64_t now);
bool complete_el0_task(Task* task, ExecutionState state,
                       ExecutionResultKind result_kind, uint64_t result);
bool request_el0_cancel(Task* task);
bool reap_task(Task* task);
bool discard_unstarted_task(Task* task);
void set_phase(Phase phase);
void set_completion_task(Task* task);
void set_idle_task(Task* task);
bool prepare_interrupt_contexts();
void start();
void yield();
/* Called with interrupts already masked by a wait service.  The check,
 * TASK_BLOCKED transition, and switch are one scheduler critical section. */
bool block_current_locked();
void wake_task_locked(Task* task);
void wake_task(Task* task);
void mark_cooperative_complete();
void* timer_interrupt(void* frame);
void* timer_interrupt_at(void* frame, uint64_t monotonic_now);
void return_to_bootstrap();
void thread_exit();
void note_execution();

Task* current_task();
Task* task_at(uint32_t index);
uint32_t task_count();
uint64_t cooperative_switches();
uint64_t preemptions();
uint64_t idle_wake_ticks();
bool preemptive_complete();
bool failed();
bool stack_integrity();
void note_unexpected_irq(uint32_t irq);
void get_stats(Stats* stats);

} // namespace scheduler
} // namespace kernel

extern "C" void scheduler_yield();
extern "C" void scheduler_thread_exit();
