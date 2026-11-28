#include "../kernel/core/include/kernel/amd64_exception_contract.h"
#include "../kernel/core/include/kernel/native_elf_fault.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

using kernel::amd64_exception::FaultRecord;
using kernel::amd64_exception::NormalizedFrame;
using kernel::amd64_exception::RecoveryContract;
using kernel::amd64_protected_task::TaskState;

static const uint64_t kCode = 0x00007F8000000000ull;
static const uint64_t kStack = 0x00007F8000003000ull;
static const uint64_t kGuard = 0x00007F8000002000ull;
static const uint64_t kKernelCr3 = 0x1000u;
static const uint64_t kTaskCr3 = 0x5000u;
static const uint64_t kLifecycleStack = 0xFFFF800000100000ull;
static const uint64_t kContinuation = 0xFFFF800000001000ull;

static RecoveryContract make_page_fault_contract(NormalizedFrame* frame)
{
    *frame = NormalizedFrame{};
    frame->vector = 14u;
    frame->errorCode = 0x4u;
    frame->rip = kCode + 10u;
    frame->cs = 0x23u;
    frame->rflags = 0x2u;
    frame->userRsp = kStack + 0xFF0u;
    frame->userSs = 0x1Bu;

    RecoveryContract contract{};
    contract.frame = frame;
    contract.taskId = 0x522901u;
    contract.activeTaskId = 0x522901u;
    contract.addressSpaceTaskId = 0x522901u;
    contract.taskState = TaskState::Running;
    contract.activeCr3 = kTaskCr3 | 0x18u;
    contract.taskRootCr3 = kTaskCr3;
    contract.kernelCr3 = kKernelCr3;
    contract.expectedFaultVector = 14u;
    contract.expectedFaultErrorCode = 0x4u;
    contract.expectedFaultCr2 = kGuard;
    contract.cr2 = kGuard;
    contract.continuationRsp = kLifecycleStack + 0x808u;
    contract.lifecycleStackBase = kLifecycleStack;
    contract.lifecycleStackTop = kLifecycleStack + 0x4000u;
    contract.continuationRip = kContinuation;
    contract.expectedContinuationRip = kContinuation;
    contract.continuationRflags = 0x202u;
    contract.kernelFsBase = 0u;
    contract.kernelGsBase = 0u;
    contract.userCodeBase = kCode;
    contract.userCodeBytes = 0x1000u;
    contract.userStackBase = kStack;
    contract.userStackBytes = 0x1000u;
    return contract;
}

int main()
{
    static_assert(sizeof(NormalizedFrame) == 176u,
                  "normalized frame must include optional user RSP/SS words");
    static_assert(offsetof(NormalizedFrame, vector) == 120u,
                  "stub GPR prefix changed");
    static_assert(offsetof(NormalizedFrame, errorCode) == 128u,
                  "normalized error-code offset changed");
    static_assert(offsetof(NormalizedFrame, rip) == 136u,
                  "hardware RIP offset changed");
    static_assert(offsetof(NormalizedFrame, userRsp) == 160u,
                  "privilege-transition RSP offset changed");

    assert(kernel::amd64_exception::exception_has_error_code(8u));
    assert(kernel::amd64_exception::exception_has_error_code(10u));
    assert(kernel::amd64_exception::exception_has_error_code(11u));
    assert(kernel::amd64_exception::exception_has_error_code(12u));
    assert(kernel::amd64_exception::exception_has_error_code(13u));
    assert(kernel::amd64_exception::exception_has_error_code(14u));
    assert(kernel::amd64_exception::exception_has_error_code(17u));
    assert(kernel::amd64_exception::exception_has_error_code(21u));
    assert(kernel::amd64_exception::exception_has_error_code(29u));
    assert(kernel::amd64_exception::exception_has_error_code(30u));
    assert(!kernel::amd64_exception::exception_has_error_code(6u));
    assert(!kernel::amd64_exception::exception_has_error_code(3u));

    NormalizedFrame noError{};
    noError.vector = 6u;
    assert(kernel::amd64_exception::normalized_frame_valid(noError));
    noError.errorCode = 1u;
    assert(!kernel::amd64_exception::normalized_frame_valid(noError));
    noError.vector = 32u;
    noError.errorCode = 0u;
    assert(!kernel::amd64_exception::normalized_frame_valid(noError));

    NormalizedFrame frame{};
    RecoveryContract contract = make_page_fault_contract(&frame);
    assert(kernel::amd64_exception::supported_diagnostic_fault(6u));
    assert(kernel::amd64_exception::supported_diagnostic_fault(13u));
    assert(kernel::amd64_exception::supported_diagnostic_fault(14u));
    assert(!kernel::amd64_exception::supported_diagnostic_fault(8u));
    assert(kernel::amd64_exception::can_recover_diagnostic_fault(contract));

    NormalizedFrame opcodeFrame = frame;
    opcodeFrame.vector = 6u;
    opcodeFrame.errorCode = 0u;
    opcodeFrame.rip = kCode;
    RecoveryContract opcodeContract = contract;
    opcodeContract.frame = &opcodeFrame;
    opcodeContract.expectedFaultVector = 6u;
    opcodeContract.expectedFaultErrorCode = 0u;
    opcodeContract.expectedFaultCr2 = 0u;
    opcodeContract.cr2 = 0u;
    assert(kernel::amd64_exception::can_recover_diagnostic_fault(opcodeContract));

    NormalizedFrame gpFrame = frame;
    gpFrame.vector = 13u;
    gpFrame.errorCode = (0x81u << 3u) | 0x2u;
    gpFrame.rip = kCode + 2u;
    RecoveryContract gpContract = contract;
    gpContract.frame = &gpFrame;
    gpContract.expectedFaultVector = 13u;
    gpContract.expectedFaultErrorCode = (0x81u << 3u) | 0x2u;
    gpContract.expectedFaultCr2 = 0u;
    gpContract.cr2 = 0u;
    assert(kernel::amd64_exception::can_recover_diagnostic_fault(gpContract));

    RecoveryContract rejected = contract;
    rejected.taskState = TaskState::Faulted;
    assert(!kernel::amd64_exception::can_recover_diagnostic_fault(rejected));
    rejected = contract; rejected.taskId++;
    assert(!kernel::amd64_exception::can_recover_diagnostic_fault(rejected));
    rejected = contract; rejected.addressSpaceTaskId++;
    assert(!kernel::amd64_exception::can_recover_diagnostic_fault(rejected));
    rejected = contract; rejected.activeTaskId++;
    assert(!kernel::amd64_exception::can_recover_diagnostic_fault(rejected));
    rejected = contract; rejected.activeCr3 = kKernelCr3;
    assert(!kernel::amd64_exception::can_recover_diagnostic_fault(rejected));
    rejected = contract; rejected.kernelCr3 = kTaskCr3;
    assert(!kernel::amd64_exception::can_recover_diagnostic_fault(rejected));
    rejected = contract; rejected.expectedFaultVector = 6u;
    assert(!kernel::amd64_exception::can_recover_diagnostic_fault(rejected));
    rejected = contract; rejected.expectedFaultErrorCode = 0u;
    assert(!kernel::amd64_exception::can_recover_diagnostic_fault(rejected));
    rejected = contract; rejected.cr2++;
    assert(!kernel::amd64_exception::can_recover_diagnostic_fault(rejected));
    rejected = contract; rejected.frame = nullptr;
    assert(!kernel::amd64_exception::can_recover_diagnostic_fault(rejected));
    rejected = contract; frame.cs = 0x08u;
    assert(!kernel::amd64_exception::can_recover_diagnostic_fault(rejected));
    frame.cs = 0x23u;
    rejected = contract; frame.userSs = 0x10u;
    assert(!kernel::amd64_exception::can_recover_diagnostic_fault(rejected));
    frame.userSs = 0x1Bu;
    rejected = contract; frame.rip = kCode + 0x1000u;
    assert(!kernel::amd64_exception::can_recover_diagnostic_fault(rejected));
    frame.rip = kCode + 10u;
    rejected = contract; frame.userRsp = kStack - 8u;
    assert(!kernel::amd64_exception::can_recover_diagnostic_fault(rejected));
    frame.userRsp = kStack + 0xFF1u;
    assert(!kernel::amd64_exception::can_recover_diagnostic_fault(contract));
    frame.userRsp = kStack + 0xFF0u;
    rejected = contract; rejected.continuationRsp = kLifecycleStack + 0x800u;
    assert(!kernel::amd64_exception::can_recover_diagnostic_fault(rejected));
    rejected = contract; rejected.continuationRsp = kLifecycleStack + 0x4000u;
    assert(!kernel::amd64_exception::can_recover_diagnostic_fault(rejected));
    rejected = contract; rejected.continuationRip++;
    assert(!kernel::amd64_exception::can_recover_diagnostic_fault(rejected));
    rejected = contract; rejected.continuationRflags |= 1ull << 12u;
    assert(!kernel::amd64_exception::can_recover_diagnostic_fault(rejected));
    rejected = contract; rejected.kernelFsBase = 0x0001000000000000ull;
    assert(!kernel::amd64_exception::can_recover_diagnostic_fault(rejected));
    rejected = contract; rejected.taskState = TaskState::Exited;
    assert(!kernel::amd64_exception::can_recover_diagnostic_fault(rejected));

    FaultRecord record{};
    for (uint32_t word = 0u; word < sizeof(frame) / sizeof(uint64_t); ++word) {
        reinterpret_cast<uint64_t*>(&frame)[word] = 0x100u + word;
    }
    frame.vector = 14u;
    frame.errorCode = 4u;
    assert(kernel::amd64_exception::preserve_fault_record(
        &record, 0x522901u, frame, kGuard, kTaskCr3 | 0x18u));
    assert(record.taskId == 0x522901u && record.cr2 == kGuard);
    assert(record.taskCr3 == (kTaskCr3 | 0x18u));
    assert(memcmp(&record.frame, &frame, sizeof(frame)) == 0);
    assert(!kernel::amd64_exception::preserve_fault_record(
        &record, 0u, frame, kGuard, kTaskCr3));
    assert(!kernel::amd64_exception::preserve_fault_record(
        nullptr, 0x522901u, frame, kGuard, kTaskCr3));

    assert(kernel::amd64_protected_task::is_user_origin(0x23u));
    assert(!kernel::amd64_protected_task::is_user_origin(0x08u));
    assert(kernel::amd64_protected_task::can_transition(TaskState::Running,
                                                        TaskState::Faulted));
    assert(kernel::amd64_protected_task::can_transition(TaskState::Faulted,
                                                        TaskState::Reaped));
    assert(!kernel::amd64_protected_task::can_transition(TaskState::Running,
                                                         TaskState::Reaped));
    return 0;
}
