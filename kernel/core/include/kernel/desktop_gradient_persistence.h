#ifndef KERNEL_DESKTOP_GRADIENT_PERSISTENCE_H
#define KERNEL_DESKTOP_GRADIENT_PERSISTENCE_H

#include <stddef.h>
#include <stdint.h>

namespace kernel {
namespace desktop {

static const size_t kGradientConfigTextCapacity = 512u;
static const size_t kGradientPresetIdCapacity = 96u;
static const char kDesktopDefaultWallpaperId[] = "legacy_blue_flower";
static const char kDesktopAppDataConfigPath[] = "/AppData/DESKAPP.CFG";

enum class DesktopAppDataReadStatus : uint8_t {
    Loaded = 0,
    Missing = 1,
    Unavailable = 2,
    Invalid = 3,
    UnsupportedVersion = 4
};

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

// Load values explicitly saved by Modern Settings from the versioned file on
// the writable data volume. Missing keys are not overrides of system defaults.
DesktopAppDataReadStatus readAppDataWallpaperId(
    char* output, size_t outputCapacity);
DesktopAppDataReadStatus readAppDataDesktopIconVisibility(
    uint32_t defaultVisibility, uint32_t* outputVisibility);

// Update only the requested appearance fields in /AppData/DESKAPP.CFG while
// retaining every other byte and key. On a failed write or readback, restore
// the previous bounded file image and report whether that restoration worked.
// FAT32 cannot make this truncate-and-write sequence atomic or crash-safe.
GradientPersistenceStatus persistBuiltInBackgroundId(
    const char* expectedCurrentId,
    const char* requestedId,
    GradientPersistenceResult* output);

// Kept as a source-compatible name for the accepted S16 gradient operation.
GradientPersistenceStatus persistBuiltInGradientId(
    const char* expectedCurrentId,
    const char* requestedId,
    GradientPersistenceResult* output);

// Patch only the four supported system-icon visibility keys in the versioned
// AppData file. Missing keys use the active system defaults. Malformed known
// fields and duplicate icon keys are rejected; unknown lines are retained.
DesktopIconPersistenceStatus persistDesktopIconVisibility(
    uint32_t expectedVisibility,
    uint32_t requestedVisibility,
    DesktopIconPersistenceResult* output);

// Compatibility query for the saved AppData wallpaper override.
bool readCanonicalWallpaperId(char* output, size_t outputCapacity);

} // namespace desktop
} // namespace kernel

#endif
