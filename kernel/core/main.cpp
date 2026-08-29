//
// guideXOS Minimal Kernel - Entry Point
//
// ROLE: Bridge between bootloader and guideXOS Server (user-mode)
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/kernel/version.h"
#include "include/kernel/build_identity.h"
#include "include/kernel/arch.h"
#include "include/kernel/vga.h"
#include "include/kernel/framebuffer.h"
#include "include/kernel/process.h"
#include "include/kernel/desktop.h"
#include "include/kernel/kernel_apps.h"
#include "include/kernel/kernel_app.h"
#include "include/kernel/kernel_compositor.h"
#include "include/kernel/interrupts.h"
#include "include/kernel/ps2mouse.h"
#include "include/kernel/ps2keyboard.h"
#include "include/kernel/input_manager.h"
#include "include/kernel/pit.h"
#include "include/kernel/serial_debug.h"
#include "include/kernel/desktop_capabilities.h"
#include "include/kernel/app_launch_target_resolver.h"
#include "include/kernel/mmio.h"
#include "include/kernel/qemu_display_input_proof.h"
#include "include/kernel/qemu_display_resolution_rebuild_proof.h"
#include "include/kernel/qemu_display_resolution_persistence_proof.h"
#include "display_configuration_service.h"
#include "include/kernel/file_clipboard.h"
#include "include/kernel/native_elf_baremetal.h"

// Storage subsystem
#include "include/kernel/block_device.h"
#include "include/kernel/ata.h"
#include "include/kernel/nvme.h"
#include "include/kernel/ramdisk.h"
#include "include/kernel/vfs.h"
#include "include/kernel/fs_fat.h"
#include "include/kernel/fs_ext4.h"
#include "include/kernel/fs_ntfs.h"
#include "include/kernel/fs_xfs.h"

// Network subsystem
#include "include/kernel/nic.h"
#include "include/kernel/ethernet.h"
#include "include/kernel/ipv4.h"
#include "include/kernel/icmp.h"
#include "include/kernel/udp.h"
#include "include/kernel/tcp.h"
#include "include/kernel/socket.h"
#include "include/kernel/dns.h"
#include "include/kernel/dhcp.h"

// VirtIO subsystem
#include "include/kernel/virtio_block.h"
#include "include/kernel/virtio_net.h"
#include "include/kernel/virtio_gpu.h"
#include "include/kernel/qemu_display_configuration_control_proof.h"
#include "include/kernel/qemu_display_configuration_persistence_proof.h"
#include "include/kernel/qemu_display_events_proof.h"
#include "include/kernel/secure_random.h"
#include "include/kernel/virtio_rng.h"

// Interrupt support
#include "include/kernel/msi.h"

// Device discovery
#include "include/kernel/device_tree.h"

// Feature reporting
#include "include/kernel/feature_report.h"

#if ARCH_HAS_PIC_8259
#include "include/kernel/multiboot.h"
// Include BootInfo structure from bootloader (x86 / amd64 UEFI only)
#include "../../guideXOSBootLoader/guidexOSBootInfo.h"
#endif

// Existing proof builds historically selected their coordinator through the
// control/persistence flags.  Preserve that behavior while making proof
// activation a distinct internal concept that is never inferred for manual
// mode.
#if !defined(GXOS_QEMU_VIRTIO_GPU_MANUAL_MODE) && \
    (defined(GXOS_QEMU_VIRTIO_GPU_DISPLAY_CONFIGURATION_CONTROL_ACTIVE) || \
     defined(GXOS_QEMU_VIRTIO_GPU_DISPLAY_CONFIGURATION_PERSISTENCE_ACTIVE))
#define GXOS_QEMU_VIRTIO_GPU_PROOF_ACTIVE
#endif

#if defined(ARCH_SPARC)
#include <arch/zs_serial.h>
#endif

#if defined(ARCH_SPARC64)
#include <arch/zs_serial.h>
#endif

#if defined(ARCH_IA64)
#include <arch/ski_console.h>
#endif

#if defined(ARCH_RISCV64)
#include <arch/sbi_console.h>
#include <arch/graphics.h>
#endif

static bool navigator_smoke_mount_path_exists_exact(const char* path)
{
    if (!path) return false;
    const uint8_t mountCount = kernel::vfs::mount_count();
    for (uint8_t index = 0; index < mountCount; ++index) {
        const kernel::vfs::MountPoint* mount = kernel::vfs::get_mount_by_index(index);
        if (!mount || !mount->active) continue;
        const char* mountPath = mount->path;
        if (!mountPath) continue;

        size_t mountLen = 0;
        while (mountPath[mountLen]) ++mountLen;

        size_t pathLen = 0;
        while (path[pathLen]) ++pathLen;

        if (mountLen != pathLen) continue;

        bool same = true;
        for (size_t i = 0; i < mountLen; ++i) {
            if (mountPath[i] != path[i]) {
                same = false;
                break;
            }
        }
        if (same) return true;
    }
    return false;
}

static void mount_navigator_smoke_alias_if_available(const char* aliasPath,
                                                     const char* sourcePath,
                                                     const char* missingMessage,
                                                     const char* alreadyMountedMessage,
                                                     const char* failureMessage,
                                                     const char* successMessage)
{
    kernel::vfs::FileInfo info{};
    const kernel::vfs::Status sourceStatus = kernel::vfs::stat(sourcePath, &info);
    if (sourceStatus != kernel::vfs::VFS_OK || info.type != kernel::vfs::FILE_TYPE_DIRECTORY) {
        kernel::serial::puts(missingMessage);
        return;
    }

    if (navigator_smoke_mount_path_exists_exact(aliasPath)) {
        kernel::serial::puts(alreadyMountedMessage);
        return;
    }

    if (kernel::vfs::mount_alias(aliasPath, sourcePath) == 0xFF) {
        kernel::serial::puts(failureMessage);
        return;
    }

    kernel::serial::puts(successMessage);
}

static void mount_navigator_boot_ca_store_if_available()
{
    mount_navigator_smoke_alias_if_available(
        "/certs",
        "/system/certs",
        "[KERNEL] Navigator boot CA source directory unavailable at /system/certs\n",
        "[KERNEL] Navigator /certs mount already active\n",
        "[KERNEL] Navigator failed to mount /certs from /system/certs\n",
        "[KERNEL] Navigator mounted /certs from boot ramdisk path /system/certs\n");
}

static void mount_navigator_boot_config_if_available()
{
    mount_navigator_smoke_alias_if_available(
        "/config",
        "/system/config",
        "[KERNEL] Navigator boot config source directory unavailable at /system/config\n",
        "[KERNEL] Navigator /config mount already active\n",
        "[KERNEL] Navigator failed to mount /config from /system/config\n",
        "[KERNEL] Navigator mounted /config from boot ramdisk path /system/config\n");
    mount_navigator_smoke_alias_if_available(
        "/config/certs",
        "/system/config/certs",
        "[KERNEL] Navigator boot config certs directory unavailable at /system/config/certs\n",
        "[KERNEL] Navigator /config/certs mount already active\n",
        "[KERNEL] Navigator failed to mount /config/certs from /system/config/certs\n",
        "[KERNEL] Navigator mounted /config/certs from boot ramdisk path /system/config/certs\n");
    mount_navigator_smoke_alias_if_available(
        "/config/navigator",
        "/system/config/navigator",
        "[KERNEL] Navigator boot config navigator directory unavailable at /system/config/navigator\n",
        "[KERNEL] Navigator /config/navigator mount already active\n",
        "[KERNEL] Navigator failed to mount /config/navigator from /system/config/navigator\n",
        "[KERNEL] Navigator mounted /config/navigator from boot ramdisk path /system/config/navigator\n");
}

#if ARCH_HAS_PIC_8259
namespace {

static void serial_put_u32_decimal(uint32_t value)
{
    char buffer[11];
    int index = 10;
    buffer[index] = '\0';

    do {
        buffer[--index] = static_cast<char>('0' + (value % 10u));
        value /= 10u;
    } while (value != 0u && index > 0);

    kernel::serial::puts(&buffer[index]);
}

#if defined(GXOS_QEMU_VIRTIO_GPU_MANUAL_VALIDATION_ACTIVE)
static bool manual_logical_mode_supported(int32_t width, int32_t height)
{
    return (width == 1280 && height == 800) ||
           (width == 1024 && height == 768) ||
           (width == 800 && height == 600);
}

static void manual_copy_text(char* destination, uint32_t capacity, const char* source)
{
    if (destination == nullptr || capacity == 0u) return;
    uint32_t index = 0u;
    if (source != nullptr) {
        while (source[index] != '\0' && index + 1u < capacity) {
            destination[index] = source[index];
            ++index;
        }
    }
    destination[index] = '\0';
}

static bool manual_active_configuration_has_valid_modes(
    const gxos::display::DisplayConfigurationSnapshot& snapshot)
{
    if (snapshot.outputCount != 2u) return false;
    for (uint32_t index = 0u; index < 2u; ++index) {
        const gxos::display::DisplayConfigurationOutput& output = snapshot.outputs[index];
        if (output.enabled == 0u || !manual_logical_mode_supported(output.width, output.height)) {
            return false;
        }
    }
    return true;
}

static bool initialize_manual_logical_configuration()
{
    gxos::display::DisplayConfigurationCommand query{};
    query.version = gxos::display::kDisplayConfigurationContractVersion;
    query.structureSize = sizeof(query);
    query.requestId = gxos::display::DisplayConfigurationService::nextRequestId();
    query.commandType = static_cast<uint32_t>(gxos::display::DisplayConfigurationCommandType::QueryActiveConfiguration);
    gxos::display::DisplayConfigurationResponse activeResponse{};
    const bool queried = gxos::display::DisplayConfigurationService::submit(query, activeResponse) &&
        activeResponse.success != 0u;
    if (!queried || activeResponse.activeConfiguration.outputCount != 2u) {
        kernel::serial::puts("[QEMU-MANUAL] logical configuration initialization=blocked reason=two operational outputs are not active\n");
        return false;
    }

    if (manual_active_configuration_has_valid_modes(activeResponse.activeConfiguration) &&
        activeResponse.persistedLoaded != 0u) {
        kernel::serial::puts("[QEMU-MANUAL] logical configuration source=persistence-or-active result=preserved\n");
        return true;
    }

    if (manual_active_configuration_has_valid_modes(activeResponse.activeConfiguration)) {
        kernel::serial::puts("[QEMU-MANUAL] logical configuration source=validated-default result=committing-through-public-service\n");
    }

    gxos::display::DisplayConfigurationCommand apply{};
    apply.version = gxos::display::kDisplayConfigurationContractVersion;
    apply.structureSize = sizeof(apply);
    apply.requestId = gxos::display::DisplayConfigurationService::nextRequestId();
    apply.commandType = static_cast<uint32_t>(gxos::display::DisplayConfigurationCommandType::ApplyConfiguration);
    apply.flags = gxos::display::DisplayConfigurationFlagCommitPersistence;
    apply.origin = static_cast<uint32_t>(gxos::display::DisplayConfigurationRequestOrigin::UserApply);
    apply.requestedConfiguration.mode = static_cast<uint32_t>(gxos::display::DisplayConfigurationMode::Extend);
    apply.requestedConfiguration.outputCount = 2u;
    manual_copy_text(apply.requestedConfiguration.primaryOutputId,
                     sizeof(apply.requestedConfiguration.primaryOutputId),
                     activeResponse.activeConfiguration.primaryOutputId);

    bool primaryFound = false;
    for (uint32_t index = 0u; index < 2u; ++index) {
        apply.requestedConfiguration.outputs[index] = activeResponse.activeConfiguration.outputs[index];
        gxos::display::DisplayConfigurationOutput& output = apply.requestedConfiguration.outputs[index];
        output.width = 1280;
        output.height = 800;
        output.virtualX = index == 0u ? 0 : 1280;
        output.virtualY = 0;
        manual_copy_text(output.modeId, sizeof(output.modeId), "qemu-1280x800");
        output.enabled = 1u;
        output.primary = (!primaryFound && activeResponse.activeConfiguration.outputs[index].primary != 0u) ? 1u : 0u;
        if (output.primary != 0u) primaryFound = true;
    }
    if (!primaryFound) {
        apply.requestedConfiguration.outputs[0].primary = 1u;
        manual_copy_text(apply.requestedConfiguration.primaryOutputId,
                         sizeof(apply.requestedConfiguration.primaryOutputId),
                         apply.requestedConfiguration.outputs[0].stableId);
    }

    gxos::display::DisplayConfigurationResponse response{};
    const bool submitted = gxos::display::DisplayConfigurationService::submit(apply, response);
    const bool configurationReady = submitted && response.success != 0u &&
        response.activeConfiguration.outputCount == 2u &&
        response.activeConfiguration.outputs[0].width == 1280 &&
        response.activeConfiguration.outputs[0].height == 800 &&
        response.activeConfiguration.outputs[1].width == 1280 &&
        response.activeConfiguration.outputs[1].height == 800;
    kernel::serial::puts("[QEMU-MANUAL] logical configuration initialization=public-service result=");
    kernel::serial::puts(configurationReady ? "ready" : "blocked");
    kernel::serial::puts(" persistenceCommitted=");
    kernel::serial::puts(response.persistenceCommitted != 0u ? "yes" : "no");
    kernel::serial::puts(" diagnostic=");
    kernel::serial::puts(response.diagnostic[0] != '\0' ? response.diagnostic : "none");
    kernel::serial::putc('\n');
    return configurationReady;
}

static bool verify_manual_display_options_launch()
{
    if (!kernel::app::AppManager::isAppAvailable("DisplayOptions")) return false;

    kernel::app::KernelApp* existing = nullptr;
    for (int index = 0; index < kernel::app::AppManager::getRunningAppCount(); ++index) {
        kernel::app::KernelApp* app = kernel::app::AppManager::getRunningApp(index);
        if (app != nullptr && app->getName() != nullptr &&
            app->getName()[0] == 'D' && app->getName()[1] == 'i') {
            existing = app;
            break;
        }
    }
    if (existing != nullptr) return existing->getWindow() != nullptr;

    const int windowsBefore = kernel::compositor::KernelCompositor::getWindowCount();
    if (!kernel::desktop::launch_app("DisplayOptions")) return false;
    kernel::app::KernelApp* launched = nullptr;
    for (int index = 0; index < kernel::app::AppManager::getRunningAppCount(); ++index) {
        kernel::app::KernelApp* app = kernel::app::AppManager::getRunningApp(index);
        if (app != nullptr && app->getName() != nullptr &&
            app->getName()[0] == 'D' && app->getName()[1] == 'i') {
            launched = app;
            break;
        }
    }
    const bool createdWindow = launched != nullptr && launched->getWindow() != nullptr &&
        kernel::compositor::KernelCompositor::getWindowCount() > windowsBefore;
    if (launched != nullptr) kernel::app::AppManager::closeApp(launched);
    return createdWindow;
}

static bool handle_manual_topology_control_key(uint32_t key)
{
    // PS/2 F9..F12 are 0x118..0x11B in ps2keyboard.cpp.  These are explicit
    // QEMU-only test controls; they never claim genuine host hotplug.
    uint32_t injectedKind = 0u;
    const char* label = nullptr;
    if (key == 0x118u) {
        injectedKind = static_cast<uint32_t>(gxos::display::VirtioGpuInjectedTopologyChangeKind::OutputRemoval);
        label = "secondary-removal-injected";
    } else if (key == 0x119u) {
        injectedKind = static_cast<uint32_t>(gxos::display::VirtioGpuInjectedTopologyChangeKind::OutputAddition);
        label = "secondary-restoration-injected";
    } else if (key == 0x11Au) {
        injectedKind = static_cast<uint32_t>(gxos::display::VirtioGpuInjectedTopologyChangeKind::PreferredGeometry);
        label = "preferred-geometry-change-injected";
    } else if (key == 0x11Bu) {
        gxos::display::DisplayConfigurationCommand query{};
        query.version = gxos::display::kDisplayConfigurationContractVersion;
        query.structureSize = sizeof(query);
        query.requestId = gxos::display::DisplayConfigurationService::nextRequestId();
        query.commandType = static_cast<uint32_t>(gxos::display::DisplayConfigurationCommandType::QueryPendingTopologyChange);
        gxos::display::DisplayConfigurationResponse pending{};
        if (!gxos::display::DisplayConfigurationService::submit(query, pending) ||
            pending.pendingTopology == 0u) {
            kernel::serial::puts("[QEMU-MANUAL] topology control=clear-pending result=no-pending-state\n");
            return true;
        }
        gxos::display::DisplayConfigurationCommand dismiss{};
        dismiss.version = gxos::display::kDisplayConfigurationContractVersion;
        dismiss.structureSize = sizeof(dismiss);
        dismiss.requestId = gxos::display::DisplayConfigurationService::nextRequestId();
        dismiss.commandType = static_cast<uint32_t>(gxos::display::DisplayConfigurationCommandType::DismissPendingTopologyChange);
        dismiss.origin = static_cast<uint32_t>(gxos::display::DisplayConfigurationRequestOrigin::UserApply);
        dismiss.topologyGeneration = pending.detectedTopologyChange.topologyGeneration;
        gxos::display::DisplayConfigurationResponse response{};
        const bool cleared = gxos::display::DisplayConfigurationService::submit(dismiss, response) && response.success != 0u;
        kernel::serial::puts("[QEMU-MANUAL] topology control=clear-pending result=");
        kernel::serial::puts(cleared ? "cleared" : "blocked");
        kernel::serial::puts(" injectedEvent=no\n");
        return true;
    } else {
        return false;
    }

    const bool injected = kernel::virtio::gpu::inject_display_topology_change_for_test(injectedKind);
    kernel::serial::puts("[QEMU-MANUAL] topology control=");
    kernel::serial::puts(label);
    kernel::serial::puts(" result=");
    kernel::serial::puts(injected ? "pending" : "blocked");
    kernel::serial::puts(" injectedEvent=yes automaticApply=no genuineDeviceEvent=no\n");
    return true;
}

static void log_manual_dual_monitor_validation_banner()
{
    gxos::display::DisplayConfigurationCommand command{};
    command.version = gxos::display::kDisplayConfigurationContractVersion;
    command.structureSize = sizeof(command);
    command.requestId = gxos::display::DisplayConfigurationService::nextRequestId();
    command.commandType = static_cast<uint32_t>(gxos::display::DisplayConfigurationCommandType::QueryActiveConfiguration);
    gxos::display::DisplayConfigurationResponse response{};
    const bool queried = gxos::display::DisplayConfigurationService::submit(command, response) &&
        response.success != 0u;

    kernel::serial::puts("Manual dual-monitor validation: backend=virtio-gpu outputs=");
    if (!queried) {
        kernel::serial::puts("unknown mode=unknown primary=unknown resolutions=unknown virtualDesktop=unknown");
    } else {
        serial_put_u32_decimal(response.activeConfiguration.outputCount);
        kernel::serial::puts(" mode=");
        kernel::serial::puts(response.activeConfiguration.mode ==
            static_cast<uint32_t>(gxos::display::DisplayConfigurationMode::Extend) ? "Extend" : "Mirror");
        kernel::serial::puts(" primary=");
        uint32_t primary = 0u;
        for (uint32_t index = 0u; index < response.activeConfiguration.outputCount; ++index) {
            if (response.activeConfiguration.outputs[index].primary != 0u) {
                primary = index + 1u;
                break;
            }
        }
        serial_put_u32_decimal(primary);
        kernel::serial::puts(" resolutions=");
        for (uint32_t index = 0u; index < response.activeConfiguration.outputCount; ++index) {
            if (index != 0u) kernel::serial::putc(',');
            kernel::serial::puts("Display ");
            serial_put_u32_decimal(index + 1u);
            kernel::serial::putc(':');
            serial_put_u32_decimal(static_cast<uint32_t>(response.activeConfiguration.outputs[index].width));
            kernel::serial::putc('x');
            serial_put_u32_decimal(static_cast<uint32_t>(response.activeConfiguration.outputs[index].height));
        }
        kernel::serial::puts(" virtualDesktop=");
        serial_put_u32_decimal(static_cast<uint32_t>(response.activeConfiguration.virtualDesktopWidth));
        kernel::serial::putc('x');
        serial_put_u32_decimal(static_cast<uint32_t>(response.activeConfiguration.virtualDesktopHeight));
    }
    kernel::serial::puts(" persistence=/display.cfg topologyTestControls=enabled=yes");
    kernel::serial::puts(" topologyInjectionAvailable=yes automaticProof=disabled realHardware=no");
    kernel::serial::puts(" displayOptionsRegistered=yes displayOptionsLaunchPath=normal-app-model\n");
}

static bool emit_manual_readiness_report(bool persistenceReady)
{
    kernel::virtio::gpu::VirtioGpuDisplayReadiness backendReadiness{};
    const bool backendQueried = kernel::virtio::gpu::get_display_readiness(&backendReadiness);
    gxos::display::DisplayConfigurationCommand query{};
    query.version = gxos::display::kDisplayConfigurationContractVersion;
    query.structureSize = sizeof(query);
    query.requestId = gxos::display::DisplayConfigurationService::nextRequestId();
    query.commandType = static_cast<uint32_t>(gxos::display::DisplayConfigurationCommandType::QueryActiveConfiguration);
    gxos::display::DisplayConfigurationResponse active{};
    const bool activeReady = gxos::display::DisplayConfigurationService::submit(query, active) && active.success != 0u;
    const bool desktopReady = kernel::desktop::is_initialized() && kernel::desktop::is_compositor_available();
    const bool displayOptionsRegistered = kernel::app::AppManager::isAppAvailable("DisplayOptions");
    const bool displayOptionsLaunchable = displayOptionsRegistered && verify_manual_display_options_launch();
    const bool logicalModesValid = activeReady && manual_active_configuration_has_valid_modes(active.activeConfiguration);
    const bool outputsReady = backendQueried && backendReadiness.operationalOutputCount == 2u &&
        backendReadiness.displayMonitorCount == 2u && backendReadiness.displayRenderTargetCount == 2u &&
        activeReady && active.activeConfiguration.outputCount == 2u;
    const bool ready = backendReadiness.backendInitialized != 0u && outputsReady && desktopReady &&
        displayOptionsRegistered && displayOptionsLaunchable && logicalModesValid &&
        backendReadiness.presenterActive != 0u && persistenceReady &&
        backendReadiness.topologyControlsAvailable != 0u;

    kernel::serial::puts("[QEMU-MANUAL] DisplayMonitor count=");
    serial_put_u32_decimal(backendReadiness.displayMonitorCount);
    kernel::serial::puts(" DisplayRenderTarget count=");
    serial_put_u32_decimal(backendReadiness.displayRenderTargetCount);
    kernel::serial::puts(" presenter=");
    kernel::serial::puts(backendReadiness.presenterActive != 0u ? "live" : "inactive");
    kernel::serial::puts(" logicalModes=");
    kernel::serial::puts(logicalModesValid ? "valid" : "invalid");
    kernel::serial::puts(" virtualDesktop=");
    serial_put_u32_decimal(activeReady ? static_cast<uint32_t>(active.activeConfiguration.virtualDesktopWidth) : 0u);
    kernel::serial::putc('x');
    serial_put_u32_decimal(activeReady ? static_cast<uint32_t>(active.activeConfiguration.virtualDesktopHeight) : 0u);
    kernel::serial::puts(" hostGtkWindowSize=host-managed-qemu-gtk resourceDimensions=guest-reported logicalActiveResolution=service-reported\n");

    kernel::serial::puts("Manual dual-monitor readiness: backend=virtio-gpu outputs=");
    serial_put_u32_decimal(backendReadiness.operationalOutputCount);
    kernel::serial::puts(" desktop=");
    kernel::serial::puts(desktopReady ? "ready" : "blocked");
    kernel::serial::puts(" shell=");
    kernel::serial::puts(desktopReady ? "ready" : "blocked");
    kernel::serial::puts(" startMenu=");
    kernel::serial::puts(desktopReady ? "ready" : "blocked");
    kernel::serial::puts(" displayOptionsRegistered=");
    kernel::serial::puts(displayOptionsRegistered ? "yes" : "no");
    kernel::serial::puts(" displayOptionsLaunchable=");
    kernel::serial::puts(displayOptionsLaunchable ? "yes" : "no");
    kernel::serial::puts(" presenter=");
    kernel::serial::puts(backendReadiness.presenterActive != 0u ? "live" : "inactive");
    kernel::serial::puts(" proofCoordinator=disabled persistence=");
    kernel::serial::puts(persistenceReady ? "ready" : "blocked");
    kernel::serial::puts(" topologyTestControls=");
    kernel::serial::puts(backendReadiness.topologyControlsAvailable != 0u ? "yes" : "no");
    kernel::serial::puts(" realHardware=no result=");
    kernel::serial::puts(ready ? "ready\n" : "blocked\n");
    if (!ready) {
        kernel::serial::puts("[QEMU-MANUAL] readiness blocker=");
        if (!backendReadiness.backendInitialized) kernel::serial::puts("backend-not-initialized");
        else if (!outputsReady) kernel::serial::puts("two-operational-outputs-or-target-inventory-unavailable");
        else if (!desktopReady) kernel::serial::puts("normal-desktop-not-initialized");
        else if (!displayOptionsRegistered) kernel::serial::puts("DisplayOptions-not-registered");
        else if (!displayOptionsLaunchable) kernel::serial::puts("DisplayOptions-window-launch-failed");
        else if (!logicalModesValid) kernel::serial::puts("active-logical-modes-invalid");
        else if (!backendReadiness.presenterActive) kernel::serial::puts("live-presenter-inactive");
        else if (!persistenceReady) kernel::serial::puts("persistent-storage-not-writable");
        else kernel::serial::puts("topology-test-controls-unavailable");
        kernel::serial::putc('\n');
    }
    return ready;
}
#endif

static const char* framebuffer_format_name(guideXOS::FramebufferFormat format)
{
    switch (format) {
    case guideXOS::FramebufferFormat::R8G8B8A8: return "R8G8B8A8";
    case guideXOS::FramebufferFormat::B8G8R8A8: return "B8G8R8A8";
    default: return "Unknown";
    }
}

static const char* multiboot_framebuffer_format_name(const kernel::multiboot::Info* info)
{
    if (!info) {
        return "Unknown";
    }

    switch (info->framebuffer_type) {
    case kernel::multiboot::FRAMEBUFFER_TYPE_RGB:
        switch (info->framebuffer_bpp) {
        case 32: return "RGB32";
        case 24: return "RGB24";
        case 16: return "RGB565";
        default: return "RGB-direct";
        }
    case kernel::multiboot::FRAMEBUFFER_TYPE_INDEXED:
        return "Indexed";
    case kernel::multiboot::FRAMEBUFFER_TYPE_EGA_TEXT:
        return "Text";
    default:
        return "Unknown";
    }
}

static void log_framebuffer_descriptor(
    const char* source,
    uint32_t framebufferCount,
    uint32_t index,
    uint32_t descriptorFlags,
    uint64_t base,
    uint64_t size,
    uint32_t width,
    uint32_t height,
    uint32_t pitch,
    uint32_t bpp,
    const char* format)
{
    kernel::serial::puts("[KERNEL] Framebuffer source=");
    kernel::serial::puts(source ? source : "(unknown)");
    kernel::serial::puts(" framebufferCount=");
    serial_put_u32_decimal(framebufferCount);
    kernel::serial::puts(" index=");
    serial_put_u32_decimal(index);

    kernel::serial::puts(" status=");
    if (descriptorFlags & guideXOS::FRAMEBUFFER_DESCRIPTOR_FLAG_DUPLICATE) {
        kernel::serial::puts("duplicate");
    } else {
        kernel::serial::puts("canonical");
    }
    if (descriptorFlags & guideXOS::FRAMEBUFFER_DESCRIPTOR_FLAG_ALIAS) {
        kernel::serial::puts(" alias");
    }
    if (descriptorFlags & guideXOS::FRAMEBUFFER_DESCRIPTOR_FLAG_SAME_AS_PRIMARY) {
        kernel::serial::puts(" same-as-primary");
    }
    if (descriptorFlags & guideXOS::FRAMEBUFFER_DESCRIPTOR_FLAG_SUSPICIOUS) {
        kernel::serial::puts(" suspicious");
    }
    if (descriptorFlags & guideXOS::FRAMEBUFFER_DESCRIPTOR_FLAG_PRIMARY) {
        kernel::serial::puts(" primary");
    }
    if (descriptorFlags & guideXOS::FRAMEBUFFER_DESCRIPTOR_FLAG_SELECTED) {
        kernel::serial::puts(" selected");
    }
    kernel::serial::puts(" base=");
    kernel::serial::put_hex64(base);
    kernel::serial::puts(" size=");
    kernel::serial::put_hex64(size);
    kernel::serial::puts(" geometry=");
    serial_put_u32_decimal(width);
    kernel::serial::putc('x');
    serial_put_u32_decimal(height);
    kernel::serial::puts(" pitch=");
    serial_put_u32_decimal(pitch);
    kernel::serial::puts(" bpp=");
    serial_put_u32_decimal(bpp);
    kernel::serial::puts(" format=");
    kernel::serial::puts(format ? format : "(unknown)");
    kernel::serial::puts("\n");
}

static void log_framebuffer_summary(
    uint32_t framebufferCount,
    uint32_t uniqueFramebufferCount,
    uint32_t duplicateFramebufferCount,
    uint32_t suspiciousFramebufferCount,
    uint32_t activeFramebufferTargetCount,
    uint32_t disabledFramebufferCandidateCount)
{
    kernel::serial::puts("[KERNEL] FramebufferCount=");
    serial_put_u32_decimal(framebufferCount);
    kernel::serial::puts(" UniqueFramebufferCount=");
    serial_put_u32_decimal(uniqueFramebufferCount);
    kernel::serial::puts(" DuplicateFramebufferCount=");
    serial_put_u32_decimal(duplicateFramebufferCount);
    kernel::serial::puts(" SuspiciousFramebufferCount=");
    serial_put_u32_decimal(suspiciousFramebufferCount);
    kernel::serial::puts(" ActiveFramebufferTargetCount=");
    serial_put_u32_decimal(activeFramebufferTargetCount);
    kernel::serial::puts(" DisabledDiagnosticFramebufferCandidateCount=");
    serial_put_u32_decimal(disabledFramebufferCandidateCount);
    kernel::serial::puts("\n");
}

} // namespace
#endif

#if defined(GXOS_QEMU_VIRTIO_GPU_DISPLAY_CONFIGURATION_CONTROL_ACTIVE) || defined(GXOS_QEMU_VIRTIO_GPU_DISPLAY_CONFIGURATION_PERSISTENCE_ACTIVE)
static bool initialize_qemu_display_configuration_storage()
{
    kernel::serial::puts("[QEMU-PERSISTENCE] initializing writable display configuration store\n");
    kernel::feature_report::init();
    kernel::block::init();
    kernel::fs_fat::init();
    kernel::msi::init();
    kernel::virtio::block::init();
    kernel::ata::init();
    kernel::nvme::init();
    kernel::vfs::init();

    bool mounted = kernel::vfs::get_mount("/") != nullptr;
    for (uint8_t index = 0u; !mounted && index < kernel::block::device_count(); ++index) {
        if (kernel::vfs::mount("/", index) != 0xFF) mounted = true;
    }
    kernel::serial::puts("[QEMU-PERSISTENCE] config store mount=");
    kernel::serial::puts(mounted ? "ready" : "unavailable");
    kernel::serial::puts(" writable=expected artifact=ESP\n");
    return mounted;
}
#endif

static bool is_transient_block_device(const kernel::block::BlockDevice* device)
{
    if (!device || !device->name[0]) return true;
    if (device->name[0] == 'r' && device->name[1] == 'a' && device->name[2] == 'm') return true;
    if (device->name[0] == 'w' && device->name[1] == 'a' && device->name[2] == 'l' &&
        device->name[3] == 'l' && device->name[4] == 'i' && device->name[5] == 'm' &&
        device->name[6] == 'g' && device->name[7] == '\0') return true;
    return false;
}

static bool mount_persistent_storage()
{
    if (kernel::vfs::get_mount("/")) {
        kernel::serial::puts("[KERNEL] Persistent storage already mounted at /\n");
        return true;
    }

    for (uint8_t index = 0; index < kernel::block::MAX_BLOCK_DEVICES; ++index) {
        const kernel::block::BlockDevice* device = kernel::block::get_device(index);
        if (!device || is_transient_block_device(device) || !device->readFn || !device->writeFn) continue;

        kernel::serial::puts("[KERNEL] Trying persistent storage device ");
        kernel::serial::puts(device->name);
        kernel::serial::puts(" at /\n");
        const uint8_t mountResult = kernel::vfs::mount("/", index);
        if (mountResult != 0xFF) {
            kernel::serial::puts("[KERNEL] Successfully mounted persistent storage from ");
            kernel::serial::puts(device->name);
            kernel::serial::puts("\n");
            return true;
        }
    }

    kernel::serial::puts("[KERNEL] WARNING: No writable persistent storage filesystem found\n");
    return false;
}

extern "C" void kernel_main(void* boot_environment, uint32_t boot_magic)
{
#if ARCH_HAS_PIC_8259
    // ============================================================
    // x86 / amd64 boot path  â€”  Multiboot (BIOS) or BootInfo (UEFI)
    // ============================================================

    // Initialize serial debug output early
    kernel::serial::init();
    kernel::serial::puts("[GXOS-BUILD] identity=");
    kernel::serial::puts(GXOS_BUILD_IDENTITY);
    kernel::serial::puts(" probe=");
    kernel::serial::puts(GXOS_BUILD_PROBE_ID);
    kernel::serial::puts("\n");
    kernel::serial::puts("[KERNEL] guideXOS kernel_main entered\n");
#if defined(GXOS_DESKTOP_CLEANUP_RUNTIME_PASS)
    kernel::serial::puts("[KERNEL] desktopCleanupRuntimePass=2\n");
#endif

    // Support both Multiboot (legacy) and BootInfo (UEFI) boot
    bool is_multiboot = (boot_magic == 0x2BADB002);
    bool is_bootinfo = false;
    
    guideXOS::BootInfo* bootinfo = nullptr;
    kernel::multiboot::Info* multiboot_info = nullptr;
    
    if (is_multiboot) {
        multiboot_info = static_cast<kernel::multiboot::Info*>(boot_environment);
        kernel::serial::puts("[KERNEL] Boot method: Multiboot\n");
    } else {
        bootinfo = static_cast<guideXOS::BootInfo*>(boot_environment);
        if (bootinfo && bootinfo->Magic == guideXOS::GUIDEXOS_BOOTINFO_MAGIC) {
            is_bootinfo = true;
            kernel::serial::puts("[KERNEL] Boot method: UEFI BootInfo\n");
            kernel::mmio::set_kernel_physical_base(bootinfo->KernelPhysicalBase);
            kernel::nic::set_kernel_physical_base(bootinfo->KernelPhysicalBase);
            kernel::virtio::rng::set_kernel_physical_base(bootinfo->KernelPhysicalBase);
            kernel::virtio::gpu::set_kernel_physical_base(bootinfo->KernelPhysicalBase);
        }
    }
    
    // If neither boot method is valid, halt
    if (!is_multiboot && !is_bootinfo) {
        kernel::serial::puts("[KERNEL] ERROR: No valid boot method detected, halting\n");
        while(1) { }
    }
    
    // Initialize framebuffer for graphics mode
    bool has_fb = false;
#if defined(GXOS_QEMU_VIRTIO_GPU_MANUAL_VALIDATION_ACTIVE)
    bool manualPersistenceReady = false;
    bool manualReadinessReported = false;
#endif
    
    if (is_bootinfo) {
        has_fb = kernel::framebuffer::init_from_bootinfo(bootinfo);
    } else {
        has_fb = kernel::framebuffer::init(multiboot_info);
    }

    // Diagnostic-only virtio-gpu probe runs regardless of framebuffer
    // handoff success so QEMU display discovery logs are still captured
    // on GOP/BootInfo paths that do not expose a usable framebuffer array.
    kernel::virtio::gpu::init();

    bool qemuManualVirtualFramebuffer = false;
#if defined(GXOS_QEMU_VIRTIO_GPU_MANUAL_VALIDATION_ACTIVE)
    if (!has_fb) {
        // VirtIO-GPU owns the visible scanouts in this explicitly QEMU-only
        // validation build.  Give the normal desktop lifecycle the bounded
        // software canvas it expects; the live presenter copies this canvas
        // into the already-active VirtIO-GPU resources.
        qemuManualVirtualFramebuffer = kernel::framebuffer::init_virtual(2560u, 800u);
        has_fb = qemuManualVirtualFramebuffer;
        kernel::serial::puts("[QEMU-MANUAL] softwareCanvas=2560x800 hostGtkWindowSize=host-managed-qemu-gtk realHardware=no\n");
    }
#endif
    
    if (has_fb) {
        if (qemuManualVirtualFramebuffer) {
            kernel::serial::puts("[KERNEL] QEMU-only VirtIO-GPU manual virtual framebuffer ready; firmware GOP remains disabled\n");
        } else if (is_bootinfo && bootinfo) {
            uint32_t framebufferCount = bootinfo->FramebufferCount;
            if (framebufferCount > guideXOS::GUIDEXOS_MAX_FRAMEBUFFERS) {
                kernel::serial::puts("[KERNEL] WARNING: BootInfo framebufferCount exceeds array bound; truncating log\n");
                framebufferCount = guideXOS::GUIDEXOS_MAX_FRAMEBUFFERS;
            }

            const kernel::framebuffer::DiagnosticFramebufferInventorySummary& framebufferInventory =
                kernel::framebuffer::diagnostic_framebuffer_inventory_summary();
            log_framebuffer_summary(
                framebufferInventory.RawCount,
                framebufferInventory.UniqueCount,
                framebufferInventory.DuplicateCount,
                framebufferInventory.SuspiciousCount,
                framebufferInventory.ActiveRenderTargetCount,
                framebufferInventory.DisabledCandidateCount);

            if (framebufferCount == 0u) {
                kernel::serial::puts("[KERNEL] WARNING: BootInfo framebufferCount=0; logging primary framebuffer fields only\n");
                log_framebuffer_descriptor(
                    "UEFI BootInfo",
                    0u,
                    0u,
                    guideXOS::FRAMEBUFFER_DESCRIPTOR_FLAG_PRIMARY | guideXOS::FRAMEBUFFER_DESCRIPTOR_FLAG_SELECTED,
                    bootinfo->FramebufferBase,
                    bootinfo->FramebufferSize,
                    bootinfo->FramebufferWidth,
                    bootinfo->FramebufferHeight,
                    bootinfo->FramebufferPitch,
                    kernel::framebuffer::get_bpp(),
                    framebuffer_format_name(bootinfo->FramebufferFormat));
            } else {
                for (uint32_t i = 0; i < framebufferCount; ++i) {
                    const guideXOS::FramebufferDescriptor& descriptor = bootinfo->FramebufferDescriptors[i];
                    log_framebuffer_descriptor(
                        "UEFI BootInfo",
                        framebufferCount,
                        i,
                        descriptor.Flags,
                        descriptor.Base,
                        descriptor.Size,
                        descriptor.Width,
                        descriptor.Height,
                        descriptor.Pitch,
                        descriptor.BitsPerPixel,
                        framebuffer_format_name(descriptor.Format));
                }
            }

            // TODO: once bare-metal multi-target rendering is enabled, feed this
            // diagnostic descriptor list into the compositor's multi-framebuffer
            // render/present path; primary remains render target for now.
        } else {
            log_framebuffer_descriptor(
                "Multiboot",
                1u,
                0u,
                guideXOS::FRAMEBUFFER_DESCRIPTOR_FLAG_PRIMARY | guideXOS::FRAMEBUFFER_DESCRIPTOR_FLAG_SELECTED,
                reinterpret_cast<uint64_t>(kernel::framebuffer::get_buffer()),
                static_cast<uint64_t>(kernel::framebuffer::get_pitch()) * static_cast<uint64_t>(kernel::framebuffer::get_height()),
                kernel::framebuffer::get_width(),
                kernel::framebuffer::get_height(),
                kernel::framebuffer::get_pitch(),
                kernel::framebuffer::get_bpp(),
                multiboot_framebuffer_format_name(multiboot_info));
        }

        // === GRAPHICS MODE BOOT ===
        kernel::serial::puts("[KERNEL] Framebuffer ready\n");
        
        // Clear screen to dark color
        kernel::framebuffer::clear(0xFF101828);
        
        // Initialize desktop and draw immediately (skip boot splash)
        kernel::desktop::init();
        kernel::desktop::draw();
        kernel::serial::puts("[KERNEL] Desktop drawn\n");
        
        // Set up IDT, remap PIC, enable interrupts
        kernel::interrupts::init();
        kernel::serial::puts("[KERNEL] IDT + PIC initialized, interrupts enabled\n");
        
        // Initialize PIT timer for periodic IRQ0 (100 Hz heartbeat)
        // This ensures the CPU wakes from HLT regularly to poll input.
        kernel::pit::init(100);
        kernel::interrupts::register_irq(0, kernel::pit::irq_handler);
        kernel::serial::puts("[KERNEL] PIT timer initialized, IRQ0 registered\n");
        
        // ============================================================
        // Storage Subsystem Initialization
        // ============================================================
        kernel::serial::puts("[KERNEL] Initializing storage subsystem...\n");
        
        // Initialize feature reporting
        kernel::feature_report::init();
        
        // Initialize block device layer
        kernel::block::init();
        kernel::serial::puts("[KERNEL] Block device layer initialized\n");
        
        // Initialize filesystem drivers
        kernel::fs_fat::init();
        kernel::feature_report::complete_init(kernel::feature_report::FS_FAT32);
        
        kernel::fs_ext4::init();
        kernel::feature_report::complete_init(kernel::feature_report::FS_EXT4);
        kernel::feature_report::complete_init(kernel::feature_report::FS_EXT2);
        
        // Initialize NTFS driver
        kernel::fs_ntfs::init();
        kernel::feature_report::complete_init(kernel::feature_report::FS_NTFS);
        
        // Initialize XFS driver
        kernel::fs_xfs::init();
        kernel::feature_report::complete_init(kernel::feature_report::FS_XFS);
        
        kernel::serial::puts("[KERNEL] Filesystem drivers initialized (FAT32, ext2/4, NTFS, XFS)\n");
        
        // Initialize MSI/MSI-X subsystem
        kernel::msi::init();
        kernel::feature_report::complete_init(kernel::feature_report::INT_MSI);
        kernel::serial::puts("[KERNEL] MSI/MSI-X subsystem initialized\n");
        
        // Initialize VirtIO subsystem
        kernel::virtio::block::init();
        kernel::virtio::rng::init();
        kernel::secure_random::init();
        kernel::serial::puts("[KERNEL] VirtIO subsystem initialized\n");
        
        // Initialize ATA/SATA driver (scans for IDE and AHCI controllers)
        kernel::ata::init();
        kernel::serial::puts("[KERNEL] ATA/SATA driver initialized, ");
        kernel::serial::put_hex32(kernel::ata::device_count());
        kernel::serial::puts(" drive(s) found\n");
        
        // Initialize NVMe driver (scans for NVMe controllers)
        kernel::nvme::init();
        kernel::serial::puts("[KERNEL] NVMe driver initialized, ");
        kernel::serial::put_hex32(kernel::nvme::device_count());
        kernel::serial::puts(" namespace(s) found\n");
        
        // Initialize RAM disk subsystem
        kernel::ramdisk::init();
        
        // Create a 4MB RAM disk for temporary storage / testing
        uint8_t ramdiskIdx = kernel::ramdisk::create(4 * 1024 * 1024, "ram0");
        if (ramdiskIdx != 0xFF) {
            kernel::serial::puts("[KERNEL] Created 4MB RAM disk 'ram0'\n");
        }
        
        // Initialize VFS layer
        kernel::vfs::init();
        kernel::serial::puts("[KERNEL] VFS layer initialized\n");
        
        // Report total block devices
        kernel::serial::puts("[KERNEL] Total block devices: ");
        kernel::serial::put_hex32(kernel::block::device_count());
        kernel::serial::putc('\n');
        
        const bool mounted = mount_persistent_storage();

        if (is_bootinfo && bootinfo && bootinfo->RamdiskBase != 0 && bootinfo->RamdiskSize != 0) {
            kernel::serial::puts("[KERNEL] Boot wallpaper pack found in ramdisk.img\n");
            kernel::desktop::set_wallpaper_image_pack(reinterpret_cast<const void*>(static_cast<uintptr_t>(bootinfo->RamdiskBase)), bootinfo->RamdiskSize);
            mount_navigator_boot_ca_store_if_available();
            mount_navigator_boot_config_if_available();
        }

        kernel::desktop::reload_persisted_wallpaper();
#if defined(GXOS_BARE_METAL)
        kernel::desktop::refresh_bare_metal_desktop_folders_after_vfs_ready();
#endif
        // Discover external NativeElf packages only after the persistent FAT
        // filesystem is mounted. The loader consumes /Apps through VFS and
        // never reaches back to a host filesystem path at runtime.
        kernel::native_elf::discover();
        // The first desktop draw happens before VFS and the boot ramdisk are ready.
        // Redraw now so bare-metal thumbnails and the selected wallpaper use /system/wallpapers.
        kernel::desktop::draw();

#if defined(GXOS_QEMU_VIRTIO_GPU_MANUAL_VALIDATION_ACTIVE)
#if defined(GXOS_QEMU_VIRTIO_GPU_DISPLAY_CONFIGURATION_PERSISTENCE_ACTIVE)
        manualPersistenceReady = mounted;
        if (manualPersistenceReady) {
            gxos::display::DisplayConfigurationService::requestStartupRestore();
        }
#else
        manualPersistenceReady = false;
#endif
#ifdef GXOS_FILE_OPERATIONS_RUNTIME_SMOKE_ACTIVE
        // Run the storage/clipboard regression immediately after VFS mount.
        // Network initialization can wait for DHCP on QEMU or physical
        // hardware, so it must not gate this filesystem-specific smoke result.
        kernel::serial::puts("[FILE-OPS-RUNTIME-SMOKE] issuing command=file-operations.runtime\n");
#ifdef GXOS_FILE_OPERATIONS_TRASH_RUNTIME_SMOKE_ACTIVE
        kernel::file_clipboard::run_trash_runtime_smoke();
#else
        kernel::file_clipboard::run_runtime_smoke();
#endif
        kernel::serial::puts("[FILE-OPS-RUNTIME-SMOKE] done\n");
#endif
#endif // GXOS_QEMU_VIRTIO_GPU_MANUAL_VALIDATION_ACTIVE
        
        // ============================================================
        
        // ============================================================
        // Network Subsystem Initialization
        // ============================================================
        kernel::serial::puts("[KERNEL] Initializing NIC driver...\n");
        
        // Try to initialize NIC from BootInfo first (MMIO already mapped by bootloader)
        bool nicInitialized = false;
        if (bootinfo != nullptr) {
            // Cast BootInfo's Nic field to our NicBootInfo structure
            const kernel::nic::NicBootInfo* nicInfo = 
                reinterpret_cast<const kernel::nic::NicBootInfo*>(&bootinfo->Nic);
            
            if (nicInfo->flags & kernel::nic::NIC_BOOT_FLAG_FOUND) {
                kernel::serial::puts("[KERNEL] NIC info found in BootInfo, using mapped MMIO\n");
                nicInitialized = kernel::nic::init_from_bootinfo(nicInfo);
                if (nicInitialized) {
                    kernel::serial::puts("[KERNEL] NIC initialized from BootInfo successfully\n");
                } else {
                    kernel::serial::puts("[KERNEL] WARNING: BootInfo NIC init failed; bound device retained for diagnostics\n");
                }
            } else {
                kernel::serial::puts("[KERNEL] NIC not found in BootInfo (flags=");
                kernel::serial::put_hex32(nicInfo->flags);
                kernel::serial::puts("), falling back to PCI scan\n");
            }
        } else {
            kernel::serial::puts("[KERNEL] No BootInfo available, using PCI scan\n");
        }
        
        // Fall back to PCI scan only when BootInfo did not produce a bound
        // device structure.  A failed I219 hardware stage is intentionally
        // retained for netdiag so the exact frontier is not erased by a
        // second identity-only scan.
        if (!nicInitialized && kernel::nic::get_device() == nullptr) {
            kernel::serial::puts("[KERNEL] Falling back to PCI scan...\n");
            kernel::nic::init();
        } else if (!nicInitialized) {
            kernel::serial::puts("[KERNEL] NIC binding retained for diagnostics after init failure\n");
        }
        
        if (kernel::nic::is_active()) {
            kernel::serial::puts("[KERNEL] NIC active, registering IRQ");
            kernel::serial::put_hex8(kernel::nic::get_device()->irqLine);
            kernel::serial::putc('\n');
            kernel::interrupts::register_irq(
                kernel::nic::get_device()->irqLine,
                kernel::nic::irq_handler);
            kernel::nic::set_irq_registered(true);
            
            // Initialize IPv4 layer
            kernel::ipv4::init();
            kernel::ipv4::set_mac_address(kernel::nic::get_mac_address());
            
            // Configure with default IP (can be changed via DHCP later)
            // Default: 10.0.2.15/24, gateway 10.0.2.2 (QEMU user networking)
            kernel::ipv4::configure(
                kernel::ipv4::make_ip(10, 0, 2, 15),   // IP
                kernel::ipv4::MASK_24,                  // Subnet mask
                kernel::ipv4::make_ip(10, 0, 2, 2),    // Gateway
                kernel::ipv4::make_ip(10, 0, 2, 3)     // DNS
            );
            
            // Initialize ICMP (ping support)
            kernel::icmp::init();
            
            // Initialize UDP layer
            kernel::udp::init();
            
            // Initialize TCP stack
            kernel::tcp::init();
            
            // Initialize Socket API
            kernel::socket::init();
            
            // Initialize DNS client
            kernel::dns::init();

            // Reset DHCP state/counters for this boot and seed its XID source
            // from the active NIC MAC before attempting discovery.
            kernel::dhcp::init();

            kernel::serial::puts("[KERNEL] Attempting DHCP network configuration...\n");
            if (kernel::dhcp::discover() == kernel::dhcp::DHCP_OK) {
                kernel::serial::puts("[KERNEL] DHCP network configuration complete\n");
                kernel::dns::init();
            } else {
                kernel::serial::puts("[KERNEL] DHCP failed, keeping static network configuration\n");
            }
        }
        
        // ============================================================
        
        // Initialize PS/2 mouse driver and register IRQ12 handler
        // (PS/2 is used as fallback when USB HID is not available)
        kernel::serial::puts("[KERNEL] Initializing PS/2 mouse...\n");
        kernel::ps2mouse::init(kernel::framebuffer::get_width(),
                               kernel::framebuffer::get_height());
        kernel::serial::puts("[KERNEL] PS/2 mouse init complete\n");
        kernel::interrupts::register_irq(12, kernel::ps2mouse::irq_handler);
        kernel::serial::puts("[KERNEL] IRQ12 handler registered and unmasked\n");
        
        // Initialize PS/2 keyboard driver and register IRQ1 handler
        kernel::serial::puts("[KERNEL] Initializing PS/2 keyboard...\n");
        kernel::ps2keyboard::init();
        kernel::interrupts::register_irq(1, kernel::ps2keyboard::irq_handler);
        kernel::serial::puts("[KERNEL] IRQ1 (keyboard) handler registered\n");
        
        // Initialize input manager (handles USB HID, PS/2, VirtIO fallback)
        kernel::serial::puts("[KERNEL] Initializing input manager...\n");
        kernel::input::init(kernel::framebuffer::get_width(),
                            kernel::framebuffer::get_height());
        kernel::serial::puts("[KERNEL] Input manager initialized\n");
        
        // Update feature report for input devices
        kernel::feature_report::complete_init(kernel::feature_report::INPUT_PS2_KB);
        kernel::feature_report::complete_init(kernel::feature_report::INPUT_PS2_MOUSE);
        kernel::feature_report::complete_init(kernel::feature_report::INT_PIC_8259);
        
        // ============================================================
        // Print Hardware Feature Report
        // ============================================================
        kernel::feature_report::print_report();
        
        // Draw initial cursor at center of screen
        kernel::desktop::draw_cursor(kernel::input::mouse_x(),
                                     kernel::input::mouse_y());
        kernel::desktop_capabilities::log_current(true, true);
        kernel::apps::printNavigatorRuntimeSmokeReport();
#ifdef GXOS_DESKTOP_CLEANUP_RUNTIME_PASS
        kernel::serial::puts("[KERNEL] desktopCleanupRuntimePass=2 launch-smoke begin\n");
        const bool cleanupDisplayOptionsLaunched = kernel::desktop::launch_app("DisplayOptions");
        kernel::serial::puts("[KERNEL] desktopCleanupRuntimePass=2 launch app=DisplayOptions result=");
        kernel::serial::puts(cleanupDisplayOptionsLaunched ? "PASS" : "FAIL");
        kernel::serial::puts("\n");
        const bool cleanupNotepadLaunched = kernel::desktop::launch_app("Notepad");
        kernel::serial::puts("[KERNEL] desktopCleanupRuntimePass=2 launch app=Notepad result=");
        kernel::serial::puts(cleanupNotepadLaunched ? "PASS" : "FAIL");
        kernel::serial::puts("\n");
        const bool cleanupCalculatorLaunched = kernel::desktop::launch_app("Calculator");
        kernel::serial::puts("[KERNEL] desktopCleanupRuntimePass=2 launch app=Calculator result=");
        kernel::serial::puts(cleanupCalculatorLaunched ? "PASS" : "FAIL");
        kernel::serial::puts("\n");
        kernel::serial::puts("[KERNEL] desktopCleanupRuntimePass=2 launch-smoke end\n");
#endif
#ifdef GXOS_APPMODEL_LAUNCHSHADOW_SMOKE_ACTIVE
        kernel::serial::puts("[APPMODEL-LAUNCHSHADOW-SMOKE] issuing command=desktop.smoke.launchshadow\n");
        kernel::appmodel::printLaunchTargetShadowSmokeDiagnostic(kernel::serial::puts);
#if defined(GXOS_APPMODEL_TYPED_DISPATCH_SHADOW_ONLY)
        kernel::desktop::run_launch_shadow_folder_fileopen_smoke();
        kernel::desktop::run_launch_shadow_text_fileopen_smoke();
#endif
        kernel::serial::puts("[APPMODEL-LAUNCHSHADOW-SMOKE] done\n");
#endif
#ifdef GXOS_LIVE_DIRECTORY_DESKTOP_RUNTIME_SMOKE_ACTIVE
        kernel::serial::puts("[LIVE-DIRECTORY-RUNTIME-SMOKE] issuing command=desktop.smoke.live-directory-runtime\n");
        kernel::desktop::run_live_directory_runtime_smoke();
        kernel::serial::puts("[LIVE-DIRECTORY-RUNTIME-SMOKE] done\n");
#endif
#ifdef GXOS_IMAGEVIEWER_BARE_METAL_RUNTIME_SMOKE_ACTIVE
        kernel::serial::puts("[IMAGEVIEWER-RUNTIME-SMOKE] issuing command=desktop.smoke.imageviewer-runtime\n");
        kernel::desktop::run_imageviewer_runtime_smoke();
        kernel::serial::puts("[IMAGEVIEWER-RUNTIME-SMOKE] done\n");
#endif
        kernel::serial::puts("[KERNEL] Entering main loop (waiting for input)...\n");
        
        
        // Main kernel loop â€” poll input and redraw cursor
        while (1) {
            uint64_t workStartTicks = kernel::pit::ticks();

#if defined(GXOS_QEMU_VIRTIO_GPU_MANUAL_VALIDATION_ACTIVE)
            if (!manualReadinessReported) {
#if defined(GXOS_QEMU_VIRTIO_GPU_DISPLAY_CONFIGURATION_PERSISTENCE_ACTIVE)
                for (uint32_t restoreAttempt = 0u; restoreAttempt < 8u; ++restoreAttempt) {
                    gxos::display::DisplayConfigurationService::processPendingAtSafePoint();
                    kernel::virtio::gpu::presentation_tick();
                    if (gxos::display::DisplayConfigurationService::startupRestoreComplete()) break;
                }
#endif
                if (gxos::display::DisplayConfigurationService::startupRestoreComplete()) {
                    const bool manualLogicalConfigurationReady = initialize_manual_logical_configuration();
                    (void)manualLogicalConfigurationReady;
                    kernel::desktop::request_redraw();
                    log_manual_dual_monitor_validation_banner();
                    const bool manualReadinessReady = emit_manual_readiness_report(manualPersistenceReady);
                    (void)manualReadinessReady;
                    manualReadinessReported = true;
                } else {
                    manualPersistenceReady = false;
                    const bool manualReadinessReady = emit_manual_readiness_report(false);
                    (void)manualReadinessReady;
                    manualReadinessReported = true;
                }
            }
#endif

            // Poll input manager for updates (handles USB HID polling)
            kernel::input::poll();
            
            // Poll network for received packets
            kernel::ipv4::poll_network();

            int8_t wheelDelta = kernel::input::mouse_scroll_y();
            
            if (kernel::input::mouse_dirty()) {
                kernel::input::mouse_clear_dirty();
                kernel::desktop::handle_mouse(
                    kernel::input::mouse_x(),
                    kernel::input::mouse_y(),
                    kernel::input::mouse_buttons());
#if defined(GXOS_QEMU_VIRTIO_GPU_MANUAL_VALIDATION_ACTIVE)
                // The manual VirtIO-GPU presenter is dirty-aware and copies
                // the normal desktop canvas asynchronously.  The legacy
                // framebuffer path presents directly from handle_mouse();
                // explicitly publish the same invalidation for QEMU manual
                // mode so pointer movement and clicks become visible.
                kernel::desktop::request_redraw();
#endif
            }

            if (wheelDelta != 0) {
                kernel::desktop::handle_mouse_wheel(
                    kernel::input::mouse_x(),
                    kernel::input::mouse_y(),
                    wheelDelta);
#if defined(GXOS_QEMU_VIRTIO_GPU_MANUAL_VALIDATION_ACTIVE)
                kernel::desktop::request_redraw();
#endif
            }
            
            // Process buffered keyboard input from PS/2 IRQ handler
            if (kernel::ps2keyboard::has_key()) {
                uint32_t key = kernel::ps2keyboard::get_key();
                if (key != 0) {
#if defined(GXOS_QEMU_VIRTIO_GPU_MANUAL_VALIDATION_ACTIVE)
                    if (handle_manual_topology_control_key(key)) {
                        kernel::desktop::request_redraw();
                    } else
#endif
                    kernel::desktop::handle_key(key);
#if defined(GXOS_QEMU_VIRTIO_GPU_MANUAL_VALIDATION_ACTIVE)
                    // handle_key() redraws the software canvas directly, but
                    // the live VirtIO-GPU presenter must also observe an
                    // invalidation before it will transfer that canvas.
                    kernel::desktop::request_redraw();
#endif
                    // handle_key calls draw() internally; redraw cursor overlay
                    kernel::desktop::draw_cursor(
                        kernel::input::mouse_x(),
                        kernel::input::mouse_y());
                }
            }

            kernel::desktop::tick();

            // Check if any other source triggered a redraw
            if (kernel::desktop::needs_redraw()) {
                kernel::desktop::draw();
                kernel::desktop::draw_cursor(
                    kernel::input::mouse_x(),
                    kernel::input::mouse_y());
            }

            // The virtio-gpu live path consumes the normal desktop update
            // cadence; it does not own desktop/window state or input routing.
            kernel::virtio::gpu::presentation_tick();

            uint64_t workEndTicks = kernel::pit::ticks();
            if (workEndTicks >= workStartTicks) {
                kernel::desktop::record_cpu_busy_ticks(workEndTicks - workStartTicks);
            }
            
            uint64_t idleStartTicks = kernel::pit::ticks();

            // Halt CPU until next interrupt (saves power)
            kernel::arch::halt();

            uint64_t idleEndTicks = kernel::pit::ticks();
            if (idleEndTicks >= idleStartTicks) {
                kernel::desktop::record_cpu_idle_ticks(idleEndTicks - idleStartTicks);
            }
        }
    }
    else {
#if defined(GXOS_QEMU_VIRTIO_GPU_PROBE_ACTIVE) && defined(GXOS_QEMU_VIRTIO_GPU_COMPOSITOR_LIVE)
        // QEMU virtio-gpu owns its scanout resources rather than exporting a
        // GOP framebuffer.  Keep this small probe-only pump separate from the
        // normal text fallback so the bounded live proof still receives PIT
        // ticks without creating a product compositor loop.
        kernel::interrupts::init();
        kernel::pit::init(100);
        kernel::interrupts::register_irq(0, kernel::pit::irq_handler);
#if defined(GXOS_QEMU_VIRTIO_GPU_DISPLAY_CONFIGURATION_CONTROL_ACTIVE) || defined(GXOS_QEMU_VIRTIO_GPU_DISPLAY_CONFIGURATION_PERSISTENCE_ACTIVE)
        const bool displayConfigurationStoreReady = initialize_qemu_display_configuration_storage();
        (void)displayConfigurationStoreReady;
#if defined(GXOS_QEMU_VIRTIO_GPU_DISPLAY_CONFIGURATION_PERSISTENCE_ACTIVE)
        gxos::display::DisplayConfigurationService::requestStartupRestore();
        for (uint32_t restoreAttempt = 0u; restoreAttempt < 8u &&
             !gxos::display::DisplayConfigurationService::startupRestoreComplete(); ++restoreAttempt) {
            gxos::display::DisplayConfigurationService::processPendingAtSafePoint();
            kernel::virtio::gpu::presentation_tick();
        }
#endif
#endif
        int32_t inputLeft = 0;
        int32_t inputTop = 0;
        int32_t inputRight = 0;
        int32_t inputBottom = 0;
        uint8_t inputMonitorCount = 0;
        kernel::display_input::DisplayInputMonitor inputMonitors[
            kernel::display_input::kDisplayInputMaxMonitors];
        const bool inputLayoutReady = kernel::virtio::gpu::get_display_input_layout(
            &inputLeft, &inputTop, &inputRight, &inputBottom,
            inputMonitors, kernel::display_input::kDisplayInputMaxMonitors,
            &inputMonitorCount);
        if (inputLayoutReady && inputMonitorCount > 0u) {
            const int32_t virtualWidth = inputRight - inputLeft;
            const int32_t virtualHeight = inputBottom - inputTop;
            kernel::serial::puts("[INPUT] QEMU input path=ps2-relative virtualDesktop=");
            serial_put_u32_decimal(static_cast<uint32_t>(virtualWidth));
            kernel::serial::putc('x');
            serial_put_u32_decimal(static_cast<uint32_t>(virtualHeight));
            kernel::serial::puts(" monitors=");
            serial_put_u32_decimal(inputMonitorCount);
            kernel::serial::putc('\n');
            kernel::ps2mouse::init(static_cast<uint32_t>(virtualWidth),
                                   static_cast<uint32_t>(virtualHeight));
            kernel::interrupts::register_irq(12, kernel::ps2mouse::irq_handler);
            kernel::input::init(static_cast<uint32_t>(virtualWidth),
                                static_cast<uint32_t>(virtualHeight));
            kernel::input::configure_display_layout(
                inputLeft, inputTop, inputRight, inputBottom,
                inputMonitors, inputMonitorCount);
            kernel::input::set_mapping_diagnostics(true, 96u);
#if !defined(GXOS_QEMU_VIRTIO_GPU_MANUAL_VALIDATION_ACTIVE)
            kernel::qemu_display_input_proof::init(
                virtualWidth, virtualHeight,
                kernel::input::get_display_input_mapper());
#endif
        } else {
            kernel::serial::puts(
                "[INPUT-PROOF] headAwareAbsolute=unavailable reason=virtio-gpu monitor geometry unavailable; live presentation retained\n");
        }
#if defined(GXOS_QEMU_VIRTIO_GPU_MANUAL_VALIDATION_ACTIVE)
        log_manual_dual_monitor_validation_banner();
#elif defined(GXOS_QEMU_VIRTIO_GPU_PROOF_ACTIVE) && defined(GXOS_QEMU_VIRTIO_GPU_DISPLAY_CONFIGURATION_CONTROL_ACTIVE)
        kernel::qemu_display_resolution_rebuild_proof::run();
        kernel::qemu_display_configuration_control_proof::run();
#elif defined(GXOS_QEMU_VIRTIO_GPU_PROOF_ACTIVE) && defined(GXOS_QEMU_VIRTIO_GPU_DISPLAY_CONFIGURATION_PERSISTENCE_ACTIVE)
        kernel::qemu_display_configuration_persistence_proof::run();
        kernel::qemu_display_resolution_persistence_proof::run();
#endif
#if defined(GXOS_QEMU_VIRTIO_GPU_DISPLAY_EVENTS_ACTIVE) && defined(GXOS_QEMU_VIRTIO_GPU_PROOF_ACTIVE) && !defined(GXOS_QEMU_VIRTIO_GPU_MANUAL_VALIDATION_ACTIVE)
        kernel::qemu_display_events_proof::run();
#endif
        kernel::serial::puts(
#if defined(GXOS_QEMU_VIRTIO_GPU_MANUAL_VALIDATION_ACTIVE)
            "[KERNEL] QEMU-only virtio-gpu manual presentation pump active; automatic proof disabled\n"
#else
            "[KERNEL] QEMU-only virtio-gpu live presentation pump active\n"
#endif
        );
        while (!kernel::virtio::gpu::presentation_finished()) {
            if (inputLayoutReady) {
                kernel::input::poll();
                if (kernel::input::mouse_dirty()) {
                    kernel::input::mouse_clear_dirty();
                    const kernel::display_input::DisplayPointerEvent* pointer =
                        kernel::input::get_last_pointer_event();
                    if (pointer != nullptr) {
#if !defined(GXOS_QEMU_VIRTIO_GPU_MANUAL_VALIDATION_ACTIVE)
                        kernel::qemu_display_input_proof::handle(*pointer);
#endif
                    }
                }
            }
            kernel::virtio::gpu::presentation_tick();
            // Consume the normal compositor invalidation request after the
            // live presenter has observed it, allowing clean-frame skips.
            (void)kernel::desktop::needs_redraw();
            kernel::arch::halt();
        }
        if (inputLayoutReady) {
#if !defined(GXOS_QEMU_VIRTIO_GPU_MANUAL_VALIDATION_ACTIVE)
            kernel::qemu_display_input_proof::finish();
#endif
            const kernel::display_input::DisplayInputMapperCounters* counters =
                kernel::input::get_mapping_counters();
            if (counters != nullptr) {
                kernel::serial::puts("[INPUT-MAP] summary eventsSeen=");
                serial_put_u32_decimal(counters->eventsSeen);
                kernel::serial::puts(" valid=");
                serial_put_u32_decimal(counters->validEvents);
                kernel::serial::puts(" invalid=");
                serial_put_u32_decimal(counters->invalidEvents);
                kernel::serial::puts(" relative=");
                serial_put_u32_decimal(counters->relativeEvents);
                kernel::serial::puts(" headAbsolute=");
                serial_put_u32_decimal(counters->headAbsoluteEvents);
                kernel::serial::puts(" normalizedAbsolute=");
                serial_put_u32_decimal(counters->normalizedAbsoluteEvents);
                kernel::serial::puts(" unknownHeadFallbacks=");
                serial_put_u32_decimal(counters->unknownHeadFallbacks);
                kernel::serial::puts(" clamped=");
                serial_put_u32_decimal(counters->clampedEvents);
                kernel::serial::putc('\n');
            }
        }
#if defined(GXOS_QEMU_VIRTIO_GPU_MANUAL_VALIDATION_ACTIVE)
        kernel::serial::puts("[KERNEL] QEMU-only virtio-gpu manual presentation pump remains active until QEMU exit\n");
#else
        kernel::serial::puts("[KERNEL] QEMU-only virtio-gpu bounded live presentation pump stopped\n");
#endif
        while (1) {
            kernel::arch::halt();
        }
#else
        // === TEXT MODE FALLBACK ===
        kernel::vga::init();
        kernel::vga::print_colored("guideXOS Kernel\n", kernel::vga::Color::LightCyan, kernel::vga::Color::Black);
        kernel::vga::print("Framebuffer not available - text mode only\n");
        if (is_bootinfo && bootinfo) {
            const kernel::framebuffer::DiagnosticFramebufferInventorySummary& framebufferInventory =
                kernel::framebuffer::diagnostic_framebuffer_inventory_summary();
            log_framebuffer_summary(
                framebufferInventory.RawCount,
                framebufferInventory.UniqueCount,
                framebufferInventory.DuplicateCount,
                framebufferInventory.SuspiciousCount,
                framebufferInventory.ActiveRenderTargetCount,
                framebufferInventory.DisabledCandidateCount);
        }
        log_framebuffer_descriptor(
            is_bootinfo ? "UEFI BootInfo" : "Multiboot",
            0u,
            0u,
            0u,
            0u,
            0u,
            0u,
            0u,
            0u,
            0u,
            "Unknown");

        while (1) { }
#endif
    }

#else
    // ============================================================
    // Non-x86 boot path  (SPARC, IA-64, ARM, ...)
    // ============================================================
    (void)boot_environment;
    (void)boot_magic;

    // Initialize architecture-specific hardware
    kernel::arch::init();

#if defined(ARCH_SPARC)
    // ---- SPARC Sun4m boot path ----

    // Discover the TCX framebuffer at its well-known MMIO address
    bool has_fb = kernel::framebuffer::init_sun4m();

    if (has_fb) {
        // Clear screen to dark colour
        kernel::framebuffer::clear(0xFF101828);

        // Initialize and draw the desktop
        kernel::desktop::init();
        kernel::desktop::draw();

        // Set up interrupts (SLAVIO already initialised in arch::init)
        kernel::interrupts::init();

        // Initialize the Z8530 serial keyboard/mouse driver
        kernel::arch::sparc::zs::init(kernel::framebuffer::get_width(),
                                       kernel::framebuffer::get_height());

        // Register the ZS IRQ handler on SLAVIO IRQ 12 (serial/SBus level 6)
        kernel::interrupts::register_irq(12, kernel::arch::sparc::zs::irq_handler);

        // Draw initial cursor at centre of screen
        kernel::desktop::draw_cursor(kernel::arch::sparc::zs::mouse_x(),
                                     kernel::arch::sparc::zs::mouse_y());
        kernel::desktop_capabilities::log_current(true, true);
        kernel::apps::printNavigatorRuntimeSmokeReport();

        // Main kernel loop â€” poll mouse state and redraw cursor
        while (1) {
            if (kernel::arch::sparc::zs::mouse_dirty()) {
                kernel::arch::sparc::zs::mouse_clear_dirty();
                kernel::desktop::handle_mouse(
                    kernel::arch::sparc::zs::mouse_x(),
                    kernel::arch::sparc::zs::mouse_y(),
                    kernel::arch::sparc::zs::mouse_buttons());
            }
            kernel::desktop::tick();
            kernel::arch::halt();
        }
    }
#endif // ARCH_SPARC

#if defined(ARCH_SPARC64)
    // ---- SPARC v9 Sun4u boot path ----

    bool has_fb = kernel::framebuffer::init_sun4u();

    if (has_fb) {
        kernel::framebuffer::clear(0xFF101828);

        kernel::desktop::init();
        kernel::desktop::draw();

        kernel::interrupts::init();

        kernel::arch::sparc64::zs::init(kernel::framebuffer::get_width(),
                                         kernel::framebuffer::get_height());

        kernel::interrupts::register_irq(12, kernel::arch::sparc64::zs::irq_handler);

        kernel::desktop::draw_cursor(kernel::arch::sparc64::zs::mouse_x(),
                                     kernel::arch::sparc64::zs::mouse_y());
        kernel::desktop_capabilities::log_current(true, true);
        kernel::apps::printNavigatorRuntimeSmokeReport();

        while (1) {
            if (kernel::arch::sparc64::zs::mouse_dirty()) {
                kernel::arch::sparc64::zs::mouse_clear_dirty();
                kernel::desktop::handle_mouse(
                    kernel::arch::sparc64::zs::mouse_x(),
                    kernel::arch::sparc64::zs::mouse_y(),
                    kernel::arch::sparc64::zs::mouse_buttons());
            }
            kernel::desktop::tick();
            kernel::arch::halt();
        }
    }
#endif // ARCH_SPARC64

#if defined(ARCH_IA64)
    // ---- IA-64 / ski simulator boot path ----

    // arch::init() already set up IVT, RSE, and ski console.
    // Print boot information to the firmware console.
    kernel::arch::ia64::ski_console::puts("\r\n");
    kernel::arch::ia64::ski_console::puts("========================================\r\n");
    kernel::arch::ia64::ski_console::puts("  guideXOS Server - Itanium (IA-64)\r\n");
    kernel::arch::ia64::ski_console::puts("  Running on HP ski simulator\r\n");
    kernel::arch::ia64::ski_console::puts("========================================\r\n");
    kernel::arch::ia64::ski_console::puts("\r\n");
    kernel::arch::ia64::ski_console::puts("Kernel loaded at 1 MB physical\r\n");
    kernel::arch::ia64::ski_console::puts("Architecture: ");
    kernel::arch::ia64::ski_console::puts(kernel::arch::get_arch_name());
    kernel::arch::ia64::ski_console::puts("\r\n");
    kernel::arch::ia64::ski_console::puts("\r\n");
    kernel::arch::ia64::ski_console::puts("Entering idle loop (Ctrl-C in ski to exit)\r\n");
    kernel::desktop_capabilities::log_current(false, false);

    // Enable interrupts and idle
    kernel::interrupts::init();
    while (1) {
        kernel::desktop::tick();
        kernel::arch::halt();
    }
#endif // ARCH_IA64

#if defined(ARCH_RISCV64)
    // ---- RISC-V 64 / QEMU virt boot path ----

    // arch::init() already set up SBI console.
    // Print boot information to the SBI serial console.
    kernel::arch::riscv64::sbi_console::puts("\r\n");
    kernel::arch::riscv64::sbi_console::puts("========================================\r\n");
    kernel::arch::riscv64::sbi_console::puts("  guideXOS Server - RISC-V 64 (RV64IMA)\r\n");
    kernel::arch::riscv64::sbi_console::puts("  Running on QEMU virt + OpenSBI\r\n");
    kernel::arch::riscv64::sbi_console::puts("========================================\r\n");
    kernel::arch::riscv64::sbi_console::puts("\r\n");
    kernel::arch::riscv64::sbi_console::puts("Kernel loaded at 0x80200000\r\n");
    kernel::arch::riscv64::sbi_console::puts("Architecture: ");
    kernel::arch::riscv64::sbi_console::puts(kernel::arch::get_arch_name());
    kernel::arch::riscv64::sbi_console::puts("\r\n");
    kernel::arch::riscv64::sbi_console::puts("\r\n");

    // Try to initialise ramfb / PCI VGA graphics
    kernel::arch::riscv64::sbi_console::puts("Probing graphics...\r\n");
    bool has_fb = kernel::arch::riscv64::graphics::init();

    if (has_fb) {
        kernel::arch::riscv64::sbi_console::puts("Framebuffer found: ");
        kernel::arch::riscv64::sbi_console::put_hex(
            kernel::arch::riscv64::graphics::get_lfb_address());
        kernel::arch::riscv64::sbi_console::puts("\r\n");

        // Register the framebuffer with the core driver
        kernel::framebuffer::init_riscv_ramfb(
            kernel::arch::riscv64::graphics::get_lfb_address(),
            kernel::arch::riscv64::graphics::get_width(),
            kernel::arch::riscv64::graphics::get_height(),
            kernel::arch::riscv64::graphics::get_pitch(),
            kernel::arch::riscv64::graphics::get_bpp());

        kernel::framebuffer::clear(0xFF101828);

        kernel::desktop::init();
        kernel::desktop::draw();

        kernel::arch::riscv64::sbi_console::puts("Desktop drawn\r\n");
        kernel::desktop_capabilities::log_current(false, false);
    } else {
        kernel::arch::riscv64::sbi_console::puts("No framebuffer detected\r\n");
        kernel::desktop_capabilities::log_current(false, false);
    }

    kernel::arch::riscv64::sbi_console::puts("Entering idle loop\r\n");

    // Enable interrupts and idle
    kernel::interrupts::init();
    while (1) {
        kernel::desktop::tick();
        kernel::arch::halt();
    }
#endif // ARCH_RISCV64

    // Fallback: no framebuffer or unsupported non-x86 platform
    kernel::desktop_capabilities::log_current(false, false);
    kernel::interrupts::init();
    while (1) {
        kernel::desktop::tick();
        kernel::arch::halt();
    }
#endif // ARCH_HAS_PIC_8259
}


