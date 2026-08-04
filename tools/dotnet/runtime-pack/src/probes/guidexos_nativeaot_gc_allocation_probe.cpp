#ifndef LPVOID
typedef void* LPVOID;
#endif

#include "gcenv.h"
#include "gcheaputilities.h"
#include "thread.h"
#include "threadstore.h"
#include "thread.inl"
#include "threadstore.inl"
#include "gc.h"

// The public IGCHeap surface intentionally exposes only the allocated and
// reserved range.  This probe is built against the locked collector source,
// so make the source-backed heap_segment metadata available for diagnostics
// without changing the collector ABI or its production headers.
#include <memory>
#include <utility>
namespace WKS {
#define private public
#define allocator guidexos_gc_allocator
#define pair guidexos_gc_pair
#include "gcpriv.h"
#undef pair
#undef allocator
#undef private
}

// The locked gcwks.cpp translation unit keeps this helper private.  MSVC
// encodes that access level in the decorated symbol, so bridge the public
// declaration above to the collector's original private symbol without
// changing or relinking the collector object.
#pragma comment(linker, "/alternatename:?find_segment@gc_heap@WKS@@SAPEAVheap_segment@2@PEAEH@Z=?find_segment@gc_heap@WKS@@CAPEAVheap_segment@2@PEAEH@Z")

#include <stdint.h>

extern "C" int32_t guidexos_nativeaot_gc_read_state(
    uintptr_t* allocationContext,
    uintptr_t* allocationLimit,
    uintptr_t* currentThread,
    uintptr_t* gcHeap,
    uint32_t* gcCount,
    uintptr_t* allocatedBytes,
    uint32_t* finalizableObjects,
    uint32_t* gcInProgress,
    uint32_t* gcMode,
    uintptr_t* contextIdentity,
    uintptr_t* allocBytes,
    uintptr_t* allocBytesUoh) {
    Thread* thread = ThreadStore::GetCurrentThreadIfAvailable();
    if (thread == nullptr || thread->GetAllocContext() == nullptr ||
        !GCHeapUtilities::IsGCHeapInitialized()) {
        return -1;
    }

    gc_alloc_context* context = thread->GetAllocContext();
    IGCHeap* heap = GCHeapUtilities::GetGCHeap();
    if (allocationContext != nullptr) {
        *allocationContext = reinterpret_cast<uintptr_t>(context->alloc_ptr);
    }
    if (allocationLimit != nullptr) {
        *allocationLimit = reinterpret_cast<uintptr_t>(context->alloc_limit);
    }
    if (currentThread != nullptr) {
        *currentThread = reinterpret_cast<uintptr_t>(thread);
    }
    if (gcHeap != nullptr) {
        *gcHeap = reinterpret_cast<uintptr_t>(heap);
    }
    if (gcCount != nullptr) {
        *gcCount = heap->GetGcCount();
    }
    if (allocatedBytes != nullptr) {
        *allocatedBytes = static_cast<uintptr_t>(heap->GetTotalAllocatedBytes());
    }
    if (finalizableObjects != nullptr) {
        *finalizableObjects = static_cast<uint32_t>(heap->GetNumberOfFinalizable());
    }
    if (gcInProgress != nullptr) {
        *gcInProgress = heap->IsGCInProgressHelper(false) ? 1u : 0u;
    }
    if (gcMode != nullptr) {
        *gcMode = thread->IsCurrentThreadInCooperativeMode() ? 1u : 0u;
    }
    if (contextIdentity != nullptr) {
        *contextIdentity = reinterpret_cast<uintptr_t>(context);
    }
    if (allocBytes != nullptr) {
        *allocBytes = static_cast<uintptr_t>(context->alloc_bytes);
    }
    if (allocBytesUoh != nullptr) {
        *allocBytesUoh = static_cast<uintptr_t>(context->alloc_bytes_uoh);
    }
    return 0;
}

extern "C" int32_t guidexos_nativeaot_gc_describe_object(
    void* object,
    uintptr_t* heapBase,
    uintptr_t* heapAllocated,
    uintptr_t* heapReserved,
    uint32_t* heapOwned) {
    if (heapBase != nullptr) *heapBase = 0;
    if (heapAllocated != nullptr) *heapAllocated = 0;
    if (heapReserved != nullptr) *heapReserved = 0;
    if (heapOwned != nullptr) *heapOwned = 0;
    if (object == nullptr || !GCHeapUtilities::IsGCHeapInitialized()) return -1;

    IGCHeap* heap = GCHeapUtilities::GetGCHeap();
    const bool owned = heap->IsHeapPointer(object, false);
    if (heapOwned != nullptr) *heapOwned = owned ? 1u : 0u;
    if (!owned) return 0;

    uint8_t* start = nullptr;
    uint8_t* allocated = nullptr;
    uint8_t* reserved = nullptr;
    heap->GetGenerationWithRange(
        reinterpret_cast<Object*>(object), &start, &allocated, &reserved);
    if (heapBase != nullptr) *heapBase = reinterpret_cast<uintptr_t>(start);
    if (heapAllocated != nullptr) *heapAllocated = reinterpret_cast<uintptr_t>(allocated);
    if (heapReserved != nullptr) *heapReserved = reinterpret_cast<uintptr_t>(reserved);
    return 0;
}

extern "C" int32_t guidexos_nativeaot_gc_describe_segment(
    void* object,
    uintptr_t* segmentIdentity,
    uintptr_t* segmentBase,
    uintptr_t* segmentAllocated,
    uintptr_t* segmentCommitted,
    uintptr_t* segmentReserved,
    uint32_t* segmentFlags,
    uint32_t* segmentGeneration) {
    if (segmentIdentity != nullptr) *segmentIdentity = 0;
    if (segmentBase != nullptr) *segmentBase = 0;
    if (segmentAllocated != nullptr) *segmentAllocated = 0;
    if (segmentCommitted != nullptr) *segmentCommitted = 0;
    if (segmentReserved != nullptr) *segmentReserved = 0;
    if (segmentFlags != nullptr) *segmentFlags = 0;
    if (segmentGeneration != nullptr) *segmentGeneration = 0;
    if (object == nullptr || !GCHeapUtilities::IsGCHeapInitialized()) return -1;

    // find_segment is the collector's own address-to-segment lookup.  The
    // identity is the heap_segment pointer, not a rounded or guessed address.
    WKS::heap_segment* segment = WKS::gc_heap::find_segment(
        reinterpret_cast<uint8_t*>(object), 0);
    if (segment == nullptr) return -1;

    if (segmentIdentity != nullptr) {
        *segmentIdentity = reinterpret_cast<uintptr_t>(segment);
    }
    if (segmentBase != nullptr) {
        *segmentBase = reinterpret_cast<uintptr_t>(heap_segment_mem(segment));
    }
    if (segmentAllocated != nullptr) {
        *segmentAllocated = reinterpret_cast<uintptr_t>(heap_segment_allocated(segment));
    }
    if (segmentCommitted != nullptr) {
        *segmentCommitted = reinterpret_cast<uintptr_t>(heap_segment_committed(segment));
    }
    if (segmentReserved != nullptr) {
        *segmentReserved = reinterpret_cast<uintptr_t>(heap_segment_reserved(segment));
    }
    if (segmentFlags != nullptr) {
        *segmentFlags = static_cast<uint32_t>(segment->flags);
    }
#ifdef USE_REGIONS
    if (segmentGeneration != nullptr) {
        *segmentGeneration = static_cast<uint32_t>(heap_segment_gen_num(segment));
    }
#endif
    return 0;
}
