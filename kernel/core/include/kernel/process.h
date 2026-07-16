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

#if defined(GXOS_NATIVE_THREAD_QEMU_TEST)

struct NativeThreadTestSnapshot {
    tid_t tid;
    uint32_t slot;
    uint32_t generation;
    uintptr_t stack_base;
    uintptr_t stack_limit;
    uintptr_t stack_pointer;
    uintptr_t initial_stack_pointer;
    uintptr_t initial_instruction_pointer;
    uintptr_t initial_rbx;
    uintptr_t initial_r12;
    uintptr_t initial_r13;
    uintptr_t initial_r14;
    uintptr_t initial_r15;
    bool native_started;
    bool wait_queue_linked;
    bool timer_linked;
};

bool native_thread_test_snapshot_current(NativeThreadTestSnapshot* snapshot);
uint32_t native_thread_test_live_count();
bool native_thread_test_slot_matches(uint32_t slot, uint32_t generation);
void native_thread_test_set_current_owner(pid_t owner);

#endif

} // namespace process
} // namespace kernel

#endif // KERNEL_PROCESS_H
