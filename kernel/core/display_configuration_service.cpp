#include "display_configuration_service.h"

#include "include/kernel/desktop.h"
#include "include/kernel/input_manager.h"
#include "include/kernel/kernel_compositor.h"
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
static DisplayConfigurationCommand s_lastKnownGoodTwoOutput{};
static bool s_haveLastKnownGoodTwoOutput = false;
static uint32_t s_activeConfigurationGeneration = 1u;

enum class StartupRestoreState : uint32_t {
    Idle = 0u,
    Pending = 1u,
    WaitingForPersistentStore = 2u,
    WaitingForBackend = 3u,
    Loading = 4u,
    Reconciled = 5u,
    Applying = 6u,
    Applied = 7u,
    Fallback = 8u,
    Complete = 9u
};

struct StartupRestoreDiagnostics {
    StartupRestoreState state{StartupRestoreState::Idle};
    uint8_t persistedLoaded{0u};
    uint8_t persistedValidated{0u};
    uint8_t persistedReconciled{0u};
    uint8_t startupRestoreAttempted{0u};
    uint8_t activeApplied{0u};
    uint8_t startupValidationFrame{0u};
    uint8_t fallbackUsed{0u};
    uint8_t outputsDetected{0u};
    uint32_t persistedVersion{0u};
    uint32_t matchedOutputCount{0u};
    uint32_t unmatchedSavedOutputs{0u};
    uint32_t unmatchedDetectedOutputs{0u};
    uint32_t reconciliationResult{static_cast<uint32_t>(gxos::display::DisplayConfigurationReconciliationResult::NotRun)};
    char fallbackReason[kDisplayConfigurationDiagnosticBytes]{};
};

static StartupRestoreDiagnostics s_startupRestore{};
// The QEMU FAT32 proof store is intentionally constrained to the existing
// bounded 8.3 writer; the serialized contents remain versioned and complete.
static constexpr const char* kDisplayConfigurationPersistencePath = "/display.cfg";

struct PersistedDisplayConfiguration {
    DisplayConfigurationSnapshot snapshot{};
    uint32_t version{0u};
    bool hasOutputs{false};
    bool legacyV1{false};
};

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

static uint32_t text_length(const char* value, uint32_t capacity = 0xFFFFFFFFu)
{
    if (value == nullptr) return 0u;
    uint32_t length = 0u;
    while (length < capacity && value[length] != '\0') ++length;
    return length;
}

static const char* origin_name(uint32_t origin)
{
    switch (static_cast<DisplayConfigurationRequestOrigin>(origin)) {
    case DisplayConfigurationRequestOrigin::UserApply: return "UserApply";
    case DisplayConfigurationRequestOrigin::StartupRestore: return "StartupRestore";
    case DisplayConfigurationRequestOrigin::TestCoordinator: return "TestCoordinator";
    case DisplayConfigurationRequestOrigin::LastKnownGoodRecovery: return "LastKnownGoodRecovery";
    default: return "Unknown";
    }
}

static bool append_text(char* destination, uint32_t capacity, uint32_t& position, const char* value)
{
    if (destination == nullptr || value == nullptr || position >= capacity) return false;
    const uint32_t length = text_length(value);
    if (length >= capacity - position) return false;
    for (uint32_t i = 0u; i < length; ++i) destination[position++] = value[i];
    destination[position] = '\0';
    return true;
}

static bool append_u32(char* destination, uint32_t capacity, uint32_t& position, uint32_t value)
{
    char digits[11];
    uint32_t count = 0u;
    do {
        digits[count++] = static_cast<char>('0' + (value % 10u));
        value /= 10u;
    } while (value != 0u && count < sizeof(digits));
    while (count > 0u) {
        if (position + 1u >= capacity) return false;
        destination[position++] = digits[--count];
    }
    destination[position] = '\0';
    return true;
}

static bool append_i32(char* destination, uint32_t capacity, uint32_t& position, int32_t value)
{
    if (value < 0) {
        if (!append_text(destination, capacity, position, "-")) return false;
        const uint32_t magnitude = static_cast<uint32_t>(-(value + 1)) + 1u;
        return append_u32(destination, capacity, position, magnitude);
    }
    return append_u32(destination, capacity, position, static_cast<uint32_t>(value));
}

static bool parse_u32(const char* value, uint32_t* result)
{
    if (value == nullptr || result == nullptr || value[0] == '\0') return false;
    uint32_t parsed = 0u;
    for (uint32_t i = 0u; value[i] != '\0'; ++i) {
        if (value[i] < '0' || value[i] > '9') return false;
        const uint32_t digit = static_cast<uint32_t>(value[i] - '0');
        if (parsed > (0xFFFFFFFFu - digit) / 10u) return false;
        parsed = parsed * 10u + digit;
    }
    *result = parsed;
    return true;
}

static bool parse_i32_bounded(const char* value, int32_t* result)
{
    if (value == nullptr || result == nullptr || value[0] == '\0') return false;
    bool negative = false;
    uint32_t offset = 0u;
    if (value[0] == '-') {
        negative = true;
        offset = 1u;
    }
    if (value[offset] == '\0') return false;
    uint32_t magnitude = 0u;
    if (!parse_u32(value + offset, &magnitude)) return false;
    if ((!negative && magnitude > 2147483647u) || (negative && magnitude > 2147483648u)) return false;
    if (negative) {
        if (magnitude == 2147483648u) *result = static_cast<int32_t>(-2147483647 - 1);
        else *result = -static_cast<int32_t>(magnitude);
    } else {
        *result = static_cast<int32_t>(magnitude);
    }
    return true;
}

static bool parse_hex32(const char* value, uint32_t* result)
{
    if (value == nullptr || result == nullptr || value[0] == '\0') return false;
    uint32_t parsed = 0u;
    for (uint32_t i = 0u; value[i] != '\0'; ++i) {
        uint32_t digit = 0u;
        if (value[i] >= '0' && value[i] <= '9') digit = static_cast<uint32_t>(value[i] - '0');
        else if (value[i] >= 'a' && value[i] <= 'f') digit = static_cast<uint32_t>(value[i] - 'a' + 10);
        else if (value[i] >= 'A' && value[i] <= 'F') digit = static_cast<uint32_t>(value[i] - 'A' + 10);
        else return false;
        if (parsed > 0x0FFFFFFFu) return false;
        parsed = (parsed << 4u) | digit;
    }
    *result = parsed;
    return true;
}

static uint32_t fnv1a32(const char* value, uint32_t length)
{
    uint32_t hash = 2166136261u;
    for (uint32_t i = 0u; i < length; ++i) {
        hash ^= static_cast<uint8_t>(value[i]);
        hash *= 16777619u;
    }
    return hash;
}

static void patch_decimal5(char* text, uint32_t offset, uint32_t value)
{
    char digits[5]{};
    for (uint32_t i = 0u; i < sizeof(digits); ++i) {
        digits[sizeof(digits) - 1u - i] = static_cast<char>('0' + (value % 10u));
        value /= 10u;
    }
    for (uint32_t i = 0u; i < sizeof(digits); ++i) text[offset + i] = digits[i];
}

static void patch_hex8(char* text, uint32_t offset, uint32_t value)
{
    const char* digits = "0123456789ABCDEF";
    for (uint32_t i = 0u; i < 8u; ++i) {
        text[offset + 7u - i] = digits[value & 0xFu];
        value >>= 4u;
    }
}

static bool bounded_coordinate(int32_t value)
{
    return value >= -static_cast<int32_t>(kDisplayConfigurationPersistenceMaxCoordinate) &&
        value <= static_cast<int32_t>(kDisplayConfigurationPersistenceMaxCoordinate);
}

static bool bounded_dimension(int32_t value)
{
    return value > 0 && value <= static_cast<int32_t>(kDisplayConfigurationPersistenceMaxDimension);
}

static bool output_is_bounded(const DisplayConfigurationOutput& output)
{
    return output.stableId[0] != '\0' &&
        bounded_coordinate(output.virtualX) && bounded_coordinate(output.virtualY) &&
        bounded_dimension(output.width) && bounded_dimension(output.height) &&
        output.enabled != 0u;
}

static bool split_field(char*& cursor, char* field, uint32_t fieldCapacity)
{
    if (cursor == nullptr || field == nullptr || fieldCapacity == 0u) return false;
    uint32_t length = 0u;
    while (cursor[length] != '\0' && cursor[length] != '|') ++length;
    if (length == 0u || length >= fieldCapacity) return false;
    for (uint32_t i = 0u; i < length; ++i) field[i] = cursor[i];
    field[length] = '\0';
    cursor += length;
    if (*cursor == '|') ++cursor;
    return true;
}

static bool parse_persisted_output(char* value, DisplayConfigurationOutput& output)
{
    char fields[13][kDisplayConfigurationOutputIdBytes]{};
    char* cursor = value;
    uint32_t enabled = 0u;
    uint32_t primary = 0u;
    for (uint32_t i = 0u; i < 12u; ++i) {
        if (!split_field(cursor, fields[i], sizeof(fields[i]))) return false;
    }
    if (!parse_u32(fields[3], &output.scanoutId) || !parse_u32(fields[4], &output.logicalOrdinal) ||
        !parse_i32_bounded(fields[6], &output.virtualX) || !parse_i32_bounded(fields[7], &output.virtualY) ||
        !parse_i32_bounded(fields[8], &output.width) || !parse_i32_bounded(fields[9], &output.height) ||
        !parse_u32(fields[10], &enabled) || !parse_u32(fields[11], &primary)) return false;
    if (enabled > 1u || primary > 1u) return false;
    copy_text(output.stableId, sizeof(output.stableId), fields[0]);
    copy_text(output.backendType, sizeof(output.backendType), fields[1]);
    copy_text(output.backendDeviceId, sizeof(output.backendDeviceId), fields[2]);
    copy_text(output.stableName, sizeof(output.stableName), fields[5]);
    // Per-output logical mode identity was added without invalidating the
    // bounded version-2 record. Older records simply omit this optional tail.
    if (cursor != nullptr && cursor[0] != '\0') {
        if (!split_field(cursor, fields[12], sizeof(fields[12]))) return false;
        copy_text(output.modeId, sizeof(output.modeId), fields[12]);
    }
    output.enabled = static_cast<uint8_t>(enabled);
    output.primary = static_cast<uint8_t>(primary);
    output.reserved[0] = output.reserved[1] = 0u;
    return output_is_bounded(output);
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

static bool output_identity_equals(const char* left, const char* right)
{
    return left != nullptr && right != nullptr && text_equals(left, right);
}

static bool service_output_identity_valid(const DisplayConfigurationOutput& output,
                                          uint32_t ordinal)
{
    const char* expected = ordinal == 1u ? "display-1" : ordinal == 2u ? "display-2" : "";
    return expected[0] != '\0' && output.stableId[0] != '\0' &&
        text_equals(output.stableId, expected) && output.scanoutId == ordinal - 1u;
}

static bool query_contains_identity(const char identities[][kVirtioGpuDisplayEventIdentityBytes],
                                    uint32_t count,
                                    const char* identity)
{
    for (uint32_t i = 0u; i < count && i < kVirtioGpuDisplayEventMaxScanouts; ++i) {
        if (output_identity_equals(identities[i], identity)) return true;
    }
    return false;
}

static bool snapshot_contains_identity(const DisplayConfigurationSnapshot& snapshot,
                                       const char* identity,
                                       uint32_t* indexOut = nullptr)
{
    for (uint32_t i = 0u; i < snapshot.outputCount && i < kDisplayConfigurationMaxOutputs; ++i) {
        if (output_identity_equals(snapshot.outputs[i].stableId, identity)) {
            if (indexOut != nullptr) *indexOut = i;
            return true;
        }
    }
    return false;
}

static void set_request_primary(DisplayConfigurationRequest& request,
                                uint32_t primaryIndex)
{
    request.primaryOutputId[0] = '\0';
    for (uint32_t i = 0u; i < request.outputCount && i < kDisplayConfigurationMaxOutputs; ++i) {
        request.outputs[i].primary = i == primaryIndex ? 1u : 0u;
        if (i == primaryIndex) copy_text(request.primaryOutputId, sizeof(request.primaryOutputId), request.outputs[i].stableId);
    }
}

static void normalize_request_arrangement(DisplayConfigurationRequest& request)
{
    int32_t nextX = 0;
    for (uint32_t i = 0u; i < request.outputCount && i < kDisplayConfigurationMaxOutputs; ++i) {
        if (request.mode == static_cast<uint32_t>(DisplayConfigurationMode::Mirror)) {
            request.outputs[i].virtualX = 0;
            request.outputs[i].virtualY = 0;
        } else {
            request.outputs[i].virtualX = nextX;
            request.outputs[i].virtualY = 0;
            nextX += request.outputs[i].width > 0 ? request.outputs[i].width : 0;
        }
    }
}

static void make_default_added_output(DisplayConfigurationOutput& output,
                                      const char* identity,
                                      uint32_t ordinal)
{
    output = DisplayConfigurationOutput{};
    copy_text(output.stableId, sizeof(output.stableId), identity);
    copy_text(output.backendType, sizeof(output.backendType), "virtio-gpu");
    copy_text(output.backendDeviceId, sizeof(output.backendDeviceId), "gpu0");
    copy_text(output.modeId, sizeof(output.modeId), "qemu-1280x800");
    output.scanoutId = ordinal > 0u ? ordinal - 1u : 0u;
    output.logicalOrdinal = ordinal;
    copy_text(output.stableName, sizeof(output.stableName), ordinal == 2u ? "Display 2" : "Display 1");
    output.width = 1280;
    output.height = 800;
    output.enabled = 1u;
    output.primary = 0u;
}

static bool build_topology_request(
    const DisplayConfigurationSnapshot& active,
    const DisplayTopologyChangeQuery& pending,
    DisplayConfigurationRequest& request,
    DisplayTopologyReconciliationPlan& plan,
    const DisplayConfigurationCommand& context,
    const char** reasonOut)
{
    request = DisplayConfigurationRequest{};
    plan = DisplayTopologyReconciliationPlan{};
    plan.version = kDisplayConfigurationContractVersion;
    plan.sourceTopologyGeneration = pending.topologyGeneration;
    plan.activeConfigurationGeneration = s_activeConfigurationGeneration;
    plan.changeType = pending.changeType;
    plan.currentActiveConfiguration = active;
    copy_text(plan.oldPrimaryOutputId, sizeof(plan.oldPrimaryOutputId), active.primaryOutputId);
    if (reasonOut != nullptr) *reasonOut = nullptr;
    if (pending.pending == 0u) {
        if (reasonOut != nullptr) *reasonOut = "no pending topology change";
        copy_text(plan.rejectionReason, sizeof(plan.rejectionReason), "no pending topology change");
        return false;
    }

    request.mode = active.mode;
    if (pending.changeType == static_cast<uint32_t>(VirtioGpuTopologyChangeType::OutputRemoval)) {
        for (uint32_t i = 0u; i < active.outputCount && i < kDisplayConfigurationMaxOutputs; ++i) {
            if (query_contains_identity(pending.removedOutputIdentities, pending.removedOutputCount,
                                         active.outputs[i].stableId)) {
                plan.removedOutputs[plan.removedOutputCount++] = active.outputs[i];
                continue;
            }
            if (request.outputCount < kDisplayConfigurationMaxOutputs) {
                request.outputs[request.outputCount++] = active.outputs[i];
                ++plan.retainedOutputCount;
            }
        }
    } else if (pending.changeType == static_cast<uint32_t>(VirtioGpuTopologyChangeType::OutputAddition)) {
        for (uint32_t i = 0u; i < active.outputCount && i < kDisplayConfigurationMaxOutputs; ++i) {
            request.outputs[request.outputCount++] = active.outputs[i];
            ++plan.retainedOutputCount;
        }
        for (uint32_t i = 0u; i < pending.addedOutputCount && request.outputCount < kDisplayConfigurationMaxOutputs; ++i) {
            if (snapshot_contains_identity(active, pending.addedOutputIdentities[i])) continue;
            DisplayConfigurationOutput added{};
            const uint32_t ordinal = pending.addedOutputIdentities[i][8] == '2' ? 2u : 1u;
            if (s_haveLastKnownGoodTwoOutput) {
                bool restored = false;
                for (uint32_t j = 0u; j < s_lastKnownGoodTwoOutput.requestedConfiguration.outputCount; ++j) {
                    if (output_identity_equals(s_lastKnownGoodTwoOutput.requestedConfiguration.outputs[j].stableId,
                                                pending.addedOutputIdentities[i])) {
                        added = s_lastKnownGoodTwoOutput.requestedConfiguration.outputs[j];
                        restored = true;
                        break;
                    }
                }
                if (!restored) make_default_added_output(added, pending.addedOutputIdentities[i], ordinal);
            } else {
                make_default_added_output(added, pending.addedOutputIdentities[i], ordinal);
            }
            request.outputs[request.outputCount++] = added;
            plan.addedOutputs[plan.addedOutputCount++] = added;
        }
    } else {
        // Metadata-only changes are deliberately no-op plans. They update the
        // detected inventory through refresh/query but preserve active state.
        request_from_snapshot(active, request);
    }

    if (request.outputCount == 0u) {
        if (reasonOut != nullptr) *reasonOut = "no retained operational output for primary fallback";
        copy_text(plan.rejectionReason, sizeof(plan.rejectionReason), "no retained operational output for primary fallback");
        return false;
    }

    uint32_t primaryIndex = 0u;
    bool oldPrimaryRetained = false;
    for (uint32_t i = 0u; i < request.outputCount; ++i) {
        if (output_identity_equals(request.outputs[i].stableId, active.primaryOutputId)) {
            primaryIndex = i;
            oldPrimaryRetained = true;
            break;
        }
    }
    if (!oldPrimaryRetained) primaryIndex = 0u;
    set_request_primary(request, primaryIndex);
    normalize_request_arrangement(request);
    plan.proposedRequestedConfiguration = active;
    plan.proposedRequestedConfiguration.mode = request.mode;
    plan.proposedRequestedConfiguration.outputCount = request.outputCount;
    copy_text(plan.proposedRequestedConfiguration.primaryOutputId,
              sizeof(plan.proposedRequestedConfiguration.primaryOutputId), request.primaryOutputId);
    copy_text(plan.proposedRequestedConfiguration.taskbarMonitorId,
              sizeof(plan.proposedRequestedConfiguration.taskbarMonitorId), request.primaryOutputId);
    for (uint32_t i = 0u; i < request.outputCount; ++i) {
        plan.proposedRequestedConfiguration.outputs[i] = request.outputs[i];
        plan.retainedOutputs[i] = request.outputs[i];
    }
    plan.proposedRequestedConfiguration.virtualDesktopX = 0;
    plan.proposedRequestedConfiguration.virtualDesktopY = 0;
    plan.proposedRequestedConfiguration.virtualDesktopWidth = 0;
    plan.proposedRequestedConfiguration.virtualDesktopHeight = 0;
    for (uint32_t i = 0u; i < request.outputCount; ++i) {
        const int32_t right = request.outputs[i].virtualX + request.outputs[i].width;
        const int32_t bottom = request.outputs[i].virtualY + request.outputs[i].height;
        if (right > plan.proposedRequestedConfiguration.virtualDesktopWidth) plan.proposedRequestedConfiguration.virtualDesktopWidth = right;
        if (bottom > plan.proposedRequestedConfiguration.virtualDesktopHeight) plan.proposedRequestedConfiguration.virtualDesktopHeight = bottom;
    }
    plan.proposedRequestedConfiguration.presenterActive = active.presenterActive;
    plan.proposedRequestedConfiguration.qemuOnly = active.qemuOnly;
    plan.valid = 1u;
    copy_text(plan.proposedPrimaryOutputId, sizeof(plan.proposedPrimaryOutputId), request.primaryOutputId);
    copy_text(plan.monitorRectanglesBefore, sizeof(plan.monitorRectanglesBefore), "active monitor union retained for rollback");
    copy_text(plan.monitorRectanglesAfter, sizeof(plan.monitorRectanglesAfter),
              request.outputCount > 1u ? "Display 1 at 0,0; Display 2 to the right" : "Display 1 at 0,0");
    copy_text(plan.resourceActions, sizeof(plan.resourceActions),
              pending.changeType == static_cast<uint32_t>(VirtioGpuTopologyChangeType::OutputRemoval)
                  ? "unbind removed scanout; retain resource until commit"
                  : pending.changeType == static_cast<uint32_t>(VirtioGpuTopologyChangeType::OutputAddition)
                      ? "prepare bounded resource and bind added scanout"
                      : "no resource action");
    copy_text(plan.scanoutActions, sizeof(plan.scanoutActions),
              pending.changeType == static_cast<uint32_t>(VirtioGpuTopologyChangeType::OutputRemoval)
                  ? "SET_SCANOUT resource=0 provisionally"
                  : pending.changeType == static_cast<uint32_t>(VirtioGpuTopologyChangeType::OutputAddition)
                      ? "SET_SCANOUT added resource after validation"
                      : "no scanout action");
    plan.windowReconciliationRequired = pending.changeType == static_cast<uint32_t>(VirtioGpuTopologyChangeType::OutputRemoval) ? 1u : 0u;
    plan.cursorReconciliationRequired = plan.windowReconciliationRequired;
    plan.persistenceChangeRequired = (context.flags & DisplayConfigurationFlagCommitPersistence) != 0u ? 1u : 0u;
    return true;
}

static bool validate_topology_request(const DisplayConfigurationRequest& request,
                                      const DisplayConfigurationSnapshot& active,
                                      const char** reasonOut)
{
    const char* reason = nullptr;
    if (request.outputCount == 0u || request.outputCount > 2u) reason = "bounded topology request output count is invalid";
    if (reason == nullptr && request.mode != static_cast<uint32_t>(DisplayConfigurationMode::Extend) &&
        request.mode != static_cast<uint32_t>(DisplayConfigurationMode::Mirror)) reason = "topology request mode is unsupported";
    if (reason == nullptr && request.outputCount > active.outputCount + 1u) reason = "topology addition exceeds device capacity";
    int32_t mirrorWidth = 0;
    int32_t mirrorHeight = 0;
    bool primaryFound = false;
    for (uint32_t i = 0u; reason == nullptr && i < request.outputCount; ++i) {
        if (!service_output_identity_valid(request.outputs[i], i + 1u) ||
            request.outputs[i].width <= 0 || request.outputs[i].height <= 0) {
            reason = "topology request contains an unavailable output";
            break;
        }
        if (request.outputs[i].primary != 0u) primaryFound = true;
        if (request.mode == static_cast<uint32_t>(DisplayConfigurationMode::Mirror)) {
            if (mirrorWidth == 0) {
                mirrorWidth = request.outputs[i].width;
                mirrorHeight = request.outputs[i].height;
            } else if (mirrorWidth != request.outputs[i].width || mirrorHeight != request.outputs[i].height) {
                reason = "Mirror dimensions incompatible";
            }
        }
    }
    if (reason == nullptr && !primaryFound) reason = "topology request has no primary fallback";
    if (reasonOut != nullptr) *reasonOut = reason;
    return reason == nullptr;
}

static void fill_pending_response(DisplayConfigurationResponse& response,
                                  const DisplayTopologyChangeQuery& pending,
                                  const DisplayTopologyReconciliationPlan* plan)
{
    response.topologyGeneration = pending.topologyGeneration;
    response.pendingChangeType = pending.changeType;
    response.injectedTopologyGeneration = pending.injectedTopologyGeneration;
    response.pendingTopology = pending.pending;
    response.pendingAffectsActiveConfiguration = pending.activeConfigurationAffected;
    response.pendingRequiresUserAction = pending.requiresUserAction;
    response.pendingAcknowledged = pending.acknowledged;
    response.pendingDismissed = pending.dismissed;
    response.pendingApplied = pending.applied;
    response.genuineDeviceEvent = pending.genuineDeviceEvent;
    response.injectedTestEvent = pending.injectedEvent;
    response.addedOutputCount = pending.addedOutputCount;
    response.removedOutputCount = pending.removedOutputCount;
    copy_text(response.pendingSource, sizeof(response.pendingSource), pending.source);
    copy_text(response.injectedChangeType, sizeof(response.injectedChangeType), pending.injectedChangeType);
    for (uint32_t i = 0u; i < kDisplayConfigurationMaxOutputs; ++i) {
        response.addedOutputs[i] = DisplayConfigurationOutput{};
        response.removedOutputs[i] = DisplayConfigurationOutput{};
    }
    if (plan != nullptr) {
        response.proposedConfiguration = plan->proposedRequestedConfiguration;
        response.requestedConfiguration = plan->proposedRequestedConfiguration;
        response.addedOutputCount = plan->addedOutputCount;
        response.removedOutputCount = plan->removedOutputCount;
        response.retainedOutputCount = plan->retainedOutputCount;
        copy_text(response.proposedPrimaryOutputId, sizeof(response.proposedPrimaryOutputId), plan->proposedPrimaryOutputId);
        copy_text(response.proposedArrangement, sizeof(response.proposedArrangement), plan->monitorRectanglesAfter);
        copy_text(response.resourceActions, sizeof(response.resourceActions), plan->resourceActions);
        response.windowReconciliationRequired = plan->windowReconciliationRequired;
        response.cursorReconciliationRequired = plan->cursorReconciliationRequired;
        for (uint32_t i = 0u; i < kDisplayConfigurationMaxOutputs; ++i) {
            response.addedOutputs[i] = plan->addedOutputs[i];
            response.removedOutputs[i] = plan->removedOutputs[i];
            response.retainedOutputs[i] = plan->retainedOutputs[i];
        }
    }
    copy_text(response.persistenceImpact, sizeof(response.persistenceImpact),
              "persist only after successful validation and commit");
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
    char text[kDisplayConfigurationPersistenceMaxBytes + 1u]{};
    uint32_t position = 0u;
    const uint32_t serializedSizeOffset = 25u; // version=2\nserializedSize=
    const uint32_t checksumOffset = 40u;      // serializedSize=00000\nchecksum=
    bool ok = true;
    ok = ok && append_text(text, sizeof(text), position, "version=2\nserializedSize=00000\nchecksum=00000000\nbackend=virtio-gpu\nmode=");
    ok = ok && append_text(text, sizeof(text), position,
        snapshot.mode == static_cast<uint32_t>(DisplayConfigurationMode::Extend) ? "extend" : "mirror");
    ok = ok && append_text(text, sizeof(text), position, "\nprimaryOutputId=");
    ok = ok && append_text(text, sizeof(text), position, snapshot.primaryOutputId);
    ok = ok && append_text(text, sizeof(text), position, "\noutputCount=");
    ok = ok && append_u32(text, sizeof(text), position, snapshot.outputCount);
    for (uint32_t i = 0u; ok && i < snapshot.outputCount && i < kDisplayConfigurationMaxOutputs; ++i) {
        const DisplayConfigurationOutput& output = snapshot.outputs[i];
        ok = ok && append_text(text, sizeof(text), position, "\noutput");
        ok = ok && append_u32(text, sizeof(text), position, i);
        ok = ok && append_text(text, sizeof(text), position, "=");
        ok = ok && append_text(text, sizeof(text), position, output.stableId);
        ok = ok && append_text(text, sizeof(text), position, "|");
        ok = ok && append_text(text, sizeof(text), position, output.backendType[0] ? output.backendType : "virtio-gpu");
        ok = ok && append_text(text, sizeof(text), position, "|");
        ok = ok && append_text(text, sizeof(text), position, output.backendDeviceId[0] ? output.backendDeviceId : "gpu0");
        ok = ok && append_text(text, sizeof(text), position, "|");
        ok = ok && append_u32(text, sizeof(text), position, output.scanoutId);
        ok = ok && append_text(text, sizeof(text), position, "|");
        ok = ok && append_u32(text, sizeof(text), position, output.logicalOrdinal == 0u ? i + 1u : output.logicalOrdinal);
        ok = ok && append_text(text, sizeof(text), position, "|");
        ok = ok && append_text(text, sizeof(text), position, output.stableName[0] ? output.stableName : output.stableId);
        ok = ok && append_text(text, sizeof(text), position, "|");
        ok = ok && append_i32(text, sizeof(text), position, output.virtualX);
        ok = ok && append_text(text, sizeof(text), position, "|");
        ok = ok && append_i32(text, sizeof(text), position, output.virtualY);
        ok = ok && append_text(text, sizeof(text), position, "|");
        ok = ok && append_i32(text, sizeof(text), position, output.width);
        ok = ok && append_text(text, sizeof(text), position, "|");
        ok = ok && append_i32(text, sizeof(text), position, output.height);
        ok = ok && append_text(text, sizeof(text), position, "|");
        ok = ok && append_u32(text, sizeof(text), position, output.enabled ? 1u : 0u);
        ok = ok && append_text(text, sizeof(text), position, "|");
        ok = ok && append_u32(text, sizeof(text), position, output.primary ? 1u : 0u);
        ok = ok && append_text(text, sizeof(text), position, "|");
        ok = ok && append_text(text, sizeof(text), position, output.modeId[0] ? output.modeId : "qemu-1280x800");
    }
    ok = ok && append_text(text, sizeof(text), position, "\nvirtualDesktop=");
    ok = ok && append_i32(text, sizeof(text), position, snapshot.virtualDesktopX);
    ok = ok && append_text(text, sizeof(text), position, "|");
    ok = ok && append_i32(text, sizeof(text), position, snapshot.virtualDesktopY);
    ok = ok && append_text(text, sizeof(text), position, "|");
    ok = ok && append_i32(text, sizeof(text), position, snapshot.virtualDesktopWidth);
    ok = ok && append_text(text, sizeof(text), position, "|");
    ok = ok && append_i32(text, sizeof(text), position, snapshot.virtualDesktopHeight);
    ok = ok && append_text(text, sizeof(text), position, "\npresenterRequired=1\n");
    if (!ok || position >= sizeof(text) || position > 99999u) return false;

    patch_decimal5(text, serializedSizeOffset, position);
    const uint32_t checksum = fnv1a32(text, position);
    patch_hex8(text, checksumOffset, checksum);
    const int32_t written = kernel::vfs::write_file(kDisplayConfigurationPersistencePath, text, position);
    if (written != static_cast<int32_t>(position)) return false;

    kernel::serial::puts("Display persistence commit: request=");
    serial_u64(s_nextRequest - 1u);
    kernel::serial::puts(" mode=");
    serial_text(snapshot.mode == static_cast<uint32_t>(DisplayConfigurationMode::Extend) ? "Extend" : "Mirror");
    kernel::serial::puts(" primary=");
    serial_text(snapshot.primaryOutputId);
    kernel::serial::puts(" outputs=");
    serial_u64(snapshot.outputCount);
    kernel::serial::puts(" result=success bytes=");
    serial_u64(position);
    kernel::serial::puts(" version=2\n");
    return true;
}

static bool parse_virtual_desktop(char* value, DisplayConfigurationSnapshot& snapshot)
{
    char fields[4][kDisplayConfigurationOutputIdBytes]{};
    char* cursor = value;
    for (uint32_t i = 0u; i < 4u; ++i) {
        if (!split_field(cursor, fields[i], sizeof(fields[i]))) return false;
    }
    int32_t left = 0;
    int32_t top = 0;
    int32_t right = 0;
    int32_t bottom = 0;
    if (!parse_i32_bounded(fields[0], &left) || !parse_i32_bounded(fields[1], &top) ||
        !parse_i32_bounded(fields[2], &right) || !parse_i32_bounded(fields[3], &bottom) ||
        !bounded_coordinate(left) || !bounded_coordinate(top) || !bounded_coordinate(right) ||
        !bounded_coordinate(bottom) || right <= left || bottom <= top) return false;
    snapshot.virtualDesktopX = left;
    snapshot.virtualDesktopY = top;
    snapshot.virtualDesktopWidth = right - left;
    snapshot.virtualDesktopHeight = bottom - top;
    return snapshot.virtualDesktopWidth <= static_cast<int32_t>(kDisplayConfigurationPersistenceMaxDimension * kDisplayConfigurationMaxOutputs) &&
        snapshot.virtualDesktopHeight <= static_cast<int32_t>(kDisplayConfigurationPersistenceMaxDimension);
}

static bool persisted_identity_duplicate(const DisplayConfigurationSnapshot& snapshot, uint32_t index)
{
    for (uint32_t i = 0u; i < index; ++i) {
        if (text_equals(snapshot.outputs[i].stableId, snapshot.outputs[index].stableId)) return true;
    }
    return false;
}

static bool parse_persisted_configuration(PersistedDisplayConfiguration& persisted,
                                          bool& found,
                                          const char*& failureReason)
{
    found = false;
    failureReason = "not found";
    persisted = PersistedDisplayConfiguration{};
    kernel::vfs::FileInfo info{};
    if (kernel::vfs::stat(kDisplayConfigurationPersistencePath, &info) != kernel::vfs::VFS_OK) {
        return true;
    }
    found = true;
    if (info.size == 0u || info.size > kDisplayConfigurationPersistenceMaxBytes) {
        failureReason = "bounded size check failed";
        return false;
    }

    char text[kDisplayConfigurationPersistenceMaxBytes + 1u]{};
    const int32_t read = kernel::vfs::read_file(kDisplayConfigurationPersistencePath, text,
        kDisplayConfigurationPersistenceMaxBytes);
    if (read <= 0 || static_cast<uint32_t>(read) != info.size) {
        failureReason = "truncated or unreadable serialized data";
        return false;
    }
    const uint32_t length = static_cast<uint32_t>(read);
    text[length] = '\0';
    char rawText[kDisplayConfigurationPersistenceMaxBytes + 1u]{};
    for (uint32_t i = 0u; i < length; ++i) rawText[i] = text[i];
    rawText[length] = '\0';

    uint32_t version = 0u;
    uint32_t serializedSize = 0u;
    uint32_t expectedChecksum = 0u;
    bool haveVersion = false;
    bool haveSerializedSize = false;
    bool haveChecksum = false;
    bool haveBackend = false;
    bool haveMode = false;
    bool havePrimary = false;
    bool haveOutputCount = false;
    bool haveVirtualDesktop = false;
    uint32_t outputCount = 0u;
    uint32_t checksumValueOffset = 0u;
    char backend[kDisplayConfigurationBackendNameBytes]{};
    char mode[16]{};
    char primary[kDisplayConfigurationOutputIdBytes]{};
    char checksumText[16]{};

    uint32_t lineStart = 0u;
    while (lineStart < length) {
        uint32_t lineEnd = lineStart;
        while (lineEnd < length && text[lineEnd] != '\n') ++lineEnd;
        if (lineEnd > lineStart && text[lineEnd - 1u] == '\r') text[lineEnd - 1u] = '\0';
        if (lineEnd < length) text[lineEnd] = '\0';
        char* line = text + lineStart;
        if (line[0] != '\0' && line[0] != '#' && line[0] != ';') {
            char* separator = line;
            while (*separator != '\0' && *separator != '=') ++separator;
            if (*separator != '=') {
                failureReason = "serialized line has no key/value separator";
                return false;
            }
            *separator = '\0';
            char* value = separator + 1u;
            if (text_equals(line, "version")) {
                haveVersion = parse_u32(value, &version);
            } else if (text_equals(line, "serializedSize")) {
                haveSerializedSize = parse_u32(value, &serializedSize);
            } else if (text_equals(line, "checksum")) {
                copy_text(checksumText, sizeof(checksumText), value);
                haveChecksum = parse_hex32(value, &expectedChecksum) && text_length(value) == 8u;
                checksumValueOffset = static_cast<uint32_t>(value - text);
            } else if (text_equals(line, "backend")) {
                copy_text(backend, sizeof(backend), value);
                haveBackend = backend[0] != '\0';
            } else if (text_equals(line, "mode")) {
                copy_text(mode, sizeof(mode), value);
                haveMode = text_equals(mode, "extend") || text_equals(mode, "mirror");
            } else if (text_equals(line, "primaryOutputId")) {
                copy_text(primary, sizeof(primary), value);
                havePrimary = primary[0] != '\0';
            } else if (text_equals(line, "outputCount")) {
                haveOutputCount = parse_u32(value, &outputCount);
            } else if (text_equals(line, "virtualDesktop")) {
                haveVirtualDesktop = parse_virtual_desktop(value, persisted.snapshot);
            } else if (line[0] == 'o' && line[1] == 'u' && line[2] == 't' && line[3] == 'p' && line[4] == 'u' && line[5] == 't') {
                uint32_t index = 0u;
                if (line[6] < '0' || line[6] > '3' || line[7] != '\0' ||
                    !parse_u32(line + 6u, &index) || index >= kDisplayConfigurationMaxOutputs ||
                    !parse_persisted_output(value, persisted.snapshot.outputs[index])) {
                    failureReason = "malformed output identity or geometry";
                    return false;
                }
                persisted.hasOutputs = true;
            }
        }
        lineStart = lineEnd + 1u;
    }

    if (!haveVersion) {
        failureReason = "missing persisted format version";
        return false;
    }
    if (version > kDisplayConfigurationPersistenceVersion || version == 0u) {
        failureReason = "unsupported future or invalid persisted version";
        return false;
    }
    if (!haveBackend || !text_equals(backend, "virtio-gpu") || !haveMode || !havePrimary) {
        failureReason = "missing backend-neutral configuration fields";
        return false;
    }
    persisted.version = version;
    persisted.snapshot.version = kDisplayConfigurationContractVersion;
    persisted.snapshot.structureSize = sizeof(DisplayConfigurationSnapshot);
    copy_text(persisted.snapshot.backend, sizeof(persisted.snapshot.backend), backend);
    copy_text(persisted.snapshot.primaryOutputId, sizeof(persisted.snapshot.primaryOutputId), primary);
    persisted.snapshot.mode = text_equals(mode, "extend")
        ? static_cast<uint32_t>(DisplayConfigurationMode::Extend)
        : static_cast<uint32_t>(DisplayConfigurationMode::Mirror);
    persisted.snapshot.qemuOnly = 1u;
    persisted.snapshot.presenterActive = 1u;

    if (version == 1u) {
        // Version 1 carried only mode, primary, and desktop dimensions. It is
        // accepted as a legacy request and rebuilt against current outputs.
        persisted.legacyV1 = true;
        failureReason = "ok";
        return true;
    }

    if (!haveSerializedSize || !haveChecksum || serializedSize != length ||
        !haveOutputCount || outputCount == 0u || outputCount > kDisplayConfigurationMaxOutputs ||
        !persisted.hasOutputs || !haveVirtualDesktop) {
        failureReason = "bounded count, size, checksum, or geometry field failed";
        return false;
    }
    persisted.snapshot.outputCount = outputCount;
    for (uint32_t i = 0u; i < outputCount; ++i) {
        if (!output_is_bounded(persisted.snapshot.outputs[i]) || persisted_identity_duplicate(persisted.snapshot, i)) {
            failureReason = "duplicate output identity or invalid output bounds";
            return false;
        }
    }
    uint32_t primaryCount = 0u;
    bool primaryFound = false;
    for (uint32_t i = 0u; i < outputCount; ++i) {
        if (persisted.snapshot.outputs[i].primary != 0u) ++primaryCount;
        if (text_equals(persisted.snapshot.outputs[i].stableId, primary)) primaryFound = true;
    }
    if (!primaryFound || primaryCount != 1u) {
        failureReason = "invalid primary output identity";
        return false;
    }
    char checksumInput[kDisplayConfigurationPersistenceMaxBytes + 1u]{};
    for (uint32_t i = 0u; i < length; ++i) checksumInput[i] = rawText[i];
    checksumInput[length] = '\0';
    if (checksumValueOffset == 0u || checksumValueOffset + 8u > length) {
        failureReason = "checksum field is out of bounds";
        return false;
    }
    for (uint32_t i = 0u; i < 8u; ++i) checksumInput[checksumValueOffset + i] = '0';
    if (fnv1a32(checksumInput, length) != expectedChecksum) {
        failureReason = "bounded checksum mismatch";
        return false;
    }
    failureReason = "ok";
    return true;
}

static bool output_identity_matches(const DisplayConfigurationOutput& saved,
                                    const DisplayConfigurationOutput& detected)
{
    if (!text_equals(saved.stableId, detected.stableId)) return false;
    if (saved.backendType[0] != '\0' && !text_equals(saved.backendType, detected.backendType)) return false;
    if (saved.backendDeviceId[0] != '\0' && !text_equals(saved.backendDeviceId, detected.backendDeviceId)) return false;
    if (saved.scanoutId != detected.scanoutId) return false;
    // Requested logical mode dimensions are intentionally not connector
    // identity. A persisted 1024x768 request must reconcile with the same
    // stable output when its detected/preferred geometry is still 1280x800.
    return true;
}

static void default_request_from_detected(const DisplayConfigurationSnapshot& detected,
                                          DisplayConfigurationRequest& request)
{
    request = DisplayConfigurationRequest{};
    request.mode = static_cast<uint32_t>(DisplayConfigurationMode::Extend);
    request.outputCount = detected.outputCount > 2u ? 2u : detected.outputCount;
    int32_t firstWidth = request.outputCount > 0u ? detected.outputs[0].width : 0;
    for (uint32_t i = 0u; i < request.outputCount; ++i) {
        request.outputs[i] = detected.outputs[i];
        request.outputs[i].virtualX = firstWidth * static_cast<int32_t>(i);
        request.outputs[i].virtualY = 0;
        request.outputs[i].primary = i == 0u ? 1u : 0u;
        request.outputs[i].enabled = 1u;
    }
    if (request.outputCount > 0u) copy_text(request.primaryOutputId, sizeof(request.primaryOutputId), request.outputs[0].stableId);
}

static bool reconcile_persisted_request(const PersistedDisplayConfiguration& persisted,
                                        const DisplayConfigurationSnapshot& detected,
                                        DisplayConfigurationRequest& request)
{
    request = DisplayConfigurationRequest{};
    request.mode = persisted.snapshot.mode;
    s_startupRestore.matchedOutputCount = 0u;
    s_startupRestore.unmatchedSavedOutputs = 0u;
    s_startupRestore.unmatchedDetectedOutputs = 0u;
    s_startupRestore.reconciliationResult = static_cast<uint32_t>(DisplayConfigurationReconciliationResult::Rejected);

    if (persisted.legacyV1) {
        default_request_from_detected(detected, request);
        request.mode = persisted.snapshot.mode;
        copy_text(request.primaryOutputId, sizeof(request.primaryOutputId), persisted.snapshot.primaryOutputId);
        for (uint32_t i = 0u; i < request.outputCount; ++i) {
            request.outputs[i].primary = text_equals(request.outputs[i].stableId, request.primaryOutputId) ? 1u : 0u;
        }
        s_startupRestore.matchedOutputCount = request.outputCount;
        s_startupRestore.unmatchedDetectedOutputs = detected.outputCount >= request.outputCount
            ? detected.outputCount - request.outputCount : 0u;
        s_startupRestore.persistedReconciled = request.outputCount >= 2u ? 1u : 0u;
        s_startupRestore.reconciliationResult = request.outputCount >= 2u
            ? static_cast<uint32_t>(DisplayConfigurationReconciliationResult::Success)
            : static_cast<uint32_t>(DisplayConfigurationReconciliationResult::Partial);
        return request.outputCount >= 2u;
    }

    bool savedMatched[kDisplayConfigurationMaxOutputs]{};
    for (uint32_t detectedIndex = 0u; detectedIndex < detected.outputCount && detectedIndex < kDisplayConfigurationMaxOutputs; ++detectedIndex) {
        for (uint32_t savedIndex = 0u; savedIndex < persisted.snapshot.outputCount && savedIndex < kDisplayConfigurationMaxOutputs; ++savedIndex) {
            if (savedMatched[savedIndex] || !output_identity_matches(persisted.snapshot.outputs[savedIndex], detected.outputs[detectedIndex])) continue;
            savedMatched[savedIndex] = true;
            request.outputs[request.outputCount] = persisted.snapshot.outputs[savedIndex];
            if (request.outputs[request.outputCount].backendType[0] == '\0') {
                copy_text(request.outputs[request.outputCount].backendType,
                          sizeof(request.outputs[request.outputCount].backendType),
                          detected.outputs[detectedIndex].backendType);
            }
            if (request.outputs[request.outputCount].backendDeviceId[0] == '\0') {
                copy_text(request.outputs[request.outputCount].backendDeviceId,
                          sizeof(request.outputs[request.outputCount].backendDeviceId),
                          detected.outputs[detectedIndex].backendDeviceId);
            }
            ++request.outputCount;
            ++s_startupRestore.matchedOutputCount;
            break;
        }
    }
    for (uint32_t i = 0u; i < persisted.snapshot.outputCount && i < kDisplayConfigurationMaxOutputs; ++i) {
        if (!savedMatched[i]) ++s_startupRestore.unmatchedSavedOutputs;
    }
    s_startupRestore.unmatchedDetectedOutputs = detected.outputCount > s_startupRestore.matchedOutputCount
        ? detected.outputCount - s_startupRestore.matchedOutputCount : 0u;
    copy_text(request.primaryOutputId, sizeof(request.primaryOutputId), persisted.snapshot.primaryOutputId);
    const bool complete = request.outputCount >= 2u && s_startupRestore.unmatchedSavedOutputs == 0u &&
        s_startupRestore.unmatchedDetectedOutputs == 0u;
    s_startupRestore.persistedReconciled = complete ? 1u : 0u;
    s_startupRestore.reconciliationResult = complete
        ? static_cast<uint32_t>(DisplayConfigurationReconciliationResult::Success)
        : static_cast<uint32_t>(DisplayConfigurationReconciliationResult::Partial);
    return complete;
}

static void set_startup_fallback_reason(const char* reason)
{
    copy_text(s_startupRestore.fallbackReason, sizeof(s_startupRestore.fallbackReason),
        reason != nullptr ? reason : "startup restore fallback");
}

static void fill_startup_diagnostics(DisplayConfigurationResponse& response)
{
    response.persistedLoaded = s_startupRestore.persistedLoaded;
    response.persistedValidated = s_startupRestore.persistedValidated;
    response.persistedReconciled = s_startupRestore.persistedReconciled;
    response.startupRestoreAttempted = s_startupRestore.startupRestoreAttempted;
    response.activeApplied = s_startupRestore.activeApplied;
    response.startupValidationFrame = s_startupRestore.startupValidationFrame;
    response.fallbackUsed = s_startupRestore.fallbackUsed;
    response.outputsDetected = s_startupRestore.outputsDetected;
    response.persistedVersion = s_startupRestore.persistedVersion;
    response.matchedOutputCount = s_startupRestore.matchedOutputCount;
    response.unmatchedSavedOutputs = s_startupRestore.unmatchedSavedOutputs;
    response.unmatchedDetectedOutputs = s_startupRestore.unmatchedDetectedOutputs;
    response.reconciliationResult = s_startupRestore.reconciliationResult;
    copy_text(response.fallbackReason, sizeof(response.fallbackReason), s_startupRestore.fallbackReason);
}

static void log_persistence_load(bool found, bool valid, uint32_t version, const char* reason,
                                 const DisplayConfigurationSnapshot& persisted)
{
    kernel::serial::puts("Display persistence load: found=");
    serial_text(found ? "yes" : "no");
    kernel::serial::puts(" version=");
    serial_u64(version);
    kernel::serial::puts(" valid=");
    serial_text(valid ? "yes" : "no");
    kernel::serial::puts(" requestedMode=");
    if (!found) serial_text("none");
    else serial_text(persisted.mode == static_cast<uint32_t>(DisplayConfigurationMode::Extend) ? "Extend" : "Mirror");
    if (reason != nullptr && reason[0] != '\0') {
        kernel::serial::puts(" reason=");
        serial_text(reason);
    }
    kernel::serial::putc('\n');
}

static void log_persistence_reconcile()
{
    kernel::serial::puts("Display persistence reconcile: savedOutputs=");
    serial_u64(s_startupRestore.matchedOutputCount + s_startupRestore.unmatchedSavedOutputs);
    kernel::serial::puts(" detectedOutputs=");
    serial_u64(s_startupRestore.matchedOutputCount + s_startupRestore.unmatchedDetectedOutputs);
    kernel::serial::puts(" matched=");
    serial_u64(s_startupRestore.matchedOutputCount);
    kernel::serial::puts(" unmatchedSaved=");
    serial_u64(s_startupRestore.unmatchedSavedOutputs);
    kernel::serial::puts(" result=");
    serial_text(s_startupRestore.reconciliationResult == static_cast<uint32_t>(DisplayConfigurationReconciliationResult::Success) ? "success" : "partial");
    kernel::serial::putc('\n');
}

static void log_startup_restore_result(const DisplayConfigurationResponse& response, const char* source)
{
    kernel::serial::puts("Display configuration startup restore: source=");
    serial_text(source != nullptr ? source : "unknown");
    kernel::serial::puts(" injectedByHost=no loaded=");
    serial_text(response.persistedLoaded ? "yes" : "no");
    kernel::serial::puts(" reconciled=");
    serial_text(response.persistedReconciled ? "yes" : "no");
    kernel::serial::puts(" applied=");
    serial_text(response.activeApplied ? "yes" : "no");
    kernel::serial::puts(" mode=");
    serial_text(response.activeConfiguration.mode == static_cast<uint32_t>(DisplayConfigurationMode::Extend) ? "Extend" : "Mirror");
    kernel::serial::puts(" primary=");
    serial_text(response.activeConfiguration.primaryOutputId);
    kernel::serial::puts(" taskbarMonitor=");
    serial_text(response.activeConfiguration.taskbarMonitorId);
    kernel::serial::puts(" virtualDesktop=");
    serial_u64(static_cast<uint32_t>(response.activeConfiguration.virtualDesktopWidth));
    kernel::serial::putc('x');
    serial_u64(static_cast<uint32_t>(response.activeConfiguration.virtualDesktopHeight));
    kernel::serial::puts(" validationFrame=");
    serial_text(response.startupValidationFrame ? "ok" : "failed");
    kernel::serial::puts(" fallback=");
    serial_text(response.fallbackUsed ? "yes" : "no");
    kernel::serial::putc('\n');
}

static bool apply_startup_fallback(const DisplayConfigurationSnapshot& detected,
                                   const char* reason)
{
    s_startupRestore.state = StartupRestoreState::Fallback;
    s_startupRestore.fallbackUsed = 1u;
    set_startup_fallback_reason(reason);
    s_startupRestore.reconciliationResult = static_cast<uint32_t>(DisplayConfigurationReconciliationResult::Fallback);

    DisplayConfigurationCommand fallback{};
    fallback.version = kDisplayConfigurationContractVersion;
    fallback.structureSize = sizeof(fallback);
    fallback.requestId = DisplayConfigurationService::nextRequestId();
    fallback.commandType = static_cast<uint32_t>(DisplayConfigurationCommandType::ApplyConfiguration);
    fallback.origin = static_cast<uint32_t>(DisplayConfigurationRequestOrigin::LastKnownGoodRecovery);
    if (s_haveLastKnownGood) {
        fallback.requestedConfiguration = s_lastKnownGood.requestedConfiguration;
    } else {
        default_request_from_detected(detected, fallback.requestedConfiguration);
    }

    DisplayConfigurationResponse response{};
    const bool applied = DisplayConfigurationService::submit(fallback, response);
    if (!applied || response.success == 0u) {
        fallback.requestedConfiguration = DisplayConfigurationRequest{};
        default_request_from_detected(detected, fallback.requestedConfiguration);
        response = DisplayConfigurationResponse{};
        (void)DisplayConfigurationService::submit(fallback, response);
    }
    s_startupRestore.activeApplied = response.success ? 1u : 0u;
    s_startupRestore.startupValidationFrame = response.validationResult == static_cast<uint32_t>(DisplayConfigurationValidationResult::Passed) ? 1u : 0u;
    s_startupRestore.state = StartupRestoreState::Complete;
    fill_startup_diagnostics(response);
    log_startup_restore_result(response, "safe-fallback");
    return response.success != 0u;
}

static void attempt_startup_restore()
{
    if (s_startupRestore.state == StartupRestoreState::Complete ||
        s_startupRestore.state == StartupRestoreState::Applied ||
        s_startupRestore.state == StartupRestoreState::Loading ||
        s_startupRestore.state == StartupRestoreState::Applying) return;

    s_startupRestore.startupRestoreAttempted = 1u;
    // REAL HARDWARE GPU/MMIO ENABLEMENT IS MULE TERRITORY AND REQUIRES A SEPARATE SAFETY CHECKPOINT.
    if (kernel::vfs::get_mount("/") == nullptr) {
        s_startupRestore.state = StartupRestoreState::WaitingForPersistentStore;
        kernel::serial::puts("Display persistence startup restore: waitingForPersistentStore=yes\n");
        return;
    }

    DisplayConfigurationSnapshot detected{};
    DisplayConfigurationSnapshot active{};
    if (!kernel::virtio::gpu::get_display_configuration_backend_snapshots(&detected, &active)) {
        s_startupRestore.state = StartupRestoreState::WaitingForBackend;
        kernel::serial::puts("Display persistence startup restore: waitingForBackend=yes\n");
        return;
    }
    s_startupRestore.outputsDetected = detected.outputCount >= 2u ? 1u : 0u;
    s_startupRestore.state = StartupRestoreState::Loading;

    PersistedDisplayConfiguration persisted{};
    bool found = false;
    const char* failureReason = "unknown";
    const bool valid = parse_persisted_configuration(persisted, found, failureReason);
    s_startupRestore.persistedLoaded = found ? 1u : 0u;
    s_startupRestore.persistedVersion = persisted.version;
    log_persistence_load(found, valid, persisted.version, failureReason, persisted.snapshot);
    if (!found) {
        s_startupRestore.reconciliationResult = static_cast<uint32_t>(DisplayConfigurationReconciliationResult::NoPersistedConfiguration);
        s_startupRestore.state = StartupRestoreState::Complete;
        return;
    }
    if (!valid) {
        s_startupRestore.persistedValidated = 0u;
        apply_startup_fallback(detected, failureReason);
        return;
    }

    s_startupRestore.persistedValidated = 1u;
    DisplayConfigurationRequest requested{};
    const bool reconciled = reconcile_persisted_request(persisted, detected, requested);
    log_persistence_reconcile();
    if (!reconciled) {
        apply_startup_fallback(detected, "saved output identity could not be fully reconciled");
        return;
    }

    s_startupRestore.state = StartupRestoreState::Applying;
    DisplayConfigurationCommand restore{};
    restore.version = kDisplayConfigurationContractVersion;
    restore.structureSize = sizeof(restore);
    restore.requestId = DisplayConfigurationService::nextRequestId();
    restore.commandType = static_cast<uint32_t>(DisplayConfigurationCommandType::ApplyConfiguration);
    restore.origin = static_cast<uint32_t>(DisplayConfigurationRequestOrigin::StartupRestore);
    restore.requestedConfiguration = requested;
    DisplayConfigurationResponse response{};
    const bool applied = DisplayConfigurationService::submit(restore, response);
    s_startupRestore.activeApplied = applied && response.success != 0u ? 1u : 0u;
    s_startupRestore.startupValidationFrame = response.validationResult == static_cast<uint32_t>(DisplayConfigurationValidationResult::Passed) ? 1u : 0u;
    if (!applied || response.success == 0u || s_startupRestore.startupValidationFrame == 0u) {
        apply_startup_fallback(detected, "persisted configuration transaction failed");
        return;
    }
    s_startupRestore.state = StartupRestoreState::Applied;
    s_startupRestore.state = StartupRestoreState::Complete;
    fill_startup_diagnostics(response);
    log_startup_restore_result(response, "persisted-store");
}

static void log_command(const DisplayConfigurationCommand& command)
{
    kernel::serial::puts("Display config command: request=");
    serial_u64(command.requestId);
    kernel::serial::puts(" origin=");
    serial_text(origin_name(command.origin == 0u
        ? static_cast<uint32_t>(DisplayConfigurationRequestOrigin::UserApply) : command.origin));
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
    response.activeConfigurationGeneration = s_activeConfigurationGeneration;
    const auto type = static_cast<DisplayConfigurationCommandType>(command.commandType);
    const uint32_t failureInjectionFlags = command.flags & kDisplayConfigurationTestFailureMask;
    const bool injectionRequested = failureInjectionFlags != 0u;
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
            ? detected.outputCount >= 1u : activeBefore.outputCount >= 1u)) ? 1u : 0u;
        response.resultCode = response.success
            ? static_cast<uint32_t>(DisplayConfigurationResultCode::Success)
            : static_cast<uint32_t>(DisplayConfigurationResultCode::BackendUnavailable);
        set_diagnostic(response, response.success ? "display configuration query complete" : "QEMU virtio-gpu backend unavailable");
        if (type == DisplayConfigurationCommandType::QueryActiveConfiguration) {
            log_bridge(command.requestId, activeBefore, response.success != 0u);
        }
    } else if (type == DisplayConfigurationCommandType::QueryDetectedTopologyChange ||
               type == DisplayConfigurationCommandType::QueryPendingTopologyChange) {
        response.detectedConfiguration = detected;
        response.activeConfiguration = activeBefore;
        const bool observerReady = kernel::virtio::gpu::query_detected_topology_change(
            &response.detectedTopologyChange);
        response.success = observerReady ? 1u : 0u;
        response.resultCode = response.success
            ? static_cast<uint32_t>(DisplayConfigurationResultCode::Success)
            : static_cast<uint32_t>(DisplayConfigurationResultCode::BackendUnavailable);
        response.activeConfigurationGeneration = s_activeConfigurationGeneration;
        fill_pending_response(response, response.detectedTopologyChange, nullptr);
        set_diagnostic(response, observerReady
            ? (response.detectedTopologyChange.pending != 0u
                ? "Display hardware configuration changed. Review settings. automaticApply=no"
                : "detected topology query complete; active configuration unchanged")
            : "QEMU-only display-event observer unavailable");
    } else if (type == DisplayConfigurationCommandType::PreviewTopologyReconciliation ||
               type == DisplayConfigurationCommandType::ApplyPendingTopologyChange ||
               type == DisplayConfigurationCommandType::DismissPendingTopologyChange) {
        response.detectedConfiguration = detected;
        response.activeConfiguration = activeBefore;
        response.activeConfigurationGeneration = s_activeConfigurationGeneration;
        DisplayTopologyChangeQuery pending{};
        const bool observerReady = kernel::virtio::gpu::query_detected_topology_change(&pending);
        fill_pending_response(response, pending, nullptr);
        if (!observerReady || !backendReady) {
            response.resultCode = static_cast<uint32_t>(DisplayConfigurationResultCode::TopologyReconciliationUnavailable);
            set_diagnostic(response, "QEMU-only topology reconciliation service is unavailable");
            accepted = false;
        } else if (command.topologyGeneration == 0u ||
                   command.topologyGeneration != pending.topologyGeneration ||
                   command.activeConfigurationGeneration == 0u ||
                   command.activeConfigurationGeneration != s_activeConfigurationGeneration ||
                   pending.pending == 0u) {
            response.resultCode = static_cast<uint32_t>(DisplayConfigurationResultCode::TopologyGenerationStale);
            set_diagnostic(response, "TopologyGenerationStale: pending topology or active configuration changed");
            accepted = false;
        } else if (type == DisplayConfigurationCommandType::DismissPendingTopologyChange) {
            const bool dismissed = kernel::virtio::gpu::dismiss_detected_topology_for_service(
                command.topologyGeneration);
            response.success = dismissed ? 1u : 0u;
            response.resultCode = dismissed
                ? static_cast<uint32_t>(DisplayConfigurationResultCode::Success)
                : static_cast<uint32_t>(DisplayConfigurationResultCode::TopologyGenerationStale);
            response.activeConfiguration = activeBefore;
            response.detectedConfiguration = detected;
            response.activeConfigurationGeneration = s_activeConfigurationGeneration;
            (void)kernel::virtio::gpu::query_detected_topology_change(&pending);
            fill_pending_response(response, pending, nullptr);
            set_diagnostic(response, dismissed
                ? "pending topology dismissed; active configuration and persistence unchanged"
                : "TopologyGenerationStale: dismiss was not applied");
        } else {
            DisplayConfigurationRequest topologyRequest{};
            DisplayTopologyReconciliationPlan plan{};
            const char* planReason = nullptr;
            const bool planned = build_topology_request(activeBefore, pending, topologyRequest,
                                                        plan, command, &planReason);
            const bool valid = planned && validate_topology_request(topologyRequest, activeBefore, &planReason);
            plan.valid = valid ? 1u : 0u;
            if (!valid) copy_text(plan.rejectionReason, sizeof(plan.rejectionReason),
                                  planReason != nullptr ? planReason : "topology reconciliation plan rejected");
            fill_pending_response(response, pending, &plan);
            response.requestedConfiguration = plan.proposedRequestedConfiguration;
            response.proposedConfiguration = plan.proposedRequestedConfiguration;
            response.validationResult = valid
                ? static_cast<uint32_t>(DisplayConfigurationValidationResult::Passed)
                : static_cast<uint32_t>(DisplayConfigurationValidationResult::Failed);
            if (type == DisplayConfigurationCommandType::PreviewTopologyReconciliation) {
                response.success = valid ? 1u : 0u;
                response.resultCode = valid
                    ? static_cast<uint32_t>(DisplayConfigurationResultCode::Success)
                    : static_cast<uint32_t>(DisplayConfigurationResultCode::InvalidConfiguration);
                set_diagnostic(response, valid
                    ? "topology reconciliation preview complete; no GPU mutation"
                    : (planReason != nullptr ? planReason : "topology reconciliation preview rejected"));
            } else if (!valid) {
                response.resultCode = static_cast<uint32_t>(DisplayConfigurationResultCode::InvalidConfiguration);
                set_diagnostic(response, planReason != nullptr ? planReason : "topology reconciliation plan rejected");
                accepted = false;
            } else {
                // This is the sole authoritative topology mutation path. The
                // observer only publishes pending state; it never reaches this
                // branch on its own.
                DisplayConfigurationCommand applyCommand = command;
                applyCommand.origin = command.origin == 0u
                    ? static_cast<uint32_t>(DisplayConfigurationRequestOrigin::UserApply) : command.origin;
                applyCommand.requestedConfiguration = topologyRequest;
                applyCommand.commandType = static_cast<uint32_t>(DisplayConfigurationCommandType::ApplyConfiguration);
                const DisplayConfigurationRequest oldRequest = [&]() {
                    DisplayConfigurationRequest value{};
                    request_from_snapshot(activeBefore, value);
                    return value;
                }();
                kernel::serial::puts("Topology reconciliation apply: generation=");
                serial_u64(command.topologyGeneration);
                kernel::serial::puts(" action=");
                serial_text(pending.changeType == static_cast<uint32_t>(VirtioGpuTopologyChangeType::OutputRemoval) ? "remove" : "add");
                kernel::serial::puts(" genuineDeviceEvent=");
                serial_text(pending.genuineDeviceEvent ? "yes" : "no");
                kernel::serial::puts(" injectedTestEvent=");
                serial_text(pending.injectedEvent ? "yes" : "no");
                kernel::serial::puts(" state=pausing-presentation\n");
                kernel::virtio::gpu::set_display_configuration_backend_presentation_paused(true);
                response.presentationPaused = 1u;

                kernel::virtio::gpu::DisplayConfigurationBackendResult backendResult{};
                const bool backendApplied = kernel::virtio::gpu::apply_display_configuration_backend_layout(
                    topologyRequest, failureInjectionFlags, &backendResult);
                response.targetRebuildSucceeded = backendResult.targetRebuilt;
                response.validationResult = backendResult.validationFrame
                    ? static_cast<uint32_t>(DisplayConfigurationValidationResult::Passed)
                    : static_cast<uint32_t>(DisplayConfigurationValidationResult::Failed);
                response.rollbackAttempted = backendApplied ? 0u : 1u;
                response.success = backendApplied ? 1u : 0u;
                response.resultCode = backendApplied
                    ? static_cast<uint32_t>(DisplayConfigurationResultCode::Success)
                    : result_code_for_backend(backendResult);
                set_backend_diagnostic(response, backendResult);

                if (response.success != 0u) {
                    kernel::virtio::gpu::get_display_configuration_backend_snapshots(
                        &detected, &response.activeConfiguration);
                    response.detectedConfiguration = detected;
                    response.success = update_input_layout(response.activeConfiguration) ? 1u : 0u;
                    if (response.success != 0u) {
                        kernel::compositor::KernelCompositor::reconcileDisplayTopology(
                            static_cast<uint32_t>(response.activeConfiguration.virtualDesktopWidth),
                            static_cast<uint32_t>(response.activeConfiguration.virtualDesktopHeight));
                    } else {
                        response.resultCode = static_cast<uint32_t>(DisplayConfigurationResultCode::TargetRebuildFailed);
                        set_diagnostic(response, "topology reconciliation input update failed");
                    }
                    if (response.success != 0u &&
                        (applyCommand.flags & DisplayConfigurationFlagCommitPersistence) != 0u) {
                        if (persist_configuration(response.activeConfiguration)) {
                            response.persistenceCommitted = 1u;
                        } else {
                            response.success = 0u;
                            response.resultCode = static_cast<uint32_t>(DisplayConfigurationResultCode::PersistenceFailed);
                            set_diagnostic(response, "topology reconciliation persistence failed; rolling back");
                        }
                    }
                    if (response.success != 0u &&
                        !kernel::virtio::gpu::apply_detected_topology_for_service(command.topologyGeneration)) {
                        response.success = 0u;
                        response.resultCode = static_cast<uint32_t>(DisplayConfigurationResultCode::TopologyGenerationStale);
                        set_diagnostic(response, "TopologyGenerationStale: pending topology changed before commit");
                    }
                }

                if (response.success == 0u) {
                    response.rollbackAttempted = 1u;
                    kernel::virtio::gpu::DisplayConfigurationBackendResult rollbackResult{};
                    const bool rollback = kernel::virtio::gpu::apply_display_configuration_backend_layout(
                        oldRequest, 0u, &rollbackResult);
                    response.rollbackSucceeded = rollback ? 1u : 0u;
                    response.rollbackOldOutputsRestored = rollback ? 1u : 0u;
                    response.rollbackOldPrimaryRestored = rollback ? 1u : 0u;
                    response.rollbackOldLayoutRestored = rollback ? 1u : 0u;
                    if (rollback) {
                        kernel::virtio::gpu::get_display_configuration_backend_snapshots(
                            &detected, &response.activeConfiguration);
                        response.detectedConfiguration = detected;
                        update_input_layout(response.activeConfiguration);
                        kernel::compositor::KernelCompositor::reconcileDisplayTopology(
                            static_cast<uint32_t>(response.activeConfiguration.virtualDesktopWidth),
                            static_cast<uint32_t>(response.activeConfiguration.virtualDesktopHeight));
                        set_diagnostic(response, "topology reconciliation failed; complete rollback succeeded");
                        copy_text(response.rollbackResult, sizeof(response.rollbackResult),
                                  "scanouts, resources, layout, primary, windows, cursor restored");
                    } else {
                        response.resultCode = static_cast<uint32_t>(DisplayConfigurationResultCode::RollbackFailed);
                        set_diagnostic(response, "topology reconciliation rollback failed");
                    }
                } else {
                    ++s_activeConfigurationGeneration;
                    applyCommand.commandType = static_cast<uint32_t>(DisplayConfigurationCommandType::ApplyConfiguration);
                    s_lastKnownGood = applyCommand;
                    s_haveLastKnownGood = true;
                    if (response.activeConfiguration.outputCount >= 2u) {
                        s_lastKnownGoodTwoOutput = applyCommand;
                        s_haveLastKnownGoodTwoOutput = true;
                    }
                    response.activeConfigurationGeneration = s_activeConfigurationGeneration;
                    response.provisionalResourcesReleased = 1u;
                    set_diagnostic(response, pending.changeType == static_cast<uint32_t>(VirtioGpuTopologyChangeType::OutputRemoval)
                        ? "topology reconciliation removal committed; provisional resource retired after validation"
                        : "topology reconciliation addition committed; live dual-output presentation restored");
                }
                kernel::virtio::gpu::set_display_configuration_backend_presentation_paused(false);
                response.presentationResumed = 1u;
                response.rollbackPresentationResumed = response.rollbackSucceeded;
                if (response.success == 0u && response.rollbackSucceeded != 0u) {
                    response.resultCode = static_cast<uint32_t>(DisplayConfigurationResultCode::RollbackSucceeded);
                }
            }
        }
    } else if (type == DisplayConfigurationCommandType::RefreshDetectedTopology) {
        const bool refreshed = kernel::virtio::gpu::refresh_detected_topology_for_service();
        response.detectedConfiguration = detected;
        response.activeConfiguration = activeBefore;
        (void)kernel::virtio::gpu::query_detected_topology_change(&response.detectedTopologyChange);
        response.success = refreshed ? 1u : 0u;
        response.resultCode = response.success
            ? static_cast<uint32_t>(DisplayConfigurationResultCode::Success)
            : static_cast<uint32_t>(DisplayConfigurationResultCode::BackendBusy);
        set_diagnostic(response, refreshed
            ? "detected topology refreshed; active configuration unchanged"
            : "detected topology refresh deferred or unavailable");
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
        if (applyCommand.origin == 0u) {
            applyCommand.origin = static_cast<uint32_t>(DisplayConfigurationRequestOrigin::UserApply);
        }
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

        // Reject a known-invalid QEMU Mirror geometry before pausing the
        // presenter. No replacement resources are needed for this case and
        // the current active state remains the last-known-good state.
        if (type == DisplayConfigurationCommandType::ApplyConfiguration &&
            applyCommand.requestedConfiguration.mode == static_cast<uint32_t>(DisplayConfigurationMode::Mirror) &&
            applyCommand.requestedConfiguration.outputCount >= 2u) {
            const DisplayConfigurationOutput& first = applyCommand.requestedConfiguration.outputs[0];
            const DisplayConfigurationOutput& second = applyCommand.requestedConfiguration.outputs[1];
            if (first.width > 0 && first.height > 0 && second.width > 0 && second.height > 0 &&
                (first.width != second.width || first.height != second.height)) {
                response.detectedConfiguration = detected;
                response.activeConfiguration = activeBefore;
                response.success = 0u;
                response.completed = 1u;
                response.rollbackAttempted = 0u;
                response.rollbackSucceeded = 0u;
                response.targetRebuildSucceeded = 0u;
                response.resultCode = static_cast<uint32_t>(DisplayConfigurationResultCode::MirrorGeometryIncompatible);
                response.validationResult = static_cast<uint32_t>(DisplayConfigurationValidationResult::NotRun);
                response.presentationPaused = 0u;
                response.presentationResumed = 1u;
                set_diagnostic(response, "Mirror dimensions incompatible: all outputs must use the same logical resolution");
                fill_startup_diagnostics(response);
                s_lastApplyResponse = response;
                s_lastResponse = response;
                s_busy = false;
                log_response(response);
                return false;
            }
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
            applyCommand.requestedConfiguration,
            failureInjectionFlags | (injectFailure ? DisplayConfigurationFlagTestInjectValidationFailure : 0u),
            &backendResult);
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
            } else {
                kernel::compositor::KernelCompositor::reconcileDisplayTopology(
                    static_cast<uint32_t>(response.activeConfiguration.virtualDesktopWidth),
                    static_cast<uint32_t>(response.activeConfiguration.virtualDesktopHeight));
            }
            if (response.success != 0u && (applyCommand.flags & DisplayConfigurationFlagCommitPersistence) != 0u) {
                if (persist_configuration(response.activeConfiguration)) {
                    response.persistenceCommitted = 1u;
                } else {
                    // The real backend transaction and validation frame have
                    // already succeeded. Keep that active state visible and
                    // report that it will not survive restart.
                    response.persistenceCommitted = 0u;
                    response.resultCode = static_cast<uint32_t>(DisplayConfigurationResultCode::Success);
                    set_diagnostic(response, "persistence commit failed; active configuration retained");
                }
            }
        }

        if (response.success == 0u) {
            kernel::virtio::gpu::DisplayConfigurationBackendResult rollbackResult{};
            const bool rolledBack = kernel::virtio::gpu::apply_display_configuration_backend_layout(oldRequest, 0u, &rollbackResult);
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
            ++s_activeConfigurationGeneration;
            response.activeConfigurationGeneration = s_activeConfigurationGeneration;
            s_lastKnownGood = applyCommand;
            s_lastKnownGood.commandType = static_cast<uint32_t>(DisplayConfigurationCommandType::ApplyConfiguration);
            s_haveLastKnownGood = true;
            if (response.activeConfiguration.outputCount >= 2u) {
                s_lastKnownGoodTwoOutput = applyCommand;
                s_haveLastKnownGoodTwoOutput = true;
            }
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
    fill_startup_diagnostics(response);
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
    // Kernel app callbacks and the QEMU live presenter execute on this owner
    // path. Startup restore stays pending until the persistent store and the
    // operational virtio-gpu inventory are both ready, then consumes this
    // slot exactly once through submit().
    if (s_startupRestore.state == StartupRestoreState::Pending ||
        s_startupRestore.state == StartupRestoreState::WaitingForPersistentStore ||
        s_startupRestore.state == StartupRestoreState::WaitingForBackend) {
        if (!s_busy) attempt_startup_restore();
    }
}

void DisplayConfigurationService::requestStartupRestore()
{
    if (s_startupRestore.state == StartupRestoreState::Idle) {
        s_startupRestore.state = StartupRestoreState::Pending;
        s_startupRestore.startupRestoreAttempted = 0u;
        s_startupRestore.activeApplied = 0u;
        s_startupRestore.startupValidationFrame = 0u;
        s_startupRestore.fallbackUsed = 0u;
        s_startupRestore.fallbackReason[0] = '\0';
        kernel::serial::puts("Display persistence startup restore: requested=yes origin=StartupRestore\n");
    }
}

bool DisplayConfigurationService::startupRestoreComplete()
{
    return s_startupRestore.state == StartupRestoreState::Complete ||
        s_startupRestore.state == StartupRestoreState::Applied;
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
    case DisplayConfigurationResultCode::TopologyGenerationStale: return "TopologyGenerationStale";
    case DisplayConfigurationResultCode::TopologyReconciliationUnavailable: return "TopologyReconciliationUnavailable";
    case DisplayConfigurationResultCode::LocalConfigurationConflict: return "LocalConfigurationConflict";
    default: return "Unknown";
    }
}

} // namespace display
} // namespace gxos
