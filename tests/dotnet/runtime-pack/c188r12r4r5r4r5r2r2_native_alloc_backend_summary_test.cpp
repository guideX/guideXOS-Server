#define GUIDEXOS_NATIVEAOT_C188R12R4R5R4R5R2R2_NATIVE_ALLOC 1
#include "tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_native_alloc_record.h"
#include "kernel/nativeaot_native_alloc_backend_summary.h"

using kernel::nativeaot::diagnostics::NativeAllocBackendCommit;
using kernel::nativeaot::diagnostics::NativeAllocBackendReserve;
using kernel::nativeaot::diagnostics::NativeAllocBackendRollbackRelease;
using kernel::nativeaot::diagnostics::NativeAllocBackendSummary;
using kernel::nativeaot::diagnostics::NativeAllocBackendTerminalIncomplete;
using kernel::nativeaot::diagnostics::NativeAllocBackendTerminalRuntimeFailure;
using kernel::nativeaot::diagnostics::NativeAllocBackendTerminalSuccess;

static void begin(NativeAllocBackendSummary& summary, uint32_t detailCapacity,
                  uint32_t* ordinal) {
    *ordinal = kernel::nativeaot::diagnostics::beginNativeAllocBackendAttempt(
        summary, summary.attemptedCount < detailCapacity);
}

int main() {
    // Successful startup within the detailed-row capacity.
    NativeAllocBackendSummary success{};
    kernel::nativeaot::diagnostics::resetNativeAllocBackendSummary(success);
    for (uint32_t index = 0u; index < 8u; ++index) {
        uint32_t ordinal = 0u;
        begin(success, 9u, &ordinal);
        kernel::nativeaot::diagnostics::recordNativeAllocBackendOperation(success, NativeAllocBackendReserve);
        kernel::nativeaot::diagnostics::recordNativeAllocBackendOperation(success, NativeAllocBackendCommit);
        kernel::nativeaot::diagnostics::finishNativeAllocBackendAttempt(success, true);
    }
    if (success.attemptedCount != 8u || success.completedCount != 8u ||
        success.successfulCount != 8u || success.failedCount != 0u ||
        success.droppedDetailCount != 0u ||
        success.operationCounts[NativeAllocBackendReserve] != 8u ||
        success.operationCounts[NativeAllocBackendCommit] != 8u ||
        kernel::nativeaot::diagnostics::nativeAllocBackendTerminalStatus(success) !=
            NativeAllocBackendTerminalSuccess ||
        !kernel::nativeaot::diagnostics::nativeAllocBackendSummaryComplete(success)) {
        return 1;
    }

    // High volume stays bounded while aggregate totals retain every attempt
    // and repeated reserve/commit operations.
    NativeAllocBackendSummary highVolume{};
    kernel::nativeaot::diagnostics::resetNativeAllocBackendSummary(highVolume);
    constexpr uint32_t kHighVolumeAttempts = 2048u;
    for (uint32_t index = 0u; index < kHighVolumeAttempts; ++index) {
        uint32_t ordinal = 0u;
        begin(highVolume, 9u, &ordinal);
        kernel::nativeaot::diagnostics::recordNativeAllocBackendOperation(highVolume, NativeAllocBackendReserve);
        kernel::nativeaot::diagnostics::recordNativeAllocBackendOperation(highVolume, NativeAllocBackendCommit);
        kernel::nativeaot::diagnostics::recordNativeAllocBackendOperation(highVolume, NativeAllocBackendCommit);
        kernel::nativeaot::diagnostics::finishNativeAllocBackendAttempt(highVolume, true);
    }
    if (highVolume.attemptedCount != kHighVolumeAttempts ||
        highVolume.completedCount != kHighVolumeAttempts ||
        highVolume.successfulCount != kHighVolumeAttempts ||
        highVolume.failedCount != 0u || highVolume.droppedDetailCount != kHighVolumeAttempts - 9u ||
        highVolume.firstDroppedAttempt != 9u ||
        highVolume.operationCounts[NativeAllocBackendReserve] != kHighVolumeAttempts ||
        highVolume.operationCounts[NativeAllocBackendCommit] != 2u * kHighVolumeAttempts ||
        kernel::nativeaot::diagnostics::nativeAllocBackendTerminalStatus(highVolume) !=
            NativeAllocBackendTerminalSuccess ||
        !kernel::nativeaot::diagnostics::nativeAllocBackendSummaryComplete(highVolume)) {
        return 2;
    }

    // Failures before capacity retain the original failure and distinct
    // cleanup status/branch codes without duplicate inflation.
    NativeAllocBackendSummary failureBeforeOverflow{};
    kernel::nativeaot::diagnostics::resetNativeAllocBackendSummary(failureBeforeOverflow);
    uint32_t firstOrdinal = 0u;
    begin(failureBeforeOverflow, 9u, &firstOrdinal);
    kernel::nativeaot::diagnostics::recordNativeAllocBackendOperation(
        failureBeforeOverflow, NativeAllocBackendReserve);
    kernel::nativeaot::diagnostics::recordNativeAllocBackendFailure(
        failureBeforeOverflow, firstOrdinal, GX_NATIVE_ALLOC_OP_VM_COMMIT,
        GX_NATIVE_ALLOC_BRANCH_VM_COMMIT_FRAME_UNAVAILABLE, 7u,
        GX_NATIVE_ALLOC_STATUS_VM_RESULT);
    kernel::nativeaot::diagnostics::recordNativeAllocBackendFailure(
        failureBeforeOverflow, firstOrdinal, GX_NATIVE_ALLOC_OP_VM_COMMIT,
        GX_NATIVE_ALLOC_BRANCH_VM_COMMIT_FRAME_UNAVAILABLE, 7u,
        GX_NATIVE_ALLOC_STATUS_VM_RESULT);
    kernel::nativeaot::diagnostics::recordNativeAllocBackendFailure(
        failureBeforeOverflow, firstOrdinal, GX_NATIVE_ALLOC_OP_VM_RELEASE_ROLLBACK,
        GX_NATIVE_ALLOC_FAILURE_VM_ROLLBACK_RELEASE, 6u,
        GX_NATIVE_ALLOC_STATUS_VM_RESULT);
    kernel::nativeaot::diagnostics::recordNativeAllocBackendOperation(
        failureBeforeOverflow, NativeAllocBackendRollbackRelease);
    kernel::nativeaot::diagnostics::finishNativeAllocBackendAttempt(
        failureBeforeOverflow, false);
    if (failureBeforeOverflow.firstFailureAttempt != 0u ||
        failureBeforeOverflow.firstFailureOperation != GX_NATIVE_ALLOC_OP_VM_COMMIT ||
        failureBeforeOverflow.firstFailureBranch != GX_NATIVE_ALLOC_BRANCH_VM_COMMIT_FRAME_UNAVAILABLE ||
        failureBeforeOverflow.firstFailureStatus != 7u ||
        failureBeforeOverflow.firstFailureStatusSource != GX_NATIVE_ALLOC_STATUS_VM_RESULT ||
        failureBeforeOverflow.distinctFailureCount != 2u ||
        failureBeforeOverflow.distinctFailureOverflow != 0u ||
        failureBeforeOverflow.failedCount != 1u ||
        kernel::nativeaot::diagnostics::nativeAllocBackendTerminalStatus(failureBeforeOverflow) !=
            NativeAllocBackendTerminalRuntimeFailure ||
        !kernel::nativeaot::diagnostics::nativeAllocBackendSummaryComplete(failureBeforeOverflow)) {
        return 3;
    }

    // A failure after all detailed rows are used still retains its source
    // operation, distinct cleanup code, attempt ordinal, and terminal state.
    NativeAllocBackendSummary failureAfterPressure{};
    kernel::nativeaot::diagnostics::resetNativeAllocBackendSummary(failureAfterPressure);
    for (uint32_t index = 0u; index < 9u; ++index) {
        uint32_t ordinal = 0u;
        begin(failureAfterPressure, 9u, &ordinal);
        kernel::nativeaot::diagnostics::recordNativeAllocBackendOperation(
            failureAfterPressure, NativeAllocBackendReserve);
        kernel::nativeaot::diagnostics::recordNativeAllocBackendOperation(
            failureAfterPressure, NativeAllocBackendCommit);
        kernel::nativeaot::diagnostics::finishNativeAllocBackendAttempt(
            failureAfterPressure, true);
    }
    uint32_t droppedOrdinal = 0u;
    begin(failureAfterPressure, 9u, &droppedOrdinal);
    kernel::nativeaot::diagnostics::recordNativeAllocBackendOperation(
        failureAfterPressure, NativeAllocBackendReserve);
    kernel::nativeaot::diagnostics::recordNativeAllocBackendFailure(
        failureAfterPressure, droppedOrdinal, GX_NATIVE_ALLOC_OP_VM_COMMIT,
        GX_NATIVE_ALLOC_BRANCH_VM_COMMIT_FRAME_UNAVAILABLE, 7u,
        GX_NATIVE_ALLOC_STATUS_VM_RESULT);
    kernel::nativeaot::diagnostics::recordNativeAllocBackendOperation(
        failureAfterPressure, NativeAllocBackendCommit);
    kernel::nativeaot::diagnostics::finishNativeAllocBackendAttempt(
        failureAfterPressure, false);
    uint32_t cleanupOrdinal = 0u;
    begin(failureAfterPressure, 9u, &cleanupOrdinal);
    kernel::nativeaot::diagnostics::recordNativeAllocBackendOperation(
        failureAfterPressure, NativeAllocBackendReserve);
    kernel::nativeaot::diagnostics::recordNativeAllocBackendFailure(
        failureAfterPressure, cleanupOrdinal, GX_NATIVE_ALLOC_OP_VM_RELEASE_ROLLBACK,
        GX_NATIVE_ALLOC_FAILURE_VM_ROLLBACK_RELEASE, 6u,
        GX_NATIVE_ALLOC_STATUS_VM_RESULT);
    kernel::nativeaot::diagnostics::recordNativeAllocBackendOperation(
        failureAfterPressure, NativeAllocBackendRollbackRelease);
    kernel::nativeaot::diagnostics::finishNativeAllocBackendAttempt(
        failureAfterPressure, false);
    if (droppedOrdinal != 9u || cleanupOrdinal != 10u ||
        failureAfterPressure.attemptedCount != 11u ||
        failureAfterPressure.droppedDetailCount != 2u ||
        failureAfterPressure.firstDroppedAttempt != 9u ||
        failureAfterPressure.firstFailureAttempt != 9u ||
        failureAfterPressure.firstFailureOperation != GX_NATIVE_ALLOC_OP_VM_COMMIT ||
        failureAfterPressure.distinctFailureCount != 2u ||
        failureAfterPressure.completedCount != 11u ||
        failureAfterPressure.successfulCount != 9u ||
        failureAfterPressure.failedCount != 2u ||
        failureAfterPressure.operationCounts[NativeAllocBackendRollbackRelease] != 1u ||
        kernel::nativeaot::diagnostics::nativeAllocBackendTerminalStatus(failureAfterPressure) !=
            NativeAllocBackendTerminalRuntimeFailure ||
        !kernel::nativeaot::diagnostics::nativeAllocBackendSummaryComplete(failureAfterPressure)) {
        return 4;
    }

    // A missing finish is never reported as a complete terminal record.
    NativeAllocBackendSummary unfinished{};
    kernel::nativeaot::diagnostics::resetNativeAllocBackendSummary(unfinished);
    uint32_t unfinishedOrdinal = 0u;
    begin(unfinished, 9u, &unfinishedOrdinal);
    if (kernel::nativeaot::diagnostics::nativeAllocBackendTerminalStatus(unfinished) !=
            NativeAllocBackendTerminalIncomplete ||
        kernel::nativeaot::diagnostics::nativeAllocBackendSummaryComplete(unfinished)) {
        return 5;
    }

    // Counter saturation and failure-code capacity have explicit incomplete
    // outcomes instead of wrapping into apparently valid totals.
    NativeAllocBackendSummary saturated{};
    kernel::nativeaot::diagnostics::resetNativeAllocBackendSummary(saturated);
    saturated.attemptedCount = UINT32_MAX;
    kernel::nativeaot::diagnostics::incrementNativeAllocBackendCounter(
        saturated.attemptedCount, saturated);
    if (saturated.counterSaturated != 1u ||
        kernel::nativeaot::diagnostics::nativeAllocBackendTerminalStatus(saturated) !=
            NativeAllocBackendTerminalIncomplete) {
        return 6;
    }

    // Outcome totals are compared at 64 bits so a wrapped 32-bit sum cannot
    // make a malformed zero-attempt summary look complete.
    NativeAllocBackendSummary wrappedOutcomeTotals{};
    kernel::nativeaot::diagnostics::resetNativeAllocBackendSummary(wrappedOutcomeTotals);
    wrappedOutcomeTotals.successfulCount = UINT32_MAX;
    wrappedOutcomeTotals.failedCount = 1u;
    if (kernel::nativeaot::diagnostics::nativeAllocBackendTerminalStatus(wrappedOutcomeTotals) !=
            NativeAllocBackendTerminalIncomplete ||
        kernel::nativeaot::diagnostics::nativeAllocBackendSummaryComplete(wrappedOutcomeTotals)) {
        return 7;
    }

    return 0;
}
