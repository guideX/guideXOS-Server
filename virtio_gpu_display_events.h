#pragma once

// Backend-neutral, bounded observation records for the QEMU-only VirtIO-GPU
// display-configuration event path.  These records intentionally contain no
// pointers, MMIO addresses, queue addresses, resource backing addresses, or
// persistence-owned state.

#include <stdint.h>

namespace gxos {
namespace display {

static constexpr uint32_t kVirtioGpuDisplayEventRecordVersion = 1u;
static constexpr uint32_t kVirtioGpuDisplayEventMaxScanouts = 16u;
static constexpr uint32_t kVirtioGpuDisplayEventIdentityBytes = 32u;
static constexpr uint32_t kVirtioGpuDisplayEventReasonBytes = 128u;
static constexpr uint32_t kVirtioGpuDisplayEventClassificationBytes = 40u;
static constexpr uint32_t kVirtioGpuDisplayEventActionBytes = 96u;

enum class VirtioGpuInjectedTopologyChangeKind : uint32_t {
    ConnectorState = 1u,
    PreferredGeometry = 2u,
    OutputAddition = 3u,
    OutputRemoval = 4u
};

enum class VirtioGpuTopologyChangeType : uint32_t {
    None = 0u,
    MetadataOnly = 1u,
    OutputAddition = 2u,
    OutputRemoval = 3u,
    Mixed = 4u
};

// One detected protocol scanout.  `reported` describes the latest
// GET_DISPLAY_INFO response, while `operational` and the runtime fields
// describe the still-active guest state.  They must not be conflated.
struct VirtioGpuDetectedOutput {
    uint32_t scanoutId{0u};
    uint8_t reported{0u};
    uint8_t connectorEnabled{0u};
    uint8_t operational{0u};
    uint8_t presentationReady{0u};
    uint32_t resourceId{0u};
    int32_t reportedX{0};
    int32_t reportedY{0};
    int32_t reportedWidth{0};
    int32_t reportedHeight{0};
    int32_t assignedX{0};
    int32_t assignedY{0};
    int32_t assignedWidth{0};
    int32_t assignedHeight{0};
    uint32_t currentModeWidth{0u};
    uint32_t currentModeHeight{0u};
    char stableIdentity[kVirtioGpuDisplayEventIdentityBytes]{};
    char currentModeId[kVirtioGpuDisplayEventIdentityBytes]{};
};

struct VirtioGpuDetectedTopologySnapshot {
    uint32_t version{kVirtioGpuDisplayEventRecordVersion};
    uint32_t sourceBackend{0u};
    uint32_t configGeneration{0u};
    uint32_t numScanouts{0u};
    uint32_t outputCount{0u};
    uint64_t observedTick{0u};
    char sourceBackendName[kVirtioGpuDisplayEventIdentityBytes]{};
    char deviceIdentity[kVirtioGpuDisplayEventIdentityBytes]{};
    VirtioGpuDetectedOutput outputs[kVirtioGpuDisplayEventMaxScanouts]{};
};

struct VirtioGpuDisplayTopologyChange {
    uint32_t version{kVirtioGpuDisplayEventRecordVersion};
    uint32_t oldGeneration{0u};
    uint32_t newGeneration{0u};
    uint32_t oldScanoutCount{0u};
    uint32_t newScanoutCount{0u};
    uint32_t addedOutputCount{0u};
    uint32_t removedOutputCount{0u};
    uint32_t changedOutputCount{0u};
    uint32_t connectorEnabledChangeCount{0u};
    uint32_t preferredGeometryChangeCount{0u};
    uint32_t unchangedOperationalOutputCount{0u};
    uint8_t activeConfigurationAffected{0u};
    uint8_t persistedConfigurationAffected{0u};
    uint8_t requiresResourceRebuild{0u};
    uint8_t requiresLayoutReconciliation{0u};
    uint8_t metadataOnly{0u};
    uint8_t supportedAutomatically{0u};
    uint8_t injectedEvent{0u};
    uint8_t reasserted{0u};
    uint8_t genuineDeviceEvent{0u};
    uint8_t dismissed{0u};
    uint8_t acknowledged{0u};
    uint8_t applied{0u};
    uint32_t changeType{static_cast<uint32_t>(VirtioGpuTopologyChangeType::None)};
    uint32_t injectedTopologyGeneration{0u};
    char source[kVirtioGpuDisplayEventIdentityBytes]{};
    char injectedChangeType[kVirtioGpuDisplayEventClassificationBytes]{};
    char classification[kVirtioGpuDisplayEventClassificationBytes]{};
    char recommendedAction[kVirtioGpuDisplayEventActionBytes]{};
    char reason[kVirtioGpuDisplayEventReasonBytes]{};
    char addedOutputIdentities[kVirtioGpuDisplayEventMaxScanouts][kVirtioGpuDisplayEventIdentityBytes]{};
    char removedOutputIdentities[kVirtioGpuDisplayEventMaxScanouts][kVirtioGpuDisplayEventIdentityBytes]{};
};

// Fixed-size service response.  A successful query means the observer state
// was queried; `pending` indicates whether user/service reconciliation is
// currently required.  automaticApplyPerformed is deliberately always zero
// in this milestone.
struct DisplayTopologyChangeQuery {
    uint32_t version{kVirtioGpuDisplayEventRecordVersion};
    uint32_t structureSize{0u};
    uint8_t pending{0u};
    uint8_t activeConfigurationAffected{0u};
    uint8_t automaticApplyPerformed{0u};
    uint8_t injectedEvent{0u};
    uint8_t genuineDeviceEvent{0u};
    uint8_t requiresUserAction{0u};
    uint8_t acknowledged{0u};
    uint8_t dismissed{0u};
    uint8_t applied{0u};
    uint8_t metadataOnly{0u};
    uint8_t reserved0{0u};
    uint32_t topologyGeneration{0u};
    uint32_t injectedTopologyGeneration{0u};
    uint32_t changeType{static_cast<uint32_t>(VirtioGpuTopologyChangeType::None)};
    uint32_t addedOutputCount{0u};
    uint32_t removedOutputCount{0u};
    uint32_t changedOutputCount{0u};
    uint32_t connectorEnabledChangeCount{0u};
    uint32_t preferredGeometryChangeCount{0u};
    char classification[kVirtioGpuDisplayEventClassificationBytes]{};
    char recommendedAction[kVirtioGpuDisplayEventActionBytes]{};
    char reason[kVirtioGpuDisplayEventReasonBytes]{};
    char source[kVirtioGpuDisplayEventIdentityBytes]{};
    char injectedChangeType[kVirtioGpuDisplayEventClassificationBytes]{};
    char addedOutputIdentities[kVirtioGpuDisplayEventMaxScanouts][kVirtioGpuDisplayEventIdentityBytes]{};
    char removedOutputIdentities[kVirtioGpuDisplayEventMaxScanouts][kVirtioGpuDisplayEventIdentityBytes]{};
};

} // namespace display
} // namespace gxos
