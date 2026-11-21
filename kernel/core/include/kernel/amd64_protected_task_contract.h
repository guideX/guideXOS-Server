#pragma once

#include <stdint.h>

namespace kernel {
namespace amd64_protected_task {

static const uint16_t kKernelCodeSelector = 0x08u;
static const uint16_t kKernelDataSelector = 0x10u;
static const uint16_t kUserDataSelector = 0x1Bu;
static const uint16_t kUserCodeSelector = 0x23u;
static const uint16_t kTssSelector = 0x28u;

static const uint8_t kKernelCodeAccess = 0x9Au;
static const uint8_t kKernelDataAccess = 0x92u;
static const uint8_t kUserCodeAccess = 0xFAu;
static const uint8_t kUserDataAccess = 0xF2u;
static const uint8_t kLongCodeFlags = 0x20u;
static const uint8_t kLongDataFlags = 0x00u;
static const uint64_t kUserRflags = 0x02u;

static const uint64_t kUserCanonicalLimit = 0x0000800000000000ull;
static const uint64_t kExitRequestMagic = 0x53325228ull; // S2R(
static const uint32_t kExitRequestOperation = 1u;

static const uint64_t kTaskUserWindowBase = 0x00007F8000000000ull;
static const uint64_t kTaskUserCodeAddress = kTaskUserWindowBase;
static const uint64_t kTaskUserDataAddress = kTaskUserWindowBase + 0x1000u;
static const uint64_t kTaskUserStackGuardAddress = kTaskUserWindowBase + 0x2000u;
static const uint64_t kTaskUserStackAddress = kTaskUserWindowBase + 0x3000u;
static const uint64_t kPageBytes = 4096u;

constexpr uint64_t flat_segment_descriptor(uint8_t access, uint8_t flags)
{
    return 0x000000000000FFFFull |
           (static_cast<uint64_t>(access) << 40) |
           (0xFull << 48) |
           (static_cast<uint64_t>(flags & 0xF0u) << 48);
}

constexpr bool valid_selector_layout()
{
    return kKernelCodeSelector == 0x08u &&
           kKernelDataSelector == 0x10u &&
           kUserDataSelector == 0x1Bu &&
           kUserCodeSelector == 0x23u &&
           kTssSelector == 0x28u &&
           (kKernelCodeSelector & 3u) == 0u &&
           (kKernelDataSelector & 3u) == 0u &&
           (kUserCodeSelector & 3u) == 3u &&
           (kUserDataSelector & 3u) == 3u &&
           (kTssSelector & 3u) == 0u;
}

#pragma pack(push, 1)
struct Tss64Layout {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomapBase;
};
#pragma pack(pop)

struct UserIretFrame {
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
};

struct UserExitFrame {
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t r11;
    uint64_t r10;
    uint64_t r9;
    uint64_t r8;
    uint64_t rdi;
    uint64_t rsi;
    uint64_t rbp;
    uint64_t rbx;
    uint64_t rdx;
    uint64_t rcx;
    uint64_t rax;
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t userRsp;
    uint64_t userSs;
};

enum class TaskState : uint8_t {
    Empty = 0,
    Created,
    Ready,
    Running,
    Exited,
    Faulted,
    Reaped
};

inline bool canonical_user_address(uint64_t address)
{
    return address < kUserCanonicalLimit;
}

inline bool build_user_iret_frame(uint64_t entry, uint64_t stackPointer,
                                  UserIretFrame* outFrame)
{
    if (!outFrame || !canonical_user_address(entry) ||
        !canonical_user_address(stackPointer) ||
        entry < kTaskUserCodeAddress ||
        entry >= kTaskUserCodeAddress + kPageBytes ||
        stackPointer < kTaskUserStackAddress ||
        stackPointer > kTaskUserStackAddress + kPageBytes ||
        (stackPointer & 0xFu) != 0u) {
        return false;
    }
    outFrame->rip = entry;
    outFrame->cs = kUserCodeSelector;
    outFrame->rflags = kUserRflags;
    outFrame->rsp = stackPointer;
    outFrame->ss = kUserDataSelector;
    return true;
}

inline bool is_user_origin(uint64_t codeSelector)
{
    return (codeSelector & 3u) == 3u;
}

inline bool valid_exit_request(const UserExitFrame& frame,
                               uint64_t expectedRip,
                               uint64_t expectedUserRsp)
{
    const uint64_t forbiddenFlags = (1ull << 8) |  // TF
                                    (1ull << 10) | // DF
                                    (3ull << 12) | // IOPL
                                    (1ull << 14) | // NT
                                    (1ull << 17);  // VM
    return frame.cs == kUserCodeSelector &&
           frame.userSs == kUserDataSelector &&
           frame.rip == expectedRip &&
           frame.userRsp == expectedUserRsp &&
           frame.rax == kExitRequestOperation &&
           frame.rdi == kExitRequestMagic &&
           (frame.rflags & 0x202u) == 0x002u &&
           (frame.rflags & forbiddenFlags) == 0u;
}

inline bool can_transition(TaskState from, TaskState to)
{
    if (from == TaskState::Empty) return to == TaskState::Created;
    if (from == TaskState::Created) return to == TaskState::Ready || to == TaskState::Reaped;
    if (from == TaskState::Ready) return to == TaskState::Running || to == TaskState::Reaped;
    if (from == TaskState::Running) return to == TaskState::Exited || to == TaskState::Faulted;
    if (from == TaskState::Exited || from == TaskState::Faulted) return to == TaskState::Reaped;
    return false;
}

} // namespace amd64_protected_task
} // namespace kernel
