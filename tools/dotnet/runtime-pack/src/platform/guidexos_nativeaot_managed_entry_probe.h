#pragma once

#include <stddef.h>
#include <stdint.h>

static constexpr uint32_t kGuideXosManagedEntryProbeEntryMarker = 0x5237454Eu;
static constexpr uint32_t kGuideXosManagedEntryProbeCompletionMarker = 0x5237444Eu;
static constexpr uint32_t kGuideXosManagedEntryProbeSeed = 14u;
static constexpr uint32_t kGuideXosManagedEntryProbeResult = 49u;

struct GuideXosNativeAotManagedEntryProbeRecord {
    uint32_t entryMarker;
    uint32_t input;
    uint32_t computed;
    uint32_t completionMarker;
};

struct GuideXosNativeAotManagedEntryProbeRuntimeState {
    uint32_t recordBytes;
    uint32_t status;
    uint32_t threadStoreMembership;
    uint32_t threadStateFlags;
    uint32_t cooperativeState;
    uint32_t flsMatchesCurrentThread;
    uint32_t stackBoundsReady;
    uint32_t stackPointerInBounds;
    uint32_t transitionFrameIsTopOfStack;
    uint32_t modulesInitialized;
    uint32_t codeManagerRegistered;
    uint32_t reserved;
    uintptr_t currentThread;
    uintptr_t palFlsThread;
    uintptr_t transitionFrame;
    uintptr_t stackLow;
    uintptr_t stackHigh;
    uintptr_t stackCurrent;
    uint64_t runtimeThreadId;
    uint64_t observedThreadId;
};

static_assert(sizeof(GuideXosNativeAotManagedEntryProbeRecord) == 16u,
    "R7 managed/native probe record ABI changed");
static_assert(offsetof(GuideXosNativeAotManagedEntryProbeRecord, entryMarker) == 0u &&
              offsetof(GuideXosNativeAotManagedEntryProbeRecord, input) == 4u &&
              offsetof(GuideXosNativeAotManagedEntryProbeRecord, computed) == 8u &&
              offsetof(GuideXosNativeAotManagedEntryProbeRecord, completionMarker) == 12u,
    "R7 managed/native probe record field offsets changed");
static_assert(sizeof(GuideXosNativeAotManagedEntryProbeRuntimeState) == 112u,
    "R7 post-entry runtime-state capture ABI changed");

enum GuideXosManagedEntryProbeRuntimeStatus : uint32_t {
    GX_MANAGED_ENTRY_PROBE_RUNTIME_READY = 0u,
    GX_MANAGED_ENTRY_PROBE_RUNTIME_RECORD_INVALID = 1u,
    GX_MANAGED_ENTRY_PROBE_RUNTIME_THREAD_MISSING = 2u,
    GX_MANAGED_ENTRY_PROBE_RUNTIME_THREADSTORE_MISSING = 3u,
    GX_MANAGED_ENTRY_PROBE_RUNTIME_ATTACHMENT_INVALID = 4u,
    GX_MANAGED_ENTRY_PROBE_RUNTIME_FLS_MISMATCH = 5u,
    GX_MANAGED_ENTRY_PROBE_RUNTIME_THREAD_ID_MISMATCH = 6u,
    GX_MANAGED_ENTRY_PROBE_RUNTIME_STACK_INVALID = 7u,
    GX_MANAGED_ENTRY_PROBE_RUNTIME_FRAME_INVALID = 8u,
    GX_MANAGED_ENTRY_PROBE_RUNTIME_MODULES_UNAVAILABLE = 9u,
};

static constexpr uint32_t kGuideXosManagedEntryProbeThreadAttached = 0x1u;
static constexpr uint32_t kGuideXosManagedEntryProbeCooperative = 2u;
static constexpr uint32_t kGuideXosManagedEntryProbePreemptive = 1u;
