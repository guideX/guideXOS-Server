#include "include/kernel/protected_task.h"
#include "include/kernel/amd64_exception_contract.h"
#include "include/kernel/amd64_protected_task_contract.h"
#include "include/kernel/arch.h"
#include "include/kernel/interrupts.h"
#include "include/kernel/native_elf_fault.h"
#include "include/kernel/physical_frame_owner.h"
#include "include/kernel/serial_debug.h"
#include "include/kernel/task_address_space.h"

#if defined(__x86_64__) || defined(_M_X64)
extern "C" {
uint64_t gxos_protected_task_kernel_resume_rsp = 0u;
uint64_t gxos_protected_task_kernel_resume_rip = 0u;
uint64_t gxos_protected_task_kernel_resume_rflags = 0u;
uint64_t gxos_protected_task_kernel_fs_base = 0u;
uint64_t gxos_protected_task_kernel_gs_base = 0u;
extern uint8_t gxos_amd64_user_entry_stack_bottom[];
extern uint8_t gxos_amd64_user_entry_stack_top[];
extern uint8_t gxos_task_lifecycle_stack_bottom[];
extern uint8_t gxos_task_lifecycle_stack_top[];
extern uint8_t gxos_amd64_user_kernel_resume[];
}
#endif

namespace kernel {
namespace interrupts {
bool set_user_entry_stack(uint64_t stackBase, uint64_t stackBytes);
bool user_selectors_valid();
bool user_exit_gate_valid();
}
}

namespace kernel {
namespace protected_task {

#if defined(__x86_64__) || defined(_M_X64)

namespace {

static const uint64_t kCr3AddressMask = 0x000FFFFFFFFFF000ull;
static const uint64_t kExpectedUserDataValue = 42u;
static const uint64_t kExpectedUserCodeSelector =
    amd64_protected_task::kUserCodeSelector;
static const uint32_t kDiagnosticTaskIdBase = 0x522800u;
static const uint32_t kDiagnosticTaskCycles = 3u;
static const uint64_t kUserStackPointer =
    memory::kTaskUserStackAddress + memory::kOwnedFrameBytes - 16u;
static const uint64_t kUserExitRip = memory::kTaskUserCodeAddress + 50u;
static const uint64_t kPageFaultError = 0x4u;
static const uint64_t kGeneralProtectionError = (0x81u << 3u) | 0x2u;

enum class TerminalStatus : uint8_t {
    None = 0,
    Exited,
    Faulted
};

enum class DiagnosticKind : uint8_t {
    Normal = 0,
    PageFault,
    InvalidOpcode,
    GeneralProtection
};

struct ProtectedTaskDescriptor {
    uint32_t identity;
    amd64_protected_task::TaskState state;
    memory::TaskAddressSpace addressSpace;
    uint64_t kernelCr3;
    uint64_t codeFrame;
    uint64_t dataFrame;
    uint64_t stackFrame;
    uint64_t expectedExitRip;
    uint64_t expectedUserRsp;
    uint64_t observedExitRip;
    uint64_t observedExitCs;
    uint64_t observedFaultVector;
    uint64_t observedFaultError;
    uint64_t observedFaultRip;
    uint64_t observedFaultCr2;
    uint64_t expectedFaultVector;
    uint64_t expectedFaultError;
    uint64_t expectedFaultCr2;
    DiagnosticKind diagnosticKind;
    amd64_exception::FaultRecord savedFault;
    amd64_protected_task::UserExitFrame savedExitFrame;
    uint64_t userResult;
    TerminalStatus terminalStatus;
};

static ProtectedTaskDescriptor s_task{};
static ProtectedTaskDescriptor* s_activeTask = nullptr;
static bool s_cycleFailed = false;

extern "C" void gxos_amd64_call_on_lifecycle_stack(void (*entry)(void*), void* context);
extern "C" void gxos_amd64_enter_user(uint64_t entry, uint64_t userRsp);
static void halt_forever()
{
    arch::disable_interrupts();
    for (;;) arch::halt();
}

static bool transition_task(amd64_protected_task::TaskState next)
{
    if (!amd64_protected_task::can_transition(s_task.state, next)) return false;
    s_task.state = next;
    return true;
}

static bool same_accounting(const memory::FrameAccounting& a,
                            const memory::FrameAccounting& b)
{
    return a.capacityFrames == b.capacityFrames &&
           a.freeFrames == b.freeFrames &&
           a.reservedFrames == b.reservedFrames &&
           a.kernelOwnedFrames == b.kernelOwnedFrames &&
           a.sharedKernelFrames == b.sharedKernelFrames &&
           a.taskDataFrames == b.taskDataFrames &&
           a.taskCodeFrames == b.taskCodeFrames &&
           a.taskStackFrames == b.taskStackFrames &&
           a.pageTableFrames == b.pageTableFrames &&
           a.mappedTaskFrames == b.mappedTaskFrames;
}

static void log_address_space_status(const char* label,
                                     memory::AddressSpaceStatus status)
{
    serial::puts("[CPL3] ");
    serial::puts(label);
    serial::puts(" status=");
    serial::puts(memory::address_space_status_name(status));
    serial::putc('\n');
}

static void log_frame_status(const char* label, memory::FrameStatus status)
{
    serial::puts("[CPL3] ");
    serial::puts(label);
    serial::puts(" status=");
    serial::puts(memory::frame_status_name(status));
    serial::putc('\n');
}

static void write_u64(void* address, uint64_t value)
{
    uint8_t* bytes = static_cast<uint8_t*>(address);
    for (uint32_t i = 0u; i < 8u; ++i) {
        bytes[i] = static_cast<uint8_t>(value >> (i * 8u));
    }
}

static void fill_diagnostic_payload(uint8_t* code, DiagnosticKind kind)
{
    if (kind == DiagnosticKind::InvalidOpcode) {
        code[0] = 0x0Fu;
        code[1] = 0x0Bu; // UD2: processor-generated #UD at CPL3.
        return;
    }
    if (kind == DiagnosticKind::GeneralProtection) {
        code[0] = 0xCDu;
        code[1] = 0x81u; // Absent DPL0 IDT gate: processor-generated #GP(0x40A).
        return;
    }
    if (kind == DiagnosticKind::PageFault) {
        // Read the explicitly unmapped CPL3 guard page. The following UD2 is
        // an unexpected-path sentinel: eligibility requires the expected
        // #PF, so a successful read cannot be misreported as recovery.
        code[0] = 0x48u; code[1] = 0xBBu; // movabs rbx, imm64
        write_u64(code + 2u, memory::kTaskUserStackGuardAddress);
        code[10] = 0x48u; code[11] = 0x8Bu; code[12] = 0x03u; // mov rax,[rbx]
        code[13] = 0x0Fu; code[14] = 0x0Bu;
        return;
    }

    // movabs rbx, userData; mov rax,[rbx]; inc rax; mov [rbx],rax;
    // xor ecx,ecx; mov cx,cs; movabs rdx,userData+8; mov [rdx],rcx;
    // mov eax,exit-operation; mov edi,magic; int 0x80; ud2.
    static const uint8_t payload[] = {
        0x48u, 0xBBu, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u,
        0x48u, 0x8Bu, 0x03u,
        0x48u, 0x83u, 0xC0u, 0x01u,
        0x48u, 0x89u, 0x03u,
        0x31u, 0xC9u,
        0x66u, 0x8Cu, 0xC9u,
        0x48u, 0xBAu, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u,
        0x48u, 0x89u, 0x0Au,
        0xB8u, 0x01u, 0u, 0u, 0u,
        0xBFu, 0x28u, 0x52u, 0x32u, 0x53u,
        0xCDu, 0x80u,
        0x0Fu, 0x0Bu
    };
    for (uint32_t i = 0u; i < sizeof(payload); ++i) code[i] = payload[i];
    write_u64(code + 2u, memory::kTaskUserDataAddress);
    write_u64(code + 27u, memory::kTaskUserDataAddress + 8u);
}

static bool mapping_has_permissions(memory::TaskAddressSpace* space,
                                    uint64_t virtualAddress,
                                    bool writable,
                                    bool executable,
                                    const memory::AddressSpacePlatform& platform)
{
    memory::MappingPermissions permissions{};
    if (memory::query_task_mapping(space, virtualAddress, &permissions, platform) !=
        memory::AddressSpaceStatus::Ok) return false;
    return permissions.present && permissions.userAccessible &&
           permissions.writable == writable &&
           permissions.executable == executable;
}

static bool validate_mappings(const memory::AddressSpacePlatform& platform)
{
    if (!mapping_has_permissions(&s_task.addressSpace,
                                 memory::kTaskUserCodeAddress, false, true, platform) ||
        !mapping_has_permissions(&s_task.addressSpace,
                                 memory::kTaskUserDataAddress, true, false, platform) ||
        !mapping_has_permissions(&s_task.addressSpace,
                                 memory::kTaskUserStackAddress, true, false, platform)) {
        return false;
    }
    memory::MappingPermissions guard{};
    if (memory::query_task_mapping(&s_task.addressSpace,
            memory::kTaskUserStackGuardAddress, &guard, platform) !=
        memory::AddressSpaceStatus::MappingNotFound) {
        return false;
    }

    const uint64_t kernelAddress = reinterpret_cast<uintptr_t>(&s_task);
    memory::MappingPermissions kernelMapping{};
    if (memory::query_task_mapping(&s_task.addressSpace, kernelAddress,
                                   &kernelMapping, platform) !=
        memory::AddressSpaceStatus::Ok || !kernelMapping.present ||
        kernelMapping.userAccessible || kernelMapping.writable == false) {
        return false;
    }
    return memory::verify_task_access(&s_task.addressSpace, kernelAddress,
               memory::UserAccess::Read, platform) ==
           memory::AddressSpaceStatus::AccessDenied;
}

static bool release_unmapped_frame(uint64_t physical,
                                   memory::FrameKind kind,
                                   uint32_t taskId)
{
    if (physical == 0u) return true;
    memory::FrameInfo info{};
    const memory::FrameStatus query = memory::query_frame(physical, &info);
    if (query != memory::FrameStatus::Ok) return false;
    if (info.kind == memory::FrameKind::Free && info.taskId == 0u) return true;
    if (info.taskId != taskId || info.kind != kind || info.mapped) return false;
    return memory::release_task_frame(physical, kind, taskId) == memory::FrameStatus::Ok;
}

static bool cleanup_task(const memory::AddressSpacePlatform& platform,
                         const memory::FrameAccounting& baseline)
{
    if (s_task.addressSpace.initialized) {
        const memory::AddressSpaceStatus status =
            memory::destroy_task_address_space(&s_task.addressSpace, platform);
        if (status != memory::AddressSpaceStatus::Ok) {
            log_address_space_status("teardown", status);
            return false;
        }
    }
    if (!release_unmapped_frame(s_task.codeFrame, memory::FrameKind::TaskPrivateCode,
                                s_task.identity) ||
        !release_unmapped_frame(s_task.dataFrame, memory::FrameKind::TaskPrivateData,
                                s_task.identity) ||
        !release_unmapped_frame(s_task.stackFrame, memory::FrameKind::TaskPrivateStack,
                                s_task.identity)) {
        serial::puts("[CPL3] rollback status=owned-frame-not-released\n");
        return false;
    }

    memory::FrameAccounting after{};
    memory::get_frame_accounting(&after);
    if (!same_accounting(baseline, after)) {
        serial::puts("[CPL3] rollback status=frame-accounting-mismatch\n");
        return false;
    }
    if (s_task.state != amd64_protected_task::TaskState::Reaped) {
        if (!transition_task(amd64_protected_task::TaskState::Reaped)) return false;
    }
    return true;
}

static void cleanup_failure_or_halt(const memory::AddressSpacePlatform& platform,
                                    const memory::FrameAccounting& baseline)
{
    if (!cleanup_task(platform, baseline)) {
        serial::puts("[CPL3] cleanup failed; stopping diagnostic kernel path\n");
        halt_forever();
    }
}

static bool allocate_and_map(memory::FrameKind kind,
                             uint64_t virtualAddress,
                             uint64_t* outPhysical,
                             const memory::AddressSpacePlatform& platform)
{
    const memory::FrameStatus allocation =
        memory::allocate_task_frame(kind, s_task.identity, outPhysical);
    if (allocation != memory::FrameStatus::Ok) {
        log_frame_status("allocation", allocation);
        return false;
    }
    const memory::AddressSpaceStatus mapping = memory::map_task_frame(
        &s_task.addressSpace, virtualAddress, *outPhysical, platform);
    if (mapping != memory::AddressSpaceStatus::Ok) {
        log_address_space_status("mapping", mapping);
        return false;
    }
    return true;
}

static const char* diagnostic_name(DiagnosticKind kind)
{
    switch (kind) {
    case DiagnosticKind::PageFault: return "page-fault";
    case DiagnosticKind::InvalidOpcode: return "invalid-opcode";
    case DiagnosticKind::GeneralProtection: return "general-protection";
    default: return "normal-exit";
    }
}

static uint64_t expected_vector(DiagnosticKind kind)
{
    switch (kind) {
    case DiagnosticKind::PageFault: return 14u;
    case DiagnosticKind::InvalidOpcode: return 6u;
    case DiagnosticKind::GeneralProtection: return 13u;
    default: return 0u;
    }
}

static uint64_t expected_error(DiagnosticKind kind)
{
    switch (kind) {
    case DiagnosticKind::PageFault: return kPageFaultError;
    case DiagnosticKind::GeneralProtection: return kGeneralProtectionError;
    default: return 0u;
    }
}

static uint64_t expected_cr2(DiagnosticKind kind)
{
    return kind == DiagnosticKind::PageFault ?
        memory::kTaskUserStackGuardAddress : 0u;
}

static bool run_one_cycle(uint32_t cycle, uint32_t taskIdBase,
                          DiagnosticKind kind)
{
    memory::AddressSpacePlatform platform = memory::amd64_address_space_platform();
    memory::FrameAccounting baseline{};
    memory::get_frame_accounting(&baseline);

    s_task = ProtectedTaskDescriptor{};
    s_task.identity = taskIdBase + cycle;
    s_task.diagnosticKind = kind;
    s_task.expectedFaultVector = expected_vector(kind);
    s_task.expectedFaultError = expected_error(kind);
    s_task.expectedFaultCr2 = expected_cr2(kind);
    s_task.state = amd64_protected_task::TaskState::Empty;
    s_task.kernelCr3 = arch::read_cr3();
    if ((s_task.kernelCr3 & kCr3AddressMask) == 0u ||
        !transition_task(amd64_protected_task::TaskState::Created)) {
        serial::puts("[CPL3] task creation status=invalid-kernel-context\n");
        return false;
    }

    s_task.expectedExitRip = kUserExitRip;
    s_task.expectedUserRsp = kUserStackPointer;

    memory::AddressSpaceStatus addressStatus = memory::create_task_address_space(
        &s_task.addressSpace, s_task.identity,
        s_task.kernelCr3 & kCr3AddressMask, platform);
    if (addressStatus != memory::AddressSpaceStatus::Ok) {
        log_address_space_status("create", addressStatus);
        cleanup_failure_or_halt(platform, baseline);
        return false;
    }

    if (!allocate_and_map(memory::FrameKind::TaskPrivateCode,
                          memory::kTaskUserCodeAddress, &s_task.codeFrame, platform) ||
        !allocate_and_map(memory::FrameKind::TaskPrivateData,
                          memory::kTaskUserDataAddress, &s_task.dataFrame, platform) ||
        !allocate_and_map(memory::FrameKind::TaskPrivateStack,
                          memory::kTaskUserStackAddress, &s_task.stackFrame, platform)) {
        cleanup_failure_or_halt(platform, baseline);
        return false;
    }

    uint8_t* code = static_cast<uint8_t*>(memory::task_frame_pointer(
        s_task.codeFrame, memory::FrameKind::TaskPrivateCode, s_task.identity));
    uint64_t* data = static_cast<uint64_t*>(memory::task_frame_pointer(
        s_task.dataFrame, memory::FrameKind::TaskPrivateData, s_task.identity));
    if (!code || !data) {
        serial::puts("[CPL3] task creation status=frame-map-unavailable\n");
        cleanup_failure_or_halt(platform, baseline);
        return false;
    }
    fill_diagnostic_payload(code, kind);
    data[0] = 41u;
    data[1] = 0u;

    if (!validate_mappings(platform)) {
        serial::puts("[CPL3] task creation status=permission-validation-failed\n");
        cleanup_failure_or_halt(platform, baseline);
        return false;
    }
    amd64_protected_task::UserIretFrame userFrame{};
    if (!amd64_protected_task::build_user_iret_frame(
            memory::kTaskUserCodeAddress, kUserStackPointer, &userFrame) ||
        userFrame.cs != amd64_protected_task::kUserCodeSelector ||
        userFrame.ss != amd64_protected_task::kUserDataSelector ||
        userFrame.rflags != amd64_protected_task::kUserRflags) {
        serial::puts("[CPL3] task creation status=user-entry-frame-invalid\n");
        cleanup_failure_or_halt(platform, baseline);
        return false;
    }

    const uint64_t entryStackBase =
        reinterpret_cast<uintptr_t>(gxos_amd64_user_entry_stack_bottom);
    const uint64_t entryStackTop =
        reinterpret_cast<uintptr_t>(gxos_amd64_user_entry_stack_top);
    if (!interrupts::user_selectors_valid() ||
        !interrupts::user_exit_gate_valid() ||
        !interrupts::set_user_entry_stack(entryStackBase,
                                          entryStackTop - entryStackBase)) {
        serial::puts("[CPL3] task creation status=tss-or-selector-invalid\n");
        cleanup_failure_or_halt(platform, baseline);
        return false;
    }
    if (!transition_task(amd64_protected_task::TaskState::Ready) ||
        !transition_task(amd64_protected_task::TaskState::Running)) {
        serial::puts("[CPL3] task creation status=invalid-lifecycle-transition\n");
        cleanup_failure_or_halt(platform, baseline);
        return false;
    }

    s_activeTask = &s_task;
    const uint64_t taskCr3 = s_task.addressSpace.rootPhysicalAddress |
                             (s_task.kernelCr3 & 0x18u);
    arch::write_cr3(taskCr3);
    if ((arch::read_cr3() & kCr3AddressMask) !=
        (s_task.addressSpace.rootPhysicalAddress & kCr3AddressMask)) {
        arch::write_cr3(s_task.kernelCr3);
        s_activeTask = nullptr;
        s_task.terminalStatus = TerminalStatus::Faulted;
        (void)transition_task(amd64_protected_task::TaskState::Faulted);
        serial::puts("[CPL3] task activation status=cr3-write-mismatch\n");
        cleanup_failure_or_halt(platform, baseline);
        return false;
    }

    serial::puts("[CPL3] entry task=0x"); serial::put_hex32(s_task.identity);
    serial::puts(" scenario="); serial::puts(diagnostic_name(kind));
    serial::puts(" CR3=0x"); serial::put_hex64(arch::read_cr3() & kCr3AddressMask);
    serial::puts(" RIP=0x"); serial::put_hex64(memory::kTaskUserCodeAddress);
    serial::puts(" RSP=0x"); serial::put_hex64(kUserStackPointer);
    serial::puts(" CS=0x"); serial::put_hex16(amd64_protected_task::kUserCodeSelector);
    serial::puts(" SS=0x"); serial::put_hex16(amd64_protected_task::kUserDataSelector);
    serial::puts(" IF=0\n");
    gxos_amd64_enter_user(memory::kTaskUserCodeAddress, kUserStackPointer);

    if (kind != DiagnosticKind::Normal) {
        const uint64_t taskRootMask = s_task.addressSpace.rootPhysicalAddress & kCr3AddressMask;
        if (arch::read_cr3() != s_task.kernelCr3 || s_activeTask != nullptr ||
            s_task.state != amd64_protected_task::TaskState::Faulted ||
            s_task.terminalStatus != TerminalStatus::Faulted ||
            s_task.savedFault.taskId != s_task.identity ||
            (s_task.savedFault.taskCr3 & kCr3AddressMask) != taskRootMask ||
            s_task.savedFault.frame.vector != s_task.expectedFaultVector ||
            s_task.savedFault.frame.errorCode != s_task.expectedFaultError ||
            s_task.savedFault.frame.cs != amd64_protected_task::kUserCodeSelector ||
            s_task.savedFault.frame.userSs != amd64_protected_task::kUserDataSelector ||
            s_task.savedFault.cr2 != s_task.expectedFaultCr2) {
            serial::puts("[CPL3-FAULT] continuation status=validation-failed kernel=halt\n");
            cleanup_failure_or_halt(platform, baseline);
            return false;
        }
        s_task.observedFaultVector = s_task.savedFault.frame.vector;
        s_task.observedFaultError = s_task.savedFault.frame.errorCode;
        s_task.observedFaultRip = s_task.savedFault.frame.rip;
        s_task.observedFaultCr2 = s_task.savedFault.cr2;
        if (!cleanup_task(platform, baseline)) {
            serial::puts("[CPL3-FAULT] task teardown failed after exception\n");
            return false;
        }
        serial::puts("[CPL3-FAULT] recovered task=0x"); serial::put_hex32(s_task.identity);
        serial::puts(" vector=0x"); serial::put_hex64(s_task.observedFaultVector);
        serial::puts(" error=0x"); serial::put_hex64(s_task.observedFaultError);
        serial::puts(" RIP=0x"); serial::put_hex64(s_task.observedFaultRip);
        serial::puts(" CS=0x"); serial::put_hex64(s_task.savedFault.frame.cs);
        serial::puts(" RSP=0x"); serial::put_hex64(s_task.savedFault.frame.userRsp);
        serial::puts(" SS=0x"); serial::put_hex64(s_task.savedFault.frame.userSs);
        serial::puts(" CR2=0x"); serial::put_hex64(s_task.observedFaultCr2);
        serial::puts(" task-CR3=0x"); serial::put_hex64(s_task.savedFault.taskCr3 & kCr3AddressMask);
        serial::puts(" continuation=kernel cleanup=verified state=reaped\n");
        return true;
    }

    if (arch::read_cr3() != s_task.kernelCr3 || s_activeTask != nullptr ||
        s_task.state != amd64_protected_task::TaskState::Exited ||
        s_task.terminalStatus != TerminalStatus::Exited ||
        s_task.userResult != kExpectedUserDataValue ||
        s_task.observedExitCs != kExpectedUserCodeSelector ||
        s_task.observedExitRip != kUserExitRip) {
        serial::puts("[CPL3] return status=unexpected-kernel-state\n");
        cleanup_failure_or_halt(platform, baseline);
        return false;
    }

    if (!cleanup_task(platform, baseline)) {
        serial::puts("[CPL3] task teardown failed after user exit\n");
        return false;
    }
    serial::puts("[CPL3] exit task=0x"); serial::put_hex32(s_task.identity);
    serial::puts(" actual-CS=0x"); serial::put_hex64(s_task.observedExitCs);
    serial::puts(" result=0x"); serial::put_hex64(s_task.userResult);
    serial::puts(" cleanup=verified state=reaped\n");
    s_task = ProtectedTaskDescriptor{};
    return true;
}

static void run_diagnostic_cycles(void*)
{
    s_cycleFailed = false;
    for (uint32_t cycle = 0u; cycle < kDiagnosticTaskCycles; ++cycle) {
        if (!run_one_cycle(cycle, kDiagnosticTaskIdBase,
                           DiagnosticKind::Normal)) {
            s_cycleFailed = true;
            break;
        }
    }
}

#if defined(GXOS_ENABLE_CPL3_FAULT_RECOVERY_DIAGNOSTIC)
static void run_fault_recovery_cycles(void*)
{
    static const DiagnosticKind sequence[] = {
        DiagnosticKind::PageFault,
        DiagnosticKind::Normal,
        DiagnosticKind::InvalidOpcode,
        DiagnosticKind::Normal,
        DiagnosticKind::GeneralProtection,
        DiagnosticKind::Normal,
        DiagnosticKind::PageFault,
        DiagnosticKind::Normal
    };
    s_cycleFailed = false;
    for (uint32_t cycle = 0u;
         cycle < sizeof(sequence) / sizeof(sequence[0]); ++cycle) {
        if (!run_one_cycle(cycle, 0x522900u, sequence[cycle])) {
            s_cycleFailed = true;
            break;
        }
    }
}
#endif

} // namespace

extern "C" void gxos_amd64_user_exit_dispatch(
    const amd64_protected_task::UserExitFrame* frame)
{
    ProtectedTaskDescriptor* task = s_activeTask;
    if (!frame || !task || task != &s_task ||
        task->state != amd64_protected_task::TaskState::Running ||
        task->terminalStatus != TerminalStatus::None ||
        (arch::read_cr3() & kCr3AddressMask) !=
            (task->addressSpace.rootPhysicalAddress & kCr3AddressMask) ||
        !amd64_protected_task::valid_exit_request(
            *frame, task->expectedExitRip, task->expectedUserRsp)) {
        serial::puts("[CPL3] exit trap rejected; kernel halted\n");
        halt_forever();
    }

    uint64_t* data = static_cast<uint64_t*>(memory::task_frame_pointer(
        task->dataFrame, memory::FrameKind::TaskPrivateData, task->identity));
    if (!data || data[0] != kExpectedUserDataValue ||
        data[1] != kExpectedUserCodeSelector) {
        serial::puts("[CPL3] task result rejected; kernel halted\n");
        halt_forever();
    }

    task->observedExitRip = frame->rip;
    task->observedExitCs = frame->cs;
    task->savedExitFrame = *frame;
    task->userResult = data[0];
    task->terminalStatus = TerminalStatus::Exited;
    if (!transition_task(amd64_protected_task::TaskState::Exited)) {
        serial::puts("[CPL3] exit lifecycle transition rejected; kernel halted\n");
        halt_forever();
    }
    s_activeTask = nullptr;

    arch::write_cr3(task->kernelCr3);
    if (arch::read_cr3() != task->kernelCr3) {
        serial::puts("[CPL3] kernel CR3 restore failed; kernel halted\n");
        halt_forever();
    }
    serial::puts("[CPL3] trap origin=CPL3 vector=0x80 kernel-stack=verified CR3=restored\n");
}

extern "C" uint32_t gxos_amd64_record_user_fault(
    const amd64_exception::NormalizedFrame* frame,
    uint64_t cr2, uint64_t taskCr3)
{
    ProtectedTaskDescriptor* task = s_activeTask;
    if (!frame || !task || task != &s_task ||
        task->state != amd64_protected_task::TaskState::Running ||
        task->terminalStatus != TerminalStatus::None) {
        return 0u;
    }

    amd64_exception::RecoveryContract contract{};
    contract.frame = frame;
    contract.taskId = task->identity;
    contract.activeTaskId = s_activeTask->identity;
    contract.addressSpaceTaskId = task->addressSpace.taskId;
    contract.taskState = task->state;
    contract.activeCr3 = taskCr3;
    contract.taskRootCr3 = task->addressSpace.rootPhysicalAddress;
    contract.kernelCr3 = task->kernelCr3;
    contract.expectedFaultVector = task->expectedFaultVector;
    contract.expectedFaultErrorCode = task->expectedFaultError;
    contract.expectedFaultCr2 = task->expectedFaultCr2;
    contract.cr2 = cr2;
    contract.continuationRsp = gxos_protected_task_kernel_resume_rsp;
    contract.lifecycleStackBase = reinterpret_cast<uintptr_t>(
        gxos_task_lifecycle_stack_bottom);
    contract.lifecycleStackTop = reinterpret_cast<uintptr_t>(
        gxos_task_lifecycle_stack_top);
    contract.continuationRip = gxos_protected_task_kernel_resume_rip;
    contract.expectedContinuationRip = reinterpret_cast<uintptr_t>(
        gxos_amd64_user_kernel_resume);
    contract.continuationRflags = gxos_protected_task_kernel_resume_rflags;
    contract.kernelFsBase = gxos_protected_task_kernel_fs_base;
    contract.kernelGsBase = gxos_protected_task_kernel_gs_base;
    contract.userCodeBase = memory::kTaskUserCodeAddress;
    contract.userCodeBytes = memory::kOwnedFrameBytes;
    contract.userStackBase = memory::kTaskUserStackAddress;
    contract.userStackBytes = memory::kOwnedFrameBytes;
    if (!amd64_exception::can_recover_diagnostic_fault(contract) ||
        !amd64_exception::preserve_fault_record(
            &task->savedFault, task->identity, *frame, cr2, taskCr3)) {
        return 0u;
    }

    task->terminalStatus = TerminalStatus::Faulted;
    if (!transition_task(amd64_protected_task::TaskState::Faulted)) return 0u;
    s_activeTask = nullptr;
    arch::write_cr3(task->kernelCr3);
    if (arch::read_cr3() != task->kernelCr3) {
        serial::puts("[CPL3-FAULT] kernel CR3 restoration failed; kernel halted\n");
        halt_forever();
    }
    return task->identity;
}

extern "C" uint32_t gxos_amd64_protected_task_context_active()
{
    return s_activeTask != nullptr ? 1u : 0u;
}

bool run_cpl3_diagnostic()
{
#if defined(GXOS_ENABLE_CPL3_DIAGNOSTIC)
    if (!memory::frame_owner_initialized()) {
        serial::puts("[CPL3] status=frame-owner-unavailable skipped=1\n");
        return false;
    }
    if (!interrupts::user_selectors_valid()) {
        serial::puts("[CPL3] status=user-selectors-invalid skipped=1\n");
        return false;
    }
    gxos_amd64_call_on_lifecycle_stack(&run_diagnostic_cycles, nullptr);
    if (s_cycleFailed) {
        serial::puts("[CPL3] status=failed cycles=bounded\n");
        return false;
    }
    serial::puts("[CPL3] status=passed cycles=3 nonfaulting=1 preemption=0\n");
    return true;
#else
    serial::puts("[CPL3] status=disabled\n");
    return false;
#endif
}

bool run_cpl3_fault_recovery_diagnostic()
{
#if defined(GXOS_ENABLE_CPL3_DIAGNOSTIC) && \
    defined(GXOS_ENABLE_CPL3_FAULT_RECOVERY_DIAGNOSTIC)
    if (!memory::frame_owner_initialized() ||
        !interrupts::user_selectors_valid()) {
        serial::puts("[CPL3-FAULT] status=prerequisite-unavailable skipped=1\n");
        return false;
    }
    gxos_amd64_call_on_lifecycle_stack(&run_fault_recovery_cycles, nullptr);
    if (s_cycleFailed) {
        serial::puts("[CPL3-FAULT] status=failed sequence=bounded\n");
        return false;
    }
    serial::puts("[CPL3-FAULT] status=passed page-fault=2 invalid-opcode=1 general-protection=1 normal-after-each=1 preemption=0\n");
    return true;
#else
    return false;
#endif
}

#else

bool run_cpl3_diagnostic()
{
    return false;
}

bool run_cpl3_fault_recovery_diagnostic()
{
    return false;
}

#endif

} // namespace protected_task
} // namespace kernel
