#pragma once

#include <stdint.h>

// This structure is proof-gated.  It records observations made by the
// standalone [ThreadStatic] workload; it is not part of the NativeAOT runtime
// storage contract and is never used to service a field access.
struct guidexos_nativeaot_thread_static_diagnostics {
    uint32_t schemaVersion;
    uint32_t primitiveStartCount;
    uint32_t primitiveSuccessCount;
    uint32_t referenceStartCount;
    uint32_t referenceSuccessCount;
    uint32_t primitiveInitialValue;
    uint32_t primitiveAssignedValue;
    uint32_t primitiveReadbackValue;
    uint32_t primitiveMismatchCount;
    uintptr_t referenceAssigned;
    uintptr_t referenceReadback;
    uint32_t referenceIdentityMatch;
    uint32_t referenceObjectValid;
    uintptr_t runtimeThread;
    uint64_t nativeThreadId;
    uintptr_t tlsBlock;
    uintptr_t flsRuntimeIdentity;
    uintptr_t threadStaticStorage;
    uintptr_t inlinedRootList;
    uintptr_t inlinedStorageBase;
    uint32_t inlinedStorageSize;
    uintptr_t inlinedTypeManager;
    uint32_t registeredThreadCount;
    uint32_t storageInitializationRequests;
    uint32_t storageInitializationEntries;
    uint32_t storageInitializationCompletions;
    uint32_t storageAllocationCount;
    uint32_t repeatedLookupCount;
    uint32_t duplicateStorageCount;
    uint32_t writeBarrierRequests;
    uint32_t moduleInitializationRequests;
    uint32_t moduleInitializationEntries;
    uint32_t moduleInitializationCompletions;
    uint32_t unexpectedGcRequests;
    uint32_t collectionEntries;
    uint32_t suspensionRequests;
    uint32_t faultCount;
    uint32_t finalMarker;
    int32_t managedResult;
};
