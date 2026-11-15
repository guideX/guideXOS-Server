#pragma once

#include "guidexos_nativeaot_failfast_origin.h"

enum {
    kGuideXosR29EvalThreadStore = 1u << 0,
    kGuideXosR29EvalAllocationContext = 1u << 1,
    kGuideXosR29EvalGcHeapInitialization = 1u << 2,
    kGuideXosR29EvalGcHeapPointer = 1u << 3,
    kGuideXosR29EvalStateOutputs = 1u << 4,
};

struct GuideXosR29GcStateReadEvidence {
    uint64_t prerequisiteEvaluationMask;
    uint64_t threadStoreResult;
    uint64_t allocationContextResult;
    uint64_t gcHeapInitializationResult;
    uint64_t safeReadResult;
    uint64_t observedCurrentThread;
    uint64_t observedGcHeap;
};

struct GuideXosR29PreallocationGcStateEvidence {
    uint64_t firstFailedSubcondition;
    uint64_t gcStateReadStatus;
    uint64_t prerequisiteEvaluationMask;
    uint64_t threadStoreResult;
    uint64_t allocationContextResult;
    uint64_t gcHeapInitializationResult;
    uint64_t safeReadResult;
    uint64_t observedCurrentThread;
    uint64_t observedGcHeap;
};

static inline GuideXosR29PreallocationGcStateEvidence
guideXosR29ClassifyPreallocationGcState(
    int32_t readStatus,
    const GuideXosR29GcStateReadEvidence& read,
    uintptr_t currentThread,
    uintptr_t gcHeap) {
    GuideXosR29PreallocationGcStateEvidence evidence = {};
    evidence.gcStateReadStatus = static_cast<uint64_t>(static_cast<int64_t>(readStatus));
    evidence.prerequisiteEvaluationMask = read.prerequisiteEvaluationMask;
    evidence.threadStoreResult = read.threadStoreResult;
    evidence.allocationContextResult = read.allocationContextResult;
    evidence.gcHeapInitializationResult = read.gcHeapInitializationResult;
    evidence.safeReadResult = read.safeReadResult;
    evidence.observedCurrentThread = read.observedCurrentThread;
    evidence.observedGcHeap = read.observedGcHeap;

    if (read.safeReadResult == kGuideXosR29SafeReadFailed) {
        evidence.firstFailedSubcondition = kGuideXosR29GuardSafeReadFailed;
        return evidence;
    }

    if (readStatus != 0) {
        if (read.threadStoreResult == kGuideXosR29CheckUnavailable) {
            evidence.firstFailedSubcondition = kGuideXosR29GuardThreadStoreUnavailable;
        } else if (read.allocationContextResult == kGuideXosR29CheckUnavailable) {
            evidence.firstFailedSubcondition = kGuideXosR29GuardAllocationContextUnavailable;
        } else if (read.gcHeapInitializationResult == kGuideXosR29CheckUnavailable) {
            evidence.firstFailedSubcondition = kGuideXosR29GuardGcHeapUninitialized;
        } else if ((read.prerequisiteEvaluationMask & kGuideXosR29EvalGcHeapPointer) != 0u &&
                   read.observedGcHeap == 0u) {
            evidence.firstFailedSubcondition = kGuideXosR29GuardGcHeapPointerUnavailable;
        } else {
            evidence.firstFailedSubcondition = kGuideXosR29GuardStateReadFailed;
        }
        return evidence;
    }

    if (read.safeReadResult != kGuideXosR29SafeReadCompleted ||
        (read.prerequisiteEvaluationMask & kGuideXosR29EvalStateOutputs) == 0u) {
        evidence.firstFailedSubcondition = kGuideXosR29GuardStateReadIncomplete;
        return evidence;
    }

    evidence.prerequisiteEvaluationMask |= kGuideXosR29EvalStateOutputs;
    if (currentThread == 0u) {
        evidence.firstFailedSubcondition = kGuideXosR29GuardCurrentThreadOutputNull;
    } else if (gcHeap == 0u) {
        evidence.firstFailedSubcondition = kGuideXosR29GuardGcHeapOutputNull;
    } else {
        evidence.firstFailedSubcondition = kGuideXosR29GuardSubconditionNone;
    }
    return evidence;
}
