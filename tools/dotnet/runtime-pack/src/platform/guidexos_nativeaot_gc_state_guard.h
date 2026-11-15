#pragma once

#include <stdint.h>

#if defined(GUIDEXOS_NATIVEAOT_C188R28_GC_STATE_GUARD_DISCRIMINATOR)
#include "guidexos_nativeaot_failfast_origin.h"

// This classifier is shared by the production guard and the hosted contract
// tests. It only compares captured scalar values and never dereferences them.
static inline GuideXosR28GcStateGuardEvidence
guideXosR28ClassifyGcStateGuard(
    int32_t gcStateReadStatus,
    uintptr_t expectedCurrentThread,
    uintptr_t observedCurrentThread,
    uintptr_t expectedGcHeap,
    uintptr_t observedGcHeap) {
    GuideXosR28GcStateGuardEvidence evidence = {};
    evidence.gcStateReadStatus = static_cast<uint64_t>(
        static_cast<int64_t>(gcStateReadStatus));
    evidence.expectedCurrentThread = expectedCurrentThread;
    evidence.expectedGcHeap = expectedGcHeap;

    // Identity outputs are unavailable when the prerequisite read failed.
    // Return immediately so no later comparison is attributed to that read.
    if (gcStateReadStatus != 0) {
        evidence.firstFailedSubcondition = kGuideXosR28GuardGcStateReadFailed;
        return evidence;
    }

    evidence.observedCurrentThread = observedCurrentThread;
    evidence.observedGcHeap = observedGcHeap;
    const bool threadMatches = observedCurrentThread == expectedCurrentThread;
    const bool heapMatches = observedGcHeap == expectedGcHeap;
    evidence.threadIdentityResult = threadMatches
        ? kGuideXosR28PredicateMatched : kGuideXosR28PredicateMismatched;
    evidence.heapIdentityResult = heapMatches
        ? kGuideXosR28PredicateMatched : kGuideXosR28PredicateMismatched;

    if (!threadMatches) {
        evidence.firstFailedSubcondition =
            kGuideXosR28GuardThreadIdentityMismatch;
    } else if (!heapMatches) {
        evidence.firstFailedSubcondition =
            kGuideXosR28GuardHeapIdentityMismatch;
    }
    return evidence;
}
#endif
