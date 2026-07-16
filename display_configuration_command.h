#pragma once

// Kernel-safe guest display configuration control contract.
//
// This header is shared by the hosted server and the bare-metal kernel.  It is
// deliberately fixed-size and backend-neutral so it can later cross a real
// process boundary without exposing resource pointers, kernel addresses, UI
// objects, or MMIO state.

#include <stdint.h>

namespace gxos {
namespace display {

static constexpr uint32_t kDisplayConfigurationContractVersion = 1u;
static constexpr uint32_t kDisplayConfigurationMaxOutputs = 4u;
static constexpr uint32_t kDisplayConfigurationBackendNameBytes = 32u;
static constexpr uint32_t kDisplayConfigurationOutputIdBytes = 32u;
static constexpr uint32_t kDisplayConfigurationDiagnosticBytes = 128u;

enum class DisplayConfigurationCommandType : uint32_t {
    QueryDetectedConfiguration = 1u,
    QueryActiveConfiguration = 2u,
    QueryLastApplyResult = 3u,
    ApplyConfiguration = 4u,
    RestoreLastKnownGood = 5u,
    ForceValidationFrame = 6u
};

enum class DisplayConfigurationMode : uint32_t {
    Mirror = 1u,
    Extend = 2u
};

enum class DisplayConfigurationValidationResult : uint32_t {
    NotRun = 0u,
    Passed = 1u,
    Failed = 2u
};

enum class DisplayConfigurationResultCode : uint32_t {
    Success = 0u,
    InvalidVersion = 1u,
    InvalidSize = 2u,
    InvalidCommand = 3u,
    InvalidConfiguration = 4u,
    BackendUnavailable = 5u,
    BackendBusy = 6u,
    OutputUnavailable = 7u,
    MirrorGeometryIncompatible = 8u,
    PresentationPauseTimeout = 9u,
    TargetRebuildFailed = 10u,
    ValidationFrameFailed = 11u,
    PersistenceFailed = 12u,
    RollbackSucceeded = 13u,
    RollbackFailed = 14u,
    QemuOnlyGateRequired = 15u,
    UnsupportedBackend = 16u
};

enum DisplayConfigurationCommandFlags : uint32_t {
    DisplayConfigurationFlagNone = 0u,
    DisplayConfigurationFlagCommitPersistence = 1u << 0,
    // This bit is accepted only by the explicitly gated QEMU smoke path.
    // Normal builds and non-QEMU backends reject it.
    DisplayConfigurationFlagTestInjectValidationFailure = 1u << 30
};

struct DisplayConfigurationOutput {
    char stableId[kDisplayConfigurationOutputIdBytes];
    int32_t virtualX;
    int32_t virtualY;
    int32_t width;
    int32_t height;
    uint8_t enabled;
    uint8_t primary;
    uint8_t reserved[2];
};

struct DisplayConfigurationSnapshot {
    uint32_t version;
    uint32_t structureSize;
    char backend[kDisplayConfigurationBackendNameBytes];
    uint32_t mode;
    uint32_t outputCount;
    char primaryOutputId[kDisplayConfigurationOutputIdBytes];
    char taskbarMonitorId[kDisplayConfigurationOutputIdBytes];
    int32_t virtualDesktopX;
    int32_t virtualDesktopY;
    int32_t virtualDesktopWidth;
    int32_t virtualDesktopHeight;
    uint8_t presenterActive;
    uint8_t qemuOnly;
    uint8_t reserved[2];
    DisplayConfigurationOutput outputs[kDisplayConfigurationMaxOutputs];
};

struct DisplayConfigurationRequest {
    uint32_t mode;
    uint32_t outputCount;
    char primaryOutputId[kDisplayConfigurationOutputIdBytes];
    DisplayConfigurationOutput outputs[kDisplayConfigurationMaxOutputs];
};

struct DisplayConfigurationCommand {
    uint32_t version;
    uint32_t structureSize;
    uint64_t requestId;
    uint32_t commandType;
    uint32_t flags;
    DisplayConfigurationRequest requestedConfiguration;
};

struct DisplayConfigurationResponse {
    uint32_t version;
    uint32_t structureSize;
    uint64_t requestId;
    uint32_t commandType;
    uint8_t accepted;
    uint8_t completed;
    uint8_t success;
    uint8_t reserved0;
    uint32_t resultCode;
    uint32_t validationResult;
    uint8_t rollbackAttempted;
    uint8_t rollbackSucceeded;
    uint8_t persistenceCommitted;
    uint8_t targetRebuildSucceeded;
    uint8_t presentationPaused;
    uint8_t presentationResumed;
    uint8_t reserved1[3];
    DisplayConfigurationSnapshot detectedConfiguration;
    DisplayConfigurationSnapshot activeConfiguration;
    char diagnostic[kDisplayConfigurationDiagnosticBytes];
};

inline bool displayConfigurationCommandIsMutation(uint32_t commandType)
{
    return commandType == static_cast<uint32_t>(DisplayConfigurationCommandType::ApplyConfiguration)
        || commandType == static_cast<uint32_t>(DisplayConfigurationCommandType::RestoreLastKnownGood)
        || commandType == static_cast<uint32_t>(DisplayConfigurationCommandType::ForceValidationFrame);
}

inline bool displayConfigurationCommandTypeIsValid(uint32_t commandType)
{
    return commandType >= static_cast<uint32_t>(DisplayConfigurationCommandType::QueryDetectedConfiguration)
        && commandType <= static_cast<uint32_t>(DisplayConfigurationCommandType::ForceValidationFrame);
}

} // namespace display
} // namespace gxos
