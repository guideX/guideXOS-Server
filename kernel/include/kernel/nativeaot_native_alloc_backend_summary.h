#pragma once

#include <stdint.h>

namespace kernel {
namespace nativeaot {
namespace diagnostics {

constexpr uint32_t kNativeAllocBackendFailureCodeCapacity = 32u;

enum NativeAllocBackendOperationCounter : uint32_t {
    NativeAllocBackendReserve = 0u,
    NativeAllocBackendCommit = 1u,
    NativeAllocBackendRollbackRelease = 2u,
    NativeAllocBackendOperationCounterCount = 3u,
};

enum NativeAllocBackendTerminalStatus : uint32_t {
    NativeAllocBackendTerminalInProgress = 0u,
    NativeAllocBackendTerminalSuccess = 1u,
    NativeAllocBackendTerminalRuntimeFailure = 2u,
    NativeAllocBackendTerminalIncomplete = 3u,
};

struct NativeAllocBackendFailureCode {
    uint32_t operation;
    uint32_t branch;
    uint32_t status;
    uint32_t statusSource;
};

// Fixed-storage summary for attempts whose detailed row did not fit. The
// summary is updated on the same reserve/commit/failure/finish callbacks as
// retained rows and performs no allocation, locking, or I/O.
struct NativeAllocBackendSummary {
    uint32_t attemptedCount;
    uint32_t completedCount;
    uint32_t successfulCount;
    uint32_t failedCount;
    uint32_t droppedDetailCount;
    uint32_t firstDroppedAttempt = UINT32_MAX;
    uint32_t operationCounts[NativeAllocBackendOperationCounterCount];
    uint32_t firstFailureAttempt = UINT32_MAX;
    uint32_t firstFailureOperation = UINT32_MAX;
    uint32_t firstFailureBranch = UINT32_MAX;
    uint32_t firstFailureStatus = UINT32_MAX;
    uint32_t firstFailureStatusSource = 0u;
    uint32_t distinctFailureCount;
    uint32_t distinctFailureOverflow;
    uint32_t counterSaturated;
    NativeAllocBackendFailureCode distinctFailures[kNativeAllocBackendFailureCodeCapacity];
};

inline void resetNativeAllocBackendSummary(NativeAllocBackendSummary& summary) {
    summary = {};
    summary.firstDroppedAttempt = UINT32_MAX;
    summary.firstFailureAttempt = UINT32_MAX;
    summary.firstFailureOperation = UINT32_MAX;
    summary.firstFailureBranch = UINT32_MAX;
    summary.firstFailureStatus = UINT32_MAX;
    summary.firstFailureStatusSource = 0u;
}

inline void incrementNativeAllocBackendCounter(
    uint32_t& counter, NativeAllocBackendSummary& summary) {
    if (counter == UINT32_MAX) {
        summary.counterSaturated = 1u;
        return;
    }
    ++counter;
}

inline uint32_t beginNativeAllocBackendAttempt(
    NativeAllocBackendSummary& summary, bool detailRetained) {
    const uint32_t ordinal = summary.attemptedCount;
    incrementNativeAllocBackendCounter(summary.attemptedCount, summary);
    if (!detailRetained) {
        if (summary.firstDroppedAttempt == UINT32_MAX) {
            summary.firstDroppedAttempt = ordinal;
        }
        incrementNativeAllocBackendCounter(summary.droppedDetailCount, summary);
    }
    return ordinal;
}

inline void recordNativeAllocBackendOperation(
    NativeAllocBackendSummary& summary, NativeAllocBackendOperationCounter operation) {
    if (operation >= NativeAllocBackendOperationCounterCount) {
        summary.counterSaturated = 1u;
        return;
    }
    incrementNativeAllocBackendCounter(summary.operationCounts[operation], summary);
}

inline void recordNativeAllocBackendFailure(
    NativeAllocBackendSummary& summary, uint32_t attempt,
    uint32_t operation, uint32_t branch, uint32_t status, uint32_t statusSource) {
    if (summary.firstFailureAttempt == UINT32_MAX) {
        summary.firstFailureAttempt = attempt;
        summary.firstFailureOperation = operation;
        summary.firstFailureBranch = branch;
        summary.firstFailureStatus = status;
        summary.firstFailureStatusSource = statusSource;
    }

    for (uint32_t index = 0u; index < summary.distinctFailureCount; ++index) {
        const NativeAllocBackendFailureCode& existing = summary.distinctFailures[index];
        if (existing.operation == operation && existing.branch == branch &&
            existing.status == status && existing.statusSource == statusSource) {
            return;
        }
    }
    if (summary.distinctFailureCount >= kNativeAllocBackendFailureCodeCapacity) {
        summary.distinctFailureOverflow = 1u;
        return;
    }
    NativeAllocBackendFailureCode& code =
        summary.distinctFailures[summary.distinctFailureCount++];
    code = { operation, branch, status, statusSource };
}

inline void finishNativeAllocBackendAttempt(
    NativeAllocBackendSummary& summary, bool success) {
    incrementNativeAllocBackendCounter(summary.completedCount, summary);
    incrementNativeAllocBackendCounter(success ? summary.successfulCount : summary.failedCount,
                                       summary);
}

inline uint32_t nativeAllocBackendTerminalStatus(
    const NativeAllocBackendSummary& summary) {
    if (summary.counterSaturated != 0u || summary.distinctFailureOverflow != 0u ||
        summary.completedCount != summary.attemptedCount) {
        return NativeAllocBackendTerminalIncomplete;
    }
    const uint64_t outcomeCount = static_cast<uint64_t>(summary.successfulCount) +
        static_cast<uint64_t>(summary.failedCount);
    if (outcomeCount != static_cast<uint64_t>(summary.attemptedCount)) {
        return NativeAllocBackendTerminalIncomplete;
    }
    if (summary.failedCount != 0u || summary.distinctFailureCount != 0u) {
        return NativeAllocBackendTerminalRuntimeFailure;
    }
    if (summary.successfulCount == summary.attemptedCount) {
        return NativeAllocBackendTerminalSuccess;
    }
    return NativeAllocBackendTerminalIncomplete;
}

inline bool nativeAllocBackendSummaryComplete(
    const NativeAllocBackendSummary& summary) {
    return summary.counterSaturated == 0u && summary.distinctFailureOverflow == 0u &&
        summary.completedCount == summary.attemptedCount &&
        static_cast<uint64_t>(summary.successfulCount) +
            static_cast<uint64_t>(summary.failedCount) ==
            static_cast<uint64_t>(summary.attemptedCount);
}

} // namespace diagnostics
} // namespace nativeaot
} // namespace kernel
