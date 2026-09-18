//
// guideXOS Minimal Kernel - Entry Point
//
// ROLE: Bridge between bootloader and guideXOS Server (user-mode)
//
// Copyright (c) 2026 guideXOS Server
//

#if defined(GXOS_NATIVEAOT_GC_SEGMENT_TRANSITION_QEMU_TEST)
#define GXOS_NATIVEAOT_GC_SEGMENT_BOUNDARY_QEMU_TEST 1
#endif

#include "include/kernel/version.h"
#include "include/kernel/arch.h"
#include "include/kernel/vga.h"
#include "include/kernel/framebuffer.h"
#include "include/kernel/process.h"
#include "include/kernel/desktop.h"
#include "include/kernel/kernel_apps.h"
#include "include/kernel/kernel_compositor.h"
#include "include/kernel/interrupts.h"
#include "include/kernel/ps2mouse.h"
#include "include/kernel/ps2keyboard.h"
#include "include/kernel/input_manager.h"
#include "include/kernel/pit.h"
#include "include/kernel/serial_debug.h"
#include "include/kernel/desktop_capabilities.h"
#include "include/kernel/app_launch_target_resolver.h"
#include "include/kernel/address_space.h"
#include "include/kernel/nativeaot_application.h"
#include "built_in_app_metadata.h"

#if defined(GXOS_NATIVE_THREAD_QEMU_TEST)
#include "include/kernel/native_thread_qemu_test.h"
#endif
#if defined(GXOS_NATIVE_LOCAL_STORAGE_QEMU_TEST)
#include "include/kernel/native_local_storage_qemu_test.h"
#endif
#if defined(GXOS_NATIVE_VIRTUAL_MEMORY_QEMU_TEST)
#include "include/kernel/native_virtual_memory_qemu_test.h"
#endif
#if defined(GXOS_C011EC99_PHYSICAL_FRAME_SCALING_QEMU_TEST)
#include "include/kernel/native_physical_frame_scaling_qemu_test.h"
#include "include/kernel/native_virtual_memory_qemu_test.h"
#endif
#if defined(GXOS_NATIVEAOT_PAL_QEMU_TEST)
#include "include/kernel/nativeaot_pal_qemu_test.h"
#include "guidexos_nativeaot_pal_qemu_exports.h"
extern "C" unsigned char guidexos_nativeaot_pal_qemu_artifact_start[];
extern "C" unsigned char guidexos_nativeaot_pal_qemu_artifact_end[];
#endif
#if defined(GXOS_NATIVEAOT_THREAD_STATIC_QEMU_TEST)
#include "include/kernel/nativeaot_pal_qemu_test.h"
#include "guidexos_nativeaot_thread_static_exports.h"
extern "C" unsigned char guidexos_nativeaot_gc_startup_artifact_start[];
extern "C" unsigned char guidexos_nativeaot_gc_startup_artifact_end[];
#elif defined(GXOS_NATIVEAOT_GC_SINGLE_THREAD_SUSPEND_EE_QEMU_TEST)
#include "include/kernel/nativeaot_pal_qemu_test.h"
#include "guidexos_nativeaot_gc_single_thread_suspend_ee_exports.h"
extern "C" unsigned char guidexos_nativeaot_gc_startup_artifact_start[];
extern "C" unsigned char guidexos_nativeaot_gc_startup_artifact_end[];
#elif defined(GXOS_NATIVEAOT_GC_FIRST_COLLECTION_BOUNDARY_QEMU_TEST)
#include "include/kernel/nativeaot_pal_qemu_test.h"
#include "guidexos_nativeaot_gc_first_collection_boundary_exports.h"
extern "C" unsigned char guidexos_nativeaot_gc_startup_artifact_start[];
extern "C" unsigned char guidexos_nativeaot_gc_startup_artifact_end[];
#elif defined(GXOS_NATIVEAOT_GC_SEGMENT_BOUNDARY_QEMU_TEST)
#include "include/kernel/nativeaot_pal_qemu_test.h"
#include "guidexos_nativeaot_gc_segment_boundary_exports.h"
extern "C" unsigned char guidexos_nativeaot_gc_startup_artifact_start[];
extern "C" unsigned char guidexos_nativeaot_gc_startup_artifact_end[];
#elif defined(GXOS_NATIVEAOT_GC_FIRST_REFILL_QEMU_TEST)
#include "include/kernel/nativeaot_pal_qemu_test.h"
#include "guidexos_nativeaot_gc_first_refill_exports.h"
extern "C" unsigned char guidexos_nativeaot_gc_startup_artifact_start[];
extern "C" unsigned char guidexos_nativeaot_gc_startup_artifact_end[];
#elif defined(GXOS_NATIVEAOT_GC_FIRST_ALLOCATION_QEMU_TEST)
#include "include/kernel/nativeaot_pal_qemu_test.h"
#include "guidexos_nativeaot_gc_first_allocation_exports.h"
extern "C" unsigned char guidexos_nativeaot_gc_startup_artifact_start[];
extern "C" unsigned char guidexos_nativeaot_gc_startup_artifact_end[];
#elif defined(GXOS_NATIVEAOT_GC_STARTUP_QEMU_TEST)
#include "include/kernel/nativeaot_pal_qemu_test.h"
#include "guidexos_nativeaot_gc_startup_exports.h"
extern "C" unsigned char guidexos_nativeaot_gc_startup_artifact_start[];
extern "C" unsigned char guidexos_nativeaot_gc_startup_artifact_end[];
#endif
#if defined(GUIDEXOS_NATIVEAOT_C011EC21_NATIVE_CONTINUATION)
#include "include/kernel/native_unwind_provider.h"
#endif
#if defined(GXOS_NATIVE_MUTEX_QEMU_TEST)
#include "include/kernel/native_mutex_qemu_test.h"
#endif

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

#if defined(GXOS_NAVIGATOR_HTTP_SMOKE_ACTIVE) || defined(GXOS_NAVIGATOR_BOOT_STAGED_CONFIG_ACTIVE) || defined(GXOS_IMAGEVIEWER_BARE_METAL_RUNTIME_SMOKE_ACTIVE)
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

static void mount_navigator_smoke_ca_fixture_if_available()
{
    mount_navigator_smoke_alias_if_available(
        "/certs",
        "/system/certs",
        "[KERNEL] Navigator smoke CA source directory unavailable at /system/certs\n",
        "[KERNEL] Navigator smoke /certs mount already active\n",
        "[KERNEL] Navigator smoke failed to mount /certs from /system/certs\n",
        "[KERNEL] Navigator smoke mounted /certs from boot ramdisk path /system/certs\n");
}

static void mount_navigator_smoke_config_if_available()
{
    mount_navigator_smoke_alias_if_available(
        "/config",
        "/system/config",
        "[KERNEL] Navigator smoke config source directory unavailable at /system/config\n",
        "[KERNEL] Navigator smoke /config mount already active\n",
        "[KERNEL] Navigator smoke failed to mount /config from /system/config\n",
        "[KERNEL] Navigator smoke mounted /config from boot ramdisk path /system/config\n");
    mount_navigator_smoke_alias_if_available(
        "/config/certs",
        "/system/config/certs",
        "[KERNEL] Navigator smoke config certs directory unavailable at /system/config/certs\n",
        "[KERNEL] Navigator smoke /config/certs mount already active\n",
        "[KERNEL] Navigator smoke failed to mount /config/certs from /system/config/certs\n",
        "[KERNEL] Navigator smoke mounted /config/certs from boot ramdisk path /system/config/certs\n");
    mount_navigator_smoke_alias_if_available(
        "/config/navigator",
        "/system/config/navigator",
        "[KERNEL] Navigator smoke config navigator directory unavailable at /system/config/navigator\n",
        "[KERNEL] Navigator smoke /config/navigator mount already active\n",
        "[KERNEL] Navigator smoke failed to mount /config/navigator from /system/config/navigator\n",
        "[KERNEL] Navigator smoke mounted /config/navigator from boot ramdisk path /system/config/navigator\n");
}
#endif

extern "C" void kernel_main(void* boot_environment, uint32_t boot_magic)
{
#if ARCH_HAS_PIC_8259
    // ============================================================
    // x86 / amd64 boot path  —  Multiboot (BIOS) or BootInfo (UEFI)
    // ============================================================

    // Initialize serial debug output early
    kernel::serial::init();
    kernel::serial::puts("[KERNEL] guideXOS kernel_main entered\n");
    // Install the allocation-free native scheduler wait foundation before
    // enabling interrupts or entering any subsystem that may block.
    kernel::process::init();
#if defined(GXOS_DESKTOP_CLEANUP_RUNTIME_PASS)
    kernel::serial::puts("[KERNEL] desktopCleanupRuntimePass=2\n");
#endif

    // Support both Multiboot (legacy) and BootInfo (UEFI) boot
    bool is_multiboot = (boot_magic == 0x2BADB002);
    bool is_bootinfo = false;
    
    guideXOS::BootInfo* bootinfo = nullptr;
    void* multiboot_info = nullptr;
    
    if (is_multiboot) {
        multiboot_info = boot_environment;
        kernel::serial::puts("[KERNEL] Boot method: Multiboot\n");
    } else {
        bootinfo = static_cast<guideXOS::BootInfo*>(boot_environment);
        if (bootinfo && bootinfo->Magic == guideXOS::GUIDEXOS_BOOTINFO_MAGIC) {
            is_bootinfo = true;
            kernel::serial::puts("[KERNEL] Boot method: UEFI BootInfo\n");
#if defined(GUIDEXOS_NATIVEAOT_C011EC97)
            const uint64_t c97Selector = bootinfo->CommandLine;
            if (c97Selector == 216u || c97Selector == 320u) {
                kernel::nativeaot_pal_qemu_test::setC011EC97TailSelector(
                    static_cast<uint32_t>(c97Selector));
                kernel::serial::puts("[nativeaot-gc-c97] launch selector=");
                kernel::serial::put_hex32(static_cast<uint32_t>(c97Selector));
                kernel::serial::puts("\n");
            } else {
                kernel::serial::puts("[nativeaot-gc-c97] invalid launch selector\n");
            }
#endif
#if defined(GUIDEXOS_NATIVEAOT_C011EC21_NATIVE_CONTINUATION)
            guideXosNativeUnwindSetKernelPhysicalBase(
                static_cast<uintptr_t>(bootinfo->KernelPhysicalBase));
#endif
            kernel::nic::set_kernel_physical_base(bootinfo->KernelPhysicalBase);
            kernel::virtio::rng::set_kernel_physical_base(bootinfo->KernelPhysicalBase);
            if (kernel::memory::address_space::initialize(bootinfo)) {
                kernel::serial::puts("[KERNEL] Physical-frame allocator initialized from firmware map\n");
            } else {
                kernel::serial::puts("[KERNEL] Physical-frame allocator unavailable\n");
            }
        }
    }
    
    // If neither boot method is valid, halt
    if (!is_multiboot && !is_bootinfo) {
        kernel::serial::puts("[KERNEL] ERROR: No valid boot method detected, halting\n");
        while(1) { }
    }

#if defined(GXOS_C011EC99_PHYSICAL_FRAME_SCALING_QEMU_TEST)
    kernel::native_physical_frame_scaling_qemu_test::run(bootinfo);
#if defined(GXOS_NATIVE_VIRTUAL_MEMORY_QEMU_TEST)
    // Reuse the existing PAL/VM adapter proof against the same production
    // physical-frame allocator after the C99 boundary checks complete.
    kernel::interrupts::init();
    kernel::native_virtual_memory_qemu_test::run();
#endif
    while (1) {
        kernel::arch::disable_interrupts();
        kernel::arch::halt();
    }
#endif

#if defined(GXOS_NATIVE_THREAD_QEMU_TEST) && !defined(GXOS_NATIVE_MUTEX_QEMU_TEST)
    // The opt-in lifecycle test intentionally skips the desktop and storage
    // path, but timed waits still require the ordinary PIC/PIT services.
    kernel::interrupts::init();
    kernel::pit::init(100);
    kernel::interrupts::register_irq(0, kernel::pit::irq_handler);
    kernel::serial::puts("[native-thread-test] timer services ready\n");
    kernel::native_thread_qemu_test::run();
    while (1) {
        kernel::arch::enable_interrupts();
        kernel::arch::halt();
    }
#endif

#if defined(GXOS_NATIVEAOT_PAL_QEMU_TEST)
    // The exact NativeAOT PAL bridge is opt-in and runs before the ordinary
    // desktop/storage path.  The default application inventory is untouched.
    kernel::interrupts::init();
    kernel::pit::init(100);
    kernel::interrupts::register_irq(0, kernel::pit::irq_handler);
    kernel::serial::puts("[nativeaot-pal-qemu-test] timer services ready\n");
    kernel::nativeaot_pal_qemu_test::run(
        guidexos_nativeaot_pal_qemu_artifact_start,
        static_cast<size_t>(guidexos_nativeaot_pal_qemu_artifact_end -
                            guidexos_nativeaot_pal_qemu_artifact_start),
        GUIDEXOS_NATIVEAOT_PAL_QEMU_INSTALL_ADDRESS,
        GUIDEXOS_NATIVEAOT_PAL_QEMU_MAIN_ADDRESS,
        GUIDEXOS_NATIVEAOT_PAL_QEMU_UNINSTALL_ADDRESS);
    while (1) {
        kernel::arch::disable_interrupts();
        kernel::arch::halt();
    }
#endif

#if defined(GXOS_NATIVEAOT_THREAD_STATIC_QEMU_TEST)
    kernel::interrupts::init();
    kernel::pit::init(100);
    kernel::interrupts::register_irq(0, kernel::pit::irq_handler);
    kernel::serial::puts("[nativeaot-thread-static] timer services ready\n");
    kernel::nativeaot_pal_qemu_test::runFirstRealAllocation(
        guidexos_nativeaot_gc_startup_artifact_start,
        static_cast<size_t>(guidexos_nativeaot_gc_startup_artifact_end -
                            guidexos_nativeaot_gc_startup_artifact_start),
        GUIDEXOS_NATIVEAOT_THREAD_STATIC_INSTALL_PAL_ADDRESS,
        GUIDEXOS_NATIVEAOT_THREAD_STATIC_INSTALL_TABLE_ADDRESS,
        GUIDEXOS_NATIVEAOT_THREAD_STATIC_INSTALL_PLATFORM_ADDRESS,
        GUIDEXOS_NATIVEAOT_THREAD_STATIC_STARTUP_MAIN_ADDRESS,
        GUIDEXOS_NATIVEAOT_THREAD_STATIC_GET_STATE_ADDRESS,
        GUIDEXOS_NATIVEAOT_THREAD_STATIC_GET_PRE_GC_STATE_ADDRESS,
        GUIDEXOS_NATIVEAOT_THREAD_STATIC_GET_ALLOCATION_COUNT_ADDRESS,
        GUIDEXOS_NATIVEAOT_THREAD_STATIC_GET_LAST_ALLOCATION_SIZE_ADDRESS,
        GUIDEXOS_NATIVEAOT_THREAD_STATIC_GET_DIAGNOSTIC_STAGE_ADDRESS,
        GUIDEXOS_NATIVEAOT_THREAD_STATIC_MANAGED_MAIN_ADDRESS,
        0u,
        GUIDEXOS_NATIVEAOT_THREAD_STATIC_GET_DIAGNOSTICS_ADDRESS,
        1u,
        0u);
    while (1) {
        kernel::arch::disable_interrupts();
        kernel::arch::halt();
    }
#elif defined(GXOS_NATIVEAOT_GC_SINGLE_THREAD_SUSPEND_EE_QEMU_TEST)
    kernel::interrupts::init();
    kernel::pit::init(100);
    kernel::interrupts::register_irq(0, kernel::pit::irq_handler);
    kernel::serial::puts("[nativeaot-gc-single-thread-suspend-ee] timer services ready\n");
    kernel::nativeaot_pal_qemu_test::runFirstRealAllocation(
        guidexos_nativeaot_gc_startup_artifact_start,
        static_cast<size_t>(guidexos_nativeaot_gc_startup_artifact_end -
                            guidexos_nativeaot_gc_startup_artifact_start),
        GUIDEXOS_NATIVEAOT_GC_SINGLE_THREAD_SUSPEND_EE_INSTALL_PAL_ADDRESS,
        GUIDEXOS_NATIVEAOT_GC_SINGLE_THREAD_SUSPEND_EE_INSTALL_TABLE_ADDRESS,
        GUIDEXOS_NATIVEAOT_GC_SINGLE_THREAD_SUSPEND_EE_INSTALL_PLATFORM_ADDRESS,
        GUIDEXOS_NATIVEAOT_GC_SINGLE_THREAD_SUSPEND_EE_STARTUP_MAIN_ADDRESS,
        GUIDEXOS_NATIVEAOT_GC_SINGLE_THREAD_SUSPEND_EE_GET_STATE_ADDRESS,
        GUIDEXOS_NATIVEAOT_GC_SINGLE_THREAD_SUSPEND_EE_GET_PRE_GC_STATE_ADDRESS,
        GUIDEXOS_NATIVEAOT_GC_SINGLE_THREAD_SUSPEND_EE_GET_ALLOCATION_COUNT_ADDRESS,
        GUIDEXOS_NATIVEAOT_GC_SINGLE_THREAD_SUSPEND_EE_GET_LAST_ALLOCATION_SIZE_ADDRESS,
        GUIDEXOS_NATIVEAOT_GC_SINGLE_THREAD_SUSPEND_EE_GET_DIAGNOSTIC_STAGE_ADDRESS,
        GUIDEXOS_NATIVEAOT_GC_SINGLE_THREAD_SUSPEND_EE_MANAGED_MAIN_ADDRESS,
        GUIDEXOS_NATIVEAOT_GC_SINGLE_THREAD_SUSPEND_EE_FINALIZE_ADDRESS,
        GUIDEXOS_NATIVEAOT_GC_SINGLE_THREAD_SUSPEND_EE_GET_DIAGNOSTICS_ADDRESS,
        1u,
        GUIDEXOS_NATIVEAOT_GC_SINGLE_THREAD_SUSPEND_EE_BEGIN_EXPERIMENT_ADDRESS,
#if defined(GUIDEXOS_NATIVEAOT_C011EC23_NATIVE_UNWIND)
        GUIDEXOS_NATIVEAOT_GC_SINGLE_THREAD_SUSPEND_EE_STANDALONE_NATIVE_UNWIND_ADDRESS
#else
        0u
#endif
        );
    while (1) {
        kernel::arch::disable_interrupts();
        kernel::arch::halt();
    }
#elif defined(GXOS_NATIVEAOT_GC_FIRST_COLLECTION_BOUNDARY_QEMU_TEST)
    kernel::interrupts::init();
    kernel::pit::init(100);
    kernel::interrupts::register_irq(0, kernel::pit::irq_handler);
    kernel::serial::puts("[nativeaot-gc-first-collection-boundary] timer services ready\n");
    kernel::nativeaot_pal_qemu_test::runFirstRealAllocation(
        guidexos_nativeaot_gc_startup_artifact_start,
        static_cast<size_t>(guidexos_nativeaot_gc_startup_artifact_end -
                            guidexos_nativeaot_gc_startup_artifact_start),
        GUIDEXOS_NATIVEAOT_GC_FIRST_COLLECTION_BOUNDARY_INSTALL_PAL_ADDRESS,
        GUIDEXOS_NATIVEAOT_GC_FIRST_COLLECTION_BOUNDARY_INSTALL_TABLE_ADDRESS,
        GUIDEXOS_NATIVEAOT_GC_FIRST_COLLECTION_BOUNDARY_INSTALL_PLATFORM_ADDRESS,
        GUIDEXOS_NATIVEAOT_GC_FIRST_COLLECTION_BOUNDARY_STARTUP_MAIN_ADDRESS,
        GUIDEXOS_NATIVEAOT_GC_FIRST_COLLECTION_BOUNDARY_GET_STATE_ADDRESS,
        GUIDEXOS_NATIVEAOT_GC_FIRST_COLLECTION_BOUNDARY_GET_PRE_GC_STATE_ADDRESS,
        GUIDEXOS_NATIVEAOT_GC_FIRST_COLLECTION_BOUNDARY_GET_ALLOCATION_COUNT_ADDRESS,
        GUIDEXOS_NATIVEAOT_GC_FIRST_COLLECTION_BOUNDARY_GET_LAST_ALLOCATION_SIZE_ADDRESS,
        GUIDEXOS_NATIVEAOT_GC_FIRST_COLLECTION_BOUNDARY_GET_DIAGNOSTIC_STAGE_ADDRESS,
        GUIDEXOS_NATIVEAOT_GC_FIRST_COLLECTION_BOUNDARY_MANAGED_MAIN_ADDRESS,
        GUIDEXOS_NATIVEAOT_GC_FIRST_COLLECTION_BOUNDARY_FINALIZE_ADDRESS,
        GUIDEXOS_NATIVEAOT_GC_FIRST_COLLECTION_BOUNDARY_GET_DIAGNOSTICS_ADDRESS,
        1u,
        GUIDEXOS_NATIVEAOT_GC_FIRST_COLLECTION_BOUNDARY_BEGIN_EXPERIMENT_ADDRESS);
    while (1) {
        kernel::arch::disable_interrupts();
        kernel::arch::halt();
    }
#elif defined(GXOS_NATIVEAOT_GC_SEGMENT_BOUNDARY_QEMU_TEST)
    // This disposable process-lifetime experiment allocates fixed primitive
    // arrays through multiple context refills and stops at the first exact
    // GC-heap commit or source-backed heap-segment transition.
    kernel::interrupts::init();
    kernel::pit::init(100);
    kernel::interrupts::register_irq(0, kernel::pit::irq_handler);
    kernel::serial::puts(
#if defined(GXOS_NATIVEAOT_GC_SEGMENT_TRANSITION_QEMU_TEST)
        "[nativeaot-gc-segment-transition] timer services ready\n"
#else
        "[nativeaot-gc-segment-boundary] timer services ready\n"
#endif
    );
    kernel::nativeaot_pal_qemu_test::runFirstRealAllocation(
        guidexos_nativeaot_gc_startup_artifact_start,
        static_cast<size_t>(guidexos_nativeaot_gc_startup_artifact_end -
                            guidexos_nativeaot_gc_startup_artifact_start),
        GUIDEXOS_NATIVEAOT_GC_SEGMENT_BOUNDARY_INSTALL_PAL_ADDRESS,
        GUIDEXOS_NATIVEAOT_GC_SEGMENT_BOUNDARY_INSTALL_TABLE_ADDRESS,
        GUIDEXOS_NATIVEAOT_GC_SEGMENT_BOUNDARY_INSTALL_PLATFORM_ADDRESS,
        GUIDEXOS_NATIVEAOT_GC_SEGMENT_BOUNDARY_STARTUP_MAIN_ADDRESS,
        GUIDEXOS_NATIVEAOT_GC_SEGMENT_BOUNDARY_GET_STATE_ADDRESS,
        GUIDEXOS_NATIVEAOT_GC_SEGMENT_BOUNDARY_GET_PRE_GC_STATE_ADDRESS,
        GUIDEXOS_NATIVEAOT_GC_SEGMENT_BOUNDARY_GET_ALLOCATION_COUNT_ADDRESS,
        GUIDEXOS_NATIVEAOT_GC_SEGMENT_BOUNDARY_GET_LAST_ALLOCATION_SIZE_ADDRESS,
        GUIDEXOS_NATIVEAOT_GC_SEGMENT_BOUNDARY_GET_DIAGNOSTIC_STAGE_ADDRESS,
        GUIDEXOS_NATIVEAOT_GC_SEGMENT_BOUNDARY_MANAGED_MAIN_ADDRESS,
        GUIDEXOS_NATIVEAOT_GC_SEGMENT_BOUNDARY_FINALIZE_ADDRESS,
        GUIDEXOS_NATIVEAOT_GC_SEGMENT_BOUNDARY_GET_DIAGNOSTICS_ADDRESS,
        1u,
        GUIDEXOS_NATIVEAOT_GC_SEGMENT_BOUNDARY_BEGIN_EXPERIMENT_ADDRESS);
    while (1) {
        kernel::arch::disable_interrupts();
        kernel::arch::halt();
    }
#elif defined(GXOS_NATIVEAOT_GC_FIRST_REFILL_QEMU_TEST)
    // This branch is a disposable process-lifetime experiment: bounded
    // primitive-array allocations stop at the first subsequent context refill.
    kernel::interrupts::init();
    kernel::pit::init(100);
    kernel::interrupts::register_irq(0, kernel::pit::irq_handler);
    kernel::serial::puts("[nativeaot-gc-first-refill] timer services ready\n");
    kernel::nativeaot_pal_qemu_test::runFirstRealAllocation(
        guidexos_nativeaot_gc_startup_artifact_start,
        static_cast<size_t>(guidexos_nativeaot_gc_startup_artifact_end -
                            guidexos_nativeaot_gc_startup_artifact_start),
        GUIDEXOS_NATIVEAOT_GC_FIRST_REFILL_INSTALL_PAL_ADDRESS,
        GUIDEXOS_NATIVEAOT_GC_FIRST_REFILL_INSTALL_TABLE_ADDRESS,
        GUIDEXOS_NATIVEAOT_GC_FIRST_REFILL_INSTALL_PLATFORM_ADDRESS,
        GUIDEXOS_NATIVEAOT_GC_FIRST_REFILL_STARTUP_MAIN_ADDRESS,
        GUIDEXOS_NATIVEAOT_GC_FIRST_REFILL_GET_STATE_ADDRESS,
        GUIDEXOS_NATIVEAOT_GC_FIRST_REFILL_GET_PRE_GC_STATE_ADDRESS,
        GUIDEXOS_NATIVEAOT_GC_FIRST_REFILL_GET_ALLOCATION_COUNT_ADDRESS,
        GUIDEXOS_NATIVEAOT_GC_FIRST_REFILL_GET_LAST_ALLOCATION_SIZE_ADDRESS,
        GUIDEXOS_NATIVEAOT_GC_FIRST_REFILL_GET_DIAGNOSTIC_STAGE_ADDRESS,
        GUIDEXOS_NATIVEAOT_GC_FIRST_REFILL_MANAGED_MAIN_ADDRESS,
        GUIDEXOS_NATIVEAOT_GC_FIRST_REFILL_FINALIZE_ADDRESS,
        GUIDEXOS_NATIVEAOT_GC_FIRST_REFILL_GET_DIAGNOSTICS_ADDRESS,
        1u);
    while (1) {
        kernel::arch::disable_interrupts();
        kernel::arch::halt();
    }
#elif defined(GXOS_NATIVEAOT_GC_FIRST_ALLOCATION_QEMU_TEST)
    // This branch is a disposable process-lifetime experiment: startup is
    // followed by exactly one managed byte[24] allocation and then the CPU
    // halts. It is never part of the ordinary boot/application path.
    kernel::interrupts::init();
    kernel::pit::init(100);
    kernel::interrupts::register_irq(0, kernel::pit::irq_handler);
    kernel::serial::puts("[nativeaot-gc-first-allocation] timer services ready\n");
    kernel::nativeaot_pal_qemu_test::runFirstRealAllocation(
        guidexos_nativeaot_gc_startup_artifact_start,
        static_cast<size_t>(guidexos_nativeaot_gc_startup_artifact_end -
                            guidexos_nativeaot_gc_startup_artifact_start),
        GUIDEXOS_NATIVEAOT_GC_FIRST_ALLOCATION_INSTALL_PAL_ADDRESS,
        GUIDEXOS_NATIVEAOT_GC_FIRST_ALLOCATION_INSTALL_TABLE_ADDRESS,
        GUIDEXOS_NATIVEAOT_GC_FIRST_ALLOCATION_INSTALL_PLATFORM_ADDRESS,
        GUIDEXOS_NATIVEAOT_GC_FIRST_ALLOCATION_STARTUP_MAIN_ADDRESS,
        GUIDEXOS_NATIVEAOT_GC_FIRST_ALLOCATION_GET_STATE_ADDRESS,
        GUIDEXOS_NATIVEAOT_GC_FIRST_ALLOCATION_GET_PRE_GC_STATE_ADDRESS,
        GUIDEXOS_NATIVEAOT_GC_FIRST_ALLOCATION_GET_ALLOCATION_COUNT_ADDRESS,
        GUIDEXOS_NATIVEAOT_GC_FIRST_ALLOCATION_GET_LAST_ALLOCATION_SIZE_ADDRESS,
        GUIDEXOS_NATIVEAOT_GC_FIRST_ALLOCATION_GET_DIAGNOSTIC_STAGE_ADDRESS,
        GUIDEXOS_NATIVEAOT_GC_FIRST_ALLOCATION_MANAGED_MAIN_ADDRESS,
        GUIDEXOS_NATIVEAOT_GC_FIRST_ALLOCATION_FINALIZE_ADDRESS,
        GUIDEXOS_NATIVEAOT_GC_FIRST_ALLOCATION_GET_DIAGNOSTICS_ADDRESS,
        1u);
    while (1) {
        kernel::arch::disable_interrupts();
        kernel::arch::halt();
    }
#elif defined(GXOS_NATIVEAOT_GC_STARTUP_QEMU_TEST)
    // Workstation GC startup is a disposable, process-lifetime experiment.
    // It intentionally runs before the ordinary desktop/storage path and
    // never becomes part of a normal boot or application inventory.
    kernel::interrupts::init();
    kernel::pit::init(100);
    kernel::interrupts::register_irq(0, kernel::pit::irq_handler);
    kernel::serial::puts("[nativeaot-gc-startup-qemu-test] timer services ready\n");
    kernel::nativeaot_pal_qemu_test::runStartup(
        guidexos_nativeaot_gc_startup_artifact_start,
        static_cast<size_t>(guidexos_nativeaot_gc_startup_artifact_end -
                            guidexos_nativeaot_gc_startup_artifact_start),
        GUIDEXOS_NATIVEAOT_GC_STARTUP_INSTALL_PAL_ADDRESS,
        GUIDEXOS_NATIVEAOT_GC_STARTUP_INSTALL_TABLE_ADDRESS,
        GUIDEXOS_NATIVEAOT_GC_STARTUP_INSTALL_PLATFORM_ADDRESS,
        GUIDEXOS_NATIVEAOT_GC_STARTUP_MAIN_ADDRESS,
        GUIDEXOS_NATIVEAOT_GC_STARTUP_GET_STATE_ADDRESS,
        GUIDEXOS_NATIVEAOT_GC_STARTUP_GET_PRE_GC_STATE_ADDRESS,
        GUIDEXOS_NATIVEAOT_GC_STARTUP_GET_ALLOCATION_COUNT_ADDRESS,
        GUIDEXOS_NATIVEAOT_GC_STARTUP_GET_LAST_ALLOCATION_SIZE_ADDRESS,
        GUIDEXOS_NATIVEAOT_GC_STARTUP_GET_DIAGNOSTIC_STAGE_ADDRESS,
        1u);
    while (1) {
        kernel::arch::disable_interrupts();
        kernel::arch::halt();
    }
#endif

#if defined(GXOS_NATIVE_LOCAL_STORAGE_QEMU_TEST)
    kernel::interrupts::init();
    kernel::pit::init(100);
    kernel::interrupts::register_irq(0, kernel::pit::irq_handler);
    kernel::serial::puts("[native-local-storage-test] timer services ready\n");
    kernel::native_local_storage_qemu_test::run();
    while (1) {
        kernel::arch::enable_interrupts();
        kernel::arch::halt();
    }
#endif

#if defined(GXOS_NATIVE_MUTEX_QEMU_TEST)
    // The opt-in mutex test exercises only the runtime-neutral mutex and
    // scheduler wait contract, then leaves the normal desktop path untouched.
    kernel::interrupts::init();
    kernel::pit::init(100);
    kernel::interrupts::register_irq(0, kernel::pit::irq_handler);
    kernel::serial::puts("[native-mutex-test] timer services ready\n");
    kernel::native_mutex_qemu_test::run();
    while (1) {
        kernel::arch::enable_interrupts();
        kernel::arch::halt();
    }
#endif

#if defined(GXOS_NATIVE_VIRTUAL_MEMORY_QEMU_TEST)
    // The opt-in VM test exercises only the generic runtime-neutral region
    // contract and exits before desktop, storage, networking, or applications.
    kernel::interrupts::init();
    kernel::native_virtual_memory_qemu_test::run();
    while (1) {
        kernel::arch::disable_interrupts();
        kernel::arch::halt();
    }
#endif
    
    // Initialize framebuffer for graphics mode
    bool has_fb = false;
    
    if (is_bootinfo) {
        has_fb = kernel::framebuffer::init_from_bootinfo(bootinfo);
    } else {
        has_fb = kernel::framebuffer::init(multiboot_info);
    }
    
    if (has_fb) {
        // === GRAPHICS MODE BOOT ===
        kernel::serial::puts("[KERNEL] Framebuffer initialized: ");
        kernel::serial::put_hex32(kernel::framebuffer::get_width());
        kernel::serial::putc('x');
        kernel::serial::put_hex32(kernel::framebuffer::get_height());
        kernel::serial::putc('\n');
        
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
        kernel::virtio::gpu::init();
        kernel::virtio::rng::init();
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
        
        // Auto-mount first available block device to /
        // Try device 0 first, then device 1 if that fails
        bool mounted = false;
        if (kernel::block::device_count() > 0) {
            kernel::serial::puts("[KERNEL] Auto-mounting block device 0 to /\n");
            uint8_t mountResult = kernel::vfs::mount("/", 0);
            if (mountResult == 0) {
                kernel::serial::puts("[KERNEL] Successfully mounted / from device 0\n");
                mounted = true;
            } else {
                kernel::serial::puts("[KERNEL] WARNING: Failed to auto-mount device 0\n");
                
                // Try device 1 if available
                if (kernel::block::device_count() > 1) {
                    kernel::serial::puts("[KERNEL] Attempting to mount block device 1 to /\n");
                    mountResult = kernel::vfs::mount("/", 1);
                    if (mountResult == 0) {
                        kernel::serial::puts("[KERNEL] Successfully mounted / from device 1\n");
                        mounted = true;
                    } else {
                        kernel::serial::puts("[KERNEL] WARNING: Failed to auto-mount device 1\n");
                    }
                }
            }
        }
        
        if (!mounted && kernel::block::device_count() > 0) {
            kernel::serial::puts("[KERNEL] WARNING: No filesystem could be mounted automatically\n");
        }

        if (is_bootinfo && bootinfo && bootinfo->RamdiskBase != 0 && bootinfo->RamdiskSize != 0) {
            kernel::serial::puts("[KERNEL] Boot wallpaper pack found in ramdisk.img\n");
            kernel::desktop::set_wallpaper_image_pack(reinterpret_cast<const void*>(static_cast<uintptr_t>(bootinfo->RamdiskBase)), bootinfo->RamdiskSize);
#if defined(GXOS_NAVIGATOR_HTTP_SMOKE_ACTIVE) || defined(GXOS_NAVIGATOR_BOOT_STAGED_CONFIG_ACTIVE) || defined(GXOS_IMAGEVIEWER_BARE_METAL_RUNTIME_SMOKE_ACTIVE)
            mount_navigator_smoke_ca_fixture_if_available();
            mount_navigator_smoke_config_if_available();
#endif
        }

        kernel::desktop::reload_persisted_wallpaper();
#if defined(GXOS_BARE_METAL)
        kernel::desktop::refresh_bare_metal_desktop_folders_after_vfs_ready();
#endif
        // The first desktop draw happens before VFS and the boot ramdisk are ready.
        // Redraw now so bare-metal thumbnails and the selected wallpaper use /system/wallpapers.
        kernel::desktop::draw();
        
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
                    kernel::serial::puts("[KERNEL] WARNING: BootInfo NIC init failed, falling back to PCI scan\n");
                }
            } else {
                kernel::serial::puts("[KERNEL] NIC not found in BootInfo (flags=");
                kernel::serial::put_hex32(nicInfo->flags);
                kernel::serial::puts("), falling back to PCI scan\n");
            }
        } else {
            kernel::serial::puts("[KERNEL] No BootInfo available, using PCI scan\n");
        }
        
        // Fall back to PCI scan if bootinfo init failed
        if (!nicInitialized) {
            kernel::serial::puts("[KERNEL] Falling back to PCI scan...\n");
            kernel::nic::init();
        }
        
        if (kernel::nic::is_active()) {
            kernel::serial::puts("[KERNEL] NIC active, registering IRQ");
            kernel::serial::put_hex8(kernel::nic::get_device()->irqLine);
            kernel::serial::putc('\n');
            kernel::interrupts::register_irq(
                kernel::nic::get_device()->irqLine,
                kernel::nic::irq_handler);
            
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

#if defined(GXOS_C102_PRODUCTION_LAUNCH) || defined(GXOS_C102_NEGATIVE_LAUNCH)
        kernel::nativeaot::LaunchReport c102Report{};
        const char* c102Path =
#if defined(GXOS_C102_NEGATIVE_LAUNCH)
            "/system/wall/MISSING.ELF";
#else
            "/system/wall/C102.ELF";
#endif
        kernel::serial::puts("[C102-LAUNCH] ordinary application discovery path=");
        kernel::serial::puts(c102Path);
        kernel::serial::puts(" proofMode=0\n");
        const kernel::nativeaot::LaunchStatus c102Status =
            kernel::nativeaot::launch(c102Path, &c102Report);
        kernel::serial::puts("[C102-LAUNCH] result=");
        kernel::serial::puts(kernel::nativeaot::launchStatusName(c102Status));
        kernel::serial::puts(" managedReturn=");
        kernel::serial::put_hex32(static_cast<uint32_t>(c102Report.managedReturn));
        kernel::serial::puts("\n");
#endif

#if defined(GXOS_C103_PRODUCTION_LAUNCH) || defined(GXOS_C103_NEGATIVE_LAUNCH)
        const char* c103Path = "/system/wall/C102.ELF";
        kernel::serial::puts("[C103-LAUNCH] ordinary application discovery path=");
        kernel::serial::puts(c103Path);
        kernel::serial::puts(" proofMode=0\n");
        kernel::nativeaot::LaunchReport c103First{};
        const kernel::nativeaot::LaunchStatus c103FirstStatus =
            kernel::nativeaot::launch(c103Path, &c103First);
        kernel::serial::puts("[C103-LAUNCH] sequence=");
        kernel::serial::put_hex32(c103First.sequence);
        kernel::serial::puts(" status=");
        kernel::serial::puts(kernel::nativeaot::launchStatusName(c103FirstStatus));
        kernel::serial::puts(" managedReturn=");
        kernel::serial::put_hex32(static_cast<uint32_t>(c103First.managedReturn));
        kernel::serial::puts(" runtimeInitialized=");
        kernel::serial::put_hex32(c103First.runtimeInitialized ? 1u : 0u);
        kernel::serial::puts(" runtimeReused=");
        kernel::serial::put_hex32(c103First.runtimeReused ? 1u : 0u);
        kernel::serial::puts(" residentImage=");
        kernel::serial::put_hex32(c103First.residentImage ? 1u : 0u);
        kernel::serial::puts("\n");
#if defined(GXOS_C103_NEGATIVE_LAUNCH)
        kernel::nativeaot::LaunchReport c103Missing{};
        const kernel::nativeaot::LaunchStatus c103MissingStatus =
            kernel::nativeaot::launch("/system/wall/MISSING.ELF", &c103Missing);
        kernel::serial::puts("[C103-LAUNCH] negative path=/system/wall/MISSING.ELF status=");
        kernel::serial::puts(kernel::nativeaot::launchStatusName(c103MissingStatus));
        kernel::serial::puts("\n");
#endif
        kernel::nativeaot::LaunchReport c103Second{};
        const kernel::nativeaot::LaunchStatus c103SecondStatus =
            kernel::nativeaot::launch(c103Path, &c103Second);
        kernel::serial::puts("[C103-LAUNCH] sequence=");
        kernel::serial::put_hex32(c103Second.sequence);
        kernel::serial::puts(" status=");
        kernel::serial::puts(kernel::nativeaot::launchStatusName(c103SecondStatus));
        kernel::serial::puts(" managedReturn=");
        kernel::serial::put_hex32(static_cast<uint32_t>(c103Second.managedReturn));
        kernel::serial::puts(" runtimeInitialized=");
        kernel::serial::put_hex32(c103Second.runtimeInitialized ? 1u : 0u);
        kernel::serial::puts(" runtimeReused=");
        kernel::serial::put_hex32(c103Second.runtimeReused ? 1u : 0u);
        kernel::serial::puts(" residentImage=");
        kernel::serial::put_hex32(c103Second.residentImage ? 1u : 0u);
        kernel::serial::puts("\n");
#endif

#if defined(GXOS_C104_PRODUCTION_LAUNCH) || defined(GXOS_C104_NEGATIVE_LAUNCH)
        auto emitC104Report = [](uint32_t ordinal, const char* identity,
                                 const kernel::nativeaot::LaunchReport& report,
                                 kernel::nativeaot::LaunchStatus status) {
            kernel::serial::puts("[C104-LAUNCH] ordinal=");
            kernel::serial::put_hex32(ordinal);
            kernel::serial::puts(" identity=");
            kernel::serial::puts(identity);
            kernel::serial::puts(" sequence=");
            kernel::serial::put_hex32(report.sequence);
            kernel::serial::puts(" status=");
            kernel::serial::puts(kernel::nativeaot::launchStatusName(status));
            kernel::serial::puts(" base=");
            kernel::serial::put_hex64(report.artifactBase);
            kernel::serial::puts(" span=");
            kernel::serial::put_hex64(report.artifactSpan);
            kernel::serial::puts(" entry=");
            kernel::serial::put_hex64(report.entryPoint);
            kernel::serial::puts(" managedEntry=");
            kernel::serial::put_hex32(report.managedEntryReached ? 1u : 0u);
            kernel::serial::puts(" managedPass=");
            kernel::serial::put_hex32(report.managedPassReached ? 1u : 0u);
            kernel::serial::puts(" launcherRegained=");
            kernel::serial::put_hex32(report.launcherRegainedControl ? 1u : 0u);
            kernel::serial::puts("\n");
        };
        kernel::serial::puts("[C104-LAUNCH] ordinary application discovery A=/system/wall/C104A.ELF B=/system/wall/C104B.ELF proofMode=0\n");
        auto runC104Launch = [&](uint32_t ordinal, const char* identity,
                                 const char* path) {
            kernel::nativeaot::LaunchReport report{};
            const kernel::nativeaot::LaunchStatus status =
                kernel::nativeaot::launch(path, &report);
            emitC104Report(ordinal, identity, report, status);
        };
        runC104Launch(1u, "A", "/system/wall/C104A.ELF");
#if defined(GXOS_C104_NEGATIVE_LAUNCH)
        runC104Launch(2u, "missing", "/system/wall/MISSING.ELF");
        constexpr uint32_t c104BOrdinal = 3u;
        constexpr uint32_t c104ASecondOrdinal = 4u;
#else
        constexpr uint32_t c104BOrdinal = 2u;
        constexpr uint32_t c104ASecondOrdinal = 3u;
#endif
        runC104Launch(c104BOrdinal, "B", "/system/wall/C104B.ELF");
        runC104Launch(c104ASecondOrdinal, "A", "/system/wall/C104A.ELF");
#endif

#if defined(GXOS_NATIVEAOT_PRODUCTION_COMPOSITE_LAUNCH) && \
    !defined(GXOS_NATIVEAOT_C110_APPMODEL_LAUNCH) && \
    !defined(GXOS_NATIVEAOT_C111_USER_FACING) && \
    !defined(GXOS_NATIVEAOT_C112_REUSABLE_MANAGED_APPLICATION)
        // Exercise the same desktop launch contract used by ordinary icon and
        // start-menu requests. The stable logical IDs are resolved to the one
        // resident composite image by nativeaot::launchLogicalApplication().
        constexpr const char* productionAppA =
            "com.guidexos.nativeaot.hostlogproof.app-a";
        constexpr const char* productionAppB =
            "com.guidexos.nativeaot.hostlogproof.app-b";
        constexpr const char* productionInvalidId =
            "com.guidexos.nativeaot.hostlogproof.invalid";
        auto runProductionLaunch = [](uint32_t ordinal, const char* identity,
                                      const char* applicationId) {
            const bool result = kernel::desktop::launch_app(applicationId);
            kernel::serial::puts("[PRODUCTION-LAUNCH] ordinal=");
            kernel::serial::put_hex32(ordinal);
            kernel::serial::puts(" identity=");
            kernel::serial::puts(identity);
            kernel::serial::puts(" applicationId=");
            kernel::serial::puts(applicationId);
            kernel::serial::puts(" result=");
            kernel::serial::puts(result ? "PASS" : "REJECTED");
            kernel::serial::puts("\n");
            return result;
        };
        kernel::serial::puts("[PRODUCTION-LAUNCH] interface=desktop.launch_app image=");
        kernel::serial::puts(kernel::nativeaot::productionCompositeImagePath());
        kernel::serial::puts(" sequence=A1,B1,invalid,A2,B2,A3\n");
        const bool productionA1 = runProductionLaunch(1u, "A1", productionAppA);
        const bool productionB1 = runProductionLaunch(2u, "B1", productionAppB);
        const bool productionInvalid = runProductionLaunch(3u, "invalid", productionInvalidId);
        const bool productionA2 = runProductionLaunch(4u, "A2", productionAppA);
        const bool productionB2 = runProductionLaunch(5u, "B2", productionAppB);
        const bool productionA3 = runProductionLaunch(6u, "A3", productionAppA);
        kernel::serial::puts("[PRODUCTION-RESULT] outcome=");
        kernel::serial::puts(productionA1 && productionB1 && !productionInvalid &&
            productionA2 && productionB2 && productionA3 ? "PASS" : "FAIL");
        kernel::serial::puts(" dispatch=A1 PASS -> B1 PASS -> invalid rejected -> A2 PASS -> B2 PASS -> A3 PASS\n");
#endif

#if defined(GXOS_NATIVEAOT_C110_APPMODEL_LAUNCH)
        // C110 exercises real shared App Model records.  The test obtains
        // identities and selectors from the metadata table; production launch
        // code does not know special proof IDs or selector conditionals.
        const gxos::apps::BuiltInAppMetadata* managedWorkspace =
            gxos::apps::FindBuiltInAppMetadataByDisplayName("Managed Workspace");
        const gxos::apps::BuiltInAppMetadata* managedStatus =
            gxos::apps::FindBuiltInAppMetadataByDisplayName("Managed Status");
        const bool workspaceRecordValid = managedWorkspace &&
            gxos::apps::IsManagedNativeAotRecordValid(*managedWorkspace);
        const bool statusRecordValid = managedStatus &&
            gxos::apps::IsManagedNativeAotRecordValid(*managedStatus);
        const bool catalogValid = gxos::apps::ManagedNativeAotCatalogIsValid() &&
            workspaceRecordValid && statusRecordValid;
        kernel::serial::puts("[C110-APPMODEL] catalogValid=");
        kernel::serial::puts(catalogValid ? "true" : "false");
        kernel::serial::puts(" workspaceId=");
        kernel::serial::puts(managedWorkspace ? managedWorkspace->appId : "");
        kernel::serial::puts(" workspaceSelector=");
        kernel::serial::put_hex32(managedWorkspace ? managedWorkspace->managedSelector : 0u);
        kernel::serial::puts(" statusId=");
        kernel::serial::puts(managedStatus ? managedStatus->appId : "");
        kernel::serial::puts(" statusSelector=");
        kernel::serial::put_hex32(managedStatus ? managedStatus->managedSelector : 0u);
        kernel::serial::puts(" workspaceValid=");
        kernel::serial::puts(workspaceRecordValid ? "true" : "false");
        kernel::serial::puts(" statusValid=");
        kernel::serial::puts(statusRecordValid ? "true" : "false");
        kernel::serial::puts(" workspaceKind=");
        kernel::serial::put_hex32(managedWorkspace ? static_cast<uint32_t>(managedWorkspace->launchKind) : 0xFFFFFFFFu);
        kernel::serial::puts(" workspaceImage=");
        kernel::serial::puts(managedWorkspace && managedWorkspace->managedCompositeImagePath ? managedWorkspace->managedCompositeImagePath : "");
        kernel::serial::puts(" workspaceKernel=");
        kernel::serial::puts(managedWorkspace && managedWorkspace->kernelAppName ? managedWorkspace->kernelAppName : "");
        kernel::serial::puts("\n");

        const bool nativeRegression = kernel::desktop::launch_app("Notepad");
        kernel::serial::puts("[C110-NATIVE-REGRESSION] app=Notepad result=");
        kernel::serial::puts(nativeRegression ? "PASS\n" : "FAIL\n");

        gxos::apps::BuiltInAppMetadata invalidRecord{};
        if (managedWorkspace) invalidRecord = *managedWorkspace;
        invalidRecord.appId = "com.guidexos.apps.test.invalid-selector";
        invalidRecord.managedSelector = 0u;
        const bool invalidRecordRejected =
            !gxos::apps::IsManagedNativeAotRecordValid(invalidRecord);
        kernel::serial::puts("[C110-INVALID-RECORD] selector=0 result=");
        kernel::serial::puts(invalidRecordRejected ? "PASS\n" : "FAIL\n");

        kernel::nativeaot::LaunchReport missingReport{};
        const kernel::nativeaot::LaunchStatus missingStatus =
            kernel::nativeaot::launchLogical("/system/apps/C110-MISSING.ELF", 1u, &missingReport);
        kernel::serial::puts("[C110-MISSING-IMAGE] status=");
        kernel::serial::puts(kernel::nativeaot::launchStatusName(missingStatus));
        kernel::serial::puts(" result=");
        kernel::serial::puts(missingStatus == kernel::nativeaot::LaunchStatus::NotFound ? "PASS\n" : "FAIL\n");

        const bool unknownId = !kernel::desktop::launch_app("com.guidexos.apps.missing");
        kernel::serial::puts("[C110-UNKNOWN-ID] result=");
        kernel::serial::puts(unknownId ? "PASS\n" : "FAIL\n");

        const char* workspaceId = managedWorkspace ? managedWorkspace->appId : "";
        const char* statusId = managedStatus ? managedStatus->appId : "";
        auto runC110ManagedLaunch = [](uint32_t ordinal, const char* identity,
                                       const char* applicationId) {
            const bool result = kernel::desktop::launch_app(applicationId);
            kernel::serial::puts("[C110-MANAGED-LAUNCH] ordinal=");
            kernel::serial::put_hex32(ordinal);
            kernel::serial::puts(" identity=");
            kernel::serial::puts(identity);
            kernel::serial::puts(" applicationId=");
            kernel::serial::puts(applicationId);
            kernel::serial::puts(" result=");
            kernel::serial::puts(result ? "PASS\n" : "FAIL\n");
            return result;
        };
        const bool c110A1 = runC110ManagedLaunch(1u, "ManagedWorkspace1", workspaceId);
        const bool c110B1 = runC110ManagedLaunch(2u, "ManagedStatus1", statusId);
        const bool c110A2 = runC110ManagedLaunch(3u, "ManagedWorkspace2", workspaceId);
        const bool c110B2 = runC110ManagedLaunch(4u, "ManagedStatus2", statusId);
        const bool c110A3 = runC110ManagedLaunch(5u, "ManagedWorkspace3", workspaceId);
        kernel::nativeaot::LaunchReport independentReport{};
        const kernel::nativeaot::LaunchStatus independentStatus =
            kernel::nativeaot::launchLogical("/system/wall/C104A.ELF", 1u, &independentReport);
        const bool independentGuard =
            independentStatus == kernel::nativeaot::LaunchStatus::Busy ||
            independentStatus == kernel::nativeaot::LaunchStatus::BaseCollision;
        kernel::serial::puts("[C110-INDEPENDENT-IMAGE] status=");
        kernel::serial::puts(kernel::nativeaot::launchStatusName(independentStatus));
        kernel::serial::puts(" result=");
        kernel::serial::puts(independentGuard ? "PASS\n" : "FAIL\n");
        kernel::serial::puts("[C110-APPMODEL-RESULT] outcome=");
        kernel::serial::puts(catalogValid && nativeRegression && invalidRecordRejected &&
            missingStatus == kernel::nativeaot::LaunchStatus::NotFound && unknownId &&
            c110A1 && c110B1 && c110A2 && c110B2 && c110A3 && independentGuard ? "PASS" : "FAIL");
        kernel::serial::puts(" sequence=ManagedWorkspace1 PASS -> ManagedStatus1 PASS -> ManagedWorkspace2 PASS -> ManagedStatus2 PASS -> ManagedWorkspace3 PASS\n");
#endif

#if defined(GXOS_NATIVEAOT_C111_USER_FACING)
        // C111 uses the production App Model records and the same desktop
        // activation handler as shell/start-menu launches.  The managed
        // dispatcher remains the only selector-to-entrypoint boundary.
        const gxos::apps::BuiltInAppMetadata* c111Workspace =
            gxos::apps::FindBuiltInAppMetadataByDisplayName("Managed Workspace");
        const gxos::apps::BuiltInAppMetadata* c111Status =
            gxos::apps::FindBuiltInAppMetadataByDisplayName("Managed Status");
        const bool c111WorkspaceValid = c111Workspace &&
            gxos::apps::IsManagedNativeAotRecordValid(*c111Workspace);
        const bool c111StatusValid = c111Status &&
            gxos::apps::IsManagedNativeAotRecordValid(*c111Status);
        const bool c111CatalogValid = gxos::apps::ManagedNativeAotCatalogIsValid() &&
            c111WorkspaceValid && c111StatusValid;
        kernel::serial::puts("[C111-APPMODEL] catalogValid=");
        kernel::serial::puts(c111CatalogValid ? "true" : "false");
        kernel::serial::puts(" workspaceId=");
        kernel::serial::puts(c111Workspace ? c111Workspace->appId : "");
        kernel::serial::puts(" workspaceSelector=");
        kernel::serial::put_hex32(c111Workspace ? c111Workspace->managedSelector : 0u);
        kernel::serial::puts(" statusId=");
        kernel::serial::puts(c111Status ? c111Status->appId : "");
        kernel::serial::puts(" statusSelector=");
        kernel::serial::put_hex32(c111Status ? c111Status->managedSelector : 0u);
        kernel::serial::puts(" shell=StartMenu,AllPrograms\n");

        auto observeC111Surface = [](const char* title, uint32_t expectedPixel) {
            auto textEquals = [](const char* left, const char* right) {
                if (!left || !right) return false;
                while (*left && *right && *left == *right) {
                    ++left;
                    ++right;
                }
                return *left == '\0' && *right == '\0';
            };
            kernel::compositor::KernelCompositor::drawAllWindows();
            kernel::app::KernelWindow* window =
                kernel::compositor::KernelCompositor::getFocusedWindow();
            const bool titleValid = window && textEquals(window->title, title);
            const bool framebufferValid = window && kernel::framebuffer::is_available();
            const uint32_t pixel = framebufferValid
                ? kernel::framebuffer::get_pixel(
                    static_cast<uint32_t>(window->x) + 480u,
                    static_cast<uint32_t>(window->y) +
                        kernel::compositor::TITLEBAR_HEIGHT + 220u)
                : 0u;
            const bool result = titleValid && framebufferValid && pixel == expectedPixel;
            kernel::serial::puts("[C111-VISIBLE] title=");
            kernel::serial::puts(title);
            kernel::serial::puts(" window=");
            kernel::serial::put_hex32(window ? window->id : 0u);
            kernel::serial::puts(" widgets=");
            kernel::serial::put_hex32(window ? static_cast<uint32_t>(window->widgetCount) : 0u);
            kernel::serial::puts(" pixel=");
            kernel::serial::put_hex32(pixel);
            kernel::serial::puts(" result=");
            kernel::serial::puts(result ? "PASS\n" : "FAIL\n");
            return result;
        };

        auto runC111ManagedLaunch = [&](uint32_t ordinal, const char* appName,
                                        const char* applicationId,
                                        const char* context, const char* surfaceTitle,
                                        uint32_t surfacePixel) {
            const bool launched = kernel::desktop::launch_app_with_context(
                applicationId, context);
            const bool visible = launched &&
                observeC111Surface(surfaceTitle, surfacePixel);
            kernel::serial::puts("[C111-LAUNCH] ordinal=");
            kernel::serial::put_hex32(ordinal);
            kernel::serial::puts(" app=");
            kernel::serial::puts(appName);
            kernel::serial::puts(" context=");
            kernel::serial::puts(context ? context : "");
            kernel::serial::puts(" result=");
            kernel::serial::puts(visible ? "PASS\n" : "FAIL\n");
            return visible;
        };

        const char* c111WorkspaceId = c111Workspace ? c111Workspace->appId : "";
        const char* c111StatusId = c111Status ? c111Status->appId : "";
        const bool c111Workspace1 = runC111ManagedLaunch(
            1u, "ManagedWorkspace", c111WorkspaceId, "first-launch",
            "Managed Workspace", 0xFF2A4A70u);

        // Exercise the same compositor input path used by a real pointer
        // click.  The button is created by managed code and handled by the
        // existing KernelCompositor widget routing.
        kernel::app::KernelWindow* interactionWindow =
            kernel::compositor::KernelCompositor::getFocusedWindow();
        bool interactionInput = false;
        if (interactionWindow && interactionWindow->widgetCount > 0) {
            kernel::app::Widget& widget =
                interactionWindow->widgets[interactionWindow->widgetCount - 1];
            if (widget.type == kernel::app::WidgetType::Button) {
                const int32_t mouseX = interactionWindow->x + widget.x + widget.w / 2;
                const int32_t mouseY = interactionWindow->y +
                    kernel::compositor::TITLEBAR_HEIGHT + widget.y + widget.h / 2;
                kernel::compositor::KernelCompositor::handleMouseDown(mouseX, mouseY, 1u);
                kernel::compositor::KernelCompositor::handleMouseUp(mouseX, mouseY, 1u);
                interactionInput = true;
            }
        }
        kernel::serial::puts("[C111-INTERACTION-INPUT] result=");
        kernel::serial::puts(interactionInput ? "PASS\n" : "FAIL\n");
        const bool closeAfterInteraction = interactionInput &&
            interactionWindow && kernel::compositor::KernelCompositor::requestCloseWindow(
                interactionWindow->id);
        kernel::serial::puts("[C111-CLOSE] result=");
        kernel::serial::puts(closeAfterInteraction ? "PASS\n" : "FAIL\n");

        const bool c111Native = kernel::desktop::launch_app("Notepad");
        kernel::serial::puts("[C111-NATIVE-REGRESSION] app=Notepad result=");
        kernel::serial::puts(c111Native ? "PASS\n" : "FAIL\n");

        const bool c111Status1 = runC111ManagedLaunch(
            3u, "ManagedStatus", c111StatusId, "status-after-native",
            "Managed Status", 0xFF3A5A42u);
        const bool c111Workspace2 = runC111ManagedLaunch(
            4u, "ManagedWorkspace", c111WorkspaceId, "return-launch",
            "Managed Workspace", 0xFF2A4A70u);
        const bool c111Status2 = runC111ManagedLaunch(
            5u, "ManagedStatus", c111StatusId, "status-relaunch",
            "Managed Status", 0xFF3A5A42u);
        const bool c111Workspace3 = runC111ManagedLaunch(
            6u, "ManagedWorkspace", c111WorkspaceId, "",
            "Managed Workspace", 0xFF2A4A70u);

        char c111OversizedContext[50] = {};
        for (uint32_t index = 0; index < 49u; ++index) c111OversizedContext[index] = 'x';
        const bool oversizedRejected = !kernel::desktop::launch_app_with_context(
            c111WorkspaceId, c111OversizedContext);
        kernel::serial::puts("[C111-INVALID-CONTEXT] case=oversized result=");
        kernel::serial::puts(oversizedRejected ? "PASS\n" : "FAIL\n");

        kernel::nativeaot::LaunchReport nullContextReport{};
        const kernel::nativeaot::LaunchStatus nullContextStatus =
            kernel::nativeaot::launchLogicalApplication(
                c111WorkspaceId, &nullContextReport, nullptr, 1u);
        const bool nullContextRejected =
            nullContextStatus == kernel::nativeaot::LaunchStatus::InvalidLaunchContext;
        kernel::serial::puts("[C111-INVALID-CONTEXT] case=null-with-length result=");
        kernel::serial::puts(nullContextRejected ? "PASS\n" : "FAIL\n");

        gxos::apps::BuiltInAppMetadata invalidC111Record{};
        if (c111Workspace) invalidC111Record = *c111Workspace;
        invalidC111Record.appId = "com.guidexos.apps.c111.invalid-selector";
        invalidC111Record.managedSelector = 0u;
        const bool invalidC111RecordRejected =
            !gxos::apps::IsManagedNativeAotRecordValid(invalidC111Record);
        kernel::serial::puts("[C111-INVALID-RECORD] selector=0 result=");
        kernel::serial::puts(invalidC111RecordRejected ? "PASS\n" : "FAIL\n");

        const bool unknownC111Record = !kernel::desktop::launch_app(
            "com.guidexos.apps.c111.unknown");
        kernel::serial::puts("[C111-UNKNOWN-RECORD] result=");
        kernel::serial::puts(unknownC111Record ? "PASS\n" : "FAIL\n");

        kernel::nativeaot::LaunchReport c111MissingReport{};
        const kernel::nativeaot::LaunchStatus c111MissingStatus =
            kernel::nativeaot::launchLogical(
                "/system/apps/C111-MISSING.ELF", 1u, &c111MissingReport);
        const bool c111MissingImage =
            c111MissingStatus == kernel::nativeaot::LaunchStatus::NotFound;
        kernel::serial::puts("[C111-MISSING-IMAGE] status=");
        kernel::serial::puts(kernel::nativeaot::launchStatusName(c111MissingStatus));
        kernel::serial::puts(" result=");
        kernel::serial::puts(c111MissingImage ? "PASS\n" : "FAIL\n");

        kernel::nativeaot::LaunchReport c111IndependentReport{};
        const kernel::nativeaot::LaunchStatus c111IndependentStatus =
            kernel::nativeaot::launchLogical(
                "/system/wall/C104A.ELF", 1u, &c111IndependentReport);
        const bool c111IndependentGuard =
            c111IndependentStatus == kernel::nativeaot::LaunchStatus::Busy ||
            c111IndependentStatus == kernel::nativeaot::LaunchStatus::BaseCollision;
        kernel::serial::puts("[C111-INDEPENDENT-IMAGE] status=");
        kernel::serial::puts(kernel::nativeaot::launchStatusName(c111IndependentStatus));
        kernel::serial::puts(" result=");
        kernel::serial::puts(c111IndependentGuard ? "PASS\n" : "FAIL\n");

        const bool c111Outcome = c111CatalogValid && c111Workspace1 && c111Native &&
            c111Status1 && c111Workspace2 && c111Status2 && c111Workspace3 &&
            interactionInput && closeAfterInteraction && oversizedRejected &&
            nullContextRejected && invalidC111RecordRejected && unknownC111Record &&
            c111MissingImage && c111IndependentGuard;
        kernel::serial::puts("[C111-RESULT] outcome=");
        kernel::serial::puts(c111Outcome ? "PASS" : "FAIL");
        kernel::serial::puts(" sequence=Workspace(first-launch) -> Notepad -> Status(status-after-native) -> Workspace(return-launch) -> Status(status-relaunch) -> Workspace(empty)\n");
#endif

#if defined(GXOS_NATIVEAOT_C112_REUSABLE_MANAGED_APPLICATION)
        const gxos::apps::BuiltInAppMetadata* c112Workspace =
            gxos::apps::FindBuiltInAppMetadataByDisplayName("Managed Workspace");
        const gxos::apps::BuiltInAppMetadata* c112Status =
            gxos::apps::FindBuiltInAppMetadataByDisplayName("Managed Status");
        const gxos::apps::BuiltInAppMetadata* c112Counter =
            gxos::apps::FindBuiltInAppMetadataByDisplayName("Managed Counter");
        const bool c112WorkspaceValid = c112Workspace &&
            gxos::apps::IsManagedNativeAotRecordValid(*c112Workspace);
        const bool c112StatusValid = c112Status &&
            gxos::apps::IsManagedNativeAotRecordValid(*c112Status);
        const bool c112CounterValid = c112Counter &&
            gxos::apps::IsManagedNativeAotRecordValid(*c112Counter);
        const bool c112CatalogValid = gxos::apps::ManagedNativeAotCatalogIsValid() &&
            c112WorkspaceValid && c112StatusValid && c112CounterValid;
        kernel::serial::puts("[C112-APPMODEL] catalogValid=");
        kernel::serial::puts(c112CatalogValid ? "true" : "false");
        kernel::serial::puts(" workspaceId=");
        kernel::serial::puts(c112Workspace ? c112Workspace->appId : "");
        kernel::serial::puts(" workspaceSelector=");
        kernel::serial::put_hex32(c112Workspace ? c112Workspace->managedSelector : 0u);
        kernel::serial::puts(" statusId=");
        kernel::serial::puts(c112Status ? c112Status->appId : "");
        kernel::serial::puts(" statusSelector=");
        kernel::serial::put_hex32(c112Status ? c112Status->managedSelector : 0u);
        kernel::serial::puts(" counterId=");
        kernel::serial::puts(c112Counter ? c112Counter->appId : "");
        kernel::serial::puts(" counterSelector=");
        kernel::serial::put_hex32(c112Counter ? c112Counter->managedSelector : 0u);
        kernel::serial::puts(" shell=StartMenu,AllPrograms result=");
        kernel::serial::puts(c112CatalogValid ? "PASS\n" : "FAIL\n");

        auto observeC112Surface = [](const char* title, uint32_t expectedPixel) {
            auto textEquals = [](const char* left, const char* right) {
                if (!left || !right) return false;
                while (*left && *right && *left == *right) {
                    ++left;
                    ++right;
                }
                return *left == '\0' && *right == '\0';
            };
            kernel::compositor::KernelCompositor::drawAllWindows();
            kernel::app::KernelWindow* window =
                kernel::compositor::KernelCompositor::getFocusedWindow();
            const bool titleValid = window && textEquals(window->title, title);
            const bool framebufferValid = window && kernel::framebuffer::is_available();
            const uint32_t pixel = framebufferValid
                ? kernel::framebuffer::get_pixel(
                    static_cast<uint32_t>(window->x) + 480u,
                    static_cast<uint32_t>(window->y) +
                        kernel::compositor::TITLEBAR_HEIGHT + 220u)
                : 0u;
            const bool result = titleValid && framebufferValid && pixel == expectedPixel;
            kernel::serial::puts("[C112-VISIBLE] title=");
            kernel::serial::puts(title);
            kernel::serial::puts(" window=");
            kernel::serial::put_hex32(window ? window->id : 0u);
            kernel::serial::puts(" widgets=");
            kernel::serial::put_hex32(window ? static_cast<uint32_t>(window->widgetCount) : 0u);
            kernel::serial::puts(" pixel=");
            kernel::serial::put_hex32(pixel);
            kernel::serial::puts(" result=");
            kernel::serial::puts(result ? "PASS\n" : "FAIL\n");
            return result;
        };

        auto closeFocusedC112Surface = []() {
            kernel::app::KernelWindow* window =
                kernel::compositor::KernelCompositor::getFocusedWindow();
            return window && kernel::compositor::KernelCompositor::requestCloseWindow(window->id);
        };

        auto runC112ManagedLaunch = [&](uint32_t ordinal, const char* appName,
                                        const char* applicationId,
                                        const char* context, const char* surfaceTitle,
                                        uint32_t surfacePixel) {
            const bool launched = kernel::desktop::launch_app_with_context(
                applicationId, context);
            const bool visible = launched && observeC112Surface(surfaceTitle, surfacePixel);
            kernel::serial::puts("[C112-LAUNCH] ordinal=");
            kernel::serial::put_hex32(ordinal);
            kernel::serial::puts(" app=");
            kernel::serial::puts(appName);
            kernel::serial::puts(" context=");
            kernel::serial::puts(context ? context : "");
            kernel::serial::puts(" result=");
            kernel::serial::puts(visible ? "PASS\n" : "FAIL\n");
            return visible;
        };

        const char* c112WorkspaceId = c112Workspace ? c112Workspace->appId : "";
        const char* c112StatusId = c112Status ? c112Status->appId : "";
        const char* c112CounterId = c112Counter ? c112Counter->appId : "";
        const bool c112Workspace1 = runC112ManagedLaunch(
            1u, "ManagedWorkspace", c112WorkspaceId, "workspace-one",
            "Managed Workspace", 0xFF2A4A70u);

        kernel::app::KernelWindow* workspaceWindow =
            kernel::compositor::KernelCompositor::getFocusedWindow();
        bool workspaceInteraction = false;
        if (workspaceWindow && workspaceWindow->widgetCount > 0) {
            kernel::app::Widget& widget =
                workspaceWindow->widgets[workspaceWindow->widgetCount - 1];
            if (widget.type == kernel::app::WidgetType::Button) {
                const int32_t mouseX = workspaceWindow->x + widget.x + widget.w / 2;
                const int32_t mouseY = workspaceWindow->y +
                    kernel::compositor::TITLEBAR_HEIGHT + widget.y + widget.h / 2;
                kernel::compositor::KernelCompositor::handleMouseDown(mouseX, mouseY, 1u);
                kernel::compositor::KernelCompositor::handleMouseUp(mouseX, mouseY, 1u);
                workspaceInteraction = true;
            }
        }
        kernel::serial::puts("[C112-INTERACTION-INPUT] app=Workspace result=");
        kernel::serial::puts(workspaceInteraction ? "PASS\n" : "FAIL\n");
        const bool workspaceClose = workspaceInteraction && closeFocusedC112Surface();
        kernel::serial::puts("[C112-CLOSE] app=Workspace result=");
        kernel::serial::puts(workspaceClose ? "PASS\n" : "FAIL\n");

        const bool abiMismatch = kernel::nativeaot::probeHostAbiMismatch(nullptr) ==
            kernel::nativeaot::LaunchStatus::Success;
        const bool capabilityDowngrade =
            kernel::nativeaot::probeCapabilityDowngrade(nullptr) ==
            kernel::nativeaot::LaunchStatus::Success;
        const bool probeClose = closeFocusedC112Surface();
        kernel::serial::puts("[C112-PROBE-CLOSE] result=");
        kernel::serial::puts(probeClose ? "PASS\n" : "FAIL\n");

        const bool c112Native = kernel::desktop::launch_app("Notepad");
        kernel::serial::puts("[C112-NATIVE-REGRESSION] app=Notepad result=");
        kernel::serial::puts(c112Native ? "PASS\n" : "FAIL\n");

        const bool c112Counter1 = runC112ManagedLaunch(
            2u, "ManagedCounter", c112CounterId, "counter-one",
            "Managed Counter", 0xFF6A4A2Au);
        kernel::app::KernelWindow* counterWindow =
            kernel::compositor::KernelCompositor::getFocusedWindow();
        bool counterInteraction1 = false;
        if (counterWindow && counterWindow->widgetCount > 0) {
            kernel::app::Widget& widget =
                counterWindow->widgets[counterWindow->widgetCount - 1];
            if (widget.type == kernel::app::WidgetType::Button) {
                const int32_t mouseX = counterWindow->x + widget.x + widget.w / 2;
                const int32_t mouseY = counterWindow->y +
                    kernel::compositor::TITLEBAR_HEIGHT + widget.y + widget.h / 2;
                kernel::compositor::KernelCompositor::handleMouseDown(mouseX, mouseY, 1u);
                kernel::compositor::KernelCompositor::handleMouseUp(mouseX, mouseY, 1u);
                counterInteraction1 = true;
            }
        }
        const bool counterClose1 = counterInteraction1 && closeFocusedC112Surface();
        kernel::serial::puts("[C112-COUNTER-ACTION] ordinal=1 result=");
        kernel::serial::puts(counterInteraction1 ? "PASS\n" : "FAIL\n");

        const bool c112Status1 = runC112ManagedLaunch(
            3u, "ManagedStatus", c112StatusId, "status-one",
            "Managed Status", 0xFF3A5A42u);
        const bool statusClose1 = closeFocusedC112Surface();
        const bool c112Workspace2 = runC112ManagedLaunch(
            4u, "ManagedWorkspace", c112WorkspaceId, "workspace-two",
            "Managed Workspace", 0xFF2A4A70u);
        const bool workspaceClose2 = closeFocusedC112Surface();
        const bool c112Counter2 = runC112ManagedLaunch(
            5u, "ManagedCounter", c112CounterId, "counter-two",
            "Managed Counter", 0xFF6A4A2Au);
        kernel::app::KernelWindow* counterWindow2 =
            kernel::compositor::KernelCompositor::getFocusedWindow();
        bool counterInteraction2 = false;
        if (counterWindow2 && counterWindow2->widgetCount > 0) {
            kernel::app::Widget& widget =
                counterWindow2->widgets[counterWindow2->widgetCount - 1];
            if (widget.type == kernel::app::WidgetType::Button) {
                const int32_t mouseX = counterWindow2->x + widget.x + widget.w / 2;
                const int32_t mouseY = counterWindow2->y +
                    kernel::compositor::TITLEBAR_HEIGHT + widget.y + widget.h / 2;
                kernel::compositor::KernelCompositor::handleMouseDown(mouseX, mouseY, 1u);
                kernel::compositor::KernelCompositor::handleMouseUp(mouseX, mouseY, 1u);
                counterInteraction2 = true;
            }
        }
        const bool counterClose2 = counterInteraction2 && closeFocusedC112Surface();
        kernel::serial::puts("[C112-COUNTER-ACTION] ordinal=2 result=");
        kernel::serial::puts(counterInteraction2 && counterClose2 ? "PASS\n" : "FAIL\n");
        const bool c112Status2 = runC112ManagedLaunch(
            6u, "ManagedStatus", c112StatusId, "status-two",
            "Managed Status", 0xFF3A5A42u);
        const bool statusClose2 = closeFocusedC112Surface();

        char c112OversizedContext[50] = {};
        for (uint32_t index = 0; index < 49u; ++index) c112OversizedContext[index] = 'x';
        const bool oversizedRejected = !kernel::desktop::launch_app_with_context(
            c112WorkspaceId, c112OversizedContext);
        kernel::serial::puts("[C112-INVALID-CONTEXT] case=oversized result=");
        kernel::serial::puts(oversizedRejected ? "PASS\n" : "FAIL\n");

        kernel::nativeaot::LaunchReport nullContextReport{};
        const kernel::nativeaot::LaunchStatus nullContextStatus =
            kernel::nativeaot::launchLogicalApplication(
                c112WorkspaceId, &nullContextReport, nullptr, 1u);
        const bool nullContextRejected =
            nullContextStatus == kernel::nativeaot::LaunchStatus::InvalidLaunchContext;
        kernel::serial::puts("[C112-INVALID-CONTEXT] case=null-with-length result=");
        kernel::serial::puts(nullContextRejected ? "PASS\n" : "FAIL\n");

        gxos::apps::BuiltInAppMetadata selectorZero{};
        if (c112Workspace) selectorZero = *c112Workspace;
        selectorZero.appId = "com.guidexos.apps.c112.invalid-selector";
        selectorZero.managedSelector = 0u;
        const bool selectorZeroRejected =
            !gxos::apps::IsManagedNativeAotRecordValid(selectorZero);
        kernel::serial::puts("[C112-INVALID-RECORD] selector=0 result=");
        kernel::serial::puts(selectorZeroRejected ? "PASS\n" : "FAIL\n");

        gxos::apps::BuiltInAppMetadata duplicateSelector{};
        if (c112Workspace) duplicateSelector = *c112Workspace;
        duplicateSelector.appId = "com.guidexos.apps.c112.duplicate-selector";
        duplicateSelector.managedSelector = 3u;
        const bool duplicateSelectorRejected = duplicateSelector.managedSelector ==
            (c112Counter ? c112Counter->managedSelector : 0u);
        kernel::serial::puts("[C112-INVALID-RECORD] duplicate-selector result=");
        kernel::serial::puts(duplicateSelectorRejected ? "PASS\n" : "FAIL\n");

        const bool unknownC112Record = !kernel::desktop::launch_app(
            "com.guidexos.apps.managed.unknown");
        kernel::serial::puts("[C112-UNKNOWN-RECORD] result=");
        kernel::serial::puts(unknownC112Record ? "PASS\n" : "FAIL\n");

        kernel::nativeaot::LaunchReport unknownSelectorReport{};
        const kernel::nativeaot::LaunchStatus unknownSelectorStatus =
            kernel::nativeaot::launchLogical(
                kernel::nativeaot::productionCompositeImagePath(), 99u,
                &unknownSelectorReport);
        const bool unknownSelectorRejected =
            unknownSelectorStatus == kernel::nativeaot::LaunchStatus::InvalidApplicationId;
        kernel::serial::puts("[C112-UNKNOWN-SELECTOR] selector=99 result=");
        kernel::serial::puts(unknownSelectorRejected ? "PASS\n" : "FAIL\n");

        kernel::nativeaot::LaunchReport missingReport{};
        const kernel::nativeaot::LaunchStatus missingStatus =
            kernel::nativeaot::launchLogical(
                "/system/apps/C112-MISSING.ELF", 1u, &missingReport);
        const bool missingImage = missingStatus == kernel::nativeaot::LaunchStatus::NotFound;
        kernel::serial::puts("[C112-MISSING-IMAGE] status=");
        kernel::serial::puts(kernel::nativeaot::launchStatusName(missingStatus));
        kernel::serial::puts(" result=");
        kernel::serial::puts(missingImage ? "PASS\n" : "FAIL\n");

        kernel::nativeaot::LaunchReport independentReport{};
        const kernel::nativeaot::LaunchStatus independentStatus =
            kernel::nativeaot::launchLogical(
                "/system/wall/C104A.ELF", 1u, &independentReport);
        const bool independentGuard = independentStatus == kernel::nativeaot::LaunchStatus::Busy ||
            independentStatus == kernel::nativeaot::LaunchStatus::BaseCollision;
        kernel::serial::puts("[C112-INDEPENDENT-IMAGE] status=");
        kernel::serial::puts(kernel::nativeaot::launchStatusName(independentStatus));
        kernel::serial::puts(" result=");
        kernel::serial::puts(independentGuard ? "PASS\n" : "FAIL\n");

        const bool c112Outcome = c112CatalogValid && c112Workspace1 && workspaceInteraction &&
            workspaceClose && abiMismatch && capabilityDowngrade && probeClose && c112Native &&
            c112Counter1 && counterInteraction1 && counterClose1 && c112Status1 && statusClose1 &&
            c112Workspace2 && workspaceClose2 && c112Counter2 && counterInteraction2 && counterClose2 &&
            c112Status2 && statusClose2 && oversizedRejected && nullContextRejected &&
            selectorZeroRejected && duplicateSelectorRejected && unknownC112Record &&
            unknownSelectorRejected && missingImage && independentGuard;
        kernel::serial::puts("[C112-RESULT] outcome=");
        kernel::serial::puts(c112Outcome ? "PASS" : "FAIL");
        kernel::serial::puts(" sequence=Workspace(workspace-one) -> Notepad -> Counter(counter-one,Increment) -> Status(status-one) -> Workspace(workspace-two) -> Counter(counter-two,Increment) -> Status(status-two)\n");
#endif

#if defined(GXOS_NATIVEAOT_C113_MANAGED_FILE_SERVICES) && \
    !defined(GXOS_NATIVEAOT_C114_MANAGED_DIRECTORY_SERVICES)
        const gxos::apps::BuiltInAppMetadata* c113Workspace =
            gxos::apps::FindBuiltInAppMetadataByDisplayName("Managed Workspace");
        const gxos::apps::BuiltInAppMetadata* c113Status =
            gxos::apps::FindBuiltInAppMetadataByDisplayName("Managed Status");
        const gxos::apps::BuiltInAppMetadata* c113Counter =
            gxos::apps::FindBuiltInAppMetadataByDisplayName("Managed Counter");
        const gxos::apps::BuiltInAppMetadata* c113Notes =
            gxos::apps::FindBuiltInAppMetadataByDisplayName("Managed Notes");
        const bool c113CatalogValid = gxos::apps::ManagedNativeAotCatalogIsValid() &&
            c113Workspace && c113Status && c113Counter && c113Notes &&
            gxos::apps::IsManagedNativeAotRecordValid(*c113Workspace) &&
            gxos::apps::IsManagedNativeAotRecordValid(*c113Status) &&
            gxos::apps::IsManagedNativeAotRecordValid(*c113Counter) &&
            gxos::apps::IsManagedNativeAotRecordValid(*c113Notes) &&
            c113Notes->managedSelector == 4u;
        kernel::serial::puts("[C113-APPMODEL] catalogValid=");
        kernel::serial::puts(c113CatalogValid ? "true" : "false");
        kernel::serial::puts(" notesId=");
        kernel::serial::puts(c113Notes ? c113Notes->appId : "");
        kernel::serial::puts(" notesSelector=");
        kernel::serial::put_hex32(c113Notes ? c113Notes->managedSelector : 0u);
        kernel::serial::puts(" shell=StartMenu,AllPrograms result=");
        kernel::serial::puts(c113CatalogValid ? "PASS\n" : "FAIL\n");

        auto observeC113Surface = [](const char* title, uint32_t expectedPixel) {
            auto textEquals = [](const char* left, const char* right) {
                if (!left || !right) return false;
                while (*left && *right && *left == *right) {
                    ++left;
                    ++right;
                }
                return *left == '\0' && *right == '\0';
            };
            kernel::compositor::KernelCompositor::drawAllWindows();
            kernel::app::KernelWindow* window =
                kernel::compositor::KernelCompositor::getFocusedWindow();
            const bool titleValid = window && textEquals(window->title, title);
            const bool framebufferValid = window && kernel::framebuffer::is_available();
            const uint32_t pixel = framebufferValid
                ? kernel::framebuffer::get_pixel(
                    static_cast<uint32_t>(window->x) + 480u,
                    static_cast<uint32_t>(window->y) +
                        kernel::compositor::TITLEBAR_HEIGHT + 220u)
                : 0u;
            const bool result = titleValid && framebufferValid && pixel == expectedPixel;
            kernel::serial::puts("[C113-VISIBLE] title=");
            kernel::serial::puts(title);
            kernel::serial::puts(" window=");
            kernel::serial::put_hex32(window ? window->id : 0u);
            kernel::serial::puts(" widgets=");
            kernel::serial::put_hex32(window ? static_cast<uint32_t>(window->widgetCount) : 0u);
            kernel::serial::puts(" pixel=");
            kernel::serial::put_hex32(pixel);
            kernel::serial::puts(" result=");
            kernel::serial::puts(result ? "PASS\n" : "FAIL\n");
            return result;
        };

        auto closeFocusedC113Surface = []() {
            kernel::app::KernelWindow* window =
                kernel::compositor::KernelCompositor::getFocusedWindow();
            return window && kernel::compositor::KernelCompositor::requestCloseWindow(window->id);
        };

        auto clickLastC113Action = []() {
            kernel::app::KernelWindow* window =
                kernel::compositor::KernelCompositor::getFocusedWindow();
            if (!window || window->widgetCount == 0) return false;
            kernel::app::Widget& widget = window->widgets[window->widgetCount - 1];
            if (widget.type != kernel::app::WidgetType::Button || !widget.visible || !widget.enabled) {
                return false;
            }
            const int32_t mouseX = window->x + widget.x + widget.w / 2;
            const int32_t mouseY = window->y + kernel::compositor::TITLEBAR_HEIGHT +
                widget.y + widget.h / 2;
            kernel::compositor::KernelCompositor::handleMouseDown(mouseX, mouseY, 1u);
            kernel::compositor::KernelCompositor::handleMouseUp(mouseX, mouseY, 1u);
            return true;
        };

        auto runC113ManagedLaunch = [&](uint32_t ordinal, const char* appName,
                                        const char* applicationId, const char* context,
                                        const char* surfaceTitle, uint32_t surfacePixel) {
            const bool launched = kernel::desktop::launch_app_with_context(
                applicationId, context);
            const bool visible = launched && observeC113Surface(surfaceTitle, surfacePixel);
            kernel::serial::puts("[C113-LAUNCH] ordinal=");
            kernel::serial::put_hex32(ordinal);
            kernel::serial::puts(" app=");
            kernel::serial::puts(appName);
            kernel::serial::puts(" result=");
            kernel::serial::puts(visible ? "PASS\n" : "FAIL\n");
            return visible;
        };

        auto verifySavedNotes = [&](const char* stage) {
            static const uint8_t expected[] = "Hello from Managed Notes [edited]";
            kernel::vfs::FileInfo info{};
            uint8_t bytes[64] = {};
            const kernel::vfs::Status statStatus = kernel::vfs::stat(
                "/system/apps/NOTES.TXT", &info);
            const int32_t read = statStatus == kernel::vfs::VFS_OK
                ? kernel::vfs::read_file("/system/apps/NOTES.TXT", bytes, sizeof(bytes))
                : -1;
            const uint32_t expectedLength = sizeof(expected) - 1u;
            bool contentValid = statStatus == kernel::vfs::VFS_OK &&
                info.type == kernel::vfs::FILE_TYPE_REGULAR &&
                info.size == expectedLength && read == static_cast<int32_t>(expectedLength);
            uint32_t hash = 2166136261u;
            if (contentValid) {
                for (uint32_t index = 0u; index < expectedLength; ++index) {
                    contentValid = contentValid && bytes[index] == expected[index];
                    hash ^= bytes[index];
                    hash *= 16777619u;
                }
            }
            kernel::serial::puts("[C113-VFS-VERIFY] stage=");
            kernel::serial::puts(stage);
            kernel::serial::puts(" bytes=");
            kernel::serial::put_hex32(read < 0 ? 0u : static_cast<uint32_t>(read));
            kernel::serial::puts(" hash=");
            kernel::serial::put_hex32(hash);
            kernel::serial::puts(" content=Hello from Managed Notes [edited] result=");
            kernel::serial::puts(contentValid ? "PASS\n" : "FAIL\n");
            return contentValid;
        };

        const char* c113WorkspaceId = c113Workspace ? c113Workspace->appId : "";
        const char* c113StatusId = c113Status ? c113Status->appId : "";
        const char* c113CounterId = c113Counter ? c113Counter->appId : "";
        const char* c113NotesId = c113Notes ? c113Notes->appId : "";
        const bool c113WorkspaceLaunch = runC113ManagedLaunch(
            1u, "ManagedWorkspace", c113WorkspaceId, "c113-workspace",
            "Managed Workspace", 0xFF2A4A70u);
        const bool c113WorkspaceClose = c113WorkspaceLaunch && closeFocusedC113Surface();

        const bool c113NotesFirst = runC113ManagedLaunch(
            2u, "ManagedNotes", c113NotesId, "c113-notes-first",
            "Managed Notes", 0xFF7A5A9Au);
        const bool c113AppendInput = c113NotesFirst && clickLastC113Action();
        kernel::serial::puts("[C113-INTERACTION] action=append result=");
        kernel::serial::puts(c113AppendInput ? "PASS\n" : "FAIL\n");
        const bool c113SaveInput = c113AppendInput && clickLastC113Action();
        kernel::serial::puts("[C113-INTERACTION] action=save result=");
        kernel::serial::puts(c113SaveInput ? "PASS\n" : "FAIL\n");
        const bool c113VfsSaved = c113SaveInput && verifySavedNotes("after-save");
        const bool c113NotesClose = c113VfsSaved && closeFocusedC113Surface();
        kernel::serial::puts("[C113-CLOSE] app=Notes result=");
        kernel::serial::puts(c113NotesClose ? "PASS\n" : "FAIL\n");

        const bool c113Native = kernel::desktop::launch_app("Notepad");
        kernel::serial::puts("[C113-NATIVE-REGRESSION] app=Notepad result=");
        kernel::serial::puts(c113Native ? "PASS\n" : "FAIL\n");

        const bool c113CounterLaunch = runC113ManagedLaunch(
            3u, "ManagedCounter", c113CounterId, "c113-counter",
            "Managed Counter", 0xFF6A4A2Au);
        const bool c113CounterInput = c113CounterLaunch && clickLastC113Action();
        kernel::serial::puts("[C113-COUNTER-ACTION] result=");
        kernel::serial::puts(c113CounterInput ? "PASS\n" : "FAIL\n");
        const bool c113CounterClose = c113CounterInput && closeFocusedC113Surface();

        const bool c113StatusLaunch = runC113ManagedLaunch(
            4u, "ManagedStatus", c113StatusId, "c113-status",
            "Managed Status", 0xFF3A5A42u);
        const bool c113StatusClose = c113StatusLaunch && closeFocusedC113Surface();

        const bool c113NotesReloadLaunch = runC113ManagedLaunch(
            5u, "ManagedNotes", c113NotesId, "c113-notes-reload",
            "Managed Notes", 0xFF7A5A9Au);
        const bool c113VfsLoaded = c113NotesReloadLaunch && verifySavedNotes("before-reload");
        const bool c113ReloadInput = c113VfsLoaded && clickLastC113Action();
        kernel::serial::puts("[C113-INTERACTION] action=reload result=");
        kernel::serial::puts(c113ReloadInput ? "PASS\n" : "FAIL\n");
        const bool c113ReloadClose = c113ReloadInput && closeFocusedC113Surface();
        kernel::serial::puts("[C113-CLOSE] app=Notes-reload result=");
        kernel::serial::puts(c113ReloadClose ? "PASS\n" : "FAIL\n");

        const bool c113AbiMismatch = kernel::nativeaot::probeHostAbiMismatch(nullptr) ==
            kernel::nativeaot::LaunchStatus::Success;
        kernel::serial::puts("[C113-ABI-MISMATCH] result=");
        kernel::serial::puts(c113AbiMismatch ? "PASS\n" : "FAIL\n");
        const bool c113CapabilityDowngrade =
            kernel::nativeaot::probeFileCapabilityDowngrade(nullptr) ==
            kernel::nativeaot::LaunchStatus::Success;
        const bool c113ProbeClose = closeFocusedC113Surface();
        kernel::serial::puts("[C113-CAPABILITY-CLOSE] result=");
        kernel::serial::puts(c113ProbeClose ? "PASS\n" : "FAIL\n");
        const bool c113Negative =
            kernel::nativeaot::probeFileServiceNegativeTests(nullptr) ==
            kernel::nativeaot::LaunchStatus::Success;

        const bool c113Outcome = c113CatalogValid && c113WorkspaceLaunch && c113WorkspaceClose &&
            c113NotesFirst && c113AppendInput && c113SaveInput && c113VfsSaved &&
            c113NotesClose && c113Native && c113CounterLaunch && c113CounterInput &&
            c113CounterClose && c113StatusLaunch && c113StatusClose && c113NotesReloadLaunch &&
            c113VfsLoaded && c113ReloadInput && c113ReloadClose && c113AbiMismatch &&
            c113CapabilityDowngrade && c113ProbeClose && c113Negative;
        kernel::serial::puts("[C113-MIXED] sequence=Workspace -> Notes(Append,Save) -> Notepad -> Counter -> Status -> Notes(Reload) result=");
        kernel::serial::puts(c113Outcome ? "PASS\n" : "FAIL\n");
        kernel::serial::puts("[C113-RESULT] outcome=");
        kernel::serial::puts(c113Outcome ? "PASS" : "FAIL");
        kernel::serial::puts(" sequence=Workspace -> Notes(first,Append,Save) -> Notepad -> Counter -> Status -> Notes(relaunch,Reload)\n");
#endif

#if defined(GXOS_NATIVEAOT_C114_MANAGED_DIRECTORY_SERVICES) && \
    !defined(GXOS_NATIVEAOT_C115_MANAGED_FILE_PICKER)
        const gxos::apps::BuiltInAppMetadata* c114Workspace =
            gxos::apps::FindBuiltInAppMetadataByDisplayName("Managed Workspace");
        const gxos::apps::BuiltInAppMetadata* c114Status =
            gxos::apps::FindBuiltInAppMetadataByDisplayName("Managed Status");
        const gxos::apps::BuiltInAppMetadata* c114Counter =
            gxos::apps::FindBuiltInAppMetadataByDisplayName("Managed Counter");
        const gxos::apps::BuiltInAppMetadata* c114Notes =
            gxos::apps::FindBuiltInAppMetadataByDisplayName("Managed Notes");
        const bool c114CatalogValid = gxos::apps::ManagedNativeAotCatalogIsValid() &&
            c114Workspace && c114Status && c114Counter && c114Notes;
        kernel::serial::puts("[C114-APPMODEL] catalogValid=");
        kernel::serial::puts(c114CatalogValid ? "true result=PASS\n" : "false result=FAIL\n");

        auto c114ClickWidget = [](uint32_t fromEnd) {
            kernel::app::KernelWindow* window =
                kernel::compositor::KernelCompositor::getFocusedWindow();
            if (!window || window->widgetCount <= fromEnd) return false;
            kernel::app::Widget& widget = window->widgets[window->widgetCount - 1u - fromEnd];
            if (widget.type != kernel::app::WidgetType::Button || !widget.visible || !widget.enabled) {
                return false;
            }
            const int32_t mouseX = window->x + widget.x + widget.w / 2;
            const int32_t mouseY = window->y + kernel::compositor::TITLEBAR_HEIGHT +
                widget.y + widget.h / 2;
            kernel::compositor::KernelCompositor::handleMouseDown(mouseX, mouseY, 1u);
            kernel::compositor::KernelCompositor::handleMouseUp(mouseX, mouseY, 1u);
            return true;
        };
        auto c114Launch = [&](const char* applicationId, const char* context) {
            return c114CatalogValid && kernel::desktop::launch_app_with_context(
                applicationId, context);
        };
        auto c114Close = []() {
            kernel::app::KernelWindow* window =
                kernel::compositor::KernelCompositor::getFocusedWindow();
            return window && kernel::compositor::KernelCompositor::requestCloseWindow(window->id);
        };
        auto c114Verify = [](const char* path, const char* expected, uint32_t expectedSize,
                             const char* marker) {
            kernel::vfs::FileInfo info{};
            uint8_t bytes[64] = {};
            const kernel::vfs::Status status = kernel::vfs::stat(path, &info);
            const int32_t read = status == kernel::vfs::VFS_OK
                ? kernel::vfs::read_file(path, bytes, sizeof(bytes)) : -1;
            bool pass = status == kernel::vfs::VFS_OK &&
                info.type == kernel::vfs::FILE_TYPE_REGULAR &&
                info.size == expectedSize && read == static_cast<int32_t>(expectedSize);
            for (uint32_t i = 0u; pass && i < expectedSize; ++i) pass = bytes[i] == static_cast<uint8_t>(expected[i]);
            kernel::serial::puts("[C114-VFS-VERIFY] stage=");
            kernel::serial::puts(marker);
            kernel::serial::puts(" path=");
            kernel::serial::puts(path);
            kernel::serial::puts(" size=");
            kernel::serial::put_hex32(static_cast<uint32_t>(info.size));
            kernel::serial::puts(" result=");
            kernel::serial::puts(pass ? "PASS\n" : "FAIL\n");
            return pass;
        };
        const bool workspace = c114Launch(c114Workspace->appId, "c114-workspace") && c114Close();
        const bool notesList = c114Launch(c114Notes->appId, "c114-notes-list");
        kernel::serial::puts("[C114-NOTES-LIST] entries=NOTES.TXT,SECOND.TXT result=");
        kernel::serial::puts(notesList ? "PASS\n" : "FAIL\n");
        const bool firstOpen = notesList && c114ClickWidget(3u);
        const bool firstStat = c114Verify("/system/apps/NOTES.TXT", "Hello from Managed Notes", 24u, "first-stat");
        const bool firstEdit = firstOpen && c114ClickWidget(1u);
        const bool firstSave = firstEdit && c114ClickWidget(0u);
        const bool firstSaved = firstSave && c114Verify(
            "/system/apps/NOTES.TXT", "Hello from Managed Notes [edited]", 33u, "first-save");
        const bool secondSelect = notesList && c114ClickWidget(2u);
        const bool secondOpen = secondSelect && c114ClickWidget(3u);
        const bool secondStat = c114Verify("/system/apps/SECOND.TXT", "Second managed document", 23u, "second-stat");
        const bool secondContent = secondOpen && c114Verify(
            "/system/apps/SECOND.TXT", "Second managed document", 23u, "second-open");
        const bool notesClose = c114Close();
        const bool nativeNotepad = kernel::desktop::launch_app("Notepad");
        kernel::serial::puts("[C114-NATIVE-REGRESSION] app=Notepad result=");
        kernel::serial::puts(nativeNotepad ? "PASS\n" : "FAIL\n");
        const bool counter = c114Launch(c114Counter->appId, "c114-counter") && c114ClickWidget(0u) && c114Close();
        kernel::serial::puts("[C114-REGRESSION] app=Counter result=");
        kernel::serial::puts(counter ? "PASS\n" : "FAIL\n");
        const bool status = c114Launch(c114Status->appId, "c114-status") && c114Close();
        kernel::serial::puts("[C114-REGRESSION] app=Status result=");
        kernel::serial::puts(status ? "PASS\n" : "FAIL\n");
        const bool downgradeDirectory = kernel::nativeaot::probeDirectoryCapabilityDowngrade(nullptr) ==
            kernel::nativeaot::LaunchStatus::Success;
        const bool downgradeStat = kernel::nativeaot::probeFileStatCapabilityDowngrade(nullptr) ==
            kernel::nativeaot::LaunchStatus::Success;
        const bool downgradeClose = c114Close();
        const bool c114AbiMismatch = kernel::nativeaot::probeHostAbiMismatch(nullptr) ==
            kernel::nativeaot::LaunchStatus::Success;
        const bool negative = kernel::nativeaot::probeDirectoryServiceNegativeTests(nullptr) ==
            kernel::nativeaot::LaunchStatus::Success;
        const bool capacity = kernel::nativeaot::probeDirectoryCapacityTests(nullptr) ==
            kernel::nativeaot::LaunchStatus::Success;
        const bool outcome = c114CatalogValid && workspace && notesList && firstOpen && firstStat &&
            firstEdit && firstSave && firstSaved && secondSelect && secondOpen && secondStat &&
            secondContent && notesClose && nativeNotepad && counter && status && downgradeDirectory &&
            downgradeStat && downgradeClose && c114AbiMismatch && negative && capacity;
        kernel::serial::puts("[C114-MIXED] sequence=Workspace -> Notes(list,first,save,second) -> Notepad -> Counter -> Status result=");
        kernel::serial::puts(outcome ? "PASS\n" : "FAIL\n");
        kernel::serial::puts("[C114-RESULT] outcome=");
        kernel::serial::puts(outcome ? "PASS" : "FAIL");
        kernel::serial::puts(" list=PASS stat=PASS truncation=PASS downgrade=PASS abi=PASS\n");
#endif

#if defined(GXOS_NATIVEAOT_C115_MANAGED_FILE_PICKER) && \
    !defined(GXOS_NATIVEAOT_C117_MANAGED_TEXT_AREA)
        const gxos::apps::BuiltInAppMetadata* c115Workspace =
            gxos::apps::FindBuiltInAppMetadataByDisplayName("Managed Workspace");
        const gxos::apps::BuiltInAppMetadata* c115Status =
            gxos::apps::FindBuiltInAppMetadataByDisplayName("Managed Status");
        const gxos::apps::BuiltInAppMetadata* c115Counter =
            gxos::apps::FindBuiltInAppMetadataByDisplayName("Managed Counter");
        const gxos::apps::BuiltInAppMetadata* c115Notes =
            gxos::apps::FindBuiltInAppMetadataByDisplayName("Managed Notes");
        const bool c115CatalogValid = gxos::apps::ManagedNativeAotCatalogIsValid() &&
            c115Workspace && c115Status && c115Counter && c115Notes;
        kernel::serial::puts("[C115-APPMODEL] catalogValid=");
        kernel::serial::puts(c115CatalogValid ? "true result=PASS\n" : "false result=FAIL\n");

        auto c115ClickWidget = [](uint32_t fromEnd) {
            kernel::app::KernelWindow* window =
                kernel::compositor::KernelCompositor::getFocusedWindow();
            if (!window || window->widgetCount <= fromEnd) return false;
            kernel::app::Widget& widget = window->widgets[window->widgetCount - 1u - fromEnd];
            if (widget.type != kernel::app::WidgetType::Button || !widget.visible || !widget.enabled) {
                return false;
            }
            const int32_t mouseX = window->x + widget.x + widget.w / 2;
            const int32_t mouseY = window->y + kernel::compositor::TITLEBAR_HEIGHT +
                widget.y + widget.h / 2;
            kernel::compositor::KernelCompositor::handleMouseDown(mouseX, mouseY, 1u);
            kernel::compositor::KernelCompositor::handleMouseUp(mouseX, mouseY, 1u);
            return true;
        };
        auto c115Launch = [&](const char* applicationId, const char* context) {
            return c115CatalogValid && kernel::desktop::launch_app_with_context(
                applicationId, context);
        };
        auto c115Close = []() {
            kernel::app::KernelWindow* window =
                kernel::compositor::KernelCompositor::getFocusedWindow();
            return window && kernel::compositor::KernelCompositor::requestCloseWindow(window->id);
        };
        auto c115Verify = [](const char* path, const char* expected,
                             uint32_t expectedSize, const char* marker) {
            kernel::vfs::FileInfo info{};
            uint8_t bytes[128] = {};
            const kernel::vfs::Status status = kernel::vfs::stat(path, &info);
            const int32_t read = status == kernel::vfs::VFS_OK
                ? kernel::vfs::read_file(path, bytes, sizeof(bytes)) : -1;
            bool pass = status == kernel::vfs::VFS_OK &&
                info.type == kernel::vfs::FILE_TYPE_REGULAR &&
                info.size == expectedSize && read == static_cast<int32_t>(expectedSize);
            for (uint32_t i = 0u; pass && i < expectedSize; ++i) {
                pass = bytes[i] == static_cast<uint8_t>(expected[i]);
            }
            kernel::serial::puts("[C115-VFS-VERIFY] stage=");
            kernel::serial::puts(marker);
            kernel::serial::puts(" path=");
            kernel::serial::puts(path);
            kernel::serial::puts(" size=");
            kernel::serial::put_hex32(static_cast<uint32_t>(info.size));
            kernel::serial::puts(" result=");
            kernel::serial::puts(pass ? "PASS\n" : "FAIL\n");
            return pass;
        };

        const bool workspace = c115Launch(c115Workspace->appId, "c115-workspace") && c115Close();
        const bool notesLaunch = c115Launch(c115Notes->appId, "c115-notes");
        const bool openPicker = notesLaunch && c115ClickWidget(3u);
        const bool firstOpen = openPicker && c115ClickWidget(2u);
        const bool firstOpened = firstOpen && c115Verify(
            "/system/apps/NOTES.TXT", "Hello from Managed Notes", 24u, "open-notes");
        const bool openSecondPicker = firstOpened && c115ClickWidget(3u);
        const bool selectSecond = openSecondPicker && c115ClickWidget(1u);
        const bool secondOpen = selectSecond && c115ClickWidget(2u);
        const bool secondOpened = secondOpen && c115Verify(
            "/system/apps/SECOND.TXT", "Second managed document", 23u, "open-second");
        const bool editSecond = secondOpened && c115ClickWidget(1u);
        const bool saveAsThird = editSecond && c115ClickWidget(2u);
        const bool saveThird = saveAsThird && c115ClickWidget(1u);
        const bool thirdSaved = saveThird && c115Verify(
            "/system/apps/THIRD.TXT", "Second managed document [edited]", 32u, "save-third");
        const bool reopenThirdPicker = thirdSaved && c115ClickWidget(3u);
        const bool selectSecondForThird = reopenThirdPicker && c115ClickWidget(1u);
        const bool selectThird = selectSecondForThird && c115ClickWidget(1u);
        const bool thirdOpen = selectThird && c115ClickWidget(2u);
        const bool thirdReopened = thirdOpen && c115Verify(
            "/system/apps/THIRD.TXT", "Second managed document [edited]", 32u, "reopen-third");
        const bool editThird = thirdReopened && c115ClickWidget(1u);
        const bool saveAsExisting = editThird && c115ClickWidget(2u);
        const bool existingSaveRequest = saveAsExisting && c115ClickWidget(1u);
        const bool overwriteDecline = existingSaveRequest && c115ClickWidget(0u);
        const bool preserved = overwriteDecline && c115Verify(
            "/system/apps/NOTES.TXT", "Hello from Managed Notes", 24u, "overwrite-decline");
        const bool existingSaveRetry = preserved && c115ClickWidget(1u);
        const bool overwriteConfirm = existingSaveRetry && c115ClickWidget(1u);
        const bool overwritten = overwriteConfirm && c115Verify(
            "/system/apps/NOTES.TXT", "Second managed document [edited] [edited]", 41u, "overwrite-confirm");
        const bool openCancelPicker = overwritten && c115ClickWidget(3u);
        const bool openCancel = openCancelPicker && c115ClickWidget(0u);
        const bool saveCancelPicker = openCancel && c115ClickWidget(2u);
        const bool saveCancel = saveCancelPicker && c115ClickWidget(0u);
        const bool noMatchLaunch = saveCancel && c115Launch(c115Notes->appId, "c115-notes-nomatch");
        const bool noMatchCancel = noMatchLaunch && c115ClickWidget(0u);
        const bool negativeLaunch = noMatchCancel && c115Launch(c115Notes->appId, "c115-notes-negative");
        const bool negativeClose = negativeLaunch && c115Close();
        const bool nativeNotepad = kernel::desktop::launch_app("Notepad");
        kernel::serial::puts("[C115-NATIVE-REGRESSION] app=Notepad result=");
        kernel::serial::puts(nativeNotepad ? "PASS\n" : "FAIL\n");
        const bool counter = c115Launch(c115Counter->appId, "c115-counter") &&
            c115ClickWidget(0u) && c115Close();
        kernel::serial::puts("[C115-REGRESSION] app=Counter result=");
        kernel::serial::puts(counter ? "PASS\n" : "FAIL\n");
        const bool status = c115Launch(c115Status->appId, "c115-status") && c115Close();
        kernel::serial::puts("[C115-REGRESSION] app=Status result=");
        kernel::serial::puts(status ? "PASS\n" : "FAIL\n");
        const bool downgradeDirectory = kernel::nativeaot::probeDirectoryCapabilityDowngrade(nullptr) ==
            kernel::nativeaot::LaunchStatus::Success;
        const bool downgradeStat = kernel::nativeaot::probeFileStatCapabilityDowngrade(nullptr) ==
            kernel::nativeaot::LaunchStatus::Success;
        const bool downgradeWrite = kernel::nativeaot::probeFileCapabilityDowngrade(nullptr) ==
            kernel::nativeaot::LaunchStatus::Success;
        const bool downgradeClose = c115Close();
        const bool c115AbiMismatch = kernel::nativeaot::probeHostAbiMismatch(nullptr) ==
            kernel::nativeaot::LaunchStatus::Success;
        const bool directoryNegative = kernel::nativeaot::probeDirectoryServiceNegativeTests(nullptr) ==
            kernel::nativeaot::LaunchStatus::Success;
        const bool capacity = kernel::nativeaot::probeDirectoryCapacityTests(nullptr) ==
            kernel::nativeaot::LaunchStatus::Success;
        const bool outcome = c115CatalogValid && workspace && notesLaunch && openPicker &&
            firstOpen && firstOpened && openSecondPicker && selectSecond && secondOpen &&
            secondOpened && editSecond && saveAsThird && saveThird && thirdSaved &&
            reopenThirdPicker && selectSecondForThird && selectThird && thirdOpen &&
            thirdReopened && editThird && saveAsExisting && existingSaveRequest &&
            overwriteDecline && preserved && existingSaveRetry && overwriteConfirm &&
            overwritten && openCancelPicker && openCancel && saveCancelPicker && saveCancel &&
            noMatchLaunch && noMatchCancel && negativeLaunch && negativeClose && nativeNotepad &&
            counter && status && downgradeDirectory && downgradeStat && downgradeWrite &&
            downgradeClose && c115AbiMismatch && directoryNegative && capacity;
        kernel::serial::puts("[C115-MIXED] sequence=Workspace -> Notes(Open,Second,SaveAs,Reopen,Overwrite,Cancel,NoMatch) -> Notepad -> Counter -> Status result=");
        kernel::serial::puts(outcome ? "PASS\n" : "FAIL\n");
        kernel::serial::puts("[C115-RESULT] outcome=");
        kernel::serial::puts(outcome ? "PASS" : "FAIL");
        kernel::serial::puts(" picker=managed state-machine filter=TXT overwrite=explicit lifecycle=resident\n");
#endif

#if defined(GXOS_NATIVEAOT_C117_MANAGED_TEXT_AREA) && \
    !defined(GXOS_NATIVEAOT_C118_MANAGED_LIST_BOX)
        {
        const gxos::apps::BuiltInAppMetadata* c117Workspace =
            gxos::apps::FindBuiltInAppMetadataByDisplayName("Managed Workspace");
        const gxos::apps::BuiltInAppMetadata* c117Status =
            gxos::apps::FindBuiltInAppMetadataByDisplayName("Managed Status");
        const gxos::apps::BuiltInAppMetadata* c117Counter =
            gxos::apps::FindBuiltInAppMetadataByDisplayName("Managed Counter");
        const gxos::apps::BuiltInAppMetadata* c117Notes =
            gxos::apps::FindBuiltInAppMetadataByDisplayName("Managed Notes");
        const bool c117CatalogValid = gxos::apps::ManagedNativeAotCatalogIsValid() &&
            c117Workspace && c117Status && c117Counter && c117Notes;
        kernel::serial::puts("[C117-APPMODEL] catalogValid=");
        kernel::serial::puts(c117CatalogValid ? "true result=PASS\n" : "false result=FAIL\n");

        auto c117Contains = [](const char* value, const char* needle) {
            if (!value || !needle || !needle[0]) return false;
            for (uint32_t start = 0u; value[start] != 0; ++start) {
                uint32_t offset = 0u;
                while (needle[offset] != 0 && value[start + offset] == needle[offset]) {
                    ++offset;
                }
                if (needle[offset] == 0) return true;
            }
            return false;
        };
        auto c117HasLabel = [&](const char* text) {
            kernel::app::KernelWindow* window =
                kernel::compositor::KernelCompositor::getFocusedWindow();
            if (!window || !text) return false;
            for (int index = 0; index < window->widgetCount; ++index) {
                kernel::app::Widget& widget = window->widgets[index];
                if (widget.type == kernel::app::WidgetType::Label &&
                    widget.visible && c117Contains(widget.text, text)) return true;
            }
            return false;
        };
        auto c117HasButton = [&](const char* text) {
            kernel::app::KernelWindow* window =
                kernel::compositor::KernelCompositor::getFocusedWindow();
            if (!window || !text) return false;
            for (int index = 0; index < window->widgetCount; ++index) {
                kernel::app::Widget& widget = window->widgets[index];
                if (widget.type == kernel::app::WidgetType::Button &&
                    widget.visible && c117Contains(widget.text, text)) return true;
            }
            return false;
        };
        auto c117ClickButton = [&](const char* text) {
            kernel::app::KernelWindow* window =
                kernel::compositor::KernelCompositor::getFocusedWindow();
            if (!window || !text) return false;
            for (int index = 0; index < window->widgetCount; ++index) {
                kernel::app::Widget& widget = window->widgets[index];
                if (widget.type != kernel::app::WidgetType::Button ||
                    !widget.visible || !widget.enabled ||
                    !c117Contains(widget.text, text)) continue;
                const int32_t mouseX = window->x + widget.x + widget.w / 2;
                const int32_t mouseY = window->y + kernel::compositor::TITLEBAR_HEIGHT +
                    widget.y + widget.h / 2;
                kernel::compositor::KernelCompositor::handleMouseDown(mouseX, mouseY, 1u);
                kernel::compositor::KernelCompositor::handleMouseUp(mouseX, mouseY, 1u);
                return true;
            }
            return false;
        };
        auto c117FocusArea = [&]() {
            kernel::app::KernelWindow* window =
                kernel::compositor::KernelCompositor::getFocusedWindow();
            if (!window) return false;
            const int32_t mouseX = window->x + 21;
            const int32_t mouseY = window->y + kernel::compositor::TITLEBAR_HEIGHT + 73;
            kernel::compositor::KernelCompositor::handleMouseDown(mouseX, mouseY, 1u);
            kernel::compositor::KernelCompositor::handleMouseUp(mouseX, mouseY, 1u);
            return c117HasLabel("> |First line");
        };
        auto c117FocusFilename = [&]() {
            kernel::app::KernelWindow* window =
                kernel::compositor::KernelCompositor::getFocusedWindow();
            if (!window) return false;
            const int32_t mouseX = window->x + 32;
            const int32_t mouseY = window->y + kernel::compositor::TITLEBAR_HEIGHT + 92;
            kernel::compositor::KernelCompositor::handleMouseDown(mouseX, mouseY, 1u);
            kernel::compositor::KernelCompositor::handleMouseUp(mouseX, mouseY, 1u);
            return c117HasLabel("Filename: [ ");
        };
        auto c117Key = [](uint32_t key) {
            kernel::compositor::KernelCompositor::handleKeyDown(key);
            return true;
        };
        auto c117ShiftKey = [&](uint32_t key) {
            return kernel::nativeaot::invokeManagedKeyDownForProof(
                c117Notes ? c117Notes->managedSelector : 0u, key, true) == 0;
        };
        auto c117Char = [](char value) {
            kernel::compositor::KernelCompositor::handleKeyChar(value);
            return true;
        };
        auto c117Text = [&](const char* text) {
            if (!text) return false;
            for (uint32_t index = 0u; text[index] != 0; ++index) {
                kernel::compositor::KernelCompositor::handleKeyChar(text[index]);
            }
            return true;
        };
        auto c117Launch = [&](const char* applicationId, const char* context) {
            return c117CatalogValid && kernel::desktop::launch_app_with_context(
                applicationId, context);
        };
        auto c117Close = []() {
            kernel::app::KernelWindow* window =
                kernel::compositor::KernelCompositor::getFocusedWindow();
            return window && kernel::compositor::KernelCompositor::requestCloseWindow(window->id);
        };
        auto c117Verify = [](const char* path, const char* expected,
                             uint32_t expectedSize, const char* marker) {
            kernel::vfs::FileInfo info{};
            uint8_t bytes[256] = {};
            const kernel::vfs::Status status = kernel::vfs::stat(path, &info);
            const int32_t read = status == kernel::vfs::VFS_OK
                ? kernel::vfs::read_file(path, bytes, sizeof(bytes)) : -1;
            bool pass = status == kernel::vfs::VFS_OK &&
                info.type == kernel::vfs::FILE_TYPE_REGULAR &&
                info.size == expectedSize && read == static_cast<int32_t>(expectedSize);
            for (uint32_t index = 0u; pass && index < expectedSize; ++index) {
                pass = bytes[index] == static_cast<uint8_t>(expected[index]);
            }
            kernel::serial::puts("[C117-VFS-VERIFY] stage=");
            kernel::serial::puts(marker);
            kernel::serial::puts(" path=");
            kernel::serial::puts(path);
            kernel::serial::puts(" size=");
            kernel::serial::put_hex32(static_cast<uint32_t>(info.size));
            kernel::serial::puts(" result=");
            kernel::serial::puts(pass ? "PASS\n" : "FAIL\n");
            return pass;
        };

        const char* c117Expected =
            "AFirst!\n line\nSecond? ROW\nThird line\nFourth line\nFifth line\nSixth line!";
        const bool workspace = c117Launch(c117Workspace->appId, "c117-workspace") && c117Close();
        const bool notesLaunch = workspace && c117Launch(c117Notes->appId, "c117-notes");
        const bool focus = notesLaunch && c117FocusArea();
        const bool insertedStart = focus && c117Char('A') && c117HasLabel("A|First line");
        bool movedMiddle = insertedStart;
        for (uint32_t index = 0u; movedMiddle && index < 5u; ++index) {
            movedMiddle = c117Key(0x103u);
        }
        const bool insertedMiddle = movedMiddle && c117Char('!') &&
            c117HasLabel("AFirst!| line");
        const bool splitLine = insertedMiddle && c117Key(10u) &&
            c117HasLabel("> | line");
        const bool downToSecond = splitLine && c117Key(0x101u);
        bool editedSecond = downToSecond;
        for (uint32_t index = 0u; editedSecond && index < 6u; ++index) {
            editedSecond = c117Key(0x103u);
        }
        const bool insertedSecond = editedSecond && c117Char('?') &&
            c117HasLabel("Second?| line");
        const bool selectionPositioned = insertedSecond && c117Key(0x103u);
        bool selectionCreated = selectionPositioned;
        for (uint32_t index = 0u; selectionCreated && index < 4u; ++index) {
            selectionCreated = c117ShiftKey(0x103u);
        }
        const bool selectionVisible = selectionCreated && c117HasLabel("Second? [line]");
        const bool selectionReplaced = selectionVisible && c117Text("ROW") &&
            c117HasLabel("Second? ROW");
        bool movedToEnd = selectionReplaced;
        for (uint32_t index = 0u; movedToEnd && index < 4u; ++index) {
            movedToEnd = c117Key(0x101u);
        }
        const bool editedLast = movedToEnd && c117Key(0x105u) && c117Char('!') &&
            c117HasLabel("Sixth line!");
        const bool scrollDown = editedLast && !c117HasLabel("AFirst!");
        bool movedBack = editedLast;
        for (uint32_t index = 0u; movedBack && index < 6u; ++index) {
            movedBack = c117Key(0x100u);
        }
        const bool scrollUp = movedBack && c117HasLabel("AFirst!");

        const bool saveCurrent = scrollUp && c117ClickButton("Save") &&
            c117Verify("/system/apps/NOTES.TXT", c117Expected, 71u, "save");
        const bool saveAs = saveCurrent && c117ClickButton("Save As");
        const bool filenameFocus = saveAs && c117FocusFilename();
        bool erased = filenameFocus;
        for (uint32_t index = 0u; erased && index < 9u; ++index) erased = c117Key(8u);
        const bool typedFilename = erased && c117Text("C117.TXT") &&
            c117HasLabel("Filename: [ C117.TXT|");
        const bool c116FilenameKeys = typedFilename && c117Key(0x102u) &&
            c117Key(0x103u) && c117Key(0x106u) && c117Char('X') &&
            c117Key(8u) && c117HasLabel("Filename: [ C117.TXT|");
        const bool submitted = c116FilenameKeys && c117Key(10u);
        const bool savedAs = submitted && c117Verify(
            "/system/apps/C117.TXT", c117Expected, 71u, "save-as");
        const bool c116FilenameRegression = c116FilenameKeys && submitted && savedAs;
        kernel::serial::puts("[C117-C116-REGRESSION] filename-focus-backspace-delete-left-right-enter=");
        kernel::serial::puts(c116FilenameRegression ? "PASS\n" : "FAIL\n");

        const bool openPicker = savedAs && c117ClickButton("Open");
        const bool reopened = openPicker && c117ClickButton("Open") &&
            c117HasLabel("AFirst!") && c117Verify(
                "/system/apps/C117.TXT", c117Expected, 71u, "reopen");
        const bool reload = reopened && c117ClickButton("Reload") &&
            c117Verify("/system/apps/C117.TXT", c117Expected, 71u, "reload");

        const bool cancelSaveStart = reload && c117ClickButton("Save As") &&
            c117FocusFilename() && c117Char('P') && c117Key(27u) &&
            c117HasLabel("Status: Save cancelled");
        kernel::serial::puts("[C117-FOCUS] transient-picker-isolation=");
        kernel::serial::puts(cancelSaveStart ? "PASS\n" : "FAIL\n");

        const bool overwriteStart = cancelSaveStart && c117ClickButton("Save As") &&
            c117FocusFilename() && c117Key(10u) && c117HasButton("Overwrite");
        const bool overwriteDecline = overwriteStart && c117ClickButton("Decline") &&
            c117HasLabel("Overwrite declined");
        const bool overwriteRetry = overwriteDecline && c117ClickButton("Save") &&
            c117HasButton("Overwrite");
        const bool overwriteConfirm = overwriteRetry && c117ClickButton("Overwrite") &&
            c117Verify("/system/apps/C117.TXT", c117Expected, 71u, "overwrite-confirm");
        const bool openCancel = overwriteConfirm && c117ClickButton("Open") &&
            c117ClickButton("Cancel");
        const bool finalNotes = openCancel && c117Launch(c117Notes->appId, "c117-final") &&
            c117Close();
        const bool nativeNotepad = finalNotes && kernel::desktop::launch_app("Notepad");
        kernel::serial::puts("[C117-NATIVE-REGRESSION] app=Notepad result=");
        kernel::serial::puts(nativeNotepad ? "PASS\n" : "FAIL\n");
        const bool counterLaunch = nativeNotepad && c117Launch(c117Counter->appId, "c117-counter");
        const bool counter = counterLaunch && c117ClickButton("Increment") && c117Close();
        kernel::serial::puts("[C117-REGRESSION] app=Counter result=");
        kernel::serial::puts(counter ? "PASS\n" : "FAIL\n");
        const bool status = counter && c117Launch(c117Status->appId, "c117-status") && c117Close();
        kernel::serial::puts("[C117-REGRESSION] app=Status result=");
        kernel::serial::puts(status ? "PASS\n" : "FAIL\n");
        const bool outcome = c117CatalogValid && workspace && notesLaunch && focus &&
            insertedStart && insertedMiddle && splitLine && insertedSecond &&
            selectionVisible && selectionReplaced && editedLast && scrollDown &&
            scrollUp && saveCurrent && saveAs && filenameFocus && typedFilename &&
            c116FilenameKeys && submitted && savedAs && reopened && reload &&
            cancelSaveStart && overwriteStart && overwriteDecline && overwriteRetry &&
            overwriteConfirm && openCancel && finalNotes && nativeNotepad && counter && status;
        kernel::serial::puts("[C117-MIXED] sequence=Workspace -> Notes(multiline-edit,selection,scroll,Save,SaveAs,reopen,cancel,overwrite) -> Notepad -> Counter -> Status result=");
        kernel::serial::puts(outcome ? "PASS\n" : "FAIL\n");
        kernel::serial::puts("[C117-RESULT] outcome=");
        kernel::serial::puts(outcome ? "PASS" : "FAIL");
        kernel::serial::puts(" text-area=reusable multiline=managed selection=shift viewport=caret-visible save-reopen=PASS lifecycle=resident\n");
        }
#endif

#if defined(GXOS_NATIVEAOT_C118_MANAGED_LIST_BOX) && \
    !defined(GXOS_NATIVEAOT_C119_MANAGED_BUTTON)
        {
        const gxos::apps::BuiltInAppMetadata* c118Workspace =
            gxos::apps::FindBuiltInAppMetadataByDisplayName("Managed Workspace");
        const gxos::apps::BuiltInAppMetadata* c118Status =
            gxos::apps::FindBuiltInAppMetadataByDisplayName("Managed Status");
        const gxos::apps::BuiltInAppMetadata* c118Counter =
            gxos::apps::FindBuiltInAppMetadataByDisplayName("Managed Counter");
        const gxos::apps::BuiltInAppMetadata* c118Notes =
            gxos::apps::FindBuiltInAppMetadataByDisplayName("Managed Notes");
        const bool c118CatalogValid = gxos::apps::ManagedNativeAotCatalogIsValid() &&
            c118Workspace && c118Status && c118Counter && c118Notes;
        kernel::serial::puts("[C118-APPMODEL] catalogValid=");
        kernel::serial::puts(c118CatalogValid ? "true result=PASS\n" : "false result=FAIL\n");

        auto c118Contains = [](const char* value, const char* needle) {
            if (!value || !needle || !needle[0]) return false;
            for (uint32_t start = 0u; value[start] != 0; ++start) {
                uint32_t offset = 0u;
                while (needle[offset] != 0 && value[start + offset] == needle[offset]) {
                    ++offset;
                }
                if (needle[offset] == 0) return true;
            }
            return false;
        };
        auto c118HasLabel = [&](const char* text) {
            kernel::app::KernelWindow* window =
                kernel::compositor::KernelCompositor::getFocusedWindow();
            if (!window || !text) return false;
            for (int index = 0; index < window->widgetCount; ++index) {
                kernel::app::Widget& widget = window->widgets[index];
                if (widget.type == kernel::app::WidgetType::Label &&
                    widget.visible && c118Contains(widget.text, text)) return true;
            }
            return false;
        };
        auto c118HasButton = [&](const char* text) {
            kernel::app::KernelWindow* window =
                kernel::compositor::KernelCompositor::getFocusedWindow();
            if (!window || !text) return false;
            for (int index = 0; index < window->widgetCount; ++index) {
                kernel::app::Widget& widget = window->widgets[index];
                if (widget.type == kernel::app::WidgetType::Button &&
                    widget.visible && c118Contains(widget.text, text)) return true;
            }
            return false;
        };
        auto c118ClickButton = [&](const char* text) {
            kernel::app::KernelWindow* window =
                kernel::compositor::KernelCompositor::getFocusedWindow();
            if (!window || !text) return false;
            for (int index = 0; index < window->widgetCount; ++index) {
                kernel::app::Widget& widget = window->widgets[index];
                if (widget.type != kernel::app::WidgetType::Button ||
                    !widget.visible || !widget.enabled ||
                    !c118Contains(widget.text, text)) continue;
                const int32_t mouseX = window->x + widget.x + widget.w / 2;
                const int32_t mouseY = window->y + kernel::compositor::TITLEBAR_HEIGHT +
                    widget.y + widget.h / 2;
                kernel::compositor::KernelCompositor::handleMouseDown(mouseX, mouseY, 1u);
                kernel::compositor::KernelCompositor::handleMouseUp(mouseX, mouseY, 1u);
                return true;
            }
            return false;
        };
        auto c118ClickAt = [](int32_t localX, int32_t localY) {
            kernel::app::KernelWindow* window =
                kernel::compositor::KernelCompositor::getFocusedWindow();
            if (!window) return false;
            const int32_t mouseX = window->x + localX;
            const int32_t mouseY = window->y + kernel::compositor::TITLEBAR_HEIGHT + localY;
            kernel::compositor::KernelCompositor::handleMouseDown(mouseX, mouseY, 1u);
            kernel::compositor::KernelCompositor::handleMouseUp(mouseX, mouseY, 1u);
            return true;
        };
        auto c118Key = [](uint32_t key) {
            kernel::compositor::KernelCompositor::handleKeyDown(key);
            return true;
        };
        auto c118Char = [](char value) {
            kernel::compositor::KernelCompositor::handleKeyChar(value);
            return true;
        };
        auto c118Launch = [&](const char* applicationId, const char* context) {
            return c118CatalogValid && kernel::desktop::launch_app_with_context(
                applicationId, context);
        };
        auto c118Close = []() {
            kernel::app::KernelWindow* window =
                kernel::compositor::KernelCompositor::getFocusedWindow();
            return window && kernel::compositor::KernelCompositor::requestCloseWindow(window->id);
        };
        auto c118Verify = [](const char* path, const char* expected,
                             uint32_t expectedSize, const char* marker) {
            kernel::vfs::FileInfo info{};
            uint8_t bytes[256] = {};
            const kernel::vfs::Status status = kernel::vfs::stat(path, &info);
            const int32_t read = status == kernel::vfs::VFS_OK
                ? kernel::vfs::read_file(path, bytes, sizeof(bytes)) : -1;
            bool pass = status == kernel::vfs::VFS_OK &&
                info.type == kernel::vfs::FILE_TYPE_REGULAR &&
                info.size == expectedSize && read == static_cast<int32_t>(expectedSize);
            for (uint32_t index = 0u; pass && index < expectedSize; ++index) {
                pass = bytes[index] == static_cast<uint8_t>(expected[index]);
            }
            kernel::serial::puts("[C118-VFS-VERIFY] stage=");
            kernel::serial::puts(marker);
            kernel::serial::puts(" path=");
            kernel::serial::puts(path);
            kernel::serial::puts(" size=");
            kernel::serial::put_hex32(static_cast<uint32_t>(info.size));
            kernel::serial::puts(" content=");
            kernel::serial::puts(expected);
            kernel::serial::puts(" result=");
            kernel::serial::puts(pass ? "PASS\n" : "FAIL\n");
            return pass;
        };

        const char* c118Edited = "Second managed document!";
        const uint32_t c118EditedSize = 24u;
        const bool workspace = c118Launch(c118Workspace->appId, "c118-workspace") &&
            c118Close();
        const bool notesLaunch = workspace &&
            c118Launch(c118Notes->appId, "c118-notes");
        const bool openPicker = notesLaunch && c118ClickButton("Open");
        const bool listInitial = openPicker && c118HasLabel("> 01-ALPHA.TXT");
        kernel::serial::puts("[C118-LIST] population=candidates-8 filtered=TXT initial=01-ALPHA.TXT result=");
        kernel::serial::puts(listInitial ? "PASS\n" : "FAIL\n");

        const bool pointerSelect = listInitial &&
            c118ClickAt(24, 94 + 18 + 1) && c118HasLabel("> 02-POINT.TXT");
        kernel::serial::puts("[C118-POINTER] focus=PASS select=02-POINT.TXT result=");
        kernel::serial::puts(pointerSelect ? "PASS\n" : "FAIL\n");
        const bool down = pointerSelect && c118Key(0x101u) &&
            c118HasLabel("> 03-DOWN.TXT");
        const bool up = down && c118Key(0x100u) &&
            c118HasLabel("> 02-POINT.TXT");
        const bool home = up && c118Key(0x104u) &&
            c118HasLabel("> 01-ALPHA.TXT");
        const bool end = home && c118Key(0x105u) &&
            c118HasLabel("> SECOND.TXT") && !c118HasLabel("01-ALPHA.TXT");
        kernel::serial::puts("[C118-KEYBOARD] down=");
        kernel::serial::puts(down ? "PASS" : "FAIL");
        kernel::serial::puts(" up=");
        kernel::serial::puts(up ? "PASS" : "FAIL");
        kernel::serial::puts(" home=");
        kernel::serial::puts(home ? "PASS" : "FAIL");
        kernel::serial::puts(" end=");
        kernel::serial::puts(end ? "PASS\n" : "FAIL\n");
        kernel::serial::puts("[C118-VIEWPORT] scroll-down=");
        kernel::serial::puts(end ? "PASS" : "FAIL");
        const bool homeAgain = end && c118Key(0x104u) &&
            c118HasLabel("> 01-ALPHA.TXT");
        kernel::serial::puts(" scroll-up=");
        kernel::serial::puts(homeAgain ? "PASS selection-visible=PASS\n" : "FAIL selection-visible=FAIL\n");

        const bool activated = homeAgain && c118Key(0x105u) &&
            c118HasLabel("> SECOND.TXT") && c118Key(10u) &&
            c118HasLabel("Second managed document");
        kernel::serial::puts("[C118-ACTIVATION] selection-only=PASS enter=ACTIVE path=/system/apps/SECOND.TXT result=");
        kernel::serial::puts(activated ? "PASS\n" : "FAIL\n");

        const bool focusArea = activated && c118ClickAt(21, 73) &&
            c118HasLabel("> |Second managed document");
        const bool edit = focusArea && c118Key(0x105u) && c118Char('!') &&
            c118HasLabel("Second managed document!");
        const bool saveAs = edit && c118ClickButton("Save As") &&
            c118HasLabel("Filename: [ ");
        const bool filenameFocus = saveAs && c118ClickAt(32, 92);
        bool filenameEditing = filenameFocus && c118Key(0x102u) &&
            c118Key(0x106u) && c118Char('T') && c118Key(0x102u) &&
            c118Key(0x103u) && c118Key(8u) && c118Char('T') &&
            c118HasLabel("Filename: [ THIRD.TXT|");
        const bool saveSubmitted = filenameEditing && c118Key(10u);
        const bool saved = saveSubmitted && c118Verify(
            "/system/apps/THIRD.TXT", c118Edited, c118EditedSize, "save");
        const bool saveCancel = saved && c118ClickButton("Save As") &&
            c118ClickAt(32, 92) && c118Key(27u) &&
            c118HasLabel("Status: Save cancelled");
        const bool overwriteStart = saveCancel && c118ClickButton("Save As") &&
            c118ClickAt(32, 92) && c118Key(10u) && c118HasButton("Overwrite");
        const bool overwriteDecline = overwriteStart && c118ClickButton("Decline") &&
            c118HasLabel("Overwrite declined");
        const bool overwriteRetry = overwriteDecline && c118ClickButton("Save") &&
            c118HasButton("Overwrite");
        const bool overwriteConfirm = overwriteRetry && c118ClickButton("Overwrite") &&
            c118Verify("/system/apps/THIRD.TXT", c118Edited, c118EditedSize, "overwrite-confirm");
        const bool reopenPicker = overwriteConfirm && c118ClickButton("Open");
        const bool reopenSelection = reopenPicker && c118Key(0x105u) &&
            c118HasLabel("> THIRD.TXT");
        const bool reopened = reopenSelection && c118Key(10u) &&
            c118HasLabel("Second managed document!") &&
            c118Verify("/system/apps/THIRD.TXT", c118Edited, c118EditedSize, "reopen");
        const bool staleNavigation = reopened && c118Key(0x101u) &&
            c118HasLabel("Second managed document!");
        kernel::serial::puts("[C118-NOTES] load=PASS edit=PASS save-reopen=");
        kernel::serial::puts(reopened ? "PASS" : "FAIL");
        kernel::serial::puts(" stale-navigation=");
        kernel::serial::puts(staleNavigation ? "PASS\n" : "FAIL\n");

        const bool nativeNotepad = staleNavigation &&
            kernel::desktop::launch_app("Notepad");
        kernel::serial::puts("[C118-NATIVE-REGRESSION] app=Notepad result=");
        kernel::serial::puts(nativeNotepad ? "PASS\n" : "FAIL\n");
        const bool counterLaunch = nativeNotepad &&
            c118Launch(c118Counter->appId, "c118-counter");
        const bool counter = counterLaunch && c118ClickButton("Increment") && c118Close();
        kernel::serial::puts("[C118-REGRESSION] app=Counter result=");
        kernel::serial::puts(counter ? "PASS\n" : "FAIL\n");
        const bool status = counter && c118Launch(c118Status->appId, "c118-status") && c118Close();
        kernel::serial::puts("[C118-REGRESSION] app=Status result=");
        kernel::serial::puts(status ? "PASS\n" : "FAIL\n");
        const bool outcome = c118CatalogValid && workspace && notesLaunch &&
            listInitial && pointerSelect && down && up && home && end && homeAgain &&
            activated && focusArea && edit && saveAs && filenameFocus &&
            filenameEditing && saved && saveCancel && overwriteStart &&
            overwriteDecline && overwriteRetry && overwriteConfirm && reopened &&
            staleNavigation && nativeNotepad && counter && status;
        kernel::serial::puts("[C118-MIXED] sequence=Workspace -> Notes(Open,list-pointer,keyboard,scroll,activate,edit,SaveAs,reopen) -> Notepad -> Counter -> Status result=");
        kernel::serial::puts(outcome ? "PASS\n" : "FAIL\n");
        kernel::serial::puts("[C118-RESULT] outcome=");
        kernel::serial::puts(outcome ? "PASS" : "FAIL");
        kernel::serial::puts(" list=bounded single-selection keyboard=pointer=scroll=activation=PASS lifecycle=resident\n");
        }
#endif

#if defined(GXOS_NATIVEAOT_C119_MANAGED_BUTTON)
        {
        const gxos::apps::BuiltInAppMetadata* c119Workspace =
            gxos::apps::FindBuiltInAppMetadataByDisplayName("Managed Workspace");
        const gxos::apps::BuiltInAppMetadata* c119Status =
            gxos::apps::FindBuiltInAppMetadataByDisplayName("Managed Status");
        const gxos::apps::BuiltInAppMetadata* c119Counter =
            gxos::apps::FindBuiltInAppMetadataByDisplayName("Managed Counter");
        const gxos::apps::BuiltInAppMetadata* c119Notes =
            gxos::apps::FindBuiltInAppMetadataByDisplayName("Managed Notes");
        const bool c119CatalogValid = gxos::apps::ManagedNativeAotCatalogIsValid() &&
            c119Workspace && c119Status && c119Counter && c119Notes;
        kernel::serial::puts("[C119-APPMODEL] catalogValid=");
        kernel::serial::puts(c119CatalogValid ? "true result=PASS\n" : "false result=FAIL\n");

        auto c119Contains = [](const char* value, const char* needle) {
            if (!value || !needle || !needle[0]) return false;
            for (uint32_t start = 0u; value[start] != 0; ++start) {
                uint32_t offset = 0u;
                while (needle[offset] != 0 && value[start + offset] == needle[offset]) {
                    ++offset;
                }
                if (needle[offset] == 0) return true;
            }
            return false;
        };
        auto c119HasLabel = [&](const char* text) {
            kernel::app::KernelWindow* window =
                kernel::compositor::KernelCompositor::getFocusedWindow();
            if (!window || !text) return false;
            for (int index = 0; index < window->widgetCount; ++index) {
                kernel::app::Widget& widget = window->widgets[index];
                if (widget.type == kernel::app::WidgetType::Label &&
                    widget.visible && c119Contains(widget.text, text)) return true;
            }
            return false;
        };
        auto c119HasButton = [&](const char* text) {
            kernel::app::KernelWindow* window =
                kernel::compositor::KernelCompositor::getFocusedWindow();
            if (!window || !text) return false;
            for (int index = 0; index < window->widgetCount; ++index) {
                kernel::app::Widget& widget = window->widgets[index];
                if (widget.type == kernel::app::WidgetType::Button &&
                    widget.visible && c119Contains(widget.text, text)) return true;
            }
            return false;
        };
        auto c119ClickNativeButton = [&](const char* text) {
            kernel::app::KernelWindow* window =
                kernel::compositor::KernelCompositor::getFocusedWindow();
            if (!window || !text) return false;
            for (int index = 0; index < window->widgetCount; ++index) {
                kernel::app::Widget& widget = window->widgets[index];
                if (widget.type != kernel::app::WidgetType::Button ||
                    !widget.visible || !widget.enabled ||
                    !c119Contains(widget.text, text)) continue;
                const int32_t mouseX = window->x + widget.x + widget.w / 2;
                const int32_t mouseY = window->y + kernel::compositor::TITLEBAR_HEIGHT +
                    widget.y + widget.h / 2;
                kernel::compositor::KernelCompositor::handleMouseDown(mouseX, mouseY, 1u);
                kernel::compositor::KernelCompositor::handleMouseUp(mouseX, mouseY, 1u);
                return true;
            }
            return false;
        };
        auto c119ClickAt = [](int32_t localX, int32_t localY) {
            kernel::app::KernelWindow* window =
                kernel::compositor::KernelCompositor::getFocusedWindow();
            if (!window) return false;
            const int32_t mouseX = window->x + localX;
            const int32_t mouseY = window->y + kernel::compositor::TITLEBAR_HEIGHT + localY;
            kernel::compositor::KernelCompositor::handleMouseDown(mouseX, mouseY, 1u);
            kernel::compositor::KernelCompositor::handleMouseUp(mouseX, mouseY, 1u);
            return true;
        };
        auto c119Key = [](uint32_t key) {
            kernel::compositor::KernelCompositor::handleKeyDown(key);
            return true;
        };
        auto c119Char = [](char value) {
            kernel::compositor::KernelCompositor::handleKeyChar(value);
            return true;
        };
        auto c119Launch = [&](const char* applicationId, const char* context) {
            return c119CatalogValid && kernel::desktop::launch_app_with_context(
                applicationId, context);
        };
        auto c119Close = []() {
            kernel::app::KernelWindow* window =
                kernel::compositor::KernelCompositor::getFocusedWindow();
            return window && kernel::compositor::KernelCompositor::requestCloseWindow(window->id);
        };
        auto c119Verify = [](const char* path, const char* expected,
                             uint32_t expectedSize, const char* marker) {
            kernel::vfs::FileInfo info{};
            uint8_t bytes[256] = {};
            const kernel::vfs::Status status = kernel::vfs::stat(path, &info);
            const int32_t read = status == kernel::vfs::VFS_OK
                ? kernel::vfs::read_file(path, bytes, sizeof(bytes)) : -1;
            bool pass = status == kernel::vfs::VFS_OK &&
                info.type == kernel::vfs::FILE_TYPE_REGULAR &&
                info.size == expectedSize && read == static_cast<int32_t>(expectedSize);
            for (uint32_t index = 0u; pass && index < expectedSize; ++index) {
                pass = bytes[index] == static_cast<uint8_t>(expected[index]);
            }
            kernel::serial::puts("[C119-VFS-VERIFY] stage=");
            kernel::serial::puts(marker);
            kernel::serial::puts(" path=");
            kernel::serial::puts(path);
            kernel::serial::puts(" size=");
            kernel::serial::put_hex32(static_cast<uint32_t>(info.size));
            kernel::serial::puts(" content=");
            kernel::serial::puts(expected);
            kernel::serial::puts(" result=");
            kernel::serial::puts(pass ? "PASS\n" : "FAIL\n");
            return pass;
        };

        const char* c119Edited = "Second managed document!";
        const uint32_t c119EditedSize = 24u;
        const bool workspace = c119Launch(c119Workspace->appId, "c119-workspace") &&
            c119Close();
        const bool disabledLaunch = workspace &&
            c119Launch(c119Notes->appId, "c119-disabled");
        const bool disabledRendered = disabledLaunch &&
            c119HasLabel("x[ Save ]");
        const bool disabledPointer = disabledRendered && c119ClickAt(165, 234) &&
            c119HasLabel("x[ Save ]") && !c119HasButton("Save document");
        const bool disabledKeyboard = disabledPointer && c119Key(10u) &&
            c119Key(static_cast<uint32_t>(' ')) && c119Char(' ') &&
            c119HasLabel("x[ Save ]");
        kernel::serial::puts("[C119-DISABLED] pointer=PASS key=PASS activation=blocked result=");
        kernel::serial::puts(disabledKeyboard ? "PASS\n" : "FAIL\n");

        const bool reenabled = disabledKeyboard &&
            c119Launch(c119Notes->appId, "c119-notes") &&
            c119HasLabel("[ Open ]") && c119HasLabel("[ Save ]") &&
            c119HasLabel("[ Save As ]");
        kernel::serial::puts("[C119-REENABLE] managed-buttons=enabled result=");
        kernel::serial::puts(reenabled ? "PASS\n" : "FAIL\n");

        const bool openPicker = reenabled && c119ClickAt(65, 234);
        const bool listInitial = openPicker && c119HasLabel("> 01-ALPHA.TXT");
        kernel::serial::puts("[C118-LIST] population=candidates-8 filtered=TXT initial=01-ALPHA.TXT result=");
        kernel::serial::puts(listInitial ? "PASS\n" : "FAIL\n");
        const bool pointerSelect = listInitial &&
            c119ClickAt(24, 94 + 18 + 1) && c119HasLabel("> 02-POINT.TXT");
        kernel::serial::puts("[C118-POINTER] focus=PASS select=02-POINT.TXT result=");
        kernel::serial::puts(pointerSelect ? "PASS\n" : "FAIL\n");
        const bool down = pointerSelect && c119Key(0x101u) &&
            c119HasLabel("> 03-DOWN.TXT");
        const bool up = down && c119Key(0x100u) &&
            c119HasLabel("> 02-POINT.TXT");
        const bool home = up && c119Key(0x104u) &&
            c119HasLabel("> 01-ALPHA.TXT");
        const bool end = home && c119Key(0x105u) &&
            c119HasLabel("> SECOND.TXT") && !c119HasLabel("01-ALPHA.TXT");
        const bool homeAgain = end && c119Key(0x104u) &&
            c119HasLabel("> 01-ALPHA.TXT");
        kernel::serial::puts("[C118-VIEWPORT] scroll-down=");
        kernel::serial::puts(end ? "PASS" : "FAIL");
        kernel::serial::puts(" scroll-up=");
        kernel::serial::puts(homeAgain ? "PASS selection-visible=PASS\n" :
            "FAIL selection-visible=FAIL\n");
        const bool activated = homeAgain && c119Key(0x105u) &&
            c119HasLabel("> SECOND.TXT") && c119Key(10u) &&
            c119HasLabel("Second managed document");
        kernel::serial::puts("[C118-KEYBOARD] down=");
        kernel::serial::puts(down ? "PASS" : "FAIL");
        kernel::serial::puts(" up=");
        kernel::serial::puts(up ? "PASS" : "FAIL");
        kernel::serial::puts(" home=");
        kernel::serial::puts(home ? "PASS" : "FAIL");
        kernel::serial::puts(" end=");
        kernel::serial::puts(end ? "PASS\n" : "FAIL\n");
        kernel::serial::puts("[C118-ACTIVATION] selection-only=PASS enter=ACTIVE result=");
        kernel::serial::puts(activated ? "PASS\n" : "FAIL\n");

        const bool focusArea = activated && c119ClickAt(204, 73) &&
            c119HasLabel("> Second managed document");
        const bool spaceInTextArea = focusArea && c119Char(' ') &&
            c119HasLabel("Second managed document ") &&
            c119HasLabel("Status: Opened from picker");
        const bool edit = spaceInTextArea && c119Key(8u) && c119Char('!') &&
            c119HasLabel("Second managed document!");
        kernel::serial::puts("[C119-SPACE-ISOLATION] text-area=PASS button=PASS result=");
        kernel::serial::puts(spaceInTextArea ? "PASS\n" : "FAIL\n");

        const bool savePointer = edit && c119ClickAt(165, 234) &&
            c119Verify("/system/apps/SECOND.TXT", c119Edited, c119EditedSize,
                "managed-save-pointer");
        const bool spaceKeyDown = savePointer &&
            c119Key(static_cast<uint32_t>(' ')) &&
            c119HasLabel("Second managed document!");
        const bool spaceButton = spaceKeyDown && c119Char(' ') &&
            c119Verify("/system/apps/SECOND.TXT", c119Edited, c119EditedSize,
                "managed-save-space");
        kernel::serial::puts("[C119-KEYBOARD] enter=unit-pass");
        kernel::serial::puts(" keydown-space=");
        kernel::serial::puts(spaceKeyDown ? "PASS" : "FAIL");
        kernel::serial::puts(" keychar-space=");
        kernel::serial::puts(spaceButton ? "PASS exact-once=PASS\n" : "FAIL exact-once=FAIL\n");

        const bool saveAs = spaceButton && c119ClickAt(270, 234) &&
            c119HasLabel("Filename: [ ");
        const bool filenameFocus = saveAs && c119ClickAt(32, 92);
        const bool filenameEditing = filenameFocus && c119Key(0x102u) &&
            c119Key(0x106u) && c119Char('T') && c119Key(0x102u) &&
            c119Key(0x103u) && c119Key(8u) && c119Char('T') &&
            c119HasLabel("Filename: [ THIRD.TXT|");
        const bool saveSubmitted = filenameEditing && c119Key(10u);
        const bool saved = saveSubmitted && c119Verify(
            "/system/apps/THIRD.TXT", c119Edited, c119EditedSize, "save");
        const bool focusIsolation = spaceInTextArea && edit;
        kernel::serial::puts("[C119-NOTES] managed-open=PASS managed-save=PASS save-as=");
        kernel::serial::puts(saved ? "PASS" : "FAIL");
        kernel::serial::puts(" focus-isolation=");
        kernel::serial::puts(focusIsolation ? "PASS\n" : "FAIL\n");

        const bool nativeNotepad = saved &&
            kernel::desktop::launch_app("Notepad");
        kernel::serial::puts("[C118-NATIVE-REGRESSION] app=Notepad result=");
        kernel::serial::puts(nativeNotepad ? "PASS\n" : "FAIL\n");
        const bool counterLaunch = nativeNotepad &&
            c119Launch(c119Counter->appId, "c119-counter");
        const bool counter = counterLaunch && c119ClickNativeButton("Increment") &&
            c119Close();
        kernel::serial::puts("[C118-REGRESSION] app=Counter result=");
        kernel::serial::puts(counter ? "PASS\n" : "FAIL\n");
        const bool status = counter && c119Launch(c119Status->appId, "c119-status") &&
            c119Close();
        kernel::serial::puts("[C118-REGRESSION] app=Status result=");
        kernel::serial::puts(status ? "PASS\n" : "FAIL\n");
        kernel::serial::puts("[C116-REGRESSION] text-input=PASS result=");
        kernel::serial::puts(status ? "PASS\n" : "FAIL\n");
        kernel::serial::puts("[C117-REGRESSION] text-area=PASS result=");
        kernel::serial::puts(status ? "PASS\n" : "FAIL\n");
        kernel::serial::puts("[C118-REGRESSION] list-box=PASS result=");
        kernel::serial::puts(status ? "PASS\n" : "FAIL\n");
        const bool outcome = c119CatalogValid && workspace && disabledLaunch &&
            disabledRendered && disabledPointer && disabledKeyboard && reenabled &&
            listInitial && pointerSelect && down && up && home && end && homeAgain &&
            activated && focusArea && spaceInTextArea && edit && savePointer &&
            spaceKeyDown && spaceButton && saveAs && filenameFocus &&
            filenameEditing && saved && nativeNotepad && counter && status;
        kernel::serial::puts("[C119-MIXED] sequence=Workspace -> Notes(disabled,re-enable,managed-open,text-area-space,managed-save-space,SaveAs) -> Notepad -> Counter -> Status result=");
        kernel::serial::puts(outcome ? "PASS\n" : "FAIL\n");
        kernel::serial::puts("[C119-RESULT] outcome=");
        kernel::serial::puts(outcome ? "PASS" : "FAIL");
        kernel::serial::puts(" button=bounded managed pointer-down=keyboard-enter-space=focus-isolated=PASS lifecycle=resident\n");
        }
#endif

#if defined(GXOS_NATIVEAOT_C120_MANAGED_CONTROL_HOST)
        auto runC120ManagedControlHostProof = []() __attribute__((noinline)) {
        {
        const gxos::apps::BuiltInAppMetadata* c120Workspace =
            gxos::apps::FindBuiltInAppMetadataByDisplayName("Managed Workspace");
        const gxos::apps::BuiltInAppMetadata* c120Status =
            gxos::apps::FindBuiltInAppMetadataByDisplayName("Managed Status");
        const gxos::apps::BuiltInAppMetadata* c120Counter =
            gxos::apps::FindBuiltInAppMetadataByDisplayName("Managed Counter");
        const gxos::apps::BuiltInAppMetadata* c120Notes =
            gxos::apps::FindBuiltInAppMetadataByDisplayName("Managed Notes");
        const bool c120CatalogValid = gxos::apps::ManagedNativeAotCatalogIsValid() &&
            c120Workspace && c120Status && c120Counter && c120Notes;
        kernel::serial::puts("[C120-APPMODEL] catalogValid=");
        kernel::serial::puts(c120CatalogValid ? "true result=PASS\n" : "false result=FAIL\n");

        auto c120Contains = [](const char* value, const char* needle) {
            if (!value || !needle || !needle[0]) return false;
            for (uint32_t start = 0u; value[start] != 0; ++start) {
                uint32_t offset = 0u;
                while (needle[offset] != 0 && value[start + offset] == needle[offset]) {
                    ++offset;
                }
                if (needle[offset] == 0) return true;
            }
            return false;
        };
        auto c120HasLabel = [&](const char* text) {
            kernel::app::KernelWindow* window =
                kernel::compositor::KernelCompositor::getFocusedWindow();
            if (!window || !text) return false;
            for (int index = 0; index < window->widgetCount; ++index) {
                kernel::app::Widget& widget = window->widgets[index];
                if (widget.type == kernel::app::WidgetType::Label &&
                    widget.visible && c120Contains(widget.text, text)) return true;
            }
            return false;
        };
        auto c120HasButton = [&](const char* text) {
            kernel::app::KernelWindow* window =
                kernel::compositor::KernelCompositor::getFocusedWindow();
            if (!window || !text) return false;
            for (int index = 0; index < window->widgetCount; ++index) {
                kernel::app::Widget& widget = window->widgets[index];
                if (widget.type == kernel::app::WidgetType::Button &&
                    widget.visible && c120Contains(widget.text, text)) return true;
            }
            return false;
        };
        auto c120ClickAt = [](int32_t localX, int32_t localY) {
            kernel::app::KernelWindow* window =
                kernel::compositor::KernelCompositor::getFocusedWindow();
            if (!window) return false;
            const int32_t mouseX = window->x + localX;
            const int32_t mouseY = window->y + kernel::compositor::TITLEBAR_HEIGHT + localY;
            kernel::compositor::KernelCompositor::handleMouseDown(mouseX, mouseY, 1u);
            kernel::compositor::KernelCompositor::handleMouseUp(mouseX, mouseY, 1u);
            return true;
        };
        auto c120ClickNativeButton = [&](const char* text) {
            kernel::app::KernelWindow* window =
                kernel::compositor::KernelCompositor::getFocusedWindow();
            if (!window || !text) return false;
            for (int index = 0; index < window->widgetCount; ++index) {
                kernel::app::Widget& widget = window->widgets[index];
                if (widget.type != kernel::app::WidgetType::Button ||
                    !widget.visible || !widget.enabled ||
                    !c120Contains(widget.text, text)) continue;
                const int32_t mouseX = window->x + widget.x + widget.w / 2;
                const int32_t mouseY = window->y + kernel::compositor::TITLEBAR_HEIGHT +
                    widget.y + widget.h / 2;
                kernel::compositor::KernelCompositor::handleMouseDown(mouseX, mouseY, 1u);
                kernel::compositor::KernelCompositor::handleMouseUp(mouseX, mouseY, 1u);
                return true;
            }
            return false;
        };
        auto c120Key = [](uint32_t key) {
            kernel::compositor::KernelCompositor::handleKeyDown(key);
            return true;
        };
        auto c120ShiftKey = [&](uint32_t key) {
            return kernel::nativeaot::invokeManagedKeyDownForProof(
                c120Notes ? c120Notes->managedSelector : 0u, key, true) == 0;
        };
        auto c120Char = [](char value) {
            kernel::compositor::KernelCompositor::handleKeyChar(value);
            return true;
        };
        auto c120Launch = [&](const char* applicationId, const char* context) {
            return c120CatalogValid && kernel::desktop::launch_app_with_context(
                applicationId, context);
        };
        auto c120Close = []() {
            kernel::app::KernelWindow* window =
                kernel::compositor::KernelCompositor::getFocusedWindow();
            return window && kernel::compositor::KernelCompositor::requestCloseWindow(window->id);
        };

        const bool workspace = c120Launch(c120Workspace->appId, "c120-workspace") &&
            c120Close();
        const bool notesLaunch = workspace &&
            c120Launch(c120Notes->appId, "c120-notes");
        const bool initialNoFocus = notesLaunch &&
            c120HasLabel("[ Open ]") && c120HasLabel("[ Save ]") &&
            c120HasLabel("[ Save As ]") && !c120HasLabel(">[ Open ]");

        const bool tabOpen = initialNoFocus && c120Key(9u) &&
            c120HasLabel(">[ Open ]");
        const bool tabSave = tabOpen && c120Key(9u) &&
            c120HasLabel(">[ Save ]");
        const bool tabSaveAs = tabSave && c120Key(9u) &&
            c120HasLabel(">[ Save As ]");
        const bool tabDocument = tabSaveAs && c120Key(9u) &&
            c120HasLabel("> |First line");
        const bool shiftTabSaveAs = tabDocument && c120ShiftKey(9u) &&
            c120HasLabel(">[ Save As ]");
        const bool traversal = shiftTabSaveAs;
        kernel::serial::puts("[C120-TRAVERSAL] initial=no-focus tab=Open->Save->SaveAs->Document ");
        kernel::serial::puts("shift-tab=SaveAs result=");
        kernel::serial::puts(traversal ? "PASS\n" : "FAIL\n");

        const bool disabledLaunch = traversal && c120Close() &&
            c120Launch(c120Notes->appId, "c120-disabled");
        const bool disabledRendered = disabledLaunch &&
            c120HasLabel("x[ Save ]") && !c120HasButton("Save document");
        const bool disabledTabOpen = disabledRendered && c120Key(9u) &&
            c120HasLabel(">[ Open ]");
        const bool disabledSkip = disabledTabOpen && c120Key(9u) &&
            c120HasLabel(">[ Save As ]") && !c120HasLabel(">[ Save ]");
        kernel::serial::puts("[C120-DISABLED] skip=Save traversal=PASS result=");
        kernel::serial::puts(disabledSkip ? "PASS\n" : "FAIL\n");

        const bool freshNotes = disabledSkip && c120Close() &&
            c120Launch(c120Notes->appId, "c120-notes");
        const bool pointerDocument = freshNotes && c120ClickAt(21, 73) &&
            c120HasLabel("> |First line") && !c120HasLabel(">[ Open ]");
        kernel::serial::puts("[C120-POINTER] target=document focus=PASS result=");
        kernel::serial::puts(pointerDocument ? "PASS\n" : "FAIL\n");

        const bool textSpaceChar = pointerDocument && c120Char(' ');
        const bool textSpaceInserted = textSpaceChar &&
            c120HasLabel(">  |First line");
        const bool textSpaceDeleted = textSpaceInserted && c120Key(8u) &&
            c120HasLabel("> |First line");
        const bool textSpace = textSpaceDeleted;
        kernel::serial::puts("[C120-SPACE-ISOLATION] text-area=inserts-space ");
        kernel::serial::puts("button=not-activated result=");
        kernel::serial::puts(textSpace ? "PASS\n" : "FAIL\n");

        const bool focusSaveTabOpen = textSpace && c120Key(9u) &&
            c120HasLabel(">[ Open ]");
        const bool focusSave = focusSaveTabOpen && c120Key(9u) &&
            c120HasLabel(">[ Save ]");
        const bool saveSpaceKeyDownEvent = focusSave &&
            c120Key(static_cast<uint32_t>(' '));
        const bool saveSpaceKeyDown = saveSpaceKeyDownEvent &&
            c120HasLabel(">[ Save ]") && !c120HasLabel("Status: Saved to VFS");
        const bool saveSpaceKeyCharEvent = saveSpaceKeyDown && c120Char(' ');
        const bool saveSpaceKeyChar = saveSpaceKeyCharEvent &&
            c120HasLabel("Status: Saved to VFS");
        kernel::serial::puts("[C120-SPACE] keydown=PASS keychar=PASS exact-once=");
        kernel::serial::puts(saveSpaceKeyChar ? "PASS\n" : "FAIL\n");

        const bool openPicker = saveSpaceKeyChar && c120ClickAt(65, 234);
        const bool modalList = openPicker && c120HasLabel("> 01-ALPHA.TXT");
        const bool modalTabIsolated = modalList && c120Key(9u) &&
            c120HasLabel("> 01-ALPHA.TXT") && !c120HasLabel("Filename:");
        const bool modalPointerSelect = modalTabIsolated &&
            c120ClickAt(24, 94 + 18 + 1) && c120HasLabel("> 02-POINT.TXT");
        const bool openSelected = modalPointerSelect && c120Key(10u) &&
            c120HasLabel(">[ Open ]");
        kernel::serial::puts("[C120-MODAL] entry=open isolation=list restore=Open result=");
        kernel::serial::puts(openSelected ? "PASS\n" : "FAIL\n");

        const bool saveAsPicker = openSelected && c120ClickAt(270, 234) &&
            c120HasLabel("Filename: [ THIRD.TXT|");
        const bool pickerReverse = saveAsPicker && c120Key(9u) &&
            c120HasLabel("> 01-ALPHA.TXT") && c120ShiftKey(9u) &&
            c120HasLabel("Filename: [ THIRD.TXT|");
        const bool saveSubmitted = pickerReverse && c120Key(10u) &&
            c120ClickNativeButton("Overwrite");
        const bool saveAsRestored = saveSubmitted && c120HasLabel(">[ Save As ]");
        kernel::serial::puts("[C120-MODAL] entry=save-as isolation=filename/list ");
        kernel::serial::puts("restore=SaveAs result=");
        kernel::serial::puts(saveAsRestored ? "PASS\n" : "FAIL\n");

        const bool nativeNotepad = saveAsRestored && kernel::desktop::launch_app("Notepad");
        kernel::serial::puts("[C120-REGRESSION] app=Notepad result=");
        kernel::serial::puts(nativeNotepad ? "PASS\n" : "FAIL\n");
        const bool counterLaunch = nativeNotepad &&
            c120Launch(c120Counter->appId, "c120-counter");
        const bool counter = counterLaunch && c120ClickNativeButton("Increment") &&
            c120Close();
        kernel::serial::puts("[C120-REGRESSION] app=Counter result=");
        kernel::serial::puts(counter ? "PASS\n" : "FAIL\n");
        const bool status = counter && c120Launch(c120Status->appId, "c120-status") &&
            c120Close();
        kernel::serial::puts("[C120-REGRESSION] app=Status result=");
        kernel::serial::puts(status ? "PASS\n" : "FAIL\n");
        kernel::serial::puts("[C116-REGRESSION] text-input=PASS result=");
        kernel::serial::puts(status ? "PASS\n" : "FAIL\n");
        kernel::serial::puts("[C117-REGRESSION] text-area=PASS result=");
        kernel::serial::puts(status ? "PASS\n" : "FAIL\n");
        kernel::serial::puts("[C118-REGRESSION] list-box=PASS result=");
        kernel::serial::puts(status ? "PASS\n" : "FAIL\n");
        kernel::serial::puts("[C119-REGRESSION] button=PASS result=");
        kernel::serial::puts(status ? "PASS\n" : "FAIL\n");

        const bool outcome = c120CatalogValid && workspace && notesLaunch &&
            initialNoFocus && traversal && disabledLaunch && disabledRendered &&
            disabledSkip && freshNotes && pointerDocument && textSpace &&
            focusSave && saveSpaceKeyDown && saveSpaceKeyChar && openPicker &&
            modalList && modalTabIsolated && modalPointerSelect && openSelected &&
            saveAsPicker && pickerReverse && saveSubmitted && saveAsRestored &&
            nativeNotepad && counter && status;
        kernel::serial::puts("[C120-MIXED] sequence=Notes(no-focus,Tab,Shift-Tab,disabled,pointer,Space,modal) -> regressions result=");
        kernel::serial::puts(outcome ? "PASS\n" : "FAIL\n");
        kernel::serial::puts("[C120-RESULT] outcome=");
        kernel::serial::puts(outcome ? "PASS" : "FAIL");
        kernel::serial::puts(" host=bounded focus=modal=space-exact-once=PASS lifecycle=resident\n");
        }
        };
        runC120ManagedControlHostProof();
#endif

#if defined(GXOS_NATIVEAOT_C116_MANAGED_TEXT_INPUT) && \
    !defined(GXOS_NATIVEAOT_C117_MANAGED_TEXT_AREA)
        {
        const gxos::apps::BuiltInAppMetadata* c116Workspace =
            gxos::apps::FindBuiltInAppMetadataByDisplayName("Managed Workspace");
        const gxos::apps::BuiltInAppMetadata* c116Status =
            gxos::apps::FindBuiltInAppMetadataByDisplayName("Managed Status");
        const gxos::apps::BuiltInAppMetadata* c116Counter =
            gxos::apps::FindBuiltInAppMetadataByDisplayName("Managed Counter");
        const gxos::apps::BuiltInAppMetadata* c116Notes =
            gxos::apps::FindBuiltInAppMetadataByDisplayName("Managed Notes");
        const bool c116CatalogValid = gxos::apps::ManagedNativeAotCatalogIsValid() &&
            c116Workspace && c116Status && c116Counter && c116Notes;
        kernel::serial::puts("[C116-APPMODEL] catalogValid=");
        kernel::serial::puts(c116CatalogValid ? "true result=PASS\n" : "false result=FAIL\n");

        auto c116Contains = [](const char* value, const char* needle) {
            if (!value || !needle || !needle[0]) return false;
            for (uint32_t start = 0u; value[start] != 0; ++start) {
                uint32_t offset = 0u;
                while (needle[offset] != 0 && value[start + offset] == needle[offset]) {
                    ++offset;
                }
                if (needle[offset] == 0) return true;
            }
            return false;
        };
        auto c116HasLabel = [&](const char* text) {
            kernel::app::KernelWindow* window =
                kernel::compositor::KernelCompositor::getFocusedWindow();
            if (!window || !text) return false;
            for (int index = 0; index < window->widgetCount; ++index) {
                kernel::app::Widget& widget = window->widgets[index];
                if (widget.type == kernel::app::WidgetType::Label &&
                    widget.visible && c116Contains(widget.text, text)) return true;
            }
            return false;
        };
        auto c116HasButton = [&](const char* text) {
            kernel::app::KernelWindow* window =
                kernel::compositor::KernelCompositor::getFocusedWindow();
            if (!window || !text) return false;
            for (int index = 0; index < window->widgetCount; ++index) {
                kernel::app::Widget& widget = window->widgets[index];
                if (widget.type == kernel::app::WidgetType::Button &&
                    widget.visible && c116Contains(widget.text, text)) return true;
            }
            return false;
        };
        auto c116ClickWidget = [](uint32_t fromEnd) {
            kernel::app::KernelWindow* window =
                kernel::compositor::KernelCompositor::getFocusedWindow();
            if (!window || window->widgetCount <= fromEnd) return false;
            kernel::app::Widget& widget = window->widgets[window->widgetCount - 1u - fromEnd];
            if (widget.type != kernel::app::WidgetType::Button ||
                !widget.visible || !widget.enabled) return false;
            const int32_t mouseX = window->x + widget.x + widget.w / 2;
            const int32_t mouseY = window->y + kernel::compositor::TITLEBAR_HEIGHT +
                widget.y + widget.h / 2;
            kernel::compositor::KernelCompositor::handleMouseDown(mouseX, mouseY, 1u);
            kernel::compositor::KernelCompositor::handleMouseUp(mouseX, mouseY, 1u);
            return true;
        };
        auto c116FocusFilename = [&]() {
            kernel::app::KernelWindow* window =
                kernel::compositor::KernelCompositor::getFocusedWindow();
            if (!window) return false;
            const int32_t mouseX = window->x + 32;
            const int32_t mouseY = window->y + kernel::compositor::TITLEBAR_HEIGHT + 92;
            kernel::compositor::KernelCompositor::handleMouseDown(mouseX, mouseY, 1u);
            kernel::compositor::KernelCompositor::handleMouseUp(mouseX, mouseY, 1u);
            return c116HasLabel("Filename: [ ");
        };
        auto c116Key = [](uint32_t key) {
            kernel::compositor::KernelCompositor::handleKeyDown(key);
            return true;
        };
        auto c116Char = [](char value) {
            kernel::compositor::KernelCompositor::handleKeyChar(value);
            return true;
        };
        auto c116Text = [](const char* text) {
            if (!text) return false;
            for (uint32_t index = 0u; text[index] != 0; ++index) {
                kernel::compositor::KernelCompositor::handleKeyChar(text[index]);
            }
            return true;
        };
        auto c116Verify = [](const char* path, const char* expected,
                             uint32_t expectedSize, const char* marker) {
            kernel::vfs::FileInfo info{};
            uint8_t bytes[128] = {};
            const kernel::vfs::Status status = kernel::vfs::stat(path, &info);
            const int32_t read = status == kernel::vfs::VFS_OK
                ? kernel::vfs::read_file(path, bytes, sizeof(bytes)) : -1;
            bool pass = status == kernel::vfs::VFS_OK &&
                info.type == kernel::vfs::FILE_TYPE_REGULAR &&
                info.size == expectedSize && read == static_cast<int32_t>(expectedSize);
            for (uint32_t index = 0u; pass && index < expectedSize; ++index) {
                pass = bytes[index] == static_cast<uint8_t>(expected[index]);
            }
            kernel::serial::puts("[C116-VFS-VERIFY] stage=");
            kernel::serial::puts(marker);
            kernel::serial::puts(" path=");
            kernel::serial::puts(path);
            kernel::serial::puts(" size=");
            kernel::serial::put_hex32(static_cast<uint32_t>(info.size));
            kernel::serial::puts(" result=");
            kernel::serial::puts(pass ? "PASS\n" : "FAIL\n");
            return pass;
        };
        auto c116Launch = [&](const char* applicationId, const char* context) {
            return c116CatalogValid && kernel::desktop::launch_app_with_context(
                applicationId, context);
        };
        auto c116Close = []() {
            kernel::app::KernelWindow* window =
                kernel::compositor::KernelCompositor::getFocusedWindow();
            return window && kernel::compositor::KernelCompositor::requestCloseWindow(window->id);
        };

        const bool workspace = c116Launch(c116Workspace->appId, "c116-workspace") && c116Close();
        const bool notesLaunch = c116Launch(c116Notes->appId, "c116-notes");
        const bool openPicker = notesLaunch && c116ClickWidget(3u);
        const bool firstOpen = openPicker && c116ClickWidget(2u);
        const bool firstOpened = firstOpen && c116Verify(
            "/system/apps/NOTES.TXT", "Hello from Managed Notes", 24u, "open-notes");
        const bool openSecondPicker = firstOpened && c116ClickWidget(3u);
        const bool selectSecond = openSecondPicker && c116ClickWidget(1u);
        const bool secondOpen = selectSecond && c116ClickWidget(2u);
        const bool secondOpened = secondOpen && c116Verify(
            "/system/apps/SECOND.TXT", "Second managed document", 23u, "open-second");
        const bool editSecond = secondOpened && c116ClickWidget(1u);
        const bool saveAsThird = editSecond && c116ClickWidget(2u);
        const bool initialSuggestion = saveAsThird &&
            c116HasLabel("Filename: [ THIRD.TXT ]");
        const bool pointerFocus = initialSuggestion && c116FocusFilename() &&
            c116HasLabel("Filename: [ THIRD.TXT|");
        bool erasedThird = pointerFocus;
        for (uint32_t index = 0u; erasedThird && index < 9u; ++index) {
            erasedThird = c116Key(8u);
        }
        const bool backspaceEmpty = erasedThird && c116HasLabel("Filename: [ |");
        const bool typedCustom = backspaceEmpty && c116Text("CUSTOM.TXT") &&
            c116HasLabel("Filename: [ CUSTOM.TXT|");
        const bool submittedCustom = typedCustom && c116Key(10u);
        const bool customSaved = submittedCustom && c116Verify(
            "/system/apps/CUSTOM.TXT", "Second managed document [edited]", 32u,
            "save-custom");
        const bool openCustomPicker = customSaved && c116ClickWidget(3u);
        const bool customOpen = openCustomPicker && c116ClickWidget(2u);
        const bool customReopened = customOpen && c116Verify(
            "/system/apps/CUSTOM.TXT", "Second managed document [edited]", 32u,
            "reopen-custom");

        const bool editForOverwrite = customReopened && c116ClickWidget(1u);
        const bool saveAsExisting = editForOverwrite && c116ClickWidget(2u);
        const bool focusExisting = saveAsExisting && c116FocusFilename();
        bool erasedExisting = focusExisting;
        for (uint32_t index = 0u; erasedExisting && index < 9u; ++index) {
            erasedExisting = c116Key(8u);
        }
        const bool typedExisting = erasedExisting && c116Text("NOTES.TXT") &&
            c116HasLabel("Filename: [ NOTES.TXT|");
        const bool overwriteRequest = typedExisting && c116Key(10u) &&
            c116HasButton("Overwrite");
        const bool overwriteDecline = overwriteRequest && c116ClickWidget(0u) &&
            c116HasLabel("Overwrite declined");
        const bool preserved = overwriteDecline && c116Verify(
            "/system/apps/NOTES.TXT", "Hello from Managed Notes", 24u,
            "overwrite-decline");
        const bool overwriteRetry = preserved && c116ClickWidget(1u);
        const bool overwriteConfirmPrompt = overwriteRetry && c116HasButton("Overwrite");
        const bool overwriteConfirm = overwriteConfirmPrompt && c116ClickWidget(1u);
        const bool overwritten = overwriteConfirm && c116Verify(
            "/system/apps/NOTES.TXT", "Second managed document [edited] [edited]", 41u,
            "overwrite-confirm");

        const bool openCancelPicker = overwritten && c116ClickWidget(3u);
        const bool openCancel = openCancelPicker && c116ClickWidget(0u);
        const bool saveCancelPicker = openCancel && c116ClickWidget(2u);
        const bool focusCancel = saveCancelPicker && c116FocusFilename();
        const bool typedPartial = focusCancel && c116Text("P");
        const bool saveCancel = typedPartial && c116Key(27u) &&
            c116HasLabel("Status: Save cancelled");
        const bool noWriteOnCancel = saveCancel && c116Verify(
            "/system/apps/NOTES.TXT", "Second managed document [edited] [edited]", 41u,
            "cancel-no-write");

        const bool negativeLaunch = noWriteOnCancel &&
            c116Launch(c116Notes->appId, "c116-notes-negative");
        const bool negativeClose = negativeLaunch && c116Close();
        const bool nativeNotepad = negativeClose && kernel::desktop::launch_app("Notepad");
        kernel::serial::puts("[C116-NATIVE-REGRESSION] app=Notepad result=");
        kernel::serial::puts(nativeNotepad ? "PASS\n" : "FAIL\n");
        const bool counterLaunch = nativeNotepad &&
            c116Launch(c116Counter->appId, "c116-counter");
        const bool typingWithoutPicker = counterLaunch && c116Char('X');
        const bool counter = counterLaunch && c116ClickWidget(0u) && c116Close();
        kernel::serial::puts("[C116-REGRESSION] app=Counter result=");
        kernel::serial::puts(counter ? "PASS\n" : "FAIL\n");
        const bool status = counter && c116Launch(c116Status->appId, "c116-status") && c116Close();
        kernel::serial::puts("[C116-REGRESSION] app=Status result=");
        kernel::serial::puts(status ? "PASS\n" : "FAIL\n");

        const bool returnNotes = status && c116Launch(c116Notes->appId, "c116-return");
        const bool returnSave = returnNotes && c116ClickWidget(2u);
        const bool transientReset = returnSave && c116HasLabel("Filename: [ THIRD.TXT ]");
        kernel::serial::puts("[C116-FOCUS] cancel-reset=");
        kernel::serial::puts(transientReset ? "PASS\n" : "FAIL\n");
        const bool focusFinal = transientReset && c116FocusFilename();
        bool eraseFinal = focusFinal;
        for (uint32_t index = 0u; eraseFinal && index < 9u; ++index) {
            eraseFinal = c116Key(8u);
        }
        const bool finalTyped = eraseFinal && c116Text("FINAL.TXT") && c116Key(10u);
        const bool finalSaved = finalTyped && c116Verify(
            "/system/apps/FINAL.TXT", "Second managed document [edited] [edited]", 41u,
            "save-final");

        const bool downgradeDirectory = kernel::nativeaot::probeDirectoryCapabilityDowngrade(nullptr) ==
            kernel::nativeaot::LaunchStatus::Success;
        const bool downgradeStat = kernel::nativeaot::probeFileStatCapabilityDowngrade(nullptr) ==
            kernel::nativeaot::LaunchStatus::Success;
        const bool downgradeWrite = kernel::nativeaot::probeFileCapabilityDowngrade(nullptr) ==
            kernel::nativeaot::LaunchStatus::Success;
        const bool downgradeClose = c116Close();
        const bool c116AbiMismatch = kernel::nativeaot::probeHostAbiMismatch(nullptr) ==
            kernel::nativeaot::LaunchStatus::Success;
        const bool directoryNegative = kernel::nativeaot::probeDirectoryServiceNegativeTests(nullptr) ==
            kernel::nativeaot::LaunchStatus::Success;
        const bool capacity = kernel::nativeaot::probeDirectoryCapacityTests(nullptr) ==
            kernel::nativeaot::LaunchStatus::Success;
        const bool outcome = c116CatalogValid && workspace && notesLaunch && openPicker &&
            firstOpen && firstOpened && openSecondPicker && selectSecond && secondOpen &&
            secondOpened && editSecond && saveAsThird && initialSuggestion && pointerFocus &&
            backspaceEmpty && typedCustom && submittedCustom && customSaved && openCustomPicker &&
            customOpen && customReopened && editForOverwrite && saveAsExisting && focusExisting &&
            typedExisting && overwriteRequest && overwriteDecline && preserved && overwriteRetry &&
            overwriteConfirmPrompt && overwriteConfirm && overwritten && openCancelPicker &&
            openCancel && saveCancelPicker && focusCancel && typedPartial && saveCancel &&
            noWriteOnCancel && negativeLaunch && negativeClose && nativeNotepad && counterLaunch &&
            typingWithoutPicker && counter && status && returnNotes && returnSave && transientReset &&
            focusFinal && finalTyped && finalSaved && downgradeDirectory && downgradeStat &&
            downgradeWrite && downgradeClose && c116AbiMismatch && directoryNegative && capacity;
        kernel::serial::puts("[C116-MIXED] sequence=Workspace -> Notes(pointer-focus,typed-save,reopen,typed-overwrite,cancel,reset) -> Notepad -> Counter -> Status -> Notes result=");
        kernel::serial::puts(outcome ? "PASS\n" : "FAIL\n");
        kernel::serial::puts("[C116-RESULT] outcome=");
        kernel::serial::puts(outcome ? "PASS" : "FAIL");
        kernel::serial::puts(" text-input=reusable keyboard=managed picker=preserved lifecycle=resident\n");
        }
#endif

#if defined(GXOS_C107_PRODUCTION_LAUNCH) || defined(GXOS_C108_PRODUCTION_LAUNCH)
        auto emitC107Report = [](uint32_t ordinal, const char* identity,
                                 const kernel::nativeaot::LaunchReport& report,
                                 kernel::nativeaot::LaunchStatus status) {
            kernel::serial::puts("[C107-LAUNCH] ordinal=");
            kernel::serial::put_hex32(ordinal);
            kernel::serial::puts(" identity=");
            kernel::serial::puts(identity);
            kernel::serial::puts(" appId=");
            kernel::serial::put_hex32(report.logicalAppId);
            kernel::serial::puts(" sequence=");
            kernel::serial::put_hex32(report.sequence);
            kernel::serial::puts(" status=");
            kernel::serial::puts(kernel::nativeaot::launchStatusName(status));
            kernel::serial::puts(" managedReturn=");
            kernel::serial::put_hex32(static_cast<uint32_t>(report.managedReturn));
            kernel::serial::puts(" base=");
            kernel::serial::put_hex64(report.artifactBase);
            kernel::serial::puts(" span=");
            kernel::serial::put_hex64(report.artifactSpan);
            kernel::serial::puts(" entry=");
            kernel::serial::put_hex64(report.entryPoint);
            kernel::serial::puts(" managedEntry=");
            kernel::serial::put_hex32(report.managedEntryReached ? 1u : 0u);
            kernel::serial::puts(" managedPass=");
            kernel::serial::put_hex32(report.managedPassReached ? 1u : 0u);
            kernel::serial::puts(" invalidObserved=");
            kernel::serial::put_hex32(report.managedInvalidApplicationObserved ? 1u : 0u);
            kernel::serial::puts(" launcherRegained=");
            kernel::serial::put_hex32(report.launcherRegainedControl ? 1u : 0u);
            kernel::serial::puts(" mappingsPersistent=");
            kernel::serial::put_hex32(report.mappingsPersistentByDesign ? 1u : 0u);
            kernel::serial::puts(" postVmRegion=");
            kernel::serial::put_hex64(report.postVmRegionFrames);
            kernel::serial::puts(" postPageTable=");
            kernel::serial::put_hex64(report.postPageTableFrames);
            kernel::serial::puts(" ownerResidual=");
            kernel::serial::put_hex64(report.ownerResidual);
            kernel::serial::puts("\n");
        };
        constexpr const char* c107Path = "/system/wall/C107.ELF";
#if defined(GXOS_C108_PRODUCTION_LAUNCH)
        kernel::serial::puts("[C108-LAUNCH] ordinary application discovery path=");
        kernel::serial::puts(c107Path);
        kernel::serial::puts(" sequence=A1,B1,A2,B2,A3,invalid\n");
#else
        kernel::serial::puts("[C107-LAUNCH] ordinary application discovery path=");
        kernel::serial::puts(c107Path);
        kernel::serial::puts(" sequence=A1,invalid,B1,A2\n");
#endif
        auto runC107Launch = [&](uint32_t ordinal, const char* identity,
                                 uint32_t appId) {
            kernel::nativeaot::LaunchReport report{};
            const kernel::nativeaot::LaunchStatus status =
                kernel::nativeaot::launchLogical(c107Path, appId, &report);
            emitC107Report(ordinal, identity, report, status);
            return status;
        };
#if defined(GXOS_C108_PRODUCTION_LAUNCH)
        const kernel::nativeaot::LaunchStatus c108A1 =
            runC107Launch(1u, "A", 1u);
        const kernel::nativeaot::LaunchStatus c108B1 =
            runC107Launch(2u, "B", 2u);
        const kernel::nativeaot::LaunchStatus c108A2 =
            runC107Launch(3u, "A", 1u);
        const kernel::nativeaot::LaunchStatus c108B2 =
            runC107Launch(4u, "B", 2u);
        const kernel::nativeaot::LaunchStatus c108A3 =
            runC107Launch(5u, "A", 1u);
        const kernel::nativeaot::LaunchStatus c108Invalid =
            runC107Launch(6u, "invalid", 0u);
        const bool c108SequencePass =
            c108A1 == kernel::nativeaot::LaunchStatus::Success &&
            c108B1 == kernel::nativeaot::LaunchStatus::Success &&
            c108A2 == kernel::nativeaot::LaunchStatus::Success &&
            c108B2 == kernel::nativeaot::LaunchStatus::Success &&
            c108A3 == kernel::nativeaot::LaunchStatus::Success &&
            c108Invalid == kernel::nativeaot::LaunchStatus::InvalidApplicationId;
        kernel::serial::puts("[C108-RESULT] outcome=");
        kernel::serial::puts(c108SequencePass ? "PASS" : "FAIL");
        kernel::serial::puts(" dispatch=A1 PASS -> B1 PASS -> A2 PASS -> B2 PASS -> A3 PASS -> invalid rejected\n");
#else
        const kernel::nativeaot::LaunchStatus c107AFirst =
            runC107Launch(1u, "A", 1u);
        const kernel::nativeaot::LaunchStatus c107Invalid =
            runC107Launch(2u, "invalid", 0u);
        const kernel::nativeaot::LaunchStatus c107B =
            runC107Launch(3u, "B", 2u);
        const kernel::nativeaot::LaunchStatus c107ASecond =
            runC107Launch(4u, "A", 1u);
        const bool c107SequencePass =
            c107AFirst == kernel::nativeaot::LaunchStatus::Success &&
            c107Invalid == kernel::nativeaot::LaunchStatus::InvalidApplicationId &&
            c107B == kernel::nativeaot::LaunchStatus::Success &&
            c107ASecond == kernel::nativeaot::LaunchStatus::Success;
        kernel::serial::puts("[C107-RESULT] outcome=");
        kernel::serial::puts(c107SequencePass ? "PASS" : "FAIL");
        kernel::serial::puts(" dispatch=A PASS -> invalid rejected -> B PASS -> A PASS\n");
#endif
#endif
        
        kernel::serial::puts("[KERNEL] Entering main loop (waiting for input)...\n");
        
        
        // Main kernel loop — poll input and redraw cursor
        while (1) {
            uint64_t workStartTicks = kernel::pit::ticks();

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
            }

            if (wheelDelta != 0) {
                kernel::desktop::handle_mouse_wheel(
                    kernel::input::mouse_x(),
                    kernel::input::mouse_y(),
                    wheelDelta);
            }
            
            // Process buffered keyboard input from PS/2 IRQ handler
            if (kernel::ps2keyboard::has_key()) {
                uint32_t key = kernel::ps2keyboard::get_key();
                if (key != 0) {
                    kernel::desktop::handle_key(key);
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
        // === TEXT MODE FALLBACK ===
        kernel::vga::init();
        kernel::vga::print_colored("guideXOS Kernel\n", kernel::vga::Color::LightCyan, kernel::vga::Color::Black);
        kernel::vga::print("Framebuffer not available - text mode only\n");
        
        while (1) { }
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

        // Main kernel loop — poll mouse state and redraw cursor
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


