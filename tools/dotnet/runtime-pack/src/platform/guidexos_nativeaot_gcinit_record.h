#pragma once

#include <stdint.h>

#if defined(GUIDEXOS_NATIVEAOT_C188R12R4R5R4R5_GC_INIT_PROBES)
enum guidexos_nativeaot_gcinit_state : uint32_t {
    GX_GCINIT_NOT_ENTERED = 0u,
    GX_GCINIT_ENTERED = 1u,
    GX_GCINIT_RETURNED_SUCCESS = 2u,
    GX_GCINIT_EXPLICIT_FAILURE = 3u,
    GX_GCINIT_DID_NOT_RETURN = 4u,
    GX_GCINIT_STATE_RESULT_UNAVAILABLE = 5u,
    GX_GCINIT_TRACE_INCOMPLETE = 6u,
};

enum guidexos_nativeaot_gcinit_failure_site : uint32_t {
    GX_GCINIT_FAILURE_NONE = 0u,
    GX_GCINIT_FAILURE_INIT_SEMI_SHARED = 1u,
    GX_GCINIT_FAILURE_WAIT_EVENT_ALLOCATION = 2u,
    GX_GCINIT_FAILURE_WAIT_EVENT_CREATE = 3u,
    GX_GCINIT_FAILURE_OTHER_HEAP_RETURN = 4u,
};

enum guidexos_nativeaot_gcinit_record_field : uint32_t {
    GX_GCINIT_FIELD_MAGIC = 0u,
    GX_GCINIT_FIELD_VERSION = 1u,
    GX_GCINIT_FIELD_INVOCATION = 2u,
    GX_GCINIT_FIELD_HEAP_STATE = 3u,
    GX_GCINIT_FIELD_FINAL_HEAP_HRESULT = 4u,
    GX_GCINIT_FIELD_INITIALIZE_GC_HRESULT = 5u,
    GX_GCINIT_FIELD_SEMI_SHARED_STATE = 6u,
    GX_GCINIT_FIELD_SEMI_SHARED_RESULT = 7u,
    GX_GCINIT_FIELD_WAIT_EVENT_ALLOCATION_STATE = 8u,
    GX_GCINIT_FIELD_WAIT_EVENT_ALLOCATION_RESULT = 9u,
    GX_GCINIT_FIELD_WAIT_EVENT_CREATE_STATE = 10u,
    GX_GCINIT_FIELD_WAIT_EVENT_CREATE_RESULT = 11u,
    GX_GCINIT_FIELD_FIRST_FAILURE_SITE = 12u,
    GX_GCINIT_FIELD_NATIVE_STATUS = 13u,
    GX_GCINIT_FIELD_NATIVE_STATUS_SOURCE = 14u,
    GX_GCINIT_FIELD_FLAGS = 15u,
    GX_GCINIT_FIELD_COUNT = 16u,
};

enum : uint32_t {
    GX_GCINIT_RECORD_MAGIC = 0x43385235u,
    GX_GCINIT_RECORD_VERSION = 1u,
    GX_GCINIT_VALUE_UNAVAILABLE = 0xFFFFFFFFu,
    GX_GCINIT_NATIVE_STATUS_UNAVAILABLE = 0xFFFFFFFFu,
    GX_GCINIT_NATIVE_STATUS_SOURCE_UNAVAILABLE = 0u,
};

extern "C" void __cdecl GuideXosNativeAotGcInitBeginHeap(void);
extern "C" void __cdecl GuideXosNativeAotGcInitBeginSemiShared(void);
extern "C" void __cdecl GuideXosNativeAotGcInitCompleteSemiShared(int result);
extern "C" void __cdecl GuideXosNativeAotGcInitRecordInitializeGcResult(uint32_t result);
extern "C" void __cdecl GuideXosNativeAotGcInitBeginWaitForGCEvent(void);
extern "C" void __cdecl GuideXosNativeAotGcInitCompleteWaitEventAllocation(bool success);
extern "C" void __cdecl GuideXosNativeAotGcInitBeginWaitEventCreate(void);
extern "C" void __cdecl GuideXosNativeAotGcInitCompleteWaitEventCreate(bool success);
extern "C" int32_t __cdecl GuideXosNativeAotGcInitRecordHeapResult(int32_t result);
extern "C" uint32_t __cdecl GuideXosNativeAotGcInitGetField(uint32_t field);
extern "C" void __cdecl GuideXosNativeAotGcInitFinalizeForRead(void);
#endif
