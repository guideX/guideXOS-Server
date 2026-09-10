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
};

struct LaunchReport {
    LaunchStatus status;
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
    uint64_t accountingResidual;
    uint64_t ownerResidual;
    bool mappingsPersistentByDesign;
    bool managedEntryReached;
    bool managedPassReached;
    bool launcherRegainedControl;
};

LaunchStatus launch(const char* path, LaunchReport* report);
const char* launchStatusName(LaunchStatus status);

} // namespace nativeaot
} // namespace kernel
