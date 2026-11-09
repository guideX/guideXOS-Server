#pragma once

#include <stddef.h>
#include <stdint.h>

static constexpr uint32_t kGuideXosManagedSettingsObjectEntryMarker = 0x52313245u;
static constexpr uint32_t kGuideXosManagedSettingsObjectCompletionMarker = 0x52313244u;
static constexpr uint32_t kGuideXosManagedSettingsObjectConstructorMarker = 0x52313243u;
static constexpr uint32_t kGuideXosManagedSettingsObjectSeed = 14u;
static constexpr uint32_t kGuideXosManagedSettingsObjectResult = 1122u;

struct GuideXosNativeAotManagedSettingsObjectAllocationState {
    uint32_t recordBytes;
    uint32_t status;
    uint32_t allocationRequests;
    uint32_t allocationCompleted;
    uint32_t capacityFailures;
    uint32_t contractFailures;
    uint32_t threadAttached;
    uint32_t methodTableMatches;
    uint32_t objectOwnedByGcHeap;
    uint32_t gcReadBeforeSucceeded;
    uint32_t gcReadAfterSucceeded;
    uint32_t gcCountBefore;
    uint32_t gcCountAfter;
    uint32_t allocationContextReadyBefore;
    uint32_t allocationContextReadyAfter;
    uint32_t reserved;
    uintptr_t objectSize;
    uintptr_t allocationBytesAvailableBefore;
    uintptr_t allocationBytesAvailableAfter;
    uintptr_t allocationContextBytesAdvanced;
};

static_assert(sizeof(GuideXosNativeAotManagedSettingsObjectAllocationState) == 96u,
    "R12 managed-object allocation state ABI changed");
static_assert(offsetof(GuideXosNativeAotManagedSettingsObjectAllocationState,
                       objectSize) == 64u &&
              offsetof(GuideXosNativeAotManagedSettingsObjectAllocationState,
                       allocationContextBytesAdvanced) == 88u,
    "R12 managed-object allocation state field offsets changed");

enum GuideXosNativeAotManagedSettingsObjectAllocationStatus : uint32_t {
    GX_MANAGED_SETTINGS_OBJECT_ALLOCATION_READY = 0u,
    GX_MANAGED_SETTINGS_OBJECT_ALLOCATION_INVALID_RECORD = 1u,
    GX_MANAGED_SETTINGS_OBJECT_ALLOCATION_NOT_ATTEMPTED = 2u,
    GX_MANAGED_SETTINGS_OBJECT_ALLOCATION_COUNT_INVALID = 3u,
    GX_MANAGED_SETTINGS_OBJECT_ALLOCATION_CONTRACT_INVALID = 4u,
};
