//
// Process management for guideXOS kernel
//
// Copyright (c) 2026 guideXOS Server
//

#ifndef KERNEL_PROCESS_H
#define KERNEL_PROCESS_H

#include "kernel/types.h"
#include "runtime/synchronization/guidexos_scheduler_wait.h"

namespace kernel {
namespace process {

// Process ID type
using pid_t = uint64_t;
using tid_t = uint64_t;

// Process state
enum class State {
    Ready,
    Running,
    Blocked,
    Zombie
};

// Scheduler-owned lifecycle.  A thread with TimedWait is also absent from
// the runnable queue; the separate name preserves the timer-registration
// invariant without inventing a second wait-node model.
enum class ThreadState {
    Runnable,
    Running,
    Blocked,
    TimedWait,
    Terminated
};

// Simple process structure
struct Process {
    pid_t pid;
    State state;
    uint64_t entry_point;
    uint64_t stack_pointer;
    const char* name;
};

// Initialize process subsystem
void init();

// Create the first user-mode process
// Returns PID on success, 0 on failure
pid_t create_init_process(uint64_t entry, uint64_t stack_top, const char* name);

// Schedule next process (round-robin for now)
void schedule();

// Create/destroy a native kernel thread.  The current pass intentionally does
// not create managed threads; these are the minimal scheduler-owned threads
// needed to exercise blocking and wakeup.
tid_t create_thread(pid_t owner, void (*entry)(void*), void* arg, const char* name);
bool terminate_thread(tid_t tid);
uint32_t terminate_process_threads(pid_t owner);
tid_t current_thread_id();

// Called by the monotonic timer interrupt.  Work is bounded per tick.
void timer_tick();

} // namespace process
} // namespace kernel

#endif // KERNEL_PROCESS_H
