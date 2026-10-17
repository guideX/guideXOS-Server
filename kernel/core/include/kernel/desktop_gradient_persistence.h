#ifndef KERNEL_DESKTOP_GRADIENT_PERSISTENCE_H
#define KERNEL_DESKTOP_GRADIENT_PERSISTENCE_H

#include <stddef.h>
#include <stdint.h>

namespace kernel {
namespace desktop {

static const size_t kGradientConfigTextCapacity = 512u;
static const size_t kGradientPresetIdCapacity = 96u;

enum class GradientPersistenceStatus : uint32_t {
    Ok = 0,
    Unavailable = 1,
    InvalidConfiguration = 2,
    Conflict = 3,
    WriteFailedRolledBack = 4,
    ReadbackFailedRolledBack = 5,
    RollbackFailed = 6
};

struct GradientPersistenceResult {
    GradientPersistenceStatus status;
    uint32_t flags;
    char persistedId[kGradientPresetIdCapacity];
};

enum : uint32_t {
    kGradientPersistenceRollbackAttempted = 1u << 0,
    kGradientPersistenceRollbackVerified = 1u << 1,
    kGradientPersistenceReadbackAvailable = 1u << 2
};

// Update only wallpaperId in the canonical /desktop.cfg while retaining every
// other byte and key. On a failed write or readback, restore the previous
// bounded file image and report whether that restoration was verified.
GradientPersistenceStatus persistBuiltInGradientId(
    const char* expectedCurrentId,
    const char* requestedId,
    GradientPersistenceResult* output);

bool readCanonicalWallpaperId(
    char* output, size_t outputCapacity);

// Cache the writable root device while the boot-time VFS mount is available.
// This lets the settings flow restore / if another subsystem drops its mount.
void rememberDesktopGradientStorageDevice();

} // namespace desktop
} // namespace kernel

#endif
