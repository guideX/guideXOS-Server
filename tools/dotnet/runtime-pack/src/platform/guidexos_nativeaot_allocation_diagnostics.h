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
} guidexos_nativeaot_allocation_diagnostics;

#ifdef __cplusplus
}
#endif
