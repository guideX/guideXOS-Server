#pragma once

#include <stddef.h>
#include <stdint.h>

namespace kernel {
namespace nativeaot_pal_qemu_test {

void run(const uint8_t* artifact, size_t artifactSize,
         uintptr_t installAddress, uintptr_t mainAddress,
         uintptr_t uninstallAddress);

} // namespace nativeaot_pal_qemu_test
} // namespace kernel

