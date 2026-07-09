//
// guideXOS Minimal Kernel - Entry Point
//
// ROLE: Bridge between bootloader and guideXOS Server (user-mode)
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/kernel/version.h"
#include "include/kernel/arch.h"
#include "include/kernel/vga.h"
#include "include/kernel/framebuffer.h"
#include "include/kernel/process.h"
#include "include/kernel/desktop.h"
#include "include/kernel/kernel_apps.h"
#include "include/kernel/interrupts.h"
#include "include/kernel/ps2mouse.h"
#include "include/kernel/ps2keyboard.h"
#include "include/kernel/input_manager.h"
#include "include/kernel/pit.h"
#include "include/kernel/serial_debug.h"
#include "include/kernel/desktop_capabilities.h"
#include "include/kernel/app_launch_target_resolver.h"

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

extern "C" void kernel_main(void* boot_environment, uint32_t boot_magic)
{
#if ARCH_HAS_PIC_8259
    // ============================================================
    // x86 / amd64 boot path  â€”  Multiboot (BIOS) or BootInfo (UEFI)
    // ============================================================

    // Initialize serial debug output early
    kernel::serial::init();
    kernel::serial::puts("[KERNEL] guideXOS kernel_main entered\n");

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
            kernel::nic::set_kernel_physical_base(bootinfo->KernelPhysicalBase);
            kernel::virtio::rng::set_kernel_physical_base(bootinfo->KernelPhysicalBase);
        }
    }
    
    // If neither boot method is valid, halt
    if (!is_multiboot && !is_bootinfo) {
        kernel::serial::puts("[KERNEL] ERROR: No valid boot method detected, halting\n");
        while(1) { }
    }
    
    // Initialize framebuffer for graphics mode
    bool has_fb = false;
    
    if (is_bootinfo) {
        has_fb = kernel::framebuffer::init_from_bootinfo(bootinfo);
    } else {
        has_fb = kernel::framebuffer::init(multiboot_info);
    }

    // Diagnostic-only virtio-gpu probe runs regardless of framebuffer
    // handoff success so QEMU display discovery logs are still captured
    // on GOP/BootInfo paths that do not expose a usable framebuffer array.
    kernel::virtio::gpu::init();
    
    if (has_fb) {
        if (is_bootinfo && bootinfo) {
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


