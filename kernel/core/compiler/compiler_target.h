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

const char* target_architecture(CompilerTarget target);
uint16_t target_elf_machine(CompilerTarget target);
bool target_is_supported(CompilerTarget target);
CompilerTarget target_from_profile(const char* profile);
CompilerTarget current_target();

} // namespace compiler
} // namespace kernel
