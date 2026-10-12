#ifndef KERNEL_SETTINGS_NATIVE_PROVIDER_H
#define KERNEL_SETTINGS_NATIVE_PROVIDER_H

#include "kernel_app.h"

#include "../../../../settings_presentation_adapter.h"

#include <stddef.h>
#include <stdint.h>

namespace kernel {
namespace settings_native {

static const uint32_t kSystemAboutSnapshotVersion = 1;
static const uint64_t kSystemAboutSnapshotRevision = 1;
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

struct SystemAboutSnapshotRequestV1 {
    uint32_t structureSize;
    uint32_t version;
    uint64_t knownRevision;
};

enum : uint32_t {
    kSystemAboutSnapshotAvailable = 1u << 0
};

struct SystemAboutSnapshotV1 {
    uint32_t structureSize;
    uint32_t version;
    uint64_t revision;
    uint32_t flags;
    char productName[32];
    char kernelVersion[16];
    char architecture[32];
};

// Copies only bounded values from kernel-owned identity/version/architecture
// sources. The returned structure contains no pointer into kernel state.
SnapshotStatus buildSystemAboutSnapshot(const char* productName,
                                        const char* kernelVersion,
                                        const char* architecture,
                                        uint64_t revision,
                                        SystemAboutSnapshotV1* output);

bool validSystemAboutSnapshot(const SystemAboutSnapshotV1& snapshot);

// The app identity is read from the AppManager-assigned KernelApp field.
// The caller supplies its output storage; the provider retains no pointer.
SnapshotStatus readSystemAboutSnapshot(
    const app::KernelApp* caller,
    const SystemAboutSnapshotRequestV1* request,
    void* output,
    size_t capacity,
    size_t* outSize,
    uint64_t* outRevision);

// S11 exposes no Settings write operation. This callback deliberately
// rejects every operation request from the S10 adapter contract.
inline bool rejectSettingsMutation(void*, const gxos::apps::settings::OperationRequest*)
{
    return false;
}

} // namespace settings_native
} // namespace kernel

#endif // KERNEL_SETTINGS_NATIVE_PROVIDER_H
