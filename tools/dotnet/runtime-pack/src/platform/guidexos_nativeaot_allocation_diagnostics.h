#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct guidexos_nativeaot_allocation_diagnostics {
    uint32_t schemaVersion;
    uint32_t heapInitialized;
    uint32_t allocationSucceeded;
    uint32_t managedReturnCode;
    uint32_t allocationCount;
    uint32_t rhpNewArrayEntries;
    uint32_t realGcAllocationEntries;
    uint32_t allocationContextRefills;
    uint32_t slowAllocationEntries;
    uint32_t collectionTriggeringEntries;
    uint32_t largeObjectEntries;
    uint32_t pinnedObjectEntries;
    uint32_t collectionRequests;
    uint32_t collectionsEntered;
    uint32_t finalizationScans;
    uint32_t finalizersExecuted;
    uint32_t requestedArrayLength;
    uint32_t requestedObjectSize;
    uint32_t arrayLengthObserved;
    uint32_t zeroByteCount;
    uint32_t patternVerified;
    uint32_t heapOwnershipVerified;
    uint32_t objectAlignmentVerified;
    uint32_t objectLayoutVerified;
    uint32_t objectRangeVerified;
    uint32_t pointerContractFailures;
    uint32_t finalizableObjectCountBefore;
    uint32_t finalizableObjectCountAfter;
    uint32_t reserved;
    uintptr_t allocationContextBefore;
    uintptr_t allocationLimitBefore;
    uintptr_t allocationContextAfter;
    uintptr_t allocationLimitAfter;
    uintptr_t heapBase;
    uintptr_t heapAllocated;
    uintptr_t heapReserved;
    uintptr_t objectAddress;
    uintptr_t arrayData;
    uintptr_t eeType;
    uintptr_t returnedObject;
    uintptr_t gcBytesBefore;
    uintptr_t gcBytesAfter;
    uint32_t gcCountBefore;
    uint32_t gcCountAfter;
    uint32_t gcInProgressBefore;
    uint32_t gcInProgressAfter;
    /*
     * Fixed, nonallocating first-allocation boundary record.  These fields
     * are appended so the established result fields retain their offsets.
     * A single allocating thread writes the record; a watchdog may sample it
     * without taking a runtime lock.
     */
    uint32_t stage;
    uint32_t sequence;
    uint32_t currentThreadId;
    uint32_t gcMode;
    uint32_t waitReason;
    uint32_t failFastReason;
    uint32_t watchdogTicks;
    uint32_t stageReserved;
    uintptr_t currentRip;
    uintptr_t currentRsp;
    uintptr_t runtimeThreadRecord;
    uintptr_t transitionFrame;
    uintptr_t computedObjectSize;
    uintptr_t lastDirectTarget;
    uintptr_t lastIndirectCell;
    uintptr_t lastIndirectTarget;
    uintptr_t lastLockId;
    uintptr_t lastEventId;

    /*
     * Bounded first-subsequent-refill experiment fields.  These are appended
     * to preserve the closed first-allocation record layout.  All values are
     * scalar, fixed-storage diagnostics written by the allocating thread.
     */
    uint32_t managedEntryCount;
    uint32_t allocationRequestCount;
    uint32_t rhpNewArrayCount;
    uint32_t fastAllocationCount;
    uint32_t rarePathCount;
    uint32_t realGcAllocationCount;
    uint32_t allocationContextRefillCount;
    uint32_t slowAllocationCount;
    uint32_t largeObjectCount;
    uint32_t pinnedObjectCount;
    uint32_t collectionRequestCount;
    uint32_t collectionEntryCount;
    uint32_t finalizationScanCount;
    uint32_t managedFinalizerCount;
    uint32_t collectionConsideredCount;
    uint32_t suspensionRequestCount;
    uint32_t gcLockTransitionCount;
    uint32_t helperWakeCount;
    uint32_t refill2Attempted;
    uint32_t refill2Returned;
    uint32_t newContextSupplied;
    uint32_t managedStopObserved;
    uint32_t ownershipModel;
    uint32_t failureReason;
    uint32_t hardAllocationLimit;
    uint32_t reservedRefill;

    uintptr_t derivedObjectSize;
    uintptr_t initialAllocPtr;
    uintptr_t initialAllocLimit;
    uintptr_t initialAvailableBytes;
    uintptr_t expectedFastAllocationCount;
    uintptr_t currentIteration;
    uintptr_t currentObject;
    uintptr_t currentObjectEnd;
    uintptr_t currentAllocPtr;
    uintptr_t currentAllocLimit;
    uintptr_t lastFastObject;
    uintptr_t lastFastObjectEnd;
    uintptr_t refill2Object;
    uintptr_t refill2ObjectEnd;
    uintptr_t refill2AllocPtrBefore;
    uintptr_t refill2AllocLimitBefore;
    uintptr_t refill2RemainingBytesBefore;
    uintptr_t refill2AllocPtrAfter;
    uintptr_t refill2AllocLimitAfter;
    uintptr_t initialSegmentBase;
    uintptr_t initialSegmentAllocated;
    uintptr_t initialSegmentReserved;
    uintptr_t refill2SegmentBase;
    uintptr_t refill2SegmentAllocated;
    uintptr_t refill2SegmentReserved;

    uint32_t zeroValidationFailures;
    uint32_t patternValidationFailures;
    uint32_t layoutFailures;
    uint32_t ownershipFailures;
    uint32_t overlapFailures;
    uint32_t monotonicityFailures;
    uint32_t contextGeometryFailures;
    uint32_t collectionBoundaryFailures;
    uint32_t refill2ContextChanged;
    uint32_t refill2ContextPublished;
    uint32_t finalizerStateValid;
    uint32_t helperStateValid;
    uint32_t sourceSizeValid;
    uint32_t belowLargeObjectThreshold;
    uint32_t primitiveArrayValid;
    uint32_t noPostRefillAllocation;
    uint32_t finalizationQueueScans;
    uint32_t reservedValidation[4];
} guidexos_nativeaot_allocation_diagnostics;

enum {
    GUIDEXOS_NATIVEAOT_ALLOC_STAGE_NONE = 0u,
    GUIDEXOS_NATIVEAOT_ALLOC_STAGE_A00_MANAGED_ENTRY = 0xA00u,
    GUIDEXOS_NATIVEAOT_ALLOC_STAGE_A01_REVERSE_PINVOKE_READY = 0xA01u,
    GUIDEXOS_NATIVEAOT_ALLOC_STAGE_A02_RHP_NEW_ARRAY_ENTRY = 0xA02u,
    GUIDEXOS_NATIVEAOT_ALLOC_STAGE_A03_TYPE_LENGTH_ACCEPTED = 0xA03u,
    GUIDEXOS_NATIVEAOT_ALLOC_STAGE_A04_OBJECT_SIZE_COMPUTED = 0xA04u,
    GUIDEXOS_NATIVEAOT_ALLOC_STAGE_A05_ALLOCATION_CONTEXT_LOADED = 0xA05u,
    GUIDEXOS_NATIVEAOT_ALLOC_STAGE_A06_FAST_PATH_ATTEMPTED = 0xA06u,
    GUIDEXOS_NATIVEAOT_ALLOC_STAGE_A07_RARE_REFILL_ENTERED = 0xA07u,
    GUIDEXOS_NATIVEAOT_ALLOC_STAGE_A08_GC_HEAP_ALLOCATION_ENTERED = 0xA08u,
    GUIDEXOS_NATIVEAOT_ALLOC_STAGE_A09_HEAP_LOCK_REQUESTED = 0xA09u,
    GUIDEXOS_NATIVEAOT_ALLOC_STAGE_A10_HEAP_LOCK_ACQUIRED = 0xA0Au,
    GUIDEXOS_NATIVEAOT_ALLOC_STAGE_A11_SEGMENT_SELECTED = 0xA0Bu,
    GUIDEXOS_NATIVEAOT_ALLOC_STAGE_A12_VM_COMMIT_REQUESTED = 0xA0Cu,
    GUIDEXOS_NATIVEAOT_ALLOC_STAGE_A13_OBJECT_MEMORY_OBTAINED = 0xA0Du,
    GUIDEXOS_NATIVEAOT_ALLOC_STAGE_A14_HEADER_INITIALIZED = 0xA0Eu,
    GUIDEXOS_NATIVEAOT_ALLOC_STAGE_A15_ARRAY_LENGTH_INITIALIZED = 0xA0Fu,
    GUIDEXOS_NATIVEAOT_ALLOC_STAGE_A16_ALLOCATION_RETURNED = 0xA10u,
    GUIDEXOS_NATIVEAOT_ALLOC_STAGE_FAILFAST_REVERSE_PINVOKE = 0xF02u,
    GUIDEXOS_NATIVEAOT_ALLOC_STAGE_FAILFAST_GC_STATE = 0xF08u,
    GUIDEXOS_NATIVEAOT_ALLOC_STAGE_FAILFAST_ALLOCATION = 0xF06u,
};

#ifdef __cplusplus
}
#endif
