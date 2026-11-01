#include <stdint.h>

#include "phase2_gic.h"
#include "gicv3_controller.h"

namespace {

static volatile uint32_t* gDistributor = nullptr;
static volatile uint32_t* gCpuInterface = nullptr;
static uint32_t gTimerIrq = 0;
static uint32_t gInterruptRegisters = 0;
static uint8_t gControllerVersion = 0;
static arm64::gicv3::State gGicv3State = {};
static arm64::gicv3::Error gGicv3Error = arm64::gicv3::Error::Ok;
static volatile uint64_t gGicv3AcknowledgeCount = 0;
static volatile uint64_t gGicv3EoiCount = 0;
static volatile uint64_t gGicv3InvalidAcknowledgements = 0;
static volatile uint64_t gGicv3CompletionErrors = 0;

static inline void gic_barrier()
{
    __asm__ volatile("dsb sy\n isb" ::: "memory");
}

static inline void write_byte(volatile uint8_t* base, uint64_t offset, uint8_t value)
{
    base[offset] = value;
}

} // namespace

phase2_gic_controller_kind phase2_gic_select_controller(
    const gxos_aarch64_phase2_platform* platform)
{
    if (!platform || !platform->valid) return phase2_gic_controller_kind::Unsupported;
    if (platform->gic_version == GXOS_AARCH64_GIC_V2) return phase2_gic_controller_kind::GicV2;
    if (platform->gic_version == GXOS_AARCH64_GIC_V3) return phase2_gic_controller_kind::GicV3;
    return phase2_gic_controller_kind::Unsupported;
}

uint8_t phase2_gic_init(const gxos_aarch64_phase2_platform* platform, uint32_t timer_irq)
{
    const phase2_gic_controller_kind kind = phase2_gic_select_controller(platform);
    if (!platform || gControllerVersion != 0 || timer_irq < 16 || timer_irq >= 32 ||
        timer_irq != platform->timer_irq ||
        kind == phase2_gic_controller_kind::Unsupported) return 0;
    if (kind == phase2_gic_controller_kind::GicV3) {
        gGicv3AcknowledgeCount = 0;
        gGicv3EoiCount = 0;
        gGicv3InvalidAcknowledgements = 0;
        gGicv3CompletionErrors = 0;
        const arm64::gicv3::RegisterIo io = arm64::gicv3::production_register_io();
        gGicv3Error = arm64::gicv3::initialize(&gGicv3State, platform, &io, 100000);
        if (gGicv3Error != arm64::gicv3::Error::Ok) return 0;
        gTimerIrq = timer_irq;
        gControllerVersion = GXOS_AARCH64_GIC_V3;
        return 1;
    }
    if (kind != phase2_gic_controller_kind::GicV2 ||
        platform->gicd_base == 0 || platform->gicc_base == 0 ||
        platform->gicd_size < 0x1000 || platform->gicc_size < 0x1000) return 0;
    gDistributor = (volatile uint32_t*)(uintptr_t)platform->gicd_base;
    gCpuInterface = (volatile uint32_t*)(uintptr_t)platform->gicc_base;
    gTimerIrq = timer_irq;

    gDistributor[0x000 / 4] = 0;
    const uint32_t type = gDistributor[0x004 / 4];
    gInterruptRegisters = (type & 0x1f) + 1;
    if (gInterruptRegisters == 0 || gInterruptRegisters > 32) gInterruptRegisters = 32;
    for (uint32_t i = 0; i < gInterruptRegisters; ++i) {
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
    gControllerVersion = GXOS_AARCH64_GIC_V2;
    return 1;
}

uint8_t phase2_gic_enable_irq(uint32_t irq)
{
    if (gControllerVersion == GXOS_AARCH64_GIC_V3) {
        gGicv3Error = arm64::gicv3::enable(&gGicv3State, irq);
        return gGicv3Error == arm64::gicv3::Error::Ok;
    }
    if (!gDistributor || irq < 32u || irq / 32u >= gInterruptRegisters) return 0;
    const uint32_t bit = UINT32_C(1) << (irq % 32u);
    // Keep platform device interrupts in Group 0, matching the simple
    // GICC_IAR/GICC_EOIR path used by this GICv2 bring-up.
    gDistributor[(0x080 / 4) + (irq / 32u)] &= ~bit;
    volatile uint8_t* priority = (volatile uint8_t*)(uintptr_t)gDistributor;
    priority[0x400 + irq] = 0x80;
    // Route every discovered SPI to the only Phase-7 CPU.
    priority[0x800 + irq] = 0x01;
    gDistributor[(0x100 / 4) + (irq / 32u)] |= bit;
    gic_barrier();
    return 1;
}

uint8_t phase2_gic_disable_irq(uint32_t irq)
{
    if (gControllerVersion == GXOS_AARCH64_GIC_V3) {
        gGicv3Error = arm64::gicv3::disable(&gGicv3State, irq);
        return gGicv3Error == arm64::gicv3::Error::Ok;
    }
    if (!gDistributor || irq < 32u || irq / 32u >= gInterruptRegisters) return 0;
    gDistributor[(0x180 / 4) + (irq / 32u)] = UINT32_C(1) << (irq % 32u);
    gic_barrier();
    return 1;
}

uint32_t phase2_gic_acknowledge()
{
    if (gControllerVersion == GXOS_AARCH64_GIC_V3) {
        const uint32_t acknowledgement = arm64::gicv3::acknowledge(&gGicv3State);
        ++gGicv3AcknowledgeCount;
        return acknowledgement;
    }
    if (!gCpuInterface) return 1023;
    return gCpuInterface[0x00c / 4];
}

void phase2_gic_complete(uint32_t acknowledgement)
{
    if (gControllerVersion == GXOS_AARCH64_GIC_V3) {
        gGicv3Error = arm64::gicv3::complete(&gGicv3State, acknowledgement);
        if (gGicv3Error == arm64::gicv3::Error::Ok) ++gGicv3EoiCount;
        else ++gGicv3CompletionErrors;
    } else if (gCpuInterface && phase2_gic_ack_valid(acknowledgement)) {
        gCpuInterface[0x010 / 4] = acknowledgement;
        gic_barrier();
    }
}

uint32_t phase2_gic_irq_id(uint32_t acknowledgement)
{
    return gControllerVersion == GXOS_AARCH64_GIC_V3
        ? arm64::gicv3::interrupt_id(acknowledgement) : acknowledgement & 0x3ff;
}

uint8_t phase2_gic_ack_valid(uint32_t acknowledgement)
{
    if (gControllerVersion == GXOS_AARCH64_GIC_V3) {
        const bool valid = arm64::gicv3::valid_acknowledgement(&gGicv3State, acknowledgement);
        if (!valid) ++gGicv3InvalidAcknowledgements;
        return valid;
    }
    return gCpuInterface && (acknowledgement & 0x3ff) < 1020;
}

uint32_t phase2_gic_version() { return gControllerVersion; }
uint32_t phase2_gic_last_error() { return static_cast<uint32_t>(gGicv3Error); }

uint32_t phase2_gic_distributor_control() { return gDistributor ? gDistributor[0x000 / 4] : 0; }
uint32_t phase2_gic_enable0() { return gDistributor ? gDistributor[0x100 / 4] : 0; }
uint32_t phase2_gic_group0() { return gDistributor ? gDistributor[0x080 / 4] : 0; }
uint32_t phase2_gic_cpu_control() { return gCpuInterface ? gCpuInterface[0x000 / 4] : 0; }
uint32_t phase2_gic_priority_mask() { return gCpuInterface ? gCpuInterface[0x004 / 4] : 0; }

uint8_t phase2_gicv3_get_diagnostics(phase2_gicv3_diagnostics* diagnostics)
{
    if (!diagnostics || gControllerVersion != GXOS_AARCH64_GIC_V3 ||
        !gGicv3State.initialized || !gGicv3State.io.read32 ||
        !gGicv3State.io.read8 || !gGicv3State.io.sys_read) return 0;

    const uint32_t interrupt = gGicv3State.timer_interrupt;
    const uint32_t group = gGicv3State.io.read32(gGicv3State.io.context,
                                                  gGicv3State.sgi_base, 0x0080);
    const uint32_t enable = gGicv3State.io.read32(gGicv3State.io.context,
                                                   gGicv3State.sgi_base, 0x0100);
    const uint32_t config = gGicv3State.io.read32(
        gGicv3State.io.context, gGicv3State.sgi_base,
        0x0c00 + (interrupt / 16) * 4);
    diagnostics->distributor_base = gGicv3State.distributor_base;
    diagnostics->redistributor_base = gGicv3State.redistributor_base;
    diagnostics->sgi_base = gGicv3State.sgi_base;
    diagnostics->boot_affinity = gGicv3State.boot_affinity;
    diagnostics->mpidr_el1 = gGicv3State.io.sys_read(
        gGicv3State.io.context, arm64::gicv3::SystemRegister::MpidrEl1);
    diagnostics->icc_sre_el1 = gGicv3State.io.sys_read(
        gGicv3State.io.context, arm64::gicv3::SystemRegister::SreEl1);
    diagnostics->icc_pmr_el1 = gGicv3State.io.sys_read(
        gGicv3State.io.context, arm64::gicv3::SystemRegister::PmrEl1);
    diagnostics->icc_bpr1_el1 = gGicv3State.io.sys_read(
        gGicv3State.io.context, arm64::gicv3::SystemRegister::Bpr1El1);
    diagnostics->icc_igrpen1_el1 = gGicv3State.io.sys_read(
        gGicv3State.io.context, arm64::gicv3::SystemRegister::IgrpEn1El1);
    diagnostics->icc_ctlr_el1 = gGicv3State.io.sys_read(
        gGicv3State.io.context, arm64::gicv3::SystemRegister::CtlrEl1);
    diagnostics->acknowledge_count = gGicv3AcknowledgeCount;
    diagnostics->eoi_count = gGicv3EoiCount;
    diagnostics->invalid_acknowledgements = gGicv3InvalidAcknowledgements;
    diagnostics->completion_errors = gGicv3CompletionErrors;
    diagnostics->timer_interrupt = interrupt;
    diagnostics->wait_limit = gGicv3State.wait_limit;
    diagnostics->last_acknowledgement = gGicv3State.last_acknowledgement;
    diagnostics->distributor_ctlr = gGicv3State.io.read32(
        gGicv3State.io.context, gGicv3State.distributor_base, 0x0000);
    diagnostics->redistributor_ctlr = gGicv3State.io.read32(
        gGicv3State.io.context, gGicv3State.redistributor_base, 0x0000);
    diagnostics->redistributor_waker = gGicv3State.io.read32(
        gGicv3State.io.context, gGicv3State.redistributor_base, 0x0014);
    diagnostics->timer_group = group;
    diagnostics->timer_enable = enable;
    diagnostics->timer_config = config;
    diagnostics->timer_priority = gGicv3State.io.read8(
        gGicv3State.io.context, gGicv3State.sgi_base, 0x0400 + interrupt);
    diagnostics->initialized = gGicv3State.initialized;
    diagnostics->eoimode_split = gGicv3State.eoimode_split;
    diagnostics->acknowledgement_pending = gGicv3State.acknowledgement_pending;
    return 1;
}
