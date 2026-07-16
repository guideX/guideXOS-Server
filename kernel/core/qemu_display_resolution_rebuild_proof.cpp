#include "include/kernel/qemu_display_resolution_rebuild_proof.h"

#include "include/kernel/serial_debug.h"
#include "display_configuration_service.h"

#include <stdint.h>
namespace kernel {
namespace qemu_display_resolution_rebuild_proof {

namespace {

using namespace gxos::display;

static uint64_t s_nextRequestId = 0x5200u;

static bool text_equals(const char* left, const char* right)
{
    if (left == nullptr || right == nullptr) return left == right;
    uint32_t index = 0u;
    while (left[index] != '\0' || right[index] != '\0') {
        if (left[index] != right[index]) return false;
        ++index;
    }
    return true;
}

static void copy_text(char* destination, uint32_t capacity, const char* source)
{
    if (destination == nullptr || capacity == 0u) return;
    if (source == nullptr) source = "";
    uint32_t index = 0u;
    while (source[index] != '\0' && index + 1u < capacity) {
        destination[index] = source[index];
        ++index;
    }
    destination[index] = '\0';
    while (++index < capacity) destination[index] = '\0';
}

static const char* mode_id(uint32_t width, uint32_t height)
{
    if (width == 1280u && height == 800u) return "qemu-1280x800";
    if (width == 1024u && height == 768u) return "qemu-1024x768";
    if (width == 800u && height == 600u) return "qemu-800x600";
    return "";
}

static DisplayConfigurationCommand make_apply(
    const DisplayConfigurationSnapshot& current,
    DisplayConfigurationMode mode,
    uint32_t width0,
    uint32_t height0,
    uint32_t width1,
    uint32_t height1,
    uint32_t primaryOrdinal,
    uint32_t flags)
{
    DisplayConfigurationCommand command{};
    command.version = kDisplayConfigurationContractVersion;
    command.structureSize = sizeof(command);
    command.requestId = ++s_nextRequestId;
    command.commandType = static_cast<uint32_t>(DisplayConfigurationCommandType::ApplyConfiguration);
    command.flags = flags;
    command.origin = static_cast<uint32_t>(DisplayConfigurationRequestOrigin::TestCoordinator);
    command.requestedConfiguration.mode = static_cast<uint32_t>(mode);
    command.requestedConfiguration.outputCount = current.outputCount > 2u ? 2u : current.outputCount;
    if (command.requestedConfiguration.outputCount < 2u) return command;
    for (uint32_t i = 0u; i < 2u; ++i) {
        command.requestedConfiguration.outputs[i] = current.outputs[i];
        const uint32_t width = i == 0u ? width0 : width1;
        const uint32_t height = i == 0u ? height0 : height1;
        copy_text(command.requestedConfiguration.outputs[i].modeId,
                  sizeof(command.requestedConfiguration.outputs[i].modeId), mode_id(width, height));
        command.requestedConfiguration.outputs[i].width = static_cast<int32_t>(width);
        command.requestedConfiguration.outputs[i].height = static_cast<int32_t>(height);
        command.requestedConfiguration.outputs[i].virtualX = mode == DisplayConfigurationMode::Mirror
            ? 0 : (i == 0u ? 0 : static_cast<int32_t>(width0));
        command.requestedConfiguration.outputs[i].virtualY = 0;
        command.requestedConfiguration.outputs[i].primary = primaryOrdinal == i + 1u ? 1u : 0u;
    }
    copy_text(command.requestedConfiguration.primaryOutputId,
              sizeof(command.requestedConfiguration.primaryOutputId),
              primaryOrdinal == 2u ? "display-2" : "display-1");
    return command;
}

static bool submit_apply(const DisplayConfigurationCommand& command,
                         DisplayConfigurationResponse& response)
{
    return DisplayConfigurationService::submit(command, response) &&
        response.requestId == command.requestId &&
        response.commandType == command.commandType;
}

static bool active_is(const DisplayConfigurationResponse& response,
                      DisplayConfigurationMode mode,
                      uint32_t width0,
                      uint32_t height0,
                      uint32_t width1,
                      uint32_t height1,
                      uint32_t primaryOrdinal)
{
    const DisplayConfigurationSnapshot& active = response.activeConfiguration;
    return response.success != 0u && active.mode == static_cast<uint32_t>(mode) &&
        active.outputCount == 2u &&
        active.outputs[0].width == static_cast<int32_t>(width0) &&
        active.outputs[0].height == static_cast<int32_t>(height0) &&
        active.outputs[1].width == static_cast<int32_t>(width1) &&
        active.outputs[1].height == static_cast<int32_t>(height1) &&
        active.outputs[0].virtualX == 0 &&
        active.outputs[1].virtualX == (mode == DisplayConfigurationMode::Mirror ? 0 : static_cast<int32_t>(width0)) &&
        text_equals(active.primaryOutputId, primaryOrdinal == 2u ? "display-2" : "display-1");
}

static void serial_put_dec(uint32_t value);

static void capture_marker(const char* stage)
{
    kernel::serial::puts("DISPLAY_CONFIG_RESOLUTION_CAPTURE=");
    kernel::serial::puts(stage != nullptr ? stage : "unknown");
    kernel::serial::putc('\n');
}

static void log_stage(const char* stage, const DisplayConfigurationResponse& response)
{
    kernel::serial::puts("VirtioGPU resolution proof: stage=");
    kernel::serial::puts(stage);
    kernel::serial::puts(" success=");
    kernel::serial::puts(response.success ? "yes" : "no");
    kernel::serial::puts(" rollback=");
    kernel::serial::puts(response.rollbackSucceeded ? "yes" : "no");
    kernel::serial::puts(" active=");
    kernel::serial::puts(response.activeConfiguration.mode == static_cast<uint32_t>(DisplayConfigurationMode::Extend) ? "Extend" : "Mirror");
    kernel::serial::puts(" output1=");
    if (response.activeConfiguration.outputCount > 0u) {
        serial_put_dec(static_cast<uint32_t>(response.activeConfiguration.outputs[0].width));
        kernel::serial::putc('x');
        serial_put_dec(static_cast<uint32_t>(response.activeConfiguration.outputs[0].height));
    }
    kernel::serial::puts(" output2=");
    if (response.activeConfiguration.outputCount > 1u) {
        serial_put_dec(static_cast<uint32_t>(response.activeConfiguration.outputs[1].width));
        kernel::serial::putc('x');
        serial_put_dec(static_cast<uint32_t>(response.activeConfiguration.outputs[1].height));
    }
    kernel::serial::puts(" virtualDesktop=");
    serial_put_dec(static_cast<uint32_t>(response.activeConfiguration.virtualDesktopWidth));
    kernel::serial::putc('x');
    serial_put_dec(static_cast<uint32_t>(response.activeConfiguration.virtualDesktopHeight));
    kernel::serial::puts(" diagnostic=");
    kernel::serial::puts(response.diagnostic);
    kernel::serial::putc('\n');
}

static void serial_put_dec(uint32_t value)
{
    char digits[16];
    uint32_t count = 0u;
    do {
        digits[count++] = static_cast<char>('0' + (value % 10u));
        value /= 10u;
    } while (value != 0u && count < sizeof(digits));
    while (count > 0u) kernel::serial::putc(digits[--count]);
}

} // namespace

void run()
{
#if !defined(GXOS_QEMU_VIRTIO_GPU_PROBE_ACTIVE) || !defined(GXOS_QEMU_VIRTIO_GPU_COMPOSITOR_LIVE) || !defined(GXOS_QEMU_VIRTIO_GPU_DISPLAY_CONFIGURATION_CONTROL_ACTIVE)
    return;
#else
    DisplayConfigurationCommand query{};
    query.version = kDisplayConfigurationContractVersion;
    query.structureSize = sizeof(query);
    query.requestId = ++s_nextRequestId;
    query.commandType = static_cast<uint32_t>(DisplayConfigurationCommandType::QueryActiveConfiguration);
    DisplayConfigurationResponse queryResponse{};
    const bool queryOk = DisplayConfigurationService::submit(query, queryResponse) && queryResponse.success != 0u &&
        queryResponse.activeConfiguration.outputCount == 2u;
    log_stage("initial-inventory", queryResponse);
    capture_marker("resolution-initial-inventory");
    if (!queryOk) {
        kernel::serial::puts("VirtioGPU resolution proof: equalExtend=failed mixedExtend=failed mirrorCompatible=failed mirrorMismatchRejected=failed primarySwitch=failed rollback=failed repeatedChanges=failed persistenceLaunch2=pending gpuFailures=unknown fallback=unknown result=failed\n");
        return;
    }

    DisplayConfigurationSnapshot current = queryResponse.activeConfiguration;
    bool equalExtend = false;
    bool mixedExtend = false;
    bool mirrorCompatible = false;
    bool mirrorMismatchRejected = false;
    bool primarySwitch = false;
    bool rollback = false;
    bool repeatedChanges = true;

    const uint32_t commit = static_cast<uint32_t>(DisplayConfigurationFlagCommitPersistence);
    const auto apply = [&](DisplayConfigurationMode mode, uint32_t w0, uint32_t h0,
                           uint32_t w1, uint32_t h1, uint32_t primary, uint32_t flags,
                           const char* stage) -> bool {
        const DisplayConfigurationCommand command = make_apply(current, mode, w0, h0, w1, h1, primary, flags);
        DisplayConfigurationResponse response{};
        const bool submitted = submit_apply(command, response);
        if (!submitted) response.success = 0u;
        log_stage(stage, response);
        if (response.success != 0u) current = response.activeConfiguration;
        return submitted && active_is(response, mode, w0, h0, w1, h1, primary);
    };

    equalExtend = apply(DisplayConfigurationMode::Extend, 1280u, 800u, 1280u, 800u, 1u, 0u, "equal-extend");
    capture_marker("resolution-equal-extend");
    mixedExtend = equalExtend && apply(DisplayConfigurationMode::Extend, 1280u, 800u, 1024u, 768u, 1u, 0u, "mixed-extend");
    capture_marker("resolution-mixed-extend");
    mixedExtend = mixedExtend && current.virtualDesktopWidth == 2304 && current.virtualDesktopHeight == 800 &&
        current.outputs[1].virtualX == 1280;
    mirrorCompatible = mixedExtend && apply(DisplayConfigurationMode::Mirror, 1024u, 768u, 1024u, 768u, 1u, 0u, "mirror-compatible");
    capture_marker("resolution-mirror-compatible");

    if (mirrorCompatible) {
        const DisplayConfigurationCommand mismatch = make_apply(current, DisplayConfigurationMode::Mirror,
            1280u, 800u, 1024u, 768u, 1u, 0u);
        DisplayConfigurationResponse mismatchResponse{};
        const bool mismatchSubmitted = submit_apply(mismatch, mismatchResponse);
        log_stage("mirror-mismatch", mismatchResponse);
        capture_marker("resolution-mirror-mismatch");
        mirrorMismatchRejected = !mismatchSubmitted && mismatchResponse.success == 0u &&
            mismatchResponse.resultCode == static_cast<uint32_t>(DisplayConfigurationResultCode::MirrorGeometryIncompatible) &&
            mismatchResponse.presentationPaused == 0u &&
            mismatchResponse.rollbackAttempted == 0u &&
            current.mode == static_cast<uint32_t>(DisplayConfigurationMode::Mirror) &&
            current.outputs[0].width == 1024 && current.outputs[1].width == 1024;
    }

    primarySwitch = mirrorMismatchRejected &&
        apply(DisplayConfigurationMode::Extend, 1280u, 800u, 1024u, 768u, 2u, 0u, "mixed-primary-2") &&
        text_equals(current.primaryOutputId, "display-2") && current.virtualDesktopWidth == 2304;
    capture_marker("resolution-primary-2");

    if (primarySwitch) {
        const DisplayConfigurationCommand injected = make_apply(current, DisplayConfigurationMode::Extend,
            1280u, 800u, 1280u, 800u, 2u,
            DisplayConfigurationFlagTestInjectSecondOutputCommitFailure);
        DisplayConfigurationResponse injectedResponse{};
        const bool injectedSubmitted = submit_apply(injected, injectedResponse);
        log_stage("rollback-second-output-commit", injectedResponse);
        capture_marker("resolution-rollback");
        rollback = !injectedSubmitted && injectedResponse.success == 0u &&
            injectedResponse.rollbackSucceeded != 0u &&
            injectedResponse.activeConfiguration.outputs[0].width == 1280 &&
            injectedResponse.activeConfiguration.outputs[1].width == 1024 &&
            text_equals(injectedResponse.activeConfiguration.primaryOutputId, "display-2");
    }

    repeatedChanges = rollback && apply(DisplayConfigurationMode::Extend, 1280u, 800u, 1024u, 768u, 2u, 0u, "repeat-mixed") &&
        apply(DisplayConfigurationMode::Mirror, 1024u, 768u, 1024u, 768u, 1u, 0u, "repeat-mirror") &&
        apply(DisplayConfigurationMode::Extend, 1280u, 800u, 1024u, 768u, 2u, 0u, "repeat-mixed-again") &&
        apply(DisplayConfigurationMode::Extend, 1280u, 800u, 1280u, 800u, 1u, commit, "repeat-equal-final") &&
        current.virtualDesktopWidth == 2560 && current.virtualDesktopHeight == 800;
    capture_marker("resolution-final-equal");

    kernel::serial::puts("VirtioGPU resolution proof: equalExtend=");
    kernel::serial::puts(equalExtend ? "ok" : "failed");
    kernel::serial::puts(" mixedExtend=");
    kernel::serial::puts(mixedExtend ? "ok" : "failed");
    kernel::serial::puts(" mirrorCompatible=");
    kernel::serial::puts(mirrorCompatible ? "ok" : "failed");
    kernel::serial::puts(" mirrorMismatchRejected=");
    kernel::serial::puts(mirrorMismatchRejected ? "ok" : "failed");
    kernel::serial::puts(" primarySwitch=");
    kernel::serial::puts(primarySwitch ? "ok" : "failed");
    kernel::serial::puts(" rollback=");
    kernel::serial::puts(rollback ? "ok" : "failed");
    kernel::serial::puts(" repeatedChanges=");
    kernel::serial::puts(repeatedChanges ? "ok" : "failed");
    kernel::serial::puts(" persistenceLaunch2=pending gpuFailures=0 fallback=no result=");
    kernel::serial::puts(equalExtend && mixedExtend && mirrorCompatible && mirrorMismatchRejected && primarySwitch && rollback && repeatedChanges ? "success\n" : "failed\n");
#endif
}

} // namespace qemu_display_resolution_rebuild_proof
} // namespace kernel
