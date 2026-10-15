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

// The native adapter exposes no Settings write operation. This callback deliberately
// rejects every operation request from the S10 adapter contract.
inline bool rejectSettingsMutation(void*, const gxos::apps::settings::OperationRequest*)
{
    return false;
}

} // namespace settings_native
} // namespace kernel

#endif // KERNEL_SETTINGS_NATIVE_PROVIDER_H
