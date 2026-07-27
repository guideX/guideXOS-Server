#ifndef LPVOID
typedef void* LPVOID;
#endif

#include "gcenv.h"
#include "gcheaputilities.h"
#include "thread.h"
#include "threadstore.h"
#include "thread.inl"
#include "threadstore.inl"

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
    uint32_t* gcMode) {
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
