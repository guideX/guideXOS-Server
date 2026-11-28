#ifndef KERNEL_NATIVE_ELF_FAULT_H
#define KERNEL_NATIVE_ELF_FAULT_H

#include <stdint.h>

#include "amd64_exception_contract.h"

namespace kernel {
namespace native_elf {

// Internal kernel diagnostic frame; it is not part of the Native ELF ABI.
using NativeExceptionFrame = amd64_exception::NormalizedFrame;

} // namespace native_elf
} // namespace kernel

extern "C" uint64_t gxos_native_exception_dispatch(
    const kernel::native_elf::NativeExceptionFrame* frame);

#endif // KERNEL_NATIVE_ELF_FAULT_H
