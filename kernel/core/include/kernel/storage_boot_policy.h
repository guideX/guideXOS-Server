#ifndef KERNEL_STORAGE_BOOT_POLICY_H
#define KERNEL_STORAGE_BOOT_POLICY_H

#include "../../../../guideXOSBootLoader/guidexOSBootInfo.h"

namespace kernel {
namespace storage_boot {

static inline bool controller_discovery_allowed(
    guideXOS::StorageBootProfile profile)
{
    return profile == guideXOS::StorageBootProfile::LegacyDefault ||
           profile == guideXOS::StorageBootProfile::Normal;
}

static inline bool automatic_mount_allowed(
    guideXOS::StorageBootProfile profile)
{
    return controller_discovery_allowed(profile);
}

// Keep the profile admission check in the same call path used by kernel_main.
// Hosted tests supply observable initialization callbacks here; production
// supplies the ATA/AHCI/NVMe/USB/VirtIO startup sequence.
template <typename InitializeControllers>
static inline bool initialize_controllers_if_allowed(
    guideXOS::StorageBootProfile profile,
    InitializeControllers initializeControllers)
{
    if (!controller_discovery_allowed(profile)) return false;
    initializeControllers();
    return true;
}

template <typename MountAction>
static inline bool mount_if_allowed(
    guideXOS::StorageBootProfile profile,
    MountAction mountAction)
{
    if (!automatic_mount_allowed(profile)) return false;
    mountAction();
    return true;
}

} // namespace storage_boot
} // namespace kernel

#endif // KERNEL_STORAGE_BOOT_POLICY_H
