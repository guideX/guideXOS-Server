#pragma once

#include <stdint.h>
#include "boot_info.h"

namespace kernel {
namespace memory {

bool initialize(const boot::CommonBootInfo& boot_info);
bool allocate_pages(uint64_t pages, uint64_t* base);
bool allocate_pages_at(uint64_t base, uint64_t pages);
bool release_pages(uint64_t base, uint64_t pages);
bool is_reserved(uint64_t base, uint64_t size);
bool ready();
uint64_t total_pages();
uint64_t free_pages();
uint64_t allocated_pages();
uint64_t allocation_count();

} // namespace memory
} // namespace kernel
