#pragma once

#include <stdint.h>

static constexpr uint64_t kGuideXosR25FailFastOriginMagic = 0x4738523235464154ull;
static constexpr uint64_t kGuideXosR25FailFastOriginPublished = 0x5055424C49534844ull;
static constexpr uint32_t kGuideXosR25FailFastFastFailCode = 7u;
static constexpr uint32_t kGuideXosR25FailFastStageUnknown = 0u;
static constexpr uint32_t kGuideXosR25FailFastStageNativeModulePreparation = 1u;
static constexpr uint32_t kGuideXosR25FailFastStageBeforeInitializeModules = 2u;
static constexpr uint32_t kGuideXosR25FailFastStageInitializeModulesReturned = 3u;

#if defined(GUIDEXOS_NATIVEAOT_C188R29_PREALLOCATION_GC_STATE_GUARD)
static constexpr uint64_t kGuideXosR25FailFastOriginSchema = 3u;
static constexpr uint64_t kGuideXosR25FailFastOriginRecordBytes = 192u;
static constexpr uint64_t kGuideXosR29GuardLocationNone = 0u;
static constexpr uint64_t kGuideXosR29GuardLocationPreallocation = 1u;
static constexpr uint64_t kGuideXosR29GuardLocationPostallocation = 2u;
static constexpr uint64_t kGuideXosR29GuardSubconditionNone = 0u;
static constexpr uint64_t kGuideXosR29GuardThreadStoreUnavailable = 1u;
static constexpr uint64_t kGuideXosR29GuardAllocationContextUnavailable = 2u;
static constexpr uint64_t kGuideXosR29GuardGcHeapUninitialized = 3u;
static constexpr uint64_t kGuideXosR29GuardGcHeapPointerUnavailable = 4u;
static constexpr uint64_t kGuideXosR29GuardStateReadFailed = 5u;
static constexpr uint64_t kGuideXosR29GuardCurrentThreadOutputNull = 6u;
static constexpr uint64_t kGuideXosR29GuardGcHeapOutputNull = 7u;
static constexpr uint64_t kGuideXosR29GuardSafeReadFailed = 8u;
static constexpr uint64_t kGuideXosR29GuardStateReadIncomplete = 9u;
static constexpr uint64_t kGuideXosR29CheckNotEvaluated = 0u;
static constexpr uint64_t kGuideXosR29CheckAvailable = 1u;
static constexpr uint64_t kGuideXosR29CheckUnavailable = 2u;
static constexpr uint64_t kGuideXosR29CheckCompleted = 3u;
static constexpr uint64_t kGuideXosR29SafeReadNotEvaluated = 0u;
static constexpr uint64_t kGuideXosR29SafeReadCompleted = 1u;
static constexpr uint64_t kGuideXosR29SafeReadFailed = 2u;

// R29 extends the R28 capture layout, so the post-allocation R28
// discriminator remains available in the same build profile.
static constexpr uint64_t kGuideXosR28GuardSubconditionNone = 0u;
static constexpr uint64_t kGuideXosR28GuardGcStateReadFailed = 1u;
static constexpr uint64_t kGuideXosR28GuardThreadIdentityMismatch = 2u;
static constexpr uint64_t kGuideXosR28GuardHeapIdentityMismatch = 3u;
static constexpr uint64_t kGuideXosR28PredicateNotEvaluated = 0u;
static constexpr uint64_t kGuideXosR28PredicateMatched = 1u;
static constexpr uint64_t kGuideXosR28PredicateMismatched = 2u;

struct GuideXosR28GcStateGuardEvidence {
    uint64_t firstFailedSubcondition;
    uint64_t gcStateReadStatus;
    uint64_t threadIdentityResult;
    uint64_t heapIdentityResult;
    uint64_t expectedCurrentThread;
    uint64_t observedCurrentThread;
    uint64_t expectedGcHeap;
    uint64_t observedGcHeap;
};

#elif defined(GUIDEXOS_NATIVEAOT_C188R28_GC_STATE_GUARD_DISCRIMINATOR)
static constexpr uint64_t kGuideXosR25FailFastOriginSchema = 2u;
static constexpr uint64_t kGuideXosR25FailFastOriginRecordBytes = 144u;
static constexpr uint64_t kGuideXosR28GuardSubconditionNone = 0u;
static constexpr uint64_t kGuideXosR28GuardGcStateReadFailed = 1u;
static constexpr uint64_t kGuideXosR28GuardThreadIdentityMismatch = 2u;
static constexpr uint64_t kGuideXosR28GuardHeapIdentityMismatch = 3u;
static constexpr uint64_t kGuideXosR28PredicateNotEvaluated = 0u;
static constexpr uint64_t kGuideXosR28PredicateMatched = 1u;
static constexpr uint64_t kGuideXosR28PredicateMismatched = 2u;

struct GuideXosR28GcStateGuardEvidence {
    uint64_t firstFailedSubcondition;
    uint64_t gcStateReadStatus;
    uint64_t threadIdentityResult;
    uint64_t heapIdentityResult;
    uint64_t expectedCurrentThread;
    uint64_t observedCurrentThread;
    uint64_t expectedGcHeap;
    uint64_t observedGcHeap;
};
#else
static constexpr uint64_t kGuideXosR25FailFastOriginSchema = 1u;
static constexpr uint64_t kGuideXosR25FailFastOriginRecordBytes = 80u;
#endif

// Ten fixed-width words make this record directly readable with a bounded QEMU
// HMP memory query. It owns no NativeAOT, managed, GC, or transition-frame data.
struct GuideXosR25FailFastOriginRecord {
    uint64_t magic;
    uint64_t schemaVersion;
    uint64_t recordBytes;
    uint64_t originalReason;
    uint64_t fastFailCode;
    uint64_t callerCaptureReturnAddress;
    uint64_t failFastImplementationAddress;
    uint64_t initializationStage;
    uint64_t flags;
#if defined(GUIDEXOS_NATIVEAOT_C188R28_GC_STATE_GUARD_DISCRIMINATOR)
    uint64_t guardFailureSubcondition;
    uint64_t gcStateReadStatus;
    uint64_t threadIdentityResult;
    uint64_t heapIdentityResult;
    uint64_t expectedCurrentThread;
    uint64_t observedCurrentThread;
    uint64_t expectedGcHeap;
    uint64_t observedGcHeap;
#if defined(GUIDEXOS_NATIVEAOT_C188R29_PREALLOCATION_GC_STATE_GUARD)
    uint64_t r29GuardLocation;
    uint64_t r29PrerequisiteEvaluationMask;
    uint64_t r29ThreadStoreResult;
    uint64_t r29AllocationContextResult;
    uint64_t r29GcHeapInitializationResult;
    uint64_t r29SafeReadResult;
#endif
#endif
    uint64_t published;
};

static_assert(sizeof(GuideXosR25FailFastOriginRecord) ==
        kGuideXosR25FailFastOriginRecordBytes,
    "first-failure record size must match its bounded schema");
