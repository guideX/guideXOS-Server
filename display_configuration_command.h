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
static constexpr uint32_t kDisplayConfigurationModeIdBytes = 32u;
static constexpr uint32_t kDisplayConfigurationDiagnosticBytes = 128u;
static constexpr uint32_t kDisplayConfigurationPersistenceVersion = 2u;
static constexpr uint32_t kDisplayConfigurationPersistenceMaxBytes = 2048u;
static constexpr uint32_t kDisplayConfigurationPersistenceMaxCoordinate = 16384u;
static constexpr uint32_t kDisplayConfigurationPersistenceMaxDimension = 8192u;

// Backend-neutral identity used by persistence and reconciliation.  The
// stableId is the authoritative logical output identity for this contract.
// Backend/device/scanout fields strengthen matching when the backend exposes
// them; logicalOrdinal is only a bounded fallback for older records.
struct DisplayOutputIdentity {
    uint32_t version;
    char backendType[kDisplayConfigurationBackendNameBytes];
    char backendDeviceId[kDisplayConfigurationOutputIdBytes];
    char stableId[kDisplayConfigurationOutputIdBytes];
    uint32_t scanoutId;
    uint32_t logicalOrdinal;
    char stableName[kDisplayConfigurationOutputIdBytes];
    int32_t expectedWidth;
    int32_t expectedHeight;
};

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

enum class DisplayConfigurationRequestOrigin : uint32_t {
    UserApply = 1u,
    StartupRestore = 2u,
    TestCoordinator = 3u,
    LastKnownGoodRecovery = 4u
};

enum class DisplayConfigurationReconciliationResult : uint32_t {
    NotRun = 0u,
    NoPersistedConfiguration = 1u,
    Success = 2u,
    Partial = 3u,
    Rejected = 4u,
    Fallback = 5u
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
    DisplayConfigurationFlagTestInjectValidationFailure = 1u << 30,
    DisplayConfigurationFlagTestInjectBackingAllocationFailure = 1u << 29,
    DisplayConfigurationFlagTestInjectResourceCreateFailure = 1u << 28,
    DisplayConfigurationFlagTestInjectAttachBackingFailure = 1u << 27,
    DisplayConfigurationFlagTestInjectSetScanoutFailure = 1u << 26,
    DisplayConfigurationFlagTestInjectSecondOutputCommitFailure = 1u << 25,
    DisplayConfigurationFlagTestInjectValidationFrameFailure = 1u << 24
};

static constexpr uint32_t kDisplayConfigurationTestFailureMask =
    DisplayConfigurationFlagTestInjectValidationFailure |
    DisplayConfigurationFlagTestInjectBackingAllocationFailure |
    DisplayConfigurationFlagTestInjectResourceCreateFailure |
    DisplayConfigurationFlagTestInjectAttachBackingFailure |
    DisplayConfigurationFlagTestInjectSetScanoutFailure |
    DisplayConfigurationFlagTestInjectSecondOutputCommitFailure |
    DisplayConfigurationFlagTestInjectValidationFrameFailure;

struct DisplayConfigurationOutput {
    char stableId[kDisplayConfigurationOutputIdBytes];
    char backendType[kDisplayConfigurationBackendNameBytes];
    char backendDeviceId[kDisplayConfigurationOutputIdBytes];
    char modeId[kDisplayConfigurationModeIdBytes];
    uint32_t scanoutId;
    uint32_t logicalOrdinal;
    char stableName[kDisplayConfigurationOutputIdBytes];
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
    uint32_t origin;
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
    uint8_t persistedLoaded;
    uint8_t persistedValidated;
    uint8_t persistedReconciled;
    uint8_t startupRestoreAttempted;
    uint8_t activeApplied;
    uint8_t startupValidationFrame;
    uint8_t fallbackUsed;
    uint8_t outputsDetected;
    uint32_t persistedVersion;
    uint32_t matchedOutputCount;
    uint32_t unmatchedSavedOutputs;
    uint32_t unmatchedDetectedOutputs;
    uint32_t reconciliationResult;
    char fallbackReason[kDisplayConfigurationDiagnosticBytes];
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
