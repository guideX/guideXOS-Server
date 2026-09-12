#include <stdint.h>

#include "phase3_timer.h"
#if defined(GXOS_AARCH64_RPI4_P1)
#include "../../../aarch64/rpi4/rpi4_p1_contract.h"
#include "../../../aarch64/phase2/phase2_contract.h"
using Aarch64Handoff = gxos_aarch64_rpi4_p1_handoff;
#else
#include "../../../aarch64/phase2/phase2_contract.h"
using Aarch64Handoff = gxos_aarch64_phase2_handoff;
#endif
#include "../../../aarch64/phase2/phase2_platform.h"
#include "../../../kernel/core/include/kernel/arch_interface.h"
#include "../../../kernel/core/include/kernel/common_kernel_entry.h"
#include "../../../kernel/core/include/kernel/common_scheduler.h"

extern "C" void phase3_serial_init();
extern "C" void phase3_serial_set_base(uint64_t base);
extern "C" uint64_t phase3_serial_base();
extern "C" void phase3_serial_print(const char* text);
extern "C" void phase3_serial_hex(uint64_t value);
extern "C" void phase3_serial_dec(uint64_t value);
extern "C" uint8_t phase3_vectors[];
extern "C" void phase3_register_probe(uint64_t task_id, uint64_t register_base);
extern "C" void phase3_preemptive_worker(void* argument);
extern "C" void phase3_preemptive_step(void* argument);
extern "C" void phase3_register_failure(uint64_t task_id, uint64_t register_id,
                                         uint64_t expected, uint64_t observed);
extern "C" uint8_t phase3_register_failure_active();
extern "C" uint8_t phase3_irq_controller_init(
    const gxos_aarch64_phase2_platform* platform, uint32_t timer_irq);
extern "C" uint32_t phase3_exception_count();
uint8_t phase2_early_allocator_init(
    const gxos_aarch64_phase2_handoff* handoff,
    const gxos_aarch64_phase2_platform* platform,
    uint64_t handoff_address);
uint8_t phase2_early_allocator_allocate(uint64_t pages, uint64_t* base);
uint8_t phase2_mmu_build(const gxos_aarch64_phase2_platform* platform,
                         uint64_t kernel_base, uint64_t kernel_size);
#if defined(GXOS_AARCH64_RPI4_P1)
uint8_t phase2_mmu_build_with_framebuffer(const gxos_aarch64_phase2_platform* platform,
                                          uint64_t kernel_base, uint64_t kernel_size,
                                          uint64_t framebuffer_base,
                                          uint64_t framebuffer_size);
#endif
void phase2_mmu_enable();
uint64_t phase2_mmu_root();
uint64_t phase2_mmu_read_sctlr();
uint64_t phase2_mmu_read_ttbr0();
uint64_t phase2_mmu_descriptor_for(uint64_t virtual_address);
uint8_t phase2_memory_validate_handoff(
    const gxos_aarch64_phase2_handoff* handoff);

namespace {

static const uint64_t kCooperativeTarget =
#if defined(GXOS_AARCH64_RPI4_P1)
    UINT64_C(1000);
#else
    UINT64_C(10000);
#endif
static const uint64_t kPreemptionTarget =
#if defined(GXOS_AARCH64_RPI4_P1)
    UINT64_C(1000);
#else
    UINT64_C(10000);
#endif
static const uint64_t kPrivateMagic = UINT64_C(0x47584f5350524956);

struct Work {
    volatile uint64_t counter;
    uint64_t private_magic;
    uint64_t register_base;
    uint32_t id;
    uint32_t reserved;
};

static Work gWork[3] = {
    { 0, kPrivateMagic ^ 1, UINT64_C(0xa100000000000000), 1, 0 },
    { 0, kPrivateMagic ^ 2, UINT64_C(0xb200000000000000), 2, 0 },
    { 0, kPrivateMagic ^ 3, UINT64_C(0xc300000000000000), 3, 0 }
};
static volatile uint8_t gFirstTaskEntered = 0;
static volatile uint8_t gRegisterFailure = 0;
static volatile uint8_t gIdleEntered = 0;
static volatile uint8_t gIdleWoke = 0;

static void fail(const char* reason)
{
    phase3_serial_print("[guideXOS] ");
    phase3_serial_print(reason);
#if defined(GXOS_AARCH64_RPI4_P1)
    phase3_serial_print("\n[guideXOS] AARCH64_P1_ERROR\n");
#else
    phase3_serial_print("\n[guideXOS] AARCH64_PHASE3_ERROR\n");
#endif
    for (;;) __asm__ volatile("wfi");
}

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

static bool stack_is_owned(const Aarch64Handoff* handoff, uint64_t sp)
{
    uint64_t end = 0;
    return handoff && handoff->stack_size != 0 &&
           handoff->stack_base <= UINT64_MAX - handoff->stack_size &&
           (end = handoff->stack_base + handoff->stack_size) > handoff->stack_base &&
           handoff->stack_top == end && sp >= handoff->stack_base && sp <= end &&
           (sp & 0xf) == 0;
}

static void log_text(const char* text)
{
    phase3_serial_print(text);
}

static void allocate_pages(uint64_t pages, uint64_t* base)
{
    if (!base || !phase2_early_allocator_allocate(pages, base)) {
        if (base) *base = 0;
    }
}

static bool work_valid(const Work* work)
{
    return work && work->private_magic == (kPrivateMagic ^ work->id) &&
           work->id >= 1 && work->id <= 3;
}

static bool all_work_progressed()
{
    for (uint32_t i = 0; i < 3; ++i) {
        if (!work_valid(&gWork[i]) || gWork[i].counter == 0) return false;
    }
    return true;
}

static void cooperative_task(void* argument)
{
    Work* work = static_cast<Work*>(argument);
    gFirstTaskEntered = 1;
    for (;;) {
        if (!work_valid(work)) {
            phase3_register_failure(work ? work->id : 0, 0, kPrivateMagic, 0);
            gRegisterFailure = 1;
            kernel::scheduler::mark_cooperative_complete();
            kernel::scheduler::yield();
            for (;;) kernel::arch::idle();
        }
        if (kernel::scheduler::cooperative_switches() >= kCooperativeTarget) {
            kernel::scheduler::mark_cooperative_complete();
            kernel::scheduler::yield();
            for (;;) kernel::arch::idle();
        }
        ++work->counter;
        kernel::scheduler::note_execution();
        phase3_register_probe(work->id, work->register_base);
        if (gRegisterFailure) {
            kernel::scheduler::mark_cooperative_complete();
            kernel::scheduler::yield();
            for (;;) kernel::arch::idle();
        }
        kernel::scheduler::yield();
    }
}

static void completion_task(void*)
{
    kernel::scheduler::return_to_bootstrap();
}

static void idle_task(void*)
{
    gIdleEntered = 1;
    kernel::arch::irq_enable();
    while (kernel::scheduler::idle_wake_ticks() == 0) kernel::arch::idle();
    gIdleWoke = 1;
    kernel::scheduler::return_to_bootstrap();
}

} // namespace

extern "C" uint8_t phase3_register_failure_active()
{
    return gRegisterFailure;
}

extern "C" void phase3_preemptive_step(void* argument)
{
    Work* work = static_cast<Work*>(argument);
    if (!work_valid(work)) {
        phase3_register_failure(work ? work->id : 0, 0, kPrivateMagic, 0);
        gRegisterFailure = 1;
        return;
    }
    ++work->counter;
    kernel::scheduler::note_execution();
}

extern "C" void phase3_register_failure(uint64_t task_id, uint64_t register_id,
                                         uint64_t expected, uint64_t observed)
{
    gRegisterFailure = 1;
    phase3_serial_print("[guideXOS] register integrity: FAIL task=");
    phase3_serial_dec(task_id);
    phase3_serial_print(" reg=x");
    phase3_serial_dec(register_id);
    phase3_serial_print(" expected=");
    phase3_serial_hex(expected);
    phase3_serial_print(" observed=");
    phase3_serial_hex(observed);
    phase3_serial_print("\n");
}

extern "C" void phase3_main(const Aarch64Handoff* handoff,
                             uint64_t initial_el)
{
#if defined(GXOS_AARCH64_RPI4_P1)
    phase3_serial_set_base(handoff ? handoff->uart_base : 0);
#endif
    phase3_serial_init();
    phase3_serial_print("[guideXOS] AARCH64 kernel entry\n");
#if defined(GXOS_AARCH64_RPI4_P1)
    phase3_serial_print("[guideXOS] P1-04 ExitBootServices: PASS\n");
    phase3_serial_print("[guideXOS] P1-05 physical kernel entry: PASS\n");
#endif
    if (read_current_el() != 1) fail("execution level: unsupported");
    phase3_serial_print("[guideXOS] execution level: EL1\n");
    if (!stack_is_owned(handoff, read_sp())) fail("stack: FAIL");
    phase3_serial_print("[guideXOS] stack: OK\n");
#if defined(GXOS_AARCH64_RPI4_P1)
    phase3_serial_print("[guideXOS] P1-06 owned kernel stack: PASS\n");
#endif
    if (!phase2_memory_validate_handoff(
            reinterpret_cast<const gxos_aarch64_phase2_handoff*>(handoff))) fail("firmware handoff: FAIL");
    phase3_serial_print("[guideXOS] ExitBootServices: OK\n");
    phase3_serial_print("[guideXOS] firmware handoff: OK\n");
    (void)initial_el;

    gxos_aarch64_phase2_platform platform;
    for (uint64_t i = 0; i < sizeof(platform); ++i) ((uint8_t*)&platform)[i] = 0;
    if (!gxos_aarch64_phase2_parse_dtb((const void*)(uintptr_t)handoff->dtb_base,
                                       handoff->dtb_size, &platform)) fail("DTB: FAIL");
#if defined(GXOS_AARCH64_RPI4_P1)
    if (platform.platform_kind != GXOS_AARCH64_PLATFORM_RASPBERRY_PI4 ||
        platform.timer_source != 2 || platform.gic_version != 2) fail("platform: unsupported");
    phase3_serial_print("[guideXOS] physical platform: Raspberry Pi 4\n");
    phase3_serial_print("[guideXOS] SoC: BCM2711\n");
#else
    if (platform.timer_source != 2 || platform.gic_version != 2) fail("platform: unsupported");
#endif
    phase3_serial_print("[guideXOS] DTB: OK\n");
    if (platform.uart_base != phase3_serial_base()) {
        phase3_serial_set_base(platform.uart_base);
        phase3_serial_init();
    }
    phase3_serial_print("[guideXOS] PL011: active console validated\n");
#if defined(GXOS_AARCH64_RPI4_P1)
    phase3_serial_print("[guideXOS] physical UART: PASS base=");
    phase3_serial_hex(platform.uart_base);
    phase3_serial_print("\n[guideXOS] firmware handoff: UEFI\n");
#endif

#if defined(GXOS_AARCH64_RPI4_P1)
    const bool hasFramebuffer = (handoff->flags & GXOS_AARCH64_RPI4_P1_FLAG_FRAMEBUFFER_VALID) != 0 &&
                                handoff->framebuffer_base != 0 && handoff->framebuffer_size != 0;
    if ((hasFramebuffer && !phase2_mmu_build_with_framebuffer(&platform, handoff->kernel_base,
                                                               handoff->kernel_size,
                                                               handoff->framebuffer_base,
                                                               handoff->framebuffer_size)) ||
        (!hasFramebuffer && !phase2_mmu_build(&platform, handoff->kernel_base, handoff->kernel_size))) {
#else
    if (!phase2_mmu_build(&platform, handoff->kernel_base, handoff->kernel_size)) {
#endif
        fail("MMU tables: FAIL");
    }
    phase3_serial_print("[guideXOS] MMU tables: built\n");
    phase2_mmu_enable();
    const uint64_t sctlr = phase2_mmu_read_sctlr();
    const uint64_t ttbr0 = phase2_mmu_read_ttbr0();
    if ((sctlr & ((UINT64_C(1) << 0) | (UINT64_C(1) << 2) |
                  (UINT64_C(1) << 12) | (UINT64_C(1) << 19))) !=
            ((UINT64_C(1) << 0) | (UINT64_C(1) << 2) |
             (UINT64_C(1) << 12) | (UINT64_C(1) << 19)) ||
        (ttbr0 & ~UINT64_C(0xfff)) != phase2_mmu_root()) fail("MMU: FAIL after transition");
    phase3_serial_print("[guideXOS] MMU: guideXOS tables active\n");
#if defined(GXOS_AARCH64_RPI4_P1)
    phase3_serial_print("[guideXOS] P1-07 MMU established: PASS\n");
    if (hasFramebuffer) {
        const uint64_t descriptor = phase2_mmu_descriptor_for(handoff->framebuffer_base);
        if ((descriptor & 3u) != 3u || ((descriptor >> 2) & 7u) != 2u) {
            fail("physical framebuffer mapping: FAIL");
        }
        phase3_serial_print("[guideXOS] physical framebuffer: PASS base=");
        phase3_serial_hex(handoff->framebuffer_base);
        phase3_serial_print(" size=");
        phase3_serial_hex(handoff->framebuffer_size);
        phase3_serial_print(" resolution=");
        phase3_serial_dec(handoff->framebuffer_width);
        phase3_serial_print("x");
        phase3_serial_dec(handoff->framebuffer_height);
        phase3_serial_print(" pitch=");
        phase3_serial_dec(handoff->framebuffer_pitch);
        phase3_serial_print(" format=");
        phase3_serial_dec(handoff->framebuffer_format);
        phase3_serial_print("\n");
        volatile uint32_t* pixels =
            reinterpret_cast<volatile uint32_t*>(static_cast<uintptr_t>(handoff->framebuffer_base));
        const uint32_t panelWidth = handoff->framebuffer_width < 256
            ? handoff->framebuffer_width : 256;
        const uint32_t panelHeight = handoff->framebuffer_height < 32
            ? handoff->framebuffer_height : 32;
        for (uint32_t y = 0; y < panelHeight; ++y) {
            for (uint32_t x = 0; x < panelWidth; ++x) {
                pixels[(static_cast<uint64_t>(y) * handoff->framebuffer_pitch) / 4 + x] =
                    0xff2040c0u;
            }
        }
        __asm__ volatile("dsb sy" ::: "memory");
        phase3_serial_print("[guideXOS] physical framebuffer status panel: PASS\n");
    } else {
        phase3_serial_print("[guideXOS] physical framebuffer: not available\n");
    }
#endif
    if (read_vbar() != (uint64_t)(uintptr_t)phase3_vectors ||
        ((uint64_t)(uintptr_t)phase3_vectors & 0x7ff) != 0) fail("exception vectors: FAIL");
    phase3_serial_print("[guideXOS] exception vectors: OK\n");
#if defined(GXOS_AARCH64_RPI4_P1)
    phase3_serial_print("[guideXOS] P1-08 exceptions established: PASS\n");
#endif

    if (!phase2_early_allocator_init(
            reinterpret_cast<const gxos_aarch64_phase2_handoff*>(handoff),
            &platform, (uint64_t)(uintptr_t)handoff)) {
        fail("physical memory: FAIL");
    }
    phase3_serial_print("[guideXOS] physical memory: OK\n");
    if (!phase3_irq_controller_init(&platform, platform.timer_irq)) fail("GIC: FAIL");
    phase3_serial_print("[guideXOS] GIC: OK version=");
    phase3_serial_dec(platform.gic_version);
    phase3_serial_print("\n");
#if defined(GXOS_AARCH64_RPI4_P1)
    phase3_serial_print("[guideXOS] P1-09 physical GIC online: PASS\n");
    phase3_serial_print("[guideXOS] GICv2 physical: initialized\n");
#endif
    if (!phase3_timer_configure(platform.timer_irq, 100)) fail("timer: FAIL");
    phase3_serial_print("[guideXOS] timer configured: physical frequency=");
    phase3_serial_dec(phase3_timer_frequency());
    phase3_serial_print(" IRQ=");
    phase3_serial_dec(phase3_timer_irq());
    phase3_serial_print(" interval-us=100\n");
#if defined(GXOS_AARCH64_RPI4_P1)
    phase3_serial_print("[guideXOS] architectural timer: programmed frequency=");
    phase3_serial_dec(phase3_timer_frequency());
    phase3_serial_print(" IRQ=");
    phase3_serial_dec(phase3_timer_irq());
    phase3_serial_print("\n");
#endif

    kernel::common::KernelEntryConfig commonConfig = {
        allocate_pages, 16, kCooperativeTarget, kPreemptionTarget,
        kernel::arch::interface_name(), log_text
    };
    if (!kernel::common::kernel_entry_init(commonConfig)) fail("common kernel entry: FAIL");
    phase3_serial_print("[guideXOS] scheduler: initialized\n");

    kernel::scheduler::Task* workers[3] = { 0, 0, 0 };
    for (uint32_t i = 0; i < 3; ++i) {
        workers[i] = kernel::scheduler::create_task(
            gWork[i].id, i == 0 ? "task-A" : (i == 1 ? "task-B" : "task-C"),
            cooperative_task, &gWork[i], true);
        if (!workers[i]) fail("thread context: FAIL");
    }
    kernel::scheduler::Task* completion = kernel::scheduler::create_task(
        99, "scheduler-report", completion_task, 0, false);
    if (!completion || kernel::scheduler::task_count() != 4) fail("thread context: FAIL");
    phase3_serial_print("[guideXOS] ARM64 thread context: OK\n");
    phase3_serial_print("[guideXOS] first kernel task: pending\n");

    kernel::scheduler::set_phase(kernel::scheduler::PHASE_COOPERATIVE);
    kernel::scheduler::start();
    if (!gFirstTaskEntered || gRegisterFailure || kernel::scheduler::failed() ||
        kernel::scheduler::cooperative_switches() < kCooperativeTarget ||
        !all_work_progressed() || !kernel::scheduler::stack_integrity()) {
        fail("cooperative scheduling: FAIL");
    }
    phase3_serial_print("[guideXOS] first kernel task: entered\n");
    phase3_serial_print("[guideXOS] cooperative scheduling: PASS switches=");
    phase3_serial_dec(kernel::scheduler::cooperative_switches());
    phase3_serial_print("\n");

    for (uint32_t i = 0; i < 3; ++i) {
        if (!kernel::scheduler::reset_task(workers[i], phase3_preemptive_worker,
                                           &gWork[i], true)) fail("preemption setup: FAIL");
        gWork[i].counter = 0;
    }
    if (!kernel::scheduler::reset_task(completion, completion_task, 0, true)) {
        fail("preemption setup: FAIL");
    }
    kernel::scheduler::set_completion_task(completion);
    kernel::scheduler::set_idle_task(0);
    kernel::scheduler::set_phase(kernel::scheduler::PHASE_PREEMPTIVE);
    if (!kernel::scheduler::prepare_interrupt_contexts()) fail("preemption setup: FAIL");
    phase3_timer_start();
    kernel::arch::irq_enable();
    kernel::scheduler::start();
    phase3_timer_stop();
    if (!kernel::scheduler::preemptive_complete() || gRegisterFailure ||
        kernel::scheduler::failed() || kernel::scheduler::preemptions() < kPreemptionTarget ||
        !all_work_progressed() || !kernel::scheduler::stack_integrity()) {
        fail("timer preemption: FAIL");
    }
    phase3_serial_print("[guideXOS] timer preemption: PASS preemptions=");
    phase3_serial_dec(kernel::scheduler::preemptions());
    phase3_serial_print("\n");
#if defined(GXOS_AARCH64_RPI4_P1)
    if (phase3_timer_count() < 100) fail("physical timer IRQ proof: FAIL");
    phase3_serial_print("[guideXOS] P1-10 timer IRQ observed: PASS count=");
    phase3_serial_dec(phase3_timer_count());
    phase3_serial_print("\n[guideXOS] P1-11 scheduler switch observed: PASS\n");
#endif

    for (uint32_t i = 0; i < 3; ++i) {
        if (!kernel::scheduler::reset_task(workers[i], cooperative_task, &gWork[i], false)) {
            fail("idle setup: FAIL");
        }
    }
    if (!kernel::scheduler::reset_task(completion, idle_task, 0, true)) fail("idle setup: FAIL");
    kernel::scheduler::set_completion_task(0);
    kernel::scheduler::set_idle_task(completion);
    kernel::scheduler::set_phase(kernel::scheduler::PHASE_IDLE);
    phase3_timer_start();
    kernel::arch::irq_enable();
    kernel::scheduler::start();
    phase3_timer_stop();
    if (!gIdleEntered || !gIdleWoke || kernel::scheduler::idle_wake_ticks() == 0) {
        fail("idle/wake: FAIL");
    }
    phase3_serial_print("[guideXOS] idle/wake: PASS WFI-entered timer-wake=");
    phase3_serial_dec(kernel::scheduler::idle_wake_ticks());
    phase3_serial_print("\n");

    kernel::scheduler::Stats stats;
    kernel::scheduler::get_stats(&stats);
    phase3_serial_print("[guideXOS] scheduler durability: PASS ticks=");
    phase3_serial_dec(stats.timer_ticks);
    phase3_serial_print(" context-switches=");
    phase3_serial_dec(stats.context_switches);
    phase3_serial_print(" preemptions=");
    phase3_serial_dec(stats.preemptions);
    phase3_serial_print(" unexpected-irq=");
    phase3_serial_dec(stats.unexpected_irqs);
    phase3_serial_print(" exceptions=");
    phase3_serial_dec(phase3_exception_count());
    phase3_serial_print(" last-unexpected-irq=");
    phase3_serial_dec(stats.last_unexpected_irq);
    phase3_serial_print(" task-A=");
    phase3_serial_dec(gWork[0].counter);
    phase3_serial_print(" task-B=");
    phase3_serial_dec(gWork[1].counter);
    phase3_serial_print(" task-C=");
    phase3_serial_dec(gWork[2].counter);
#if defined(GXOS_AARCH64_RPI4_P1)
    phase3_serial_print("[guideXOS] physical scheduler preemption: PASS preemptions=");
    phase3_serial_dec(kernel::scheduler::preemptions());
    phase3_serial_print("\n[guideXOS] P1-PASS\nAARCH64_P1_PASS\n");
#else
    phase3_serial_print("\nAARCH64_PHASE3_PASS\n");
#endif
    for (;;) __asm__ volatile("wfi");
}
