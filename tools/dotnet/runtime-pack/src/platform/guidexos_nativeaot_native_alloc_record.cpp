#include "guidexos_nativeaot_native_alloc_record.h"

#if defined(GUIDEXOS_NATIVEAOT_C188R12R4R5R4R5R2R2_NATIVE_ALLOC)
namespace {

struct NativeAllocationAttempt {
    uint32_t site;
    uint32_t state;
    uint64_t requestSize;
    uint64_t alignment;
    uint32_t newOperation;
    uintptr_t callsite;
    uint32_t backingAllocator;
    uint64_t mallocSize;
    uint64_t backingTotal;
    uint32_t palReadiness;
    uint32_t palCallbackEntered;
    uintptr_t backendPointer;
    uint32_t finalPointerNonNull;
    uint32_t constructorEntered;
    uint32_t failureOperation;
    uint32_t failureBranch;
    uint32_t failureStatus;
    uint32_t failureStatusSource;
};

struct NativeAllocationRecord {
    uint32_t magic;
    uint32_t version;
    uint32_t attemptCount;
    uint32_t overflow;
    uint32_t firstFailedAttempt;
    uint32_t firstFailureOperation;
    uint32_t firstFailureBranch;
    uint32_t firstFailureStatus;
    uint32_t firstFailureStatusSource;
    uint32_t activeAttempt;
    NativeAllocationAttempt attempts[GX_NATIVE_ALLOC_ATTEMPT_CAPACITY];
};

NativeAllocationRecord g_record = {
    GX_NATIVE_ALLOC_RECORD_MAGIC,
    GX_NATIVE_ALLOC_RECORD_VERSION,
    0u,
    0u,
    GX_NATIVE_ALLOC_VALUE_UNAVAILABLE,
    GX_NATIVE_ALLOC_VALUE_UNAVAILABLE,
    GX_NATIVE_ALLOC_VALUE_UNAVAILABLE,
    GX_NATIVE_ALLOC_VALUE_UNAVAILABLE,
    GX_NATIVE_ALLOC_STATUS_UNAVAILABLE,
    GX_NATIVE_ALLOC_VALUE_UNAVAILABLE,
    {},
};

void recordFailure(NativeAllocationAttempt& attempt, uint32_t operation,
                   uint32_t branch, uint32_t status,
                   uint32_t statusSource) {
    if (attempt.failureOperation != GX_NATIVE_ALLOC_VALUE_UNAVAILABLE) return;
    attempt.failureOperation = operation;
    attempt.failureBranch = branch;
    attempt.failureStatus = status;
    attempt.failureStatusSource = statusSource;
    if (g_record.firstFailedAttempt != GX_NATIVE_ALLOC_VALUE_UNAVAILABLE) return;
    g_record.firstFailedAttempt = g_record.attemptCount - 1u;
    g_record.firstFailureOperation = operation;
    g_record.firstFailureBranch = branch;
    g_record.firstFailureStatus = status;
    g_record.firstFailureStatusSource = statusSource;
}

NativeAllocationAttempt* activeAttempt() {
    if (g_record.activeAttempt >= g_record.attemptCount ||
        g_record.activeAttempt >= GX_NATIVE_ALLOC_ATTEMPT_CAPACITY) {
        return nullptr;
    }
    return &g_record.attempts[g_record.activeAttempt];
}

} // namespace

extern "C" void __cdecl GuideXosNativeAotNativeAllocBegin(
    uint32_t site, uint64_t requestSize, uint64_t alignment) {
    if (g_record.activeAttempt != GX_NATIVE_ALLOC_VALUE_UNAVAILABLE) {
        NativeAllocationAttempt* active = activeAttempt();
        if (active != nullptr) {
            recordFailure(*active, GX_NATIVE_ALLOC_SITE_GC_EVENT_IMPL,
                          GX_NATIVE_ALLOC_FAILURE_SCOPE_OVERFLOW,
                          GX_NATIVE_ALLOC_VALUE_UNAVAILABLE,
                          GX_NATIVE_ALLOC_STATUS_UNAVAILABLE);
        }
        g_record.overflow = 1u;
        return;
    }
    if (g_record.attemptCount >= GX_NATIVE_ALLOC_ATTEMPT_CAPACITY) {
        g_record.overflow = 1u;
        return;
    }

    const uint32_t index = g_record.attemptCount++;
    NativeAllocationAttempt& attempt = g_record.attempts[index];
    attempt = {};
    attempt.site = site;
    attempt.state = 1u;
    attempt.requestSize = requestSize;
    attempt.alignment = alignment;
    attempt.newOperation = GX_NATIVE_ALLOC_VALUE_UNAVAILABLE;
    attempt.callsite = 0u;
    attempt.palReadiness = GX_NATIVE_ALLOC_PAL_STATE_UNKNOWN;
    attempt.backendPointer = UINTPTR_MAX;
    attempt.failureOperation = GX_NATIVE_ALLOC_VALUE_UNAVAILABLE;
    attempt.failureBranch = GX_NATIVE_ALLOC_VALUE_UNAVAILABLE;
    attempt.failureStatus = GX_NATIVE_ALLOC_VALUE_UNAVAILABLE;
    attempt.failureStatusSource = GX_NATIVE_ALLOC_STATUS_UNAVAILABLE;
    g_record.activeAttempt = index;
}

extern "C" void __cdecl GuideXosNativeAotNativeAllocObserveNothrowNew(
    uint64_t requestSize, uintptr_t callsite, void* result) {
    NativeAllocationAttempt* attempt = activeAttempt();
    if (attempt == nullptr) return;
    attempt->newOperation = GX_NATIVE_ALLOC_OP_NOTHROW_NEW;
    attempt->callsite = callsite;
    attempt->finalPointerNonNull = result != nullptr ? 1u : 0u;
    if (requestSize != attempt->requestSize) {
        recordFailure(*attempt, GX_NATIVE_ALLOC_OP_NOTHROW_NEW,
                      GX_NATIVE_ALLOC_FAILURE_SOURCE_INCOMPLETE,
                      GX_NATIVE_ALLOC_VALUE_UNAVAILABLE,
                      GX_NATIVE_ALLOC_STATUS_UNAVAILABLE);
    } else if (result == nullptr) {
        recordFailure(*attempt, GX_NATIVE_ALLOC_OP_NOTHROW_NEW,
                      GX_NATIVE_ALLOC_FAILURE_PAL_RETURNED_NULL,
                      GX_NATIVE_ALLOC_VALUE_UNAVAILABLE,
                      GX_NATIVE_ALLOC_STATUS_UNAVAILABLE);
    }
}

extern "C" void __cdecl GuideXosNativeAotNativeAllocObserveMalloc(
    uint64_t requestSize, uint64_t backingTotal, uint32_t failureBranch) {
    NativeAllocationAttempt* attempt = activeAttempt();
    if (attempt == nullptr) return;
    attempt->mallocSize = requestSize;
    attempt->backingTotal = backingTotal;
    attempt->backingAllocator = GX_NATIVE_ALLOC_BACKING_STARTUP_MALLOC;
    if (requestSize != attempt->requestSize) {
        recordFailure(*attempt, GX_NATIVE_ALLOC_BACKING_STARTUP_MALLOC,
                      GX_NATIVE_ALLOC_FAILURE_SOURCE_INCOMPLETE,
                      GX_NATIVE_ALLOC_VALUE_UNAVAILABLE,
                      GX_NATIVE_ALLOC_STATUS_UNAVAILABLE);
    } else if (failureBranch != GX_NATIVE_ALLOC_FAILURE_NONE) {
        recordFailure(*attempt, GX_NATIVE_ALLOC_BACKING_STARTUP_MALLOC,
                      failureBranch, GX_NATIVE_ALLOC_VALUE_UNAVAILABLE,
                      GX_NATIVE_ALLOC_STATUS_UNAVAILABLE);
    }
}

extern "C" void __cdecl GuideXosNativeAotNativeAllocObservePal(
    uint32_t readiness, uint32_t callbackEntered, void* result) {
    NativeAllocationAttempt* attempt = activeAttempt();
    if (attempt == nullptr) return;
    attempt->palReadiness = readiness;
    attempt->palCallbackEntered = callbackEntered;
    attempt->backendPointer = reinterpret_cast<uintptr_t>(result);
    if (readiness != GX_NATIVE_ALLOC_PAL_STATE_READY) {
        recordFailure(*attempt, GX_NATIVE_ALLOC_OP_PAL_VIRTUAL_ALLOC,
                      GX_NATIVE_ALLOC_FAILURE_PAL_NOT_READY,
                      GX_NATIVE_ALLOC_VALUE_UNAVAILABLE,
                      GX_NATIVE_ALLOC_STATUS_UNAVAILABLE);
    } else if (callbackEntered != 0u && result == nullptr) {
        recordFailure(*attempt, GX_NATIVE_ALLOC_OP_PAL_VIRTUAL_ALLOC,
                      GX_NATIVE_ALLOC_FAILURE_PAL_RETURNED_NULL,
                      GX_NATIVE_ALLOC_VALUE_UNAVAILABLE,
                      GX_NATIVE_ALLOC_STATUS_UNAVAILABLE);
    }
}

extern "C" void __cdecl GuideXosNativeAotNativeAllocPalCallbackEntered(void) {
    NativeAllocationAttempt* attempt = activeAttempt();
    if (attempt != nullptr) attempt->palCallbackEntered = 1u;
}

extern "C" void __cdecl GuideXosNativeAotNativeAllocComplete(void* result) {
    NativeAllocationAttempt* attempt = activeAttempt();
    if (attempt == nullptr) return;
    attempt->state = 2u;
    attempt->finalPointerNonNull = result != nullptr ? 1u : 0u;
    if (result == nullptr &&
        attempt->failureOperation == GX_NATIVE_ALLOC_VALUE_UNAVAILABLE) {
        recordFailure(*attempt, GX_NATIVE_ALLOC_OP_FINAL_POINTER,
                      GX_NATIVE_ALLOC_FAILURE_PAL_RETURNED_NULL,
                      GX_NATIVE_ALLOC_VALUE_UNAVAILABLE,
                      GX_NATIVE_ALLOC_STATUS_UNAVAILABLE);
    }
    g_record.activeAttempt = GX_NATIVE_ALLOC_VALUE_UNAVAILABLE;
}

extern "C" void __cdecl GuideXosNativeAotNativeAllocConstructorEntered(void) {
    NativeAllocationAttempt* attempt = activeAttempt();
    if (attempt != nullptr) attempt->constructorEntered = 1u;
}

extern "C" void __cdecl GuideXosNativeAotNativeAllocRecordFailure(
    uint32_t operation, uint32_t branch, uint32_t status,
    uint32_t statusSource) {
    NativeAllocationAttempt* attempt = activeAttempt();
    if (attempt != nullptr) {
        recordFailure(*attempt, operation, branch, status, statusSource);
    }
}

extern "C" uint32_t __cdecl GuideXosNativeAotNativeAllocGetSummaryField(
    uint32_t field) {
    switch (field) {
    case GX_NATIVE_ALLOC_SUMMARY_MAGIC: return g_record.magic;
    case GX_NATIVE_ALLOC_SUMMARY_VERSION: return g_record.version;
    case GX_NATIVE_ALLOC_SUMMARY_ATTEMPT_COUNT: return g_record.attemptCount;
    case GX_NATIVE_ALLOC_SUMMARY_OVERFLOW: return g_record.overflow;
    case GX_NATIVE_ALLOC_SUMMARY_FIRST_FAILED_ATTEMPT: return g_record.firstFailedAttempt;
    case GX_NATIVE_ALLOC_SUMMARY_FIRST_FAILURE_OPERATION: return g_record.firstFailureOperation;
    case GX_NATIVE_ALLOC_SUMMARY_FIRST_FAILURE_BRANCH: return g_record.firstFailureBranch;
    case GX_NATIVE_ALLOC_SUMMARY_FIRST_FAILURE_STATUS: return g_record.firstFailureStatus;
    case GX_NATIVE_ALLOC_SUMMARY_FIRST_FAILURE_STATUS_SOURCE: return g_record.firstFailureStatusSource;
    default: return GX_NATIVE_ALLOC_VALUE_UNAVAILABLE;
    }
}

extern "C" uint32_t __cdecl GuideXosNativeAotNativeAllocGetAttemptCount(void) {
    return g_record.attemptCount;
}

extern "C" uint32_t __cdecl GuideXosNativeAotNativeAllocGetAttemptField(
    uint32_t index, uint32_t field) {
    if (index >= g_record.attemptCount || index >= GX_NATIVE_ALLOC_ATTEMPT_CAPACITY) {
        return GX_NATIVE_ALLOC_VALUE_UNAVAILABLE;
    }
    const NativeAllocationAttempt& attempt = g_record.attempts[index];
    switch (field) {
    case GX_NATIVE_ALLOC_FIELD_SEQUENCE: return index;
    case GX_NATIVE_ALLOC_FIELD_SITE: return attempt.site;
    case GX_NATIVE_ALLOC_FIELD_STATE: return attempt.state;
    case GX_NATIVE_ALLOC_FIELD_REQUEST_SIZE_LOW: return static_cast<uint32_t>(attempt.requestSize);
    case GX_NATIVE_ALLOC_FIELD_REQUEST_SIZE_HIGH: return static_cast<uint32_t>(attempt.requestSize >> 32);
    case GX_NATIVE_ALLOC_FIELD_ALIGNMENT_LOW: return static_cast<uint32_t>(attempt.alignment);
    case GX_NATIVE_ALLOC_FIELD_ALIGNMENT_HIGH: return static_cast<uint32_t>(attempt.alignment >> 32);
    case GX_NATIVE_ALLOC_FIELD_NEW_OPERATION: return attempt.newOperation;
    case GX_NATIVE_ALLOC_FIELD_CALLSITE_LOW: return static_cast<uint32_t>(attempt.callsite);
    case GX_NATIVE_ALLOC_FIELD_CALLSITE_HIGH: return static_cast<uint32_t>(static_cast<uint64_t>(attempt.callsite) >> 32);
    case GX_NATIVE_ALLOC_FIELD_BACKING_ALLOCATOR: return attempt.backingAllocator;
    case GX_NATIVE_ALLOC_FIELD_MALLOC_SIZE_LOW: return static_cast<uint32_t>(attempt.mallocSize);
    case GX_NATIVE_ALLOC_FIELD_MALLOC_SIZE_HIGH: return static_cast<uint32_t>(attempt.mallocSize >> 32);
    case GX_NATIVE_ALLOC_FIELD_BACKING_TOTAL_LOW: return static_cast<uint32_t>(attempt.backingTotal);
    case GX_NATIVE_ALLOC_FIELD_BACKING_TOTAL_HIGH: return static_cast<uint32_t>(attempt.backingTotal >> 32);
    case GX_NATIVE_ALLOC_FIELD_PAL_STATE: return attempt.palReadiness;
    case GX_NATIVE_ALLOC_FIELD_PAL_CALLBACK_ENTERED: return attempt.palCallbackEntered;
    case GX_NATIVE_ALLOC_FIELD_BACKEND_POINTER_LOW:
        return static_cast<uint32_t>(attempt.backendPointer);
    case GX_NATIVE_ALLOC_FIELD_BACKEND_POINTER_HIGH:
        return static_cast<uint32_t>(static_cast<uint64_t>(attempt.backendPointer) >> 32);
    case GX_NATIVE_ALLOC_FIELD_FINAL_POINTER_NON_NULL: return attempt.finalPointerNonNull;
    case GX_NATIVE_ALLOC_FIELD_CONSTRUCTOR_ENTERED: return attempt.constructorEntered;
    case GX_NATIVE_ALLOC_FIELD_FAILURE_OPERATION: return attempt.failureOperation;
    case GX_NATIVE_ALLOC_FIELD_FAILURE_BRANCH: return attempt.failureBranch;
    case GX_NATIVE_ALLOC_FIELD_FAILURE_STATUS: return attempt.failureStatus;
    case GX_NATIVE_ALLOC_FIELD_FAILURE_STATUS_SOURCE: return attempt.failureStatusSource;
    default: return GX_NATIVE_ALLOC_VALUE_UNAVAILABLE;
    }
}
#endif
