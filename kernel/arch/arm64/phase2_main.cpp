#include <stdint.h>

#include "phase2_exceptions.h"
#include "phase2_gic.h"
#include "phase2_memory.h"
#include "phase2_mmu.h"
#include "phase2_timer.h"
#include "../../../aarch64/phase2/phase2_contract.h"
#include "../../../aarch64/phase2/phase2_platform.h"
#include "../../../aarch64/phase2/phase2_validation.h"

extern "C" void phase2_serial_init();
extern "C" void phase2_serial_set_base(uint64_t base);
extern "C" uint64_t phase2_serial_base();
extern "C" void phase2_serial_print(const char* text);
extern "C" void phase2_serial_hex(uint64_t value);
extern "C" void phase2_serial_dec(uint64_t value);
extern "C" uint8_t phase2_vectors[];

namespace {

static uint64_t read_current_el()
{
    uint64_t value = 0;
    __asm__ volatile("mrs %0, CurrentEL" : "=r"(value));
    return (value >> 2) & 3;
}

static uint64_t read_sp()
{
    uint64_t value = 0;
    __asm__ volatile("mov %0, sp" : "=r"(value));
    return value;
}

static uint64_t read_vbar()
{
    uint64_t value = 0;
    __asm__ volatile("mrs %0, vbar_el1" : "=r"(value));
    return value;
}

static bool stack_is_owned(const gxos_aarch64_phase2_handoff* handoff, uint64_t sp)
{
    uint64_t end = 0;
    return handoff && gxos_aarch64_add_u64(handoff->stack_base, handoff->stack_size, &end) &&
           end > handoff->stack_base && handoff->stack_top == end && sp >= handoff->stack_base &&
           sp <= end && (sp & 0xf) == 0;
}

static void fail(const char* reason)
{
    phase2_serial_print("[guideXOS] ");
    phase2_serial_print(reason);
    phase2_serial_print("\n[guideXOS] AARCH64_PHASE2_ERROR\n");
    for (;;) __asm__ volatile("wfi");
}

static void print_range(const char* label, uint64_t base, uint64_t size)
{
    phase2_serial_print(label);
    phase2_serial_hex(base);
    phase2_serial_print(" size=");
    phase2_serial_hex(size);
    phase2_serial_print("\n");
}

static void enable_irq()
{
    __asm__ volatile("msr daifclr, #2\n isb" ::: "memory");
}

static void disable_irq()
{
    __asm__ volatile("msr daifset, #2\n isb" ::: "memory");
}

} // namespace

extern "C" void phase2_main(const gxos_aarch64_phase2_handoff* handoff, uint64_t initial_el)
{
    phase2_serial_init();
    phase2_serial_print("[guideXOS] AARCH64 kernel entry\n");

    if (read_current_el() != 1) fail("execution level: unsupported");
    phase2_serial_print("[guideXOS] execution level: EL1\n");
    if (!stack_is_owned(handoff, read_sp())) fail("stack: FAIL");
    phase2_serial_print("[guideXOS] stack: OK\n");
    if (!phase2_memory_validate_handoff(handoff)) fail("firmware handoff: FAIL");
    phase2_serial_print("[guideXOS] ExitBootServices: OK\n");
    phase2_serial_print("[guideXOS] firmware handoff: OK\n");
    (void)initial_el;

    gxos_aarch64_phase2_platform platform;
    for (uint64_t i = 0; i < sizeof(platform); ++i) ((uint8_t*)&platform)[i] = 0;
    if (!gxos_aarch64_phase2_parse_dtb((const void*)(uintptr_t)handoff->dtb_base,
                                       handoff->dtb_size, &platform)) fail("DTB: FAIL");
    if (platform.timer_source != 2) fail("DTB timer source: unsupported");
    phase2_serial_print("[guideXOS] DTB: OK\n");
    for (uint32_t i = 0; i < platform.ram_count; ++i) print_range("[guideXOS] RAM: ", platform.ram[i].base, platform.ram[i].size);
    phase2_serial_print("[guideXOS] PL011: discovered ");
    phase2_serial_hex(platform.uart_base);
    phase2_serial_print("\n");
    if (platform.uart_base != phase2_serial_base()) {
        phase2_serial_set_base(platform.uart_base);
        phase2_serial_init();
    }
    phase2_serial_print("[guideXOS] PL011: active console validated\n");

    if (!phase2_mmu_build(&platform, handoff->kernel_base, handoff->kernel_size)) fail("MMU tables: FAIL");
    phase2_serial_print("[guideXOS] MMU tables: built\n");
    phase2_serial_print("[guideXOS] MMU root="); phase2_serial_hex(phase2_mmu_root());
    phase2_serial_print(" vector="); phase2_serial_hex((uint64_t)(uintptr_t)&phase2_vectors);
    phase2_serial_print(" vector-pte="); phase2_serial_hex(phase2_mmu_descriptor_for((uint64_t)(uintptr_t)&phase2_vectors));
    phase2_serial_print(" mmu-pte="); phase2_serial_hex(phase2_mmu_descriptor_for((uint64_t)(uintptr_t)&phase2_mmu_enable));
    phase2_serial_print(" stack="); phase2_serial_hex(handoff->stack_base);
    phase2_serial_print("\n");
    phase2_mmu_enable();
    const uint64_t sctlr = phase2_mmu_read_sctlr();
    const uint64_t ttbr0 = phase2_mmu_read_ttbr0();
    if ((sctlr & (UINT64_C(1) << 0)) == 0 ||
        (sctlr & (UINT64_C(1) << 2)) == 0 ||
        (sctlr & (UINT64_C(1) << 12)) == 0 ||
        (sctlr & (UINT64_C(1) << 19)) == 0 ||
        (ttbr0 & ~UINT64_C(0xfff)) != phase2_mmu_root()) fail("MMU: FAIL after transition");
    phase2_serial_print("[guideXOS] MMU: guideXOS tables active\n");
    phase2_serial_print("[guideXOS] MAIR_EL1="); phase2_serial_hex(phase2_mmu_read_mair());
    phase2_serial_print(" TCR_EL1="); phase2_serial_hex(phase2_mmu_read_tcr());
    phase2_serial_print(" TTBR0_EL1="); phase2_serial_hex(ttbr0);
    phase2_serial_print(" SCTLR_EL1="); phase2_serial_hex(sctlr);
    phase2_serial_print("\n");

    const uint64_t vectorAddress = (uint64_t)(uintptr_t)phase2_vectors;
    if (read_vbar() != vectorAddress || (vectorAddress & 0x7ff) != 0) fail("exception vectors: FAIL");
    phase2_serial_print("[guideXOS] exception vectors: OK\n");
    if (!phase2_exception_self_test()) fail("synchronous exception self-test: FAIL");
    phase2_serial_print("[guideXOS] synchronous exception self-test: PASS ESR=");
    phase2_serial_hex(phase2_exception_self_test_esr());
    phase2_serial_print(" ELR=");
    phase2_serial_hex(phase2_exception_self_test_elr());
    phase2_serial_print("\n");

    if (!phase2_early_allocator_init(handoff, &platform, (uint64_t)(uintptr_t)handoff)) {
        fail("physical memory: FAIL");
    }
    phase2_serial_print("[guideXOS] physical memory: OK\n");
    if (!phase2_early_allocator_self_test()) fail("early allocator: FAIL");
    phase2_serial_print("[guideXOS] early allocator: PASS free-pages=");
    phase2_serial_dec(phase2_early_allocator_free_pages());
    phase2_serial_print("\n");

    if (!phase2_gic_init(&platform, platform.timer_irq)) fail("GIC: FAIL");
    phase2_serial_print("[guideXOS] GIC: OK version=");
    phase2_serial_dec(platform.gic_version);
    phase2_serial_print(" GICD="); phase2_serial_hex(platform.gicd_base);
    phase2_serial_print(" GICC="); phase2_serial_hex(platform.gicc_base);
    phase2_serial_print("\n");
    phase2_serial_print("[guideXOS] GIC state: D_CTLR="); phase2_serial_hex(phase2_gic_distributor_control());
    phase2_serial_print(" D_ISENABLER0="); phase2_serial_hex(phase2_gic_enable0());
    phase2_serial_print(" D_IGROUPR0="); phase2_serial_hex(phase2_gic_group0());
    phase2_serial_print(" C_CTLR="); phase2_serial_hex(phase2_gic_cpu_control());
    phase2_serial_print(" C_PMR="); phase2_serial_hex(phase2_gic_priority_mask());
    phase2_serial_print("\n");
    if (!phase2_timer_init(platform.timer_irq)) fail("timer: FAIL");
    phase2_serial_print("[guideXOS] timer configured: physical frequency=");
    phase2_serial_dec(phase2_timer_frequency());
    phase2_serial_print(" IRQ="); phase2_serial_dec(phase2_timer_irq());
    phase2_serial_print(" CTL="); phase2_serial_hex(phase2_timer_control());
    phase2_serial_print(" TVAL="); phase2_serial_hex(phase2_timer_value());
    phase2_serial_print("\n");

    enable_irq();
    uint64_t waitLoops = 0;
    while (phase2_timer_count() == 0 && waitLoops++ < UINT64_C(1000000)) {
        __asm__ volatile("wfi");
    }
    disable_irq();
    if (phase2_timer_count() == 0) fail("timer IRQ: FAIL (timeout)");
    phase2_serial_print("[guideXOS] timer IRQ: PASS count=");
    phase2_serial_dec(phase2_timer_count());
    phase2_serial_print(" (returned from IRQ)\n");
    phase2_serial_print("AARCH64_PHASE2_PASS\n");
    for (;;) __asm__ volatile("wfi");
}
