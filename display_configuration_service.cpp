#include "display_configuration_service.h"

#include "compositor.h"
#include "display_configuration.h"
#include "display_options_store.h"
#include "logger.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>
#include <string>

namespace gxos {
namespace display {

namespace {

std::atomic<uint64_t> s_nextRequest{1};
std::atomic<bool> s_busy{false};
std::mutex s_stateMutex;
DisplayConfigurationResponse s_lastResponse{};
DisplayConfigurationResponse s_lastApplyResponse{};
DisplayConfigurationCommand s_lastKnownGoodCommand{};
bool s_haveLastKnownGood = false;

void copyText(char* destination, size_t capacity, const std::string& value)
{
    if (destination == nullptr || capacity == 0) return;
    std::memset(destination, 0, capacity);
    const size_t count = std::min(capacity - 1, value.size());
    if (count != 0) std::memcpy(destination, value.data(), count);
}

std::string boundedText(const char* value, size_t capacity)
{
    if (value == nullptr || capacity == 0) return {};
    size_t length = 0;
    while (length < capacity && value[length] != '\0') ++length;
    return std::string(value, length);
}

uint32_t contractMode(gxos::gui::DisplayModeKind mode)
{
    return mode == gxos::gui::DisplayModeKind::Extend
        ? static_cast<uint32_t>(DisplayConfigurationMode::Extend)
        : static_cast<uint32_t>(DisplayConfigurationMode::Mirror);
}

gxos::gui::DisplayModeKind modelMode(uint32_t mode)
{
    return mode == static_cast<uint32_t>(DisplayConfigurationMode::Extend)
        ? gxos::gui::DisplayModeKind::Extend
        : gxos::gui::DisplayModeKind::Mirror;
}

void fillOutput(DisplayConfigurationOutput& output, const gxos::gui::DisplayMonitorDescriptor& monitor)
{
    output = DisplayConfigurationOutput{};
    copyText(output.stableId, sizeof(output.stableId), monitor.id);
    copyText(output.modeId, sizeof(output.modeId), monitor.modeId);
    output.virtualX = monitor.virtualX;
    output.virtualY = monitor.virtualY;
    output.width = monitor.width;
    output.height = monitor.height;
    output.enabled = monitor.enabled ? 1u : 0u;
    output.primary = monitor.primary ? 1u : 0u;
}

void fillSnapshot(DisplayConfigurationSnapshot& snapshot,
                  const gxos::gui::DetectedDisplayInventory& inventory)
{
    snapshot = DisplayConfigurationSnapshot{};
    snapshot.version = kDisplayConfigurationContractVersion;
    snapshot.structureSize = sizeof(DisplayConfigurationSnapshot);
    copyText(snapshot.backend, sizeof(snapshot.backend), inventory.backend);
    snapshot.mode = contractMode(inventory.currentDesktop.mode);
    snapshot.outputCount = static_cast<uint32_t>(std::min<size_t>(
        kDisplayConfigurationMaxOutputs, inventory.monitors.size()));
    snapshot.virtualDesktopX = inventory.currentDesktop.left;
    snapshot.virtualDesktopY = inventory.currentDesktop.top;
    snapshot.virtualDesktopWidth = inventory.currentDesktop.width();
    snapshot.virtualDesktopHeight = inventory.currentDesktop.height();
    snapshot.qemuOnly = inventory.qemuOnly ? 1u : 0u;
    snapshot.presenterActive = inventory.presentationConfirmedCount() > 0 ? 1u : 0u;
    for (uint32_t i = 0; i < snapshot.outputCount; ++i) {
        fillOutput(snapshot.outputs[i], inventory.monitors[i]);
        if (inventory.monitors[i].primary) {
            copyText(snapshot.primaryOutputId, sizeof(snapshot.primaryOutputId), inventory.monitors[i].id);
        }
    }
    if (snapshot.primaryOutputId[0] == '\0' && snapshot.outputCount > 0) {
        copyText(snapshot.primaryOutputId, sizeof(snapshot.primaryOutputId), inventory.monitors[0].id);
    }
    copyText(snapshot.taskbarMonitorId, sizeof(snapshot.taskbarMonitorId),
             snapshot.primaryOutputId[0] == '\0' ? std::string("display-1") : boundedText(snapshot.primaryOutputId, sizeof(snapshot.primaryOutputId)));
}

void fillSnapshot(DisplayConfigurationSnapshot& snapshot,
                  const gxos::gui::ActiveDisplayConfiguration& active)
{
    snapshot = DisplayConfigurationSnapshot{};
    snapshot.version = kDisplayConfigurationContractVersion;
    snapshot.structureSize = sizeof(DisplayConfigurationSnapshot);
    copyText(snapshot.backend, sizeof(snapshot.backend), active.backend);
    snapshot.mode = contractMode(active.mode);
    snapshot.outputCount = static_cast<uint32_t>(std::min<size_t>(
        kDisplayConfigurationMaxOutputs, active.monitors.size()));
    snapshot.primaryOutputId[0] = '\0';
    copyText(snapshot.primaryOutputId, sizeof(snapshot.primaryOutputId), active.primaryOutputId);
    copyText(snapshot.taskbarMonitorId, sizeof(snapshot.taskbarMonitorId), active.taskbarMonitorId);
    snapshot.virtualDesktopX = active.virtualDesktop.left;
    snapshot.virtualDesktopY = active.virtualDesktop.top;
    snapshot.virtualDesktopWidth = active.virtualDesktop.width();
    snapshot.virtualDesktopHeight = active.virtualDesktop.height();
    snapshot.presenterActive = active.renderTargets.empty() ? 0u : 1u;
    snapshot.qemuOnly = active.backend == "virtio-gpu" ? 1u : 0u;
    for (uint32_t i = 0; i < snapshot.outputCount; ++i) {
        fillOutput(snapshot.outputs[i], active.monitors[i]);
    }
    if (snapshot.taskbarMonitorId[0] == '\0') {
        copyText(snapshot.taskbarMonitorId, sizeof(snapshot.taskbarMonitorId), active.primaryOutputId);
    }
}

gxos::gui::RequestedDisplayConfiguration requestedFromContract(
    const DisplayConfigurationRequest& request)
{
    gxos::gui::RequestedDisplayConfiguration result;
    result.mode = modelMode(request.mode);
    result.primaryOutputId = boundedText(request.primaryOutputId, sizeof(request.primaryOutputId));
    if (result.primaryOutputId.empty()) result.primaryOutputId = "display-1";
    const uint32_t outputCount = std::min(request.outputCount, kDisplayConfigurationMaxOutputs);
    for (uint32_t i = 0; i < outputCount; ++i) {
        const DisplayConfigurationOutput& source = request.outputs[i];
        gxos::gui::DisplayMonitorDescriptor monitor;
        monitor.id = boundedText(source.stableId, sizeof(source.stableId));
        if (monitor.id.empty()) monitor.id = std::string("display-") + std::to_string(i + 1u);
        monitor.name = monitor.id;
        monitor.modeId = boundedText(source.modeId, sizeof(source.modeId));
        monitor.virtualX = source.virtualX;
        monitor.virtualY = source.virtualY;
        monitor.width = source.width;
        monitor.height = source.height;
        monitor.enabled = source.enabled != 0;
        monitor.primary = source.primary != 0;
        monitor.assignedX = source.virtualX;
        monitor.assignedY = source.virtualY;
        monitor.assignedWidth = source.width;
        monitor.assignedHeight = source.height;
        result.arrangement.push_back(monitor);
        if (monitor.enabled) result.enabledOutputIds.push_back(monitor.id);
    }
    return result;
}

void requestedFromSnapshot(const DisplayConfigurationSnapshot& snapshot,
                           DisplayConfigurationRequest& request)
{
    request = DisplayConfigurationRequest{};
    request.mode = snapshot.mode;
    request.outputCount = std::min(snapshot.outputCount, kDisplayConfigurationMaxOutputs);
    std::memcpy(request.primaryOutputId, snapshot.primaryOutputId, sizeof(request.primaryOutputId));
    for (uint32_t i = 0; i < request.outputCount; ++i) {
        request.outputs[i] = snapshot.outputs[i];
    }
}

uint32_t resultCodeFor(const gxos::gui::DisplayApplyResult& result)
{
    if (result.success) return static_cast<uint32_t>(DisplayConfigurationResultCode::Success);
    if (result.reason.find("busy") != std::string::npos || result.reason.find("Backend busy") != std::string::npos) {
        return static_cast<uint32_t>(DisplayConfigurationResultCode::BackendBusy);
    }
    if (result.reason.find("Mirror dimensions") != std::string::npos) {
        return static_cast<uint32_t>(DisplayConfigurationResultCode::MirrorGeometryIncompatible);
    }
    if (result.reason.find("output unavailable") != std::string::npos ||
        result.reason.find("primary output") != std::string::npos) {
        return static_cast<uint32_t>(DisplayConfigurationResultCode::OutputUnavailable);
    }
    if (result.reason.find("persistence") != std::string::npos) {
        return static_cast<uint32_t>(DisplayConfigurationResultCode::PersistenceFailed);
    }
    if (result.reason.find("validation") != std::string::npos ||
        result.reason.find("injected") != std::string::npos) {
        return static_cast<uint32_t>(DisplayConfigurationResultCode::ValidationFrameFailed);
    }
    return static_cast<uint32_t>(DisplayConfigurationResultCode::InvalidConfiguration);
}

void prepareResponse(const DisplayConfigurationCommand& command,
                     DisplayConfigurationResponse& response)
{
    response = DisplayConfigurationResponse{};
    response.version = kDisplayConfigurationContractVersion;
    response.structureSize = sizeof(DisplayConfigurationResponse);
    response.requestId = command.requestId;
    response.commandType = command.commandType;
    response.validationResult = static_cast<uint32_t>(DisplayConfigurationValidationResult::NotRun);
    response.presentationResumed = 1u;
}

void completeResponse(DisplayConfigurationResponse& response,
                      const gxos::gui::DetectedDisplayInventory& detected,
                      const gxos::gui::ActiveDisplayConfiguration& active)
{
    fillSnapshot(response.detectedConfiguration, detected);
    fillSnapshot(response.activeConfiguration, active);
    response.completed = 1u;
    if (response.success) {
        response.validationResult = static_cast<uint32_t>(DisplayConfigurationValidationResult::Passed);
    }
}

void logResponse(const DisplayConfigurationResponse& response)
{
    Logger::write(LogLevel::Info,
        std::string("Display config response: request=") + std::to_string(response.requestId) +
        " success=" + (response.success ? "yes" : "no") +
        " activeMode=" + (response.activeConfiguration.mode == static_cast<uint32_t>(DisplayConfigurationMode::Extend) ? "Extend" : "Mirror") +
        " logicalDesktop=" + std::to_string(response.activeConfiguration.virtualDesktopWidth) + "x" + std::to_string(response.activeConfiguration.virtualDesktopHeight) +
        " persisted=" + (response.persistenceCommitted ? "yes" : "no") +
        " rollback=" + (response.rollbackAttempted ? "yes" : "no"));
}

} // namespace

uint64_t DisplayConfigurationService::nextRequestId()
{
    return s_nextRequest.fetch_add(1, std::memory_order_relaxed);
}

bool DisplayConfigurationService::submit(const DisplayConfigurationCommand& command,
                                         DisplayConfigurationResponse& response)
{
    prepareResponse(command, response);
    if (command.version != kDisplayConfigurationContractVersion) {
        response.accepted = 0u;
        response.completed = 1u;
        response.resultCode = static_cast<uint32_t>(DisplayConfigurationResultCode::InvalidVersion);
        std::strncpy(response.diagnostic, "display configuration version mismatch", sizeof(response.diagnostic) - 1);
        return false;
    }
    if (command.structureSize < sizeof(DisplayConfigurationCommand)) {
        response.accepted = 0u;
        response.completed = 1u;
        response.resultCode = static_cast<uint32_t>(DisplayConfigurationResultCode::InvalidSize);
        std::strncpy(response.diagnostic, "display configuration command size is too small", sizeof(response.diagnostic) - 1);
        return false;
    }
    if (command.requestId == 0 || !displayConfigurationCommandTypeIsValid(command.commandType)) {
        response.accepted = 0u;
        response.completed = 1u;
        response.resultCode = command.requestId == 0
            ? static_cast<uint32_t>(DisplayConfigurationResultCode::InvalidCommand)
            : static_cast<uint32_t>(DisplayConfigurationResultCode::InvalidCommand);
        std::strncpy(response.diagnostic, "display configuration command is invalid", sizeof(response.diagnostic) - 1);
        return false;
    }
    if (s_busy.exchange(true, std::memory_order_acq_rel)) {
        response.accepted = 0u;
        response.completed = 1u;
        response.resultCode = static_cast<uint32_t>(DisplayConfigurationResultCode::BackendBusy);
        std::strncpy(response.diagnostic, "display configuration service is busy", sizeof(response.diagnostic) - 1);
        return false;
    }

    bool accepted = true;
    try {
        const gxos::gui::DetectedDisplayInventory detected = gxos::gui::Compositor::detectedDisplayInventory();
        const gxos::gui::ActiveDisplayConfiguration activeBefore = gxos::gui::Compositor::activeDisplayConfiguration();
        response.accepted = 1u;
        Logger::write(LogLevel::Info,
            std::string("Display config command: request=") + std::to_string(command.requestId) +
            " type=" + std::to_string(command.commandType) +
            " accepted=yes");

        const auto commandType = static_cast<DisplayConfigurationCommandType>(command.commandType);
        if (commandType == DisplayConfigurationCommandType::QueryDetectedConfiguration) {
            response.success = detected.hasOperationalOutputs() ? 1u : 0u;
            response.resultCode = response.success
                ? static_cast<uint32_t>(DisplayConfigurationResultCode::Success)
                : static_cast<uint32_t>(DisplayConfigurationResultCode::BackendUnavailable);
            std::strncpy(response.diagnostic, response.success ? "detected configuration query complete" : "no operational display backend", sizeof(response.diagnostic) - 1);
        } else if (commandType == DisplayConfigurationCommandType::QueryActiveConfiguration) {
            response.success = activeBefore.valid() ? 1u : 0u;
            response.resultCode = response.success
                ? static_cast<uint32_t>(DisplayConfigurationResultCode::Success)
                : static_cast<uint32_t>(DisplayConfigurationResultCode::BackendUnavailable);
            std::strncpy(response.diagnostic, response.success ? "active configuration query complete" : "active configuration unavailable", sizeof(response.diagnostic) - 1);
        } else if (commandType == DisplayConfigurationCommandType::QueryLastApplyResult) {
            std::lock_guard<std::mutex> stateLock(s_stateMutex);
            const DisplayConfigurationResponse previous = s_lastApplyResponse;
            response = previous;
            response.version = kDisplayConfigurationContractVersion;
            response.structureSize = sizeof(DisplayConfigurationResponse);
            response.requestId = command.requestId;
            response.commandType = command.commandType;
            response.accepted = 1u;
            response.completed = 1u;
            accepted = previous.completed != 0u;
        } else if (commandType == DisplayConfigurationCommandType::ForceValidationFrame) {
            response.success = gxos::gui::Compositor::forceDisplayValidationFrame() ? 1u : 0u;
            response.validationResult = response.success
                ? static_cast<uint32_t>(DisplayConfigurationValidationResult::Passed)
                : static_cast<uint32_t>(DisplayConfigurationValidationResult::Failed);
            response.resultCode = response.success
                ? static_cast<uint32_t>(DisplayConfigurationResultCode::Success)
                : static_cast<uint32_t>(DisplayConfigurationResultCode::ValidationFrameFailed);
            std::strncpy(response.diagnostic, response.success ? "validation frame complete" : "validation frame failed", sizeof(response.diagnostic) - 1);
        } else {
            DisplayConfigurationCommand applyCommand = command;
            if (commandType == DisplayConfigurationCommandType::RestoreLastKnownGood) {
                std::lock_guard<std::mutex> stateLock(s_stateMutex);
                if (!s_haveLastKnownGood) {
                    response.resultCode = static_cast<uint32_t>(DisplayConfigurationResultCode::InvalidConfiguration);
                    std::strncpy(response.diagnostic, "no last-known-good display configuration", sizeof(response.diagnostic) - 1);
                    response.completed = 1u;
                    accepted = false;
                } else {
                    applyCommand.requestedConfiguration = s_lastKnownGoodCommand.requestedConfiguration;
                }
            }
            if (accepted) {
                const bool requestedTestFailure =
                    (applyCommand.flags & DisplayConfigurationFlagTestInjectValidationFailure) != 0u;
                const bool qemuGate = detected.backend == "virtio-gpu" && detected.qemuOnly && detected.backendGateActive;
                if (requestedTestFailure && !qemuGate) {
                    response.resultCode = qemuGate
                        ? static_cast<uint32_t>(DisplayConfigurationResultCode::InvalidCommand)
                        : static_cast<uint32_t>(DisplayConfigurationResultCode::QemuOnlyGateRequired);
                    std::strncpy(response.diagnostic, "test failure injection requires the QEMU-only backend gate", sizeof(response.diagnostic) - 1);
                    response.completed = 1u;
                    accepted = false;
                } else {
                    if (requestedTestFailure) gxos::gui::Compositor::injectDisplayConfigurationValidationFailureOnce();
                    const gxos::gui::RequestedDisplayConfiguration requested = requestedFromContract(applyCommand.requestedConfiguration);
                    Logger::write(LogLevel::Info, std::string("Display config service: request=") + std::to_string(command.requestId) + " state=pausing-presentation");
                    const gxos::gui::DisplayApplyResult result = gxos::gui::Compositor::applyDisplayConfiguration(
                        requested,
                        (applyCommand.flags & DisplayConfigurationFlagCommitPersistence) != 0u);
                    response.validationResult = result.validationFrameResult
                        ? static_cast<uint32_t>(DisplayConfigurationValidationResult::Passed)
                        : static_cast<uint32_t>(DisplayConfigurationValidationResult::Failed);
                    response.rollbackAttempted = result.rollbackAttempted ? 1u : 0u;
                    response.rollbackSucceeded = result.rollbackSucceeded ? 1u : 0u;
                    response.persistenceCommitted = result.persistenceCommitted ? 1u : 0u;
                    response.targetRebuildSucceeded = result.targetsRebuilt ? 1u : 0u;
                    response.presentationPaused = result.presentationPaused ? 1u : 0u;
                    response.presentationResumed = result.presentationPaused ? 1u : 0u;
                    response.success = result.success ? 1u : 0u;
                    response.resultCode = resultCodeFor(result);
                    if (result.rollbackSucceeded && !result.success) {
                        response.resultCode = static_cast<uint32_t>(DisplayConfigurationResultCode::RollbackSucceeded);
                    }
                    std::strncpy(response.diagnostic,
                        result.reason.empty() ? result.summary().c_str() : result.reason.c_str(),
                        sizeof(response.diagnostic) - 1);
                    if (result.success) {
                        std::lock_guard<std::mutex> stateLock(s_stateMutex);
                        s_lastKnownGoodCommand = applyCommand;
                        s_lastKnownGoodCommand.commandType = static_cast<uint32_t>(DisplayConfigurationCommandType::ApplyConfiguration);
                        s_haveLastKnownGood = true;
                    }
                }
            }
        }

        const gxos::gui::DetectedDisplayInventory detectedAfter = gxos::gui::Compositor::detectedDisplayInventory();
        const gxos::gui::ActiveDisplayConfiguration activeAfter = gxos::gui::Compositor::activeDisplayConfiguration();
        completeResponse(response, detectedAfter, activeAfter);
        if (response.diagnostic[0] == '\0') {
            std::strncpy(response.diagnostic, response.success ? "display configuration complete" : "display configuration failed", sizeof(response.diagnostic) - 1);
        }
        if (commandType == DisplayConfigurationCommandType::QueryActiveConfiguration) {
            Logger::write(LogLevel::Info,
                std::string("Display configuration bridge: request=") + std::to_string(command.requestId) +
                " query=active result=" + (response.success ? "success" : "failed") +
                " backend=" + boundedText(response.activeConfiguration.backend, sizeof(response.activeConfiguration.backend)) +
                " mode=" + (response.activeConfiguration.mode == static_cast<uint32_t>(DisplayConfigurationMode::Extend) ? "Extend" : "Mirror") +
                " primary=" + boundedText(response.activeConfiguration.primaryOutputId, sizeof(response.activeConfiguration.primaryOutputId)) +
                " outputs=" + std::to_string(response.activeConfiguration.outputCount) +
                " virtualDesktop=" + std::to_string(response.activeConfiguration.virtualDesktopWidth) + "x" + std::to_string(response.activeConfiguration.virtualDesktopHeight));
        }
        if (commandType == DisplayConfigurationCommandType::ApplyConfiguration ||
            commandType == DisplayConfigurationCommandType::RestoreLastKnownGood) {
            std::lock_guard<std::mutex> stateLock(s_stateMutex);
            s_lastApplyResponse = response;
        }
        {
            std::lock_guard<std::mutex> stateLock(s_stateMutex);
            s_lastResponse = response;
        }
        logResponse(response);
    } catch (...) {
        response.accepted = 1u;
        response.completed = 1u;
        response.success = 0u;
        response.resultCode = static_cast<uint32_t>(DisplayConfigurationResultCode::TargetRebuildFailed);
        std::strncpy(response.diagnostic, "display configuration service exception", sizeof(response.diagnostic) - 1);
        accepted = false;
    }
    s_busy.store(false, std::memory_order_release);
    return accepted && response.success != 0u;
}

void DisplayConfigurationService::processPendingAtSafePoint()
{
    // Current hosted callbacks are already dispatched on the desktop owner
    // path.  Keep this explicit hook so a future process adapter can enqueue
    // the same fixed contract without adding a second transaction owner.
}

void DisplayConfigurationService::requestStartupRestore()
{
    // Hosted startup already loads Display Options before the compositor
    // owner path becomes active. Keep the public hook explicit so QEMU and
    // hosted callers share the same service surface without a second restore
    // transaction path.
}

bool DisplayConfigurationService::startupRestoreComplete()
{
    return true;
}

DisplayConfigurationResponse DisplayConfigurationService::lastResult()
{
    std::lock_guard<std::mutex> stateLock(s_stateMutex);
    return s_lastApplyResponse;
}

bool DisplayConfigurationService::isBusy()
{
    return s_busy.load(std::memory_order_acquire);
}

const char* DisplayConfigurationService::resultCodeName(uint32_t resultCode)
{
    switch (static_cast<DisplayConfigurationResultCode>(resultCode)) {
    case DisplayConfigurationResultCode::Success: return "Success";
    case DisplayConfigurationResultCode::InvalidVersion: return "InvalidVersion";
    case DisplayConfigurationResultCode::InvalidSize: return "InvalidSize";
    case DisplayConfigurationResultCode::InvalidCommand: return "InvalidCommand";
    case DisplayConfigurationResultCode::InvalidConfiguration: return "InvalidConfiguration";
    case DisplayConfigurationResultCode::BackendUnavailable: return "BackendUnavailable";
    case DisplayConfigurationResultCode::BackendBusy: return "BackendBusy";
    case DisplayConfigurationResultCode::OutputUnavailable: return "OutputUnavailable";
    case DisplayConfigurationResultCode::MirrorGeometryIncompatible: return "MirrorGeometryIncompatible";
    case DisplayConfigurationResultCode::PresentationPauseTimeout: return "PresentationPauseTimeout";
    case DisplayConfigurationResultCode::TargetRebuildFailed: return "TargetRebuildFailed";
    case DisplayConfigurationResultCode::ValidationFrameFailed: return "ValidationFrameFailed";
    case DisplayConfigurationResultCode::PersistenceFailed: return "PersistenceFailed";
    case DisplayConfigurationResultCode::RollbackSucceeded: return "RollbackSucceeded";
    case DisplayConfigurationResultCode::RollbackFailed: return "RollbackFailed";
    case DisplayConfigurationResultCode::QemuOnlyGateRequired: return "QemuOnlyGateRequired";
    case DisplayConfigurationResultCode::UnsupportedBackend: return "UnsupportedBackend";
    default: return "Unknown";
    }
}

} // namespace display
} // namespace gxos
