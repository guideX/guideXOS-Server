#pragma once

#include <stdint.h>

#include "../../../aarch64/phase2/phase2_platform.h"

namespace arm64 {
namespace gicv3 {

enum class Error : uint32_t {
    Ok = 0,
    InvalidArgument,
    UnsupportedCapability,
    AlreadyInitialized,
    InvalidLayout,
    RedistributorNotFound,
    RedistributorWakeTimeout,
    RedistributorWriteTimeout,
    DistributorWriteTimeout,
    InvalidInterrupt,
    InvalidAcknowledgement,
    IoUnavailable
};

enum class SystemRegister : uint32_t {
    IdAa64Pfr0El1,
    MpidrEl1,
    SreEl1,
    PmrEl1,
    Bpr1El1,
    IgrpEn1El1,
    Iar1El1,
    Eoir1El1,
    DirEl1,
    CtlrEl1
};

enum class Barrier : uint32_t { DsbSy, Isb };
enum class Trigger : uint32_t { Level = 0, Edge = 1 };

struct RegisterIo {
    void* context;
    uint32_t (*read32)(void* context, uint64_t base, uint32_t offset);
    uint64_t (*read64)(void* context, uint64_t base, uint32_t offset);
    uint8_t (*read8)(void* context, uint64_t base, uint32_t offset);
    void (*write32)(void* context, uint64_t base, uint32_t offset, uint32_t value);
    void (*write64)(void* context, uint64_t base, uint32_t offset, uint64_t value);
    void (*write8)(void* context, uint64_t base, uint32_t offset, uint8_t value);
    uint64_t (*sys_read)(void* context, SystemRegister reg);
    void (*sys_write)(void* context, SystemRegister reg, uint64_t value);
    void (*barrier)(void* context, Barrier barrier);
};

struct State {
    RegisterIo io;
    uint64_t distributor_base;
    uint64_t distributor_size;
    gxos_aarch64_gicv3_region redistributor_regions[GXOS_AARCH64_PHASE2_MAX_GICR_REGIONS];
    uint32_t redistributor_region_count;
    uint32_t redistributor_stride;
    uint64_t redistributor_base;
    uint64_t sgi_base;
    uint64_t boot_affinity;
    uint32_t interrupt_count;
    uint32_t timer_interrupt;
    uint32_t wait_limit;
    uint32_t last_acknowledgement;
    uint8_t initialized;
    uint8_t eoimode_split;
    uint8_t acknowledgement_pending;
    uint8_t reserved;
};

Error initialize(State* state, const gxos_aarch64_phase2_platform* platform,
                 const RegisterIo* io, uint32_t wait_limit);
Error enable(State* state, uint32_t interrupt);
Error disable(State* state, uint32_t interrupt);
Error set_priority(State* state, uint32_t interrupt, uint8_t priority);
Error set_trigger(State* state, uint32_t interrupt, Trigger trigger);
Error is_pending(State* state, uint32_t interrupt, bool* pending);
Error clear_pending(State* state, uint32_t interrupt);

uint32_t acknowledge(State* state);
uint32_t interrupt_id(uint32_t acknowledgement);
bool valid_acknowledgement(State* state, uint32_t acknowledgement);
Error complete(State* state, uint32_t acknowledgement);

const char* error_name(Error error);

// Production register backend. The same controller logic is exercised by
// hosted tests through RegisterIo callbacks.
RegisterIo production_register_io();

} // namespace gicv3
} // namespace arm64
