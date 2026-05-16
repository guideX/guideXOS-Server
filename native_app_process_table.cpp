#include "native_app_process_table.h"

#include <algorithm>
#include <atomic>
#include <mutex>

namespace gxos {
namespace apps {
namespace {

std::atomic<uint64_t> g_nextNativeAppRuntimeId{1};
std::mutex g_processTableMutex;
std::vector<NativeAppProcessInfo> g_processes;

NativeAppProcessInfo* findLocked(uint64_t runtimeId) {
    for (NativeAppProcessInfo& process : g_processes) {
        if (process.runtimeId == runtimeId) return &process;
    }
    return nullptr;
}

uint32_t createdWindowCount(const NativeAppRuntimeContext& context) {
    return context.cleanedWindowCount + static_cast<uint32_t>(context.createdWindowHandles.size());
}

void applyRuntimeState(NativeAppProcessInfo& process, const NativeAppRuntimeContext& context) {
    process.lifecycleState = context.lifecycleState;
    process.startTime = context.startTime;
    process.endTime = context.endTime;
    process.exitCode = context.exitCode;
    process.failureReason = context.failureReason;
    process.createdWindowCount = createdWindowCount(context);
    process.cleanedWindowCount = context.cleanedWindowCount;
    process.remainingWindowCount = static_cast<uint32_t>(context.createdWindowHandles.size());
    process.pollEventCallCount = context.pollEventCallCount;
    process.lastEventType = context.lastEventType;
    process.lastEventWindow = context.lastEventWindow;
    process.lastPollEventResult = context.lastPollEventResult;
    process.drawRectCallCount = context.drawRectCallCount;
    process.lastDrawRectWindow = context.lastDrawRectWindow;
    process.lastDrawRectWidth = context.lastDrawRectWidth;
    process.lastDrawRectHeight = context.lastDrawRectHeight;
    process.lastDrawRectColor = context.lastDrawRectColor;
    process.lastDrawRectResult = context.lastDrawRectResult;
    process.paintEventCount = context.paintEventCount;
    process.lastPaintWindow = context.lastPaintWindow;
    process.lastPaintWidth = context.lastPaintWidth;
    process.lastPaintHeight = context.lastPaintHeight;
}

} // namespace

uint64_t NativeAppProcessTable::AllocateRuntimeId() {
#ifdef GX_ENABLE_EXPERIMENTAL_NATIVE_ELF_EXECUTION
    return g_nextNativeAppRuntimeId.fetch_add(1);
#else
    return 0;
#endif
}

void NativeAppProcessTable::RegisterPrepared(const NativeAppRuntimeContext& context, bool experimentalExecutionEnabled, const std::string& hostArchitecture) {
#ifdef GX_ENABLE_EXPERIMENTAL_NATIVE_ELF_EXECUTION
    if (context.runtimeId == 0) return;

    std::lock_guard<std::mutex> lock(g_processTableMutex);
    NativeAppProcessInfo* existing = findLocked(context.runtimeId);
    if (!existing) {
        NativeAppProcessInfo process;
        process.nativePid = context.runtimeId;
        process.runtimeId = context.runtimeId;
        process.appId = context.appId;
        process.displayName = context.displayName;
        process.architecture = context.architecture;
        process.experimentalExecutionEnabled = experimentalExecutionEnabled;
        process.hostArchitecture = hostArchitecture;
        applyRuntimeState(process, context);
        g_processes.push_back(process);
        return;
    }

    existing->appId = context.appId;
    existing->displayName = context.displayName;
    existing->architecture = context.architecture;
    existing->experimentalExecutionEnabled = experimentalExecutionEnabled;
    existing->hostArchitecture = hostArchitecture;
    applyRuntimeState(*existing, context);
#else
    (void)context;
    (void)experimentalExecutionEnabled;
    (void)hostArchitecture;
#endif
}

void NativeAppProcessTable::MarkRunning(uint64_t runtimeId) {
#ifdef GX_ENABLE_EXPERIMENTAL_NATIVE_ELF_EXECUTION
    std::lock_guard<std::mutex> lock(g_processTableMutex);
    NativeAppProcessInfo* process = findLocked(runtimeId);
    if (!process) return;

    process->lifecycleState = NativeAppLifecycleState::Running;
    process->startTime = std::chrono::steady_clock::now();
#else
    (void)runtimeId;
#endif
}

void NativeAppProcessTable::UpdateFromRuntime(const NativeAppRuntimeContext& context) {
#ifdef GX_ENABLE_EXPERIMENTAL_NATIVE_ELF_EXECUTION
    if (context.runtimeId == 0) return;

    std::lock_guard<std::mutex> lock(g_processTableMutex);
    NativeAppProcessInfo* process = findLocked(context.runtimeId);
    if (!process) return;

    applyRuntimeState(*process, context);
#else
    (void)context;
#endif
}

void NativeAppProcessTable::MarkCompleted(uint64_t runtimeId, NativeAppLifecycleState state, int32_t exitCode, const std::string& failureReason) {
#ifdef GX_ENABLE_EXPERIMENTAL_NATIVE_ELF_EXECUTION
    std::lock_guard<std::mutex> lock(g_processTableMutex);
    NativeAppProcessInfo* process = findLocked(runtimeId);
    if (!process) return;

    process->lifecycleState = state;
    process->endTime = std::chrono::steady_clock::now();
    process->exitCode = exitCode;
    if (!failureReason.empty()) process->failureReason = failureReason;
#else
    (void)runtimeId;
    (void)state;
    (void)exitCode;
    (void)failureReason;
#endif
}

std::vector<NativeAppProcessInfo> NativeAppProcessTable::List() {
#ifdef GX_ENABLE_EXPERIMENTAL_NATIVE_ELF_EXECUTION
    std::lock_guard<std::mutex> lock(g_processTableMutex);
    return g_processes;
#else
    return {};
#endif
}

bool NativeAppProcessTable::Find(uint64_t runtimeId, NativeAppProcessInfo& outInfo) {
#ifdef GX_ENABLE_EXPERIMENTAL_NATIVE_ELF_EXECUTION
    std::lock_guard<std::mutex> lock(g_processTableMutex);
    NativeAppProcessInfo* process = findLocked(runtimeId);
    if (!process) return false;

    outInfo = *process;
    return true;
#else
    (void)runtimeId;
    (void)outInfo;
    return false;
#endif
}

} // namespace apps
} // namespace gxos
