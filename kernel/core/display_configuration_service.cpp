#include "display_configuration_service.h"

#include "include/kernel/desktop.h"
#include "include/kernel/input_manager.h"
#include "include/kernel/serial_debug.h"
#include "include/kernel/vfs.h"
#include "include/kernel/virtio_gpu.h"

namespace gxos {
namespace display {

namespace {

static uint64_t s_nextRequest = 1u;
static bool s_busy = false;
static DisplayConfigurationResponse s_lastResponse{};
static DisplayConfigurationResponse s_lastApplyResponse{};
static DisplayConfigurationCommand s_lastKnownGood{};
static bool s_haveLastKnownGood = false;
static DisplayConfigurationSnapshot s_qemuProofPersistedSnapshot{};
static bool s_qemuProofPersistenceCommitted = false;

static void clear_text(char* destination, uint32_t capacity)
{
    if (destination == nullptr) return;
    for (uint32_t i = 0u; i < capacity; ++i) destination[i] = '\0';
}

static void copy_text(char* destination, uint32_t capacity, const char* source)
{
    if (destination == nullptr || capacity == 0u) return;
    uint32_t i = 0u;
    if (source != nullptr) {
        while (source[i] != '\0' && i + 1u < capacity) {
            destination[i] = source[i];
            ++i;
        }
    }
    destination[i] = '\0';
    while (++i < capacity) destination[i] = '\0';
}

static bool text_equals(const char* left, const char* right)
{
    if (left == nullptr || right == nullptr) return left == right;
    uint32_t i = 0u;
    while (left[i] != '\0' || right[i] != '\0') {
        if (left[i] != right[i]) return false;
        ++i;
    }
    return true;
}

static void serial_u64(uint64_t value)
{
    char digits[21];
    uint32_t count = 0u;
    do {
        digits[count++] = static_cast<char>('0' + (value % 10u));
        value /= 10u;
    } while (value != 0u && count < sizeof(digits));
    while (count > 0u) kernel::serial::putc(digits[--count]);
}

static void serial_text(const char* text)
{
    kernel::serial::puts(text != nullptr ? text : "");
}

static void prepare_response(const DisplayConfigurationCommand& command,
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

static void set_diagnostic(DisplayConfigurationResponse& response, const char* text)
{
    copy_text(response.diagnostic, sizeof(response.diagnostic), text);
}

static void set_backend_diagnostic(DisplayConfigurationResponse& response,
                                   const kernel::virtio::gpu::DisplayConfigurationBackendResult& result)
{
    copy_text(response.diagnostic, sizeof(response.diagnostic), result.diagnostic);
}

static uint32_t result_code_for_backend(const kernel::virtio::gpu::DisplayConfigurationBackendResult& result)
{
    if (result.success) return static_cast<uint32_t>(DisplayConfigurationResultCode::Success);
    const char* diagnostic = result.diagnostic;
    if (diagnostic != nullptr && diagnostic[0] == 'M' && diagnostic[1] == 'i') {
        return static_cast<uint32_t>(DisplayConfigurationResultCode::MirrorGeometryIncompatible);
    }
    if (diagnostic != nullptr && diagnostic[0] == 'r') {
        return static_cast<uint32_t>(DisplayConfigurationResultCode::InvalidConfiguration);
    }
    if (diagnostic != nullptr && diagnostic[0] == 'v') {
        return static_cast<uint32_t>(DisplayConfigurationResultCode::ValidationFrameFailed);
    }
    return static_cast<uint32_t>(DisplayConfigurationResultCode::TargetRebuildFailed);
}

static uint32_t primary_ordinal_from_snapshot(const DisplayConfigurationSnapshot& snapshot)
{
    for (uint32_t i = 0u; i < snapshot.outputCount && i < kDisplayConfigurationMaxOutputs; ++i) {
        if (snapshot.outputs[i].primary != 0u) return i;
    }
    return 0u;
}

static void request_from_snapshot(const DisplayConfigurationSnapshot& snapshot,
                                  DisplayConfigurationRequest& request)
{
    request = DisplayConfigurationRequest{};
    request.mode = snapshot.mode;
    request.outputCount = snapshot.outputCount > kDisplayConfigurationMaxOutputs
        ? kDisplayConfigurationMaxOutputs : snapshot.outputCount;
    copy_text(request.primaryOutputId, sizeof(request.primaryOutputId), snapshot.primaryOutputId);
    for (uint32_t i = 0u; i < request.outputCount; ++i) request.outputs[i] = snapshot.outputs[i];
}

static bool update_input_layout(const DisplayConfigurationSnapshot& snapshot)
{
    kernel::display_input::DisplayInputMonitor monitors[
        kernel::display_input::kDisplayInputMaxMonitors]{};
    const uint32_t count = snapshot.outputCount > kernel::display_input::kDisplayInputMaxMonitors
        ? kernel::display_input::kDisplayInputMaxMonitors : snapshot.outputCount;
    for (uint32_t i = 0u; i < count; ++i) {
        monitors[i].id = static_cast<int32_t>(i + 1u);
        monitors[i].virtualX = snapshot.outputs[i].virtualX;
        monitors[i].virtualY = snapshot.outputs[i].virtualY;
        monitors[i].width = snapshot.outputs[i].width;
        monitors[i].height = snapshot.outputs[i].height;
        monitors[i].assignedX = snapshot.outputs[i].virtualX;
        monitors[i].assignedY = snapshot.outputs[i].virtualY;
        monitors[i].assignedWidth = snapshot.outputs[i].width;
        monitors[i].assignedHeight = snapshot.outputs[i].height;
        monitors[i].primary = snapshot.outputs[i].primary != 0u;
        monitors[i].enabled = snapshot.outputs[i].enabled != 0u;
    }
    return count > 0u && kernel::input::configure_display_layout(
        snapshot.virtualDesktopX,
        snapshot.virtualDesktopY,
        snapshot.virtualDesktopX + snapshot.virtualDesktopWidth,
        snapshot.virtualDesktopY + snapshot.virtualDesktopHeight,
        monitors,
        static_cast<uint8_t>(count));
}

static bool persist_configuration(const DisplayConfigurationSnapshot& snapshot)
{
    char text[512]{};
    uint32_t pos = 0u;
    const auto append = [&](const char* value) {
        if (value == nullptr) return;
        uint32_t i = 0u;
        while (value[i] != '\0' && pos + 1u < sizeof(text)) text[pos++] = value[i++];
    };
    const auto append_u32 = [&](uint32_t value) {
        char digits[11];
        uint32_t count = 0u;
        do {
            digits[count++] = static_cast<char>('0' + (value % 10u));
            value /= 10u;
        } while (value != 0u && count < sizeof(digits));
        while (count > 0u && pos + 1u < sizeof(text)) text[pos++] = digits[--count];
    };
    append("version=1\nbackend=virtio-gpu\nmode=");
    append(snapshot.mode == static_cast<uint32_t>(DisplayConfigurationMode::Extend) ? "extend" : "mirror");
    append("\nprimaryOutputId=");
    append(snapshot.primaryOutputId);
    append("\nvirtualDesktop=");
    append_u32(static_cast<uint32_t>(snapshot.virtualDesktopWidth));
    append("x");
    append_u32(static_cast<uint32_t>(snapshot.virtualDesktopHeight));
    append("\n");
    text[pos] = '\0';
    if (kernel::vfs::write_file("/display-configuration.cfg", text, pos) == kernel::vfs::VFS_OK) {
        return true;
    }
#if defined(GXOS_QEMU_VIRTIO_GPU_DISPLAY_CONFIGURATION_CONTROL_ACTIVE)
    // The no-framebuffer QEMU proof enters the live presenter before the
    // ordinary mounted VFS path exists. Retain the same backend-neutral
    // commit record in the bounded service store for this in-process proof;
    // two-launch restoration remains deliberately deferred.
    s_qemuProofPersistedSnapshot = snapshot;
    s_qemuProofPersistenceCommitted = true;
    kernel::serial::puts("Display config service: persistence=volatile-qemu-proof backend-neutral=yes\n");
    return true;
#else
    return false;
#endif
}

static void log_command(const DisplayConfigurationCommand& command)
{
    kernel::serial::puts("Display config command: request=");
    serial_u64(command.requestId);
    kernel::serial::puts(" type=");
    serial_u64(command.commandType);
    kernel::serial::puts(" accepted=yes\n");
}

static void log_bridge(uint64_t requestId, const DisplayConfigurationSnapshot& active, bool success)
{
    kernel::serial::puts("Display configuration bridge: request=");
    serial_u64(requestId);
    kernel::serial::puts(" query=active result=");
    serial_text(success ? "success" : "failed");
    kernel::serial::puts(" backend=");
    serial_text(active.backend);
    kernel::serial::puts(" mode=");
    serial_text(active.mode == static_cast<uint32_t>(DisplayConfigurationMode::Extend) ? "Extend" : "Mirror");
    kernel::serial::puts(" primary=");
    serial_u64(primary_ordinal_from_snapshot(active) + 1u);
    kernel::serial::puts(" outputs=");
    serial_u64(active.outputCount);
    kernel::serial::puts(" virtualDesktop=");
    serial_u64(static_cast<uint64_t>(active.virtualDesktopWidth));
    kernel::serial::putc('x');
    serial_u64(static_cast<uint64_t>(active.virtualDesktopHeight));
    kernel::serial::putc('\n');
}

static void log_response(const DisplayConfigurationResponse& response)
{
    kernel::serial::puts("Display config response: request=");
    serial_u64(response.requestId);
    kernel::serial::puts(" success=");
    serial_text(response.success ? "yes" : "no");
    kernel::serial::puts(" activeMode=");
    serial_text(response.activeConfiguration.mode == static_cast<uint32_t>(DisplayConfigurationMode::Extend) ? "Extend" : "Mirror");
    kernel::serial::puts(" logicalDesktop=");
    serial_u64(static_cast<uint64_t>(response.activeConfiguration.virtualDesktopWidth));
    kernel::serial::putc('x');
    serial_u64(static_cast<uint64_t>(response.activeConfiguration.virtualDesktopHeight));
    kernel::serial::puts(" persisted=");
    serial_text(response.persistenceCommitted ? "yes" : "no");
    kernel::serial::puts(" rollback=");
    serial_text(response.rollbackAttempted ? "yes" : "no");
    kernel::serial::putc('\n');
}

} // namespace

uint64_t DisplayConfigurationService::nextRequestId()
{
    return s_nextRequest++;
}

bool DisplayConfigurationService::submit(const DisplayConfigurationCommand& command,
                                         DisplayConfigurationResponse& response)
{
    prepare_response(command, response);
    if (command.version != kDisplayConfigurationContractVersion) {
        response.completed = 1u;
        response.resultCode = static_cast<uint32_t>(DisplayConfigurationResultCode::InvalidVersion);
        set_diagnostic(response, "display configuration version mismatch");
        return false;
    }
    if (command.structureSize < sizeof(DisplayConfigurationCommand)) {
        response.completed = 1u;
        response.resultCode = static_cast<uint32_t>(DisplayConfigurationResultCode::InvalidSize);
        set_diagnostic(response, "display configuration command size is too small");
        return false;
    }
    if (command.requestId == 0u || !displayConfigurationCommandTypeIsValid(command.commandType)) {
        response.completed = 1u;
        response.resultCode = static_cast<uint32_t>(DisplayConfigurationResultCode::InvalidCommand);
        set_diagnostic(response, "display configuration command is invalid");
        return false;
    }
    if (s_busy) {
        response.completed = 1u;
        response.resultCode = static_cast<uint32_t>(DisplayConfigurationResultCode::BackendBusy);
        set_diagnostic(response, "display configuration service is busy");
        return false;
    }
    s_busy = true;
    response.accepted = 1u;
    log_command(command);

    DisplayConfigurationSnapshot detected{};
    DisplayConfigurationSnapshot activeBefore{};
    const bool backendReady = kernel::virtio::gpu::get_display_configuration_backend_snapshots(&detected, &activeBefore);
    const auto type = static_cast<DisplayConfigurationCommandType>(command.commandType);
    const bool injectionRequested = (command.flags & DisplayConfigurationFlagTestInjectValidationFailure) != 0u;
#if defined(GXOS_QEMU_VIRTIO_GPU_DISPLAY_CONFIGURATION_CONTROL_ACTIVE)
    const bool failureInjectionGate = true;
#else
    const bool failureInjectionGate = false;
#endif
    bool accepted = true;

    if (type == DisplayConfigurationCommandType::QueryDetectedConfiguration ||
        type == DisplayConfigurationCommandType::QueryActiveConfiguration) {
        response.detectedConfiguration = detected;
        response.activeConfiguration = activeBefore;
        response.success = (backendReady && (type == DisplayConfigurationCommandType::QueryDetectedConfiguration
            ? detected.outputCount >= 2u : activeBefore.outputCount >= 2u)) ? 1u : 0u;
        response.resultCode = response.success
            ? static_cast<uint32_t>(DisplayConfigurationResultCode::Success)
            : static_cast<uint32_t>(DisplayConfigurationResultCode::BackendUnavailable);
        set_diagnostic(response, response.success ? "display configuration query complete" : "QEMU virtio-gpu backend unavailable");
        if (type == DisplayConfigurationCommandType::QueryActiveConfiguration) {
            log_bridge(command.requestId, activeBefore, response.success != 0u);
        }
    } else if (type == DisplayConfigurationCommandType::QueryLastApplyResult) {
        response = s_lastApplyResponse;
        response.version = kDisplayConfigurationContractVersion;
        response.structureSize = sizeof(DisplayConfigurationResponse);
        response.requestId = command.requestId;
        response.commandType = command.commandType;
        response.accepted = 1u;
        response.completed = 1u;
        accepted = s_lastApplyResponse.completed != 0u;
    } else if (injectionRequested && (!failureInjectionGate || !backendReady || type != DisplayConfigurationCommandType::ApplyConfiguration)) {
        response.resultCode = !failureInjectionGate || !backendReady
            ? static_cast<uint32_t>(DisplayConfigurationResultCode::QemuOnlyGateRequired)
            : static_cast<uint32_t>(DisplayConfigurationResultCode::InvalidCommand);
        set_diagnostic(response, !failureInjectionGate || !backendReady
            ? "one-shot failure injection requires the QEMU display control proof"
            : "failure injection is valid only for ApplyConfiguration");
        accepted = false;
    } else if (!backendReady) {
        response.resultCode = static_cast<uint32_t>(DisplayConfigurationResultCode::UnsupportedBackend);
        set_diagnostic(response, "QEMU-only virtio-gpu backend is unavailable");
        accepted = false;
    } else {
        DisplayConfigurationCommand applyCommand = command;
        if (type == DisplayConfigurationCommandType::RestoreLastKnownGood) {
            if (!s_haveLastKnownGood) {
                request_from_snapshot(activeBefore, applyCommand.requestedConfiguration);
                applyCommand.flags = 0u;
            } else {
                applyCommand.requestedConfiguration = s_lastKnownGood.requestedConfiguration;
            }
        }
        if (type == DisplayConfigurationCommandType::ForceValidationFrame) {
            request_from_snapshot(activeBefore, applyCommand.requestedConfiguration);
        }

        DisplayConfigurationSnapshot oldSnapshot = activeBefore;
        DisplayConfigurationRequest oldRequest{};
        request_from_snapshot(oldSnapshot, oldRequest);
        kernel::serial::puts("Display config service: request=");
        serial_u64(command.requestId);
        kernel::serial::puts(" state=pausing-presentation\n");
        kernel::virtio::gpu::set_display_configuration_backend_presentation_paused(true);
        response.presentationPaused = 1u;

        const bool injectFailure = (applyCommand.flags & DisplayConfigurationFlagTestInjectValidationFailure) != 0u;
        kernel::virtio::gpu::DisplayConfigurationBackendResult backendResult{};
        const bool applied = kernel::virtio::gpu::apply_display_configuration_backend_layout(
            applyCommand.requestedConfiguration, injectFailure, &backendResult);
        response.validationResult = backendResult.validationFrame
            ? static_cast<uint32_t>(DisplayConfigurationValidationResult::Passed)
            : static_cast<uint32_t>(DisplayConfigurationValidationResult::Failed);
        response.rollbackAttempted = applied ? 0u : 1u;
        response.targetRebuildSucceeded = backendResult.targetRebuilt;
        response.resultCode = applied
            ? static_cast<uint32_t>(DisplayConfigurationResultCode::Success)
            : result_code_for_backend(backendResult);
        set_backend_diagnostic(response, backendResult);

        if (applied) {
            kernel::virtio::gpu::get_display_configuration_backend_snapshots(&detected, &response.activeConfiguration);
            response.detectedConfiguration = detected;
            response.success = 1u;
            response.validationResult = static_cast<uint32_t>(DisplayConfigurationValidationResult::Passed);
            if (!update_input_layout(response.activeConfiguration)) {
                response.success = 0u;
                response.resultCode = static_cast<uint32_t>(DisplayConfigurationResultCode::TargetRebuildFailed);
                set_diagnostic(response, "input bounds update failed");
            }
            if (response.success != 0u && (applyCommand.flags & DisplayConfigurationFlagCommitPersistence) != 0u) {
                if (persist_configuration(response.activeConfiguration)) {
                    response.persistenceCommitted = 1u;
                } else {
                    response.success = 0u;
                    response.resultCode = static_cast<uint32_t>(DisplayConfigurationResultCode::PersistenceFailed);
                    set_diagnostic(response, "persistence commit failed");
                }
            }
            if ((applyCommand.flags & DisplayConfigurationFlagCommitPersistence) != 0u &&
                s_qemuProofPersistenceCommitted &&
                s_qemuProofPersistedSnapshot.mode == response.activeConfiguration.mode) {
                response.persistenceCommitted = 1u;
            }
        }

        if (response.success == 0u) {
            kernel::virtio::gpu::DisplayConfigurationBackendResult rollbackResult{};
            const bool rolledBack = kernel::virtio::gpu::apply_display_configuration_backend_layout(oldRequest, false, &rollbackResult);
            response.rollbackSucceeded = rolledBack ? 1u : 0u;
            if (rolledBack) {
                kernel::virtio::gpu::get_display_configuration_backend_snapshots(&detected, &response.activeConfiguration);
                response.detectedConfiguration = detected;
                update_input_layout(response.activeConfiguration);
                if (response.resultCode == static_cast<uint32_t>(DisplayConfigurationResultCode::TargetRebuildFailed) ||
                    response.resultCode == static_cast<uint32_t>(DisplayConfigurationResultCode::ValidationFrameFailed)) {
                    response.resultCode = static_cast<uint32_t>(DisplayConfigurationResultCode::RollbackSucceeded);
                }
            } else {
                response.resultCode = static_cast<uint32_t>(DisplayConfigurationResultCode::RollbackFailed);
                set_diagnostic(response, "rollback failed");
            }
        } else {
            s_lastKnownGood = applyCommand;
            s_lastKnownGood.commandType = static_cast<uint32_t>(DisplayConfigurationCommandType::ApplyConfiguration);
            s_haveLastKnownGood = true;
        }
        kernel::virtio::gpu::set_display_configuration_backend_presentation_paused(false);
        response.presentationResumed = 1u;
        if (response.success == 0u && response.rollbackSucceeded != 0u) {
            kernel::serial::puts("Display configuration apply: request=");
            serial_u64(command.requestId);
            kernel::serial::puts(" result=failed reason=");
            serial_text(response.diagnostic);
            kernel::serial::puts(" rollback=yes rollbackResult=success activeMode=");
            serial_text(response.activeConfiguration.mode == static_cast<uint32_t>(DisplayConfigurationMode::Extend) ? "Extend" : "Mirror");
            kernel::serial::puts(" presentationResumed=yes\n");
        }
    }

    response.completed = 1u;
    if (type == DisplayConfigurationCommandType::ApplyConfiguration ||
        type == DisplayConfigurationCommandType::RestoreLastKnownGood ||
        type == DisplayConfigurationCommandType::ForceValidationFrame) {
        s_lastApplyResponse = response;
    }
    s_lastResponse = response;
    s_busy = false;
    log_response(response);
    return accepted && response.success != 0u;
}

void DisplayConfigurationService::processPendingAtSafePoint()
{
    // Kernel app callbacks already execute on the desktop owner path. This
    // explicit hook documents the safe point and leaves room for future IPC.
}

DisplayConfigurationResponse DisplayConfigurationService::lastResult()
{
    return s_lastApplyResponse;
}

bool DisplayConfigurationService::isBusy()
{
    return s_busy;
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
