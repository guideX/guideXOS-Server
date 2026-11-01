#include "gicv3_controller.h"

namespace arm64 {
namespace gicv3 {
namespace {

static const uint32_t kDistributorCtlr = 0x0000;
static const uint32_t kDistributorTyper = 0x0004;
static const uint32_t kDistributorPidr2 = 0xffe8;
static const uint32_t kDistributorGroup = 0x0080;
static const uint32_t kDistributorEnable = 0x0100;
static const uint32_t kDistributorPending = 0x0200;
static const uint32_t kDistributorPriority = 0x0400;
static const uint32_t kDistributorConfig = 0x0c00;
static const uint32_t kDistributorRoute = 0x6000;
static const uint32_t kRedistributorCtlr = 0x0000;
static const uint32_t kRedistributorTyper = 0x0008;
static const uint32_t kRedistributorPidr2 = 0xffe8;
static const uint32_t kRedistributorWaker = 0x0014;
static const uint32_t kRedistributorSgiBase = 0x10000;
static const uint32_t kFrameBytes = 0x20000;
static const uint32_t kGicdCtlrEnableGroup1Ns = UINT32_C(1) << 1;
static const uint32_t kGicdCtlrAffinityRoutingNs = UINT32_C(1) << 4;
static const uint32_t kGicdCtlrRwp = UINT32_C(1) << 31;
static const uint32_t kGicrCtlrRwp = UINT32_C(1) << 3;
static const uint32_t kGicrWakerProcessorSleep = UINT32_C(1) << 1;
static const uint32_t kGicrWakerChildrenAsleep = UINT32_C(1) << 2;
static const uint32_t kGicrTyperLast = UINT32_C(1) << 4;
static const uint32_t kGicrTyperVlpIs = UINT32_C(1) << 1;
static const uint32_t kMaxIntid = 1020;
static const uint32_t kMaxRedistributorsVisited = 1024;

static bool has_io(const RegisterIo& io)
{
    return io.read32 && io.read64 && io.read8 && io.write32 && io.write64 &&
           io.write8 && io.sys_read && io.sys_write && io.barrier;
}

static bool range_contains(uint64_t size, uint32_t offset, uint32_t width)
{
    return width != 0 && offset <= size && width <= size - offset;
}

static uint64_t affinity_value(uint64_t mpidr)
{
    return (((mpidr >> 32) & UINT64_C(0xff)) << 24) |
           (((mpidr >> 16) & UINT64_C(0xff)) << 16) |
           (((mpidr >> 8) & UINT64_C(0xff)) << 8) |
           (mpidr & UINT64_C(0xff));
}

static bool wait_distributor(State* state)
{
    for (uint32_t i = 0; i < state->wait_limit; ++i) {
        if ((state->io.read32(state->io.context, state->distributor_base,
                              kDistributorCtlr) & kGicdCtlrRwp) == 0) return true;
    }
    return false;
}

static bool wait_redistributor(State* state)
{
    for (uint32_t i = 0; i < state->wait_limit; ++i) {
        if ((state->io.read32(state->io.context, state->redistributor_base,
                              kRedistributorCtlr) & kGicrCtlrRwp) == 0) return true;
    }
    return false;
}

static bool wait_children_awake(State* state)
{
    for (uint32_t i = 0; i < state->wait_limit; ++i) {
        if ((state->io.read32(state->io.context, state->redistributor_base,
                              kRedistributorWaker) & kGicrWakerChildrenAsleep) == 0) return true;
    }
    return false;
}

static Error locate_redistributor(State* state,
                                  const gxos_aarch64_phase2_platform* platform)
{
    for (uint32_t region = 0; region < state->redistributor_region_count; ++region) {
        const gxos_aarch64_gicv3_region& mapping = state->redistributor_regions[region];
        uint64_t offset = 0;
        for (uint32_t visited = 0; visited < kMaxRedistributorsVisited; ++visited) {
            if (offset > mapping.size || mapping.size - offset < kFrameBytes) break;
            const uint64_t frame = mapping.base + offset;
            const uint64_t typer = state->io.read64(state->io.context, frame,
                                                    kRedistributorTyper);
            const uint64_t affinity = (typer >> 32) & UINT64_C(0xffffffff);
            if (affinity == state->boot_affinity) {
                const uint32_t architecture_revision =
                    (state->io.read32(state->io.context, frame, kRedistributorPidr2) >> 4) & 0xf;
                if (architecture_revision != 3) return Error::UnsupportedCapability;
                state->redistributor_base = frame;
                state->sgi_base = frame + kRedistributorSgiBase;
                uint32_t waker = state->io.read32(state->io.context, frame,
                                                  kRedistributorWaker);
                waker &= ~kGicrWakerProcessorSleep;
                state->io.write32(state->io.context, frame, kRedistributorWaker, waker);
                state->io.barrier(state->io.context, Barrier::DsbSy);
                state->io.barrier(state->io.context, Barrier::Isb);
                return wait_children_awake(state)
                    ? Error::Ok : Error::RedistributorWakeTimeout;
            }
            if ((typer & kGicrTyperLast) != 0) break;
            uint64_t stride = state->redistributor_stride;
            if (stride == 0) stride = (typer & kGicrTyperVlpIs) ? UINT64_C(0x40000)
                                                                 : UINT64_C(0x20000);
            if (stride < kFrameBytes || (stride & UINT64_C(0xffff)) != 0 ||
                stride > mapping.size - offset) break;
            offset += stride;
        }
    }
    (void)platform;
    return Error::RedistributorNotFound;
}

static bool is_ppi(const State* state, uint32_t interrupt)
{
    return interrupt >= 16 && interrupt < 32 && state->sgi_base != 0;
}

static uint64_t register_base(const State* state, uint32_t interrupt)
{
    return is_ppi(state, interrupt) ? state->sgi_base : state->distributor_base;
}

static uint64_t register_size(const State* state, uint32_t interrupt)
{
    return is_ppi(state, interrupt) ? UINT64_C(0x10000) : state->distributor_size;
}

static Error validate_interrupt(const State* state, uint32_t interrupt)
{
    if (!state || !state->initialized || interrupt < 16 ||
        interrupt >= state->interrupt_count || interrupt >= kMaxIntid) {
        return Error::InvalidInterrupt;
    }
    if (interrupt < 32 && !is_ppi(state, interrupt)) return Error::InvalidInterrupt;
    return Error::Ok;
}

static bool write_pending_sync(State* state, uint32_t interrupt)
{
    if (is_ppi(state, interrupt)) return wait_redistributor(state);
    state->io.barrier(state->io.context, Barrier::DsbSy);
    return wait_distributor(state);
}

static void program_group1(State* state, uint32_t interrupt)
{
    const uint32_t offset = kDistributorGroup + (interrupt / 32) * 4;
    const uint32_t bit = UINT32_C(1) << (interrupt % 32);
    const uint64_t base = register_base(state, interrupt);
    uint32_t value = state->io.read32(state->io.context, base, offset);
    if ((value & bit) == 0) state->io.write32(state->io.context, base, offset, value | bit);
}

static Error program_route(State* state, uint32_t interrupt)
{
    if (interrupt < 32) return Error::Ok;
    const uint32_t offset = kDistributorRoute + (interrupt - 32) * 8;
    if (!range_contains(state->distributor_size, offset, 8)) return Error::InvalidLayout;
    // MPIDR/GICR affinity is stored as Aff3:Aff2:Aff1:Aff0 in a compact
    // 32-bit value. IROUTER places Aff3 in bits 39:32 and leaves bit 31 for
    // IRM, so move Aff3 into its architectural field before writing it.
    const uint64_t route_affinity =
        ((state->boot_affinity & UINT64_C(0xff000000)) << 8) |
        (state->boot_affinity & UINT64_C(0x00ffffff));
    state->io.write64(state->io.context, state->distributor_base, offset,
                      route_affinity);
    return Error::Ok;
}

static uint32_t config_offset(uint32_t interrupt)
{
    return kDistributorConfig + (interrupt / 16) * 4;
}

static Error mmio_read_pending(State* state, uint32_t interrupt, bool* pending)
{
    const uint32_t offset = kDistributorPending + (interrupt / 32) * 4;
    const uint64_t base = register_base(state, interrupt);
    if (!pending || !range_contains(register_size(state, interrupt), offset, 4)) {
        return Error::InvalidLayout;
    }
    *pending = (state->io.read32(state->io.context, base, offset) &
                (UINT32_C(1) << (interrupt % 32))) != 0;
    return Error::Ok;
}

static uint32_t production_read32(void*, uint64_t base, uint32_t offset)
{
    const uintptr_t address = static_cast<uintptr_t>(base + offset);
    return *reinterpret_cast<volatile uint32_t*>(address);
}

static uint64_t production_read64(void*, uint64_t base, uint32_t offset)
{
    const uintptr_t address = static_cast<uintptr_t>(base + offset);
    return *reinterpret_cast<volatile uint64_t*>(address);
}

static uint8_t production_read8(void*, uint64_t base, uint32_t offset)
{
    const uintptr_t address = static_cast<uintptr_t>(base + offset);
    return *reinterpret_cast<volatile uint8_t*>(address);
}

static void production_write32(void*, uint64_t base, uint32_t offset, uint32_t value)
{
    const uintptr_t address = static_cast<uintptr_t>(base + offset);
    *reinterpret_cast<volatile uint32_t*>(address) = value;
}

static void production_write64(void*, uint64_t base, uint32_t offset, uint64_t value)
{
    const uintptr_t address = static_cast<uintptr_t>(base + offset);
    *reinterpret_cast<volatile uint64_t*>(address) = value;
}

static void production_write8(void*, uint64_t base, uint32_t offset, uint8_t value)
{
    const uintptr_t address = static_cast<uintptr_t>(base + offset);
    *reinterpret_cast<volatile uint8_t*>(address) = value;
}

static uint64_t production_sys_read(void*, SystemRegister reg)
{
#if defined(__aarch64__)
    uint64_t value = 0;
    switch (reg) {
    case SystemRegister::IdAa64Pfr0El1: __asm__ volatile("mrs %0, id_aa64pfr0_el1" : "=r"(value)); break;
    case SystemRegister::MpidrEl1: __asm__ volatile("mrs %0, mpidr_el1" : "=r"(value)); break;
    case SystemRegister::SreEl1: __asm__ volatile("mrs %0, ICC_SRE_EL1" : "=r"(value)); break;
    case SystemRegister::PmrEl1: __asm__ volatile("mrs %0, ICC_PMR_EL1" : "=r"(value)); break;
    case SystemRegister::Bpr1El1: __asm__ volatile("mrs %0, ICC_BPR1_EL1" : "=r"(value)); break;
    case SystemRegister::IgrpEn1El1: __asm__ volatile("mrs %0, ICC_IGRPEN1_EL1" : "=r"(value)); break;
    case SystemRegister::Iar1El1: __asm__ volatile("mrs %0, ICC_IAR1_EL1" : "=r"(value)); break;
    case SystemRegister::Eoir1El1: break;
    case SystemRegister::DirEl1: break;
    case SystemRegister::CtlrEl1: __asm__ volatile("mrs %0, ICC_CTLR_EL1" : "=r"(value)); break;
    }
    return value;
#else
    (void)reg;
    return 0;
#endif
}

static void production_sys_write(void*, SystemRegister reg, uint64_t value)
{
#if defined(__aarch64__)
    switch (reg) {
    case SystemRegister::SreEl1: __asm__ volatile("msr ICC_SRE_EL1, %0" : : "r"(value) : "memory"); break;
    case SystemRegister::PmrEl1: __asm__ volatile("msr ICC_PMR_EL1, %0" : : "r"(value) : "memory"); break;
    case SystemRegister::Bpr1El1: __asm__ volatile("msr ICC_BPR1_EL1, %0" : : "r"(value) : "memory"); break;
    case SystemRegister::IgrpEn1El1: __asm__ volatile("msr ICC_IGRPEN1_EL1, %0" : : "r"(value) : "memory"); break;
    case SystemRegister::Eoir1El1: __asm__ volatile("msr ICC_EOIR1_EL1, %0" : : "r"(value) : "memory"); break;
    case SystemRegister::DirEl1: __asm__ volatile("msr ICC_DIR_EL1, %0" : : "r"(value) : "memory"); break;
    case SystemRegister::CtlrEl1: __asm__ volatile("msr ICC_CTLR_EL1, %0" : : "r"(value) : "memory"); break;
    case SystemRegister::IdAa64Pfr0El1:
    case SystemRegister::MpidrEl1:
    case SystemRegister::Iar1El1: break;
    }
#else
    (void)reg;
    (void)value;
#endif
}

static void production_barrier(void*, Barrier barrier)
{
#if defined(__aarch64__)
    if (barrier == Barrier::DsbSy) __asm__ volatile("dsb sy" ::: "memory");
    else __asm__ volatile("isb" ::: "memory");
#else
    (void)barrier;
#endif
}

} // namespace

RegisterIo production_register_io()
{
    RegisterIo io = {};
    io.read32 = production_read32;
    io.read64 = production_read64;
    io.read8 = production_read8;
    io.write32 = production_write32;
    io.write64 = production_write64;
    io.write8 = production_write8;
    io.sys_read = production_sys_read;
    io.sys_write = production_sys_write;
    io.barrier = production_barrier;
    return io;
}

Error initialize(State* state, const gxos_aarch64_phase2_platform* platform,
                 const RegisterIo* io, uint32_t wait_limit)
{
    if (!state || !platform || !io) return Error::InvalidArgument;
    if (state->initialized) return Error::AlreadyInitialized;
    if (!has_io(*io)) return Error::IoUnavailable;
    if (!platform->valid || platform->gic_version != GXOS_AARCH64_GIC_V3 ||
        platform->gicd_base == 0 ||
        platform->gicd_size < GXOS_AARCH64_GICV3_MIN_DISTRIBUTOR_SIZE ||
        platform->gic_redistributor_region_count == 0 ||
        platform->gic_redistributor_region_count > GXOS_AARCH64_PHASE2_MAX_GICR_REGIONS ||
        (platform->gic_redistributor_stride != 0 &&
         (platform->gic_redistributor_stride < kFrameBytes ||
          (platform->gic_redistributor_stride & UINT32_C(0xffff)) != 0)) ||
        platform->timer_irq < 16 || platform->timer_irq >= 32 ||
        platform->gicd_size > UINT64_MAX - platform->gicd_base) return Error::InvalidLayout;

    state->io.context = io->context;
    state->io.read32 = io->read32;
    state->io.read64 = io->read64;
    state->io.read8 = io->read8;
    state->io.write32 = io->write32;
    state->io.write64 = io->write64;
    state->io.write8 = io->write8;
    state->io.sys_read = io->sys_read;
    state->io.sys_write = io->sys_write;
    state->io.barrier = io->barrier;
    state->distributor_base = platform->gicd_base;
    state->distributor_size = platform->gicd_size;
    state->redistributor_region_count = platform->gic_redistributor_region_count;
    state->redistributor_stride = platform->gic_redistributor_stride;
    state->timer_interrupt = platform->timer_irq;
    state->wait_limit = wait_limit == 0 ? 100000 : wait_limit;
    if (state->wait_limit > 1000000) state->wait_limit = 1000000;
    for (uint32_t i = 0; i < state->redistributor_region_count; ++i) {
        const gxos_aarch64_gicv3_region region = platform->gic_redistributor_regions[i];
        uint64_t end = 0;
        if (region.base == 0 || region.size < GXOS_AARCH64_GICV3_MIN_REDISTRIBUTOR_SIZE ||
            (region.base & UINT64_C(0xffff)) != 0 ||
            (region.size & UINT64_C(0xffff)) != 0 ||
            region.size > UINT64_MAX - region.base) return Error::InvalidLayout;
        end = region.base + region.size;
        const uint64_t dist_end = platform->gicd_base + platform->gicd_size;
        if (end <= region.base || (region.base < dist_end && platform->gicd_base < end)) {
            return Error::InvalidLayout;
        }
        for (uint32_t j = 0; j < i; ++j) {
            const gxos_aarch64_gicv3_region prior = state->redistributor_regions[j];
            if (region.base < prior.base + prior.size && prior.base < end) {
                return Error::InvalidLayout;
            }
        }
        state->redistributor_regions[i] = region;
    }

    const uint64_t pfr0 = state->io.sys_read(state->io.context, SystemRegister::IdAa64Pfr0El1);
    const uint64_t gic_field = (pfr0 >> 24) & UINT64_C(0xf);
    if (gic_field == 0) return Error::UnsupportedCapability;
    state->boot_affinity = affinity_value(
        state->io.sys_read(state->io.context, SystemRegister::MpidrEl1));
    if (platform->gic_cpu_affinity_count > GXOS_AARCH64_PHASE2_MAX_CPU_AFFINITIES) {
        return Error::InvalidLayout;
    }
    if (platform->gic_cpu_affinity_count != 0) {
        bool found_cpu = false;
        for (uint32_t i = 0; i < platform->gic_cpu_affinity_count; ++i) {
            if (affinity_value(platform->gic_cpu_affinity[i]) == state->boot_affinity) {
                found_cpu = true;
                break;
            }
        }
        if (!found_cpu) return Error::RedistributorNotFound;
    }

    uint64_t sre = state->io.sys_read(state->io.context, SystemRegister::SreEl1);
    state->io.sys_write(state->io.context, SystemRegister::SreEl1, sre | UINT64_C(1));
    state->io.barrier(state->io.context, Barrier::Isb);
    if ((state->io.sys_read(state->io.context, SystemRegister::SreEl1) & UINT64_C(1)) == 0) {
        return Error::UnsupportedCapability;
    }
    state->io.sys_write(state->io.context, SystemRegister::IgrpEn1El1, 0);
    state->io.barrier(state->io.context, Barrier::Isb);
    uint64_t ctlr = state->io.sys_read(state->io.context, SystemRegister::CtlrEl1);
    ctlr &= ~(UINT64_C(1) << 1); // Combined priority drop and deactivation.
    state->io.sys_write(state->io.context, SystemRegister::CtlrEl1, ctlr);
    state->io.barrier(state->io.context, Barrier::Isb);
    state->eoimode_split = (state->io.sys_read(state->io.context,
                                               SystemRegister::CtlrEl1) &
                            (UINT64_C(1) << 1)) != 0;
    if (state->eoimode_split) return Error::UnsupportedCapability;
    state->io.sys_write(state->io.context, SystemRegister::PmrEl1, UINT64_C(0xff));
    state->io.sys_write(state->io.context, SystemRegister::Bpr1El1, 0);
    state->io.barrier(state->io.context, Barrier::Isb);

    if (!range_contains(state->distributor_size, kDistributorPidr2, 4) ||
        ((state->io.read32(state->io.context, state->distributor_base,
                           kDistributorPidr2) >> 4) & 0xf) != 3) {
        return Error::UnsupportedCapability;
    }
    const uint32_t typer = state->io.read32(state->io.context, state->distributor_base,
                                            kDistributorTyper);
    uint32_t lines = (typer & UINT32_C(0x1f)) + 1;
    if (lines == 0 || lines > 32) return Error::UnsupportedCapability;
    state->interrupt_count = lines * 32;
    if (state->interrupt_count > kMaxIntid) state->interrupt_count = kMaxIntid;
    if (state->timer_interrupt != platform->timer_irq ||
        state->timer_interrupt >= state->interrupt_count ||
        !range_contains(state->distributor_size, kDistributorTyper, 4)) {
        return Error::InvalidLayout;
    }

    // Only the non-secure Group 1 enable is touched. The distributor is not
    // globally reset, preserving firmware-owned state for other interrupts.
    uint32_t dctlr = state->io.read32(state->io.context, state->distributor_base,
                                     kDistributorCtlr);
    dctlr &= ~(kGicdCtlrEnableGroup1Ns | kGicdCtlrRwp);
    state->io.write32(state->io.context, state->distributor_base, kDistributorCtlr, dctlr);
    state->io.barrier(state->io.context, Barrier::DsbSy);
    if (!wait_distributor(state)) return Error::DistributorWriteTimeout;
    dctlr = state->io.read32(state->io.context, state->distributor_base,
                             kDistributorCtlr);
    dctlr = (dctlr & ~kGicdCtlrRwp) | kGicdCtlrAffinityRoutingNs;
    state->io.write32(state->io.context, state->distributor_base, kDistributorCtlr, dctlr);
    state->io.barrier(state->io.context, Barrier::DsbSy);
    if (!wait_distributor(state)) return Error::DistributorWriteTimeout;

    const Error rd_error = locate_redistributor(state, platform);
    if (rd_error != Error::Ok) return rd_error;
    uint32_t timer_bit = UINT32_C(1) << (state->timer_interrupt % 32);
    uint32_t rdist_value = state->io.read32(state->io.context, state->sgi_base,
                                            kDistributorGroup);
    state->io.write32(state->io.context, state->sgi_base, kDistributorGroup,
                      rdist_value | timer_bit);
    const uint32_t timer_config = config_offset(state->timer_interrupt);
    if (!range_contains(UINT64_C(0x10000), timer_config, 4)) return Error::InvalidLayout;
    uint32_t config = state->io.read32(state->io.context, state->sgi_base, timer_config);
    const uint32_t config_shift = (state->timer_interrupt % 16) * 2;
    config &= ~(UINT32_C(3) << config_shift); // Architected timer PPI is level-sensitive.
    state->io.write32(state->io.context, state->sgi_base, timer_config, config);
    state->io.write8(state->io.context, state->sgi_base,
                     kDistributorPriority + state->timer_interrupt, UINT8_C(0x80));
    state->io.write32(state->io.context, state->sgi_base, kDistributorEnable,
                      timer_bit);
    state->io.barrier(state->io.context, Barrier::DsbSy);
    if (!wait_redistributor(state)) return Error::RedistributorWriteTimeout;

    dctlr = state->io.read32(state->io.context, state->distributor_base,
                             kDistributorCtlr);
    dctlr = (dctlr & ~kGicdCtlrRwp) | kGicdCtlrAffinityRoutingNs |
            kGicdCtlrEnableGroup1Ns;
    state->io.write32(state->io.context, state->distributor_base, kDistributorCtlr, dctlr);
    state->io.barrier(state->io.context, Barrier::DsbSy);
    if (!wait_distributor(state)) return Error::DistributorWriteTimeout;
    state->initialized = 1;
    state->io.sys_write(state->io.context, SystemRegister::IgrpEn1El1, 1);
    state->io.barrier(state->io.context, Barrier::Isb);
    return Error::Ok;
}

Error enable(State* state, uint32_t interrupt)
{
    Error error = validate_interrupt(state, interrupt);
    if (error != Error::Ok) return error;
    program_group1(state, interrupt);
    error = set_priority(state, interrupt, UINT8_C(0x80));
    if (error != Error::Ok) return error;
    error = program_route(state, interrupt);
    if (error != Error::Ok) return error;
    const uint32_t offset = kDistributorEnable + (interrupt / 32) * 4;
    const uint64_t base = register_base(state, interrupt);
    if (!range_contains(register_size(state, interrupt), offset, 4)) return Error::InvalidLayout;
    state->io.write32(state->io.context, base, offset,
                      UINT32_C(1) << (interrupt % 32));
    state->io.barrier(state->io.context, Barrier::DsbSy);
    return write_pending_sync(state, interrupt)
        ? Error::Ok : (is_ppi(state, interrupt) ? Error::RedistributorWriteTimeout
                                                : Error::DistributorWriteTimeout);
}

Error disable(State* state, uint32_t interrupt)
{
    const Error error = validate_interrupt(state, interrupt);
    if (error != Error::Ok) return error;
    const uint32_t offset = 0x0180 + (interrupt / 32) * 4;
    const uint64_t base = register_base(state, interrupt);
    if (!range_contains(register_size(state, interrupt), offset, 4)) return Error::InvalidLayout;
    state->io.write32(state->io.context, base, offset,
                      UINT32_C(1) << (interrupt % 32));
    state->io.barrier(state->io.context, Barrier::DsbSy);
    return write_pending_sync(state, interrupt)
        ? Error::Ok : (is_ppi(state, interrupt) ? Error::RedistributorWriteTimeout
                                                : Error::DistributorWriteTimeout);
}

Error set_priority(State* state, uint32_t interrupt, uint8_t priority)
{
    const Error error = validate_interrupt(state, interrupt);
    if (error != Error::Ok) return error;
    const uint32_t offset = kDistributorPriority + interrupt;
    if (!range_contains(register_size(state, interrupt), offset, 1)) return Error::InvalidLayout;
    state->io.write8(state->io.context, register_base(state, interrupt), offset, priority);
    state->io.barrier(state->io.context, Barrier::DsbSy);
    return Error::Ok;
}

Error set_trigger(State* state, uint32_t interrupt, Trigger trigger)
{
    const Error error = validate_interrupt(state, interrupt);
    if (error != Error::Ok) return error;
    if (trigger != Trigger::Level && trigger != Trigger::Edge) return Error::InvalidArgument;
    const uint32_t offset = config_offset(interrupt);
    if (!range_contains(register_size(state, interrupt), offset, 4)) return Error::InvalidLayout;
    const uint32_t shift = (interrupt % 16) * 2;
    uint32_t config = state->io.read32(state->io.context, register_base(state, interrupt), offset);
    config &= ~(UINT32_C(3) << shift);
    if (trigger == Trigger::Edge) config |= UINT32_C(2) << shift;
    state->io.write32(state->io.context, register_base(state, interrupt), offset, config);
    state->io.barrier(state->io.context, Barrier::DsbSy);
    return write_pending_sync(state, interrupt)
        ? Error::Ok : (is_ppi(state, interrupt) ? Error::RedistributorWriteTimeout
                                                : Error::DistributorWriteTimeout);
}

Error is_pending(State* state, uint32_t interrupt, bool* pending)
{
    if (!pending) return Error::InvalidArgument;
    const Error error = validate_interrupt(state, interrupt);
    return error == Error::Ok ? mmio_read_pending(state, interrupt, pending) : error;
}

Error clear_pending(State* state, uint32_t interrupt)
{
    const Error error = validate_interrupt(state, interrupt);
    if (error != Error::Ok) return error;
    const uint32_t offset = 0x0280 + (interrupt / 32) * 4;
    const uint64_t base = register_base(state, interrupt);
    if (!range_contains(register_size(state, interrupt), offset, 4)) return Error::InvalidLayout;
    state->io.write32(state->io.context, base, offset,
                      UINT32_C(1) << (interrupt % 32));
    state->io.barrier(state->io.context, Barrier::DsbSy);
    return write_pending_sync(state, interrupt)
        ? Error::Ok : (is_ppi(state, interrupt) ? Error::RedistributorWriteTimeout
                                                : Error::DistributorWriteTimeout);
}

uint32_t acknowledge(State* state)
{
    if (!state || !state->initialized) return UINT32_C(0x3ff);
    const uint32_t acknowledgement = static_cast<uint32_t>(
        state->io.sys_read(state->io.context, SystemRegister::Iar1El1));
    state->last_acknowledgement = acknowledgement;
    const uint32_t interrupt = interrupt_id(acknowledgement);
    state->acknowledgement_pending = interrupt < kMaxIntid &&
                                    interrupt < state->interrupt_count;
    return acknowledgement;
}

uint32_t interrupt_id(uint32_t acknowledgement)
{
    return acknowledgement & UINT32_C(0x00ffffff);
}

bool valid_acknowledgement(State* state, uint32_t acknowledgement)
{
    const uint32_t interrupt = interrupt_id(acknowledgement);
    return state && state->initialized && state->acknowledgement_pending &&
           state->last_acknowledgement == acknowledgement && interrupt < kMaxIntid &&
           interrupt < state->interrupt_count;
}

Error complete(State* state, uint32_t acknowledgement)
{
    if (!valid_acknowledgement(state, acknowledgement)) return Error::InvalidAcknowledgement;
    state->io.sys_write(state->io.context, SystemRegister::Eoir1El1, acknowledgement);
    state->io.barrier(state->io.context, Barrier::Isb);
    if (state->eoimode_split) {
        state->io.sys_write(state->io.context, SystemRegister::DirEl1,
                            interrupt_id(acknowledgement));
        state->io.barrier(state->io.context, Barrier::Isb);
    }
    state->acknowledgement_pending = 0;
    return Error::Ok;
}

const char* error_name(Error error)
{
    switch (error) {
    case Error::Ok: return "ok";
    case Error::InvalidArgument: return "invalid argument";
    case Error::UnsupportedCapability: return "unsupported GICv3 CPU interface capability";
    case Error::AlreadyInitialized: return "controller already initialized";
    case Error::InvalidLayout: return "invalid GICv3 register layout";
    case Error::RedistributorNotFound: return "boot CPU Redistributor not found";
    case Error::RedistributorWakeTimeout: return "Redistributor synchronization timeout";
    case Error::RedistributorWriteTimeout: return "Redistributor write-pending timeout";
    case Error::DistributorWriteTimeout: return "Distributor write-pending timeout";
    case Error::InvalidInterrupt: return "invalid interrupt ID";
    case Error::InvalidAcknowledgement: return "invalid interrupt acknowledgement";
    case Error::IoUnavailable: return "register access backend unavailable";
    }
    return "unknown GICv3 error";
}

} // namespace gicv3
} // namespace arm64
