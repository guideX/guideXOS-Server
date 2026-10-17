#ifndef KERNEL_SETTINGS_NATIVE_PROVIDER_H
#define KERNEL_SETTINGS_NATIVE_PROVIDER_H

#include "kernel_app.h"

#include "../../../../settings_presentation_adapter.h"

#include <stddef.h>
#include <stdint.h>

namespace kernel {
namespace settings_native {

static const uint32_t kSystemAboutSnapshotVersion = 2;
static const uint64_t kSystemAboutSnapshotRevision = 1;
static const uint32_t kDisplaySnapshotVersion = 1;
static const uint32_t kDesktopAppearanceSnapshotVersion = 1;
static const uint32_t kBuiltInGradientPresetListVersion = 1;
static const uint32_t kApplyBuiltInGradientPresetVersion = 1;
static const uint32_t kApplyDesktopIconVisibilityVersion = 1;
static const size_t kBuiltInGradientPresetCapacity = 16u;
static const uint32_t kBuiltInWallpaperPresetListVersion = 1;
static const uint32_t kApplyBuiltInWallpaperVersion = 1;
static const size_t kBuiltInWallpaperPresetCapacity = 16u;
// Reserved revision for the zero/unavailable active-configuration generation.
static const uint64_t kDisplaySnapshotRevision = 1;
static const char kSettingsRuntimeAppId[] = "gxos.builtin.settings";

enum class SnapshotStatus : uint32_t {
    Ok,
    InvalidArgument,
    UnsupportedVersion,
    BufferTooSmall,
    Unauthorized,
    StaleRevision,
    Unavailable
};

struct SystemAboutSnapshotRequestV2 {
    uint32_t structureSize;
    uint32_t version;
    uint64_t knownRevision;
};

enum : uint32_t {
    kSystemAboutSnapshotAvailable = 1u << 0,
    kSystemAboutProductNameAvailable = 1u << 1,
    kSystemAboutOsReleaseVersionAvailable = 1u << 2,
    kSystemAboutKernelVersionAvailable = 1u << 3,
    kSystemAboutArchitectureAvailable = 1u << 4
};

struct SystemAboutSnapshotV2 {
    uint32_t structureSize;
    uint32_t version;
    uint64_t revision;
    uint32_t flags;
    char productName[32];
    char osReleaseVersion[16];
    char kernelVersion[16];
    char architecture[32];
};

// Copies bounded values from kernel-owned product, kernel-version, and
// architecture sources. An absent OS release remains explicitly unavailable.
// The returned structure contains no pointer into kernel state.
SnapshotStatus buildSystemAboutSnapshot(const char* productName,
                                        const char* osReleaseVersion,
                                        const char* kernelVersion,
                                        const char* architecture,
                                        uint64_t revision,
                                        SystemAboutSnapshotV2* output);

bool validSystemAboutSnapshot(const SystemAboutSnapshotV2& snapshot);

// The app identity is read from the AppManager-assigned KernelApp field.
// The caller supplies its output storage; the provider retains no pointer.
SnapshotStatus readSystemAboutSnapshot(
    const app::KernelApp* caller,
    const SystemAboutSnapshotRequestV2* request,
    void* output,
    size_t capacity,
    size_t* outSize,
    uint64_t* outRevision);

struct DisplaySnapshotRequestV1 {
    uint32_t structureSize;
    uint32_t version;
    uint64_t knownRevision;
};

enum class DisplayInformationStatus : uint32_t {
    Unavailable = 0,
    Available = 1
};

enum : uint32_t {
    kDisplayFramebufferWidthAvailable = 1u << 0,
    kDisplayFramebufferHeightAvailable = 1u << 1,
    kDisplayPixelFormatAvailable = 1u << 2,
    kDisplayBitsPerPixelAvailable = 1u << 3,
    kDisplayPitchAvailable = 1u << 4,
    kDisplayLogicalDesktopWidthAvailable = 1u << 5,
    kDisplayLogicalDesktopHeightAvailable = 1u << 6,
    kDisplayActiveLayoutModeAvailable = 1u << 7,
    kDisplayPrimaryModeIdAvailable = 1u << 8
};

static const size_t kDisplayPrimaryModeIdCapacity = 32;

struct DisplaySnapshotV1 {
    uint32_t structureSize;
    uint32_t version;
    uint64_t revision;
    DisplayInformationStatus status;
    uint32_t flags;
    uint32_t framebufferWidth;
    uint32_t framebufferHeight;
    uint32_t pixelFormat;
    uint32_t bitsPerPixel;
    uint32_t bytesPerScanline;
    uint32_t logicalDesktopWidth;
    uint32_t logicalDesktopHeight;
    uint32_t activeLayoutMode;
    char primaryModeId[kDisplayPrimaryModeIdCapacity];
};

// Creates a value-only snapshot. Unsupported or inconsistent optional fields
// remain absent; unavailable framebuffer geometry is represented explicitly.
SnapshotStatus buildDisplaySnapshot(bool framebufferAvailable,
                                    uint32_t width,
                                    uint32_t height,
                                    uint32_t pitch,
                                    uint32_t bitsPerPixel,
                                    uint32_t pixelFormat,
                                    uint64_t revision,
                                    DisplaySnapshotV1* output);

bool validDisplaySnapshot(const DisplaySnapshotV1& snapshot);

// Reads live framebuffer metadata and a single active-configuration response
// for a trusted Settings caller. Revisions encode the authoritative 32-bit
// active-configuration generation plus one, even when that response truthfully
// reports no active topology. The snapshot contains no address, pointer, or
// writable mapping.
SnapshotStatus readDisplaySnapshot(
    const app::KernelApp* caller,
    const DisplaySnapshotRequestV1* request,
    void* output,
    size_t capacity,
    size_t* outSize,
    uint64_t* outRevision);

struct DesktopAppearanceSnapshotRequestV1 {
    uint32_t structureSize;
    uint32_t version;
    uint64_t knownRevision;
};

enum : uint32_t {
    kDesktopAppearanceBackgroundAvailable = 1u << 0,
    kDesktopAppearanceScaleModeAvailable = 1u << 1,
    kDesktopAppearanceIconSettingsAvailable = 1u << 2,
    kDesktopAppearanceGradientColorsAvailable = 1u << 3,
    kDesktopAppearanceIconTrashVisible = 1u << 8,
    kDesktopAppearanceIconThisSystemVisible = 1u << 9,
    kDesktopAppearanceIconFileManagerVisible = 1u << 10,
    kDesktopAppearanceIconSystemSettingsVisible = 1u << 11
};

enum class DesktopBackgroundKind : uint32_t {
    Wallpaper = 1,
    Gradient = 2
};

static const size_t kDesktopAppearanceIdCapacity = 96;
static const size_t kDesktopAppearanceNameCapacity = 32;
static const size_t kDesktopAppearanceScaleModeCapacity = 16;

struct DesktopAppearanceSnapshotV1 {
    uint32_t structureSize;
    uint32_t version;
    uint64_t revision;
    uint32_t flags;
    DesktopBackgroundKind backgroundKind;
    uint32_t gradientTopColor;
    uint32_t gradientBottomColor;
    uint32_t gradientAccentColor;
    char backgroundId[kDesktopAppearanceIdCapacity];
    char backgroundName[kDesktopAppearanceNameCapacity];
    char backgroundScaleMode[kDesktopAppearanceScaleModeCapacity];
};

struct BuiltInGradientPresetV1 {
    uint32_t structureSize;
    uint32_t version;
    uint32_t topColor;
    uint32_t bottomColor;
    uint32_t accentColor;
    char id[kDesktopAppearanceIdCapacity];
    char displayName[kDesktopAppearanceNameCapacity];
};

enum : uint32_t {
    kBuiltInWallpaperAssetPresent = 1u << 0
};

struct BuiltInWallpaperPresetV1 {
    uint32_t structureSize;
    uint32_t version;
    uint32_t flags;
    char id[kDesktopAppearanceIdCapacity];
    char displayName[kDesktopAppearanceNameCapacity];
};

enum class GradientOperationStatus : uint32_t {
    Committed = 0,
    NoChange = 1,
    InvalidPreset = 2,
    StaleRevision = 3,
    PersistenceUnavailable = 4,
    PersistenceConflict = 5,
    ApplyFailed = 6,
    SaveFailedRolledBack = 7,
    ReadbackFailedRolledBack = 8,
    RollbackFailed = 9,
    Unauthorized = 10,
    InvalidArgument = 11,
    UnsupportedVersion = 12,
    AssetUnavailable = 13
};

typedef GradientOperationStatus WallpaperOperationStatus;

enum : uint32_t {
    kGradientOperationActiveAvailable = 1u << 0,
    kGradientOperationPersistedAvailable = 1u << 1,
    kGradientOperationRollbackVerified = 1u << 2
};

struct ApplyBuiltInGradientPresetRequestV1 {
    uint32_t structureSize;
    uint32_t version;
    uint64_t expectedRevision;
    char presetId[kDesktopAppearanceIdCapacity];
};

struct ApplyBuiltInGradientPresetResultV1 {
    uint32_t structureSize;
    uint32_t version;
    GradientOperationStatus status;
    uint32_t flags;
    uint64_t revision;
    char appliedId[kDesktopAppearanceIdCapacity];
    char persistedId[kDesktopAppearanceIdCapacity];
};

struct ApplyBuiltInWallpaperRequestV1 {
    uint32_t structureSize;
    uint32_t version;
    uint64_t expectedRevision;
    char wallpaperId[kDesktopAppearanceIdCapacity];
};

struct ApplyBuiltInWallpaperResultV1 {
    uint32_t structureSize;
    uint32_t version;
    WallpaperOperationStatus status;
    uint32_t flags;
    uint64_t revision;
    char appliedId[kDesktopAppearanceIdCapacity];
    char persistedId[kDesktopAppearanceIdCapacity];
};

enum : uint32_t {
    kDesktopIconTargetTrash = 1u << 0,
    kDesktopIconTargetThisSystem = 1u << 1,
    kDesktopIconTargetFileManager = 1u << 2,
    kDesktopIconTargetSystemSettings = 1u << 3,
    kDesktopIconTargetFileExplorer =
        kDesktopIconTargetThisSystem | kDesktopIconTargetFileManager,
    kDesktopIconTargetKnownMask = (1u << 4) - 1u
};

enum : uint32_t {
    kDesktopIconOperationActiveAvailable = 1u << 0,
    kDesktopIconOperationPersistedAvailable = 1u << 1,
    kDesktopIconOperationRollbackVerified = 1u << 2
};

enum class DesktopIconOperationStatus : uint32_t {
    Committed = 0,
    NoChange = 1,
    InvalidTarget = 2,
    StaleRevision = 3,
    PersistenceUnavailable = 4,
    PersistenceConflict = 5,
    SaveFailedRolledBack = 6,
    ReadbackFailedRolledBack = 7,
    RollbackFailed = 8,
    ApplyFailed = 9,
    Unauthorized = 10,
    InvalidArgument = 11,
    UnsupportedVersion = 12
};

struct ApplyDesktopIconVisibilityRequestV1 {
    uint32_t structureSize;
    uint32_t version;
    uint64_t expectedRevision;
    uint32_t targetMask;
    uint32_t visibleMask;
};

struct ApplyDesktopIconVisibilityResultV1 {
    uint32_t structureSize;
    uint32_t version;
    DesktopIconOperationStatus status;
    uint32_t flags;
    uint64_t revision;
    uint32_t activeVisibility;
    uint32_t persistedVisibility;
};

SnapshotStatus buildDesktopAppearanceSnapshot(
    const char* backgroundId,
    const char* backgroundName,
    const char* backgroundScaleMode,
    uint32_t backgroundKind,
    uint32_t gradientTopColor,
    uint32_t gradientBottomColor,
    uint32_t gradientAccentColor,
    uint32_t visibleIconFlags,
    uint64_t revision,
    DesktopAppearanceSnapshotV1* output);

bool validDesktopAppearanceSnapshot(const DesktopAppearanceSnapshotV1& snapshot);

// Copies the active desktop-owned background, scale mode, and supported
// system icon visibility into a bounded, read-only value snapshot.
SnapshotStatus readDesktopAppearanceSnapshot(
    const app::KernelApp* caller,
    const DesktopAppearanceSnapshotRequestV1* request,
    void* output,
    size_t capacity,
    size_t* outSize,
    uint64_t* outRevision);

// Built-in presets are copied from the same desktop-owned registry used by
// legacy Display Options; the caller receives no mutable desktop state.
SnapshotStatus readBuiltInGradientPresets(
    const app::KernelApp* caller,
    BuiltInGradientPresetV1* output,
    size_t capacity,
    size_t* outCount);

// Copies the fixed built-in image catalog owned by the desktop renderer.
// AssetPresent means the fixed image path is present in the mounted pack;
// Apply still verifies the full GXIMG decode before mutating desktop state.
SnapshotStatus readBuiltInWallpaperPresets(
    const app::KernelApp* caller,
    BuiltInWallpaperPresetV1* output,
    size_t capacity,
    size_t* outCount);

// The caller identity is read from the AppManager-assigned KernelApp field.
// No application-supplied identity string is part of this request.
GradientOperationStatus applyBuiltInGradientPreset(
    const app::KernelApp* caller,
    const ApplyBuiltInGradientPresetRequestV1* request,
    ApplyBuiltInGradientPresetResultV1* output);

// Applies one fixed built-in image through the shared desktop appearance
// transaction. Caller identity comes only from the runtime-owned KernelApp.
WallpaperOperationStatus applyBuiltInWallpaper(
    const app::KernelApp* caller,
    const ApplyBuiltInWallpaperRequestV1* request,
    ApplyBuiltInWallpaperResultV1* output);

// The request contains only bounded system-icon target/value masks. Caller
// identity is taken from the AppManager-owned KernelApp runtime identity.
DesktopIconOperationStatus applyDesktopIconVisibility(
    const app::KernelApp* caller,
    const ApplyDesktopIconVisibilityRequestV1* request,
    ApplyDesktopIconVisibilityResultV1* output);

// The native adapter exposes no Settings write operation. This callback deliberately
// rejects every operation request from the S10 adapter contract.
inline bool rejectSettingsMutation(void*, const gxos::apps::settings::OperationRequest*)
{
    return false;
}

} // namespace settings_native
} // namespace kernel

#endif // KERNEL_SETTINGS_NATIVE_PROVIDER_H
