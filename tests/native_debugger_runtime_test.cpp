#include "native_app_debugger.h"
#include "executable_memory.h"
#include "allocator.h"
#include "platform.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>

namespace gxos {
PlatformInfo queryPlatform() {
    PlatformInfo info{};
    info.cpuCount = std::thread::hardware_concurrency();
    info.totalMemBytes = 512ull * 1024 * 1024;
    info.startTicks = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
    return info;
}

uint64_t ticks() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}
}

namespace {

using namespace gxos::apps;

bool waitFor(const std::atomic<bool>& value) {
    for (uint32_t i = 0; i < 1000000 && !value.load(std::memory_order_acquire); ++i)
        std::this_thread::yield();
    return value.load(std::memory_order_acquire);
}

gx_development_debug_request makeRequest(uint32_t command, uint64_t breakpointId,
                                          uint64_t targetAddress, uint64_t processId = 42,
                                          uint64_t runtimeId = 77) {
    gx_development_debug_request request{};
    request.size = sizeof(request);
    request.version = GX_DEVELOPMENT_DEBUG_API_VERSION;
    request.command = command;
    request.handle = 1;
    request.sessionGeneration = 9;
    request.processId = processId;
    request.nativeRuntimeId = runtimeId;
    request.breakpointId = breakpointId;
    request.targetAddress = targetAddress;
    request.artifactSha256 = "native-debugger-runtime-proof";
    return request;
}

bool pollForTrapFor(uint32_t expectedKind, gx_development_debug_snapshot& snapshot,
                    uint64_t processId, uint64_t runtimeId);

bool pollForTrap(uint32_t expectedKind, gx_development_debug_snapshot& snapshot) {
    return pollForTrapFor(expectedKind, snapshot, 42, 77);
}

bool pollForTrapFor(uint32_t expectedKind, gx_development_debug_snapshot& snapshot,
                    uint64_t processId, uint64_t runtimeId) {
    for (uint32_t i = 0; i < 1000000; ++i) {
        gx_development_debug_request poll = makeRequest(GX_DEVELOPMENT_DEBUG_POLL, 0, 0, processId, runtimeId);
        if (NativeAppDebugger::Command(poll, "native-debugger-runtime-proof", &snapshot) != gxos::apps::GX_OK) return false;
        if (snapshot.status == GX_DEVELOPMENT_DEBUG_STATUS_TRAP && snapshot.trapKind == expectedKind) return true;
        std::this_thread::yield();
    }
    return false;
}

bool expect(bool condition, const char* message) {
    if (condition) return true;
    std::cerr << "Native debugger runtime test FAIL: " << message << "\n";
    return false;
}

} // namespace

int main() {
#ifndef _WIN32
    std::cout << "Native debugger runtime test SKIP: Windows VEH is required\n";
    return 0;
#else
    gxos::Allocator::init(4 * 1024 * 1024);
    gxos::Allocator::setCurrentPid(0);

    ExecutableMemoryBlock mapping;
    std::string error;
    if (!expect(ExecutableMemory::Allocate(4096, mapping, error), "executable memory allocation")) return 1;
    bool registered = false;
    std::atomic<bool> waiting{false};
    std::atomic<bool> completed{false};
    volatile uint32_t* counter = nullptr;
    std::thread targetThread;
    auto cleanup = [&]() {
        if (targetThread.joinable()) {
            if (!completed.load(std::memory_order_acquire)) {
                gx_development_debug_snapshot cancelSnapshot{};
                gx_development_debug_request cancel = makeRequest(GX_DEVELOPMENT_DEBUG_CANCEL_EXECUTION, 0, 0);
                NativeAppDebugger::Command(cancel, "native-debugger-runtime-proof", &cancelSnapshot);
            }
            targetThread.join();
        }
        if (registered) NativeAppDebugger::UnregisterRuntime(77);
        if (counter) { delete counter; counter = nullptr; }
        ExecutableMemory::Free(mapping);
    };

    const uint64_t imageBase = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(mapping.base));
    const uint64_t targetAddress = imageBase + 10;
    // MOV RAX, counter; INC dword ptr [RAX]; RET. The breakpoint is placed on
    // the two-byte INC instruction so its observable counter effect is tied
    // directly to the restored instruction.
    uint8_t* code = static_cast<uint8_t*>(mapping.base);
    counter = new volatile uint32_t(0);
    *counter = 0;
    code[0] = 0x48;
    code[1] = 0xB8;
    const uint64_t counterAddress = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(counter));
    std::memcpy(code + 2, &counterAddress, sizeof(counterAddress));
    code[10] = 0xFF;
    code[11] = 0x00;
    code[12] = 0xC3;
    volatile uint8_t* breakpointByte = code + 10;
    const uint8_t originalByte = code[10];
    if (!expect(ExecutableMemory::Protect(mapping, 0, 1, ExecutableMemoryProtection::ReadExecute, error),
                "initial executable protection")) {
        cleanup();
        return 1;
    }

    NativeAppRuntimeContext context{};
    context.runtimeId = 77;
    context.processId = 42;
    // The direct VEH harness does not run through BeginHostCallDispatch, so
    // provide the same owned-range contract with a broad test-only range.
    // The production runtime narrows this to the active target thread stack.
    context.nativeStackBaseAddress = 0x1000;
    context.nativeStackEndAddress = 0x0000800000000000ull;
    NativeElfImage image{};
    image.preferredBaseAddress = imageBase;
    image.imageSize = mapping.size;
    NativeElfSegment segment{};
    segment.virtualAddress = imageBase;
    segment.memorySize = mapping.size;
    segment.flags = 1;
    image.loadedSegments.push_back(segment);
    if (!expect(NativeAppDebugger::RegisterRuntime(context, mapping, image, true, error),
                "debug runtime registration")) {
        cleanup();
        return 1;
    }
    registered = true;

    gx_development_debug_snapshot rejectedSnapshot{};
    gx_development_debug_request wrongArtifact = makeRequest(GX_DEVELOPMENT_DEBUG_BIND_SOFTWARE_BREAKPOINT, 899, targetAddress);
    wrongArtifact.artifactSha256 = "wrong-artifact";
    if (!expect(NativeAppDebugger::Command(wrongArtifact, "native-debugger-runtime-proof", &rejectedSnapshot) == gxos::apps::GX_ERROR_FAILED &&
                rejectedSnapshot.status == GX_DEVELOPMENT_DEBUG_STATUS_REJECTED,
                "stale artifact rejection")) {
        cleanup();
        return 1;
    }
    gx_development_debug_request outsideImage = makeRequest(GX_DEVELOPMENT_DEBUG_BIND_SOFTWARE_BREAKPOINT, 898,
                                                            targetAddress + mapping.size);
    if (!expect(NativeAppDebugger::Command(outsideImage, "native-debugger-runtime-proof", &rejectedSnapshot) == gxos::apps::GX_ERROR_FAILED &&
                rejectedSnapshot.status == GX_DEVELOPMENT_DEBUG_STATUS_REJECTED,
                "non-executable address rejection")) {
        cleanup();
        return 1;
    }

    gx_development_debug_snapshot bindSnapshot{};
    gx_development_debug_request bind = makeRequest(GX_DEVELOPMENT_DEBUG_BIND_SOFTWARE_BREAKPOINT, 900, targetAddress);
    if (!expect(NativeAppDebugger::Command(bind, "native-debugger-runtime-proof", &bindSnapshot) == gxos::apps::GX_OK &&
                bindSnapshot.status == GX_DEVELOPMENT_DEBUG_STATUS_BOUND && bindSnapshot.bindingInstalled != 0 &&
                bindSnapshot.originalByte == originalByte && bindSnapshot.installedByte == 0xCC,
                "INT3 binding and original-byte capture")) {
        cleanup();
        return 1;
    }
    gx_development_debug_snapshot duplicateSnapshot{};
    gx_development_debug_request duplicate = makeRequest(GX_DEVELOPMENT_DEBUG_BIND_SOFTWARE_BREAKPOINT, 901, targetAddress);
    if (!expect(NativeAppDebugger::Command(duplicate, "native-debugger-runtime-proof", &duplicateSnapshot) == gxos::apps::GX_OK &&
                duplicateSnapshot.status == GX_DEVELOPMENT_DEBUG_STATUS_BOUND &&
                duplicateSnapshot.bindingId == bindSnapshot.bindingId && duplicateSnapshot.bindingCount == 2,
                "duplicate logical breakpoint shares one physical binding")) {
        cleanup();
        return 1;
    }
    if (!expect(*breakpointByte == 0xCC, "patched memory contains INT3")) {
        cleanup();
        return 1;
    }

    targetThread = std::thread([&]() {
        gxos::Allocator::setCurrentPid(42);
        waiting.store(true, std::memory_order_release);
        typedef void (*TargetFunction)();
        for (uint32_t invocation = 0; invocation < 2; ++invocation) {
            if (!NativeAppDebugger::WaitForExecutionGate(77)) return;
            reinterpret_cast<TargetFunction>(mapping.base)();
        }
        completed.store(true, std::memory_order_release);
        gxos::Allocator::setCurrentPid(0);
    });
    if (!expect(waitFor(waiting), "target reached the closed execution gate")) {
        cleanup();
        return 1;
    }

    gx_development_debug_snapshot releaseSnapshot{};
    gx_development_debug_request release = makeRequest(GX_DEVELOPMENT_DEBUG_RELEASE_EXECUTION, 0, 0);
    if (!expect(NativeAppDebugger::Command(release, "native-debugger-runtime-proof", &releaseSnapshot) == gxos::apps::GX_OK &&
                releaseSnapshot.status == GX_DEVELOPMENT_DEBUG_STATUS_READY,
                "execution gate release")) {
        cleanup();
        return 1;
    }
    gx_development_debug_snapshot trapSnapshot{};
    const bool trapObserved = pollForTrap(GX_DEVELOPMENT_DEBUG_TRAP_BREAKPOINT, trapSnapshot);
    if (!(trapObserved && trapSnapshot.trapKind == GX_DEVELOPMENT_DEBUG_TRAP_BREAKPOINT &&
                trapSnapshot.processId == 42 && trapSnapshot.nativeRuntimeId == 77 && trapSnapshot.threadId != 0 &&
                trapSnapshot.targetAddress == targetAddress &&
                (trapSnapshot.instructionPointer == targetAddress || trapSnapshot.instructionPointer == targetAddress + 1) &&
                trapSnapshot.bindingId == bindSnapshot.bindingId && trapSnapshot.context.valid != 0 &&
                trapSnapshot.context.stopGeneration != 0 &&
                trapSnapshot.context.threadId == trapSnapshot.threadId)) {
        std::cerr << "Trap snapshot status=" << trapSnapshot.status
                  << " kind=" << trapSnapshot.trapKind
                  << " process=" << trapSnapshot.processId
                  << " runtime=" << trapSnapshot.nativeRuntimeId
                  << " thread=" << trapSnapshot.threadId
                  << " ip=0x" << std::hex << trapSnapshot.instructionPointer
                  << " target=0x" << trapSnapshot.targetAddress
                  << " binding=" << std::dec << trapSnapshot.bindingId
                  << " expectedTarget=0x" << std::hex << targetAddress
                  << " expectedBinding=" << std::dec << bindSnapshot.bindingId << "\n";
        expect(false, "real EXCEPTION_BREAKPOINT routing and RIP normalization");
        cleanup();
        return 1;
    }
    if (!expect(!completed.load(std::memory_order_acquire), "faulting target thread remains stopped")) {
        cleanup();
        return 1;
    }

    gx_development_debug_request stackRead = makeRequest(GX_DEVELOPMENT_DEBUG_READ_MEMORY, 0,
                                                          trapSnapshot.context.rbp);
    stackRead.threadId = trapSnapshot.threadId;
    stackRead.stopGeneration = trapSnapshot.context.stopGeneration;
    stackRead.readByteCount = 16;
    gx_development_debug_snapshot stackReadSnapshot{};
    if (!expect(NativeAppDebugger::Command(stackRead, "native-debugger-runtime-proof",
                                           &stackReadSnapshot) == gxos::apps::GX_OK &&
                stackReadSnapshot.status == GX_DEVELOPMENT_DEBUG_STATUS_READY &&
                stackReadSnapshot.byteCount == 16,
                "stopped target stack read returns exactly one frame link")) {
        cleanup();
        return 1;
    }
    gx_development_debug_request staleStackRead = stackRead;
    staleStackRead.stopGeneration++;
    gx_development_debug_snapshot staleStackSnapshot{};
    if (!expect(NativeAppDebugger::Command(staleStackRead, "native-debugger-runtime-proof",
                                           &staleStackSnapshot) == gxos::apps::GX_ERROR_FAILED &&
                staleStackSnapshot.status == GX_DEVELOPMENT_DEBUG_STATUS_REJECTED,
                "stale stopped stack read is rejected")) {
        cleanup();
        return 1;
    }
    gx_development_debug_request outsideStackRead = stackRead;
    outsideStackRead.targetAddress = 0x0000800000000000ull - 8;
    gx_development_debug_snapshot outsideStackSnapshot{};
    if (!expect(NativeAppDebugger::Command(outsideStackRead, "native-debugger-runtime-proof",
                                           &outsideStackSnapshot) == gxos::apps::GX_ERROR_FAILED &&
                outsideStackSnapshot.status == GX_DEVELOPMENT_DEBUG_STATUS_REJECTED,
                "stopped target stack read outside the owned range is rejected")) {
        cleanup();
        return 1;
    }

    std::cout << "Breakpoint address: 0x" << std::hex << targetAddress
              << " original byte: 0x" << static_cast<uint32_t>(originalByte)
              << " INT3 byte: 0xCC raw breakpoint RIP: 0x" << trapSnapshot.instructionPointer
              << " normalized RIP: 0x" << trapSnapshot.targetAddress
              << " delta=" << std::dec << (static_cast<int64_t>(trapSnapshot.instructionPointer) -
                                             static_cast<int64_t>(trapSnapshot.targetAddress))
              << " RFLAGS=0x" << std::hex << trapSnapshot.context.rflags
              << " RSP=0x" << trapSnapshot.context.rsp
              << " RBP=0x" << trapSnapshot.context.rbp << std::dec << "\n";

    auto continueBreakpoint = [&](const gx_development_debug_snapshot& stop,
                                  uint64_t breakpointId) {
        gx_development_debug_request request = makeRequest(GX_DEVELOPMENT_DEBUG_CONTINUE_BREAKPOINT,
                                                            breakpointId, targetAddress);
        request.flags = GX_DEVELOPMENT_DEBUG_FLAG_REINSTALL_BREAKPOINT;
        request.threadId = stop.threadId;
        request.stopGeneration = stop.context.stopGeneration;
        gx_development_debug_snapshot result{};
        const bool accepted = NativeAppDebugger::Command(request, "native-debugger-runtime-proof", &result) == gxos::apps::GX_OK &&
            result.status == GX_DEVELOPMENT_DEBUG_STATUS_SINGLE_STEP_PENDING &&
            *breakpointByte == originalByte;
        if (!accepted) {
            std::cerr << "Continue status=" << result.status << " error=" << result.errorMessage << "\n";
        }
        return accepted;
    };

    gx_development_debug_snapshot singleStepSnapshot{};
    if (!expect(continueBreakpoint(trapSnapshot, 900), "restore byte, rewind RIP, and enable internal single-step")) {
        cleanup();
        return 1;
    }
    if (!expect(pollForTrap(GX_DEVELOPMENT_DEBUG_TRAP_SINGLE_STEP, singleStepSnapshot),
                "real EXCEPTION_SINGLE_STEP observed")) {
        cleanup();
        return 1;
    }
    if (!expect(singleStepSnapshot.context.valid != 0 && singleStepSnapshot.threadId == trapSnapshot.threadId &&
                singleStepSnapshot.context.rip == targetAddress + 2 &&
                singleStepSnapshot.rflagsWithTrapFlag == (singleStepSnapshot.rflagsBeforeStep | 0x100u) &&
                singleStepSnapshot.rflagsAfterTrapFlagClear == singleStepSnapshot.rflagsBeforeStep &&
                *breakpointByte == 0xCC && *counter == 1,
                "original instruction executes once and breakpoint is rebound")) {
        std::cerr << "single-step rip=0x" << std::hex << singleStepSnapshot.context.rip
                  << " expected=0x" << (targetAddress + 2)
                  << " flags=0x" << singleStepSnapshot.context.rflags
                  << " flagsBefore=0x" << singleStepSnapshot.rflagsBeforeStep
                  << " flagsWithTF=0x" << singleStepSnapshot.rflagsWithTrapFlag
                  << " flagsAfter=0x" << singleStepSnapshot.rflagsAfterTrapFlagClear
                  << " byte=0x" << static_cast<uint32_t>(*breakpointByte)
                  << " counter=" << std::dec << *counter << "\n";
        cleanup();
        return 1;
    }
    std::cout << "RFLAGS before=0x" << std::hex << singleStepSnapshot.rflagsBeforeStep
              << " with TF=0x" << singleStepSnapshot.rflagsWithTrapFlag
              << " after clear=0x" << singleStepSnapshot.rflagsAfterTrapFlagClear
              << " single-step RIP=0x" << singleStepSnapshot.context.rip << std::dec << "\n";

    gx_development_debug_snapshot secondTrap{};
    if (!expect(pollForTrap(GX_DEVELOPMENT_DEBUG_TRAP_BREAKPOINT, secondTrap),
                "same breakpoint traps again after continuation")) {
        cleanup();
        return 1;
    }
    if (!expect(secondTrap.context.stopGeneration > trapSnapshot.context.stopGeneration &&
                secondTrap.threadId == trapSnapshot.threadId && secondTrap.bindingId == trapSnapshot.bindingId,
                "second stop has a new generation and the same target thread/binding")) {
        cleanup();
        return 1;
    }

    gx_development_debug_request sourceStep = makeRequest(GX_DEVELOPMENT_DEBUG_STEP_INSTRUCTION, 900, targetAddress);
    sourceStep.flags = GX_DEVELOPMENT_DEBUG_FLAG_REINSTALL_BREAKPOINT;
    sourceStep.threadId = secondTrap.threadId;
    sourceStep.stopGeneration = secondTrap.context.stopGeneration;
    gx_development_debug_snapshot sourceStepPending{};
    if (!expect(NativeAppDebugger::Command(sourceStep, "native-debugger-runtime-proof", &sourceStepPending) == gxos::apps::GX_OK &&
                sourceStepPending.status == GX_DEVELOPMENT_DEBUG_STATUS_SINGLE_STEP_PENDING &&
                sourceStepPending.singleStepKind == GX_DEVELOPMENT_DEBUG_SINGLE_STEP_USER_SOURCE &&
                *breakpointByte == originalByte,
                "user source-step from a breakpoint restores the original byte")) {
        cleanup();
        return 1;
    }
    gx_development_debug_snapshot userStepOne{};
    if (!expect(pollForTrap(GX_DEVELOPMENT_DEBUG_TRAP_SINGLE_STEP, userStepOne) &&
                userStepOne.singleStepKind == GX_DEVELOPMENT_DEBUG_SINGLE_STEP_USER_SOURCE &&
                userStepOne.context.rip == targetAddress + 2 && *breakpointByte == 0xCC && *counter == 2,
                "user source-step observes a real instruction and rebinds the breakpoint")) {
        cleanup();
        return 1;
    }
    std::cout << "User step #1 RIP=0x" << std::hex << userStepOne.context.rip
              << " RFLAGS=0x" << userStepOne.context.rflags << std::dec << "\n";

    gx_development_debug_request sourceStepTwo = makeRequest(GX_DEVELOPMENT_DEBUG_STEP_INSTRUCTION, 0, 0);
    sourceStepTwo.threadId = userStepOne.threadId;
    sourceStepTwo.stopGeneration = userStepOne.context.stopGeneration;
    gx_development_debug_snapshot sourceStepTwoPending{};
    if (!expect(NativeAppDebugger::Command(sourceStepTwo, "native-debugger-runtime-proof", &sourceStepTwoPending) == gxos::apps::GX_OK &&
                sourceStepTwoPending.status == GX_DEVELOPMENT_DEBUG_STATUS_SINGLE_STEP_PENDING &&
                sourceStepTwoPending.singleStepKind == GX_DEVELOPMENT_DEBUG_SINGLE_STEP_USER_SOURCE,
                "repeated user source-step request is accepted on the same stopped thread")) {
        cleanup();
        return 1;
    }
    gx_development_debug_snapshot userStepTwo{};
    if (!expect(pollForTrap(GX_DEVELOPMENT_DEBUG_TRAP_SINGLE_STEP, userStepTwo) &&
                userStepTwo.singleStepKind == GX_DEVELOPMENT_DEBUG_SINGLE_STEP_USER_SOURCE &&
                userStepTwo.context.threadId == userStepOne.context.threadId,
                "second user source-step observes a real EXCEPTION_SINGLE_STEP")) {
        cleanup();
        return 1;
    }
    std::cout << "User step #2 RIP=0x" << std::hex << userStepTwo.context.rip
              << " RFLAGS=0x" << userStepTwo.context.rflags << std::dec << "\n";
    gx_development_debug_request resumeStep = makeRequest(GX_DEVELOPMENT_DEBUG_RESUME_STEP, 0, 0);
    resumeStep.threadId = userStepTwo.threadId;
    resumeStep.stopGeneration = userStepTwo.context.stopGeneration;
    gx_development_debug_snapshot resumeStepResult{};
    if (!expect(NativeAppDebugger::Command(resumeStep, "native-debugger-runtime-proof", &resumeStepResult) == gxos::apps::GX_OK &&
                resumeStepResult.status == GX_DEVELOPMENT_DEBUG_STATUS_READY,
                "source-step resume releases the stopped target")) {
        cleanup();
        return 1;
    }
    targetThread.join();
    if (!expect(completed.load(std::memory_order_acquire) && *counter == 2,
                "target resumed normally after the repeated-hit proof")) {
        cleanup();
        return 1;
    }
    if (!expect(*breakpointByte == 0xCC,
                "breakpoint remains physically installed after the second step")) {
        cleanup();
        return 1;
    }
    NativeAppDebugger::UnregisterRuntime(77);
    registered = false;
    if (!expect(*breakpointByte == originalByte,
                "teardown restores the original instruction byte")) {
        ExecutableMemory::Free(mapping);
        return 1;
    }

    // Phase 6 machine-level proof: a real CALL executes its callee at full
    // speed and traps only at the temporary return address. The user
    // breakpoint at the call site is shared/rebound rather than replaced.
    ExecutableMemoryBlock stepOverMapping;
    if (!expect(ExecutableMemory::Allocate(4096, stepOverMapping, error),
                "Step Over executable memory allocation")) return 1;
    volatile uint32_t* stepOverCounter = new volatile uint32_t(0);
    uint8_t* stepOverCode = static_cast<uint8_t*>(stepOverMapping.base);
    const uint64_t stepOverBase = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(stepOverMapping.base));
    const uint64_t stepOverCall = stepOverBase;
    const uint64_t stepOverReturn = stepOverBase + 5;
    const uint64_t stepOverCallee = stepOverBase + 32;
    stepOverCode[0] = 0xE8;
    const int64_t relative = static_cast<int64_t>(stepOverCallee) - static_cast<int64_t>(stepOverCall + 5);
    const int32_t relative32 = static_cast<int32_t>(relative);
    std::memcpy(stepOverCode + 1, &relative32, sizeof(relative32));
    stepOverCode[5] = 0xC3;
    stepOverCode[32] = 0x48;
    stepOverCode[33] = 0xB8;
    const uint64_t stepOverCounterAddress = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(stepOverCounter));
    std::memcpy(stepOverCode + 34, &stepOverCounterAddress, sizeof(stepOverCounterAddress));
    stepOverCode[42] = 0xFF;
    stepOverCode[43] = 0x00;
    stepOverCode[44] = 0xC3;
    const uint8_t returnOriginalByte = stepOverCode[5];
    if (!expect(ExecutableMemory::Protect(stepOverMapping, 0, 1,
                                          ExecutableMemoryProtection::ReadExecute, error),
                "Step Over executable protection")) {
        delete stepOverCounter;
        ExecutableMemory::Free(stepOverMapping);
        return 1;
    }
    NativeAppRuntimeContext stepOverContext{};
    stepOverContext.runtimeId = 78;
    stepOverContext.processId = 43;
    stepOverContext.nativeStackBaseAddress = 0x1000;
    stepOverContext.nativeStackEndAddress = 0x0000800000000000ull;
    NativeElfImage stepOverImage{};
    stepOverImage.preferredBaseAddress = stepOverBase;
    stepOverImage.imageSize = stepOverMapping.size;
    NativeElfSegment stepOverSegment{};
    stepOverSegment.virtualAddress = stepOverBase;
    stepOverSegment.memorySize = stepOverMapping.size;
    stepOverSegment.flags = 1;
    stepOverImage.loadedSegments.push_back(stepOverSegment);
    if (!expect(NativeAppDebugger::RegisterRuntime(stepOverContext, stepOverMapping, stepOverImage, true, error),
                "Step Over runtime registration")) {
        delete stepOverCounter;
        ExecutableMemory::Free(stepOverMapping);
        return 1;
    }
    std::atomic<bool> stepOverCompleted{false};
    std::atomic<bool> stepOverWaiting{false};
    std::thread stepOverThread([&]() {
        gxos::Allocator::setCurrentPid(43);
        stepOverWaiting.store(true, std::memory_order_release);
        typedef void (*StepOverFunction)();
        if (NativeAppDebugger::WaitForExecutionGate(78))
            reinterpret_cast<StepOverFunction>(stepOverMapping.base)();
        stepOverCompleted.store(true, std::memory_order_release);
        gxos::Allocator::setCurrentPid(0);
    });
    if (!expect(waitFor(stepOverWaiting), "Step Over target reached the closed execution gate")) {
        gx_development_debug_snapshot cancel{};
        NativeAppDebugger::Command(makeRequest(GX_DEVELOPMENT_DEBUG_CANCEL_EXECUTION, 0, 0, 43, 78),
                                   "native-debugger-runtime-proof", &cancel);
        stepOverThread.join();
        NativeAppDebugger::UnregisterRuntime(78);
        delete stepOverCounter;
        ExecutableMemory::Free(stepOverMapping);
        return 1;
    }
    gx_development_debug_snapshot stepOverUserBind{};
    gx_development_debug_request stepOverUserBindRequest = makeRequest(
        GX_DEVELOPMENT_DEBUG_BIND_SOFTWARE_BREAKPOINT, 910, stepOverCall, 43, 78);
    if (!expect(NativeAppDebugger::Command(stepOverUserBindRequest,
                                           "native-debugger-runtime-proof", &stepOverUserBind) == gxos::apps::GX_OK &&
                stepOverUserBind.status == GX_DEVELOPMENT_DEBUG_STATUS_BOUND && stepOverCode[0] == 0xCC,
                "Step Over call-site user breakpoint bind")) {
        gx_development_debug_snapshot cancel{};
        NativeAppDebugger::Command(makeRequest(GX_DEVELOPMENT_DEBUG_CANCEL_EXECUTION, 0, 0, 43, 78),
                                    "native-debugger-runtime-proof", &cancel);
        stepOverThread.join();
        NativeAppDebugger::UnregisterRuntime(78);
        delete stepOverCounter;
        ExecutableMemory::Free(stepOverMapping);
        return 1;
    }
    gx_development_debug_snapshot stepOverTempBind{};
    const uint64_t stepOverOwner = 0x8000000000000091ull;
    gx_development_debug_request stepOverTempBindRequest = makeRequest(
        GX_DEVELOPMENT_DEBUG_BIND_SOFTWARE_BREAKPOINT, stepOverOwner, stepOverReturn, 43, 78);
    if (!expect(NativeAppDebugger::Command(stepOverTempBindRequest,
                                           "native-debugger-runtime-proof", &stepOverTempBind) == gxos::apps::GX_OK &&
                stepOverTempBind.status == GX_DEVELOPMENT_DEBUG_STATUS_BOUND &&
                stepOverTempBind.bindingId != stepOverUserBind.bindingId && stepOverTempBind.bindingCount == 1,
                "Step Over return breakpoint uses the shared physical binding manager")) {
        gx_development_debug_snapshot cancel{};
        NativeAppDebugger::Command(makeRequest(GX_DEVELOPMENT_DEBUG_CANCEL_EXECUTION, 0, 0, 43, 78),
                                    "native-debugger-runtime-proof", &cancel);
        stepOverThread.join();
        NativeAppDebugger::UnregisterRuntime(78);
        delete stepOverCounter;
        ExecutableMemory::Free(stepOverMapping);
        return 1;
    }
    gx_development_debug_snapshot stepOverRelease{};
    if (!expect(NativeAppDebugger::Command(makeRequest(GX_DEVELOPMENT_DEBUG_RELEASE_EXECUTION, 0, 0, 43, 78),
                                           "native-debugger-runtime-proof", &stepOverRelease) == gxos::apps::GX_OK,
                "Step Over execution release")) return 1;
    gx_development_debug_snapshot stepOverCallTrap{};
    if (!expect(pollForTrapFor(GX_DEVELOPMENT_DEBUG_TRAP_BREAKPOINT, stepOverCallTrap, 43, 78) &&
                stepOverCallTrap.targetAddress == stepOverCall && stepOverCallTrap.context.valid != 0,
                "real user breakpoint trap at the Step Over call site")) return 1;
    gx_development_debug_request stepOverCallRequest = makeRequest(
        GX_DEVELOPMENT_DEBUG_STEP_OVER_CALL, stepOverOwner, stepOverCall, 43, 78);
    stepOverCallRequest.auxiliaryAddress = stepOverReturn;
    stepOverCallRequest.threadId = stepOverCallTrap.threadId;
    stepOverCallRequest.stopGeneration = stepOverCallTrap.context.stopGeneration;
    gx_development_debug_snapshot stepOverCallResult{};
    if (!expect(NativeAppDebugger::Command(stepOverCallRequest,
                                           "native-debugger-runtime-proof", &stepOverCallResult) == gxos::apps::GX_OK &&
                stepOverCallResult.status == GX_DEVELOPMENT_DEBUG_STATUS_READY &&
                stepOverCode[0] == 0xE8,
                "Step Over resumes the call with the original CALL byte and no TF")) return 1;
    gx_development_debug_snapshot stepOverReturnTrap{};
    if (!expect(pollForTrapFor(GX_DEVELOPMENT_DEBUG_TRAP_BREAKPOINT, stepOverReturnTrap, 43, 78) &&
                stepOverReturnTrap.internalBreakpointTrap != 0 &&
                stepOverReturnTrap.internalBreakpointId == stepOverOwner &&
                stepOverReturnTrap.targetAddress == stepOverReturn && *stepOverCounter == 1 &&
                stepOverCode[5] == 0xCC,
                "callee executes exactly once and the real return INT3 traps")) return 1;
    gx_development_debug_request removeStepOver = makeRequest(
        GX_DEVELOPMENT_DEBUG_REMOVE_BREAKPOINT_OWNER, stepOverOwner, stepOverReturn, 43, 78);
    gx_development_debug_snapshot removeStepOverResult{};
    if (!expect(NativeAppDebugger::Command(removeStepOver, "native-debugger-runtime-proof",
                                           &removeStepOverResult) == gxos::apps::GX_OK &&
                stepOverCode[5] == returnOriginalByte && stepOverCode[0] == 0xCC,
                "temporary owner removed, return byte restored, user call breakpoint rebound")) return 1;
    gx_development_debug_request resumeInternal = makeRequest(
        GX_DEVELOPMENT_DEBUG_RESUME_INTERNAL_TRAP, 0, stepOverReturn, 43, 78);
    resumeInternal.threadId = stepOverReturnTrap.threadId;
    resumeInternal.stopGeneration = stepOverReturnTrap.context.stopGeneration;
    gx_development_debug_snapshot resumeInternalResult{};
    if (!expect(NativeAppDebugger::Command(resumeInternal, "native-debugger-runtime-proof",
                                           &resumeInternalResult) == gxos::apps::GX_OK,
                "resume after internal Step Over stop")) return 1;
    stepOverThread.join();
    if (!expect(stepOverCompleted.load(std::memory_order_acquire) && *stepOverCounter == 1,
                "Step Over target exits after one callee execution")) return 1;
    NativeAppDebugger::UnregisterRuntime(78);
    if (!expect(stepOverCode[0] == 0xE8 && stepOverCode[5] == returnOriginalByte,
                "Step Over teardown restores both original call-site bytes")) return 1;
    delete stepOverCounter;
    ExecutableMemory::Free(stepOverMapping);

    // Phase 8 machine-level proof: the current target frame saves a caller
    // return address inside the same Native ELF image. Step Out reads that
    // raw address from the stopped RBP frame, arms a temporary owner there,
    // resumes the current function normally, and observes the real RET trap.
    ExecutableMemoryBlock stepOutMapping;
    if (!expect(ExecutableMemory::Allocate(4096, stepOutMapping, error),
                "Step Out executable memory allocation")) return 1;
    volatile uint32_t* stepOutCounter = new volatile uint32_t(0);
    uint8_t* stepOutCode = static_cast<uint8_t*>(stepOutMapping.base);
    const uint64_t stepOutBase = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(stepOutMapping.base));
    const uint64_t stepOutFunction = stepOutBase;
    const uint64_t stepOutCallee = stepOutBase + 32;
    const uint64_t stepOutCaller = stepOutBase + 64;
    const uint64_t stepOutBody = stepOutBase + 17;
    const uint64_t stepOutReturn = stepOutCaller + 5;
    stepOutCode[0] = 0x55;                         // push rbp
    stepOutCode[1] = 0x48; stepOutCode[2] = 0x89; stepOutCode[3] = 0xE5; // mov rbp,rsp
    stepOutCode[4] = 0x48; stepOutCode[5] = 0x83; stepOutCode[6] = 0xEC; stepOutCode[7] = 0x08;
    stepOutCode[8] = 0xE8;                        // call callee
    const int64_t calleeRelative = static_cast<int64_t>(stepOutCallee) - static_cast<int64_t>(stepOutFunction + 13);
    const int32_t calleeRelative32 = static_cast<int32_t>(calleeRelative);
    std::memcpy(stepOutCode + 9, &calleeRelative32, sizeof(calleeRelative32));
    stepOutCode[13] = 0x48; stepOutCode[14] = 0x83; stepOutCode[15] = 0xC4; stepOutCode[16] = 0x08;
    stepOutCode[17] = 0x90;                       // deterministic Step Out stop
    stepOutCode[18] = 0x5D;                       // pop rbp
    stepOutCode[19] = 0xC3;                       // ret
    stepOutCode[32] = 0xC3;                       // nested callee ret
    stepOutCode[64] = 0xE8;                       // caller calls current function
    const int64_t functionRelative = static_cast<int64_t>(stepOutFunction) - static_cast<int64_t>(stepOutCaller + 5);
    const int32_t functionRelative32 = static_cast<int32_t>(functionRelative);
    std::memcpy(stepOutCode + 65, &functionRelative32, sizeof(functionRelative32));
    stepOutCode[69] = 0x48; stepOutCode[70] = 0xB8;
    const uint64_t stepOutCounterAddress = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(stepOutCounter));
    std::memcpy(stepOutCode + 71, &stepOutCounterAddress, sizeof(stepOutCounterAddress));
    stepOutCode[79] = 0xFF; stepOutCode[80] = 0x00;     // inc dword ptr [rax]
    stepOutCode[81] = 0xC3;
    const uint8_t stepOutBodyOriginalByte = stepOutCode[17];
    const uint8_t stepOutReturnOriginalByte = stepOutCode[69];
    if (!expect(ExecutableMemory::Protect(stepOutMapping, 0, 1,
                                          ExecutableMemoryProtection::ReadExecute, error),
                "Step Out executable protection")) {
        delete stepOutCounter;
        ExecutableMemory::Free(stepOutMapping);
        return 1;
    }
    NativeAppRuntimeContext stepOutContext{};
    stepOutContext.runtimeId = 79;
    stepOutContext.processId = 44;
    stepOutContext.nativeStackBaseAddress = 0x1000;
    stepOutContext.nativeStackEndAddress = 0x0000800000000000ull;
    NativeElfImage stepOutImage{};
    stepOutImage.preferredBaseAddress = stepOutBase;
    stepOutImage.imageSize = stepOutMapping.size;
    NativeElfSegment stepOutSegment{};
    stepOutSegment.virtualAddress = stepOutBase;
    stepOutSegment.memorySize = stepOutMapping.size;
    stepOutSegment.flags = 1;
    stepOutImage.loadedSegments.push_back(stepOutSegment);
    if (!expect(NativeAppDebugger::RegisterRuntime(stepOutContext, stepOutMapping, stepOutImage, true, error),
                "Step Out runtime registration")) {
        delete stepOutCounter;
        ExecutableMemory::Free(stepOutMapping);
        return 1;
    }
    std::atomic<bool> stepOutWaiting{false};
    std::atomic<bool> stepOutCompleted{false};
    std::thread stepOutThread([&]() {
        gxos::Allocator::setCurrentPid(44);
        stepOutWaiting.store(true, std::memory_order_release);
        typedef void (*StepOutFunction)();
        if (NativeAppDebugger::WaitForExecutionGate(79))
            reinterpret_cast<StepOutFunction>(stepOutCaller)();
        stepOutCompleted.store(true, std::memory_order_release);
        gxos::Allocator::setCurrentPid(0);
    });
    auto cleanupStepOut = [&]() {
        if (stepOutThread.joinable()) {
            if (!stepOutCompleted.load(std::memory_order_acquire)) {
                gx_development_debug_snapshot cancel{};
                NativeAppDebugger::Command(makeRequest(GX_DEVELOPMENT_DEBUG_CANCEL_EXECUTION, 0, 0, 44, 79),
                                           "native-debugger-runtime-proof", &cancel);
            }
            stepOutThread.join();
        }
        NativeAppDebugger::UnregisterRuntime(79);
        delete stepOutCounter;
        ExecutableMemory::Free(stepOutMapping);
    };
    if (!expect(waitFor(stepOutWaiting), "Step Out target reached the closed execution gate")) {
        cleanupStepOut();
        return 1;
    }
    const uint64_t stepOutUserOwner = 0x8000000000000092ull;
    gx_development_debug_snapshot stepOutUserBind{};
    if (!expect(NativeAppDebugger::Command(makeRequest(GX_DEVELOPMENT_DEBUG_BIND_SOFTWARE_BREAKPOINT,
                                                       stepOutUserOwner, stepOutBody, 44, 79),
                                           "native-debugger-runtime-proof", &stepOutUserBind) == gxos::apps::GX_OK &&
                stepOutUserBind.status == GX_DEVELOPMENT_DEBUG_STATUS_BOUND && stepOutCode[17] == 0xCC,
                "Step Out current-function breakpoint bind")) {
        cleanupStepOut();
        return 1;
    }
    gx_development_debug_snapshot stepOutRelease{};
    if (!expect(NativeAppDebugger::Command(makeRequest(GX_DEVELOPMENT_DEBUG_RELEASE_EXECUTION, 0, 0, 44, 79),
                                           "native-debugger-runtime-proof", &stepOutRelease) == gxos::apps::GX_OK,
                "Step Out execution release")) {
        cleanupStepOut();
        return 1;
    }
    gx_development_debug_snapshot stepOutCurrentTrap{};
    if (!expect(pollForTrapFor(GX_DEVELOPMENT_DEBUG_TRAP_BREAKPOINT, stepOutCurrentTrap, 44, 79) &&
                stepOutCurrentTrap.targetAddress == stepOutBody && stepOutCurrentTrap.context.valid != 0,
                "real current-function breakpoint trap before Step Out")) {
        cleanupStepOut();
        return 1;
    }
    const uint64_t savedReturnSlot = stepOutCurrentTrap.context.rbp + 8;
    gx_development_debug_request savedReturnRead = makeRequest(
        GX_DEVELOPMENT_DEBUG_READ_MEMORY, 0, savedReturnSlot, 44, 79);
    savedReturnRead.threadId = stepOutCurrentTrap.threadId;
    savedReturnRead.stopGeneration = stepOutCurrentTrap.context.stopGeneration;
    savedReturnRead.readByteCount = 8;
    gx_development_debug_snapshot savedReturnSnapshot{};
    if (!expect(NativeAppDebugger::Command(savedReturnRead, "native-debugger-runtime-proof",
                                           &savedReturnSnapshot) == gxos::apps::GX_OK &&
                savedReturnSnapshot.status == GX_DEVELOPMENT_DEBUG_STATUS_READY &&
                savedReturnSnapshot.byteCount == 8,
                "Step Out raw saved return-address read")) {
        cleanupStepOut();
        return 1;
    }
    uint64_t savedReturnAddress = 0;
    for (uint32_t i = 0; i < 8; ++i)
        savedReturnAddress |= static_cast<uint64_t>(savedReturnSnapshot.bytes[i]) << (i * 8);
    if (!expect(savedReturnAddress == stepOutReturn,
                "Step Out saved return address equals the target caller return address")) {
        cleanupStepOut();
        return 1;
    }
    const uint64_t stepOutOwner = 0x8000000000000093ull;
    gx_development_debug_snapshot stepOutTempBind{};
    if (!expect(NativeAppDebugger::Command(makeRequest(GX_DEVELOPMENT_DEBUG_BIND_SOFTWARE_BREAKPOINT,
                                                       stepOutOwner, savedReturnAddress, 44, 79),
                                           "native-debugger-runtime-proof", &stepOutTempBind) == gxos::apps::GX_OK &&
                stepOutTempBind.status == GX_DEVELOPMENT_DEBUG_STATUS_BOUND &&
                stepOutTempBind.bindingCount == 1 && stepOutCode[69] == 0xCC,
                "Step Out temporary return breakpoint bind")) {
        cleanupStepOut();
        return 1;
    }
    gx_development_debug_request stepOutRequest = makeRequest(
        GX_DEVELOPMENT_DEBUG_STEP_OUT_RETURN, stepOutOwner, stepOutBody, 44, 79);
    stepOutRequest.auxiliaryAddress = savedReturnAddress;
    stepOutRequest.threadId = stepOutCurrentTrap.threadId;
    stepOutRequest.stopGeneration = stepOutCurrentTrap.context.stopGeneration;
    stepOutRequest.flags = GX_DEVELOPMENT_DEBUG_FLAG_REINSTALL_BREAKPOINT;
    gx_development_debug_snapshot stepOutStart{};
    if (!expect(NativeAppDebugger::Command(stepOutRequest, "native-debugger-runtime-proof", &stepOutStart) == gxos::apps::GX_OK &&
                stepOutStart.status == GX_DEVELOPMENT_DEBUG_STATUS_READY &&
                stepOutCode[17] == stepOutBodyOriginalByte,
                "Step Out restores the current breakpoint and resumes its original instruction")) {
        cleanupStepOut();
        return 1;
    }
    gx_development_debug_snapshot stepOutReturnTrap{};
    if (!expect(pollForTrapFor(GX_DEVELOPMENT_DEBUG_TRAP_BREAKPOINT, stepOutReturnTrap, 44, 79) &&
                stepOutReturnTrap.internalBreakpointTrap != 0 &&
                stepOutReturnTrap.internalBreakpointPurpose == GX_DEVELOPMENT_DEBUG_INTERNAL_BREAKPOINT_STEP_OUT &&
                stepOutReturnTrap.internalBreakpointId == stepOutOwner &&
                stepOutReturnTrap.targetAddress == savedReturnAddress &&
                stepOutReturnTrap.threadId == stepOutCurrentTrap.threadId && *stepOutCounter == 0 &&
                stepOutCode[69] == 0xCC,
                "real Step Out return INT3 trap in the immediate caller")) {
        cleanupStepOut();
        return 1;
    }
    gx_development_debug_snapshot stepOutRemove{};
    if (!expect(NativeAppDebugger::Command(makeRequest(GX_DEVELOPMENT_DEBUG_REMOVE_BREAKPOINT_OWNER,
                                                       stepOutOwner, savedReturnAddress, 44, 79),
                                           "native-debugger-runtime-proof", &stepOutRemove) == gxos::apps::GX_OK &&
                stepOutCode[69] == stepOutReturnOriginalByte && stepOutCode[17] == 0xCC,
                "Step Out temporary owner removal restores only the return byte")) {
        cleanupStepOut();
        return 1;
    }
    gx_development_debug_request stepOutResume = makeRequest(
        GX_DEVELOPMENT_DEBUG_RESUME_INTERNAL_TRAP, 0, savedReturnAddress, 44, 79);
    stepOutResume.threadId = stepOutReturnTrap.threadId;
    stepOutResume.stopGeneration = stepOutReturnTrap.context.stopGeneration;
    gx_development_debug_snapshot stepOutResumeResult{};
    if (!expect(NativeAppDebugger::Command(stepOutResume, "native-debugger-runtime-proof",
                                           &stepOutResumeResult) == gxos::apps::GX_OK,
                "Step Out caller resume after the return trap")) {
        cleanupStepOut();
        return 1;
    }
    stepOutThread.join();
    if (!expect(stepOutCompleted.load(std::memory_order_acquire) && *stepOutCounter == 1,
                "Step Out caller executes after the real return trap")) {
        cleanupStepOut();
        return 1;
    }
    NativeAppDebugger::UnregisterRuntime(79);
    if (!expect(stepOutCode[17] == stepOutBodyOriginalByte && stepOutCode[69] == stepOutReturnOriginalByte,
                "Step Out teardown restores current and return bytes")) {
        delete stepOutCounter;
        ExecutableMemory::Free(stepOutMapping);
        return 1;
    }
    std::cout << "Step Out runtime PASS: current=0x" << std::hex << stepOutBody
              << " rbp=0x" << stepOutCurrentTrap.context.rbp
              << " raw-return=0x" << savedReturnAddress
              << " lookup=0x" << (savedReturnAddress - 1)
              << " original=0x" << static_cast<uint32_t>(stepOutReturnOriginalByte)
              << " patched=0xCC trap=0x" << stepOutReturnTrap.targetAddress
              << " restored=0x" << static_cast<uint32_t>(stepOutReturnOriginalByte)
              << std::dec << " counter=" << *stepOutCounter << "\n";
    delete stepOutCounter;
    ExecutableMemory::Free(stepOutMapping);
    const uint32_t finalCounter = *counter;
    delete counter;
    counter = nullptr;
    ExecutableMemory::Free(mapping);
    std::cout << "Native debugger runtime test PASS: target=" << targetAddress
              << " original=0x" << std::hex << static_cast<uint32_t>(originalByte)
              << " patched=0xCC single-step=EXCEPTION_SINGLE_STEP counter=" << std::dec << finalCounter
              << " rebound=0xCC restored-on-teardown=0x" << std::hex << static_cast<uint32_t>(originalByte) << "\n";
    return 0;
#endif
}
