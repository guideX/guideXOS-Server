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

/* C011EC33 per-collection snapshot for one last-strong-reference transition. */
typedef struct guidexos_nativeaot_c011ec33_collection_record {
    uint32_t gcScanRootsEntries;
    uint32_t gcScanRootsReturns;
    uint32_t afterGcScanRootsEntries;
    uint32_t afterGcScanRootsReturns;
    uint32_t rootEnumerationEntries;
    uint32_t rootReports;
    uint32_t targetRootMatches;
    uint32_t stackRootMatches;
    uint32_t registerRootMatches;
    uint32_t ordinaryRootMatches;
    uint32_t staticThreadStaticRootMatches;
    uint32_t threadAbortRootMatches;
    uint32_t strongHandleMatches;
    uint32_t graphDerivedPromotions;
    uint32_t targetQueueInsertions;
    uint32_t targetChildDiscoveries;
    uint32_t targetMarkWrites;
    uint32_t targetPromoteCount;
    uint32_t targetMarkStateBefore;
    uint32_t targetMarkStateAfter;
    uint32_t targetMarked;
    uint32_t markWordMask;
    uint32_t handleScanEntries;
    uint32_t handleMapReads;
    uint32_t bucketsVisited;
    uint32_t tablesVisited;
    uint32_t segmentsVisited;
    uint32_t blocksVisited;
    uint32_t slotsInspected;
    uint32_t candidateHandles;
    uint32_t livenessCallbacks;
    uint32_t livenessDecisions;
    uint32_t liveDecisions;
    uint32_t deadDecisions;
    uint32_t mutationAttempts;
    uint32_t preservedCount;
    uint32_t clearedCount;
    uint32_t weakSlotMatched;
    uint32_t collectionCompleted;
    uint32_t eeRestartEntries;
    uint32_t eeRestartReturns;
    uint32_t managedResumeCount;
    uint32_t nativeUnwindFrames;
    uint32_t thirdUnwindAttempts;
    uint32_t condemnedGeneration;
    uint32_t targetGeneration;
    uint32_t collectionReason;
    uint32_t compacting;
    uint32_t relocating;
    uint32_t eeSuspended;
    uint32_t threadStoreLockHeld;
    uint32_t managedEntryProhibited;
    uint32_t managedFrames;
    uint32_t rootOwningFrameMatches;
    uint32_t postWeakPhase;
    uint32_t postWeakPhaseCount;

    /* C011EC37 bounded post-clear phase chronology.  The arrays are fixed
     * scalar storage so the suspended collector path never allocates or
     * constructs a diagnostic container. */
    uint32_t c011ec37PhaseEventCount;
    uint32_t c011ec37PhaseOrderErrors;
    uint32_t c011ec37PhaseLast;
    uint32_t c011ec37PhaseEntryMask;
    uint32_t c011ec37PhaseReturnMask;
    uint32_t c011ec37PhaseMutationMask;
    uint32_t c011ec37PhaseEntryCounts[24];
    uint32_t c011ec37PhaseReturnCounts[24];

    uintptr_t targetAtRoot;
    uintptr_t weakHandleSlot;
    uintptr_t weakSlotBefore;
    uintptr_t weakSlotAfter;
    uintptr_t rootSlot;
    uintptr_t rootValue;
    uintptr_t rootRegisterSlot;
    uint32_t rootKind;
    uintptr_t markWordAddress;
    uintptr_t markWordBefore;
    uintptr_t markWordAfter;
    uintptr_t livenessCallbackAddress;
    uintptr_t livenessDecisionAddress;
    uintptr_t clearingStoreAddress;
    uintptr_t controlPc;
    uintptr_t methodInfo;
    uintptr_t methodStart;
    uintptr_t methodEnd;
    uintptr_t gcInfo;
    uintptr_t safePointAddress;
    uintptr_t postWeakPhaseAddress;
} guidexos_nativeaot_c011ec33_collection_record;

/* C011EC34 bounded relocation-root evidence.  This is append-only proof
 * storage: it records the production callback contract and the first real
 * relocation lookup without changing collector metadata or root slots. */
typedef struct guidexos_nativeaot_c011ec34_relocation_record {
    uint32_t gcScanRootsEntries;
    uint32_t gcScanRootsReturns;
    uint32_t rootReports;
    uint32_t managedFrames;
    uint32_t callbackEntries;
    uint32_t callbackReturns;
    uint32_t rootsUnchanged;
    uint32_t rootsRewritten;
    uint32_t relocationLookupEntries;
    uint32_t relocationLookupReturns;
    uint32_t relocationLookupSuccesses;
    uint32_t relocationLookupFailures;
    uint32_t callbackInvariantFailures;
    uint32_t iteratorCompletions;
    uint32_t iteratorFrames;
    uint32_t nativeUnwinds;
    uint32_t thirdUnwindAttempts;
    uint32_t condemnedGeneration;
    uint32_t maximumGeneration;
    uint32_t compacting;
    uint32_t relocating;
    uint32_t promotion;
    uint32_t concurrent;
    uint32_t stackBoundsConsumed;
    uint32_t threadUnderCrawl;
    uint32_t eeSuspended;
    uint32_t threadStoreLockHeld;
    uint32_t managedEntryProhibited;
    uint32_t callbackType;
    uint32_t firstRootKind;
    uint32_t firstRootFlags;
    uint32_t firstRootInCondemnedGeneration;
    uint32_t firstRootPlannedToMove;
    uint32_t firstRootRewrite;
    uint32_t preflightEmitted;
    uint32_t markerEmitted;
    uint32_t safeStopReason;
    uint32_t reserved[4];

    uintptr_t scanContext;
    uintptr_t scanRootsCallback;
    uintptr_t scanRootsCaller;
    uintptr_t firstRootSlot;
    uintptr_t firstRootOldValue;
    uintptr_t firstRootNewValue;
    uintptr_t firstRootCallback;
    uintptr_t firstRootCallbackContext;
    uintptr_t firstRootCallbackEntry;
    uintptr_t firstRootCallbackReturn;
    uintptr_t firstRootControlPc;
    uintptr_t firstRootMethodInfo;
    uintptr_t firstRootMethodStart;
    uintptr_t firstRootMethodEnd;
    uintptr_t firstRootGcInfo;
    uintptr_t firstRootSafePoint;
    uintptr_t firstRootThread;
    uintptr_t firstRelocationLookupAddress;
    uintptr_t firstRelocationLookupReturnAddress;
    uintptr_t firstRelocationOldObject;
    uintptr_t firstRelocationNewObject;
    uintptr_t firstRelocationBrickTable;
    uintptr_t firstRelocationBrickIndex;
    uintptr_t firstRelocationBrickEntry;
    uintptr_t firstRelocationTreeNode;
    uintptr_t firstRelocationDistance;
    uintptr_t firstRelocationCallbackSlot;
    uintptr_t firstRelocationSlotBefore;
    uintptr_t firstRelocationSlotAfter;
    uintptr_t nextCallerAfterReturn;
} guidexos_nativeaot_c011ec34_relocation_record;

/* C011EC35 authentic handle-relocation evidence.  This record is append-only
 * and contains only scalar observations from the real UpdatePointer scan. */
typedef struct guidexos_nativeaot_c011ec35_relocated_handle_record {
    uint32_t handleScanEntered;
    uint32_t handleScanReturned;
    uint32_t slotInspections;
    uint32_t callbackEntries;
    uint32_t callbackReturns;
    uint32_t rewrittenHandles;
    uint32_t unchangedHandles;
    uint32_t shortWeakInspected;
    uint32_t shortWeakCallbacks;
    uint32_t shortWeakRewritten;
    uint32_t shortWeakUnchanged;
    uint32_t exactSlotObserved;
    uint32_t exactSlotCallbackEntered;
    uint32_t exactSlotCallbackReturned;
    uint32_t exactCallbackActive;
    uint32_t exactSlotRewritten;
    uint32_t exactSlotStale;
    uint32_t scanMode;
    uint32_t scanFlags;
    uint32_t condemnedGeneration;
    uint32_t maximumGeneration;
    uint32_t typeCount;
    uint32_t typeMask;
    uint32_t callbackInvariantFailures;
    uint32_t relocationLookupEntries;
    uint32_t relocationLookupReturns;
    uint32_t relocationLookupSuccesses;
    uint32_t relocationLookupFailures;
    uint32_t phaseHandleScanReturned;
    uint32_t phaseRelocateReturned;
    uint32_t phaseCompactReturned;
    uint32_t phaseGenerationBoundsReturned;
    uint32_t phaseCardsReturned;
    uint32_t gcDone;
    uint32_t restartEntries;
    uint32_t restartReturns;
    uint32_t managedResume;
    uint32_t eeSuspendedBeforeRestart;
    uint32_t eeResumedAfterRestart;
    uint32_t threadStoreLockHeld;
    uint32_t managedEntryProhibited;
    uint32_t sensitiveAllocations;
    uint32_t managedReentryWhileSuspended;
    uint32_t pendingCallbackType;
    uintptr_t pendingCallbackSlot;
    uint32_t preflightEmitted;
    uint32_t handleMarkerEmitted;
    uint32_t completionMarkerEmitted;
    uint32_t safeStopReason;
    uint32_t categoryInspected[16];
    uint32_t categoryCallbacks[16];
    uint32_t categoryRewritten[16];
    uint32_t categoryUnchanged[16];
    uintptr_t scanContext;
    uintptr_t scanCallback;
    uintptr_t updatePointerFunction;
    uintptr_t exactTable;
    uintptr_t exactSegment;
    uintptr_t exactBlock;
    uintptr_t exactBlockFirstSlot;
    uintptr_t exactSlot;
    uintptr_t exactSlotBefore;
    uintptr_t exactSlotAfter;
    uintptr_t exactTargetGeneration;
    uintptr_t exactHandleType;
    uintptr_t exactBlockIndex;
    uintptr_t exactSlotIndex;
    uintptr_t exactFlags;
    uintptr_t exactCallbackFunction;
    uintptr_t exactCallbackEntry;
    uintptr_t exactCallbackReturn;
    uintptr_t exactMutationFunction;
    uintptr_t exactStoreReturnAddress;
    uintptr_t exactRelocationLookupAddress;
    uintptr_t exactRelocationLookupReturnAddress;
    uintptr_t exactRelocationOldObject;
    uintptr_t exactRelocationNewObject;
    uintptr_t exactBrickTable;
    uintptr_t exactBrickIndex;
    uintptr_t exactBrickEntry;
    uintptr_t exactTreeNode;
    uintptr_t exactRelocationDistance;
    uintptr_t preRelocationObject;
    uintptr_t postRelocationObject;
    uintptr_t managedRootPostRelocation;
    uintptr_t managedResumeControlPc;
    uintptr_t restartEntryAddress;
    uintptr_t restartReturnAddress;
    uintptr_t firstPhaseAddress;
    uintptr_t nextPhaseAddress;
} guidexos_nativeaot_c011ec35_relocated_handle_record;

/* C011EC38 authentic Workstation region-sweep reclamation evidence.  This is
 * scalar, append-only proof storage.  The collector callbacks only copy
 * values already exposed by the locked GC source; they never create free
 * objects, touch headers, or traverse arbitrary heap contents. */
typedef struct guidexos_nativeaot_c011ec38_reclamation_record {
    uint32_t preflightEmitted;
    uint32_t collectionPathObserved;
    uint32_t actualCompacting;
    uint32_t sweepEntered;
    uint32_t sweepReturned;
    uint32_t targetObserved;
    uint32_t targetDeadRecognized;
    uint32_t targetMarkedBeforeSweep;
    uint32_t targetFullyContained;
    uint32_t coalescedWithNeighbors;
    uint32_t reclaimedEmitted;
    uint32_t generationPublished;
    uint32_t regionFreeListCountBefore;
    uint32_t regionFreeListCountAfter;
    uint32_t regionFreeObjCountBefore;
    uint32_t regionFreeObjCountAfter;
    uint32_t freeListCountBefore;
    uint32_t freeListCountAfter;
    uint32_t allocationTestAttempted;
    uint32_t allocationCount;
    uint32_t allocationPath;
    uint32_t sameReclaimedSpanConsumed;
    uint32_t oldTargetExtentOverlap;
    uint32_t exactStartReuse;
    uint32_t residualFreeSpanValid;
    uint32_t weakSlotCleared;
    uint32_t staleTargetReferences;
    uint32_t relocationResurrectionCount;
    uint32_t collection3Triggered;
    uint32_t allocatorIntegrityFailures;
    uint32_t safeStopReason;
    uint32_t managedMarkerEmitted;
    uint32_t completionMarkerEmitted;
    uint32_t managedCheckpoint;
    uint32_t allocationRequestedPayload;
    uint32_t allocationElementCount;
    uint32_t allocationRequestedCount;
    uint32_t reserved[3];

    uintptr_t originalTarget;
    uintptr_t relocatedTarget;
    uintptr_t targetType;
    uintptr_t targetEEType;
    uintptr_t targetStart;
    uintptr_t targetEnd;
    uintptr_t targetRawSize;
    uintptr_t targetAlignedSize;
    uintptr_t targetArrayHeaderSize;
    uintptr_t targetBaseSize;
    uintptr_t targetComponentSize;
    uintptr_t targetLogicalPayloadSize;
    uintptr_t targetElementCount;
    uintptr_t weakSlot;
    uintptr_t segment;
    uintptr_t segmentStart;
    uintptr_t segmentEnd;
    uintptr_t segmentAllocatedBefore;
    uintptr_t segmentAllocatedAfter;
    uintptr_t segmentCommitted;
    uintptr_t segmentReserved;
    uintptr_t segmentUsed;
    uintptr_t heapNumber;
    uintptr_t generation;
    uintptr_t planGeneration;
    uintptr_t brickAddress;
    uintptr_t brickIndex;
    uintptr_t sweepEntryAddress;
    uintptr_t sweepReturnAddress;
    uintptr_t generationAddress;
    uintptr_t generationAllocator;

    uintptr_t regionFreeListHeadBefore;
    uintptr_t regionFreeListTailBefore;
    uintptr_t regionFreeListHeadAfter;
    uintptr_t regionFreeListTailAfter;
    uintptr_t regionFreeListBytesBefore;
    uintptr_t regionFreeListBytesAfter;
    uintptr_t regionFreeObjBytesBefore;
    uintptr_t regionFreeObjBytesAfter;
    uintptr_t generationFreeListBytesBefore;
    uintptr_t generationFreeListBytesAfter;
    uintptr_t generationFreeObjBytesBefore;
    uintptr_t generationFreeObjBytesAfter;
    uintptr_t reclaimedStart;
    uintptr_t reclaimedEnd;
    uintptr_t reclaimedBytes;
    uintptr_t targetSpecificReclaimBytes;
    uintptr_t deadRangeStart;
    uintptr_t deadRangeEnd;
    uintptr_t deadRangeBytes;

    uintptr_t allocationSelectedStart;
    uintptr_t allocationSelectedEnd;
    uintptr_t allocationAddress;
    uintptr_t allocationEnd;
    uintptr_t allocationResidualStart;
    uintptr_t allocationResidualEnd;
    uintptr_t allocationRequestedSize;
    uintptr_t allocationAlignedSize;
    uintptr_t allocationFreeBytesBefore;
    uintptr_t allocationFreeBytesAfter;
} guidexos_nativeaot_c011ec38_reclamation_record;

/* C011EC39 locked Workstation plan provenance.  This record is deliberately
 * bounded and scalar-only.  The GC-side callbacks copy planner values into
 * this record while the EE is suspended; serial emission happens only after
 * the C37 managed continuation has resumed.  UINTPTR_MAX means that the
 * locked source did not expose that metric at the chosen observation point. */
typedef struct guidexos_nativeaot_c011ec39_plan_record {
    uint32_t plannerEntryObserved;
    uint32_t decideObserved;
    uint32_t finalDecisionObserved;
    uint32_t actualPhaseObserved;
    uint32_t finalDecision;
    uint32_t actualPhase;
    uint32_t condemnedGeneration;
    uint32_t collectionReason;
    uint32_t heapNumber;
    uint32_t maximumGeneration;
    uint32_t prePlanCompacting;
    uint32_t prePlanPromotion;
    uint32_t prePlanConcurrent;
    uint32_t prePlanPauseMode;
    uint32_t finalPauseMode;
    uint32_t dispatchPriorCompacting;
    uint32_t finalCompacting;
    uint32_t finalRelocatingDerived;
    uint32_t plannerEntryCount;
    uint32_t decideCount;
    uint32_t finalDecisionCount;
    uint32_t compactPhaseCount;
    uint32_t sweepPhaseCount;
    uint32_t forceCompact;
    uint32_t lastGcBeforeOom;
    uint32_t inducedCompacting;
    uint32_t inducedAggressive;
    uint32_t pmFullGcReason;
    uint32_t provisionalMode;
    uint32_t lowEphemeral;
    uint32_t fragmentationExceeded;
    uint32_t highMemory;
    uint32_t ensureGapForcedCompaction;
    uint32_t compactionMechanism;
    uint32_t entryMemoryLoad;
    uint32_t gen0ReductionCount;
    uint32_t pmTriggerFullGc;
    uint32_t sensitiveAllocations;
    uint32_t plannerMutations;
    uint32_t managedReentryWhileSuspended;
    uint32_t collection3Triggered;
    uint32_t preflightEmitted;
    uint32_t completionMarkerEmitted;
    uint32_t safeStopReason;
    uint32_t segmentCountAvailable;
    uint32_t ensureGapObserved;
    uint32_t ensureGapAvailable;
    uint32_t compactionSpaceObserved;
    uint32_t compactionSpaceDecisionObserved;
    uint32_t compactionSpaceDecision;
    uint32_t compactionSpaceSweepSufficient;
    uint32_t compactionSpaceCompactSufficient;
    uint32_t compactionSpaceLargeChunkFound;

    uintptr_t entryAvailablePhysicalMemory;
    uintptr_t fragmentation;
    uintptr_t generationSizes;
    uintptr_t fragmentationThreshold;
    uintptr_t fragmentationBurdenPpm;
    uintptr_t fragmentationBurdenThresholdPpm;
    uintptr_t generationSize;
    uintptr_t generationPlanSize;
    uintptr_t generationFreeListBytes;
    uintptr_t generationFreeObjBytes;
    uintptr_t generationCondemnedAllocated;
    uintptr_t generationSweepAllocated;
    uintptr_t generationPinnedCompactBytes;
    uintptr_t generationPinnedSweepBytes;
    uintptr_t promotedBytes;
    uintptr_t survivedBytes;
    uintptr_t desiredAllocation;
    uintptr_t newAllocation;
    uintptr_t currentSize;
    uintptr_t gen0DesiredAllocation;
    uintptr_t gen0NewAllocation;
    uintptr_t gen0CurrentSize;
    uintptr_t gen1DesiredAllocation;
    uintptr_t gen1NewAllocation;
    uintptr_t gen1CurrentSize;
    uintptr_t segmentCount;
    uintptr_t compactionSpaceGen0Size;
    uintptr_t compactionSpaceSweepFreeBytes;
    uintptr_t compactionSpacePinnedFreeBytes;
    uintptr_t compactionSpaceEndGen0Bytes;
    uintptr_t plannerEntryAddress;
    uintptr_t decideAddress;
    uintptr_t finalDecisionAddress;
    uintptr_t compactPhaseAddress;
    uintptr_t sweepPhaseAddress;
} guidexos_nativeaot_c011ec39_plan_record;

/* C011EC40 authentic Workstation compaction reclamation evidence.  The GC
 * callbacks record only bounded scalar facts supplied by the locked plug tree
 * and segment/frontier helpers.  A target-specific 0x58 extent is kept
 * separate from aggregate dead-gap bytes so the latter is never over-attributed
 * to the target. */
typedef struct guidexos_nativeaot_c011ec40_compaction_record {
    uint32_t compactEntryObserved;
    uint32_t compactReturned;
    uint32_t generationPublished;
    uint32_t targetObserved;
    uint32_t targetDeadRecognized;
    uint32_t targetMarkedBeforeCompact;
    uint32_t targetLivePlugMembership;
    uint32_t targetRelocationCallbackCount;
    uint32_t targetCopyMoveCount;
    uint32_t livePlugCount;
    uint32_t deadGapCount;
    uint32_t targetDeadGapObserved;
    uint32_t holeClosureObserved;
    uint32_t destinationOverlapsTarget;
    uint32_t frontierValid;
    uint32_t frontierReduced;
    uint32_t allocatorVisibleTail;
    uint32_t allocationTestAttempted;
    uint32_t allocationCount;
    uint32_t allocationRequestedCount;
    uint32_t allocationRequestedPayload;
    uint32_t allocationConsumedTail;
    uint32_t oldTargetExtentOverlap;
    uint32_t exactStartReuse;
    uint32_t weakSlotCleared;
    uint32_t staleTargetReferences;
    uint32_t relocationResurrectionCount;
    uint32_t collection3Triggered;
    uint32_t compactIntegrityFailures;
    uint32_t allocatorIntegrityFailures;
    uint32_t safeStopReason;
    uint32_t preflightEmitted;
    uint32_t reclaimedEmitted;
    uint32_t managedMarkerEmitted;
    uint32_t completionMarkerEmitted;
    uint32_t successLevel;
    uint32_t reserved[3];

    uintptr_t originalTarget;
    uintptr_t relocatedTarget;
    uintptr_t targetType;
    uintptr_t targetEEType;
    uintptr_t targetStart;
    uintptr_t targetEnd;
    uintptr_t targetRawSize;
    uintptr_t targetAlignedSize;
    uintptr_t targetArrayHeaderSize;
    uintptr_t targetBaseSize;
    uintptr_t targetComponentSize;
    uintptr_t targetLogicalPayloadSize;
    uintptr_t targetElementCount;
    uintptr_t weakSlot;
    uintptr_t heapNumber;
    uintptr_t generation;
    uintptr_t planGeneration;

    uintptr_t segment;
    uintptr_t segmentStart;
    uintptr_t segmentEnd;
    uintptr_t segmentCommitted;
    uintptr_t segmentReserved;
    uintptr_t segmentUsedBefore;
    uintptr_t segmentUsedAfter;
    uintptr_t segmentAllocatedBefore;
    uintptr_t segmentAllocatedAfter;
    uintptr_t segmentPlanAllocatedBefore;
    uintptr_t segmentPlanAllocatedAfter;
    uintptr_t compactedLiveEnd;

    uintptr_t liveSourceStart;
    uintptr_t liveSourceEnd;
    uintptr_t liveDestinationStart;
    uintptr_t liveDestinationEnd;
    uintptr_t liveShift;
    uintptr_t deadGapStart;
    uintptr_t deadGapEnd;
    uintptr_t deadGapBytes;
    uintptr_t totalCompactedAwayDeadBytes;
    uintptr_t neighboringLiveSourceStart;
    uintptr_t neighboringLiveSourceEnd;
    uintptr_t neighboringLiveDestinationStart;
    uintptr_t neighboringLiveDestinationEnd;
    uintptr_t neighboringLiveShift;

    uintptr_t oldCompactedFrontier;
    uintptr_t newCompactedFrontier;
    uintptr_t frontierDelta;
    uintptr_t freeTailStart;
    uintptr_t freeTailEnd;
    uintptr_t freeTailSize;

    uintptr_t allocationAddress;
    uintptr_t allocationEnd;
    uintptr_t allocationAlignedSize;
    uintptr_t allocationPath;
} guidexos_nativeaot_c011ec40_compaction_record;

enum {
    GUIDEXOS_NATIVEAOT_C011EC41_MAX_ALLOCATIONS = 8u
};

/* C011EC41 post-GC allocation provenance.  These records are fixed-size and
 * written only by the managed-resume/managed-allocation observers.  They
 * describe the production Thread allocation context and the object/segment
 * returned by the locked NativeAOT helper; they do not select or modify any
 * allocator state. */
typedef struct guidexos_nativeaot_c011ec41_allocation_record {
    uint32_t observed;
    uint32_t ordinal;
    uint32_t requestedPayload;
    uint32_t requestedSize;
    uint32_t alignedSize;
    uint32_t fastPath;
    uint32_t rarePath;
    uint32_t segmentChanged;
    uint32_t pointerValid;
    uint32_t fitValid;
    uint32_t monotonicValid;
    uint32_t overlapValid;
    uint32_t heapOwned;
    uint32_t collectionBefore;
    uint32_t collectionAfter;
    uint32_t invariantFailures;

    uintptr_t objectAddress;
    uintptr_t objectEnd;
    uintptr_t contextBefore;
    uintptr_t limitBefore;
    uintptr_t contextAfter;
    uintptr_t limitAfter;
    uintptr_t allocBytesBefore;
    uintptr_t allocBytesAfter;
    uintptr_t threadIdentity;
    uintptr_t contextIdentity;
    uintptr_t heapIdentity;
    uintptr_t segmentIdentity;
    uintptr_t segmentBase;
    uintptr_t segmentAllocated;
    uintptr_t segmentCommitted;
    uintptr_t segmentReserved;
    uintptr_t segmentGeneration;
} guidexos_nativeaot_c011ec41_allocation_record;

typedef struct guidexos_nativeaot_c011ec41_provenance_record {
    uint32_t managedResumeObserved;
    uint32_t activeContextCaptured;
    uint32_t activeContextValid;
    uint32_t firstHelperEntry;
    uint32_t preflightEmitted;
    uint32_t provenanceEmitted;
    uint32_t completionMarkerEmitted;
    uint32_t allocationCount;
    uint32_t fastCount;
    uint32_t rareCount;
    uint32_t refillCount;
    uint32_t collection3Triggered;
    uint32_t sensitiveDiagnosticAllocations;
    uint32_t invariantFailures;
    uint32_t contextOverlapsTail;
    uint32_t contextSameSegment;
    uint32_t tailEligible;
    uint32_t tailEligibilityTiming;
    uint32_t tailConsidered;
    uint32_t tailConsumed;
    uint32_t naturalRefillObserved;
    uint32_t contextRestoredValid;
    uint32_t supplyingRegionKnown;
    uint32_t safeStopReason;
    uint32_t requestActive;
    uint32_t currentOrdinal;
    uint32_t nativeHelperObserved;
    uint32_t nativeGcCountBefore;
    uint32_t reserved[4];

    uintptr_t threadIdentity;
    uintptr_t contextIdentity;
    uintptr_t initialAllocPtr;
    uintptr_t initialAllocLimit;
    uintptr_t initialAllocBytes;
    uintptr_t initialAllocBytesUoh;
    uintptr_t homeHeap;
    uintptr_t activeSegment;
    uintptr_t activeSegmentBase;
    uintptr_t activeSegmentAllocated;
    uintptr_t activeSegmentCommitted;
    uintptr_t activeSegmentReserved;
    uintptr_t activeGeneration;

    uintptr_t tailSegment;
    uintptr_t tailStart;
    uintptr_t tailEnd;
    uintptr_t tailSize;

    uintptr_t supplyingStart;
    uintptr_t supplyingEnd;
    uintptr_t supplyingSegment;
    uintptr_t supplyingGeneration;

    uintptr_t finalAllocPtr;
    uintptr_t finalAllocLimit;
    uintptr_t finalAllocBytes;
    uintptr_t firstPointerBefore;
    uintptr_t firstLimit;
    uintptr_t firstObject;
    uintptr_t firstPointerAfter;
    uintptr_t firstAlignedSize;

    /* C011EC41 stock AllocFast entry snapshot.  These fields are populated by
     * the diagnostic call inserted around the locked RhpNewArray entry and
     * consumed by the post-allocation callback. */
    uintptr_t nativeRequestedSize;
    uintptr_t nativePointerBefore;
    uintptr_t nativeLimitBefore;
    uintptr_t nativeAllocBytesBefore;
    uintptr_t nativeThreadBefore;
    uintptr_t nativeContextBefore;
    uintptr_t nativeHeapBefore;

    guidexos_nativeaot_c011ec41_allocation_record allocations[
        GUIDEXOS_NATIVEAOT_C011EC41_MAX_ALLOCATIONS];
} guidexos_nativeaot_c011ec41_provenance_record;

enum {
    GUIDEXOS_NATIVEAOT_C011EC42_MAX_ALLOCATIONS = 256u
};

/* C011EC42 bounded natural post-Collection-3 lifecycle evidence.  The
 * workload records each ordinary pressure allocation in fixed storage.  The
 * collector callbacks publish only scalar state; they do not select a
 * segment, force a collection, or retain a managed object. */
typedef struct guidexos_nativeaot_c011ec42_allocation_record {
    uint32_t observed;
    uint32_t ordinal;
    uint32_t requestedPayload;
    uint32_t requestedSize;
    uint32_t fastPath;
    uint32_t rarePath;
    uint32_t collectionBefore;
    uint32_t collectionAfter;
    uint32_t postCollection;
    uint32_t pointerInTailBefore;
    uint32_t objectInTail;
    uint32_t contextAfterInTail;
    uint32_t heapOwned;
    uint32_t invariantFailures;
    uint32_t reserved0;
    uint32_t reserved1;

    uintptr_t objectAddress;
    uintptr_t objectEnd;
    uintptr_t contextBefore;
    uintptr_t limitBefore;
    uintptr_t contextAfter;
    uintptr_t limitAfter;
    uintptr_t allocBytesBefore;
    uintptr_t allocBytesAfter;
    uintptr_t threadIdentity;
    uintptr_t contextIdentity;
    uintptr_t heapIdentity;
    uintptr_t segmentIdentity;
    uintptr_t segmentBase;
    uintptr_t segmentAllocated;
    uintptr_t segmentCommitted;
    uintptr_t segmentReserved;
    uintptr_t segmentGeneration;
} guidexos_nativeaot_c011ec42_allocation_record;

typedef struct guidexos_nativeaot_c011ec42_lifecycle_record {
    uint32_t started;
    uint32_t preflightEmitted;
    uint32_t collectionEntryObserved;
    uint32_t plannerDecisionObserved;
    uint32_t phaseObserved;
    uint32_t restartObserved;
    uint32_t managedResumeObserved;
    uint32_t completionMarkerEmitted;
    uint32_t allocationCount;
    uint32_t allocationsBeforeCollection;
    uint32_t allocationsAfterCollection;
    uint32_t postCollectionAllocationCount;
    uint32_t collectionCountBefore;
    uint32_t collectionCountAfter;
    uint32_t condemnedGeneration;
    uint32_t collectionReason;
    uint32_t plannerDecision;
    uint32_t actualPhase;
    uint32_t tailEligible;
    uint32_t tailConsidered;
    uint32_t tailConsumed;
    uint32_t tailStillMapped;
    uint32_t tailGenerationBefore;
    uint32_t tailGenerationAfter;
    uint32_t ephemeralBoundaryObserved;
    uint32_t safeStopReason;
    uint32_t invariantFailures;
    uint32_t sensitiveDiagnosticAllocations;
    uint32_t requestActive;
    uint32_t currentOrdinal;
    uint32_t reserved[5];

    uintptr_t threadIdentity;
    uintptr_t contextIdentity;
    uintptr_t homeHeap;
    uintptr_t initialAllocPtr;
    uintptr_t initialAllocLimit;
    uintptr_t finalAllocPtr;
    uintptr_t finalAllocLimit;

    uintptr_t tailSegment;
    uintptr_t tailStart;
    uintptr_t tailEnd;
    uintptr_t tailSize;
    uintptr_t tailSegmentBaseBefore;
    uintptr_t tailSegmentCommittedBefore;
    uintptr_t tailSegmentReservedBefore;
    uintptr_t tailSegmentAfter;
    uintptr_t tailSegmentBaseAfter;
    uintptr_t tailSegmentCommittedAfter;
    uintptr_t tailSegmentReservedAfter;

    uintptr_t ephemeralLowBefore;
    uintptr_t ephemeralHighBefore;
    uintptr_t ephemeralSegmentBefore;
    uintptr_t ephemeralLowAfter;
    uintptr_t ephemeralHighAfter;
    uintptr_t ephemeralSegmentAfter;
    uintptr_t ephemeralSegmentBaseAfter;
    uintptr_t ephemeralSegmentAllocatedAfter;
    uintptr_t ephemeralSegmentCommittedAfter;
    uintptr_t ephemeralSegmentReservedAfter;

    guidexos_nativeaot_c011ec42_allocation_record allocations[
        GUIDEXOS_NATIVEAOT_C011EC42_MAX_ALLOCATIONS];
} guidexos_nativeaot_c011ec42_lifecycle_record;

enum {
    GUIDEXOS_NATIVEAOT_C011EC44_MAX_CHECKPOINTS = 8u,
    GUIDEXOS_NATIVEAOT_C011EC44_FRAME_CREATE = 1u,
    GUIDEXOS_NATIVEAOT_C011EC44_PRE_GC = 2u,
    GUIDEXOS_NATIVEAOT_C011EC44_SUSPEND = 3u,
    GUIDEXOS_NATIVEAOT_C011EC44_ROOT_SOURCE = 4u,
    GUIDEXOS_NATIVEAOT_C011EC44_ITERATOR = 5u,
    GUIDEXOS_NATIVEAOT_C011EC44_DIVERGENCE = 6u
};

/* C011EC44 bounded transition-frame provenance.  Each checkpoint is a
 * scalar snapshot; no frame is retained as a runtime root and no diagnostic
 * path writes through any captured address.  The reverse-slot fields identify
 * the exact producer load used by CoffNativeCodeManager. */
typedef struct guidexos_nativeaot_c011ec44_checkpoint {
    uint32_t observed;
    uint32_t kind;
    uint32_t classification; /* 0 unavailable, 1 valid, 2 invalid, 3 not authoritative */
    uint32_t gcOrdinal;
    uint32_t gcState;
    uint32_t controlPcManaged;
    uint32_t codeManagerResolved;
    uint32_t methodInfoValid;
    uint32_t suspendState;
    uint32_t reserved[3];

    uintptr_t frameAddress;
    uintptr_t controlPc;
    uintptr_t sp;
    uintptr_t fp;
    uintptr_t flags;
    uintptr_t threadAddress;
    uintptr_t sourcePointer;
    uintptr_t transitionFramePointer;
    uintptr_t savedControlPc;
    uintptr_t sourceBase;
    uintptr_t sourceSlotOffset;
    uintptr_t sourceSlotAddress;
    uintptr_t sourceSlotValue;
    uintptr_t liveTransitionFrame;
    uintptr_t deferredTransitionFrame;
    uintptr_t cachedTransitionFrame;
    uintptr_t threadStateFlags;
    uintptr_t allocationContext;
    uintptr_t codeManager;
    uintptr_t methodInfo;
} guidexos_nativeaot_c011ec44_checkpoint;

typedef struct guidexos_nativeaot_c011ec44_provenance_record {
    uint32_t checkpointCount;
    uint32_t frameCreateCount;
    uint32_t preGcCount;
    uint32_t suspendCount;
    uint32_t rootSourceCount;
    uint32_t iteratorCount;
    uint32_t divergenceCount;
    uint32_t firstInvalidKind;
    uint32_t firstValidKind;
    uint32_t writeProvenanceAvailable;
    uint32_t liveFrameOverwriteProven;
    uint32_t staleFrameProven;
    uint32_t wrongFrameProven;
    uint32_t abiLayoutMismatchProven;
    uint32_t invariantFailures;
    uint32_t sensitiveDiagnosticAllocations;

    uintptr_t neighborDestinationEnd;
    uintptr_t firstFrameAddress;
    uintptr_t firstControlPc;
    uintptr_t firstSourcePointer;
    uintptr_t firstDivergenceSourceSlotAddress;
    uintptr_t firstDivergenceSourceSlotValue;
    uintptr_t firstDivergenceFrameAddress;
    uintptr_t firstDivergenceControlPc;
    uintptr_t firstDivergenceCodeManager;

    guidexos_nativeaot_c011ec44_checkpoint checkpoints[
        GUIDEXOS_NATIVEAOT_C011EC44_MAX_CHECKPOINTS];
} guidexos_nativeaot_c011ec44_provenance_record;

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

    /* Proof-only first genuine-root condemned=true pre-mark boundary. */
    uint32_t preMarkTrueBranchRequestCount;
    uint32_t preMarkTrueBranchEntryCount;
    uint32_t preMarkTrueBranchDuplicateCount;
    uint32_t preMarkDprintfCompiled;
    uint32_t preMarkDprintfRequestCount;
    uint32_t preMarkDprintfEntryCount;
    uint32_t preMarkDprintfReturnCount;
    uint32_t preMarkRootFlagTestCount;
    uint32_t preMarkInteriorFlagResult;
    uint32_t preMarkPinnedFlagResult;
    uint32_t preMarkConservativeCheckCount;
    uint32_t preMarkConservativeGcEnabled;
    uint32_t preMarkObjectIsFree;
    uint32_t preMarkDebugValidationEntryCount;
    uint32_t preMarkDebugValidationCompletionCount;
    uint32_t preMarkDebugNoRangeChecks;
    uint32_t preMarkDebugVerifyHeapGc;
    uint32_t preMarkSmallHeapPointerResult;
    uint32_t preMarkLargeHeapPointerResult;
    uint32_t preMarkSegmentLookupCount;
    uint32_t preMarkGcMetadataReadCount;
    uint32_t preMarkObjectHeaderReadCount;
    uint32_t preMarkMethodTableReadCount;
    uint32_t preMarkBoundaryReached;
    uint32_t preMarkMarkHelperCallAttemptCount;
    uint32_t preMarkMarkHelperCallCount;
    uint32_t preMarkSafeStopObserved;
    uint32_t preMarkSafeStopReason;
    uint32_t preMarkRootFlags;
    uint32_t preMarkSourceDebugBranchCompiled;
    uint32_t preMarkConservativeBranchCompiled;
    uint32_t preMarkStressPinningBranchCompiled;
    uint32_t preMarkMarkStateReadCount;
    uint32_t preMarkMarkStateReadResult;
    uint32_t firstMarkHelperDuplicateEntryCount;
    uint32_t firstMarkWorklistMetadataReadCount;
    uint32_t firstMarkMutationAttemptCount;
    uint32_t firstMarkMutationExecutionCount;
    uint32_t secondMarkMutationAttemptCount;
    uint32_t secondMarkMutationExecutionCount;
    uint32_t firstMarkWorklistSlotWriteCount;
    uint32_t firstMarkWorklistCursorWriteCount;
    uint32_t firstMarkMarkBitWriteCount;
    uint32_t firstMarkObjectHeaderWriteCount;
    uint32_t firstMarkGcMetadataWriteCount;
    uint32_t firstMarkSegmentWriteCount;
    uint32_t firstMarkGraphTraversalStartCount;
    uint32_t firstMarkChildReferenceReadCount;
    uint32_t firstMarkChildObjectDiscoveredCount;
    uint32_t firstMarkSecondObjectMarkAttemptCount;
    uint32_t firstMarkLogicalMarkComplete;
    uint32_t firstMarkTraversalScheduled;
    uint32_t firstMarkSafeStopObserved;
    uint32_t firstMarkSafeStopReason;
    uint32_t firstMarkHelperReturnCount;
    uint32_t reservedFirstRootPreMark[2];

    uintptr_t preMarkObjectInput;
    uintptr_t preMarkHeapSentinel;
    uintptr_t preMarkMarkHelperAddress;
    uintptr_t preMarkBoundaryReturnAddress;
    uintptr_t preMarkMutationCallSiteAddress;
    uintptr_t preMarkFirstMutationInstructionAddress;
    uintptr_t preMarkDebugValidationObject;
    uintptr_t preMarkFirstObjectMetadataReadAddress;
    uintptr_t preMarkMethodTableIdentity;
    uintptr_t firstMarkHelperPo;
    uintptr_t firstMarkHelperObject;
    uintptr_t firstMarkWorklistTarget;
    uintptr_t firstMarkWorklistOldValue;
    uintptr_t firstMarkWorklistNewValue;
    uintptr_t firstMarkWorklistQueueBase;
    uintptr_t firstMarkWorklistSlotIndexBefore;
    uintptr_t firstMarkWorklistCursorBefore;
    uintptr_t firstMarkWorklistSlotIndexAfter;
    uintptr_t firstMarkWorklistCursorAfter;
    uintptr_t firstMarkWorklistCapacity;
    uintptr_t firstMarkInstructionAddress;
    uintptr_t firstMarkNextMutationInstructionAddress;
    uintptr_t firstMarkSafeStopReturnAddress;

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

    /*
     * C011EC13 proof-only post-queue decision evidence.  These fields are
     * append-only so every earlier checkpoint retains its layout.  The
     * source-injected observers record the real queue_mark old_o/null/marked
     * decision without introducing a mark representation or a synthetic
     * work item.
     */
    uint32_t postQueueDecisionRequestCount;
    uint32_t postQueueDecisionEntryCount;
    uint32_t postQueueDecisionCompletionCount;
    uint32_t postQueueDecisionDuplicateCount;
    uint32_t postQueueNullTestCount;
    uint32_t postQueueNullTestResult;
    uint32_t postQueueMarkedRequestCount;
    uint32_t postQueueMarkedEntryCount;
    uint32_t postQueueMarkedReturnCount;
    uint32_t postQueueMarkedResult;
    uint32_t postQueueMarkStateReadCount;
    uint32_t postQueueObjectHeaderReadCount;
    uint32_t postQueueMethodTableReadCount;
    uint32_t postQueueSegmentReadCount;
    uint32_t postQueueRegionReadCount;
    uint32_t postQueueNewMutationAttemptCount;
    uint32_t postQueueNewMutationExecutionCount;
    uint32_t postQueueLogicalMarkComplete;
    uint32_t postQueueTraversalScheduled;
    uint32_t postQueueSafeStopObserved;
    uint32_t postQueueSafeStopReason;
    uint32_t postQueueBranch;
    uint32_t reservedPostQueueDecision[4];

    uintptr_t postQueueObjectInput;
    uintptr_t postQueueSelectedSlotAddress;
    uintptr_t postQueueOldSlotValue;
    uintptr_t postQueueNewSlotValue;
    uintptr_t postQueueQueueBase;
    uintptr_t postQueueSlotIndex;
    uintptr_t postQueueCursorBefore;
    uintptr_t postQueueCursorAfter;
    uintptr_t postQueueDecisionReturnAddress;
    uintptr_t postQueueSafeStopAddress;
    uintptr_t postQueueNextMutationAddress;

    /*
     * C011EC14 proof-only first naturally non-null old_o evidence.  This
     * block is append-only after C011EC13 so earlier checkpoint layouts stay
     * stable.  The queue history is the real sixteen-slot ring history plus
     * the seventeenth insertion that displaces the first object.
     */
    uint32_t firstNonNullOldOCallbackCount;
    uint32_t firstNonNullOldOCandidateCount;
    uint32_t firstNonNullOldOMarkHelperCount;
    uint32_t firstNonNullOldOQueueInsertionCount;
    uint32_t firstNonNullOldOWorklistWriteCount;
    uint32_t firstNonNullOldONullDecisionCount;
    uint32_t firstNonNullOldONonNullDecisionCount;
    uint32_t firstNonNullOldODecisionRequestCount;
    uint32_t firstNonNullOldODecisionEntryCount;
    uint32_t firstNonNullOldOMarkedRequestCount;
    uint32_t firstNonNullOldOMarkedEntryCount;
    uint32_t firstNonNullOldOMarkedReturnCount;
    uint32_t firstNonNullOldOMarkStateReadCount;
    uint32_t firstNonNullOldOMarkedResult;
    uint32_t firstNonNullOldORawMarkWordReadCount;
    uint32_t firstNonNullOldOCallbackReturnCount;
    uint32_t firstNonNullOldOCallbackReturnsBeforeDecision;
    uint32_t firstNonNullOldONewMutationAttemptCount;
    uint32_t firstNonNullOldONewMutationExecutionCount;
    uint32_t firstNonNullOldOMarkBitWriteCount;
    uint32_t firstNonNullOldOObjectHeaderWriteCount;
    uint32_t firstNonNullOldOGcMetadataWriteCount;
    uint32_t firstNonNullOldOSegmentWriteCount;
    uint32_t firstNonNullOldOGraphTraversalCount;
    uint32_t firstNonNullOldOChildReferenceReadCount;
    uint32_t firstNonNullOldOChildObjectCount;
    uint32_t firstNonNullOldOSecondObjectMarkAttemptCount;
    uint32_t firstNonNullOldOSafeStopObserved;
    uint32_t firstNonNullOldOSafeStopReason;
    uint32_t firstNonNullOldOBranch;
    uint32_t firstNonNullOldOQueueHistoryOverflow;
    uint32_t firstNonNullOldOProvenanceValid;
    uint32_t firstNonNullOldOFindRangeResult;
    uint32_t firstNonNullOldOHeapMembershipResult;
    uint32_t firstNonNullOldOGeneration;
    uint32_t firstNonNullOldOHeaderReadCount;
    uint32_t firstNonNullOldOOtherHeaderWriteCount;
    uint32_t firstNonNullOldOTraversalWorkItemWriteCount;
    uint32_t firstNonNullOldOObjectHistoryIndex;
    uint32_t reservedFirstNonNullOldO[8];

    uintptr_t firstNonNullOldOCurrentCallbackObject;
    uintptr_t firstNonNullOldORootSlot;
    uintptr_t firstNonNullOldORawRoot;
    uintptr_t firstNonNullOldOSelectedSlotAddress;
    uintptr_t firstNonNullOldOSlotOldValue;
    uintptr_t firstNonNullOldOSlotNewValue;
    uintptr_t firstNonNullOldOQueueBase;
    uintptr_t firstNonNullOldOSlotIndex;
    uintptr_t firstNonNullOldOCursorBefore;
    uintptr_t firstNonNullOldOCursorAfter;
    uintptr_t firstNonNullOldOOldObject;
    uintptr_t firstNonNullOldOOldObjectHeaderAddress;
    uintptr_t firstNonNullOldORawHeader;
    uintptr_t firstNonNullOldOMarkMask;
    uintptr_t firstNonNullOldODecisionReturnAddress;
    uintptr_t firstNonNullOldOSafeStopAddress;
    uintptr_t firstNonNullOldONextMutationAddress;
    uintptr_t firstNonNullOldOHeapIdentity;
    uintptr_t firstNonNullOldOCallbackEntryAddress;
    uintptr_t firstNonNullOldOCallbackEntryReturnAddress;
    uintptr_t firstNonNullOldOMarkHelperAddress;
    uintptr_t firstNonNullOldOMarkHelperPo;
    uintptr_t firstNonNullOldOMarkHelperObject;
    uintptr_t firstNonNullOldOQueueObjectHistory[GUIDEXOS_NATIVEAOT_MAX_OBJECT_HISTORY];
    uintptr_t firstNonNullOldOSlotAddressHistory[GUIDEXOS_NATIVEAOT_MAX_OBJECT_HISTORY];
    uintptr_t firstNonNullOldOOldSlotHistory[GUIDEXOS_NATIVEAOT_MAX_OBJECT_HISTORY];
    uintptr_t firstNonNullOldONewSlotHistory[GUIDEXOS_NATIVEAOT_MAX_OBJECT_HISTORY];
    uintptr_t firstNonNullOldOSlotIndexHistory[GUIDEXOS_NATIVEAOT_MAX_OBJECT_HISTORY];
    uintptr_t firstNonNullOldOCursorBeforeHistory[GUIDEXOS_NATIVEAOT_MAX_OBJECT_HISTORY];
    uintptr_t firstNonNullOldOCursorAfterHistory[GUIDEXOS_NATIVEAOT_MAX_OBJECT_HISTORY];

    /*
     * C011EC15 proof-only continuation after the first genuine root callback.
     * This block is append-only.  The source-injected GcEnumObject observer
     * records every authentic root slot, lets the first Promote/queue path
     * return, and stops before the next non-null candidate is promoted.
     */
    uint32_t c011ec15GcScanRootsRequestCount;
    uint32_t c011ec15ProviderEntryCount;
    uint32_t c011ec15ProviderRequestCount;
    uint32_t c011ec15RootSlotVisitCount;
    uint32_t c011ec15NullCandidateCount;
    uint32_t c011ec15NonNullCandidateCount;
    uint32_t c011ec15FirstRootCallbackReturnCount;
    uint32_t c011ec15EnumGcRefContinuationCount;
    uint32_t c011ec15PromoteReturnCount;
    uint32_t c011ec15MarkHelperReturnCount;
    uint32_t c011ec15QueueMarkReturnCount;
    uint32_t c011ec15SecondPromoteAttemptCount;
    uint32_t c011ec15SecondPromoteEntryCount;
    uint32_t c011ec15SecondQueueMutationAttemptCount;
    uint32_t c011ec15SecondQueueMutationExecutionCount;
    uint32_t c011ec15MarkBitWriteCount;
    uint32_t c011ec15ChildReferenceReadCount;
    uint32_t c011ec15GraphTraversalCount;
    uint32_t c011ec15ProviderCategory;
    uint32_t c011ec15FirstRootProviderCategory;
    uint32_t c011ec15ProviderContinuationCategory;
    uint32_t c011ec15StopObserved;
    uint32_t c011ec15StopReason;
    uint32_t c011ec15CallbackFlags;
    uint32_t c011ec15CallbackContextValid;
    uint32_t c011ec15PromoteEntryCount;
    uint32_t c011ec15ThreadStoreLockHeld;
    uint32_t c011ec15EeSuspended;
    uint32_t c011ec15ManagedEntryProhibited;
    uint32_t c011ec15Reserved[5];

    uintptr_t c011ec15FirstRootSlot;
    uintptr_t c011ec15FirstRootValue;
    uintptr_t c011ec15FirstRootProvider;
    uintptr_t c011ec15CurrentProvider;
    uintptr_t c011ec15FirstRootCallback;
    uintptr_t c011ec15FirstRootContext;
    uintptr_t c011ec15NextRootSlot;
    uintptr_t c011ec15NextRootValue;
    uintptr_t c011ec15NextRootProvider;
    uintptr_t c011ec15NextRootCallback;
    uintptr_t c011ec15NextRootContext;
    uintptr_t c011ec15FirstQueueSlot;
    uintptr_t c011ec15FirstQueueSlotIndex;
    uintptr_t c011ec15FirstQueueOldValue;
    uintptr_t c011ec15FirstQueueNewValue;
    uintptr_t c011ec15FirstQueueCursorBefore;
    uintptr_t c011ec15FirstQueueCursorAfter;
    uintptr_t c011ec15FirstQueueBase;
    uintptr_t c011ec15StopAddress;

    /*
     * C011EC18 transition-frame provenance. These fields are append-only and
     * are populated from the locked RhpGcAlloc/PInvokeTransitionFrame and
     * StackFrameIterator paths. They are structural observations, not a
     * substitute context or a managed-PC correction.
     */
    uint32_t c011ec18TransitionFrameCount;
    uint32_t c011ec18IteratorInitialCount;
    uint32_t c011ec18LookupCount;
    uint32_t c011ec18FindMethodInfoAttemptCount;
    uint32_t c011ec18FindMethodInfoSuccessCount;
    uint32_t c011ec18FramePointerCalculationCount;
    uint32_t c011ec18UnwindStepCount;
    uint32_t c011ec18StackFrameCount;
    uint32_t c011ec18StackRootSlotCount;
    uint32_t c011ec18StackProviderCallbackCount;
    uint32_t c011ec18StackBoundsConsumed;
    uint32_t c011ec18MethodMetadataValid;
    uint32_t c011ec18MarkerEmitted;
    uint32_t c011ec18TransitionInManagedRange;
    uint32_t c011ec18FailFastReason;
    uint32_t c011ec18Reserved[2];

    uintptr_t c011ec18CurrentNativeRip;
    uintptr_t c011ec18CurrentNativeRsp;
    uintptr_t c011ec18TransitionFrameAddress;
    uintptr_t c011ec18TransitionFrameRip;
    uintptr_t c011ec18TransitionFrameRbp;
    uintptr_t c011ec18TransitionFrameThreadField;
    uintptr_t c011ec18ThreadAddress;
    uintptr_t c011ec18PreviousTransitionFrame;
    uintptr_t c011ec18TransitionFrameFlags;
    uintptr_t c011ec18SavedRbx;
    uintptr_t c011ec18SavedRsi;
    uintptr_t c011ec18SavedRdi;
    uintptr_t c011ec18SavedR12;
    uintptr_t c011ec18SavedR13;
    uintptr_t c011ec18SavedR14;
    uintptr_t c011ec18SavedR15;
    uintptr_t c011ec18SavedRsp;
    uintptr_t c011ec18IteratorControlPc;
    uintptr_t c011ec18IteratorInitialSp;
    uintptr_t c011ec18IteratorInitialFp;
    uintptr_t c011ec18IteratorFrameAddress;
    uintptr_t c011ec18IteratorCodeManager;
    uintptr_t c011ec18IteratorMethodInfo;
    uintptr_t c011ec18IteratorFramePointer;
    uintptr_t c011ec18IteratorUnwindControlPc;
    uintptr_t c011ec18CurrentNativeCodeManager;
    uintptr_t c011ec18CurrentNativeInManagedRange;
    uintptr_t c011ec18TransitionCodeManager;
    uintptr_t c011ec18AuthenticManagedCodeManager;

    /*
     * C011EC19 first genuine NativeAOT unwind / GC-info boundary. These
     * append-only scalar fields distinguish method lookup, unwind metadata,
     * caller state, GC-info decoding, root callbacks, and promotion.
     */
    uint32_t c011ec19UnwindEntryCount;
    uint32_t c011ec19UnwindMetadataCount;
    uint32_t c011ec19UnwindRtlCount;
    uint32_t c011ec19UnwindCompletedCount;
    uint32_t c011ec19UnwindResult;
    uint32_t c011ec19GcInfoLookupCount;
    uint32_t c011ec19GcInfoDecodeAttemptCount;
    uint32_t c011ec19GcInfoDecodeResult;
    uint32_t c011ec19Interruptible;
    uint32_t c011ec19HasInterruptibleRanges;
    uint32_t c011ec19RootReportCount;
    uint32_t c011ec19RegisterRootCount;
    uint32_t c011ec19StackRootCount;
    uint32_t c011ec19FirstRootKind;
    uint32_t c011ec19FirstStackDerivedPromoteAttemptCount;
    uint32_t c011ec19FirstStackDerivedPromoteEntryCount;
    uint32_t c011ec19FirstStackDerivedPromoteReturnCount;
    uint32_t c011ec19SecondQueueInsertionCount;
    uint32_t c011ec19MarkerEmitted;
    uint32_t c011ec19SafeStopReason;
    uint32_t c011ec19PreservedRegisterCount;
    uint32_t c011ec19Reserved[5];

    uintptr_t c011ec19MethodInfo;
    uintptr_t c011ec19RuntimeFunction;
    uintptr_t c011ec19MainRuntimeFunction;
    uintptr_t c011ec19MethodStart;
    uintptr_t c011ec19MethodEnd;
    uintptr_t c011ec19ControlPc;
    uintptr_t c011ec19InputSp;
    uintptr_t c011ec19InputFp;
    uintptr_t c011ec19UnwindInfo;
    uintptr_t c011ec19UnwindInfoSize;
    uintptr_t c011ec19UnwindBlockFlags;
    uintptr_t c011ec19GcInfo;
    uintptr_t c011ec19EhInfo;
    uintptr_t c011ec19CodeOffset;
    uintptr_t c011ec19SafePointAddress;
    uintptr_t c011ec19CallerControlPc;
    uintptr_t c011ec19CallerSp;
    uintptr_t c011ec19CallerFp;
    uintptr_t c011ec19PreviousTransitionFrame;
    uintptr_t c011ec19FirstRootSlot;
    uintptr_t c011ec19FirstRootValue;
    uintptr_t c011ec19FirstRootRegisterSlot;
    uintptr_t c011ec19FirstStackRootSlot;
    uintptr_t c011ec19FirstStackRootValue;
    uintptr_t c011ec19SecondQueueSlot;
    uintptr_t c011ec19SecondQueueCursorBefore;
    uintptr_t c011ec19SecondQueueCursorAfter;
    uintptr_t c011ec19SecondQueueOldValue;
    uintptr_t c011ec19SecondQueueNewValue;

    /*
     * C011EC20 ordinary AMD64 unwind evidence. These fields are append-only:
     * C011EC19 remains the historical first-frame/root boundary above, while
     * C011EC20 records the real transition crossing, RtlVirtualUnwind
     * context, preserved registers, and independent caller validation.
     */
    uint32_t c011ec20TransitionCrossingAttempts;
    uint32_t c011ec20TransitionCrossingResults;
    uint32_t c011ec20UnwindAttemptCount;
    uint32_t c011ec20RtlVirtualUnwindCallCount;
    uint32_t c011ec20RtlVirtualUnwindReturned;
    uint32_t c011ec20UnwindResult;
    uint32_t c011ec20CallerManagedRange;
    uint32_t c011ec20CallerCodeManagerFound;
    uint32_t c011ec20CallerFindMethodInfoAttempts;
    uint32_t c011ec20CallerFindMethodInfoSuccess;
    uint32_t c011ec20CallerGcInfoAttempted;
    uint32_t c011ec20CallerGcInfoResult;
    uint32_t c011ec20CallerSpMoved;
    uint32_t c011ec20CallerSpAligned;
    uint32_t c011ec20CallerFrameDistinct;
    uint32_t c011ec20RestoredRegisterCount;
    uint32_t c011ec20MarkerEmitted;
    uint32_t c011ec20SafeStopReason;
    uint32_t c011ec20Outcome;
    uint32_t c011ec20SensitiveAllocationCount;
    uint32_t c011ec20Reserved[3];

    /*
     * C011EC21 native continuation evidence. These fields are append-only:
     * C011EC19/C20 retain the managed-frame/root chronology above, while
     * C011EC21 records the recovered native helper provenance and the
     * metadata-governed stop at that helper.
     */
    uint32_t c011ec21NativeFrameCandidate;
    uint32_t c011ec21NativeUnwindAttemptCount;
    uint32_t c011ec21NativeUnwindMetadataAvailable;
    uint32_t c011ec21NativeUnwindResult;
    uint32_t c011ec21ManagedReentryFound;
    uint32_t c011ec21ManagedStackBottomProven;
    uint32_t c011ec21TransitionNullInterpretation;
    uint32_t c011ec21TransitionLinkingDefect;
    uint32_t c011ec21MarkerEmitted;
    uint32_t c011ec21Outcome;
    uint32_t c011ec21Reserved[2];

    uintptr_t c011ec20TransitionFrameType;
    uintptr_t c011ec20TransitionFrameAddress;
    uintptr_t c011ec20TransitionSavedRip;
    uintptr_t c011ec20TransitionSavedSp;
    uintptr_t c011ec20TransitionSavedFp;
    uintptr_t c011ec20TransitionThread;
    uintptr_t c011ec20TransitionFlags;
    uintptr_t c011ec20PreviousTransitionFrame;
    uintptr_t c011ec20InputRip;
    uintptr_t c011ec20InputRsp;
    uintptr_t c011ec20InputRbp;
    uintptr_t c011ec20ImageBase;
    uintptr_t c011ec20RuntimeFunction;
    uintptr_t c011ec20BeginRva;
    uintptr_t c011ec20EndRva;
    uintptr_t c011ec20UnwindInfo;
    uintptr_t c011ec20UnwindInfoSize;
    uintptr_t c011ec20UnwindBlockFlags;
    uintptr_t c011ec20OutputRip;
    uintptr_t c011ec20OutputRsp;
    uintptr_t c011ec20OutputRbp;
    uintptr_t c011ec20EstablisherFrame;
    uintptr_t c011ec20HandlerData;
    uintptr_t c011ec20RtlVirtualUnwindResult;
    uintptr_t c011ec20InputRbx;
    uintptr_t c011ec20InputRsi;
    uintptr_t c011ec20InputRdi;
    uintptr_t c011ec20InputR12;
    uintptr_t c011ec20InputR13;
    uintptr_t c011ec20InputR14;
    uintptr_t c011ec20InputR15;
    uintptr_t c011ec20RestoredRbx;
    uintptr_t c011ec20RestoredRsi;
    uintptr_t c011ec20RestoredRdi;
    uintptr_t c011ec20RestoredR12;
    uintptr_t c011ec20RestoredR13;
    uintptr_t c011ec20RestoredR14;
    uintptr_t c011ec20RestoredR15;
    uintptr_t c011ec20CallerCodeManager;
    uintptr_t c011ec20CallerMethodInfo;
    uintptr_t c011ec20CallerMethodStart;
    uintptr_t c011ec20CallerMethodEnd;
    uintptr_t c011ec20CallerRuntimeFunction;
    uintptr_t c011ec20CallerUnwindInfo;
    uintptr_t c011ec20CallerUnwindInfoSize;
    uintptr_t c011ec20CallerUnwindBlockFlags;
    uintptr_t c011ec20CallerGcInfo;

    uintptr_t c011ec21NativeRip;
    uintptr_t c011ec21NativeRsp;
    uintptr_t c011ec21NativeRbp;
    uintptr_t c011ec21NativeHelperStart;
    uintptr_t c011ec21NativeHelperEnd;
    uintptr_t c011ec21NativeFunctionOffset;
    uintptr_t c011ec21NativeCallSite;
    uintptr_t c011ec21NativeModuleIdentity;
    uintptr_t c011ec21NativeSectionIdentity;
    uintptr_t c011ec21NativeRuntimeFunction;
    uintptr_t c011ec21NativeUnwindInfo;

    /*
     * C011EC23 bounded native-module provider evidence. These fields are
     * append-only so the C011EC19-C011EC21 chronology remains decodable.
     * Native frames are recorded here but are never represented as managed
     * code-manager frames.
     */
    uint32_t c011ec23LookupAttemptCount;
    uint32_t c011ec23LookupSuccessCount;
    uint32_t c011ec23UnwindAttemptCount;
    uint32_t c011ec23RtlVirtualUnwindCallCount;
    uint32_t c011ec23RtlVirtualUnwindReturned;
    uint32_t c011ec23UnwindResult;
    uint32_t c011ec23NativeFramesCrossed;
    uint32_t c011ec23ManagedReentryFound;
    uint32_t c011ec23CallerManagedRange;
    uint32_t c011ec23CallerCodeManagerFound;
    uint32_t c011ec23CallerFindMethodInfoAttempts;
    uint32_t c011ec23CallerFindMethodInfoSuccess;
    uint32_t c011ec23RestoredRegisterCount;
    uint32_t c011ec23MarkerEmitted;
    uint32_t c011ec23SafeStopReason;
    uint32_t c011ec23Outcome;
    uint32_t c011ec23SecondFunctionAttempted;
    uint32_t c011ec23SecondFunctionSucceeded;
    uint32_t c011ec23SecondFunctionResult;
    uint32_t c011ec23SecondFunctionIndex;

    uintptr_t c011ec23InputRip;
    uintptr_t c011ec23InputRsp;
    uintptr_t c011ec23InputRbp;
    uintptr_t c011ec23OutputRip;
    uintptr_t c011ec23OutputRsp;
    uintptr_t c011ec23OutputRbp;
    uintptr_t c011ec23EstablisherFrame;
    uintptr_t c011ec23HandlerData;
    uintptr_t c011ec23ModuleBase;
    uintptr_t c011ec23ExecutableStart;
    uintptr_t c011ec23ExecutableEnd;
    uintptr_t c011ec23PdataStart;
    uintptr_t c011ec23PdataEnd;
    uintptr_t c011ec23XdataStart;
    uintptr_t c011ec23XdataEnd;
    uintptr_t c011ec23RuntimeFunction;
    uintptr_t c011ec23UnwindInfo;
    uintptr_t c011ec23BeginAddress;
    uintptr_t c011ec23EndAddress;
    uintptr_t c011ec23UnwindData;
    uint32_t c011ec23UnwindVersion;
    uint32_t c011ec23UnwindFlags;
    uint32_t c011ec23PrologueSize;
    uint32_t c011ec23UnwindCodeCount;
    uint32_t c011ec23FrameRegister;
    uint32_t c011ec23FrameOffset;
    uintptr_t c011ec23CallerCodeManager;
    uintptr_t c011ec23CallerMethodInfo;
    uintptr_t c011ec23CallerMethodStart;
    uintptr_t c011ec23CallerMethodEnd;
    uintptr_t c011ec23RestoredRbx;
    uintptr_t c011ec23RestoredRbp;
    uintptr_t c011ec23RestoredRsi;
    uintptr_t c011ec23RestoredRdi;
    uintptr_t c011ec23RestoredR12;
    uintptr_t c011ec23RestoredR13;
    uintptr_t c011ec23RestoredR14;
    uintptr_t c011ec23RestoredR15;
    uintptr_t c011ec23SecondRuntimeFunction;
    uintptr_t c011ec23SecondUnwindInfo;
    uintptr_t c011ec23SecondOutputRip;
    uintptr_t c011ec23SecondOutputRsp;

    /* C011EC24 caller-provenance evidence; append-only after C011EC23. */
    uint32_t c011ec24MarkerEmitted;
    uint32_t c011ec24PreflightProven;
    uint32_t c011ec24OutputAgreement;
    uint32_t c011ec24CallerValid;
    uint32_t c011ec24CallerKernelRange;
    uint32_t c011ec24CallerManagedRange;
    uint32_t c011ec24StandaloneTests;
    uint32_t c011ec24HelperStandalonePassed;
    uint32_t c011ec24SecondStandalonePassed;
    uint32_t c011ec24SecondProductionUnwindAttempted;
    uint32_t c011ec24UnwindOpcodeCount;
    uint32_t c011ec24StackAdvance;
    uint32_t c011ec24SafeStopReason;
    uint16_t c011ec24OpcodeWords[12];

    uintptr_t c011ec24LiveRsp;
    uintptr_t c011ec24PreflightReturnSlot;
    uintptr_t c011ec24PreflightReturnValue;
    uintptr_t c011ec24PreflightOutputRip;
    uintptr_t c011ec24PreflightOutputRsp;
    uintptr_t c011ec24DerivedReturnSlot;
    uintptr_t c011ec24DerivedReturnValue;
    uintptr_t c011ec24ExpectedCallerRip;
    uintptr_t c011ec24ExpectedCallerRsp;
    uint32_t c011ec24SecondProviderLookupAttempted;
    uint32_t c011ec24SecondProviderLookupSucceeded;
    uint32_t c011ec24SecondUnwindResult;
    uintptr_t c011ec24SecondModuleBase;
    uintptr_t c011ec24SecondExecutableStart;
    uintptr_t c011ec24SecondExecutableEnd;
    uintptr_t c011ec24SecondRuntimeFunction;
    uintptr_t c011ec24SecondUnwindInfo;
    uintptr_t c011ec24SecondInputRip;
    uintptr_t c011ec24SecondInputRsp;
    uintptr_t c011ec24SecondInputRbp;
    uintptr_t c011ec24SecondOutputRip;
    uintptr_t c011ec24SecondOutputRsp;
    uintptr_t c011ec24SecondOutputRbp;
    uintptr_t c011ec24PreRbx;
    uintptr_t c011ec24PreRsi;
    uintptr_t c011ec24PreRdi;
    uintptr_t c011ec24PreRbp;
    uintptr_t c011ec24PreR12;
    uintptr_t c011ec24PreR13;
    uintptr_t c011ec24PreR14;
    uintptr_t c011ec24PreR15;
    uintptr_t c011ec24SourceRbx;
    uintptr_t c011ec24SourceRsi;
    uintptr_t c011ec24SourceRdi;
    uintptr_t c011ec24SourceRbp;
    uintptr_t c011ec24SourceR12;
    uintptr_t c011ec24SourceR13;
    uintptr_t c011ec24SourceR14;
    uintptr_t c011ec24SourceR15;
    uintptr_t c011ec24RecoveredRbx;
    uintptr_t c011ec24RecoveredRsi;
    uintptr_t c011ec24RecoveredRdi;
    uintptr_t c011ec24RecoveredRbp;
    uintptr_t c011ec24RecoveredR12;
    uintptr_t c011ec24RecoveredR13;
    uintptr_t c011ec24RecoveredR14;
    uintptr_t c011ec24RecoveredR15;

    /* C011EC25 kernel-entry boundary evidence; append-only after C24. */
    uint32_t c011ec25MarkerEmitted;
    uint32_t c011ec25PreflightProven;
    uint32_t c011ec25SecondMetadataValid;
    uint32_t c011ec25SecondOutputAgreement;
    uint32_t c011ec25ThirdInKernelRange;
    uint32_t c011ec25ThirdLinkedLookupAttempted;
    uint32_t c011ec25ThirdLinkedLookupSucceeded;
    uint32_t c011ec25ThirdPhysicalLookupAttempted;
    uint32_t c011ec25ThirdPhysicalLookupSucceeded;
    uint32_t c011ec25ThirdMetadataPresent;
    uint32_t c011ec25AssemblyEntryBoundary;
    uint32_t c011ec25NonReturningHandoff;
    uint32_t c011ec25StackBottomProven;
    uint32_t c011ec25SecondOpcodeCount;
    uint32_t c011ec25SecondStackAdvance;
    uint32_t c011ec25ProviderLookupResult;
    uint32_t c011ec25LinkedLookupResult;
    uint32_t c011ec25PhysicalLookupResult;
    uint32_t c011ec25SafeStopReason;
    uint16_t c011ec25SecondOpcodeWords[12];

    uintptr_t c011ec25SecondInputRip;
    uintptr_t c011ec25SecondInputRsp;
    uintptr_t c011ec25SecondInputRbp;
    uintptr_t c011ec25SecondReturnSlot;
    uintptr_t c011ec25SecondReturnValue;
    uintptr_t c011ec25ExpectedCallerRip;
    uintptr_t c011ec25ExpectedCallerRsp;
    uintptr_t c011ec25SecondOutputRip;
    uintptr_t c011ec25SecondOutputRsp;
    uintptr_t c011ec25SecondOutputRbp;
    uintptr_t c011ec25SecondEstablisherFrame;
    uintptr_t c011ec25SecondHandlerData;
    uintptr_t c011ec25SecondRecoveredRbx;
    uintptr_t c011ec25SecondRecoveredRsi;
    uintptr_t c011ec25SecondRecoveredRdi;
    uintptr_t c011ec25SecondRecoveredRbp;
    uintptr_t c011ec25ThirdPhysicalPc;
    uintptr_t c011ec25ThirdLinkedPc;
    uintptr_t c011ec25LinkedEntryPc;
    uintptr_t c011ec25LinkedHaltPc;
    uintptr_t c011ec25BootStackTop;

    /* C011EC26 normal terminal-boundary completion; append-only after C25. */
    uint32_t c011ec26MarkerEmitted;
    uint32_t c011ec26PreflightProven;
    uint32_t c011ec26TerminalLookupAttemptCount;
    uint32_t c011ec26TerminalLookupSuccessCount;
    uint32_t c011ec26TerminalClassificationResult;
    uint32_t c011ec26TerminalDescriptorValid;
    uint32_t c011ec26IteratorCompletionCount;
    uint32_t c011ec26StackProviderCallbackEntryCount;
    uint32_t c011ec26StackProviderCallbackReturnCount;
    uint32_t c011ec26GcScanRootsEntryCount;
    uint32_t c011ec26GcScanRootsReturnCount;
    uint32_t c011ec26ThreadGcScanRootsEntryCount;
    uint32_t c011ec26ThreadGcScanRootsReturnCount;
    uint32_t c011ec26GcScanRootsEnumerationComplete;
    uint32_t c011ec26ThirdUnwindAttemptCount;
    uint32_t c011ec26FirstPostScanEvent;
    uint32_t c011ec26FirstPostScanQueueOperation;
    uint32_t c011ec26FirstPostStackRootSource;
    uint32_t c011ec26PostStackRootSourceCount;
    uint32_t c011ec26StackScanTotalRootCount;
    uint32_t c011ec26StackScanCategory3RootCount;
    uint32_t c011ec26StackScanRegisterRootCount;
    uint32_t c011ec26StackScanStackRootCount;
    uint32_t c011ec26StackScanPromoteAttemptCount;
    uint32_t c011ec26StackScanPromoteEntryCount;
    uint32_t c011ec26StackScanPromoteReturnCount;
    uint32_t c011ec26SafeStopReason;
    uint32_t c011ec26Reserved[2];

    uintptr_t c011ec26TerminalInputPc;
    uintptr_t c011ec26TerminalSelectedPc;
    uintptr_t c011ec26TerminalLinkedPc;
    uintptr_t c011ec26TerminalModuleBase;
    uintptr_t c011ec26TerminalExecutableStart;
    uintptr_t c011ec26TerminalExecutableEnd;
    uintptr_t c011ec26TerminalBeginRva;
    uintptr_t c011ec26TerminalEndRva;
    uintptr_t c011ec26TerminalRsp;
    uintptr_t c011ec26QueueCursorBeforeStack;
    uintptr_t c011ec26QueueCursorAfterStack;
    uintptr_t c011ec26QueueCursorAtGcScanRootsReturn;
    uintptr_t c011ec26PostScanAddress;

    /* C011EC27 first authentic post-root Workstation queue/mark boundary. */
    uint32_t c011ec27AfterGcScanRootsReached;
    uint32_t c011ec27PreflightProven;
    uint32_t c011ec27QueueItemConsumedCount;
    uint32_t c011ec27MarkStateReadCount;
    uint32_t c011ec27MarkStateResult;
    uint32_t c011ec27MarkWriteAttemptCount;
    uint32_t c011ec27MarkWriteCount;
    uint32_t c011ec27ChildScanAttemptCount;
    uint32_t c011ec27ChildReferenceReadCount;
    uint32_t c011ec27ChildPromoteAttemptCount;
    uint32_t c011ec27GraphTraversalCount;
    uint32_t c011ec27NewQueueInsertionCount;
    uint32_t c011ec27OutcomeLevel;
    uint32_t c011ec27MarkerEmitted;
    uint32_t c011ec27SafeStopReason;
    uint32_t c011ec27QueueInvariantFailures;
    uint32_t c011ec27ObjectInvariantFailures;

    uintptr_t c011ec27AfterGcScanRootsAddress;
    uintptr_t c011ec27QueueOwner;
    uintptr_t c011ec27QueueBase;
    uintptr_t c011ec27ConsumedSlot;
    uintptr_t c011ec27ConsumedSlotIndex;
    uintptr_t c011ec27QueueCursorBefore;
    uintptr_t c011ec27QueueCursorAfterConsumption;
    uintptr_t c011ec27ConsumedObject;
    uintptr_t c011ec27ConsumedSlotValueAfter;
    uintptr_t c011ec27MarkWordAddress;
    uintptr_t c011ec27MarkWordBefore;
    uintptr_t c011ec27MarkMask;
    uintptr_t c011ec27MarkWordAfter;
    uintptr_t c011ec27ParentObject;
    uintptr_t c011ec27ParentMethodTable;
    uintptr_t c011ec27ChildSlot;
    uintptr_t c011ec27ChildValue;
    uintptr_t c011ec27QueueInsertionsAtConsumed;
    uintptr_t c011ec27QueueInsertionsAtAfter;

    /* C011EC28 bounded aggregate Workstation mark-queue chronology. */
    uint32_t c011ec28QueueSemanticsValidated;
    uint32_t c011ec28DrainEntryCount;
    uint32_t c011ec28DrainReturnCount;
    uint32_t c011ec28QueueDequeueAttemptCount;
    uint32_t c011ec28QueueSuccessfulDequeueCount;
    uint32_t c011ec28QueueEnqueueAttemptCount;
    uint32_t c011ec28QueueSuccessfulEnqueueCount;
    uint32_t c011ec28AlreadyMarkedSkipCount;
    uint32_t c011ec28QueueWrapCount;
    uint32_t c011ec28QueueDisplacementCount;
    uint32_t c011ec28QueueFullCount;
    uint32_t c011ec28QueueFullResolvedCount;
    uint32_t c011ec28QueueInvariantFailures;
    uint32_t c011ec28QueueOccupancy;
    uint32_t c011ec28QueueMaxOccupancy;
    uint32_t c011ec28QueueEmptyTestCount;
    uint32_t c011ec28QueueEmptyResult;
    uint32_t c011ec28FinalDrainEmptyTestCount;
    uint32_t c011ec28FinalDrainEmptyResult;
    uint32_t c011ec28MarkTestCount;
    uint32_t c011ec28AlreadyMarkedCount;
    uint32_t c011ec28NewlyMarkedCount;
    uint32_t c011ec28MarkWriteCount;
    uint32_t c011ec28ObjectsScanned;
    uint32_t c011ec28ReferenceSlotsVisited;
    uint32_t c011ec28NullReferences;
    uint32_t c011ec28NonNullReferences;
    uint32_t c011ec28ChildPromoteAttemptCount;
    uint32_t c011ec28ChildQueueMarkEntryCount;
    uint32_t c011ec28ChildQueueMarkReturnCount;
    uint32_t c011ec28ChildQueueInsertionCount;
    uint32_t c011ec28ChildQueuePending;
    uint32_t c011ec28CurrentObjectChildSlots;
    uint32_t c011ec28CurrentObjectChildEnqueues;
    uint32_t c011ec28DrainCurrentEmptyTestCount;
    uint32_t c011ec28DrainCurrentSuccessfulDequeueCount;
    uint32_t c011ec28DrainCurrentEmptyResult;
    uint32_t c011ec28DrainLastObjectChildSlots;
    uint32_t c011ec28DrainLastObjectChildEnqueues;
    uint32_t c011ec28DrainFinalObjectMarkState;
    uint32_t c011ec28DrainFinalObjectNewlyMarked;
    uint32_t c011ec28MarkDisplacementResolvedCount;
    uint32_t c011ec28MarkDisplacementPending;
    uint32_t c011ec28InitialStateCaptured;

    uintptr_t c011ec28InitialCursor;
    uintptr_t c011ec28InitialHead;
    uintptr_t c011ec28InitialTail;
    uintptr_t c011ec28InitialCount;
    uintptr_t c011ec28FinalCursor;
    uintptr_t c011ec28FinalHead;
    uintptr_t c011ec28FinalTail;
    uintptr_t c011ec28FinalCount;
    uintptr_t c011ec28InitialQueueBase;
    uintptr_t c011ec28FinalQueueBase;
    uintptr_t c011ec28CurrentObject;
    uintptr_t c011ec28CurrentMethodTable;
    uintptr_t c011ec28FinalDequeuedObject;
    uintptr_t c011ec28FinalDequeuedSlot;
    uintptr_t c011ec28FinalDequeuedIndex;
    uintptr_t c011ec28FinalDequeuedCursorBefore;
    uintptr_t c011ec28FinalDequeuedCursorAfter;
    uintptr_t c011ec28FinalScannedObject;
    uintptr_t c011ec28FinalScannedMarkWordAddress;
    uintptr_t c011ec28FinalScannedMarkWordBefore;
    uintptr_t c011ec28FinalScannedMarkWordAfter;
    uintptr_t c011ec28FinalScannedMarkMask;
    uintptr_t c011ec28RepresentativeLaterObject;
    uintptr_t c011ec28RepresentativeLaterMarkWordAddress;
    uintptr_t c011ec28RepresentativeLaterMarkWordBefore;
    uintptr_t c011ec28RepresentativeLaterMarkWordAfter;
    uintptr_t c011ec28RepresentativeLaterMarkMask;
    uintptr_t c011ec28FirstScanParent;
    uintptr_t c011ec28FirstScanMethodTable;
    uintptr_t c011ec28FirstScanFirstChild;
    uintptr_t c011ec28LaterScanParent;
    uintptr_t c011ec28LaterScanMethodTable;
    uintptr_t c011ec28FinalScanParent;
    uintptr_t c011ec28FinalScanMethodTable;
    uintptr_t c011ec28FinalScanFirstChild;
    uintptr_t c011ec28LastMarkObject;
    uintptr_t c011ec28LastMarkWordAddress;
    uintptr_t c011ec28LastMarkWordBefore;
    uintptr_t c011ec28LastMarkWordAfter;
    uintptr_t c011ec28LastMarkMask;
    uintptr_t c011ec28NextProductionBoundary;

    /* C011EC29 first post-mark short-weak handle phase boundary. */
    uint32_t c011ec29AfterGcScanRootsEntryCount;
    uint32_t c011ec29AfterGcScanRootsReturnCount;
    uint32_t c011ec29PreflightProven;
    uint32_t c011ec29NextPhaseEntryCount;
    uint32_t c011ec29ShortWeakHandleScanEntryCount;
    uint32_t c011ec29HandleMapReadCount;
    uint32_t c011ec29FirstMutationAttempted;
    uint32_t c011ec29MarkerEmitted;
    uint32_t c011ec29SafeStopReason;
    uint32_t c011ec29HandleTableMapBucketPresent;
    uint32_t c011ec29HandleScanFlags;
    uint32_t c011ec29CondemnedGeneration;
    uint32_t c011ec29MaximumGeneration;
    uint32_t c011ec29GenerationCount;
    uint32_t c011ec29HeapCount;
    uint32_t c011ec29HeapNumber;
    uint32_t c011ec29CollectionReason;
    uint32_t c011ec29Compacting;
    uint32_t c011ec29Promotion;
    uint32_t c011ec29FullCollection;
    uint32_t c011ec29EeSuspended;
    uint32_t c011ec29Cooperative;
    uint32_t c011ec29Preemptive;
    uint32_t c011ec29ThreadStoreLockHeld;
    uint32_t c011ec29ThreadStoreRecursion;
    uint32_t c011ec29ManagedEntryProhibited;
    uint32_t c011ec29ManagedEntryAttempts;
    uint32_t c011ec29SensitiveAllocationCount;
    uint32_t c011ec29PendingQueueAtTransition;
    uint32_t c011ec29MarkPendingAtTransition;
    uint32_t c011ec29RestartCount;
    uint32_t c011ec29ResumeCount;

    uintptr_t c011ec29AfterGcScanRootsEntryAddress;
    uintptr_t c011ec29AfterGcScanRootsReturnAddress;
    uintptr_t c011ec29ScanContext;
    uintptr_t c011ec29NextPhaseCallSite;
    uintptr_t c011ec29HeapAddress;
    uintptr_t c011ec29FirstHandleScanAddress;
    uintptr_t c011ec29FirstHandleTableMapAddress;
    uintptr_t c011ec29FirstHandleTableMapBucketsFieldAddress;
    uintptr_t c011ec29FirstHandleTableMapBucketsValue;
    uintptr_t c011ec29FirstHandleTableMapMaxIndex;
    uintptr_t c011ec29FirstOperationAddress;
    uintptr_t c011ec29ThreadStoreLockOwner;
    uintptr_t c011ec29FirstMutationTarget;
    uintptr_t c011ec29FirstMutationBefore;
    uintptr_t c011ec29FirstMutationAfter;

    /* C011EC30 first authentic short-weak handle-table operation. */
    uint32_t c011ec30HandleScanEntryCount;
    uint32_t c011ec30HandleMapReadCount;
    uint32_t c011ec30BucketVisitCount;
    uint32_t c011ec30HandleTableVisitCount;
    uint32_t c011ec30SegmentVisitCount;
    uint32_t c011ec30BlockVisitCount;
    uint32_t c011ec30HandleSlotInspectCount;
    uint32_t c011ec30NullHandleCount;
    uint32_t c011ec30CandidateHandleCount;
    uint32_t c011ec30LivenessCheckCount;
    uint32_t c011ec30LivenessDecisionCount;
    uint32_t c011ec30LiveDecisionCount;
    uint32_t c011ec30DeadDecisionCount;
    uint32_t c011ec30MutationAttemptCount;
    uint32_t c011ec30ClearedCount;
    uint32_t c011ec30PreservedCount;
    uint32_t c011ec30PreflightProven;
    uint32_t c011ec30NoHandleCompletion;
    uint32_t c011ec30MarkerEmitted;
    uint32_t c011ec30SafeStopReason;
    uint32_t c011ec30CondemnedGeneration;
    uint32_t c011ec30MaximumGeneration;
    uint32_t c011ec30HandleScanFlags;
    uint32_t c011ec30FirstBlockType;
    uint32_t c011ec30FirstBlockIndex;
    uint32_t c011ec30FirstSlotIndex;
    uint32_t c011ec30FirstGenerationWord;
    uint32_t c011ec30FirstAgeMask;
    uint32_t c011ec30FirstDecisionPromoted;
    uint32_t c011ec30FirstTargetMarked;
    uint32_t c011ec30FirstTargetInCondemnedGeneration;
    uint32_t c011ec30DiagnosticMutationCount;
    uint32_t c011ec30CallbackDispatchCount;
    uint32_t c011ec30ProductionCallbackEntryCount;
    uint32_t c011ec30Reserved[1];

    uintptr_t c011ec30ScanContext;
    uintptr_t c011ec30FirstHandleScanAddress;
    uintptr_t c011ec30FirstHandleTableMapAddress;
    uintptr_t c011ec30FirstHandleTableMapBucketsFieldAddress;
    uintptr_t c011ec30FirstHandleTableMapBucketsValue;
    uintptr_t c011ec30FirstHandleTableMapMaxIndex;
    uintptr_t c011ec30FirstBucketAddress;
    uintptr_t c011ec30FirstBucketTableArray;
    uintptr_t c011ec30FirstTableAddress;
    uintptr_t c011ec30FirstSegmentAddress;
    uintptr_t c011ec30FirstNextSegmentAddress;
    uintptr_t c011ec30FirstBlockAddress;
    uintptr_t c011ec30FirstSlotAddress;
    uintptr_t c011ec30FirstSlotBefore;
    uintptr_t c011ec30FirstSlotAfter;
    uintptr_t c011ec30FirstTarget;
    uintptr_t c011ec30FirstMarkWordAddress;
    uintptr_t c011ec30FirstMarkWordBefore;
    uintptr_t c011ec30FirstDecisionAddress;
    uintptr_t c011ec30FirstCallbackAddress;
    uintptr_t c011ec30FirstProductionCallbackEntryAddress;
    uintptr_t c011ec30ExpectedCallbackAddress;
    uintptr_t c011ec30FirstOperationAddress;
    uintptr_t c011ec30FirstTableIndex;
    uintptr_t c011ec30FirstCpuIndex;

    /* C011EC31 one genuine live short-weak handle. */
    uint32_t c011ec31AllocationEntryCount;
    uint32_t c011ec31AllocationCount;
    uint32_t c011ec31WeakHandleAllocationCallbackCount;
    uint32_t c011ec31HandleType;
    uint32_t c011ec31StrongRootCandidateCount;
    uint32_t c011ec31StrongHandlePromotionCount;
    uint32_t c011ec31StrongRootMatched;
    uint32_t c011ec31ProofHandleMatched;
    uint32_t c011ec31PreflightProven;
    uint32_t c011ec31LivenessResult;
    uint32_t c011ec31MarkMask;
    uint32_t c011ec31MarkStateBefore;
    uint32_t c011ec31CondemnedGeneration;
    uint32_t c011ec31TargetGeneration;
    uint32_t c011ec31MutationAttempted;
    uint32_t c011ec31ClearingStore;
    uint32_t c011ec31PreservedCount;
    uint32_t c011ec31ClearedCount;
    uint32_t c011ec31UnexpectedWeakRooting;
    uint32_t c011ec31SensitiveAllocationCount;
    uint32_t c011ec31Reserved[1];

    uintptr_t c011ec31AllocationEntryAddress;
    uintptr_t c011ec31Target;
    uintptr_t c011ec31StrongRootSlot;
    uintptr_t c011ec31StrongRootValueBefore;
    uintptr_t c011ec31WeakHandleSlot;
    uintptr_t c011ec31WeakHandleValueBefore;
    uintptr_t c011ec31HandleTableAddress;
    uintptr_t c011ec31SegmentAddress;
    uintptr_t c011ec31BlockAddress;
    uintptr_t c011ec31BlockFirstSlotAddress;
    uintptr_t c011ec31SlotAddress;
    uintptr_t c011ec31SlotBefore;
    uintptr_t c011ec31SlotAfter;
    uintptr_t c011ec31MarkWordAddress;
    uintptr_t c011ec31MarkWordBefore;
    uintptr_t c011ec31LivenessCallbackEntryAddress;
    uintptr_t c011ec31LivenessDecisionAddress;
    uintptr_t c011ec31CurrentSegmentAddress;
    uintptr_t c011ec31CurrentBlockFirstSlotAddress;
    uint32_t c011ec31CurrentBlockIndex;
    uint32_t c011ec31CurrentBlockType;

    /* C011EC32 one genuine dead short-weak handle. */
    uint32_t c011ec32AllocationEntryCount;
    uint32_t c011ec32AllocationCount;
    uint32_t c011ec32WeakHandleAllocationCallbackCount;
    uint32_t c011ec32HandleType;
    uint32_t c011ec32HelperReturned;
    uint32_t c011ec32StrongRootMatchCount;
    uint32_t c011ec32StackRootMatchCount;
    uint32_t c011ec32RegisterRootMatchCount;
    uint32_t c011ec32OrdinaryRootMatchCount;
    uint32_t c011ec32StaticThreadStaticRootMatchCount;
    uint32_t c011ec32ThreadAbortRootMatchCount;
    uint32_t c011ec32StrongHandleMatchCount;
    uint32_t c011ec32GraphDerivedPromotionCount;
    uint32_t c011ec32TargetQueueInsertionCount;
    uint32_t c011ec32TargetChildDiscoveryCount;
    uint32_t c011ec32TargetMarkWriteCount;
    uint32_t c011ec32ProofHandleMatched;
    uint32_t c011ec32BucketIndex;
    uint32_t c011ec32PreflightProven;
    uint32_t c011ec32LivenessResult;
    uint32_t c011ec32MarkMask;
    uint32_t c011ec32MarkStateBefore;
    uint32_t c011ec32CondemnedGeneration;
    uint32_t c011ec32TargetGeneration;
    uint32_t c011ec32MutationAttempted;
    uint32_t c011ec32ClearingStore;
    uint32_t c011ec32ClearedCount;
    uint32_t c011ec32PreservedCount;
    uint32_t c011ec32UnexpectedWeakRooting;
    uint32_t c011ec32SensitiveAllocationCount;
    uint32_t c011ec32Reserved[2];

    uintptr_t c011ec32AllocationEntryAddress;
    uintptr_t c011ec32HelperReturnAddress;
    uintptr_t c011ec32Target;
    uintptr_t c011ec32TargetType;
    uintptr_t c011ec32WeakHandleSlot;
    uintptr_t c011ec32WeakHandleValueBefore;
    uintptr_t c011ec32HandleTableAddress;
    uintptr_t c011ec32SegmentAddress;
    uintptr_t c011ec32BlockAddress;
    uintptr_t c011ec32BlockFirstSlotAddress;
    uintptr_t c011ec32SlotAddress;
    uintptr_t c011ec32SlotBefore;
    uintptr_t c011ec32SlotAfter;
    uintptr_t c011ec32MarkWordAddress;
    uintptr_t c011ec32MarkWordBefore;
    uintptr_t c011ec32LivenessCallbackEntryAddress;
    uintptr_t c011ec32LivenessDecisionAddress;
    uintptr_t c011ec32ClearingStoreAddress;
    uintptr_t c011ec32CurrentSegmentAddress;
    uintptr_t c011ec32CurrentBlockFirstSlotAddress;
    uint32_t c011ec32CurrentBlockIndex;
    uint32_t c011ec32CurrentBlockType;

    /* C011EC33 one target/one short-weak handle across two collections. */
    uint32_t c011ec33ActiveCollection;
    uint32_t c011ec33GcScanRootsEntryCount;
    uint32_t c011ec33GcScanRootsReturnCount;
    uint32_t c011ec33AfterGcScanRootsEntryCount;
    uint32_t c011ec33AfterGcScanRootsReturnCount;
    uint32_t c011ec33WeakHandleAllocationCount;
    uint32_t c011ec33HelperReturned;
    uint32_t c011ec33LifetimeBoundaryReturned;
    uint32_t c011ec33Collection1Completed;
    uint32_t c011ec33GcDoneCount;
    uint32_t c011ec33RestartEntryCount;
    uint32_t c011ec33RestartReturnCount;
    uint32_t c011ec33ManagedResumeCount;
    uint32_t c011ec33TargetRelocated;
    uint32_t c011ec33PreflightProven;
    uint32_t c011ec33MarkerEmitted;
    uint32_t c011ec33SafeStopReason;
    uint32_t c011ec33LastPostWeakPhase;
    uint32_t c011ec33PostWeakPhaseCount;

    /* C011EC37 repeated-collection completion evidence. */
    uint32_t c011ec37PreflightEmitted;
    uint32_t c011ec37ManagedMarkerEmitted;
    uint32_t c011ec37CompletionMarkerEmitted;
    uint32_t c011ec37ManagedCheckpoint;
    uint32_t c011ec37DeadTargetRerootCount;
    uint32_t c011ec37StaleWeakPointerCount;
    uint32_t c011ec37SafeStopReason;
    uintptr_t c011ec37ManagedContinuationControlPc;

    uintptr_t c011ec33InitialTarget;
    uintptr_t c011ec33TargetType;
    uintptr_t c011ec33WeakHandleSlot;
    uintptr_t c011ec33InitialWeakValue;
    uintptr_t c011ec33TargetAfterCollection1;
    uintptr_t c011ec33HelperReturnAddress;
    uintptr_t c011ec33LifetimeBoundaryAddress;
    uintptr_t c011ec33LastPostWeakPhaseAddress;

    guidexos_nativeaot_c011ec33_collection_record c011ec33Collections[2];

    /* C011EC34 relocation-root update evidence. */
    guidexos_nativeaot_c011ec34_relocation_record c011ec34Relocation;
    /* C011EC35 surviving short-weak handle relocation evidence. */
    guidexos_nativeaot_c011ec35_relocated_handle_record c011ec35RelocatedHandle;
    /* C011EC38 dead-object physical reclamation/reuse evidence. */
    guidexos_nativeaot_c011ec38_reclamation_record c011ec38Reclamation;
    /* C011EC39 authoritative Collection-2 planner provenance. */
    guidexos_nativeaot_c011ec39_plan_record c011ec39Plan;
    /* C011EC40 authentic compacting reclamation provenance. */
    guidexos_nativeaot_c011ec40_compaction_record c011ec40Compaction;
    /* C011EC41 authentic post-GC allocation-context provenance. */
    guidexos_nativeaot_c011ec41_provenance_record c011ec41Provenance;
    /* C011EC42 bounded natural post-Collection-3 lifecycle evidence. */
    guidexos_nativeaot_c011ec42_lifecycle_record c011ec42Lifecycle;
    /* C011EC44 malformed transition-frame provenance. */
    guidexos_nativeaot_c011ec44_provenance_record c011ec44Provenance;
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
    GUIDEXOS_NATIVEAOT_ALLOC_STAGE_F29_FIRST_ROOT_PRE_MARK_BOUNDARY_SAFE_STOP = 0xF29u,
    GUIDEXOS_NATIVEAOT_ALLOC_STAGE_F30_FIRST_ROOT_FIRST_MARK_MUTATION_SAFE_STOP = 0xF30u,
    GUIDEXOS_NATIVEAOT_ALLOC_STAGE_F31_FIRST_ROOT_POST_QUEUE_MARK_DECISION_SAFE_STOP = 0xF31u,
    GUIDEXOS_NATIVEAOT_ALLOC_STAGE_F32_FIRST_ROOT_FIRST_NON_NULL_OLD_O_SAFE_STOP = 0xF32u,
    GUIDEXOS_NATIVEAOT_ALLOC_STAGE_F33_NEXT_GENUINE_ROOT_PROVIDER_SAFE_STOP = 0xF33u,
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
    GUIDEXOS_NATIVEAOT_FIRST_ROOT_PRE_MARK_BOUNDARY_SAFE_STOP_MARKER = 0xC011EC11u,
    GUIDEXOS_NATIVEAOT_FIRST_ROOT_FIRST_MARK_MUTATION_SAFE_STOP_MARKER = 0xC011EC12u,
    GUIDEXOS_NATIVEAOT_FIRST_ROOT_POST_QUEUE_MARK_DECISION_SAFE_STOP_MARKER = 0xC011EC13u,
    GUIDEXOS_NATIVEAOT_FIRST_ROOT_FIRST_NON_NULL_OLD_O_SAFE_STOP_MARKER = 0xC011EC14u,
    GUIDEXOS_NATIVEAOT_NEXT_GENUINE_ROOT_PROVIDER_SAFE_STOP_MARKER = 0xC011EC15u,
    GUIDEXOS_NATIVEAOT_TRANSITION_FRAME_CONTROL_PC_MARKER = 0xC011EC18u,
    GUIDEXOS_NATIVEAOT_UNWIND_GC_INFO_BOUNDARY_MARKER = 0xC011EC19u,
    GUIDEXOS_NATIVEAOT_CALLER_FRAME_UNWIND_MARKER = 0xC011EC20u,
    GUIDEXOS_NATIVEAOT_SINGLE_THREAD_SUSPEND_EE_NEXT_GC_START_WORK = 1u,
    GUIDEXOS_NATIVEAOT_SINGLE_THREAD_SUSPEND_EE_NEXT_POST_DISABLE = 2u,
};

#ifdef __cplusplus
}
#endif
