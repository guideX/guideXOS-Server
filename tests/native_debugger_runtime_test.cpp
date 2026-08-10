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
                                         uint64_t targetAddress) {
    gx_development_debug_request request{};
    request.size = sizeof(request);
    request.version = GX_DEVELOPMENT_DEBUG_API_VERSION;
    request.command = command;
    request.handle = 1;
    request.sessionGeneration = 9;
    request.processId = 42;
    request.nativeRuntimeId = 77;
    request.breakpointId = breakpointId;
    request.targetAddress = targetAddress;
    request.artifactSha256 = "native-debugger-runtime-proof";
    return request;
}

bool pollForTrap(uint32_t expectedKind, gx_development_debug_snapshot& snapshot) {
    for (uint32_t i = 0; i < 1000000; ++i) {
        gx_development_debug_request poll = makeRequest(GX_DEVELOPMENT_DEBUG_POLL, 0, 0);
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

    gx_development_debug_snapshot secondSingleStep{};
    if (!expect(continueBreakpoint(secondTrap, 900), "second breakpoint continuation accepted")) {
        cleanup();
        return 1;
    }
    if (!expect(pollForTrap(GX_DEVELOPMENT_DEBUG_TRAP_SINGLE_STEP, secondSingleStep),
                "second real EXCEPTION_SINGLE_STEP observed")) {
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
