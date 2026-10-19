#ifndef KERNEL_DESKTOP_GRADIENT_PERSISTENCE_H
#define KERNEL_DESKTOP_GRADIENT_PERSISTENCE_H

#include <stddef.h>
#include <stdint.h>

namespace kernel {
namespace desktop {

static const size_t kGradientConfigTextCapacity = 512u;
static const size_t kGradientPresetIdCapacity = 96u;
static const char kDesktopDefaultWallpaperId[] = "legacy_blue_flower";

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

enum : uint32_t {
    kDesktopIconVisibilityTrash = 1u << 0,
    kDesktopIconVisibilityThisSystem = 1u << 1,
    kDesktopIconVisibilityFileManager = 1u << 2,
    kDesktopIconVisibilitySystemSettings = 1u << 3,
    kDesktopIconVisibilityKnownMask = (1u << 4) - 1u
};

enum class DesktopIconPersistenceStatus : uint32_t {
    Ok = 0,
    NoChange = 1,
    Unavailable = 2,
    InvalidConfiguration = 3,
    Conflict = 4,
    WriteFailedRolledBack = 5,
    ReadbackFailedRolledBack = 6,
    RollbackFailed = 7
};

struct DesktopIconPersistenceResult {
    DesktopIconPersistenceStatus status;
    uint32_t flags;
    uint32_t persistedVisibility;
};

enum : uint32_t {
    kDesktopIconPersistenceRollbackAttempted = 1u << 0,
    kDesktopIconPersistenceRollbackVerified = 1u << 1,
    kDesktopIconPersistenceReadbackAvailable = 1u << 2
};

// Update only wallpaperId in the canonical /desktop.cfg while retaining every
// other byte and key. On a failed write or readback, restore the previous
// bounded file image and report whether that restoration was verified.
GradientPersistenceStatus persistBuiltInBackgroundId(
    const char* expectedCurrentId,
    const char* requestedId,
    GradientPersistenceResult* output);

// Kept as a source-compatible name for the accepted S16 gradient operation.
GradientPersistenceStatus persistBuiltInGradientId(
    const char* expectedCurrentId,
    const char* requestedId,
    GradientPersistenceResult* output);

// Patch only the four supported system-icon visibility keys in /desktop.cfg.
// Missing keys use legacy defaults. Malformed known fields and duplicate icon
// keys are rejected. Unknown key/value lines and unrelated bytes are retained.
DesktopIconPersistenceStatus persistDesktopIconVisibility(
    uint32_t expectedVisibility,
    uint32_t requestedVisibility,
    DesktopIconPersistenceResult* output);

bool readCanonicalWallpaperId(
    char* output, size_t outputCapacity);

// Cache the writable root device while the boot-time VFS mount is available.
// This lets the settings flow restore / if another subsystem drops its mount.
void rememberDesktopGradientStorageDevice();

} // namespace desktop
} // namespace kernel

#endif
