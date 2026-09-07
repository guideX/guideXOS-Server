#include <cassert>
#include <cstdint>
#include <cstdio>

#include "kernel/scheduler_contract.h"

int main()
{
    const uint64_t base = UINT64_C(0x40000000);
    const uint64_t size = UINT64_C(0x2000);
    const uint64_t top = base + size - UINT64_C(0x10);

    assert(gxos_scheduler_stack_layout_valid(base, size, top));
    assert(!gxos_scheduler_stack_layout_valid(base + 1, size, top));
    assert(!gxos_scheduler_stack_layout_valid(base, size, base + size));
    assert(!gxos_scheduler_stack_layout_valid(base, UINT64_C(0x1000), base + UINT64_C(0xff0)));
    assert(!gxos_scheduler_stack_layout_valid(UINT64_MAX - 7, size, 0));
    assert(gxos_scheduler_entry_valid(reinterpret_cast<const void*>(1)));
    assert(!gxos_scheduler_entry_valid(0));

    const uint64_t low = UINT64_C(0x1122334455667788);
    const uint64_t high = UINT64_C(0x8877665544332211);
    assert(gxos_scheduler_canaries_valid(low, high, low, high));
    assert(!gxos_scheduler_canaries_valid(low ^ 1, high, low, high));
    assert(!gxos_scheduler_canaries_valid(low, high ^ 1, low, high));

    std::printf("AARCH64_PHASE3_HOST_CONTRACT_PASS\n");
    return 0;
}
