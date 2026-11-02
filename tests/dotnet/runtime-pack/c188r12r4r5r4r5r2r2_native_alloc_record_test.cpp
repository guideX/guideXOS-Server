#include "guidexos_nativeaot_native_alloc_record.h"

int main() {
    const void* const raw = reinterpret_cast<void*>(static_cast<uintptr_t>(0x10000u));
    void* const object = reinterpret_cast<void*>(static_cast<uintptr_t>(0x10010u));

    GuideXosNativeAotNativeAllocBegin(GX_NATIVE_ALLOC_SITE_GC_EVENT_IMPL, 8u, 8u);
    GuideXosNativeAotNativeAllocObserveMalloc(8u, 24u, GX_NATIVE_ALLOC_FAILURE_NONE);
    GuideXosNativeAotNativeAllocPalCallbackEntered();
    GuideXosNativeAotNativeAllocObservePal(
        GX_NATIVE_ALLOC_PAL_STATE_READY, 1u, const_cast<void*>(raw));
    GuideXosNativeAotNativeAllocObserveNothrowNew(8u, 0x12345678u, object);
    GuideXosNativeAotNativeAllocConstructorEntered();
    GuideXosNativeAotNativeAllocComplete(object);
    if (GuideXosNativeAotNativeAllocGetAttemptField(
            0u, GX_NATIVE_ALLOC_FIELD_FINAL_POINTER_NON_NULL) != 1u ||
        GuideXosNativeAotNativeAllocGetAttemptField(
            0u, GX_NATIVE_ALLOC_FIELD_CONSTRUCTOR_ENTERED) != 1u ||
        GuideXosNativeAotNativeAllocGetAttemptField(
            0u, GX_NATIVE_ALLOC_FIELD_BACKING_TOTAL_LOW) != 24u) {
        return 1;
    }

    GuideXosNativeAotNativeAllocBegin(GX_NATIVE_ALLOC_SITE_GC_EVENT_IMPL, 8u, 8u);
    GuideXosNativeAotNativeAllocObserveMalloc(8u, 24u, GX_NATIVE_ALLOC_FAILURE_NONE);
    GuideXosNativeAotNativeAllocPalCallbackEntered();
    GuideXosNativeAotNativeAllocRecordFailure(
        GX_NATIVE_ALLOC_OP_VM_COMMIT,
        GX_NATIVE_ALLOC_BRANCH_VM_COMMIT_FRAME_UNAVAILABLE,
        7u, GX_NATIVE_ALLOC_STATUS_VM_RESULT);
    GuideXosNativeAotNativeAllocObservePal(
        GX_NATIVE_ALLOC_PAL_STATE_READY, 1u, nullptr);
    GuideXosNativeAotNativeAllocObserveNothrowNew(8u, 0x12345678u, nullptr);
    // This represents the return from the unchanged VM rollback/cleanup path.
    GuideXosNativeAotNativeAllocComplete(nullptr);
    if (GuideXosNativeAotNativeAllocGetAttemptField(
            1u, GX_NATIVE_ALLOC_FIELD_FINAL_POINTER_NON_NULL) != 0u ||
        GuideXosNativeAotNativeAllocGetAttemptField(
            1u, GX_NATIVE_ALLOC_FIELD_CONSTRUCTOR_ENTERED) != 0u ||
        GuideXosNativeAotNativeAllocGetAttemptField(
            1u, GX_NATIVE_ALLOC_FIELD_FAILURE_OPERATION) != GX_NATIVE_ALLOC_OP_VM_COMMIT ||
        GuideXosNativeAotNativeAllocGetAttemptField(
            1u, GX_NATIVE_ALLOC_FIELD_FAILURE_STATUS) != 7u ||
        GuideXosNativeAotNativeAllocGetSummaryField(
            GX_NATIVE_ALLOC_SUMMARY_FIRST_FAILURE_OPERATION) != GX_NATIVE_ALLOC_OP_VM_COMMIT) {
        return 2;
    }

    GuideXosNativeAotNativeAllocBegin(GX_NATIVE_ALLOC_SITE_GC_EVENT_IMPL, 8u, 8u);
    GuideXosNativeAotNativeAllocObserveMalloc(8u, 24u, GX_NATIVE_ALLOC_FAILURE_NONE);
    GuideXosNativeAotNativeAllocObservePal(
        GX_NATIVE_ALLOC_PAL_STATE_NOT_READY, 0u, nullptr);
    GuideXosNativeAotNativeAllocObserveNothrowNew(8u, 0x12345678u, nullptr);
    GuideXosNativeAotNativeAllocComplete(nullptr);
    if (GuideXosNativeAotNativeAllocGetAttemptField(
            2u, GX_NATIVE_ALLOC_FIELD_FAILURE_BRANCH) != GX_NATIVE_ALLOC_FAILURE_PAL_NOT_READY ||
        GuideXosNativeAotNativeAllocGetAttemptField(
            2u, GX_NATIVE_ALLOC_FIELD_FAILURE_STATUS_SOURCE) != GX_NATIVE_ALLOC_STATUS_UNAVAILABLE) {
        return 3;
    }

    for (uint32_t attempt = 3u; attempt < GX_NATIVE_ALLOC_ATTEMPT_CAPACITY; ++attempt) {
        GuideXosNativeAotNativeAllocBegin(GX_NATIVE_ALLOC_SITE_GC_EVENT_IMPL, 8u, 8u);
        GuideXosNativeAotNativeAllocComplete(nullptr);
    }
    GuideXosNativeAotNativeAllocBegin(GX_NATIVE_ALLOC_SITE_GC_EVENT_IMPL, 8u, 8u);
    if (GuideXosNativeAotNativeAllocGetAttemptCount() != GX_NATIVE_ALLOC_ATTEMPT_CAPACITY ||
        GuideXosNativeAotNativeAllocGetSummaryField(GX_NATIVE_ALLOC_SUMMARY_OVERFLOW) != 1u) {
        return 4;
    }
    return 0;
}
