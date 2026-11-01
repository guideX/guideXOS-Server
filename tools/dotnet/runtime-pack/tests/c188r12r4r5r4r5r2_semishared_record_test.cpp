#include "../src/platform/guidexos_nativeaot_semishared_record.h"

#include <stdint.h>
#include <cstdio>

#if defined(GUIDEXOS_NATIVEAOT_C188R12R4R5R4R5R2_SEMISHARED)
namespace {
int failures = 0;
int assertions = 0;

void check(bool condition, const char* name) {
    ++assertions;
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", name);
        ++failures;
    }
}

uint32_t summary(uint32_t field) {
    return GuideXosNativeAotSemiSharedGetSummaryField(field);
}

uint32_t operation(uint32_t index, uint32_t field) {
    return GuideXosNativeAotSemiSharedGetOperationField(index, field);
}

void test_successful_initialization_record() {
    GuideXosNativeAotSemiSharedBegin(7u);
    GuideXosNativeAotSemiSharedBeginOperation(GX_SEMISHARED_OP_MARK_LIST);
    GuideXosNativeAotSemiSharedSetAllocationRequest(
        GX_SEMISHARED_RESOURCE_GC_ARRAY, 8192u * sizeof(void*), alignof(void*));
    GuideXosNativeAotSemiSharedSetResourceResult(GX_SEMISHARED_RESOURCE_GC_ARRAY, 1u);
    GuideXosNativeAotSemiSharedCompleteOperation(1u, true);
    GuideXosNativeAotSemiSharedRecordCleanupEntry(GX_SEMISHARED_CLEANUP_SCOPE_OUTER);
    GuideXosNativeAotSemiSharedRecordCleanupComplete(GX_SEMISHARED_CLEANUP_SCOPE_OUTER);
    GuideXosNativeAotSemiSharedFinalize(1);

    check(summary(GX_SEMISHARED_SUMMARY_MAGIC) == GX_SEMISHARED_RECORD_MAGIC,
          "record magic is source-defined");
    check(summary(GX_SEMISHARED_SUMMARY_VERSION) == GX_SEMISHARED_RECORD_VERSION,
          "record version is source-defined");
    check(summary(GX_SEMISHARED_SUMMARY_INVOCATION) == 7u,
          "invocation identifier is retained");
    check(summary(GX_SEMISHARED_SUMMARY_FINAL_RESULT) == 1u,
          "successful final result is retained");
    check(summary(GX_SEMISHARED_SUMMARY_OPERATION_COUNT) == 2u,
          "operation count includes the final result");
    check(summary(GX_SEMISHARED_SUMMARY_FIRST_FAILED_OPERATION) == GX_SEMISHARED_OP_NONE,
          "success has no failed operation");
    check(summary(GX_SEMISHARED_SUMMARY_FIRST_FAILURE_BRANCH) == GX_SEMISHARED_OP_NONE,
          "success has no failure branch");
    check(summary(GX_SEMISHARED_SUMMARY_CLEANUP_ENTERED_MASK) == 1u,
          "cleanup entry is retained");
    check(summary(GX_SEMISHARED_SUMMARY_CLEANUP_COMPLETED_MASK) == 1u,
          "cleanup completion is retained");
    check(operation(0u, GX_SEMISHARED_OPERATION_FIELD_ID) == GX_SEMISHARED_OP_MARK_LIST,
          "operation order is retained");
    check(operation(0u, GX_SEMISHARED_OPERATION_FIELD_STATE) == GX_SEMISHARED_RETURNED_SUCCESS,
          "successful operation state is retained");
    check(operation(0u, GX_SEMISHARED_OPERATION_FIELD_RESOURCE) == GX_SEMISHARED_RESOURCE_GC_ARRAY,
          "resource acquisition kind is retained");
    check(operation(0u, GX_SEMISHARED_OPERATION_FIELD_REQUEST_RESOURCE) == GX_SEMISHARED_RESOURCE_GC_ARRAY,
          "allocation request kind is retained");
    check(operation(0u, GX_SEMISHARED_OPERATION_FIELD_SIZE_LOW) == 8192u * sizeof(void*),
          "requested allocation size is retained");
    check(operation(0u, GX_SEMISHARED_OPERATION_FIELD_ALIGNMENT_LOW) == alignof(void*),
          "requested allocation alignment is retained");
}

void test_first_failure_survives_cleanup_failure() {
    GuideXosNativeAotSemiSharedBegin(8u);
    GuideXosNativeAotSemiSharedBeginOperation(GX_SEMISHARED_OP_BGC_DONE_EVENT);
    GuideXosNativeAotSemiSharedSetAllocationRequest(
        GX_SEMISHARED_RESOURCE_GC_EVENT_IMPL, 16u, 8u);
    GuideXosNativeAotSemiSharedSetResourceResult(GX_SEMISHARED_RESOURCE_GC_EVENT_IMPL, 0u);
    GuideXosNativeAotSemiSharedSetNativeStatus(
        GX_SEMISHARED_VALUE_UNAVAILABLE, GX_SEMISHARED_STATUS_SOURCE_UNAVAILABLE);
    GuideXosNativeAotSemiSharedSetDetail(
        GX_SEMISHARED_DETAIL_EVENT_IMPL_ALLOCATION_FAILED);
    GuideXosNativeAotSemiSharedCompleteOperation(0u, false);
    GuideXosNativeAotSemiSharedRecordFailureBranch(GX_SEMISHARED_OP_BGC_DONE_EVENT);

    GuideXosNativeAotSemiSharedRecordCleanupEntry(GX_SEMISHARED_CLEANUP_SCOPE_BGC_SUPPORT);
    GuideXosNativeAotSemiSharedBeginOperation(GX_SEMISHARED_OP_CLEANUP_FULL_GC_APPROACH_EVENT);
    GuideXosNativeAotSemiSharedSetNativeStatus(2u, GX_SEMISHARED_STATUS_SOURCE_EVENT_CLOSE_PAL);
    GuideXosNativeAotSemiSharedCompleteOperation(2u, false);
    GuideXosNativeAotSemiSharedFinalize(0);

    check(summary(GX_SEMISHARED_SUMMARY_FINAL_RESULT) == 0u,
          "failed final result is retained");
    check(summary(GX_SEMISHARED_SUMMARY_FIRST_FAILED_OPERATION) == GX_SEMISHARED_OP_BGC_DONE_EVENT,
          "first failing operation is retained");
    check(summary(GX_SEMISHARED_SUMMARY_FIRST_FAILURE_BRANCH) == GX_SEMISHARED_OP_BGC_DONE_EVENT,
          "first explicit branch is retained");
    check(summary(GX_SEMISHARED_SUMMARY_FIRST_FAILURE_STATUS) == GX_SEMISHARED_VALUE_UNAVAILABLE,
          "unavailable allocation status is not invented");
    check(summary(GX_SEMISHARED_SUMMARY_CLEANUP_FAILURE_OPERATION) == GX_SEMISHARED_OP_CLEANUP_FULL_GC_APPROACH_EVENT,
          "cleanup failure is stored independently");
    check(summary(GX_SEMISHARED_SUMMARY_CLEANUP_FAILURE_STATUS) == 2u,
          "raw cleanup status survives later finalization");
    check(summary(GX_SEMISHARED_SUMMARY_CLEANUP_FAILURE_STATUS_SOURCE) == GX_SEMISHARED_STATUS_SOURCE_EVENT_CLOSE_PAL,
          "cleanup status source is retained");
    check(operation(0u, GX_SEMISHARED_OPERATION_FIELD_ID) == GX_SEMISHARED_OP_BGC_DONE_EVENT,
          "first operation record remains in sequence zero");
    check(operation(1u, GX_SEMISHARED_OPERATION_FIELD_ID) == GX_SEMISHARED_OP_CLEANUP_FULL_GC_APPROACH_EVENT,
          "cleanup operation follows the initialization failure");
    check(operation(1u, GX_SEMISHARED_OPERATION_FIELD_NATIVE_STATUS) == 2u,
          "raw cleanup status is preserved on its operation");
}

void test_nested_operation_restores_parent() {
    GuideXosNativeAotSemiSharedBegin(9u);
    GuideXosNativeAotSemiSharedBeginOperation(GX_SEMISHARED_OP_BGC_SUPPORT_RESULT);
    GuideXosNativeAotSemiSharedBeginOperation(GX_SEMISHARED_OP_BGC_SYNC_EVENT);
    GuideXosNativeAotSemiSharedCompleteOperation(1u, true);
    GuideXosNativeAotSemiSharedCompleteOperation(1u, true);
    GuideXosNativeAotSemiSharedFinalize(1);
    check(operation(0u, GX_SEMISHARED_OPERATION_FIELD_ID) == GX_SEMISHARED_OP_BGC_SUPPORT_RESULT,
          "parent operation keeps its original slot");
    check(operation(0u, GX_SEMISHARED_OPERATION_FIELD_STATE) == GX_SEMISHARED_RETURNED_SUCCESS,
          "nested completion restores the parent operation");
    check(operation(1u, GX_SEMISHARED_OPERATION_FIELD_ID) == GX_SEMISHARED_OP_BGC_SYNC_EVENT,
          "nested event receives its own sequence slot");
}

void test_incomplete_operation_remains_unresolved() {
    GuideXosNativeAotSemiSharedBegin(10u);
    GuideXosNativeAotSemiSharedBeginOperation(GX_SEMISHARED_OP_FULL_GC_END_EVENT);
    check(operation(0u, GX_SEMISHARED_OPERATION_FIELD_STATE) == GX_SEMISHARED_ENTERED,
          "missing result remains visibly incomplete");
    check(operation(0u, GX_SEMISHARED_OPERATION_FIELD_RESULT) == GX_SEMISHARED_VALUE_UNAVAILABLE,
          "missing result uses the unavailable sentinel");
}
}

int main() {
    test_successful_initialization_record();
    test_first_failure_survives_cleanup_failure();
    test_nested_operation_restores_parent();
    test_incomplete_operation_remains_unresolved();
    if (failures != 0) return 1;
    std::printf("C188R12R4R5R4R5R2 semi-shared record tests passed (%d assertions)\n", assertions);
    return 0;
}
#else
int main() { return 2; }
#endif
