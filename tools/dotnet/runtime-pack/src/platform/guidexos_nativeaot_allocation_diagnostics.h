#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    GUIDEXOS_NATIVEAOT_MAX_REFILL_HISTORY = 128u,
};

/*
 * One bounded record is written for each rare-path context refill.  The
 * record deliberately contains only scalar values so the allocator-side
 * probe cannot allocate, wait, or call back into the PAL while publishing
 * evidence.
 */
typedef struct guidexos_nativeaot_refill_history_entry {
    uint32_t ordinal;
    uint32_t allocationOrdinal;
    uint32_t fastPath;
    uint32_t vmCommitObserved;
    uint32_t segmentChanged;
    uint32_t boundaryType;
    uint32_t collectionBefore;
    uint32_t collectionAfter;
    uint32_t traceStart;
    uint32_t traceEnd;
    uint32_t reserved0;
    uint32_t reserved1;
    uintptr_t contextBefore;
    uintptr_t limitBefore;
    uintptr_t remainingBefore;
    uintptr_t objectAddress;
    uintptr_t objectEnd;
    uintptr_t contextAfter;
    uintptr_t limitAfter;
    uintptr_t segmentIdentity;
    uintptr_t segmentBase;
    uintptr_t segmentAllocated;
    uintptr_t segmentCommitted;
    uintptr_t segmentReserved;
    uintptr_t commitAddress;
    uintptr_t commitRequested;
    uintptr_t commitActual;
    uintptr_t committedBefore;
    uintptr_t committedAfter;
} guidexos_nativeaot_refill_history_entry;

enum {
    GUIDEXOS_NATIVEAOT_MAX_ALLOCATION_CONTEXT_SNAPSHOTS = 8u,
    GUIDEXOS_NATIVEAOT_MAX_OBJECT_HISTORY = 64u,
    GUIDEXOS_NATIVEAOT_MAX_ROOT_THREAD_RECORDS = 32u,
    GUIDEXOS_NATIVEAOT_MAX_CANDIDATE_SLOTS = 8u,
};

typedef struct guidexos_nativeaot_allocation_context_snapshot {
    uintptr_t contextIdentity;
    uintptr_t owningRuntimeThread;
    uintptr_t owningGcHeap;
    uintptr_t allocPtr;
    uintptr_t allocLimit;
    uintptr_t allocationStart;
    uintptr_t allocationSize;
    uintptr_t unusedTailBytes;
    uintptr_t segmentIdentity;
    uintptr_t segmentBase;
    uintptr_t segmentAllocated;
    uintptr_t segmentCommitted;
    uintptr_t segmentReserved;
    uintptr_t generationAllocationStart;
    uintptr_t heapAllocatedBytes;
    uintptr_t allocBytes;
    uintptr_t allocBytesUoh;
    uint32_t active;
    uint32_t current;
    uint32_t retired;
    uint32_t cleared;
    uint32_t segmentFlags;
    uint32_t segmentGeneration;
    uint32_t reserved0;
    uint32_t reserved1;
} guidexos_nativeaot_allocation_context_snapshot;

typedef struct guidexos_nativeaot_object_history_entry {
    uintptr_t address;
    uintptr_t end;
    uintptr_t eeType;
    uint32_t length;
    uint32_t sequence;
    uint32_t zeroByteCount;
    uint32_t patternValid;
    uint32_t beforeValid;
    uint32_t afterValid;
    uint32_t sentinel;
    uint32_t reserved0;
    uint32_t reserved1;
} guidexos_nativeaot_object_history_entry;

typedef struct guidexos_nativeaot_root_thread_record {
    uint32_t enumerationOrdinal;
    uint32_t registered;
    uint32_t initialized;
    uint32_t lifecycleState;
    uint32_t threadStateFlags;
    uint32_t cooperative;
    uint32_t preemptive;
    uint32_t gcSpecial;
    uint32_t included;
    uint32_t excluded;
    uint32_t inclusionReason;
    uint32_t collectionInitiatorMatch;
    uint32_t currentThreadMatch;
    uint32_t lockOwnerMatch;
    uint32_t reserved0;
    uint32_t reserved1;
    uintptr_t runtimeThread;
    uintptr_t nativeThreadId;
    uintptr_t stackLow;
    uintptr_t stackHigh;
    uintptr_t allocationContext;
    uintptr_t nextThread;
} guidexos_nativeaot_root_thread_record;

typedef struct guidexos_nativeaot_candidate_slot_record {
    uint32_t ordinal;
    uint32_t loadCount;
    uint32_t duplicateLoadCount;
    uint32_t rawRootFlags;
    uint32_t rootKind;
    uint32_t valueIsNull;
    uint32_t knownAddressMatch;
    uint32_t exactSelectedSentinelMatch;
    uintptr_t slotAddress;
    uintptr_t rawValue;
    uintptr_t callbackIdentity;
    uintptr_t scanContextIdentity;
} guidexos_nativeaot_candidate_slot_record;

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

    /*
     * Appended multi-refill/segment-boundary evidence.  The original
     * first-allocation and first-refill records above are intentionally not
     * reordered or resized.
     */
    uint32_t experimentMode;
    uint32_t selectedArrayLength;
    uint32_t vmTraceStartCount;
    uint32_t vmTraceCursor;
    uint32_t vmTraceEndCount;
    uint32_t vmCommitEventCount;
    uint32_t heapCommitEventCount;
    uint32_t segmentTransitionCount;
    uint32_t refillHistoryCount;
    uint32_t refillHistoryOverflow;
    uint32_t boundaryType;
    uint32_t boundaryAllocationOrdinal;
    uint32_t boundaryRefillOrdinal;
    uint32_t boundaryStopObserved;
    uint32_t boundaryCommitValidated;
    uint32_t boundarySegmentValidated;
    uint32_t completionStatus;
    uint32_t lastSegmentGeneration;
    uint32_t lastSegmentFlags;
    uint32_t initialHeapCommitObserved;
    uint32_t initialHeapCommitEventCount;
    uint32_t hardRefillLimit;
    uint32_t hardCommitLimit;
    uint32_t hardSegmentTransitionLimit;
    uint32_t collectionDecisionObserved;
    uint32_t collectionDecisionPath;
    uint32_t stopReason;
    uint32_t currentSegmentFlags;
    uint32_t currentSegmentGeneration;
    uint32_t reservedBoundary[4];

    uintptr_t initialSegmentIdentity;
    uintptr_t initialSegmentCommitted;
    uintptr_t initialSegmentGeneration;
    uintptr_t currentSegmentIdentity;
    uintptr_t currentSegmentCommitted;
    uintptr_t boundarySegmentIdentity;
    uintptr_t boundarySegmentBase;
    uintptr_t boundarySegmentAllocated;
    uintptr_t boundarySegmentCommitted;
    uintptr_t boundarySegmentReserved;
    uintptr_t boundaryCommitAddress;
    uintptr_t boundaryCommitRequested;
    uintptr_t boundaryCommitActual;
    uintptr_t boundaryCommittedBefore;
    uintptr_t boundaryCommittedAfter;
    uintptr_t boundaryObjectAddress;
    uintptr_t boundaryObjectEnd;
    uintptr_t boundaryAllocationContextBefore;
    uintptr_t boundaryAllocationContextAfter;
    uintptr_t boundaryAllocationLimitBefore;
    uintptr_t boundaryAllocationLimitAfter;
    uintptr_t initialHeapCommitTraceIndex;
    uintptr_t initialHeapCommitAddress;
    uintptr_t initialHeapCommitRequested;
    uintptr_t initialHeapCommitActual;
    uintptr_t initialHeapCommittedBefore;
    uintptr_t initialHeapCommittedAfter;
    uintptr_t currentSegmentBase;
    uintptr_t currentSegmentAllocated;
    uintptr_t currentSegmentReserved;
    uintptr_t collectionDecisionSegment;
    uintptr_t collectionDecisionAllocPtr;
    uintptr_t collectionDecisionAllocLimit;
    uintptr_t collectionDecisionIteration;
    uintptr_t collectionDecisionRemainingBytes;

    guidexos_nativeaot_refill_history_entry refillHistory[
        GUIDEXOS_NATIVEAOT_MAX_REFILL_HISTORY];

    /*
     * First-collection-boundary proof fields. These are append-only so the
     * established first-allocation/refill/segment records retain their
     * offsets. The proof stops in GCToEEInterface::SuspendEE before the EE
     * thread store or heap state is mutated.
     */
    uint32_t firstCollectionBoundaryMarker;
    uint32_t firstCollectionRequestCount;
    uint32_t firstCollectionEntryCount;
    uint32_t requestedGeneration;
    uint32_t collectionReason;
    uint32_t collectionBlockingMode;
    uint32_t collectionCompactingMode;
    uint32_t suspensionEntryCount;
    uint32_t restartResumeCount;
    uint32_t heapMutationStarted;
    uint32_t managedExecutionResumed;
    uint32_t firstUnsupportedContract;
    uint32_t safeStopObserved;
    uint32_t collectionRequestAllocationOrdinal;
    uint32_t collectionEntryAllocationOrdinal;
    uint32_t sentinelValidationCount;
    uint32_t sentinelValidationFailures;
    uint32_t liveSentinelCount;
    uint32_t reservedFirstCollection[5];

    uintptr_t collectionEntryThread;
    uintptr_t collectionEntryHeap;
    uintptr_t collectionEntryAllocPtr;
    uintptr_t collectionEntryAllocLimit;
    uintptr_t collectionEntryObjectSize;
    uintptr_t collectionEntrySegmentIdentity;
    uintptr_t collectionEntrySegmentCommitted;
    uintptr_t collectionEntrySegmentReserved;

    /*
     * Single-mutator SuspendEE proof fields. These are append-only so the
     * established allocation, refill, segment, and first-collection records
     * retain their offsets. The fields describe the real locked runtime
     * ThreadStore path; they are not an application-scoped replacement.
     */
    uint32_t singleThreadSuspendEeMarker;
    uint32_t suspendEeEntryCount;
    uint32_t suspendEeReturnCount;
    uint32_t suspendEeSuspensionCount;
    uint32_t suspendEeReason;
    uint32_t suspendEeEpoch;
    uint32_t threadStoreLockRequestCount;
    uint32_t threadStoreLockAcquisitionCount;
    uint32_t threadStoreLockFailureCount;
    uint32_t threadStoreUnlockCount;
    uint32_t threadStoreLockRecursionDepth;
    uint32_t threadStoreRegistryMutationAttemptsWhileLocked;
    uint32_t threadStoreAdapterRegistrationCount;
    uint32_t registeredManagedThreadCount;
    uint32_t expectedOtherMutators;
    uint32_t stoppedOtherMutators;
    uint32_t currentThreadRegistered;
    uint32_t currentThreadIsInitiator;
    uint32_t currentAndInitiatorMatch;
    uint32_t currentThreadExemptFromPeerStop;
    uint32_t managedEntryProhibited;
    uint32_t eeSuspended;
    uint32_t currentThreadStateFlagsBefore;
    uint32_t currentThreadStateFlagsDuring;
    uint32_t currentThreadCooperativeBefore;
    uint32_t currentThreadCooperativeDuring;
    uint32_t nextBoundary;
    uint32_t rootEnumerationRequestCount;
    uint32_t rootEnumerationEntryCount;
    uint32_t stackWalkRequestCount;
    uint32_t stackWalkEntryCount;
    uint32_t handleScanRequestCount;
    uint32_t handleScanEntryCount;
    uint32_t restartRequestCount;
    uint32_t restartEntryCount;
    uint32_t managedResumeCount;
    uint32_t suspendEeHeapMutationStarted;
    uint32_t suspendEeSafeStopObserved;
    uint32_t suspendEeStopReason;
    uint32_t suspendEeGcMode;
    uint32_t reservedSingleThreadSuspendEe[5];

    uintptr_t suspendEeCurrentNativeThreadId;
    uintptr_t suspendEeCurrentRuntimeThread;
    uintptr_t suspendEeInitiatingRuntimeThread;
    uintptr_t suspendEeSuspensionOwner;
    uintptr_t threadStoreLockOwner;
    uintptr_t threadStoreLockOwnerNativeThreadId;
    uintptr_t suspendEeCurrentStackLow;
    uintptr_t suspendEeCurrentStackHigh;
    uintptr_t suspendEeCurrentTransitionFrame;
    uintptr_t suspendEeCollectionInitiatorNativeThreadId;

    /*
     * Bounded allocation-context fixup and first-root-boundary proof fields.
     * These are append-only. They describe the source-defined fixup contract
     * and the pre-dispatch root boundary; they do not implement root scanning.
     */
    uint32_t allocationContextFixupRequestCount;
    uint32_t allocationContextFixupEntryCount;
    uint32_t allocationContextFixupCompletionCount;
    uint32_t allocationContextFixupMode;
    uint32_t allocationContextFixupContextsVisited;
    uint32_t allocationContextFixupContextsChanged;
    uint32_t allocationContextFixupContextsActiveBefore;
    uint32_t allocationContextFixupContextsActiveAfter;
    uint32_t allocationContextFixupContextsCleared;
    uint32_t allocationContextFixupContextsRetired;
    uint32_t allocationContextFixupEnumerationComplete;
    uint32_t allocationContextFixupInvariantFailures;
    uint32_t allocationContextMetadataMutationStarted;
    uint32_t allocationContextMetadataMutationCompleted;
    uint32_t segmentBookkeepingMutationCount;
    uint32_t objectMemoryMutationStarted;
    uint32_t objectValidationBeforeFixupCount;
    uint32_t objectValidationAfterFixupCount;
    uint32_t objectValidationFailuresBeforeFixup;
    uint32_t objectValidationFailuresAfterFixup;
    uint32_t objectOverlapFailuresAfterFixup;
    uint32_t objectBoundaryFailuresAfterFixup;
    uint32_t objectAlignmentFailuresAfterFixup;
    uint32_t objectTypeLayoutFailuresAfterFixup;
    uint32_t objectPatternFailuresAfterFixup;
    uint32_t objectAddressChangesAfterFixup;
    uint32_t duplicateObjectAddressFailures;
    uint32_t objectTailClassificationFailures;
    uint32_t sentinelChecksBeforeFixup;
    uint32_t sentinelChecksAfterFixup;
    uint32_t sentinelChecksAtRootBoundary;
    uint32_t rootPhaseRequestCount;
    uint32_t rootDispatcherEntryCount;
    uint32_t rootCategorySelected;
    uint32_t rootProviderRequestCount;
    uint32_t rootProviderEntryCount;
    uint32_t firstRootCandidateCount;
    uint32_t rootCallbacksDelivered;
    uint32_t promotionCallbacksDelivered;
    uint32_t markingEntryCount;
    uint32_t sweepingEntryCount;
    uint32_t compactionEntryCount;
    uint32_t relocationEntryCount;
    uint32_t stackBoundRequestCount;
    uint32_t stackScanEntryCount;
    uint32_t staticRootRequestCount;
    uint32_t staticRootEntryCount;
    uint32_t handleRootRequestCount;
    uint32_t handleRootEntryCount;
    uint32_t finalizerRootRequestCount;
    uint32_t finalizerRootEntryCount;
    uint32_t rootBoundaryInvariantFailures;
    uint32_t allocationContextFixupSafeStopObserved;
    uint32_t allocationContextFixupStopReason;
    uint32_t allocationContextFixupRootBoundaryMarker;
    uint32_t reservedAllocationContextProof[5];

    uintptr_t validAllocatedExtentBeforeFixup;
    uintptr_t validAllocatedExtentAfterFixup;
    uintptr_t unusedTailBytesBeforeFixup;
    uintptr_t unusedTailBytesAfterFixup;
    uintptr_t heapAllocationCounterBeforeFixup;
    uintptr_t heapAllocationCounterAfterFixup;
    uintptr_t allocationPointerBeforeFixup;
    uintptr_t allocationLimitBeforeFixup;
    uintptr_t allocationPointerAfterFixup;
    uintptr_t allocationLimitAfterFixup;
    uintptr_t segmentAllocatedBeforeFixup;
    uintptr_t segmentAllocatedAfterFixup;
    uintptr_t segmentCommittedBeforeFixup;
    uintptr_t segmentCommittedAfterFixup;
    uintptr_t segmentReservedBeforeFixup;
    uintptr_t segmentReservedAfterFixup;
    uintptr_t rootBoundaryFunction;
    uintptr_t firstRootProviderFunction;

    /*
     * Bounded first-per-thread-root-provider proof fields.  These fields are
     * proof-only and stop before a GC candidate value is loaded.  They record
     * the real ThreadStore iterator, the actual registered thread, and the
     * first selected provider without implementing root enumeration.
     */
    uint32_t gcScanRootsRequestCount;
    uint32_t gcScanRootsEntryCount;
    uint32_t foreachThreadRequestCount;
    uint32_t foreachThreadEntryCount;
    uint32_t threadIteratorInitializationCount;
    uint32_t threadIteratorCompletionCount;
    uint32_t rootPerThreadDispatchRequestCount;
    uint32_t rootPerThreadDispatchEntryCount;
    uint32_t rootProviderSourceOrderCategory;
    uint32_t rootProviderRuntimeCategory;
    uint32_t rootProviderSkipCount;
    uint32_t rootProviderSkipReason;
    uint32_t metadataContainerCount;
    uint32_t candidateMetadataLocationCount;
    uint32_t candidateValueReadCount;
    uint32_t rootCandidateDiscoveryCount;
    uint32_t rootProviderInvariantFailures;
    uint32_t rootProviderSafeStopObserved;
    uint32_t rootProviderStopReason;
    uint32_t registeredThreadCountBeforeRoot;
    uint32_t registeredThreadCountAfterRoot;
    uint32_t enumeratedThreadCount;
    uint32_t includedThreadCount;
    uint32_t excludedThreadCount;
    uint32_t duplicateThreadCount;
    uint32_t threadListIntegrityFailures;
    uint32_t threadRegistryMutationCountBeforeRoot;
    uint32_t threadRegistryMutationCountAfterRoot;
    uint32_t stackBoundsRequested;
    uint32_t stackScanningStarted;
    uint32_t threadStaticStorageRequested;
    uint32_t threadStaticScanningStarted;
    uint32_t frameChainRequested;
    uint32_t frameScanningStarted;
    uint32_t exceptionRootRequested;
    uint32_t explicitThreadRootRequested;
    uint32_t rootProviderFunctionCode;
    uint32_t rootProviderMetadataKind;
    uint32_t rootThreadRecordCount;
    uint32_t rootThreadRegistryGenerationBefore;
    uint32_t rootThreadRegistryGenerationAfter;
    uint32_t reservedFirstPerThreadRootProvider[3];

    uintptr_t rootCurrentThreadIdentity;
    uintptr_t rootEnumeratedThreadIdentity;
    uintptr_t rootCollectionInitiatorIdentity;
    uintptr_t rootLockOwnerIdentity;
    uintptr_t rootThreadListHeadBefore;
    uintptr_t rootThreadListTailBefore;
    uintptr_t rootThreadListHeadAfter;
    uintptr_t rootThreadListTailAfter;
    uintptr_t firstRootProviderThread;
    uintptr_t firstRootProviderMetadataContainer;
    uintptr_t firstRootCandidateMetadataLocation;
    uintptr_t rootProviderEntryFunction;

    guidexos_nativeaot_root_thread_record rootThreadRecords[
        GUIDEXOS_NATIVEAOT_MAX_ROOT_THREAD_RECORDS];

    /*
     * Bounded first-root-candidate-load proof fields.  The candidate is an
     * opaque pointer-width value loaded once from the real slot passed through
     * EnumGcRef.  These fields deliberately distinguish the slot load from
     * any later candidate-pointee dereference or GC interpretation.
     */
    uint32_t candidateLoadRequestCount;
    uint32_t candidateLoadEntryCount;
    uint32_t candidateMachineWordLoadCount;
    uint32_t candidateDuplicateLoadCount;
    uint32_t candidateLoadFaultCount;
    uint32_t candidateLoadSuccess;
    uint32_t candidateSlotWidth;
    uint32_t candidateSlotAlignment;
    uint32_t candidateSlotMapped;
    uint32_t candidateSlotCommitted;
    uint32_t candidateSlotWritableContract;
    uint32_t candidateSlotStable;
    uint32_t candidateSlotExpectedThreadStorage;
    uint32_t candidateSlotOverlapsManagedHeap;
    uint32_t candidateSlotOverlapsRuntimeThread;
    uint32_t candidateSlotOverlapsAllocationContext;
    uint32_t candidateSlotOverlapsNativeStack;
    uint32_t candidateSlotOverlapsOtherKnownRegion;
    uint32_t candidateValueIsNull;
    uint32_t candidateKnownAddressMatch;
    uint32_t candidatePointeeDereferenceCount;
    uint32_t candidateHeapMembershipTestCount;
    uint32_t candidateObjectHeaderInspectionCount;
    uint32_t candidateMethodTableInspectionCount;
    uint32_t candidateRootFlagApplicationCount;
    uint32_t candidateRootCandidateDiscoveryCount;
    uint32_t candidateRootCallbacksDelivered;
    uint32_t candidatePromotionCallbacksDelivered;
    uint32_t candidateObjectValidationBeforeLoadCount;
    uint32_t candidateObjectValidationAfterLoadCount;
    uint32_t candidateObjectValidationAtStopCount;
    uint32_t candidateSafeStopObserved;
    uint32_t candidateStopReason;
    uint32_t candidateRawRootFlags;
    uint32_t candidateRootKind;
    uint32_t candidateProviderFunctionCode;
    uint32_t reservedFirstRootCandidateLoad[3];

    uintptr_t candidateSlotAddress;
    uintptr_t candidateSlotOffsetFromThread;
    uintptr_t candidateRawValue;
    uintptr_t candidateCallbackIdentity;
    uintptr_t candidateScanContextIdentity;
    uintptr_t candidateProviderThreadIdentity;
    uintptr_t candidateOwnerThreadIdentity;
    uintptr_t candidateMetadataContainerIdentity;
    uintptr_t candidateLoadAddress;

    /* Managed proof-root evidence. */
    uint32_t threadStaticProofAssignmentCount;
    uint32_t threadStaticProofClearCount;
    uint32_t threadStaticProofReadbackCount;
    uint32_t threadStaticProofManagedAssignmentValid;
    uint32_t threadStaticProofManagedReadbackValid;
    uint32_t threadStaticProofInitializationIndicator;
    uint32_t threadStaticProofSentinelOrdinal;
    uint32_t threadStaticProofReadbackExactMatch;
    uintptr_t threadStaticProofSentinelAddress;
    uintptr_t threadStaticProofSentinelSize;
    uintptr_t threadStaticProofManagedReadbackAddress;
    uintptr_t threadStaticProofManagedThread;
    uintptr_t threadStaticProofStorageAddress;
    uint32_t runtimeThreadStaticStorageAllocationCount;
    uint32_t runtimeThreadStaticStoragePublicationCount;
    uint32_t runtimeThreadStaticStorageObjectValid;
    uint32_t reservedThreadStaticRuntime[1];
    uintptr_t runtimeThreadStaticStorageObjectAddress;
    uintptr_t runtimeThreadStaticInlinedRootAddress;

    /* Bounded non-null-root candidate sequence. */
    uint32_t candidateSlotVisitCount;
    uint32_t candidateNullCount;
    uint32_t candidateNonNullCount;
    uint32_t candidateProofRootObserved;
    uint32_t candidateMatchesProofRoot;
    uint32_t candidateUnexpectedNonNull;
    uint32_t candidateBoundReached;
    uint32_t candidateProviderTerminated;
    uint32_t candidateFirstNonNullKnownAddressMatch;
    uint32_t candidateMatchesStorageObject;
    uint32_t reservedNonNullRootProof[3];
    uintptr_t candidateFirstNonNullValue;
    uintptr_t candidateFirstNonNullSlot;
    uintptr_t candidateExpectedSentinelAddress;
    uintptr_t candidateExpectedStorageObjectAddress;
    uint32_t rootCondemnedGeneration;
    uint32_t rootMaximumGeneration;
    uint32_t rootScanContextPromotion;
    uint32_t rootScanContextConcurrent;
    uintptr_t rootScanContextIdentity;
    guidexos_nativeaot_candidate_slot_record candidateSlotRecords[
        GUIDEXOS_NATIVEAOT_MAX_CANDIDATE_SLOTS];

    /*
     * Proof-only first real root-callback entry evidence.  The call-site
     * values are recorded before the indirect call in GcEnumObject.  The
     * entry values are recorded by the real GCHeap::Promote body before its
     * locked source loads *ppObject.  No callback return is permitted in
     * this bounded proof.
     */
    uint32_t callbackRequestCount;
    uint32_t callbackCallSiteEntryCount;
    uint32_t callbackInvocationCount;
    uint32_t callbackEntryCount;
    uint32_t callbackReturnCount;
    uint32_t duplicateCallbackInvocationCount;
    uint32_t callbackRootSlotLoadCount;
    uint32_t callbackNullTestCount;
    uint32_t callbackNullTestNonNullCount;
    uint32_t callbackContextFieldReadCount;
    uint32_t callbackCandidateClassificationStartCount;
    uint32_t callbackGenerationClassificationStartCount;
    uint32_t callbackGenerationQueryStartCount;
    uint32_t callbackCondemnedGenerationComparisonCount;
    uint32_t callbackHeapMembershipTestCount;
    uint32_t callbackSegmentLookupCount;
    uint32_t callbackObjectHeaderReadCount;
    uint32_t callbackMethodTableReadCount;
    uint32_t callbackPromotionStartCount;
    uint32_t callbackPromotionCount;
    uint32_t callbackMarkingStartCount;
    uint32_t callbackGraphTraversalCount;
    uint32_t callbackMarkStateWriteCount;
    uint32_t callbackPromotionStateWriteCount;
    uint32_t callbackObjectMemoryMutationCount;
    uint32_t callbackGcMetadataMutationCount;
    uint32_t callbackSegmentMetadataMutationCount;
    uint32_t callbackSafeStopObserved;
    uint32_t callbackSafeStopReason;
    uint32_t callbackFirstSemanticOperation;
    uint32_t callbackEntryCurrentGeneration;
    uint32_t callbackEntryCondemnedGeneration;
    uint32_t callbackEntryPromotion;
    uint32_t callbackEntryConcurrent;
    uint32_t callbackEntryThreadStoreLockHeld;
    uint32_t callbackEntryEeSuspended;
    uint32_t callbackEntryManagedEntryProhibited;
    uint32_t callbackExpectedRootFlags;
    uint32_t callbackActualRootFlags;
    uint32_t callbackEntryArgumentsMatch;
    uint32_t callbackRootSlotMatchesExpected;
    uint32_t callbackRawRootMatchesStorage;
    uint32_t callbackScanContextMatchesExpected;
    uint32_t callbackFlagsMatchExpected;
    uint32_t reservedFirstRootCallbackEntry[3];

    uintptr_t callbackSiteRootSlot;
    uintptr_t callbackSiteRawRootValue;
    uintptr_t callbackSiteScanContext;
    uintptr_t callbackSiteCallbackIdentity;
    uintptr_t callbackSiteReturnAddress;
    uintptr_t callbackEntryAddress;
    uintptr_t callbackEntryReturnAddress;
    uintptr_t callbackEntryStackPointer;
    uintptr_t callbackRawArgument1Rcx;
    uintptr_t callbackRawArgument2Rdx;
    uintptr_t callbackRawArgument3R8;
    uintptr_t callbackNormalizedArgument1;
    uintptr_t callbackNormalizedArgument2;
    uintptr_t callbackNormalizedArgument3;
    uintptr_t callbackRootSlot;
    uintptr_t callbackRootRawValue;
    uintptr_t callbackRootSlotLoadedValue;
    uintptr_t callbackContextAddress;
    uintptr_t callbackContextThreadUnderCrawl;
    uintptr_t callbackContextStackLimit;
    uintptr_t callbackEntryManagedThread;
    uintptr_t callbackEntryLockOwner;
    uintptr_t callbackEntryCurrentThread;
    uintptr_t callbackEntryScanContextFieldThreadNumberAddress;
    uintptr_t callbackEntryScanContextFieldThreadNumberValue;
    uintptr_t callbackEntryScanContextFieldThreadCountAddress;
    uintptr_t callbackEntryScanContextFieldThreadCountValue;
    uintptr_t callbackEntryScanContextFieldPromotionAddress;
    uintptr_t callbackEntryScanContextFieldConcurrentAddress;

    /* Proof-only first real-root managed-range membership classification. */
    uint32_t membershipRequestCount;
    uint32_t membershipEntryCount;
    uint32_t membershipCompletionCount;
    uint32_t membershipReturnCount;
    uint32_t membershipDuplicateCheckCount;
    uint32_t membershipObjectDereferenceCount;
    uint32_t membershipLowerComparisonEvaluated;
    uint32_t membershipUpperComparisonEvaluated;
    uint32_t membershipLowerComparisonResult;
    uint32_t membershipUpperComparisonResult;
    uint32_t membershipResult;
    uint32_t membershipSourceBranch;
    uint32_t membershipObjectMatchesCallbackRoot;
    uint32_t membershipPostCheckBoundaryCount;
    uint32_t membershipSafeStopObserved;
    uint32_t membershipSafeStopReason;
    uint32_t membershipHeapFieldReadCount;
    uint32_t membershipSegmentLookupCount;
    uint32_t membershipSegmentLookupSucceeded;
    uint32_t reservedFirstRootMembershipClassification[1];

    uintptr_t membershipObjectInput;
    uintptr_t membershipCallbackLoadedRoot;
    uintptr_t membershipLowerBound;
    uintptr_t membershipUpperBound;
    uintptr_t membershipHeapIdentity;
    uintptr_t membershipSegmentIdentity;
    uintptr_t membershipCompletionReturnAddress;
    uintptr_t membershipPostCheckReturnAddress;

    /* Proof-only first real-root Workstation heap-resolution evidence. */
    uint32_t heapResolutionRequestCount;
    uint32_t heapResolutionEntryCount;
    uint32_t heapResolutionCompletionCount;
    uint32_t heapResolutionDuplicateCount;
    uint32_t heapResolutionFailureCount;
    uint32_t heapResolutionSucceeded;
    uint32_t heapResolutionMembershipPassed;
    uint32_t heapResolutionThreadNumber;
    uint32_t heapResolutionHeapNumber;
    uint32_t heapResolutionTotalHeapCount;
    uint32_t heapResolutionObjectAddressConsulted;
    uint32_t heapResolutionThreadStateConsulted;
    uint32_t heapResolutionHeapTableReadCount;
    uint32_t heapResolutionSegmentMapReadCount;
    uint32_t heapResolutionBrickCardReadCount;
    uint32_t heapResolutionRangeReadCount;
    uint32_t heapResolutionFailureReason;
    uint32_t heapResolutionSafeStopObserved;
    uint32_t heapResolutionSafeStopReason;
    uintptr_t heapResolutionObjectInput;
    uintptr_t heapResolutionThreadHeap;
    uintptr_t heapResolutionHeapIdentity;
    uintptr_t heapResolutionHeapTableIdentity;
    uintptr_t heapResolutionHeapTableSlot;
    uintptr_t heapResolutionSegmentIdentity;
    uintptr_t heapResolutionAllocationContextHeap;
    uintptr_t heapResolutionCompletionReturnAddress;

    /* Proof-only first real-root condemned-generation membership decision. */
    uint32_t workstationMultipleHeapsEnabled;
    uint32_t workstationSingleHeapSentinelValid;
    uint32_t condemnedCheckRequestCount;
    uint32_t condemnedCheckEntryCount;
    uint32_t condemnedCheckCompletionCount;
    uint32_t condemnedCheckReturnCount;
    uint32_t condemnedCheckDuplicateCount;
    uint32_t condemnedCheckObjectDereferenceCount;
    uint32_t condemnedCheckGenerationQueryStartCount;
    uint32_t condemnedCheckGenerationQueryCompletionCount;
    uint32_t condemnedCheckGenerationTableReadCount;
    uint32_t condemnedCheckSegmentLookupCount;
    uint32_t condemnedCheckObjectHeaderReadCount;
    uint32_t condemnedCheckMethodTableReadCount;
    uint32_t condemnedCheckResult;
    uint32_t condemnedCheckSourceBranch;
    uint32_t condemnedCheckSafeStopObserved;
    uint32_t condemnedCheckSafeStopReason;
    uint32_t condemnedCheckHeapResolutionInputMatch;
    uint32_t condemnedCheckCallbackRootInputMatch;
    uint32_t condemnedCheckMembershipInputMatch;
    uint32_t condemnedCheckStorageObjectInputMatch;
    uint32_t condemnedCheckGenerationInputValid;
    uint32_t condemnedCheckGenerationTableEntry;
    uint32_t condemnedCheckGeneration;
    uint32_t condemnedCheckCondemnedGeneration;
    uint32_t condemnedCheckMaximumGeneration;
    uint32_t condemnedCheckMinimumSegmentSizeShift;
    uint32_t reservedFirstRootCondemnedDecision[2];

    uintptr_t condemnedCheckObjectInput;
    uintptr_t condemnedCheckLowerBound;
    uintptr_t condemnedCheckUpperBound;
    uintptr_t condemnedCheckGenerationTableIdentity;
    uintptr_t condemnedCheckGenerationTableIndex;
    uintptr_t condemnedCheckSegmentIdentity;
    uintptr_t condemnedCheckCompletionReturnAddress;
    uintptr_t condemnedCheckSafeStopReturnAddress;

    guidexos_nativeaot_allocation_context_snapshot allocationContextFixupBefore[
        GUIDEXOS_NATIVEAOT_MAX_ALLOCATION_CONTEXT_SNAPSHOTS];
    guidexos_nativeaot_allocation_context_snapshot allocationContextFixupAfter[
        GUIDEXOS_NATIVEAOT_MAX_ALLOCATION_CONTEXT_SNAPSHOTS];
    guidexos_nativeaot_object_history_entry objectHistory[
        GUIDEXOS_NATIVEAOT_MAX_OBJECT_HISTORY];
    uint32_t allocationContextBeforeCount;
    uint32_t allocationContextAfterCount;
    uint32_t objectHistoryCount;
    uint32_t objectHistoryOverflow;
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
    GUIDEXOS_NATIVEAOT_ALLOC_STAGE_S00_ALLOCATION_REQUEST = 0xB00u,
    GUIDEXOS_NATIVEAOT_ALLOC_STAGE_S01_FAST_CAPACITY_FAILURE = 0xB01u,
    GUIDEXOS_NATIVEAOT_ALLOC_STAGE_S02_RARE_PATH_ENTERED = 0xB02u,
    GUIDEXOS_NATIVEAOT_ALLOC_STAGE_S03_GC_HEAP_ALLOC_ENTERED = 0xB03u,
    GUIDEXOS_NATIVEAOT_ALLOC_STAGE_S04_CURRENT_SEGMENT_INSPECTED = 0xB04u,
    GUIDEXOS_NATIVEAOT_ALLOC_STAGE_S05_CURRENT_SEGMENT_UNSUITABLE = 0xB05u,
    GUIDEXOS_NATIVEAOT_ALLOC_STAGE_S06_COLLECTION_DECISION_EVALUATED = 0xB06u,
    GUIDEXOS_NATIVEAOT_ALLOC_STAGE_S07_NEW_SEGMENT_SEARCH = 0xB07u,
    GUIDEXOS_NATIVEAOT_ALLOC_STAGE_S08_EXISTING_SEGMENT_SELECTED = 0xB08u,
    GUIDEXOS_NATIVEAOT_ALLOC_STAGE_S09_NEW_SEGMENT_RESERVE = 0xB09u,
    GUIDEXOS_NATIVEAOT_ALLOC_STAGE_S10_NEW_SEGMENT_COMMIT = 0xB0Au,
    GUIDEXOS_NATIVEAOT_ALLOC_STAGE_S11_SEGMENT_METADATA_INITIALIZED = 0xB0Bu,
    GUIDEXOS_NATIVEAOT_ALLOC_STAGE_S12_SEGMENT_LINKED = 0xB0Cu,
    GUIDEXOS_NATIVEAOT_ALLOC_STAGE_S13_ALLOCATION_CONTEXT_PUBLISHED = 0xB0Du,
    GUIDEXOS_NATIVEAOT_ALLOC_STAGE_S14_STOP_OBJECT_RETURNED = 0xB0Eu,
    GUIDEXOS_NATIVEAOT_ALLOC_STAGE_F20_FIRST_COLLECTION_SAFE_STOP = 0xF20u,
    GUIDEXOS_NATIVEAOT_ALLOC_STAGE_F21_SINGLE_THREAD_SUSPEND_EE_SAFE_STOP = 0xF21u,
    GUIDEXOS_NATIVEAOT_ALLOC_STAGE_F22_ALLOCATION_CONTEXT_FIXUP_ROOT_BOUNDARY_SAFE_STOP = 0xF22u,
    GUIDEXOS_NATIVEAOT_ALLOC_STAGE_F23_FIRST_PER_THREAD_ROOT_PROVIDER_SAFE_STOP = 0xF23u,
    GUIDEXOS_NATIVEAOT_ALLOC_STAGE_F24_FIRST_NON_NULL_ROOT_CALLBACK_BOUNDARY_SAFE_STOP = 0xF24u,
    GUIDEXOS_NATIVEAOT_ALLOC_STAGE_F25_FIRST_ROOT_CALLBACK_ENTRY_SAFE_STOP = 0xF25u,
    GUIDEXOS_NATIVEAOT_ALLOC_STAGE_F26_FIRST_ROOT_MEMBERSHIP_CLASSIFICATION_SAFE_STOP = 0xF26u,
    GUIDEXOS_NATIVEAOT_ALLOC_STAGE_F27_FIRST_ROOT_HEAP_RESOLUTION_SAFE_STOP = 0xF27u,
    GUIDEXOS_NATIVEAOT_ALLOC_STAGE_F28_FIRST_ROOT_CONDEMNED_GENERATION_DECISION_SAFE_STOP = 0xF28u,
};

enum {
    GUIDEXOS_NATIVEAOT_COLLECTION_REASON_OUT_OF_SO_H = 5u,
    GUIDEXOS_NATIVEAOT_COLLECTION_BLOCKING = 1u,
    GUIDEXOS_NATIVEAOT_COLLECTION_NONCOMPACTING_NOT_SELECTED = 0u,
    GUIDEXOS_NATIVEAOT_FIRST_COLLECTION_SAFE_STOP_MARKER = 0xC011EC01u,
    GUIDEXOS_NATIVEAOT_FIRST_COLLECTION_UNSUPPORTED_SUSPEND_EE = 1u,
    GUIDEXOS_NATIVEAOT_SINGLE_THREAD_SUSPEND_EE_SAFE_STOP_MARKER = 0xC011EC02u,
    GUIDEXOS_NATIVEAOT_ALLOCATION_CONTEXT_FIXUP_ROOT_BOUNDARY_SAFE_STOP_MARKER = 0xC011EC03u,
    GUIDEXOS_NATIVEAOT_FIRST_PER_THREAD_ROOT_PROVIDER_SAFE_STOP_MARKER = 0xC011EC04u,
    GUIDEXOS_NATIVEAOT_FIRST_ROOT_CANDIDATE_LOAD_SAFE_STOP_MARKER = 0xC011EC05u,
    GUIDEXOS_NATIVEAOT_FIRST_NON_NULL_ROOT_CALLBACK_BOUNDARY_SAFE_STOP_MARKER = 0xC011EC06u,
    GUIDEXOS_NATIVEAOT_FIRST_ROOT_CALLBACK_ENTRY_SAFE_STOP_MARKER = 0xC011EC07u,
    GUIDEXOS_NATIVEAOT_FIRST_ROOT_MEMBERSHIP_CLASSIFICATION_SAFE_STOP_MARKER = 0xC011EC08u,
    GUIDEXOS_NATIVEAOT_FIRST_ROOT_HEAP_RESOLUTION_SAFE_STOP_MARKER = 0xC011EC09u,
    GUIDEXOS_NATIVEAOT_FIRST_ROOT_CONDEMNED_GENERATION_DECISION_SAFE_STOP_MARKER = 0xC011EC10u,
    GUIDEXOS_NATIVEAOT_SINGLE_THREAD_SUSPEND_EE_NEXT_GC_START_WORK = 1u,
    GUIDEXOS_NATIVEAOT_SINGLE_THREAD_SUSPEND_EE_NEXT_POST_DISABLE = 2u,
};

#ifdef __cplusplus
}
#endif
