#include "../kernel/core/include/kernel/amd64_protected_task_contract.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

using namespace kernel::amd64_protected_task;

int main()
{
    static_assert(valid_selector_layout(), "selector RPL contract changed");
    static_assert(sizeof(Tss64Layout) == 104u, "AMD64 TSS size changed");
    static_assert(offsetof(Tss64Layout, rsp0) == 4u, "TSS RSP0 offset changed");
    static_assert(offsetof(Tss64Layout, ist1) == 36u, "TSS IST1 offset changed");
    static_assert(offsetof(Tss64Layout, iomapBase) == 102u,
                  "TSS I/O-map offset changed");
    static_assert(offsetof(UserExitFrame, rdi) == 64u,
                  "exit stub RDI save position changed");
    static_assert(offsetof(UserExitFrame, rax) == 112u,
                  "exit stub RAX save position changed");
    static_assert(offsetof(UserExitFrame, rip) == 120u,
                  "privilege return frame offset changed");
    static_assert(offsetof(UserExitFrame, userRsp) == 144u,
                  "privilege stack frame offset changed");

    assert(kKernelCodeSelector == 0x08u && (kKernelCodeSelector & 3u) == 0u);
    assert(kKernelDataSelector == 0x10u && (kKernelDataSelector & 3u) == 0u);
    assert(kUserDataSelector == 0x1Bu && (kUserDataSelector & 3u) == 3u);
    assert(kUserCodeSelector == 0x23u && (kUserCodeSelector & 3u) == 3u);
    assert(kTssSelector == 0x28u && (kTssSelector & 3u) == 0u);

    const uint64_t kernelCode = flat_segment_descriptor(
        kKernelCodeAccess, kLongCodeFlags);
    const uint64_t userCode = flat_segment_descriptor(
        kUserCodeAccess, kLongCodeFlags);
    const uint64_t userData = flat_segment_descriptor(
        kUserDataAccess, 0xC0u);
    assert(((kernelCode >> 40) & 0xFFu) == 0x9Au);
    assert(((userCode >> 40) & 0xFFu) == 0xFAu);
    assert(((userData >> 40) & 0xFFu) == 0xF2u);
    assert(((userCode >> 53) & 1u) == 1u); // L=1
    assert(((userCode >> 54) & 1u) == 0u); // D/B=0
    assert(((userData >> 54) & 1u) == 1u); // flat data descriptor

    UserIretFrame iret{};
    const uint64_t stack = kTaskUserStackAddress + kPageBytes - 16u;
    assert(build_user_iret_frame(kTaskUserCodeAddress, stack, &iret));
    assert(iret.rip == kTaskUserCodeAddress);
    assert(iret.cs == kUserCodeSelector);
    assert(iret.rflags == 0x02u);
    assert(iret.rsp == stack && iret.ss == kUserDataSelector);
    assert(!build_user_iret_frame(kTaskUserWindowBase - 1u, stack, &iret));
    assert(!build_user_iret_frame(kTaskUserStackGuardAddress, stack, &iret));
    assert(!build_user_iret_frame(kTaskUserCodeAddress, stack - 1u, &iret));
    assert(!build_user_iret_frame(kTaskUserCodeAddress,
                                  kTaskUserStackAddress - 16u, &iret));

    assert(!is_user_origin(kKernelCodeSelector));
    assert(is_user_origin(kUserCodeSelector));
    assert(is_user_origin(0x3Bu));

    UserExitFrame exit{};
    exit.rdi = kExitRequestMagic;
    exit.rax = kExitRequestOperation;
    exit.rip = kTaskUserCodeAddress + 50u;
    exit.cs = kUserCodeSelector;
    exit.userSs = kUserDataSelector;
    exit.userRsp = stack;
    exit.rflags = 0x02u;
    assert(valid_exit_request(exit, kTaskUserCodeAddress + 50u, stack));
    exit.cs = kKernelCodeSelector;
    assert(!valid_exit_request(exit, kTaskUserCodeAddress + 50u, stack));
    exit.cs = kUserCodeSelector;
    exit.rdi = 0u;
    assert(!valid_exit_request(exit, kTaskUserCodeAddress + 50u, stack));
    exit.rdi = kExitRequestMagic;
    exit.rflags |= (1ull << 9); // IF remains disabled for the bounded task.
    assert(!valid_exit_request(exit, kTaskUserCodeAddress + 50u, stack));

    assert(can_transition(TaskState::Empty, TaskState::Created));
    assert(can_transition(TaskState::Created, TaskState::Ready));
    assert(can_transition(TaskState::Ready, TaskState::Running));
    assert(can_transition(TaskState::Running, TaskState::Exited));
    assert(can_transition(TaskState::Running, TaskState::Faulted));
    assert(can_transition(TaskState::Exited, TaskState::Reaped));
    assert(can_transition(TaskState::Faulted, TaskState::Reaped));
    assert(!can_transition(TaskState::Running, TaskState::Reaped));
    assert(!can_transition(TaskState::Reaped, TaskState::Running));
    return 0;
}
