#pragma once

#include <stdint.h>

#include "../../../aarch64/phase2/phase2_contract.h"
#include "../../../aarch64/phase2/phase2_platform.h"

uint8_t phase2_memory_validate_handoff(const gxos_aarch64_phase2_handoff* handoff);
uint8_t phase2_early_allocator_init(const gxos_aarch64_phase2_handoff* handoff,
                                    const gxos_aarch64_phase2_platform* platform,
                                    uint64_t handoff_address);
uint8_t phase2_early_allocator_self_test();
uint8_t phase2_early_allocator_allocate(uint64_t pages, uint64_t* base);
uint8_t phase2_early_allocator_release(uint64_t base, uint64_t pages);
uint8_t phase2_early_allocator_is_protected(uint64_t base, uint64_t size);
uint64_t phase2_early_allocator_free_pages();
