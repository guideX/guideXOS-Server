#pragma once

#include <stddef.h>
#include <kernel/types.h>

namespace kernel {
namespace arch {
namespace ia64 {

enum CpuStateCheckpointField : uint64_t {
    CHECKPOINT_HAS_PSR      = 1ull << 0,
    CHECKPOINT_HAS_RSC      = 1ull << 1,
    CHECKPOINT_HAS_BSP      = 1ull << 2,
    CHECKPOINT_HAS_BSPSTORE = 1ull << 3,
    CHECKPOINT_HAS_RNAT     = 1ull << 4,
    CHECKPOINT_HAS_FPSR     = 1ull << 5,
    CHECKPOINT_HAS_IVA      = 1ull << 6,
    CHECKPOINT_HAS_PFS      = 1ull << 7,
    CHECKPOINT_HAS_UNAT     = 1ull << 8,
};

struct alignas(16) CpuStateCheckpoint {
    uint64_t stage;
    uint64_t valid_fields;
    uint64_t psr;
    uint64_t rsc;
    uint64_t bsp;
    uint64_t bspstore;
    uint64_t rnat;
    uint64_t fpsr;
    uint64_t iva;
    uint64_t pfs;
    uint64_t unat;
    uint64_t reserved;
};

static_assert(sizeof(CpuStateCheckpoint) == 96, "IA-64 checkpoint records are 96 bytes");
static_assert(offsetof(CpuStateCheckpoint, stage) == 0, "checkpoint stage offset changed");
static_assert(offsetof(CpuStateCheckpoint, valid_fields) == 8, "checkpoint mask offset changed");
static_assert(offsetof(CpuStateCheckpoint, psr) == 16, "checkpoint PSR offset changed");
static_assert(offsetof(CpuStateCheckpoint, rsc) == 24, "checkpoint RSC offset changed");
static_assert(offsetof(CpuStateCheckpoint, bsp) == 32, "checkpoint BSP offset changed");
static_assert(offsetof(CpuStateCheckpoint, bspstore) == 40, "checkpoint BSPSTORE offset changed");
static_assert(offsetof(CpuStateCheckpoint, rnat) == 48, "checkpoint RNAT offset changed");
static_assert(offsetof(CpuStateCheckpoint, fpsr) == 56, "checkpoint FPSR offset changed");
static_assert(offsetof(CpuStateCheckpoint, iva) == 64, "checkpoint IVA offset changed");
static_assert(offsetof(CpuStateCheckpoint, pfs) == 72, "checkpoint PFS offset changed");
static_assert(offsetof(CpuStateCheckpoint, unat) == 80, "checkpoint UNAT offset changed");

extern "C" volatile CpuStateCheckpoint ia64_checkpoint_a;
extern "C" volatile CpuStateCheckpoint ia64_checkpoint_b;
extern "C" volatile CpuStateCheckpoint ia64_checkpoint_c;
extern "C" volatile CpuStateCheckpoint ia64_checkpoint_d;

extern "C" void ia64_capture_checkpoint_c();
extern "C" void ia64_capture_checkpoint_d();

void report_startup_cpu_state_checkpoints();
void report_preexit_cpu_state_checkpoint();

} // namespace ia64
} // namespace arch
} // namespace kernel
