#include <stdint.h>

#include "phase2_gic.h"

namespace {

static volatile uint32_t* gDistributor = nullptr;
static volatile uint32_t* gCpuInterface = nullptr;
static uint32_t gTimerIrq = 0;

static inline void gic_barrier()
{
    __asm__ volatile("dsb sy\n isb" ::: "memory");
}

static inline void write_byte(volatile uint8_t* base, uint64_t offset, uint8_t value)
{
    base[offset] = value;
}

} // namespace

uint8_t phase2_gic_init(const gxos_aarch64_phase2_platform* platform, uint32_t timer_irq)
{
    if (!platform || platform->gic_version != 2 || timer_irq < 16 || timer_irq >= 32 ||
        platform->gicd_base == 0 || platform->gicc_base == 0 ||
        platform->gicd_size < 0x1000 || platform->gicc_size < 0x1000) return 0;
    gDistributor = (volatile uint32_t*)(uintptr_t)platform->gicd_base;
    gCpuInterface = (volatile uint32_t*)(uintptr_t)platform->gicc_base;
    gTimerIrq = timer_irq;

    gDistributor[0x000 / 4] = 0;
    const uint32_t type = gDistributor[0x004 / 4];
    uint32_t interruptRegisters = (type & 0x1f) + 1;
    if (interruptRegisters == 0 || interruptRegisters > 32) interruptRegisters = 32;
    for (uint32_t i = 0; i < interruptRegisters; ++i) {
        gDistributor[(0x180 / 4) + i] = 0xffffffff; // disable
        gDistributor[(0x280 / 4) + i] = 0xffffffff; // clear pending
        gDistributor[(0x080 / 4) + i] = 0xffffffff; // Group 1 / non-secure
    }
    // Keep the timer in Group 0 for the simple GICC_IAR/EOIR path used by
    // this focused GICv2 bring-up.  QEMU's virt GIC permits this at EL1.
    gDistributor[0x080 / 4] &= ~(UINT32_C(1) << timer_irq);

    volatile uint8_t* priority = (volatile uint8_t*)(uintptr_t)platform->gicd_base;
    write_byte(priority, 0x400 + gTimerIrq, 0x80);
    // GICD_ISENABLER0 covers SGIs and PPIs (IDs 0-31); the architectural
    // timer PPI therefore uses its ordinary interrupt ID bit here.
    gDistributor[(0x100 / 4) + (timer_irq / 32)] = UINT32_C(1) << (timer_irq % 32);

    gCpuInterface[0x000 / 4] = 0;
    gCpuInterface[0x004 / 4] = 0xff; // accept every priority
    gCpuInterface[0x008 / 4] = 0;
    gCpuInterface[0x000 / 4] = 1; // Group 0 enabled
    gDistributor[0x000 / 4] = 1;
    gic_barrier();
    return 1;
}

uint32_t phase2_gic_acknowledge()
{
    if (!gCpuInterface) return 1023;
    return gCpuInterface[0x00c / 4];
}

void phase2_gic_complete(uint32_t acknowledgement)
{
    if (gCpuInterface) {
        gCpuInterface[0x010 / 4] = acknowledgement;
        gic_barrier();
    }
}

uint32_t phase2_gic_irq_id(uint32_t acknowledgement) { return acknowledgement & 0x3ff; }

uint32_t phase2_gic_distributor_control() { return gDistributor ? gDistributor[0x000 / 4] : 0; }
uint32_t phase2_gic_enable0() { return gDistributor ? gDistributor[0x100 / 4] : 0; }
uint32_t phase2_gic_group0() { return gDistributor ? gDistributor[0x080 / 4] : 0; }
uint32_t phase2_gic_cpu_control() { return gCpuInterface ? gCpuInterface[0x000 / 4] : 0; }
uint32_t phase2_gic_priority_mask() { return gCpuInterface ? gCpuInterface[0x004 / 4] : 0; }
