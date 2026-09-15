#pragma once

#include "kernel/types.h"

namespace kernel {
namespace nativeaot {

enum class LaunchStatus : int32_t {
    Success = 0,
    NotFound = -1,
    InvalidPath = -2,
    ReadFailed = -3,
    ArtifactTooLarge = -4,
    InvalidElf = -5,
    MappingFailed = -6,
    RuntimeFoundationFailed = -7,
    TlsFailed = -8,
    StartupFailed = -9,
    ManagedFailed = -10,
    Busy = -11,
    BaseCollision = -12,
    InvalidApplicationId = -13,
    InvalidLaunchContext = -14,
};

// The managed App Model context is deliberately small and synchronous.  The
// launcher copies exactly this many UTF-8 bytes into the NativeGxAppContext
// frame before entering the resident composite image.
static constexpr uint32_t kManagedLaunchContextMaxBytes = 48u;

struct LaunchReport {
    LaunchStatus status;
    uint32_t logicalAppId;
    int32_t managedReturn;
    uint64_t artifactBytes;
    uint64_t artifactBase;
    uint64_t artifactSpan;
    uint16_t loadSegmentCount;
    uint64_t entryPoint;
    uint64_t preTotalTracked;
    uint64_t preFreeFrames;
    uint64_t preAllocatedFrames;
    uint64_t preVmRegionFrames;
    uint64_t prePageTableFrames;
    uint64_t postTotalTracked;
    uint64_t postFreeFrames;
    uint64_t postAllocatedFrames;
    uint64_t postVmRegionFrames;
    uint64_t postPageTableFrames;
    uint64_t mapTotalTracked;
    uint64_t mapFreeFrames;
    uint64_t mapAllocatedFrames;
    uint64_t mapVmRegionFrames;
    uint64_t mapPageTableFrames;
    uint64_t accountingResidual;
    uint64_t ownerResidual;
    uint32_t sequence;
    bool runtimeInitialized;
    bool runtimeReused;
    bool residentImage;
    bool lifecycleReusable;
    bool mappingsPersistentByDesign;
    bool managedEntryReached;
    bool managedPassReached;
    bool managedInvalidApplicationObserved;
    bool launcherRegainedControl;
};

LaunchStatus launch(const char* path, LaunchReport* report);
LaunchStatus launchLogical(const char* path, uint32_t logicalAppId,
                           LaunchReport* report,
                           const char* launchContext = nullptr,
                           uint32_t launchContextLength = 0u);
bool isProductionLogicalApplicationId(const char* applicationId);
LaunchStatus launchLogicalApplication(const char* applicationId,
                                      LaunchReport* report,
                                      const char* launchContext = nullptr,
                                      uint32_t launchContextLength = 0u);
// C112 negative probes execute only after the resident image exists. They
// exercise the managed contract with bounded test metadata and never alter
// the production host table used by ordinary App Model launches.
LaunchStatus probeHostAbiMismatch(LaunchReport* report);
LaunchStatus probeCapabilityDowngrade(LaunchReport* report);
LaunchStatus probeFileCapabilityDowngrade(LaunchReport* report);
LaunchStatus probeFileServiceNegativeTests(LaunchReport* report);
LaunchStatus probeDirectoryCapabilityDowngrade(LaunchReport* report);
LaunchStatus probeFileStatCapabilityDowngrade(LaunchReport* report);
LaunchStatus probeDirectoryServiceNegativeTests(LaunchReport* report);
LaunchStatus probeDirectoryCapacityTests(LaunchReport* report);
const char* productionCompositeImagePath();
const char* launchStatusName(LaunchStatus status);

} // namespace nativeaot
} // namespace kernel
