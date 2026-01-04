//
// Process management for guideXOS kernel
//
// Copyright (c) 2024 guideX
//

#ifndef KERNEL_PROCESS_H
#define KERNEL_PROCESS_H

#include "kernel/types.h"

namespace kernel {
namespace process {

// Process ID type
using pid_t = uint64_t;

// Process state
enum class State {
    Ready,
    Running,
    Blocked,
    Zombie
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

} // namespace process
} // namespace kernel

#endif // KERNEL_PROCESS_H
