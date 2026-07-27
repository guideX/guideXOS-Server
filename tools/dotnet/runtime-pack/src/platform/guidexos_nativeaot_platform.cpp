#include <intrin.h>
#include "guidexos_nativeaot_allocation_diagnostics.h"

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
#endif
#endif

#if defined(GUIDEXOS_NATIVEAOT_MANAGED_ALLOCATION) && defined(GUIDEXOS_NATIVEAOT_REAL_GC_ALLOCATION)
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
    volatile unsigned char* diagnostics = reinterpret_cast<volatile unsigned char*>(&g_guideXosAllocationDiagnostics);
    for (gx_size i = 0; i < sizeof(g_guideXosAllocationDiagnostics); ++i) {
        diagnostics[i] = 0;
    }
    g_guideXosAllocationDiagnostics.schemaVersion = 1u;
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
extern "C" __declspec(noinline) int __cdecl guideXosManagedArrayHostLog(void* context, void* arrayObject);
extern "C" __declspec(noinline) int __cdecl guideXosManagedAllocationCanFit(gx_size length);
extern "C" __declspec(noinline) int __cdecl guideXosManagedAllocationValidateObject(void* arrayObject, gx_size length, gx_uint32 sequence, gx_uint32 zeroInitialized, gx_uint32 patternValid);
extern "C" __declspec(noinline) int __cdecl guideXosManagedAllocationRecordFailure(gx_uint32 reason);
extern "C" __declspec(noinline) int __cdecl guideXosManagedAllocationReport(void* context, gx_uint32 status);
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
#endif
#endif

#if defined(GUIDEXOS_NATIVEAOT_MANAGED_ALLOCATION)
extern "C" __declspec(noinline) void* __cdecl RhpNewArray(void* eeType, gx_size length) {
#if defined(GUIDEXOS_NATIVEAOT_REAL_GC_ALLOCATION)
    recordAllocationStage(GUIDEXOS_NATIVEAOT_ALLOC_STAGE_A02_RHP_NEW_ARRAY_ENTRY,
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

    const auto* typeBytes = reinterpret_cast<const unsigned char*>(eeType);
    const gx_size componentSize = *reinterpret_cast<const gx_uint16*>(typeBytes);
    const gx_size baseSize = *reinterpret_cast<const gx_uint32*>(typeBytes + sizeof(gx_uint32));
    if (componentSize != 0u && length > ((~static_cast<gx_size>(0)) - baseSize - 7u) / componentSize) {
#if defined(GUIDEXOS_NATIVEAOT_REAL_GC_ALLOCATION)
        ++g_guideXosAllocationDiagnostics.pointerContractFailures;
        recordAllocationStage(GUIDEXOS_NATIVEAOT_ALLOC_STAGE_FAILFAST_ALLOCATION,
                              eeType, length);
#else
        g_guideXosAllocationDiagnostics.outOfMemory = 1u;
#endif
        guideXosFailFast(6u);
    }

    const gx_size unalignedObjectSize = baseSize + (componentSize * length);
    const gx_size objectSize = (unalignedObjectSize + 7u) & ~static_cast<gx_size>(7u);
    g_guideXosAllocationDiagnostics.requestedArrayLength = static_cast<gx_uint32>(length);
    g_guideXosAllocationDiagnostics.requestedObjectSize = static_cast<gx_uint32>(objectSize);

#if defined(GUIDEXOS_NATIVEAOT_REAL_GC_ALLOCATION)
    recordAllocationStage(GUIDEXOS_NATIVEAOT_ALLOC_STAGE_A03_TYPE_LENGTH_ACCEPTED,
                          eeType, length, objectSize);
    recordAllocationStage(GUIDEXOS_NATIVEAOT_ALLOC_STAGE_A04_OBJECT_SIZE_COMPUTED,
                          eeType, length, objectSize);
    ++g_guideXosAllocationDiagnostics.rhpNewArrayEntries;
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
    g_guideXosAllocationDiagnostics.realGcAllocationEntries += 1u;
    if (allocationPointerBefore == 0u || allocationLimitBefore == 0u) {
        ++g_guideXosAllocationDiagnostics.slowAllocationEntries;
        ++g_guideXosAllocationDiagnostics.allocationContextRefills;
    }
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
    recordAllocationStage(GUIDEXOS_NATIVEAOT_ALLOC_STAGE_A16_ALLOCATION_RETURNED,
                          eeType, length, objectSize, allocationPointerAfter,
                          allocationLimitAfter, currentThreadAfter, transitionFrame);
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
    return &g_guideXosAllocationDiagnostics;
}

extern "C" __declspec(dllexport) int __cdecl
guideXosManagedAllocationFinalize(gx_uint32 managedReturnCode) {
    g_guideXosAllocationDiagnostics.managedReturnCode = managedReturnCode;
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
    g_guideXosAllocationDiagnostics.objectRangeVerified =
        objectAddress != 0u && objectSize == 48u &&
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
    recordAllocationStage(GUIDEXOS_NATIVEAOT_ALLOC_STAGE_A00_MANAGED_ENTRY,
                          nullptr, 0, 0, 0, 0, 0,
                          reinterpret_cast<gx_uintptr>(frame));
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
    recordAllocationStage(GUIDEXOS_NATIVEAOT_ALLOC_STAGE_A01_REVERSE_PINVOKE_READY,
                          nullptr, 0, 0, 0, 0, 0,
                          reinterpret_cast<gx_uintptr>(frame));
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
