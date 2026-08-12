#include <intrin.h>
#include "guidexos_nativeaot_allocation_diagnostics.h"
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

void suspendEeSerialPutString(const char* value) {
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

extern "C" [[noreturn]] void __cdecl
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
    firstRootCondemnedGenerationDecisionSafeStop();
}

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
void* g_guideXosNativeAotClasslibFunctions[16] = {};
#endif

#if defined(GUIDEXOS_NATIVEAOT_THREAD_STATIC_PROOF)
extern "C" volatile guidexos_nativeaot_thread_static_diagnostics
    g_guideXosThreadStaticDiagnostics = { 1u };
#endif

[[noreturn]] void guideXosFailFast(gx_uint32 reason) {
#if defined(GUIDEXOS_NATIVEAOT_MANAGED_ALLOCATION) && defined(GUIDEXOS_NATIVEAOT_REAL_GC_ALLOCATION)
    g_guideXosAllocationDiagnostics.failFastReason = reason;
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
