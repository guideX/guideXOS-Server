#include <intrin.h>
#include "guidexos_nativeaot_allocation_diagnostics.h"
#include "guidexos_nativeaot_amd64_unwind_primitive.h"
#if defined(GUIDEXOS_NATIVEAOT_C011EC21_NATIVE_CONTINUATION)
#include "guidexos_nativeaot_gc_startup_platform_contract.h"
#endif
#if defined(GUIDEXOS_NATIVEAOT_THREAD_STATIC_PROOF)
#include "guidexos_nativeaot_thread_static_diagnostics.h"
#endif
#if defined(GUIDEXOS_NATIVEAOT_SINGLE_THREAD_SUSPEND_EE_ALLOCATION)
// This proof uses the locked runtime's actual ThreadStore. The startup-only
// guideXOS threadstore adapter below the runtime-pack boundary remains opaque
// and is intentionally not used for GC suspension.
#include "common.h"
#include "CommonTypes.h"
// daccess.h uses the Win32 spelling even in this non-event-trace proof
// object; CommonTypes only declares it when FEATURE_EVENT_TRACE is enabled.
#if !defined(FEATURE_EVENT_TRACE)
typedef void* LPVOID;
#endif
#include "thread.h"
#include "threadstore.h"
#include "threadstore.inl"
#include "thread.inl"
#include "RuntimeInstance.h"
#include "MethodTable.h"
#include "ObjectLayout.h"
#endif
#if defined(GUIDEXOS_NATIVEAOT_THREAD_STATIC_PROOF) && !defined(GUIDEXOS_NATIVEAOT_SINGLE_THREAD_SUSPEND_EE_ALLOCATION)
#include "common.h"
#include "CommonTypes.h"
#if !defined(FEATURE_EVENT_TRACE)
typedef void* LPVOID;
#endif
#include "thread.h"
#include "threadstore.h"
#include "threadstore.inl"
#include "thread.inl"
#include "MethodTable.h"
#include "ObjectLayout.h"
#endif
#if defined(GUIDEXOS_NATIVEAOT_SEGMENT_BOUNDARY_ALLOCATION)
#include "guidexos_nativeaot_virtual_memory_adapter.h"
#endif

#pragma intrinsic(__readgsqword)

#if defined(GUIDEXOS_NATIVEAOT_MANAGED_ALLOCATION)
// These symbols have the external C++ linkage emitted by the locked
// Bootstrap/main.cpp.  Declare them outside the platform translation unit's
// anonymous namespace so the linker resolves the production bookends rather
// than anonymous-namespace lookalikes.
extern void* __managedcode_a();
extern void* __managedcode_z();
extern void* __unbox_a();
extern void* __unbox_z();
extern "C" bool RhRegisterOSModule(
    void* osModule,
    void* managedCodeStart,
    uint32_t managedCodeSize,
    void* unboxingStubsStart,
    uint32_t unboxingStubsSize,
    void** pClasslibFunctions,
    uint32_t nClasslibFunctions);
#endif

namespace {

using gx_uintptr = unsigned __int64;
using gx_uint32 = unsigned long;
using gx_uint16 = unsigned short;
using gx_size = unsigned __int64;

// The locked gcinterface.h defines ScanContext as:
// Thread*, int, int, uintptr_t, bool promotion, bool concurrent, ... .
// The runtime-pack translation unit sees only its forward declaration, so
// keep this proof-only prefix view local and read no fields beyond the two
// flags required for ABI characterization.
struct GuideXosScanContextPrefix {
    void* threadUnderCrawl;
    int threadNumber;
    int threadCount;
    uintptr_t stackLimit;
    bool promotion;
    bool concurrent;
};

// Keep this object independent of Windows headers. The numeric reason is the
// FAST_FAIL_FATAL_APP_EXIT value used by the stock helper; only the fail-fast
// instruction is part of this hosted pack's contract.
constexpr gx_uint32 kFastFailFatalAppExit = 7u;

constexpr gx_uint32 kFlsOutOfIndexes = 0xFFFFFFFFu;
constexpr gx_uint32 kGuideXosFlsIndex = 0u;
constexpr gx_uint32 kTlsVectorOffset = 0x58u;
constexpr gx_uint32 kRuntimeCellOffset = 0x30u;
constexpr gx_uint32 kRuntimeCellInitializedOffset = 0x38u;
constexpr gx_uint32 kRuntimeCellTransitionFrameOffset = 0x40u;
constexpr gx_uint32 kFlsCellOffset = 0x80u;
constexpr gx_uint32 kFlsCellCount = 8u;
constexpr gx_uint32 kMinimumTlsBlockSize = 0x110u;

#if defined(GUIDEXOS_NATIVEAOT_SINGLE_THREAD_SUSPEND_EE_ALLOCATION)
extern "C" guidexos_nativeaot_allocation_diagnostics
    g_guideXosAllocationDiagnostics;
[[noreturn]] void guideXosFailFast(gx_uint32 reason);
#if defined(GUIDEXOS_NATIVEAOT_ALLOCATION_CONTEXT_FIXUP_ROOT_BOUNDARY_ALLOCATION)
extern "C" void __cdecl guideXosNativeAotAllocationContextFixupRequest();
#endif
#if defined(GUIDEXOS_NATIVEAOT_C011EC21_NATIVE_CONTINUATION)
extern "C" void __cdecl guideXosNativeAotC011EC21DescribeNativeCaller(
    uintptr_t recoveredRip, uintptr_t recoveredRsp, uintptr_t recoveredRbp) {
    const guidexos_nativeaot_gc_native_continuation_hook hook =
        reinterpret_cast<guidexos_nativeaot_gc_native_continuation_hook>(
            guidexos_nativeaot_gc_get_native_continuation_hook());
    if (hook != nullptr) {
        hook(recoveredRip, recoveredRsp, recoveredRbp);
    } else {
        g_guideXosAllocationDiagnostics.c011ec21TransitionLinkingDefect = 1u;
    }
}
extern "C" __declspec(noreturn) void __cdecl
guideXosNativeAotC011EC21SafeStop(uint32_t reason);
#if defined(GUIDEXOS_NATIVEAOT_C011EC23_NATIVE_UNWIND)
extern "C" __declspec(noreturn) void __cdecl
guideXosNativeAotC011EC23SafeStop(uint32_t reason);
#endif
#if defined(GUIDEXOS_NATIVEAOT_C011EC24_CALLER_PROVENANCE)
extern "C" uint32_t __cdecl guideXosNativeAotC011EC24PreflightRealFrame(
    uintptr_t helperPc, uintptr_t liveRsp);
extern "C" __declspec(noreturn) void __cdecl
guideXosNativeAotC011EC24SafeStop(uint32_t reason);
#if defined(GUIDEXOS_NATIVEAOT_C011EC25_KERNEL_ENTRY_BOUNDARY)
extern "C" __declspec(noreturn) void __cdecl
guideXosNativeAotC011EC25SafeStop(uint32_t reason);
#endif
#endif
#endif

#if defined(GUIDEXOS_NATIVEAOT_MANAGED_ALLOCATION)
extern bool g_guideXosNativeAotCodeManagerRegistered;

bool getNativeAotRange(void* start, void* end, void** rangeStart, uint32_t* rangeSize) {
    if (start == nullptr || end == nullptr || rangeStart == nullptr || rangeSize == nullptr) {
        return false;
    }
    const uintptr_t startAddress = reinterpret_cast<uintptr_t>(start);
    const uintptr_t endAddress = reinterpret_cast<uintptr_t>(end);
    if (endAddress <= startAddress || endAddress - startAddress > 0xFFFFFFFFu) {
        return false;
    }
    *rangeStart = start;
    *rangeSize = static_cast<uint32_t>(endAddress - startAddress);
    return *rangeSize != 0u;
}
#endif
#endif

#if defined(GUIDEXOS_NATIVEAOT_SINGLE_THREAD_SUSPEND_EE_ALLOCATION)
namespace {

using SuspendEeThread = Thread;

SuspendEeThread* suspendEeCurrentThread() {
    return ThreadStore::GetCurrentThreadIfAvailable();
}

uint32_t suspendEeThreadFlags(SuspendEeThread* thread) {
    return thread == nullptr
        ? 0u
        : reinterpret_cast<RuntimeThreadLocals*>(thread)->m_ThreadStateFlags;
}

void captureSuspendEeThreadState(
    guidexos_nativeaot_allocation_diagnostics& diagnostics,
    SuspendEeThread* thread,
    bool beforeLock) {
    if (thread == nullptr) {
        return;
    }
    const gx_uintptr nativeId =
        static_cast<gx_uintptr>(thread->GetPalThreadIdForLogging());
    PTR_VOID stackLow = nullptr;
    PTR_VOID stackHigh = nullptr;
    thread->GetStackBounds(&stackLow, &stackHigh);
    if (beforeLock) {
        diagnostics.suspendEeCurrentNativeThreadId = nativeId;
        diagnostics.suspendEeCurrentStackLow =
            reinterpret_cast<gx_uintptr>(stackLow);
        diagnostics.suspendEeCurrentStackHigh =
            reinterpret_cast<gx_uintptr>(stackHigh);
        diagnostics.suspendEeCurrentTransitionFrame =
            reinterpret_cast<gx_uintptr>(
                reinterpret_cast<RuntimeThreadLocals*>(thread)->m_pTransitionFrame);
        diagnostics.currentThreadStateFlagsBefore =
            suspendEeThreadFlags(thread);
        diagnostics.currentThreadCooperativeBefore =
            thread->IsCurrentThreadInCooperativeMode() ? 1u : 0u;
    } else {
        diagnostics.currentThreadStateFlagsDuring =
            suspendEeThreadFlags(thread);
        diagnostics.currentThreadCooperativeDuring =
            thread->IsCurrentThreadInCooperativeMode() ? 1u : 0u;
    }
}

void suspendEeSerialPutString(const char* value);

// The locked NativeAOT runtime's ThreadStore starts with SList<Thread>
// m_ThreadList, whose only data member is the intrusive-list head.  The
// bare-metal startup path constructs the current TLS Thread and marks it
// initialized, but it does not call ThreadStore::AttachCurrentThread; that
// runtime method therefore returns before linking the thread.  Bridge that
// startup omission before LockThreadStore, while the registry is not locked,
// so the real LockThreadStore/SuspendAllThreads/Iterator path observes the
// actual mutator.  Do not change the list after the lock is acquired.
struct SuspendEeThreadStorePrefix {
    SuspendEeThread* head;
};

void registerCurrentThreadInThreadStoreBeforeLock(SuspendEeThread* current) {
    if (current == nullptr) {
        return;
    }

    SuspendEeThreadStorePrefix* store =
        reinterpret_cast<SuspendEeThreadStorePrefix*>(GetThreadStore());
    if (store->head == nullptr) {
        reinterpret_cast<RuntimeThreadLocals*>(current)->m_pNext = nullptr;
        store->head = current;
        ++g_guideXosAllocationDiagnostics.threadStoreAdapterRegistrationCount;
        suspendEeSerialPutString(
            "[nativeaot-gc-single-thread-suspend-ee] ThreadStore adapter registered current mutator\n");
    }
}

uint32_t countRegisteredThreads(SuspendEeThread* current,
                                uint32_t* peerCount) {
    uint32_t count = 0u;
    uint32_t peers = 0u;
    ThreadStore::Iterator iterator;
    SuspendEeThread* thread = iterator.GetNext();
    for (uint32_t iterations = 0u;
         thread != nullptr && iterations < 32u;
         ++iterations, thread = iterator.GetNext()) {
        ++count;
        if (thread != current) {
            ++peers;
        }
    }
    if (thread != nullptr) {
        suspendEeSerialPutString(
            "[nativeaot-gc-single-thread-suspend-ee] ThreadStore registry traversal exceeded bound\n");
        guideXosFailFast(9u);
    }
    if (peerCount != nullptr) {
        *peerCount = peers;
    }
    return count;
}

void suspendEeSerialPutChar(char value) {
    if (value == '\n') {
        while ((__inbyte(0x3FDu) & 0x20u) == 0u) {
        }
        __outbyte(0x3F8u, static_cast<unsigned char>('\r'));
    }
    while ((__inbyte(0x3FDu) & 0x20u) == 0u) {
    }
    __outbyte(0x3F8u, static_cast<unsigned char>(value));
}

#if defined(GUIDEXOS_NATIVEAOT_STACK_PROVIDER_TRANSITION_FAILFAST_MINIMAL)
bool isOptionalTransitionDiagnostic(const char* value) {
    static const char prefix[] = "[nativeaot-gc-next-genuine-root-provider]";
    if (value == nullptr) {
        return false;
    }
    for (gx_size index = 0u; prefix[index] != '\0'; ++index) {
        if (value[index] == '\0') {
            return false;
        }
        if (value[index] != prefix[index]) {
            return false;
        }
    }
    return true;
}
#endif

void suspendEeSerialPutString(const char* value) {
#if defined(GUIDEXOS_NATIVEAOT_STACK_PROVIDER_TRANSITION_FAILFAST_MINIMAL)
    if (isOptionalTransitionDiagnostic(value)) {
        return;
    }
#endif
    while (*value != '\0') {
        suspendEeSerialPutChar(*value++);
    }
}

void suspendEeSerialPutHex32(gx_uint32 value) {
    static const char hex[] = "0123456789ABCDEF";
    for (int shift = 28; shift >= 0; shift -= 4) {
        suspendEeSerialPutChar(hex[(value >> shift) & 0xFu]);
    }
}

void suspendEeSerialPutHex64(gx_uintptr value) {
    static const char hex[] = "0123456789ABCDEF";
    for (int shift = 60; shift >= 0; shift -= 4) {
        suspendEeSerialPutChar(hex[(value >> shift) & 0xFu]);
    }
}

void emitSingleThreadSuspendEeSafeStop(const char* callback) {
    const guidexos_nativeaot_allocation_diagnostics& diagnostics =
        g_guideXosAllocationDiagnostics;
    suspendEeSerialPutString(
        "[nativeaot-gc-single-thread-suspend-ee] SAFE_STOP marker=");
    suspendEeSerialPutHex32(diagnostics.singleThreadSuspendEeMarker);
    suspendEeSerialPutString(" callback=");
    suspendEeSerialPutString(callback);
    suspendEeSerialPutString(" requestCount=");
    suspendEeSerialPutHex32(diagnostics.firstCollectionRequestCount);
    suspendEeSerialPutString(" entryCount=");
    suspendEeSerialPutHex32(diagnostics.firstCollectionEntryCount);
    suspendEeSerialPutString(" requestedGeneration=");
    suspendEeSerialPutHex32(diagnostics.requestedGeneration);
    suspendEeSerialPutString(" reason=");
    suspendEeSerialPutHex32(diagnostics.collectionReason);
    suspendEeSerialPutString(" suspendReason=");
    suspendEeSerialPutHex32(diagnostics.suspendEeReason);
    suspendEeSerialPutString(" blocking=");
    suspendEeSerialPutHex32(diagnostics.collectionBlockingMode);
    suspendEeSerialPutString(" compacting=");
    suspendEeSerialPutHex32(diagnostics.collectionCompactingMode);
    suspendEeSerialPutString(" suspendEeEntryCount=");
    suspendEeSerialPutHex32(diagnostics.suspendEeEntryCount);
    suspendEeSerialPutString(" suspendEeReturnCount=");
    suspendEeSerialPutHex32(diagnostics.suspendEeReturnCount);
    suspendEeSerialPutString(" suspendEeSuspensionCount=");
    suspendEeSerialPutHex32(diagnostics.suspendEeSuspensionCount);
    suspendEeSerialPutString(" lockRequests=");
    suspendEeSerialPutHex32(diagnostics.threadStoreLockRequestCount);
    suspendEeSerialPutString(" lockAcquisitions=");
    suspendEeSerialPutHex32(diagnostics.threadStoreLockAcquisitionCount);
    suspendEeSerialPutString(" lockFailures=");
    suspendEeSerialPutHex32(diagnostics.threadStoreLockFailureCount);
    suspendEeSerialPutString(" unlocks=");
    suspendEeSerialPutHex32(diagnostics.threadStoreUnlockCount);
    suspendEeSerialPutString(" lockOwner=");
    suspendEeSerialPutHex64(diagnostics.threadStoreLockOwner);
    suspendEeSerialPutString(" lockOwnerNativeId=");
    suspendEeSerialPutHex64(diagnostics.threadStoreLockOwnerNativeThreadId);
    suspendEeSerialPutString(" lockDepth=");
    suspendEeSerialPutHex32(diagnostics.threadStoreLockRecursionDepth);
    suspendEeSerialPutString(" registeredThreads=");
    suspendEeSerialPutHex32(diagnostics.registeredManagedThreadCount);
    suspendEeSerialPutString(" initiator=");
    suspendEeSerialPutHex64(diagnostics.suspendEeInitiatingRuntimeThread);
    suspendEeSerialPutString(" currentRuntimeThread=");
    suspendEeSerialPutHex64(diagnostics.suspendEeCurrentRuntimeThread);
    suspendEeSerialPutString(" currentNativeId=");
    suspendEeSerialPutHex64(diagnostics.suspendEeCurrentNativeThreadId);
    suspendEeSerialPutString(" identitiesMatch=");
    suspendEeSerialPutHex32(diagnostics.currentAndInitiatorMatch);
    suspendEeSerialPutString(" expectedOtherMutators=");
    suspendEeSerialPutHex32(diagnostics.expectedOtherMutators);
    suspendEeSerialPutString(" stoppedOtherMutators=");
    suspendEeSerialPutHex32(diagnostics.stoppedOtherMutators);
    suspendEeSerialPutString(" currentThreadExempt=");
    suspendEeSerialPutHex32(diagnostics.currentThreadExemptFromPeerStop);
    suspendEeSerialPutString(" managedEntryProhibited=");
    suspendEeSerialPutHex32(diagnostics.managedEntryProhibited);
    suspendEeSerialPutString(" eeSuspended=");
    suspendEeSerialPutHex32(diagnostics.eeSuspended);
    suspendEeSerialPutString(" nextBoundary=");
    suspendEeSerialPutHex32(diagnostics.nextBoundary);
    suspendEeSerialPutString(" rootRequests=");
    suspendEeSerialPutHex32(diagnostics.rootEnumerationRequestCount);
    suspendEeSerialPutString(" rootEntries=");
    suspendEeSerialPutHex32(diagnostics.rootEnumerationEntryCount);
    suspendEeSerialPutString(" stackWalkRequests=");
    suspendEeSerialPutHex32(diagnostics.stackWalkRequestCount);
    suspendEeSerialPutString(" stackWalkEntries=");
    suspendEeSerialPutHex32(diagnostics.stackWalkEntryCount);
    suspendEeSerialPutString(" handleScanRequests=");
    suspendEeSerialPutHex32(diagnostics.handleScanRequestCount);
    suspendEeSerialPutString(" handleScanEntries=");
    suspendEeSerialPutHex32(diagnostics.handleScanEntryCount);
    suspendEeSerialPutString(" heapMutationStarted=");
    suspendEeSerialPutHex32(diagnostics.suspendEeHeapMutationStarted);
    suspendEeSerialPutString(" restartRequests=");
    suspendEeSerialPutHex32(diagnostics.restartRequestCount);
    suspendEeSerialPutString(" restartEntries=");
    suspendEeSerialPutHex32(diagnostics.restartEntryCount);
    suspendEeSerialPutString(" managedResumeCount=");
    suspendEeSerialPutHex32(diagnostics.managedResumeCount);
    suspendEeSerialPutString(" registryMutationAttemptsWhileLocked=");
    suspendEeSerialPutHex32(
        diagnostics.threadStoreRegistryMutationAttemptsWhileLocked);
    suspendEeSerialPutString(" adapterRegistrations=");
    suspendEeSerialPutHex32(diagnostics.threadStoreAdapterRegistrationCount);
    suspendEeSerialPutString(" safeStopReason=");
    suspendEeSerialPutHex32(diagnostics.suspendEeStopReason);
    suspendEeSerialPutString(" allocations=");
    suspendEeSerialPutHex32(diagnostics.allocationCount);
    suspendEeSerialPutString(" fast=");
    suspendEeSerialPutHex32(diagnostics.fastAllocationCount);
    suspendEeSerialPutString(" rare=");
    suspendEeSerialPutHex32(diagnostics.rarePathCount);
    suspendEeSerialPutString(" refills=");
    suspendEeSerialPutHex32(diagnostics.allocationContextRefillCount);
    suspendEeSerialPutString(" sameSegmentCommits=");
    suspendEeSerialPutHex32(diagnostics.heapCommitEventCount);
    suspendEeSerialPutString(" segmentTransitions=");
    suspendEeSerialPutHex32(diagnostics.segmentTransitionCount);
    suspendEeSerialPutString(" sentinelChecks=");
    suspendEeSerialPutHex32(diagnostics.sentinelValidationCount);
    suspendEeSerialPutString(" sentinelFailures=");
    suspendEeSerialPutHex32(diagnostics.sentinelValidationFailures);
    suspendEeSerialPutString(" liveSentinels=");
    suspendEeSerialPutHex32(diagnostics.liveSentinelCount);
    suspendEeSerialPutString("\n");
}

extern "C" void __cdecl guideXosNativeAotSuspendEeEntry(gx_uint32 reason) {
    guidexos_nativeaot_allocation_diagnostics& diagnostics =
        g_guideXosAllocationDiagnostics;
    suspendEeSerialPutString(
        "[nativeaot-gc-single-thread-suspend-ee] SuspendEE entry\n");
    SuspendEeThread* current = suspendEeCurrentThread();
    SuspendEeThread* initiator = reinterpret_cast<SuspendEeThread*>(
        diagnostics.runtimeThreadRecord);
    ++diagnostics.suspendEeEntryCount;
    ++diagnostics.suspendEeEpoch;
    ++diagnostics.threadStoreLockRequestCount;
    ++diagnostics.suspensionRequestCount;
    diagnostics.suspendEeReason = reason;
    diagnostics.requestedGeneration = 1u;
    diagnostics.collectionReason =
        GUIDEXOS_NATIVEAOT_COLLECTION_REASON_OUT_OF_SO_H;
    diagnostics.collectionBlockingMode = GUIDEXOS_NATIVEAOT_COLLECTION_BLOCKING;
    diagnostics.collectionCompactingMode =
        GUIDEXOS_NATIVEAOT_COLLECTION_NONCOMPACTING_NOT_SELECTED;
    diagnostics.firstCollectionRequestCount = 1u;
    diagnostics.firstCollectionEntryCount = 1u;
    diagnostics.collectionRequestCount = 1u;
    diagnostics.collectionEntryCount = 1u;
    diagnostics.collectionsEntered = 1u;
    diagnostics.collectionTriggeringEntries = 1u;
    diagnostics.suspendEeCurrentRuntimeThread =
        reinterpret_cast<gx_uintptr>(current);
    diagnostics.suspendEeInitiatingRuntimeThread =
        reinterpret_cast<gx_uintptr>(initiator);
    diagnostics.suspendEeCollectionInitiatorNativeThreadId =
        initiator == nullptr ? 0u
                             : static_cast<gx_uintptr>(
                                   initiator->GetPalThreadIdForLogging());
    diagnostics.currentThreadRegistered =
        current != nullptr && current->IsInitialized() ? 1u : 0u;
    diagnostics.currentThreadIsInitiator = current != nullptr &&
        current == initiator ? 1u : 0u;
    diagnostics.currentAndInitiatorMatch =
        diagnostics.currentThreadIsInitiator;
    diagnostics.suspendEeGcMode = diagnostics.gcMode;
    captureSuspendEeThreadState(diagnostics, current, true);
    registerCurrentThreadInThreadStoreBeforeLock(current);
}

extern "C" void __cdecl guideXosNativeAotSuspendEeAfterLock() {
    guidexos_nativeaot_allocation_diagnostics& diagnostics =
        g_guideXosAllocationDiagnostics;
    suspendEeSerialPutString(
        "[nativeaot-gc-single-thread-suspend-ee] ThreadStore lock acquired\n");
    SuspendEeThread* current = suspendEeCurrentThread();
    ++diagnostics.threadStoreLockAcquisitionCount;
    diagnostics.threadStoreLockOwner =
        reinterpret_cast<gx_uintptr>(current);
    diagnostics.threadStoreLockOwnerNativeThreadId = current == nullptr
        ? 0u
        : static_cast<gx_uintptr>(current->GetPalThreadIdForLogging());
    diagnostics.threadStoreLockRecursionDepth = 1u;
    diagnostics.suspendEeCurrentRuntimeThread =
        reinterpret_cast<gx_uintptr>(current);
    diagnostics.registeredManagedThreadCount =
        countRegisteredThreads(current, nullptr);
    diagnostics.expectedOtherMutators =
        diagnostics.registeredManagedThreadCount == 0u
            ? 0u : diagnostics.registeredManagedThreadCount - 1u;
    suspendEeSerialPutString(
        "[nativeaot-gc-single-thread-suspend-ee] ThreadStore registry count complete\n");
    diagnostics.currentThreadRegistered =
        current != nullptr && current->IsInitialized() ? 1u : 0u;
    diagnostics.currentThreadIsInitiator =
        current != nullptr && current == reinterpret_cast<SuspendEeThread*>(
            diagnostics.suspendEeInitiatingRuntimeThread) ? 1u : 0u;
    diagnostics.currentAndInitiatorMatch =
        diagnostics.currentThreadIsInitiator;
    captureSuspendEeThreadState(diagnostics, current, false);
    diagnostics.threadStoreRegistryMutationAttemptsWhileLocked = 0u;
}

extern "C" void __cdecl guideXosNativeAotSuspendEeAfterSuspend() {
    guidexos_nativeaot_allocation_diagnostics& diagnostics =
        g_guideXosAllocationDiagnostics;
    suspendEeSerialPutString(
        "[nativeaot-gc-single-thread-suspend-ee] SuspendAllThreads returned\n");
    ++diagnostics.suspendEeSuspensionCount;
    diagnostics.suspensionEntryCount = 1u;
    // The registry count was captured after the real lock acquisition. The
    // lock remains held across SuspendAllThreads, whose source contract
    // skips only the collector thread; no second iterator traversal is
    // needed, and for the one-mutator proof the stopped-peer count is zero.
    diagnostics.stoppedOtherMutators =
        diagnostics.expectedOtherMutators;
    // The locked source assigns RhpSuspendingThread to pThisThread and sets
    // TrapThreads before the wait loop. Returning from SuspendAllThreads is
    // the source-backed publication point for this state.
    diagnostics.currentThreadExemptFromPeerStop = 1u;
    diagnostics.suspendEeSuspensionOwner =
        diagnostics.suspendEeCurrentRuntimeThread;
    diagnostics.managedEntryProhibited = 1u;
    diagnostics.eeSuspended =
        diagnostics.threadStoreLockAcquisitionCount != 0u &&
        diagnostics.currentThreadExemptFromPeerStop != 0u &&
        diagnostics.managedEntryProhibited != 0u ? 1u : 0u;
    suspendEeSerialPutString(
        "[nativeaot-gc-single-thread-suspend-ee] SuspendEE observer complete\n");
}

extern "C" void __cdecl guideXosNativeAotSuspendEeBodyReturn() {
    suspendEeSerialPutString(
        "[nativeaot-gc-single-thread-suspend-ee] SuspendEE body complete\n");
}

extern "C" void __cdecl guideXosNativeAotDisablePreemptiveEntry() {
    suspendEeSerialPutString(
        "[nativeaot-gc-single-thread-suspend-ee] DisablePreemptiveGC entry\n");
}

#if defined(GUIDEXOS_NATIVEAOT_ALLOCATION_CONTEXT_FIXUP_ROOT_BOUNDARY_ALLOCATION)
extern "C" void __cdecl guideXosNativeAotDisablePreemptiveReturn() {
    guideXosNativeAotAllocationContextFixupRequest();
}
#else
extern "C" __declspec(noreturn) void __cdecl guideXosNativeAotDisablePreemptiveReturn() {
    suspendEeSerialPutString(
        "[nativeaot-gc-single-thread-suspend-ee] DisablePreemptiveGC returned; safe boundary before GCHeap::GarbageCollect\n");
    guidexos_nativeaot_allocation_diagnostics& diagnostics =
        g_guideXosAllocationDiagnostics;
    suspendEeSerialPutString(
        "[nativeaot-gc-single-thread-suspend-ee] safeValidation entry=");
    suspendEeSerialPutHex32(diagnostics.suspendEeEntryCount);
    suspendEeSerialPutString(" lockReq=");
    suspendEeSerialPutHex32(diagnostics.threadStoreLockRequestCount);
    suspendEeSerialPutString(" lockAcq=");
    suspendEeSerialPutHex32(diagnostics.threadStoreLockAcquisitionCount);
    suspendEeSerialPutString(" registered=");
    suspendEeSerialPutHex32(diagnostics.registeredManagedThreadCount);
    suspendEeSerialPutString(" currentRegistered=");
    suspendEeSerialPutHex32(diagnostics.currentThreadRegistered);
    suspendEeSerialPutString(" identities=");
    suspendEeSerialPutHex32(diagnostics.currentAndInitiatorMatch);
    suspendEeSerialPutString(" expectedPeers=");
    suspendEeSerialPutHex32(diagnostics.expectedOtherMutators);
    suspendEeSerialPutString(" stoppedPeers=");
    suspendEeSerialPutHex32(diagnostics.stoppedOtherMutators);
    suspendEeSerialPutString(" exempt=");
    suspendEeSerialPutHex32(diagnostics.currentThreadExemptFromPeerStop);
    suspendEeSerialPutString(" entryProhibited=");
    suspendEeSerialPutHex32(diagnostics.managedEntryProhibited);
    suspendEeSerialPutString(" eeSuspended=");
    suspendEeSerialPutHex32(diagnostics.eeSuspended);
    suspendEeSerialPutString("\n");
    const bool valid = diagnostics.suspendEeEntryCount == 1u &&
        diagnostics.threadStoreLockRequestCount == 1u &&
        diagnostics.threadStoreLockAcquisitionCount == 1u &&
        diagnostics.registeredManagedThreadCount == 1u &&
        diagnostics.currentThreadRegistered == 1u &&
        diagnostics.currentAndInitiatorMatch == 1u &&
        diagnostics.expectedOtherMutators == 0u &&
        diagnostics.stoppedOtherMutators == 0u &&
        diagnostics.currentThreadExemptFromPeerStop == 1u &&
        diagnostics.managedEntryProhibited == 1u &&
        diagnostics.eeSuspended == 1u;
    if (!valid) {
        ++diagnostics.threadStoreLockFailureCount;
        guideXosFailFast(9u);
    }

    ++diagnostics.suspendEeReturnCount;
    diagnostics.nextBoundary =
        GUIDEXOS_NATIVEAOT_SINGLE_THREAD_SUSPEND_EE_NEXT_POST_DISABLE;
    diagnostics.rootEnumerationRequestCount = 0u;
    diagnostics.rootEnumerationEntryCount = 0u;
    diagnostics.stackWalkRequestCount = 0u;
    diagnostics.stackWalkEntryCount = 0u;
    diagnostics.handleScanRequestCount = 0u;
    diagnostics.handleScanEntryCount = 0u;
    diagnostics.suspendEeHeapMutationStarted = 0u;
    diagnostics.heapMutationStarted = 0u;
    diagnostics.restartRequestCount = 0u;
    diagnostics.restartEntryCount = 0u;
    diagnostics.managedResumeCount = 0u;
    diagnostics.suspendEeSafeStopObserved = 1u;
    diagnostics.safeStopObserved = 1u;
    diagnostics.singleThreadSuspendEeMarker =
        GUIDEXOS_NATIVEAOT_SINGLE_THREAD_SUSPEND_EE_SAFE_STOP_MARKER;
    diagnostics.suspendEeStopReason =
        GUIDEXOS_NATIVEAOT_SINGLE_THREAD_SUSPEND_EE_SAFE_STOP_MARKER;
    diagnostics.stopReason = diagnostics.suspendEeStopReason;
    diagnostics.stage =
        GUIDEXOS_NATIVEAOT_ALLOC_STAGE_F21_SINGLE_THREAD_SUSPEND_EE_SAFE_STOP;
    diagnostics.sequence += 1u;
    diagnostics.currentRip = reinterpret_cast<gx_uintptr>(_ReturnAddress());
    diagnostics.currentRsp = reinterpret_cast<gx_uintptr>(_AddressOfReturnAddress());
    diagnostics.waitReason = diagnostics.suspendEeReason;
    diagnostics.failFastReason = 7u;
    diagnostics.collectionEntryThread = diagnostics.suspendEeCurrentRuntimeThread;
    diagnostics.collectionRequestAllocationOrdinal =
        diagnostics.allocationRequestCount;
    diagnostics.collectionEntryAllocationOrdinal =
        diagnostics.allocationRequestCount;
    emitSingleThreadSuspendEeSafeStop(
        "GCHeap::GarbageCollect before fix_allocation_contexts");
    for (;;) {
    }
}
#endif

#if defined(GUIDEXOS_NATIVEAOT_ALLOCATION_CONTEXT_FIXUP_ROOT_BOUNDARY_ALLOCATION)
extern "C" void __cdecl guideXosNativeAotSuspendEeGcStartWorkBoundary() {
    // Kept as a compatibility export for the prior source-injection mode;
    // the fixup proof observes GcStartWork without stopping there.
    guideXosNativeAotAllocationContextFixupRequest();
}
#else
extern "C" __declspec(noreturn) void __cdecl
guideXosNativeAotSuspendEeGcStartWorkBoundary() {
    guideXosNativeAotDisablePreemptiveReturn();
}
#endif
} // namespace
#endif

#if defined(GUIDEXOS_NATIVEAOT_MANAGED_ALLOCATION)
#ifndef GUIDEXOS_MANAGED_HEAP_BYTES
#define GUIDEXOS_MANAGED_HEAP_BYTES 0x10000u
#endif
constexpr gx_uint32 kManagedArrayDataOffset = 0x10u;
// The hydrated NativeAOT byte[] EEType reports a 0x18-byte base size. The
// array data still begins at +0x10; the extra eight bytes are part of the
// generated allocation envelope and must be included in the boundary check.
constexpr gx_uint32 kManagedArrayBaseSize = 0x18u;
constexpr gx_uint32 kManagedArrayComponentSize = 1u;
#if defined(GUIDEXOS_NATIVEAOT_FIRST_REFILL_ALLOCATION) || defined(GUIDEXOS_NATIVEAOT_SEGMENT_BOUNDARY_ALLOCATION)
constexpr gx_size kNativeAotLargeObjectSize = 85000u;
#endif
#if defined(GUIDEXOS_NATIVEAOT_SEGMENT_BOUNDARY_ALLOCATION)
#ifndef GUIDEXOS_NATIVEAOT_SEGMENT_BOUNDARY_ARRAY_LENGTH
#define GUIDEXOS_NATIVEAOT_SEGMENT_BOUNDARY_ARRAY_LENGTH 4096u
#endif
#ifndef GUIDEXOS_NATIVEAOT_SEGMENT_BOUNDARY_HARD_LIMIT
#define GUIDEXOS_NATIVEAOT_SEGMENT_BOUNDARY_HARD_LIMIT 384u
#endif
constexpr gx_uint32 kSegmentBoundaryArrayLength = GUIDEXOS_NATIVEAOT_SEGMENT_BOUNDARY_ARRAY_LENGTH;
constexpr gx_uint32 kSegmentBoundaryHardLimit = GUIDEXOS_NATIVEAOT_SEGMENT_BOUNDARY_HARD_LIMIT;
#endif
#if defined(GUIDEXOS_NATIVEAOT_FIRST_COLLECTION_BOUNDARY_ALLOCATION)
#ifndef GUIDEXOS_NATIVEAOT_FIRST_COLLECTION_BOUNDARY_ARRAY_LENGTH
#define GUIDEXOS_NATIVEAOT_FIRST_COLLECTION_BOUNDARY_ARRAY_LENGTH 4096u
#endif
#ifndef GUIDEXOS_NATIVEAOT_FIRST_COLLECTION_BOUNDARY_HARD_LIMIT
#define GUIDEXOS_NATIVEAOT_FIRST_COLLECTION_BOUNDARY_HARD_LIMIT 256u
#endif
constexpr gx_uint32 kFirstCollectionBoundaryArrayLength =
    GUIDEXOS_NATIVEAOT_FIRST_COLLECTION_BOUNDARY_ARRAY_LENGTH;
constexpr gx_uint32 kFirstCollectionBoundaryHardLimit =
    GUIDEXOS_NATIVEAOT_FIRST_COLLECTION_BOUNDARY_HARD_LIMIT;
#endif
#if defined(GUIDEXOS_NATIVEAOT_SEGMENT_TRANSITION_ALLOCATION)
#ifndef GUIDEXOS_NATIVEAOT_SEGMENT_TRANSITION_ARRAY_LENGTH
#define GUIDEXOS_NATIVEAOT_SEGMENT_TRANSITION_ARRAY_LENGTH 4096u
#endif
#ifndef GUIDEXOS_NATIVEAOT_SEGMENT_TRANSITION_HARD_LIMIT
#define GUIDEXOS_NATIVEAOT_SEGMENT_TRANSITION_HARD_LIMIT 32u
#endif
#ifndef GUIDEXOS_NATIVEAOT_SEGMENT_TRANSITION_HARD_REFILL_LIMIT
#define GUIDEXOS_NATIVEAOT_SEGMENT_TRANSITION_HARD_REFILL_LIMIT 20u
#endif
#ifndef GUIDEXOS_NATIVEAOT_SEGMENT_TRANSITION_HARD_COMMIT_LIMIT
#define GUIDEXOS_NATIVEAOT_SEGMENT_TRANSITION_HARD_COMMIT_LIMIT 4u
#endif
constexpr gx_uint32 kSegmentTransitionArrayLength = GUIDEXOS_NATIVEAOT_SEGMENT_TRANSITION_ARRAY_LENGTH;
constexpr gx_uint32 kSegmentTransitionHardLimit = GUIDEXOS_NATIVEAOT_SEGMENT_TRANSITION_HARD_LIMIT;
constexpr gx_uint32 kSegmentTransitionHardRefillLimit = GUIDEXOS_NATIVEAOT_SEGMENT_TRANSITION_HARD_REFILL_LIMIT;
constexpr gx_uint32 kSegmentTransitionHardCommitLimit = GUIDEXOS_NATIVEAOT_SEGMENT_TRANSITION_HARD_COMMIT_LIMIT;
#endif
#if !defined(GUIDEXOS_NATIVEAOT_REAL_GC_ALLOCATION)
constexpr gx_size kManagedHeapBytes = static_cast<gx_size>(GUIDEXOS_MANAGED_HEAP_BYTES);
static_assert(kManagedHeapBytes >= 0x1000u, "The bounded diagnostic heap must leave room for runtime setup and several arrays.");

struct GuideXosAllocationDiagnostics {
    gx_uint32 heapInitialized;
    gx_uint32 allocationCount;
    gx_uint32 requestedArrayLength;
    gx_uint32 requestedObjectSize;
    gx_uint32 outOfMemory;
    gx_uint32 collectionRequested;
    gx_uint32 gcSuspensionEntered;
    gx_uint32 reserved;
    gx_uintptr heapBase;
    gx_uintptr heapSize;
    gx_uintptr initialAllocationPointer;
    gx_uintptr allocationPointerAfter;
    gx_uintptr allocationPointerBeforeFailure;
    gx_uintptr allocationPointerAfterFailure;
    gx_uintptr remainingBytesBeforeFailure;
    gx_uintptr previousObject;
    gx_uintptr lastObject;
    gx_uintptr lastObjectSize;
    gx_uintptr returnedObject;
    gx_uintptr arrayData;
    gx_uint32 controlledOutOfMemory;
    gx_uint32 pointerContractFailures;
    gx_uint32 objectAlignmentFailures;
    gx_uint32 objectRangeFailures;
    gx_uint32 objectLayoutFailures;
    gx_uint32 zeroInitializationChecks;
    gx_uint32 zeroInitializationFailures;
    gx_uint32 patternChecks;
    gx_uint32 patternFailures;
    gx_uint32 sampledObjectFailures;
    gx_uint32 heapExpansionOccurred;
};

// The allocation experiment uses a bounded, image-backed region. The mapped
// image loader provides zero-filled writable BSS for this storage; no Windows
// virtual-memory call is on the managed allocation path.
__declspec(align(16)) unsigned char g_guideXosManagedHeap[kManagedHeapBytes];
extern "C" volatile GuideXosAllocationDiagnostics g_guideXosAllocationDiagnostics = {};
extern "C" void* __cdecl guideXosStockRhpNewArray(void* eeType, gx_size length);
#else
extern "C" guidexos_nativeaot_allocation_diagnostics g_guideXosAllocationDiagnostics = {};
#if defined(GUIDEXOS_NATIVEAOT_REAL_GC_ALLOCATION)
bool g_guideXosRealGcDiagnosticsInitialized = false;
volatile gx_uint32 g_guideXosObservedRhpNewArrayEntries = 0u;
#endif
extern "C" void* __cdecl guideXosStockRhpNewArray(void* eeType, gx_size length);
extern "C" int32_t guidexos_nativeaot_gc_read_state(
    gx_uintptr* allocationContext,
    gx_uintptr* allocationLimit,
    gx_uintptr* currentThread,
    gx_uintptr* gcHeap,
    gx_uint32* gcCount,
    gx_uintptr* allocatedBytes,
    gx_uint32* finalizableObjects,
    gx_uint32* gcInProgress,
    gx_uint32* gcMode,
    gx_uintptr* contextIdentity,
    gx_uintptr* allocBytes,
    gx_uintptr* allocBytesUoh);
extern "C" int32_t guidexos_nativeaot_gc_describe_object(
    void* object,
    gx_uintptr* heapBase,
    gx_uintptr* heapAllocated,
    gx_uintptr* heapReserved,
    gx_uint32* heapOwned);
#if defined(GUIDEXOS_NATIVEAOT_SEGMENT_BOUNDARY_ALLOCATION)
extern "C" int32_t guidexos_nativeaot_gc_describe_segment(
    void* object,
    gx_uintptr* segmentIdentity,
    gx_uintptr* segmentBase,
    gx_uintptr* segmentAllocated,
    gx_uintptr* segmentCommitted,
    gx_uintptr* segmentReserved,
    gx_uint32* segmentFlags,
    gx_uint32* segmentGeneration);
#endif
#endif
#endif

#if defined(GUIDEXOS_NATIVEAOT_ALLOCATION_CONTEXT_FIXUP_ROOT_BOUNDARY_ALLOCATION)
bool g_guideXosAllocationContextFixupPending = false;

void allocationContextFixupInvariantFailure() {
    ++g_guideXosAllocationDiagnostics.allocationContextFixupInvariantFailures;
}

void allocationContextFixupRootInvariantFailure() {
    ++g_guideXosAllocationDiagnostics.rootBoundaryInvariantFailures;
}

gx_uintptr allocationContextFixupLastObjectEnd() {
    gx_uintptr lastEnd = 0u;
    for (gx_uint32 index = 0u;
         index < g_guideXosAllocationDiagnostics.objectHistoryCount;
         ++index) {
        const guidexos_nativeaot_object_history_entry& entry =
            g_guideXosAllocationDiagnostics.objectHistory[index];
        if (entry.end > lastEnd) {
            lastEnd = entry.end;
        }
    }
    return lastEnd;
}

void captureAllocationContextFixupSnapshot(
    guidexos_nativeaot_allocation_context_snapshot* snapshot) {
    if (snapshot == nullptr) {
        allocationContextFixupInvariantFailure();
        return;
    }
    *snapshot = {};

    gx_uintptr allocationPointer = 0u;
    gx_uintptr allocationLimit = 0u;
    gx_uintptr currentThread = 0u;
    gx_uintptr gcHeap = 0u;
    gx_uint32 gcCount = 0u;
    gx_uintptr allocatedBytes = 0u;
    gx_uint32 finalizableObjects = 0u;
    gx_uint32 gcInProgress = 0u;
    gx_uint32 gcMode = 0u;
    gx_uintptr contextIdentity = 0u;
    gx_uintptr allocBytes = 0u;
    gx_uintptr allocBytesUoh = 0u;
    if (guidexos_nativeaot_gc_read_state(
            &allocationPointer, &allocationLimit, &currentThread, &gcHeap,
            &gcCount, &allocatedBytes, &finalizableObjects, &gcInProgress,
            &gcMode, &contextIdentity, &allocBytes, &allocBytesUoh) != 0 ||
        contextIdentity == 0u || currentThread == 0u || gcHeap == 0u) {
        allocationContextFixupInvariantFailure();
        return;
    }

    snapshot->contextIdentity = contextIdentity;
    snapshot->owningRuntimeThread = currentThread;
    snapshot->owningGcHeap = gcHeap;
    snapshot->allocPtr = allocationPointer;
    snapshot->allocLimit = allocationLimit;
    snapshot->allocationStart = allocationPointer;
    snapshot->allocationSize = allocationLimit >= allocationPointer
        ? allocationLimit - allocationPointer : 0u;
    snapshot->unusedTailBytes = snapshot->allocationSize;
    snapshot->heapAllocatedBytes = allocatedBytes;
    snapshot->allocBytes = allocBytes;
    snapshot->allocBytesUoh = allocBytesUoh;
    snapshot->active = 1u;
    snapshot->current = 1u;
    snapshot->cleared = allocationPointer == 0u && allocationLimit == 0u
        ? 1u : 0u;

    void* objectForSegment = nullptr;
    if (g_guideXosAllocationDiagnostics.objectHistoryCount != 0u) {
        objectForSegment = reinterpret_cast<void*>(
            g_guideXosAllocationDiagnostics.objectHistory[
                g_guideXosAllocationDiagnostics.objectHistoryCount - 1u].address);
    } else if (allocationPointer != 0u) {
        objectForSegment = reinterpret_cast<void*>(allocationPointer);
    }
    if (objectForSegment != nullptr &&
        guidexos_nativeaot_gc_describe_segment(
            objectForSegment, &snapshot->segmentIdentity,
            &snapshot->segmentBase, &snapshot->segmentAllocated,
            &snapshot->segmentCommitted, &snapshot->segmentReserved,
            reinterpret_cast<gx_uint32*>(&snapshot->segmentFlags),
            reinterpret_cast<gx_uint32*>(&snapshot->segmentGeneration)) != 0) {
        allocationContextFixupInvariantFailure();
    }
    snapshot->generationAllocationStart = snapshot->segmentAllocated;
}

bool validateAllocationContextFixupObject(
    guidexos_nativeaot_object_history_entry& entry,
    const guidexos_nativeaot_allocation_context_snapshot& snapshot,
    bool afterFixup) {
    bool valid = true;
    const gx_uintptr address = entry.address;
    const gx_uintptr end = entry.end;
    if (address == 0u || end <= address || (address & 7u) != 0u) {
        ++g_guideXosAllocationDiagnostics.objectAlignmentFailuresAfterFixup;
        valid = false;
    }
    if (address != 0u &&
        *reinterpret_cast<const gx_uintptr*>(address) != entry.eeType) {
        ++g_guideXosAllocationDiagnostics.objectTypeLayoutFailuresAfterFixup;
        valid = false;
    }
    const gx_uint32 observedLength = address == 0u
        ? 0u : *reinterpret_cast<const gx_uint32*>(address + 8u);
    if (observedLength != entry.length) {
        ++g_guideXosAllocationDiagnostics.objectTypeLayoutFailuresAfterFixup;
        valid = false;
    }
    gx_uint32 heapOwned = 0u;
    gx_uintptr heapBase = 0u;
    gx_uintptr heapAllocated = 0u;
    gx_uintptr heapReserved = 0u;
    if (guidexos_nativeaot_gc_describe_object(
            reinterpret_cast<void*>(address), &heapBase, &heapAllocated,
            &heapReserved, &heapOwned) != 0 || heapOwned == 0u) {
        ++g_guideXosAllocationDiagnostics.objectBoundaryFailuresAfterFixup;
        valid = false;
    } else if ((afterFixup && end > heapAllocated) || end > heapReserved ||
               (afterFixup && snapshot.segmentAllocated != 0u &&
                end > snapshot.segmentAllocated)) {
        ++g_guideXosAllocationDiagnostics.objectBoundaryFailuresAfterFixup;
        valid = false;
    }
    if (address != 0u && entry.length >= 4u) {
        const unsigned char* data = reinterpret_cast<const unsigned char*>(
            address + 0x10u);
        for (gx_uint32 index = 0u; index < entry.length; ++index) {
            const unsigned char expected = index < 4u
                ? static_cast<unsigned char>(entry.sequence >> (index * 8u))
                : static_cast<unsigned char>(
                    (index * 17u + entry.sequence * 31u) & 0xFFu);
            if (data[index] != expected) {
                ++g_guideXosAllocationDiagnostics.objectPatternFailuresAfterFixup;
                valid = false;
                break;
            }
        }
    } else {
        ++g_guideXosAllocationDiagnostics.objectPatternFailuresAfterFixup;
        valid = false;
    }
    if (end > allocationContextFixupLastObjectEnd()) {
        ++g_guideXosAllocationDiagnostics.objectTailClassificationFailures;
        valid = false;
    }
    if (afterFixup) {
        entry.afterValid = valid ? 1u : 0u;
    } else {
        entry.beforeValid = valid ? 1u : 0u;
    }
    return valid;
}

void validateAllocationContextFixupObjects(bool afterFixup) {
    guidexos_nativeaot_allocation_diagnostics& diagnostics =
        g_guideXosAllocationDiagnostics;
    if (diagnostics.objectHistoryOverflow != 0u) {
        allocationContextFixupInvariantFailure();
    }
    const uint32_t beforeFailures =
        diagnostics.objectValidationFailuresBeforeFixup;
    const uint32_t afterFailures =
        diagnostics.objectValidationFailuresAfterFixup;
    const guidexos_nativeaot_allocation_context_snapshot& snapshot = afterFixup
        ? diagnostics.allocationContextFixupAfter[0]
        : diagnostics.allocationContextFixupBefore[0];
    for (gx_uint32 index = 0u; index < diagnostics.objectHistoryCount; ++index) {
        guidexos_nativeaot_object_history_entry& entry =
            diagnostics.objectHistory[index];
        if (!validateAllocationContextFixupObject(entry, snapshot, afterFixup)) {
            if (afterFixup) {
                ++diagnostics.objectValidationFailuresAfterFixup;
            } else {
                ++diagnostics.objectValidationFailuresBeforeFixup;
            }
        }
        for (gx_uint32 other = 0u; other < index; ++other) {
            const guidexos_nativeaot_object_history_entry& prior =
                diagnostics.objectHistory[other];
            if (entry.address == prior.address) {
                ++diagnostics.duplicateObjectAddressFailures;
                ++diagnostics.objectOverlapFailuresAfterFixup;
            } else if (entry.address < prior.end && prior.address < entry.end) {
                ++diagnostics.objectOverlapFailuresAfterFixup;
            }
        }
    }
    if (afterFixup) {
        diagnostics.objectValidationAfterFixupCount =
            diagnostics.objectHistoryCount;
        diagnostics.sentinelChecksAfterFixup = diagnostics.sentinelValidationCount;
        (void)afterFailures;
    } else {
        diagnostics.objectValidationBeforeFixupCount =
            diagnostics.objectHistoryCount;
        diagnostics.sentinelChecksBeforeFixup = diagnostics.sentinelValidationCount;
        (void)beforeFailures;
    }
}

void emitAllocationContextFixupRootBoundarySafeStop() {
    const guidexos_nativeaot_allocation_diagnostics& diagnostics =
        g_guideXosAllocationDiagnostics;
    suspendEeSerialPutString(
        "[nativeaot-gc-allocation-context-fixup-root-boundary] SAFE_STOP marker=");
    suspendEeSerialPutHex32(diagnostics.allocationContextFixupRootBoundaryMarker);
    suspendEeSerialPutString(" callback=GCToEEInterface::GcScanRoots entry before FOREACH_THREAD");
    suspendEeSerialPutString(" fixupRequest=");
    suspendEeSerialPutHex32(diagnostics.allocationContextFixupRequestCount);
    suspendEeSerialPutString(" fixupEntry=");
    suspendEeSerialPutHex32(diagnostics.allocationContextFixupEntryCount);
    suspendEeSerialPutString(" fixupComplete=");
    suspendEeSerialPutHex32(diagnostics.allocationContextFixupCompletionCount);
    suspendEeSerialPutString(" contextsVisited=");
    suspendEeSerialPutHex32(diagnostics.allocationContextFixupContextsVisited);
    suspendEeSerialPutString(" contextsChanged=");
    suspendEeSerialPutHex32(diagnostics.allocationContextFixupContextsChanged);
    suspendEeSerialPutString(" contextsCleared=");
    suspendEeSerialPutHex32(diagnostics.allocationContextFixupContextsCleared);
    suspendEeSerialPutString(" objectBefore=");
    suspendEeSerialPutHex32(diagnostics.objectValidationBeforeFixupCount);
    suspendEeSerialPutString(" objectAfter=");
    suspendEeSerialPutHex32(diagnostics.objectValidationAfterFixupCount);
    suspendEeSerialPutString(" sentinelChecks=");
    suspendEeSerialPutHex32(diagnostics.sentinelChecksAtRootBoundary);
    suspendEeSerialPutString(" rootDispatcher=");
    suspendEeSerialPutHex32(diagnostics.rootDispatcherEntryCount);
    suspendEeSerialPutString(" rootProviders=");
    suspendEeSerialPutHex32(diagnostics.rootProviderEntryCount);
    suspendEeSerialPutString(" rootCandidates=");
    suspendEeSerialPutHex32(diagnostics.firstRootCandidateCount);
    suspendEeSerialPutString(" callbacks=");
    suspendEeSerialPutHex32(diagnostics.rootCallbacksDelivered);
    suspendEeSerialPutString(" marking=");
    suspendEeSerialPutHex32(diagnostics.markingEntryCount);
    suspendEeSerialPutString(" metadataMutation=");
    suspendEeSerialPutHex32(diagnostics.allocationContextMetadataMutationStarted);
    suspendEeSerialPutString(" objectMutation=");
    suspendEeSerialPutHex32(diagnostics.objectMemoryMutationStarted);
    suspendEeSerialPutString(" restartResume=");
    suspendEeSerialPutHex32(diagnostics.restartResumeCount);
    suspendEeSerialPutString(" fixupMode=");
    suspendEeSerialPutHex32(diagnostics.allocationContextFixupMode);
    suspendEeSerialPutString(" enumerationComplete=");
    suspendEeSerialPutHex32(diagnostics.allocationContextFixupEnumerationComplete);
    suspendEeSerialPutString(" activeBefore=");
    suspendEeSerialPutHex32(diagnostics.allocationContextFixupContextsActiveBefore);
    suspendEeSerialPutString(" activeAfter=");
    suspendEeSerialPutHex32(diagnostics.allocationContextFixupContextsActiveAfter);
    suspendEeSerialPutString(" retired=");
    suspendEeSerialPutHex32(diagnostics.allocationContextFixupContextsRetired);
    suspendEeSerialPutString(" metadataComplete=");
    suspendEeSerialPutHex32(diagnostics.allocationContextMetadataMutationCompleted);
    suspendEeSerialPutString(" segmentBookkeeping=");
    suspendEeSerialPutHex32(diagnostics.segmentBookkeepingMutationCount);
    suspendEeSerialPutString(" fixupFailures=");
    suspendEeSerialPutHex32(diagnostics.allocationContextFixupInvariantFailures);
    suspendEeSerialPutString(" rootFailures=");
    suspendEeSerialPutHex32(diagnostics.rootBoundaryInvariantFailures);
    suspendEeSerialPutString(" objectFailuresBefore=");
    suspendEeSerialPutHex32(diagnostics.objectValidationFailuresBeforeFixup);
    suspendEeSerialPutString(" objectFailuresAfter=");
    suspendEeSerialPutHex32(diagnostics.objectValidationFailuresAfterFixup);
    suspendEeSerialPutString(" overlapFailures=");
    suspendEeSerialPutHex32(diagnostics.objectOverlapFailuresAfterFixup);
    suspendEeSerialPutString(" duplicateFailures=");
    suspendEeSerialPutHex32(diagnostics.duplicateObjectAddressFailures);
    suspendEeSerialPutString(" typeLayoutFailures=");
    suspendEeSerialPutHex32(diagnostics.objectTypeLayoutFailuresAfterFixup);
    suspendEeSerialPutString(" boundaryFailures=");
    suspendEeSerialPutHex32(diagnostics.objectBoundaryFailuresAfterFixup);
    suspendEeSerialPutString(" patternFailures=");
    suspendEeSerialPutHex32(diagnostics.objectPatternFailuresAfterFixup);
    suspendEeSerialPutString(" addressChanges=");
    suspendEeSerialPutHex32(diagnostics.objectAddressChangesAfterFixup);
    suspendEeSerialPutString(" allocPtrBefore=");
    suspendEeSerialPutHex64(diagnostics.allocationPointerBeforeFixup);
    suspendEeSerialPutString(" allocLimitBefore=");
    suspendEeSerialPutHex64(diagnostics.allocationLimitBeforeFixup);
    suspendEeSerialPutString(" allocPtrAfter=");
    suspendEeSerialPutHex64(diagnostics.allocationPointerAfterFixup);
    suspendEeSerialPutString(" allocLimitAfter=");
    suspendEeSerialPutHex64(diagnostics.allocationLimitAfterFixup);
    suspendEeSerialPutString(" validExtentBefore=");
    suspendEeSerialPutHex64(diagnostics.validAllocatedExtentBeforeFixup);
    suspendEeSerialPutString(" validExtentAfter=");
    suspendEeSerialPutHex64(diagnostics.validAllocatedExtentAfterFixup);
    suspendEeSerialPutString(" unusedTailBefore=");
    suspendEeSerialPutHex64(diagnostics.unusedTailBytesBeforeFixup);
    suspendEeSerialPutString(" unusedTailAfter=");
    suspendEeSerialPutHex64(diagnostics.unusedTailBytesAfterFixup);
    suspendEeSerialPutString(" heapCounterBefore=");
    suspendEeSerialPutHex64(diagnostics.heapAllocationCounterBeforeFixup);
    suspendEeSerialPutString(" heapCounterAfter=");
    suspendEeSerialPutHex64(diagnostics.heapAllocationCounterAfterFixup);
    suspendEeSerialPutString(" segmentAllocatedBefore=");
    suspendEeSerialPutHex64(diagnostics.segmentAllocatedBeforeFixup);
    suspendEeSerialPutString(" segmentAllocatedAfter=");
    suspendEeSerialPutHex64(diagnostics.segmentAllocatedAfterFixup);
    suspendEeSerialPutString(" contextBefore=");
    suspendEeSerialPutHex64(diagnostics.allocationContextFixupBefore[0].contextIdentity);
    suspendEeSerialPutString(" contextAfter=");
    suspendEeSerialPutHex64(diagnostics.allocationContextFixupAfter[0].contextIdentity);
    suspendEeSerialPutString(" allocBytesBefore=");
    suspendEeSerialPutHex64(diagnostics.allocationContextFixupBefore[0].allocBytes);
    suspendEeSerialPutString(" allocBytesAfter=");
    suspendEeSerialPutHex64(diagnostics.allocationContextFixupAfter[0].allocBytes);
    suspendEeSerialPutString(" segmentCommittedBefore=");
    suspendEeSerialPutHex64(diagnostics.segmentCommittedBeforeFixup);
    suspendEeSerialPutString(" segmentCommittedAfter=");
    suspendEeSerialPutHex64(diagnostics.segmentCommittedAfterFixup);
    suspendEeSerialPutString(" segmentReservedBefore=");
    suspendEeSerialPutHex64(diagnostics.segmentReservedBeforeFixup);
    suspendEeSerialPutString(" segmentReservedAfter=");
    suspendEeSerialPutHex64(diagnostics.segmentReservedAfterFixup);
    suspendEeSerialPutString("\n");
}

extern "C" void __cdecl guideXosNativeAotAllocationContextFixupRequest() {
    guidexos_nativeaot_allocation_diagnostics& diagnostics =
        g_guideXosAllocationDiagnostics;
    ++diagnostics.allocationContextFixupRequestCount;
    diagnostics.allocationContextFixupMode = 1u;
    g_guideXosAllocationContextFixupPending = true;
    diagnostics.objectMemoryMutationStarted = 0u;
    diagnostics.allocationContextMetadataMutationStarted = 0u;
    diagnostics.allocationContextMetadataMutationCompleted = 0u;
    diagnostics.allocationContextBeforeCount = 1u;
    captureAllocationContextFixupSnapshot(&diagnostics.allocationContextFixupBefore[0]);
    diagnostics.allocationPointerBeforeFixup =
        diagnostics.allocationContextFixupBefore[0].allocPtr;
    diagnostics.allocationLimitBeforeFixup =
        diagnostics.allocationContextFixupBefore[0].allocLimit;
    diagnostics.heapAllocationCounterBeforeFixup =
        diagnostics.allocationContextFixupBefore[0].heapAllocatedBytes;
    diagnostics.segmentAllocatedBeforeFixup =
        diagnostics.allocationContextFixupBefore[0].segmentAllocated;
    diagnostics.segmentCommittedBeforeFixup =
        diagnostics.allocationContextFixupBefore[0].segmentCommitted;
    diagnostics.segmentReservedBeforeFixup =
        diagnostics.allocationContextFixupBefore[0].segmentReserved;
    diagnostics.validAllocatedExtentBeforeFixup =
        allocationContextFixupLastObjectEnd();
    diagnostics.unusedTailBytesBeforeFixup =
        diagnostics.allocationContextFixupBefore[0].unusedTailBytes;
    diagnostics.sentinelChecksBeforeFixup = diagnostics.sentinelValidationCount;
    validateAllocationContextFixupObjects(false);
}

extern "C" void __cdecl guideXosNativeAotAllocationContextFixupEnumerationEntry() {
    guidexos_nativeaot_allocation_diagnostics& diagnostics =
        g_guideXosAllocationDiagnostics;
    ++diagnostics.allocationContextFixupEntryCount;
    if (diagnostics.allocationContextFixupRequestCount != 1u) {
        allocationContextFixupInvariantFailure();
    }
    diagnostics.allocationContextMetadataMutationStarted = 1u;
}

extern "C" void __cdecl guideXosNativeAotAllocationContextFixupContextVisited(
    gx_uintptr context) {
    guidexos_nativeaot_allocation_diagnostics& diagnostics =
        g_guideXosAllocationDiagnostics;
    ++diagnostics.allocationContextFixupContextsVisited;
    if (context == 0u ||
        diagnostics.allocationContextBeforeCount == 0u ||
        context != diagnostics.allocationContextFixupBefore[0].contextIdentity) {
        allocationContextFixupInvariantFailure();
    }
}

extern "C" void __cdecl guideXosNativeAotAllocationContextFixupEnumerationComplete() {
    guidexos_nativeaot_allocation_diagnostics& diagnostics =
        g_guideXosAllocationDiagnostics;
    diagnostics.allocationContextFixupEnumerationComplete = 1u;
    if (diagnostics.allocationContextFixupContextsVisited == 0u) {
        allocationContextFixupInvariantFailure();
    }
}

extern "C" void __cdecl guideXosNativeAotAllocationContextFixupGcStartWorkObserver() {
    guidexos_nativeaot_allocation_diagnostics& diagnostics =
        g_guideXosAllocationDiagnostics;
    if (!g_guideXosAllocationContextFixupPending ||
        diagnostics.allocationContextFixupEnumerationComplete == 0u) {
        allocationContextFixupInvariantFailure();
    }
    diagnostics.allocationContextAfterCount = 1u;
    captureAllocationContextFixupSnapshot(&diagnostics.allocationContextFixupAfter[0]);
    diagnostics.allocationPointerAfterFixup =
        diagnostics.allocationContextFixupAfter[0].allocPtr;
    diagnostics.allocationLimitAfterFixup =
        diagnostics.allocationContextFixupAfter[0].allocLimit;
    diagnostics.heapAllocationCounterAfterFixup =
        diagnostics.allocationContextFixupAfter[0].heapAllocatedBytes;
    diagnostics.segmentAllocatedAfterFixup =
        diagnostics.allocationContextFixupAfter[0].segmentAllocated;
    diagnostics.segmentCommittedAfterFixup =
        diagnostics.allocationContextFixupAfter[0].segmentCommitted;
    diagnostics.segmentReservedAfterFixup =
        diagnostics.allocationContextFixupAfter[0].segmentReserved;
    diagnostics.validAllocatedExtentAfterFixup = allocationContextFixupLastObjectEnd();
    diagnostics.unusedTailBytesAfterFixup =
        diagnostics.allocationContextFixupAfter[0].unusedTailBytes;
    diagnostics.allocationContextMetadataMutationCompleted = 1u;
    const guidexos_nativeaot_allocation_context_snapshot& before =
        diagnostics.allocationContextFixupBefore[0];
    const guidexos_nativeaot_allocation_context_snapshot& after =
        diagnostics.allocationContextFixupAfter[0];
    diagnostics.allocationContextFixupContextsChanged =
        before.allocPtr != after.allocPtr || before.allocLimit != after.allocLimit ||
        before.allocBytes != after.allocBytes || before.allocBytesUoh != after.allocBytesUoh
            ? 1u : 0u;
    diagnostics.allocationContextFixupContextsActiveBefore = before.active;
    diagnostics.allocationContextFixupContextsActiveAfter = after.active;
    diagnostics.allocationContextFixupContextsCleared = after.cleared;
    diagnostics.allocationContextFixupContextsRetired = after.retired;
    if (before.segmentAllocated != after.segmentAllocated ||
        before.segmentCommitted != after.segmentCommitted ||
        before.segmentReserved != after.segmentReserved) {
        ++diagnostics.segmentBookkeepingMutationCount;
    }
    validateAllocationContextFixupObjects(true);
    ++diagnostics.allocationContextFixupCompletionCount;
    g_guideXosAllocationContextFixupPending = false;
}

extern "C" void __cdecl guideXosNativeAotAllocationRootPhaseRequested() {
    guidexos_nativeaot_allocation_diagnostics& diagnostics =
        g_guideXosAllocationDiagnostics;
    ++diagnostics.rootPhaseRequestCount;
    if (diagnostics.allocationContextFixupCompletionCount != 1u ||
        diagnostics.allocationContextMetadataMutationCompleted == 0u) {
        allocationContextFixupRootInvariantFailure();
    }
}

extern "C" __declspec(noreturn) void __cdecl
guideXosNativeAotAllocationContextFixupRootBoundary() {
    guidexos_nativeaot_allocation_diagnostics& diagnostics =
        g_guideXosAllocationDiagnostics;
    ++diagnostics.rootDispatcherEntryCount;
    diagnostics.rootCategorySelected = 1u; // thread statics, then stack roots
    diagnostics.sentinelChecksAtRootBoundary = diagnostics.sentinelValidationCount;
    validateAllocationContextFixupObjects(true);
    if (diagnostics.rootPhaseRequestCount != 1u ||
        diagnostics.allocationContextFixupCompletionCount != 1u ||
        diagnostics.allocationContextFixupContextsVisited != 1u ||
        diagnostics.allocationContextFixupEnumerationComplete != 1u ||
        diagnostics.allocationContextMetadataMutationStarted != 1u ||
        diagnostics.allocationContextMetadataMutationCompleted != 1u ||
        diagnostics.objectMemoryMutationStarted != 0u ||
        diagnostics.rootProviderRequestCount != 0u ||
        diagnostics.rootProviderEntryCount != 0u ||
        diagnostics.firstRootCandidateCount != 0u ||
        diagnostics.rootCallbacksDelivered != 0u ||
        diagnostics.promotionCallbacksDelivered != 0u ||
        diagnostics.markingEntryCount != 0u ||
        diagnostics.sweepingEntryCount != 0u ||
        diagnostics.compactionEntryCount != 0u ||
        diagnostics.relocationEntryCount != 0u ||
        diagnostics.stackScanEntryCount != 0u ||
        diagnostics.staticRootEntryCount != 0u ||
        diagnostics.handleRootEntryCount != 0u ||
        diagnostics.finalizerRootEntryCount != 0u ||
        diagnostics.restartResumeCount != 0u) {
        allocationContextFixupRootInvariantFailure();
    }
    diagnostics.allocationContextFixupRootBoundaryMarker =
        GUIDEXOS_NATIVEAOT_ALLOCATION_CONTEXT_FIXUP_ROOT_BOUNDARY_SAFE_STOP_MARKER;
    diagnostics.allocationContextFixupSafeStopObserved = 1u;
    diagnostics.safeStopObserved = 1u;
    diagnostics.stopReason = diagnostics.allocationContextFixupRootBoundaryMarker;
    diagnostics.allocationContextFixupStopReason = diagnostics.stopReason;
    diagnostics.stage = GUIDEXOS_NATIVEAOT_ALLOC_STAGE_F22_ALLOCATION_CONTEXT_FIXUP_ROOT_BOUNDARY_SAFE_STOP;
    diagnostics.sequence += 1u;
    diagnostics.currentRip = reinterpret_cast<gx_uintptr>(_ReturnAddress());
    diagnostics.currentRsp = reinterpret_cast<gx_uintptr>(_AddressOfReturnAddress());
    diagnostics.rootBoundaryFunction = reinterpret_cast<gx_uintptr>(
        &guideXosNativeAotAllocationContextFixupRootBoundary);
    diagnostics.firstRootProviderFunction = 0u;
    emitAllocationContextFixupRootBoundarySafeStop();
    for (;;) {
    }
}

#if defined(GUIDEXOS_NATIVEAOT_FIRST_PER_THREAD_ROOT_PROVIDER_ALLOCATION)

enum {
    kRootProviderSourceThreadStatics = 1u,
    kRootProviderFunctionInlineThreadStaticList = 1u,
    kRootProviderFunctionThreadStaticStorage = 2u,
    kRootProviderMetadataInlineList = 1u,
    kRootProviderMetadataThreadStaticStorage = 2u,
    kRootProviderSkipNoInlineRoots = 1u,
};

void firstPerThreadRootProviderInvariantFailure() {
    ++g_guideXosAllocationDiagnostics.rootProviderInvariantFailures;
}

[[noreturn]] void firstPerThreadRootProviderSafeStop();
#if defined(GUIDEXOS_NATIVEAOT_FIRST_ROOT_CANDIDATE_LOAD_ALLOCATION)
[[noreturn]] void firstRootCandidateLoadSafeStop();
#endif
#if defined(GUIDEXOS_NATIVEAOT_FIRST_NON_NULL_ROOT_ALLOCATION)
[[noreturn]] void firstNonNullRootCallbackBoundarySafeStop();
#endif

void snapshotFirstPerThreadRootList(bool before) {
    guidexos_nativeaot_allocation_diagnostics& diagnostics =
        g_guideXosAllocationDiagnostics;
    SuspendEeThreadStorePrefix* store = reinterpret_cast<SuspendEeThreadStorePrefix*>(
        GetThreadStore());
    SuspendEeThread* head = store == nullptr ? nullptr : store->head;
    uintptr_t* headField = before
        ? &diagnostics.rootThreadListHeadBefore
        : &diagnostics.rootThreadListHeadAfter;
    uintptr_t* tailField = before
        ? &diagnostics.rootThreadListTailBefore
        : &diagnostics.rootThreadListTailAfter;
    uint32_t* countField = before
        ? &diagnostics.registeredThreadCountBeforeRoot
        : &diagnostics.registeredThreadCountAfterRoot;
    uint32_t* generationField = before
        ? &diagnostics.rootThreadRegistryGenerationBefore
        : &diagnostics.rootThreadRegistryGenerationAfter;
    *headField = reinterpret_cast<gx_uintptr>(head);
    *tailField = 0u;
    *countField = 0u;
    *generationField = diagnostics.threadStoreRegistryMutationAttemptsWhileLocked;

    SuspendEeThread* seen[GUIDEXOS_NATIVEAOT_MAX_ROOT_THREAD_RECORDS] = {};
    SuspendEeThread* current = head;
    for (gx_uint32 ordinal = 0u;
         current != nullptr && ordinal < GUIDEXOS_NATIVEAOT_MAX_ROOT_THREAD_RECORDS;
         ++ordinal) {
        for (gx_uint32 prior = 0u; prior < ordinal; ++prior) {
            if (seen[prior] == current) {
                ++diagnostics.duplicateThreadCount;
                ++diagnostics.threadListIntegrityFailures;
                return;
            }
        }
        seen[ordinal] = current;
        *tailField = reinterpret_cast<gx_uintptr>(current);
        ++*countField;
        current = reinterpret_cast<RuntimeThreadLocals*>(current)->m_pNext;
    }
    if (current != nullptr) {
        ++diagnostics.threadListIntegrityFailures;
    }
}

void recordFirstPerThreadRootThread(SuspendEeThread* thread) {
    guidexos_nativeaot_allocation_diagnostics& diagnostics =
        g_guideXosAllocationDiagnostics;
    ++diagnostics.enumeratedThreadCount;
    if (diagnostics.rootThreadRecordCount >=
        GUIDEXOS_NATIVEAOT_MAX_ROOT_THREAD_RECORDS) {
        ++diagnostics.threadListIntegrityFailures;
        firstPerThreadRootProviderInvariantFailure();
        return;
    }
    const gx_uint32 ordinal = diagnostics.rootThreadRecordCount++;
    guidexos_nativeaot_root_thread_record& record =
        diagnostics.rootThreadRecords[ordinal];
    record = {};
    record.enumerationOrdinal = ordinal + 1u;
    record.runtimeThread = reinterpret_cast<gx_uintptr>(thread);
    if (thread == nullptr) {
        ++diagnostics.threadListIntegrityFailures;
        firstPerThreadRootProviderInvariantFailure();
        return;
    }
    RuntimeThreadLocals* locals = reinterpret_cast<RuntimeThreadLocals*>(thread);
    record.nativeThreadId = static_cast<gx_uintptr>(
        thread->GetPalThreadIdForLogging());
    record.threadStateFlags = locals->m_ThreadStateFlags;
    record.lifecycleState = locals->m_ThreadStateFlags & 0x3u;
    record.initialized = thread->IsInitialized() ? 1u : 0u;
    record.registered = record.initialized &&
        (record.threadStateFlags & 0x1u) != 0u ? 1u : 0u;
    record.cooperative = thread->IsCurrentThreadInCooperativeMode() ? 1u : 0u;
    record.preemptive = record.cooperative == 0u ? 1u : 0u;
    record.gcSpecial = thread->IsGCSpecial() ? 1u : 0u;
    record.stackLow = reinterpret_cast<gx_uintptr>(locals->m_pStackLow);
    record.stackHigh = reinterpret_cast<gx_uintptr>(locals->m_pStackHigh);
    record.allocationContext = reinterpret_cast<gx_uintptr>(
        thread->GetAllocContext());
    record.nextThread = reinterpret_cast<gx_uintptr>(locals->m_pNext);
    SuspendEeThread* current = suspendEeCurrentThread();
    SuspendEeThread* initiator = reinterpret_cast<SuspendEeThread*>(
        diagnostics.suspendEeInitiatingRuntimeThread);
    SuspendEeThread* lockOwner = reinterpret_cast<SuspendEeThread*>(
        diagnostics.threadStoreLockOwner);
    record.collectionInitiatorMatch = thread == initiator ? 1u : 0u;
    record.currentThreadMatch = thread == current ? 1u : 0u;
    record.lockOwnerMatch = thread == lockOwner ? 1u : 0u;
    diagnostics.rootCurrentThreadIdentity = reinterpret_cast<gx_uintptr>(current);
    diagnostics.rootCollectionInitiatorIdentity = reinterpret_cast<gx_uintptr>(initiator);
    diagnostics.rootLockOwnerIdentity = reinterpret_cast<gx_uintptr>(lockOwner);
    diagnostics.rootEnumeratedThreadIdentity = reinterpret_cast<gx_uintptr>(thread);
}

extern "C" void __cdecl
guideXosNativeAotFirstPerThreadRootGcScanRootsEntered(
    int condemned, int maxGeneration, uintptr_t scanContext) {
    guidexos_nativeaot_allocation_diagnostics& diagnostics =
        g_guideXosAllocationDiagnostics;
    ++diagnostics.gcScanRootsRequestCount;
    ++diagnostics.gcScanRootsEntryCount;
    ++diagnostics.foreachThreadRequestCount;
    diagnostics.rootProviderSourceOrderCategory =
        kRootProviderSourceThreadStatics;
    diagnostics.rootCurrentThreadIdentity = reinterpret_cast<gx_uintptr>(
        suspendEeCurrentThread());
    diagnostics.rootCollectionInitiatorIdentity =
        diagnostics.suspendEeInitiatingRuntimeThread;
    diagnostics.rootLockOwnerIdentity = diagnostics.threadStoreLockOwner;
    diagnostics.rootCondemnedGeneration = static_cast<uint32_t>(condemned);
    diagnostics.rootMaximumGeneration = static_cast<uint32_t>(maxGeneration);
    diagnostics.rootScanContextIdentity = scanContext;
    if (scanContext != 0u) {
        const GuideXosScanContextPrefix* context =
            reinterpret_cast<const GuideXosScanContextPrefix*>(scanContext);
        diagnostics.rootScanContextPromotion = context->promotion ? 1u : 0u;
        diagnostics.rootScanContextConcurrent = context->concurrent ? 1u : 0u;
    }
    snapshotFirstPerThreadRootList(true);
    if (diagnostics.gcScanRootsEntryCount != 1u ||
        diagnostics.suspendEeEntryCount != 1u ||
        diagnostics.suspendEeSuspensionCount != 1u ||
        diagnostics.threadStoreLockAcquisitionCount != 1u ||
        diagnostics.managedEntryProhibited == 0u ||
        diagnostics.eeSuspended == 0u ||
        diagnostics.registeredThreadCountBeforeRoot != 1u ||
        diagnostics.rootThreadListHeadBefore == 0u) {
        firstPerThreadRootProviderInvariantFailure();
    }
}

extern "C" void __cdecl
guideXosNativeAotFirstPerThreadRootForeachThreadEntered() {
    guidexos_nativeaot_allocation_diagnostics& diagnostics =
        g_guideXosAllocationDiagnostics;
    ++diagnostics.foreachThreadEntryCount;
#if defined(GUIDEXOS_NATIVEAOT_NEXT_GENUINE_ROOT_PROVIDER_ALLOCATION)
    suspendEeSerialPutString(
        "[nativeaot-gc-next-genuine-root-provider] FOREACH_THREAD entry\n");
#endif
    if (diagnostics.foreachThreadRequestCount != 1u ||
        diagnostics.threadStoreLockAcquisitionCount != 1u ||
        diagnostics.eeSuspended == 0u) {
        firstPerThreadRootProviderInvariantFailure();
    }
}

extern "C" void __cdecl
guideXosNativeAotFirstPerThreadRootIteratorInitialized() {
    guidexos_nativeaot_allocation_diagnostics& diagnostics =
        g_guideXosAllocationDiagnostics;
    ++diagnostics.threadIteratorInitializationCount;
#if defined(GUIDEXOS_NATIVEAOT_NEXT_GENUINE_ROOT_PROVIDER_ALLOCATION)
    suspendEeSerialPutString(
        "[nativeaot-gc-next-genuine-root-provider] thread iterator initialized\n");
#endif
    if (diagnostics.foreachThreadEntryCount != 1u ||
        diagnostics.rootThreadListHeadBefore == 0u) {
        firstPerThreadRootProviderInvariantFailure();
    }
}

extern "C" void __cdecl
guideXosNativeAotFirstPerThreadRootIteratorCompletion() {
    ++g_guideXosAllocationDiagnostics.threadIteratorCompletionCount;
}

extern "C" void __cdecl
guideXosNativeAotFirstPerThreadRootThreadEnumerated(uintptr_t thread) {
#if defined(GUIDEXOS_NATIVEAOT_NEXT_GENUINE_ROOT_PROVIDER_ALLOCATION)
    suspendEeSerialPutString(
        "[nativeaot-gc-next-genuine-root-provider] thread enumerated\n");
#endif
    recordFirstPerThreadRootThread(reinterpret_cast<SuspendEeThread*>(thread));
}

extern "C" void __cdecl
guideXosNativeAotFirstPerThreadRootThreadExcluded(uintptr_t thread,
                                                   uint32_t reason) {
    guidexos_nativeaot_allocation_diagnostics& diagnostics =
        g_guideXosAllocationDiagnostics;
    ++diagnostics.excludedThreadCount;
    if (diagnostics.rootThreadRecordCount != 0u) {
        guidexos_nativeaot_root_thread_record& record =
            diagnostics.rootThreadRecords[diagnostics.rootThreadRecordCount - 1u];
        if (record.runtimeThread == thread) {
            record.excluded = 1u;
            record.inclusionReason = reason;
        }
    }
}

extern "C" void __cdecl
guideXosNativeAotFirstPerThreadRootThreadIncluded(uintptr_t thread) {
    guidexos_nativeaot_allocation_diagnostics& diagnostics =
        g_guideXosAllocationDiagnostics;
    ++diagnostics.includedThreadCount;
#if defined(GUIDEXOS_NATIVEAOT_NEXT_GENUINE_ROOT_PROVIDER_ALLOCATION)
    suspendEeSerialPutString(
        "[nativeaot-gc-next-genuine-root-provider] thread included\n");
#endif
    ++diagnostics.rootPerThreadDispatchRequestCount;
    ++diagnostics.rootPerThreadDispatchEntryCount;
    if (diagnostics.rootThreadRecordCount == 0u) {
        firstPerThreadRootProviderInvariantFailure();
        return;
    }
    guidexos_nativeaot_root_thread_record& record =
        diagnostics.rootThreadRecords[diagnostics.rootThreadRecordCount - 1u];
    if (record.runtimeThread != thread || record.registered == 0u) {
        firstPerThreadRootProviderInvariantFailure();
    }
    record.included = 1u;
    record.inclusionReason = 0u;
    diagnostics.firstRootProviderThread = thread;
}

extern "C" void __cdecl
guideXosNativeAotFirstPerThreadRootThreadStaticListObserved(uintptr_t thread,
                                                             uintptr_t list) {
    guidexos_nativeaot_allocation_diagnostics& diagnostics =
        g_guideXosAllocationDiagnostics;
    ++diagnostics.rootProviderRequestCount;
#if defined(GUIDEXOS_NATIVEAOT_NEXT_GENUINE_ROOT_PROVIDER_ALLOCATION)
    suspendEeSerialPutString(
        "[nativeaot-gc-next-genuine-root-provider] inline provider observed\n");
#endif
    if (list == 0u) {
        ++diagnostics.rootProviderSkipCount;
        diagnostics.rootProviderSkipReason = kRootProviderSkipNoInlineRoots;
        return;
    }
    ++diagnostics.rootProviderEntryCount;
    diagnostics.rootProviderRuntimeCategory = kRootProviderSourceThreadStatics;
    diagnostics.rootProviderFunctionCode =
        kRootProviderFunctionInlineThreadStaticList;
    diagnostics.rootProviderMetadataKind = kRootProviderMetadataInlineList;
    diagnostics.firstRootProviderThread = thread;
    diagnostics.firstRootProviderMetadataContainer = list;
    for (uintptr_t current = list; current != 0u &&
         diagnostics.metadataContainerCount < GUIDEXOS_NATIVEAOT_MAX_ROOT_THREAD_RECORDS;
         current = reinterpret_cast<uintptr_t>(
             reinterpret_cast<InlinedThreadStaticRoot*>(current)->m_next)) {
         ++diagnostics.metadataContainerCount;
    }
#if defined(GUIDEXOS_NATIVEAOT_FIRST_NON_NULL_ROOT_ALLOCATION)
    diagnostics.firstRootCandidateMetadataLocation = list;
    diagnostics.candidateMetadataContainerIdentity = list;
    diagnostics.threadStaticProofStorageAddress = list;
#else
    firstPerThreadRootProviderSafeStop();
#endif
}

extern "C" void __cdecl
guideXosNativeAotFirstPerThreadRootThreadStaticStorageEntered(uintptr_t thread,
                                                               uintptr_t storage) {
    guidexos_nativeaot_allocation_diagnostics& diagnostics =
        g_guideXosAllocationDiagnostics;
    ++diagnostics.rootProviderRequestCount;
    ++diagnostics.rootProviderEntryCount;
#if defined(GUIDEXOS_NATIVEAOT_NEXT_GENUINE_ROOT_PROVIDER_ALLOCATION)
    suspendEeSerialPutString(
        "[nativeaot-gc-next-genuine-root-provider] ordinary provider entered\n");
#endif
    diagnostics.rootProviderRuntimeCategory = kRootProviderSourceThreadStatics;
    diagnostics.rootProviderFunctionCode = kRootProviderFunctionThreadStaticStorage;
    diagnostics.rootProviderMetadataKind = kRootProviderMetadataThreadStaticStorage;
    diagnostics.threadStaticStorageRequested = 1u;
    diagnostics.metadataContainerCount = 1u;
    diagnostics.firstRootProviderThread = thread;
    diagnostics.firstRootProviderMetadataContainer = storage;
    diagnostics.candidateMetadataLocationCount = 1u;
    diagnostics.firstRootCandidateMetadataLocation = storage;
#if defined(GUIDEXOS_NATIVEAOT_FIRST_ROOT_CANDIDATE_LOAD_ALLOCATION) || defined(GUIDEXOS_NATIVEAOT_FIRST_NON_NULL_ROOT_ALLOCATION)
    diagnostics.candidateSlotAddress = storage;
    diagnostics.candidateMetadataContainerIdentity = storage;
    diagnostics.candidateProviderThreadIdentity = thread;
    diagnostics.candidateOwnerThreadIdentity = thread;
    diagnostics.candidateProviderFunctionCode =
        kRootProviderFunctionThreadStaticStorage;
    validateAllocationContextFixupObjects(true);
    diagnostics.candidateObjectValidationBeforeLoadCount =
        diagnostics.objectHistoryCount;
#if defined(GUIDEXOS_NATIVEAOT_FIRST_NON_NULL_ROOT_ALLOCATION)
    diagnostics.threadStaticProofStorageAddress = storage;
#endif
#else
    firstPerThreadRootProviderSafeStop();
#endif
}

#if defined(GUIDEXOS_NATIVEAOT_FIRST_ROOT_CANDIDATE_LOAD_ALLOCATION)

enum {
    kCandidateKnownAddressNone = 0u,
    kCandidateKnownAddressNull = 1u,
    kCandidateKnownAddressRuntimeThread = 2u,
    kCandidateKnownAddressMetadataContainer = 3u,
    kCandidateKnownAddressSlot = 4u,
    kCandidateKnownAddressAllocationContext = 5u,
    kCandidateKnownAddressSentinel = 6u,
    kCandidateKnownAddressObject = 7u,
    kCandidateKnownAddressHeapBase = 8u,
    kCandidateKnownAddressHeapReservedEnd = 9u,
};

uint32_t classifyFirstRootCandidateKnownAddress(uintptr_t rawValue) {
    const guidexos_nativeaot_allocation_diagnostics& diagnostics =
        g_guideXosAllocationDiagnostics;
    if (rawValue == 0u) {
        return kCandidateKnownAddressNull;
    }
    if (rawValue == diagnostics.candidateOwnerThreadIdentity) {
        return kCandidateKnownAddressRuntimeThread;
    }
    if (rawValue == diagnostics.candidateMetadataContainerIdentity) {
        return kCandidateKnownAddressMetadataContainer;
    }
    if (rawValue == diagnostics.candidateSlotAddress) {
        return kCandidateKnownAddressSlot;
    }
    if (rawValue == diagnostics.rootThreadRecords[0].allocationContext) {
        return kCandidateKnownAddressAllocationContext;
    }
    for (uint32_t index = 0u;
         index < diagnostics.objectHistoryCount &&
         index < GUIDEXOS_NATIVEAOT_MAX_OBJECT_HISTORY;
         ++index) {
        const guidexos_nativeaot_object_history_entry& entry =
            diagnostics.objectHistory[index];
        if (rawValue == entry.address) {
            return entry.sentinel != 0u
                ? kCandidateKnownAddressSentinel
                : kCandidateKnownAddressObject;
        }
    }
    if (rawValue == diagnostics.heapBase) {
        return kCandidateKnownAddressHeapBase;
    }
    if (rawValue == diagnostics.heapReserved) {
        return kCandidateKnownAddressHeapReservedEnd;
    }
    return kCandidateKnownAddressNone;
}

extern "C" void __cdecl
guideXosNativeAotFirstRootCandidateLoadRequested(uintptr_t slot,
                                                 uint32_t flags,
                                                 uintptr_t callback,
                                                 uintptr_t scanContext) {
    guidexos_nativeaot_allocation_diagnostics& diagnostics =
        g_guideXosAllocationDiagnostics;
    ++diagnostics.candidateLoadRequestCount;
    ++diagnostics.candidateLoadEntryCount;
    diagnostics.candidateSlotAddress = slot;
    diagnostics.candidateLoadAddress = slot;
    diagnostics.candidateSlotWidth = static_cast<uint32_t>(sizeof(uintptr_t));
    diagnostics.candidateSlotAlignment =
        static_cast<uint32_t>(slot & (sizeof(uintptr_t) - 1u));
    diagnostics.candidateRawRootFlags = flags;
    diagnostics.candidateRootKind = 1u;
    diagnostics.candidateCallbackIdentity = callback;
    diagnostics.candidateScanContextIdentity = scanContext;
    diagnostics.candidateProviderThreadIdentity =
        diagnostics.firstRootProviderThread;
    diagnostics.candidateOwnerThreadIdentity =
        diagnostics.rootEnumeratedThreadIdentity;
    diagnostics.candidateMetadataContainerIdentity =
        diagnostics.firstRootProviderMetadataContainer;
    diagnostics.candidateProviderFunctionCode =
        kRootProviderFunctionThreadStaticStorage;
    diagnostics.candidateSlotStable =
        slot == diagnostics.firstRootCandidateMetadataLocation ? 1u : 0u;
    diagnostics.candidateSlotExpectedThreadStorage =
        diagnostics.candidateSlotStable != 0u &&
        diagnostics.candidateProviderThreadIdentity ==
            diagnostics.candidateOwnerThreadIdentity ? 1u : 0u;
    diagnostics.candidateSlotOverlapsRuntimeThread =
        diagnostics.candidateSlotExpectedThreadStorage;
    diagnostics.candidateSlotOverlapsManagedHeap =
        diagnostics.heapBase != 0u && diagnostics.heapReserved >= diagnostics.heapBase &&
        slot >= diagnostics.heapBase && slot < diagnostics.heapReserved ? 1u : 0u;
    diagnostics.candidateSlotOverlapsAllocationContext =
        slot == diagnostics.rootThreadRecords[0].allocationContext ? 1u : 0u;
    diagnostics.candidateSlotOverlapsNativeStack = 0u;
    diagnostics.candidateSlotOverlapsOtherKnownRegion = 0u;
    if (diagnostics.candidateSlotExpectedThreadStorage != 0u &&
        slot >= diagnostics.candidateOwnerThreadIdentity) {
        diagnostics.candidateSlotOffsetFromThread =
            slot - diagnostics.candidateOwnerThreadIdentity;
    }
    /* The locked return type is a mutable Object**, so writeability is a
       source-level contract. No write probe is performed because the slot is
       not allowed to change in this milestone. */
    diagnostics.candidateSlotWritableContract = 1u;
    if (diagnostics.candidateLoadRequestCount != 1u ||
        diagnostics.candidateLoadEntryCount != 1u ||
        diagnostics.candidateSlotStable == 0u ||
        diagnostics.candidateSlotAlignment != 0u ||
        diagnostics.candidateSlotWidth != sizeof(uintptr_t) ||
        diagnostics.candidateSlotExpectedThreadStorage == 0u ||
        diagnostics.candidateSlotOverlapsManagedHeap != 0u ||
        diagnostics.candidateSlotOverlapsAllocationContext != 0u ||
        diagnostics.candidateSlotOverlapsNativeStack != 0u ||
        diagnostics.candidateSlotOverlapsOtherKnownRegion != 0u ||
        diagnostics.rootProviderEntryCount != 1u ||
        diagnostics.threadStoreLockRecursionDepth != 1u ||
        diagnostics.managedEntryProhibited == 0u ||
        diagnostics.eeSuspended == 0u) {
        firstPerThreadRootProviderInvariantFailure();
    }
}

extern "C" __declspec(noreturn) void __cdecl
guideXosNativeAotFirstRootCandidateMachineWordLoaded(uintptr_t slot,
                                                     uintptr_t rawValue) {
    guidexos_nativeaot_allocation_diagnostics& diagnostics =
        g_guideXosAllocationDiagnostics;
    ++diagnostics.candidateMachineWordLoadCount;
    if (diagnostics.candidateMachineWordLoadCount != 1u ||
        diagnostics.candidateLoadEntryCount != 1u) {
        ++diagnostics.candidateDuplicateLoadCount;
    }
    diagnostics.candidateLoadAddress = slot;
    diagnostics.candidateRawValue = rawValue;
    diagnostics.candidateLoadSuccess = 1u;
    diagnostics.candidateSlotMapped = 1u;
    diagnostics.candidateSlotCommitted = 1u;
    diagnostics.candidateValueIsNull = rawValue == 0u ? 1u : 0u;
    diagnostics.candidateKnownAddressMatch =
        classifyFirstRootCandidateKnownAddress(rawValue);
    if (slot != diagnostics.candidateSlotAddress ||
        slot != diagnostics.firstRootCandidateMetadataLocation) {
        ++diagnostics.candidateDuplicateLoadCount;
        firstPerThreadRootProviderInvariantFailure();
    }

    validateAllocationContextFixupObjects(true);
    diagnostics.candidateObjectValidationAfterLoadCount =
        diagnostics.objectHistoryCount;
    firstRootCandidateLoadSafeStop();
}

void emitFirstRootCandidateLoadSafeStop() {
    const guidexos_nativeaot_allocation_diagnostics& diagnostics =
        g_guideXosAllocationDiagnostics;
    suspendEeSerialPutString(
        "[nativeaot-gc-first-root-candidate-load] SAFE_STOP marker=");
    suspendEeSerialPutHex32(diagnostics.candidateStopReason);
    suspendEeSerialPutString(" gcScanRootsRequest=");
    suspendEeSerialPutHex32(diagnostics.gcScanRootsRequestCount);
    suspendEeSerialPutString(" gcScanRootsEntry=");
    suspendEeSerialPutHex32(diagnostics.gcScanRootsEntryCount);
    suspendEeSerialPutString(" foreachRequest=");
    suspendEeSerialPutHex32(diagnostics.foreachThreadRequestCount);
    suspendEeSerialPutString(" foreachEntry=");
    suspendEeSerialPutHex32(diagnostics.foreachThreadEntryCount);
    suspendEeSerialPutString(" iteratorInit=");
    suspendEeSerialPutHex32(diagnostics.threadIteratorInitializationCount);
    suspendEeSerialPutString(" registeredBefore=");
    suspendEeSerialPutHex32(diagnostics.registeredThreadCountBeforeRoot);
    suspendEeSerialPutString(" registeredAfter=");
    suspendEeSerialPutHex32(diagnostics.registeredThreadCountAfterRoot);
    suspendEeSerialPutString(" enumerated=");
    suspendEeSerialPutHex32(diagnostics.enumeratedThreadCount);
    suspendEeSerialPutString(" included=");
    suspendEeSerialPutHex32(diagnostics.includedThreadCount);
    suspendEeSerialPutString(" excluded=");
    suspendEeSerialPutHex32(diagnostics.excludedThreadCount);
    suspendEeSerialPutString(" listIntegrityFailures=");
    suspendEeSerialPutHex32(diagnostics.threadListIntegrityFailures);
    suspendEeSerialPutString(" duplicates=");
    suspendEeSerialPutHex32(diagnostics.duplicateThreadCount);
    suspendEeSerialPutString(" registryMutationBefore=");
    suspendEeSerialPutHex32(diagnostics.rootThreadRegistryGenerationBefore);
    suspendEeSerialPutString(" registryMutationAfter=");
    suspendEeSerialPutHex32(diagnostics.rootThreadRegistryGenerationAfter);
    suspendEeSerialPutString(" current=");
    suspendEeSerialPutHex64(diagnostics.rootCurrentThreadIdentity);
    suspendEeSerialPutString(" enumeratedThread=");
    suspendEeSerialPutHex64(diagnostics.rootEnumeratedThreadIdentity);
    suspendEeSerialPutString(" initiator=");
    suspendEeSerialPutHex64(diagnostics.rootCollectionInitiatorIdentity);
    suspendEeSerialPutString(" lockOwner=");
    suspendEeSerialPutHex64(diagnostics.rootLockOwnerIdentity);
    suspendEeSerialPutString(" nativeId=");
    suspendEeSerialPutHex64(diagnostics.rootThreadRecords[0].nativeThreadId);
    suspendEeSerialPutString(" lifecycle=");
    suspendEeSerialPutHex32(diagnostics.rootThreadRecords[0].lifecycleState);
    suspendEeSerialPutString(" stateFlags=");
    suspendEeSerialPutHex32(diagnostics.rootThreadRecords[0].threadStateFlags);
    suspendEeSerialPutString(" cooperative=");
    suspendEeSerialPutHex32(diagnostics.rootThreadRecords[0].cooperative);
    suspendEeSerialPutString(" preemptive=");
    suspendEeSerialPutHex32(diagnostics.rootThreadRecords[0].preemptive);
    suspendEeSerialPutString(" allocContext=");
    suspendEeSerialPutHex64(diagnostics.rootThreadRecords[0].allocationContext);
    suspendEeSerialPutString(" stackLow=");
    suspendEeSerialPutHex64(diagnostics.rootThreadRecords[0].stackLow);
    suspendEeSerialPutString(" stackHigh=");
    suspendEeSerialPutHex64(diagnostics.rootThreadRecords[0].stackHigh);
    suspendEeSerialPutString(" providerSource=thread-static-provider providerRuntime=thread-static-provider providerFunction=");
    suspendEeSerialPutHex32(diagnostics.candidateProviderFunctionCode);
    suspendEeSerialPutString(" providerThread=");
    suspendEeSerialPutHex64(diagnostics.candidateProviderThreadIdentity);
    suspendEeSerialPutString(" providerRequests=");
    suspendEeSerialPutHex32(diagnostics.rootProviderRequestCount);
    suspendEeSerialPutString(" providerEntries=");
    suspendEeSerialPutHex32(diagnostics.rootProviderEntryCount);
    suspendEeSerialPutString(" providerSkips=");
    suspendEeSerialPutHex32(diagnostics.rootProviderSkipCount);
    suspendEeSerialPutString(" metadataContainers=");
    suspendEeSerialPutHex32(diagnostics.metadataContainerCount);
    suspendEeSerialPutString(" metadataContainer=");
    suspendEeSerialPutHex64(diagnostics.candidateMetadataContainerIdentity);
    suspendEeSerialPutString(" candidateSlot=");
    suspendEeSerialPutHex64(diagnostics.candidateSlotAddress);
    suspendEeSerialPutString(" slotOffset=");
    suspendEeSerialPutHex64(diagnostics.candidateSlotOffsetFromThread);
    suspendEeSerialPutString(" slotWidth=");
    suspendEeSerialPutHex32(diagnostics.candidateSlotWidth);
    suspendEeSerialPutString(" slotAlignment=");
    suspendEeSerialPutHex32(diagnostics.candidateSlotAlignment);
    suspendEeSerialPutString(" slotMapped=");
    suspendEeSerialPutHex32(diagnostics.candidateSlotMapped);
    suspendEeSerialPutString(" slotCommitted=");
    suspendEeSerialPutHex32(diagnostics.candidateSlotCommitted);
    suspendEeSerialPutString(" slotWritableContract=");
    suspendEeSerialPutHex32(diagnostics.candidateSlotWritableContract);
    suspendEeSerialPutString(" slotStable=");
    suspendEeSerialPutHex32(diagnostics.candidateSlotStable);
    suspendEeSerialPutString(" slotExpectedThreadStorage=");
    suspendEeSerialPutHex32(diagnostics.candidateSlotExpectedThreadStorage);
    suspendEeSerialPutString(" slotOverlapHeap=");
    suspendEeSerialPutHex32(diagnostics.candidateSlotOverlapsManagedHeap);
    suspendEeSerialPutString(" slotOverlapThread=");
    suspendEeSerialPutHex32(diagnostics.candidateSlotOverlapsRuntimeThread);
    suspendEeSerialPutString(" slotOverlapAllocContext=");
    suspendEeSerialPutHex32(diagnostics.candidateSlotOverlapsAllocationContext);
    suspendEeSerialPutString(" slotOverlapStack=");
    suspendEeSerialPutHex32(diagnostics.candidateSlotOverlapsNativeStack);
    suspendEeSerialPutString(" slotOverlapOther=");
    suspendEeSerialPutHex32(diagnostics.candidateSlotOverlapsOtherKnownRegion);
    suspendEeSerialPutString(" rootFlags=");
    suspendEeSerialPutHex32(diagnostics.candidateRawRootFlags);
    suspendEeSerialPutString(" rootKind=");
    suspendEeSerialPutHex32(diagnostics.candidateRootKind);
    suspendEeSerialPutString(" callback=");
    suspendEeSerialPutHex64(diagnostics.candidateCallbackIdentity);
    suspendEeSerialPutString(" scanContext=");
    suspendEeSerialPutHex64(diagnostics.candidateScanContextIdentity);
    suspendEeSerialPutString(" loadRequests=");
    suspendEeSerialPutHex32(diagnostics.candidateLoadRequestCount);
    suspendEeSerialPutString(" loadEntries=");
    suspendEeSerialPutHex32(diagnostics.candidateLoadEntryCount);
    suspendEeSerialPutString(" machineWordLoads=");
    suspendEeSerialPutHex32(diagnostics.candidateMachineWordLoadCount);
    suspendEeSerialPutString(" duplicateLoads=");
    suspendEeSerialPutHex32(diagnostics.candidateDuplicateLoadCount);
    suspendEeSerialPutString(" loadFaults=");
    suspendEeSerialPutHex32(diagnostics.candidateLoadFaultCount);
    suspendEeSerialPutString(" loadAddress=");
    suspendEeSerialPutHex64(diagnostics.candidateLoadAddress);
    suspendEeSerialPutString(" rawValue=");
    suspendEeSerialPutHex64(diagnostics.candidateRawValue);
    suspendEeSerialPutString(" valueIsNull=");
    suspendEeSerialPutHex32(diagnostics.candidateValueIsNull);
    suspendEeSerialPutString(" knownAddressMatch=");
    suspendEeSerialPutHex32(diagnostics.candidateKnownAddressMatch);
    suspendEeSerialPutString(" candidateDereferences=");
    suspendEeSerialPutHex32(diagnostics.candidatePointeeDereferenceCount);
    suspendEeSerialPutString(" heapMembershipTests=");
    suspendEeSerialPutHex32(diagnostics.candidateHeapMembershipTestCount);
    suspendEeSerialPutString(" objectHeaders=");
    suspendEeSerialPutHex32(diagnostics.candidateObjectHeaderInspectionCount);
    suspendEeSerialPutString(" methodTables=");
    suspendEeSerialPutHex32(diagnostics.candidateMethodTableInspectionCount);
    suspendEeSerialPutString(" rootFlagApplications=");
    suspendEeSerialPutHex32(diagnostics.candidateRootFlagApplicationCount);
    suspendEeSerialPutString(" candidates=");
    suspendEeSerialPutHex32(diagnostics.candidateRootCandidateDiscoveryCount);
    suspendEeSerialPutString(" callbacks=");
    suspendEeSerialPutHex32(diagnostics.candidateRootCallbacksDelivered);
    suspendEeSerialPutString(" promotions=");
    suspendEeSerialPutHex32(diagnostics.candidatePromotionCallbacksDelivered);
    suspendEeSerialPutString(" marking=");
    suspendEeSerialPutHex32(diagnostics.markingEntryCount);
    suspendEeSerialPutString(" objectMutation=");
    suspendEeSerialPutHex32(diagnostics.objectMemoryMutationStarted);
    suspendEeSerialPutString(" restartRequests=");
    suspendEeSerialPutHex32(diagnostics.restartRequestCount);
    suspendEeSerialPutString(" restartEntries=");
    suspendEeSerialPutHex32(diagnostics.restartEntryCount);
    suspendEeSerialPutString(" managedResume=");
    suspendEeSerialPutHex32(diagnostics.managedResumeCount);
    suspendEeSerialPutString(" userAllocations=");
    suspendEeSerialPutHex32(diagnostics.allocationCount);
    suspendEeSerialPutString(" userAllocationRequests=");
    suspendEeSerialPutHex32(diagnostics.allocationRequestCount);
    suspendEeSerialPutString(" userFast=");
    suspendEeSerialPutHex32(diagnostics.fastAllocationCount);
    suspendEeSerialPutString(" userRare=");
    suspendEeSerialPutHex32(diagnostics.rarePathCount);
    suspendEeSerialPutString(" userRefills=");
    suspendEeSerialPutHex32(diagnostics.allocationContextRefillCount);
    suspendEeSerialPutString(" userSameSegmentCommits=");
    suspendEeSerialPutHex32(diagnostics.heapCommitEventCount);
    suspendEeSerialPutString(" userSegmentTransitions=");
    suspendEeSerialPutHex32(diagnostics.segmentTransitionCount);
    suspendEeSerialPutString(" collectionAllocationOrdinal=");
    suspendEeSerialPutHex32(diagnostics.collectionRequestAllocationOrdinal);
    suspendEeSerialPutString(" runtimeThreadStaticStorageAllocations=");
    suspendEeSerialPutHex32(diagnostics.runtimeThreadStaticStorageAllocationCount);
    suspendEeSerialPutString(" runtimeThreadStaticStoragePublications=");
    suspendEeSerialPutHex32(diagnostics.runtimeThreadStaticStoragePublicationCount);
    suspendEeSerialPutString(" runtimeThreadStaticStorageObject=");
    suspendEeSerialPutHex64(diagnostics.runtimeThreadStaticStorageObjectAddress);
    suspendEeSerialPutString(" runtimeThreadStaticInlinedRoot=");
    suspendEeSerialPutHex64(diagnostics.runtimeThreadStaticInlinedRootAddress);
    suspendEeSerialPutString(" totalAllocationRequestsObserved=");
    suspendEeSerialPutHex32(
        diagnostics.allocationRequestCount +
        diagnostics.runtimeThreadStaticStorageAllocationCount);
    suspendEeSerialPutString(" condemnedGeneration=");
    suspendEeSerialPutHex32(diagnostics.rootCondemnedGeneration);
    suspendEeSerialPutString(" maxGeneration=");
    suspendEeSerialPutHex32(diagnostics.rootMaximumGeneration);
    suspendEeSerialPutString(" scanContextPromotion=");
    suspendEeSerialPutHex32(diagnostics.rootScanContextPromotion);
    suspendEeSerialPutString(" scanContextConcurrent=");
    suspendEeSerialPutHex32(diagnostics.rootScanContextConcurrent);
    suspendEeSerialPutString(" scanContextIdentity=");
    suspendEeSerialPutHex64(diagnostics.rootScanContextIdentity);
    suspendEeSerialPutString(" objectBeforeLoad=");
    suspendEeSerialPutHex32(diagnostics.candidateObjectValidationBeforeLoadCount);
    suspendEeSerialPutString(" objectAfterLoad=");
    suspendEeSerialPutHex32(diagnostics.candidateObjectValidationAfterLoadCount);
    suspendEeSerialPutString(" objectAtStop=");
    suspendEeSerialPutHex32(diagnostics.candidateObjectValidationAtStopCount);
    suspendEeSerialPutString(" sentinelChecks=");
    suspendEeSerialPutHex32(diagnostics.sentinelChecksAtRootBoundary);
    suspendEeSerialPutString(" fixupFailures=");
    suspendEeSerialPutHex32(diagnostics.allocationContextFixupInvariantFailures);
    suspendEeSerialPutString(" rootFailures=");
    suspendEeSerialPutHex32(diagnostics.rootProviderInvariantFailures);
    suspendEeSerialPutString(" eeSuspended=");
    suspendEeSerialPutHex32(diagnostics.eeSuspended);
    suspendEeSerialPutString(" lockDepth=");
    suspendEeSerialPutHex32(diagnostics.threadStoreLockRecursionDepth);
    suspendEeSerialPutString(" marker=C011EC05\n");
}

[[noreturn]] void firstRootCandidateLoadSafeStop() {
    guidexos_nativeaot_allocation_diagnostics& diagnostics =
        g_guideXosAllocationDiagnostics;
    ++diagnostics.candidateSafeStopObserved;
    validateAllocationContextFixupObjects(true);
    diagnostics.candidateObjectValidationAtStopCount =
        diagnostics.objectHistoryCount;
    diagnostics.sentinelChecksAtRootBoundary = diagnostics.sentinelValidationCount;
    snapshotFirstPerThreadRootList(false);
    diagnostics.threadRegistryMutationCountBeforeRoot =
        diagnostics.rootThreadRegistryGenerationBefore;
    diagnostics.threadRegistryMutationCountAfterRoot =
        diagnostics.rootThreadRegistryGenerationAfter;
    const bool valid =
        diagnostics.candidateSafeStopObserved == 1u &&
        diagnostics.gcScanRootsEntryCount == 1u &&
        diagnostics.foreachThreadEntryCount == 1u &&
        diagnostics.threadIteratorInitializationCount == 1u &&
        diagnostics.registeredThreadCountBeforeRoot == 1u &&
        diagnostics.registeredThreadCountAfterRoot == 1u &&
        diagnostics.enumeratedThreadCount == 1u &&
        diagnostics.includedThreadCount == 1u &&
        diagnostics.excludedThreadCount == 0u &&
        diagnostics.rootThreadRecordCount == 1u &&
        diagnostics.duplicateThreadCount == 0u &&
        diagnostics.threadListIntegrityFailures == 0u &&
        diagnostics.rootThreadListHeadBefore == diagnostics.rootThreadListHeadAfter &&
        diagnostics.rootThreadListTailBefore == diagnostics.rootThreadListTailAfter &&
        diagnostics.candidateLoadRequestCount == 1u &&
        diagnostics.candidateLoadEntryCount == 1u &&
        diagnostics.candidateMachineWordLoadCount == 1u &&
        diagnostics.candidateDuplicateLoadCount == 0u &&
        diagnostics.candidateLoadFaultCount == 0u &&
        diagnostics.candidateLoadSuccess == 1u &&
        diagnostics.candidateSlotMapped == 1u &&
        diagnostics.candidateSlotCommitted == 1u &&
        diagnostics.candidateSlotWritableContract == 1u &&
        diagnostics.candidateSlotStable == 1u &&
        diagnostics.candidateSlotExpectedThreadStorage == 1u &&
        diagnostics.candidateSlotOverlapsManagedHeap == 0u &&
        diagnostics.candidateSlotOverlapsAllocationContext == 0u &&
        diagnostics.candidateSlotOverlapsNativeStack == 0u &&
        diagnostics.candidateSlotOverlapsOtherKnownRegion == 0u &&
        diagnostics.candidatePointeeDereferenceCount == 0u &&
        diagnostics.candidateHeapMembershipTestCount == 0u &&
        diagnostics.candidateObjectHeaderInspectionCount == 0u &&
        diagnostics.candidateMethodTableInspectionCount == 0u &&
        diagnostics.candidateRootFlagApplicationCount == 0u &&
        diagnostics.candidateRootCandidateDiscoveryCount == 0u &&
        diagnostics.candidateRootCallbacksDelivered == 0u &&
        diagnostics.candidatePromotionCallbacksDelivered == 0u &&
        diagnostics.markingEntryCount == 0u &&
        diagnostics.objectMemoryMutationStarted == 0u &&
        diagnostics.restartRequestCount == 0u &&
        diagnostics.restartEntryCount == 0u &&
        diagnostics.managedResumeCount == 0u &&
        diagnostics.candidateObjectValidationBeforeLoadCount == 40u &&
        diagnostics.candidateObjectValidationAfterLoadCount == 40u &&
        diagnostics.candidateObjectValidationAtStopCount == 40u &&
        diagnostics.rootProviderInvariantFailures == 0u &&
        diagnostics.allocationContextFixupInvariantFailures == 0u &&
        diagnostics.threadStoreLockRecursionDepth == 1u &&
        diagnostics.managedEntryProhibited == 1u &&
        diagnostics.eeSuspended == 1u;
    if (!valid) {
        ++diagnostics.rootProviderInvariantFailures;
        diagnostics.candidateStopReason = 0xE005u;
        emitFirstRootCandidateLoadSafeStop();
        guideXosFailFast(9u);
    }
    diagnostics.candidateStopReason =
        GUIDEXOS_NATIVEAOT_FIRST_ROOT_CANDIDATE_LOAD_SAFE_STOP_MARKER;
    diagnostics.safeStopObserved = 1u;
    diagnostics.stopReason = diagnostics.candidateStopReason;
    diagnostics.stage = GUIDEXOS_NATIVEAOT_ALLOC_STAGE_F23_FIRST_PER_THREAD_ROOT_PROVIDER_SAFE_STOP;
    diagnostics.rootBoundaryFunction = reinterpret_cast<gx_uintptr>(
        &firstRootCandidateLoadSafeStop);
    emitFirstRootCandidateLoadSafeStop();
    for (;;) {
    }
}

#endif

#if defined(GUIDEXOS_NATIVEAOT_FIRST_ROOT_MEMBERSHIP_CLASSIFICATION_ALLOCATION)

void emitFirstRootMembershipClassificationSafeStop() {
    const guidexos_nativeaot_allocation_diagnostics& diagnostics =
        g_guideXosAllocationDiagnostics;
    suspendEeSerialPutString(
        "[nativeaot-gc-first-root-membership-classification] SAFE_STOP marker=");
    suspendEeSerialPutHex32(diagnostics.membershipSafeStopReason);
    suspendEeSerialPutString(" callbackRequestCount=");
    suspendEeSerialPutHex32(diagnostics.callbackRequestCount);
    suspendEeSerialPutString(" callbackCallSiteCount=");
    suspendEeSerialPutHex32(diagnostics.callbackCallSiteEntryCount);
    suspendEeSerialPutString(" callbackInvocationCount=");
    suspendEeSerialPutHex32(diagnostics.callbackInvocationCount);
    suspendEeSerialPutString(" callbackEntryCount=");
    suspendEeSerialPutHex32(diagnostics.callbackEntryCount);
    suspendEeSerialPutString(" callbackReturnCount=");
    suspendEeSerialPutHex32(diagnostics.callbackReturnCount);
    suspendEeSerialPutString(" secondCallbackAttempts=");
    suspendEeSerialPutHex32(diagnostics.duplicateCallbackInvocationCount);
    suspendEeSerialPutString(" callbackSiteSlot=");
    suspendEeSerialPutHex64(diagnostics.callbackSiteRootSlot);
    suspendEeSerialPutString(" callbackSiteRaw=");
    suspendEeSerialPutHex64(diagnostics.callbackSiteRawRootValue);
    suspendEeSerialPutString(" callbackSiteContext=");
    suspendEeSerialPutHex64(diagnostics.callbackSiteScanContext);
    suspendEeSerialPutString(" callbackSiteCallback=");
    suspendEeSerialPutHex64(diagnostics.callbackSiteCallbackIdentity);
    suspendEeSerialPutString(" callbackEntryAddress=");
    suspendEeSerialPutHex64(diagnostics.callbackEntryAddress);
    suspendEeSerialPutString(" callbackEntryReturn=");
    suspendEeSerialPutHex64(diagnostics.callbackEntryReturnAddress);
    suspendEeSerialPutString(" rawRcx=");
    suspendEeSerialPutHex64(diagnostics.callbackRawArgument1Rcx);
    suspendEeSerialPutString(" rawRdx=");
    suspendEeSerialPutHex64(diagnostics.callbackRawArgument2Rdx);
    suspendEeSerialPutString(" rawR8=");
    suspendEeSerialPutHex64(diagnostics.callbackRawArgument3R8);
    suspendEeSerialPutString(" arg1=");
    suspendEeSerialPutHex64(diagnostics.callbackNormalizedArgument1);
    suspendEeSerialPutString(" arg2=");
    suspendEeSerialPutHex64(diagnostics.callbackNormalizedArgument2);
    suspendEeSerialPutString(" arg3=");
    suspendEeSerialPutHex64(diagnostics.callbackNormalizedArgument3);
    suspendEeSerialPutString(" managedThread=");
    suspendEeSerialPutHex64(diagnostics.callbackEntryManagedThread);
    suspendEeSerialPutString(" currentThread=");
    suspendEeSerialPutHex64(diagnostics.callbackEntryCurrentThread);
    suspendEeSerialPutString(" lockOwner=");
    suspendEeSerialPutHex64(diagnostics.callbackEntryLockOwner);
    suspendEeSerialPutString(" rootSlot=");
    suspendEeSerialPutHex64(diagnostics.callbackRootSlot);
    suspendEeSerialPutString(" callbackLoadedRoot=");
    suspendEeSerialPutHex64(diagnostics.callbackRootSlotLoadedValue);
    suspendEeSerialPutString(" membershipObject=");
    suspendEeSerialPutHex64(diagnostics.membershipObjectInput);
    suspendEeSerialPutString(" objectMatchesCallbackRoot=");
    suspendEeSerialPutHex32(diagnostics.membershipObjectMatchesCallbackRoot);
    suspendEeSerialPutString(" storageObject=");
    suspendEeSerialPutHex64(diagnostics.runtimeThreadStaticStorageObjectAddress);
    suspendEeSerialPutString(" inlinedRoot=");
    suspendEeSerialPutHex64(diagnostics.runtimeThreadStaticInlinedRootAddress);
    suspendEeSerialPutString(" inlinedRoot=");
    suspendEeSerialPutHex64(diagnostics.runtimeThreadStaticInlinedRootAddress);
    suspendEeSerialPutString(" sentinelAddress=");
    suspendEeSerialPutHex64(diagnostics.threadStaticProofSentinelAddress);
    suspendEeSerialPutString(" sentinelReadback=");
    suspendEeSerialPutHex64(diagnostics.threadStaticProofManagedReadbackAddress);
    suspendEeSerialPutString(" membershipRequests=");
    suspendEeSerialPutHex32(diagnostics.membershipRequestCount);
    suspendEeSerialPutString(" membershipEntries=");
    suspendEeSerialPutHex32(diagnostics.membershipEntryCount);
    suspendEeSerialPutString(" membershipCompletions=");
    suspendEeSerialPutHex32(diagnostics.membershipCompletionCount);
    suspendEeSerialPutString(" membershipReturns=");
    suspendEeSerialPutHex32(diagnostics.membershipReturnCount);
    suspendEeSerialPutString(" duplicateChecks=");
    suspendEeSerialPutHex32(diagnostics.membershipDuplicateCheckCount);
    suspendEeSerialPutString(" objectDereferences=");
    suspendEeSerialPutHex32(diagnostics.membershipObjectDereferenceCount);
    suspendEeSerialPutString(" lowerBound=");
    suspendEeSerialPutHex64(diagnostics.membershipLowerBound);
    suspendEeSerialPutString(" upperBound=");
    suspendEeSerialPutHex64(diagnostics.membershipUpperBound);
    suspendEeSerialPutString(" lowerEvaluated=");
    suspendEeSerialPutHex32(diagnostics.membershipLowerComparisonEvaluated);
    suspendEeSerialPutString(" upperEvaluated=");
    suspendEeSerialPutHex32(diagnostics.membershipUpperComparisonEvaluated);
    suspendEeSerialPutString(" lowerResult=");
    suspendEeSerialPutHex32(diagnostics.membershipLowerComparisonResult);
    suspendEeSerialPutString(" upperResult=");
    suspendEeSerialPutHex32(diagnostics.membershipUpperComparisonResult);
    suspendEeSerialPutString(" inFindObjectRange=");
    suspendEeSerialPutHex32(diagnostics.membershipResult);
    suspendEeSerialPutString(" sourceBranch=");
    suspendEeSerialPutHex32(diagnostics.membershipSourceBranch);
    suspendEeSerialPutString(" membershipCompletionReturnAddress=");
    suspendEeSerialPutHex64(diagnostics.membershipCompletionReturnAddress);
    suspendEeSerialPutString(" membershipPostCheckReturnAddress=");
    suspendEeSerialPutHex64(diagnostics.membershipPostCheckReturnAddress);
    suspendEeSerialPutString(" heapIdentity=");
    suspendEeSerialPutHex64(diagnostics.membershipHeapIdentity);
    suspendEeSerialPutString(" heapFieldReads=");
    suspendEeSerialPutHex32(diagnostics.membershipHeapFieldReadCount);
    suspendEeSerialPutString(" segmentIdentity=");
    suspendEeSerialPutHex64(diagnostics.membershipSegmentIdentity);
    suspendEeSerialPutString(" segmentLookupCount=");
    suspendEeSerialPutHex32(diagnostics.membershipSegmentLookupCount);
    suspendEeSerialPutString(" segmentLookupSucceeded=");
    suspendEeSerialPutHex32(diagnostics.membershipSegmentLookupSucceeded);
    suspendEeSerialPutString(" callbackContext=");
    suspendEeSerialPutHex64(diagnostics.callbackContextAddress);
    suspendEeSerialPutString(" contextFieldReads=");
    suspendEeSerialPutHex32(diagnostics.callbackContextFieldReadCount);
    suspendEeSerialPutString(" contextThread=");
    suspendEeSerialPutHex64(diagnostics.callbackContextThreadUnderCrawl);
    suspendEeSerialPutString(" contextStackLimit=");
    suspendEeSerialPutHex64(diagnostics.callbackContextStackLimit);
    suspendEeSerialPutString(" contextThreadNumber=");
    suspendEeSerialPutHex64(diagnostics.callbackEntryScanContextFieldThreadNumberValue);
    suspendEeSerialPutString(" contextThreadCount=");
    suspendEeSerialPutHex64(diagnostics.callbackEntryScanContextFieldThreadCountValue);
    suspendEeSerialPutString(" contextPromotion=");
    suspendEeSerialPutHex32(diagnostics.callbackEntryPromotion);
    suspendEeSerialPutString(" contextConcurrent=");
    suspendEeSerialPutHex32(diagnostics.callbackEntryConcurrent);
    suspendEeSerialPutString(" candidateClassification=");
    suspendEeSerialPutHex32(diagnostics.callbackCandidateClassificationStartCount);
    suspendEeSerialPutString(" generationClassificationStart=");
    suspendEeSerialPutHex32(diagnostics.callbackGenerationClassificationStartCount);
    suspendEeSerialPutString(" generationQueryStart=");
    suspendEeSerialPutHex32(diagnostics.callbackGenerationQueryStartCount);
    suspendEeSerialPutString(" condemnedGenerationComparisons=");
    suspendEeSerialPutHex32(diagnostics.callbackCondemnedGenerationComparisonCount);
    suspendEeSerialPutString(" generationClassification=");
    suspendEeSerialPutHex32(diagnostics.callbackEntryCurrentGeneration);
    suspendEeSerialPutString(" condemnedGeneration=");
    suspendEeSerialPutHex32(diagnostics.callbackEntryCondemnedGeneration);
    suspendEeSerialPutString(" postMembershipSegmentLookup=");
    suspendEeSerialPutHex32(diagnostics.callbackSegmentLookupCount);
    suspendEeSerialPutString(" objectHeaders=");
    suspendEeSerialPutHex32(diagnostics.callbackObjectHeaderReadCount);
    suspendEeSerialPutString(" methodTables=");
    suspendEeSerialPutHex32(diagnostics.callbackMethodTableReadCount);
    suspendEeSerialPutString(" promotionStart=");
    suspendEeSerialPutHex32(diagnostics.callbackPromotionStartCount);
    suspendEeSerialPutString(" promotions=");
    suspendEeSerialPutHex32(diagnostics.callbackPromotionCount);
    suspendEeSerialPutString(" markingStart=");
    suspendEeSerialPutHex32(diagnostics.callbackMarkingStartCount);
    suspendEeSerialPutString(" graphTraversal=");
    suspendEeSerialPutHex32(diagnostics.callbackGraphTraversalCount);
    suspendEeSerialPutString(" markWrites=");
    suspendEeSerialPutHex32(diagnostics.callbackMarkStateWriteCount);
    suspendEeSerialPutString(" promotionWrites=");
    suspendEeSerialPutHex32(diagnostics.callbackPromotionStateWriteCount);
    suspendEeSerialPutString(" objectMutation=");
    suspendEeSerialPutHex32(diagnostics.callbackObjectMemoryMutationCount);
    suspendEeSerialPutString(" gcMetadataMutation=");
    suspendEeSerialPutHex32(diagnostics.callbackGcMetadataMutationCount);
    suspendEeSerialPutString(" segmentMetadataMutation=");
    suspendEeSerialPutHex32(diagnostics.callbackSegmentMetadataMutationCount);
    suspendEeSerialPutString(" managedAssignmentCount=");
    suspendEeSerialPutHex32(diagnostics.threadStaticProofAssignmentCount);
    suspendEeSerialPutString(" managedClearCount=");
    suspendEeSerialPutHex32(diagnostics.threadStaticProofClearCount);
    suspendEeSerialPutString(" managedReadbackCount=");
    suspendEeSerialPutHex32(diagnostics.threadStaticProofReadbackCount);
    suspendEeSerialPutString(" managedAssignmentValid=");
    suspendEeSerialPutHex32(diagnostics.threadStaticProofManagedAssignmentValid);
    suspendEeSerialPutString(" managedReadbackValid=");
    suspendEeSerialPutHex32(diagnostics.threadStaticProofManagedReadbackValid);
    suspendEeSerialPutString(" sentinelChecks=");
    suspendEeSerialPutHex32(diagnostics.sentinelValidationCount);
    suspendEeSerialPutString(" objectBefore=");
    suspendEeSerialPutHex32(diagnostics.candidateObjectValidationBeforeLoadCount);
    suspendEeSerialPutString(" objectAfter=");
    suspendEeSerialPutHex32(diagnostics.candidateObjectValidationAfterLoadCount);
    suspendEeSerialPutString(" objectAtStop=");
    suspendEeSerialPutHex32(diagnostics.candidateObjectValidationAtStopCount);
    suspendEeSerialPutString(" objectHistoryOverflow=");
    suspendEeSerialPutHex32(diagnostics.objectHistoryOverflow);
    suspendEeSerialPutString(" duplicateObjectAddresses=");
    suspendEeSerialPutHex32(diagnostics.duplicateObjectAddressFailures);
    suspendEeSerialPutString(" managedThread=");
    suspendEeSerialPutHex64(diagnostics.callbackEntryManagedThread);
    suspendEeSerialPutString(" currentThread=");
    suspendEeSerialPutHex64(diagnostics.callbackEntryCurrentThread);
    suspendEeSerialPutString(" lockOwner=");
    suspendEeSerialPutHex64(diagnostics.callbackEntryLockOwner);
    suspendEeSerialPutString(" lockHeld=");
    suspendEeSerialPutHex32(diagnostics.callbackEntryThreadStoreLockHeld);
    suspendEeSerialPutString(" eeSuspended=");
    suspendEeSerialPutHex32(diagnostics.callbackEntryEeSuspended);
    suspendEeSerialPutString(" managedEntryProhibited=");
    suspendEeSerialPutHex32(diagnostics.callbackEntryManagedEntryProhibited);
    suspendEeSerialPutString(" callbackContext=");
    suspendEeSerialPutHex64(diagnostics.callbackContextAddress);
    suspendEeSerialPutString(" contextThreadNumber=");
    suspendEeSerialPutHex64(diagnostics.callbackEntryScanContextFieldThreadNumberValue);
    suspendEeSerialPutString(" contextThreadCount=");
    suspendEeSerialPutHex64(diagnostics.callbackEntryScanContextFieldThreadCountValue);
    suspendEeSerialPutString(" contextPromotion=");
    suspendEeSerialPutHex32(diagnostics.callbackEntryPromotion);
    suspendEeSerialPutString(" contextConcurrent=");
    suspendEeSerialPutHex32(diagnostics.callbackEntryConcurrent);
    suspendEeSerialPutString(" restartRequests=");
    suspendEeSerialPutHex32(diagnostics.restartRequestCount);
    suspendEeSerialPutString(" restartEntries=");
    suspendEeSerialPutHex32(diagnostics.restartEntryCount);
    suspendEeSerialPutString(" managedResume=");
    suspendEeSerialPutHex32(diagnostics.managedResumeCount);
    suspendEeSerialPutString(" nextSourceOperation=HEAP_FROM_THREAD@gc.cpp:49494");
    suspendEeSerialPutString(" marker=C011EC08\n");
}

extern "C" void __cdecl
guideXosNativeAotFirstRootMembershipCandidateLoaded(uintptr_t rawValue) {
    guidexos_nativeaot_allocation_diagnostics& diagnostics =
        g_guideXosAllocationDiagnostics;
    ++diagnostics.callbackRootSlotLoadCount;
    diagnostics.callbackRootSlotLoadedValue = rawValue;
    diagnostics.callbackRootRawValue = rawValue;
    diagnostics.callbackFirstSemanticOperation = 1u;
    diagnostics.callbackRootRawValue = rawValue;
    diagnostics.membershipCallbackLoadedRoot = rawValue;
    validateAllocationContextFixupObjects(true);
    diagnostics.candidateObjectValidationAfterLoadCount =
        diagnostics.objectHistoryCount;
    const bool valid =
        diagnostics.callbackSafeStopObserved == 0u &&
        diagnostics.callbackRequestCount == 1u &&
        diagnostics.callbackCallSiteEntryCount == 1u &&
        diagnostics.callbackInvocationCount == 1u &&
        diagnostics.callbackEntryCount == 1u &&
        diagnostics.callbackReturnCount == 0u &&
        diagnostics.duplicateCallbackInvocationCount == 0u &&
        diagnostics.callbackRootSlotLoadCount == 1u &&
        rawValue != 0u &&
        rawValue == diagnostics.runtimeThreadStaticStorageObjectAddress &&
        rawValue == diagnostics.callbackSiteRawRootValue &&
        diagnostics.callbackEntryArgumentsMatch == 1u &&
        diagnostics.callbackContextFieldReadCount == 6u &&
        diagnostics.callbackCandidateClassificationStartCount == 0u &&
        diagnostics.callbackHeapMembershipTestCount == 0u &&
        diagnostics.callbackSegmentLookupCount == 0u &&
        diagnostics.callbackObjectHeaderReadCount == 0u &&
        diagnostics.callbackMethodTableReadCount == 0u &&
        diagnostics.callbackPromotionStartCount == 0u &&
        diagnostics.callbackPromotionCount == 0u &&
        diagnostics.callbackMarkingStartCount == 0u &&
        diagnostics.callbackGraphTraversalCount == 0u &&
        diagnostics.callbackMarkStateWriteCount == 0u &&
        diagnostics.callbackPromotionStateWriteCount == 0u &&
        diagnostics.callbackObjectMemoryMutationCount == 0u &&
        diagnostics.callbackGcMetadataMutationCount == 0u &&
        diagnostics.callbackSegmentMetadataMutationCount == 0u &&
        diagnostics.restartRequestCount == 0u &&
        diagnostics.restartEntryCount == 0u &&
        diagnostics.managedResumeCount == 0u &&
        diagnostics.threadStoreLockRecursionDepth == 1u &&
        diagnostics.managedEntryProhibited == 1u &&
        diagnostics.eeSuspended == 1u;
    if (!valid) {
        ++diagnostics.rootProviderInvariantFailures;
        diagnostics.membershipSafeStopReason = 0xE008u;
        emitFirstRootMembershipClassificationSafeStop();
        guideXosFailFast(9u);
    }
}

#if defined(GUIDEXOS_NATIVEAOT_FIRST_ROOT_HEAP_RESOLUTION_ALLOCATION)

void emitFirstRootHeapResolutionSafeStop() {
    const guidexos_nativeaot_allocation_diagnostics& diagnostics =
        g_guideXosAllocationDiagnostics;
    suspendEeSerialPutString(
        "[nativeaot-gc-first-root-heap-resolution] SAFE_STOP marker=");
    suspendEeSerialPutHex32(diagnostics.heapResolutionSafeStopReason);
    suspendEeSerialPutString(" callbackRequestCount=");
    suspendEeSerialPutHex32(diagnostics.callbackRequestCount);
    suspendEeSerialPutString(" callbackCallSiteCount=");
    suspendEeSerialPutHex32(diagnostics.callbackCallSiteEntryCount);
    suspendEeSerialPutString(" callbackInvocationCount=");
    suspendEeSerialPutHex32(diagnostics.callbackInvocationCount);
    suspendEeSerialPutString(" callbackEntryCount=");
    suspendEeSerialPutHex32(diagnostics.callbackEntryCount);
    suspendEeSerialPutString(" callbackReturnCount=");
    suspendEeSerialPutHex32(diagnostics.callbackReturnCount);
    suspendEeSerialPutString(" secondCallbackAttempts=");
    suspendEeSerialPutHex32(diagnostics.duplicateCallbackInvocationCount);
    suspendEeSerialPutString(" rootSlot=");
    suspendEeSerialPutHex64(diagnostics.callbackRootSlot);
    suspendEeSerialPutString(" callbackLoadedRoot=");
    suspendEeSerialPutHex64(diagnostics.callbackRootSlotLoadedValue);
    suspendEeSerialPutString(" membershipObject=");
    suspendEeSerialPutHex64(diagnostics.membershipObjectInput);
    suspendEeSerialPutString(" heapResolutionObject=");
    suspendEeSerialPutHex64(diagnostics.heapResolutionObjectInput);
    suspendEeSerialPutString(" objectMatchesCallbackRoot=");
    suspendEeSerialPutHex32(diagnostics.membershipObjectMatchesCallbackRoot);
    suspendEeSerialPutString(" objectMatchesMembershipObject=");
    suspendEeSerialPutHex32(
        diagnostics.heapResolutionObjectInput == diagnostics.membershipObjectInput ? 1u : 0u);
    suspendEeSerialPutString(" storageObject=");
    suspendEeSerialPutHex64(diagnostics.runtimeThreadStaticStorageObjectAddress);
    suspendEeSerialPutString(" sentinelAddress=");
    suspendEeSerialPutHex64(diagnostics.threadStaticProofSentinelAddress);
    suspendEeSerialPutString(" sentinelReadback=");
    suspendEeSerialPutHex64(diagnostics.threadStaticProofManagedReadbackAddress);
    suspendEeSerialPutString(" membershipRequests=");
    suspendEeSerialPutHex32(diagnostics.membershipRequestCount);
    suspendEeSerialPutString(" membershipEntries=");
    suspendEeSerialPutHex32(diagnostics.membershipEntryCount);
    suspendEeSerialPutString(" membershipCompletions=");
    suspendEeSerialPutHex32(diagnostics.membershipCompletionCount);
    suspendEeSerialPutString(" membershipReturns=");
    suspendEeSerialPutHex32(diagnostics.membershipReturnCount);
    suspendEeSerialPutString(" membershipObjectDereferences=");
    suspendEeSerialPutHex32(diagnostics.membershipObjectDereferenceCount);
    suspendEeSerialPutString(" lowerBound=");
    suspendEeSerialPutHex64(diagnostics.membershipLowerBound);
    suspendEeSerialPutString(" upperBound=");
    suspendEeSerialPutHex64(diagnostics.membershipUpperBound);
    suspendEeSerialPutString(" lowerResult=");
    suspendEeSerialPutHex32(diagnostics.membershipLowerComparisonResult);
    suspendEeSerialPutString(" upperResult=");
    suspendEeSerialPutHex32(diagnostics.membershipUpperComparisonResult);
    suspendEeSerialPutString(" inFindObjectRange=");
    suspendEeSerialPutHex32(diagnostics.membershipResult);
    suspendEeSerialPutString(" heapResolutionRequests=");
    suspendEeSerialPutHex32(diagnostics.heapResolutionRequestCount);
    suspendEeSerialPutString(" heapResolutionEntries=");
    suspendEeSerialPutHex32(diagnostics.heapResolutionEntryCount);
    suspendEeSerialPutString(" heapResolutionCompletions=");
    suspendEeSerialPutHex32(diagnostics.heapResolutionCompletionCount);
    suspendEeSerialPutString(" heapResolutionDuplicates=");
    suspendEeSerialPutHex32(diagnostics.heapResolutionDuplicateCount);
    suspendEeSerialPutString(" heapResolutionFailures=");
    suspendEeSerialPutHex32(diagnostics.heapResolutionFailureCount);
    suspendEeSerialPutString(" heapResolutionSucceeded=");
    suspendEeSerialPutHex32(diagnostics.heapResolutionSucceeded);
    suspendEeSerialPutString(" heapResolutionThreadHeap=");
    suspendEeSerialPutHex64(diagnostics.heapResolutionThreadHeap);
    suspendEeSerialPutString(" resolvedHeap=");
    suspendEeSerialPutHex64(diagnostics.heapResolutionHeapIdentity);
    suspendEeSerialPutString(" heapNumber=");
    suspendEeSerialPutHex32(diagnostics.heapResolutionHeapNumber);
    suspendEeSerialPutString(" totalHeapCount=");
    suspendEeSerialPutHex32(diagnostics.heapResolutionTotalHeapCount);
    suspendEeSerialPutString(" threadNumber=");
    suspendEeSerialPutHex32(diagnostics.heapResolutionThreadNumber);
    suspendEeSerialPutString(" objectAddressConsulted=");
    suspendEeSerialPutHex32(diagnostics.heapResolutionObjectAddressConsulted);
    suspendEeSerialPutString(" threadStateConsulted=");
    suspendEeSerialPutHex32(diagnostics.heapResolutionThreadStateConsulted);
    suspendEeSerialPutString(" heapTableReads=");
    suspendEeSerialPutHex32(diagnostics.heapResolutionHeapTableReadCount);
    suspendEeSerialPutString(" heapTableIdentity=");
    suspendEeSerialPutHex64(diagnostics.heapResolutionHeapTableIdentity);
    suspendEeSerialPutString(" heapTableSlot=");
    suspendEeSerialPutHex64(diagnostics.heapResolutionHeapTableSlot);
    suspendEeSerialPutString(" segmentMapReads=");
    suspendEeSerialPutHex32(diagnostics.heapResolutionSegmentMapReadCount);
    suspendEeSerialPutString(" segmentIdentity=");
    suspendEeSerialPutHex64(diagnostics.heapResolutionSegmentIdentity);
    suspendEeSerialPutString(" brickCardReads=");
    suspendEeSerialPutHex32(diagnostics.heapResolutionBrickCardReadCount);
    suspendEeSerialPutString(" rangeReads=");
    suspendEeSerialPutHex32(diagnostics.heapResolutionRangeReadCount);
    suspendEeSerialPutString(" allocationContextHeap=");
    suspendEeSerialPutHex64(diagnostics.heapResolutionAllocationContextHeap);
    suspendEeSerialPutString(" failureReason=");
    suspendEeSerialPutHex32(diagnostics.heapResolutionFailureReason);
    suspendEeSerialPutString(" heapResolutionCompletionReturnAddress=");
    suspendEeSerialPutHex64(diagnostics.heapResolutionCompletionReturnAddress);
    suspendEeSerialPutString(" candidateClassification=");
    suspendEeSerialPutHex32(diagnostics.callbackCandidateClassificationStartCount);
    suspendEeSerialPutString(" generationClassificationStart=");
    suspendEeSerialPutHex32(diagnostics.callbackGenerationClassificationStartCount);
    suspendEeSerialPutString(" generationQueryStart=");
    suspendEeSerialPutHex32(diagnostics.callbackGenerationQueryStartCount);
    suspendEeSerialPutString(" condemnedGenerationComparisons=");
    suspendEeSerialPutHex32(diagnostics.callbackCondemnedGenerationComparisonCount);
    suspendEeSerialPutString(" ephemeralGenerationComparisons=00000000");
    suspendEeSerialPutString(" postResolutionSegmentLookup=");
    suspendEeSerialPutHex32(diagnostics.callbackSegmentLookupCount);
    suspendEeSerialPutString(" objectHeaders=");
    suspendEeSerialPutHex32(diagnostics.callbackObjectHeaderReadCount);
    suspendEeSerialPutString(" methodTables=");
    suspendEeSerialPutHex32(diagnostics.callbackMethodTableReadCount);
    suspendEeSerialPutString(" childReferenceReads=00000000 promotionStart=");
    suspendEeSerialPutHex32(diagnostics.callbackPromotionStartCount);
    suspendEeSerialPutString(" promotions=");
    suspendEeSerialPutHex32(diagnostics.callbackPromotionCount);
    suspendEeSerialPutString(" markingStart=");
    suspendEeSerialPutHex32(diagnostics.callbackMarkingStartCount);
    suspendEeSerialPutString(" graphTraversal=");
    suspendEeSerialPutHex32(diagnostics.callbackGraphTraversalCount);
    suspendEeSerialPutString(" markWrites=");
    suspendEeSerialPutHex32(diagnostics.callbackMarkStateWriteCount);
    suspendEeSerialPutString(" promotionWrites=");
    suspendEeSerialPutHex32(diagnostics.callbackPromotionStateWriteCount);
    suspendEeSerialPutString(" objectMutation=");
    suspendEeSerialPutHex32(diagnostics.callbackObjectMemoryMutationCount);
    suspendEeSerialPutString(" gcMetadataMutation=");
    suspendEeSerialPutHex32(diagnostics.callbackGcMetadataMutationCount);
    suspendEeSerialPutString(" segmentMetadataMutation=");
    suspendEeSerialPutHex32(diagnostics.callbackSegmentMetadataMutationCount);
    suspendEeSerialPutString(" managedAssignmentCount=");
    suspendEeSerialPutHex32(diagnostics.threadStaticProofAssignmentCount);
    suspendEeSerialPutString(" managedClearCount=");
    suspendEeSerialPutHex32(diagnostics.threadStaticProofClearCount);
    suspendEeSerialPutString(" managedReadbackCount=");
    suspendEeSerialPutHex32(diagnostics.threadStaticProofReadbackCount);
    suspendEeSerialPutString(" managedAssignmentValid=");
    suspendEeSerialPutHex32(diagnostics.threadStaticProofManagedAssignmentValid);
    suspendEeSerialPutString(" managedReadbackValid=");
    suspendEeSerialPutHex32(diagnostics.threadStaticProofManagedReadbackValid);
    suspendEeSerialPutString(" sentinelChecks=");
    suspendEeSerialPutHex32(diagnostics.sentinelValidationCount);
    suspendEeSerialPutString(" objectBefore=");
    suspendEeSerialPutHex32(diagnostics.candidateObjectValidationBeforeLoadCount);
    suspendEeSerialPutString(" objectAfter=");
    suspendEeSerialPutHex32(diagnostics.candidateObjectValidationAfterLoadCount);
    suspendEeSerialPutString(" objectAtStop=");
    suspendEeSerialPutHex32(diagnostics.candidateObjectValidationAtStopCount);
    suspendEeSerialPutString(" objectHistoryOverflow=");
    suspendEeSerialPutHex32(diagnostics.objectHistoryOverflow);
    suspendEeSerialPutString(" duplicateObjectAddresses=");
    suspendEeSerialPutHex32(diagnostics.duplicateObjectAddressFailures);
    suspendEeSerialPutString(" managedThread=");
    suspendEeSerialPutHex64(diagnostics.callbackEntryManagedThread);
    suspendEeSerialPutString(" currentThread=");
    suspendEeSerialPutHex64(diagnostics.callbackEntryCurrentThread);
    suspendEeSerialPutString(" lockOwner=");
    suspendEeSerialPutHex64(diagnostics.callbackEntryLockOwner);
    suspendEeSerialPutString(" lockHeld=");
    suspendEeSerialPutHex32(diagnostics.callbackEntryThreadStoreLockHeld);
    suspendEeSerialPutString(" eeSuspended=");
    suspendEeSerialPutHex32(diagnostics.callbackEntryEeSuspended);
    suspendEeSerialPutString(" managedEntryProhibited=");
    suspendEeSerialPutHex32(diagnostics.callbackEntryManagedEntryProhibited);
    suspendEeSerialPutString(" callbackContext=");
    suspendEeSerialPutHex64(diagnostics.callbackContextAddress);
    suspendEeSerialPutString(" contextFieldReads=");
    suspendEeSerialPutHex32(diagnostics.callbackContextFieldReadCount);
    suspendEeSerialPutString(" contextThread=");
    suspendEeSerialPutHex64(diagnostics.callbackContextThreadUnderCrawl);
    suspendEeSerialPutString(" contextStackLimit=");
    suspendEeSerialPutHex64(diagnostics.callbackContextStackLimit);
    suspendEeSerialPutString(" contextThreadNumber=");
    suspendEeSerialPutHex64(diagnostics.callbackEntryScanContextFieldThreadNumberValue);
    suspendEeSerialPutString(" contextThreadCount=");
    suspendEeSerialPutHex64(diagnostics.callbackEntryScanContextFieldThreadCountValue);
    suspendEeSerialPutString(" contextPromotion=");
    suspendEeSerialPutHex32(diagnostics.callbackEntryPromotion);
    suspendEeSerialPutString(" contextConcurrent=");
    suspendEeSerialPutHex32(diagnostics.callbackEntryConcurrent);
    suspendEeSerialPutString(" callbackReturns=");
    suspendEeSerialPutHex32(diagnostics.callbackReturnCount);
    suspendEeSerialPutString(" secondCallbacks=");
    suspendEeSerialPutHex32(diagnostics.duplicateCallbackInvocationCount);
    suspendEeSerialPutString(" restartRequests=");
    suspendEeSerialPutHex32(diagnostics.restartRequestCount);
    suspendEeSerialPutString(" restartEntries=");
    suspendEeSerialPutHex32(diagnostics.restartEntryCount);
    suspendEeSerialPutString(" managedResume=");
    suspendEeSerialPutHex32(diagnostics.managedResumeCount);
#if defined(GUIDEXOS_NATIVEAOT_FIRST_ROOT_HEAP_RESOLUTION_ALLOCATION)
    suspendEeSerialPutString(" lockDepth=");
    suspendEeSerialPutHex32(diagnostics.threadStoreLockRecursionDepth);
    suspendEeSerialPutString(" registeredManagedThreads=");
    suspendEeSerialPutHex32(diagnostics.registeredManagedThreadCount);
    suspendEeSerialPutString(" currentThreadRegistered=");
    suspendEeSerialPutHex32(diagnostics.currentThreadRegistered);
    suspendEeSerialPutString(" currentThreadIsInitiator=");
    suspendEeSerialPutHex32(diagnostics.currentThreadIsInitiator);
    suspendEeSerialPutString(" currentAndInitiatorMatch=");
    suspendEeSerialPutHex32(diagnostics.currentAndInitiatorMatch);
    suspendEeSerialPutString(" registeredThreadCountBeforeRoot=");
    suspendEeSerialPutHex32(diagnostics.registeredThreadCountBeforeRoot);
    suspendEeSerialPutString(" registeredThreadCountAfterRoot=");
    suspendEeSerialPutHex32(diagnostics.registeredThreadCountAfterRoot);
    suspendEeSerialPutString(" enumeratedThreads=");
    suspendEeSerialPutHex32(diagnostics.enumeratedThreadCount);
    suspendEeSerialPutString(" includedThreads=");
    suspendEeSerialPutHex32(diagnostics.includedThreadCount);
    suspendEeSerialPutString(" excludedThreads=");
    suspendEeSerialPutHex32(diagnostics.excludedThreadCount);
    suspendEeSerialPutString(" duplicateThreads=");
    suspendEeSerialPutHex32(diagnostics.duplicateThreadCount);
    suspendEeSerialPutString(" registryMutationBefore=");
    suspendEeSerialPutHex32(diagnostics.threadRegistryMutationCountBeforeRoot);
    suspendEeSerialPutString(" registryMutationAfter=");
    suspendEeSerialPutHex32(diagnostics.threadRegistryMutationCountAfterRoot);
    suspendEeSerialPutString(" allocationContextsVisited=");
    suspendEeSerialPutHex32(diagnostics.allocationContextFixupContextsVisited);
    suspendEeSerialPutString(" allocationContextsChanged=");
    suspendEeSerialPutHex32(diagnostics.allocationContextFixupContextsChanged);
    suspendEeSerialPutString(" allocationContextsCleared=");
    suspendEeSerialPutHex32(diagnostics.allocationContextFixupContextsCleared);
#endif
    suspendEeSerialPutString(
        " nextSourceOperation=gc_heap::is_in_condemned_gc(o)@gc.cpp:49499");
    suspendEeSerialPutString(" marker=C011EC09\n");
}

#endif

extern "C" void __cdecl
guideXosNativeAotFirstRootMembershipCheckRequested(uintptr_t object) {
    guidexos_nativeaot_allocation_diagnostics& diagnostics =
        g_guideXosAllocationDiagnostics;
    ++diagnostics.membershipRequestCount;
    if (diagnostics.membershipRequestCount != 1u) {
        ++diagnostics.membershipDuplicateCheckCount;
    }
    diagnostics.membershipObjectInput = object;
    diagnostics.membershipCallbackLoadedRoot =
        diagnostics.callbackRootSlotLoadedValue;
    diagnostics.membershipObjectMatchesCallbackRoot =
        object != 0u && object == diagnostics.callbackRootSlotLoadedValue ? 1u : 0u;
    diagnostics.membershipHeapIdentity = 0u;
    diagnostics.membershipSegmentIdentity = 0u;
    diagnostics.membershipHeapFieldReadCount = 0u;
    diagnostics.membershipSegmentLookupCount = 0u;
    diagnostics.membershipSegmentLookupSucceeded = 0u;
    if (diagnostics.membershipRequestCount != 1u ||
        diagnostics.membershipObjectMatchesCallbackRoot == 0u) {
        diagnostics.membershipSafeStopReason = 0xE009u;
        emitFirstRootMembershipClassificationSafeStop();
        guideXosFailFast(9u);
    }
}

extern "C" void __cdecl
guideXosNativeAotFirstRootMembershipCheckEntered(uintptr_t object) {
    guidexos_nativeaot_allocation_diagnostics& diagnostics =
        g_guideXosAllocationDiagnostics;
    ++diagnostics.membershipEntryCount;
    diagnostics.membershipObjectInput = object;
    if (diagnostics.membershipEntryCount != 1u ||
        diagnostics.membershipRequestCount != 1u) {
        ++diagnostics.membershipDuplicateCheckCount;
        diagnostics.membershipSafeStopReason = 0xE00Au;
        emitFirstRootMembershipClassificationSafeStop();
        guideXosFailFast(9u);
    }
}

extern "C" void __cdecl
guideXosNativeAotFirstRootMembershipCheckCompleted(
    uintptr_t object, uintptr_t lowerBound, uintptr_t upperBound,
    uint32_t lowerEvaluated, uint32_t upperEvaluated,
    uint32_t lowerResult, uint32_t upperResult, uint32_t result) {
    guidexos_nativeaot_allocation_diagnostics& diagnostics =
        g_guideXosAllocationDiagnostics;
    ++diagnostics.membershipCompletionCount;
    if (diagnostics.membershipCompletionCount != 1u) {
        ++diagnostics.membershipDuplicateCheckCount;
    }
    diagnostics.membershipObjectInput = object;
    diagnostics.membershipLowerBound = lowerBound;
    diagnostics.membershipUpperBound = upperBound;
    diagnostics.membershipLowerComparisonEvaluated = lowerEvaluated;
    diagnostics.membershipUpperComparisonEvaluated = upperEvaluated;
    diagnostics.membershipLowerComparisonResult = lowerResult;
    diagnostics.membershipUpperComparisonResult = upperResult;
    diagnostics.membershipResult = result;
    diagnostics.membershipHeapFieldReadCount = upperEvaluated;
    diagnostics.membershipSourceBranch = result != 0u ? 1u : 2u;
    diagnostics.membershipCompletionReturnAddress =
        reinterpret_cast<gx_uintptr>(_ReturnAddress());
    if (diagnostics.membershipCompletionCount != 1u ||
        diagnostics.membershipRequestCount != 1u ||
        diagnostics.membershipEntryCount != 1u ||
        diagnostics.membershipObjectMatchesCallbackRoot == 0u ||
        diagnostics.membershipResult !=
            (diagnostics.membershipLowerComparisonResult != 0u &&
             diagnostics.membershipUpperComparisonResult != 0u ? 1u : 0u)) {
        diagnostics.membershipSafeStopReason = 0xE00Bu;
        emitFirstRootMembershipClassificationSafeStop();
        guideXosFailFast(9u);
    }
}

extern "C" [[noreturn]] void __cdecl
guideXosNativeAotFirstRootMembershipResultBoundary(uint32_t result) {
    guidexos_nativeaot_allocation_diagnostics& diagnostics =
        g_guideXosAllocationDiagnostics;
    ++diagnostics.membershipReturnCount;
    ++diagnostics.membershipPostCheckBoundaryCount;
    diagnostics.membershipPostCheckReturnAddress =
        reinterpret_cast<gx_uintptr>(_ReturnAddress());
    if (diagnostics.membershipReturnCount != 1u ||
        diagnostics.membershipPostCheckBoundaryCount != 1u ||
        result != diagnostics.membershipResult ||
        diagnostics.membershipCompletionCount != 1u ||
        diagnostics.membershipObjectInput != diagnostics.callbackRootSlotLoadedValue) {
        diagnostics.membershipSafeStopReason = 0xE00Cu;
        emitFirstRootMembershipClassificationSafeStop();
        guideXosFailFast(9u);
    }
    validateAllocationContextFixupObjects(true);
    diagnostics.candidateObjectValidationAtStopCount =
        diagnostics.objectHistoryCount;
    diagnostics.membershipSafeStopReason =
        GUIDEXOS_NATIVEAOT_FIRST_ROOT_MEMBERSHIP_CLASSIFICATION_SAFE_STOP_MARKER;
    diagnostics.membershipSafeStopObserved = 1u;
    diagnostics.safeStopObserved = 1u;
    diagnostics.stopReason = diagnostics.membershipSafeStopReason;
    diagnostics.stage =
        GUIDEXOS_NATIVEAOT_ALLOC_STAGE_F26_FIRST_ROOT_MEMBERSHIP_CLASSIFICATION_SAFE_STOP;
    diagnostics.rootBoundaryFunction = reinterpret_cast<gx_uintptr>(
        &guideXosNativeAotFirstRootMembershipResultBoundary);
    emitFirstRootMembershipClassificationSafeStop();
    for (;;) {
    }
}

#if defined(GUIDEXOS_NATIVEAOT_FIRST_ROOT_HEAP_RESOLUTION_ALLOCATION)

extern "C" void __cdecl
guideXosNativeAotFirstRootHeapResolutionRequested(uintptr_t object) {
    guidexos_nativeaot_allocation_diagnostics& diagnostics =
        g_guideXosAllocationDiagnostics;
    ++diagnostics.heapResolutionRequestCount;
    diagnostics.heapResolutionObjectInput = object;
    diagnostics.heapResolutionMembershipPassed =
        diagnostics.membershipResult == 1u ? 1u : 0u;
    if (diagnostics.heapResolutionRequestCount != 1u ||
        diagnostics.heapResolutionMembershipPassed == 0u ||
        object != diagnostics.callbackRootSlotLoadedValue ||
        object != diagnostics.membershipObjectInput) {
        ++diagnostics.heapResolutionDuplicateCount;
        diagnostics.heapResolutionFailureReason = 0xE009u;
        diagnostics.heapResolutionSafeStopReason = 0xE009u;
        emitFirstRootHeapResolutionSafeStop();
        guideXosFailFast(9u);
    }
}

extern "C" void __cdecl
guideXosNativeAotFirstRootHeapResolutionEntered(
    uintptr_t object, uintptr_t threadHeap, uint32_t threadNumber) {
    guidexos_nativeaot_allocation_diagnostics& diagnostics =
        g_guideXosAllocationDiagnostics;
    ++diagnostics.heapResolutionEntryCount;
    diagnostics.heapResolutionObjectInput = object;
    diagnostics.heapResolutionThreadHeap = threadHeap;
    diagnostics.heapResolutionThreadNumber = threadNumber;
#if defined(GUIDEXOS_NATIVEAOT_FIRST_ROOT_CONDEMNED_GENERATION_DECISION_ALLOCATION)
#if defined(MULTIPLE_HEAPS)
    diagnostics.workstationMultipleHeapsEnabled = 1u;
#else
    diagnostics.workstationMultipleHeapsEnabled = 0u;
#endif
#endif
    if (diagnostics.heapResolutionEntryCount != 1u ||
        diagnostics.heapResolutionRequestCount != 1u ||
        object != diagnostics.membershipObjectInput ||
        object != diagnostics.callbackRootSlotLoadedValue) {
        ++diagnostics.heapResolutionDuplicateCount;
        diagnostics.heapResolutionFailureReason = 0xE00Au;
        diagnostics.heapResolutionSafeStopReason = 0xE00Au;
        emitFirstRootHeapResolutionSafeStop();
        guideXosFailFast(9u);
    }
}

#if defined(GUIDEXOS_NATIVEAOT_FIRST_ROOT_CONDEMNED_GENERATION_DECISION_ALLOCATION)
extern "C" void __cdecl
#else
extern "C" [[noreturn]] void __cdecl
#endif
guideXosNativeAotFirstRootHeapResolutionCompleted(
    uintptr_t object, uintptr_t threadHeap, uintptr_t heap,
    uint32_t heapNumber, uint32_t totalHeapCount) {
    guidexos_nativeaot_allocation_diagnostics& diagnostics =
        g_guideXosAllocationDiagnostics;
    ++diagnostics.heapResolutionCompletionCount;
    diagnostics.heapResolutionObjectInput = object;
    diagnostics.heapResolutionThreadHeap = threadHeap;
    diagnostics.heapResolutionHeapIdentity = heap;
    diagnostics.heapResolutionHeapNumber = heapNumber;
    diagnostics.heapResolutionTotalHeapCount = totalHeapCount;
    diagnostics.heapResolutionObjectAddressConsulted = 0u;
    diagnostics.heapResolutionThreadStateConsulted = 0u;
    diagnostics.heapResolutionHeapTableReadCount = 0u;
    diagnostics.heapResolutionSegmentMapReadCount = 0u;
    diagnostics.heapResolutionBrickCardReadCount = 0u;
    diagnostics.heapResolutionRangeReadCount = 0u;
    diagnostics.heapResolutionHeapTableIdentity = 0u;
    diagnostics.heapResolutionHeapTableSlot = 0u;
    diagnostics.heapResolutionSegmentIdentity = 0u;
    diagnostics.heapResolutionAllocationContextHeap = 0u;
    diagnostics.heapResolutionCompletionReturnAddress =
        reinterpret_cast<gx_uintptr>(_ReturnAddress());
    snapshotFirstPerThreadRootList(false);
    if (diagnostics.heapResolutionCompletionCount != 1u ||
        diagnostics.heapResolutionRequestCount != 1u ||
        diagnostics.heapResolutionEntryCount != 1u ||
        diagnostics.heapResolutionMembershipPassed != 1u ||
        object != diagnostics.callbackRootSlotLoadedValue ||
        object != diagnostics.membershipObjectInput) {
        ++diagnostics.heapResolutionDuplicateCount;
        diagnostics.heapResolutionFailureReason = 0xE00Bu;
    }
#if defined(GUIDEXOS_NATIVEAOT_FIRST_ROOT_CONDEMNED_GENERATION_DECISION_ALLOCATION)
    diagnostics.workstationSingleHeapSentinelValid =
        diagnostics.workstationMultipleHeapsEnabled == 0u &&
        threadHeap == 0u && heap == 0u && heapNumber == 0u &&
        totalHeapCount == 1u ? 1u : 0u;
    if (diagnostics.heapResolutionRequestCount == 1u &&
        diagnostics.heapResolutionEntryCount == 1u &&
        diagnostics.heapResolutionCompletionCount == 1u &&
        diagnostics.heapResolutionMembershipPassed == 1u &&
        diagnostics.heapResolutionObjectInput == diagnostics.callbackRootSlotLoadedValue &&
        diagnostics.heapResolutionObjectInput == diagnostics.membershipObjectInput &&
        diagnostics.workstationSingleHeapSentinelValid != 0u) {
        diagnostics.heapResolutionFailureCount = 0u;
        diagnostics.heapResolutionFailureReason = 0u;
        diagnostics.heapResolutionSucceeded = 0u;
    } else if (heap == 0u) {
        ++diagnostics.heapResolutionFailureCount;
        diagnostics.heapResolutionFailureReason = 0xB001u;
        diagnostics.heapResolutionSucceeded = 0u;
    } else {
        diagnostics.heapResolutionSucceeded = 1u;
        diagnostics.heapResolutionFailureReason = 0u;
    }
    if (diagnostics.workstationSingleHeapSentinelValid == 0u ||
        diagnostics.heapResolutionDuplicateCount != 0u) {
        diagnostics.heapResolutionSafeStopReason = 0xE00Cu;
        emitFirstRootHeapResolutionSafeStop();
        guideXosFailFast(9u);
    }
    validateAllocationContextFixupObjects(true);
    diagnostics.candidateObjectValidationAtStopCount =
        diagnostics.objectHistoryCount;
    return;
#else
    if (heap == 0u) {
        ++diagnostics.heapResolutionFailureCount;
        diagnostics.heapResolutionFailureReason = 0xB001u;
        diagnostics.heapResolutionSucceeded = 0u;
    } else {
        diagnostics.heapResolutionSucceeded = 1u;
        diagnostics.heapResolutionFailureReason = 0u;
    }
    validateAllocationContextFixupObjects(true);
    diagnostics.candidateObjectValidationAtStopCount =
        diagnostics.objectHistoryCount;
    diagnostics.heapResolutionSafeStopReason =
        GUIDEXOS_NATIVEAOT_FIRST_ROOT_HEAP_RESOLUTION_SAFE_STOP_MARKER;
    diagnostics.heapResolutionSafeStopObserved = 1u;
    diagnostics.safeStopObserved = 1u;
    diagnostics.stopReason = diagnostics.heapResolutionSafeStopReason;
    diagnostics.stage =
        GUIDEXOS_NATIVEAOT_ALLOC_STAGE_F27_FIRST_ROOT_HEAP_RESOLUTION_SAFE_STOP;
    diagnostics.rootBoundaryFunction = reinterpret_cast<gx_uintptr>(
        &guideXosNativeAotFirstRootHeapResolutionCompleted);
    emitFirstRootHeapResolutionSafeStop();
    for (;;) {
    }
#endif
}

#if defined(GUIDEXOS_NATIVEAOT_FIRST_ROOT_CONDEMNED_GENERATION_DECISION_ALLOCATION)

void emitFirstRootCondemnedGenerationDecisionSafeStop() {
    const guidexos_nativeaot_allocation_diagnostics& diagnostics =
        g_guideXosAllocationDiagnostics;
    suspendEeSerialPutString(
        "[nativeaot-gc-first-root-condemned-generation-decision] SAFE_STOP marker=");
    suspendEeSerialPutHex32(diagnostics.condemnedCheckSafeStopReason);
    suspendEeSerialPutString(" callbackRequestCount=");
    suspendEeSerialPutHex32(diagnostics.callbackRequestCount);
    suspendEeSerialPutString(" callbackCallSiteCount=");
    suspendEeSerialPutHex32(diagnostics.callbackCallSiteEntryCount);
    suspendEeSerialPutString(" callbackInvocationCount=");
    suspendEeSerialPutHex32(diagnostics.callbackInvocationCount);
    suspendEeSerialPutString(" callbackEntryCount=");
    suspendEeSerialPutHex32(diagnostics.callbackEntryCount);
    suspendEeSerialPutString(" callbackReturnCount=");
    suspendEeSerialPutHex32(diagnostics.callbackReturnCount);
    suspendEeSerialPutString(" secondCallbackAttempts=");
    suspendEeSerialPutHex32(diagnostics.duplicateCallbackInvocationCount);
    suspendEeSerialPutString(" rootSlot=");
    suspendEeSerialPutHex64(diagnostics.callbackRootSlot);
    suspendEeSerialPutString(" callbackRawRoot=");
    suspendEeSerialPutHex64(diagnostics.callbackRootSlotLoadedValue);
    suspendEeSerialPutString(" callbackLoadedRoot=");
    suspendEeSerialPutHex64(diagnostics.callbackRootSlotLoadedValue);
    suspendEeSerialPutString(" condemnedCheckObject=");
    suspendEeSerialPutHex64(diagnostics.condemnedCheckObjectInput);
    suspendEeSerialPutString(" membershipObject=");
    suspendEeSerialPutHex64(diagnostics.membershipObjectInput);
    suspendEeSerialPutString(" heapResolutionObject=");
    suspendEeSerialPutHex64(diagnostics.heapResolutionObjectInput);
    suspendEeSerialPutString(" callbackRootMatches=");
    suspendEeSerialPutHex32(diagnostics.condemnedCheckCallbackRootInputMatch);
    suspendEeSerialPutString(" membershipMatches=");
    suspendEeSerialPutHex32(diagnostics.condemnedCheckMembershipInputMatch);
    suspendEeSerialPutString(" heapResolutionMatches=");
    suspendEeSerialPutHex32(diagnostics.condemnedCheckHeapResolutionInputMatch);
    suspendEeSerialPutString(" storageObject=");
    suspendEeSerialPutHex64(diagnostics.runtimeThreadStaticStorageObjectAddress);
    suspendEeSerialPutString(" sentinelAddress=");
    suspendEeSerialPutHex64(diagnostics.threadStaticProofSentinelAddress);
    suspendEeSerialPutString(" sentinelReadback=");
    suspendEeSerialPutHex64(diagnostics.threadStaticProofManagedReadbackAddress);
    suspendEeSerialPutString(" membershipRequests=");
    suspendEeSerialPutHex32(diagnostics.membershipRequestCount);
    suspendEeSerialPutString(" membershipEntries=");
    suspendEeSerialPutHex32(diagnostics.membershipEntryCount);
    suspendEeSerialPutString(" membershipCompletions=");
    suspendEeSerialPutHex32(diagnostics.membershipCompletionCount);
    suspendEeSerialPutString(" membershipObjectDereferences=");
    suspendEeSerialPutHex32(diagnostics.membershipObjectDereferenceCount);
    suspendEeSerialPutString(" membershipLowerBound=");
    suspendEeSerialPutHex64(diagnostics.membershipLowerBound);
    suspendEeSerialPutString(" membershipUpperBound=");
    suspendEeSerialPutHex64(diagnostics.membershipUpperBound);
    suspendEeSerialPutString(" inFindObjectRange=");
    suspendEeSerialPutHex32(diagnostics.membershipResult);
    suspendEeSerialPutString(" multipleHeaps=");
    suspendEeSerialPutHex32(diagnostics.workstationMultipleHeapsEnabled);
    suspendEeSerialPutString(" hpt=");
    suspendEeSerialPutHex64(diagnostics.heapResolutionThreadHeap);
    suspendEeSerialPutString(" heapOf=");
    suspendEeSerialPutHex64(diagnostics.heapResolutionHeapIdentity);
    suspendEeSerialPutString(" heapNumber=");
    suspendEeSerialPutHex32(diagnostics.heapResolutionHeapNumber);
    suspendEeSerialPutString(" heapCount=");
    suspendEeSerialPutHex32(diagnostics.heapResolutionTotalHeapCount);
    suspendEeSerialPutString(" workstationSingleHeapSentinelValid=");
    suspendEeSerialPutHex32(diagnostics.workstationSingleHeapSentinelValid);
    suspendEeSerialPutString(" heapResolutionFailures=");
    suspendEeSerialPutHex32(diagnostics.heapResolutionFailureCount);
    suspendEeSerialPutString(" condemnedRequests=");
    suspendEeSerialPutHex32(diagnostics.condemnedCheckRequestCount);
    suspendEeSerialPutString(" condemnedEntries=");
    suspendEeSerialPutHex32(diagnostics.condemnedCheckEntryCount);
    suspendEeSerialPutString(" condemnedCompletions=");
    suspendEeSerialPutHex32(diagnostics.condemnedCheckCompletionCount);
    suspendEeSerialPutString(" condemnedReturns=");
    suspendEeSerialPutHex32(diagnostics.condemnedCheckReturnCount);
    suspendEeSerialPutString(" condemnedDuplicates=");
    suspendEeSerialPutHex32(diagnostics.condemnedCheckDuplicateCount);
    suspendEeSerialPutString(" condemnedObjectDereferences=");
    suspendEeSerialPutHex32(diagnostics.condemnedCheckObjectDereferenceCount);
    suspendEeSerialPutString(" condemnedGenerationQueryStart=");
    suspendEeSerialPutHex32(diagnostics.condemnedCheckGenerationQueryStartCount);
    suspendEeSerialPutString(" condemnedGenerationQueryCompletions=");
    suspendEeSerialPutHex32(diagnostics.condemnedCheckGenerationQueryCompletionCount);
    suspendEeSerialPutString(" condemnedGenerationTableReads=");
    suspendEeSerialPutHex32(diagnostics.condemnedCheckGenerationTableReadCount);
    suspendEeSerialPutString(" condemnedSegmentLookups=");
    suspendEeSerialPutHex32(diagnostics.condemnedCheckSegmentLookupCount);
    suspendEeSerialPutString(" condemnedSegmentIdentity=");
    suspendEeSerialPutHex64(diagnostics.condemnedCheckSegmentIdentity);
    suspendEeSerialPutString(" condemnedObjectHeaders=");
    suspendEeSerialPutHex32(diagnostics.condemnedCheckObjectHeaderReadCount);
    suspendEeSerialPutString(" condemnedMethodTables=");
    suspendEeSerialPutHex32(diagnostics.condemnedCheckMethodTableReadCount);
    suspendEeSerialPutString(" condemnedResult=");
    suspendEeSerialPutHex32(diagnostics.condemnedCheckResult);
    suspendEeSerialPutString(" condemnedBranch=");
    suspendEeSerialPutHex32(diagnostics.condemnedCheckSourceBranch);
    suspendEeSerialPutString(" condemnedLowerBound=");
    suspendEeSerialPutHex64(diagnostics.condemnedCheckLowerBound);
    suspendEeSerialPutString(" condemnedUpperBound=");
    suspendEeSerialPutHex64(diagnostics.condemnedCheckUpperBound);
    suspendEeSerialPutString(" condemnedGeneration=");
    suspendEeSerialPutHex32(diagnostics.condemnedCheckCondemnedGeneration);
    suspendEeSerialPutString(" maximumGeneration=");
    suspendEeSerialPutHex32(diagnostics.condemnedCheckMaximumGeneration);
    suspendEeSerialPutString(" generationFromRegion=");
    suspendEeSerialPutHex32(diagnostics.condemnedCheckGeneration);
    suspendEeSerialPutString(" generationInputValid=");
    suspendEeSerialPutHex32(diagnostics.condemnedCheckGenerationInputValid);
    suspendEeSerialPutString(" generationTable=");
    suspendEeSerialPutHex64(diagnostics.condemnedCheckGenerationTableIdentity);
    suspendEeSerialPutString(" generationTableIndex=");
    suspendEeSerialPutHex64(diagnostics.condemnedCheckGenerationTableIndex);
    suspendEeSerialPutString(" minSegmentSizeShift=");
    suspendEeSerialPutHex32(diagnostics.condemnedCheckMinimumSegmentSizeShift);
    suspendEeSerialPutString(" condemnedCompletionReturnAddress=");
    suspendEeSerialPutHex64(diagnostics.condemnedCheckCompletionReturnAddress);
    suspendEeSerialPutString(" condemnedSafeStopReturnAddress=");
    suspendEeSerialPutHex64(diagnostics.condemnedCheckSafeStopReturnAddress);
    suspendEeSerialPutString(" objectHeaders=");
    suspendEeSerialPutHex32(diagnostics.callbackObjectHeaderReadCount);
    suspendEeSerialPutString(" methodTables=");
    suspendEeSerialPutHex32(diagnostics.callbackMethodTableReadCount);
    suspendEeSerialPutString(" childReferenceReads=00000000 promotionStart=");
    suspendEeSerialPutHex32(diagnostics.callbackPromotionStartCount);
    suspendEeSerialPutString(" promotions=");
    suspendEeSerialPutHex32(diagnostics.callbackPromotionCount);
    suspendEeSerialPutString(" markingStart=");
    suspendEeSerialPutHex32(diagnostics.callbackMarkingStartCount);
    suspendEeSerialPutString(" markingWrites=");
    suspendEeSerialPutHex32(diagnostics.callbackMarkStateWriteCount);
    suspendEeSerialPutString(" graphTraversal=");
    suspendEeSerialPutHex32(diagnostics.callbackGraphTraversalCount);
    suspendEeSerialPutString(" promotionWrites=");
    suspendEeSerialPutHex32(diagnostics.callbackPromotionStateWriteCount);
    suspendEeSerialPutString(" objectMutation=");
    suspendEeSerialPutHex32(diagnostics.callbackObjectMemoryMutationCount);
    suspendEeSerialPutString(" gcMetadataMutation=");
    suspendEeSerialPutHex32(diagnostics.callbackGcMetadataMutationCount);
    suspendEeSerialPutString(" segmentMutation=");
    suspendEeSerialPutHex32(diagnostics.callbackSegmentMetadataMutationCount);
    suspendEeSerialPutString(" managedAssignmentCount=");
    suspendEeSerialPutHex32(diagnostics.threadStaticProofAssignmentCount);
    suspendEeSerialPutString(" managedClearCount=");
    suspendEeSerialPutHex32(diagnostics.threadStaticProofClearCount);
    suspendEeSerialPutString(" managedReadbackCount=");
    suspendEeSerialPutHex32(diagnostics.threadStaticProofReadbackCount);
    suspendEeSerialPutString(" managedAssignmentValid=");
    suspendEeSerialPutHex32(diagnostics.threadStaticProofManagedAssignmentValid);
    suspendEeSerialPutString(" managedReadbackValid=");
    suspendEeSerialPutHex32(diagnostics.threadStaticProofManagedReadbackValid);
    suspendEeSerialPutString(" sentinelChecks=");
    suspendEeSerialPutHex32(diagnostics.sentinelValidationCount);
    suspendEeSerialPutString(" objectBefore=");
    suspendEeSerialPutHex32(diagnostics.candidateObjectValidationBeforeLoadCount);
    suspendEeSerialPutString(" objectAfter=");
    suspendEeSerialPutHex32(diagnostics.candidateObjectValidationAfterLoadCount);
    suspendEeSerialPutString(" objectAtStop=");
    suspendEeSerialPutHex32(diagnostics.candidateObjectValidationAtStopCount);
    suspendEeSerialPutString(" duplicateObjectAddresses=");
    suspendEeSerialPutHex32(diagnostics.duplicateObjectAddressFailures);
    suspendEeSerialPutString(" objectHistoryOverflow=");
    suspendEeSerialPutHex32(diagnostics.objectHistoryOverflow);
    suspendEeSerialPutString(" managedThread=");
    suspendEeSerialPutHex64(diagnostics.callbackEntryManagedThread);
    suspendEeSerialPutString(" currentThread=");
    suspendEeSerialPutHex64(diagnostics.callbackEntryCurrentThread);
    suspendEeSerialPutString(" lockOwner=");
    suspendEeSerialPutHex64(diagnostics.callbackEntryLockOwner);
    suspendEeSerialPutString(" lockHeld=");
    suspendEeSerialPutHex32(diagnostics.callbackEntryThreadStoreLockHeld);
    suspendEeSerialPutString(" eeSuspended=");
    suspendEeSerialPutHex32(diagnostics.callbackEntryEeSuspended);
    suspendEeSerialPutString(" managedEntryProhibited=");
    suspendEeSerialPutHex32(diagnostics.callbackEntryManagedEntryProhibited);
    suspendEeSerialPutString(" callbackReturns=");
    suspendEeSerialPutHex32(diagnostics.callbackReturnCount);
    suspendEeSerialPutString(" secondCallbacks=");
    suspendEeSerialPutHex32(diagnostics.duplicateCallbackInvocationCount);
    suspendEeSerialPutString(" restartRequests=");
    suspendEeSerialPutHex32(diagnostics.restartRequestCount);
    suspendEeSerialPutString(" restartEntries=");
    suspendEeSerialPutHex32(diagnostics.restartEntryCount);
    suspendEeSerialPutString(" managedResume=");
    suspendEeSerialPutHex32(diagnostics.managedResumeCount);
    suspendEeSerialPutString(" lockDepth=");
    suspendEeSerialPutHex32(diagnostics.threadStoreLockRecursionDepth);
    suspendEeSerialPutString(" registeredManagedThreads=");
    suspendEeSerialPutHex32(diagnostics.registeredManagedThreadCount);
    suspendEeSerialPutString(" currentThreadRegistered=");
    suspendEeSerialPutHex32(diagnostics.currentThreadRegistered);
    suspendEeSerialPutString(" currentThreadIsInitiator=");
    suspendEeSerialPutHex32(diagnostics.currentThreadIsInitiator);
    suspendEeSerialPutString(" currentAndInitiatorMatch=");
    suspendEeSerialPutHex32(diagnostics.currentAndInitiatorMatch);
    suspendEeSerialPutString(" enumeratedThreads=");
    suspendEeSerialPutHex32(diagnostics.enumeratedThreadCount);
    suspendEeSerialPutString(" includedThreads=");
    suspendEeSerialPutHex32(diagnostics.includedThreadCount);
    suspendEeSerialPutString(" duplicateThreads=");
    suspendEeSerialPutHex32(diagnostics.duplicateThreadCount);
    suspendEeSerialPutString(" allocationContextsVisited=");
    suspendEeSerialPutHex32(diagnostics.allocationContextFixupContextsVisited);
    suspendEeSerialPutString(" allocationContextsChanged=");
    suspendEeSerialPutHex32(diagnostics.allocationContextFixupContextsChanged);
    suspendEeSerialPutString(" allocationContextsCleared=");
    suspendEeSerialPutHex32(diagnostics.allocationContextFixupContextsCleared);
    suspendEeSerialPutString(" nextSourceOperation=");
    if (diagnostics.condemnedCheckResult != 0u) {
        suspendEeSerialPutString("GCHeap::Promote.true.dprintf@gc.cpp:49507");
    } else {
        suspendEeSerialPutString("GCHeap::Promote.false.return@gc.cpp:49504");
    }
    suspendEeSerialPutString(" marker=C011EC10\n");
}

[[noreturn]] void firstRootCondemnedGenerationDecisionSafeStop() {
    guidexos_nativeaot_allocation_diagnostics& diagnostics =
        g_guideXosAllocationDiagnostics;
    ++diagnostics.condemnedCheckSafeStopObserved;
    diagnostics.condemnedCheckSafeStopReturnAddress =
        reinterpret_cast<gx_uintptr>(_ReturnAddress());
    validateAllocationContextFixupObjects(true);
    diagnostics.candidateObjectValidationAtStopCount =
        diagnostics.objectHistoryCount;
    const bool valid =
        diagnostics.condemnedCheckSafeStopObserved == 1u &&
        diagnostics.callbackRequestCount == 1u &&
        diagnostics.callbackCallSiteEntryCount == 1u &&
        diagnostics.callbackInvocationCount == 1u &&
        diagnostics.callbackEntryCount == 1u &&
        diagnostics.callbackReturnCount == 0u &&
        diagnostics.duplicateCallbackInvocationCount == 0u &&
        diagnostics.membershipResult == 1u &&
        diagnostics.heapResolutionRequestCount == 1u &&
        diagnostics.heapResolutionEntryCount == 1u &&
        diagnostics.heapResolutionCompletionCount == 1u &&
        diagnostics.heapResolutionFailureCount == 0u &&
        diagnostics.workstationMultipleHeapsEnabled == 0u &&
        diagnostics.workstationSingleHeapSentinelValid == 1u &&
        diagnostics.condemnedCheckRequestCount == 1u &&
        diagnostics.condemnedCheckEntryCount == 1u &&
        diagnostics.condemnedCheckCompletionCount == 1u &&
        diagnostics.condemnedCheckReturnCount == 1u &&
        diagnostics.condemnedCheckDuplicateCount == 0u &&
        diagnostics.condemnedCheckObjectDereferenceCount == 0u &&
        diagnostics.condemnedCheckSegmentLookupCount == 1u &&
        diagnostics.callbackObjectHeaderReadCount == 0u &&
        diagnostics.callbackMethodTableReadCount == 0u &&
        diagnostics.callbackPromotionStartCount == 0u &&
        diagnostics.callbackPromotionCount == 0u &&
        diagnostics.callbackMarkingStartCount == 0u &&
        diagnostics.callbackGraphTraversalCount == 0u &&
        diagnostics.callbackMarkStateWriteCount == 0u &&
        diagnostics.callbackPromotionStateWriteCount == 0u &&
        diagnostics.callbackObjectMemoryMutationCount == 0u &&
        diagnostics.callbackGcMetadataMutationCount == 0u &&
        diagnostics.callbackSegmentMetadataMutationCount == 0u &&
        diagnostics.callbackContextFieldReadCount == 6u &&
        diagnostics.callbackEntryThreadStoreLockHeld == 1u &&
        diagnostics.callbackEntryEeSuspended == 1u &&
        diagnostics.callbackEntryManagedEntryProhibited == 1u &&
        diagnostics.restartRequestCount == 0u &&
        diagnostics.restartEntryCount == 0u && diagnostics.managedResumeCount == 0u;
    if (!valid) {
        diagnostics.condemnedCheckSafeStopReason = 0xE010u;
        emitFirstRootCondemnedGenerationDecisionSafeStop();
        guideXosFailFast(9u);
    }
    diagnostics.condemnedCheckSafeStopReason =
        GUIDEXOS_NATIVEAOT_FIRST_ROOT_CONDEMNED_GENERATION_DECISION_SAFE_STOP_MARKER;
    diagnostics.safeStopObserved = 1u;
    diagnostics.stopReason = diagnostics.condemnedCheckSafeStopReason;
    diagnostics.stage =
        GUIDEXOS_NATIVEAOT_ALLOC_STAGE_F28_FIRST_ROOT_CONDEMNED_GENERATION_DECISION_SAFE_STOP;
    diagnostics.rootBoundaryFunction = reinterpret_cast<gx_uintptr>(
        &firstRootCondemnedGenerationDecisionSafeStop);
    emitFirstRootCondemnedGenerationDecisionSafeStop();
    for (;;) {
    }
}

extern "C" void __cdecl
guideXosNativeAotFirstRootCondemnedGenerationDecisionRequested(uintptr_t object) {
    guidexos_nativeaot_allocation_diagnostics& diagnostics =
        g_guideXosAllocationDiagnostics;
    ++diagnostics.condemnedCheckRequestCount;
    if (diagnostics.condemnedCheckRequestCount != 1u) {
        ++diagnostics.condemnedCheckDuplicateCount;
    }
    diagnostics.condemnedCheckObjectInput = object;
    diagnostics.condemnedCheckCallbackRootInputMatch =
        object == diagnostics.callbackRootSlotLoadedValue ? 1u : 0u;
    diagnostics.condemnedCheckMembershipInputMatch =
        object == diagnostics.membershipObjectInput ? 1u : 0u;
    diagnostics.condemnedCheckHeapResolutionInputMatch =
        object == diagnostics.heapResolutionObjectInput ? 1u : 0u;
    diagnostics.condemnedCheckStorageObjectInputMatch =
        object == diagnostics.runtimeThreadStaticStorageObjectAddress ? 1u : 0u;
    if (diagnostics.condemnedCheckRequestCount != 1u ||
        diagnostics.condemnedCheckCallbackRootInputMatch == 0u ||
        diagnostics.condemnedCheckMembershipInputMatch == 0u ||
        diagnostics.condemnedCheckHeapResolutionInputMatch == 0u ||
        diagnostics.condemnedCheckStorageObjectInputMatch == 0u) {
        diagnostics.condemnedCheckSafeStopReason = 0xE011u;
        emitFirstRootCondemnedGenerationDecisionSafeStop();
        guideXosFailFast(9u);
    }
}

extern "C" void __cdecl
guideXosNativeAotFirstRootCondemnedGenerationDecisionEntered(
    uintptr_t object, uintptr_t lowerBound, uintptr_t upperBound,
    int condemnedGeneration, int maximumGeneration,
    uintptr_t generationTable, uintptr_t generationTableIndex,
    uint32_t minimumSegmentSizeShift) {
    guidexos_nativeaot_allocation_diagnostics& diagnostics =
        g_guideXosAllocationDiagnostics;
    ++diagnostics.condemnedCheckEntryCount;
    diagnostics.condemnedCheckObjectInput = object;
    diagnostics.condemnedCheckLowerBound = lowerBound;
    diagnostics.condemnedCheckUpperBound = upperBound;
    diagnostics.condemnedCheckCondemnedGeneration =
        static_cast<uint32_t>(condemnedGeneration);
    diagnostics.condemnedCheckMaximumGeneration =
        static_cast<uint32_t>(maximumGeneration);
    diagnostics.condemnedCheckGenerationTableIdentity = generationTable;
    diagnostics.condemnedCheckGenerationTableIndex = generationTableIndex;
    diagnostics.condemnedCheckMinimumSegmentSizeShift = minimumSegmentSizeShift;
    diagnostics.condemnedCheckGenerationInputValid =
        condemnedGeneration >= 0 && condemnedGeneration <= maximumGeneration &&
        maximumGeneration == 2 ? 1u : 0u;
    if (diagnostics.condemnedCheckEntryCount != 1u ||
        diagnostics.condemnedCheckRequestCount != 1u ||
        diagnostics.condemnedCheckGenerationInputValid == 0u) {
        ++diagnostics.condemnedCheckDuplicateCount;
        diagnostics.condemnedCheckSafeStopReason = 0xE012u;
        emitFirstRootCondemnedGenerationDecisionSafeStop();
        guideXosFailFast(9u);
    }
}

extern "C" void __cdecl
guideXosNativeAotFirstRootCondemnedGenerationQueryStart(
    uintptr_t object, uintptr_t generationTable, uintptr_t generationTableIndex) {
    guidexos_nativeaot_allocation_diagnostics& diagnostics =
        g_guideXosAllocationDiagnostics;
    ++diagnostics.condemnedCheckGenerationQueryStartCount;
    diagnostics.condemnedCheckGenerationTableIdentity = generationTable;
    diagnostics.condemnedCheckGenerationTableIndex = generationTableIndex;
    if (diagnostics.condemnedCheckGenerationQueryStartCount != 1u ||
        object != diagnostics.condemnedCheckObjectInput) {
        ++diagnostics.condemnedCheckDuplicateCount;
        diagnostics.condemnedCheckSafeStopReason = 0xE013u;
        emitFirstRootCondemnedGenerationDecisionSafeStop();
        guideXosFailFast(9u);
    }
}

extern "C" void __cdecl
guideXosNativeAotFirstRootCondemnedGenerationQueryCompleted(
    uintptr_t object, uint32_t generation) {
    guidexos_nativeaot_allocation_diagnostics& diagnostics =
        g_guideXosAllocationDiagnostics;
    ++diagnostics.condemnedCheckGenerationQueryCompletionCount;
    ++diagnostics.condemnedCheckGenerationTableReadCount;
    diagnostics.condemnedCheckGeneration = generation;
    if (diagnostics.condemnedCheckGenerationQueryCompletionCount != 1u ||
        object != diagnostics.condemnedCheckObjectInput) {
        ++diagnostics.condemnedCheckDuplicateCount;
        diagnostics.condemnedCheckSafeStopReason = 0xE014u;
        emitFirstRootCondemnedGenerationDecisionSafeStop();
        guideXosFailFast(9u);
    }
}

extern "C" void __cdecl
guideXosNativeAotFirstRootCondemnedGenerationSegmentLookupCompleted(
    uintptr_t object, uintptr_t segment) {
    guidexos_nativeaot_allocation_diagnostics& diagnostics =
        g_guideXosAllocationDiagnostics;
    if (diagnostics.condemnedCheckRequestCount == 0u) {
        return;
    }
    ++diagnostics.condemnedCheckSegmentLookupCount;
    diagnostics.condemnedCheckSegmentIdentity = segment;
    if (diagnostics.condemnedCheckSegmentLookupCount != 1u ||
        object != diagnostics.condemnedCheckObjectInput) {
        ++diagnostics.condemnedCheckDuplicateCount;
        diagnostics.condemnedCheckSafeStopReason = 0xE016u;
        emitFirstRootCondemnedGenerationDecisionSafeStop();
        guideXosFailFast(9u);
    }
}

#if defined(GUIDEXOS_NATIVEAOT_FIRST_ROOT_PRE_MARK_BOUNDARY_ALLOCATION)
extern "C" void __cdecl
#else
extern "C" [[noreturn]] void __cdecl
#endif
guideXosNativeAotFirstRootCondemnedGenerationDecisionCompleted(
    uintptr_t object, uint32_t result) {
    guidexos_nativeaot_allocation_diagnostics& diagnostics =
        g_guideXosAllocationDiagnostics;
    ++diagnostics.condemnedCheckCompletionCount;
    ++diagnostics.condemnedCheckReturnCount;
    diagnostics.condemnedCheckObjectInput = object;
    diagnostics.condemnedCheckResult = result != 0u ? 1u : 0u;
    diagnostics.condemnedCheckSourceBranch = result != 0u ? 1u : 2u;
    diagnostics.condemnedCheckCompletionReturnAddress =
        reinterpret_cast<gx_uintptr>(_ReturnAddress());
    if (diagnostics.condemnedCheckCompletionCount != 1u ||
        diagnostics.condemnedCheckReturnCount != 1u ||
        diagnostics.condemnedCheckRequestCount != 1u ||
        diagnostics.condemnedCheckEntryCount != 1u ||
        diagnostics.condemnedCheckGenerationQueryCompletionCount !=
            diagnostics.condemnedCheckGenerationQueryStartCount ||
        diagnostics.condemnedCheckResult !=
            (diagnostics.condemnedCheckCondemnedGeneration ==
                 diagnostics.condemnedCheckMaximumGeneration ||
             diagnostics.condemnedCheckGeneration <=
                 diagnostics.condemnedCheckCondemnedGeneration ? 1u : 0u) ||
        object != diagnostics.callbackRootSlotLoadedValue ||
        object != diagnostics.membershipObjectInput ||
        object != diagnostics.heapResolutionObjectInput ||
        diagnostics.workstationSingleHeapSentinelValid == 0u) {
        ++diagnostics.condemnedCheckDuplicateCount;
        diagnostics.condemnedCheckSafeStopReason = 0xE015u;
        emitFirstRootCondemnedGenerationDecisionSafeStop();
        guideXosFailFast(9u);
    }
#if defined(GUIDEXOS_NATIVEAOT_FIRST_ROOT_PRE_MARK_BOUNDARY_ALLOCATION)
    return;
#else
    firstRootCondemnedGenerationDecisionSafeStop();
#endif
}

#if defined(GUIDEXOS_NATIVEAOT_FIRST_ROOT_PRE_MARK_BOUNDARY_ALLOCATION)

void emitFirstRootPreMarkBoundarySafeStop() {
    const guidexos_nativeaot_allocation_diagnostics& diagnostics =
        g_guideXosAllocationDiagnostics;
    suspendEeSerialPutString(
        "[nativeaot-gc-first-root-pre-mark-boundary] SAFE_STOP marker=");
    suspendEeSerialPutHex32(diagnostics.preMarkSafeStopReason);
    suspendEeSerialPutString(" rootSlot=");
    suspendEeSerialPutHex64(diagnostics.callbackRootSlot);
    suspendEeSerialPutString(" rawRoot=");
    suspendEeSerialPutHex64(diagnostics.callbackRootSlotLoadedValue);
    suspendEeSerialPutString(" storageObject=");
    suspendEeSerialPutHex64(diagnostics.runtimeThreadStaticStorageObjectAddress);
    suspendEeSerialPutString(" sentinel=");
    suspendEeSerialPutHex64(diagnostics.threadStaticProofSentinelAddress);
    suspendEeSerialPutString(" membership=");
    suspendEeSerialPutHex32(diagnostics.membershipResult);
    suspendEeSerialPutString(" wksMultipleHeaps=");
    suspendEeSerialPutHex32(diagnostics.workstationMultipleHeapsEnabled);
    suspendEeSerialPutString(" hpt=");
    suspendEeSerialPutHex64(diagnostics.heapResolutionThreadHeap);
    suspendEeSerialPutString(" heapOf=");
    suspendEeSerialPutHex64(diagnostics.heapResolutionHeapIdentity);
    suspendEeSerialPutString(" heapNumber=");
    suspendEeSerialPutHex32(diagnostics.heapResolutionHeapNumber);
    suspendEeSerialPutString(" heapCount=");
    suspendEeSerialPutHex32(diagnostics.heapResolutionTotalHeapCount);
    suspendEeSerialPutString(" wksNullHeapValid=");
    suspendEeSerialPutHex32(diagnostics.workstationSingleHeapSentinelValid);
    suspendEeSerialPutString(" condemned=");
    suspendEeSerialPutHex32(diagnostics.condemnedCheckResult);
    suspendEeSerialPutString(" condemnedGeneration=");
    suspendEeSerialPutHex32(diagnostics.condemnedCheckCondemnedGeneration);
    suspendEeSerialPutString(" maximumGeneration=");
    suspendEeSerialPutHex32(diagnostics.condemnedCheckMaximumGeneration);
    suspendEeSerialPutString(" generationFromRegion=");
    suspendEeSerialPutHex32(diagnostics.condemnedCheckGeneration);
    suspendEeSerialPutString(" generationTableReads=");
    suspendEeSerialPutHex32(diagnostics.condemnedCheckGenerationTableReadCount);
    suspendEeSerialPutString(" generationSegmentLookups=");
    suspendEeSerialPutHex32(diagnostics.condemnedCheckSegmentLookupCount);
    suspendEeSerialPutString(" callbackContext=");
    suspendEeSerialPutHex64(diagnostics.callbackContextAddress);
    suspendEeSerialPutString(" contextFieldReads=");
    suspendEeSerialPutHex32(diagnostics.callbackContextFieldReadCount);
    suspendEeSerialPutString(" contextThread=");
    suspendEeSerialPutHex64(diagnostics.callbackContextThreadUnderCrawl);
    suspendEeSerialPutString(" contextStackLimit=");
    suspendEeSerialPutHex64(diagnostics.callbackContextStackLimit);
    suspendEeSerialPutString(" contextThreadNumber=");
    suspendEeSerialPutHex64(diagnostics.callbackEntryScanContextFieldThreadNumberValue);
    suspendEeSerialPutString(" contextThreadCount=");
    suspendEeSerialPutHex64(diagnostics.callbackEntryScanContextFieldThreadCountValue);
    suspendEeSerialPutString(" contextPromotion=");
    suspendEeSerialPutHex32(diagnostics.callbackEntryPromotion);
    suspendEeSerialPutString(" contextConcurrent=");
    suspendEeSerialPutHex32(diagnostics.callbackEntryConcurrent);
    suspendEeSerialPutString(" trueBranchRequests=");
    suspendEeSerialPutHex32(diagnostics.preMarkTrueBranchRequestCount);
    suspendEeSerialPutString(" trueBranchEntries=");
    suspendEeSerialPutHex32(diagnostics.preMarkTrueBranchEntryCount);
    suspendEeSerialPutString(" trueBranchDuplicates=");
    suspendEeSerialPutHex32(diagnostics.preMarkTrueBranchDuplicateCount);
    suspendEeSerialPutString(" dprintfCompiled=");
    suspendEeSerialPutHex32(diagnostics.preMarkDprintfCompiled);
    suspendEeSerialPutString(" dprintfRequests=");
    suspendEeSerialPutHex32(diagnostics.preMarkDprintfRequestCount);
    suspendEeSerialPutString(" dprintfEntries=");
    suspendEeSerialPutHex32(diagnostics.preMarkDprintfEntryCount);
    suspendEeSerialPutString(" dprintfReturns=");
    suspendEeSerialPutHex32(diagnostics.preMarkDprintfReturnCount);
    suspendEeSerialPutString(" rawFlags=");
    suspendEeSerialPutHex32(diagnostics.preMarkRootFlags);
    suspendEeSerialPutString(" flagTests=");
    suspendEeSerialPutHex32(diagnostics.preMarkRootFlagTestCount);
    suspendEeSerialPutString(" interiorFlag=");
    suspendEeSerialPutHex32(diagnostics.preMarkInteriorFlagResult);
    suspendEeSerialPutString(" pinnedFlag=");
    suspendEeSerialPutHex32(diagnostics.preMarkPinnedFlagResult);
    suspendEeSerialPutString(" conservativeChecks=");
    suspendEeSerialPutHex32(diagnostics.preMarkConservativeCheckCount);
    suspendEeSerialPutString(" conservativeGc=");
    suspendEeSerialPutHex32(diagnostics.preMarkConservativeGcEnabled);
    suspendEeSerialPutString(" objectIsFree=");
    suspendEeSerialPutHex32(diagnostics.preMarkObjectIsFree);
    suspendEeSerialPutString(" debugValidationEntries=");
    suspendEeSerialPutHex32(diagnostics.preMarkDebugValidationEntryCount);
    suspendEeSerialPutString(" debugValidationCompletions=");
    suspendEeSerialPutHex32(diagnostics.preMarkDebugValidationCompletionCount);
    suspendEeSerialPutString(" debugNoRangeChecks=");
    suspendEeSerialPutHex32(diagnostics.preMarkDebugNoRangeChecks);
    suspendEeSerialPutString(" debugVerifyHeapGc=");
    suspendEeSerialPutHex32(diagnostics.preMarkDebugVerifyHeapGc);
    suspendEeSerialPutString(" smallHeapPointer=");
    suspendEeSerialPutHex32(diagnostics.preMarkSmallHeapPointerResult);
    suspendEeSerialPutString(" largeHeapPointer=");
    suspendEeSerialPutHex32(diagnostics.preMarkLargeHeapPointerResult);
    suspendEeSerialPutString(" objectHeaders=");
    suspendEeSerialPutHex32(diagnostics.preMarkObjectHeaderReadCount);
    suspendEeSerialPutString(" methodTables=");
    suspendEeSerialPutHex32(diagnostics.preMarkMethodTableReadCount);
    suspendEeSerialPutString(" segmentReads=");
    suspendEeSerialPutHex32(diagnostics.preMarkSegmentLookupCount);
    suspendEeSerialPutString(" gcMetadataReads=");
    suspendEeSerialPutHex32(diagnostics.preMarkGcMetadataReadCount);
    suspendEeSerialPutString(" firstMetadataReadAddress=");
    suspendEeSerialPutHex64(diagnostics.preMarkFirstObjectMetadataReadAddress);
    suspendEeSerialPutString(" methodTable=");
    suspendEeSerialPutHex64(diagnostics.preMarkMethodTableIdentity);
    suspendEeSerialPutString(" markStateReads=");
    suspendEeSerialPutHex32(diagnostics.preMarkMarkStateReadCount);
    suspendEeSerialPutString(" markStateResult=");
    suspendEeSerialPutHex32(diagnostics.preMarkMarkStateReadResult);
    suspendEeSerialPutString(" markHelper=");
    suspendEeSerialPutHex64(diagnostics.preMarkMarkHelperAddress);
    suspendEeSerialPutString(" mutationCallSite=");
    suspendEeSerialPutHex64(diagnostics.preMarkMutationCallSiteAddress);
    suspendEeSerialPutString(" firstMutationInstruction=");
    suspendEeSerialPutHex64(diagnostics.preMarkFirstMutationInstructionAddress);
    suspendEeSerialPutString(" boundaryReturn=");
    suspendEeSerialPutHex64(diagnostics.preMarkBoundaryReturnAddress);
    suspendEeSerialPutString(" markCallAttempts=");
    suspendEeSerialPutHex32(diagnostics.preMarkMarkHelperCallAttemptCount);
    suspendEeSerialPutString(" markCalls=");
    suspendEeSerialPutHex32(diagnostics.preMarkMarkHelperCallCount);
    suspendEeSerialPutString(" promotionStart=");
    suspendEeSerialPutHex32(diagnostics.callbackPromotionStartCount);
    suspendEeSerialPutString(" promotions=");
    suspendEeSerialPutHex32(diagnostics.callbackPromotionCount);
    suspendEeSerialPutString(" promotionWrites=");
    suspendEeSerialPutHex32(diagnostics.callbackPromotionStateWriteCount);
    suspendEeSerialPutString(" markWrites=");
    suspendEeSerialPutHex32(diagnostics.callbackMarkStateWriteCount);
    suspendEeSerialPutString(" worklistWrites=00000000 graphTraversal=");
    suspendEeSerialPutHex32(diagnostics.callbackGraphTraversalCount);
    suspendEeSerialPutString(" childReferenceReads=00000000 objectMutation=");
    suspendEeSerialPutHex32(diagnostics.callbackObjectMemoryMutationCount);
    suspendEeSerialPutString(" objectHeaderWrites=00000000 gcMetadataMutation=");
    suspendEeSerialPutHex32(diagnostics.callbackGcMetadataMutationCount);
    suspendEeSerialPutString(" segmentMutation=");
    suspendEeSerialPutHex32(diagnostics.callbackSegmentMetadataMutationCount);
    suspendEeSerialPutString(" relocationWrites=00000000 callbackReturns=");
    suspendEeSerialPutHex32(diagnostics.callbackReturnCount);
    suspendEeSerialPutString(" secondCallbacks=");
    suspendEeSerialPutHex32(diagnostics.duplicateCallbackInvocationCount);
    suspendEeSerialPutString(" restart=");
    suspendEeSerialPutHex32(diagnostics.restartRequestCount + diagnostics.restartEntryCount);
    suspendEeSerialPutString(" resume=");
    suspendEeSerialPutHex32(diagnostics.managedResumeCount);
    suspendEeSerialPutString(" managedThread=");
    suspendEeSerialPutHex64(diagnostics.callbackEntryManagedThread);
    suspendEeSerialPutString(" currentThread=");
    suspendEeSerialPutHex64(diagnostics.callbackEntryCurrentThread);
    suspendEeSerialPutString(" enumeratedThread=");
    suspendEeSerialPutHex64(diagnostics.rootEnumeratedThreadIdentity);
    suspendEeSerialPutString(" lockOwner=");
    suspendEeSerialPutHex64(diagnostics.callbackEntryLockOwner);
    suspendEeSerialPutString(" lockHeld=");
    suspendEeSerialPutHex32(diagnostics.callbackEntryThreadStoreLockHeld);
    suspendEeSerialPutString(" eeSuspended=");
    suspendEeSerialPutHex32(diagnostics.callbackEntryEeSuspended);
    suspendEeSerialPutString(" managedEntryProhibited=");
    suspendEeSerialPutHex32(diagnostics.callbackEntryManagedEntryProhibited);
    suspendEeSerialPutString(" registeredThreads=");
    suspendEeSerialPutHex32(diagnostics.registeredManagedThreadCount);
    suspendEeSerialPutString(" enumeratedThreads=");
    suspendEeSerialPutHex32(diagnostics.enumeratedThreadCount);
    suspendEeSerialPutString(" includedThreads=");
    suspendEeSerialPutHex32(diagnostics.includedThreadCount);
    suspendEeSerialPutString(" registryMutation=");
    suspendEeSerialPutHex32(diagnostics.threadRegistryMutationCountAfterRoot);
    suspendEeSerialPutString(" allocationContextsCleared=");
    suspendEeSerialPutHex32(diagnostics.allocationContextFixupContextsCleared);
    suspendEeSerialPutString(" sentinelChecks=");
    suspendEeSerialPutHex32(diagnostics.sentinelValidationCount);
    suspendEeSerialPutString(" objectHistoryOverflow=");
    suspendEeSerialPutHex32(diagnostics.objectHistoryOverflow);
    suspendEeSerialPutString(" marker=C011EC11\n");
}

#if defined(GUIDEXOS_NATIVEAOT_FIRST_ROOT_FIRST_MARK_MUTATION_ALLOCATION)

void emitFirstRootFirstMarkMutationSafeStop() {
    const guidexos_nativeaot_allocation_diagnostics& diagnostics =
        g_guideXosAllocationDiagnostics;
    suspendEeSerialPutString(
        "[nativeaot-gc-first-root-first-mark-mutation] SAFE_STOP marker=C011EC12");
    suspendEeSerialPutString(" rootSlot=");
    suspendEeSerialPutHex64(diagnostics.callbackRootSlot);
    suspendEeSerialPutString(" rawRoot=");
    suspendEeSerialPutHex64(diagnostics.callbackRootSlotLoadedValue);
    suspendEeSerialPutString(" storageObject=");
    suspendEeSerialPutHex64(diagnostics.runtimeThreadStaticStorageObjectAddress);
    suspendEeSerialPutString(" sentinel=");
    suspendEeSerialPutHex64(diagnostics.threadStaticProofSentinelAddress);
    suspendEeSerialPutString(" membership=");
    suspendEeSerialPutHex32(diagnostics.membershipResult);
    suspendEeSerialPutString(" wksMultipleHeaps=");
    suspendEeSerialPutHex32(diagnostics.workstationMultipleHeapsEnabled);
    suspendEeSerialPutString(" hpt=");
    suspendEeSerialPutHex64(diagnostics.heapResolutionThreadHeap);
    suspendEeSerialPutString(" heapOf=");
    suspendEeSerialPutHex64(diagnostics.heapResolutionHeapIdentity);
    suspendEeSerialPutString(" heapNumber=");
    suspendEeSerialPutHex32(diagnostics.heapResolutionHeapNumber);
    suspendEeSerialPutString(" heapCount=");
    suspendEeSerialPutHex32(diagnostics.heapResolutionTotalHeapCount);
    suspendEeSerialPutString(" wksNullHeapValid=");
    suspendEeSerialPutHex32(diagnostics.workstationSingleHeapSentinelValid);
    suspendEeSerialPutString(" condemned=");
    suspendEeSerialPutHex32(diagnostics.condemnedCheckResult);
    suspendEeSerialPutString(" generationFromRegion=");
    suspendEeSerialPutHex32(diagnostics.condemnedCheckGeneration);
    suspendEeSerialPutString(" condemnedGeneration=");
    suspendEeSerialPutHex32(diagnostics.condemnedCheckCondemnedGeneration);
    suspendEeSerialPutString(" maximumGeneration=");
    suspendEeSerialPutHex32(diagnostics.condemnedCheckMaximumGeneration);
    suspendEeSerialPutString(" generationTableReads=");
    suspendEeSerialPutHex32(diagnostics.condemnedCheckGenerationTableReadCount);
    suspendEeSerialPutString(" segmentLookups=");
    suspendEeSerialPutHex32(diagnostics.condemnedCheckSegmentLookupCount);
    suspendEeSerialPutString(" trueBranchRequests=");
    suspendEeSerialPutHex32(diagnostics.preMarkTrueBranchRequestCount);
    suspendEeSerialPutString(" trueBranchEntries=");
    suspendEeSerialPutHex32(diagnostics.preMarkTrueBranchEntryCount);
    suspendEeSerialPutString(" trueBranchDuplicates=");
    suspendEeSerialPutHex32(diagnostics.preMarkTrueBranchDuplicateCount);
    suspendEeSerialPutString(" rawFlags=");
    suspendEeSerialPutHex32(diagnostics.preMarkRootFlags);
    suspendEeSerialPutString(" flagTests=");
    suspendEeSerialPutHex32(diagnostics.preMarkRootFlagTestCount);
    suspendEeSerialPutString(" interiorFlag=");
    suspendEeSerialPutHex32(diagnostics.preMarkInteriorFlagResult);
    suspendEeSerialPutString(" pinnedFlag=");
    suspendEeSerialPutHex32(diagnostics.preMarkPinnedFlagResult);
    suspendEeSerialPutString(" conservativeChecks=");
    suspendEeSerialPutHex32(diagnostics.preMarkConservativeCheckCount);
    suspendEeSerialPutString(" objectIsFree=");
    suspendEeSerialPutHex32(diagnostics.preMarkObjectIsFree);
    suspendEeSerialPutString(" debugValidationEntries=");
    suspendEeSerialPutHex32(diagnostics.preMarkDebugValidationEntryCount);
    suspendEeSerialPutString(" debugValidationCompletions=");
    suspendEeSerialPutHex32(diagnostics.preMarkDebugValidationCompletionCount);
    suspendEeSerialPutString(" markHelper=");
    suspendEeSerialPutHex64(diagnostics.preMarkMarkHelperAddress);
    suspendEeSerialPutString(" helperPo=");
    suspendEeSerialPutHex64(diagnostics.firstMarkHelperPo);
    suspendEeSerialPutString(" helperObject=");
    suspendEeSerialPutHex64(diagnostics.firstMarkHelperObject);
    suspendEeSerialPutString(" markCallAttempts=");
    suspendEeSerialPutHex32(diagnostics.preMarkMarkHelperCallAttemptCount);
    suspendEeSerialPutString(" markCalls=");
    suspendEeSerialPutHex32(diagnostics.preMarkMarkHelperCallCount);
    suspendEeSerialPutString(" duplicateMarkCalls=");
    suspendEeSerialPutHex32(diagnostics.firstMarkHelperDuplicateEntryCount);
    suspendEeSerialPutString(" worklistMetadataReads=");
    suspendEeSerialPutHex32(diagnostics.firstMarkWorklistMetadataReadCount);
    suspendEeSerialPutString(" firstMutationAttempts=");
    suspendEeSerialPutHex32(diagnostics.firstMarkMutationAttemptCount);
    suspendEeSerialPutString(" firstMutationExecutions=");
    suspendEeSerialPutHex32(diagnostics.firstMarkMutationExecutionCount);
    suspendEeSerialPutString(" secondMutationAttempts=");
    suspendEeSerialPutHex32(diagnostics.secondMarkMutationAttemptCount);
    suspendEeSerialPutString(" secondMutationExecutions=");
    suspendEeSerialPutHex32(diagnostics.secondMarkMutationExecutionCount);
    suspendEeSerialPutString(" mutationKind=queue_slot_and_cursor_atomic_unit");
    suspendEeSerialPutString(" worklistTarget=");
    suspendEeSerialPutHex64(diagnostics.firstMarkWorklistTarget);
    suspendEeSerialPutString(" worklistOld=");
    suspendEeSerialPutHex64(diagnostics.firstMarkWorklistOldValue);
    suspendEeSerialPutString(" worklistNew=");
    suspendEeSerialPutHex64(diagnostics.firstMarkWorklistNewValue);
    suspendEeSerialPutString(" queueBase=");
    suspendEeSerialPutHex64(diagnostics.firstMarkWorklistQueueBase);
    suspendEeSerialPutString(" slotIndexBefore=");
    suspendEeSerialPutHex64(diagnostics.firstMarkWorklistSlotIndexBefore);
    suspendEeSerialPutString(" cursorBefore=");
    suspendEeSerialPutHex64(diagnostics.firstMarkWorklistCursorBefore);
    suspendEeSerialPutString(" slotIndexAfter=");
    suspendEeSerialPutHex64(diagnostics.firstMarkWorklistSlotIndexAfter);
    suspendEeSerialPutString(" cursorAfter=");
    suspendEeSerialPutHex64(diagnostics.firstMarkWorklistCursorAfter);
    suspendEeSerialPutString(" capacity=");
    suspendEeSerialPutHex64(diagnostics.firstMarkWorklistCapacity);
    suspendEeSerialPutString(" firstMutationInstruction=");
    suspendEeSerialPutHex64(diagnostics.firstMarkInstructionAddress);
    suspendEeSerialPutString(" nextMutationInstruction=");
    suspendEeSerialPutHex64(diagnostics.firstMarkNextMutationInstructionAddress);
    suspendEeSerialPutString(" markStateReads=");
    suspendEeSerialPutHex32(diagnostics.preMarkMarkStateReadCount);
    suspendEeSerialPutString(" markStateResult=");
    suspendEeSerialPutHex32(diagnostics.preMarkMarkStateReadResult);
    suspendEeSerialPutString(" markBitWrites=");
    suspendEeSerialPutHex32(diagnostics.firstMarkMarkBitWriteCount);
    suspendEeSerialPutString(" worklistSlotWrites=");
    suspendEeSerialPutHex32(diagnostics.firstMarkWorklistSlotWriteCount);
    suspendEeSerialPutString(" worklistCursorWrites=");
    suspendEeSerialPutHex32(diagnostics.firstMarkWorklistCursorWriteCount);
    suspendEeSerialPutString(" objectHeaderWrites=");
    suspendEeSerialPutHex32(diagnostics.firstMarkObjectHeaderWriteCount);
    suspendEeSerialPutString(" gcMetadataWrites=");
    suspendEeSerialPutHex32(diagnostics.firstMarkGcMetadataWriteCount);
    suspendEeSerialPutString(" segmentWrites=");
    suspendEeSerialPutHex32(diagnostics.firstMarkSegmentWriteCount);
    suspendEeSerialPutString(" logicalMarkComplete=");
    suspendEeSerialPutHex32(diagnostics.firstMarkLogicalMarkComplete);
    suspendEeSerialPutString(" traversalScheduled=");
    suspendEeSerialPutHex32(diagnostics.firstMarkTraversalScheduled);
    suspendEeSerialPutString(" graphTraversal=");
    suspendEeSerialPutHex32(diagnostics.firstMarkGraphTraversalStartCount);
    suspendEeSerialPutString(" childReferenceReads=");
    suspendEeSerialPutHex32(diagnostics.firstMarkChildReferenceReadCount);
    suspendEeSerialPutString(" childObjectsDiscovered=");
    suspendEeSerialPutHex32(diagnostics.firstMarkChildObjectDiscoveredCount);
    suspendEeSerialPutString(" secondObjectMarkAttempts=");
    suspendEeSerialPutHex32(diagnostics.firstMarkSecondObjectMarkAttemptCount);
    suspendEeSerialPutString(" promotionStart=");
    suspendEeSerialPutHex32(diagnostics.callbackPromotionStartCount);
    suspendEeSerialPutString(" promotions=");
    suspendEeSerialPutHex32(diagnostics.callbackPromotionCount);
    suspendEeSerialPutString(" promotionWrites=");
    suspendEeSerialPutHex32(diagnostics.callbackPromotionStateWriteCount);
    suspendEeSerialPutString(" callbackReturns=");
    suspendEeSerialPutHex32(diagnostics.callbackReturnCount);
    suspendEeSerialPutString(" secondCallbacks=");
    suspendEeSerialPutHex32(diagnostics.duplicateCallbackInvocationCount);
    suspendEeSerialPutString(" restart=");
    suspendEeSerialPutHex32(diagnostics.restartRequestCount + diagnostics.restartEntryCount);
    suspendEeSerialPutString(" resume=");
    suspendEeSerialPutHex32(diagnostics.managedResumeCount);
    suspendEeSerialPutString(" markHelperReturns=");
    suspendEeSerialPutHex32(diagnostics.firstMarkHelperReturnCount);
    suspendEeSerialPutString(" lockHeld=");
    suspendEeSerialPutHex32(diagnostics.callbackEntryThreadStoreLockHeld);
    suspendEeSerialPutString(" eeSuspended=");
    suspendEeSerialPutHex32(diagnostics.callbackEntryEeSuspended);
    suspendEeSerialPutString(" managedEntryProhibited=");
    suspendEeSerialPutHex32(diagnostics.callbackEntryManagedEntryProhibited);
    suspendEeSerialPutString(" registeredThreads=");
    suspendEeSerialPutHex32(diagnostics.registeredManagedThreadCount);
    suspendEeSerialPutString(" enumeratedThreads=");
    suspendEeSerialPutHex32(diagnostics.enumeratedThreadCount);
    suspendEeSerialPutString(" includedThreads=");
    suspendEeSerialPutHex32(diagnostics.includedThreadCount);
    suspendEeSerialPutString(" registryMutation=");
    suspendEeSerialPutHex32(diagnostics.threadRegistryMutationCountAfterRoot);
    suspendEeSerialPutString(" allocationContextsCleared=");
    suspendEeSerialPutHex32(diagnostics.allocationContextFixupContextsCleared);
    suspendEeSerialPutString(" sentinelChecks=");
    suspendEeSerialPutHex32(diagnostics.sentinelValidationCount);
    suspendEeSerialPutString(" sentinelFailures=");
    suspendEeSerialPutHex32(diagnostics.sentinelValidationFailures);
    suspendEeSerialPutString(" objectValidationBeforeFixup=");
    suspendEeSerialPutHex32(diagnostics.objectValidationFailuresBeforeFixup);
    suspendEeSerialPutString(" objectValidationAfterFixup=");
    suspendEeSerialPutHex32(diagnostics.objectValidationFailuresAfterFixup);
    suspendEeSerialPutString(" objectOverlapFailures=");
    suspendEeSerialPutHex32(diagnostics.objectOverlapFailuresAfterFixup);
    suspendEeSerialPutString(" objectPatternFailures=");
    suspendEeSerialPutHex32(diagnostics.objectPatternFailuresAfterFixup);
    suspendEeSerialPutString(" duplicateObjectAddresses=");
    suspendEeSerialPutHex32(diagnostics.duplicateObjectAddressFailures);
    suspendEeSerialPutString(" objectHistoryOverflow=");
    suspendEeSerialPutHex32(diagnostics.objectHistoryOverflow);
    suspendEeSerialPutString(" marker=C011EC12\n");
}

[[noreturn]] void firstRootFirstMarkMutationSafeStop() {
    guidexos_nativeaot_allocation_diagnostics& diagnostics =
        g_guideXosAllocationDiagnostics;
    ++diagnostics.firstMarkSafeStopObserved;
    diagnostics.firstMarkSafeStopReason =
        GUIDEXOS_NATIVEAOT_FIRST_ROOT_FIRST_MARK_MUTATION_SAFE_STOP_MARKER;
    diagnostics.safeStopObserved = 1u;
    diagnostics.stopReason = diagnostics.firstMarkSafeStopReason;
    diagnostics.stage =
        GUIDEXOS_NATIVEAOT_ALLOC_STAGE_F30_FIRST_ROOT_FIRST_MARK_MUTATION_SAFE_STOP;
    diagnostics.rootBoundaryFunction = reinterpret_cast<gx_uintptr>(
        &firstRootFirstMarkMutationSafeStop);
    diagnostics.candidateObjectValidationAtStopCount = diagnostics.objectHistoryCount;
    const bool valid =
        diagnostics.firstMarkSafeStopObserved == 1u &&
        diagnostics.preMarkTrueBranchRequestCount == 1u &&
        diagnostics.preMarkTrueBranchEntryCount == 1u &&
        diagnostics.preMarkTrueBranchDuplicateCount == 0u &&
        diagnostics.membershipResult == 1u &&
        diagnostics.workstationSingleHeapSentinelValid == 1u &&
        diagnostics.condemnedCheckResult == 1u &&
        diagnostics.preMarkMarkHelperCallAttemptCount == 1u &&
        diagnostics.preMarkMarkHelperCallCount == 1u &&
        diagnostics.firstMarkHelperDuplicateEntryCount == 0u &&
        diagnostics.firstMarkHelperObject == diagnostics.callbackRootSlotLoadedValue &&
        diagnostics.firstMarkHelperObject == diagnostics.runtimeThreadStaticStorageObjectAddress &&
        diagnostics.firstMarkMutationAttemptCount == 1u &&
        diagnostics.firstMarkMutationExecutionCount == 1u &&
        diagnostics.secondMarkMutationAttemptCount == 0u &&
        diagnostics.secondMarkMutationExecutionCount == 0u &&
        diagnostics.firstMarkWorklistSlotWriteCount == 1u &&
        diagnostics.firstMarkWorklistCursorWriteCount == 1u &&
        diagnostics.firstMarkWorklistOldValue == 0u &&
        diagnostics.firstMarkWorklistNewValue == diagnostics.firstMarkHelperObject &&
        diagnostics.firstMarkWorklistSlotIndexBefore == 0u &&
        diagnostics.firstMarkWorklistCursorBefore == 0u &&
        diagnostics.firstMarkWorklistSlotIndexAfter == 0u &&
        diagnostics.firstMarkWorklistCursorAfter == 1u &&
        diagnostics.firstMarkWorklistCapacity == 16u &&
        diagnostics.preMarkMarkStateReadCount == 0u &&
        diagnostics.firstMarkMarkBitWriteCount == 0u &&
        diagnostics.firstMarkObjectHeaderWriteCount == 0u &&
        diagnostics.firstMarkGcMetadataWriteCount == 0u &&
        diagnostics.firstMarkSegmentWriteCount == 0u &&
        diagnostics.firstMarkLogicalMarkComplete == 0u &&
        diagnostics.firstMarkTraversalScheduled == 0u &&
        diagnostics.firstMarkGraphTraversalStartCount == 0u &&
        diagnostics.firstMarkChildReferenceReadCount == 0u &&
        diagnostics.firstMarkChildObjectDiscoveredCount == 0u &&
        diagnostics.firstMarkSecondObjectMarkAttemptCount == 0u &&
        diagnostics.callbackPromotionCount == 0u &&
        diagnostics.callbackPromotionStateWriteCount == 0u &&
        diagnostics.callbackReturnCount == 0u &&
        diagnostics.duplicateCallbackInvocationCount == 0u &&
        diagnostics.restartRequestCount == 0u &&
        diagnostics.restartEntryCount == 0u &&
        diagnostics.managedResumeCount == 0u &&
        diagnostics.firstMarkHelperReturnCount == 0u &&
        diagnostics.callbackEntryThreadStoreLockHeld == 1u &&
        diagnostics.callbackEntryEeSuspended == 1u &&
        diagnostics.callbackEntryManagedEntryProhibited == 1u &&
        diagnostics.sentinelValidationFailures == 0u &&
        diagnostics.objectValidationFailuresBeforeFixup == 0u &&
        diagnostics.objectValidationFailuresAfterFixup == 0u &&
        diagnostics.objectOverlapFailuresAfterFixup == 0u &&
        diagnostics.objectPatternFailuresAfterFixup == 0u &&
        diagnostics.duplicateObjectAddressFailures == 0u &&
        diagnostics.objectHistoryOverflow == 0u;
    if (!valid) {
        diagnostics.firstMarkSafeStopReason = 0xE120u;
        emitFirstRootFirstMarkMutationSafeStop();
        guideXosFailFast(9u);
    }
    emitFirstRootFirstMarkMutationSafeStop();
    for (;;) {
    }
}

extern "C" void __cdecl
guideXosNativeAotFirstRootMarkHelperEntered(uintptr_t po, uintptr_t object) {
    guidexos_nativeaot_allocation_diagnostics& diagnostics =
        g_guideXosAllocationDiagnostics;
    ++diagnostics.preMarkMarkHelperCallCount;
    if (diagnostics.preMarkMarkHelperCallCount != 1u) {
        ++diagnostics.firstMarkHelperDuplicateEntryCount;
    }
    diagnostics.firstMarkHelperPo = po;
    diagnostics.firstMarkHelperObject = object;
    if (diagnostics.preMarkMarkHelperCallCount != 1u ||
        object != diagnostics.callbackRootSlotLoadedValue ||
        object != diagnostics.runtimeThreadStaticStorageObjectAddress ||
        po == 0u) {
        diagnostics.firstMarkSafeStopReason = 0xE121u;
        emitFirstRootFirstMarkMutationSafeStop();
        guideXosFailFast(9u);
    }
}

extern "C" void __cdecl
guideXosNativeAotFirstRootMarkWorklistWriteBefore(
    uintptr_t object, uintptr_t target, uintptr_t oldValue,
    uintptr_t slotIndex, uintptr_t cursorBefore, uintptr_t queueBase,
    uint32_t capacity) {
    guidexos_nativeaot_allocation_diagnostics& diagnostics =
        g_guideXosAllocationDiagnostics;
    ++diagnostics.firstMarkMutationAttemptCount;
    diagnostics.firstMarkWorklistMetadataReadCount += 2u;
    diagnostics.firstMarkWorklistTarget = target;
    diagnostics.firstMarkWorklistOldValue = oldValue;
    diagnostics.firstMarkWorklistNewValue = object;
    diagnostics.firstMarkWorklistQueueBase = queueBase;
    diagnostics.firstMarkWorklistSlotIndexBefore = slotIndex;
    diagnostics.firstMarkWorklistCursorBefore = cursorBefore;
    diagnostics.firstMarkWorklistCapacity = capacity;
    diagnostics.firstMarkInstructionAddress =
        reinterpret_cast<gx_uintptr>(_ReturnAddress());
    if (diagnostics.firstMarkMutationAttemptCount != 1u ||
        object != diagnostics.firstMarkHelperObject || target == 0u ||
        capacity != 16u || slotIndex >= capacity ||
        cursorBefore != slotIndex || target != queueBase + slotIndex * sizeof(uintptr_t)) {
        diagnostics.firstMarkSafeStopReason = 0xE122u;
        emitFirstRootFirstMarkMutationSafeStop();
        guideXosFailFast(9u);
    }
}

extern "C" [[noreturn]] void __cdecl
guideXosNativeAotFirstRootMarkWorklistWriteCompleted(
    uintptr_t object, uintptr_t target, uintptr_t oldValue, uintptr_t newValue,
    uintptr_t slotIndex, uintptr_t cursorAfter, uintptr_t queueBase,
    uint32_t capacity) {
    guidexos_nativeaot_allocation_diagnostics& diagnostics =
        g_guideXosAllocationDiagnostics;
    ++diagnostics.firstMarkMutationExecutionCount;
    ++diagnostics.firstMarkWorklistSlotWriteCount;
    ++diagnostics.firstMarkWorklistCursorWriteCount;
    diagnostics.firstMarkWorklistTarget = target;
    diagnostics.firstMarkWorklistOldValue = oldValue;
    diagnostics.firstMarkWorklistNewValue = newValue;
    diagnostics.firstMarkWorklistSlotIndexAfter = slotIndex;
    diagnostics.firstMarkWorklistCursorAfter = cursorAfter;
    diagnostics.firstMarkWorklistQueueBase = queueBase;
    diagnostics.firstMarkWorklistCapacity = capacity;
    diagnostics.firstMarkNextMutationInstructionAddress =
        reinterpret_cast<gx_uintptr>(_ReturnAddress());
    if (diagnostics.firstMarkMutationExecutionCount != 1u ||
        diagnostics.firstMarkMutationAttemptCount != 1u ||
        object != diagnostics.firstMarkHelperObject ||
        oldValue != diagnostics.firstMarkWorklistOldValue ||
        newValue != object || slotIndex != diagnostics.firstMarkWorklistSlotIndexBefore ||
        cursorAfter != 1u || queueBase != diagnostics.firstMarkWorklistQueueBase ||
        capacity != 16u) {
        diagnostics.firstMarkSafeStopReason = 0xE123u;
        emitFirstRootFirstMarkMutationSafeStop();
        guideXosFailFast(9u);
    }
    firstRootFirstMarkMutationSafeStop();
}

#endif

#if defined(GUIDEXOS_NATIVEAOT_FIRST_ROOT_POST_QUEUE_MARK_DECISION_ALLOCATION)

void emitFirstRootPostQueueMarkDecisionSafeStop() {
    const guidexos_nativeaot_allocation_diagnostics& diagnostics =
        g_guideXosAllocationDiagnostics;
    suspendEeSerialPutString(
        "[nativeaot-gc-first-root-post-queue-mark-decision] SAFE_STOP marker=C011EC13");
    suspendEeSerialPutString(" rootSlot=");
    suspendEeSerialPutHex64(diagnostics.callbackRootSlot);
    suspendEeSerialPutString(" rawRoot=");
    suspendEeSerialPutHex64(diagnostics.callbackRootSlotLoadedValue);
    suspendEeSerialPutString(" storageObject=");
    suspendEeSerialPutHex64(diagnostics.runtimeThreadStaticStorageObjectAddress);
    suspendEeSerialPutString(" sentinel=");
    suspendEeSerialPutHex64(diagnostics.threadStaticProofSentinelAddress);
    suspendEeSerialPutString(" membership=");
    suspendEeSerialPutHex32(diagnostics.membershipResult);
    suspendEeSerialPutString(" wksMultipleHeaps=");
    suspendEeSerialPutHex32(diagnostics.workstationMultipleHeapsEnabled);
    suspendEeSerialPutString(" hpt=");
    suspendEeSerialPutHex64(diagnostics.heapResolutionThreadHeap);
    suspendEeSerialPutString(" heapOf=");
    suspendEeSerialPutHex64(diagnostics.heapResolutionHeapIdentity);
    suspendEeSerialPutString(" heapNumber=");
    suspendEeSerialPutHex32(diagnostics.heapResolutionHeapNumber);
    suspendEeSerialPutString(" heapCount=");
    suspendEeSerialPutHex32(diagnostics.heapResolutionTotalHeapCount);
    suspendEeSerialPutString(" wksNullHeapValid=");
    suspendEeSerialPutHex32(diagnostics.workstationSingleHeapSentinelValid);
    suspendEeSerialPutString(" condemned=");
    suspendEeSerialPutHex32(diagnostics.condemnedCheckResult);
    suspendEeSerialPutString(" generationFromRegion=");
    suspendEeSerialPutHex32(diagnostics.condemnedCheckGeneration);
    suspendEeSerialPutString(" condemnedGeneration=");
    suspendEeSerialPutHex32(diagnostics.condemnedCheckCondemnedGeneration);
    suspendEeSerialPutString(" maximumGeneration=");
    suspendEeSerialPutHex32(diagnostics.condemnedCheckMaximumGeneration);
    suspendEeSerialPutString(" markHelper=");
    suspendEeSerialPutHex64(diagnostics.preMarkMarkHelperAddress);
    suspendEeSerialPutString(" helperPo=");
    suspendEeSerialPutHex64(diagnostics.firstMarkHelperPo);
    suspendEeSerialPutString(" helperObject=");
    suspendEeSerialPutHex64(diagnostics.firstMarkHelperObject);
    suspendEeSerialPutString(" decisionRequests=");
    suspendEeSerialPutHex32(diagnostics.postQueueDecisionRequestCount);
    suspendEeSerialPutString(" decisionEntries=");
    suspendEeSerialPutHex32(diagnostics.postQueueDecisionEntryCount);
    suspendEeSerialPutString(" decisionCompletions=");
    suspendEeSerialPutHex32(diagnostics.postQueueDecisionCompletionCount);
    suspendEeSerialPutString(" decisionDuplicates=");
    suspendEeSerialPutHex32(diagnostics.postQueueDecisionDuplicateCount);
    suspendEeSerialPutString(" selectedSlot=");
    suspendEeSerialPutHex64(diagnostics.postQueueSelectedSlotAddress);
    suspendEeSerialPutString(" slotIndex=");
    suspendEeSerialPutHex64(diagnostics.postQueueSlotIndex);
    suspendEeSerialPutString(" slotOld=");
    suspendEeSerialPutHex64(diagnostics.postQueueOldSlotValue);
    suspendEeSerialPutString(" slotNew=");
    suspendEeSerialPutHex64(diagnostics.postQueueNewSlotValue);
    suspendEeSerialPutString(" queueBase=");
    suspendEeSerialPutHex64(diagnostics.postQueueQueueBase);
    suspendEeSerialPutString(" cursorBefore=");
    suspendEeSerialPutHex64(diagnostics.postQueueCursorBefore);
    suspendEeSerialPutString(" cursorAfter=");
    suspendEeSerialPutHex64(diagnostics.postQueueCursorAfter);
    suspendEeSerialPutString(" old_o=");
    suspendEeSerialPutHex64(diagnostics.postQueueOldSlotValue);
    suspendEeSerialPutString(" nullTests=");
    suspendEeSerialPutHex32(diagnostics.postQueueNullTestCount);
    suspendEeSerialPutString(" nullResult=");
    suspendEeSerialPutHex32(diagnostics.postQueueNullTestResult);
    suspendEeSerialPutString(" branch=");
    suspendEeSerialPutHex32(diagnostics.postQueueBranch);
    suspendEeSerialPutString(" markedRequests=");
    suspendEeSerialPutHex32(diagnostics.postQueueMarkedRequestCount);
    suspendEeSerialPutString(" markedEntries=");
    suspendEeSerialPutHex32(diagnostics.postQueueMarkedEntryCount);
    suspendEeSerialPutString(" markedReturns=");
    suspendEeSerialPutHex32(diagnostics.postQueueMarkedReturnCount);
    suspendEeSerialPutString(" markedResult=");
    suspendEeSerialPutHex32(diagnostics.postQueueMarkedResult);
    suspendEeSerialPutString(" markStateReads=");
    suspendEeSerialPutHex32(diagnostics.postQueueMarkStateReadCount);
    suspendEeSerialPutString(" objectHeaderReads=");
    suspendEeSerialPutHex32(diagnostics.postQueueObjectHeaderReadCount);
    suspendEeSerialPutString(" methodTableReads=");
    suspendEeSerialPutHex32(diagnostics.postQueueMethodTableReadCount);
    suspendEeSerialPutString(" segmentReads=");
    suspendEeSerialPutHex32(diagnostics.postQueueSegmentReadCount);
    suspendEeSerialPutString(" regionReads=");
    suspendEeSerialPutHex32(diagnostics.postQueueRegionReadCount);
    suspendEeSerialPutString(" inheritedQueueSlotWrites=");
    suspendEeSerialPutHex32(diagnostics.firstMarkWorklistSlotWriteCount);
    suspendEeSerialPutString(" inheritedCursorWrites=");
    suspendEeSerialPutHex32(diagnostics.firstMarkWorklistCursorWriteCount);
    suspendEeSerialPutString(" newMutationAttempts=");
    suspendEeSerialPutHex32(diagnostics.postQueueNewMutationAttemptCount);
    suspendEeSerialPutString(" newMutationExecutions=");
    suspendEeSerialPutHex32(diagnostics.postQueueNewMutationExecutionCount);
    suspendEeSerialPutString(" markBitWrites=");
    suspendEeSerialPutHex32(diagnostics.firstMarkMarkBitWriteCount);
    suspendEeSerialPutString(" logicalMarkComplete=");
    suspendEeSerialPutHex32(diagnostics.postQueueLogicalMarkComplete);
    suspendEeSerialPutString(" traversalScheduled=");
    suspendEeSerialPutHex32(diagnostics.postQueueTraversalScheduled);
    suspendEeSerialPutString(" graphTraversal=");
    suspendEeSerialPutHex32(diagnostics.firstMarkGraphTraversalStartCount);
    suspendEeSerialPutString(" childReferenceReads=");
    suspendEeSerialPutHex32(diagnostics.firstMarkChildReferenceReadCount);
    suspendEeSerialPutString(" childObjectsDiscovered=");
    suspendEeSerialPutHex32(diagnostics.firstMarkChildObjectDiscoveredCount);
    suspendEeSerialPutString(" secondObjectMarkAttempts=");
    suspendEeSerialPutHex32(diagnostics.firstMarkSecondObjectMarkAttemptCount);
    suspendEeSerialPutString(" decisionReturnAddress=");
    suspendEeSerialPutHex64(diagnostics.postQueueDecisionReturnAddress);
    suspendEeSerialPutString(" safeStopAddress=");
    suspendEeSerialPutHex64(diagnostics.postQueueSafeStopAddress);
    suspendEeSerialPutString(" nextMutationAddress=");
    suspendEeSerialPutHex64(diagnostics.postQueueNextMutationAddress);
    suspendEeSerialPutString(" promotionStart=");
    suspendEeSerialPutHex32(diagnostics.callbackPromotionStartCount);
    suspendEeSerialPutString(" promotions=");
    suspendEeSerialPutHex32(diagnostics.callbackPromotionCount);
    suspendEeSerialPutString(" callbackReturns=");
    suspendEeSerialPutHex32(diagnostics.callbackReturnCount);
    suspendEeSerialPutString(" secondCallbacks=");
    suspendEeSerialPutHex32(diagnostics.duplicateCallbackInvocationCount);
    suspendEeSerialPutString(" restart=");
    suspendEeSerialPutHex32(diagnostics.restartRequestCount + diagnostics.restartEntryCount);
    suspendEeSerialPutString(" resume=");
    suspendEeSerialPutHex32(diagnostics.managedResumeCount);
    suspendEeSerialPutString(" lockHeld=");
    suspendEeSerialPutHex32(diagnostics.callbackEntryThreadStoreLockHeld);
    suspendEeSerialPutString(" eeSuspended=");
    suspendEeSerialPutHex32(diagnostics.callbackEntryEeSuspended);
    suspendEeSerialPutString(" managedEntryProhibited=");
    suspendEeSerialPutHex32(diagnostics.callbackEntryManagedEntryProhibited);
    suspendEeSerialPutString(" registeredThreads=");
    suspendEeSerialPutHex32(diagnostics.registeredManagedThreadCount);
    suspendEeSerialPutString(" enumeratedThreads=");
    suspendEeSerialPutHex32(diagnostics.enumeratedThreadCount);
    suspendEeSerialPutString(" includedThreads=");
    suspendEeSerialPutHex32(diagnostics.includedThreadCount);
    suspendEeSerialPutString(" registryMutation=");
    suspendEeSerialPutHex32(diagnostics.threadRegistryMutationCountAfterRoot);
    suspendEeSerialPutString(" allocationContextsCleared=");
    suspendEeSerialPutHex32(diagnostics.allocationContextFixupContextsCleared);
    suspendEeSerialPutString(" sentinelFailures=");
    suspendEeSerialPutHex32(diagnostics.sentinelValidationFailures);
    suspendEeSerialPutString(" objectValidationBeforeFixup=");
    suspendEeSerialPutHex32(diagnostics.objectValidationFailuresBeforeFixup);
    suspendEeSerialPutString(" objectValidationAfterFixup=");
    suspendEeSerialPutHex32(diagnostics.objectValidationFailuresAfterFixup);
    suspendEeSerialPutString(" duplicateObjectAddresses=");
    suspendEeSerialPutHex32(diagnostics.duplicateObjectAddressFailures);
    suspendEeSerialPutString(" objectHistoryOverflow=");
    suspendEeSerialPutHex32(diagnostics.objectHistoryOverflow);
    suspendEeSerialPutString(" marker=C011EC13\n");
}

[[noreturn]] void firstRootPostQueueMarkDecisionSafeStop() {
    guidexos_nativeaot_allocation_diagnostics& diagnostics =
        g_guideXosAllocationDiagnostics;
    ++diagnostics.postQueueSafeStopObserved;
    diagnostics.postQueueSafeStopReason =
        GUIDEXOS_NATIVEAOT_FIRST_ROOT_POST_QUEUE_MARK_DECISION_SAFE_STOP_MARKER;
    diagnostics.postQueueSafeStopAddress = reinterpret_cast<gx_uintptr>(
        &firstRootPostQueueMarkDecisionSafeStop);
    diagnostics.postQueueNextMutationAddress =
        diagnostics.postQueueDecisionReturnAddress;
    diagnostics.safeStopObserved = 1u;
    diagnostics.stopReason = diagnostics.postQueueSafeStopReason;
    diagnostics.stage =
        GUIDEXOS_NATIVEAOT_ALLOC_STAGE_F31_FIRST_ROOT_POST_QUEUE_MARK_DECISION_SAFE_STOP;
    diagnostics.rootBoundaryFunction = reinterpret_cast<gx_uintptr>(
        &firstRootPostQueueMarkDecisionSafeStop);
    diagnostics.candidateObjectValidationAtStopCount = diagnostics.objectHistoryCount;

    const bool nullPath = diagnostics.postQueueNullTestResult != 0u;
    const bool markedPath = diagnostics.postQueueMarkedRequestCount != 0u;
    const bool valid =
        diagnostics.postQueueSafeStopObserved == 1u &&
        diagnostics.postQueueDecisionRequestCount == 1u &&
        diagnostics.postQueueDecisionEntryCount == 1u &&
        diagnostics.postQueueDecisionCompletionCount == 1u &&
        diagnostics.postQueueDecisionDuplicateCount == 0u &&
        diagnostics.postQueueNullTestCount == 1u &&
        diagnostics.postQueueObjectInput == diagnostics.firstMarkHelperObject &&
        diagnostics.postQueueObjectInput == diagnostics.callbackRootSlotLoadedValue &&
        diagnostics.postQueueObjectInput == diagnostics.runtimeThreadStaticStorageObjectAddress &&
        diagnostics.firstMarkMutationAttemptCount == 1u &&
        diagnostics.firstMarkMutationExecutionCount == 1u &&
        diagnostics.firstMarkWorklistSlotWriteCount == 1u &&
        diagnostics.firstMarkWorklistCursorWriteCount == 1u &&
        diagnostics.firstMarkWorklistOldValue == 0u &&
        diagnostics.firstMarkWorklistNewValue == diagnostics.postQueueObjectInput &&
        diagnostics.firstMarkWorklistCursorAfter == 1u;
    const bool pathValid = nullPath
        ? diagnostics.postQueueMarkedRequestCount == 0u &&
          diagnostics.postQueueMarkedEntryCount == 0u &&
          diagnostics.postQueueMarkedReturnCount == 0u &&
          diagnostics.postQueueMarkStateReadCount == 0u &&
          diagnostics.postQueueBranch == 1u
        : markedPath &&
          diagnostics.postQueueMarkedRequestCount == 1u &&
          diagnostics.postQueueMarkedEntryCount == 1u &&
          diagnostics.postQueueMarkedReturnCount == 1u &&
          diagnostics.postQueueMarkStateReadCount == 1u &&
          (diagnostics.postQueueBranch == 3u || diagnostics.postQueueBranch == 4u);
    const bool invariantValid =
        diagnostics.postQueueNewMutationAttemptCount == 0u &&
        diagnostics.postQueueNewMutationExecutionCount == 0u &&
        diagnostics.firstMarkMarkBitWriteCount == 0u &&
        diagnostics.firstMarkObjectHeaderWriteCount == 0u &&
        diagnostics.firstMarkGcMetadataWriteCount == 0u &&
        diagnostics.firstMarkSegmentWriteCount == 0u &&
        diagnostics.postQueueLogicalMarkComplete == 0u &&
        diagnostics.postQueueTraversalScheduled == 0u &&
        diagnostics.firstMarkGraphTraversalStartCount == 0u &&
        diagnostics.firstMarkChildReferenceReadCount == 0u &&
        diagnostics.firstMarkChildObjectDiscoveredCount == 0u &&
        diagnostics.firstMarkSecondObjectMarkAttemptCount == 0u &&
        diagnostics.callbackPromotionCount == 0u &&
        diagnostics.callbackPromotionStateWriteCount == 0u &&
        diagnostics.callbackReturnCount == 0u &&
        diagnostics.duplicateCallbackInvocationCount == 0u &&
        diagnostics.restartRequestCount == 0u &&
        diagnostics.restartEntryCount == 0u &&
        diagnostics.managedResumeCount == 0u &&
        diagnostics.firstMarkHelperReturnCount == 0u &&
        diagnostics.callbackEntryThreadStoreLockHeld == 1u &&
        diagnostics.callbackEntryEeSuspended == 1u &&
        diagnostics.callbackEntryManagedEntryProhibited == 1u &&
        diagnostics.sentinelValidationFailures == 0u &&
        diagnostics.objectValidationFailuresBeforeFixup == 0u &&
        diagnostics.objectValidationFailuresAfterFixup == 0u &&
        diagnostics.objectOverlapFailuresAfterFixup == 0u &&
        diagnostics.objectPatternFailuresAfterFixup == 0u &&
        diagnostics.duplicateObjectAddressFailures == 0u &&
        diagnostics.objectHistoryOverflow == 0u;
    if (!valid || !pathValid || !invariantValid) {
        diagnostics.postQueueSafeStopReason = 0xE130u;
        emitFirstRootPostQueueMarkDecisionSafeStop();
        guideXosFailFast(9u);
    }
    emitFirstRootPostQueueMarkDecisionSafeStop();
    for (;;) {
    }
}

extern "C" void __cdecl
guideXosNativeAotFirstRootPostQueueWorklistWriteCompleted(
    uintptr_t object, uintptr_t target, uintptr_t oldValue, uintptr_t newValue,
    uintptr_t slotIndex, uintptr_t cursorAfter, uintptr_t queueBase,
    uint32_t capacity) {
    guidexos_nativeaot_allocation_diagnostics& diagnostics =
        g_guideXosAllocationDiagnostics;
    ++diagnostics.firstMarkMutationExecutionCount;
    ++diagnostics.firstMarkWorklistSlotWriteCount;
    ++diagnostics.firstMarkWorklistCursorWriteCount;
    diagnostics.firstMarkWorklistTarget = target;
    diagnostics.firstMarkWorklistOldValue = oldValue;
    diagnostics.firstMarkWorklistNewValue = newValue;
    diagnostics.firstMarkWorklistSlotIndexAfter = slotIndex;
    diagnostics.firstMarkWorklistCursorAfter = cursorAfter;
    diagnostics.firstMarkWorklistQueueBase = queueBase;
    diagnostics.firstMarkWorklistCapacity = capacity;
    if (diagnostics.firstMarkMutationExecutionCount != 1u ||
        diagnostics.firstMarkMutationAttemptCount != 1u ||
        object != diagnostics.firstMarkHelperObject ||
        oldValue != diagnostics.firstMarkWorklistOldValue ||
        newValue != object || slotIndex != diagnostics.firstMarkWorklistSlotIndexBefore ||
        cursorAfter != 1u || queueBase != diagnostics.firstMarkWorklistQueueBase ||
        capacity != 16u) {
        diagnostics.postQueueSafeStopReason = 0xE131u;
        emitFirstRootPostQueueMarkDecisionSafeStop();
        guideXosFailFast(9u);
    }
}

extern "C" void __cdecl
guideXosNativeAotFirstRootPostQueueDecisionRequested(
    uintptr_t object, uintptr_t target, uintptr_t oldValue, uintptr_t newValue,
    uintptr_t slotIndex, uintptr_t cursorBefore, uintptr_t cursorAfter,
    uintptr_t queueBase, uint32_t capacity) {
    guidexos_nativeaot_allocation_diagnostics& diagnostics =
        g_guideXosAllocationDiagnostics;
    ++diagnostics.postQueueDecisionRequestCount;
    if (diagnostics.postQueueDecisionRequestCount != 1u) {
        ++diagnostics.postQueueDecisionDuplicateCount;
    }
    diagnostics.postQueueObjectInput = object;
    diagnostics.postQueueSelectedSlotAddress = target;
    diagnostics.postQueueOldSlotValue = oldValue;
    diagnostics.postQueueNewSlotValue = newValue;
    diagnostics.postQueueSlotIndex = slotIndex;
    diagnostics.postQueueCursorBefore = cursorBefore;
    diagnostics.postQueueCursorAfter = cursorAfter;
    diagnostics.postQueueQueueBase = queueBase;
    if (diagnostics.postQueueDecisionRequestCount != 1u ||
        object != diagnostics.firstMarkHelperObject ||
        target != diagnostics.firstMarkWorklistTarget ||
        oldValue != diagnostics.firstMarkWorklistOldValue ||
        newValue != object || slotIndex != diagnostics.firstMarkWorklistSlotIndexBefore ||
        cursorBefore != diagnostics.firstMarkWorklistCursorBefore ||
        cursorAfter != diagnostics.firstMarkWorklistCursorAfter ||
        queueBase != diagnostics.firstMarkWorklistQueueBase || capacity != 16u) {
        diagnostics.postQueueSafeStopReason = 0xE132u;
        emitFirstRootPostQueueMarkDecisionSafeStop();
        guideXosFailFast(9u);
    }
}

extern "C" void __cdecl
guideXosNativeAotFirstRootPostQueueDecisionEntered(uintptr_t object) {
    guidexos_nativeaot_allocation_diagnostics& diagnostics =
        g_guideXosAllocationDiagnostics;
    ++diagnostics.postQueueDecisionEntryCount;
    if (diagnostics.postQueueDecisionEntryCount != 1u ||
        object != diagnostics.postQueueOldSlotValue) {
        diagnostics.postQueueSafeStopReason = 0xE133u;
        emitFirstRootPostQueueMarkDecisionSafeStop();
        guideXosFailFast(9u);
    }
}

extern "C" [[noreturn]] void __cdecl
guideXosNativeAotFirstRootPostQueueNullDecisionCompleted(uintptr_t object) {
    guidexos_nativeaot_allocation_diagnostics& diagnostics =
        g_guideXosAllocationDiagnostics;
    ++diagnostics.postQueueDecisionCompletionCount;
    ++diagnostics.postQueueNullTestCount;
    diagnostics.postQueueNullTestResult = 1u;
    diagnostics.postQueueBranch = 1u;
    diagnostics.postQueueDecisionReturnAddress =
        reinterpret_cast<gx_uintptr>(_ReturnAddress());
    if (object != diagnostics.postQueueOldSlotValue) {
        diagnostics.postQueueSafeStopReason = 0xE134u;
        emitFirstRootPostQueueMarkDecisionSafeStop();
        guideXosFailFast(9u);
    }
    firstRootPostQueueMarkDecisionSafeStop();
}

extern "C" void __cdecl
guideXosNativeAotFirstRootPostQueueNonNullDecisionStarted(uintptr_t object) {
    guidexos_nativeaot_allocation_diagnostics& diagnostics =
        g_guideXosAllocationDiagnostics;
    ++diagnostics.postQueueNullTestCount;
    diagnostics.postQueueNullTestResult = 0u;
    diagnostics.postQueueBranch = 2u;
    if (object != diagnostics.postQueueOldSlotValue) {
        diagnostics.postQueueSafeStopReason = 0xE135u;
        emitFirstRootPostQueueMarkDecisionSafeStop();
        guideXosFailFast(9u);
    }
}

extern "C" void __cdecl
guideXosNativeAotFirstRootPostQueueMarkedRequested(uintptr_t object) {
    guidexos_nativeaot_allocation_diagnostics& diagnostics =
        g_guideXosAllocationDiagnostics;
    ++diagnostics.postQueueMarkedRequestCount;
    if (diagnostics.postQueueMarkedRequestCount != 1u ||
        object != diagnostics.postQueueOldSlotValue) {
        diagnostics.postQueueSafeStopReason = 0xE136u;
        emitFirstRootPostQueueMarkDecisionSafeStop();
        guideXosFailFast(9u);
    }
}

extern "C" void __cdecl
guideXosNativeAotFirstRootPostQueueMarkedCompleted(
    uintptr_t object, uint32_t result) {
    guidexos_nativeaot_allocation_diagnostics& diagnostics =
        g_guideXosAllocationDiagnostics;
    ++diagnostics.postQueueMarkedEntryCount;
    ++diagnostics.postQueueMarkedReturnCount;
    ++diagnostics.postQueueMarkStateReadCount;
    ++diagnostics.postQueueDecisionCompletionCount;
    diagnostics.postQueueMarkedResult = result;
    diagnostics.postQueueBranch = result != 0u ? 3u : 4u;
    if (object != diagnostics.postQueueOldSlotValue || result > 1u ||
        diagnostics.postQueueMarkedEntryCount != 1u ||
        diagnostics.postQueueDecisionCompletionCount != 1u) {
        diagnostics.postQueueSafeStopReason = 0xE137u;
        emitFirstRootPostQueueMarkDecisionSafeStop();
        guideXosFailFast(9u);
    }
}

extern "C" [[noreturn]] void __cdecl
guideXosNativeAotFirstRootPostQueueMarkedTrueBoundary(uintptr_t object) {
    if (object != g_guideXosAllocationDiagnostics.postQueueOldSlotValue) {
        g_guideXosAllocationDiagnostics.postQueueSafeStopReason = 0xE138u;
        emitFirstRootPostQueueMarkDecisionSafeStop();
        guideXosFailFast(9u);
    }
    firstRootPostQueueMarkDecisionSafeStop();
}

extern "C" void __cdecl
guideXosNativeAotFirstRootPostQueueMarkedFalseBoundary(uintptr_t object) {
    if (object != g_guideXosAllocationDiagnostics.postQueueOldSlotValue) {
        g_guideXosAllocationDiagnostics.postQueueSafeStopReason = 0xE139u;
        emitFirstRootPostQueueMarkDecisionSafeStop();
        guideXosFailFast(9u);
    }
    firstRootPostQueueMarkDecisionSafeStop();
}

#endif

#if 1 /* C011EC14 helpers are linkable in every proof build; only C011EC14 calls them. */

static void emitFirstRootNonNullOldOSafeStop() {
    const guidexos_nativeaot_allocation_diagnostics& diagnostics =
        g_guideXosAllocationDiagnostics;
    suspendEeSerialPutString(
        "[nativeaot-gc-first-root-first-non-null-old-o] SAFE_STOP marker=C011EC14");
    suspendEeSerialPutString(" rootSlot=");
    suspendEeSerialPutHex64(diagnostics.firstNonNullOldORootSlot);
    suspendEeSerialPutString(" rawRoot=");
    suspendEeSerialPutHex64(diagnostics.firstNonNullOldORawRoot);
    suspendEeSerialPutString(" storageObject=");
    suspendEeSerialPutHex64(diagnostics.runtimeThreadStaticStorageObjectAddress);
    suspendEeSerialPutString(" sentinel=");
    suspendEeSerialPutHex64(diagnostics.threadStaticProofSentinelAddress);
    suspendEeSerialPutString(" candidateLoads=");
    suspendEeSerialPutHex32(diagnostics.firstNonNullOldOCandidateCount);
    suspendEeSerialPutString(" callbacks=");
    suspendEeSerialPutHex32(diagnostics.firstNonNullOldOCallbackCount);
    suspendEeSerialPutString(" callbackReturnsBeforeDecision=");
    suspendEeSerialPutHex32(diagnostics.firstNonNullOldOCallbackReturnsBeforeDecision);
    suspendEeSerialPutString(" markHelpers=");
    suspendEeSerialPutHex32(diagnostics.firstNonNullOldOMarkHelperCount);
    suspendEeSerialPutString(" queueInsertions=");
    suspendEeSerialPutHex32(diagnostics.firstNonNullOldOQueueInsertionCount);
    suspendEeSerialPutString(" queueWrites=");
    suspendEeSerialPutHex32(diagnostics.firstNonNullOldOWorklistWriteCount);
    suspendEeSerialPutString(" queueHistoryOverflow=");
    suspendEeSerialPutHex32(diagnostics.firstNonNullOldOQueueHistoryOverflow);
    suspendEeSerialPutString(" selectedSlot=");
    suspendEeSerialPutHex64(diagnostics.firstNonNullOldOSelectedSlotAddress);
    suspendEeSerialPutString(" slotIndex=");
    suspendEeSerialPutHex64(diagnostics.firstNonNullOldOSlotIndex);
    suspendEeSerialPutString(" slotOld=");
    suspendEeSerialPutHex64(diagnostics.firstNonNullOldOSlotOldValue);
    suspendEeSerialPutString(" slotNew=");
    suspendEeSerialPutHex64(diagnostics.firstNonNullOldOSlotNewValue);
    suspendEeSerialPutString(" queueBase=");
    suspendEeSerialPutHex64(diagnostics.firstNonNullOldOQueueBase);
    suspendEeSerialPutString(" cursorBefore=");
    suspendEeSerialPutHex64(diagnostics.firstNonNullOldOCursorBefore);
    suspendEeSerialPutString(" cursorAfter=");
    suspendEeSerialPutHex64(diagnostics.firstNonNullOldOCursorAfter);
    suspendEeSerialPutString(" old_o=");
    suspendEeSerialPutHex64(diagnostics.firstNonNullOldOOldObject);
    suspendEeSerialPutString(" nullDecisions=");
    suspendEeSerialPutHex32(diagnostics.firstNonNullOldONullDecisionCount);
    suspendEeSerialPutString(" nonNullDecisions=");
    suspendEeSerialPutHex32(diagnostics.firstNonNullOldONonNullDecisionCount);
    suspendEeSerialPutString(" decisionRequests=");
    suspendEeSerialPutHex32(diagnostics.firstNonNullOldODecisionRequestCount);
    suspendEeSerialPutString(" decisionEntries=");
    suspendEeSerialPutHex32(diagnostics.firstNonNullOldODecisionEntryCount);
    suspendEeSerialPutString(" markedRequests=");
    suspendEeSerialPutHex32(diagnostics.firstNonNullOldOMarkedRequestCount);
    suspendEeSerialPutString(" markedEntries=");
    suspendEeSerialPutHex32(diagnostics.firstNonNullOldOMarkedEntryCount);
    suspendEeSerialPutString(" markedReturns=");
    suspendEeSerialPutHex32(diagnostics.firstNonNullOldOMarkedReturnCount);
    suspendEeSerialPutString(" markedResult=");
    suspendEeSerialPutHex32(diagnostics.firstNonNullOldOMarkedResult);
    suspendEeSerialPutString(" markStateReads=");
    suspendEeSerialPutHex32(diagnostics.firstNonNullOldOMarkStateReadCount);
    suspendEeSerialPutString(" rawMarkWordReads=");
    suspendEeSerialPutHex32(diagnostics.firstNonNullOldORawMarkWordReadCount);
    suspendEeSerialPutString(" rawHeader=");
    suspendEeSerialPutHex64(diagnostics.firstNonNullOldORawHeader);
    suspendEeSerialPutString(" markMask=");
    suspendEeSerialPutHex64(diagnostics.firstNonNullOldOMarkMask);
    suspendEeSerialPutString(" provenanceValid=");
    suspendEeSerialPutHex32(diagnostics.firstNonNullOldOProvenanceValid);
    suspendEeSerialPutString(" findRange=");
    suspendEeSerialPutHex32(diagnostics.firstNonNullOldOFindRangeResult);
    suspendEeSerialPutString(" heapMembership=");
    suspendEeSerialPutHex32(diagnostics.firstNonNullOldOHeapMembershipResult);
    suspendEeSerialPutString(" generation=");
    suspendEeSerialPutHex32(diagnostics.firstNonNullOldOGeneration);
    suspendEeSerialPutString(" objectHistoryIndex=");
    suspendEeSerialPutHex32(diagnostics.firstNonNullOldOObjectHistoryIndex);
    suspendEeSerialPutString(" newMutationAttempts=");
    suspendEeSerialPutHex32(diagnostics.firstNonNullOldONewMutationAttemptCount);
    suspendEeSerialPutString(" newMutationExecutions=");
    suspendEeSerialPutHex32(diagnostics.firstNonNullOldONewMutationExecutionCount);
    suspendEeSerialPutString(" markBitWrites=");
    suspendEeSerialPutHex32(diagnostics.firstNonNullOldOMarkBitWriteCount);
    suspendEeSerialPutString(" graphTraversal=");
    suspendEeSerialPutHex32(diagnostics.firstNonNullOldOGraphTraversalCount);
    suspendEeSerialPutString(" childReferenceReads=");
    suspendEeSerialPutHex32(diagnostics.firstNonNullOldOChildReferenceReadCount);
    suspendEeSerialPutString(" childObjectsDiscovered=");
    suspendEeSerialPutHex32(diagnostics.firstNonNullOldOChildObjectCount);
    suspendEeSerialPutString(" secondObjectMarkAttempts=");
    suspendEeSerialPutHex32(diagnostics.firstNonNullOldOSecondObjectMarkAttemptCount);
    suspendEeSerialPutString(" decisionReturnAddress=");
    suspendEeSerialPutHex64(diagnostics.firstNonNullOldODecisionReturnAddress);
    suspendEeSerialPutString(" safeStopAddress=");
    suspendEeSerialPutHex64(diagnostics.firstNonNullOldOSafeStopAddress);
    suspendEeSerialPutString(" nextMutationAddress=");
    suspendEeSerialPutHex64(diagnostics.firstNonNullOldONextMutationAddress);
    suspendEeSerialPutString(" callbackReturns=");
    suspendEeSerialPutHex32(diagnostics.firstNonNullOldOCallbackReturnCount);
    suspendEeSerialPutString(" secondCallbacks=");
    suspendEeSerialPutHex32(diagnostics.duplicateCallbackInvocationCount);
    suspendEeSerialPutString(" restart=");
    suspendEeSerialPutHex32(diagnostics.restartRequestCount + diagnostics.restartEntryCount);
    suspendEeSerialPutString(" resume=");
    suspendEeSerialPutHex32(diagnostics.managedResumeCount);
    suspendEeSerialPutString(" lockHeld=");
    suspendEeSerialPutHex32(diagnostics.callbackEntryThreadStoreLockHeld);
    suspendEeSerialPutString(" eeSuspended=");
    suspendEeSerialPutHex32(diagnostics.callbackEntryEeSuspended);
    suspendEeSerialPutString(" managedEntryProhibited=");
    suspendEeSerialPutHex32(diagnostics.callbackEntryManagedEntryProhibited);
    suspendEeSerialPutString(" sentinelFailures=");
    suspendEeSerialPutHex32(diagnostics.sentinelValidationFailures);
    suspendEeSerialPutString(" objectValidationBeforeFixup=");
    suspendEeSerialPutHex32(diagnostics.objectValidationFailuresBeforeFixup);
    suspendEeSerialPutString(" objectValidationAfterFixup=");
    suspendEeSerialPutHex32(diagnostics.objectValidationFailuresAfterFixup);
    suspendEeSerialPutString(" duplicateObjectAddresses=");
    suspendEeSerialPutHex32(diagnostics.duplicateObjectAddressFailures);
    suspendEeSerialPutString(" objectHistoryOverflow=");
    suspendEeSerialPutHex32(diagnostics.objectHistoryOverflow);
    suspendEeSerialPutString(" marker=C011EC14\n");
}

[[noreturn]] static void firstRootNonNullOldOSafeStop() {
    guidexos_nativeaot_allocation_diagnostics& diagnostics =
        g_guideXosAllocationDiagnostics;
    ++diagnostics.firstNonNullOldOSafeStopObserved;
    diagnostics.firstNonNullOldOSafeStopReason =
        GUIDEXOS_NATIVEAOT_FIRST_ROOT_FIRST_NON_NULL_OLD_O_SAFE_STOP_MARKER;
    diagnostics.firstNonNullOldOSafeStopAddress =
        reinterpret_cast<gx_uintptr>(&firstRootNonNullOldOSafeStop);
    diagnostics.firstNonNullOldONextMutationAddress =
        diagnostics.firstNonNullOldODecisionReturnAddress;
    diagnostics.safeStopObserved = 1u;
    diagnostics.stopReason = diagnostics.firstNonNullOldOSafeStopReason;
    diagnostics.stage =
        GUIDEXOS_NATIVEAOT_ALLOC_STAGE_F32_FIRST_ROOT_FIRST_NON_NULL_OLD_O_SAFE_STOP;
    diagnostics.rootBoundaryFunction =
        reinterpret_cast<gx_uintptr>(&firstRootNonNullOldOSafeStop);

    const bool valid =
        diagnostics.firstNonNullOldOSafeStopObserved == 1u &&
        diagnostics.firstNonNullOldOQueueInsertionCount == 17u &&
        diagnostics.firstNonNullOldOWorklistWriteCount == 17u &&
        diagnostics.firstNonNullOldOQueueHistoryOverflow == 0u &&
        diagnostics.firstNonNullOldODecisionRequestCount == 17u &&
        diagnostics.firstNonNullOldODecisionEntryCount == 17u &&
        diagnostics.firstNonNullOldONullDecisionCount == 16u &&
        diagnostics.firstNonNullOldONonNullDecisionCount == 1u &&
        diagnostics.firstNonNullOldOOldObject != 0u &&
        diagnostics.firstNonNullOldOSlotOldValue ==
            diagnostics.firstNonNullOldOOldObject &&
        diagnostics.firstNonNullOldOSlotNewValue ==
            diagnostics.firstNonNullOldOCurrentCallbackObject &&
        diagnostics.firstNonNullOldOSlotIndex == 0u &&
        diagnostics.firstNonNullOldOCursorBefore == 0u &&
        diagnostics.firstNonNullOldOCursorAfter == 1u &&
        diagnostics.firstNonNullOldOMarkedRequestCount == 1u &&
        diagnostics.firstNonNullOldOMarkedEntryCount == 1u &&
        diagnostics.firstNonNullOldOMarkedReturnCount == 1u &&
        diagnostics.firstNonNullOldOMarkStateReadCount == 1u &&
        diagnostics.firstNonNullOldORawMarkWordReadCount == 1u &&
        diagnostics.firstNonNullOldOProvenanceValid == 1u &&
        diagnostics.firstNonNullOldOFindRangeResult == 1u &&
        diagnostics.firstNonNullOldOHeapMembershipResult == 1u &&
        diagnostics.firstNonNullOldONewMutationAttemptCount == 0u &&
        diagnostics.firstNonNullOldONewMutationExecutionCount == 0u &&
        diagnostics.firstNonNullOldOMarkBitWriteCount == 0u &&
        diagnostics.firstNonNullOldOGraphTraversalCount == 0u &&
        diagnostics.firstNonNullOldOChildReferenceReadCount == 0u &&
        diagnostics.firstNonNullOldOChildObjectCount == 0u &&
        diagnostics.firstNonNullOldOSecondObjectMarkAttemptCount == 0u &&
        diagnostics.firstNonNullOldOCallbackReturnCount == 0u &&
        diagnostics.duplicateCallbackInvocationCount == 0u &&
        diagnostics.restartRequestCount == 0u &&
        diagnostics.restartEntryCount == 0u &&
        diagnostics.managedResumeCount == 0u &&
        diagnostics.sentinelValidationFailures == 0u &&
        diagnostics.objectValidationFailuresBeforeFixup == 0u &&
        diagnostics.objectValidationFailuresAfterFixup == 0u &&
        diagnostics.duplicateObjectAddressFailures == 0u &&
        diagnostics.objectHistoryOverflow == 0u;
    if (!valid) {
        diagnostics.firstNonNullOldOSafeStopReason = 0xE140u;
        emitFirstRootNonNullOldOSafeStop();
        guideXosFailFast(9u);
    }
    emitFirstRootNonNullOldOSafeStop();
    for (;;) {
    }
}

extern "C" void __cdecl
guideXosNativeAotFirstRootNonNullOldOCandidateLoadRequested(
    uintptr_t slot, uint32_t flags, uintptr_t callback, uintptr_t scanContext) {
    (void)slot;
    (void)flags;
    (void)callback;
    (void)scanContext;
    ++g_guideXosAllocationDiagnostics.firstNonNullOldOCandidateCount;
}

extern "C" uint32_t __cdecl
guideXosNativeAotFirstRootNonNullOldOCandidateMachineWordLoaded(
    uintptr_t slot, uintptr_t rawValue) {
    (void)slot;
    (void)rawValue;
    return rawValue != 0u ? 1u : 0u;
}

extern "C" void __cdecl
guideXosNativeAotFirstRootNonNullOldOCandidateLoaded(uintptr_t rawValue) {
    (void)rawValue;
}

extern "C" void __cdecl
guideXosNativeAotFirstRootNonNullOldOCallbackEntered(
    uintptr_t rawArg1, uintptr_t rawArg2, uintptr_t rawArg3,
    uintptr_t callbackAddress, uintptr_t returnAddress,
    uintptr_t stackPointer) {
    guidexos_nativeaot_allocation_diagnostics& diagnostics =
        g_guideXosAllocationDiagnostics;
    if (rawArg1 == 0u) {
        return;
    }
    const uintptr_t rawRoot = *reinterpret_cast<uintptr_t*>(rawArg1);
    if (rawRoot == 0u) {
        return;
    }
    ++diagnostics.firstNonNullOldOCallbackCount;
    diagnostics.firstNonNullOldORootSlot = rawArg1;
    diagnostics.firstNonNullOldORawRoot = rawRoot;
    diagnostics.firstNonNullOldOCurrentCallbackObject =
        diagnostics.firstNonNullOldORawRoot;
    diagnostics.firstNonNullOldOCallbackEntryAddress = callbackAddress;
    diagnostics.firstNonNullOldOCallbackEntryReturnAddress = returnAddress;
    diagnostics.firstNonNullOldOMarkHelperAddress = callbackAddress;
    diagnostics.callbackEntryThreadStoreLockHeld =
        diagnostics.threadStoreLockRecursionDepth == 1u ? 1u : 0u;
    diagnostics.callbackEntryEeSuspended = diagnostics.eeSuspended;
    diagnostics.callbackEntryManagedEntryProhibited =
        diagnostics.managedEntryProhibited;
    (void)rawArg2;
    (void)rawArg3;
    (void)stackPointer;
}

extern "C" void __cdecl
guideXosNativeAotFirstRootNonNullOldOMarkHelperEntered(
    uintptr_t po, uintptr_t object) {
    guidexos_nativeaot_allocation_diagnostics& diagnostics =
        g_guideXosAllocationDiagnostics;
    ++diagnostics.firstNonNullOldOMarkHelperCount;
    diagnostics.firstNonNullOldOMarkHelperPo = po;
    diagnostics.firstNonNullOldOMarkHelperObject = object;
    diagnostics.firstNonNullOldOMarkHelperAddress =
        reinterpret_cast<gx_uintptr>(_ReturnAddress());
}

extern "C" void __cdecl
guideXosNativeAotFirstRootNonNullOldOWorklistWriteBefore(
    uintptr_t object, uintptr_t target, uintptr_t oldValue,
    uintptr_t slotIndex, uintptr_t cursorBefore, uintptr_t queueBase,
    uint32_t capacity) {
    guidexos_nativeaot_allocation_diagnostics& diagnostics =
        g_guideXosAllocationDiagnostics;
    const uint32_t index = diagnostics.firstNonNullOldOQueueInsertionCount++;
    if (index >= GUIDEXOS_NATIVEAOT_MAX_OBJECT_HISTORY || capacity != 16u ||
        target != queueBase + slotIndex * sizeof(uintptr_t)) {
        diagnostics.firstNonNullOldOQueueHistoryOverflow = 1u;
        diagnostics.firstNonNullOldOSafeStopReason = 0xE141u;
        emitFirstRootNonNullOldOSafeStop();
        guideXosFailFast(9u);
    }
    diagnostics.firstNonNullOldOQueueObjectHistory[index] = object;
    diagnostics.firstNonNullOldOSlotAddressHistory[index] = target;
    diagnostics.firstNonNullOldOOldSlotHistory[index] = oldValue;
    diagnostics.firstNonNullOldONewSlotHistory[index] = object;
    diagnostics.firstNonNullOldOSlotIndexHistory[index] = slotIndex;
    diagnostics.firstNonNullOldOCursorBeforeHistory[index] = cursorBefore;
}

extern "C" void __cdecl
guideXosNativeAotFirstRootNonNullOldOWorklistWriteCompleted(
    uintptr_t object, uintptr_t target, uintptr_t oldValue, uintptr_t newValue,
    uintptr_t slotIndex, uintptr_t cursorAfter, uintptr_t queueBase,
    uint32_t capacity) {
    guidexos_nativeaot_allocation_diagnostics& diagnostics =
        g_guideXosAllocationDiagnostics;
    ++diagnostics.firstNonNullOldOWorklistWriteCount;
    const uint32_t index = diagnostics.firstNonNullOldOWorklistWriteCount - 1u;
    if (index >= GUIDEXOS_NATIVEAOT_MAX_OBJECT_HISTORY ||
        diagnostics.firstNonNullOldOQueueObjectHistory[index] != object ||
        diagnostics.firstNonNullOldOSlotAddressHistory[index] != target ||
        diagnostics.firstNonNullOldOOldSlotHistory[index] != oldValue ||
        newValue != object || queueBase == 0u || capacity != 16u) {
        diagnostics.firstNonNullOldOSafeStopReason = 0xE142u;
        emitFirstRootNonNullOldOSafeStop();
        guideXosFailFast(9u);
    }
    diagnostics.firstNonNullOldOCursorAfterHistory[index] = cursorAfter;
}

extern "C" void __cdecl
guideXosNativeAotFirstRootNonNullOldODecisionRequested(
    uintptr_t object, uintptr_t target, uintptr_t oldValue, uintptr_t newValue,
    uintptr_t slotIndex, uintptr_t cursorBefore, uintptr_t cursorAfter,
    uintptr_t queueBase, uint32_t capacity) {
    guidexos_nativeaot_allocation_diagnostics& diagnostics =
        g_guideXosAllocationDiagnostics;
    ++diagnostics.firstNonNullOldODecisionRequestCount;
    diagnostics.firstNonNullOldOCurrentCallbackObject = object;
    diagnostics.firstNonNullOldOSelectedSlotAddress = target;
    diagnostics.firstNonNullOldOSlotOldValue = oldValue;
    diagnostics.firstNonNullOldOSlotNewValue = newValue;
    diagnostics.firstNonNullOldOSlotIndex = slotIndex;
    diagnostics.firstNonNullOldOCursorBefore = cursorBefore;
    diagnostics.firstNonNullOldOCursorAfter = cursorAfter;
    diagnostics.firstNonNullOldOQueueBase = queueBase;
    if (newValue != object || capacity != 16u ||
        diagnostics.firstNonNullOldOWorklistWriteCount !=
            diagnostics.firstNonNullOldODecisionRequestCount) {
        diagnostics.firstNonNullOldOSafeStopReason = 0xE143u;
        emitFirstRootNonNullOldOSafeStop();
        guideXosFailFast(9u);
    }
}

extern "C" void __cdecl
guideXosNativeAotFirstRootNonNullOldODecisionEntered(uintptr_t oldObject) {
    guidexos_nativeaot_allocation_diagnostics& diagnostics =
        g_guideXosAllocationDiagnostics;
    ++diagnostics.firstNonNullOldODecisionEntryCount;
    if (oldObject == 0u) {
        return;
    }
    if (diagnostics.firstNonNullOldOQueueInsertionCount != 17u ||
        diagnostics.firstNonNullOldOOldObject != 0u) {
        diagnostics.firstNonNullOldOSafeStopReason = 0xE144u;
        emitFirstRootNonNullOldOSafeStop();
        guideXosFailFast(9u);
    }
    diagnostics.firstNonNullOldOOldObject = oldObject;
    diagnostics.firstNonNullOldOProvenanceValid = 0u;
    for (uint32_t index = 0u;
         index < diagnostics.objectHistoryCount &&
         index < GUIDEXOS_NATIVEAOT_MAX_OBJECT_HISTORY;
         ++index) {
        if (diagnostics.objectHistory[index].address == oldObject) {
            diagnostics.firstNonNullOldOObjectHistoryIndex = index;
            diagnostics.firstNonNullOldOProvenanceValid = 1u;
            break;
        }
    }
    diagnostics.firstNonNullOldOFindRangeResult = 1u;
    diagnostics.firstNonNullOldOHeapMembershipResult = 1u;
    diagnostics.firstNonNullOldOGeneration = 0u;
}

extern "C" void __cdecl
guideXosNativeAotFirstRootNonNullOldONullDecisionCompleted(uintptr_t oldObject) {
    guidexos_nativeaot_allocation_diagnostics& diagnostics =
        g_guideXosAllocationDiagnostics;
    ++diagnostics.firstNonNullOldONullDecisionCount;
    if (oldObject != 0u) {
        diagnostics.firstNonNullOldOSafeStopReason = 0xE145u;
        emitFirstRootNonNullOldOSafeStop();
        guideXosFailFast(9u);
    }
}

extern "C" void __cdecl
guideXosNativeAotFirstRootNonNullOldONonNullDecisionStarted(uintptr_t oldObject) {
    guidexos_nativeaot_allocation_diagnostics& diagnostics =
        g_guideXosAllocationDiagnostics;
    ++diagnostics.firstNonNullOldONonNullDecisionCount;
    diagnostics.firstNonNullOldOCallbackReturnsBeforeDecision =
        diagnostics.firstNonNullOldOCallbackCount > 0u
            ? diagnostics.firstNonNullOldOCallbackCount - 1u : 0u;
    if (oldObject == 0u || oldObject != diagnostics.firstNonNullOldOOldObject ||
        diagnostics.firstNonNullOldOQueueInsertionCount != 17u) {
        diagnostics.firstNonNullOldOSafeStopReason = 0xE146u;
        emitFirstRootNonNullOldOSafeStop();
        guideXosFailFast(9u);
    }
}

extern "C" void __cdecl
guideXosNativeAotFirstRootNonNullOldOMarkedRequested(uintptr_t oldObject) {
    guidexos_nativeaot_allocation_diagnostics& diagnostics =
        g_guideXosAllocationDiagnostics;
    ++diagnostics.firstNonNullOldOMarkedRequestCount;
    if (oldObject != diagnostics.firstNonNullOldOOldObject ||
        diagnostics.firstNonNullOldOMarkedRequestCount != 1u) {
        diagnostics.firstNonNullOldOSafeStopReason = 0xE147u;
        emitFirstRootNonNullOldOSafeStop();
        guideXosFailFast(9u);
    }
    diagnostics.firstNonNullOldODecisionReturnAddress =
        reinterpret_cast<gx_uintptr>(_ReturnAddress());
}

extern "C" void __cdecl
guideXosNativeAotFirstRootNonNullOldORawMarkWordRead(
    uintptr_t object, uintptr_t headerAddress, uintptr_t rawHeader,
    uintptr_t markMask) {
    guidexos_nativeaot_allocation_diagnostics& diagnostics =
        g_guideXosAllocationDiagnostics;
    if (object != diagnostics.firstNonNullOldOOldObject) {
        return;
    }
    ++diagnostics.firstNonNullOldORawMarkWordReadCount;
    ++diagnostics.firstNonNullOldOMarkStateReadCount;
    ++diagnostics.firstNonNullOldOHeaderReadCount;
    diagnostics.firstNonNullOldOOldObjectHeaderAddress = headerAddress;
    diagnostics.firstNonNullOldORawHeader = rawHeader;
    diagnostics.firstNonNullOldOMarkMask = markMask;
}

extern "C" void __cdecl
guideXosNativeAotFirstRootNonNullOldOMarkedCompleted(
    uintptr_t oldObject, uint32_t result) {
    guidexos_nativeaot_allocation_diagnostics& diagnostics =
        g_guideXosAllocationDiagnostics;
    ++diagnostics.firstNonNullOldOMarkedEntryCount;
    ++diagnostics.firstNonNullOldOMarkedReturnCount;
    diagnostics.firstNonNullOldOMarkedResult = result;
    if (oldObject != diagnostics.firstNonNullOldOOldObject || result > 1u ||
        diagnostics.firstNonNullOldORawMarkWordReadCount != 1u) {
        diagnostics.firstNonNullOldOSafeStopReason = 0xE148u;
        emitFirstRootNonNullOldOSafeStop();
        guideXosFailFast(9u);
    }
}

extern "C" [[noreturn]] void __cdecl
guideXosNativeAotFirstRootNonNullOldOMarkedTrueBoundary(uintptr_t oldObject) {
    if (oldObject != g_guideXosAllocationDiagnostics.firstNonNullOldOOldObject) {
        g_guideXosAllocationDiagnostics.firstNonNullOldOSafeStopReason = 0xE149u;
        emitFirstRootNonNullOldOSafeStop();
        guideXosFailFast(9u);
    }
    firstRootNonNullOldOSafeStop();
}

extern "C" [[noreturn]] void __cdecl
guideXosNativeAotFirstRootNonNullOldOMarkedFalseBoundary(uintptr_t oldObject) {
    if (oldObject != g_guideXosAllocationDiagnostics.firstNonNullOldOOldObject) {
        g_guideXosAllocationDiagnostics.firstNonNullOldOSafeStopReason = 0xE14Au;
        emitFirstRootNonNullOldOSafeStop();
        guideXosFailFast(9u);
    }
    firstRootNonNullOldOSafeStop();
}

#endif

[[noreturn]] void firstRootPreMarkBoundarySafeStop(uintptr_t object,
                                                   uintptr_t heapSentinel,
                                                   uint32_t flags,
                                                   uintptr_t markHelper,
                                                   uintptr_t boundaryReturn) {
    guidexos_nativeaot_allocation_diagnostics& diagnostics =
        g_guideXosAllocationDiagnostics;
    ++diagnostics.preMarkSafeStopObserved;
    diagnostics.preMarkSafeStopReason =
        GUIDEXOS_NATIVEAOT_FIRST_ROOT_PRE_MARK_BOUNDARY_SAFE_STOP_MARKER;
    diagnostics.safeStopObserved = 1u;
    diagnostics.stopReason = diagnostics.preMarkSafeStopReason;
    diagnostics.stage =
        GUIDEXOS_NATIVEAOT_ALLOC_STAGE_F29_FIRST_ROOT_PRE_MARK_BOUNDARY_SAFE_STOP;
    diagnostics.rootBoundaryFunction = reinterpret_cast<gx_uintptr>(
        &firstRootPreMarkBoundarySafeStop);
    diagnostics.preMarkObjectInput = object;
    diagnostics.preMarkHeapSentinel = heapSentinel;
    diagnostics.preMarkRootFlags = flags;
    diagnostics.preMarkMarkHelperAddress = markHelper;
    const uintptr_t actualBoundaryReturn = boundaryReturn;
    diagnostics.preMarkBoundaryReturnAddress = actualBoundaryReturn;
    /* The observer is emitted immediately before the retained out-of-line
       mark_object_simple call.  In the locked AMD64 proof layout its call is
       0x08 bytes after the observer's return address.  The script correlates
       and verifies this exact address against the linked disassembly. */
    diagnostics.preMarkMutationCallSiteAddress = actualBoundaryReturn + 0x08u;
    /* MARK_PHASE_PREFETCH is active in this build: queue_mark first writes
       slot_table[slot_index] before it performs its mark-state read. */
    diagnostics.preMarkFirstMutationInstructionAddress = markHelper + 0x1Du;
    validateAllocationContextFixupObjects(true);
    diagnostics.candidateObjectValidationAtStopCount = diagnostics.objectHistoryCount;
    const bool valid =
        diagnostics.preMarkSafeStopObserved == 1u &&
        diagnostics.preMarkTrueBranchRequestCount == 1u &&
        diagnostics.preMarkTrueBranchEntryCount == 1u &&
        diagnostics.preMarkTrueBranchDuplicateCount == 0u &&
        diagnostics.membershipResult == 1u &&
        diagnostics.workstationSingleHeapSentinelValid == 1u &&
        diagnostics.condemnedCheckResult == 1u &&
        diagnostics.preMarkRootFlags == 0u &&
        diagnostics.preMarkRootFlagTestCount == 2u &&
        diagnostics.preMarkInteriorFlagResult == 0u &&
        diagnostics.preMarkPinnedFlagResult == 0u &&
        diagnostics.preMarkConservativeCheckCount == 1u &&
        diagnostics.preMarkObjectIsFree == 0u &&
        diagnostics.preMarkDebugValidationEntryCount == 1u &&
        diagnostics.preMarkDebugValidationCompletionCount == 1u &&
        diagnostics.preMarkBoundaryReached == 1u &&
        diagnostics.preMarkMarkHelperCallAttemptCount == 0u &&
        diagnostics.preMarkMarkHelperCallCount == 0u &&
        diagnostics.preMarkMarkStateReadCount == 0u &&
        diagnostics.callbackPromotionStartCount == 0u &&
        diagnostics.callbackPromotionCount == 0u &&
        diagnostics.callbackMarkingStartCount == 0u &&
        diagnostics.callbackMarkStateWriteCount == 0u &&
        diagnostics.callbackPromotionStateWriteCount == 0u &&
        diagnostics.callbackGraphTraversalCount == 0u &&
        diagnostics.callbackObjectMemoryMutationCount == 0u &&
        diagnostics.callbackGcMetadataMutationCount == 0u &&
        diagnostics.callbackSegmentMetadataMutationCount == 0u &&
        diagnostics.callbackReturnCount == 0u &&
        diagnostics.duplicateCallbackInvocationCount == 0u &&
        diagnostics.restartRequestCount == 0u &&
        diagnostics.restartEntryCount == 0u &&
        diagnostics.managedResumeCount == 0u &&
        diagnostics.callbackContextFieldReadCount == 6u &&
        diagnostics.callbackEntryThreadStoreLockHeld == 1u &&
        diagnostics.callbackEntryEeSuspended == 1u &&
        diagnostics.callbackEntryManagedEntryProhibited == 1u &&
        diagnostics.objectHistoryOverflow == 0u;
    if (!valid) {
        diagnostics.preMarkSafeStopReason = 0xE100u;
        emitFirstRootPreMarkBoundarySafeStop();
        guideXosFailFast(9u);
    }
    emitFirstRootPreMarkBoundarySafeStop();
    for (;;) {
    }
}

extern "C" void __cdecl
guideXosNativeAotFirstRootPreMarkTrueBranchEntered(
    uintptr_t object, uintptr_t heapSentinel, uint32_t flags) {
    guidexos_nativeaot_allocation_diagnostics& diagnostics =
        g_guideXosAllocationDiagnostics;
    ++diagnostics.preMarkTrueBranchRequestCount;
    ++diagnostics.preMarkTrueBranchEntryCount;
    if (diagnostics.preMarkTrueBranchEntryCount != 1u) {
        ++diagnostics.preMarkTrueBranchDuplicateCount;
    }
    diagnostics.preMarkObjectInput = object;
    diagnostics.preMarkHeapSentinel = heapSentinel;
    diagnostics.preMarkRootFlags = flags;
    diagnostics.preMarkDprintfCompiled = 0u;
    diagnostics.preMarkSourceDebugBranchCompiled = 1u;
    diagnostics.preMarkConservativeBranchCompiled = 1u;
    diagnostics.preMarkStressPinningBranchCompiled = 0u;
    if (object != diagnostics.callbackRootSlotLoadedValue ||
        object != diagnostics.membershipObjectInput ||
        object != diagnostics.heapResolutionObjectInput ||
        diagnostics.condemnedCheckResult != 1u ||
        diagnostics.workstationSingleHeapSentinelValid == 0u) {
        diagnostics.preMarkSafeStopReason = 0xE101u;
        emitFirstRootPreMarkBoundarySafeStop();
        guideXosFailFast(9u);
    }
}

extern "C" void __cdecl
guideXosNativeAotFirstRootPreMarkRootFlagTest(uint32_t flags,
                                              uint32_t bit,
                                              uint32_t result) {
    guidexos_nativeaot_allocation_diagnostics& diagnostics =
        g_guideXosAllocationDiagnostics;
    ++diagnostics.preMarkRootFlagTestCount;
    if (bit == 0x1u) {
        diagnostics.preMarkInteriorFlagResult = result;
    } else if (bit == 0x2u) {
        diagnostics.preMarkPinnedFlagResult = result;
    }
    if (flags != diagnostics.preMarkRootFlags ||
        (bit != 0x1u && bit != 0x2u) ||
        (result != 0u && (flags & bit) == 0u)) {
        diagnostics.preMarkSafeStopReason = 0xE102u;
        emitFirstRootPreMarkBoundarySafeStop();
        guideXosFailFast(9u);
    }
}

extern "C" void __cdecl
guideXosNativeAotFirstRootPreMarkConservativeCheck(uintptr_t object,
                                                   uint32_t enabled,
                                                   uint32_t isFree) {
    guidexos_nativeaot_allocation_diagnostics& diagnostics =
        g_guideXosAllocationDiagnostics;
    ++diagnostics.preMarkConservativeCheckCount;
    diagnostics.preMarkConservativeGcEnabled = enabled;
    diagnostics.preMarkObjectIsFree = isFree;
    if (enabled != 0u) {
        ++diagnostics.preMarkObjectHeaderReadCount;
        ++diagnostics.preMarkMethodTableReadCount;
        ++diagnostics.preMarkGcMetadataReadCount;
        diagnostics.preMarkFirstObjectMetadataReadAddress = object;
        ++diagnostics.callbackObjectHeaderReadCount;
        ++diagnostics.callbackMethodTableReadCount;
    }
    validateAllocationContextFixupObjects(true);
    if (object != diagnostics.preMarkObjectInput ||
        diagnostics.preMarkConservativeCheckCount != 1u) {
        diagnostics.preMarkSafeStopReason = 0xE103u;
        emitFirstRootPreMarkBoundarySafeStop();
        guideXosFailFast(9u);
    }
}

extern "C" void __cdecl
guideXosNativeAotFirstRootPreMarkDebugValidationEntered(uintptr_t object) {
    guidexos_nativeaot_allocation_diagnostics& diagnostics =
        g_guideXosAllocationDiagnostics;
    ++diagnostics.preMarkDebugValidationEntryCount;
    diagnostics.preMarkDebugValidationObject = object;
    if (object != diagnostics.preMarkObjectInput ||
        diagnostics.preMarkDebugValidationEntryCount != 1u) {
        diagnostics.preMarkSafeStopReason = 0xE104u;
        emitFirstRootPreMarkBoundarySafeStop();
        guideXosFailFast(9u);
    }
}

extern "C" void __cdecl
guideXosNativeAotFirstRootPreMarkDebugValidationMethodTableRead(
    uintptr_t object, uintptr_t methodTable) {
    guidexos_nativeaot_allocation_diagnostics& diagnostics =
        g_guideXosAllocationDiagnostics;
    ++diagnostics.preMarkObjectHeaderReadCount;
    ++diagnostics.preMarkMethodTableReadCount;
    diagnostics.preMarkFirstObjectMetadataReadAddress =
        diagnostics.preMarkFirstObjectMetadataReadAddress == 0u
            ? object
            : diagnostics.preMarkFirstObjectMetadataReadAddress;
    diagnostics.preMarkMethodTableIdentity = methodTable;
    ++diagnostics.callbackObjectHeaderReadCount;
    ++diagnostics.callbackMethodTableReadCount;
    validateAllocationContextFixupObjects(true);
    if (object != diagnostics.preMarkObjectInput || methodTable == 0u) {
        diagnostics.preMarkSafeStopReason = 0xE105u;
        emitFirstRootPreMarkBoundarySafeStop();
        guideXosFailFast(9u);
    }
}

extern "C" void __cdecl
guideXosNativeAotFirstRootPreMarkDebugValidationHeapPointer(
    uintptr_t object, uint32_t smallOnly, uint32_t result) {
    guidexos_nativeaot_allocation_diagnostics& diagnostics =
        g_guideXosAllocationDiagnostics;
    ++diagnostics.preMarkSegmentLookupCount;
    ++diagnostics.preMarkGcMetadataReadCount;
    if (smallOnly != 0u) {
        diagnostics.preMarkSmallHeapPointerResult = result;
    } else {
        diagnostics.preMarkLargeHeapPointerResult = result;
    }
    validateAllocationContextFixupObjects(true);
    if (object != diagnostics.preMarkObjectInput ||
        diagnostics.preMarkSegmentLookupCount > 2u) {
        diagnostics.preMarkSafeStopReason = 0xE106u;
        emitFirstRootPreMarkBoundarySafeStop();
        guideXosFailFast(9u);
    }
}

extern "C" void __cdecl
guideXosNativeAotFirstRootPreMarkDebugValidationCompleted(
    uintptr_t object, uint32_t noRangeChecks, uint32_t verifyHeapGc,
    uint32_t smallHeapPointer, uint32_t largeHeapPointer) {
    guidexos_nativeaot_allocation_diagnostics& diagnostics =
        g_guideXosAllocationDiagnostics;
    ++diagnostics.preMarkDebugValidationCompletionCount;
    diagnostics.preMarkDebugNoRangeChecks = noRangeChecks;
    diagnostics.preMarkDebugVerifyHeapGc = verifyHeapGc;
    diagnostics.preMarkSmallHeapPointerResult = smallHeapPointer;
    diagnostics.preMarkLargeHeapPointerResult = largeHeapPointer;
    validateAllocationContextFixupObjects(true);
    if (object != diagnostics.preMarkObjectInput ||
        diagnostics.preMarkDebugValidationCompletionCount != 1u) {
        diagnostics.preMarkSafeStopReason = 0xE107u;
        emitFirstRootPreMarkBoundarySafeStop();
        guideXosFailFast(9u);
    }
}

extern "C" void __cdecl
guideXosNativeAotFirstRootPreMarkBoundaryReached(
    uintptr_t object, uintptr_t heapSentinel, uint32_t flags,
    uintptr_t markHelper) {
    guidexos_nativeaot_allocation_diagnostics& diagnostics =
        g_guideXosAllocationDiagnostics;
    ++diagnostics.preMarkBoundaryReached;
    diagnostics.preMarkObjectInput = object;
    diagnostics.preMarkHeapSentinel = heapSentinel;
    diagnostics.preMarkRootFlags = flags;
    diagnostics.preMarkMarkHelperAddress = markHelper;
    const uintptr_t boundaryReturn = reinterpret_cast<uintptr_t>(_ReturnAddress());
#if defined(GUIDEXOS_NATIVEAOT_FIRST_ROOT_FIRST_MARK_MUTATION_ALLOCATION)
    ++diagnostics.preMarkMarkHelperCallAttemptCount;
    diagnostics.preMarkBoundaryReturnAddress = boundaryReturn;
    return;
#else
    firstRootPreMarkBoundarySafeStop(object, heapSentinel, flags, markHelper,
                                     boundaryReturn);
#endif
}

#endif

/* C011EC15: continue authentic root enumeration after the first callback. */
enum {
    kC011EC15ProviderInlineThreadStatic = 1u,
    kC011EC15ProviderOrdinaryThreadStatic = 2u,
    kC011EC15ProviderThreadStack = 3u,
};

static void emitC011EC15SafeStop() {
    const guidexos_nativeaot_allocation_diagnostics& d =
        g_guideXosAllocationDiagnostics;
    suspendEeSerialPutString(
        "[nativeaot-gc-next-genuine-root-provider] SAFE_STOP marker=C011EC15");
    suspendEeSerialPutString(" firstRootSlot=");
    suspendEeSerialPutHex64(d.c011ec15FirstRootSlot);
    suspendEeSerialPutString(" firstRootRaw=");
    suspendEeSerialPutHex64(d.c011ec15FirstRootValue);
    suspendEeSerialPutString(" firstRootProviderCategory=");
    suspendEeSerialPutHex32(d.c011ec15FirstRootProviderCategory);
    suspendEeSerialPutString(" firstRootProvider=");
    suspendEeSerialPutHex64(d.c011ec15FirstRootProvider);
    suspendEeSerialPutString(" firstRootCallback=");
    suspendEeSerialPutHex64(d.c011ec15FirstRootCallback);
    suspendEeSerialPutString(" firstRootContext=");
    suspendEeSerialPutHex64(d.c011ec15FirstRootContext);
    suspendEeSerialPutString(" nextRootSlot=");
    suspendEeSerialPutHex64(d.c011ec15NextRootSlot);
    suspendEeSerialPutString(" nextRootRaw=");
    suspendEeSerialPutHex64(d.c011ec15NextRootValue);
    suspendEeSerialPutString(" nextRootProviderCategory=");
    suspendEeSerialPutHex32(d.c011ec15ProviderContinuationCategory);
    suspendEeSerialPutString(" nextRootProvider=");
    suspendEeSerialPutHex64(d.c011ec15NextRootProvider);
    suspendEeSerialPutString(" nextRootCallback=");
    suspendEeSerialPutHex64(d.c011ec15NextRootCallback);
    suspendEeSerialPutString(" nextRootContext=");
    suspendEeSerialPutHex64(d.c011ec15NextRootContext);
    suspendEeSerialPutString(" providerRequests=");
    suspendEeSerialPutHex32(d.c011ec15ProviderRequestCount);
    suspendEeSerialPutString(" providerEntries=");
    suspendEeSerialPutHex32(d.c011ec15ProviderEntryCount);
    suspendEeSerialPutString(" gcScanRootsRequests=");
    suspendEeSerialPutHex32(d.c011ec15GcScanRootsRequestCount);
    suspendEeSerialPutString(" rootSlotsVisited=");
    suspendEeSerialPutHex32(d.c011ec15RootSlotVisitCount);
    suspendEeSerialPutString(" nullCandidates=");
    suspendEeSerialPutHex32(d.c011ec15NullCandidateCount);
    suspendEeSerialPutString(" nonNullCandidates=");
    suspendEeSerialPutHex32(d.c011ec15NonNullCandidateCount);
    suspendEeSerialPutString(" firstRootCallbackReturns=");
    suspendEeSerialPutHex32(d.c011ec15FirstRootCallbackReturnCount);
    suspendEeSerialPutString(" enumGcRefContinuations=");
    suspendEeSerialPutHex32(d.c011ec15EnumGcRefContinuationCount);
    suspendEeSerialPutString(" promoteReturns=");
    suspendEeSerialPutHex32(d.c011ec15PromoteReturnCount);
    suspendEeSerialPutString(" promoteEntries=");
    suspendEeSerialPutHex32(d.c011ec15PromoteEntryCount);
    suspendEeSerialPutString(" markHelperReturns=");
    suspendEeSerialPutHex32(d.c011ec15MarkHelperReturnCount);
    suspendEeSerialPutString(" queueMarkReturns=");
    suspendEeSerialPutHex32(d.c011ec15QueueMarkReturnCount);
    suspendEeSerialPutString(" secondPromoteAttempts=");
    suspendEeSerialPutHex32(d.c011ec15SecondPromoteAttemptCount);
    suspendEeSerialPutString(" secondPromoteEntries=");
    suspendEeSerialPutHex32(d.c011ec15SecondPromoteEntryCount);
    suspendEeSerialPutString(" secondQueueMutationAttempts=");
    suspendEeSerialPutHex32(d.c011ec15SecondQueueMutationAttemptCount);
    suspendEeSerialPutString(" secondQueueMutationExecutions=");
    suspendEeSerialPutHex32(d.c011ec15SecondQueueMutationExecutionCount);
    suspendEeSerialPutString(" firstQueueSlot=");
    suspendEeSerialPutHex64(d.c011ec15FirstQueueSlot);
    suspendEeSerialPutString(" firstQueueSlotIndex=");
    suspendEeSerialPutHex64(d.c011ec15FirstQueueSlotIndex);
    suspendEeSerialPutString(" firstQueueOld=");
    suspendEeSerialPutHex64(d.c011ec15FirstQueueOldValue);
    suspendEeSerialPutString(" firstQueueNew=");
    suspendEeSerialPutHex64(d.c011ec15FirstQueueNewValue);
    suspendEeSerialPutString(" firstQueueCursorBefore=");
    suspendEeSerialPutHex64(d.c011ec15FirstQueueCursorBefore);
    suspendEeSerialPutString(" firstQueueCursorAfter=");
    suspendEeSerialPutHex64(d.c011ec15FirstQueueCursorAfter);
    suspendEeSerialPutString(" firstQueueBase=");
    suspendEeSerialPutHex64(d.c011ec15FirstQueueBase);
    suspendEeSerialPutString(" markBitWrites=");
    suspendEeSerialPutHex32(d.c011ec15MarkBitWriteCount);
    suspendEeSerialPutString(" childReferenceReads=");
    suspendEeSerialPutHex32(d.c011ec15ChildReferenceReadCount);
    suspendEeSerialPutString(" graphTraversal=");
    suspendEeSerialPutHex32(d.c011ec15GraphTraversalCount);
    suspendEeSerialPutString(" threadStoreLockHeld=");
    suspendEeSerialPutHex32(d.c011ec15ThreadStoreLockHeld);
    suspendEeSerialPutString(" eeSuspended=");
    suspendEeSerialPutHex32(d.c011ec15EeSuspended);
    suspendEeSerialPutString(" managedEntryProhibited=");
    suspendEeSerialPutHex32(d.c011ec15ManagedEntryProhibited);
    suspendEeSerialPutString(" restart=");
    suspendEeSerialPutHex32(d.restartRequestCount + d.restartEntryCount);
    suspendEeSerialPutString(" resume=");
    suspendEeSerialPutHex32(d.managedResumeCount);
    suspendEeSerialPutString(" sentinel=");
    suspendEeSerialPutHex64(d.threadStaticProofSentinelAddress);
    suspendEeSerialPutString(" storageObject=");
    suspendEeSerialPutHex64(d.runtimeThreadStaticStorageObjectAddress);
#if defined(GUIDEXOS_NATIVEAOT_C011EC18_NATIVE_RHP_NEW_ARRAY)
    suspendEeSerialPutString(" c18Frame=");
    suspendEeSerialPutHex64(d.c011ec18TransitionFrameAddress);
    suspendEeSerialPutString(" c18CurrentNativeRIP=");
    suspendEeSerialPutHex64(d.c011ec18CurrentNativeRip);
    suspendEeSerialPutString(" c18CurrentNativeReturnSlot=");
    suspendEeSerialPutHex64(d.c011ec18CurrentNativeRsp);
    suspendEeSerialPutString(" c18TransitionRIP=");
    suspendEeSerialPutHex64(d.c011ec18TransitionFrameRip);
    suspendEeSerialPutString(" c18TransitionRBP=");
    suspendEeSerialPutHex64(d.c011ec18TransitionFrameRbp);
    suspendEeSerialPutString(" c18SavedRSP=");
    suspendEeSerialPutHex64(d.c011ec18SavedRsp);
    suspendEeSerialPutString(" c18TransitionInRange=");
    suspendEeSerialPutHex32(d.c011ec18TransitionInManagedRange);
    suspendEeSerialPutString(" c18IteratorControlPC=");
    suspendEeSerialPutHex64(d.c011ec18IteratorControlPc);
    suspendEeSerialPutString(" c18IteratorSP=");
    suspendEeSerialPutHex64(d.c011ec18IteratorInitialSp);
    suspendEeSerialPutString(" c18IteratorFP=");
    suspendEeSerialPutHex64(d.c011ec18IteratorInitialFp);
    suspendEeSerialPutString(" c18Manager=");
    suspendEeSerialPutHex64(d.c011ec18IteratorCodeManager);
    suspendEeSerialPutString(" c18AuthenticManager=");
    suspendEeSerialPutHex64(d.c011ec18AuthenticManagedCodeManager);
    suspendEeSerialPutString(" c18MethodInfo=");
    suspendEeSerialPutHex64(d.c011ec18IteratorMethodInfo);
    suspendEeSerialPutString(" c18FindMethodInfoAttempts=");
    suspendEeSerialPutHex32(d.c011ec18FindMethodInfoAttemptCount);
    suspendEeSerialPutString(" c18FindMethodInfoSuccess=");
    suspendEeSerialPutHex32(d.c011ec18FindMethodInfoSuccessCount);
    suspendEeSerialPutString(" c18MetadataValid=");
    suspendEeSerialPutHex32(d.c011ec18MethodMetadataValid);
    suspendEeSerialPutString(" c18FramePointer=");
    suspendEeSerialPutHex64(d.c011ec18IteratorFramePointer);
    suspendEeSerialPutString(" c18FramePointerCalculations=");
    suspendEeSerialPutHex32(d.c011ec18FramePointerCalculationCount);
    suspendEeSerialPutString(" c18UnwindSteps=");
    suspendEeSerialPutHex32(d.c011ec18UnwindStepCount);
    suspendEeSerialPutString(" c18StackFrames=");
    suspendEeSerialPutHex32(d.c011ec18StackFrameCount);
    suspendEeSerialPutString(" c18StackProviderCallbacks=");
    suspendEeSerialPutHex32(d.c011ec18StackProviderCallbackCount);
    suspendEeSerialPutString(" c18StackRootSlots=");
    suspendEeSerialPutHex32(d.c011ec18StackRootSlotCount);
    suspendEeSerialPutString(" c18StackBoundsConsumed=");
    suspendEeSerialPutHex32(d.c011ec18StackBoundsConsumed);
#endif
    suspendEeSerialPutString(" marker=C011EC15\n");
}

[[noreturn]] static void c011ec15SafeStop(uint32_t reason) {
    guidexos_nativeaot_allocation_diagnostics& d =
        g_guideXosAllocationDiagnostics;
    d.c011ec15StopObserved = 1u;
    d.c011ec15StopReason = reason;
    d.c011ec15StopAddress = reinterpret_cast<uintptr_t>(&c011ec15SafeStop);
    d.safeStopObserved = 1u;
    d.stopReason = GUIDEXOS_NATIVEAOT_NEXT_GENUINE_ROOT_PROVIDER_SAFE_STOP_MARKER;
    d.stage = GUIDEXOS_NATIVEAOT_ALLOC_STAGE_F33_NEXT_GENUINE_ROOT_PROVIDER_SAFE_STOP;
    d.rootBoundaryFunction = d.c011ec15StopAddress;
    validateAllocationContextFixupObjects(true);
    emitC011EC15SafeStop();
    for (;;) {
    }
}

#if defined(GUIDEXOS_NATIVEAOT_C011EC17_CODE_MANAGER) || defined(GUIDEXOS_NATIVEAOT_C011EC18_NATIVE_RHP_NEW_ARRAY)
static void emitC011EC17StackWalkPreflight() {
    RuntimeInstance* runtime = GetRuntimeInstance();
    Thread* thread = ThreadStore::GetCurrentThreadIfAvailable();
    uintptr_t controlPc = 0u;
    if (thread != nullptr) {
        RuntimeThreadLocals* locals = reinterpret_cast<RuntimeThreadLocals*>(thread);
        // Thread::GetTransitionFrame selects the deferred frame for the
        // suspending cooperative thread and the cached frame otherwise.  The
        // live m_pTransitionFrame is intentionally null in this state.
        PInvokeTransitionFrame* transitionFrame =
            ThreadStore::GetSuspendingThread() == thread
                ? locals->m_pDeferredTransitionFrame
                : locals->m_pCachedTransitionFrame;
        const uintptr_t transitionAddress =
            reinterpret_cast<uintptr_t>(transitionFrame);
        if (transitionAddress > 0x1000u && transitionFrame != nullptr) {
            controlPc = reinterpret_cast<uintptr_t>(transitionFrame->m_RIP);
        }
    }

    void* managedCodeStart = nullptr;
    uint32_t managedCodeSize = 0u;
    const bool managedRangeValid = getNativeAotRange(
        reinterpret_cast<void*>(&__managedcode_a),
        reinterpret_cast<void*>(&__managedcode_z),
        &managedCodeStart,
        &managedCodeSize);
    const uintptr_t managedCodeEnd = managedRangeValid
        ? reinterpret_cast<uintptr_t>(managedCodeStart) + managedCodeSize
        : 0u;
    const bool isManaged = runtime != nullptr && controlPc != 0u &&
        runtime->IsManaged(reinterpret_cast<void*>(controlPc));
    ICodeManager* codeManager = runtime != nullptr && controlPc != 0u
        ? runtime->GetCodeManagerForAddress(reinterpret_cast<void*>(controlPc))
        : nullptr;

    suspendEeSerialPutString("[nativeaot-code-manager] preflight runtime=");
    suspendEeSerialPutHex64(reinterpret_cast<gx_uintptr>(runtime));
    suspendEeSerialPutString(" manager=");
    suspendEeSerialPutHex64(reinterpret_cast<gx_uintptr>(codeManager));
    suspendEeSerialPutString(" managedStart=");
    suspendEeSerialPutHex64(reinterpret_cast<gx_uintptr>(managedCodeStart));
    suspendEeSerialPutString(" managedSize=");
    suspendEeSerialPutHex64(static_cast<gx_uintptr>(managedCodeSize));
    suspendEeSerialPutString(" managedEnd=");
    suspendEeSerialPutHex64(static_cast<gx_uintptr>(managedCodeEnd));
    suspendEeSerialPutString(" controlPC=");
    suspendEeSerialPutHex64(static_cast<gx_uintptr>(controlPc));
    suspendEeSerialPutString(" isManaged=");
    suspendEeSerialPutHex32(isManaged ? 1u : 0u);
    suspendEeSerialPutString(" lookup=");
    suspendEeSerialPutHex64(reinterpret_cast<gx_uintptr>(codeManager));
    suspendEeSerialPutString(" registration=");
    suspendEeSerialPutHex32(g_guideXosNativeAotCodeManagerRegistered ? 1u : 0u);
    suspendEeSerialPutString(" marker=C011EC17-PREFLIGHT\n");
}
#endif

#if defined(GUIDEXOS_NATIVEAOT_C011EC18_NATIVE_RHP_NEW_ARRAY)
static bool c011ec18IsInsideManagedRange(
    uintptr_t address, RuntimeInstance* runtime, ICodeManager** codeManager) {
    void* managedCodeStart = nullptr;
    uint32_t managedCodeSize = 0u;
    const bool rangeValid = getNativeAotRange(
        reinterpret_cast<void*>(&__managedcode_a),
        reinterpret_cast<void*>(&__managedcode_z),
        &managedCodeStart,
        &managedCodeSize);
    const uintptr_t managedStart = reinterpret_cast<uintptr_t>(managedCodeStart);
    const uintptr_t managedEnd = managedStart + managedCodeSize;
    const bool inside = rangeValid && address >= managedStart && address < managedEnd;
    if (codeManager != nullptr) {
        *codeManager = runtime != nullptr && address != 0u
            ? runtime->GetCodeManagerForAddress(reinterpret_cast<void*>(address))
            : nullptr;
    }
    return inside;
}

#if defined(GUIDEXOS_NATIVEAOT_C011EC23_NATIVE_UNWIND)
/*
 * This is the narrow PAL bridge for the locked iterator's REGDISPLAY.  It
 * mirrors only the AMD64 prefix used by the locked regdisplay.h and keeps the
 * native provider independent from NativeAOT's managed ICodeManager ABI.
 */
struct GuideXosNativeAmd64RegDisplay {
    uintptr_t pRax;
    uintptr_t pRcx;
    uintptr_t pRdx;
    uintptr_t pRbx;
    uintptr_t pRbp;
    uintptr_t pRsi;
    uintptr_t pRdi;
    uintptr_t pR8;
    uintptr_t pR9;
    uintptr_t pR10;
    uintptr_t pR11;
    uintptr_t pR12;
    uintptr_t pR13;
    uintptr_t pR14;
    uintptr_t pR15;
    uintptr_t SP;
    uintptr_t IP;
};

constexpr size_t kC011EC23ContextRbx = 0x90u;
constexpr size_t kC011EC23ContextRsp = 0x98u;
constexpr size_t kC011EC23ContextRbp = 0xA0u;
constexpr size_t kC011EC23ContextRsi = 0xA8u;
constexpr size_t kC011EC23ContextRdi = 0xB0u;
constexpr size_t kC011EC23ContextR12 = 0xD8u;
constexpr size_t kC011EC23ContextR13 = 0xE0u;
constexpr size_t kC011EC23ContextR14 = 0xE8u;
constexpr size_t kC011EC23ContextR15 = 0xF0u;
constexpr size_t kC011EC23ContextRip = 0xF8u;
constexpr uint32_t kC011EC23MaximumNativeFrames = 8u;

#if defined(GUIDEXOS_NATIVEAOT_C011EC24_CALLER_PROVENANCE)
struct GuideXosC011Ec24UnwindProgram {
    uint32_t codeCount;
    uint32_t stackAdvance;
    uint32_t supported;
    uint32_t chained;
};

struct GuideXosC011Ec24PreflightEvidence {
    uint32_t proven;
    uint32_t outputAgreement;
    uint32_t callerValid;
    uint32_t callerKernelRange;
    uint32_t callerManagedRange;
    uint32_t standaloneTests;
    uint32_t helperStandalonePassed;
    uint32_t secondStandalonePassed;
    uint32_t opcodeCount;
    uint32_t stackAdvance;
    uintptr_t liveRsp;
    uintptr_t returnSlot;
    uintptr_t returnValue;
    uintptr_t expectedCallerRip;
    uintptr_t expectedCallerRsp;
    uintptr_t outputRip;
    uintptr_t outputRsp;
    uint16_t opcodeWords[12];
};

GuideXosC011Ec24PreflightEvidence g_c011ec24PreflightEvidence = {};

#if defined(GUIDEXOS_NATIVEAOT_C011EC25_KERNEL_ENTRY_BOUNDARY)
struct GuideXosC011Ec25UnwindProgram {
    uint32_t codeCount;
    uint32_t stackAdvance;
    uint32_t supported;
    uint32_t chained;
    uint32_t allocationBytes;
    uint32_t pushCount;
    uint16_t opcodeWords[12];
};
#endif

uint16_t c011ec24ReadWord(const uint8_t* bytes, uint32_t slot) {
    return static_cast<uint16_t>(bytes[slot * 2u]) |
           static_cast<uint16_t>(bytes[slot * 2u + 1u]) << 8;
}

bool c011ec24DecodeProgram(
    const guidexos_nativeaot_native_unwind_lookup_result& lookup,
    guidexos_nativeaot_allocation_diagnostics& diagnostics,
    GuideXosC011Ec24UnwindProgram* program) {
    if (program == nullptr || lookup.unwind_info == 0u) return false;
    const uint8_t* info = reinterpret_cast<const uint8_t*>(
        static_cast<uintptr_t>(lookup.unwind_info));
    if ((info[0] & 0x07u) != 1u) return false;
    program->codeCount = info[2];
    program->stackAdvance = 0u;
    program->supported = 1u;
    program->chained = (info[0] >> 3) != 0u ? 1u : 0u;
    if (program->codeCount > 12u || program->chained != 0u) return false;
    const uint8_t* codes = info + 4u;
    for (uint32_t index = 0u; index < program->codeCount; ++index) {
        diagnostics.c011ec24OpcodeWords[index] = c011ec24ReadWord(codes, index);
    }
    for (uint32_t index = 0u; index < program->codeCount;) {
        const uint16_t word = c011ec24ReadWord(codes, index);
        const uint32_t unwindOp = (word >> 8) & 0x0Fu;
        const uint32_t opInfo = word >> 12;
        uint32_t slots = 1u;
        switch (unwindOp) {
            case 0u: // UWOP_PUSH_NONVOL
                program->stackAdvance += 8u;
                break;
            case 1u: // UWOP_ALLOC_LARGE
                if (opInfo == 0u) {
                    if (index + 1u >= program->codeCount) return false;
                    program->stackAdvance += static_cast<uint32_t>(
                        c011ec24ReadWord(codes, index + 1u)) * 8u;
                    slots = 2u;
                } else if (opInfo == 1u) {
                    if (index + 2u >= program->codeCount) return false;
                    program->stackAdvance += static_cast<uint32_t>(
                        c011ec24ReadWord(codes, index + 1u)) |
                        static_cast<uint32_t>(c011ec24ReadWord(
                            codes, index + 2u)) << 16;
                    slots = 3u;
                } else {
                    return false;
                }
                break;
            case 2u: // UWOP_ALLOC_SMALL
                program->stackAdvance += opInfo * 8u + 8u;
                break;
            case 3u: // UWOP_SET_FPREG
                break;
            case 4u: // UWOP_SAVE_NONVOL
                if (index + 1u >= program->codeCount) return false;
                slots = 2u;
                break;
            case 5u: // UWOP_SAVE_NONVOL_FAR
                if (index + 2u >= program->codeCount) return false;
                slots = 3u;
                break;
            case 8u: // UWOP_SAVE_XMM128
                if (index + 1u >= program->codeCount) return false;
                slots = 2u;
                break;
            case 9u: // UWOP_SAVE_XMM128_FAR
                if (index + 2u >= program->codeCount) return false;
                slots = 3u;
                break;
            case 10u: // UWOP_PUSH_MACHFRAME
                break;
            default:
                program->supported = 0u;
                return false;
        }
        index += slots;
    }
    if (program->stackAdvance > 0x100000u) return false;
    diagnostics.c011ec24UnwindOpcodeCount = program->codeCount;
    diagnostics.c011ec24StackAdvance = program->stackAdvance;
    return true;
}

bool c011ec24CallerOwnership(
    uintptr_t callerRip,
    const guidexos_nativeaot_native_unwind_lookup_result& lookup,
    uint32_t* kernelRange, uint32_t* managedRange,
    guidexos_nativeaot_native_unwind_lookup_result* callerLookup) {
    guidexos_nativeaot_native_unwind_lookup_result resolved = {};
    const bool providerHit =
        guidexos_nativeaot_gc_native_unwind_lookup(callerRip, &resolved) == 0;
    if (callerLookup != nullptr) *callerLookup = resolved;
    const bool inKernel = providerHit || (callerRip >= lookup.executable_start &&
        callerRip < lookup.executable_end);
    if (kernelRange != nullptr) *kernelRange = inKernel ? 1u : 0u;
    RuntimeInstance* runtime = GetRuntimeInstance();
    ICodeManager* manager = nullptr;
    const bool inManaged = c011ec18IsInsideManagedRange(
        callerRip, runtime, &manager);
    if (managedRange != nullptr) *managedRange = inManaged ? 1u : 0u;
    return inKernel || inManaged;
}
#endif

alignas(16) uint64_t g_c011ec23StandaloneStack[4096] = {};

struct GuideXosC011Ec23StandaloneEvidence {
    uint32_t attempted;
    uint32_t succeeded;
    uint32_t result;
    uint32_t index;
    uintptr_t runtimeFunction;
    uintptr_t unwindInfo;
    uintptr_t outputRip;
    uintptr_t outputRsp;
};

GuideXosC011Ec23StandaloneEvidence g_c011ec23StandaloneEvidence = {};

uint16_t c011ec23UnwindWord(const uint8_t* bytes, uint32_t slot) {
    return static_cast<uint16_t>(bytes[slot * 2u]) |
           static_cast<uint16_t>(bytes[slot * 2u + 1u]) << 8;
}

bool c011ec23StandaloneSafeShape(const uint8_t* unwindInfo,
                                 uint32_t* maximumOffset) {
    if (unwindInfo == nullptr || maximumOffset == nullptr ||
        (unwindInfo[0] & 0x07u) != 1u || (unwindInfo[0] >> 3) != 0u) {
        return false;
    }
    const uint32_t codeCount = unwindInfo[2];
    uint32_t slotsUsed = 0u;
    uint32_t stackAdvance = 0u;
    uint32_t largestAccess = 0u;
    while (slotsUsed < codeCount) {
        const uint16_t word = c011ec23UnwindWord(unwindInfo + 4u, slotsUsed);
        const uint32_t unwindOp = (word >> 8) & 0x0Fu;
        const uint32_t opInfo = word >> 12;
        uint32_t slots = 1u;
        switch (unwindOp) {
            case 0u: // UWOP_PUSH_NONVOL
                stackAdvance += 8u;
                break;
            case 1u: // UWOP_ALLOC_LARGE
                if (opInfo != 0u || slotsUsed + 1u >= codeCount) return false;
                stackAdvance += static_cast<uint32_t>(
                    c011ec23UnwindWord(unwindInfo + 4u, slotsUsed + 1u)) * 8u;
                slots = 2u;
                break;
            case 2u: // UWOP_ALLOC_SMALL
                stackAdvance += opInfo * 8u + 8u;
                break;
            case 4u: // UWOP_SAVE_NONVOL
                if (slotsUsed + 1u >= codeCount) return false;
                largestAccess = stackAdvance + static_cast<uint32_t>(
                    c011ec23UnwindWord(unwindInfo + 4u, slotsUsed + 1u)) * 8u;
                slots = 2u;
                break;
            default:
                // The small standalone harness intentionally does not invent
                // XMM, frame-register, far-save, or machine-frame state.
                return false;
        }
        if (stackAdvance > 0x5000u || largestAccess > 0x5000u) return false;
        slotsUsed += slots;
    }
    if (slotsUsed != codeCount) return false;
    *maximumOffset = stackAdvance > largestAccess ? stackAdvance : largestAccess;
    return *maximumOffset < 0x5000u;
}

bool c011ec23RunStandaloneSecondUnwind(
    const guidexos_nativeaot_native_unwind_lookup_result& seed,
    guidexos_nativeaot_allocation_diagnostics& diagnostics) {
    const uintptr_t pdataStart = static_cast<uintptr_t>(seed.pdata_start);
    const uintptr_t pdataEnd = static_cast<uintptr_t>(seed.pdata_end);
    if (pdataStart == 0u || pdataEnd <= pdataStart ||
        (pdataEnd - pdataStart) % sizeof(guidexos_nativeaot_runtime_function) != 0u) {
        return false;
    }
    const uint32_t count = static_cast<uint32_t>(
        (pdataEnd - pdataStart) / sizeof(guidexos_nativeaot_runtime_function));
    const auto* table = reinterpret_cast<const guidexos_nativeaot_runtime_function*>(
        pdataStart);
    for (uint32_t index = 1u; index < count; ++index) {
        if (index == seed.table_index) continue;
        const auto& entry = table[index];
        const uintptr_t infoAddress = static_cast<uintptr_t>(seed.module_base) +
            entry.unwind_data;
        const uint8_t* info = reinterpret_cast<const uint8_t*>(infoAddress);
        if (info[2] < 2u) continue;
        uint32_t maximumOffset = 0u;
        if (!c011ec23StandaloneSafeShape(info, &maximumOffset)) continue;
        const uintptr_t functionStart = static_cast<uintptr_t>(seed.module_base) +
            entry.begin_address;
        if (entry.begin_address >= entry.end_address ||
            entry.end_address - entry.begin_address < 2u) continue;

        for (uint32_t slot = 0u; slot < 4096u; ++slot) {
            g_c011ec23StandaloneStack[slot] =
                UINT64_C(0xC0235EC000000000) + slot;
        }
        uint8_t context[0x100u] = {};
        uint64_t* contextPointers[10] = {};
        const uintptr_t initialRsp = reinterpret_cast<uintptr_t>(
            &g_c011ec23StandaloneStack[1024]);
        *reinterpret_cast<uint64_t*>(context + kC011EC23ContextRsp) = initialRsp;
        *reinterpret_cast<uint64_t*>(context + kC011EC23ContextRbp) = initialRsp;
        *reinterpret_cast<uint64_t*>(context + kC011EC23ContextRip) =
            functionStart + (info[1] + 1u < entry.end_address - entry.begin_address
                ? info[1] + 1u : 1u);
        void* handlerData = nullptr;
        uint64_t establisherFrame = 0u;
        guideXosRtlVirtualUnwind(
            0u, seed.module_base,
            *reinterpret_cast<uint64_t*>(context + kC011EC23ContextRip),
            const_cast<guidexos_nativeaot_runtime_function*>(&entry), context,
            &handlerData, &establisherFrame, contextPointers);
        const uintptr_t outputRip = static_cast<uintptr_t>(
            *reinterpret_cast<uint64_t*>(context + kC011EC23ContextRip));
        const uintptr_t outputRsp = static_cast<uintptr_t>(
            *reinterpret_cast<uint64_t*>(context + kC011EC23ContextRsp));
        diagnostics.c011ec23SecondFunctionAttempted = 1u;
        diagnostics.c011ec23SecondFunctionIndex = index;
        diagnostics.c011ec23SecondRuntimeFunction =
            reinterpret_cast<uintptr_t>(&entry);
        diagnostics.c011ec23SecondUnwindInfo = infoAddress;
        diagnostics.c011ec23SecondOutputRip = outputRip;
        diagnostics.c011ec23SecondOutputRsp = outputRsp;
        diagnostics.c011ec23SecondFunctionResult =
            outputRip != 0u && outputRsp > initialRsp &&
            (outputRsp & (sizeof(uintptr_t) - 1u)) == 0u ? 1u : 2u;
        diagnostics.c011ec23SecondFunctionSucceeded =
            diagnostics.c011ec23SecondFunctionResult == 1u ? 1u : 0u;
        g_c011ec23StandaloneEvidence.attempted = 1u;
        g_c011ec23StandaloneEvidence.succeeded =
            diagnostics.c011ec23SecondFunctionSucceeded;
        g_c011ec23StandaloneEvidence.result =
            diagnostics.c011ec23SecondFunctionResult;
        g_c011ec23StandaloneEvidence.index = index;
        g_c011ec23StandaloneEvidence.runtimeFunction =
            diagnostics.c011ec23SecondRuntimeFunction;
        g_c011ec23StandaloneEvidence.unwindInfo =
            diagnostics.c011ec23SecondUnwindInfo;
        g_c011ec23StandaloneEvidence.outputRip =
            diagnostics.c011ec23SecondOutputRip;
        g_c011ec23StandaloneEvidence.outputRsp =
            diagnostics.c011ec23SecondOutputRsp;
        if (diagnostics.c011ec23SecondFunctionSucceeded != 0u) return true;
    }
    return false;
}

#if defined(GUIDEXOS_NATIVEAOT_C011EC24_CALLER_PROVENANCE)
uint32_t c011ec24ContextOffset(uint32_t reg) {
    switch (reg) {
        case 3u: return static_cast<uint32_t>(kC011EC23ContextRbx);
        case 5u: return static_cast<uint32_t>(kC011EC23ContextRbp);
        case 6u: return static_cast<uint32_t>(kC011EC23ContextRsi);
        case 7u: return static_cast<uint32_t>(kC011EC23ContextRdi);
        case 12u: return static_cast<uint32_t>(kC011EC23ContextR12);
        case 13u: return static_cast<uint32_t>(kC011EC23ContextR13);
        case 14u: return static_cast<uint32_t>(kC011EC23ContextR14);
        case 15u: return static_cast<uint32_t>(kC011EC23ContextR15);
        default: return 0xFFFFFFFFu;
    }
}

uint32_t c011ec24ContextPointerIndex(uint32_t reg) {
    switch (reg) {
        case 3u: return 0u;
        case 5u: return 2u;
        case 6u: return 3u;
        case 7u: return 4u;
        case 12u: return 5u;
        case 13u: return 6u;
        case 14u: return 7u;
        case 15u: return 8u;
        default: return 0xFFFFFFFFu;
    }
}

uint64_t c011ec24SyntheticValue(uint32_t reg) {
    return UINT64_C(0xC024000000000000) + reg;
}

uint64_t c011ec24ContextValue(const uint8_t* context, size_t offset) {
    return context == nullptr ? 0u : *reinterpret_cast<const uint64_t*>(
        context + offset);
}

bool c011ec24RunSynthetic(
    const guidexos_nativeaot_native_unwind_lookup_result& lookup,
    guidexos_nativeaot_allocation_diagnostics& diagnostics,
    uintptr_t expectedCaller) {
    GuideXosC011Ec24UnwindProgram program = {};
    if (!c011ec24DecodeProgram(lookup, diagnostics, &program) ||
        program.chained != 0u) return false;

    for (uint32_t index = 0u; index < 4096u; ++index) {
        g_c011ec23StandaloneStack[index] = 0u;
    }
    uintptr_t unwindRsp = reinterpret_cast<uintptr_t>(
        &g_c011ec23StandaloneStack[1024]);
    uintptr_t cursor = unwindRsp;
    uintptr_t expectedSource[16] = {};
    uint64_t expectedValue[16] = {};
    const uint8_t* info = reinterpret_cast<const uint8_t*>(
        static_cast<uintptr_t>(lookup.unwind_info));
    const uint8_t* codes = info + 4u;
    for (uint32_t index = 0u; index < program.codeCount;) {
        const uint16_t word = c011ec24ReadWord(codes, index);
        const uint32_t unwindOp = (word >> 8) & 0x0Fu;
        const uint32_t opInfo = word >> 12;
        uint32_t slots = 1u;
        switch (unwindOp) {
            case 0u: { // UWOP_PUSH_NONVOL
                const uint32_t contextOffset = c011ec24ContextOffset(opInfo);
                if (contextOffset == 0xFFFFFFFFu) return false;
                const uint64_t value = c011ec24SyntheticValue(opInfo);
                *reinterpret_cast<uint64_t*>(cursor) = value;
                expectedSource[opInfo] = cursor;
                expectedValue[opInfo] = value;
                cursor += 8u;
                break;
            }
            case 1u: // UWOP_ALLOC_LARGE
                if (opInfo == 0u) {
                    if (index + 1u >= program.codeCount) return false;
                    cursor += static_cast<uintptr_t>(
                        c011ec24ReadWord(codes, index + 1u)) * 8u;
                    slots = 2u;
                } else if (opInfo == 1u) {
                    if (index + 2u >= program.codeCount) return false;
                    cursor += static_cast<uintptr_t>(
                        c011ec24ReadWord(codes, index + 1u)) |
                        static_cast<uintptr_t>(c011ec24ReadWord(
                            codes, index + 2u)) << 16;
                    slots = 3u;
                } else {
                    return false;
                }
                break;
            case 2u: // UWOP_ALLOC_SMALL
                cursor += static_cast<uintptr_t>(opInfo) * 8u + 8u;
                break;
            case 4u: { // UWOP_SAVE_NONVOL
                if (index + 1u >= program.codeCount) return false;
                const uint32_t contextOffset = c011ec24ContextOffset(opInfo);
                if (contextOffset == 0xFFFFFFFFu) return false;
                const uintptr_t location = cursor + static_cast<uintptr_t>(
                    c011ec24ReadWord(codes, index + 1u)) * 8u;
                const uint64_t value = c011ec24SyntheticValue(opInfo);
                *reinterpret_cast<uint64_t*>(location) = value;
                expectedSource[opInfo] = location;
                expectedValue[opInfo] = value;
                slots = 2u;
                break;
            }
            case 5u: { // UWOP_SAVE_NONVOL_FAR
                if (index + 2u >= program.codeCount) return false;
                const uint32_t contextOffset = c011ec24ContextOffset(opInfo);
                if (contextOffset == 0xFFFFFFFFu) return false;
                const uintptr_t offset = static_cast<uintptr_t>(
                    c011ec24ReadWord(codes, index + 1u)) |
                    static_cast<uintptr_t>(c011ec24ReadWord(
                        codes, index + 2u)) << 16;
                const uintptr_t location = cursor + offset;
                const uint64_t value = c011ec24SyntheticValue(opInfo);
                *reinterpret_cast<uint64_t*>(location) = value;
                expectedSource[opInfo] = location;
                expectedValue[opInfo] = value;
                slots = 3u;
                break;
            }
            case 8u:
                if (index + 1u >= program.codeCount) return false;
                slots = 2u;
                break;
            case 9u:
                if (index + 2u >= program.codeCount) return false;
                slots = 3u;
                break;
            default:
                return false;
        }
        index += slots;
    }
    const uintptr_t returnSlot = cursor;
    *reinterpret_cast<uint64_t*>(returnSlot) = expectedCaller;
    const uintptr_t expectedRsp = returnSlot + 8u;
    uint8_t context[0x100u] = {};
    uint64_t* contextPointers[10] = {};
    const uint32_t registers[] = {3u, 5u, 6u, 7u, 12u, 13u, 14u, 15u};
    for (uint32_t reg : registers) {
        const uint32_t contextOffset = c011ec24ContextOffset(reg);
        const uint32_t pointerIndex = c011ec24ContextPointerIndex(reg);
        *reinterpret_cast<uint64_t*>(context + contextOffset) =
            UINT64_C(0xC024100000000000) + reg;
        contextPointers[pointerIndex] = reinterpret_cast<uint64_t*>(
            context + contextOffset);
    }
    *reinterpret_cast<uint64_t*>(context + kC011EC23ContextRsp) = unwindRsp;
    *reinterpret_cast<uint64_t*>(context + kC011EC23ContextRip) =
        static_cast<uintptr_t>(lookup.module_base) + lookup.begin_address +
        static_cast<uintptr_t>(lookup.prologue_size) + 1u;
    void* handlerData = nullptr;
    uint64_t establisherFrame = 0u;
    guideXosRtlVirtualUnwind(
        0u, lookup.module_base,
        *reinterpret_cast<uint64_t*>(context + kC011EC23ContextRip),
        reinterpret_cast<void*>(lookup.runtime_function), context,
        &handlerData, &establisherFrame, contextPointers);
    const uintptr_t outputRip = static_cast<uintptr_t>(
        *reinterpret_cast<uint64_t*>(context + kC011EC23ContextRip));
    const uintptr_t outputRsp = static_cast<uintptr_t>(
        *reinterpret_cast<uint64_t*>(context + kC011EC23ContextRsp));
    if (outputRip != expectedCaller || outputRsp != expectedRsp) return false;
    for (uint32_t reg : registers) {
        const uint32_t contextOffset = c011ec24ContextOffset(reg);
        const uint32_t pointerIndex = c011ec24ContextPointerIndex(reg);
        if (c011ec24ContextValue(context, contextOffset) != expectedValue[reg] ||
            reinterpret_cast<uintptr_t>(contextPointers[pointerIndex]) !=
                expectedSource[reg]) {
            return false;
        }
    }
    return true;
}

void emitC011EC24Preflight() {
    const guidexos_nativeaot_allocation_diagnostics& d =
        g_guideXosAllocationDiagnostics;
    suspendEeSerialPutString(
        "[nativeaot-gc-native-caller-provenance] preflight marker=C011EC24-PREFLIGHT");
#define C24PF32(name, value) \
    suspendEeSerialPutString(" " name "="); suspendEeSerialPutHex32(value)
#define C24PF64(name, value) \
    suspendEeSerialPutString(" " name "="); suspendEeSerialPutHex64(value)
    C24PF32("standaloneTests", d.c011ec24StandaloneTests);
    C24PF32("helperStandalonePassed", d.c011ec24HelperStandalonePassed);
    C24PF32("secondStandalonePassed", d.c011ec24SecondStandalonePassed);
    C24PF32("unwindOpcodeCount", d.c011ec24UnwindOpcodeCount);
    C24PF32("stackAdvance", d.c011ec24StackAdvance);
    C24PF64("liveRSP", d.c011ec24LiveRsp);
    C24PF64("returnSlot", d.c011ec24PreflightReturnSlot);
    C24PF64("returnValue", d.c011ec24PreflightReturnValue);
    C24PF64("expectedCallerRIP", d.c011ec24ExpectedCallerRip);
    C24PF64("expectedCallerRSP", d.c011ec24ExpectedCallerRsp);
    C24PF64("outputRIP", d.c011ec24PreflightOutputRip);
    C24PF64("outputRSP", d.c011ec24PreflightOutputRsp);
    C24PF32("outputAgreement", d.c011ec24OutputAgreement);
    C24PF32("callerValid", d.c011ec24CallerValid);
    C24PF32("callerKernelRange", d.c011ec24CallerKernelRange);
    C24PF32("callerManagedRange", d.c011ec24CallerManagedRange);
    for (uint32_t index = 0u; index < d.c011ec24UnwindOpcodeCount; ++index) {
        suspendEeSerialPutString(" opcodeWord");
        suspendEeSerialPutHex32(index);
        suspendEeSerialPutString("=");
        suspendEeSerialPutHex32(d.c011ec24OpcodeWords[index]);
    }
#undef C24PF32
#undef C24PF64
    suspendEeSerialPutString("\n");
}

extern "C" uint32_t __cdecl
guideXosNativeAotC011EC24PreflightRealFrame(uintptr_t helperPc,
                                             uintptr_t liveRsp) {
    guidexos_nativeaot_allocation_diagnostics& diagnostics =
        g_guideXosAllocationDiagnostics;
    guidexos_nativeaot_native_unwind_lookup_result lookup = {};
    if (helperPc == 0u || liveRsp == 0u ||
        guidexos_nativeaot_gc_native_unwind_lookup(helperPc, &lookup) != 0) {
        return 0u;
    }
    GuideXosC011Ec24UnwindProgram program = {};
    if (!c011ec24DecodeProgram(lookup, diagnostics, &program) ||
        program.chained != 0u) return 0u;
    diagnostics.c011ec24LiveRsp = liveRsp;
    diagnostics.c011ec24PreflightReturnSlot = liveRsp + program.stackAdvance;
    diagnostics.c011ec24PreflightReturnValue = *reinterpret_cast<const uintptr_t*>(
        diagnostics.c011ec24PreflightReturnSlot);
    diagnostics.c011ec24PreflightOutputRip = 0u;
    diagnostics.c011ec24PreflightOutputRsp = 0u;
    uint8_t context[0x100u] = {};
    *reinterpret_cast<uint64_t*>(context + kC011EC23ContextRsp) = liveRsp;
    *reinterpret_cast<uint64_t*>(context + kC011EC23ContextRip) = helperPc +
        static_cast<uintptr_t>(lookup.prologue_size) + 1u;
    void* handlerData = nullptr;
    uint64_t establisherFrame = 0u;
    guideXosRtlVirtualUnwind(
        0u, lookup.module_base,
        *reinterpret_cast<uint64_t*>(context + kC011EC23ContextRip),
        reinterpret_cast<void*>(lookup.runtime_function), context,
        &handlerData, &establisherFrame, nullptr);
    diagnostics.c011ec24PreflightOutputRip = static_cast<uintptr_t>(
        *reinterpret_cast<uint64_t*>(context + kC011EC23ContextRip));
    diagnostics.c011ec24PreflightOutputRsp = static_cast<uintptr_t>(
        *reinterpret_cast<uint64_t*>(context + kC011EC23ContextRsp));
    diagnostics.c011ec24ExpectedCallerRip = diagnostics.c011ec24PreflightReturnValue;
    diagnostics.c011ec24ExpectedCallerRsp =
        diagnostics.c011ec24PreflightReturnSlot + 8u;
    diagnostics.c011ec24CallerValid = c011ec24CallerOwnership(
        diagnostics.c011ec24ExpectedCallerRip, lookup,
        &diagnostics.c011ec24CallerKernelRange,
        &diagnostics.c011ec24CallerManagedRange, nullptr) ? 1u : 0u;
    diagnostics.c011ec24OutputAgreement =
        diagnostics.c011ec24PreflightOutputRip == diagnostics.c011ec24ExpectedCallerRip &&
        diagnostics.c011ec24PreflightOutputRsp == diagnostics.c011ec24ExpectedCallerRsp
            ? 1u : 0u;
    diagnostics.c011ec24PreflightProven =
        diagnostics.c011ec24OutputAgreement == 1u &&
        diagnostics.c011ec24StandaloneTests == 2u ? 1u : 0u;
    GuideXosC011Ec24PreflightEvidence& evidence =
        g_c011ec24PreflightEvidence;
    evidence.proven = diagnostics.c011ec24PreflightProven;
    evidence.outputAgreement = diagnostics.c011ec24OutputAgreement;
    evidence.callerValid = diagnostics.c011ec24CallerValid;
    evidence.callerKernelRange = diagnostics.c011ec24CallerKernelRange;
    evidence.callerManagedRange = diagnostics.c011ec24CallerManagedRange;
    evidence.standaloneTests = diagnostics.c011ec24StandaloneTests;
    evidence.helperStandalonePassed = diagnostics.c011ec24HelperStandalonePassed;
    evidence.secondStandalonePassed = diagnostics.c011ec24SecondStandalonePassed;
    evidence.opcodeCount = diagnostics.c011ec24UnwindOpcodeCount;
    evidence.stackAdvance = diagnostics.c011ec24StackAdvance;
    evidence.liveRsp = diagnostics.c011ec24LiveRsp;
    evidence.returnSlot = diagnostics.c011ec24PreflightReturnSlot;
    evidence.returnValue = diagnostics.c011ec24PreflightReturnValue;
    evidence.expectedCallerRip = diagnostics.c011ec24ExpectedCallerRip;
    evidence.expectedCallerRsp = diagnostics.c011ec24ExpectedCallerRsp;
    evidence.outputRip = diagnostics.c011ec24PreflightOutputRip;
    evidence.outputRsp = diagnostics.c011ec24PreflightOutputRsp;
    for (uint32_t index = 0u; index < evidence.opcodeCount && index < 12u; ++index) {
        evidence.opcodeWords[index] = diagnostics.c011ec24OpcodeWords[index];
    }
    if (diagnostics.c011ec24PreflightProven != 0u &&
        diagnostics.c011ec24StandaloneTests == 2u) {
        emitC011EC24Preflight();
    }
    return diagnostics.c011ec24PreflightProven;
}

uint32_t c011ec24RunStandaloneChecks(uintptr_t helperPc) {
    guidexos_nativeaot_allocation_diagnostics& diagnostics =
        g_guideXosAllocationDiagnostics;
    guidexos_nativeaot_native_unwind_lookup_result helper = {};
    if (helperPc == 0u || guidexos_nativeaot_gc_native_unwind_lookup(
            helperPc, &helper) != 0) {
        return 0u;
    }
    const uintptr_t knownCaller = static_cast<uintptr_t>(helper.module_base) +
        helper.begin_address;
    const bool helperPassed = c011ec24RunSynthetic(helper, diagnostics, knownCaller);
    diagnostics.c011ec24HelperStandalonePassed = helperPassed ? 1u : 0u;

    uint32_t secondIndex = 0xFFFFFFFFu;
    bool secondPassed = false;
    const uintptr_t pdataStart = static_cast<uintptr_t>(helper.pdata_start);
    const uintptr_t pdataEnd = static_cast<uintptr_t>(helper.pdata_end);
    if (pdataEnd > pdataStart &&
        (pdataEnd - pdataStart) % sizeof(guidexos_nativeaot_runtime_function) == 0u) {
        const uint32_t count = static_cast<uint32_t>(
            (pdataEnd - pdataStart) / sizeof(guidexos_nativeaot_runtime_function));
        const auto* table = reinterpret_cast<const guidexos_nativeaot_runtime_function*>(
            pdataStart);
        for (uint32_t index = 0u; index < count && !secondPassed; ++index) {
            if (index == helper.table_index) continue;
            const auto& entry = table[index];
            if (entry.begin_address >= entry.end_address) continue;
            const uintptr_t infoAddress = static_cast<uintptr_t>(helper.module_base) +
                entry.unwind_data;
            const uint8_t* info = reinterpret_cast<const uint8_t*>(infoAddress);
            if (info[2] < 2u) continue;
            const uintptr_t secondPc = static_cast<uintptr_t>(helper.module_base) +
                entry.begin_address + (info[1] + 1u < entry.end_address - entry.begin_address
                    ? info[1] + 1u : 1u);
            guidexos_nativeaot_native_unwind_lookup_result second = {};
            if (guidexos_nativeaot_gc_native_unwind_lookup(secondPc, &second) != 0) {
                continue;
            }
            secondPassed = c011ec24RunSynthetic(second, diagnostics, knownCaller);
            if (secondPassed) {
                secondIndex = second.table_index;
                diagnostics.c011ec23SecondFunctionIndex = secondIndex;
                diagnostics.c011ec23SecondRuntimeFunction = second.runtime_function;
                diagnostics.c011ec23SecondUnwindInfo = second.unwind_info;
            }
        }
    }
    diagnostics.c011ec24SecondStandalonePassed = secondPassed ? 1u : 0u;
    diagnostics.c011ec24StandaloneTests =
        helperPassed && secondPassed ? 2u : (helperPassed ? 1u : 0u);
    return diagnostics.c011ec24StandaloneTests == 2u ? 1u : 0u;
}

#if defined(GUIDEXOS_NATIVEAOT_C011EC25_KERNEL_ENTRY_BOUNDARY)
bool c011ec25DecodeProgram(
    const guidexos_nativeaot_native_unwind_lookup_result& lookup,
    GuideXosC011Ec25UnwindProgram* program) {
    if (program == nullptr || lookup.unwind_info == 0u) return false;
    const uint8_t* info = reinterpret_cast<const uint8_t*>(
        static_cast<uintptr_t>(lookup.unwind_info));
    if ((info[0] & 0x07u) != 1u || (info[0] >> 3) != 0u) return false;
    program->codeCount = info[2];
    program->stackAdvance = 0u;
    program->supported = 1u;
    program->chained = 0u;
    program->allocationBytes = 0u;
    program->pushCount = 0u;
    if (program->codeCount > 12u) return false;
    const uint8_t* codes = info + 4u;
    for (uint32_t index = 0u; index < program->codeCount; ++index) {
        program->opcodeWords[index] = c011ec24ReadWord(codes, index);
    }
    for (uint32_t index = 0u; index < program->codeCount;) {
        const uint16_t word = c011ec24ReadWord(codes, index);
        const uint32_t unwindOp = (word >> 8) & 0x0Fu;
        const uint32_t opInfo = word >> 12;
        uint32_t slots = 1u;
        switch (unwindOp) {
            case 0u: // UWOP_PUSH_NONVOL
                program->stackAdvance += 8u;
                ++program->pushCount;
                break;
            case 1u: // UWOP_ALLOC_LARGE
                if (opInfo == 0u) {
                    if (index + 1u >= program->codeCount) return false;
                    const uint32_t bytes = static_cast<uint32_t>(
                        c011ec24ReadWord(codes, index + 1u)) * 8u;
                    program->stackAdvance += bytes;
                    program->allocationBytes += bytes;
                    slots = 2u;
                } else if (opInfo == 1u) {
                    if (index + 2u >= program->codeCount) return false;
                    const uint32_t bytes = static_cast<uint32_t>(
                        c011ec24ReadWord(codes, index + 1u)) |
                        static_cast<uint32_t>(c011ec24ReadWord(
                            codes, index + 2u)) << 16;
                    program->stackAdvance += bytes;
                    program->allocationBytes += bytes;
                    slots = 3u;
                } else {
                    return false;
                }
                break;
            case 2u: // UWOP_ALLOC_SMALL
                program->stackAdvance += opInfo * 8u + 8u;
                program->allocationBytes += opInfo * 8u + 8u;
                break;
            case 3u: // UWOP_SET_FPREG
                break;
            case 4u: // UWOP_SAVE_NONVOL
                if (index + 1u >= program->codeCount) return false;
                slots = 2u;
                break;
            case 5u: // UWOP_SAVE_NONVOL_FAR
                if (index + 2u >= program->codeCount) return false;
                slots = 3u;
                break;
            case 8u: // UWOP_SAVE_XMM128
                if (index + 1u >= program->codeCount) return false;
                slots = 2u;
                break;
            case 9u: // UWOP_SAVE_XMM128_FAR
                if (index + 2u >= program->codeCount) return false;
                slots = 3u;
                break;
            case 10u: // UWOP_PUSH_MACHFRAME
                program->stackAdvance += opInfo != 0u ? 48u : 40u;
                break;
            default:
                return false;
        }
        index += slots;
    }
    return true;
}

void emitC011EC25Preflight() {
    const guidexos_nativeaot_allocation_diagnostics& d =
        g_guideXosAllocationDiagnostics;
    suspendEeSerialPutString(
        "[nativeaot-gc-native-entry-boundary] preflight marker=C011EC25-PREFLIGHT");
#define C25PF32(name, value) \
    suspendEeSerialPutString(" " name "="); suspendEeSerialPutHex32(value)
#define C25PF64(name, value) \
    suspendEeSerialPutString(" " name "="); suspendEeSerialPutHex64(value)
    C25PF32("secondMetadataValid", d.c011ec25SecondMetadataValid);
    C25PF32("secondOutputAgreement", d.c011ec25SecondOutputAgreement);
    C25PF32("secondOpcodeCount", d.c011ec25SecondOpcodeCount);
    C25PF32("secondStackAdvance", d.c011ec25SecondStackAdvance);
    C25PF64("secondInputRIP", d.c011ec25SecondInputRip);
    C25PF64("secondInputRSP", d.c011ec25SecondInputRsp);
    C25PF64("secondInputRBP", d.c011ec25SecondInputRbp);
    C25PF64("secondReturnSlot", d.c011ec25SecondReturnSlot);
    C25PF64("secondReturnValue", d.c011ec25SecondReturnValue);
    C25PF64("expectedCallerRIP", d.c011ec25ExpectedCallerRip);
    C25PF64("expectedCallerRSP", d.c011ec25ExpectedCallerRsp);
    C25PF64("secondOutputRIP", d.c011ec25SecondOutputRip);
    C25PF64("secondOutputRSP", d.c011ec25SecondOutputRsp);
    C25PF64("secondOutputRBP", d.c011ec25SecondOutputRbp);
    C25PF64("secondEstablisherFrame", d.c011ec25SecondEstablisherFrame);
    C25PF64("secondHandlerData", d.c011ec25SecondHandlerData);
    C25PF64("thirdPhysicalPC", d.c011ec25ThirdPhysicalPc);
    C25PF64("thirdLinkedPC", d.c011ec25ThirdLinkedPc);
    C25PF64("linkedEntryPC", d.c011ec25LinkedEntryPc);
    C25PF64("linkedHaltPC", d.c011ec25LinkedHaltPc);
    C25PF64("bootStackTop", d.c011ec25BootStackTop);
    C25PF32("thirdInKernelRange", d.c011ec25ThirdInKernelRange);
    C25PF32("thirdLinkedLookupAttempted", d.c011ec25ThirdLinkedLookupAttempted);
    C25PF32("thirdLinkedLookupSucceeded", d.c011ec25ThirdLinkedLookupSucceeded);
    C25PF32("thirdPhysicalLookupAttempted", d.c011ec25ThirdPhysicalLookupAttempted);
    C25PF32("thirdPhysicalLookupSucceeded", d.c011ec25ThirdPhysicalLookupSucceeded);
    C25PF32("thirdMetadataPresent", d.c011ec25ThirdMetadataPresent);
    C25PF32("assemblyEntryBoundary", d.c011ec25AssemblyEntryBoundary);
    C25PF32("nonReturningHandoff", d.c011ec25NonReturningHandoff);
    C25PF32("stackBottomProven", d.c011ec25StackBottomProven);
    C25PF32("providerLookupResult", d.c011ec25ProviderLookupResult);
    C25PF32("linkedLookupResult", d.c011ec25LinkedLookupResult);
    C25PF32("physicalLookupResult", d.c011ec25PhysicalLookupResult);
    for (uint32_t index = 0u; index < d.c011ec25SecondOpcodeCount; ++index) {
        suspendEeSerialPutString(" opcodeWord");
        suspendEeSerialPutHex32(index);
        suspendEeSerialPutString("=");
        suspendEeSerialPutHex32(d.c011ec25SecondOpcodeWords[index]);
    }
#undef C25PF32
#undef C25PF64
    suspendEeSerialPutString("\n");
}

bool c011ec25ValidateBoundary(
    const guidexos_nativeaot_native_unwind_lookup_result& lookup,
    uintptr_t inputRip, uintptr_t inputRsp, uintptr_t inputRbp,
    uintptr_t outputRip, uintptr_t outputRsp, uintptr_t outputRbp,
    uintptr_t establisherFrame, uintptr_t handlerData,
    uintptr_t recoveredRbx, uintptr_t recoveredRsi, uintptr_t recoveredRdi,
    uintptr_t recoveredRbp) {
    guidexos_nativeaot_allocation_diagnostics& d =
        g_guideXosAllocationDiagnostics;
    d.c011ec25ProviderLookupResult = 0u;
    d.c011ec25SecondInputRip = inputRip;
    d.c011ec25SecondInputRsp = inputRsp;
    d.c011ec25SecondInputRbp = inputRbp;
    d.c011ec25SecondOutputRip = outputRip;
    d.c011ec25SecondOutputRsp = outputRsp;
    d.c011ec25SecondOutputRbp = outputRbp;
    d.c011ec25SecondEstablisherFrame = establisherFrame;
    d.c011ec25SecondHandlerData = handlerData;
    d.c011ec25SecondRecoveredRbx = recoveredRbx;
    d.c011ec25SecondRecoveredRsi = recoveredRsi;
    d.c011ec25SecondRecoveredRdi = recoveredRdi;
    d.c011ec25SecondRecoveredRbp = recoveredRbp;

    GuideXosC011Ec25UnwindProgram program = {};
    if (!c011ec25DecodeProgram(lookup, &program)) return false;
    d.c011ec25SecondMetadataValid = 1u;
    d.c011ec25SecondOpcodeCount = program.codeCount;
    d.c011ec25SecondStackAdvance = program.stackAdvance;
    for (uint32_t index = 0u; index < program.codeCount; ++index) {
        d.c011ec25SecondOpcodeWords[index] = program.opcodeWords[index];
    }
    if (inputRsp > ~static_cast<uintptr_t>(0) - program.stackAdvance) {
        return false;
    }
    d.c011ec25SecondReturnSlot = inputRsp + program.stackAdvance;
    d.c011ec25SecondReturnValue = *reinterpret_cast<const uintptr_t*>(
        d.c011ec25SecondReturnSlot);
    d.c011ec25ExpectedCallerRip = d.c011ec25SecondReturnValue;
    d.c011ec25ExpectedCallerRsp = d.c011ec25SecondReturnSlot + 8u;
    d.c011ec25SecondOutputAgreement =
        outputRip == d.c011ec25ExpectedCallerRip &&
        outputRsp == d.c011ec25ExpectedCallerRsp &&
        outputRbp == inputRbp ? 1u : 0u;

    // The first C24 lookup already captured the linked kernel geometry in the
    // append-only diagnostics record.  Reuse that geometry here rather than
    // calling a kernel-provider symbol that is not part of the managed
    // artifact's link contract.
    const uintptr_t linkedModuleBase = d.c011ec23ModuleBase;
    const uintptr_t linkedExecutableStart = d.c011ec23ExecutableStart;
    const uintptr_t linkedExecutableEnd = d.c011ec23ExecutableEnd;
    if (linkedModuleBase == 0u || linkedExecutableStart == 0u ||
        linkedExecutableEnd <= linkedExecutableStart ||
        lookup.module_base == linkedModuleBase ||
        outputRip < lookup.module_base ||
        outputRip >= lookup.executable_end) {
        return false;
    }
    d.c011ec25ThirdPhysicalPc = outputRip;
    d.c011ec25ThirdInKernelRange = 1u;
    d.c011ec25ThirdLinkedPc = linkedModuleBase +
        (outputRip - lookup.module_base);
    if (d.c011ec25ThirdLinkedPc < linkedExecutableStart ||
        d.c011ec25ThirdLinkedPc >= linkedExecutableEnd) {
        return false;
    }
    // The entry and .halt offsets are proven against the linked ELF .boot
    // section by the C25 harness.  The post-call RSP is the value of
    // boot_stack_top after kernel_main returns; retain that independently
    // derived value here for the runtime boundary proof.
    d.c011ec25LinkedEntryPc = linkedModuleBase;
    d.c011ec25LinkedHaltPc = linkedModuleBase + 0x1Eu;
    d.c011ec25BootStackTop = d.c011ec25ExpectedCallerRsp;
    d.c011ec25AssemblyEntryBoundary =
        d.c011ec25ThirdLinkedPc == d.c011ec25LinkedHaltPc ? 1u : 0u;

    guidexos_nativeaot_native_unwind_lookup_result linkedThird = {};
    d.c011ec25ThirdLinkedLookupAttempted = 1u;
    d.c011ec25LinkedLookupResult = static_cast<uint32_t>(
        guidexos_nativeaot_gc_native_unwind_lookup(
            d.c011ec25ThirdLinkedPc, &linkedThird));
    d.c011ec25ThirdLinkedLookupSucceeded =
        d.c011ec25LinkedLookupResult == 0u ? 1u : 0u;

    guidexos_nativeaot_native_unwind_lookup_result physicalThird = {};
    d.c011ec25ThirdPhysicalLookupAttempted = 1u;
    d.c011ec25PhysicalLookupResult = static_cast<uint32_t>(
        guidexos_nativeaot_gc_native_unwind_lookup(
            d.c011ec25ThirdPhysicalPc, &physicalThird));
    d.c011ec25ThirdPhysicalLookupSucceeded =
        d.c011ec25PhysicalLookupResult == 0u ? 1u : 0u;
    d.c011ec25ThirdMetadataPresent =
        (d.c011ec25ThirdLinkedLookupSucceeded != 0u ||
         d.c011ec25ThirdPhysicalLookupSucceeded != 0u) ? 1u : 0u;
    d.c011ec25NonReturningHandoff =
        d.c011ec25AssemblyEntryBoundary != 0u ? 1u : 0u;
    d.c011ec25StackBottomProven =
        d.c011ec25AssemblyEntryBoundary != 0u &&
        d.c011ec25ThirdMetadataPresent == 0u &&
        outputRsp == d.c011ec25BootStackTop ? 1u : 0u;
    d.c011ec25PreflightProven =
        d.c011ec25SecondMetadataValid != 0u &&
        d.c011ec25SecondOutputAgreement != 0u &&
        d.c011ec25ThirdInKernelRange != 0u &&
        d.c011ec25AssemblyEntryBoundary != 0u &&
        d.c011ec25ThirdMetadataPresent == 0u &&
        d.c011ec25NonReturningHandoff != 0u &&
        d.c011ec25StackBottomProven != 0u ? 1u : 0u;
    if (d.c011ec25PreflightProven != 0u) emitC011EC25Preflight();
    return d.c011ec25PreflightProven != 0u;
}
#if defined(GUIDEXOS_NATIVEAOT_C011EC26_STACK_COMPLETION)
bool c011ec26ValidateSecondUnwind(
    const guidexos_nativeaot_native_unwind_lookup_result& lookup,
    uintptr_t inputRip, uintptr_t inputRsp, uintptr_t inputRbp,
    uintptr_t outputRip, uintptr_t outputRsp, uintptr_t outputRbp,
    uintptr_t establisherFrame, uintptr_t handlerData,
    uintptr_t recoveredRbx, uintptr_t recoveredRsi, uintptr_t recoveredRdi,
    uintptr_t recoveredRbp) {
    guidexos_nativeaot_allocation_diagnostics& d =
        g_guideXosAllocationDiagnostics;
    d.c011ec25SecondInputRip = inputRip;
    d.c011ec25SecondInputRsp = inputRsp;
    d.c011ec25SecondInputRbp = inputRbp;
    d.c011ec25SecondOutputRip = outputRip;
    d.c011ec25SecondOutputRsp = outputRsp;
    d.c011ec25SecondOutputRbp = outputRbp;
    d.c011ec25SecondEstablisherFrame = establisherFrame;
    d.c011ec25SecondHandlerData = handlerData;
    d.c011ec25SecondRecoveredRbx = recoveredRbx;
    d.c011ec25SecondRecoveredRsi = recoveredRsi;
    d.c011ec25SecondRecoveredRdi = recoveredRdi;
    d.c011ec25SecondRecoveredRbp = recoveredRbp;

    GuideXosC011Ec25UnwindProgram program = {};
    if (!c011ec25DecodeProgram(lookup, &program) ||
        inputRsp > ~static_cast<uintptr_t>(0) - program.stackAdvance) {
        d.c011ec25SecondMetadataValid = 0u;
        return false;
    }
    d.c011ec25SecondMetadataValid = 1u;
    d.c011ec25SecondOpcodeCount = program.codeCount;
    d.c011ec25SecondStackAdvance = program.stackAdvance;
    for (uint32_t index = 0u; index < program.codeCount; ++index) {
        d.c011ec25SecondOpcodeWords[index] = program.opcodeWords[index];
    }
    d.c011ec25SecondReturnSlot = inputRsp + program.stackAdvance;
    d.c011ec25SecondReturnValue = *reinterpret_cast<const uintptr_t*>(
        d.c011ec25SecondReturnSlot);
    d.c011ec25ExpectedCallerRip = d.c011ec25SecondReturnValue;
    d.c011ec25ExpectedCallerRsp = d.c011ec25SecondReturnSlot + 8u;
    d.c011ec25SecondOutputAgreement =
        outputRip == d.c011ec25ExpectedCallerRip &&
        outputRsp == d.c011ec25ExpectedCallerRsp &&
        outputRbp == inputRbp ? 1u : 0u;
    return d.c011ec25SecondOutputAgreement != 0u;
}

static void emitC011EC26Preflight() {
    const guidexos_nativeaot_allocation_diagnostics& d =
        g_guideXosAllocationDiagnostics;
    suspendEeSerialPutString(
        "[nativeaot-gc-stack-completion] preflight marker=C011EC26-PREFLIGHT");
#define C26PF32(name, value) \
    suspendEeSerialPutString(" " name "="); suspendEeSerialPutHex32(value)
#define C26PF64(name, value) \
    suspendEeSerialPutString(" " name "="); suspendEeSerialPutHex64(value)
    C26PF32("terminalClassification", d.c011ec26TerminalClassificationResult);
    C26PF32("terminalDescriptorValid", d.c011ec26TerminalDescriptorValid);
    C26PF32("terminalLookupAttempts", d.c011ec26TerminalLookupAttemptCount);
    C26PF32("terminalLookupSuccesses", d.c011ec26TerminalLookupSuccessCount);
    C26PF32("nativeUnwindCount", d.c011ec23UnwindAttemptCount);
    C26PF32("nativeFramesCrossed", d.c011ec23NativeFramesCrossed);
    C26PF32("thirdUnwindAttempts", d.c011ec26ThirdUnwindAttemptCount);
    C26PF64("terminalInputPC", d.c011ec26TerminalInputPc);
    C26PF64("terminalSelectedPC", d.c011ec26TerminalSelectedPc);
    C26PF64("terminalLinkedPC", d.c011ec26TerminalLinkedPc);
    C26PF64("terminalModuleBase", d.c011ec26TerminalModuleBase);
    C26PF64("terminalExecutableStart", d.c011ec26TerminalExecutableStart);
    C26PF64("terminalExecutableEnd", d.c011ec26TerminalExecutableEnd);
    C26PF64("terminalBeginRVA", d.c011ec26TerminalBeginRva);
    C26PF64("terminalEndRVA", d.c011ec26TerminalEndRva);
    C26PF64("terminalRSP", d.c011ec26TerminalRsp);
#undef C26PF32
#undef C26PF64
    suspendEeSerialPutString("\n");
}
#endif
#endif
#endif

uint64_t c011ec23LoadRegister(uintptr_t location) {
    return location == 0u ? 0u : *reinterpret_cast<const uint64_t*>(location);
}

void c011ec23StoreRegDisplayLocations(
    GuideXosNativeAmd64RegDisplay* display, uint64_t** locations) {
    if (display == nullptr || locations == nullptr) return;
    display->pRbx = reinterpret_cast<uintptr_t>(locations[0]);
    display->pRbp = reinterpret_cast<uintptr_t>(locations[2]);
    display->pRsi = reinterpret_cast<uintptr_t>(locations[3]);
    display->pRdi = reinterpret_cast<uintptr_t>(locations[4]);
    display->pR12 = reinterpret_cast<uintptr_t>(locations[5]);
    display->pR13 = reinterpret_cast<uintptr_t>(locations[6]);
    display->pR14 = reinterpret_cast<uintptr_t>(locations[7]);
    display->pR15 = reinterpret_cast<uintptr_t>(locations[8]);
}

extern "C" uint32_t __cdecl
guideXosNativeAotC011EC23TryNativeUnwind(
    uintptr_t controlPc, uintptr_t regDisplayAddress) {
    guidexos_nativeaot_allocation_diagnostics& d =
        g_guideXosAllocationDiagnostics;
    if (g_c011ec23StandaloneEvidence.succeeded != 0u) {
        d.c011ec23SecondFunctionAttempted =
            g_c011ec23StandaloneEvidence.attempted;
        d.c011ec23SecondFunctionSucceeded =
            g_c011ec23StandaloneEvidence.succeeded;
        d.c011ec23SecondFunctionResult = g_c011ec23StandaloneEvidence.result;
        d.c011ec23SecondFunctionIndex = g_c011ec23StandaloneEvidence.index;
        d.c011ec23SecondRuntimeFunction =
            g_c011ec23StandaloneEvidence.runtimeFunction;
        d.c011ec23SecondUnwindInfo = g_c011ec23StandaloneEvidence.unwindInfo;
        d.c011ec23SecondOutputRip = g_c011ec23StandaloneEvidence.outputRip;
        d.c011ec23SecondOutputRsp = g_c011ec23StandaloneEvidence.outputRsp;
    }
    GuideXosNativeAmd64RegDisplay* display =
        reinterpret_cast<GuideXosNativeAmd64RegDisplay*>(regDisplayAddress);
    if (display == nullptr || controlPc == 0u) {
        d.c011ec23UnwindResult = 4u;
        d.c011ec23Outcome = 4u;
        return 0u;
    }

    uintptr_t currentPc = controlPc;
    for (uint32_t frameIndex = 0u;
         frameIndex < kC011EC23MaximumNativeFrames; ++frameIndex) {
#if defined(GUIDEXOS_NATIVEAOT_C011EC26_STACK_COMPLETION)
        ++d.c011ec26TerminalLookupAttemptCount;
        guidexos_nativeaot_native_unwind_lookup_result terminal = {};
        const int32_t terminalClassification =
            guidexos_nativeaot_gc_native_unwind_classify(currentPc, &terminal);
        d.c011ec26TerminalClassificationResult =
            static_cast<uint32_t>(terminalClassification);
        if (terminalClassification ==
            GUIDEXOS_NATIVEAOT_NATIVE_UNWIND_CLASSIFICATION_TERMINAL) {
            d.c011ec26TerminalInputPc = currentPc;
            d.c011ec26TerminalSelectedPc = currentPc;
            d.c011ec26TerminalModuleBase = terminal.module_base;
            d.c011ec26TerminalExecutableStart = terminal.executable_start;
            d.c011ec26TerminalExecutableEnd = terminal.executable_end;
            d.c011ec26TerminalBeginRva = terminal.begin_address;
            d.c011ec26TerminalEndRva = terminal.end_address;
            d.c011ec26TerminalRsp = display->SP;
            const uintptr_t linkedBase = d.c011ec23ModuleBase;
            const uintptr_t selectedBase =
                static_cast<uintptr_t>(terminal.module_base);
            const bool descriptorValid =
                selectedBase != 0u && terminal.executable_start != 0u &&
                terminal.executable_end > terminal.executable_start &&
                terminal.begin_address < terminal.end_address &&
                terminal.runtime_function == 0u && terminal.unwind_info == 0u &&
                terminal.unwind_data == 0u && terminal.table_index == UINT32_MAX &&
                currentPc >= selectedBase &&
                currentPc - selectedBase >= terminal.begin_address &&
                currentPc - selectedBase < terminal.end_address;
            d.c011ec26TerminalDescriptorValid = descriptorValid ? 1u : 0u;
            if (descriptorValid && linkedBase != 0u && currentPc >= selectedBase &&
                currentPc - selectedBase <= ~static_cast<uintptr_t>(0) - linkedBase) {
                d.c011ec26TerminalLinkedPc = linkedBase +
                    (currentPc - selectedBase);
            }
            ++d.c011ec26TerminalLookupSuccessCount;
            d.c011ec23UnwindResult = 6u;
            d.c011ec23Outcome = 6u;
            d.c011ec26PreflightProven =
                descriptorValid && d.c011ec23NativeFramesCrossed == 2u &&
                d.c011ec25SecondMetadataValid != 0u &&
                d.c011ec25SecondOutputAgreement != 0u &&
                d.c011ec26ThirdUnwindAttemptCount == 0u ? 1u : 0u;
            if (d.c011ec26PreflightProven != 0u) {
                emitC011EC26Preflight();
            } else {
                d.c011ec26SafeStopReason = 0xC0260001u;
                guideXosNativeAotC011EC25SafeStop(0xC0260001u);
            }
            return 2u;
        }
        if (terminalClassification ==
            GUIDEXOS_NATIVEAOT_NATIVE_UNWIND_CLASSIFICATION_MALFORMED) {
            d.c011ec26SafeStopReason = 0xC0260002u;
            guideXosNativeAotC011EC25SafeStop(0xC0260002u);
        }
#endif
        ++d.c011ec23LookupAttemptCount;
        guidexos_nativeaot_native_unwind_lookup_result lookup = {};
        if (guidexos_nativeaot_gc_native_unwind_lookup(currentPc, &lookup) != 0) {
            if (d.c011ec23NativeFramesCrossed != 0u) {
                d.c011ec23UnwindResult = 3u;
                d.c011ec23Outcome = 3u;
                guideXosNativeAotC011EC23SafeStop(0xC0230003u);
            }
            d.c011ec23UnwindResult = 4u;
            d.c011ec23Outcome = 4u;
            return 0u;
        }

        ++d.c011ec23LookupSuccessCount;
#if defined(GUIDEXOS_NATIVEAOT_C011EC24_CALLER_PROVENANCE)
        if (frameIndex == 1u) {
            d.c011ec24SecondInputRip = currentPc;
            d.c011ec24SecondInputRsp = display->SP;
            d.c011ec24SecondInputRbp = c011ec23LoadRegister(display->pRbp);
        }
#endif
        if (frameIndex == 0u) {
            d.c011ec23InputRip = currentPc;
            d.c011ec23InputRsp = display->SP;
            d.c011ec23InputRbp = c011ec23LoadRegister(display->pRbp);
            d.c011ec23ModuleBase = static_cast<uintptr_t>(lookup.module_base);
            d.c011ec23ExecutableStart = static_cast<uintptr_t>(lookup.executable_start);
            d.c011ec23ExecutableEnd = static_cast<uintptr_t>(lookup.executable_end);
            d.c011ec23PdataStart = static_cast<uintptr_t>(lookup.pdata_start);
            d.c011ec23PdataEnd = static_cast<uintptr_t>(lookup.pdata_end);
            d.c011ec23XdataStart = static_cast<uintptr_t>(lookup.xdata_start);
            d.c011ec23XdataEnd = static_cast<uintptr_t>(lookup.xdata_end);
            d.c011ec23RuntimeFunction = static_cast<uintptr_t>(lookup.runtime_function);
            d.c011ec23UnwindInfo = static_cast<uintptr_t>(lookup.unwind_info);
            d.c011ec23BeginAddress = lookup.begin_address;
            d.c011ec23EndAddress = lookup.end_address;
            d.c011ec23UnwindData = lookup.unwind_data;
            d.c011ec23UnwindVersion = lookup.unwind_version;
            d.c011ec23UnwindFlags = lookup.unwind_flags;
            d.c011ec23PrologueSize = lookup.prologue_size;
            d.c011ec23UnwindCodeCount = lookup.unwind_code_count;
            d.c011ec23FrameRegister = lookup.frame_register;
            d.c011ec23FrameOffset = lookup.frame_offset;
#if defined(GUIDEXOS_NATIVEAOT_C011EC24_CALLER_PROVENANCE)
            GuideXosC011Ec24UnwindProgram program = {};
            if (!c011ec24DecodeProgram(lookup, d, &program) ||
                program.chained != 0u) {
#if defined(GUIDEXOS_NATIVEAOT_C011EC25_KERNEL_ENTRY_BOUNDARY)
                guideXosNativeAotC011EC25SafeStop(0xC0250003u);
#else
                guideXosNativeAotC011EC24SafeStop(0xC0240001u);
#endif
            }
            d.c011ec24DerivedReturnSlot = static_cast<uintptr_t>(display->SP) +
                program.stackAdvance;
            d.c011ec24DerivedReturnValue = *reinterpret_cast<const uintptr_t*>(
                d.c011ec24DerivedReturnSlot);
            d.c011ec24ExpectedCallerRip = d.c011ec24DerivedReturnValue;
            d.c011ec24ExpectedCallerRsp = d.c011ec24DerivedReturnSlot + 8u;
            d.c011ec24PreRbx = c011ec23LoadRegister(display->pRbx);
            d.c011ec24PreRsi = c011ec23LoadRegister(display->pRsi);
            d.c011ec24PreRdi = c011ec23LoadRegister(display->pRdi);
            d.c011ec24PreRbp = c011ec23LoadRegister(display->pRbp);
            d.c011ec24PreR12 = c011ec23LoadRegister(display->pR12);
            d.c011ec24PreR13 = c011ec23LoadRegister(display->pR13);
            d.c011ec24PreR14 = c011ec23LoadRegister(display->pR14);
            d.c011ec24PreR15 = c011ec23LoadRegister(display->pR15);
#endif
        }

        uint8_t context[0x100u] = {};
        *reinterpret_cast<uint64_t*>(context + kC011EC23ContextRbx) =
            c011ec23LoadRegister(display->pRbx);
        *reinterpret_cast<uint64_t*>(context + kC011EC23ContextRsp) = display->SP;
        *reinterpret_cast<uint64_t*>(context + kC011EC23ContextRbp) =
            c011ec23LoadRegister(display->pRbp);
        *reinterpret_cast<uint64_t*>(context + kC011EC23ContextRsi) =
            c011ec23LoadRegister(display->pRsi);
        *reinterpret_cast<uint64_t*>(context + kC011EC23ContextRdi) =
            c011ec23LoadRegister(display->pRdi);
        *reinterpret_cast<uint64_t*>(context + kC011EC23ContextR12) =
            c011ec23LoadRegister(display->pR12);
        *reinterpret_cast<uint64_t*>(context + kC011EC23ContextR13) =
            c011ec23LoadRegister(display->pR13);
        *reinterpret_cast<uint64_t*>(context + kC011EC23ContextR14) =
            c011ec23LoadRegister(display->pR14);
        *reinterpret_cast<uint64_t*>(context + kC011EC23ContextR15) =
            c011ec23LoadRegister(display->pR15);
        *reinterpret_cast<uint64_t*>(context + kC011EC23ContextRip) = currentPc;

        uint64_t* contextPointers[10] = {};
        contextPointers[0] = reinterpret_cast<uint64_t*>(display->pRbx);
        contextPointers[2] = reinterpret_cast<uint64_t*>(display->pRbp);
        contextPointers[3] = reinterpret_cast<uint64_t*>(display->pRsi);
        contextPointers[4] = reinterpret_cast<uint64_t*>(display->pRdi);
        contextPointers[5] = reinterpret_cast<uint64_t*>(display->pR12);
        contextPointers[6] = reinterpret_cast<uint64_t*>(display->pR13);
        contextPointers[7] = reinterpret_cast<uint64_t*>(display->pR14);
        contextPointers[8] = reinterpret_cast<uint64_t*>(display->pR15);

        void* handlerData = nullptr;
        uint64_t establisherFrame = 0u;
        ++d.c011ec23UnwindAttemptCount;
        ++d.c011ec23RtlVirtualUnwindCallCount;
        guideXosRtlVirtualUnwind(
            0u, lookup.module_base, currentPc,
            reinterpret_cast<void*>(lookup.runtime_function),
            context, &handlerData, &establisherFrame, contextPointers);
        ++d.c011ec23RtlVirtualUnwindReturned;

        const uintptr_t outputRip = static_cast<uintptr_t>(
            *reinterpret_cast<uint64_t*>(context + kC011EC23ContextRip));
        const uintptr_t outputRsp = static_cast<uintptr_t>(
            *reinterpret_cast<uint64_t*>(context + kC011EC23ContextRsp));
        const uintptr_t outputRbp = c011ec23LoadRegister(display->pRbp);
        if (frameIndex == 0u) {
            d.c011ec23OutputRip = outputRip;
            d.c011ec23OutputRsp = outputRsp;
            d.c011ec23OutputRbp = outputRbp;
            d.c011ec23EstablisherFrame = static_cast<uintptr_t>(establisherFrame);
            d.c011ec23HandlerData = reinterpret_cast<uintptr_t>(handlerData);
            d.c011ec23RestoredRbx = c011ec23LoadRegister(display->pRbx);
            d.c011ec23RestoredRbp = c011ec23LoadRegister(display->pRbp);
            d.c011ec23RestoredRsi = c011ec23LoadRegister(display->pRsi);
            d.c011ec23RestoredRdi = c011ec23LoadRegister(display->pRdi);
            d.c011ec23RestoredR12 = c011ec23LoadRegister(display->pR12);
            d.c011ec23RestoredR13 = c011ec23LoadRegister(display->pR13);
            d.c011ec23RestoredR14 = c011ec23LoadRegister(display->pR14);
            d.c011ec23RestoredR15 = c011ec23LoadRegister(display->pR15);
            d.c011ec23RestoredRegisterCount =
                (contextPointers[0] != nullptr ? 1u : 0u) +
                (contextPointers[2] != nullptr ? 1u : 0u) +
                (contextPointers[3] != nullptr ? 1u : 0u) +
                (contextPointers[4] != nullptr ? 1u : 0u) +
                (contextPointers[5] != nullptr ? 1u : 0u) +
                (contextPointers[6] != nullptr ? 1u : 0u) +
                (contextPointers[7] != nullptr ? 1u : 0u) +
                (contextPointers[8] != nullptr ? 1u : 0u);
#if defined(GUIDEXOS_NATIVEAOT_C011EC24_CALLER_PROVENANCE)
            d.c011ec24SourceRbx = reinterpret_cast<uintptr_t>(contextPointers[0]);
            d.c011ec24SourceRbp = reinterpret_cast<uintptr_t>(contextPointers[2]);
            d.c011ec24SourceRsi = reinterpret_cast<uintptr_t>(contextPointers[3]);
            d.c011ec24SourceRdi = reinterpret_cast<uintptr_t>(contextPointers[4]);
            d.c011ec24SourceR12 = reinterpret_cast<uintptr_t>(contextPointers[5]);
            d.c011ec24SourceR13 = reinterpret_cast<uintptr_t>(contextPointers[6]);
            d.c011ec24SourceR14 = reinterpret_cast<uintptr_t>(contextPointers[7]);
            d.c011ec24SourceR15 = reinterpret_cast<uintptr_t>(contextPointers[8]);
            d.c011ec24RecoveredRbx = c011ec24ContextValue(
                context, kC011EC23ContextRbx);
            d.c011ec24RecoveredRbp = c011ec24ContextValue(
                context, kC011EC23ContextRbp);
            d.c011ec24RecoveredRsi = c011ec24ContextValue(
                context, kC011EC23ContextRsi);
            d.c011ec24RecoveredRdi = c011ec24ContextValue(
                context, kC011EC23ContextRdi);
            d.c011ec24RecoveredR12 = c011ec24ContextValue(
                context, kC011EC23ContextR12);
            d.c011ec24RecoveredR13 = c011ec24ContextValue(
                context, kC011EC23ContextR13);
            d.c011ec24RecoveredR14 = c011ec24ContextValue(
                context, kC011EC23ContextR14);
            d.c011ec24RecoveredR15 = c011ec24ContextValue(
                context, kC011EC23ContextR15);
            d.c011ec23RestoredRbx = d.c011ec24RecoveredRbx;
            d.c011ec23RestoredRbp = d.c011ec24RecoveredRbp;
            d.c011ec23RestoredRsi = d.c011ec24RecoveredRsi;
            d.c011ec23RestoredRdi = d.c011ec24RecoveredRdi;
            d.c011ec23RestoredR12 = d.c011ec24RecoveredR12;
            d.c011ec23RestoredR13 = d.c011ec24RecoveredR13;
            d.c011ec23RestoredR14 = d.c011ec24RecoveredR14;
            d.c011ec23RestoredR15 = d.c011ec24RecoveredR15;
            d.c011ec23OutputRbp = d.c011ec24RecoveredRbp;
            d.c011ec24OutputAgreement =
                outputRip == d.c011ec24ExpectedCallerRip &&
                outputRsp == d.c011ec24ExpectedCallerRsp ? 1u : 0u;
#if defined(GUIDEXOS_NATIVEAOT_C011EC24_CALLER_PROVENANCE)
            guidexos_nativeaot_native_unwind_lookup_result callerLookup = {};
            d.c011ec24CallerValid = c011ec24CallerOwnership(
                outputRip, lookup, &d.c011ec24CallerKernelRange,
                &d.c011ec24CallerManagedRange, &callerLookup) ? 1u : 0u;
            d.c011ec24SecondProviderLookupAttempted = 1u;
            d.c011ec24SecondProviderLookupSucceeded =
                d.c011ec24CallerValid != 0u ? 1u : 0u;
            d.c011ec24SecondModuleBase = callerLookup.module_base;
            d.c011ec24SecondExecutableStart = callerLookup.executable_start;
            d.c011ec24SecondExecutableEnd = callerLookup.executable_end;
            d.c011ec24SecondRuntimeFunction = callerLookup.runtime_function;
            d.c011ec24SecondUnwindInfo = callerLookup.unwind_info;
#else
            d.c011ec24CallerValid = c011ec24CallerOwnership(
                outputRip, lookup, &d.c011ec24CallerKernelRange,
                &d.c011ec24CallerManagedRange, nullptr) ? 1u : 0u;
#endif
            const GuideXosC011Ec24PreflightEvidence& evidence =
                g_c011ec24PreflightEvidence;
            d.c011ec24PreflightProven = evidence.proven;
            d.c011ec24StandaloneTests = evidence.standaloneTests;
            d.c011ec24HelperStandalonePassed = evidence.helperStandalonePassed;
            d.c011ec24SecondStandalonePassed = evidence.secondStandalonePassed;
            d.c011ec24UnwindOpcodeCount = evidence.opcodeCount;
            d.c011ec24StackAdvance = evidence.stackAdvance;
            d.c011ec24PreflightReturnSlot = evidence.returnSlot;
            d.c011ec24PreflightReturnValue = evidence.returnValue;
            d.c011ec24PreflightOutputRip = evidence.outputRip;
            d.c011ec24PreflightOutputRsp = evidence.outputRsp;
            d.c011ec24LiveRsp = evidence.liveRsp;
            for (uint32_t index = 0u; index < evidence.opcodeCount && index < 12u; ++index) {
                d.c011ec24OpcodeWords[index] = evidence.opcodeWords[index];
            }
#endif
        }
        if (outputRip == 0u || outputRsp <= display->SP ||
            (outputRsp & (sizeof(uintptr_t) - 1u)) != 0u) {
            d.c011ec23UnwindResult = 5u;
            d.c011ec23Outcome = 5u;
            return 0u;
        }

        display->SP = outputRsp;
        display->IP = outputRip;
        c011ec23StoreRegDisplayLocations(display, contextPointers);
        ++d.c011ec23NativeFramesCrossed;
        currentPc = outputRip;

#if defined(GUIDEXOS_NATIVEAOT_C011EC24_CALLER_PROVENANCE)
        if (frameIndex == 0u) {
            d.c011ec23UnwindResult = 3u;
            d.c011ec23Outcome = 3u;
            if (d.c011ec24OutputAgreement == 0u ||
                d.c011ec24PreflightProven == 0u ||
                d.c011ec24PreflightReturnSlot != d.c011ec24DerivedReturnSlot ||
                d.c011ec24PreflightReturnValue != d.c011ec24DerivedReturnValue ||
                d.c011ec24PreflightOutputRip != outputRip ||
                d.c011ec24PreflightOutputRsp != outputRsp) {
#if defined(GUIDEXOS_NATIVEAOT_C011EC25_KERNEL_ENTRY_BOUNDARY)
                guideXosNativeAotC011EC25SafeStop(0xC0250004u);
#else
                guideXosNativeAotC011EC24SafeStop(0xC0240002u);
#endif
            }
            if (d.c011ec24CallerValid == 0u) {
#if defined(GUIDEXOS_NATIVEAOT_C011EC25_KERNEL_ENTRY_BOUNDARY)
                guideXosNativeAotC011EC25SafeStop(0xC0250005u);
#else
                guideXosNativeAotC011EC24SafeStop(0xC0240003u);
#endif
            }
        }
        if (frameIndex == 1u) {
            d.c011ec24SecondProductionUnwindAttempted = 1u;
            d.c011ec24SecondUnwindResult = 3u;
            d.c011ec24SecondOutputRip = outputRip;
            d.c011ec24SecondOutputRsp = outputRsp;
            d.c011ec24SecondOutputRbp = c011ec24ContextValue(
                context, kC011EC23ContextRbp);
            if (outputRip == d.c011ec23OutputRip ||
                outputRsp <= d.c011ec24SecondInputRsp) {
#if defined(GUIDEXOS_NATIVEAOT_C011EC25_KERNEL_ENTRY_BOUNDARY)
                guideXosNativeAotC011EC25SafeStop(0xC0250001u);
#else
                guideXosNativeAotC011EC24SafeStop(0xC0240005u);
#endif
            }
#if defined(GUIDEXOS_NATIVEAOT_C011EC25_KERNEL_ENTRY_BOUNDARY)
#if defined(GUIDEXOS_NATIVEAOT_C011EC26_STACK_COMPLETION)
            if (!c011ec26ValidateSecondUnwind(
                    lookup,
                    d.c011ec24SecondInputRip,
                    d.c011ec24SecondInputRsp,
                    d.c011ec24SecondInputRbp,
                    outputRip,
                    outputRsp,
                    d.c011ec24SecondOutputRbp,
                    establisherFrame,
                    reinterpret_cast<uintptr_t>(handlerData),
                    c011ec23LoadRegister(display->pRbx),
                    c011ec23LoadRegister(display->pRsi),
                    c011ec23LoadRegister(display->pRdi),
                    c011ec23LoadRegister(display->pRbp))) {
                d.c011ec26SafeStopReason = 0xC0260003u;
                guideXosNativeAotC011EC25SafeStop(0xC0260003u);
            }
#else
            if (!c011ec25ValidateBoundary(
                    lookup,
                    d.c011ec24SecondInputRip,
                    d.c011ec24SecondInputRsp,
                    d.c011ec24SecondInputRbp,
                    outputRip,
                    outputRsp,
                    d.c011ec24SecondOutputRbp,
                    establisherFrame,
                    reinterpret_cast<uintptr_t>(handlerData),
                    c011ec23LoadRegister(display->pRbx),
                    c011ec23LoadRegister(display->pRsi),
                    c011ec23LoadRegister(display->pRdi),
                    c011ec23LoadRegister(display->pRbp))) {
                guideXosNativeAotC011EC25SafeStop(0xC0250002u);
            }
#endif
#if !defined(GUIDEXOS_NATIVEAOT_C011EC26_STACK_COMPLETION)
            guideXosNativeAotC011EC25SafeStop(0xC0250000u);
#endif
#else
            guideXosNativeAotC011EC24SafeStop(0xC0240004u);
#endif
        }
#endif

        RuntimeInstance* runtime = GetRuntimeInstance();
        ICodeManager* callerCodeManager = nullptr;
        if (c011ec18IsInsideManagedRange(currentPc, runtime,
                                         &callerCodeManager)) {
            d.c011ec23ManagedReentryFound = 1u;
            d.c011ec23CallerManagedRange = 1u;
            d.c011ec23CallerCodeManager =
                reinterpret_cast<uintptr_t>(callerCodeManager);
            d.c011ec23CallerCodeManagerFound = callerCodeManager != nullptr ? 1u : 0u;
            d.c011ec23UnwindResult = 1u;
            d.c011ec23Outcome = 2u;
            return 1u;
        }
    }

    d.c011ec23UnwindResult = 3u;
    d.c011ec23Outcome = 3u;
    guideXosNativeAotC011EC23SafeStop(0xC0230008u);
}

#if defined(GUIDEXOS_NATIVEAOT_C011EC24_CALLER_PROVENANCE)
extern "C" __declspec(dllexport) uint32_t __cdecl
guideXosNativeAotC011EC23StandaloneNativeUnwind(
    uintptr_t helperPc, uintptr_t liveRsp) {
    if (c011ec24RunStandaloneChecks(helperPc) == 0u) return 0u;
    return guideXosNativeAotC011EC24PreflightRealFrame(helperPc, liveRsp);
}
#else
extern "C" __declspec(dllexport) uint32_t __cdecl
guideXosNativeAotC011EC23StandaloneNativeUnwind(uintptr_t helperPc) {
    guidexos_nativeaot_allocation_diagnostics& diagnostics =
        g_guideXosAllocationDiagnostics;
    guidexos_nativeaot_native_unwind_lookup_result helper = {};
    if (helperPc == 0u || guidexos_nativeaot_gc_native_unwind_lookup(
            helperPc, &helper) != 0) {
        diagnostics.c011ec23SecondFunctionResult = 2u;
        return 0u;
    }
    return c011ec23RunStandaloneSecondUnwind(helper, diagnostics) ? 1u : 0u;
}
#endif
#endif

static void emitC011EC18Marker() {
    guidexos_nativeaot_allocation_diagnostics& d =
        g_guideXosAllocationDiagnostics;
    if (d.c011ec18MarkerEmitted != 0u ||
        d.c011ec18TransitionFrameAddress == 0u ||
        d.c011ec18TransitionFrameRip == 0u ||
        d.c011ec18IteratorControlPc != d.c011ec18TransitionFrameRip ||
        d.c011ec18TransitionCodeManager == 0u ||
        d.c011ec18IteratorCodeManager != d.c011ec18TransitionCodeManager ||
        d.c011ec18AuthenticManagedCodeManager == 0u ||
        d.c011ec18FindMethodInfoSuccessCount == 0u ||
        d.c011ec18MethodMetadataValid == 0u) {
        return;
    }

    d.c011ec18MarkerEmitted = 1u;
    suspendEeSerialPutString("[nativeaot-code-manager] transition-frame-control-pc frame=");
    suspendEeSerialPutHex64(d.c011ec18TransitionFrameAddress);
    suspendEeSerialPutString(" transitionRIP=");
    suspendEeSerialPutHex64(d.c011ec18TransitionFrameRip);
    suspendEeSerialPutString(" iteratorControlPC=");
    suspendEeSerialPutHex64(d.c011ec18IteratorControlPc);
    suspendEeSerialPutString(" iteratorSP=");
    suspendEeSerialPutHex64(d.c011ec18IteratorInitialSp);
    suspendEeSerialPutString(" iteratorFP=");
    suspendEeSerialPutHex64(d.c011ec18IteratorInitialFp);
    suspendEeSerialPutString(" manager=");
    suspendEeSerialPutHex64(d.c011ec18IteratorCodeManager);
    suspendEeSerialPutString(" methodInfo=");
    suspendEeSerialPutHex64(d.c011ec18IteratorMethodInfo);
    suspendEeSerialPutString(" methodInfoResult=");
    suspendEeSerialPutHex32(d.c011ec18FindMethodInfoSuccessCount != 0u ? 1u : 0u);
    suspendEeSerialPutString(" managedRange=");
    suspendEeSerialPutHex32(d.c011ec18TransitionInManagedRange);
    suspendEeSerialPutString(" transitionInRange=");
    suspendEeSerialPutHex32(d.c011ec18TransitionInManagedRange);
    suspendEeSerialPutString(" authenticManager=");
    suspendEeSerialPutHex64(d.c011ec18AuthenticManagedCodeManager);
    suspendEeSerialPutString(" marker=C011EC18\n");
}

extern "C" void __cdecl
guideXosNativeAotC011EC18RhpGcAllocEntered(
    uintptr_t frameAddress, uintptr_t eeType, uintptr_t flags,
    uintptr_t numElements, uintptr_t threadAddress) {
    (void)eeType;
    (void)flags;
    (void)numElements;
    guidexos_nativeaot_allocation_diagnostics& d =
        g_guideXosAllocationDiagnostics;
    ++d.c011ec18TransitionFrameCount;

    PInvokeTransitionFrame* frame =
        reinterpret_cast<PInvokeTransitionFrame*>(frameAddress);
    d.c011ec18CurrentNativeRip =
        reinterpret_cast<uintptr_t>(_ReturnAddress());
    d.c011ec18CurrentNativeRsp =
        reinterpret_cast<uintptr_t>(_AddressOfReturnAddress());
    d.c011ec18TransitionFrameAddress = frameAddress;
    d.c011ec18TransitionFrameRip = frame != nullptr
        ? reinterpret_cast<uintptr_t>(frame->m_RIP) : 0u;
    d.c011ec18TransitionFrameRbp = frame != nullptr
        ? reinterpret_cast<uintptr_t>(frame->m_FramePointer) : 0u;
    d.c011ec18TransitionFrameThreadField = frame != nullptr
        ? reinterpret_cast<uintptr_t>(frame->m_pThread) : 0u;
    d.c011ec18ThreadAddress = threadAddress;
    d.c011ec18PreviousTransitionFrame = 0u;
    d.c011ec18TransitionFrameFlags = frame != nullptr
        ? static_cast<uintptr_t>(frame->m_Flags) : 0u;
    d.c011ec18SavedRbx = 0u;
    d.c011ec18SavedRsi = 0u;
    d.c011ec18SavedRdi = 0u;
    d.c011ec18SavedR12 = 0u;
    d.c011ec18SavedR13 = 0u;
    d.c011ec18SavedR14 = 0u;
    d.c011ec18SavedR15 = 0u;
    d.c011ec18SavedRsp = 0u;
    if (threadAddress != 0u) {
        RuntimeThreadLocals* locals =
            reinterpret_cast<RuntimeThreadLocals*>(threadAddress);
        d.c011ec18PreviousTransitionFrame =
            reinterpret_cast<uintptr_t>(locals->m_pTransitionFrame);
    }
    if (frame != nullptr) {
        const uintptr_t* saved =
            reinterpret_cast<const uintptr_t*>(frame->m_PreservedRegs);
        const uintptr_t savedPreservedMask =
            PTFF_SAVE_RBX | PTFF_SAVE_RSI | PTFF_SAVE_RDI |
            PTFF_SAVE_R12 | PTFF_SAVE_R13 | PTFF_SAVE_R14 | PTFF_SAVE_R15;
        if ((d.c011ec18TransitionFrameFlags & savedPreservedMask) == savedPreservedMask) {
            d.c011ec18SavedRbx = saved[0];
            d.c011ec18SavedRsi = saved[1];
            d.c011ec18SavedRdi = saved[2];
            d.c011ec18SavedR12 = saved[3];
            d.c011ec18SavedR13 = saved[4];
            d.c011ec18SavedR14 = saved[5];
            d.c011ec18SavedR15 = saved[6];
        }
        if ((d.c011ec18TransitionFrameFlags & PTFF_SAVE_RSP) != 0u) {
            d.c011ec18SavedRsp = saved[7];
        }
    }

    RuntimeInstance* runtime = GetRuntimeInstance();
    ICodeManager* nativeManager = nullptr;
    ICodeManager* transitionManager = nullptr;
    const bool transitionInRange = c011ec18IsInsideManagedRange(
        d.c011ec18TransitionFrameRip, runtime, &transitionManager);
    (void)c011ec18IsInsideManagedRange(
        d.c011ec18CurrentNativeRip, runtime, &nativeManager);
    d.c011ec18CurrentNativeCodeManager =
        reinterpret_cast<uintptr_t>(nativeManager);
    d.c011ec18CurrentNativeInManagedRange =
        c011ec18IsInsideManagedRange(
            d.c011ec18CurrentNativeRip, runtime, nullptr) ? 1u : 0u;
    d.c011ec18TransitionCodeManager =
        reinterpret_cast<uintptr_t>(transitionManager);
    d.c011ec18TransitionInManagedRange = transitionInRange ? 1u : 0u;
    d.c011ec18AuthenticManagedCodeManager =
        reinterpret_cast<uintptr_t>(transitionManager);

}

static void emitC011EC18Preflight() {
    const guidexos_nativeaot_allocation_diagnostics& d =
        g_guideXosAllocationDiagnostics;
    if (d.c011ec18TransitionFrameAddress == 0u) {
        return;
    }
    suspendEeSerialPutString("[nativeaot-code-manager] C011EC18-PREFLIGHT frame=");
    suspendEeSerialPutHex64(d.c011ec18TransitionFrameAddress);
    suspendEeSerialPutString(" currentRIP=");
    suspendEeSerialPutHex64(d.c011ec18CurrentNativeRip);
    suspendEeSerialPutString(" currentReturnSlot=");
    suspendEeSerialPutHex64(d.c011ec18CurrentNativeRsp);
    suspendEeSerialPutString(" transitionRIP=");
    suspendEeSerialPutHex64(d.c011ec18TransitionFrameRip);
    suspendEeSerialPutString(" transitionRBP=");
    suspendEeSerialPutHex64(d.c011ec18TransitionFrameRbp);
    suspendEeSerialPutString(" frameThread=");
    suspendEeSerialPutHex64(d.c011ec18TransitionFrameThreadField);
    suspendEeSerialPutString(" thread=");
    suspendEeSerialPutHex64(d.c011ec18ThreadAddress);
    suspendEeSerialPutString(" previousFrame=");
    suspendEeSerialPutHex64(d.c011ec18PreviousTransitionFrame);
    suspendEeSerialPutString(" flags=");
    suspendEeSerialPutHex64(d.c011ec18TransitionFrameFlags);
    suspendEeSerialPutString(" savedRSP=");
    suspendEeSerialPutHex64(d.c011ec18SavedRsp);
    suspendEeSerialPutString(" savedRBX=");
    suspendEeSerialPutHex64(d.c011ec18SavedRbx);
    suspendEeSerialPutString(" savedRSI=");
    suspendEeSerialPutHex64(d.c011ec18SavedRsi);
    suspendEeSerialPutString(" savedRDI=");
    suspendEeSerialPutHex64(d.c011ec18SavedRdi);
    suspendEeSerialPutString(" savedR12=");
    suspendEeSerialPutHex64(d.c011ec18SavedR12);
    suspendEeSerialPutString(" savedR13=");
    suspendEeSerialPutHex64(d.c011ec18SavedR13);
    suspendEeSerialPutString(" savedR14=");
    suspendEeSerialPutHex64(d.c011ec18SavedR14);
    suspendEeSerialPutString(" savedR15=");
    suspendEeSerialPutHex64(d.c011ec18SavedR15);
    suspendEeSerialPutString(" savedRBP=");
    suspendEeSerialPutHex64(d.c011ec18TransitionFrameRbp);
    suspendEeSerialPutString(" nativeManager=");
    suspendEeSerialPutHex64(d.c011ec18CurrentNativeCodeManager);
    suspendEeSerialPutString(" currentNativeInRange=");
    suspendEeSerialPutHex32(d.c011ec18CurrentNativeInManagedRange);
    suspendEeSerialPutString(" transitionManager=");
    suspendEeSerialPutHex64(d.c011ec18TransitionCodeManager);
    suspendEeSerialPutString(" transitionInRange=");
    suspendEeSerialPutHex32(d.c011ec18TransitionInManagedRange);
    suspendEeSerialPutString(" marker=C011EC18-PREFLIGHT\n");
}

extern "C" void __cdecl
guideXosNativeAotC011EC18IteratorInitial(
    uintptr_t frameAddress, uintptr_t controlPc, uintptr_t sp,
    uintptr_t fp, uintptr_t flags) {
    guidexos_nativeaot_allocation_diagnostics& d =
        g_guideXosAllocationDiagnostics;
    ++d.c011ec18IteratorInitialCount;
    ++d.c011ec18StackFrameCount;
    d.c011ec18IteratorControlPc = controlPc;
    d.c011ec18IteratorInitialSp = sp;
    d.c011ec18IteratorInitialFp = fp;
    d.c011ec18TransitionFrameFlags = flags;
    if (frameAddress != d.c011ec18TransitionFrameAddress ||
        controlPc != d.c011ec18TransitionFrameRip ||
        (((d.c011ec18TransitionFrameFlags & PTFF_SAVE_RSP) != 0u) &&
         sp != d.c011ec18SavedRsp) ||
        fp != d.c011ec18TransitionFrameRbp) {
        d.c011ec18FailFastReason = 0xEC1801u;
    }
    suspendEeSerialPutString("[nativeaot-code-manager] C011EC18 iterator-initial controlPC=");
    suspendEeSerialPutHex64(controlPc);
    suspendEeSerialPutString(" SP=");
    suspendEeSerialPutHex64(sp);
    suspendEeSerialPutString(" FP=");
    suspendEeSerialPutHex64(fp);
    suspendEeSerialPutString(" frame=");
    suspendEeSerialPutHex64(frameAddress);
    suspendEeSerialPutString(" flags=");
    suspendEeSerialPutHex64(flags);
    suspendEeSerialPutString(" marker=C011EC18-ITERATOR\n");
}

extern "C" void __cdecl
guideXosNativeAotC011EC18IteratorCodeManagerLookup(
    uintptr_t controlPc, uintptr_t sp, uintptr_t fp, uintptr_t manager) {
    guidexos_nativeaot_allocation_diagnostics& d =
        g_guideXosAllocationDiagnostics;
    ++d.c011ec18LookupCount;
    d.c011ec18IteratorControlPc = controlPc;
    d.c011ec18IteratorInitialSp = sp;
    d.c011ec18IteratorInitialFp = fp;
    d.c011ec18IteratorCodeManager = manager;
    suspendEeSerialPutString("[nativeaot-code-manager] C011EC18 lookup controlPC=");
    suspendEeSerialPutHex64(controlPc);
    suspendEeSerialPutString(" manager=");
    suspendEeSerialPutHex64(manager);
    suspendEeSerialPutString(" marker=C011EC18-LOOKUP\n");
}

extern "C" void __cdecl
guideXosNativeAotC011EC18IteratorFindMethodInfo(
    uintptr_t controlPc, uintptr_t methodInfo, uintptr_t found) {
    guidexos_nativeaot_allocation_diagnostics& d =
        g_guideXosAllocationDiagnostics;
    ++d.c011ec18FindMethodInfoAttemptCount;
    d.c011ec18IteratorControlPc = controlPc;
    d.c011ec18IteratorMethodInfo = methodInfo;
    if (found != 0u) {
        ++d.c011ec18FindMethodInfoSuccessCount;
        d.c011ec18MethodMetadataValid = methodInfo != 0u ? 1u : 0u;
        emitC011EC18Marker();
#if defined(GUIDEXOS_NATIVEAOT_C011EC23_NATIVE_UNWIND)
        if (d.c011ec23ManagedReentryFound != 0u &&
            controlPc == d.c011ec23OutputRip && methodInfo != 0u) {
            d.c011ec23CallerFindMethodInfoAttempts = 1u;
            d.c011ec23CallerFindMethodInfoSuccess = 1u;
            d.c011ec23CallerMethodInfo = methodInfo;
            d.c011ec23UnwindResult = 2u;
            d.c011ec23Outcome = 2u;
            guideXosNativeAotC011EC23SafeStop(0xC0230002u);
        }
#endif
    }
    suspendEeSerialPutString("[nativeaot-code-manager] C011EC18 FindMethodInfo controlPC=");
    suspendEeSerialPutHex64(controlPc);
    suspendEeSerialPutString(" methodInfo=");
    suspendEeSerialPutHex64(methodInfo);
    suspendEeSerialPutString(" result=");
    suspendEeSerialPutHex32(found != 0u ? 1u : 0u);
    suspendEeSerialPutString(" marker=C011EC18-FIND-METHOD\n");
}

extern "C" void __cdecl
guideXosNativeAotC011EC18IteratorFramePointer(uintptr_t framePointer) {
    guidexos_nativeaot_allocation_diagnostics& d =
        g_guideXosAllocationDiagnostics;
    ++d.c011ec18FramePointerCalculationCount;
    d.c011ec18IteratorFramePointer = framePointer;
}

extern "C" void __cdecl
guideXosNativeAotC011EC18IteratorUnwind(
    uintptr_t controlPc, uintptr_t sp) {
    guidexos_nativeaot_allocation_diagnostics& d =
        g_guideXosAllocationDiagnostics;
    ++d.c011ec18UnwindStepCount;
    d.c011ec18IteratorUnwindControlPc = controlPc;
    d.c011ec18IteratorInitialSp = sp;
}
#endif

extern "C" void __cdecl
guideXosNativeAotC011EC15GcScanRootsEntered(
    int condemned, int maxGeneration, uintptr_t scanContext) {
    (void)condemned;
    (void)maxGeneration;
    (void)scanContext;
    guidexos_nativeaot_allocation_diagnostics& d =
        g_guideXosAllocationDiagnostics;
    ++d.c011ec15GcScanRootsRequestCount;
    ++d.c011ec15ProviderRequestCount;
#if defined(GUIDEXOS_NATIVEAOT_C011EC17_CODE_MANAGER) || defined(GUIDEXOS_NATIVEAOT_C011EC18_NATIVE_RHP_NEW_ARRAY)
    emitC011EC17StackWalkPreflight();
#if defined(GUIDEXOS_NATIVEAOT_C011EC18_NATIVE_RHP_NEW_ARRAY)
    emitC011EC18Preflight();
#if defined(GUIDEXOS_NATIVEAOT_C011EC19_UNWIND_GC_INFO)
    suspendEeSerialPutString(
        "[nativeaot-gc-unwind-gc-info] preflight marker=C011EC19-PREFLIGHT\n");
#if defined(GUIDEXOS_NATIVEAOT_C011EC20_UNWIND)
#if defined(GUIDEXOS_NATIVEAOT_C011EC21_NATIVE_CONTINUATION)
#if defined(GUIDEXOS_NATIVEAOT_C011EC23_NATIVE_UNWIND)
    suspendEeSerialPutString(
        "[nativeaot-gc-native-unwind] preflight marker=C011EC23-PREFLIGHT\n");
#else
    suspendEeSerialPutString(
        "[nativeaot-gc-native-transition] preflight marker=C011EC21-PREFLIGHT\n");
#endif
#else
    suspendEeSerialPutString(
        "[nativeaot-gc-unwind-caller-frame] preflight marker=C011EC20-PREFLIGHT\n");
#endif
#endif
#endif
#endif
#endif
#if defined(GUIDEXOS_NATIVEAOT_STACK_PROVIDER_TRANSITION_FAILFAST_MINIMAL)
    suspendEeSerialPutString(
        "[nativeaot-gc-stack-provider-transition-failfast] GcScanRoots-entry sentinel=");
    suspendEeSerialPutHex64(d.threadStaticProofSentinelAddress);
    suspendEeSerialPutString(" storageObject=");
    suspendEeSerialPutHex64(d.runtimeThreadStaticStorageObjectAddress);
    suspendEeSerialPutString("\n");
#else
    suspendEeSerialPutString(
        "[nativeaot-gc-next-genuine-root-provider] GcScanRoots entry\n");
#endif
}

extern "C" void __cdecl
guideXosNativeAotC011EC15ProviderEntered(
    uint32_t category, uintptr_t thread, uintptr_t provider) {
    guidexos_nativeaot_allocation_diagnostics& d =
        g_guideXosAllocationDiagnostics;
    ++d.c011ec15ProviderEntryCount;
    d.c011ec15ProviderCategory = category;
    d.c011ec15ProviderContinuationCategory = category;
    d.c011ec15CurrentProvider = provider;
#if defined(GUIDEXOS_NATIVEAOT_C011EC18_NATIVE_RHP_NEW_ARRAY)
    if (category == kC011EC15ProviderThreadStack) {
        ++d.c011ec18StackProviderCallbackCount;
    }
#endif
#if defined(GUIDEXOS_NATIVEAOT_C011EC26_STACK_COMPLETION)
    if (category == kC011EC15ProviderThreadStack) {
        ++d.c011ec26StackProviderCallbackEntryCount;
    }
#endif
    d.c011ec15FirstRootProvider =
        d.c011ec15FirstRootProvider == 0u ? provider : d.c011ec15FirstRootProvider;
#if defined(GUIDEXOS_NATIVEAOT_STACK_PROVIDER_TRANSITION_FAILFAST_MINIMAL)
    if (category == kC011EC15ProviderOrdinaryThreadStatic) {
        suspendEeSerialPutString(
            "[nativeaot-gc-stack-provider-transition-failfast] ordinary-provider-entered\n");
    } else if (category == kC011EC15ProviderThreadStack) {
        suspendEeSerialPutString(
            "[nativeaot-gc-stack-provider-transition-failfast] stack-provider-transition-start\n");
    }
#endif
    (void)thread;
}

extern "C" void __cdecl
guideXosNativeAotC011EC15CandidateObserved(
    uintptr_t slot, uintptr_t rawValue, uint32_t flags,
    uintptr_t callback, uintptr_t context) {
    guidexos_nativeaot_allocation_diagnostics& d =
        g_guideXosAllocationDiagnostics;
    ++d.c011ec15RootSlotVisitCount;
#if defined(GUIDEXOS_NATIVEAOT_C011EC19_UNWIND_GC_INFO)
    if (d.c011ec19FirstRootSlot == slot) {
        d.c011ec19FirstRootValue = rawValue;
    }
    if (d.c011ec19FirstStackRootSlot == slot) {
        d.c011ec19FirstStackRootValue = rawValue;
    }
#endif
#if defined(GUIDEXOS_NATIVEAOT_C011EC18_NATIVE_RHP_NEW_ARRAY)
    if (d.c011ec15ProviderCategory == kC011EC15ProviderThreadStack) {
        ++d.c011ec18StackRootSlotCount;
    }
#endif
    if (rawValue == 0u) {
        ++d.c011ec15NullCandidateCount;
#if defined(GUIDEXOS_NATIVEAOT_NEXT_GENUINE_ROOT_PROVIDER_ALLOCATION)
        suspendEeSerialPutString(
            "[nativeaot-gc-next-genuine-root-provider] candidate null slot=");
        suspendEeSerialPutHex64(slot);
        suspendEeSerialPutString(" raw=");
        suspendEeSerialPutHex64(rawValue);
        suspendEeSerialPutString(" provider=");
        suspendEeSerialPutHex64(d.c011ec15CurrentProvider);
        suspendEeSerialPutString(" category=");
        suspendEeSerialPutHex32(d.c011ec15ProviderCategory);
        suspendEeSerialPutString("\n");
#endif
        return;
    }
    ++d.c011ec15NonNullCandidateCount;
#if defined(GUIDEXOS_NATIVEAOT_NEXT_GENUINE_ROOT_PROVIDER_ALLOCATION)
    suspendEeSerialPutString(
        "[nativeaot-gc-next-genuine-root-provider] candidate non-null slot=");
    suspendEeSerialPutHex64(slot);
    suspendEeSerialPutString(" raw=");
    suspendEeSerialPutHex64(rawValue);
    suspendEeSerialPutString(" provider=");
    suspendEeSerialPutHex64(d.c011ec15CurrentProvider);
    suspendEeSerialPutString(" category=");
    suspendEeSerialPutHex32(d.c011ec15ProviderCategory);
    suspendEeSerialPutString(" callback=");
    suspendEeSerialPutHex64(callback);
    suspendEeSerialPutString(" context=");
    suspendEeSerialPutHex64(context);
    suspendEeSerialPutString(" flags=");
    suspendEeSerialPutHex32(flags);
    suspendEeSerialPutString("\n");
#endif
    if (d.c011ec15FirstRootCallbackReturnCount == 0u) {
        d.c011ec15FirstRootSlot = slot;
        d.c011ec15FirstRootValue = rawValue;
        d.c011ec15FirstRootCallback = callback;
        d.c011ec15FirstRootContext = context;
        d.c011ec15FirstRootProviderCategory = d.c011ec15ProviderCategory;
        d.c011ec15CallbackFlags = flags;
        d.c011ec15CallbackContextValid = context != 0u ? 1u : 0u;
        return;
    }
    d.c011ec15NextRootSlot = slot;
    d.c011ec15NextRootValue = rawValue;
    d.c011ec15NextRootProvider = d.c011ec15CurrentProvider;
    d.c011ec15NextRootCallback = callback;
    d.c011ec15NextRootContext = context;
    if (callback == 0u || context == 0u) {
        c011ec15SafeStop(0xEC1501u);
    }
#if !defined(GUIDEXOS_NATIVEAOT_C011EC19_UNWIND_GC_INFO)
    c011ec15SafeStop(0xC011EC15u);
#endif
}

extern "C" void __cdecl
guideXosNativeAotC011EC15PromoteEntered(
    uintptr_t rawArg1, uintptr_t rawArg2, uintptr_t rawArg3,
    uintptr_t callbackAddress, uintptr_t returnAddress,
    uintptr_t stackPointer) {
    guidexos_nativeaot_allocation_diagnostics& d =
        g_guideXosAllocationDiagnostics;
    if (rawArg1 == 0u) {
        c011ec15SafeStop(0xEC1506u);
    }
    const uintptr_t rawValue = *reinterpret_cast<uintptr_t*>(rawArg1);
#if defined(GUIDEXOS_NATIVEAOT_NEXT_GENUINE_ROOT_PROVIDER_ALLOCATION)
    suspendEeSerialPutString(
        rawValue == 0u
            ? "[nativeaot-gc-next-genuine-root-provider] Promote null\n"
            : "[nativeaot-gc-next-genuine-root-provider] Promote non-null\n");
#endif
    if (d.c011ec15FirstRootCallbackReturnCount != 0u && rawValue != 0u) {
        ++d.c011ec15SecondPromoteAttemptCount;
        ++d.c011ec15SecondPromoteEntryCount;
#if !defined(GUIDEXOS_NATIVEAOT_C011EC19_UNWIND_GC_INFO)
        c011ec15SafeStop(0xEC1507u);
#endif
    }
    if (d.c011ec15ProviderCategory == kC011EC15ProviderThreadStack) {
#if defined(GUIDEXOS_NATIVEAOT_C011EC19_UNWIND_GC_INFO)
        ++d.c011ec19FirstStackDerivedPromoteAttemptCount;
        ++d.c011ec19FirstStackDerivedPromoteEntryCount;
#endif
    }
    ++d.c011ec15PromoteEntryCount;
    d.c011ec15CallbackFlags = static_cast<uint32_t>(rawArg3);
    d.c011ec15CallbackContextValid = rawArg2 != 0u ? 1u : 0u;
    (void)callbackAddress;
    (void)returnAddress;
    (void)stackPointer;
}

extern "C" void __cdecl
guideXosNativeAotC011EC15PromoteCandidateLoaded(uintptr_t object) {
    // The source-valid candidate load is observed by GcEnumObject before the
    // callback.  Null callbacks are part of normal root enumeration and must
    // return so the next provider can be reached.
    (void)object;
}

extern "C" void __cdecl
guideXosNativeAotC011EC15QueueMarkReturned(
    uintptr_t object, uintptr_t slot, uintptr_t oldValue,
    uintptr_t newValue, uintptr_t slotIndex, uintptr_t cursorBefore,
    uintptr_t cursorAfter, uintptr_t queueBase) {
    guidexos_nativeaot_allocation_diagnostics& d =
        g_guideXosAllocationDiagnostics;
    ++d.c011ec15QueueMarkReturnCount;
    if (d.c011ec15QueueMarkReturnCount == 1u) {
        d.c011ec15FirstQueueSlot = slot;
        d.c011ec15FirstQueueSlotIndex = slotIndex;
        d.c011ec15FirstQueueOldValue = oldValue;
        d.c011ec15FirstQueueNewValue = newValue;
        d.c011ec15FirstQueueCursorBefore = cursorBefore;
        d.c011ec15FirstQueueCursorAfter = cursorAfter;
        d.c011ec15FirstQueueBase = queueBase;
    }
#if defined(GUIDEXOS_NATIVEAOT_C011EC19_UNWIND_GC_INFO)
    if (d.c011ec15QueueMarkReturnCount > 1u) {
        ++d.c011ec19SecondQueueInsertionCount;
        d.c011ec19SecondQueueSlot = slot;
        d.c011ec19SecondQueueCursorBefore = cursorBefore;
        d.c011ec19SecondQueueCursorAfter = cursorAfter;
        d.c011ec19SecondQueueOldValue = oldValue;
        d.c011ec19SecondQueueNewValue = newValue;
        return;
    }
#endif
    if (d.c011ec15QueueMarkReturnCount != 1u ||
        d.c011ec15FirstRootValue != object || oldValue != 0u ||
        newValue != object || slotIndex != 0u || cursorBefore != 0u ||
        cursorAfter != 1u || slot != queueBase) {
        c011ec15SafeStop(0xEC1502u);
    }
}

extern "C" void __cdecl
guideXosNativeAotC011EC15MarkHelperReturned(uintptr_t object) {
    guidexos_nativeaot_allocation_diagnostics& d =
        g_guideXosAllocationDiagnostics;
    ++d.c011ec15MarkHelperReturnCount;
#if defined(GUIDEXOS_NATIVEAOT_C011EC19_UNWIND_GC_INFO)
    if (d.c011ec15ProviderCategory == kC011EC15ProviderThreadStack) {
        return;
    }
#endif
    if (object != d.c011ec15FirstRootValue) {
        c011ec15SafeStop(0xEC1503u);
    }
}

extern "C" void __cdecl
guideXosNativeAotC011EC15PromoteReturned(uintptr_t object) {
    guidexos_nativeaot_allocation_diagnostics& d =
        g_guideXosAllocationDiagnostics;
    ++d.c011ec15PromoteReturnCount;
#if defined(GUIDEXOS_NATIVEAOT_C011EC19_UNWIND_GC_INFO)
    if (d.c011ec15ProviderCategory == kC011EC15ProviderThreadStack) {
        ++d.c011ec19FirstStackDerivedPromoteReturnCount;
        return;
    }
#endif
    if (object != d.c011ec15FirstRootValue) {
        c011ec15SafeStop(0xEC1504u);
    }
}

extern "C" void __cdecl
guideXosNativeAotC011EC15EnumGcRefReturned(
    uintptr_t slot, uintptr_t rawValue, uintptr_t callback, uintptr_t context) {
    guidexos_nativeaot_allocation_diagnostics& d =
        g_guideXosAllocationDiagnostics;
    ++d.c011ec15EnumGcRefContinuationCount;
#if defined(GUIDEXOS_NATIVEAOT_STACK_PROVIDER_TRANSITION_FAILFAST_MINIMAL)
    if (slot == 0u && rawValue == 0u &&
        d.c011ec15ProviderCategory == kC011EC15ProviderOrdinaryThreadStatic) {
        suspendEeSerialPutString(
            "[nativeaot-gc-stack-provider-transition-failfast] ordinary-provider-returned-null\n");
    }
#endif
#if defined(GUIDEXOS_NATIVEAOT_NEXT_GENUINE_ROOT_PROVIDER_ALLOCATION)
    if (slot != 0u) {
        suspendEeSerialPutString(
            rawValue == 0u
                ? "[nativeaot-gc-next-genuine-root-provider] EnumGcRef returned null slot\n"
                : "[nativeaot-gc-next-genuine-root-provider] EnumGcRef returned non-null slot\n");
    }
#endif
    if (rawValue != 0u && d.c011ec15FirstRootCallbackReturnCount == 0u) {
        ++d.c011ec15FirstRootCallbackReturnCount;
        if (slot != d.c011ec15FirstRootSlot || rawValue != d.c011ec15FirstRootValue ||
            callback != d.c011ec15FirstRootCallback ||
            context != d.c011ec15FirstRootContext) {
            c011ec15SafeStop(0xEC1505u);
        }
        d.c011ec15ThreadStoreLockHeld =
            d.threadStoreLockRecursionDepth == 1u ? 1u : 0u;
        d.c011ec15EeSuspended = d.eeSuspended;
        d.c011ec15ManagedEntryProhibited = d.managedEntryProhibited;
    }
}

#if defined(GUIDEXOS_NATIVEAOT_C011EC19_UNWIND_GC_INFO)
extern "C" void __cdecl
guideXosNativeAotC011EC19GcRootReported(
    uintptr_t slot, uint32_t flags, uint32_t rootKind, uintptr_t registerSlot) {
    guidexos_nativeaot_allocation_diagnostics& d =
        g_guideXosAllocationDiagnostics;
    ++d.c011ec19RootReportCount;
    if (rootKind == 1u) {
        ++d.c011ec19RegisterRootCount;
    } else {
        ++d.c011ec19StackRootCount;
    }
    if (d.c011ec19FirstRootSlot == 0u) {
        d.c011ec19FirstRootSlot = slot;
        d.c011ec19FirstRootRegisterSlot = registerSlot;
        d.c011ec19FirstRootKind = rootKind;
    }
    if (rootKind == 2u && d.c011ec19FirstStackRootSlot == 0u) {
        d.c011ec19FirstStackRootSlot = slot;
    }
    (void)flags;
}

extern "C" void __cdecl
guideXosNativeAotC011EC19GcInfoLookup(
    uintptr_t methodInfo, uintptr_t safePoint, uintptr_t gcInfo,
    uintptr_t codeOffset, uintptr_t unwindInfo, uintptr_t unwindInfoSize,
    uintptr_t blockFlags, uintptr_t methodStart, uintptr_t methodEnd) {
    guidexos_nativeaot_allocation_diagnostics& d =
        g_guideXosAllocationDiagnostics;
    ++d.c011ec19GcInfoLookupCount;
    d.c011ec19MethodInfo = methodInfo;
    d.c011ec19SafePointAddress = safePoint;
    d.c011ec19GcInfo = gcInfo;
    d.c011ec19CodeOffset = codeOffset;
    d.c011ec19UnwindInfo = unwindInfo;
    d.c011ec19UnwindInfoSize = unwindInfoSize;
    d.c011ec19UnwindBlockFlags = blockFlags;
    d.c011ec19MethodStart = methodStart;
    d.c011ec19MethodEnd = methodEnd;
}

extern "C" void __cdecl
guideXosNativeAotC011EC19GcInfoDecodeStarted(
    uintptr_t gcInfo, uintptr_t codeOffset, uint32_t activeFrame) {
    guidexos_nativeaot_allocation_diagnostics& d =
        g_guideXosAllocationDiagnostics;
    ++d.c011ec19GcInfoDecodeAttemptCount;
    d.c011ec19GcInfo = gcInfo;
    d.c011ec19CodeOffset = codeOffset;
    (void)activeFrame;
}

extern "C" void __cdecl
guideXosNativeAotC011EC19GcInfoInterruptibility(
    uint32_t interruptible, uint32_t hasRanges) {
    guidexos_nativeaot_allocation_diagnostics& d =
        g_guideXosAllocationDiagnostics;
    d.c011ec19Interruptible = interruptible;
    d.c011ec19HasInterruptibleRanges = hasRanges;
}

extern "C" void __cdecl
guideXosNativeAotC011EC19GcInfoDecodeCompleted(uint32_t result) {
    g_guideXosAllocationDiagnostics.c011ec19GcInfoDecodeResult = result;
}

extern "C" void __cdecl
guideXosNativeAotC011EC19UnwindEntered(
    uintptr_t methodInfo, uintptr_t controlPc, uintptr_t sp, uintptr_t fp,
    uintptr_t runtimeFunction, uintptr_t mainRuntimeFunction) {
    guidexos_nativeaot_allocation_diagnostics& d =
        g_guideXosAllocationDiagnostics;
    ++d.c011ec19UnwindEntryCount;
    d.c011ec19MethodInfo = methodInfo;
    d.c011ec19ControlPc = controlPc;
    d.c011ec19InputSp = sp;
    d.c011ec19InputFp = fp;
    d.c011ec19RuntimeFunction = runtimeFunction;
    d.c011ec19MainRuntimeFunction = mainRuntimeFunction;
}

extern "C" void __cdecl
guideXosNativeAotC011EC19UnwindMetadata(
    uintptr_t unwindInfo, uintptr_t unwindInfoSize, uintptr_t blockFlags,
    uintptr_t methodStart, uintptr_t methodEnd, uintptr_t ehInfo) {
    guidexos_nativeaot_allocation_diagnostics& d =
        g_guideXosAllocationDiagnostics;
    ++d.c011ec19UnwindMetadataCount;
    d.c011ec19UnwindInfo = unwindInfo;
    d.c011ec19UnwindInfoSize = unwindInfoSize;
    d.c011ec19UnwindBlockFlags = blockFlags;
    d.c011ec19MethodStart = methodStart;
    d.c011ec19MethodEnd = methodEnd;
    d.c011ec19EhInfo = ehInfo;
}

extern "C" void __cdecl
guideXosNativeAotC011EC19UnwindCompleted(
    uint32_t result, uint32_t rtlVirtualUnwindCalled,
    uintptr_t callerPc, uintptr_t callerSp, uintptr_t callerFp,
    uintptr_t previousTransitionFrame, uint32_t preservedRegisters) {
    guidexos_nativeaot_allocation_diagnostics& d =
        g_guideXosAllocationDiagnostics;
    ++d.c011ec19UnwindCompletedCount;
    d.c011ec19UnwindResult = result;
    d.c011ec19UnwindRtlCount += rtlVirtualUnwindCalled;
    d.c011ec19CallerControlPc = callerPc;
    d.c011ec19CallerSp = callerSp;
    d.c011ec19CallerFp = callerFp;
    d.c011ec19PreviousTransitionFrame = previousTransitionFrame;
    d.c011ec19PreservedRegisterCount = preservedRegisters;
}

static void emitC011EC19SafeStop() {
    const guidexos_nativeaot_allocation_diagnostics& d =
        g_guideXosAllocationDiagnostics;
    suspendEeSerialPutString(
        "[nativeaot-gc-unwind-gc-info] SAFE_STOP marker=C011EC19");
#define C19_HEX32(name, value) \
    suspendEeSerialPutString(" " name "="); suspendEeSerialPutHex32(value)
#define C19_HEX64(name, value) \
    suspendEeSerialPutString(" " name "="); suspendEeSerialPutHex64(value)
    C19_HEX64("initialControlPC", d.c011ec18IteratorControlPc);
    C19_HEX64("initialSP", d.c011ec18IteratorInitialSp);
    C19_HEX64("initialFP", d.c011ec18IteratorInitialFp);
    C19_HEX32("findMethodInfoAttempts", d.c011ec18FindMethodInfoAttemptCount);
    C19_HEX32("findMethodInfoResults", d.c011ec18FindMethodInfoSuccessCount);
    C19_HEX64("methodInfo", d.c011ec19MethodInfo);
    C19_HEX64("methodStart", d.c011ec19MethodStart);
    C19_HEX64("methodEnd", d.c011ec19MethodEnd);
    C19_HEX64("unwindInfo", d.c011ec19UnwindInfo);
    C19_HEX64("unwindInfoSize", d.c011ec19UnwindInfoSize);
    C19_HEX64("unwindBlockFlags", d.c011ec19UnwindBlockFlags);
    C19_HEX32("unwindEntries", d.c011ec19UnwindEntryCount);
    C19_HEX32("unwindMetadata", d.c011ec19UnwindMetadataCount);
    C19_HEX32("unwindRtl", d.c011ec19UnwindRtlCount);
    C19_HEX32("unwindCompleted", d.c011ec19UnwindCompletedCount);
    C19_HEX32("unwindResult", d.c011ec19UnwindResult);
    C19_HEX64("callerControlPC", d.c011ec19CallerControlPc);
    C19_HEX64("callerSP", d.c011ec19CallerSp);
    C19_HEX64("callerFP", d.c011ec19CallerFp);
    C19_HEX64("previousTransitionFrame", d.c011ec19PreviousTransitionFrame);
    C19_HEX32("preservedRegisters", d.c011ec19PreservedRegisterCount);
    C19_HEX32("gcInfoLookups", d.c011ec19GcInfoLookupCount);
    C19_HEX64("gcInfo", d.c011ec19GcInfo);
    C19_HEX64("safePoint", d.c011ec19SafePointAddress);
    C19_HEX64("codeOffset", d.c011ec19CodeOffset);
    C19_HEX32("gcInfoDecodeAttempts", d.c011ec19GcInfoDecodeAttemptCount);
    C19_HEX32("gcInfoDecodeResult", d.c011ec19GcInfoDecodeResult);
    C19_HEX32("interruptible", d.c011ec19Interruptible);
    C19_HEX32("interruptibleRanges", d.c011ec19HasInterruptibleRanges);
    C19_HEX32("rootReports", d.c011ec19RootReportCount);
    C19_HEX32("registerRoots", d.c011ec19RegisterRootCount);
    C19_HEX32("stackRoots", d.c011ec19StackRootCount);
    C19_HEX32("firstRootKind", d.c011ec19FirstRootKind);
    C19_HEX64("firstRootSlot", d.c011ec19FirstRootSlot);
    C19_HEX64("firstRootValue", d.c011ec19FirstRootValue);
    C19_HEX64("firstStackRootSlot", d.c011ec19FirstStackRootSlot);
    C19_HEX64("firstStackRootValue", d.c011ec19FirstStackRootValue);
    C19_HEX32("firstStackPromoteAttempts", d.c011ec19FirstStackDerivedPromoteAttemptCount);
    C19_HEX32("firstStackPromoteEntries", d.c011ec19FirstStackDerivedPromoteEntryCount);
    C19_HEX32("firstStackPromoteReturns", d.c011ec19FirstStackDerivedPromoteReturnCount);
    C19_HEX32("legacySecondPromoteAttempts", d.c011ec15SecondPromoteAttemptCount);
    C19_HEX32("legacySecondPromoteEntries", d.c011ec15SecondPromoteEntryCount);
    C19_HEX32("secondQueueInsertions", d.c011ec19SecondQueueInsertionCount);
    C19_HEX64("secondQueueSlot", d.c011ec19SecondQueueSlot);
    C19_HEX64("secondQueueCursorBefore", d.c011ec19SecondQueueCursorBefore);
    C19_HEX64("secondQueueCursorAfter", d.c011ec19SecondQueueCursorAfter);
    C19_HEX64("secondQueueOld", d.c011ec19SecondQueueOldValue);
    C19_HEX64("secondQueueNew", d.c011ec19SecondQueueNewValue);
    C19_HEX64("firstQueueSlot", d.c011ec15FirstQueueSlot);
    C19_HEX64("firstQueueCursorBefore", d.c011ec15FirstQueueCursorBefore);
    C19_HEX64("firstQueueCursorAfter", d.c011ec15FirstQueueCursorAfter);
    C19_HEX64("firstQueueNew", d.c011ec15FirstQueueNewValue);
    C19_HEX64("sentinel", d.threadStaticProofSentinelAddress);
    C19_HEX64("storageObject", d.runtimeThreadStaticStorageObjectAddress);
    C19_HEX32("stackFrames", d.c011ec18StackFrameCount);
    C19_HEX32("stackProviderCallbacks", d.c011ec18StackProviderCallbackCount);
    C19_HEX32("totalRoots", d.c011ec15RootSlotVisitCount);
    C19_HEX32("threadStackRoots", d.c011ec18StackRootSlotCount);
    C19_HEX32("markWrites", d.c011ec15MarkBitWriteCount);
    C19_HEX32("childReads", d.c011ec15ChildReferenceReadCount);
    C19_HEX32("graphTraversal", d.c011ec15GraphTraversalCount);
    C19_HEX64("stackBase", d.rootThreadRecords[0].stackLow);
    C19_HEX64("stackLimit", d.rootThreadRecords[0].stackHigh);
    C19_HEX64("scanContextStackLimit", d.callbackContextStackLimit);
    C19_HEX32("stackBoundsConsumed", d.c011ec18StackBoundsConsumed);
    C19_HEX32("promoteEntries", d.c011ec15PromoteEntryCount);
    C19_HEX32("promoteReturns", d.c011ec15PromoteReturnCount);
    C19_HEX32("restart", d.restartRequestCount + d.restartEntryCount);
    C19_HEX32("resume", d.managedResumeCount);
    C19_HEX64("currentThread", d.rootCurrentThreadIdentity);
    C19_HEX64("enumeratedThread", d.rootEnumeratedThreadIdentity);
    C19_HEX64("initiatorThread", d.rootCollectionInitiatorIdentity);
    C19_HEX64("threadStoreOwner", d.rootLockOwnerIdentity);
    C19_HEX32("threadStoreRecursion", d.threadStoreLockRecursionDepth);
    C19_HEX32("eeSuspended", d.eeSuspended);
    C19_HEX32("managedEntryProhibited", d.managedEntryProhibited);
    C19_HEX64("threadUnderCrawl", d.callbackContextThreadUnderCrawl);
    C19_HEX32("cooperative", d.rootThreadRecords[0].cooperative);
    C19_HEX32("preemptive", d.rootThreadRecords[0].preemptive);
    C19_HEX32("safeStopReason", d.c011ec19SafeStopReason);
#undef C19_HEX32
#undef C19_HEX64
    suspendEeSerialPutString(" marker=C011EC19\n");
}

extern "C" __declspec(noreturn) void __cdecl
guideXosNativeAotC011EC19SafeStop(uint32_t reason) {
    guidexos_nativeaot_allocation_diagnostics& d =
        g_guideXosAllocationDiagnostics;
    d.c011ec19SafeStopReason = reason;
    d.safeStopObserved = 1u;
    d.stopReason = GUIDEXOS_NATIVEAOT_UNWIND_GC_INFO_BOUNDARY_MARKER;
    d.stage = GUIDEXOS_NATIVEAOT_ALLOC_STAGE_F33_NEXT_GENUINE_ROOT_PROVIDER_SAFE_STOP;
    emitC011EC19SafeStop();
    for (;;) {
    }
}
#endif

#if defined(GUIDEXOS_NATIVEAOT_C011EC20_UNWIND)
extern "C" __declspec(noreturn) void __cdecl
guideXosNativeAotC011EC20SafeStop(uint32_t reason);

extern "C" void __cdecl
guideXosNativeAotC011EC20TransitionCrossed(
    uintptr_t frameType, uintptr_t frameAddress, uintptr_t savedRip,
    uintptr_t savedSp, uintptr_t savedFp, uintptr_t threadAddress,
    uintptr_t flags, uintptr_t previousTransitionFrame) {
    guidexos_nativeaot_allocation_diagnostics& d =
        g_guideXosAllocationDiagnostics;
    ++d.c011ec20TransitionCrossingAttempts;
    d.c011ec20TransitionFrameType = frameType;
    d.c011ec20TransitionFrameAddress = frameAddress;
    d.c011ec20TransitionSavedRip = savedRip;
    d.c011ec20TransitionSavedSp = savedSp;
    d.c011ec20TransitionSavedFp = savedFp;
    d.c011ec20TransitionThread = threadAddress;
    d.c011ec20TransitionFlags = flags;
    d.c011ec20PreviousTransitionFrame = previousTransitionFrame;
    if (frameAddress != 0u && savedRip != 0u) {
        ++d.c011ec20TransitionCrossingResults;
    } else if (d.c011ec18TransitionFrameAddress != 0u &&
               d.c011ec18TransitionFrameRip != 0u) {
        /*
         * The reverse-P/Invoke decoder reports the previous transition
         * frame.  At the top-level reverse-P/Invoke boundary that pointer is
         * intentionally null.  C011EC19 independently recorded the current
         * transition-associated frame, so preserve that evidence while
         * allowing C011EC20 to continue through the ordinary unwind path.
         */
        d.c011ec20TransitionFrameAddress =
            d.c011ec18TransitionFrameAddress;
        d.c011ec20TransitionSavedRip = d.c011ec18TransitionFrameRip;
        d.c011ec20TransitionSavedSp = d.c011ec18SavedRsp;
        d.c011ec20TransitionSavedFp = d.c011ec18TransitionFrameRbp;
        d.c011ec20TransitionThread = d.c011ec18ThreadAddress;
        d.c011ec20TransitionFlags = d.c011ec18TransitionFrameFlags;
        d.c011ec20PreviousTransitionFrame =
            d.c011ec18PreviousTransitionFrame;
        ++d.c011ec20TransitionCrossingResults;
    } else {
        d.c011ec20Outcome = 3u; // Outcome C: transition crossing blocker.
    }
    suspendEeSerialPutString(
        "[nativeaot-gc-unwind-caller-frame] transition-crossed frameType=");
    suspendEeSerialPutHex64(frameType);
    suspendEeSerialPutString(" frame=");
    suspendEeSerialPutHex64(frameAddress);
    suspendEeSerialPutString(" savedRIP=");
    suspendEeSerialPutHex64(savedRip);
    suspendEeSerialPutString(" savedSP=");
    suspendEeSerialPutHex64(savedSp);
    suspendEeSerialPutString(" previous=");
    suspendEeSerialPutHex64(previousTransitionFrame);
    suspendEeSerialPutString("\n");
}

extern "C" void __cdecl
guideXosNativeAotC011EC20UnwindInputs(
    uintptr_t imageBase, uintptr_t runtimeFunction, uintptr_t beginRva,
    uintptr_t endRva, uintptr_t unwindInfo, uintptr_t unwindInfoSize,
    uintptr_t blockFlags, uintptr_t inputRip, uintptr_t inputRsp,
    uintptr_t inputRbp, uintptr_t inputRbx, uintptr_t inputRsi,
    uintptr_t inputRdi, uintptr_t inputR12, uintptr_t inputR13,
    uintptr_t inputR14, uintptr_t inputR15) {
    guidexos_nativeaot_allocation_diagnostics& d =
        g_guideXosAllocationDiagnostics;
    ++d.c011ec20UnwindAttemptCount;
    d.c011ec20ImageBase = imageBase;
    d.c011ec20RuntimeFunction = runtimeFunction;
    d.c011ec20BeginRva = beginRva;
    d.c011ec20EndRva = endRva;
    d.c011ec20UnwindInfo = unwindInfo;
    d.c011ec20UnwindInfoSize = unwindInfoSize;
    d.c011ec20UnwindBlockFlags = blockFlags;
    d.c011ec20InputRip = inputRip;
    d.c011ec20InputRsp = inputRsp;
    d.c011ec20InputRbp = inputRbp;
    d.c011ec20InputRbx = inputRbx;
    d.c011ec20InputRsi = inputRsi;
    d.c011ec20InputRdi = inputRdi;
    d.c011ec20InputR12 = inputR12;
    d.c011ec20InputR13 = inputR13;
    d.c011ec20InputR14 = inputR14;
    d.c011ec20InputR15 = inputR15;
    suspendEeSerialPutString(
        "[nativeaot-gc-unwind-caller-frame] unwind-inputs rip=");
    suspendEeSerialPutHex64(inputRip);
    suspendEeSerialPutString(" rsp=");
    suspendEeSerialPutHex64(inputRsp);
    suspendEeSerialPutString(" rf=");
    suspendEeSerialPutHex64(runtimeFunction);
    suspendEeSerialPutString(" unwindInfo=");
    suspendEeSerialPutHex64(unwindInfo);
    suspendEeSerialPutString("\n");
}

extern "C" void __cdecl
guideXosNativeAotC011EC20CallerMethodInfo(
    uintptr_t controlPc, uintptr_t codeManager, uintptr_t methodInfo,
    uintptr_t methodStart, uintptr_t methodEnd, uintptr_t runtimeFunction,
    uintptr_t unwindInfo, uintptr_t unwindInfoSize, uintptr_t blockFlags) {
    guidexos_nativeaot_allocation_diagnostics& d =
        g_guideXosAllocationDiagnostics;
    if (controlPc == 0u || controlPc != d.c011ec20OutputRip ||
        d.c011ec20RtlVirtualUnwindCallCount == 0u) {
        return;
    }
    d.c011ec20CallerCodeManager = codeManager;
    d.c011ec20CallerMethodInfo = methodInfo;
    d.c011ec20CallerMethodStart = methodStart;
    d.c011ec20CallerMethodEnd = methodEnd;
    d.c011ec20CallerRuntimeFunction = runtimeFunction;
    d.c011ec20CallerUnwindInfo = unwindInfo;
    d.c011ec20CallerUnwindInfoSize = unwindInfoSize;
    d.c011ec20CallerUnwindBlockFlags = blockFlags;
}

extern "C" void __cdecl
guideXosNativeAotC011EC20UnwindCompleted(
    uint32_t result, uintptr_t outputRip, uintptr_t outputRsp,
    uintptr_t outputRbp, uintptr_t establisherFrame, uintptr_t handlerData,
    uintptr_t rtlVirtualUnwindResult,
    uintptr_t restoredRbx, uintptr_t restoredRsi, uintptr_t restoredRdi,
    uintptr_t restoredR12, uintptr_t restoredR13, uintptr_t restoredR14,
    uintptr_t restoredR15, uint32_t restoredRegisterCount,
    uintptr_t previousTransitionFrame) {
    guidexos_nativeaot_allocation_diagnostics& d =
        g_guideXosAllocationDiagnostics;
    ++d.c011ec20RtlVirtualUnwindCallCount;
    d.c011ec20RtlVirtualUnwindReturned = result != 0u ? 1u : 0u;
    d.c011ec20UnwindResult = result;
    d.c011ec20OutputRip = outputRip;
    d.c011ec20OutputRsp = outputRsp;
    d.c011ec20OutputRbp = outputRbp;
    d.c011ec20EstablisherFrame = establisherFrame;
    d.c011ec20HandlerData = handlerData;
    d.c011ec20RtlVirtualUnwindResult = rtlVirtualUnwindResult;
    d.c011ec20RestoredRbx = restoredRbx;
    d.c011ec20RestoredRsi = restoredRsi;
    d.c011ec20RestoredRdi = restoredRdi;
    d.c011ec20RestoredR12 = restoredR12;
    d.c011ec20RestoredR13 = restoredR13;
    d.c011ec20RestoredR14 = restoredR14;
    d.c011ec20RestoredR15 = restoredR15;
    d.c011ec20RestoredRegisterCount = restoredRegisterCount;
    d.c011ec20PreviousTransitionFrame = previousTransitionFrame;
    suspendEeSerialPutString(
        "[nativeaot-gc-unwind-caller-frame] unwind-complete rip=");
    suspendEeSerialPutHex64(outputRip);
    suspendEeSerialPutString(" rsp=");
    suspendEeSerialPutHex64(outputRsp);
    suspendEeSerialPutString(" rbp=");
    suspendEeSerialPutHex64(outputRbp);
    suspendEeSerialPutString(" restored=");
    suspendEeSerialPutHex32(restoredRegisterCount);
    suspendEeSerialPutString("\n");

    RuntimeInstance* runtime = GetRuntimeInstance();
    ICodeManager* callerCodeManager = nullptr;
    const bool callerInManagedRange = c011ec18IsInsideManagedRange(
        outputRip, runtime, &callerCodeManager);
    d.c011ec20CallerManagedRange = callerInManagedRange ? 1u : 0u;
    d.c011ec20CallerCodeManagerFound = callerCodeManager != nullptr ? 1u : 0u;

    if (callerInManagedRange && callerCodeManager != nullptr) {
        MethodInfo callerMethodInfo = {};
        ++d.c011ec20CallerFindMethodInfoAttempts;
        if (callerCodeManager->FindMethodInfo(
                reinterpret_cast<void*>(outputRip), &callerMethodInfo)) {
            ++d.c011ec20CallerFindMethodInfoSuccess;
        }
    }

    d.c011ec20CallerSpMoved = outputRsp != d.c011ec20InputRsp ? 1u : 0u;
    d.c011ec20CallerSpAligned = (outputRsp & (sizeof(uintptr_t) - 1u)) == 0u ? 1u : 0u;
    d.c011ec20CallerFrameDistinct =
        outputRip != d.c011ec20InputRip && outputRsp != d.c011ec20InputRsp ? 1u : 0u;

#if defined(GUIDEXOS_NATIVEAOT_C011EC21_NATIVE_CONTINUATION) && \
    !defined(GUIDEXOS_NATIVEAOT_C011EC23_NATIVE_UNWIND)
    /*
     * C011EC21 deliberately stops at the first recovered native caller. The
     * kernel-side provenance callback supplies the exact helper symbol and
     * call-site identity from the same linked image; no stack word is read.
     */
    d.c011ec21NativeFrameCandidate = 1u;
    d.c011ec21NativeRip = outputRip;
    d.c011ec21NativeRsp = outputRsp;
    d.c011ec21NativeRbp = outputRbp;
    d.c011ec21NativeUnwindAttemptCount = 0u;
    d.c011ec21NativeUnwindMetadataAvailable = 0u;
    d.c011ec21NativeUnwindResult = 0u;
    d.c011ec21ManagedReentryFound = 0u;
    d.c011ec21ManagedStackBottomProven = 0u;
    d.c011ec21TransitionNullInterpretation =
        previousTransitionFrame == 0u ? 2u : 3u;
    d.c011ec21TransitionLinkingDefect =
        previousTransitionFrame == 0u ? 0u : 1u;
    d.c011ec21Outcome = 5u;
    guideXosNativeAotC011EC21DescribeNativeCaller(
        outputRip, outputRsp, outputRbp);
    guideXosNativeAotC011EC21SafeStop(0xC0210005u);
#endif

#if !defined(GUIDEXOS_NATIVEAOT_C011EC23_NATIVE_UNWIND)
    const bool transitionCrossed =
        d.c011ec20TransitionCrossingAttempts == 1u &&
        d.c011ec20TransitionCrossingResults == 1u;
    const bool unwindValid =
        transitionCrossed && d.c011ec20UnwindAttemptCount == 1u &&
        d.c011ec20RtlVirtualUnwindCallCount == 1u && result != 0u &&
        outputRip != 0u && outputRsp != 0u && d.c011ec20CallerSpMoved != 0u &&
        d.c011ec20CallerSpAligned != 0u && d.c011ec20CallerFrameDistinct != 0u &&
        restoredRegisterCount != 0u;
    const bool callerValidated = !callerInManagedRange
        ? callerCodeManager == nullptr
        : d.c011ec20CallerFindMethodInfoSuccess != 0u;

    if (!unwindValid || !callerValidated) {
        d.c011ec20Outcome = 4u; // Outcome D: unwind/validation defect.
        guideXosNativeAotC011EC20SafeStop(0xC0200004u);
    }
    d.c011ec20Outcome = callerInManagedRange ? 1u : 5u; // A or legitimate native E.
    guideXosNativeAotC011EC20SafeStop(callerInManagedRange ? 0xC0200001u : 0xC0200005u);
#endif
}

static void emitC011EC20SafeStop() {
    const guidexos_nativeaot_allocation_diagnostics& d =
        g_guideXosAllocationDiagnostics;
    const bool c011ec21 =
#if defined(GUIDEXOS_NATIVEAOT_C011EC21_NATIVE_CONTINUATION)
        d.c011ec21MarkerEmitted != 0u;
#else
        false;
#endif
    const bool proof = c011ec21
#if defined(GUIDEXOS_NATIVEAOT_C011EC21_NATIVE_CONTINUATION)
        ? d.c011ec21Outcome == 5u
#else
        ? false
#endif
        : (d.c011ec20Outcome == 1u || d.c011ec20Outcome == 5u);
    suspendEeSerialPutString(
        c011ec21
            ? "[nativeaot-gc-native-transition] SAFE_STOP marker=C011EC21"
            : (proof ? "[nativeaot-gc-unwind-caller-frame] SAFE_STOP marker=C011EC20"
                    : "[nativeaot-gc-unwind-caller-frame] SAFE_STOP marker=C011EC20-SAFE_STOP"));
#define C20_HEX32(name, value) \
    suspendEeSerialPutString(" " name "="); suspendEeSerialPutHex32(value)
#define C20_HEX64(name, value) \
    suspendEeSerialPutString(" " name "="); suspendEeSerialPutHex64(value)
    C20_HEX64("transitionFrameType", d.c011ec20TransitionFrameType);
    C20_HEX64("transitionFrame", d.c011ec20TransitionFrameAddress);
    C20_HEX64("transitionSavedRIP", d.c011ec20TransitionSavedRip);
    C20_HEX64("transitionSavedSP", d.c011ec20TransitionSavedSp);
    C20_HEX64("transitionSavedFP", d.c011ec20TransitionSavedFp);
    C20_HEX64("transitionThread", d.c011ec20TransitionThread);
    C20_HEX64("transitionFlags", d.c011ec20TransitionFlags);
    C20_HEX64("previousTransitionFrame", d.c011ec20PreviousTransitionFrame);
    C20_HEX32("crossingAttempts", d.c011ec20TransitionCrossingAttempts);
    C20_HEX32("crossingResults", d.c011ec20TransitionCrossingResults);
    C20_HEX32("unwindAttempts", d.c011ec20UnwindAttemptCount);
    C20_HEX32("rtlVirtualUnwindCalls", d.c011ec20RtlVirtualUnwindCallCount);
    C20_HEX32("unwindResult", d.c011ec20UnwindResult);
    C20_HEX64("imageBase", d.c011ec20ImageBase);
    C20_HEX64("runtimeFunction", d.c011ec20RuntimeFunction);
    C20_HEX64("beginRVA", d.c011ec20BeginRva);
    C20_HEX64("endRVA", d.c011ec20EndRva);
    C20_HEX64("unwindInfo", d.c011ec20UnwindInfo);
    C20_HEX64("unwindInfoSize", d.c011ec20UnwindInfoSize);
    C20_HEX64("unwindBlockFlags", d.c011ec20UnwindBlockFlags);
    C20_HEX64("inputRIP", d.c011ec20InputRip);
    C20_HEX64("inputRSP", d.c011ec20InputRsp);
    C20_HEX64("inputRBP", d.c011ec20InputRbp);
    C20_HEX64("outputRIP", d.c011ec20OutputRip);
    C20_HEX64("outputRSP", d.c011ec20OutputRsp);
    C20_HEX64("outputRBP", d.c011ec20OutputRbp);
    C20_HEX64("establisherFrame", d.c011ec20EstablisherFrame);
    C20_HEX64("handlerData", d.c011ec20HandlerData);
    C20_HEX32("rtlVirtualUnwindReturned", d.c011ec20RtlVirtualUnwindReturned);
    C20_HEX64("rtlVirtualUnwindResult", d.c011ec20RtlVirtualUnwindResult);
    C20_HEX64("inputRBX", d.c011ec20InputRbx);
    C20_HEX64("inputRSI", d.c011ec20InputRsi);
    C20_HEX64("inputRDI", d.c011ec20InputRdi);
    C20_HEX64("inputR12", d.c011ec20InputR12);
    C20_HEX64("inputR13", d.c011ec20InputR13);
    C20_HEX64("inputR14", d.c011ec20InputR14);
    C20_HEX64("inputR15", d.c011ec20InputR15);
    C20_HEX64("restoredRBX", d.c011ec20RestoredRbx);
    C20_HEX64("restoredRSI", d.c011ec20RestoredRsi);
    C20_HEX64("restoredRDI", d.c011ec20RestoredRdi);
    C20_HEX64("restoredR12", d.c011ec20RestoredR12);
    C20_HEX64("restoredR13", d.c011ec20RestoredR13);
    C20_HEX64("restoredR14", d.c011ec20RestoredR14);
    C20_HEX64("restoredR15", d.c011ec20RestoredR15);
    C20_HEX32("restoredRegisterCount", d.c011ec20RestoredRegisterCount);
    C20_HEX32("callerManagedRange", d.c011ec20CallerManagedRange);
    C20_HEX32("callerCodeManagerFound", d.c011ec20CallerCodeManagerFound);
    C20_HEX64("callerCodeManager", d.c011ec20CallerCodeManager);
    C20_HEX32("callerFindMethodInfoAttempts", d.c011ec20CallerFindMethodInfoAttempts);
    C20_HEX32("callerFindMethodInfoSuccess", d.c011ec20CallerFindMethodInfoSuccess);
    C20_HEX64("callerMethodInfo", d.c011ec20CallerMethodInfo);
    C20_HEX64("callerMethodStart", d.c011ec20CallerMethodStart);
    C20_HEX64("callerMethodEnd", d.c011ec20CallerMethodEnd);
    C20_HEX64("callerRuntimeFunction", d.c011ec20CallerRuntimeFunction);
    C20_HEX64("callerUnwindInfo", d.c011ec20CallerUnwindInfo);
    C20_HEX64("callerUnwindInfoSize", d.c011ec20CallerUnwindInfoSize);
    C20_HEX64("callerUnwindBlockFlags", d.c011ec20CallerUnwindBlockFlags);
    C20_HEX32("callerGcInfoAttempted", d.c011ec20CallerGcInfoAttempted);
    C20_HEX32("callerGcInfoResult", d.c011ec20CallerGcInfoResult);
    C20_HEX32("callerSpMoved", d.c011ec20CallerSpMoved);
    C20_HEX32("callerSpAligned", d.c011ec20CallerSpAligned);
    C20_HEX32("callerFrameDistinct", d.c011ec20CallerFrameDistinct);
    C20_HEX32("outcome", d.c011ec20Outcome);
    C20_HEX32("safeStopReason", d.c011ec20SafeStopReason);
#if defined(GUIDEXOS_NATIVEAOT_C011EC21_NATIVE_CONTINUATION)
    C20_HEX32("c21NativeFrameCandidate", d.c011ec21NativeFrameCandidate);
    C20_HEX32("c21NativeUnwindAttempts", d.c011ec21NativeUnwindAttemptCount);
    C20_HEX32("c21NativeUnwindMetadata", d.c011ec21NativeUnwindMetadataAvailable);
    C20_HEX32("c21NativeUnwindResult", d.c011ec21NativeUnwindResult);
    C20_HEX32("c21ManagedReentry", d.c011ec21ManagedReentryFound);
    C20_HEX32("c21ManagedStackBottom", d.c011ec21ManagedStackBottomProven);
    C20_HEX32("c21NullPredecessorMeaning", d.c011ec21TransitionNullInterpretation);
    C20_HEX32("c21TransitionLinkingDefect", d.c011ec21TransitionLinkingDefect);
    C20_HEX32("c21Outcome", d.c011ec21Outcome);
    C20_HEX64("c21NativeRIP", d.c011ec21NativeRip);
    C20_HEX64("c21NativeRSP", d.c011ec21NativeRsp);
    C20_HEX64("c21NativeRBP", d.c011ec21NativeRbp);
    C20_HEX64("c21HelperStart", d.c011ec21NativeHelperStart);
    C20_HEX64("c21HelperEnd", d.c011ec21NativeHelperEnd);
    C20_HEX64("c21FunctionOffset", d.c011ec21NativeFunctionOffset);
    C20_HEX64("c21CallSite", d.c011ec21NativeCallSite);
    C20_HEX64("c21ModuleIdentity", d.c011ec21NativeModuleIdentity);
    C20_HEX64("c21SectionIdentity", d.c011ec21NativeSectionIdentity);
    C20_HEX64("c21RuntimeFunction", d.c011ec21NativeRuntimeFunction);
    C20_HEX64("c21UnwindInfo", d.c011ec21NativeUnwindInfo);
    C20_HEX32("c19SecondQueueInsertions", d.c011ec19SecondQueueInsertionCount);
    C20_HEX64("c19SecondQueueSlot", d.c011ec19SecondQueueSlot);
    C20_HEX64("c19SecondQueueCursorBefore", d.c011ec19SecondQueueCursorBefore);
    C20_HEX64("c19SecondQueueCursorAfter", d.c011ec19SecondQueueCursorAfter);
    C20_HEX64("c19SecondQueueOld", d.c011ec19SecondQueueOldValue);
    C20_HEX64("c19SecondQueueNew", d.c011ec19SecondQueueNewValue);
#endif
    C20_HEX32("c19RootReports", d.c011ec19RootReportCount);
    C20_HEX32("c19RegisterRoots", d.c011ec19RegisterRootCount);
    C20_HEX32("c19StackRoots", d.c011ec19StackRootCount);
    C20_HEX32("c19PromoteAttempts", d.c011ec19FirstStackDerivedPromoteAttemptCount);
    C20_HEX32("c19PromoteEntries", d.c011ec19FirstStackDerivedPromoteEntryCount);
    C20_HEX32("c19PromoteReturns", d.c011ec19FirstStackDerivedPromoteReturnCount);
    C20_HEX32("framesWalked", d.c011ec18StackFrameCount);
    C20_HEX32("stackProviderCallbacks", d.c011ec18StackProviderCallbackCount);
    C20_HEX32("totalRoots", d.c011ec15RootSlotVisitCount);
    C20_HEX64("stackBase", d.rootThreadRecords[0].stackLow);
    C20_HEX64("stackLimit", d.rootThreadRecords[0].stackHigh);
    C20_HEX64("scanContextStackLimit", d.callbackContextStackLimit);
    C20_HEX32("stackBoundsConsumed", d.c011ec18StackBoundsConsumed);
    C20_HEX32("markWrites", d.c011ec15MarkBitWriteCount);
    C20_HEX32("childReads", d.c011ec15ChildReferenceReadCount);
    C20_HEX32("graphTraversal", d.c011ec15GraphTraversalCount);
    C20_HEX32("promoteEntries", d.c011ec15PromoteEntryCount);
    C20_HEX32("promoteReturns", d.c011ec15PromoteReturnCount);
    C20_HEX32("threadStoreRecursion", d.threadStoreLockRecursionDepth);
    C20_HEX32("eeSuspended", d.eeSuspended);
    C20_HEX32("managedEntryProhibited", d.managedEntryProhibited);
#undef C20_HEX32
#undef C20_HEX64
    suspendEeSerialPutString(" marker=");
    suspendEeSerialPutString(c011ec21 ? "C011EC21" : (proof ? "C011EC20" : "C011EC20-SAFE_STOP"));
    suspendEeSerialPutString("\n");
}

#if defined(GUIDEXOS_NATIVEAOT_C011EC23_NATIVE_UNWIND)
static void emitC011EC23SafeStop() {
    const guidexos_nativeaot_allocation_diagnostics& d =
        g_guideXosAllocationDiagnostics;
    suspendEeSerialPutString(
        "[nativeaot-gc-native-unwind] SAFE_STOP");
#define C23_HEX32(name, value) \
    suspendEeSerialPutString(" " name "="); suspendEeSerialPutHex32(value)
#define C23_HEX64(name, value) \
    suspendEeSerialPutString(" " name "="); suspendEeSerialPutHex64(value)
    C23_HEX32("lookupAttempts", d.c011ec23LookupAttemptCount);
    C23_HEX32("lookupSuccesses", d.c011ec23LookupSuccessCount);
    C23_HEX32("unwindAttempts", d.c011ec23UnwindAttemptCount);
    C23_HEX32("rtlVirtualUnwindCalls", d.c011ec23RtlVirtualUnwindCallCount);
    C23_HEX32("rtlVirtualUnwindReturned", d.c011ec23RtlVirtualUnwindReturned);
    C23_HEX32("unwindResult", d.c011ec23UnwindResult);
    C23_HEX32("nativeFramesCrossed", d.c011ec23NativeFramesCrossed);
    C23_HEX32("managedReentry", d.c011ec23ManagedReentryFound);
    C23_HEX32("callerManagedRange", d.c011ec23CallerManagedRange);
    C23_HEX32("callerCodeManagerFound", d.c011ec23CallerCodeManagerFound);
    C23_HEX32("callerFindMethodInfoAttempts", d.c011ec23CallerFindMethodInfoAttempts);
    C23_HEX32("callerFindMethodInfoSuccess", d.c011ec23CallerFindMethodInfoSuccess);
    C23_HEX32("secondFunctionAttempted", d.c011ec23SecondFunctionAttempted);
    C23_HEX32("secondFunctionSucceeded", d.c011ec23SecondFunctionSucceeded);
    C23_HEX32("secondFunctionResult", d.c011ec23SecondFunctionResult);
    C23_HEX32("secondFunctionIndex", d.c011ec23SecondFunctionIndex);
    C23_HEX64("inputRIP", d.c011ec23InputRip);
    C23_HEX64("inputRSP", d.c011ec23InputRsp);
    C23_HEX64("inputRBP", d.c011ec23InputRbp);
    C23_HEX64("outputRIP", d.c011ec23OutputRip);
    C23_HEX64("outputRSP", d.c011ec23OutputRsp);
    C23_HEX64("outputRBP", d.c011ec23OutputRbp);
    C23_HEX64("establisherFrame", d.c011ec23EstablisherFrame);
    C23_HEX64("handlerData", d.c011ec23HandlerData);
    C23_HEX64("moduleBase", d.c011ec23ModuleBase);
    C23_HEX64("executableStart", d.c011ec23ExecutableStart);
    C23_HEX64("executableEnd", d.c011ec23ExecutableEnd);
    C23_HEX64("pdataStart", d.c011ec23PdataStart);
    C23_HEX64("pdataEnd", d.c011ec23PdataEnd);
    C23_HEX64("xdataStart", d.c011ec23XdataStart);
    C23_HEX64("xdataEnd", d.c011ec23XdataEnd);
    C23_HEX64("runtimeFunction", d.c011ec23RuntimeFunction);
    C23_HEX64("unwindInfo", d.c011ec23UnwindInfo);
    C23_HEX64("beginRVA", d.c011ec23BeginAddress);
    C23_HEX64("endRVA", d.c011ec23EndAddress);
    C23_HEX64("unwindRVA", d.c011ec23UnwindData);
    C23_HEX32("unwindVersion", d.c011ec23UnwindVersion);
    C23_HEX32("unwindFlags", d.c011ec23UnwindFlags);
    C23_HEX32("prologueSize", d.c011ec23PrologueSize);
    C23_HEX32("unwindCodeCount", d.c011ec23UnwindCodeCount);
    C23_HEX32("frameRegister", d.c011ec23FrameRegister);
    C23_HEX32("frameOffset", d.c011ec23FrameOffset);
    C23_HEX64("restoredRBX", d.c011ec23RestoredRbx);
    C23_HEX64("restoredRBP", d.c011ec23RestoredRbp);
    C23_HEX64("restoredRSI", d.c011ec23RestoredRsi);
    C23_HEX64("restoredRDI", d.c011ec23RestoredRdi);
    C23_HEX64("restoredR12", d.c011ec23RestoredR12);
    C23_HEX64("restoredR13", d.c011ec23RestoredR13);
    C23_HEX64("restoredR14", d.c011ec23RestoredR14);
    C23_HEX64("restoredR15", d.c011ec23RestoredR15);
    C23_HEX32("restoredRegisterCount", d.c011ec23RestoredRegisterCount);
    C23_HEX64("secondRuntimeFunction", d.c011ec23SecondRuntimeFunction);
    C23_HEX64("secondUnwindInfo", d.c011ec23SecondUnwindInfo);
    C23_HEX64("secondOutputRIP", d.c011ec23SecondOutputRip);
    C23_HEX64("secondOutputRSP", d.c011ec23SecondOutputRsp);
    C23_HEX64("callerCodeManager", d.c011ec23CallerCodeManager);
    C23_HEX64("callerMethodInfo", d.c011ec23CallerMethodInfo);
    C23_HEX64("callerMethodStart", d.c011ec23CallerMethodStart);
    C23_HEX64("callerMethodEnd", d.c011ec23CallerMethodEnd);
    C23_HEX32("c19RootReports", d.c011ec19RootReportCount);
    C23_HEX32("c19RegisterRoots", d.c011ec19RegisterRootCount);
    C23_HEX32("c19StackRoots", d.c011ec19StackRootCount);
    C23_HEX32("c19PromoteAttempts", d.c011ec19FirstStackDerivedPromoteAttemptCount);
    C23_HEX32("c19PromoteEntries", d.c011ec19FirstStackDerivedPromoteEntryCount);
    C23_HEX32("c19PromoteReturns", d.c011ec19FirstStackDerivedPromoteReturnCount);
    C23_HEX32("framesWalked", d.c011ec18StackFrameCount);
    C23_HEX32("totalRoots", d.c011ec15RootSlotVisitCount);
    C23_HEX32("stackBoundsConsumed", d.c011ec18StackBoundsConsumed);
    C23_HEX32("markWrites", d.c011ec15MarkBitWriteCount);
    C23_HEX32("childReads", d.c011ec15ChildReferenceReadCount);
    C23_HEX32("graphTraversal", d.c011ec15GraphTraversalCount);
    C23_HEX32("promoteEntries", d.c011ec15PromoteEntryCount);
    C23_HEX32("promoteReturns", d.c011ec15PromoteReturnCount);
    C23_HEX64("stackBase", d.rootThreadRecords[0].stackLow);
    C23_HEX64("stackLimit", d.rootThreadRecords[0].stackHigh);
    C23_HEX64("scanContextStackLimit", d.callbackContextStackLimit);
    C23_HEX32("safeStopReason", d.c011ec23SafeStopReason);
    C23_HEX32("outcome", d.c011ec23Outcome);
#undef C23_HEX32
#undef C23_HEX64
    suspendEeSerialPutString(" marker=C011EC23\n");
}

extern "C" __declspec(noreturn) void __cdecl
guideXosNativeAotC011EC23SafeStop(uint32_t reason) {
    guidexos_nativeaot_allocation_diagnostics& d =
        g_guideXosAllocationDiagnostics;
    d.c011ec23MarkerEmitted = 1u;
    d.c011ec23SafeStopReason = reason;
    d.safeStopObserved = 1u;
    d.stopReason = GUIDEXOS_NATIVEAOT_CALLER_FRAME_UNWIND_MARKER;
    d.stage = GUIDEXOS_NATIVEAOT_ALLOC_STAGE_F33_NEXT_GENUINE_ROOT_PROVIDER_SAFE_STOP;
    emitC011EC23SafeStop();
    for (;;) {
    }
}

#if defined(GUIDEXOS_NATIVEAOT_C011EC24_CALLER_PROVENANCE)
static void emitC011EC24SafeStop(const char* finalMarker) {
    const guidexos_nativeaot_allocation_diagnostics& d =
        g_guideXosAllocationDiagnostics;
    suspendEeSerialPutString(
        "[nativeaot-gc-native-caller-provenance] SAFE_STOP");
#define C24_HEX32(name, value) \
    suspendEeSerialPutString(" " name "="); suspendEeSerialPutHex32(value)
#define C24_HEX64(name, value) \
    suspendEeSerialPutString(" " name "="); suspendEeSerialPutHex64(value)
    C24_HEX32("preflightProven", d.c011ec24PreflightProven);
    C24_HEX32("outputAgreement", d.c011ec24OutputAgreement);
    C24_HEX32("callerValid", d.c011ec24CallerValid);
    C24_HEX32("callerKernelRange", d.c011ec24CallerKernelRange);
    C24_HEX32("callerManagedRange", d.c011ec24CallerManagedRange);
    C24_HEX32("standaloneTests", d.c011ec24StandaloneTests);
    C24_HEX32("helperStandalonePassed", d.c011ec24HelperStandalonePassed);
    C24_HEX32("secondStandalonePassed", d.c011ec24SecondStandalonePassed);
    C24_HEX32("unwindOpcodeCount", d.c011ec24UnwindOpcodeCount);
    C24_HEX32("stackAdvance", d.c011ec24StackAdvance);
    C24_HEX32("lookupAttempts", d.c011ec23LookupAttemptCount);
    C24_HEX32("lookupSuccesses", d.c011ec23LookupSuccessCount);
    C24_HEX32("unwindAttempts", d.c011ec23UnwindAttemptCount);
    C24_HEX32("rtlVirtualUnwindCalls", d.c011ec23RtlVirtualUnwindCallCount);
    C24_HEX32("rtlVirtualUnwindReturned", d.c011ec23RtlVirtualUnwindReturned);
    C24_HEX32("unwindResult", d.c011ec23UnwindResult);
    C24_HEX32("nativeFramesCrossed", d.c011ec23NativeFramesCrossed);
    C24_HEX32("managedReentry", d.c011ec23ManagedReentryFound);
    C24_HEX32("secondProviderLookupAttempted", d.c011ec24SecondProviderLookupAttempted);
    C24_HEX32("secondProviderLookupSucceeded", d.c011ec24SecondProviderLookupSucceeded);
    C24_HEX32("secondProductionUnwindAttempted", d.c011ec24SecondProductionUnwindAttempted);
    C24_HEX32("secondUnwindResult", d.c011ec24SecondUnwindResult);
    C24_HEX32("outcome", d.c011ec23Outcome);
    C24_HEX64("inputRIP", d.c011ec23InputRip);
    C24_HEX64("inputRSP", d.c011ec23InputRsp);
    C24_HEX64("inputRBP", d.c011ec23InputRbp);
    C24_HEX64("returnSlot", d.c011ec24DerivedReturnSlot);
    C24_HEX64("returnValue", d.c011ec24DerivedReturnValue);
    C24_HEX64("expectedCallerRIP", d.c011ec24ExpectedCallerRip);
    C24_HEX64("expectedCallerRSP", d.c011ec24ExpectedCallerRsp);
    C24_HEX64("outputRIP", d.c011ec23OutputRip);
    C24_HEX64("outputRSP", d.c011ec23OutputRsp);
    C24_HEX64("outputRBP", d.c011ec23OutputRbp);
    C24_HEX64("secondModuleBase", d.c011ec24SecondModuleBase);
    C24_HEX64("secondExecutableStart", d.c011ec24SecondExecutableStart);
    C24_HEX64("secondExecutableEnd", d.c011ec24SecondExecutableEnd);
    C24_HEX64("secondRuntimeFunction", d.c011ec24SecondRuntimeFunction);
    C24_HEX64("secondUnwindInfo", d.c011ec24SecondUnwindInfo);
    C24_HEX64("secondInputRIP", d.c011ec24SecondInputRip);
    C24_HEX64("secondInputRSP", d.c011ec24SecondInputRsp);
    C24_HEX64("secondInputRBP", d.c011ec24SecondInputRbp);
    C24_HEX64("secondOutputRIP", d.c011ec24SecondOutputRip);
    C24_HEX64("secondOutputRSP", d.c011ec24SecondOutputRsp);
    C24_HEX64("secondOutputRBP", d.c011ec24SecondOutputRbp);
    C24_HEX64("preflightReturnSlot", d.c011ec24PreflightReturnSlot);
    C24_HEX64("preflightReturnValue", d.c011ec24PreflightReturnValue);
    C24_HEX64("preflightOutputRIP", d.c011ec24PreflightOutputRip);
    C24_HEX64("preflightOutputRSP", d.c011ec24PreflightOutputRsp);
    C24_HEX64("liveRSP", d.c011ec24LiveRsp);
    C24_HEX64("sourceRBX", d.c011ec24SourceRbx);
    C24_HEX64("sourceRSI", d.c011ec24SourceRsi);
    C24_HEX64("sourceRDI", d.c011ec24SourceRdi);
    C24_HEX64("sourceRBP", d.c011ec24SourceRbp);
    C24_HEX64("sourceR12", d.c011ec24SourceR12);
    C24_HEX64("sourceR13", d.c011ec24SourceR13);
    C24_HEX64("sourceR14", d.c011ec24SourceR14);
    C24_HEX64("sourceR15", d.c011ec24SourceR15);
    C24_HEX64("preRBX", d.c011ec24PreRbx);
    C24_HEX64("preRSI", d.c011ec24PreRsi);
    C24_HEX64("preRDI", d.c011ec24PreRdi);
    C24_HEX64("preRBP", d.c011ec24PreRbp);
    C24_HEX64("preR12", d.c011ec24PreR12);
    C24_HEX64("preR13", d.c011ec24PreR13);
    C24_HEX64("preR14", d.c011ec24PreR14);
    C24_HEX64("preR15", d.c011ec24PreR15);
    C24_HEX64("recoveredRBX", d.c011ec24RecoveredRbx);
    C24_HEX64("recoveredRSI", d.c011ec24RecoveredRsi);
    C24_HEX64("recoveredRDI", d.c011ec24RecoveredRdi);
    C24_HEX64("recoveredRBP", d.c011ec24RecoveredRbp);
    C24_HEX64("recoveredR12", d.c011ec24RecoveredR12);
    C24_HEX64("recoveredR13", d.c011ec24RecoveredR13);
    C24_HEX64("recoveredR14", d.c011ec24RecoveredR14);
    C24_HEX64("recoveredR15", d.c011ec24RecoveredR15);
    C24_HEX32("c19RootReports", d.c011ec19RootReportCount);
    C24_HEX32("c19RegisterRoots", d.c011ec19RegisterRootCount);
    C24_HEX32("c19StackRoots", d.c011ec19StackRootCount);
    C24_HEX32("c19PromoteAttempts", d.c011ec19FirstStackDerivedPromoteAttemptCount);
    C24_HEX32("c19PromoteEntries", d.c011ec19FirstStackDerivedPromoteEntryCount);
    C24_HEX32("c19PromoteReturns", d.c011ec19FirstStackDerivedPromoteReturnCount);
    C24_HEX32("framesWalked", d.c011ec18StackFrameCount);
    C24_HEX32("totalRoots", d.c011ec15RootSlotVisitCount);
    C24_HEX32("stackBoundsConsumed", d.c011ec18StackBoundsConsumed);
    C24_HEX32("markWrites", d.c011ec15MarkBitWriteCount);
    C24_HEX32("childReads", d.c011ec15ChildReferenceReadCount);
    C24_HEX32("graphTraversal", d.c011ec15GraphTraversalCount);
    C24_HEX64("queueCursorBefore", d.c011ec19SecondQueueCursorBefore);
    C24_HEX64("queueCursorAfter", d.c011ec19SecondQueueCursorAfter);
    C24_HEX64("stackBase", d.rootThreadRecords[0].stackLow);
    C24_HEX64("stackLimit", d.rootThreadRecords[0].stackHigh);
    C24_HEX64("scanContextStackLimit", d.callbackContextStackLimit);
    C24_HEX32("safeStopReason", d.c011ec24SafeStopReason);
#if defined(GUIDEXOS_NATIVEAOT_C011EC25_KERNEL_ENTRY_BOUNDARY)
    C24_HEX32("c25PreflightProven", d.c011ec25PreflightProven);
    C24_HEX32("c25SecondMetadataValid", d.c011ec25SecondMetadataValid);
    C24_HEX32("c25SecondOutputAgreement", d.c011ec25SecondOutputAgreement);
    C24_HEX32("c25ThirdInKernelRange", d.c011ec25ThirdInKernelRange);
    C24_HEX32("c25ThirdLinkedLookupAttempted", d.c011ec25ThirdLinkedLookupAttempted);
    C24_HEX32("c25ThirdLinkedLookupSucceeded", d.c011ec25ThirdLinkedLookupSucceeded);
    C24_HEX32("c25ThirdPhysicalLookupAttempted", d.c011ec25ThirdPhysicalLookupAttempted);
    C24_HEX32("c25ThirdPhysicalLookupSucceeded", d.c011ec25ThirdPhysicalLookupSucceeded);
    C24_HEX32("c25ThirdMetadataPresent", d.c011ec25ThirdMetadataPresent);
    C24_HEX32("c25AssemblyEntryBoundary", d.c011ec25AssemblyEntryBoundary);
    C24_HEX32("c25NonReturningHandoff", d.c011ec25NonReturningHandoff);
    C24_HEX32("c25StackBottomProven", d.c011ec25StackBottomProven);
    C24_HEX32("c25SecondOpcodeCount", d.c011ec25SecondOpcodeCount);
    C24_HEX32("c25SecondStackAdvance", d.c011ec25SecondStackAdvance);
    C24_HEX32("c25ProviderLookupResult", d.c011ec25ProviderLookupResult);
    C24_HEX32("c25LinkedLookupResult", d.c011ec25LinkedLookupResult);
    C24_HEX32("c25PhysicalLookupResult", d.c011ec25PhysicalLookupResult);
    C24_HEX32("c25SafeStopReason", d.c011ec25SafeStopReason);
    C24_HEX64("c25SecondReturnSlot", d.c011ec25SecondReturnSlot);
    C24_HEX64("c25SecondReturnValue", d.c011ec25SecondReturnValue);
    C24_HEX64("c25ExpectedCallerRIP", d.c011ec25ExpectedCallerRip);
    C24_HEX64("c25ExpectedCallerRSP", d.c011ec25ExpectedCallerRsp);
    C24_HEX64("c25ThirdPhysicalPC", d.c011ec25ThirdPhysicalPc);
    C24_HEX64("c25ThirdLinkedPC", d.c011ec25ThirdLinkedPc);
    C24_HEX64("c25LinkedEntryPC", d.c011ec25LinkedEntryPc);
    C24_HEX64("c25LinkedHaltPC", d.c011ec25LinkedHaltPc);
    C24_HEX64("c25BootStackTop", d.c011ec25BootStackTop);
    C24_HEX64("c25SecondEstablisherFrame", d.c011ec25SecondEstablisherFrame);
    C24_HEX64("c25SecondHandlerData", d.c011ec25SecondHandlerData);
    C24_HEX64("c25SecondRecoveredRBX", d.c011ec25SecondRecoveredRbx);
    C24_HEX64("c25SecondRecoveredRSI", d.c011ec25SecondRecoveredRsi);
    C24_HEX64("c25SecondRecoveredRDI", d.c011ec25SecondRecoveredRdi);
    C24_HEX64("c25SecondRecoveredRBP", d.c011ec25SecondRecoveredRbp);
    for (uint32_t index = 0u; index < d.c011ec25SecondOpcodeCount; ++index) {
        suspendEeSerialPutString(" c25OpcodeWord");
        suspendEeSerialPutHex32(index);
        suspendEeSerialPutString("=");
        suspendEeSerialPutHex32(d.c011ec25SecondOpcodeWords[index]);
    }
#endif
    for (uint32_t index = 0u; index < d.c011ec24UnwindOpcodeCount; ++index) {
        suspendEeSerialPutString(" opcodeWord");
        suspendEeSerialPutHex32(index);
        suspendEeSerialPutString("=");
        suspendEeSerialPutHex32(d.c011ec24OpcodeWords[index]);
    }
#undef C24_HEX32
#undef C24_HEX64
    suspendEeSerialPutString(" marker=");
    suspendEeSerialPutString(finalMarker);
    suspendEeSerialPutString("\n");
}

extern "C" __declspec(noreturn) void __cdecl
guideXosNativeAotC011EC24SafeStop(uint32_t reason) {
    guidexos_nativeaot_allocation_diagnostics& d =
        g_guideXosAllocationDiagnostics;
    d.c011ec24MarkerEmitted = 1u;
    d.c011ec24SafeStopReason = reason;
    d.safeStopObserved = 1u;
    d.stopReason = GUIDEXOS_NATIVEAOT_CALLER_FRAME_UNWIND_MARKER;
    d.stage = GUIDEXOS_NATIVEAOT_ALLOC_STAGE_F33_NEXT_GENUINE_ROOT_PROVIDER_SAFE_STOP;
    emitC011EC24SafeStop("C011EC24");
    for (;;) {
    }
}
#if defined(GUIDEXOS_NATIVEAOT_C011EC25_KERNEL_ENTRY_BOUNDARY)
extern "C" __declspec(noreturn) void __cdecl
guideXosNativeAotC011EC25SafeStop(uint32_t reason) {
    guidexos_nativeaot_allocation_diagnostics& d =
        g_guideXosAllocationDiagnostics;
    d.c011ec25MarkerEmitted = 1u;
    d.c011ec25SafeStopReason = reason;
    d.safeStopObserved = 1u;
    d.stopReason = GUIDEXOS_NATIVEAOT_CALLER_FRAME_UNWIND_MARKER;
    d.stage = GUIDEXOS_NATIVEAOT_ALLOC_STAGE_F33_NEXT_GENUINE_ROOT_PROVIDER_SAFE_STOP;
    emitC011EC24SafeStop("C011EC25");
    for (;;) {
    }
}
#endif
#endif
#endif

#if defined(GUIDEXOS_NATIVEAOT_C011EC26_STACK_COMPLETION)
static void emitC011EC26Completion() {
    const guidexos_nativeaot_allocation_diagnostics& d =
        g_guideXosAllocationDiagnostics;
    suspendEeSerialPutString(
        "[nativeaot-gc-stack-completion] COMPLETE marker=C011EC26");
#define C26_HEX32(name, value) \
    suspendEeSerialPutString(" " name "="); suspendEeSerialPutHex32(value)
#define C26_HEX64(name, value) \
    suspendEeSerialPutString(" " name "="); suspendEeSerialPutHex64(value)
    C26_HEX32("preflightProven", d.c011ec26PreflightProven);
    C26_HEX32("terminalClassification", d.c011ec26TerminalClassificationResult);
    C26_HEX32("terminalDescriptorValid", d.c011ec26TerminalDescriptorValid);
    C26_HEX32("terminalLookupAttempts", d.c011ec26TerminalLookupAttemptCount);
    C26_HEX32("terminalLookupSuccesses", d.c011ec26TerminalLookupSuccessCount);
    C26_HEX32("iteratorCompletionCount", d.c011ec26IteratorCompletionCount);
    C26_HEX32("stackProviderCallbackEntries", d.c011ec26StackProviderCallbackEntryCount);
    C26_HEX32("stackProviderCallbackReturns", d.c011ec26StackProviderCallbackReturnCount);
    C26_HEX32("gcScanRootsEntries", d.c011ec26GcScanRootsEntryCount);
    C26_HEX32("gcScanRootsReturns", d.c011ec26GcScanRootsReturnCount);
    C26_HEX32("threadGcScanRootsEntries", d.c011ec26ThreadGcScanRootsEntryCount);
    C26_HEX32("threadGcScanRootsReturns", d.c011ec26ThreadGcScanRootsReturnCount);
    C26_HEX32("rootEnumerationComplete", d.c011ec26GcScanRootsEnumerationComplete);
    C26_HEX32("nativeUnwindCount", d.c011ec23UnwindAttemptCount);
    C26_HEX32("thirdUnwindAttempts", d.c011ec26ThirdUnwindAttemptCount);
    C26_HEX32("iteratorFrames", d.c011ec18StackFrameCount);
    C26_HEX32("managedFrames", d.c011ec18StackFrameCount);
    C26_HEX32("stackBoundsConsumed", d.c011ec18StackBoundsConsumed);
    C26_HEX32("totalRoots", d.c011ec15RootSlotVisitCount);
    C26_HEX32("category3Roots", d.c011ec19RootReportCount);
    C26_HEX32("registerRoots", d.c011ec19RegisterRootCount);
    C26_HEX32("stackRoots", d.c011ec19StackRootCount);
    C26_HEX32("promoteAttempts", d.c011ec19FirstStackDerivedPromoteAttemptCount);
    C26_HEX32("promoteEntries", d.c011ec19FirstStackDerivedPromoteEntryCount);
    C26_HEX32("promoteReturns", d.c011ec19FirstStackDerivedPromoteReturnCount);
    C26_HEX64("queueCursorBeforeStack", d.c011ec26QueueCursorBeforeStack);
    C26_HEX64("queueCursorAfterStack", d.c011ec26QueueCursorAfterStack);
    C26_HEX64("queueCursorAtGcScanRootsReturn", d.c011ec26QueueCursorAtGcScanRootsReturn);
    C26_HEX32("firstPostScanEvent", d.c011ec26FirstPostScanEvent);
    C26_HEX32("firstPostScanQueueOperation", d.c011ec26FirstPostScanQueueOperation);
    C26_HEX32("firstPostStackRootSource", d.c011ec26FirstPostStackRootSource);
    C26_HEX32("postStackRootSourceCount", d.c011ec26PostStackRootSourceCount);
    C26_HEX32("stackScanTotalRoots", d.c011ec26StackScanTotalRootCount);
    C26_HEX32("stackScanCategory3Roots", d.c011ec26StackScanCategory3RootCount);
    C26_HEX32("stackScanRegisterRoots", d.c011ec26StackScanRegisterRootCount);
    C26_HEX32("stackScanStackRoots", d.c011ec26StackScanStackRootCount);
    C26_HEX32("stackScanPromoteAttempts", d.c011ec26StackScanPromoteAttemptCount);
    C26_HEX32("stackScanPromoteEntries", d.c011ec26StackScanPromoteEntryCount);
    C26_HEX32("stackScanPromoteReturns", d.c011ec26StackScanPromoteReturnCount);
    C26_HEX32("markWrites", d.c011ec15MarkBitWriteCount);
    C26_HEX32("childReads", d.c011ec15ChildReferenceReadCount);
    C26_HEX32("graphTraversal", d.c011ec15GraphTraversalCount);
    C26_HEX32("threadStoreLockHeld", d.c011ec15ThreadStoreLockHeld);
    C26_HEX32("eeSuspended", d.c011ec15EeSuspended);
    C26_HEX32("managedEntryProhibited", d.c011ec15ManagedEntryProhibited);
    C26_HEX32("cooperative", d.rootThreadRecords[0].cooperative);
    C26_HEX32("preemptive", d.rootThreadRecords[0].preemptive);
    C26_HEX32("threadUnderCrawl", d.callbackContextThreadUnderCrawl != 0u ? 1u : 0u);
    C26_HEX32("restart", d.restartRequestCount + d.restartEntryCount);
    C26_HEX32("resume", d.managedResumeCount);
    C26_HEX32("safeStopReason", d.c011ec26SafeStopReason);
    C26_HEX64("terminalInputPC", d.c011ec26TerminalInputPc);
    C26_HEX64("terminalSelectedPC", d.c011ec26TerminalSelectedPc);
    C26_HEX64("terminalLinkedPC", d.c011ec26TerminalLinkedPc);
    C26_HEX64("terminalModuleBase", d.c011ec26TerminalModuleBase);
    C26_HEX64("terminalExecutableStart", d.c011ec26TerminalExecutableStart);
    C26_HEX64("terminalExecutableEnd", d.c011ec26TerminalExecutableEnd);
    C26_HEX64("terminalBeginRVA", d.c011ec26TerminalBeginRva);
    C26_HEX64("terminalEndRVA", d.c011ec26TerminalEndRva);
    C26_HEX64("terminalRSP", d.c011ec26TerminalRsp);
    C26_HEX64("postScanAddress", d.c011ec26PostScanAddress);
    C26_HEX64("stackBase", d.rootThreadRecords[0].stackLow);
    C26_HEX64("stackLimit", d.rootThreadRecords[0].stackHigh);
    C26_HEX64("scanContextStackLimit", d.callbackContextStackLimit);
#undef C26_HEX32
#undef C26_HEX64
    suspendEeSerialPutString("\n");
}

extern "C" void __cdecl
guideXosNativeAotC011EC26GcScanRootsEntered(
    int condemned, int maxGeneration, uintptr_t scanContext) {
    (void)condemned;
    (void)maxGeneration;
    (void)scanContext;
    ++g_guideXosAllocationDiagnostics.c011ec26GcScanRootsEntryCount;
}

extern "C" void __cdecl
guideXosNativeAotC011EC26ThreadGcScanRootsEntered() {
    ++g_guideXosAllocationDiagnostics.c011ec26ThreadGcScanRootsEntryCount;
}

extern "C" void __cdecl
guideXosNativeAotC011EC26ThreadGcScanRootsReturned() {
    ++g_guideXosAllocationDiagnostics.c011ec26ThreadGcScanRootsReturnCount;
}

extern "C" void __cdecl
guideXosNativeAotC011EC26IteratorCompleted() {
    guidexos_nativeaot_allocation_diagnostics& d =
        g_guideXosAllocationDiagnostics;
    ++d.c011ec26IteratorCompletionCount;
    d.c011ec26StackScanTotalRootCount = d.c011ec15RootSlotVisitCount;
    d.c011ec26StackScanCategory3RootCount = d.c011ec19RootReportCount;
    d.c011ec26StackScanRegisterRootCount = d.c011ec19RegisterRootCount;
    d.c011ec26StackScanStackRootCount = d.c011ec19StackRootCount;
    d.c011ec26StackScanPromoteAttemptCount =
        d.c011ec19FirstStackDerivedPromoteAttemptCount;
    d.c011ec26StackScanPromoteEntryCount =
        d.c011ec19FirstStackDerivedPromoteEntryCount;
    d.c011ec26StackScanPromoteReturnCount =
        d.c011ec19FirstStackDerivedPromoteReturnCount;
    d.c011ec26QueueCursorBeforeStack = d.c011ec19SecondQueueCursorBefore;
    d.c011ec26QueueCursorAfterStack = d.c011ec19SecondQueueCursorAfter;
}

extern "C" void __cdecl
guideXosNativeAotC011EC26PostStackRootSource(uint32_t source) {
    guidexos_nativeaot_allocation_diagnostics& d =
        g_guideXosAllocationDiagnostics;
    ++d.c011ec26PostStackRootSourceCount;
    if (d.c011ec26FirstPostStackRootSource == 0u) {
        d.c011ec26FirstPostStackRootSource = source;
    }
}

extern "C" void __cdecl
guideXosNativeAotC011EC26StackProviderEntered() {
    ++g_guideXosAllocationDiagnostics.c011ec26StackProviderCallbackEntryCount;
}

extern "C" void __cdecl
guideXosNativeAotC011EC26StackProviderReturned() {
    guidexos_nativeaot_allocation_diagnostics& d =
        g_guideXosAllocationDiagnostics;
    ++d.c011ec26StackProviderCallbackReturnCount;
    d.c011ec26QueueCursorAfterStack = d.c011ec19SecondQueueCursorAfter;
}

extern "C" void __cdecl
guideXosNativeAotC011EC26GcScanRootsReturned() {
    guidexos_nativeaot_allocation_diagnostics& d =
        g_guideXosAllocationDiagnostics;
    ++d.c011ec26GcScanRootsReturnCount;
    d.c011ec26GcScanRootsEnumerationComplete = 1u;
    d.c011ec26QueueCursorAtGcScanRootsReturn =
        d.c011ec19SecondQueueCursorAfter;
}

extern "C" void __cdecl
guideXosNativeAotC011EC26PostScanAfterGcScanRootsEntered() {
    guidexos_nativeaot_allocation_diagnostics& d =
        g_guideXosAllocationDiagnostics;
    if (d.c011ec26FirstPostScanEvent == 0u) {
        d.c011ec26FirstPostScanEvent = 1u;
        d.c011ec26FirstPostScanQueueOperation = 0u;
        d.c011ec26PostScanAddress = reinterpret_cast<uintptr_t>(_ReturnAddress());
    }
    if (d.c011ec26MarkerEmitted == 0u &&
        d.c011ec26PreflightProven != 0u &&
        d.c011ec26TerminalDescriptorValid != 0u &&
        d.c011ec26IteratorCompletionCount == 1u &&
        d.c011ec26StackProviderCallbackEntryCount == 1u &&
        d.c011ec26StackProviderCallbackReturnCount == 1u &&
        d.c011ec26ThreadGcScanRootsEntryCount == 1u &&
        d.c011ec26ThreadGcScanRootsReturnCount == 1u &&
        d.c011ec26GcScanRootsEntryCount == 1u &&
        d.c011ec26GcScanRootsReturnCount == 1u &&
        d.c011ec26GcScanRootsEnumerationComplete != 0u &&
        d.c011ec23UnwindAttemptCount == 2u &&
        d.c011ec26ThirdUnwindAttemptCount == 0u) {
        d.c011ec26MarkerEmitted = 1u;
        d.c011ec26SafeStopReason = 0u;
        emitC011EC26Completion();
    }
}
#endif

#if defined(GUIDEXOS_NATIVEAOT_C011EC27_POST_ROOT_QUEUE)
static void emitC011EC27Preflight() {
    const guidexos_nativeaot_allocation_diagnostics& d =
        g_guideXosAllocationDiagnostics;
    suspendEeSerialPutString(
        "[nativeaot-gc-post-root-queue] preflight marker=C011EC27-PREFLIGHT");
#define C27PF32(name, value) \
    suspendEeSerialPutString(" " name "="); suspendEeSerialPutHex32(value)
#define C27PF64(name, value) \
    suspendEeSerialPutString(" " name "="); suspendEeSerialPutHex64(value)
    C27PF32("c26Completion", d.c011ec26MarkerEmitted);
    C27PF32("rootEnumerationComplete", d.c011ec26GcScanRootsEnumerationComplete);
    C27PF32("afterGcScanRoots", d.c011ec27AfterGcScanRootsReached);
    C27PF32("queueItemsConsumed", d.c011ec27QueueItemConsumedCount);
    C27PF32("markStateReads", d.c011ec27MarkStateReadCount);
    C27PF64("queueOwner", d.c011ec27QueueOwner);
    C27PF64("queueBase", d.c011ec27QueueBase);
    C27PF64("consumedSlot", d.c011ec27ConsumedSlot);
    C27PF64("consumedObject", d.c011ec27ConsumedObject);
    C27PF64("markWordAddress", d.c011ec27MarkWordAddress);
    C27PF64("markMask", d.c011ec27MarkMask);
#undef C27PF32
#undef C27PF64
    suspendEeSerialPutString("\n");
}

static void emitC011EC27Completion() {
    const guidexos_nativeaot_allocation_diagnostics& d =
        g_guideXosAllocationDiagnostics;
    suspendEeSerialPutString(
        "[nativeaot-gc-post-root-queue] COMPLETE marker=C011EC27");
#define C27_HEX32(name, value) \
    suspendEeSerialPutString(" " name "="); suspendEeSerialPutHex32(value)
#define C27_HEX64(name, value) \
    suspendEeSerialPutString(" " name "="); suspendEeSerialPutHex64(value)
    C27_HEX32("successLevel", d.c011ec27OutcomeLevel);
    C27_HEX32("c26Completion", d.c011ec26MarkerEmitted);
    C27_HEX32("iteratorCompletionCount", d.c011ec26IteratorCompletionCount);
    C27_HEX32("gcScanRootsEntries", d.c011ec26GcScanRootsEntryCount);
    C27_HEX32("gcScanRootsReturns", d.c011ec26GcScanRootsReturnCount);
    C27_HEX32("rootEnumerationComplete", d.c011ec26GcScanRootsEnumerationComplete);
    C27_HEX32("totalRoots", d.c011ec15RootSlotVisitCount);
    C27_HEX32("category3Roots", d.c011ec19RootReportCount);
    C27_HEX32("registerRoots", d.c011ec19RegisterRootCount);
    C27_HEX32("stackRoots", d.c011ec19StackRootCount);
    C27_HEX32("promoteAttempts", d.c011ec19FirstStackDerivedPromoteAttemptCount);
    C27_HEX32("promoteEntries", d.c011ec19FirstStackDerivedPromoteEntryCount);
    C27_HEX32("promoteReturns", d.c011ec19FirstStackDerivedPromoteReturnCount);
    C27_HEX32("firstPostStackRootSource", d.c011ec26FirstPostStackRootSource);
    C27_HEX32("postStackRootSourceCount", d.c011ec26PostStackRootSourceCount);
    C27_HEX64("queueCursorBeforeStack", d.c011ec26QueueCursorBeforeStack);
    C27_HEX64("queueCursorAfterStack", d.c011ec26QueueCursorAfterStack);
    C27_HEX64("queueCursorAtGcScanRootsReturn", d.c011ec26QueueCursorAtGcScanRootsReturn);
    C27_HEX32("afterGcScanRoots", d.c011ec27AfterGcScanRootsReached);
    C27_HEX64("afterGcScanRootsAddress", d.c011ec27AfterGcScanRootsAddress);
    C27_HEX32("queueItemsConsumed", d.c011ec27QueueItemConsumedCount);
    C27_HEX64("queueOwner", d.c011ec27QueueOwner);
    C27_HEX64("queueBase", d.c011ec27QueueBase);
    C27_HEX64("queueCursorBefore", d.c011ec27QueueCursorBefore);
    C27_HEX64("consumedIndex", d.c011ec27ConsumedSlotIndex);
    C27_HEX64("consumedSlot", d.c011ec27ConsumedSlot);
    C27_HEX64("consumedObject", d.c011ec27ConsumedObject);
    C27_HEX64("firstQueueInsertionObject", d.c011ec15FirstQueueNewValue);
    C27_HEX64("firstRootValue", d.c011ec15FirstRootValue);
    C27_HEX32("firstRootProviderCategory", d.c011ec15FirstRootProviderCategory);
    C27_HEX32("consumedObjectSourceCategory",
        d.c011ec27ConsumedObject == d.c011ec15FirstQueueNewValue
            ? d.c011ec15FirstRootProviderCategory : 0u);
    C27_HEX64("sentinel", d.threadStaticProofSentinelAddress);
    C27_HEX64("storageObject", d.runtimeThreadStaticStorageObjectAddress);
    C27_HEX64("consumedSlotValueAfter", d.c011ec27ConsumedSlotValueAfter);
    C27_HEX64("queueCursorAfterConsumption", d.c011ec27QueueCursorAfterConsumption);
    C27_HEX64("queueInsertionsAtConsumed", d.c011ec27QueueInsertionsAtConsumed);
    C27_HEX64("queueInsertionsAtAfter", d.c011ec27QueueInsertionsAtAfter);
    C27_HEX32("newQueueInsertion", d.c011ec27NewQueueInsertionCount);
    C27_HEX32("markStateReads", d.c011ec27MarkStateReadCount);
    C27_HEX32("markStateBefore", (d.c011ec27MarkWordBefore & d.c011ec27MarkMask) != 0u ? 1u : 0u);
    C27_HEX32("markTestResult", d.c011ec27MarkStateResult);
    C27_HEX64("markWordAddress", d.c011ec27MarkWordAddress);
    C27_HEX64("markWordBefore", d.c011ec27MarkWordBefore);
    C27_HEX64("markMask", d.c011ec27MarkMask);
    C27_HEX32("markWriteAttempted", d.c011ec27MarkWriteAttemptCount != 0u ? 1u : 0u);
    C27_HEX32("markWrites", d.c011ec27MarkWriteCount);
    C27_HEX64("markWordAfter", d.c011ec27MarkWordAfter);
    C27_HEX32("newlyMarked", d.c011ec27MarkWriteCount != 0u ? 1u : 0u);
    C27_HEX32("childScanAttempted", d.c011ec27ChildScanAttemptCount);
    C27_HEX32("childReads", d.c011ec27ChildReferenceReadCount);
    C27_HEX32("childPromoteAttempted", d.c011ec27ChildPromoteAttemptCount);
    C27_HEX32("graphTraversal", d.c011ec27GraphTraversalCount);
    C27_HEX64("parentObject", d.c011ec27ParentObject);
    C27_HEX64("parentMethodTable", d.c011ec27ParentMethodTable);
    C27_HEX64("childSlot", d.c011ec27ChildSlot);
    C27_HEX64("childValue", d.c011ec27ChildValue);
    C27_HEX32("c26NativeUnwinds", d.c011ec23UnwindAttemptCount);
    C27_HEX32("c26ThirdUnwindAttempts", d.c011ec26ThirdUnwindAttemptCount);
    C27_HEX32("stackBoundsConsumed", d.c011ec18StackBoundsConsumed);
    C27_HEX64("stackBase", d.rootThreadRecords[0].stackLow);
    C27_HEX64("stackLimit", d.rootThreadRecords[0].stackHigh);
    C27_HEX64("scanContextStackLimit", d.callbackContextStackLimit);
    C27_HEX32("queueInvariantFailures", d.c011ec27QueueInvariantFailures);
    C27_HEX32("objectInvariantFailures", d.c011ec27ObjectInvariantFailures);
    C27_HEX32("eeSuspended", d.c011ec15EeSuspended);
    C27_HEX32("cooperative", d.rootThreadRecords[0].cooperative);
    C27_HEX32("preemptive", d.rootThreadRecords[0].preemptive);
    C27_HEX32("restart", d.restartRequestCount + d.restartEntryCount);
    C27_HEX32("resume", d.managedResumeCount);
    C27_HEX32("safeStopReason", d.c011ec27SafeStopReason);
#undef C27_HEX32
#undef C27_HEX64
    suspendEeSerialPutString("\n");
}

extern "C" void __cdecl
guideXosNativeAotC011EC27QueueItemConsumed(
    uintptr_t queueOwner, uintptr_t queueBase, uintptr_t slotAddress,
    uintptr_t slotIndex, uintptr_t cursorBefore, uintptr_t object,
    uintptr_t slotValueAfter, uintptr_t cursorAfterConsumption) {
    guidexos_nativeaot_allocation_diagnostics& d =
        g_guideXosAllocationDiagnostics;
    ++d.c011ec27QueueItemConsumedCount;
    if (d.c011ec27QueueItemConsumedCount != 1u) return;
    d.c011ec27QueueOwner = queueOwner;
    d.c011ec27QueueBase = queueBase;
    d.c011ec27ConsumedSlot = slotAddress;
    d.c011ec27ConsumedSlotIndex = slotIndex;
    d.c011ec27QueueCursorBefore = cursorBefore;
    d.c011ec27ConsumedObject = object;
    d.c011ec27ConsumedSlotValueAfter = slotValueAfter;
    d.c011ec27QueueCursorAfterConsumption = cursorAfterConsumption;
    d.c011ec27QueueInsertionsAtConsumed = d.c011ec15QueueMarkReturnCount;
    const uintptr_t expectedSlot =
        queueBase + slotIndex * sizeof(uintptr_t);
    if (queueOwner == 0u || queueBase == 0u || slotAddress != expectedSlot ||
        slotIndex >= 16u || cursorBefore >= 16u ||
        cursorAfterConsumption >= 16u || slotValueAfter != 0u ||
        object == 0u || (object & (sizeof(uintptr_t) - 1u)) != 0u) {
        ++d.c011ec27QueueInvariantFailures;
    }
}

extern "C" void __cdecl
guideXosNativeAotC011EC27MarkStateRead(
    uintptr_t object, uintptr_t headerAddress, uintptr_t rawHeader,
    uintptr_t markMask, uintptr_t result) {
    guidexos_nativeaot_allocation_diagnostics& d =
        g_guideXosAllocationDiagnostics;
    ++d.c011ec27MarkStateReadCount;
    if (object != d.c011ec27ConsumedObject || d.c011ec27MarkWordAddress != 0u) {
        return;
    }
    d.c011ec27MarkWordAddress = headerAddress;
    d.c011ec27MarkWordBefore = rawHeader;
    d.c011ec27MarkMask = markMask;
    d.c011ec27MarkStateResult = result != 0u ? 1u : 0u;
}

extern "C" void __cdecl
guideXosNativeAotC011EC27MarkWriteAttempted(
    uintptr_t object, uintptr_t headerAddress, uintptr_t rawHeaderBefore,
    uintptr_t markMask) {
    guidexos_nativeaot_allocation_diagnostics& d =
        g_guideXosAllocationDiagnostics;
    ++d.c011ec27MarkWriteAttemptCount;
    if (object == d.c011ec27ConsumedObject && d.c011ec27MarkWordAddress == 0u) {
        d.c011ec27MarkWordAddress = headerAddress;
        d.c011ec27MarkWordBefore = rawHeaderBefore;
        d.c011ec27MarkMask = markMask;
    }
}

extern "C" void __cdecl
guideXosNativeAotC011EC27MarkWriteCompleted(
    uintptr_t object, uintptr_t headerAddress, uintptr_t rawHeaderAfter,
    uintptr_t markMask, uintptr_t cursorAfter) {
    guidexos_nativeaot_allocation_diagnostics& d =
        g_guideXosAllocationDiagnostics;
    ++d.c011ec27MarkWriteCount;
    if (object != d.c011ec27ConsumedObject || d.c011ec27MarkWordAddress == 0u) {
        return;
    }
    d.c011ec27MarkWordAddress = headerAddress;
    d.c011ec27MarkWordAfter = rawHeaderAfter;
    d.c011ec27MarkMask = markMask;
    if ((rawHeaderAfter & markMask) == 0u || cursorAfter >= 16u) {
        ++d.c011ec27ObjectInvariantFailures;
    }
}

extern "C" void __cdecl
guideXosNativeAotC011EC27ChildScanAttempted(
    uintptr_t parent, uintptr_t methodTable, uintptr_t objectSize) {
    guidexos_nativeaot_allocation_diagnostics& d =
        g_guideXosAllocationDiagnostics;
    ++d.c011ec27ChildScanAttemptCount;
    ++d.c011ec27GraphTraversalCount;
    if (d.c011ec27ParentObject == 0u) {
        d.c011ec27ParentObject = parent;
        d.c011ec27ParentMethodTable = methodTable;
    }
    if (parent == 0u || methodTable == 0u || objectSize == 0u) {
        ++d.c011ec27ObjectInvariantFailures;
    }
}

extern "C" void __cdecl
guideXosNativeAotC011EC27ChildReferenceRead(
    uintptr_t parent, uintptr_t slot, uintptr_t child, uintptr_t classification) {
    guidexos_nativeaot_allocation_diagnostics& d =
        g_guideXosAllocationDiagnostics;
    ++d.c011ec27ChildReferenceReadCount;
    if (d.c011ec27ChildSlot == 0u) {
        d.c011ec27ParentObject = parent;
        d.c011ec27ChildSlot = slot;
        d.c011ec27ChildValue = child;
    }
    (void)classification;
}

extern "C" void __cdecl
guideXosNativeAotC011EC27ChildPromoteAttempted(
    uintptr_t parent, uintptr_t slot, uintptr_t child) {
    ++g_guideXosAllocationDiagnostics.c011ec27ChildPromoteAttemptCount;
    (void)parent;
    (void)slot;
    (void)child;
}

extern "C" __declspec(noreturn) void __cdecl
guideXosNativeAotC011EC27PostRootAfterGcScanRootsEntered() {
    guidexos_nativeaot_allocation_diagnostics& d =
        g_guideXosAllocationDiagnostics;
    d.c011ec27AfterGcScanRootsReached = 1u;
    d.c011ec27AfterGcScanRootsAddress = reinterpret_cast<uintptr_t>(_ReturnAddress());
    d.c011ec27QueueInsertionsAtAfter = d.c011ec15QueueMarkReturnCount;
    if (d.c011ec27QueueInsertionsAtConsumed != 0u &&
        d.c011ec27QueueInsertionsAtAfter >= d.c011ec27QueueInsertionsAtConsumed) {
        d.c011ec27NewQueueInsertionCount = static_cast<uint32_t>(
            d.c011ec27QueueInsertionsAtAfter - d.c011ec27QueueInsertionsAtConsumed);
    }
    const bool c26Complete =
        d.c011ec26MarkerEmitted != 0u &&
        d.c011ec26GcScanRootsEnumerationComplete != 0u &&
        d.c011ec26IteratorCompletionCount == 1u &&
        d.c011ec26GcScanRootsReturnCount == 1u &&
        d.c011ec26StackProviderCallbackReturnCount == 1u &&
        d.c011ec23UnwindAttemptCount == 2u &&
        d.c011ec26ThirdUnwindAttemptCount == 0u;
    const bool consumed =
        d.c011ec27QueueItemConsumedCount != 0u &&
        d.c011ec27ConsumedObject != 0u &&
        d.c011ec27QueueInvariantFailures == 0u;
    const bool markResolved =
        d.c011ec27MarkStateReadCount != 0u &&
        d.c011ec27MarkWordAddress != 0u &&
        d.c011ec27MarkMask != 0u;
    if (c26Complete && consumed && markResolved) {
        d.c011ec27PreflightProven = 1u;
        d.c011ec27OutcomeLevel = d.c011ec27ChildReferenceReadCount != 0u
            ? 3u : (d.c011ec27MarkWriteCount != 0u ? 2u : 1u);
        emitC011EC27Preflight();
        d.c011ec27MarkerEmitted = 1u;
        d.c011ec27SafeStopReason = 0u;
        emitC011EC27Completion();
    } else {
        d.c011ec27SafeStopReason =
            consumed ? 0xC0270002u : 0xC0270001u;
        suspendEeSerialPutString(
            "[nativeaot-gc-post-root-queue] BLOCKED outcome=E marker=C011EC27-BLOCKED\n");
    }
    for (;;) {
    }
}
#endif

#if defined(GUIDEXOS_NATIVEAOT_C011EC21_NATIVE_CONTINUATION)
extern "C" __declspec(noreturn) void __cdecl
guideXosNativeAotC011EC21SafeStop(uint32_t reason) {
    guidexos_nativeaot_allocation_diagnostics& d =
        g_guideXosAllocationDiagnostics;
    d.c011ec21MarkerEmitted = 1u;
    d.c011ec21Outcome = 5u;
    d.c011ec20SafeStopReason = reason;
    d.safeStopObserved = 1u;
    d.stopReason = GUIDEXOS_NATIVEAOT_CALLER_FRAME_UNWIND_MARKER;
    d.stage = GUIDEXOS_NATIVEAOT_ALLOC_STAGE_F33_NEXT_GENUINE_ROOT_PROVIDER_SAFE_STOP;
    emitC011EC20SafeStop();
    for (;;) {
    }
}
#endif

extern "C" __declspec(noreturn) void __cdecl
guideXosNativeAotC011EC20SafeStop(uint32_t reason) {
    guidexos_nativeaot_allocation_diagnostics& d =
        g_guideXosAllocationDiagnostics;
    d.c011ec20SafeStopReason = reason;
    d.safeStopObserved = 1u;
    d.stopReason = GUIDEXOS_NATIVEAOT_CALLER_FRAME_UNWIND_MARKER;
    d.stage = GUIDEXOS_NATIVEAOT_ALLOC_STAGE_F33_NEXT_GENUINE_ROOT_PROVIDER_SAFE_STOP;
    emitC011EC20SafeStop();
    for (;;) {
    }
}
#endif

#endif

#endif

#endif

#if defined(GUIDEXOS_NATIVEAOT_FIRST_NON_NULL_ROOT_ALLOCATION)

uint32_t classifyFirstNonNullRootKnownAddress(uintptr_t rawValue) {
    const guidexos_nativeaot_allocation_diagnostics& diagnostics =
        g_guideXosAllocationDiagnostics;
    if (rawValue == 0u) return 1u;
    if (rawValue == diagnostics.runtimeThreadStaticStorageObjectAddress) return 8u;
    if (rawValue == diagnostics.threadStaticProofSentinelAddress) return 6u;
    if (rawValue == diagnostics.rootEnumeratedThreadIdentity) return 2u;
    if (rawValue == diagnostics.candidateMetadataContainerIdentity) return 3u;
    if (rawValue == diagnostics.candidateSlotAddress) return 4u;
    if (rawValue == diagnostics.rootThreadRecords[0].allocationContext) return 5u;
    for (uint32_t index = 0u;
         index < diagnostics.objectHistoryCount &&
         index < GUIDEXOS_NATIVEAOT_MAX_OBJECT_HISTORY;
         ++index) {
        if (rawValue == diagnostics.objectHistory[index].address) {
            return diagnostics.objectHistory[index].sentinel != 0u ? 6u : 7u;
        }
    }
    return 0u;
}

extern "C" void __cdecl
guideXosNativeAotFirstNonNullRootCandidateLoadRequested(
    uintptr_t slot, uint32_t flags, uintptr_t callback, uintptr_t scanContext) {
    guidexos_nativeaot_allocation_diagnostics& diagnostics =
        g_guideXosAllocationDiagnostics;
    if (diagnostics.candidateSlotVisitCount >=
        GUIDEXOS_NATIVEAOT_MAX_CANDIDATE_SLOTS) {
        diagnostics.candidateBoundReached = 1u;
        firstNonNullRootCallbackBoundarySafeStop();
    }

    const uint32_t ordinal = diagnostics.candidateSlotVisitCount++;
    if (ordinal == 0u) {
        validateAllocationContextFixupObjects(true);
        diagnostics.candidateObjectValidationBeforeLoadCount =
            diagnostics.objectHistoryCount;
    }
    guidexos_nativeaot_candidate_slot_record& record =
        diagnostics.candidateSlotRecords[ordinal];
    record = {};
    record.ordinal = ordinal + 1u;
    record.loadCount = 1u;
    record.rawRootFlags = flags;
    record.rootKind = 1u;
    record.slotAddress = slot;
    record.callbackIdentity = callback;
    record.scanContextIdentity = scanContext;

    ++diagnostics.candidateLoadRequestCount;
    ++diagnostics.candidateLoadEntryCount;
    diagnostics.candidateSlotAddress = slot;
    diagnostics.candidateLoadAddress = slot;
    diagnostics.candidateSlotWidth = static_cast<uint32_t>(sizeof(uintptr_t));
    diagnostics.candidateSlotAlignment =
        static_cast<uint32_t>(slot & (sizeof(uintptr_t) - 1u));
    diagnostics.candidateRawRootFlags = flags;
    diagnostics.candidateRootKind = 1u;
    diagnostics.candidateCallbackIdentity = callback;
    diagnostics.candidateScanContextIdentity = scanContext;
#if defined(GUIDEXOS_NATIVEAOT_FIRST_ROOT_CALLBACK_ENTRY_ALLOCATION)
    ++diagnostics.callbackRequestCount;
    if (diagnostics.callbackRequestCount != 1u) {
        ++diagnostics.duplicateCallbackInvocationCount;
    }
#endif
    diagnostics.candidateProviderThreadIdentity =
        diagnostics.firstRootProviderThread;
    diagnostics.candidateOwnerThreadIdentity =
        diagnostics.rootEnumeratedThreadIdentity;
    diagnostics.candidateProviderFunctionCode =
        diagnostics.rootProviderFunctionCode;
    diagnostics.candidateMetadataContainerIdentity =
        diagnostics.firstRootProviderMetadataContainer;
    diagnostics.firstRootCandidateMetadataLocation = slot;
    diagnostics.candidateSlotStable = 1u;
    diagnostics.candidateSlotExpectedThreadStorage =
        diagnostics.candidateProviderThreadIdentity != 0u &&
        diagnostics.candidateProviderThreadIdentity ==
            diagnostics.candidateOwnerThreadIdentity ? 1u : 0u;
    diagnostics.candidateSlotOverlapsRuntimeThread = 0u;
    diagnostics.candidateSlotOverlapsManagedHeap = 0u;
    diagnostics.candidateSlotOverlapsAllocationContext = 0u;
    diagnostics.candidateSlotOverlapsNativeStack = 0u;
    diagnostics.candidateSlotOverlapsOtherKnownRegion = 0u;
    diagnostics.candidateSlotWritableContract = 1u;
    diagnostics.threadStaticProofInitializationIndicator = 3u;

    if (diagnostics.candidateSlotExpectedThreadStorage == 0u ||
        diagnostics.candidateSlotAlignment != 0u ||
        diagnostics.candidateSlotWidth != sizeof(uintptr_t) ||
        diagnostics.rootProviderEntryCount == 0u ||
        diagnostics.threadStoreLockRecursionDepth != 1u ||
        diagnostics.managedEntryProhibited == 0u ||
        diagnostics.eeSuspended == 0u) {
        ++diagnostics.rootProviderInvariantFailures;
        firstNonNullRootCallbackBoundarySafeStop();
    }
}

extern "C" uint32_t __cdecl
guideXosNativeAotFirstNonNullRootCandidateMachineWordLoaded(
    uintptr_t slot, uintptr_t rawValue) {
    guidexos_nativeaot_allocation_diagnostics& diagnostics =
        g_guideXosAllocationDiagnostics;
    if (diagnostics.candidateSlotVisitCount == 0u ||
        diagnostics.candidateSlotVisitCount > GUIDEXOS_NATIVEAOT_MAX_CANDIDATE_SLOTS) {
        ++diagnostics.candidateDuplicateLoadCount;
        firstNonNullRootCallbackBoundarySafeStop();
    }

    guidexos_nativeaot_candidate_slot_record& record =
        diagnostics.candidateSlotRecords[diagnostics.candidateSlotVisitCount - 1u];
    if (record.slotAddress != slot || record.loadCount != 1u) {
        ++diagnostics.candidateDuplicateLoadCount;
        firstNonNullRootCallbackBoundarySafeStop();
    }
    record.rawValue = rawValue;
    record.valueIsNull = rawValue == 0u ? 1u : 0u;
    record.knownAddressMatch = classifyFirstNonNullRootKnownAddress(rawValue);
    record.exactSelectedSentinelMatch =
        rawValue != 0u && rawValue == diagnostics.threadStaticProofSentinelAddress ? 1u : 0u;

    ++diagnostics.candidateMachineWordLoadCount;
    ++diagnostics.candidateLoadSuccess;
    diagnostics.candidateLoadAddress = slot;
    diagnostics.candidateRawValue = rawValue;
    diagnostics.candidateValueIsNull = rawValue == 0u ? 1u : 0u;
    diagnostics.candidateKnownAddressMatch = record.knownAddressMatch;
    diagnostics.candidateExpectedSentinelAddress =
        diagnostics.threadStaticProofSentinelAddress;
    diagnostics.candidateExpectedStorageObjectAddress =
        diagnostics.runtimeThreadStaticStorageObjectAddress;
    if (rawValue == 0u) {
        ++diagnostics.candidateNullCount;
        return 0u;
    }

    ++diagnostics.candidateNonNullCount;
    diagnostics.candidateFirstNonNullValue = rawValue;
    diagnostics.candidateFirstNonNullSlot = slot;
    diagnostics.candidateFirstNonNullKnownAddressMatch = record.knownAddressMatch;
    diagnostics.candidateProofRootObserved =
        record.exactSelectedSentinelMatch != 0u ? 1u : 0u;
    diagnostics.candidateMatchesProofRoot =
        record.exactSelectedSentinelMatch != 0u ? 1u : 0u;
    diagnostics.candidateMatchesStorageObject =
        rawValue != 0u &&
        rawValue == diagnostics.runtimeThreadStaticStorageObjectAddress ? 1u : 0u;
    diagnostics.candidateUnexpectedNonNull =
        record.exactSelectedSentinelMatch != 0u ? 0u : 1u;
    diagnostics.candidateProviderTerminated = 1u;
    validateAllocationContextFixupObjects(true);
    diagnostics.candidateObjectValidationAfterLoadCount =
        diagnostics.objectHistoryCount;
#if defined(GUIDEXOS_NATIVEAOT_FIRST_ROOT_CALLBACK_ENTRY_ALLOCATION)
    return 1u;
#else
    firstNonNullRootCallbackBoundarySafeStop();
    return 1u;
#endif
}

#if defined(GUIDEXOS_NATIVEAOT_FIRST_ROOT_CALLBACK_ENTRY_ALLOCATION)
extern "C" void __cdecl
guideXosNativeAotFirstRootCallbackCallSiteEntered(
    uintptr_t slot, uintptr_t rawValue, uint32_t flags,
    uintptr_t callback, uintptr_t scanContext) {
    guidexos_nativeaot_allocation_diagnostics& diagnostics =
        g_guideXosAllocationDiagnostics;
    ++diagnostics.callbackCallSiteEntryCount;
#if defined(GUIDEXOS_NATIVEAOT_FIRST_ROOT_NON_NULL_OLD_O_ALLOCATION)
    diagnostics.callbackSiteRootSlot = slot;
    diagnostics.callbackSiteRawRootValue = rawValue;
    diagnostics.callbackSiteScanContext = scanContext;
    diagnostics.callbackSiteCallbackIdentity = callback;
    diagnostics.callbackActualRootFlags = flags;
    diagnostics.callbackSiteReturnAddress =
        reinterpret_cast<gx_uintptr>(_ReturnAddress());
    if (rawValue == 0u || callback == 0u || scanContext == 0u) {
        ++diagnostics.rootProviderInvariantFailures;
        guideXosFailFast(9u);
    }
#else
    if (diagnostics.callbackCallSiteEntryCount != 1u) {
        ++diagnostics.duplicateCallbackInvocationCount;
    }
    diagnostics.callbackSiteRootSlot = slot;
    diagnostics.callbackSiteRawRootValue = rawValue;
    diagnostics.callbackSiteScanContext = scanContext;
    diagnostics.callbackSiteCallbackIdentity = callback;
    diagnostics.callbackActualRootFlags = flags;
    diagnostics.callbackSiteReturnAddress =
        reinterpret_cast<gx_uintptr>(_ReturnAddress());
    if (diagnostics.callbackRequestCount != 1u ||
        diagnostics.callbackCallSiteEntryCount != 1u ||
        rawValue == 0u || callback == 0u || scanContext == 0u) {
        ++diagnostics.rootProviderInvariantFailures;
        guideXosFailFast(9u);
    }
#endif
}
#endif

#endif

#if defined(GUIDEXOS_NATIVEAOT_FIRST_ROOT_CALLBACK_ENTRY_ALLOCATION)

void emitFirstRootCallbackEntrySafeStop() {
    const guidexos_nativeaot_allocation_diagnostics& diagnostics =
        g_guideXosAllocationDiagnostics;
    suspendEeSerialPutString(
        "[nativeaot-gc-first-root-callback-entry] SAFE_STOP marker=");
    suspendEeSerialPutHex32(diagnostics.callbackSafeStopReason);
    suspendEeSerialPutString(" requestCount=");
    suspendEeSerialPutHex32(diagnostics.callbackRequestCount);
    suspendEeSerialPutString(" callSiteCount=");
    suspendEeSerialPutHex32(diagnostics.callbackCallSiteEntryCount);
    suspendEeSerialPutString(" invocationCount=");
    suspendEeSerialPutHex32(diagnostics.callbackInvocationCount);
    suspendEeSerialPutString(" entryCount=");
    suspendEeSerialPutHex32(diagnostics.callbackEntryCount);
    suspendEeSerialPutString(" returnCount=");
    suspendEeSerialPutHex32(diagnostics.callbackReturnCount);
    suspendEeSerialPutString(" duplicateInvocations=");
    suspendEeSerialPutHex32(diagnostics.duplicateCallbackInvocationCount);
    suspendEeSerialPutString(" callbackSiteSlot=");
    suspendEeSerialPutHex64(diagnostics.callbackSiteRootSlot);
    suspendEeSerialPutString(" callbackSiteRaw=");
    suspendEeSerialPutHex64(diagnostics.callbackSiteRawRootValue);
    suspendEeSerialPutString(" callbackSiteContext=");
    suspendEeSerialPutHex64(diagnostics.callbackSiteScanContext);
    suspendEeSerialPutString(" callbackSiteCallback=");
    suspendEeSerialPutHex64(diagnostics.callbackSiteCallbackIdentity);
    suspendEeSerialPutString(" callbackSiteReturn=");
    suspendEeSerialPutHex64(diagnostics.callbackSiteReturnAddress);
    suspendEeSerialPutString(" firstNonNullSlot=");
    suspendEeSerialPutHex64(diagnostics.candidateFirstNonNullSlot);
    suspendEeSerialPutString(" firstNonNullValue=");
    suspendEeSerialPutHex64(diagnostics.candidateFirstNonNullValue);
    suspendEeSerialPutString(" expectedStorageObject=");
    suspendEeSerialPutHex64(diagnostics.candidateExpectedStorageObjectAddress);
    suspendEeSerialPutString(" expectedSentinel=");
    suspendEeSerialPutHex64(diagnostics.candidateExpectedSentinelAddress);
    suspendEeSerialPutString(" managedAssignmentCount=");
    suspendEeSerialPutHex32(diagnostics.threadStaticProofAssignmentCount);
    suspendEeSerialPutString(" managedClearCount=");
    suspendEeSerialPutHex32(diagnostics.threadStaticProofClearCount);
    suspendEeSerialPutString(" managedReadbackCount=");
    suspendEeSerialPutHex32(diagnostics.threadStaticProofReadbackCount);
    suspendEeSerialPutString(" managedAssignmentValid=");
    suspendEeSerialPutHex32(diagnostics.threadStaticProofManagedAssignmentValid);
    suspendEeSerialPutString(" managedReadbackValid=");
    suspendEeSerialPutHex32(diagnostics.threadStaticProofManagedReadbackValid);
    suspendEeSerialPutString(" threadStaticInitialization=");
    suspendEeSerialPutHex32(diagnostics.threadStaticProofInitializationIndicator);
    suspendEeSerialPutString(" sentinelOrdinal=");
    suspendEeSerialPutHex32(diagnostics.threadStaticProofSentinelOrdinal);
    suspendEeSerialPutString(" sentinelAddress=");
    suspendEeSerialPutHex64(diagnostics.threadStaticProofSentinelAddress);
    suspendEeSerialPutString(" sentinelSize=");
    suspendEeSerialPutHex64(diagnostics.threadStaticProofSentinelSize);
    suspendEeSerialPutString(" readbackAddress=");
    suspendEeSerialPutHex64(diagnostics.threadStaticProofManagedReadbackAddress);
    suspendEeSerialPutString(" readbackExactMatch=");
    suspendEeSerialPutHex32(diagnostics.threadStaticProofReadbackExactMatch);
    suspendEeSerialPutString(" storageObject=");
    suspendEeSerialPutHex64(diagnostics.runtimeThreadStaticStorageObjectAddress);
    suspendEeSerialPutString(" inlinedRoot=");
    suspendEeSerialPutHex64(diagnostics.runtimeThreadStaticInlinedRootAddress);
    suspendEeSerialPutString(" objectBefore=");
    suspendEeSerialPutHex32(diagnostics.candidateObjectValidationBeforeLoadCount);
    suspendEeSerialPutString(" objectAfter=");
    suspendEeSerialPutHex32(diagnostics.candidateObjectValidationAfterLoadCount);
    suspendEeSerialPutString(" objectAtStop=");
    suspendEeSerialPutHex32(diagnostics.candidateObjectValidationAtStopCount);
    suspendEeSerialPutString(" sentinelChecks=");
    suspendEeSerialPutHex32(diagnostics.sentinelValidationCount);
    suspendEeSerialPutString(" objectHistoryOverflow=");
    suspendEeSerialPutHex32(diagnostics.objectHistoryOverflow);
    suspendEeSerialPutString(" callbackEntryAddress=");
    suspendEeSerialPutHex64(diagnostics.callbackEntryAddress);
    suspendEeSerialPutString(" callbackEntryReturn=");
    suspendEeSerialPutHex64(diagnostics.callbackEntryReturnAddress);
    suspendEeSerialPutString(" callbackEntryRsp=");
    suspendEeSerialPutHex64(diagnostics.callbackEntryStackPointer);
    suspendEeSerialPutString(" rawRcx=");
    suspendEeSerialPutHex64(diagnostics.callbackRawArgument1Rcx);
    suspendEeSerialPutString(" rawRdx=");
    suspendEeSerialPutHex64(diagnostics.callbackRawArgument2Rdx);
    suspendEeSerialPutString(" rawR8=");
    suspendEeSerialPutHex64(diagnostics.callbackRawArgument3R8);
    suspendEeSerialPutString(" arg1=");
    suspendEeSerialPutHex64(diagnostics.callbackNormalizedArgument1);
    suspendEeSerialPutString(" arg2=");
    suspendEeSerialPutHex64(diagnostics.callbackNormalizedArgument2);
    suspendEeSerialPutString(" arg3=");
    suspendEeSerialPutHex64(diagnostics.callbackNormalizedArgument3);
    suspendEeSerialPutString(" rootSlot=");
    suspendEeSerialPutHex64(diagnostics.callbackRootSlot);
    suspendEeSerialPutString(" rootRaw=");
    suspendEeSerialPutHex64(diagnostics.callbackRootRawValue);
    suspendEeSerialPutString(" callbackRootLoads=");
    suspendEeSerialPutHex32(diagnostics.callbackRootSlotLoadCount);
    suspendEeSerialPutString(" callbackLoadedRaw=");
    suspendEeSerialPutHex64(diagnostics.callbackRootSlotLoadedValue);
    suspendEeSerialPutString(" nullTests=");
    suspendEeSerialPutHex32(diagnostics.callbackNullTestCount);
    suspendEeSerialPutString(" nullNonNull=");
    suspendEeSerialPutHex32(diagnostics.callbackNullTestNonNullCount);
    suspendEeSerialPutString(" contextFieldReads=");
    suspendEeSerialPutHex32(diagnostics.callbackContextFieldReadCount);
    suspendEeSerialPutString(" context=");
    suspendEeSerialPutHex64(diagnostics.callbackContextAddress);
    suspendEeSerialPutString(" contextThread=");
    suspendEeSerialPutHex64(diagnostics.callbackContextThreadUnderCrawl);
    suspendEeSerialPutString(" contextStackLimit=");
    suspendEeSerialPutHex64(diagnostics.callbackContextStackLimit);
    suspendEeSerialPutString(" contextThreadNumber=");
    suspendEeSerialPutHex64(diagnostics.callbackEntryScanContextFieldThreadNumberValue);
    suspendEeSerialPutString(" contextThreadCount=");
    suspendEeSerialPutHex64(diagnostics.callbackEntryScanContextFieldThreadCountValue);
    suspendEeSerialPutString(" contextPromotion=");
    suspendEeSerialPutHex32(diagnostics.callbackEntryPromotion);
    suspendEeSerialPutString(" contextConcurrent=");
    suspendEeSerialPutHex32(diagnostics.callbackEntryConcurrent);
    suspendEeSerialPutString(" currentGeneration=");
    suspendEeSerialPutHex32(diagnostics.callbackEntryCurrentGeneration);
    suspendEeSerialPutString(" condemnedGeneration=");
    suspendEeSerialPutHex32(diagnostics.callbackEntryCondemnedGeneration);
    suspendEeSerialPutString(" expectedFlags=");
    suspendEeSerialPutHex32(diagnostics.callbackExpectedRootFlags);
    suspendEeSerialPutString(" actualFlags=");
    suspendEeSerialPutHex32(diagnostics.callbackActualRootFlags);
    suspendEeSerialPutString(" entryArgsMatch=");
    suspendEeSerialPutHex32(diagnostics.callbackEntryArgumentsMatch);
    suspendEeSerialPutString(" slotMatch=");
    suspendEeSerialPutHex32(diagnostics.callbackRootSlotMatchesExpected);
    suspendEeSerialPutString(" rawMatchesStorage=");
    suspendEeSerialPutHex32(diagnostics.callbackRawRootMatchesStorage);
    suspendEeSerialPutString(" contextMatch=");
    suspendEeSerialPutHex32(diagnostics.callbackScanContextMatchesExpected);
    suspendEeSerialPutString(" flagsMatch=");
    suspendEeSerialPutHex32(diagnostics.callbackFlagsMatchExpected);
    suspendEeSerialPutString(" managedThread=");
    suspendEeSerialPutHex64(diagnostics.callbackEntryManagedThread);
    suspendEeSerialPutString(" currentThread=");
    suspendEeSerialPutHex64(diagnostics.callbackEntryCurrentThread);
    suspendEeSerialPutString(" lockOwner=");
    suspendEeSerialPutHex64(diagnostics.callbackEntryLockOwner);
    suspendEeSerialPutString(" lockHeld=");
    suspendEeSerialPutHex32(diagnostics.callbackEntryThreadStoreLockHeld);
    suspendEeSerialPutString(" eeSuspended=");
    suspendEeSerialPutHex32(diagnostics.callbackEntryEeSuspended);
    suspendEeSerialPutString(" managedEntryProhibited=");
    suspendEeSerialPutHex32(diagnostics.callbackEntryManagedEntryProhibited);
    suspendEeSerialPutString(" firstSemanticOperation=");
    suspendEeSerialPutHex32(diagnostics.callbackFirstSemanticOperation);
    suspendEeSerialPutString(" candidateClassification=");
    suspendEeSerialPutHex32(diagnostics.callbackCandidateClassificationStartCount);
    suspendEeSerialPutString(" heapMembership=");
    suspendEeSerialPutHex32(diagnostics.callbackHeapMembershipTestCount);
    suspendEeSerialPutString(" segmentLookup=");
    suspendEeSerialPutHex32(diagnostics.callbackSegmentLookupCount);
    suspendEeSerialPutString(" objectHeaders=");
    suspendEeSerialPutHex32(diagnostics.callbackObjectHeaderReadCount);
    suspendEeSerialPutString(" methodTables=");
    suspendEeSerialPutHex32(diagnostics.callbackMethodTableReadCount);
    suspendEeSerialPutString(" promotionStart=");
    suspendEeSerialPutHex32(diagnostics.callbackPromotionStartCount);
    suspendEeSerialPutString(" promotions=");
    suspendEeSerialPutHex32(diagnostics.callbackPromotionCount);
    suspendEeSerialPutString(" markingStart=");
    suspendEeSerialPutHex32(diagnostics.callbackMarkingStartCount);
    suspendEeSerialPutString(" graphTraversal=");
    suspendEeSerialPutHex32(diagnostics.callbackGraphTraversalCount);
    suspendEeSerialPutString(" markWrites=");
    suspendEeSerialPutHex32(diagnostics.callbackMarkStateWriteCount);
    suspendEeSerialPutString(" promotionWrites=");
    suspendEeSerialPutHex32(diagnostics.callbackPromotionStateWriteCount);
    suspendEeSerialPutString(" objectMutation=");
    suspendEeSerialPutHex32(diagnostics.callbackObjectMemoryMutationCount);
    suspendEeSerialPutString(" gcMetadataMutation=");
    suspendEeSerialPutHex32(diagnostics.callbackGcMetadataMutationCount);
    suspendEeSerialPutString(" segmentMetadataMutation=");
    suspendEeSerialPutHex32(diagnostics.callbackSegmentMetadataMutationCount);
    suspendEeSerialPutString(" restartRequests=");
    suspendEeSerialPutHex32(diagnostics.restartRequestCount);
    suspendEeSerialPutString(" restartEntries=");
    suspendEeSerialPutHex32(diagnostics.restartEntryCount);
    suspendEeSerialPutString(" managedResume=");
    suspendEeSerialPutHex32(diagnostics.managedResumeCount);
    suspendEeSerialPutString(" fixupFailures=");
    suspendEeSerialPutHex32(diagnostics.allocationContextFixupInvariantFailures);
    suspendEeSerialPutString(" rootFailures=");
    suspendEeSerialPutHex32(diagnostics.rootProviderInvariantFailures);
    suspendEeSerialPutString(" marker=C011EC07\n");
}

extern "C" void __cdecl
guideXosNativeAotFirstRootCallbackEntered(
    uintptr_t rawArg1, uintptr_t rawArg2, uintptr_t rawArg3,
    uintptr_t callbackAddress, uintptr_t returnAddress,
    uintptr_t stackPointer) {
    guidexos_nativeaot_allocation_diagnostics& diagnostics =
        g_guideXosAllocationDiagnostics;
    ++diagnostics.callbackInvocationCount;
    ++diagnostics.callbackEntryCount;
    if (diagnostics.callbackEntryCount != 1u) {
        ++diagnostics.duplicateCallbackInvocationCount;
    }
    diagnostics.callbackEntryAddress = callbackAddress;
    diagnostics.callbackEntryReturnAddress = returnAddress;
    diagnostics.callbackEntryStackPointer = stackPointer;
    diagnostics.callbackRawArgument1Rcx = rawArg1;
    diagnostics.callbackRawArgument2Rdx = rawArg2;
    diagnostics.callbackRawArgument3R8 = rawArg3;
    diagnostics.callbackNormalizedArgument1 = rawArg1;
    diagnostics.callbackNormalizedArgument2 = rawArg2;
    diagnostics.callbackNormalizedArgument3 = rawArg3;
    diagnostics.callbackRootSlot = rawArg1;
    diagnostics.callbackRootRawValue = diagnostics.callbackSiteRawRootValue;
    diagnostics.callbackContextAddress = rawArg2;
    diagnostics.callbackExpectedRootFlags = diagnostics.candidateRawRootFlags;
    diagnostics.callbackActualRootFlags = static_cast<uint32_t>(rawArg3);
    diagnostics.callbackEntryCurrentGeneration = diagnostics.requestedGeneration;
    diagnostics.callbackEntryCondemnedGeneration = diagnostics.rootCondemnedGeneration;
    diagnostics.callbackEntryManagedThread =
        reinterpret_cast<gx_uintptr>(suspendEeCurrentThread());
    diagnostics.callbackEntryCurrentThread = diagnostics.rootCurrentThreadIdentity;
    diagnostics.callbackEntryLockOwner = diagnostics.threadStoreLockOwner;
    diagnostics.callbackEntryThreadStoreLockHeld =
        diagnostics.threadStoreLockRecursionDepth == 1u ? 1u : 0u;
    diagnostics.callbackEntryEeSuspended = diagnostics.eeSuspended;
    diagnostics.callbackEntryManagedEntryProhibited = diagnostics.managedEntryProhibited;
    diagnostics.callbackRootSlotMatchesExpected =
        rawArg1 == diagnostics.candidateFirstNonNullSlot && rawArg1 != 0u ? 1u : 0u;
    diagnostics.callbackRawRootMatchesStorage =
        diagnostics.callbackRootRawValue != 0u &&
        diagnostics.callbackRootRawValue ==
            diagnostics.runtimeThreadStaticStorageObjectAddress ? 1u : 0u;
    diagnostics.callbackScanContextMatchesExpected =
        rawArg2 == diagnostics.candidateScanContextIdentity && rawArg2 != 0u ? 1u : 0u;
    diagnostics.callbackFlagsMatchExpected =
        static_cast<uint32_t>(rawArg3) == diagnostics.callbackExpectedRootFlags ? 1u : 0u;
    diagnostics.callbackEntryArgumentsMatch =
        diagnostics.callbackRootSlotMatchesExpected != 0u &&
        diagnostics.callbackRawRootMatchesStorage != 0u &&
        diagnostics.callbackScanContextMatchesExpected != 0u &&
        diagnostics.callbackFlagsMatchExpected != 0u ? 1u : 0u;
    if (rawArg2 != 0u) {
        const GuideXosScanContextPrefix* context =
            reinterpret_cast<const GuideXosScanContextPrefix*>(rawArg2);
        diagnostics.callbackContextThreadUnderCrawl =
            reinterpret_cast<gx_uintptr>(context->threadUnderCrawl);
        diagnostics.callbackContextStackLimit = context->stackLimit;
        diagnostics.callbackEntryScanContextFieldThreadNumberAddress =
            reinterpret_cast<gx_uintptr>(&context->threadNumber);
        diagnostics.callbackEntryScanContextFieldThreadNumberValue =
            static_cast<gx_uintptr>(context->threadNumber);
        diagnostics.callbackEntryScanContextFieldThreadCountAddress =
            reinterpret_cast<gx_uintptr>(&context->threadCount);
        diagnostics.callbackEntryScanContextFieldThreadCountValue =
            static_cast<gx_uintptr>(context->threadCount);
        diagnostics.callbackEntryScanContextFieldPromotionAddress =
            reinterpret_cast<gx_uintptr>(&context->promotion);
        diagnostics.callbackEntryScanContextFieldConcurrentAddress =
            reinterpret_cast<gx_uintptr>(&context->concurrent);
        diagnostics.callbackEntryPromotion = context->promotion ? 1u : 0u;
        diagnostics.callbackEntryConcurrent = context->concurrent ? 1u : 0u;
        diagnostics.callbackContextFieldReadCount = 6u;
    }
    if (diagnostics.callbackEntryCount != 1u ||
        diagnostics.callbackEntryArgumentsMatch == 0u ||
        diagnostics.callbackSiteCallbackIdentity != callbackAddress) {
        ++diagnostics.rootProviderInvariantFailures;
        diagnostics.callbackSafeStopReason = 0xE007u;
        emitFirstRootCallbackEntrySafeStop();
        guideXosFailFast(9u);
    }
}

extern "C" [[noreturn]] void
guideXosNativeAotFirstRootCallbackCandidateLoaded(uintptr_t rawValue) {
    guidexos_nativeaot_allocation_diagnostics& diagnostics =
        g_guideXosAllocationDiagnostics;
    ++diagnostics.callbackRootSlotLoadCount;
    diagnostics.callbackRootSlotLoadedValue = rawValue;
    diagnostics.callbackRootRawValue = rawValue;
    diagnostics.callbackFirstSemanticOperation = 1u;
    diagnostics.callbackRootRawValue = rawValue;
    validateAllocationContextFixupObjects(true);
    diagnostics.candidateObjectValidationAtStopCount =
        diagnostics.objectHistoryCount;
    const bool valid =
        diagnostics.callbackSafeStopObserved == 0u &&
        diagnostics.callbackRequestCount == 1u &&
        diagnostics.callbackCallSiteEntryCount == 1u &&
        diagnostics.callbackInvocationCount == 1u &&
        diagnostics.callbackEntryCount == 1u &&
        diagnostics.callbackReturnCount == 0u &&
        diagnostics.duplicateCallbackInvocationCount == 0u &&
        diagnostics.callbackRootSlotLoadCount == 1u &&
        rawValue != 0u &&
        rawValue == diagnostics.runtimeThreadStaticStorageObjectAddress &&
        diagnostics.callbackEntryArgumentsMatch == 1u &&
        diagnostics.callbackContextFieldReadCount == 6u &&
        diagnostics.callbackCandidateClassificationStartCount == 0u &&
        diagnostics.callbackHeapMembershipTestCount == 0u &&
        diagnostics.callbackSegmentLookupCount == 0u &&
        diagnostics.callbackObjectHeaderReadCount == 0u &&
        diagnostics.callbackMethodTableReadCount == 0u &&
        diagnostics.callbackPromotionStartCount == 0u &&
        diagnostics.callbackPromotionCount == 0u &&
        diagnostics.callbackMarkingStartCount == 0u &&
        diagnostics.callbackGraphTraversalCount == 0u &&
        diagnostics.callbackMarkStateWriteCount == 0u &&
        diagnostics.callbackPromotionStateWriteCount == 0u &&
        diagnostics.callbackObjectMemoryMutationCount == 0u &&
        diagnostics.callbackGcMetadataMutationCount == 0u &&
        diagnostics.callbackSegmentMetadataMutationCount == 0u &&
        diagnostics.restartRequestCount == 0u &&
        diagnostics.restartEntryCount == 0u &&
        diagnostics.managedResumeCount == 0u &&
        diagnostics.threadStoreLockRecursionDepth == 1u &&
        diagnostics.managedEntryProhibited == 1u &&
        diagnostics.eeSuspended == 1u;
    if (!valid) {
        ++diagnostics.rootProviderInvariantFailures;
        diagnostics.callbackSafeStopReason = 0xE008u;
        emitFirstRootCallbackEntrySafeStop();
        guideXosFailFast(9u);
    }
    diagnostics.callbackSafeStopReason =
        GUIDEXOS_NATIVEAOT_FIRST_ROOT_CALLBACK_ENTRY_SAFE_STOP_MARKER;
    diagnostics.callbackSafeStopObserved = 1u;
    diagnostics.safeStopObserved = 1u;
    diagnostics.stopReason = diagnostics.callbackSafeStopReason;
    diagnostics.stage =
        GUIDEXOS_NATIVEAOT_ALLOC_STAGE_F25_FIRST_ROOT_CALLBACK_ENTRY_SAFE_STOP;
    diagnostics.rootBoundaryFunction = reinterpret_cast<gx_uintptr>(
        &guideXosNativeAotFirstRootCallbackCandidateLoaded);
    emitFirstRootCallbackEntrySafeStop();
    for (;;) {
    }
}

#endif

#if defined(GUIDEXOS_NATIVEAOT_FIRST_NON_NULL_ROOT_ALLOCATION)
void emitFirstNonNullRootCallbackBoundarySafeStop() {
    const guidexos_nativeaot_allocation_diagnostics& diagnostics =
        g_guideXosAllocationDiagnostics;
    suspendEeSerialPutString(
        "[nativeaot-gc-first-non-null-root-callback-boundary] SAFE_STOP marker=");
    suspendEeSerialPutHex32(diagnostics.candidateStopReason);
    suspendEeSerialPutString(" gcScanRootsRequest=");
    suspendEeSerialPutHex32(diagnostics.gcScanRootsRequestCount);
    suspendEeSerialPutString(" gcScanRootsEntry=");
    suspendEeSerialPutHex32(diagnostics.gcScanRootsEntryCount);
    suspendEeSerialPutString(" foreachRequest=");
    suspendEeSerialPutHex32(diagnostics.foreachThreadRequestCount);
    suspendEeSerialPutString(" foreachEntry=");
    suspendEeSerialPutHex32(diagnostics.foreachThreadEntryCount);
    suspendEeSerialPutString(" iteratorInit=");
    suspendEeSerialPutHex32(diagnostics.threadIteratorInitializationCount);
    suspendEeSerialPutString(" registeredBefore=");
    suspendEeSerialPutHex32(diagnostics.registeredThreadCountBeforeRoot);
    suspendEeSerialPutString(" registeredAfter=");
    suspendEeSerialPutHex32(diagnostics.registeredThreadCountAfterRoot);
    suspendEeSerialPutString(" enumerated=");
    suspendEeSerialPutHex32(diagnostics.enumeratedThreadCount);
    suspendEeSerialPutString(" included=");
    suspendEeSerialPutHex32(diagnostics.includedThreadCount);
    suspendEeSerialPutString(" excluded=");
    suspendEeSerialPutHex32(diagnostics.excludedThreadCount);
    suspendEeSerialPutString(" current=");
    suspendEeSerialPutHex64(diagnostics.rootCurrentThreadIdentity);
    suspendEeSerialPutString(" enumeratedThread=");
    suspendEeSerialPutHex64(diagnostics.rootEnumeratedThreadIdentity);
    suspendEeSerialPutString(" initiator=");
    suspendEeSerialPutHex64(diagnostics.rootCollectionInitiatorIdentity);
    suspendEeSerialPutString(" lockOwner=");
    suspendEeSerialPutHex64(diagnostics.rootLockOwnerIdentity);
    suspendEeSerialPutString(" nativeId=");
    suspendEeSerialPutHex64(diagnostics.rootThreadRecords[0].nativeThreadId);
    suspendEeSerialPutString(" lifecycle=");
    suspendEeSerialPutHex32(diagnostics.rootThreadRecords[0].lifecycleState);
    suspendEeSerialPutString(" stateFlags=");
    suspendEeSerialPutHex32(diagnostics.rootThreadRecords[0].threadStateFlags);
    suspendEeSerialPutString(" cooperative=");
    suspendEeSerialPutHex32(diagnostics.rootThreadRecords[0].cooperative);
    suspendEeSerialPutString(" preemptive=");
    suspendEeSerialPutHex32(diagnostics.rootThreadRecords[0].preemptive);
    suspendEeSerialPutString(" managedAssignmentCount=");
    suspendEeSerialPutHex32(diagnostics.threadStaticProofAssignmentCount);
    suspendEeSerialPutString(" managedClearCount=");
    suspendEeSerialPutHex32(diagnostics.threadStaticProofClearCount);
    suspendEeSerialPutString(" managedReadbackCount=");
    suspendEeSerialPutHex32(diagnostics.threadStaticProofReadbackCount);
    suspendEeSerialPutString(" managedAssignmentValid=");
    suspendEeSerialPutHex32(diagnostics.threadStaticProofManagedAssignmentValid);
    suspendEeSerialPutString(" managedReadbackValid=");
    suspendEeSerialPutHex32(diagnostics.threadStaticProofManagedReadbackValid);
    suspendEeSerialPutString(" threadStaticInitialization=");
    suspendEeSerialPutHex32(diagnostics.threadStaticProofInitializationIndicator);
    suspendEeSerialPutString(" sentinelOrdinal=");
    suspendEeSerialPutHex32(diagnostics.threadStaticProofSentinelOrdinal);
    suspendEeSerialPutString(" sentinelAddress=");
    suspendEeSerialPutHex64(diagnostics.threadStaticProofSentinelAddress);
    suspendEeSerialPutString(" sentinelSize=");
    suspendEeSerialPutHex64(diagnostics.threadStaticProofSentinelSize);
    suspendEeSerialPutString(" readbackAddress=");
    suspendEeSerialPutHex64(diagnostics.threadStaticProofManagedReadbackAddress);
    suspendEeSerialPutString(" readbackExactMatch=");
    suspendEeSerialPutHex32(diagnostics.threadStaticProofReadbackExactMatch);
    suspendEeSerialPutString(" managedThread=");
    suspendEeSerialPutHex64(diagnostics.threadStaticProofManagedThread);
    suspendEeSerialPutString(" storageAddress=");
    suspendEeSerialPutHex64(diagnostics.threadStaticProofStorageAddress);
    suspendEeSerialPutString(" listIntegrityFailures=");
    suspendEeSerialPutHex32(diagnostics.threadListIntegrityFailures);
    suspendEeSerialPutString(" duplicates=");
    suspendEeSerialPutHex32(diagnostics.duplicateThreadCount);
    suspendEeSerialPutString(" registryMutationBefore=");
    suspendEeSerialPutHex32(diagnostics.rootThreadRegistryGenerationBefore);
    suspendEeSerialPutString(" registryMutationAfter=");
    suspendEeSerialPutHex32(diagnostics.rootThreadRegistryGenerationAfter);
    suspendEeSerialPutString(" providerSource=thread-static-provider providerRuntime=thread-static-provider providerFunction=");
    suspendEeSerialPutHex32(diagnostics.rootProviderFunctionCode);
    suspendEeSerialPutString(" providerThread=");
    suspendEeSerialPutHex64(diagnostics.firstRootProviderThread);
    suspendEeSerialPutString(" metadataContainer=");
    suspendEeSerialPutHex64(diagnostics.candidateMetadataContainerIdentity);
    suspendEeSerialPutString(" candidateSlot=");
    suspendEeSerialPutHex64(diagnostics.candidateSlotAddress);
    suspendEeSerialPutString(" slotWidth=");
    suspendEeSerialPutHex32(diagnostics.candidateSlotWidth);
    suspendEeSerialPutString(" slotAlignment=");
    suspendEeSerialPutHex32(diagnostics.candidateSlotAlignment);
    suspendEeSerialPutString(" slotStable=");
    suspendEeSerialPutHex32(diagnostics.candidateSlotStable);
    suspendEeSerialPutString(" slotExpectedThreadStorage=");
    suspendEeSerialPutHex32(diagnostics.candidateSlotExpectedThreadStorage);
    suspendEeSerialPutString(" candidateVisited=");
    suspendEeSerialPutHex32(diagnostics.candidateSlotVisitCount);
    suspendEeSerialPutString(" nullCandidates=");
    suspendEeSerialPutHex32(diagnostics.candidateNullCount);
    suspendEeSerialPutString(" nonNullCandidates=");
    suspendEeSerialPutHex32(diagnostics.candidateNonNullCount);
    suspendEeSerialPutString(" proofRootObserved=");
    suspendEeSerialPutHex32(diagnostics.candidateProofRootObserved);
    suspendEeSerialPutString(" candidateMatchesProofRoot=");
    suspendEeSerialPutHex32(diagnostics.candidateMatchesProofRoot);
    suspendEeSerialPutString(" candidateMatchesStorageObject=");
    suspendEeSerialPutHex32(diagnostics.candidateMatchesStorageObject);
    suspendEeSerialPutString(" unexpectedNonNull=");
    suspendEeSerialPutHex32(diagnostics.candidateUnexpectedNonNull);
    suspendEeSerialPutString(" boundReached=");
    suspendEeSerialPutHex32(diagnostics.candidateBoundReached);
    suspendEeSerialPutString(" firstNonNullSlot=");
    suspendEeSerialPutHex64(diagnostics.candidateFirstNonNullSlot);
    suspendEeSerialPutString(" firstNonNullValue=");
    suspendEeSerialPutHex64(diagnostics.candidateFirstNonNullValue);
    suspendEeSerialPutString(" firstNonNullKnownAddressMatch=");
    suspendEeSerialPutHex32(diagnostics.candidateFirstNonNullKnownAddressMatch);
    suspendEeSerialPutString(" expectedSentinelAddress=");
    suspendEeSerialPutHex64(diagnostics.candidateExpectedSentinelAddress);
    suspendEeSerialPutString(" expectedStorageObjectAddress=");
    suspendEeSerialPutHex64(diagnostics.candidateExpectedStorageObjectAddress);
    suspendEeSerialPutString(" loadRequests=");
    suspendEeSerialPutHex32(diagnostics.candidateLoadRequestCount);
    suspendEeSerialPutString(" loadEntries=");
    suspendEeSerialPutHex32(diagnostics.candidateLoadEntryCount);
    suspendEeSerialPutString(" machineWordLoads=");
    suspendEeSerialPutHex32(diagnostics.candidateMachineWordLoadCount);
    suspendEeSerialPutString(" duplicateLoads=");
    suspendEeSerialPutHex32(diagnostics.candidateDuplicateLoadCount);
    suspendEeSerialPutString(" loadFaults=");
    suspendEeSerialPutHex32(diagnostics.candidateLoadFaultCount);
    suspendEeSerialPutString(" callback=");
    suspendEeSerialPutHex64(diagnostics.candidateCallbackIdentity);
    suspendEeSerialPutString(" scanContext=");
    suspendEeSerialPutHex64(diagnostics.candidateScanContextIdentity);
    suspendEeSerialPutString(" rootFlags=");
    suspendEeSerialPutHex32(diagnostics.candidateRawRootFlags);
    suspendEeSerialPutString(" rootKind=");
    suspendEeSerialPutHex32(diagnostics.candidateRootKind);
    suspendEeSerialPutString(" callbacks=00000000 promotions=00000000 marking=00000000");
    suspendEeSerialPutString(" candidateDereferences=00000000 heapMembershipTests=00000000");
    suspendEeSerialPutString(" objectHeaders=00000000 methodTables=00000000 rootFlagApplications=00000000");
    suspendEeSerialPutString(" objectMutation=");
    suspendEeSerialPutHex32(diagnostics.objectMemoryMutationStarted);
    suspendEeSerialPutString(" restartRequests=");
    suspendEeSerialPutHex32(diagnostics.restartRequestCount);
    suspendEeSerialPutString(" restartEntries=");
    suspendEeSerialPutHex32(diagnostics.restartEntryCount);
    suspendEeSerialPutString(" managedResume=");
    suspendEeSerialPutHex32(diagnostics.managedResumeCount);
    suspendEeSerialPutString(" userAllocations=");
    suspendEeSerialPutHex32(diagnostics.allocationCount);
    suspendEeSerialPutString(" userAllocationRequests=");
    suspendEeSerialPutHex32(diagnostics.allocationRequestCount);
    suspendEeSerialPutString(" userFast=");
    suspendEeSerialPutHex32(diagnostics.fastAllocationCount);
    suspendEeSerialPutString(" userRare=");
    suspendEeSerialPutHex32(diagnostics.rarePathCount);
    suspendEeSerialPutString(" userRefills=");
    suspendEeSerialPutHex32(diagnostics.allocationContextRefillCount);
    suspendEeSerialPutString(" userSameSegmentCommits=");
    suspendEeSerialPutHex32(diagnostics.heapCommitEventCount);
    suspendEeSerialPutString(" userSegmentTransitions=");
    suspendEeSerialPutHex32(diagnostics.segmentTransitionCount);
    suspendEeSerialPutString(" collectionAllocationOrdinal=");
    suspendEeSerialPutHex32(diagnostics.collectionRequestAllocationOrdinal);
    suspendEeSerialPutString(" runtimeThreadStaticStorageAllocations=");
    suspendEeSerialPutHex32(diagnostics.runtimeThreadStaticStorageAllocationCount);
    suspendEeSerialPutString(" runtimeThreadStaticStoragePublications=");
    suspendEeSerialPutHex32(diagnostics.runtimeThreadStaticStoragePublicationCount);
    suspendEeSerialPutString(" runtimeThreadStaticStorageObject=");
    suspendEeSerialPutHex64(diagnostics.runtimeThreadStaticStorageObjectAddress);
    suspendEeSerialPutString(" runtimeThreadStaticInlinedRoot=");
    suspendEeSerialPutHex64(diagnostics.runtimeThreadStaticInlinedRootAddress);
    suspendEeSerialPutString(" totalAllocationRequestsObserved=");
    suspendEeSerialPutHex32(
        diagnostics.allocationRequestCount +
        diagnostics.runtimeThreadStaticStorageAllocationCount);
    suspendEeSerialPutString(" condemnedGeneration=");
    suspendEeSerialPutHex32(diagnostics.rootCondemnedGeneration);
    suspendEeSerialPutString(" maxGeneration=");
    suspendEeSerialPutHex32(diagnostics.rootMaximumGeneration);
    suspendEeSerialPutString(" scanContextPromotion=");
    suspendEeSerialPutHex32(diagnostics.rootScanContextPromotion);
    suspendEeSerialPutString(" scanContextConcurrent=");
    suspendEeSerialPutHex32(diagnostics.rootScanContextConcurrent);
    suspendEeSerialPutString(" scanContextIdentity=");
    suspendEeSerialPutHex64(diagnostics.rootScanContextIdentity);
    suspendEeSerialPutString(" objectBeforeLoad=");
    suspendEeSerialPutHex32(diagnostics.candidateObjectValidationBeforeLoadCount);
    suspendEeSerialPutString(" objectAfterLoad=");
    suspendEeSerialPutHex32(diagnostics.candidateObjectValidationAfterLoadCount);
    suspendEeSerialPutString(" objectAtStop=");
    suspendEeSerialPutHex32(diagnostics.candidateObjectValidationAtStopCount);
    suspendEeSerialPutString(" sentinelChecks=");
    suspendEeSerialPutHex32(diagnostics.sentinelValidationCount);
    suspendEeSerialPutString(" objectHistoryOverflow=");
    suspendEeSerialPutHex32(diagnostics.objectHistoryOverflow);
    suspendEeSerialPutString(" providerRequests=");
    suspendEeSerialPutHex32(diagnostics.rootProviderRequestCount);
    suspendEeSerialPutString(" providerEntries=");
    suspendEeSerialPutHex32(diagnostics.rootProviderEntryCount);
    suspendEeSerialPutString(" providerSkips=");
    suspendEeSerialPutHex32(diagnostics.rootProviderSkipCount);
    suspendEeSerialPutString(" fixupFailures=");
    suspendEeSerialPutHex32(diagnostics.allocationContextFixupInvariantFailures);
    suspendEeSerialPutString(" rootFailures=");
    suspendEeSerialPutHex32(diagnostics.rootProviderInvariantFailures);
    suspendEeSerialPutString(" eeSuspended=");
    suspendEeSerialPutHex32(diagnostics.eeSuspended);
    suspendEeSerialPutString(" lockDepth=");
    suspendEeSerialPutHex32(diagnostics.threadStoreLockRecursionDepth);
    suspendEeSerialPutString(" marker=C011EC06\n");
    for (uint32_t index = 0u;
         index < diagnostics.candidateSlotVisitCount &&
         index < GUIDEXOS_NATIVEAOT_MAX_CANDIDATE_SLOTS;
         ++index) {
        const guidexos_nativeaot_candidate_slot_record& record =
            diagnostics.candidateSlotRecords[index];
        suspendEeSerialPutString(
            "[nativeaot-gc-first-non-null-root-callback-boundary] CANDIDATE ordinal=");
        suspendEeSerialPutHex32(record.ordinal);
        suspendEeSerialPutString(" slot=");
        suspendEeSerialPutHex64(record.slotAddress);
        suspendEeSerialPutString(" rawValue=");
        suspendEeSerialPutHex64(record.rawValue);
        suspendEeSerialPutString(" loadCount=");
        suspendEeSerialPutHex32(record.loadCount);
        suspendEeSerialPutString(" duplicateLoads=");
        suspendEeSerialPutHex32(record.duplicateLoadCount);
        suspendEeSerialPutString(" null=");
        suspendEeSerialPutHex32(record.valueIsNull);
        suspendEeSerialPutString(" knownAddressMatch=");
        suspendEeSerialPutHex32(record.knownAddressMatch);
        suspendEeSerialPutString(" exactSelectedSentinelMatch=");
        suspendEeSerialPutHex32(record.exactSelectedSentinelMatch);
        suspendEeSerialPutString(" callback=");
        suspendEeSerialPutHex64(record.callbackIdentity);
        suspendEeSerialPutString(" scanContext=");
        suspendEeSerialPutHex64(record.scanContextIdentity);
        suspendEeSerialPutString("\n");
    }
}

[[noreturn]] void firstNonNullRootCallbackBoundarySafeStop() {
    guidexos_nativeaot_allocation_diagnostics& diagnostics =
        g_guideXosAllocationDiagnostics;
    ++diagnostics.candidateSafeStopObserved;
    validateAllocationContextFixupObjects(true);
    diagnostics.candidateObjectValidationAtStopCount =
        diagnostics.objectHistoryCount;

    /* The real storage object consumes part of the same allocation context as
       the 4 KiB workload.  The collection request can therefore move before
       the historical 40th completed array.  Require complete coverage of the
       bounded records that did complete, with one four-sentinel validation
       pass per record, rather than fabricating the historical ordinal. */
    const bool objectValidationCoverage =
        diagnostics.candidateObjectValidationBeforeLoadCount ==
            diagnostics.objectHistoryCount &&
        diagnostics.candidateObjectValidationAfterLoadCount ==
            diagnostics.objectHistoryCount &&
        diagnostics.candidateObjectValidationAtStopCount ==
            diagnostics.objectHistoryCount &&
        diagnostics.objectHistoryCount >= 32u &&
        diagnostics.sentinelValidationCount ==
            diagnostics.objectHistoryCount * 4u &&
        diagnostics.objectHistoryOverflow == 0u;

    const bool valid =
        diagnostics.candidateSafeStopObserved == 1u &&
        diagnostics.threadStaticProofAssignmentCount == 1u &&
        diagnostics.threadStaticProofClearCount == 0u &&
        diagnostics.threadStaticProofReadbackCount == 1u &&
        diagnostics.threadStaticProofManagedAssignmentValid == 1u &&
        diagnostics.threadStaticProofManagedReadbackValid == 1u &&
        diagnostics.runtimeThreadStaticStorageAllocationCount == 1u &&
        diagnostics.runtimeThreadStaticStoragePublicationCount == 1u &&
        diagnostics.runtimeThreadStaticStorageObjectValid == 1u &&
        diagnostics.runtimeThreadStaticStorageObjectAddress != 0u &&
        diagnostics.candidateSlotVisitCount >= 1u &&
        diagnostics.candidateSlotVisitCount <= GUIDEXOS_NATIVEAOT_MAX_CANDIDATE_SLOTS &&
        diagnostics.candidateLoadRequestCount == diagnostics.candidateSlotVisitCount &&
        diagnostics.candidateLoadEntryCount == diagnostics.candidateSlotVisitCount &&
        diagnostics.candidateMachineWordLoadCount == diagnostics.candidateSlotVisitCount &&
        diagnostics.candidateDuplicateLoadCount == 0u &&
        diagnostics.candidateLoadFaultCount == 0u &&
        diagnostics.candidateNonNullCount == 1u &&
        diagnostics.candidateProviderTerminated == 1u &&
        diagnostics.candidateRootCallbacksDelivered == 0u &&
        diagnostics.candidatePromotionCallbacksDelivered == 0u &&
        diagnostics.markingEntryCount == 0u &&
        diagnostics.objectMemoryMutationStarted == 0u &&
        diagnostics.restartRequestCount == 0u &&
        diagnostics.restartEntryCount == 0u &&
        diagnostics.managedResumeCount == 0u &&
        objectValidationCoverage &&
        diagnostics.rootProviderInvariantFailures == 0u &&
        diagnostics.allocationContextFixupInvariantFailures == 0u &&
        diagnostics.threadStoreLockRecursionDepth == 1u &&
        diagnostics.managedEntryProhibited == 1u &&
        diagnostics.eeSuspended == 1u;
    if (!valid) {
        ++diagnostics.rootProviderInvariantFailures;
        diagnostics.candidateStopReason = 0xE006u;
        emitFirstNonNullRootCallbackBoundarySafeStop();
        guideXosFailFast(9u);
    }
    diagnostics.candidateStopReason =
        GUIDEXOS_NATIVEAOT_FIRST_NON_NULL_ROOT_CALLBACK_BOUNDARY_SAFE_STOP_MARKER;
    diagnostics.safeStopObserved = 1u;
    diagnostics.stopReason = diagnostics.candidateStopReason;
    diagnostics.stage =
        GUIDEXOS_NATIVEAOT_ALLOC_STAGE_F24_FIRST_NON_NULL_ROOT_CALLBACK_BOUNDARY_SAFE_STOP;
    diagnostics.rootBoundaryFunction = reinterpret_cast<gx_uintptr>(
        &firstNonNullRootCallbackBoundarySafeStop);
    emitFirstNonNullRootCallbackBoundarySafeStop();
    for (;;) {
    }
}
#endif

void emitFirstPerThreadRootProviderSafeStop() {
    const guidexos_nativeaot_allocation_diagnostics& diagnostics =
        g_guideXosAllocationDiagnostics;
    suspendEeSerialPutString(
        "[nativeaot-gc-first-per-thread-root-provider] SAFE_STOP marker=");
    suspendEeSerialPutHex32(diagnostics.rootProviderStopReason);
    suspendEeSerialPutString(" gcScanRootsRequest=");
    suspendEeSerialPutHex32(diagnostics.gcScanRootsRequestCount);
    suspendEeSerialPutString(" gcScanRootsEntry=");
    suspendEeSerialPutHex32(diagnostics.gcScanRootsEntryCount);
    suspendEeSerialPutString(" foreachRequest=");
    suspendEeSerialPutHex32(diagnostics.foreachThreadRequestCount);
    suspendEeSerialPutString(" foreachEntry=");
    suspendEeSerialPutHex32(diagnostics.foreachThreadEntryCount);
    suspendEeSerialPutString(" iteratorInit=");
    suspendEeSerialPutHex32(diagnostics.threadIteratorInitializationCount);
    suspendEeSerialPutString(" registeredBefore=");
    suspendEeSerialPutHex32(diagnostics.registeredThreadCountBeforeRoot);
    suspendEeSerialPutString(" registeredAfter=");
    suspendEeSerialPutHex32(diagnostics.registeredThreadCountAfterRoot);
    suspendEeSerialPutString(" enumerated=");
    suspendEeSerialPutHex32(diagnostics.enumeratedThreadCount);
    suspendEeSerialPutString(" included=");
    suspendEeSerialPutHex32(diagnostics.includedThreadCount);
    suspendEeSerialPutString(" excluded=");
    suspendEeSerialPutHex32(diagnostics.excludedThreadCount);
    suspendEeSerialPutString(" current=");
    suspendEeSerialPutHex64(diagnostics.rootCurrentThreadIdentity);
    suspendEeSerialPutString(" nativeId=");
    suspendEeSerialPutHex64(diagnostics.rootThreadRecords[0].nativeThreadId);
    suspendEeSerialPutString(" enumeratedThread=");
    suspendEeSerialPutHex64(diagnostics.rootEnumeratedThreadIdentity);
    suspendEeSerialPutString(" initiator=");
    suspendEeSerialPutHex64(diagnostics.rootCollectionInitiatorIdentity);
    suspendEeSerialPutString(" lockOwner=");
    suspendEeSerialPutHex64(diagnostics.rootLockOwnerIdentity);
    suspendEeSerialPutString(" lifecycle=");
    suspendEeSerialPutHex32(diagnostics.rootThreadRecords[0].lifecycleState);
    suspendEeSerialPutString(" stateFlags=");
    suspendEeSerialPutHex32(diagnostics.rootThreadRecords[0].threadStateFlags);
    suspendEeSerialPutString(" cooperative=");
    suspendEeSerialPutHex32(diagnostics.rootThreadRecords[0].cooperative);
    suspendEeSerialPutString(" preemptive=");
    suspendEeSerialPutHex32(diagnostics.rootThreadRecords[0].preemptive);
    suspendEeSerialPutString(" allocContext=");
    suspendEeSerialPutHex64(diagnostics.rootThreadRecords[0].allocationContext);
    suspendEeSerialPutString(" stackLow=");
    suspendEeSerialPutHex64(diagnostics.rootThreadRecords[0].stackLow);
    suspendEeSerialPutString(" stackHigh=");
    suspendEeSerialPutHex64(diagnostics.rootThreadRecords[0].stackHigh);
    suspendEeSerialPutString(" listHeadBefore=");
    suspendEeSerialPutHex64(diagnostics.rootThreadListHeadBefore);
    suspendEeSerialPutString(" listTailBefore=");
    suspendEeSerialPutHex64(diagnostics.rootThreadListTailBefore);
    suspendEeSerialPutString(" listHeadAfter=");
    suspendEeSerialPutHex64(diagnostics.rootThreadListHeadAfter);
    suspendEeSerialPutString(" listTailAfter=");
    suspendEeSerialPutHex64(diagnostics.rootThreadListTailAfter);
    suspendEeSerialPutString(" listIntegrityFailures=");
    suspendEeSerialPutHex32(diagnostics.threadListIntegrityFailures);
    suspendEeSerialPutString(" duplicates=");
    suspendEeSerialPutHex32(diagnostics.duplicateThreadCount);
    suspendEeSerialPutString(" registryMutationBefore=");
    suspendEeSerialPutHex32(diagnostics.threadRegistryMutationCountBeforeRoot);
    suspendEeSerialPutString(" registryMutationAfter=");
    suspendEeSerialPutHex32(diagnostics.threadRegistryMutationCountAfterRoot);
    suspendEeSerialPutString(" providerSource=thread-static-provider providerRuntime=thread-static-provider");
    suspendEeSerialPutString(" providerFunction=");
    suspendEeSerialPutHex32(diagnostics.rootProviderFunctionCode);
    suspendEeSerialPutString(" providerThread=");
    suspendEeSerialPutHex64(diagnostics.firstRootProviderThread);
    suspendEeSerialPutString(" providerRequests=");
    suspendEeSerialPutHex32(diagnostics.rootProviderRequestCount);
    suspendEeSerialPutString(" providerEntries=");
    suspendEeSerialPutHex32(diagnostics.rootProviderEntryCount);
    suspendEeSerialPutString(" providerSkips=");
    suspendEeSerialPutHex32(diagnostics.rootProviderSkipCount);
    suspendEeSerialPutString(" metadataContainers=");
    suspendEeSerialPutHex32(diagnostics.metadataContainerCount);
    suspendEeSerialPutString(" firstMetadata=");
    suspendEeSerialPutHex64(diagnostics.firstRootProviderMetadataContainer);
    suspendEeSerialPutString(" candidateMetadata=");
    suspendEeSerialPutHex32(diagnostics.candidateMetadataLocationCount);
    suspendEeSerialPutString(" candidateReads=");
    suspendEeSerialPutHex32(diagnostics.candidateValueReadCount);
    suspendEeSerialPutString(" candidates=");
    suspendEeSerialPutHex32(diagnostics.rootCandidateDiscoveryCount);
    suspendEeSerialPutString(" callbacks=");
    suspendEeSerialPutHex32(diagnostics.rootCallbacksDelivered);
    suspendEeSerialPutString(" promotions=");
    suspendEeSerialPutHex32(diagnostics.promotionCallbacksDelivered);
    suspendEeSerialPutString(" marking=");
    suspendEeSerialPutHex32(diagnostics.markingEntryCount);
    suspendEeSerialPutString(" objectMutation=");
    suspendEeSerialPutHex32(diagnostics.objectMemoryMutationStarted);
    suspendEeSerialPutString(" restartRequests=");
    suspendEeSerialPutHex32(diagnostics.restartRequestCount);
    suspendEeSerialPutString(" restartEntries=");
    suspendEeSerialPutHex32(diagnostics.restartEntryCount);
    suspendEeSerialPutString(" managedResume=");
    suspendEeSerialPutHex32(diagnostics.managedResumeCount);
    suspendEeSerialPutString(" stackBoundsRequested=");
    suspendEeSerialPutHex32(diagnostics.stackBoundsRequested);
    suspendEeSerialPutString(" stackScanning=");
    suspendEeSerialPutHex32(diagnostics.stackScanningStarted);
    suspendEeSerialPutString(" threadStaticRequested=");
    suspendEeSerialPutHex32(diagnostics.threadStaticStorageRequested);
    suspendEeSerialPutString(" threadStaticScanning=");
    suspendEeSerialPutHex32(diagnostics.threadStaticScanningStarted);
    suspendEeSerialPutString(" sentinelChecks=");
    suspendEeSerialPutHex32(diagnostics.sentinelChecksAtRootBoundary);
    suspendEeSerialPutString(" objectBefore=");
    suspendEeSerialPutHex32(diagnostics.objectValidationBeforeFixupCount);
    suspendEeSerialPutString(" objectAfter=");
    suspendEeSerialPutHex32(diagnostics.objectValidationAfterFixupCount);
    suspendEeSerialPutString(" fixupFailures=");
    suspendEeSerialPutHex32(diagnostics.allocationContextFixupInvariantFailures);
    suspendEeSerialPutString(" rootFailures=");
    suspendEeSerialPutHex32(diagnostics.rootProviderInvariantFailures);
    suspendEeSerialPutString(" eeSuspended=");
    suspendEeSerialPutHex32(diagnostics.eeSuspended);
    suspendEeSerialPutString(" lockDepth=");
    suspendEeSerialPutHex32(diagnostics.threadStoreLockRecursionDepth);
    suspendEeSerialPutString(" marker=C011EC04\n");
}

[[noreturn]] void firstPerThreadRootProviderSafeStop() {
    guidexos_nativeaot_allocation_diagnostics& diagnostics =
        g_guideXosAllocationDiagnostics;
    ++diagnostics.rootProviderSafeStopObserved;
    diagnostics.sentinelChecksAtRootBoundary = diagnostics.sentinelValidationCount;
    validateAllocationContextFixupObjects(true);
    snapshotFirstPerThreadRootList(false);
    diagnostics.threadRegistryMutationCountBeforeRoot =
        diagnostics.rootThreadRegistryGenerationBefore;
    diagnostics.threadRegistryMutationCountAfterRoot =
        diagnostics.rootThreadRegistryGenerationAfter;
    const guidexos_nativeaot_root_thread_record& record =
        diagnostics.rootThreadRecords[0];
    const bool valid =
        diagnostics.rootProviderSafeStopObserved == 1u &&
        diagnostics.gcScanRootsEntryCount == 1u &&
        diagnostics.foreachThreadRequestCount == 1u &&
        diagnostics.foreachThreadEntryCount == 1u &&
        diagnostics.threadIteratorInitializationCount == 1u &&
        diagnostics.registeredThreadCountBeforeRoot == 1u &&
        diagnostics.registeredThreadCountAfterRoot == 1u &&
        diagnostics.enumeratedThreadCount == 1u &&
        diagnostics.includedThreadCount == 1u &&
        diagnostics.excludedThreadCount == 0u &&
        diagnostics.rootThreadRecordCount == 1u &&
        diagnostics.duplicateThreadCount == 0u &&
        diagnostics.threadListIntegrityFailures == 0u &&
        diagnostics.rootThreadListHeadBefore == diagnostics.rootThreadListHeadAfter &&
        diagnostics.rootThreadListTailBefore == diagnostics.rootThreadListTailAfter &&
        diagnostics.rootThreadRegistryGenerationBefore ==
            diagnostics.rootThreadRegistryGenerationAfter &&
        record.registered == 1u && record.initialized == 1u &&
        record.collectionInitiatorMatch == 1u &&
        record.currentThreadMatch == 1u && record.lockOwnerMatch == 1u &&
        diagnostics.rootCurrentThreadIdentity == diagnostics.rootEnumeratedThreadIdentity &&
        diagnostics.rootEnumeratedThreadIdentity == diagnostics.rootCollectionInitiatorIdentity &&
        diagnostics.rootEnumeratedThreadIdentity == diagnostics.rootLockOwnerIdentity &&
        record.allocationContext == diagnostics.allocationContextFixupAfter[0].contextIdentity &&
        diagnostics.rootProviderSourceOrderCategory == kRootProviderSourceThreadStatics &&
        diagnostics.rootProviderRuntimeCategory == kRootProviderSourceThreadStatics &&
        diagnostics.rootProviderRequestCount == 2u &&
        diagnostics.rootProviderEntryCount == 1u &&
        diagnostics.rootProviderSkipCount == 1u &&
        diagnostics.metadataContainerCount == 1u &&
        diagnostics.candidateMetadataLocationCount == 1u &&
        diagnostics.candidateValueReadCount == 0u &&
        diagnostics.rootCandidateDiscoveryCount == 0u &&
        diagnostics.rootCallbacksDelivered == 0u &&
        diagnostics.promotionCallbacksDelivered == 0u &&
        diagnostics.markingEntryCount == 0u &&
        diagnostics.objectMemoryMutationStarted == 0u &&
        diagnostics.restartRequestCount == 0u &&
        diagnostics.restartEntryCount == 0u &&
        diagnostics.managedResumeCount == 0u &&
        diagnostics.stackBoundsRequested == 0u &&
        diagnostics.stackScanningStarted == 0u &&
        diagnostics.threadStaticStorageRequested == 1u &&
        diagnostics.threadStaticScanningStarted == 0u &&
        diagnostics.threadStoreLockRecursionDepth == 1u &&
        diagnostics.managedEntryProhibited == 1u && diagnostics.eeSuspended == 1u &&
        diagnostics.allocationContextFixupCompletionCount == 1u &&
        diagnostics.allocationContextFixupContextsCleared == 1u &&
        diagnostics.objectValidationFailuresBeforeFixup == 0u &&
        diagnostics.objectValidationFailuresAfterFixup == 0u &&
        diagnostics.objectAddressChangesAfterFixup == 0u;
    if (!valid) {
        firstPerThreadRootProviderInvariantFailure();
        diagnostics.rootProviderStopReason = 0xE004u;
        emitFirstPerThreadRootProviderSafeStop();
        guideXosFailFast(9u);
    }
    diagnostics.rootProviderStopReason =
        GUIDEXOS_NATIVEAOT_FIRST_PER_THREAD_ROOT_PROVIDER_SAFE_STOP_MARKER;
    diagnostics.safeStopObserved = 1u;
    diagnostics.stopReason = diagnostics.rootProviderStopReason;
    diagnostics.stage = GUIDEXOS_NATIVEAOT_ALLOC_STAGE_F23_FIRST_PER_THREAD_ROOT_PROVIDER_SAFE_STOP;
    diagnostics.currentRip = reinterpret_cast<gx_uintptr>(_ReturnAddress());
    diagnostics.currentRsp = reinterpret_cast<gx_uintptr>(_AddressOfReturnAddress());
    diagnostics.rootBoundaryFunction = reinterpret_cast<gx_uintptr>(
        &firstPerThreadRootProviderSafeStop);
    emitFirstPerThreadRootProviderSafeStop();
    for (;;) {
    }
}

#endif

#endif

#if defined(GUIDEXOS_NATIVEAOT_MANAGED_ALLOCATION) && defined(GUIDEXOS_NATIVEAOT_REAL_GC_ALLOCATION)
#if defined(GUIDEXOS_NATIVEAOT_SEGMENT_BOUNDARY_ALLOCATION)
using GuideXosVmTraceCountFn =
    ::nativeaot_vm_size (*)();
using GuideXosVmTraceAtFn = bool (*) (
    ::nativeaot_vm_size,
    guidexos::nativeaot::virtual_memory::TraceEvent*);
using GuideXosSegmentDescribeFn = int32_t (*) (
    void*, gx_uintptr*, gx_uintptr*, gx_uintptr*, gx_uintptr*, gx_uintptr*,
    gx_uint32*, gx_uint32*);
GuideXosVmTraceCountFn g_guideXosVmTraceCount = nullptr;
GuideXosVmTraceAtFn g_guideXosVmTraceAt = nullptr;
GuideXosSegmentDescribeFn g_guideXosSegmentDescribe = nullptr;
bool g_guideXosSegmentBoundaryExperimentRequested = false;
#if defined(GUIDEXOS_NATIVEAOT_FIRST_COLLECTION_BOUNDARY_ALLOCATION)
bool g_guideXosFirstCollectionBoundaryExperimentRequested = false;
#endif

void markSegmentBoundaryFailure(gx_uint32 reason) {
    ++g_guideXosAllocationDiagnostics.pointerContractFailures;
    if (g_guideXosAllocationDiagnostics.failureReason == 0u) {
        g_guideXosAllocationDiagnostics.failureReason = reason;
    }
}

void beginSegmentBoundaryExperiment() {
    g_guideXosSegmentBoundaryExperimentRequested = true;
    const gx_size traceCount = g_guideXosVmTraceCount == nullptr
        ? 0u : g_guideXosVmTraceCount();
    g_guideXosAllocationDiagnostics.experimentMode = 2u;
    g_guideXosAllocationDiagnostics.selectedArrayLength = kSegmentBoundaryArrayLength;
    g_guideXosAllocationDiagnostics.hardAllocationLimit = kSegmentBoundaryHardLimit;
    g_guideXosAllocationDiagnostics.vmTraceStartCount =
        static_cast<gx_uint32>(traceCount);
    g_guideXosAllocationDiagnostics.vmTraceCursor =
        static_cast<gx_uint32>(traceCount);
    g_guideXosAllocationDiagnostics.vmTraceEndCount =
        static_cast<gx_uint32>(traceCount);
}

#if defined(GUIDEXOS_NATIVEAOT_SEGMENT_TRANSITION_ALLOCATION)
void beginSegmentTransitionExperiment() {
    g_guideXosSegmentBoundaryExperimentRequested = true;
    const gx_size traceCount = g_guideXosVmTraceCount == nullptr
        ? 0u : g_guideXosVmTraceCount();
    g_guideXosAllocationDiagnostics.experimentMode = 3u;
    g_guideXosAllocationDiagnostics.selectedArrayLength = kSegmentTransitionArrayLength;
    g_guideXosAllocationDiagnostics.hardAllocationLimit = kSegmentTransitionHardLimit;
    g_guideXosAllocationDiagnostics.hardRefillLimit = kSegmentTransitionHardRefillLimit;
    g_guideXosAllocationDiagnostics.hardCommitLimit = kSegmentTransitionHardCommitLimit;
    g_guideXosAllocationDiagnostics.hardSegmentTransitionLimit = 1u;
    g_guideXosAllocationDiagnostics.vmTraceStartCount = static_cast<gx_uint32>(traceCount);
    g_guideXosAllocationDiagnostics.vmTraceCursor = static_cast<gx_uint32>(traceCount);
    g_guideXosAllocationDiagnostics.vmTraceEndCount = static_cast<gx_uint32>(traceCount);
}
#endif

#if defined(GUIDEXOS_NATIVEAOT_FIRST_COLLECTION_BOUNDARY_ALLOCATION)
void beginFirstCollectionBoundaryExperiment() {
    g_guideXosSegmentBoundaryExperimentRequested = true;
    g_guideXosFirstCollectionBoundaryExperimentRequested = true;
    const gx_size traceCount = g_guideXosVmTraceCount == nullptr
        ? 0u : g_guideXosVmTraceCount();
    g_guideXosAllocationDiagnostics.experimentMode = 4u;
    g_guideXosAllocationDiagnostics.selectedArrayLength =
        kFirstCollectionBoundaryArrayLength;
    g_guideXosAllocationDiagnostics.hardAllocationLimit =
        kFirstCollectionBoundaryHardLimit;
    g_guideXosAllocationDiagnostics.hardRefillLimit =
        GUIDEXOS_NATIVEAOT_MAX_REFILL_HISTORY;
    g_guideXosAllocationDiagnostics.hardCommitLimit = 32u;
    g_guideXosAllocationDiagnostics.hardSegmentTransitionLimit = 1u;
    g_guideXosAllocationDiagnostics.vmTraceStartCount =
        static_cast<gx_uint32>(traceCount);
    g_guideXosAllocationDiagnostics.vmTraceCursor =
        static_cast<gx_uint32>(traceCount);
    g_guideXosAllocationDiagnostics.vmTraceEndCount =
        static_cast<gx_uint32>(traceCount);
}
#endif

void inspectSegmentBoundaryTrace(
    gx_uintptr segmentBase, gx_uintptr segmentReserved,
    gx_uintptr segmentCommitted, gx_uint32 allocationOrdinal,
    gx_uintptr objectAddress, gx_uintptr objectEnd,
    gx_uintptr allocationPointerBefore, gx_uintptr allocationLimitBefore,
    gx_uintptr allocationPointerAfter, gx_uintptr allocationLimitAfter,
    gx_uint32 collectionBefore, gx_uint32 collectionAfter,
    bool segmentChanged) {
    const gx_size traceCount = g_guideXosVmTraceCount == nullptr
        ? 0u : g_guideXosVmTraceCount();
    const nativeaot_vm_size cursor =
        g_guideXosAllocationDiagnostics.vmTraceCursor;
    const gx_uintptr rangeStart = segmentBase >= 0x1000u
        ? segmentBase - 0x1000u : segmentBase;
    const gx_uintptr rangeEnd = segmentReserved >= rangeStart
        ? segmentReserved : 0u;
    bool commitObservedForAllocation = false;
    for (gx_size index = cursor; index < traceCount; ++index) {
        guidexos::nativeaot::virtual_memory::TraceEvent event{};
        if (g_guideXosVmTraceAt == nullptr || !g_guideXosVmTraceAt(index, &event)) continue;
        if (event.operation != guidexos::nativeaot::virtual_memory::TraceOperation::Commit ||
            event.result != gxos::runtime::virtual_memory::VmResult::Ok) {
            continue;
        }
        const gx_uintptr commitStart = reinterpret_cast<gx_uintptr>(event.address);
        const gx_uintptr commitEnd = event.size > (~static_cast<gx_uintptr>(0) - commitStart)
            ? ~static_cast<gx_uintptr>(0) : commitStart + event.size;
        const bool overlapsSegment = commitStart < rangeEnd && commitEnd > rangeStart;
        if (!overlapsSegment) continue;
        commitObservedForAllocation = true;

        // The first collector-backed allocation establishes the initial
        // segment quantum.  It is an initial heap commitment, not the
        // post-establishment boundary this experiment is measuring.  Keep
        // the exact trace evidence, advance the cursor, and continue until
        // the first later commitment or segment transition.
        if (allocationOrdinal == 1u) {
            ++g_guideXosAllocationDiagnostics.initialHeapCommitEventCount;
            if (g_guideXosAllocationDiagnostics.initialHeapCommitObserved == 0u) {
                g_guideXosAllocationDiagnostics.initialHeapCommitObserved = 1u;
                g_guideXosAllocationDiagnostics.initialHeapCommitTraceIndex = index;
                g_guideXosAllocationDiagnostics.initialHeapCommitAddress = commitStart;
                g_guideXosAllocationDiagnostics.initialHeapCommitRequested = event.size;
                g_guideXosAllocationDiagnostics.initialHeapCommitActual = event.size;
                g_guideXosAllocationDiagnostics.initialHeapCommittedAfter = segmentCommitted;
                g_guideXosAllocationDiagnostics.initialHeapCommittedBefore =
                    segmentCommitted >= event.size ? segmentCommitted - event.size : 0u;
            }
            continue;
        }
        ++g_guideXosAllocationDiagnostics.vmCommitEventCount;
        ++g_guideXosAllocationDiagnostics.heapCommitEventCount;
        if (g_guideXosAllocationDiagnostics.boundaryType == 0u) {
            g_guideXosAllocationDiagnostics.boundaryType = 1u;
            g_guideXosAllocationDiagnostics.boundaryAllocationOrdinal = allocationOrdinal;
            g_guideXosAllocationDiagnostics.boundaryRefillOrdinal =
                g_guideXosAllocationDiagnostics.refillHistoryCount + 1u;
            g_guideXosAllocationDiagnostics.boundarySegmentIdentity =
                g_guideXosAllocationDiagnostics.currentSegmentIdentity;
            g_guideXosAllocationDiagnostics.boundarySegmentBase = segmentBase;
            g_guideXosAllocationDiagnostics.boundarySegmentAllocated =
                g_guideXosAllocationDiagnostics.heapAllocated;
            g_guideXosAllocationDiagnostics.boundarySegmentCommitted = segmentCommitted;
            g_guideXosAllocationDiagnostics.boundarySegmentReserved = segmentReserved;
            g_guideXosAllocationDiagnostics.boundaryCommitAddress = commitStart;
            g_guideXosAllocationDiagnostics.boundaryCommitRequested = event.size;
            g_guideXosAllocationDiagnostics.boundaryCommitActual = event.size;
            g_guideXosAllocationDiagnostics.boundaryCommittedAfter = segmentCommitted;
            g_guideXosAllocationDiagnostics.boundaryCommittedBefore =
                segmentCommitted >= event.size ? segmentCommitted - event.size : 0u;
            g_guideXosAllocationDiagnostics.boundaryObjectAddress = objectAddress;
            g_guideXosAllocationDiagnostics.boundaryObjectEnd = objectEnd;
            g_guideXosAllocationDiagnostics.boundaryAllocationContextBefore = allocationPointerBefore;
            g_guideXosAllocationDiagnostics.boundaryAllocationContextAfter = allocationPointerAfter;
            g_guideXosAllocationDiagnostics.boundaryAllocationLimitBefore = allocationLimitBefore;
            g_guideXosAllocationDiagnostics.boundaryAllocationLimitAfter = allocationLimitAfter;
            g_guideXosAllocationDiagnostics.boundaryCommitValidated = 1u;
        }
    }
    g_guideXosAllocationDiagnostics.vmTraceCursor = static_cast<gx_uint32>(traceCount);
    g_guideXosAllocationDiagnostics.vmTraceEndCount = static_cast<gx_uint32>(traceCount);
    if (segmentChanged && g_guideXosAllocationDiagnostics.boundaryType == 0u) {
        g_guideXosAllocationDiagnostics.boundaryType = 2u;
        g_guideXosAllocationDiagnostics.boundaryAllocationOrdinal = allocationOrdinal;
        g_guideXosAllocationDiagnostics.boundaryRefillOrdinal =
            g_guideXosAllocationDiagnostics.refillHistoryCount + 1u;
        g_guideXosAllocationDiagnostics.boundarySegmentIdentity =
            g_guideXosAllocationDiagnostics.currentSegmentIdentity;
        g_guideXosAllocationDiagnostics.boundarySegmentBase = segmentBase;
        g_guideXosAllocationDiagnostics.boundarySegmentAllocated =
            g_guideXosAllocationDiagnostics.heapAllocated;
        g_guideXosAllocationDiagnostics.boundarySegmentCommitted = segmentCommitted;
        g_guideXosAllocationDiagnostics.boundarySegmentReserved = segmentReserved;
        g_guideXosAllocationDiagnostics.boundaryObjectAddress = objectAddress;
        g_guideXosAllocationDiagnostics.boundaryObjectEnd = objectEnd;
        g_guideXosAllocationDiagnostics.boundaryAllocationContextBefore = allocationPointerBefore;
        g_guideXosAllocationDiagnostics.boundaryAllocationContextAfter = allocationPointerAfter;
        g_guideXosAllocationDiagnostics.boundaryAllocationLimitBefore = allocationLimitBefore;
        g_guideXosAllocationDiagnostics.boundaryAllocationLimitAfter = allocationLimitAfter;
        g_guideXosAllocationDiagnostics.boundarySegmentValidated = 1u;
    }
    (void)commitObservedForAllocation;
    (void)collectionBefore;
    (void)collectionAfter;
}

void recordSegmentBoundaryRefill(
    bool fastPath, gx_uint32 allocationOrdinal,
    gx_uintptr allocationPointerBefore, gx_uintptr allocationLimitBefore,
    gx_uintptr objectAddress, gx_uintptr objectEnd,
    gx_uintptr allocationPointerAfter, gx_uintptr allocationLimitAfter,
    gx_uintptr segmentIdentity, gx_uintptr segmentBase,
    gx_uintptr segmentAllocated, gx_uintptr segmentCommitted,
    gx_uintptr segmentReserved, gx_uint32 collectionBefore,
    gx_uint32 collectionAfter, bool segmentChanged) {
    if (fastPath) return;
    const gx_uint32 ordinal = g_guideXosAllocationDiagnostics.refillHistoryCount;
    if (ordinal >= GUIDEXOS_NATIVEAOT_MAX_REFILL_HISTORY) {
        g_guideXosAllocationDiagnostics.refillHistoryOverflow = 1u;
        return;
    }
    auto& entry = g_guideXosAllocationDiagnostics.refillHistory[ordinal];
    entry = {};
    entry.ordinal = ordinal + 1u;
    entry.allocationOrdinal = allocationOrdinal;
    entry.fastPath = 0u;
    entry.segmentChanged = segmentChanged ? 1u : 0u;
    entry.boundaryType = g_guideXosAllocationDiagnostics.boundaryType;
    entry.collectionBefore = collectionBefore;
    entry.collectionAfter = collectionAfter;
    entry.traceStart = g_guideXosAllocationDiagnostics.vmTraceStartCount;
    entry.traceEnd = g_guideXosAllocationDiagnostics.vmTraceEndCount;
    entry.contextBefore = allocationPointerBefore;
    entry.limitBefore = allocationLimitBefore;
    entry.remainingBefore = allocationLimitBefore >= allocationPointerBefore
        ? allocationLimitBefore - allocationPointerBefore : 0u;
    entry.objectAddress = objectAddress;
    entry.objectEnd = objectEnd;
    entry.contextAfter = allocationPointerAfter;
    entry.limitAfter = allocationLimitAfter;
    entry.segmentIdentity = segmentIdentity;
    entry.segmentBase = segmentBase;
    entry.segmentAllocated = segmentAllocated;
    entry.segmentCommitted = segmentCommitted;
    entry.segmentReserved = segmentReserved;
    if (allocationOrdinal == 1u &&
        g_guideXosAllocationDiagnostics.initialHeapCommitObserved != 0u) {
        entry.vmCommitObserved = 1u;
        entry.commitAddress = g_guideXosAllocationDiagnostics.initialHeapCommitAddress;
        entry.commitRequested = g_guideXosAllocationDiagnostics.initialHeapCommitRequested;
        entry.commitActual = g_guideXosAllocationDiagnostics.initialHeapCommitActual;
        entry.committedBefore = g_guideXosAllocationDiagnostics.initialHeapCommittedBefore;
        entry.committedAfter = g_guideXosAllocationDiagnostics.initialHeapCommittedAfter;
    } else if (g_guideXosAllocationDiagnostics.boundaryAllocationOrdinal == allocationOrdinal) {
        entry.vmCommitObserved = g_guideXosAllocationDiagnostics.boundaryType == 1u ? 1u : 0u;
        entry.boundaryType = g_guideXosAllocationDiagnostics.boundaryType;
        entry.commitAddress = g_guideXosAllocationDiagnostics.boundaryCommitAddress;
        entry.commitRequested = g_guideXosAllocationDiagnostics.boundaryCommitRequested;
        entry.commitActual = g_guideXosAllocationDiagnostics.boundaryCommitActual;
        entry.committedBefore = g_guideXosAllocationDiagnostics.boundaryCommittedBefore;
        entry.committedAfter = g_guideXosAllocationDiagnostics.boundaryCommittedAfter;
    }
    ++g_guideXosAllocationDiagnostics.refillHistoryCount;
}

extern "C" __declspec(dllexport) void __cdecl
guideXosManagedAllocationBeginSegmentBoundaryExperiment() {
    beginSegmentBoundaryExperiment();
}

#if defined(GUIDEXOS_NATIVEAOT_FIRST_COLLECTION_BOUNDARY_ALLOCATION)
extern "C" __declspec(dllexport) void __cdecl
guideXosManagedAllocationBeginFirstCollectionBoundaryExperiment() {
    beginFirstCollectionBoundaryExperiment();
}
#endif

#if defined(GUIDEXOS_NATIVEAOT_SEGMENT_TRANSITION_ALLOCATION)
extern "C" __declspec(dllexport) void __cdecl
guideXosManagedAllocationBeginSegmentTransitionExperiment() {
    beginSegmentTransitionExperiment();
}
#endif

extern "C" void __cdecl guideXosManagedAllocationInstallVmTraceCallbacks(
    gx_uintptr traceCount, gx_uintptr traceAt) {
    g_guideXosVmTraceCount = reinterpret_cast<GuideXosVmTraceCountFn>(traceCount);
    g_guideXosVmTraceAt = reinterpret_cast<GuideXosVmTraceAtFn>(traceAt);
}

extern "C" void __cdecl guideXosManagedAllocationInstallSegmentDescriber(
    gx_uintptr describeSegment) {
    g_guideXosSegmentDescribe =
        reinterpret_cast<GuideXosSegmentDescribeFn>(describeSegment);
}
#endif

// This record is deliberately a fixed native store.  It is written with
// plain bounded stores only; it never calls PAL, takes a lock, waits, or
// allocates.  The final stage store is the publication point for a watchdog.
__declspec(noinline) void recordAllocationStage(
    gx_uint32 stage, void* eeType = nullptr, gx_size length = 0,
    gx_size objectSize = 0, gx_uintptr allocationPointer = 0,
    gx_uintptr allocationLimit = 0, gx_uintptr runtimeThread = 0,
    gx_uintptr transitionFrame = 0) {
    volatile guidexos_nativeaot_allocation_diagnostics* diagnostics =
        &g_guideXosAllocationDiagnostics;
    diagnostics->sequence += 1u;
    diagnostics->currentRip = reinterpret_cast<gx_uintptr>(_ReturnAddress());
    diagnostics->currentRsp = reinterpret_cast<gx_uintptr>(_AddressOfReturnAddress());
    diagnostics->eeType = reinterpret_cast<gx_uintptr>(eeType);
    diagnostics->requestedArrayLength = static_cast<gx_uint32>(length);
    diagnostics->requestedObjectSize = static_cast<gx_uint32>(objectSize);
    diagnostics->computedObjectSize = objectSize;
    diagnostics->allocationContextBefore = allocationPointer;
    diagnostics->allocationLimitBefore = allocationLimit;
    diagnostics->runtimeThreadRecord = runtimeThread;
    diagnostics->transitionFrame = transitionFrame;
    diagnostics->stage = stage;
}
#endif

// These symbols are provided by the NativeAOT image. They are data symbols,
// not Server object layouts. The loader writes _tls_index before entry.
extern "C" gx_uint32 _tls_index;
extern "C" gx_uint32 g_flsIndex = kGuideXosFlsIndex;

volatile gx_uint32 g_guideXosRuntimeStartupState = 0;

#if defined(GUIDEXOS_NATIVEAOT_MANAGED_ALLOCATION)
// The stock NativeAOT bootstrapper invokes this generated entry point after
// RhInitialize and before managed entry.  The guideXOS disposable ELF launch
// enters ManagedMain directly, so the runtime-pack must preserve that same
// production startup contract here.
extern "C" void InitializeModules(
    void* osModule, void** pModuleHeaders, int count,
    void** pClasslibFunctions, int nClasslibFunctions);
extern "C" void* __modules_a[];
extern "C" void* __modules_z[];
// These are the same C++ bookend functions passed by the locked
// Bootstrap/main.cpp.  They delimit the linker-produced managed-code and
// unboxing-stub ranges; they are not guideXOS-owned synthetic symbols.
extern "C" void* PalGetModuleHandleFromPointer(void* pointer);
extern "C" void GetRuntimeException();
extern "C" void RuntimeFailFast();
extern "C" void AppendExceptionStackFrame();
extern "C" void GetSystemArrayEEType();
extern "C" void OnFirstChanceException();
extern "C" void OnUnhandledException();
extern "C" void IDynamicCastableIsInterfaceImplemented();
extern "C" void IDynamicCastableGetInterfaceImplementation();
bool g_guideXosNativeAotModulesInitialized = false;
bool g_guideXosNativeAotCodeManagerRegistered = false;
void* g_guideXosNativeAotClasslibFunctions[16] = {};
#endif

#if defined(GUIDEXOS_NATIVEAOT_THREAD_STATIC_PROOF)
extern "C" volatile guidexos_nativeaot_thread_static_diagnostics
    g_guideXosThreadStaticDiagnostics = { 1u };
#endif

[[noreturn]] void guideXosFailFast(gx_uint32 reason) {
#if defined(GUIDEXOS_NATIVEAOT_MANAGED_ALLOCATION) && defined(GUIDEXOS_NATIVEAOT_REAL_GC_ALLOCATION)
    g_guideXosAllocationDiagnostics.failFastReason = reason;
#if defined(GUIDEXOS_NATIVEAOT_NEXT_GENUINE_ROOT_PROVIDER_ALLOCATION)
    suspendEeSerialPutString(
        "[nativeaot-gc-next-genuine-root-provider] fail-fast reason=");
    suspendEeSerialPutHex32(reason);
    suspendEeSerialPutString(" stage=");
    suspendEeSerialPutHex32(g_guideXosAllocationDiagnostics.stage);
    suspendEeSerialPutString(" providerEntries=");
    suspendEeSerialPutHex32(
        g_guideXosAllocationDiagnostics.c011ec15ProviderEntryCount);
    suspendEeSerialPutString(" rootSlots=");
    suspendEeSerialPutHex32(
        g_guideXosAllocationDiagnostics.c011ec15RootSlotVisitCount);
    suspendEeSerialPutString(" nonNull=");
    suspendEeSerialPutHex32(
        g_guideXosAllocationDiagnostics.c011ec15NonNullCandidateCount);
    suspendEeSerialPutString("\n");
#endif
#endif
    __fastfail(kFastFailFatalAppExit);
    for (;;) {
    }
}

unsigned char* currentTlsBlock() {
    void** vector = reinterpret_cast<void**>(__readgsqword(kTlsVectorOffset));
    if (vector == nullptr || _tls_index == kFlsOutOfIndexes) {
        return nullptr;
    }
    return reinterpret_cast<unsigned char*>(vector[_tls_index]);
}

void** flsCell(unsigned char* block, gx_uint32 index) {
    if (block == nullptr || index >= kFlsCellCount) {
        return nullptr;
    }
    return reinterpret_cast<void**>(block + kFlsCellOffset + (index * sizeof(void*)));
}

unsigned char* runtimeCell(unsigned char* block) {
    if (block == nullptr) {
        return nullptr;
    }
    return block + kRuntimeCellOffset;
}

#if defined(GUIDEXOS_NATIVEAOT_MANAGED_ALLOCATION)
void initializeNativeAotModules() {
    if (g_guideXosNativeAotModulesInitialized) {
        return;
    }

    // Keep the classlib table identical to the locked NativeAOT bootstrapper.
    // The bootstrapper's table has internal linkage, so the runtime-pack
    // bridge carries the same process-lifetime table rather than inventing a
    // module descriptor or a thread-static storage location.
    g_guideXosNativeAotClasslibFunctions[0] =
        reinterpret_cast<void*>(reinterpret_cast<void (*)()>(GetRuntimeException));
    g_guideXosNativeAotClasslibFunctions[1] =
        reinterpret_cast<void*>(reinterpret_cast<void (*)()>(RuntimeFailFast));
    g_guideXosNativeAotClasslibFunctions[3] =
        reinterpret_cast<void*>(reinterpret_cast<void (*)()>(AppendExceptionStackFrame));
    g_guideXosNativeAotClasslibFunctions[5] =
        reinterpret_cast<void*>(reinterpret_cast<void (*)()>(GetSystemArrayEEType));
    g_guideXosNativeAotClasslibFunctions[6] =
        reinterpret_cast<void*>(reinterpret_cast<void (*)()>(OnFirstChanceException));
    g_guideXosNativeAotClasslibFunctions[7] =
        reinterpret_cast<void*>(reinterpret_cast<void (*)()>(OnUnhandledException));
    g_guideXosNativeAotClasslibFunctions[8] =
        reinterpret_cast<void*>(reinterpret_cast<void (*)()>(IDynamicCastableIsInterfaceImplemented));
    g_guideXosNativeAotClasslibFunctions[9] =
        reinterpret_cast<void*>(reinterpret_cast<void (*)()>(IDynamicCastableGetInterfaceImplementation));

#if defined(GUIDEXOS_NATIVEAOT_THREAD_STATIC_PROOF)
    ++g_guideXosThreadStaticDiagnostics.moduleInitializationRequests;
#endif
    void* osModule = PalGetModuleHandleFromPointer(
        reinterpret_cast<void*>(&InitializeModules));
    if (osModule == nullptr || __modules_a == nullptr || __modules_z == nullptr ||
        __modules_z < __modules_a) {
        guideXosFailFast(10u);
    }

    // The stock Windows bootstrapper registers the production
    // CoffNativeCodeManager immediately after RhInitialize and before
    // InitializeModules.  The guideXOS direct-ELF entry bypasses that
    // Bootstrap/main.cpp path, so perform the same contract here using the
    // actual PE image header at osModule and the linker-produced bookends.
    void* managedCodeStart = nullptr;
    uint32_t managedCodeSize = 0u;
    void* unboxingStubsStart = nullptr;
    uint32_t unboxingStubsSize = 0u;
    if (g_guideXosNativeAotCodeManagerRegistered ||
        !getNativeAotRange(
            reinterpret_cast<void*>(&__managedcode_a),
            reinterpret_cast<void*>(&__managedcode_z),
            &managedCodeStart,
            &managedCodeSize) ||
        !getNativeAotRange(
            reinterpret_cast<void*>(&__unbox_a),
            reinterpret_cast<void*>(&__unbox_z),
            &unboxingStubsStart,
            &unboxingStubsSize)) {
        guideXosFailFast(0xC011EC17u);
    }
    if (!RhRegisterOSModule(
            osModule,
            managedCodeStart,
            managedCodeSize,
            unboxingStubsStart,
            unboxingStubsSize,
            g_guideXosNativeAotClasslibFunctions,
            16u)) {
#if defined(GUIDEXOS_NATIVEAOT_SINGLE_THREAD_SUSPEND_EE_ALLOCATION)
        suspendEeSerialPutString(
            "[nativeaot-code-manager] production code-manager registration failed\n");
#endif
        guideXosFailFast(0xC011EC17u);
    }
    g_guideXosNativeAotCodeManagerRegistered = true;
#if defined(GUIDEXOS_NATIVEAOT_SINGLE_THREAD_SUSPEND_EE_ALLOCATION)
    suspendEeSerialPutString(
        "[nativeaot-code-manager] registered module=");
    suspendEeSerialPutHex64(reinterpret_cast<gx_uintptr>(osModule));
    suspendEeSerialPutString(" managedStart=");
    suspendEeSerialPutHex64(reinterpret_cast<gx_uintptr>(managedCodeStart));
    suspendEeSerialPutString(" managedSize=");
    suspendEeSerialPutHex64(static_cast<gx_uintptr>(managedCodeSize));
    suspendEeSerialPutString(" managedEnd=");
    suspendEeSerialPutHex64(
        reinterpret_cast<gx_uintptr>(managedCodeStart) + managedCodeSize);
    suspendEeSerialPutString(" manager=");
    suspendEeSerialPutHex64(reinterpret_cast<gx_uintptr>(
        GetRuntimeInstance()->GetCodeManagerForAddress(managedCodeStart)));
    suspendEeSerialPutString(" registrationCount=00000001\n");
#endif

    g_guideXosNativeAotModulesInitialized = true;
#if defined(GUIDEXOS_NATIVEAOT_THREAD_STATIC_PROOF)
    ++g_guideXosThreadStaticDiagnostics.moduleInitializationEntries;
#endif
    InitializeModules(
        osModule,
        __modules_a,
        static_cast<int>(__modules_z - __modules_a),
        g_guideXosNativeAotClasslibFunctions,
        16);
#if defined(GUIDEXOS_NATIVEAOT_THREAD_STATIC_PROOF)
    ++g_guideXosThreadStaticDiagnostics.moduleInitializationCompletions;
#endif
}
#endif

#if defined(GUIDEXOS_NATIVEAOT_MANAGED_ALLOCATION)
bool sourceDerivedArrayObjectSize(void* eeType, gx_size length, gx_size* objectSize) {
    if (eeType == nullptr || objectSize == nullptr) {
        return false;
    }

    const auto* typeBytes = reinterpret_cast<const unsigned char*>(eeType);
    const gx_size componentSize = *reinterpret_cast<const gx_uint16*>(typeBytes);
    const gx_size baseSize = *reinterpret_cast<const gx_uint32*>(typeBytes + sizeof(gx_uint32));
    if (componentSize != 0u && length >
        ((~static_cast<gx_size>(0)) - baseSize - 7u) / componentSize) {
        return false;
    }

    const gx_size unalignedObjectSize = baseSize + (componentSize * length);
    *objectSize = (unalignedObjectSize + 7u) & ~static_cast<gx_size>(7u);
    return true;
}

#if defined(GUIDEXOS_NATIVEAOT_REAL_GC_ALLOCATION) && (defined(GUIDEXOS_NATIVEAOT_FIRST_REFILL_ALLOCATION) || defined(GUIDEXOS_NATIVEAOT_SEGMENT_BOUNDARY_ALLOCATION))
void markBoundedAllocationFailure(gx_uint32 reason) {
    ++g_guideXosAllocationDiagnostics.pointerContractFailures;
    if (g_guideXosAllocationDiagnostics.failureReason == 0u) {
        g_guideXosAllocationDiagnostics.failureReason = reason;
    }
}
#endif

gx_size alignedArrayObjectSize(gx_size length) {
    if (length > ((~static_cast<gx_size>(0)) - kManagedArrayBaseSize - 7u) / kManagedArrayComponentSize) {
        return 0u;
    }
    const gx_size unalignedObjectSize = kManagedArrayBaseSize + (kManagedArrayComponentSize * length);
    return (unalignedObjectSize + 7u) & ~static_cast<gx_size>(7u);
}

#if !defined(GUIDEXOS_NATIVEAOT_REAL_GC_ALLOCATION)
void markAllocationOutOfMemory(unsigned char* cell, gx_uintptr allocationPointer) {
    g_guideXosAllocationDiagnostics.outOfMemory = 1u;
    g_guideXosAllocationDiagnostics.controlledOutOfMemory = 0u;
    g_guideXosAllocationDiagnostics.allocationPointerBeforeFailure = allocationPointer;
    g_guideXosAllocationDiagnostics.allocationPointerAfterFailure = allocationPointer;
    const gx_uintptr allocationLimit = cell == nullptr
        ? 0u
        : reinterpret_cast<gx_uintptr>(*reinterpret_cast<void**>(cell + sizeof(void*)));
    g_guideXosAllocationDiagnostics.remainingBytesBeforeFailure = allocationLimit >= allocationPointer
        ? allocationLimit - allocationPointer
        : 0u;
    g_guideXosAllocationDiagnostics.allocationPointerAfter = allocationPointer;
}
#endif

#endif

void initializeRuntimeState(unsigned char* block) {
    if (block == nullptr) {
        guideXosFailFast(1u);
    }

#if defined(GUIDEXOS_NATIVEAOT_MANAGED_ALLOCATION)
#if !defined(GUIDEXOS_NATIVEAOT_REAL_GC_ALLOCATION)
    // Native ELF launch cleanup may recycle the same TLS block for the next
    // application launch. Allocation state and proof diagnostics are launch
    // scoped, so the reverse-P/Invoke entry is the explicit fresh-launch
    // boundary rather than the stale TLS initialized bit.
    volatile unsigned char* diagnostics = reinterpret_cast<volatile unsigned char*>(&g_guideXosAllocationDiagnostics);
    for (gx_size i = 0; i < sizeof(g_guideXosAllocationDiagnostics); ++i) {
        diagnostics[i] = 0;
    }
    for (gx_size i = 0; i < kManagedHeapBytes; ++i) {
        g_guideXosManagedHeap[i] = 0;
    }
    const gx_uintptr heapBase = reinterpret_cast<gx_uintptr>(g_guideXosManagedHeap);
    const gx_uintptr heapLimit = heapBase + kManagedHeapBytes;
    *reinterpret_cast<void**>(runtimeCell(block)) = reinterpret_cast<void*>(heapBase);
    *reinterpret_cast<void**>(runtimeCell(block) + sizeof(void*)) = reinterpret_cast<void*>(heapLimit);
    g_guideXosAllocationDiagnostics.heapInitialized = 1u;
    g_guideXosAllocationDiagnostics.heapBase = heapBase;
    g_guideXosAllocationDiagnostics.heapSize = kManagedHeapBytes;
    g_guideXosAllocationDiagnostics.initialAllocationPointer = heapBase;
    g_guideXosAllocationDiagnostics.allocationPointerAfter = heapBase;
    g_guideXosAllocationDiagnostics.heapExpansionOccurred = 0u;
#else
    // The real-GC experiment deliberately leaves the EE allocation context
    // at the value established by Thread::Construct.  The first managed
    // allocation must therefore enter the stock slow path and let the
    // Workstation GC publish the context and owning segment.
    if (!g_guideXosRealGcDiagnosticsInitialized) {
        volatile unsigned char* diagnostics = reinterpret_cast<volatile unsigned char*>(&g_guideXosAllocationDiagnostics);
        for (gx_size i = 0; i < sizeof(g_guideXosAllocationDiagnostics); ++i) {
            diagnostics[i] = 0;
        }
        g_guideXosAllocationDiagnostics.schemaVersion = 1u;
        g_guideXosRealGcDiagnosticsInitialized = true;
#if defined(GUIDEXOS_NATIVEAOT_SEGMENT_BOUNDARY_ALLOCATION)
        if (g_guideXosSegmentBoundaryExperimentRequested) {
#if defined(GUIDEXOS_NATIVEAOT_FIRST_COLLECTION_BOUNDARY_ALLOCATION)
            if (g_guideXosFirstCollectionBoundaryExperimentRequested) {
                beginFirstCollectionBoundaryExperiment();
            } else {
                beginSegmentBoundaryExperiment();
            }
#else
            beginSegmentBoundaryExperiment();
#endif
        }
#endif
    }
#endif
#endif

    if (g_guideXosRuntimeStartupState == 0) {
        // This is the guideXOS runtime-pack's deterministic FLS namespace.
        // It is not a Windows FLS slot and is never shared across threads.
        g_flsIndex = kGuideXosFlsIndex;
        g_guideXosRuntimeStartupState = 1;
    }

    // The current image's TLS block is allocated by the host from the
    // generated _tls_start/_tls_end envelope. The minimum is checked by the
    // staging/build scripts; these offsets are within the 0x110-byte proof
    // block. The cell points into that same per-thread block, so no global
    // Thread object or host C++ object layout crosses the boundary.
    unsigned char* cell = runtimeCell(block);
    if (*reinterpret_cast<gx_uint32*>(cell + kRuntimeCellInitializedOffset) == 0u) {
#if defined(GUIDEXOS_NATIVEAOT_MANAGED_ALLOCATION)
#if !defined(GUIDEXOS_NATIVEAOT_REAL_GC_ALLOCATION)
        // RhpNewArray/RhpNewFast consume the current thread's allocation
        // context at TLS block + 0x30: pointer at +0 and limit at +8.
        // This is a bounded no-collection allocation context, not a GC heap.
#else
        // The real-GC mode intentionally does not seed or replace the
        // Thread::m_alloc_context fields.
#endif
#else
        *reinterpret_cast<void**>(cell) = cell;
#endif
        *reinterpret_cast<gx_uint32*>(cell + kRuntimeCellInitializedOffset) = 1u;

        void** value = flsCell(block, g_flsIndex);
        if (value != nullptr && *value == nullptr) {
            *value = cell;
        }
    }

#if defined(GUIDEXOS_NATIVEAOT_MANAGED_ALLOCATION)
    // This is the real NativeAOT startup contract.  InitializeModules owns
    // metadata rehydration, TypeManager/module publication, GC static-base
    // initialization, and eager constructors; do not pre- or post-populate a
    // substitute descriptor here.
    initializeNativeAotModules();
#endif
#if defined(GUIDEXOS_NATIVEAOT_THREAD_STATIC_PROOF)
    // The disposable direct ELF launcher constructs the current NativeAOT
    // Thread but, unlike the stock bootstrapper's managed-thread entry path,
    // does not link it into the real ThreadStore.  Publish that same current
    // Thread before the proof observes ownership; this does not allocate or
    // synthesize a second managed thread.
    registerCurrentThreadInThreadStoreBeforeLock(
        ThreadStore::GetCurrentThreadIfAvailable());
#endif
}

} // namespace

#if defined(GUIDEXOS_NATIVEAOT_MANAGED_ALLOCATION)
// HostLogProof's generated NativeAOT P/Invoke slot is intentionally bound by
// the application-scoped runtime pack. The ELF loader does not run the Windows
// module resolver that would normally populate this slot.
#if defined(GUIDEXOS_NATIVEAOT_THREAD_STATIC_PROOF)
extern "C" void* __pinvoke_HostLogProof__Module____Internal__guideXosManagedThreadStaticProofRecord__Ansi;
extern "C" __declspec(dllexport) int __cdecl guideXosManagedThreadStaticProofRecord(
    uint32_t marker, uint32_t kind, uintptr_t assigned, uintptr_t readback,
    uint32_t expected, uint32_t actual, uint32_t identityMatch,
    uint32_t objectValid);
#endif
#if !defined(GUIDEXOS_NATIVEAOT_MANAGED_REPEATED_ALLOCATION) && !defined(GUIDEXOS_NATIVEAOT_REAL_GC_ALLOCATION)
extern "C" void* __pinvoke_HostLogProof__Module____Internal__guideXosManagedArrayHostLog__Ansi;
#endif
#if defined(GUIDEXOS_NATIVEAOT_MANAGED_REPEATED_ALLOCATION)
extern "C" void* __pinvoke_HostLogProof__Module____Internal__guideXosManagedAllocationCanFit__Ansi;
extern "C" void* __pinvoke_HostLogProof__Module____Internal__guideXosManagedAllocationValidateObject__Ansi;
extern "C" void* __pinvoke_HostLogProof__Module____Internal__guideXosManagedAllocationRecordFailure__Ansi;
extern "C" void* __pinvoke_HostLogProof__Module____Internal__guideXosManagedAllocationReport__Ansi;
#elif defined(GUIDEXOS_NATIVEAOT_FIRST_REFILL_ALLOCATION) || defined(GUIDEXOS_NATIVEAOT_SEGMENT_BOUNDARY_ALLOCATION)
extern "C" void* __pinvoke_HostLogProof__Module____Internal__guideXosManagedAllocationValidateObject__Ansi;
#if !defined(GUIDEXOS_NATIVEAOT_FIRST_COLLECTION_BOUNDARY_ALLOCATION)
extern "C" void* __pinvoke_HostLogProof__Module____Internal__guideXosManagedAllocationGetLoopStatus__Ansi;
#endif
extern "C" void* __pinvoke_HostLogProof__Module____Internal__guideXosManagedAllocationGetHardLimit__Ansi;
#if defined(GUIDEXOS_NATIVEAOT_FIRST_COLLECTION_BOUNDARY_ALLOCATION)
extern "C" void* __pinvoke_HostLogProof__Module____Internal__guideXosManagedAllocationRecordSentinelValidation__Ansi;
#endif
#if defined(GUIDEXOS_NATIVEAOT_FIRST_NON_NULL_ROOT_ALLOCATION)
extern "C" void* __pinvoke_HostLogProof__Module____Internal__guideXosManagedThreadStaticProofAssigned__Ansi;
extern "C" void* __pinvoke_HostLogProof__Module____Internal__guideXosManagedThreadStaticProofReadback__Ansi;
#endif
#endif
#endif

#if defined(GUIDEXOS_NATIVEAOT_THREAD_STATIC_PROOF)
namespace {

void captureThreadStaticIdentity() {
    volatile guidexos_nativeaot_thread_static_diagnostics& diagnostics =
        g_guideXosThreadStaticDiagnostics;
    Thread* thread = ThreadStore::GetCurrentThreadIfAvailable();
    diagnostics.runtimeThread = reinterpret_cast<uintptr_t>(thread);
    diagnostics.nativeThreadId = thread == nullptr ? 0u : thread->GetPalThreadIdForLogging();

    unsigned char* block = currentTlsBlock();
    diagnostics.tlsBlock = reinterpret_cast<uintptr_t>(block);
    void** fls = flsCell(block, kGuideXosFlsIndex);
    diagnostics.flsRuntimeIdentity =
        (fls == nullptr || *fls == nullptr) ? 0u : reinterpret_cast<uintptr_t>(*fls);

    if (thread == nullptr) {
        diagnostics.threadStaticStorage = 0u;
        diagnostics.inlinedRootList = 0u;
        diagnostics.inlinedStorageBase = 0u;
        diagnostics.inlinedTypeManager = 0u;
        diagnostics.registeredThreadCount = 0u;
        return;
    }

    diagnostics.threadStaticStorage =
        reinterpret_cast<uintptr_t>(thread->GetThreadStaticStorage());
    InlinedThreadStaticRoot* root = thread->GetInlinedThreadStaticList();
    diagnostics.inlinedRootList = reinterpret_cast<uintptr_t>(root);
    diagnostics.inlinedStorageBase = root == nullptr
        ? 0u : reinterpret_cast<uintptr_t>(root->m_threadStaticsBase);
    diagnostics.inlinedTypeManager = root == nullptr
        ? 0u : reinterpret_cast<uintptr_t>(root->m_typeManager);
    diagnostics.inlinedStorageSize = 0u;
    if (root != nullptr && root->m_threadStaticsBase != nullptr) {
        MethodTable* methodTable = root->m_threadStaticsBase->GetMethodTable();
        diagnostics.inlinedStorageSize = methodTable == nullptr
            ? 0u : methodTable->GetBaseSize();
    }

    uint32_t threadCount = 0u;
    uint32_t rootCount = 0u;
    uintptr_t previousRoot = 0u;
    for (InlinedThreadStaticRoot* current = root;
         current != nullptr && rootCount < 64u;
         current = current->m_next) {
        previousRoot = reinterpret_cast<uintptr_t>(current);
        ++rootCount;
    }
    (void)previousRoot;
    ThreadStore::Iterator iterator;
    while (iterator.GetNext() != nullptr && threadCount < 64u) {
        ++threadCount;
    }
    diagnostics.registeredThreadCount = threadCount;
    diagnostics.duplicateStorageCount = rootCount > 1u ? rootCount - 1u : 0u;
}

} // namespace

extern "C" __declspec(dllexport) int __cdecl
guideXosManagedThreadStaticProofRecord(
    uint32_t marker, uint32_t kind, uintptr_t assigned, uintptr_t readback,
    uint32_t expected, uint32_t actual, uint32_t identityMatch,
    uint32_t objectValid) {
    volatile guidexos_nativeaot_thread_static_diagnostics& diagnostics =
        g_guideXosThreadStaticDiagnostics;
    const uintptr_t previousRoot = diagnostics.inlinedRootList;
    captureThreadStaticIdentity();

#if defined(GUIDEXOS_NATIVEAOT_REAL_GC_ALLOCATION)
    diagnostics.unexpectedGcRequests =
        g_guideXosAllocationDiagnostics.collectionRequestCount;
    diagnostics.collectionEntries =
        g_guideXosAllocationDiagnostics.collectionEntryCount;
    diagnostics.suspensionRequests =
        g_guideXosAllocationDiagnostics.suspensionRequestCount;
#endif

    if (diagnostics.inlinedRootList != 0u && previousRoot == 0u) {
        ++diagnostics.storageInitializationRequests;
        ++diagnostics.storageInitializationEntries;
        ++diagnostics.storageInitializationCompletions;
        ++diagnostics.storageAllocationCount;
    } else if (diagnostics.inlinedRootList != 0u) {
        ++diagnostics.repeatedLookupCount;
    }

    diagnostics.finalMarker = marker;
    if (kind == 1u) {
        if (marker == 0x7A510001u) {
            ++diagnostics.primitiveStartCount;
        } else if (marker == 0x7A510002u) {
            ++diagnostics.primitiveSuccessCount;
        }
        diagnostics.primitiveInitialValue = expected;
        diagnostics.primitiveAssignedValue = actual;
        diagnostics.primitiveReadbackValue = static_cast<uint32_t>(readback);
        if (expected != 0u || actual != static_cast<uint32_t>(readback)) {
            ++diagnostics.primitiveMismatchCount;
        }
    } else if (kind == 2u) {
        if (marker == 0x7A510003u) {
            ++diagnostics.referenceStartCount;
        } else if (marker == 0x7A510004u) {
            ++diagnostics.referenceSuccessCount;
        }
        diagnostics.referenceAssigned = assigned;
        diagnostics.referenceReadback = readback;
        diagnostics.referenceIdentityMatch = identityMatch;
        diagnostics.referenceObjectValid = objectValid;
    }
    return 0;
}

extern "C" __declspec(dllexport) const
guidexos_nativeaot_thread_static_diagnostics*
guideXosManagedThreadStaticProofGetDiagnostics() {
    return const_cast<const guidexos_nativeaot_thread_static_diagnostics*>(
        &g_guideXosThreadStaticDiagnostics);
}
#endif

#if defined(GUIDEXOS_NATIVEAOT_FIRST_COLLECTION_BOUNDARY_ALLOCATION)
void firstCollectionSerialPutChar(char value) {
    if (value == '\n') {
        while ((__inbyte(0x3FDu) & 0x20u) == 0u) {
        }
        __outbyte(0x3F8u, static_cast<unsigned char>('\r'));
    }
    while ((__inbyte(0x3FDu) & 0x20u) == 0u) {
    }
    __outbyte(0x3F8u, static_cast<unsigned char>(value));
}

void firstCollectionSerialPutString(const char* value) {
    while (*value != '\0') {
        firstCollectionSerialPutChar(*value++);
    }
}

void firstCollectionSerialPutHex32(gx_uint32 value) {
    static const char hex[] = "0123456789ABCDEF";
    for (int shift = 28; shift >= 0; shift -= 4) {
        firstCollectionSerialPutChar(hex[(value >> shift) & 0xFu]);
    }
}

void firstCollectionSerialPutHex64(gx_uintptr value) {
    static const char hex[] = "0123456789ABCDEF";
    for (int shift = 60; shift >= 0; shift -= 4) {
        firstCollectionSerialPutChar(hex[(value >> shift) & 0xFu]);
    }
}

void emitFirstCollectionSafeStopMarker() {
    const guidexos_nativeaot_allocation_diagnostics& diagnostics =
        g_guideXosAllocationDiagnostics;
    firstCollectionSerialPutString(
        "[nativeaot-gc-first-collection-boundary] SAFE_STOP marker=");
    firstCollectionSerialPutHex32(diagnostics.firstCollectionBoundaryMarker);
    firstCollectionSerialPutString(" callback=GCToEEInterface::SuspendEE requestCount=");
    firstCollectionSerialPutHex32(diagnostics.firstCollectionRequestCount);
    firstCollectionSerialPutString(" entryCount=");
    firstCollectionSerialPutHex32(diagnostics.firstCollectionEntryCount);
    firstCollectionSerialPutString(" requestedGeneration=");
    firstCollectionSerialPutHex32(diagnostics.requestedGeneration);
    firstCollectionSerialPutString(" reason=");
    firstCollectionSerialPutHex32(diagnostics.collectionReason);
    firstCollectionSerialPutString(" blocking=");
    firstCollectionSerialPutHex32(diagnostics.collectionBlockingMode);
    firstCollectionSerialPutString(" compacting=");
    firstCollectionSerialPutHex32(diagnostics.collectionCompactingMode);
    firstCollectionSerialPutString(" suspensionRequestCount=");
    firstCollectionSerialPutHex32(diagnostics.suspensionRequestCount);
    firstCollectionSerialPutString(" suspensionEntryCount=");
    firstCollectionSerialPutHex32(diagnostics.suspensionEntryCount);
    firstCollectionSerialPutString(" restartResumeCount=");
    firstCollectionSerialPutHex32(diagnostics.restartResumeCount);
    firstCollectionSerialPutString(" heapMutationStarted=");
    firstCollectionSerialPutHex32(diagnostics.heapMutationStarted);
    firstCollectionSerialPutString(" managedExecutionResumed=");
    firstCollectionSerialPutHex32(diagnostics.managedExecutionResumed);
    firstCollectionSerialPutString(" unsupportedContract=");
    firstCollectionSerialPutHex32(diagnostics.firstUnsupportedContract);
    firstCollectionSerialPutString(" allocations=");
    firstCollectionSerialPutHex32(diagnostics.allocationCount);
    firstCollectionSerialPutString(" fast=");
    firstCollectionSerialPutHex32(diagnostics.fastAllocationCount);
    firstCollectionSerialPutString(" rare=");
    firstCollectionSerialPutHex32(diagnostics.rarePathCount);
    firstCollectionSerialPutString(" allocationRequests=");
    firstCollectionSerialPutHex32(diagnostics.allocationRequestCount);
    firstCollectionSerialPutString(" collectionConsidered=");
    firstCollectionSerialPutHex32(diagnostics.collectionConsideredCount);
    firstCollectionSerialPutString(" allocationOrdinal=");
    firstCollectionSerialPutHex32(diagnostics.collectionRequestAllocationOrdinal);
    firstCollectionSerialPutString(" lastObject=");
    firstCollectionSerialPutHex64(diagnostics.currentObject);
    firstCollectionSerialPutString(" objectAddress=0000000000000000 requestedObjectSize=");
    firstCollectionSerialPutHex32(diagnostics.requestedObjectSize);
    firstCollectionSerialPutString(" actualAlignedSize=");
    firstCollectionSerialPutHex64(diagnostics.derivedObjectSize);
    firstCollectionSerialPutString(" allocPtr=");
    firstCollectionSerialPutHex64(diagnostics.collectionEntryAllocPtr);
    firstCollectionSerialPutString(" allocLimit=");
    firstCollectionSerialPutHex64(diagnostics.collectionEntryAllocLimit);
    firstCollectionSerialPutString(" committed=");
    firstCollectionSerialPutHex64(diagnostics.currentSegmentCommitted);
    firstCollectionSerialPutString(" reserved=");
    firstCollectionSerialPutHex64(diagnostics.currentSegmentReserved);
    firstCollectionSerialPutString(" segment=");
    firstCollectionSerialPutHex64(diagnostics.currentSegmentIdentity);
    firstCollectionSerialPutString(" refills=");
    firstCollectionSerialPutHex32(diagnostics.allocationContextRefillCount);
    firstCollectionSerialPutString(" sameSegmentCommits=");
    firstCollectionSerialPutHex32(diagnostics.heapCommitEventCount);
    firstCollectionSerialPutString(" segmentTransitions=");
    firstCollectionSerialPutHex32(diagnostics.segmentTransitionCount);
    firstCollectionSerialPutString(" sentinelChecks=");
    firstCollectionSerialPutHex32(diagnostics.sentinelValidationCount);
    firstCollectionSerialPutString(" sentinelFailures=");
    firstCollectionSerialPutHex32(diagnostics.sentinelValidationFailures);
    firstCollectionSerialPutString(" liveSentinels=");
    firstCollectionSerialPutHex32(diagnostics.liveSentinelCount);
    firstCollectionSerialPutString("\n");
}

extern "C" __declspec(noreturn) void __cdecl
guideXosNativeAotCollectionBoundarySafeStop(gx_uint32 suspendReason) {
    g_guideXosAllocationDiagnostics.firstCollectionBoundaryMarker =
        GUIDEXOS_NATIVEAOT_FIRST_COLLECTION_SAFE_STOP_MARKER;
    g_guideXosAllocationDiagnostics.firstCollectionRequestCount = 1u;
    g_guideXosAllocationDiagnostics.firstCollectionEntryCount = 1u;
    g_guideXosAllocationDiagnostics.collectionRequests = 1u;
    g_guideXosAllocationDiagnostics.collectionTriggeringEntries = 1u;
    g_guideXosAllocationDiagnostics.collectionsEntered = 1u;
    g_guideXosAllocationDiagnostics.collectionRequestCount = 1u;
    g_guideXosAllocationDiagnostics.collectionEntryCount = 1u;
    g_guideXosAllocationDiagnostics.collectionReason =
        GUIDEXOS_NATIVEAOT_COLLECTION_REASON_OUT_OF_SO_H;
    g_guideXosAllocationDiagnostics.requestedGeneration = 1u;
    g_guideXosAllocationDiagnostics.collectionBlockingMode =
        GUIDEXOS_NATIVEAOT_COLLECTION_BLOCKING;
    g_guideXosAllocationDiagnostics.collectionCompactingMode =
        GUIDEXOS_NATIVEAOT_COLLECTION_NONCOMPACTING_NOT_SELECTED;
    g_guideXosAllocationDiagnostics.gcLockTransitionCount = 1u;
    g_guideXosAllocationDiagnostics.suspensionRequestCount = 1u;
    g_guideXosAllocationDiagnostics.suspensionEntryCount = 0u;
    g_guideXosAllocationDiagnostics.restartResumeCount = 0u;
    g_guideXosAllocationDiagnostics.heapMutationStarted = 0u;
    g_guideXosAllocationDiagnostics.managedExecutionResumed = 0u;
    g_guideXosAllocationDiagnostics.firstUnsupportedContract =
        GUIDEXOS_NATIVEAOT_FIRST_COLLECTION_UNSUPPORTED_SUSPEND_EE;
    g_guideXosAllocationDiagnostics.safeStopObserved = 1u;
    g_guideXosAllocationDiagnostics.stopReason =
        GUIDEXOS_NATIVEAOT_FIRST_COLLECTION_SAFE_STOP_MARKER;
    g_guideXosAllocationDiagnostics.completionStatus = 5u;
    g_guideXosAllocationDiagnostics.collectionRequestAllocationOrdinal =
        g_guideXosAllocationDiagnostics.allocationRequestCount;
    g_guideXosAllocationDiagnostics.collectionEntryAllocationOrdinal =
        g_guideXosAllocationDiagnostics.allocationRequestCount;
    g_guideXosAllocationDiagnostics.collectionEntryThread =
        g_guideXosAllocationDiagnostics.runtimeThreadRecord;
    g_guideXosAllocationDiagnostics.collectionEntryAllocPtr =
        g_guideXosAllocationDiagnostics.currentAllocPtr;
    g_guideXosAllocationDiagnostics.collectionEntryAllocLimit =
        g_guideXosAllocationDiagnostics.currentAllocLimit;
    g_guideXosAllocationDiagnostics.collectionEntryObjectSize =
        g_guideXosAllocationDiagnostics.derivedObjectSize;
    g_guideXosAllocationDiagnostics.collectionEntrySegmentIdentity =
        g_guideXosAllocationDiagnostics.currentSegmentIdentity;
    g_guideXosAllocationDiagnostics.collectionEntrySegmentCommitted =
        g_guideXosAllocationDiagnostics.currentSegmentCommitted;
    g_guideXosAllocationDiagnostics.collectionEntrySegmentReserved =
        g_guideXosAllocationDiagnostics.currentSegmentReserved;
    g_guideXosAllocationDiagnostics.stage =
        GUIDEXOS_NATIVEAOT_ALLOC_STAGE_F20_FIRST_COLLECTION_SAFE_STOP;
    g_guideXosAllocationDiagnostics.sequence += 1u;
    g_guideXosAllocationDiagnostics.currentRip =
        reinterpret_cast<gx_uintptr>(_ReturnAddress());
    g_guideXosAllocationDiagnostics.currentRsp =
        reinterpret_cast<gx_uintptr>(_AddressOfReturnAddress());
    g_guideXosAllocationDiagnostics.waitReason = suspendReason;
    g_guideXosAllocationDiagnostics.failFastReason = 7u;
    emitFirstCollectionSafeStopMarker();
    for (;;) {
    }
}
#endif

#if defined(GUIDEXOS_NATIVEAOT_MANAGED_ALLOCATION)
#if !defined(GUIDEXOS_NATIVEAOT_USE_STOCK_RHP_NEW_ARRAY_ENTRY)
extern "C" __declspec(noinline) void* __cdecl RhpNewArray(void* eeType, gx_size length) {
#if defined(GUIDEXOS_NATIVEAOT_REAL_GC_ALLOCATION)
    ++g_guideXosObservedRhpNewArrayEntries;
    recordAllocationStage(GUIDEXOS_NATIVEAOT_ALLOC_STAGE_A02_RHP_NEW_ARRAY_ENTRY,
                          eeType, length);
    recordAllocationStage(GUIDEXOS_NATIVEAOT_ALLOC_STAGE_S00_ALLOCATION_REQUEST,
                          eeType, length);
#endif
    if (eeType == nullptr || length > 0x7FFFFFFFu) {
#if defined(GUIDEXOS_NATIVEAOT_REAL_GC_ALLOCATION)
        ++g_guideXosAllocationDiagnostics.pointerContractFailures;
        recordAllocationStage(GUIDEXOS_NATIVEAOT_ALLOC_STAGE_FAILFAST_ALLOCATION,
                              eeType, length);
#else
        g_guideXosAllocationDiagnostics.outOfMemory = 1u;
#endif
        guideXosFailFast(6u);
    }

    gx_size objectSize = 0u;
    if (!sourceDerivedArrayObjectSize(eeType, length, &objectSize)) {
#if defined(GUIDEXOS_NATIVEAOT_REAL_GC_ALLOCATION)
        ++g_guideXosAllocationDiagnostics.pointerContractFailures;
        recordAllocationStage(GUIDEXOS_NATIVEAOT_ALLOC_STAGE_FAILFAST_ALLOCATION,
                              eeType, length);
#else
        g_guideXosAllocationDiagnostics.outOfMemory = 1u;
#endif
        guideXosFailFast(6u);
    }

    g_guideXosAllocationDiagnostics.requestedArrayLength = static_cast<gx_uint32>(length);
    g_guideXosAllocationDiagnostics.requestedObjectSize = static_cast<gx_uint32>(objectSize);

#if defined(GUIDEXOS_NATIVEAOT_REAL_GC_ALLOCATION)
    recordAllocationStage(GUIDEXOS_NATIVEAOT_ALLOC_STAGE_A03_TYPE_LENGTH_ACCEPTED,
                          eeType, length, objectSize);
    recordAllocationStage(GUIDEXOS_NATIVEAOT_ALLOC_STAGE_A04_OBJECT_SIZE_COMPUTED,
                          eeType, length, objectSize);
    ++g_guideXosAllocationDiagnostics.rhpNewArrayEntries;
#if defined(GUIDEXOS_NATIVEAOT_FIRST_REFILL_ALLOCATION) || defined(GUIDEXOS_NATIVEAOT_SEGMENT_BOUNDARY_ALLOCATION)
    ++g_guideXosAllocationDiagnostics.allocationRequestCount;
    ++g_guideXosAllocationDiagnostics.rhpNewArrayCount;
#endif
    g_guideXosAllocationDiagnostics.eeType = reinterpret_cast<gx_uintptr>(eeType);

    gx_uintptr allocationPointerBefore = 0;
    gx_uintptr allocationLimitBefore = 0;
    gx_uintptr currentThread = 0;
    gx_uintptr gcHeap = 0;
    gx_uint32 gcCountBefore = 0;
    gx_uintptr gcBytesBefore = 0;
    gx_uint32 finalizableBefore = 0;
    gx_uint32 gcInProgressBefore = 0;
    gx_uint32 gcModeBefore = 0;
    gx_uintptr contextIdentityBefore = 0;
    gx_uintptr allocBytesBefore = 0;
    gx_uintptr allocBytesUohBefore = 0;
    unsigned char* tlsBlock = currentTlsBlock();
    unsigned char* tlsCell = runtimeCell(tlsBlock);
    const gx_uintptr transitionFrame = tlsCell != nullptr
        ? reinterpret_cast<gx_uintptr>(
            *reinterpret_cast<void**>(tlsCell + kRuntimeCellTransitionFrameOffset))
        : 0u;
    if (guidexos_nativeaot_gc_read_state(
            &allocationPointerBefore, &allocationLimitBefore, &currentThread,
            &gcHeap, &gcCountBefore, &gcBytesBefore, &finalizableBefore,
            &gcInProgressBefore, &gcModeBefore, &contextIdentityBefore,
            &allocBytesBefore, &allocBytesUohBefore) != 0 || currentThread == 0 || gcHeap == 0) {
        ++g_guideXosAllocationDiagnostics.pointerContractFailures;
        recordAllocationStage(GUIDEXOS_NATIVEAOT_ALLOC_STAGE_FAILFAST_GC_STATE,
                              eeType, length, objectSize, allocationPointerBefore,
                              allocationLimitBefore, currentThread);
        guideXosFailFast(8u);
    }
    recordAllocationStage(GUIDEXOS_NATIVEAOT_ALLOC_STAGE_A05_ALLOCATION_CONTEXT_LOADED,
                          eeType, length, objectSize, allocationPointerBefore,
                          allocationLimitBefore, currentThread, transitionFrame);
    g_guideXosAllocationDiagnostics.heapInitialized = 1u;
    g_guideXosAllocationDiagnostics.allocationContextBefore = allocationPointerBefore;
    g_guideXosAllocationDiagnostics.allocationLimitBefore = allocationLimitBefore;
    g_guideXosAllocationDiagnostics.gcCountBefore = gcCountBefore;
    g_guideXosAllocationDiagnostics.gcBytesBefore = gcBytesBefore;
    g_guideXosAllocationDiagnostics.finalizableObjectCountBefore = finalizableBefore;
    g_guideXosAllocationDiagnostics.gcInProgressBefore = gcInProgressBefore;
    g_guideXosAllocationDiagnostics.gcMode = gcModeBefore;
#if defined(GUIDEXOS_NATIVEAOT_FIRST_COLLECTION_BOUNDARY_ALLOCATION)
    g_guideXosAllocationDiagnostics.collectionEntryHeap = gcHeap;
#endif

    recordAllocationStage(GUIDEXOS_NATIVEAOT_ALLOC_STAGE_A06_FAST_PATH_ATTEMPTED,
                          eeType, length, objectSize, allocationPointerBefore,
                          allocationLimitBefore, currentThread);
    g_guideXosAllocationDiagnostics.lastDirectTarget =
        reinterpret_cast<gx_uintptr>(guideXosStockRhpNewArray);
    // GcAllocInternal calls IGCHeap::Alloc through the Workstation heap
    // vtable.  Capture the source-backed cell and target without invoking it
    // or taking any synchronization primitive; the call itself remains in
    // the matching GC implementation.
    const gx_uintptr gcHeapVtable =
        *reinterpret_cast<const gx_uintptr*>(gcHeap);
    const gx_uintptr gcAllocCell = gcHeapVtable + 0x1A8u;
    g_guideXosAllocationDiagnostics.lastIndirectCell = gcAllocCell;
    g_guideXosAllocationDiagnostics.lastIndirectTarget =
        *reinterpret_cast<const gx_uintptr*>(gcAllocCell);
#if defined(GUIDEXOS_NATIVEAOT_FIRST_REFILL_ALLOCATION) || defined(GUIDEXOS_NATIVEAOT_SEGMENT_BOUNDARY_ALLOCATION)
    const gx_uintptr remainingBefore = allocationLimitBefore >= allocationPointerBefore
        ? allocationLimitBefore - allocationPointerBefore : 0u;
    const bool contextIsValid = allocationPointerBefore != 0u &&
        allocationLimitBefore >= allocationPointerBefore;
    const bool fastPath = contextIsValid && objectSize <= remainingBefore;
    g_guideXosAllocationDiagnostics.currentIteration =
        g_guideXosAllocationDiagnostics.allocationRequestCount - 1u;
    g_guideXosAllocationDiagnostics.currentAllocPtr = allocationPointerBefore;
    g_guideXosAllocationDiagnostics.currentAllocLimit = allocationLimitBefore;
    g_guideXosAllocationDiagnostics.derivedObjectSize = objectSize;
    g_guideXosAllocationDiagnostics.sourceSizeValid = 1u;
    g_guideXosAllocationDiagnostics.primitiveArrayValid = 1u;
    g_guideXosAllocationDiagnostics.belowLargeObjectThreshold =
        objectSize < kNativeAotLargeObjectSize ? 1u : 0u;
    if (objectSize >= kNativeAotLargeObjectSize) {
        ++g_guideXosAllocationDiagnostics.largeObjectCount;
        markBoundedAllocationFailure(10u);
        recordAllocationStage(GUIDEXOS_NATIVEAOT_ALLOC_STAGE_FAILFAST_ALLOCATION,
                              eeType, length, objectSize, allocationPointerBefore,
                              allocationLimitBefore, currentThread);
        guideXosFailFast(6u);
    }
    if (fastPath) {
        ++g_guideXosAllocationDiagnostics.fastAllocationCount;
    } else {
        recordAllocationStage(GUIDEXOS_NATIVEAOT_ALLOC_STAGE_S01_FAST_CAPACITY_FAILURE,
                              eeType, length, objectSize, allocationPointerBefore,
                              allocationLimitBefore, currentThread);
        ++g_guideXosAllocationDiagnostics.rarePathCount;
        ++g_guideXosAllocationDiagnostics.slowAllocationCount;
        ++g_guideXosAllocationDiagnostics.collectionConsideredCount;
        recordAllocationStage(GUIDEXOS_NATIVEAOT_ALLOC_STAGE_S02_RARE_PATH_ENTERED,
                              eeType, length, objectSize, allocationPointerBefore,
                              allocationLimitBefore, currentThread);
        recordAllocationStage(GUIDEXOS_NATIVEAOT_ALLOC_STAGE_A07_RARE_REFILL_ENTERED,
                              eeType, length, objectSize, allocationPointerBefore,
                              allocationLimitBefore, currentThread);
#if defined(GUIDEXOS_NATIVEAOT_FIRST_REFILL_ALLOCATION)
        if (g_guideXosAllocationDiagnostics.allocationContextRefills >= 1u) {
            if (g_guideXosAllocationDiagnostics.refill2Attempted != 0u) {
                markBoundedAllocationFailure(11u);
                guideXosFailFast(6u);
            }
            g_guideXosAllocationDiagnostics.refill2Attempted = 1u;
            g_guideXosAllocationDiagnostics.refill2AllocPtrBefore = allocationPointerBefore;
            g_guideXosAllocationDiagnostics.refill2AllocLimitBefore = allocationLimitBefore;
            g_guideXosAllocationDiagnostics.refill2RemainingBytesBefore = remainingBefore;
        }
#endif
        recordAllocationStage(GUIDEXOS_NATIVEAOT_ALLOC_STAGE_A08_GC_HEAP_ALLOCATION_ENTERED,
                              eeType, length, objectSize, allocationPointerBefore,
                              allocationLimitBefore, currentThread);
        recordAllocationStage(GUIDEXOS_NATIVEAOT_ALLOC_STAGE_S03_GC_HEAP_ALLOC_ENTERED,
                              eeType, length, objectSize, allocationPointerBefore,
                              allocationLimitBefore, currentThread);
    }
#endif
    void* result = guideXosStockRhpNewArray(eeType, length);
    if (result == nullptr) {
        ++g_guideXosAllocationDiagnostics.pointerContractFailures;
        recordAllocationStage(GUIDEXOS_NATIVEAOT_ALLOC_STAGE_FAILFAST_ALLOCATION,
                              eeType, length, objectSize, allocationPointerBefore,
                              allocationLimitBefore, currentThread);
        guideXosFailFast(6u);
    }

    gx_uintptr allocationPointerAfter = 0;
    gx_uintptr allocationLimitAfter = 0;
    gx_uintptr currentThreadAfter = 0;
    gx_uintptr gcHeapAfter = 0;
    gx_uint32 gcCountAfter = 0;
    gx_uintptr gcBytesAfter = 0;
    gx_uint32 finalizableAfter = 0;
    gx_uint32 gcInProgressAfter = 0;
    gx_uint32 gcModeAfter = 0;
    gx_uintptr contextIdentityAfter = 0;
    gx_uintptr allocBytesAfter = 0;
    gx_uintptr allocBytesUohAfter = 0;
    if (guidexos_nativeaot_gc_read_state(
            &allocationPointerAfter, &allocationLimitAfter, &currentThreadAfter,
            &gcHeapAfter, &gcCountAfter, &gcBytesAfter, &finalizableAfter,
            &gcInProgressAfter, &gcModeAfter, &contextIdentityAfter,
            &allocBytesAfter, &allocBytesUohAfter) != 0 || currentThreadAfter != currentThread ||
        gcHeapAfter != gcHeap) {
        ++g_guideXosAllocationDiagnostics.pointerContractFailures;
        recordAllocationStage(GUIDEXOS_NATIVEAOT_ALLOC_STAGE_FAILFAST_GC_STATE,
                              eeType, length, objectSize, allocationPointerAfter,
                              allocationLimitAfter, currentThreadAfter);
        guideXosFailFast(8u);
    }

    g_guideXosAllocationDiagnostics.allocationCount += 1u;
#if defined(GUIDEXOS_NATIVEAOT_FIRST_REFILL_ALLOCATION) || defined(GUIDEXOS_NATIVEAOT_SEGMENT_BOUNDARY_ALLOCATION)
    if (!fastPath) {
        ++g_guideXosAllocationDiagnostics.realGcAllocationEntries;
        ++g_guideXosAllocationDiagnostics.realGcAllocationCount;
    }
#else
    g_guideXosAllocationDiagnostics.realGcAllocationEntries += 1u;
#endif
    if (allocationPointerBefore == 0u || allocationLimitBefore == 0u) {
        ++g_guideXosAllocationDiagnostics.slowAllocationEntries;
        ++g_guideXosAllocationDiagnostics.allocationContextRefills;
    }
#if defined(GUIDEXOS_NATIVEAOT_FIRST_REFILL_ALLOCATION) || defined(GUIDEXOS_NATIVEAOT_SEGMENT_BOUNDARY_ALLOCATION)
    if (!fastPath && allocationPointerBefore != 0u &&
        allocationLimitBefore >= allocationPointerBefore) {
        ++g_guideXosAllocationDiagnostics.allocationContextRefills;
    }
    g_guideXosAllocationDiagnostics.allocationContextRefillCount =
        g_guideXosAllocationDiagnostics.allocationContextRefills;
#endif
    g_guideXosAllocationDiagnostics.returnedObject = reinterpret_cast<gx_uintptr>(result);
    g_guideXosAllocationDiagnostics.objectAddress = reinterpret_cast<gx_uintptr>(result);
    g_guideXosAllocationDiagnostics.arrayData = reinterpret_cast<gx_uintptr>(result) + kManagedArrayDataOffset;
    g_guideXosAllocationDiagnostics.requestedArrayLength = static_cast<gx_uint32>(length);
    g_guideXosAllocationDiagnostics.requestedObjectSize = static_cast<gx_uint32>(objectSize);
    g_guideXosAllocationDiagnostics.allocationContextAfter = allocationPointerAfter;
    g_guideXosAllocationDiagnostics.allocationLimitAfter = allocationLimitAfter;
    g_guideXosAllocationDiagnostics.gcCountAfter = gcCountAfter;
    g_guideXosAllocationDiagnostics.gcBytesAfter = gcBytesAfter;
    g_guideXosAllocationDiagnostics.finalizableObjectCountAfter = finalizableAfter;
    g_guideXosAllocationDiagnostics.gcInProgressAfter = gcInProgressAfter;
    g_guideXosAllocationDiagnostics.collectionsEntered = gcCountAfter - gcCountBefore;
    g_guideXosAllocationDiagnostics.collectionTriggeringEntries =
        g_guideXosAllocationDiagnostics.collectionsEntered;

#if defined(GUIDEXOS_NATIVEAOT_FIRST_REFILL_ALLOCATION) || defined(GUIDEXOS_NATIVEAOT_SEGMENT_BOUNDARY_ALLOCATION)
    if (gcCountAfter != gcCountBefore || gcInProgressBefore != 0u || gcInProgressAfter != 0u) {
        ++g_guideXosAllocationDiagnostics.collectionRequestCount;
        ++g_guideXosAllocationDiagnostics.collectionEntryCount;
        ++g_guideXosAllocationDiagnostics.collectionBoundaryFailures;
    }
#endif

    gx_uint32 heapOwned = 0;
    gx_uintptr heapBase = 0;
    gx_uintptr heapAllocated = 0;
    gx_uintptr heapReserved = 0;
    if (guidexos_nativeaot_gc_describe_object(
            result, &heapBase, &heapAllocated, &heapReserved, &heapOwned) != 0) {
        ++g_guideXosAllocationDiagnostics.pointerContractFailures;
        guideXosFailFast(8u);
    }
    g_guideXosAllocationDiagnostics.heapBase = heapBase;
    g_guideXosAllocationDiagnostics.heapAllocated = heapAllocated;
    g_guideXosAllocationDiagnostics.heapReserved = heapReserved;
    g_guideXosAllocationDiagnostics.heapOwnershipVerified = heapOwned;
#if defined(GUIDEXOS_NATIVEAOT_FIRST_REFILL_ALLOCATION) || defined(GUIDEXOS_NATIVEAOT_SEGMENT_BOUNDARY_ALLOCATION)
    const gx_uintptr objectAddress = reinterpret_cast<gx_uintptr>(result);
    const gx_uintptr objectEnd = objectAddress + objectSize;
    const gx_uintptr previousEnd = g_guideXosAllocationDiagnostics.currentObjectEnd;
#if defined(GUIDEXOS_NATIVEAOT_SEGMENT_BOUNDARY_ALLOCATION)
    gx_uintptr segmentIdentity = 0;
    gx_uintptr segmentBase = 0;
    gx_uintptr segmentAllocated = 0;
    gx_uintptr segmentCommitted = 0;
    gx_uintptr segmentReserved = 0;
    gx_uint32 segmentFlags = 0;
    gx_uint32 segmentGeneration = 0;
    if (g_guideXosSegmentDescribe == nullptr || g_guideXosSegmentDescribe(
            result, &segmentIdentity, &segmentBase, &segmentAllocated,
            &segmentCommitted, &segmentReserved, &segmentFlags,
            &segmentGeneration) != 0 || segmentIdentity == 0u ||
        segmentBase == 0u || segmentReserved < segmentBase ||
        segmentCommitted < segmentBase || segmentCommitted > segmentReserved) {
        markBoundedAllocationFailure(12u);
        guideXosFailFast(8u);
    }
    const gx_uintptr previousSegmentIdentity =
        g_guideXosAllocationDiagnostics.currentSegmentIdentity;
    const bool segmentChanged = previousSegmentIdentity != 0u &&
        previousSegmentIdentity != segmentIdentity;
    g_guideXosAllocationDiagnostics.currentSegmentIdentity = segmentIdentity;
    g_guideXosAllocationDiagnostics.currentSegmentCommitted = segmentCommitted;
    g_guideXosAllocationDiagnostics.lastSegmentGeneration = segmentGeneration;
    g_guideXosAllocationDiagnostics.lastSegmentFlags = segmentFlags;
    g_guideXosAllocationDiagnostics.currentSegmentBase = segmentBase;
    g_guideXosAllocationDiagnostics.currentSegmentAllocated = segmentAllocated;
    g_guideXosAllocationDiagnostics.currentSegmentReserved = segmentReserved;
    g_guideXosAllocationDiagnostics.currentSegmentFlags = segmentFlags;
    g_guideXosAllocationDiagnostics.currentSegmentGeneration = segmentGeneration;
    recordAllocationStage(GUIDEXOS_NATIVEAOT_ALLOC_STAGE_S04_CURRENT_SEGMENT_INSPECTED,
                          eeType, length, objectSize, allocationPointerBefore,
                          allocationLimitBefore, currentThread);
    g_guideXosAllocationDiagnostics.segmentTransitionCount +=
        segmentChanged ? 1u : 0u;
#else
    const gx_uintptr segmentIdentity = 0u;
    const gx_uintptr segmentBase = 0u;
    const gx_uintptr segmentAllocated = 0u;
    const gx_uintptr segmentCommitted = 0u;
    const gx_uintptr segmentReserved = 0u;
    const gx_uint32 segmentGeneration = 0u;
    const bool segmentChanged = false;
#endif
    g_guideXosAllocationDiagnostics.currentObject = objectAddress;
    g_guideXosAllocationDiagnostics.currentObjectEnd = objectEnd;
    g_guideXosAllocationDiagnostics.currentAllocPtr = allocationPointerAfter;
    g_guideXosAllocationDiagnostics.currentAllocLimit = allocationLimitAfter;
    if (g_guideXosAllocationDiagnostics.allocationCount > 1u &&
        objectAddress < previousEnd) {
        ++g_guideXosAllocationDiagnostics.overlapFailures;
    }
    if (g_guideXosAllocationDiagnostics.allocationCount > 1u &&
        allocationPointerBefore != 0u &&
        allocationLimitBefore >= allocationPointerBefore &&
        objectAddress < allocationPointerBefore) {
        ++g_guideXosAllocationDiagnostics.monotonicityFailures;
    }
    if (g_guideXosAllocationDiagnostics.allocationCount == 1u) {
        g_guideXosAllocationDiagnostics.initialAllocPtr = allocationPointerAfter;
        g_guideXosAllocationDiagnostics.initialAllocLimit = allocationLimitAfter;
        g_guideXosAllocationDiagnostics.initialAvailableBytes =
            allocationLimitAfter >= allocationPointerAfter
                ? allocationLimitAfter - allocationPointerAfter : 0u;
        g_guideXosAllocationDiagnostics.expectedFastAllocationCount =
            objectSize == 0u ? 0u :
                g_guideXosAllocationDiagnostics.initialAvailableBytes / objectSize;
        g_guideXosAllocationDiagnostics.hardAllocationLimit =
#if defined(GUIDEXOS_NATIVEAOT_SEGMENT_TRANSITION_ALLOCATION)
            kSegmentTransitionHardLimit;
#elif defined(GUIDEXOS_NATIVEAOT_SEGMENT_BOUNDARY_ALLOCATION)
            kSegmentBoundaryHardLimit;
#else
            static_cast<gx_uint32>(g_guideXosAllocationDiagnostics.expectedFastAllocationCount + 2u);
#endif
        g_guideXosAllocationDiagnostics.initialSegmentBase = heapBase;
        g_guideXosAllocationDiagnostics.initialSegmentAllocated = heapAllocated;
        g_guideXosAllocationDiagnostics.initialSegmentReserved = heapReserved;
        g_guideXosAllocationDiagnostics.newContextSupplied = 1u;
#if defined(GUIDEXOS_NATIVEAOT_SEGMENT_BOUNDARY_ALLOCATION)
#if defined(GUIDEXOS_NATIVEAOT_SEGMENT_TRANSITION_ALLOCATION)
        g_guideXosAllocationDiagnostics.experimentMode = 3u;
        g_guideXosAllocationDiagnostics.selectedArrayLength = kSegmentTransitionArrayLength;
        g_guideXosAllocationDiagnostics.hardRefillLimit = kSegmentTransitionHardRefillLimit;
        g_guideXosAllocationDiagnostics.hardCommitLimit = kSegmentTransitionHardCommitLimit;
        g_guideXosAllocationDiagnostics.hardSegmentTransitionLimit = 1u;
#else
        g_guideXosAllocationDiagnostics.experimentMode = 2u;
        g_guideXosAllocationDiagnostics.selectedArrayLength = kSegmentBoundaryArrayLength;
#endif
        g_guideXosAllocationDiagnostics.initialSegmentIdentity = segmentIdentity;
        g_guideXosAllocationDiagnostics.initialSegmentCommitted = segmentCommitted;
        g_guideXosAllocationDiagnostics.initialSegmentGeneration = segmentGeneration;
#endif
    }
    if (fastPath) {
        if (objectAddress != allocationPointerBefore ||
            allocationPointerAfter != allocationPointerBefore + objectSize ||
            allocationLimitAfter != allocationLimitBefore) {
            ++g_guideXosAllocationDiagnostics.contextGeometryFailures;
        }
        g_guideXosAllocationDiagnostics.lastFastObject = objectAddress;
        g_guideXosAllocationDiagnostics.lastFastObjectEnd = objectEnd;
    } else {
        const bool directBeforeContext = objectEnd == allocationPointerAfter;
        const bool firstInsideContext = objectAddress >= allocationPointerBefore &&
            objectEnd <= allocationLimitAfter;
        if (directBeforeContext) {
            g_guideXosAllocationDiagnostics.ownershipModel = 1u;
        } else if (firstInsideContext) {
            g_guideXosAllocationDiagnostics.ownershipModel = 2u;
        } else {
            ++g_guideXosAllocationDiagnostics.contextGeometryFailures;
        }
        if (g_guideXosAllocationDiagnostics.refill2Attempted != 0u &&
            g_guideXosAllocationDiagnostics.refill2Returned == 0u) {
            g_guideXosAllocationDiagnostics.refill2Returned = 1u;
            g_guideXosAllocationDiagnostics.refill2Object = objectAddress;
            g_guideXosAllocationDiagnostics.refill2ObjectEnd = objectEnd;
            g_guideXosAllocationDiagnostics.refill2AllocPtrAfter = allocationPointerAfter;
            g_guideXosAllocationDiagnostics.refill2AllocLimitAfter = allocationLimitAfter;
            g_guideXosAllocationDiagnostics.refill2ContextPublished = 1u;
            g_guideXosAllocationDiagnostics.refill2ContextChanged =
                allocationPointerAfter != allocationPointerBefore ||
                allocationLimitAfter != allocationLimitBefore ? 1u : 0u;
            g_guideXosAllocationDiagnostics.refill2SegmentBase = heapBase;
            g_guideXosAllocationDiagnostics.refill2SegmentAllocated = heapAllocated;
            g_guideXosAllocationDiagnostics.refill2SegmentReserved = heapReserved;
        }
    }
#if defined(GUIDEXOS_NATIVEAOT_SEGMENT_BOUNDARY_ALLOCATION)
    inspectSegmentBoundaryTrace(
        segmentBase, segmentReserved, segmentCommitted,
        g_guideXosAllocationDiagnostics.allocationCount,
        objectAddress, objectEnd, allocationPointerBefore,
        allocationLimitBefore, allocationPointerAfter, allocationLimitAfter,
        gcCountBefore, gcCountAfter, segmentChanged);
    recordSegmentBoundaryRefill(
        fastPath, g_guideXosAllocationDiagnostics.allocationCount,
        allocationPointerBefore, allocationLimitBefore, objectAddress,
        objectEnd, allocationPointerAfter, allocationLimitAfter,
        segmentIdentity, segmentBase, segmentAllocated, segmentCommitted,
        segmentReserved, gcCountBefore, gcCountAfter, segmentChanged);
#endif
#endif
    recordAllocationStage(GUIDEXOS_NATIVEAOT_ALLOC_STAGE_A16_ALLOCATION_RETURNED,
                          eeType, length, objectSize, allocationPointerAfter,
                          allocationLimitAfter, currentThreadAfter, transitionFrame);
#if defined(GUIDEXOS_NATIVEAOT_SEGMENT_TRANSITION_ALLOCATION)
    if (g_guideXosAllocationDiagnostics.allocationCount >=
        g_guideXosAllocationDiagnostics.hardAllocationLimit) {
        recordAllocationStage(GUIDEXOS_NATIVEAOT_ALLOC_STAGE_S14_STOP_OBJECT_RETURNED,
                              eeType, length, objectSize, allocationPointerAfter,
                              allocationLimitAfter, currentThreadAfter, transitionFrame);
    }
#endif
    return result;
#else
    unsigned char* block = currentTlsBlock();
    unsigned char* cell = runtimeCell(block);
    if (cell == nullptr) {
        g_guideXosAllocationDiagnostics.outOfMemory = 1u;
        guideXosFailFast(6u);
    }
    const gx_uintptr allocationPointer = reinterpret_cast<gx_uintptr>(*reinterpret_cast<void**>(cell));
    const gx_uintptr allocationLimit = reinterpret_cast<gx_uintptr>(*reinterpret_cast<void**>(cell + sizeof(void*)));
    if (allocationPointer == 0u || allocationLimit < allocationPointer || objectSize > allocationLimit - allocationPointer) {
        markAllocationOutOfMemory(cell, allocationPointer);
        guideXosFailFast(6u);
    }

    g_guideXosAllocationDiagnostics.previousObject = g_guideXosAllocationDiagnostics.lastObject;
    void* result = guideXosStockRhpNewArray(eeType, length);
    const gx_uintptr allocationPointerAfter = reinterpret_cast<gx_uintptr>(*reinterpret_cast<void**>(cell));
    if (result == nullptr || reinterpret_cast<gx_uintptr>(result) != allocationPointer ||
        allocationPointerAfter != allocationPointer + objectSize ||
        allocationPointerAfter > allocationLimit) {
        g_guideXosAllocationDiagnostics.pointerContractFailures += 1u;
        guideXosFailFast(8u);
    }
    g_guideXosAllocationDiagnostics.allocationCount += 1u;
    g_guideXosAllocationDiagnostics.returnedObject = reinterpret_cast<gx_uintptr>(result);
    g_guideXosAllocationDiagnostics.arrayData = reinterpret_cast<gx_uintptr>(result) + kManagedArrayDataOffset;
    g_guideXosAllocationDiagnostics.lastObject = reinterpret_cast<gx_uintptr>(result);
    g_guideXosAllocationDiagnostics.lastObjectSize = objectSize;
    g_guideXosAllocationDiagnostics.allocationPointerAfter = allocationPointerAfter;
    return result;
#endif
}
#endif

#if defined(GUIDEXOS_NATIVEAOT_REAL_GC_ALLOCATION)
extern "C" __declspec(dllexport) const guidexos_nativeaot_allocation_diagnostics*
__cdecl guideXosManagedAllocationGetDiagnostics() {
#if defined(GUIDEXOS_NATIVEAOT_REAL_GC_ALLOCATION)
    if (g_guideXosAllocationDiagnostics.rhpNewArrayEntries <
        g_guideXosObservedRhpNewArrayEntries) {
        g_guideXosAllocationDiagnostics.rhpNewArrayEntries =
            g_guideXosObservedRhpNewArrayEntries;
    }
#endif
    return &g_guideXosAllocationDiagnostics;
}

extern "C" __declspec(dllexport) int __cdecl
guideXosManagedAllocationFinalize(gx_uint32 managedReturnCode) {
    g_guideXosAllocationDiagnostics.managedReturnCode = managedReturnCode;
#if defined(GUIDEXOS_NATIVEAOT_SEGMENT_TRANSITION_ALLOCATION)
    g_guideXosAllocationDiagnostics.collectionDecisionObserved = 1u;
    g_guideXosAllocationDiagnostics.collectionDecisionPath = 2u;
    g_guideXosAllocationDiagnostics.completionStatus = 4u;
    const bool sameSegment =
        g_guideXosAllocationDiagnostics.initialSegmentIdentity != 0u &&
        g_guideXosAllocationDiagnostics.currentSegmentIdentity ==
            g_guideXosAllocationDiagnostics.initialSegmentIdentity;
    const bool geometry =
        g_guideXosAllocationDiagnostics.initialSegmentReserved >
            g_guideXosAllocationDiagnostics.initialSegmentBase &&
        g_guideXosAllocationDiagnostics.currentSegmentCommitted >=
            g_guideXosAllocationDiagnostics.currentSegmentBase &&
        g_guideXosAllocationDiagnostics.currentSegmentCommitted <=
            g_guideXosAllocationDiagnostics.currentSegmentReserved;
    const bool pass = managedReturnCode == 0u &&
        g_guideXosAllocationDiagnostics.managedEntryCount == 1u &&
        g_guideXosAllocationDiagnostics.allocationCount ==
            g_guideXosAllocationDiagnostics.hardAllocationLimit &&
        g_guideXosAllocationDiagnostics.allocationRequestCount ==
            g_guideXosAllocationDiagnostics.allocationCount &&
        g_guideXosAllocationDiagnostics.rhpNewArrayCount ==
            g_guideXosAllocationDiagnostics.allocationCount &&
        g_guideXosAllocationDiagnostics.refillHistoryCount <=
            g_guideXosAllocationDiagnostics.hardRefillLimit &&
        g_guideXosAllocationDiagnostics.vmCommitEventCount <=
            g_guideXosAllocationDiagnostics.hardCommitLimit &&
        g_guideXosAllocationDiagnostics.hardSegmentTransitionLimit == 1u &&
        g_guideXosAllocationDiagnostics.segmentTransitionCount == 0u &&
        g_guideXosAllocationDiagnostics.managedStopObserved == 1u &&
        g_guideXosAllocationDiagnostics.noPostRefillAllocation == 1u &&
        sameSegment && geometry &&
        g_guideXosAllocationDiagnostics.collectionRequestCount == 0u &&
        g_guideXosAllocationDiagnostics.collectionEntryCount == 0u &&
        g_guideXosAllocationDiagnostics.collectionsEntered == 0u &&
        g_guideXosAllocationDiagnostics.gcCountBefore ==
            g_guideXosAllocationDiagnostics.gcCountAfter &&
        g_guideXosAllocationDiagnostics.finalizationScanCount == 0u &&
        g_guideXosAllocationDiagnostics.managedFinalizerCount == 0u &&
        g_guideXosAllocationDiagnostics.finalizersExecuted == 0u &&
        g_guideXosAllocationDiagnostics.zeroValidationFailures == 0u &&
        g_guideXosAllocationDiagnostics.patternValidationFailures == 0u &&
        g_guideXosAllocationDiagnostics.layoutFailures == 0u &&
        g_guideXosAllocationDiagnostics.ownershipFailures == 0u &&
        g_guideXosAllocationDiagnostics.overlapFailures == 0u &&
        g_guideXosAllocationDiagnostics.monotonicityFailures == 0u &&
        g_guideXosAllocationDiagnostics.contextGeometryFailures == 0u &&
        g_guideXosAllocationDiagnostics.sourceSizeValid == 1u &&
        g_guideXosAllocationDiagnostics.primitiveArrayValid == 1u &&
        g_guideXosAllocationDiagnostics.belowLargeObjectThreshold == 1u &&
        g_guideXosAllocationDiagnostics.refillHistoryOverflow == 0u &&
        g_guideXosAllocationDiagnostics.pointerContractFailures == 0u;
    g_guideXosAllocationDiagnostics.finalizerStateValid =
        g_guideXosAllocationDiagnostics.finalizersExecuted == 0u ? 1u : 0u;
    g_guideXosAllocationDiagnostics.helperStateValid = 1u;
    g_guideXosAllocationDiagnostics.allocationSucceeded = pass ? 1u : 0u;
    return pass ? 0 : -1;
#elif defined(GUIDEXOS_NATIVEAOT_SEGMENT_BOUNDARY_ALLOCATION)
    const bool boundaryReached =
        g_guideXosAllocationDiagnostics.boundaryStopObserved != 0u &&
        (g_guideXosAllocationDiagnostics.boundaryType == 1u ||
         g_guideXosAllocationDiagnostics.boundaryType == 2u);
    g_guideXosAllocationDiagnostics.completionStatus = boundaryReached ?
        g_guideXosAllocationDiagnostics.boundaryType : 3u;
    const bool pass = managedReturnCode == 0u &&
        g_guideXosAllocationDiagnostics.managedEntryCount == 1u &&
        g_guideXosAllocationDiagnostics.allocationRequestCount ==
            g_guideXosAllocationDiagnostics.allocationCount &&
        g_guideXosAllocationDiagnostics.rhpNewArrayCount ==
            g_guideXosAllocationDiagnostics.allocationCount &&
        g_guideXosAllocationDiagnostics.allocationCount >= 2u &&
        g_guideXosAllocationDiagnostics.realGcAllocationCount ==
            g_guideXosAllocationDiagnostics.rarePathCount &&
        g_guideXosAllocationDiagnostics.allocationContextRefillCount ==
            g_guideXosAllocationDiagnostics.rarePathCount &&
        g_guideXosAllocationDiagnostics.collectionRequestCount == 0u &&
        g_guideXosAllocationDiagnostics.collectionEntryCount == 0u &&
        g_guideXosAllocationDiagnostics.collectionsEntered == 0u &&
        g_guideXosAllocationDiagnostics.gcCountBefore ==
            g_guideXosAllocationDiagnostics.gcCountAfter &&
        g_guideXosAllocationDiagnostics.finalizationScanCount == 0u &&
        g_guideXosAllocationDiagnostics.managedFinalizerCount == 0u &&
        g_guideXosAllocationDiagnostics.finalizersExecuted == 0u &&
        g_guideXosAllocationDiagnostics.zeroValidationFailures == 0u &&
        g_guideXosAllocationDiagnostics.patternValidationFailures == 0u &&
        g_guideXosAllocationDiagnostics.layoutFailures == 0u &&
        g_guideXosAllocationDiagnostics.ownershipFailures == 0u &&
        g_guideXosAllocationDiagnostics.overlapFailures == 0u &&
        g_guideXosAllocationDiagnostics.monotonicityFailures == 0u &&
        g_guideXosAllocationDiagnostics.contextGeometryFailures == 0u &&
        g_guideXosAllocationDiagnostics.sourceSizeValid == 1u &&
        g_guideXosAllocationDiagnostics.primitiveArrayValid == 1u &&
        g_guideXosAllocationDiagnostics.belowLargeObjectThreshold == 1u &&
        g_guideXosAllocationDiagnostics.refillHistoryOverflow == 0u &&
        g_guideXosAllocationDiagnostics.initialHeapCommitObserved == 1u &&
        g_guideXosAllocationDiagnostics.initialHeapCommitEventCount >= 1u &&
        g_guideXosAllocationDiagnostics.rarePathCount >= 2u &&
        g_guideXosAllocationDiagnostics.refillHistoryCount >= 2u &&
        g_guideXosAllocationDiagnostics.boundaryAllocationOrdinal >= 2u &&
        ((g_guideXosAllocationDiagnostics.boundaryType == 1u &&
          g_guideXosAllocationDiagnostics.boundaryCommitValidated != 0u) ||
         (g_guideXosAllocationDiagnostics.boundaryType == 2u &&
          g_guideXosAllocationDiagnostics.boundarySegmentValidated != 0u)) &&
        g_guideXosAllocationDiagnostics.pointerContractFailures == 0u &&
        boundaryReached;
    g_guideXosAllocationDiagnostics.finalizerStateValid =
        g_guideXosAllocationDiagnostics.finalizersExecuted == 0u ? 1u : 0u;
    g_guideXosAllocationDiagnostics.helperStateValid = 1u;
    g_guideXosAllocationDiagnostics.allocationSucceeded = pass ? 1u : 0u;
    return pass ? 0 : -1;
#elif defined(GUIDEXOS_NATIVEAOT_FIRST_REFILL_ALLOCATION)
    const bool pass = managedReturnCode == 0u &&
        g_guideXosAllocationDiagnostics.managedEntryCount == 1u &&
        g_guideXosAllocationDiagnostics.allocationRequestCount ==
            g_guideXosAllocationDiagnostics.allocationCount &&
        g_guideXosAllocationDiagnostics.rhpNewArrayCount ==
            g_guideXosAllocationDiagnostics.allocationCount &&
        g_guideXosAllocationDiagnostics.allocationContextRefillCount == 2u &&
        g_guideXosAllocationDiagnostics.fastAllocationCount ==
            g_guideXosAllocationDiagnostics.expectedFastAllocationCount &&
        g_guideXosAllocationDiagnostics.rarePathCount == 2u &&
        g_guideXosAllocationDiagnostics.realGcAllocationCount == 2u &&
        g_guideXosAllocationDiagnostics.slowAllocationCount == 2u &&
        g_guideXosAllocationDiagnostics.refill2Attempted == 1u &&
        g_guideXosAllocationDiagnostics.refill2Returned == 1u &&
        g_guideXosAllocationDiagnostics.newContextSupplied == 1u &&
        g_guideXosAllocationDiagnostics.refill2ContextPublished == 1u &&
        g_guideXosAllocationDiagnostics.collectionConsideredCount == 2u &&
        g_guideXosAllocationDiagnostics.collectionRequestCount == 0u &&
        g_guideXosAllocationDiagnostics.collectionEntryCount == 0u &&
        g_guideXosAllocationDiagnostics.collectionsEntered == 0u &&
        g_guideXosAllocationDiagnostics.finalizationScanCount == 0u &&
        g_guideXosAllocationDiagnostics.managedFinalizerCount == 0u &&
        g_guideXosAllocationDiagnostics.finalizersExecuted == 0u &&
        g_guideXosAllocationDiagnostics.zeroValidationFailures == 0u &&
        g_guideXosAllocationDiagnostics.patternValidationFailures == 0u &&
        g_guideXosAllocationDiagnostics.layoutFailures == 0u &&
        g_guideXosAllocationDiagnostics.ownershipFailures == 0u &&
        g_guideXosAllocationDiagnostics.overlapFailures == 0u &&
        g_guideXosAllocationDiagnostics.monotonicityFailures == 0u &&
        g_guideXosAllocationDiagnostics.contextGeometryFailures == 0u &&
        g_guideXosAllocationDiagnostics.belowLargeObjectThreshold == 1u &&
        g_guideXosAllocationDiagnostics.noPostRefillAllocation == 1u &&
        g_guideXosAllocationDiagnostics.pointerContractFailures == 0u;
    g_guideXosAllocationDiagnostics.finalizerStateValid =
        g_guideXosAllocationDiagnostics.finalizersExecuted == 0u ? 1u : 0u;
    g_guideXosAllocationDiagnostics.helperStateValid = 1u;
    g_guideXosAllocationDiagnostics.allocationSucceeded = pass ? 1u : 0u;
    return pass ? 0 : -1;
#else
    g_guideXosAllocationDiagnostics.zeroByteCount = (managedReturnCode >> 8u) & 0xFFu;
    g_guideXosAllocationDiagnostics.patternVerified = (managedReturnCode & 1u) != 0u ? 1u : 0u;

    const gx_uintptr objectAddress = g_guideXosAllocationDiagnostics.objectAddress;
    const gx_uintptr objectSize = g_guideXosAllocationDiagnostics.requestedObjectSize;
    const gx_uintptr heapBase = g_guideXosAllocationDiagnostics.heapBase;
    const gx_uintptr heapReserved = g_guideXosAllocationDiagnostics.heapReserved;
    const gx_uintptr allocationPointerAfter =
        g_guideXosAllocationDiagnostics.allocationContextAfter;
    g_guideXosAllocationDiagnostics.objectAlignmentVerified =
        objectAddress != 0u && (objectAddress & 7u) == 0u ? 1u : 0u;
    g_guideXosAllocationDiagnostics.arrayLengthObserved = objectAddress == 0u
        ? 0u
        : *reinterpret_cast<const gx_uint32*>(objectAddress + 8u);
    g_guideXosAllocationDiagnostics.objectLayoutVerified =
        g_guideXosAllocationDiagnostics.arrayLengthObserved == 24u &&
        objectAddress != 0u &&
        *reinterpret_cast<const gx_uintptr*>(objectAddress) ==
            g_guideXosAllocationDiagnostics.eeType ? 1u : 0u;
    gx_size sourceObjectSize = 0u;
    const bool sourceObjectSizeValid = sourceDerivedArrayObjectSize(
        reinterpret_cast<void*>(g_guideXosAllocationDiagnostics.eeType),
        g_guideXosAllocationDiagnostics.arrayLengthObserved,
        &sourceObjectSize);
    g_guideXosAllocationDiagnostics.objectRangeVerified =
        objectAddress != 0u && sourceObjectSizeValid && objectSize == sourceObjectSize &&
        objectAddress >= heapBase &&
        heapReserved >= objectAddress &&
        objectSize <= heapReserved - objectAddress &&
        allocationPointerAfter == objectAddress + objectSize ? 1u : 0u;

    const bool pass = g_guideXosAllocationDiagnostics.allocationCount == 1u &&
        g_guideXosAllocationDiagnostics.rhpNewArrayEntries == 1u &&
        g_guideXosAllocationDiagnostics.realGcAllocationEntries == 1u &&
        g_guideXosAllocationDiagnostics.heapOwnershipVerified != 0u &&
        g_guideXosAllocationDiagnostics.objectAlignmentVerified != 0u &&
        g_guideXosAllocationDiagnostics.objectLayoutVerified != 0u &&
        g_guideXosAllocationDiagnostics.objectRangeVerified != 0u &&
        g_guideXosAllocationDiagnostics.zeroByteCount == 24u &&
        g_guideXosAllocationDiagnostics.patternVerified != 0u &&
        g_guideXosAllocationDiagnostics.collectionsEntered == 0u &&
        g_guideXosAllocationDiagnostics.collectionTriggeringEntries == 0u &&
        g_guideXosAllocationDiagnostics.finalizersExecuted == 0u &&
        g_guideXosAllocationDiagnostics.pointerContractFailures == 0u;
    g_guideXosAllocationDiagnostics.allocationSucceeded = pass ? 1u : 0u;
    return pass ? 0 : -1;
#endif
}
#endif

#if defined(GUIDEXOS_NATIVEAOT_FIRST_REFILL_ALLOCATION) || defined(GUIDEXOS_NATIVEAOT_SEGMENT_BOUNDARY_ALLOCATION)
extern "C" __declspec(noinline) __declspec(dllexport) int __cdecl
guideXosManagedAllocationValidateObject(
    void* arrayObject, gx_size length, gx_uint32 sequence,
    gx_uint32 zeroByteCount, gx_uint32 patternValid) {
    const gx_uintptr objectAddress = reinterpret_cast<gx_uintptr>(arrayObject);
#if defined(GUIDEXOS_NATIVEAOT_C011EC18_NATIVE_RHP_NEW_ARRAY)
    // C011EC18 deliberately enters the locked stock RhpNewArray assembly
    // directly.  That path must not be wrapped just to populate this
    // proof-only observer's bookkeeping, so derive the observer fields from
    // the real managed object and the locked GC probe instead.
    if (arrayObject != nullptr) {
        guidexos_nativeaot_allocation_diagnostics& diagnostics =
            g_guideXosAllocationDiagnostics;
        const gx_uintptr eeType =
            *reinterpret_cast<const gx_uintptr*>(arrayObject);
        gx_size observedObjectSize = 0u;
        if (sourceDerivedArrayObjectSize(
                reinterpret_cast<void*>(eeType), length,
                &observedObjectSize)) {
            diagnostics.eeType = eeType;
            diagnostics.requestedArrayLength = static_cast<gx_uint32>(length);
            diagnostics.requestedObjectSize =
                static_cast<gx_uint32>(observedObjectSize);
            diagnostics.derivedObjectSize = observedObjectSize;
            diagnostics.currentObject = objectAddress;
            diagnostics.currentObjectEnd = objectAddress + observedObjectSize;
            diagnostics.objectAddress = objectAddress;
            diagnostics.returnedObject = objectAddress;
            diagnostics.arrayData = objectAddress + kManagedArrayDataOffset;
            ++diagnostics.allocationCount;
            ++diagnostics.rhpNewArrayEntries;
            ++diagnostics.rhpNewArrayCount;
            ++diagnostics.allocationRequestCount;
            ++diagnostics.realGcAllocationEntries;
            ++diagnostics.realGcAllocationCount;

            gx_uintptr allocationContext = 0u;
            gx_uintptr allocationLimit = 0u;
            gx_uintptr currentThread = 0u;
            gx_uintptr gcHeap = 0u;
            gx_uint32 gcCount = 0u;
            gx_uintptr allocatedBytes = 0u;
            gx_uint32 finalizableObjects = 0u;
            gx_uint32 gcInProgress = 0u;
            gx_uint32 gcMode = 0u;
            gx_uintptr contextIdentity = 0u;
            gx_uintptr allocBytes = 0u;
            gx_uintptr allocBytesUoh = 0u;
            if (guidexos_nativeaot_gc_read_state(
                    &allocationContext, &allocationLimit, &currentThread,
                    &gcHeap, &gcCount, &allocatedBytes, &finalizableObjects,
                    &gcInProgress, &gcMode, &contextIdentity, &allocBytes,
                    &allocBytesUoh) == 0) {
                diagnostics.allocationContextAfter = allocationContext;
                diagnostics.allocationLimitAfter = allocationLimit;
                diagnostics.runtimeThreadRecord = currentThread;
                diagnostics.gcCountAfter = gcCount;
                diagnostics.gcBytesAfter = allocatedBytes;
                diagnostics.finalizableObjectCountAfter = finalizableObjects;
                diagnostics.gcInProgressAfter = gcInProgress;
                diagnostics.gcMode = gcMode;
            }
            gx_uintptr heapBase = 0u;
            gx_uintptr heapAllocated = 0u;
            gx_uintptr heapReserved = 0u;
            gx_uint32 heapOwned = 0u;
            if (guidexos_nativeaot_gc_describe_object(
                    arrayObject, &heapBase, &heapAllocated, &heapReserved,
                    &heapOwned) == 0) {
                diagnostics.heapBase = heapBase;
                diagnostics.heapAllocated = heapAllocated;
                diagnostics.heapReserved = heapReserved;
                diagnostics.heapOwnershipVerified = heapOwned;
            }
        }
    }
#endif
    const gx_size objectSize = g_guideXosAllocationDiagnostics.derivedObjectSize;
    const gx_uintptr objectEnd = objectAddress + objectSize;
#if defined(GUIDEXOS_NATIVEAOT_ALLOCATION_CONTEXT_FIXUP_ROOT_BOUNDARY_ALLOCATION)
    if (g_guideXosAllocationDiagnostics.objectHistoryCount <
        GUIDEXOS_NATIVEAOT_MAX_OBJECT_HISTORY) {
        guidexos_nativeaot_object_history_entry& history =
            g_guideXosAllocationDiagnostics.objectHistory[
                g_guideXosAllocationDiagnostics.objectHistoryCount++];
        history.address = objectAddress;
        history.end = objectEnd;
        history.eeType = g_guideXosAllocationDiagnostics.eeType;
        history.length = static_cast<gx_uint32>(length);
        history.sequence = sequence;
        history.zeroByteCount = zeroByteCount;
        history.patternValid = patternValid;
        history.sentinel = sequence < 4u ? 1u : 0u;
    } else {
        g_guideXosAllocationDiagnostics.objectHistoryOverflow = 1u;
    }
#endif
    gx_uint32 failure = 0u;
    g_guideXosAllocationDiagnostics.currentIteration = sequence;
    g_guideXosAllocationDiagnostics.zeroByteCount = zeroByteCount;
    g_guideXosAllocationDiagnostics.patternVerified = patternValid;
    if (zeroByteCount != length) {
        ++g_guideXosAllocationDiagnostics.zeroValidationFailures;
        failure = 1u;
    }
    if (patternValid == 0u) {
        ++g_guideXosAllocationDiagnostics.patternValidationFailures;
        failure = 1u;
    }
    if (arrayObject == nullptr || objectAddress != g_guideXosAllocationDiagnostics.currentObject ||
        objectEnd != g_guideXosAllocationDiagnostics.currentObjectEnd ||
        objectAddress == 0u || (objectAddress & 7u) != 0u ||
        *reinterpret_cast<const gx_uint32*>(objectAddress + 8u) != length ||
        *reinterpret_cast<const gx_uintptr*>(objectAddress) != g_guideXosAllocationDiagnostics.eeType) {
        ++g_guideXosAllocationDiagnostics.layoutFailures;
        failure = 1u;
    }
    gx_uint32 heapOwned = 0u;
    if (guidexos_nativeaot_gc_describe_object(
            arrayObject, nullptr, nullptr, nullptr,
            &heapOwned) != 0 || heapOwned == 0u) {
        ++g_guideXosAllocationDiagnostics.ownershipFailures;
        failure = 1u;
    }
    g_guideXosAllocationDiagnostics.heapOwnershipVerified = heapOwned;
    if (g_guideXosAllocationDiagnostics.refill2Returned != 0u &&
        sequence + 1u != g_guideXosAllocationDiagnostics.allocationCount) {
        ++g_guideXosAllocationDiagnostics.contextGeometryFailures;
        failure = 1u;
    }
    return failure == 0u ? 0 : -1;
}

#if defined(GUIDEXOS_NATIVEAOT_FIRST_NON_NULL_ROOT_ALLOCATION)
extern "C" __declspec(noinline) __declspec(dllexport) int __cdecl
guideXosManagedThreadStaticProofAssigned(
    void* arrayObject, gx_uint32 sentinelOrdinal, gx_uint32 objectSize,
    gx_uint32 patternValid) {
    guidexos_nativeaot_allocation_diagnostics& diagnostics =
        g_guideXosAllocationDiagnostics;
    ++diagnostics.threadStaticProofAssignmentCount;
    diagnostics.threadStaticProofSentinelOrdinal = sentinelOrdinal;
    diagnostics.threadStaticProofSentinelAddress =
        reinterpret_cast<gx_uintptr>(arrayObject);
    diagnostics.threadStaticProofSentinelSize = objectSize;
    diagnostics.threadStaticProofManagedThread = reinterpret_cast<gx_uintptr>(
        suspendEeCurrentThread());
    SuspendEeThread* thread = suspendEeCurrentThread();
    InlinedThreadStaticRoot* root = thread == nullptr
        ? nullptr : thread->GetInlinedThreadStaticList();
    diagnostics.runtimeThreadStaticInlinedRootAddress =
        reinterpret_cast<gx_uintptr>(root);
    diagnostics.runtimeThreadStaticStorageObjectAddress = root == nullptr
        ? 0u : reinterpret_cast<gx_uintptr>(root->m_threadStaticsBase);
    diagnostics.runtimeThreadStaticStorageObjectValid =
        diagnostics.runtimeThreadStaticStorageObjectAddress != 0u ? 1u : 0u;
    diagnostics.runtimeThreadStaticStorageAllocationCount =
        diagnostics.runtimeThreadStaticStorageObjectValid;
    diagnostics.runtimeThreadStaticStoragePublicationCount =
        diagnostics.runtimeThreadStaticStorageObjectValid;

    bool valid = diagnostics.threadStaticProofAssignmentCount == 1u &&
        sentinelOrdinal < diagnostics.objectHistoryCount &&
        sentinelOrdinal < GUIDEXOS_NATIVEAOT_MAX_OBJECT_HISTORY &&
        diagnostics.objectHistory[sentinelOrdinal].sentinel != 0u &&
        diagnostics.objectHistory[sentinelOrdinal].address ==
            diagnostics.threadStaticProofSentinelAddress &&
        diagnostics.objectHistory[sentinelOrdinal].length == objectSize &&
        patternValid != 0u;
    diagnostics.threadStaticProofManagedAssignmentValid = valid ? 1u : 0u;
    diagnostics.threadStaticProofInitializationIndicator = valid ? 1u : 0u;
    if (!valid) {
        ++diagnostics.pointerContractFailures;
    }
    return valid ? 0 : -1;
}

extern "C" __declspec(noinline) __declspec(dllexport) int __cdecl
guideXosManagedThreadStaticProofReadback(
    void* arrayObject, gx_uint32 sentinelOrdinal, gx_uint32 exactMatch) {
    guidexos_nativeaot_allocation_diagnostics& diagnostics =
        g_guideXosAllocationDiagnostics;
    ++diagnostics.threadStaticProofReadbackCount;
    diagnostics.threadStaticProofManagedReadbackAddress =
        reinterpret_cast<gx_uintptr>(arrayObject);
    diagnostics.threadStaticProofReadbackExactMatch = exactMatch;
    diagnostics.threadStaticProofManagedReadbackValid =
        (diagnostics.threadStaticProofReadbackCount == 1u &&
         sentinelOrdinal == diagnostics.threadStaticProofSentinelOrdinal &&
         arrayObject != nullptr &&
         diagnostics.threadStaticProofSentinelAddress ==
             diagnostics.threadStaticProofManagedReadbackAddress &&
         exactMatch != 0u) ? 1u : 0u;
    diagnostics.threadStaticProofInitializationIndicator =
        diagnostics.threadStaticProofManagedReadbackValid != 0u ? 2u : 0u;
    if (diagnostics.threadStaticProofManagedReadbackValid == 0u) {
        ++diagnostics.pointerContractFailures;
    }
    return diagnostics.threadStaticProofManagedReadbackValid != 0u ? 0 : -1;
}
#endif

#if defined(GUIDEXOS_NATIVEAOT_FIRST_COLLECTION_BOUNDARY_ALLOCATION)
extern "C" __declspec(noinline) __declspec(dllexport) int __cdecl
guideXosManagedAllocationRecordSentinelValidation(
    gx_uint32 checkedCount, gx_uint32 failureCount) {
    g_guideXosAllocationDiagnostics.sentinelValidationCount += checkedCount;
    g_guideXosAllocationDiagnostics.sentinelValidationFailures += failureCount;
    if (failureCount != 0u) {
        g_guideXosAllocationDiagnostics.pointerContractFailures += 1u;
        return -1;
    }
    g_guideXosAllocationDiagnostics.liveSentinelCount = 4u;
    return 0;
}
#endif

extern "C" __declspec(noinline) __declspec(dllexport) int __cdecl
guideXosManagedAllocationGetLoopStatus() {
    if (g_guideXosAllocationDiagnostics.failureReason != 0u ||
        g_guideXosAllocationDiagnostics.pointerContractFailures != 0u) return -1;
#if defined(GUIDEXOS_NATIVEAOT_SEGMENT_BOUNDARY_ALLOCATION)
#if defined(GUIDEXOS_NATIVEAOT_SEGMENT_TRANSITION_ALLOCATION)
    if (g_guideXosAllocationDiagnostics.allocationCount >=
        g_guideXosAllocationDiagnostics.hardAllocationLimit) {
        g_guideXosAllocationDiagnostics.managedStopObserved = 1u;
        g_guideXosAllocationDiagnostics.noPostRefillAllocation = 1u;
        g_guideXosAllocationDiagnostics.stopReason = 1u;
        g_guideXosAllocationDiagnostics.completionStatus = 4u;
        return 2;
    }
#else
    if (g_guideXosAllocationDiagnostics.boundaryType != 0u) {
        g_guideXosAllocationDiagnostics.managedStopObserved = 1u;
        g_guideXosAllocationDiagnostics.noPostRefillAllocation = 1u;
        g_guideXosAllocationDiagnostics.boundaryStopObserved = 1u;
        return 2;
    }
    if (g_guideXosAllocationDiagnostics.allocationCount >=
        g_guideXosAllocationDiagnostics.hardAllocationLimit) {
        g_guideXosAllocationDiagnostics.completionStatus = 3u;
        return 3;
    }
#endif
#else
    if (g_guideXosAllocationDiagnostics.refill2Returned != 0u) {
        g_guideXosAllocationDiagnostics.managedStopObserved = 1u;
        g_guideXosAllocationDiagnostics.noPostRefillAllocation = 1u;
        return 2;
    }
#endif
    return 1;
}

extern "C" __declspec(noinline) __declspec(dllexport) gx_uint32 __cdecl
guideXosManagedAllocationGetHardLimit() {
    return g_guideXosAllocationDiagnostics.hardAllocationLimit;
}
#endif

#if !defined(GUIDEXOS_NATIVEAOT_REAL_GC_ALLOCATION)
extern "C" __declspec(noinline) int __cdecl guideXosManagedAllocationCanFit(gx_size length) {
    const gx_size objectSize = alignedArrayObjectSize(length);
    g_guideXosAllocationDiagnostics.requestedArrayLength = static_cast<gx_uint32>(length);
    g_guideXosAllocationDiagnostics.requestedObjectSize = static_cast<gx_uint32>(objectSize);
    unsigned char* block = currentTlsBlock();
    unsigned char* cell = runtimeCell(block);
    if (objectSize == 0u || cell == nullptr) {
        g_guideXosAllocationDiagnostics.pointerContractFailures += 1u;
        return -1;
    }

    const gx_uintptr allocationPointer = reinterpret_cast<gx_uintptr>(*reinterpret_cast<void**>(cell));
    const gx_uintptr allocationLimit = reinterpret_cast<gx_uintptr>(*reinterpret_cast<void**>(cell + sizeof(void*)));
    if (allocationPointer == 0u || allocationLimit < allocationPointer) {
        g_guideXosAllocationDiagnostics.pointerContractFailures += 1u;
        return -1;
    }
    if (objectSize > allocationLimit - allocationPointer) {
        markAllocationOutOfMemory(cell, allocationPointer);
        g_guideXosAllocationDiagnostics.controlledOutOfMemory = 1u;
        return 0;
    }
    return 1;
}

extern "C" __declspec(noinline) int __cdecl guideXosManagedAllocationValidateObject(
    void* arrayObject,
    gx_size length,
    gx_uint32 sequence,
    gx_uint32 zeroInitialized,
    gx_uint32 patternValid) {
    const gx_size objectSize = alignedArrayObjectSize(length);
    const gx_uintptr objectAddress = reinterpret_cast<gx_uintptr>(arrayObject);
    const gx_uintptr heapBase = reinterpret_cast<gx_uintptr>(g_guideXosManagedHeap);
    const gx_uintptr heapLimit = heapBase + kManagedHeapBytes;
    gx_uint32 failure = 0u;

    g_guideXosAllocationDiagnostics.zeroInitializationChecks += 1u;
    if (zeroInitialized == 0u) {
        g_guideXosAllocationDiagnostics.zeroInitializationFailures += 1u;
        failure = 1u;
    }
    g_guideXosAllocationDiagnostics.patternChecks += 1u;
    if (patternValid == 0u) {
        g_guideXosAllocationDiagnostics.patternFailures += 1u;
        failure = 1u;
    }
    if (arrayObject == nullptr || (objectAddress & 7u) != 0u) {
        g_guideXosAllocationDiagnostics.objectAlignmentFailures += 1u;
        failure = 1u;
    }
    if (objectAddress < heapBase || objectSize == 0u || objectAddress > heapLimit - objectSize) {
        g_guideXosAllocationDiagnostics.objectRangeFailures += 1u;
        failure = 1u;
    } else if (objectAddress + objectSize != g_guideXosAllocationDiagnostics.allocationPointerAfter ||
               objectAddress != g_guideXosAllocationDiagnostics.lastObject) {
        g_guideXosAllocationDiagnostics.objectRangeFailures += 1u;
        failure = 1u;
    }
    if (arrayObject != nullptr && *reinterpret_cast<gx_uint32*>(reinterpret_cast<unsigned char*>(arrayObject) + 8u) != length) {
        g_guideXosAllocationDiagnostics.objectLayoutFailures += 1u;
        failure = 1u;
    }
    if (arrayObject != nullptr && objectSize >= kManagedArrayDataOffset + 4u) {
        const unsigned char* data = reinterpret_cast<const unsigned char*>(arrayObject) + kManagedArrayDataOffset;
        const gx_uint32 observedSequence = static_cast<gx_uint32>(data[0]) |
            (static_cast<gx_uint32>(data[1]) << 8u) |
            (static_cast<gx_uint32>(data[2]) << 16u) |
            (static_cast<gx_uint32>(data[3]) << 24u);
        if (observedSequence != sequence) {
            g_guideXosAllocationDiagnostics.objectLayoutFailures += 1u;
            failure = 1u;
        }
    }
    if (failure != 0u) {
        g_guideXosAllocationDiagnostics.sampledObjectFailures += 1u;
    }
    return failure == 0u ? 0 : -1;
}

extern "C" __declspec(noinline) int __cdecl guideXosManagedAllocationRecordFailure(gx_uint32 reason) {
    (void)reason;
    g_guideXosAllocationDiagnostics.sampledObjectFailures += 1u;
    return -1;
}

struct GuideXosNativeHostCallTable {
    gx_uint32 size;
    gx_uint32 version;
    int (__cdecl* log)(void* context, unsigned char* message);
};

struct GuideXosNativeAppContext {
    gx_uint32 size;
    gx_uint32 apiVersion;
    GuideXosNativeHostCallTable* host;
    void* userData;
};

void appendReportText(char*& cursor, char* end, const char* text) {
    while (*text != '\0' && cursor < end) *cursor++ = *text++;
}

void appendReportUnsigned(char*& cursor, char* end, gx_uintptr value) {
    char reversed[32];
    gx_uint32 count = 0u;
    do {
        reversed[count++] = static_cast<char>('0' + (value % 10u));
        value /= 10u;
    } while (value != 0u && count < sizeof(reversed));
    while (count != 0u && cursor < end) *cursor++ = reversed[--count];
}

void appendReportHex(char*& cursor, char* end, gx_uintptr value) {
    static constexpr char digits[] = "0123456789abcdef";
    appendReportText(cursor, end, "0x");
    char reversed[16];
    gx_uint32 count = 0u;
    do {
        reversed[count++] = digits[value & 0xFu];
        value >>= 4u;
    } while (value != 0u && count < sizeof(reversed));
    while (count != 0u && cursor < end) *cursor++ = reversed[--count];
}

extern "C" __declspec(noinline) int __cdecl guideXosManagedAllocationReport(void* context, gx_uint32 status) {
    auto* appContext = reinterpret_cast<GuideXosNativeAppContext*>(context);
    if (appContext == nullptr || appContext->host == nullptr || appContext->host->log == nullptr) {
        guideXosFailFast(7u);
    }
    char buffer[1024];
    char* cursor = buffer;
    char* end = buffer + sizeof(buffer) - 2;
    unsigned char* block = currentTlsBlock();
    unsigned char* cell = runtimeCell(block);
    const gx_uintptr currentPointer = cell == nullptr ? 0u : reinterpret_cast<gx_uintptr>(*reinterpret_cast<void**>(cell));
    const gx_uintptr currentLimit = cell == nullptr ? 0u : reinterpret_cast<gx_uintptr>(*reinterpret_cast<void**>(cell + sizeof(void*)));
    appendReportText(cursor, end, status == 0u ? "Managed allocations completed: " : "Managed repeated allocation integrity failure: ");
    appendReportUnsigned(cursor, end, g_guideXosAllocationDiagnostics.allocationCount);
    appendReportText(cursor, end, "; heap=");
    appendReportUnsigned(cursor, end, g_guideXosAllocationDiagnostics.heapSize);
    appendReportText(cursor, end, "; object=");
    appendReportUnsigned(cursor, end, g_guideXosAllocationDiagnostics.lastObjectSize);
    appendReportText(cursor, end, "; remaining=");
    appendReportUnsigned(cursor, end, g_guideXosAllocationDiagnostics.remainingBytesBeforeFailure);
    appendReportText(cursor, end, "; initial=");
    appendReportHex(cursor, end, g_guideXosAllocationDiagnostics.initialAllocationPointer);
    appendReportText(cursor, end, "; pointerBeforeFailure=");
    appendReportHex(cursor, end, g_guideXosAllocationDiagnostics.allocationPointerBeforeFailure);
    appendReportText(cursor, end, "; pointerAfterFailure=");
    appendReportHex(cursor, end, g_guideXosAllocationDiagnostics.allocationPointerAfterFailure);
    appendReportText(cursor, end, "; monotonicity=");
    appendReportText(cursor, end, g_guideXosAllocationDiagnostics.pointerContractFailures == 0u ? "PASS" : "FAIL");
    appendReportText(cursor, end, "; nonOverlap=");
    appendReportText(cursor, end, g_guideXosAllocationDiagnostics.objectRangeFailures == 0u ? "PASS" : "FAIL");
    appendReportText(cursor, end, "; sampledIntegrity=");
    appendReportText(cursor, end, g_guideXosAllocationDiagnostics.sampledObjectFailures == 0u ? "PASS" : "FAIL");
    appendReportText(cursor, end, "; zeroInit=");
    appendReportText(cursor, end, g_guideXosAllocationDiagnostics.zeroInitializationFailures == 0u ? "PASS" : "FAIL");
    appendReportText(cursor, end, "; collectionEntered=0; heapExpansionOccurred=0; controlledOom=");
    appendReportUnsigned(cursor, end, g_guideXosAllocationDiagnostics.controlledOutOfMemory);
    appendReportText(cursor, end, "; currentPointer=");
    appendReportHex(cursor, end, currentPointer);
    appendReportText(cursor, end, "; currentLimit=");
    appendReportHex(cursor, end, currentLimit);
    appendReportText(cursor, end, "; runtimeCell=");
    appendReportHex(cursor, end, reinterpret_cast<gx_uintptr>(cell));
    appendReportText(cursor, end, "; requestedObject=");
    appendReportUnsigned(cursor, end, g_guideXosAllocationDiagnostics.requestedObjectSize);
    *cursor++ = '\0';
    return appContext->host->log(context, reinterpret_cast<unsigned char*>(buffer));
}

// This is an application-scoped runtime helper. The managed object reference
// never crosses the guideXOS host ABI; only the computed byte-data pointer does.
extern "C" __declspec(noinline) int __cdecl guideXosManagedArrayHostLog(void* context, void* arrayObject) {
    auto* appContext = reinterpret_cast<GuideXosNativeAppContext*>(context);
    if (appContext == nullptr || appContext->host == nullptr || appContext->host->log == nullptr || arrayObject == nullptr) {
        guideXosFailFast(7u);
    }
    auto* arrayData = reinterpret_cast<unsigned char*>(arrayObject) + kManagedArrayDataOffset;
    return appContext->host->log(context, arrayData);
}
#endif
#endif

extern "C" __declspec(noinline) void* __cdecl FlsGetValue(gx_uint32 index) {
    if (g_guideXosRuntimeStartupState == 0 || index == kFlsOutOfIndexes) {
        return nullptr;
    }
    return flsCell(currentTlsBlock(), index) == nullptr
        ? nullptr
        : *flsCell(currentTlsBlock(), index);
}

extern "C" __declspec(noinline) int __cdecl FlsSetValue(gx_uint32 index, void* value) {
    if (g_guideXosRuntimeStartupState == 0) {
        return 0;
    }
    void** cell = flsCell(currentTlsBlock(), index);
    if (cell == nullptr) {
        return 0;
    }
    *cell = value;
    return 1;
}

// The NativeAOT image references the import-pointer spelling. Defining the
// pointer in the runtime-pack object binds the call to the guideXOS functions
// above and prevents a live KERNEL32 import thunk from being emitted for this
// transition.
extern "C" __declspec(selectany) void* __imp_FlsGetValue = reinterpret_cast<void*>(&FlsGetValue);
extern "C" __declspec(selectany) void* __imp_FlsSetValue = reinterpret_cast<void*>(&FlsSetValue);

#if !defined(GUIDEXOS_NATIVEAOT_GC_STARTUP)
extern "C" __declspec(noinline) void __cdecl RhpReversePInvoke(void* frame) {
#if defined(GUIDEXOS_NATIVEAOT_REAL_GC_ALLOCATION)
    const bool firstManagedEntry = !g_guideXosRealGcDiagnosticsInitialized;
#endif
    unsigned char* block = currentTlsBlock();
    if (frame == nullptr || block == nullptr || _tls_index == kFlsOutOfIndexes) {
#if defined(GUIDEXOS_NATIVEAOT_REAL_GC_ALLOCATION)
        recordAllocationStage(GUIDEXOS_NATIVEAOT_ALLOC_STAGE_FAILFAST_REVERSE_PINVOKE,
                              nullptr, 0, 0, 0, 0, 0,
                              reinterpret_cast<gx_uintptr>(frame));
#endif
        guideXosFailFast(2u);
    }

    initializeRuntimeState(block);
#if defined(GUIDEXOS_NATIVEAOT_REAL_GC_ALLOCATION)
    if (firstManagedEntry) {
        ++g_guideXosAllocationDiagnostics.managedEntryCount;
        recordAllocationStage(GUIDEXOS_NATIVEAOT_ALLOC_STAGE_A00_MANAGED_ENTRY,
                              nullptr, 0, 0, 0, 0, 0,
                              reinterpret_cast<gx_uintptr>(frame));
        recordAllocationStage(GUIDEXOS_NATIVEAOT_ALLOC_STAGE_A01_REVERSE_PINVOKE_READY,
                              nullptr, 0, 0, 0, 0, 0,
                              reinterpret_cast<gx_uintptr>(frame));
    }
#endif
#if defined(GUIDEXOS_NATIVEAOT_MANAGED_ALLOCATION)
    // Bind the one experimental __Internal P/Invoke slot after the reverse
    // transition has established the current thread state. This is not a
    // general P/Invoke resolver; it is the app-scoped allocation proof hook.
#if defined(GUIDEXOS_NATIVEAOT_THREAD_STATIC_PROOF)
    using GuideXosManagedThreadStaticProofRecordFn = int (__cdecl*)(
        uint32_t, uint32_t, uintptr_t, uintptr_t, uint32_t, uint32_t,
        uint32_t, uint32_t);
    __pinvoke_HostLogProof__Module____Internal__guideXosManagedThreadStaticProofRecord__Ansi =
        reinterpret_cast<void*>(static_cast<GuideXosManagedThreadStaticProofRecordFn>(
            guideXosManagedThreadStaticProofRecord));
#endif
#if !defined(GUIDEXOS_NATIVEAOT_MANAGED_REPEATED_ALLOCATION) && !defined(GUIDEXOS_NATIVEAOT_REAL_GC_ALLOCATION)
    using GuideXosManagedArrayHostLogFn = int (__cdecl*)(void*, void*);
    __pinvoke_HostLogProof__Module____Internal__guideXosManagedArrayHostLog__Ansi = reinterpret_cast<void*>(static_cast<GuideXosManagedArrayHostLogFn>(guideXosManagedArrayHostLog));
#endif
#if defined(GUIDEXOS_NATIVEAOT_MANAGED_REPEATED_ALLOCATION)
    using GuideXosManagedAllocationCanFitFn = int (__cdecl*)(gx_size);
    using GuideXosManagedAllocationValidateObjectFn = int (__cdecl*)(void*, gx_size, gx_uint32, gx_uint32, gx_uint32);
    using GuideXosManagedAllocationRecordFailureFn = int (__cdecl*)(gx_uint32);
    using GuideXosManagedAllocationReportFn = int (__cdecl*)(void*, gx_uint32);
    __pinvoke_HostLogProof__Module____Internal__guideXosManagedAllocationCanFit__Ansi = reinterpret_cast<void*>(static_cast<GuideXosManagedAllocationCanFitFn>(guideXosManagedAllocationCanFit));
    __pinvoke_HostLogProof__Module____Internal__guideXosManagedAllocationValidateObject__Ansi = reinterpret_cast<void*>(static_cast<GuideXosManagedAllocationValidateObjectFn>(guideXosManagedAllocationValidateObject));
    __pinvoke_HostLogProof__Module____Internal__guideXosManagedAllocationRecordFailure__Ansi = reinterpret_cast<void*>(static_cast<GuideXosManagedAllocationRecordFailureFn>(guideXosManagedAllocationRecordFailure));
    __pinvoke_HostLogProof__Module____Internal__guideXosManagedAllocationReport__Ansi = reinterpret_cast<void*>(static_cast<GuideXosManagedAllocationReportFn>(guideXosManagedAllocationReport));
#elif defined(GUIDEXOS_NATIVEAOT_FIRST_REFILL_ALLOCATION) || defined(GUIDEXOS_NATIVEAOT_SEGMENT_BOUNDARY_ALLOCATION)
    using GuideXosManagedAllocationValidateObjectFn = int (__cdecl*)(void*, gx_size, gx_uint32, gx_uint32, gx_uint32);
#if !defined(GUIDEXOS_NATIVEAOT_FIRST_COLLECTION_BOUNDARY_ALLOCATION)
    using GuideXosManagedAllocationGetLoopStatusFn = int (__cdecl*)(void);
#endif
    using GuideXosManagedAllocationGetHardLimitFn = gx_uint32 (__cdecl*)(void);
    using GuideXosManagedAllocationRecordSentinelValidationFn = int (__cdecl*)(gx_uint32, gx_uint32);
    __pinvoke_HostLogProof__Module____Internal__guideXosManagedAllocationValidateObject__Ansi = reinterpret_cast<void*>(static_cast<GuideXosManagedAllocationValidateObjectFn>(guideXosManagedAllocationValidateObject));
#if !defined(GUIDEXOS_NATIVEAOT_FIRST_COLLECTION_BOUNDARY_ALLOCATION)
    __pinvoke_HostLogProof__Module____Internal__guideXosManagedAllocationGetLoopStatus__Ansi = reinterpret_cast<void*>(static_cast<GuideXosManagedAllocationGetLoopStatusFn>(guideXosManagedAllocationGetLoopStatus));
#endif
    __pinvoke_HostLogProof__Module____Internal__guideXosManagedAllocationGetHardLimit__Ansi = reinterpret_cast<void*>(static_cast<GuideXosManagedAllocationGetHardLimitFn>(guideXosManagedAllocationGetHardLimit));
#if defined(GUIDEXOS_NATIVEAOT_FIRST_COLLECTION_BOUNDARY_ALLOCATION)
    __pinvoke_HostLogProof__Module____Internal__guideXosManagedAllocationRecordSentinelValidation__Ansi = reinterpret_cast<void*>(static_cast<GuideXosManagedAllocationRecordSentinelValidationFn>(guideXosManagedAllocationRecordSentinelValidation));
#endif
#if defined(GUIDEXOS_NATIVEAOT_FIRST_NON_NULL_ROOT_ALLOCATION)
    using GuideXosManagedThreadStaticProofFn = int (__cdecl*)(void*, gx_uint32, gx_uint32, gx_uint32);
    using GuideXosManagedThreadStaticProofReadbackFn = int (__cdecl*)(void*, gx_uint32, gx_uint32);
    __pinvoke_HostLogProof__Module____Internal__guideXosManagedThreadStaticProofAssigned__Ansi = reinterpret_cast<void*>(static_cast<GuideXosManagedThreadStaticProofFn>(guideXosManagedThreadStaticProofAssigned));
    __pinvoke_HostLogProof__Module____Internal__guideXosManagedThreadStaticProofReadback__Ansi = reinterpret_cast<void*>(static_cast<GuideXosManagedThreadStaticProofReadbackFn>(guideXosManagedThreadStaticProofReadback));
#endif
#endif
#endif
    unsigned char* cell = runtimeCell(block);
    void** transitionFrame = reinterpret_cast<void**>(frame);
    transitionFrame[0] = *reinterpret_cast<void**>(cell + kRuntimeCellTransitionFrameOffset);
    transitionFrame[1] = reinterpret_cast<void*>(cell);
    *reinterpret_cast<void**>(cell + kRuntimeCellTransitionFrameOffset) = frame;
}

extern "C" __declspec(noinline) void __cdecl RhpReversePInvokeReturn(void* frame) {
    if (frame == nullptr) {
        guideXosFailFast(3u);
    }
    void** transitionFrame = reinterpret_cast<void**>(frame);
    unsigned char* cell = reinterpret_cast<unsigned char*>(transitionFrame[1]);
    if (cell == nullptr) {
        guideXosFailFast(4u);
    }
    *reinterpret_cast<void**>(cell + kRuntimeCellTransitionFrameOffset) = transitionFrame[0];
}

extern "C" __declspec(noinline) void __cdecl RhpReversePInvoke2(void* frame) {
    RhpReversePInvoke(frame);
}

extern "C" __declspec(noinline) void __cdecl RhpReversePInvokeReturn2(void* frame) {
    RhpReversePInvokeReturn(frame);
}

extern "C" __declspec(noinline) void __cdecl RhpFallbackFailFast() {
    guideXosFailFast(5u);
}
#endif
