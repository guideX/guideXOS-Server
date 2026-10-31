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
    GX_THREADSTORE_EVENT_PAL_STACK_CURRENT = 22u
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
#ifdef __cplusplus
}
#endif

struct GuideXosThreadStoreTransitionRecord {
    void* frame;
    void* previousTelemetryFrame;
};

// Store diagnostics outside ReversePInvokeFrame. The stock runtime owns that
// frame's fields and reads them again in RhpReversePInvokeReturn.
static inline bool GuideXosThreadStorePushTelemetryFrame(
    void* frame,
    unsigned char* runtimeCell,
    uint32_t telemetryOffset,
    GuideXosThreadStoreTransitionRecord* records,
    uint32_t* depth,
    uint32_t capacity) {
    if (frame == nullptr || runtimeCell == nullptr || records == nullptr ||
        depth == nullptr || *depth >= capacity) {
        return false;
    }

    void** telemetry = reinterpret_cast<void**>(runtimeCell + telemetryOffset);
    records[*depth].frame = frame;
    records[*depth].previousTelemetryFrame = *telemetry;
    ++(*depth);
    *telemetry = frame;
    return true;
}

static inline bool GuideXosThreadStorePopTelemetryFrame(
    void* frame,
    unsigned char* runtimeCell,
    uint32_t telemetryOffset,
    GuideXosThreadStoreTransitionRecord* records,
    uint32_t* depth,
    void** restoredTelemetryFrame) {
    if (frame == nullptr || runtimeCell == nullptr || records == nullptr ||
        depth == nullptr || *depth == 0u ||
        records[*depth - 1u].frame != frame) {
        return false;
    }

    void* previous = records[*depth - 1u].previousTelemetryFrame;
    --(*depth);
    void** telemetry = reinterpret_cast<void**>(runtimeCell + telemetryOffset);
    *telemetry = previous;
    if (restoredTelemetryFrame != nullptr) {
        *restoredTelemetryFrame = previous;
    }
    return true;
}
