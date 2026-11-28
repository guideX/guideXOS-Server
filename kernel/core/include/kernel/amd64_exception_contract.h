#pragma once

#include <stddef.h>
#include <stdint.h>

#include "amd64_protected_task_contract.h"

namespace kernel {
namespace amd64_exception {

// The amd64 exception stubs save all GPRs, then a normalized vector/error
// pair, followed by the processor return frame. userRsp/userSs are present
// only when the saved CS has CPL3; callers must classify CS before reading
// those two words. CR2 and CR3 are captured separately by C++ at entry.
struct NormalizedFrame {
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
    uint64_t userRsp;
    uint64_t userSs;
};

static_assert(offsetof(NormalizedFrame, vector) == 120u,
              "exception stub vector offset changed");
static_assert(offsetof(NormalizedFrame, errorCode) == 128u,
              "exception stub error-code offset changed");
static_assert(offsetof(NormalizedFrame, rip) == 136u,
              "exception stub RIP offset changed");
static_assert(offsetof(NormalizedFrame, cs) == 144u,
              "exception stub CS offset changed");
static_assert(offsetof(NormalizedFrame, rflags) == 152u,
              "exception stub RFLAGS offset changed");
static_assert(offsetof(NormalizedFrame, userRsp) == 160u,
              "privilege-transition RSP offset changed");
static_assert(offsetof(NormalizedFrame, userSs) == 168u,
              "privilege-transition SS offset changed");

struct FaultRecord {
    uint32_t taskId;
    uint32_t reserved;
    uint64_t cr2;
    uint64_t taskCr3;
    NormalizedFrame frame;
};

inline bool exception_has_error_code(uint64_t vector)
{
    // Hardware error-code vectors in the supported AMD64 architectural
    // exception table, including #AC, #CP, #VC, and #SX on CPUs that define
    // them. Software exceptions never use these normalized stubs.
    return vector == 8u || vector == 10u || vector == 11u ||
           vector == 12u || vector == 13u || vector == 14u ||
           vector == 17u || vector == 21u || vector == 29u ||
           vector == 30u;
}

inline bool normalized_frame_valid(const NormalizedFrame& frame)
{
    return frame.vector < 32u &&
           (exception_has_error_code(frame.vector) || frame.errorCode == 0u);
}

inline bool supported_diagnostic_fault(uint64_t vector)
{
    return vector == 6u || vector == 13u || vector == 14u;
}

inline bool canonical_address(uint64_t address)
{
    const uint64_t upper = address >> 48u;
    const bool signBit = ((address >> 47u) & 1u) != 0u;
    return signBit ? upper == 0xFFFFu : upper == 0u;
}

struct RecoveryContract {
    const NormalizedFrame* frame;
    uint32_t taskId;
    uint32_t activeTaskId;
    uint32_t addressSpaceTaskId;
    amd64_protected_task::TaskState taskState;
    uint64_t activeCr3;
    uint64_t taskRootCr3;
    uint64_t kernelCr3;
    uint64_t expectedFaultVector;
    uint64_t expectedFaultErrorCode;
    uint64_t expectedFaultCr2;
    uint64_t cr2;
    uint64_t continuationRsp;
    uint64_t lifecycleStackBase;
    uint64_t lifecycleStackTop;
    uint64_t continuationRip;
    uint64_t expectedContinuationRip;
    uint64_t continuationRflags;
    uint64_t kernelFsBase;
    uint64_t kernelGsBase;
    uint64_t userCodeBase;
    uint64_t userCodeBytes;
    uint64_t userStackBase;
    uint64_t userStackBytes;
};

inline bool can_recover_diagnostic_fault(const RecoveryContract& contract)
{
    static const uint64_t kCr3AddressMask = 0x000FFFFFFFFFF000ull;
    static const uint64_t kForbiddenKernelRflags =
        (1ull << 8) | (1ull << 10) | (3ull << 12) | (1ull << 14) | (1ull << 17);
    const NormalizedFrame* frame = contract.frame;
    if (!frame || !normalized_frame_valid(*frame) ||
        !supported_diagnostic_fault(frame->vector) ||
        frame->vector != contract.expectedFaultVector ||
        frame->errorCode != contract.expectedFaultErrorCode ||
        frame->cs != amd64_protected_task::kUserCodeSelector ||
        frame->userSs != amd64_protected_task::kUserDataSelector ||
        frame->rflags != amd64_protected_task::kUserRflags ||
        contract.taskState != amd64_protected_task::TaskState::Running ||
        contract.taskId == 0u || contract.taskId != contract.activeTaskId ||
        contract.taskId != contract.addressSpaceTaskId ||
        contract.kernelCr3 == 0u || contract.taskRootCr3 == 0u ||
        (contract.activeCr3 & kCr3AddressMask) !=
            (contract.taskRootCr3 & kCr3AddressMask) ||
        (contract.kernelCr3 & kCr3AddressMask) ==
            (contract.taskRootCr3 & kCr3AddressMask) ||
        contract.continuationRip != contract.expectedContinuationRip ||
        (contract.continuationRflags & 0x2u) == 0u ||
        (contract.continuationRflags & kForbiddenKernelRflags) != 0u ||
        !canonical_address(contract.continuationRip) ||
        !canonical_address(contract.kernelFsBase) ||
        !canonical_address(contract.kernelGsBase) ||
        contract.lifecycleStackTop <= contract.lifecycleStackBase ||
        contract.continuationRsp < contract.lifecycleStackBase + 64u ||
        contract.continuationRsp >= contract.lifecycleStackTop ||
        (contract.continuationRsp & 0xFu) != 8u ||
        contract.userCodeBytes == 0u ||
        contract.userCodeBase > UINT64_MAX - contract.userCodeBytes ||
        frame->rip < contract.userCodeBase ||
        frame->rip >= contract.userCodeBase + contract.userCodeBytes ||
        contract.userStackBytes == 0u ||
        contract.userStackBase > UINT64_MAX - contract.userStackBytes ||
        frame->userRsp < contract.userStackBase ||
        frame->userRsp > contract.userStackBase + contract.userStackBytes ||
        (frame->userRsp & 0xFu) != 0u) {
        return false;
    }
    return contract.cr2 == contract.expectedFaultCr2 &&
           (frame->vector == 14u || contract.cr2 == 0u);
}

inline bool preserve_fault_record(FaultRecord* outRecord, uint32_t taskId,
                                  const NormalizedFrame& frame,
                                  uint64_t cr2, uint64_t taskCr3)
{
    if (!outRecord || taskId == 0u || !normalized_frame_valid(frame)) return false;
    outRecord->taskId = taskId;
    outRecord->reserved = 0u;
    outRecord->cr2 = cr2;
    outRecord->taskCr3 = taskCr3;
    outRecord->frame = frame;
    return true;
}

} // namespace amd64_exception
} // namespace kernel
