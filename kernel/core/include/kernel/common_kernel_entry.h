// Common kernel initialization boundary.

#pragma once

#include <stdint.h>

#include "common_scheduler.h"

namespace kernel {
namespace common {

struct KernelEntryConfig {
    scheduler::page_allocate_fn allocate_pages;
    uint64_t stack_pages;
    uint64_t cooperative_switch_target;
    uint64_t preemption_target;
    const char* architecture_name;
    void (*log)(const char*);
    scheduler::page_release_fn release_pages;
};

bool kernel_entry_init(const KernelEntryConfig& config);

} // namespace common
} // namespace kernel

