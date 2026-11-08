// Common compiler target model shared by the bootstrap frontend, backends,
// ELF writer, and the Developer Studio build service.
#pragma once

#include "kernel/types.h"

namespace kernel {
namespace compiler {

enum class CompilerTarget : uint8_t {
    Current = 0,
    Amd64 = 1,
    Arm64 = 2
};

// Native ARM64 bootstrap applications emitted by the resident compiler use
// this fixed image base.  The Studio process itself occupies 0x50000000, so
// compiled applications loaded alongside it use the adjacent 0x51000000 slot.
static const uint64_t ARM64_NATIVE_ELF_BOOTSTRAP_BASE = UINT64_C(0x51000000);

const char* target_architecture(CompilerTarget target);
uint16_t target_elf_machine(CompilerTarget target);
bool target_is_supported(CompilerTarget target);
CompilerTarget target_from_profile(const char* profile);
CompilerTarget current_target();

} // namespace compiler
} // namespace kernel
