#ifndef KERNEL_APPLICATION_SNAPSHOT_H
#define KERNEL_APPLICATION_SNAPSHOT_H

#include <stddef.h>
#include <stdint.h>

namespace kernel {
namespace app {

static constexpr uint32_t kApplicationSnapshotRecordVersion = 1u;
static constexpr uint32_t kApplicationSnapshotAppManagerCapacity = 16u;
static constexpr uint32_t kApplicationSnapshotManagedCapacity = 3u;
// C162 permits Notes, Managed Calculator, and Managed Task Manager to coexist
// while preserving the full bounded AppManager and shell snapshot sources.
static constexpr uint32_t kApplicationSnapshotCapacity = 20u;
static constexpr uint32_t kApplicationSnapshotDisplayNameBytes = 32u;
static constexpr uint32_t kApplicationSnapshotApplicationIdBytes = 96u;

enum class ApplicationSnapshotSource : uint32_t {
    AppManagerInstance = 1u,
    ShellSurface = 2u,
    ManagedLogicalApplication = 3u,
};

enum class ApplicationSnapshotState : uint32_t {
    NotLoaded = 0u,
    Running = 1u,
    Suspended = 2u,
    Terminated = 3u,
};

enum ApplicationSnapshotFlags : uint32_t {
    ApplicationSnapshotFlagNone = 0u,
    ApplicationSnapshotFlagActive = 1u << 0,
};

// Value-only record shared with the managed NativeAOT host. Its explicit
// reserved fields keep the 168-byte record layout stable across compilers.
struct alignas(8) ApplicationSnapshotRecord {
    uint32_t recordVersion;
    uint32_t source;
    uint64_t instanceId;
    uint32_t state;
    uint32_t flags;
    uint32_t displayNameLength;
    uint32_t applicationIdLength;
    uint32_t reserved0;
    uint32_t reserved1;
    uint8_t displayName[kApplicationSnapshotDisplayNameBytes];
    uint8_t applicationId[kApplicationSnapshotApplicationIdBytes];
};

static_assert(sizeof(ApplicationSnapshotRecord) == 168u,
              "C160 application snapshot record size drift");
static_assert(offsetof(ApplicationSnapshotRecord, recordVersion) == 0u,
              "C160 snapshot record version offset drift");
static_assert(offsetof(ApplicationSnapshotRecord, source) == 4u,
              "C160 snapshot record source offset drift");
static_assert(offsetof(ApplicationSnapshotRecord, instanceId) == 8u,
              "C160 snapshot record identity offset drift");
static_assert(offsetof(ApplicationSnapshotRecord, state) == 16u,
              "C160 snapshot record state offset drift");
static_assert(offsetof(ApplicationSnapshotRecord, flags) == 20u,
              "C160 snapshot record flags offset drift");
static_assert(offsetof(ApplicationSnapshotRecord, displayNameLength) == 24u,
              "C160 snapshot record name length offset drift");
static_assert(offsetof(ApplicationSnapshotRecord, applicationIdLength) == 28u,
              "C160 snapshot record application ID length offset drift");
static_assert(offsetof(ApplicationSnapshotRecord, reserved0) == 32u,
              "C160 snapshot record reserved0 offset drift");
static_assert(offsetof(ApplicationSnapshotRecord, reserved1) == 36u,
              "C160 snapshot record reserved1 offset drift");
static_assert(offsetof(ApplicationSnapshotRecord, displayName) == 40u,
              "C160 snapshot record name offset drift");
static_assert(offsetof(ApplicationSnapshotRecord, applicationId) == 72u,
              "C160 snapshot record application ID offset drift");

} // namespace app
} // namespace kernel

#endif // KERNEL_APPLICATION_SNAPSHOT_H
