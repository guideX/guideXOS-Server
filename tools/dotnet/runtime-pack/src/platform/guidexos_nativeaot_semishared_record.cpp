#include "guidexos_nativeaot_semishared_record.h"

#if defined(GUIDEXOS_NATIVEAOT_C188R12R4R5R4R5R2_SEMISHARED)
#include <stddef.h>

namespace {
struct SemiSharedOperation {
    uint32_t sequence;
    uint32_t operation;
    uint32_t state;
    uint32_t result;
    uint32_t detail;
    uint32_t resource;
    uint32_t resourceResult;
    uint32_t requestResource;
    uint32_t sizeLow;
    uint32_t sizeHigh;
    uint32_t alignmentLow;
    uint32_t alignmentHigh;
    uint32_t nativeStatus;
    uint32_t nativeStatusSource;
};

struct SemiSharedRecord {
    uint32_t magic;
    uint32_t version;
    uint32_t invocation;
    uint32_t operationCount;
    uint32_t overflow;
    uint32_t finalResult;
    uint32_t firstFailedOperation;
    uint32_t firstFailureBranch;
    uint32_t firstFailureStatus;
    uint32_t firstFailureStatusSource;
    uint32_t cleanupFailureOperation;
    uint32_t cleanupFailureStatus;
    uint32_t cleanupFailureStatusSource;
    uint32_t cleanupEnteredMask;
    uint32_t cleanupCompletedMask;
};

static_assert(sizeof(SemiSharedOperation) == 56u,
              "C188R12R4R5R4R5R2 operation record size changed");
static_assert(offsetof(SemiSharedOperation, nativeStatus) == 48u,
              "C188R12R4R5R4R5R2 native status offset changed");
static_assert(sizeof(SemiSharedRecord) == 60u,
              "C188R12R4R5R4R5R2 summary record size changed");

volatile SemiSharedRecord g_record = {
    GX_SEMISHARED_RECORD_MAGIC,
    GX_SEMISHARED_RECORD_VERSION,
    0u,
    0u,
    0u,
    GX_SEMISHARED_VALUE_UNAVAILABLE,
    GX_SEMISHARED_OP_NONE,
    GX_SEMISHARED_OP_NONE,
    GX_SEMISHARED_VALUE_UNAVAILABLE,
    GX_SEMISHARED_STATUS_SOURCE_UNAVAILABLE,
    GX_SEMISHARED_OP_NONE,
    GX_SEMISHARED_VALUE_UNAVAILABLE,
    GX_SEMISHARED_STATUS_SOURCE_UNAVAILABLE,
    0u,
    0u,
};
volatile SemiSharedOperation g_operations[GX_SEMISHARED_OPERATION_CAPACITY] = {};
uint32_t g_activeOperation = GX_SEMISHARED_VALUE_UNAVAILABLE;
uint32_t g_recordActive = 0u;
uint32_t g_operationStack[8] = {};
uint32_t g_operationStackDepth = 0u;

void resetOperation(uint32_t index, uint32_t operation) {
    volatile SemiSharedOperation& record = g_operations[index];
    record.sequence = index;
    record.operation = operation;
    record.state = GX_SEMISHARED_ENTERED;
    record.result = GX_SEMISHARED_VALUE_UNAVAILABLE;
    record.detail = GX_SEMISHARED_DETAIL_NONE;
    record.resource = GX_SEMISHARED_RESOURCE_NONE;
    record.resourceResult = GX_SEMISHARED_VALUE_UNAVAILABLE;
    record.requestResource = GX_SEMISHARED_RESOURCE_NONE;
    record.sizeLow = GX_SEMISHARED_VALUE_UNAVAILABLE;
    record.sizeHigh = GX_SEMISHARED_VALUE_UNAVAILABLE;
    record.alignmentLow = GX_SEMISHARED_VALUE_UNAVAILABLE;
    record.alignmentHigh = GX_SEMISHARED_VALUE_UNAVAILABLE;
    record.nativeStatus = GX_SEMISHARED_VALUE_UNAVAILABLE;
    record.nativeStatusSource = GX_SEMISHARED_STATUS_SOURCE_UNAVAILABLE;
}

volatile SemiSharedOperation* activeOperation() {
    if (g_activeOperation >= g_record.operationCount ||
        g_activeOperation >= GX_SEMISHARED_OPERATION_CAPACITY) {
        return nullptr;
    }
    return &g_operations[g_activeOperation];
}

void recordCleanupFailureIfNeeded(const volatile SemiSharedOperation& operation) {
    const uint32_t id = operation.operation;
    if (id < GX_SEMISHARED_OP_SEMI_SHARED_CLEANUP ||
        id > GX_SEMISHARED_OP_CLEANUP_BGC_START_EVENT) {
        return;
    }
    if (g_record.cleanupFailureOperation == GX_SEMISHARED_OP_NONE) {
        g_record.cleanupFailureOperation = id;
        g_record.cleanupFailureStatus = operation.nativeStatus;
        g_record.cleanupFailureStatusSource = operation.nativeStatusSource;
    }
}
}

extern "C" void __cdecl GuideXosNativeAotSemiSharedBegin(uint32_t invocation) {
    g_record.magic = GX_SEMISHARED_RECORD_MAGIC;
    g_record.version = GX_SEMISHARED_RECORD_VERSION;
    g_record.invocation = invocation;
    g_record.operationCount = 0u;
    g_record.overflow = 0u;
    g_record.finalResult = GX_SEMISHARED_VALUE_UNAVAILABLE;
    g_record.firstFailedOperation = GX_SEMISHARED_OP_NONE;
    g_record.firstFailureBranch = GX_SEMISHARED_OP_NONE;
    g_record.firstFailureStatus = GX_SEMISHARED_VALUE_UNAVAILABLE;
    g_record.firstFailureStatusSource = GX_SEMISHARED_STATUS_SOURCE_UNAVAILABLE;
    g_record.cleanupFailureOperation = GX_SEMISHARED_OP_NONE;
    g_record.cleanupFailureStatus = GX_SEMISHARED_VALUE_UNAVAILABLE;
    g_record.cleanupFailureStatusSource = GX_SEMISHARED_STATUS_SOURCE_UNAVAILABLE;
    g_record.cleanupEnteredMask = 0u;
    g_record.cleanupCompletedMask = 0u;
    g_activeOperation = GX_SEMISHARED_VALUE_UNAVAILABLE;
    g_recordActive = 1u;
    g_operationStackDepth = 0u;
}

extern "C" void __cdecl GuideXosNativeAotSemiSharedBeginOperation(uint32_t operation) {
    if (g_recordActive == 0u) {
        g_activeOperation = GX_SEMISHARED_VALUE_UNAVAILABLE;
        return;
    }
    if (g_operationStackDepth < 8u) {
        g_operationStack[g_operationStackDepth++] = g_activeOperation;
    } else {
        g_record.overflow = 1u;
        g_activeOperation = GX_SEMISHARED_VALUE_UNAVAILABLE;
        return;
    }
    const uint32_t index = g_record.operationCount;
    if (index >= GX_SEMISHARED_OPERATION_CAPACITY) {
        g_record.overflow = 1u;
        g_activeOperation = GX_SEMISHARED_VALUE_UNAVAILABLE;
        return;
    }
    resetOperation(index, operation);
    g_record.operationCount = index + 1u;
    g_activeOperation = index;
}

extern "C" void __cdecl GuideXosNativeAotSemiSharedSetDetail(uint32_t detail) {
    volatile SemiSharedOperation* operation = activeOperation();
    if (operation != nullptr) operation->detail = detail;
}

extern "C" void __cdecl GuideXosNativeAotSemiSharedSetAllocationRequest(
    uint32_t resource, uint64_t size, uint64_t alignment) {
    volatile SemiSharedOperation* operation = activeOperation();
    if (operation == nullptr) return;
    operation->requestResource = resource;
    operation->sizeLow = static_cast<uint32_t>(size);
    operation->sizeHigh = static_cast<uint32_t>(size >> 32);
    operation->alignmentLow = static_cast<uint32_t>(alignment);
    operation->alignmentHigh = static_cast<uint32_t>(alignment >> 32);
}

extern "C" void __cdecl GuideXosNativeAotSemiSharedSetResourceResult(
    uint32_t resource, uint32_t result) {
    volatile SemiSharedOperation* operation = activeOperation();
    if (operation == nullptr) return;
    operation->resource = resource;
    operation->resourceResult = result;
}

extern "C" void __cdecl GuideXosNativeAotSemiSharedSetNativeStatus(
    uint32_t status, uint32_t source) {
    volatile SemiSharedOperation* operation = activeOperation();
    if (operation == nullptr) return;
    operation->nativeStatus = status;
    operation->nativeStatusSource = source;
}

extern "C" void __cdecl GuideXosNativeAotSemiSharedCompleteOperation(
    uint32_t result, bool succeeded) {
    volatile SemiSharedOperation* operation = activeOperation();
    if (operation == nullptr) {
        // A bounded-record overflow can make the active slot unavailable.
        // Still unwind the bounded nesting stack so a parent record is usable.
        if (g_operationStackDepth != 0u) {
            g_activeOperation = g_operationStack[--g_operationStackDepth];
        } else {
            g_activeOperation = GX_SEMISHARED_VALUE_UNAVAILABLE;
        }
        return;
    }
    operation->result = result;
    operation->state = succeeded
        ? GX_SEMISHARED_RETURNED_SUCCESS : GX_SEMISHARED_EXPLICIT_FAILURE;
    if (!succeeded && g_record.firstFailedOperation == GX_SEMISHARED_OP_NONE) {
        g_record.firstFailedOperation = operation->operation;
        g_record.firstFailureStatus = operation->nativeStatus;
        g_record.firstFailureStatusSource = operation->nativeStatusSource;
    }
    if (!succeeded) recordCleanupFailureIfNeeded(*operation);
    if (g_operationStackDepth != 0u) {
        g_activeOperation = g_operationStack[--g_operationStackDepth];
    } else {
        g_activeOperation = GX_SEMISHARED_VALUE_UNAVAILABLE;
    }
}

extern "C" void __cdecl GuideXosNativeAotSemiSharedRecordFailureBranch(
    uint32_t branch) {
    if (g_record.firstFailureBranch == GX_SEMISHARED_OP_NONE) {
        g_record.firstFailureBranch = branch;
    }
}

extern "C" void __cdecl GuideXosNativeAotSemiSharedRecordCleanupEntry(uint32_t scope) {
    if (scope < 32u) g_record.cleanupEnteredMask |= (1u << scope);
}

extern "C" void __cdecl GuideXosNativeAotSemiSharedRecordCleanupComplete(uint32_t scope) {
    if (scope < 32u) g_record.cleanupCompletedMask |= (1u << scope);
}

extern "C" void __cdecl GuideXosNativeAotSemiSharedFinalize(int result) {
    GuideXosNativeAotSemiSharedBeginOperation(GX_SEMISHARED_OP_FINAL_RESULT);
    GuideXosNativeAotSemiSharedCompleteOperation(
        static_cast<uint32_t>(result), result != 0);
    g_record.finalResult = static_cast<uint32_t>(result);
    g_activeOperation = GX_SEMISHARED_VALUE_UNAVAILABLE;
    g_recordActive = 0u;
}

extern "C" uint32_t __cdecl GuideXosNativeAotSemiSharedGetSummaryField(uint32_t field) {
    switch (field) {
    case GX_SEMISHARED_SUMMARY_MAGIC: return g_record.magic;
    case GX_SEMISHARED_SUMMARY_VERSION: return g_record.version;
    case GX_SEMISHARED_SUMMARY_INVOCATION: return g_record.invocation;
    case GX_SEMISHARED_SUMMARY_OPERATION_COUNT: return g_record.operationCount;
    case GX_SEMISHARED_SUMMARY_OVERFLOW: return g_record.overflow;
    case GX_SEMISHARED_SUMMARY_FINAL_RESULT: return g_record.finalResult;
    case GX_SEMISHARED_SUMMARY_FIRST_FAILED_OPERATION: return g_record.firstFailedOperation;
    case GX_SEMISHARED_SUMMARY_FIRST_FAILURE_BRANCH: return g_record.firstFailureBranch;
    case GX_SEMISHARED_SUMMARY_FIRST_FAILURE_STATUS: return g_record.firstFailureStatus;
    case GX_SEMISHARED_SUMMARY_FIRST_FAILURE_STATUS_SOURCE: return g_record.firstFailureStatusSource;
    case GX_SEMISHARED_SUMMARY_CLEANUP_FAILURE_OPERATION: return g_record.cleanupFailureOperation;
    case GX_SEMISHARED_SUMMARY_CLEANUP_FAILURE_STATUS: return g_record.cleanupFailureStatus;
    case GX_SEMISHARED_SUMMARY_CLEANUP_FAILURE_STATUS_SOURCE: return g_record.cleanupFailureStatusSource;
    case GX_SEMISHARED_SUMMARY_CLEANUP_ENTERED_MASK: return g_record.cleanupEnteredMask;
    case GX_SEMISHARED_SUMMARY_CLEANUP_COMPLETED_MASK: return g_record.cleanupCompletedMask;
    default: return GX_SEMISHARED_VALUE_UNAVAILABLE;
    }
}

extern "C" uint32_t __cdecl GuideXosNativeAotSemiSharedGetOperationCount(void) {
    return g_record.operationCount;
}

extern "C" uint32_t __cdecl GuideXosNativeAotSemiSharedGetOperationField(
    uint32_t index, uint32_t field) {
    if (index >= g_record.operationCount || index >= GX_SEMISHARED_OPERATION_CAPACITY) {
        return GX_SEMISHARED_VALUE_UNAVAILABLE;
    }
    const volatile SemiSharedOperation& operation = g_operations[index];
    switch (field) {
    case GX_SEMISHARED_OPERATION_FIELD_SEQUENCE: return operation.sequence;
    case GX_SEMISHARED_OPERATION_FIELD_ID: return operation.operation;
    case GX_SEMISHARED_OPERATION_FIELD_STATE: return operation.state;
    case GX_SEMISHARED_OPERATION_FIELD_RESULT: return operation.result;
    case GX_SEMISHARED_OPERATION_FIELD_DETAIL: return operation.detail;
    case GX_SEMISHARED_OPERATION_FIELD_RESOURCE: return operation.resource;
    case GX_SEMISHARED_OPERATION_FIELD_RESOURCE_RESULT: return operation.resourceResult;
    case GX_SEMISHARED_OPERATION_FIELD_REQUEST_RESOURCE: return operation.requestResource;
    case GX_SEMISHARED_OPERATION_FIELD_SIZE_LOW: return operation.sizeLow;
    case GX_SEMISHARED_OPERATION_FIELD_SIZE_HIGH: return operation.sizeHigh;
    case GX_SEMISHARED_OPERATION_FIELD_ALIGNMENT_LOW: return operation.alignmentLow;
    case GX_SEMISHARED_OPERATION_FIELD_ALIGNMENT_HIGH: return operation.alignmentHigh;
    case GX_SEMISHARED_OPERATION_FIELD_NATIVE_STATUS: return operation.nativeStatus;
    case GX_SEMISHARED_OPERATION_FIELD_NATIVE_STATUS_SOURCE: return operation.nativeStatusSource;
    default: return GX_SEMISHARED_VALUE_UNAVAILABLE;
    }
}
#endif
