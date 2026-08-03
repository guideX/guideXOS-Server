#include <intrin.h>
#include "guidexos_nativeaot_allocation_diagnostics.h"
#if defined(GUIDEXOS_NATIVEAOT_SEGMENT_BOUNDARY_ALLOCATION)
#include "guidexos_nativeaot_virtual_memory_adapter.h"
#endif

#pragma intrinsic(__readgsqword)

namespace {

using gx_uintptr = unsigned __int64;
using gx_uint32 = unsigned long;
using gx_uint16 = unsigned short;
using gx_size = unsigned __int64;

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

#if defined(GUIDEXOS_NATIVEAOT_MANAGED_ALLOCATION)
constexpr gx_uint32 kReadyToRunDehydratedDataSection = 0xCFu;
constexpr gx_uint32 kReadyToRunHeaderSectionCountOffset = 0x0Cu;
constexpr gx_uint32 kReadyToRunHeaderSectionsOffset = 0x10u;
constexpr gx_uint32 kReadyToRunSectionEntrySize = 0x18u;
constexpr gx_uint32 kReadyToRunSectionTypeOffset = 0x00u;
constexpr gx_uint32 kReadyToRunSectionStartOffset = 0x08u;
constexpr gx_uint32 kReadyToRunSectionEndOffset = 0x10u;
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
    gx_uint32* gcMode);
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
extern "C" void __cdecl S_P_CoreLib_Internal_Runtime_CompilerHelpers_StartupCodeHelpers__RehydrateData(void* dehydratedData, gx_uint32 size);
extern "C" unsigned char __dehydrated_data[];
extern "C" unsigned char __ReadyToRunHeader[];
volatile gx_uint32 g_guideXosGeneratedMetadataHydrated = 0;
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
void hydrateGeneratedMetadata() {
    if (g_guideXosGeneratedMetadataHydrated != 0u) {
        return;
    }

    const unsigned char* header = __ReadyToRunHeader;
    const gx_uint32 sectionCount = *reinterpret_cast<const gx_uint16*>(header + kReadyToRunHeaderSectionCountOffset);
    if (sectionCount > 0x1000u) {
        guideXosFailFast(9u);
    }

    for (gx_uint32 i = 0; i < sectionCount; ++i) {
        const unsigned char* section = header + kReadyToRunHeaderSectionsOffset +
            (static_cast<gx_size>(i) * kReadyToRunSectionEntrySize);
        const gx_uint32 sectionType = *reinterpret_cast<const gx_uint32*>(section + kReadyToRunSectionTypeOffset);
        if (sectionType != kReadyToRunDehydratedDataSection) {
            continue;
        }

        const gx_uintptr sectionStart = *reinterpret_cast<const gx_uintptr*>(section + kReadyToRunSectionStartOffset);
        const gx_uintptr sectionEnd = *reinterpret_cast<const gx_uintptr*>(section + kReadyToRunSectionEndOffset);
        const gx_uintptr expectedStart = reinterpret_cast<gx_uintptr>(__dehydrated_data);
        if (sectionStart != expectedStart || sectionEnd <= sectionStart || sectionEnd - sectionStart > 0xFFFFFFFFu) {
            guideXosFailFast(9u);
        }

        S_P_CoreLib_Internal_Runtime_CompilerHelpers_StartupCodeHelpers__RehydrateData(
            reinterpret_cast<void*>(sectionStart), static_cast<gx_uint32>(sectionEnd - sectionStart));
        g_guideXosGeneratedMetadataHydrated = 1u;
        return;
    }

    guideXosFailFast(9u);
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
    hydrateGeneratedMetadata();
#endif
}

} // namespace

#if defined(GUIDEXOS_NATIVEAOT_MANAGED_ALLOCATION)
// HostLogProof's generated NativeAOT P/Invoke slot is intentionally bound by
// the application-scoped runtime pack. The ELF loader does not run the Windows
// module resolver that would normally populate this slot.
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
#endif
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
    unsigned char* tlsBlock = currentTlsBlock();
    unsigned char* tlsCell = runtimeCell(tlsBlock);
    const gx_uintptr transitionFrame = tlsCell != nullptr
        ? reinterpret_cast<gx_uintptr>(
            *reinterpret_cast<void**>(tlsCell + kRuntimeCellTransitionFrameOffset))
        : 0u;
    if (guidexos_nativeaot_gc_read_state(
            &allocationPointerBefore, &allocationLimitBefore, &currentThread,
            &gcHeap, &gcCountBefore, &gcBytesBefore, &finalizableBefore,
            &gcInProgressBefore, &gcModeBefore) != 0 || currentThread == 0 || gcHeap == 0) {
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
    if (guidexos_nativeaot_gc_read_state(
            &allocationPointerAfter, &allocationLimitAfter, &currentThreadAfter,
            &gcHeapAfter, &gcCountAfter, &gcBytesAfter, &finalizableAfter,
            &gcInProgressAfter, &gcModeAfter) != 0 || currentThreadAfter != currentThread ||
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
