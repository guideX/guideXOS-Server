#ifndef KERNEL_NATIVE_ELF_FAULT_H
#define KERNEL_NATIVE_ELF_FAULT_H

#include <stdint.h>

namespace kernel {
namespace native_elf {

// Layout produced by the amd64 Native ELF exception stubs.  The first 15
// words are saved registers, followed by the normalized vector/error pair and
// the CPU return frame.  This is internal kernel diagnostic state; it is not
// part of the public Native ELF ABI.
struct NativeExceptionFrame {
    uint64_t rax;
    uint64_t rcx;
    uint64_t rdx;
    uint64_t rbx;
    uint64_t rbp;
    uint64_t rsi;
    uint64_t rdi;
    uint64_t r8;
    uint64_t r9;
    uint64_t r10;
    uint64_t r11;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;
    uint64_t vector;
    uint64_t errorCode;
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
};

} // namespace native_elf
} // namespace kernel

extern "C" uint64_t gxos_native_exception_dispatch(
    const kernel::native_elf::NativeExceptionFrame* frame);

#endif // KERNEL_NATIVE_ELF_FAULT_H
