#pragma once

#include <stddef.h>
#include <stdint.h>

static constexpr uint32_t kGuideXosManagedGcRootEntryMarker = 0x52313345u;
static constexpr uint32_t kGuideXosManagedGcRootRequestMarker = 0x52313351u;
static constexpr uint32_t kGuideXosManagedGcRootCompletionMarker = 0x52313343u;
static constexpr uint32_t kGuideXosManagedGcRootReturnMarker = 0x52313352u;
static constexpr uint32_t kGuideXosManagedGcRootSeed = 14u;
static constexpr uint32_t kGuideXosManagedGcRootInitialResult = 1122u;
static constexpr uint32_t kGuideXosManagedGcRootFinalResult = 1123u;

struct GuideXosNativeAotManagedGcRootProbeRecord {
    uint32_t entryMarker;
    uint32_t input;
    uint32_t initialResult;
    uint32_t requestMarker;
    uint32_t counterStatusBefore;
    uint32_t collectionCountBefore;
    uint32_t collectionCountAfter;
    uint32_t counterStatusAfter;
    uint32_t completionMarker;
    uint32_t finalResult;
    uint32_t returnMarker;
};

static_assert(sizeof(GuideXosNativeAotManagedGcRootProbeRecord) == 44u,
    "R13 managed-GC record ABI changed");
static_assert(offsetof(GuideXosNativeAotManagedGcRootProbeRecord, requestMarker) == 12u &&
              offsetof(GuideXosNativeAotManagedGcRootProbeRecord, collectionCountBefore) == 20u &&
              offsetof(GuideXosNativeAotManagedGcRootProbeRecord, completionMarker) == 32u &&
              offsetof(GuideXosNativeAotManagedGcRootProbeRecord, returnMarker) == 40u,
    "R13 managed-GC record field offsets changed");
