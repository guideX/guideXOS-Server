#pragma once

#include <stdint.h>

enum GuideXosThreadStoreReadinessEvent : uint32_t {
    GX_THREADSTORE_EVENT_ADAPTER_ENTER = 1u,
    GX_THREADSTORE_EVENT_RHINITIALIZE_RETURN = 2u,
    GX_THREADSTORE_EVENT_TLS_INSTALLED = 3u,
    GX_THREADSTORE_EVENT_RUNTIME_INSTANCE = 4u,
    GX_THREADSTORE_EVENT_THREADSTORE = 5u,
    GX_THREADSTORE_EVENT_CURRENT_THREAD_TLS = 6u,
    GX_THREADSTORE_EVENT_CURRENT_THREAD_STATE = 7u,
    GX_THREADSTORE_EVENT_MODULE_REGISTRATION_ENTER = 8u,
    GX_THREADSTORE_EVENT_MODULE_REGISTRATION_COMPLETE = 9u,
    GX_THREADSTORE_EVENT_MODULE_INITIALIZE_ENTER = 10u,
    GX_THREADSTORE_EVENT_MODULE_INITIALIZE_COMPLETE = 11u,
    GX_THREADSTORE_EVENT_TRANSITION_ENTER = 12u,
    GX_THREADSTORE_EVENT_PAL_ATTACH_ENTER = 13u,
    GX_THREADSTORE_EVENT_PAL_ATTACH_EXISTING = 14u,
    GX_THREADSTORE_EVENT_PAL_ATTACH_COMPLETE = 15u,
    GX_THREADSTORE_EVENT_TRANSITION_ATTACH_COMPLETE = 16u,
    GX_THREADSTORE_EVENT_TRANSITION_ATTACH_INCOMPLETE = 17u,
    GX_THREADSTORE_EVENT_MANAGED_ENTRY = 18u,
    GX_THREADSTORE_EVENT_TRANSITION_RETURN = 19u,
    GX_THREADSTORE_EVENT_PAL_THREAD_ID = 20u,
    GX_THREADSTORE_EVENT_PAL_STACK_BOUNDS = 21u,
    GX_THREADSTORE_EVENT_PAL_STACK_CURRENT = 22u,
    GX_THREADSTORE_EVENT_DIAGNOSTIC_SUMMARY = 23u,
    GX_THREADSTORE_EVENT_PAL_FLS_ALLOCATE = 24u,
    GX_THREADSTORE_EVENT_PAL_DETACH_ENTER = 25u,
    GX_THREADSTORE_EVENT_PAL_DETACH_EXISTING = 26u,
    GX_THREADSTORE_EVENT_PAL_DETACH_COMPLETE = 27u,
    GX_THREADSTORE_EVENT_PAL_FLS_GET_CONTEXT = 28u,
    GX_THREADSTORE_EVENT_PAL_FLS_GET_VALUE = 29u,
    GX_THREADSTORE_EVENT_PAL_FLS_SET_CONTEXT = 30u,
    GX_THREADSTORE_EVENT_PAL_FLS_SET_VALUE = 31u,
    GX_THREADSTORE_EVENT_THREADSTORE_PUBLICATION = 32u,
    GX_THREADSTORE_EVENT_TRANSITION_RETURN_FRAMES = 33u,
    GX_THREADSTORE_EVENT_TRANSITION_RETURN_CACHED = 34u,
    GX_THREADSTORE_EVENT_TRANSITION_RETURN_FLS = 35u,
    GX_THREADSTORE_EVENT_MANAGED_ENTRY_BRIDGE_BEFORE_THREAD = 36u,
    GX_THREADSTORE_EVENT_MANAGED_ENTRY_BRIDGE_BEFORE_FRAME = 37u,
    GX_THREADSTORE_EVENT_MANAGED_ENTRY_BRIDGE_AFTER_THREAD = 38u,
    GX_THREADSTORE_EVENT_MANAGED_ENTRY_BRIDGE_AFTER_FRAME = 39u,
    GX_THREADSTORE_EVENT_INITIALIZEMODULES_RETURN_THREAD = 40u,
    GX_THREADSTORE_EVENT_INITIALIZEMODULES_RETURN_FRAME = 41u,
    GX_THREADSTORE_EVENT_ATTRIBUTION_PREPARE_AFTER_INIT_THREAD = 42u,
    GX_THREADSTORE_EVENT_ATTRIBUTION_PREPARE_AFTER_INIT_FRAME = 43u,
    GX_THREADSTORE_EVENT_ATTRIBUTION_PREPARE_BEFORE_RETURN_THREAD = 44u,
    GX_THREADSTORE_EVENT_ATTRIBUTION_PREPARE_BEFORE_RETURN_FRAME = 45u
};

#if defined(_MSC_VER)
#define GX_THREADSTORE_READINESS_CALL __cdecl
#elif defined(__x86_64__)
#define GX_THREADSTORE_READINESS_CALL __attribute__((ms_abi))
#else
#define GX_THREADSTORE_READINESS_CALL
#endif

#ifdef __cplusplus
extern "C" {
#endif
void GX_THREADSTORE_READINESS_CALL GuideXosNativeAotThreadReadinessMark(
    uint32_t event, uintptr_t raw0, uintptr_t raw1);
void GX_THREADSTORE_READINESS_CALL GuideXosNativeAotThreadReadinessEmitSummary(void);
uint32_t GX_THREADSTORE_READINESS_CALL GuideXosNativeAotThreadReadinessGetOverflow(void);
#ifdef __cplusplus
}
#endif

struct GuideXosThreadStoreTransitionRecord {
    void* frame;
    void* previousTelemetryFrame;
};

// Keep diagnostic nesting in this side table. The stock runtime owns both the
// ReversePInvokeFrame fields and Thread::m_pTransitionFrame; neither is scratch
// storage for telemetry.
static inline bool GuideXosThreadStorePushTelemetryFrame(
    void* frame,
    GuideXosThreadStoreTransitionRecord* records,
    uint32_t* depth,
    uint32_t capacity) {
    if (frame == nullptr || records == nullptr ||
        depth == nullptr || *depth >= capacity) {
        return false;
    }

    void* previous = *depth == 0u ? nullptr : records[*depth - 1u].frame;
    records[*depth].frame = frame;
    records[*depth].previousTelemetryFrame = previous;
    ++(*depth);
    return true;
}

static inline bool GuideXosThreadStorePopTelemetryFrame(
    void* frame,
    GuideXosThreadStoreTransitionRecord* records,
    uint32_t* depth,
    void** restoredTelemetryFrame) {
    if (frame == nullptr || records == nullptr ||
        depth == nullptr || *depth == 0u ||
        records[*depth - 1u].frame != frame) {
        return false;
    }

    void* previous = records[*depth - 1u].previousTelemetryFrame;
    --(*depth);
    if (restoredTelemetryFrame != nullptr) {
        *restoredTelemetryFrame = previous;
    }
    return true;
}
