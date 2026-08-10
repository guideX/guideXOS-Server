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
        ExecutableMemory::Free(mapping);
    };

    const uint64_t targetAddress = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(mapping.base));
    const uint8_t originalByte = 0xC3;
    *static_cast<uint8_t*>(mapping.base) = originalByte;
    if (!expect(ExecutableMemory::Protect(mapping, 0, 1, ExecutableMemoryProtection::ReadExecute, error),
                "initial executable protection")) {
        cleanup();
        return 1;
    }

    NativeAppRuntimeContext context{};
    context.runtimeId = 77;
    context.processId = 42;
    NativeElfImage image{};
    image.preferredBaseAddress = targetAddress;
    image.imageSize = mapping.size;
    NativeElfSegment segment{};
    segment.virtualAddress = targetAddress;
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
    if (!expect(*static_cast<volatile uint8_t*>(mapping.base) == 0xCC, "patched memory contains INT3")) {
        cleanup();
        return 1;
    }

    targetThread = std::thread([&]() {
        gxos::Allocator::setCurrentPid(42);
        waiting.store(true, std::memory_order_release);
        if (!NativeAppDebugger::WaitForExecutionGate(77)) return;
        typedef void (*TargetFunction)();
        reinterpret_cast<TargetFunction>(mapping.base)();
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
    bool trapObserved = false;
    for (uint32_t i = 0; i < 1000000 && !trapObserved; ++i) {
        gx_development_debug_request poll = makeRequest(GX_DEVELOPMENT_DEBUG_POLL, 0, 0);
        if (NativeAppDebugger::Command(poll, "native-debugger-runtime-proof", &trapSnapshot) != gxos::apps::GX_OK) break;
        trapObserved = trapSnapshot.status == GX_DEVELOPMENT_DEBUG_STATUS_TRAP;
        if (!trapObserved) std::this_thread::yield();
    }
    if (!(trapObserved && trapSnapshot.trapKind == GX_DEVELOPMENT_DEBUG_TRAP_BREAKPOINT &&
                trapSnapshot.processId == 42 && trapSnapshot.nativeRuntimeId == 77 && trapSnapshot.threadId != 0 &&
                trapSnapshot.targetAddress == targetAddress && trapSnapshot.instructionPointer == targetAddress &&
                trapSnapshot.bindingId == bindSnapshot.bindingId)) {
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

    gx_development_debug_snapshot restoreSnapshot{};
    gx_development_debug_request restore = makeRequest(GX_DEVELOPMENT_DEBUG_RESTORE_ALL, 0, 0);
    if (!expect(NativeAppDebugger::Command(restore, "native-debugger-runtime-proof", &restoreSnapshot) == gxos::apps::GX_OK &&
                restoreSnapshot.status == GX_DEVELOPMENT_DEBUG_STATUS_RESTORED,
                "breakpoint restoration")) {
        cleanup();
        return 1;
    }
    if (!expect(*static_cast<volatile uint8_t*>(mapping.base) == originalByte, "restored byte matches original")) {
        cleanup();
        return 1;
    }
    targetThread.join();
    if (!expect(completed.load(std::memory_order_acquire), "target resumed and returned after restoration")) {
        cleanup();
        return 1;
    }
    NativeAppDebugger::UnregisterRuntime(77);
    registered = false;
    ExecutableMemory::Free(mapping);
    std::cout << "Native debugger runtime test PASS: target=" << targetAddress
              << " original=0xC3 patched=0xCC restored=0xC3 trap=EXCEPTION_BREAKPOINT\n";
    return 0;
#endif
}
