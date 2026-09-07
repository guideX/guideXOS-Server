// Architecture-neutral, freestanding kernel scheduler.

#pragma once

#include <stdint.h>

#include "arch_interface.h"

namespace kernel {
namespace scheduler {

typedef void (*page_allocate_fn)(uint64_t pages, uint64_t* base);

enum Phase : uint8_t {
    PHASE_COOPERATIVE = 0,
    PHASE_PREEMPTIVE = 1,
    PHASE_IDLE = 2
};

enum TaskState : uint8_t {
    TASK_UNUSED = 0,
    TASK_RUNNABLE = 1,
    TASK_BLOCKED = 2,
    TASK_FINISHED = 3
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
};

struct Config {
    page_allocate_fn allocate_pages;
    uint64_t stack_pages;
    uint64_t cooperative_switch_target;
    uint64_t preemption_target;
};

struct Stats {
    uint64_t timer_ticks;
    uint64_t context_switches;
    uint64_t cooperative_switches;
    uint64_t preemptions;
    uint64_t idle_wake_ticks;
    uint32_t task_count;
    uint32_t unexpected_irqs;
};

bool initialize(const Config& config);
Task* create_task(uint32_t id, const char* name, kernel::arch::thread_entry_t entry,
                  void* argument, bool enabled);
bool reset_task(Task* task, kernel::arch::thread_entry_t entry, void* argument, bool enabled);
void set_phase(Phase phase);
void set_completion_task(Task* task);
void set_idle_task(Task* task);
bool prepare_interrupt_contexts();
void start();
void yield();
void mark_cooperative_complete();
void* timer_interrupt(void* frame);
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
void note_unexpected_irq();
void get_stats(Stats* stats);

} // namespace scheduler
} // namespace kernel

extern "C" void scheduler_yield();
extern "C" void scheduler_thread_exit();

