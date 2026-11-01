#include "guidexos_nativeaot_gcinit_record.h"

#if defined(GUIDEXOS_NATIVEAOT_C188R12R4R5R4R5_GC_INIT_PROBES)
#include <stddef.h>

namespace {
struct GcInitRecord {
    uint32_t magic;
    uint32_t version;
    uint32_t invocation;
    uint32_t heapState;
    uint32_t finalHeapHresult;
    uint32_t initializeGcHresult;
    uint32_t semiSharedState;
    uint32_t semiSharedResult;
    uint32_t waitEventAllocationState;
    uint32_t waitEventAllocationResult;
    uint32_t waitEventCreateState;
    uint32_t waitEventCreateResult;
    uint32_t firstFailureSite;
    uint32_t nativeStatus;
    uint32_t nativeStatusSource;
    uint32_t flags;
};

static_assert(sizeof(GcInitRecord) == 64u, "C188R12R4R5R4R5 GC-init record size changed");
static_assert(offsetof(GcInitRecord, invocation) == 8u, "C188R12R4R5R4R5 invocation offset changed");
static_assert(offsetof(GcInitRecord, firstFailureSite) == 48u, "C188R12R4R5R4R5 failure-site offset changed");
static_assert(offsetof(GcInitRecord, flags) == 60u, "C188R12R4R5R4R5 flags offset changed");

volatile GcInitRecord g_gcInitRecord = {
    GX_GCINIT_RECORD_MAGIC, GX_GCINIT_RECORD_VERSION, 0u,
    GX_GCINIT_NOT_ENTERED, GX_GCINIT_VALUE_UNAVAILABLE,
    GX_GCINIT_VALUE_UNAVAILABLE, GX_GCINIT_NOT_ENTERED,
    GX_GCINIT_VALUE_UNAVAILABLE, GX_GCINIT_NOT_ENTERED,
    GX_GCINIT_VALUE_UNAVAILABLE, GX_GCINIT_NOT_ENTERED,
    GX_GCINIT_VALUE_UNAVAILABLE, GX_GCINIT_FAILURE_NONE,
    GX_GCINIT_NATIVE_STATUS_UNAVAILABLE,
    GX_GCINIT_NATIVE_STATUS_SOURCE_UNAVAILABLE, 0u
};

void resetRecord(uint32_t invocation) {
    g_gcInitRecord.magic = GX_GCINIT_RECORD_MAGIC;
    g_gcInitRecord.version = GX_GCINIT_RECORD_VERSION;
    g_gcInitRecord.invocation = invocation;
    g_gcInitRecord.heapState = GX_GCINIT_NOT_ENTERED;
    g_gcInitRecord.finalHeapHresult = GX_GCINIT_VALUE_UNAVAILABLE;
    g_gcInitRecord.initializeGcHresult = GX_GCINIT_VALUE_UNAVAILABLE;
    g_gcInitRecord.semiSharedState = GX_GCINIT_NOT_ENTERED;
    g_gcInitRecord.semiSharedResult = GX_GCINIT_VALUE_UNAVAILABLE;
    g_gcInitRecord.waitEventAllocationState = GX_GCINIT_NOT_ENTERED;
    g_gcInitRecord.waitEventAllocationResult = GX_GCINIT_VALUE_UNAVAILABLE;
    g_gcInitRecord.waitEventCreateState = GX_GCINIT_NOT_ENTERED;
    g_gcInitRecord.waitEventCreateResult = GX_GCINIT_VALUE_UNAVAILABLE;
    g_gcInitRecord.firstFailureSite = GX_GCINIT_FAILURE_NONE;
    g_gcInitRecord.nativeStatus = GX_GCINIT_NATIVE_STATUS_UNAVAILABLE;
    g_gcInitRecord.nativeStatusSource = GX_GCINIT_NATIVE_STATUS_SOURCE_UNAVAILABLE;
    g_gcInitRecord.flags = 0u;
}

void recordFirstFailure(uint32_t site) {
    if (g_gcInitRecord.firstFailureSite == GX_GCINIT_FAILURE_NONE) {
        g_gcInitRecord.firstFailureSite = site;
    }
}
}

extern "C" void __cdecl GuideXosNativeAotGcInitBeginHeap(void) {
    uint32_t invocation = g_gcInitRecord.invocation + 1u;
    if (invocation == 0u) invocation = 1u;
    resetRecord(invocation);
    g_gcInitRecord.heapState = GX_GCINIT_ENTERED;
}

extern "C" void __cdecl GuideXosNativeAotGcInitBeginSemiShared(void) {
    g_gcInitRecord.semiSharedState = GX_GCINIT_ENTERED;
    g_gcInitRecord.semiSharedResult = GX_GCINIT_VALUE_UNAVAILABLE;
}

extern "C" void __cdecl GuideXosNativeAotGcInitCompleteSemiShared(int result) {
    g_gcInitRecord.semiSharedResult = static_cast<uint32_t>(result);
    g_gcInitRecord.semiSharedState = result != 0
        ? GX_GCINIT_RETURNED_SUCCESS : GX_GCINIT_EXPLICIT_FAILURE;
    if (result == 0) recordFirstFailure(GX_GCINIT_FAILURE_INIT_SEMI_SHARED);
}

extern "C" void __cdecl GuideXosNativeAotGcInitRecordInitializeGcResult(uint32_t result) {
    g_gcInitRecord.initializeGcHresult = result;
}

extern "C" void __cdecl GuideXosNativeAotGcInitBeginWaitForGCEvent(void) {
    g_gcInitRecord.waitEventAllocationState = GX_GCINIT_ENTERED;
    g_gcInitRecord.waitEventAllocationResult = GX_GCINIT_VALUE_UNAVAILABLE;
    g_gcInitRecord.waitEventCreateState = GX_GCINIT_NOT_ENTERED;
    g_gcInitRecord.waitEventCreateResult = GX_GCINIT_VALUE_UNAVAILABLE;
}

extern "C" void __cdecl GuideXosNativeAotGcInitCompleteWaitEventAllocation(bool success) {
    g_gcInitRecord.waitEventAllocationResult = success ? 1u : 0u;
    g_gcInitRecord.waitEventAllocationState = success
        ? GX_GCINIT_RETURNED_SUCCESS : GX_GCINIT_EXPLICIT_FAILURE;
    if (!success) recordFirstFailure(GX_GCINIT_FAILURE_WAIT_EVENT_ALLOCATION);
}

extern "C" void __cdecl GuideXosNativeAotGcInitBeginWaitEventCreate(void) {
    g_gcInitRecord.waitEventCreateState = GX_GCINIT_ENTERED;
    g_gcInitRecord.waitEventCreateResult = GX_GCINIT_VALUE_UNAVAILABLE;
}

extern "C" void __cdecl GuideXosNativeAotGcInitCompleteWaitEventCreate(bool success) {
    g_gcInitRecord.waitEventCreateResult = success ? 1u : 0u;
    g_gcInitRecord.waitEventCreateState = success
        ? GX_GCINIT_RETURNED_SUCCESS : GX_GCINIT_EXPLICIT_FAILURE;
    if (!success) recordFirstFailure(GX_GCINIT_FAILURE_WAIT_EVENT_CREATE);
}

extern "C" int32_t __cdecl GuideXosNativeAotGcInitRecordHeapResult(int32_t result) {
    const uint32_t rawResult = static_cast<uint32_t>(result);
    g_gcInitRecord.finalHeapHresult = rawResult;
    g_gcInitRecord.heapState = rawResult == 0u
        ? GX_GCINIT_RETURNED_SUCCESS : GX_GCINIT_EXPLICIT_FAILURE;
    if (rawResult != 0u) recordFirstFailure(GX_GCINIT_FAILURE_OTHER_HEAP_RETURN);
    return result;
}

extern "C" void __cdecl GuideXosNativeAotGcInitFinalizeForRead(void) {
    if (g_gcInitRecord.heapState == GX_GCINIT_ENTERED)
        g_gcInitRecord.heapState = GX_GCINIT_DID_NOT_RETURN;
    if (g_gcInitRecord.semiSharedState == GX_GCINIT_ENTERED)
        g_gcInitRecord.semiSharedState = GX_GCINIT_DID_NOT_RETURN;
    if (g_gcInitRecord.waitEventAllocationState == GX_GCINIT_ENTERED)
        g_gcInitRecord.waitEventAllocationState = GX_GCINIT_DID_NOT_RETURN;
    if (g_gcInitRecord.waitEventCreateState == GX_GCINIT_ENTERED)
        g_gcInitRecord.waitEventCreateState = GX_GCINIT_DID_NOT_RETURN;
}

extern "C" uint32_t __cdecl GuideXosNativeAotGcInitGetField(uint32_t field) {
    switch (field) {
    case GX_GCINIT_FIELD_MAGIC: return g_gcInitRecord.magic;
    case GX_GCINIT_FIELD_VERSION: return g_gcInitRecord.version;
    case GX_GCINIT_FIELD_INVOCATION: return g_gcInitRecord.invocation;
    case GX_GCINIT_FIELD_HEAP_STATE: return g_gcInitRecord.heapState;
    case GX_GCINIT_FIELD_FINAL_HEAP_HRESULT: return g_gcInitRecord.finalHeapHresult;
    case GX_GCINIT_FIELD_INITIALIZE_GC_HRESULT: return g_gcInitRecord.initializeGcHresult;
    case GX_GCINIT_FIELD_SEMI_SHARED_STATE: return g_gcInitRecord.semiSharedState;
    case GX_GCINIT_FIELD_SEMI_SHARED_RESULT: return g_gcInitRecord.semiSharedResult;
    case GX_GCINIT_FIELD_WAIT_EVENT_ALLOCATION_STATE: return g_gcInitRecord.waitEventAllocationState;
    case GX_GCINIT_FIELD_WAIT_EVENT_ALLOCATION_RESULT: return g_gcInitRecord.waitEventAllocationResult;
    case GX_GCINIT_FIELD_WAIT_EVENT_CREATE_STATE: return g_gcInitRecord.waitEventCreateState;
    case GX_GCINIT_FIELD_WAIT_EVENT_CREATE_RESULT: return g_gcInitRecord.waitEventCreateResult;
    case GX_GCINIT_FIELD_FIRST_FAILURE_SITE: return g_gcInitRecord.firstFailureSite;
    case GX_GCINIT_FIELD_NATIVE_STATUS: return g_gcInitRecord.nativeStatus;
    case GX_GCINIT_FIELD_NATIVE_STATUS_SOURCE: return g_gcInitRecord.nativeStatusSource;
    case GX_GCINIT_FIELD_FLAGS: return g_gcInitRecord.flags;
    default: return GX_GCINIT_VALUE_UNAVAILABLE;
    }
}
#endif
