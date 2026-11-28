#ifndef KERNEL_NATIVE_ELF_FAULT_H
#define KERNEL_NATIVE_ELF_FAULT_H

#include <stddef.h>
#include <stdint.h>

namespace kernel {
namespace native_elf {

// Layout produced by the amd64 exception entry stubs
// (kernel/arch/amd64/exception_entry.asm). The first 15 words are saved
// registers, followed by the normalized vector/error pair and the complete
// 64-bit CPU return frame. In 64-bit mode the CPU always pushes SS:RSP, with
// or without a privilege change. This is internal kernel diagnostic state; it
// is not part of the public Native ELF ABI.
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
    uint64_t rsp;
    uint64_t ss;
};

static_assert(offsetof(NativeExceptionFrame, vector) == 120, "exception_entry.asm vector slot");
static_assert(offsetof(NativeExceptionFrame, errorCode) == 128, "exception_entry.asm error slot");
static_assert(offsetof(NativeExceptionFrame, rip) == 136, "CPU frame starts after the error slot");
static_assert(offsetof(NativeExceptionFrame, ss) == 168, "CPU frame is RIP CS RFLAGS RSP SS");
static_assert(sizeof(NativeExceptionFrame) == 176, "normalized exception frame size");

// Dispatcher return values consumed by exception_entry.asm.
constexpr uint64_t kExceptionDispositionResume = 0;   // IRETQ to the interrupted context
constexpr uint64_t kExceptionDispositionRecover = 1;  // Native ELF kernel continuation

constexpr uint64_t kExceptionVectorCount = 32;

// Vectors for which the CPU pushes an error code in IA-32e mode (Intel SDM
// Vol. 3A Table 6-1, AMD APM Vol. 2 Table 8-1): #DF, #TS, #NP, #SS, #GP, #PF,
// #AC, #CP, and AMD #VC and #SX. #MF (16), #MC (18), #XM (19), #VE (20),
// #HV (28) and the reserved vectors push none.
constexpr uint32_t kExceptionHardwareErrorCodeMask =
    (1u << 8) | (1u << 10) | (1u << 11) | (1u << 12) | (1u << 13) |
    (1u << 14) | (1u << 17) | (1u << 21) | (1u << 29) | (1u << 30);

constexpr bool exception_vector_has_hardware_error_code(uint64_t vector) {
    return vector < kExceptionVectorCount &&
           ((kExceptionHardwareErrorCodeMask >> vector) & 1u) != 0;
}

// Selectors that the kernel GDT and the CPL3 lane can legitimately place in
// an exception frame. A CPL0 interrupted context may run with a null SS after
// a CPL3 -> CPL0 transition, which is architecturally valid in 64-bit mode.
constexpr uint64_t kExceptionKernelCs = 0x08;
constexpr uint64_t kExceptionKernelSs = 0x10;
constexpr uint64_t kExceptionUserCs = 0x23;
constexpr uint64_t kExceptionUserSs = 0x1B;

enum class ExceptionFrameStatus : uint32_t {
    Valid = 0,
    VectorOutOfRange,
    SyntheticErrorCodeNonzero,
    ErrorCodeHighBits,
    ErrorCodeMustBeZero,
    UnknownCodeSelector,
    StackSelectorMismatch,
    RflagsReservedBits,
    NonCanonicalRip,
    NonCanonicalRsp,
};

constexpr bool exception_address_canonical(uint64_t value) {
    return (value >> 47) == 0 || (value >> 47) == 0x1FFFFull;
}

// Checks only what the CPU and the entry stubs guarantee for a genuine
// delivery through this IDT. A failure means the frame was not produced by
// that contract (for example a vector/stub mismatch that shifted the frame),
// and the dispatcher must fail-stop instead of recovering.
constexpr ExceptionFrameStatus classify_exception_frame(const NativeExceptionFrame& frame) {
    if (frame.vector >= kExceptionVectorCount) return ExceptionFrameStatus::VectorOutOfRange;
    if (exception_vector_has_hardware_error_code(frame.vector)) {
        if ((frame.errorCode >> 32) != 0) return ExceptionFrameStatus::ErrorCodeHighBits;
        if ((frame.vector == 8 || frame.vector == 17) && frame.errorCode != 0) {
            return ExceptionFrameStatus::ErrorCodeMustBeZero;
        }
    } else if (frame.errorCode != 0) {
        return ExceptionFrameStatus::SyntheticErrorCodeNonzero;
    }
    if (frame.cs == kExceptionKernelCs) {
        if (frame.ss != kExceptionKernelSs && frame.ss != 0) {
            return ExceptionFrameStatus::StackSelectorMismatch;
        }
    } else if (frame.cs == kExceptionUserCs) {
        if (frame.ss != kExceptionUserSs) return ExceptionFrameStatus::StackSelectorMismatch;
    } else {
        return ExceptionFrameStatus::UnknownCodeSelector;
    }
    // Bit 1 is always one; VM (17) is impossible in 64-bit mode; bits 22-63
    // are reserved zero.
    if ((frame.rflags & 0x2u) == 0 || (frame.rflags & (1ull << 17)) != 0 ||
        (frame.rflags >> 22) != 0) {
        return ExceptionFrameStatus::RflagsReservedBits;
    }
    if (!exception_address_canonical(frame.rip)) return ExceptionFrameStatus::NonCanonicalRip;
    if (!exception_address_canonical(frame.rsp)) return ExceptionFrameStatus::NonCanonicalRsp;
    return ExceptionFrameStatus::Valid;
}

constexpr const char* exception_frame_status_name(ExceptionFrameStatus status) {
    return status == ExceptionFrameStatus::Valid ? "valid"
        : status == ExceptionFrameStatus::VectorOutOfRange ? "vector-out-of-range"
        : status == ExceptionFrameStatus::SyntheticErrorCodeNonzero ? "synthetic-error-nonzero"
        : status == ExceptionFrameStatus::ErrorCodeHighBits ? "error-high-bits"
        : status == ExceptionFrameStatus::ErrorCodeMustBeZero ? "error-must-be-zero"
        : status == ExceptionFrameStatus::UnknownCodeSelector ? "unknown-cs"
        : status == ExceptionFrameStatus::StackSelectorMismatch ? "ss-mismatch"
        : status == ExceptionFrameStatus::RflagsReservedBits ? "rflags-reserved"
        : status == ExceptionFrameStatus::NonCanonicalRip ? "rip-noncanonical"
        : status == ExceptionFrameStatus::NonCanonicalRsp ? "rsp-noncanonical"
        : "unknown";
}

} // namespace native_elf
} // namespace kernel

extern "C" uint64_t gxos_native_exception_dispatch(
    const kernel::native_elf::NativeExceptionFrame* frame);

#endif // KERNEL_NATIVE_ELF_FAULT_H
