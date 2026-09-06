// Minimal guideXOS AArch64 kernel proof for AARCH64-1.

#include <stdint.h>
#include "../../../aarch64/phase1/phase1_contract.h"

extern "C" void phase1_serial_init();
extern "C" void phase1_serial_print(const char* text);

static uint64_t read_current_el()
{
    uint64_t value = 0;
    __asm__ volatile("mrs %0, CurrentEL" : "=r"(value));
    return (value >> 2) & 3;
}

static uint64_t read_sctlr_el1()
{
    uint64_t value = 0;
    __asm__ volatile("mrs %0, sctlr_el1" : "=r"(value));
    return value;
}

static uint64_t read_sp()
{
    uint64_t value = 0;
    __asm__ volatile("mov %0, sp" : "=r"(value));
    return value;
}

static bool add_ok(uint64_t a, uint64_t b, uint64_t* result)
{
    if (b > UINT64_MAX - a) return false;
    *result = a + b;
    return true;
}

static bool stack_is_owned(const gxos_aarch64_phase1_handoff* handoff, uint64_t sp)
{
    uint64_t end = 0;
    return handoff && handoff->stack_size != 0 &&
           add_ok(handoff->stack_base, handoff->stack_size, &end) &&
           handoff->stack_top == end && sp >= handoff->stack_base &&
           sp <= end && (sp & 0xf) == 0;
}

static bool handoff_is_valid(const gxos_aarch64_phase1_handoff* handoff)
{
    if (!handoff || handoff->magic != GXOS_AARCH64_PHASE1_HANDOFF_MAGIC ||
        handoff->version != GXOS_AARCH64_PHASE1_HANDOFF_VERSION ||
        handoff->size != sizeof(*handoff)) return false;
    const uint32_t required = GXOS_AARCH64_PHASE1_FLAG_EBS_COMPLETE |
                              GXOS_AARCH64_PHASE1_FLAG_IDENTITY_LOAD |
                              GXOS_AARCH64_PHASE1_FLAG_MMU_OFF_ON_ENTRY |
                              GXOS_AARCH64_PHASE1_FLAG_STACK_ALLOCATED |
                              GXOS_AARCH64_PHASE1_FLAG_MEMORY_MAP_VALID;
    if ((handoff->flags & required) != required) return false;
    if (handoff->kernel_base != GXOS_AARCH64_PHASE1_KERNEL_LOAD_ADDRESS ||
        handoff->kernel_size == 0 || handoff->kernel_entry < handoff->kernel_base ||
        handoff->kernel_entry >= handoff->kernel_base + handoff->kernel_size) return false;
    if (handoff->memory_map == 0 || handoff->memory_map_size == 0 ||
        handoff->memory_map_descriptor_size < 40 || handoff->memory_map_entry_count == 0) return false;
    if (handoff->uart_base != GXOS_AARCH64_PHASE1_UART_BASE) return false;
    return handoff->initial_current_el == 1 || handoff->initial_current_el == 2;
}

extern "C" void phase1_main(const gxos_aarch64_phase1_handoff* handoff, uint64_t initialEl)
{
    phase1_serial_init();
    phase1_serial_print("[guideXOS] AARCH64 kernel entry\n");

    const uint64_t currentEl = read_current_el();
    if (currentEl == 1) {
        phase1_serial_print("[guideXOS] execution level: EL1\n");
    } else {
        phase1_serial_print("[guideXOS] execution level: unsupported\n");
        phase1_serial_print("[guideXOS] AARCH64_PHASE1_ERROR\n");
        (void)initialEl;
        for (;;) __asm__ volatile("wfi");
    }

    const uint64_t sp = read_sp();
    const bool stackOk = stack_is_owned(handoff, sp);
    if (stackOk) {
        phase1_serial_print("[guideXOS] stack: OK\n");
    } else {
        phase1_serial_print("[guideXOS] stack: FAIL\n");
    }

    const bool handoffOk = handoff_is_valid(handoff);
    if (handoffOk) {
        phase1_serial_print("[guideXOS] ExitBootServices: OK\n");
        phase1_serial_print("[guideXOS] firmware handoff: OK\n");
    } else {
        phase1_serial_print("[guideXOS] firmware handoff: FAIL\n");
    }

    const uint64_t sctlr = read_sctlr_el1();
    if ((sctlr & 1) == 0) {
        phase1_serial_print("[guideXOS] MMU: OFF\n");
    } else {
        phase1_serial_print("[guideXOS] MMU: ON\n");
    }

    if (stackOk && handoffOk && (sctlr & 1) == 0) {
        phase1_serial_print("AARCH64_PHASE1_PASS\n");
    } else {
        phase1_serial_print("[guideXOS] AARCH64_PHASE1_ERROR\n");
    }
    for (;;) __asm__ volatile("wfi");
}
