// Hardware Feature Report Generator
//
// Generates a summary report of enabled/available hardware features
// during kernel boot. Provides status information for debugging
// and verification of driver initialization.
//
// Features tracked:
//   - CPU architecture and capabilities
//   - Memory configuration
//   - Storage drivers and devices
//   - Filesystem support
//   - Network interfaces
//   - Graphics/display
//   - Input devices
//   - VirtIO/virtualization
//   - Interrupt controllers
//
// Copyright (c) 2026 guideXOS Server
//

#ifndef KERNEL_FEATURE_REPORT_H
#define KERNEL_FEATURE_REPORT_H

#include "kernel/types.h"

namespace kernel {
namespace feature_report {

// ================================================================
// Feature Status
// ================================================================

enum FeatureStatus : uint8_t {
    STATUS_UNKNOWN     = 0,  // Not checked yet
    STATUS_NOT_PRESENT = 1,  // Hardware not present
    STATUS_PRESENT     = 2,  // Hardware present but not initialized
    STATUS_ACTIVE      = 3,  // Fully initialized and working
    STATUS_ERROR       = 4,  // Initialization failed
    STATUS_DISABLED    = 5,  // Explicitly disabled
};

// ================================================================
// Feature Categories
// ================================================================

enum FeatureCategory : uint8_t {
    CAT_CPU        = 0,
    CAT_MEMORY     = 1,
    CAT_STORAGE    = 2,
    CAT_FILESYSTEM = 3,
    CAT_NETWORK    = 4,
    CAT_GRAPHICS   = 5,
    CAT_INPUT      = 6,
    CAT_VIRTIO     = 7,
    CAT_INTERRUPT  = 8,
    CAT_OTHER      = 9,
    
    CAT_COUNT      = 10,
};

// ================================================================
// Individual Feature IDs
// ================================================================

// CPU features
enum CpuFeature : uint16_t {
    CPU_X86        = 0x0100,
    CPU_AMD64      = 0x0101,
    CPU_ARM32      = 0x0102,
    CPU_ARM64      = 0x0103,
    CPU_RISCV32    = 0x0104,
    CPU_RISCV64    = 0x0105,
    CPU_MIPS32     = 0x0106,
    CPU_MIPS64     = 0x0107,
    CPU_PPC64      = 0x0108,
    CPU_SPARC      = 0x0109,
    CPU_IA64       = 0x010A,
    CPU_LOONGARCH  = 0x010B,
};

// Storage features
enum StorageFeature : uint16_t {
    STORAGE_ATA_PIO    = 0x0200,
    STORAGE_AHCI       = 0x0201,
    STORAGE_NVME       = 0x0202,
    STORAGE_USB_MASS   = 0x0203,
    STORAGE_VIRTIO_BLK = 0x0204,
    STORAGE_RAMDISK    = 0x0205,
    STORAGE_SCSI       = 0x0206,
};

// Filesystem features
enum FilesystemFeature : uint16_t {
    FS_FAT32    = 0x0300,
    FS_EXFAT    = 0x0301,
    FS_EXT2     = 0x0302,
    FS_EXT4     = 0x0303,
    FS_NTFS     = 0x0304,
    FS_XFS      = 0x0305,
    FS_UFS      = 0x0306,
    FS_ISO9660  = 0x0307,
    FS_TMPFS    = 0x0308,
};

// Network features
enum NetworkFeature : uint16_t {
    NET_E1000       = 0x0400,
    NET_VIRTIO_NET  = 0x0401,
    NET_RTL8139     = 0x0402,
    NET_USB_CDC     = 0x0403,
    NET_IPV4        = 0x0410,
    NET_IPV6        = 0x0411,
    NET_TCP         = 0x0412,
    NET_UDP         = 0x0413,
    NET_DHCP        = 0x0414,
    NET_DNS         = 0x0415,
};

// Graphics features
enum GraphicsFeature : uint16_t {
    GFX_VGA_TEXT    = 0x0500,
    GFX_VBE         = 0x0501,
    GFX_GOP         = 0x0502,
    GFX_VIRTIO_GPU  = 0x0503,
    GFX_RAMFB       = 0x0504,
    GFX_PL111       = 0x0505,
    GFX_SUN_FB      = 0x0506,
};

// Input features
enum InputFeature : uint16_t {
    INPUT_PS2_KB    = 0x0600,
    INPUT_PS2_MOUSE = 0x0601,
    INPUT_USB_HID   = 0x0602,
    INPUT_VIRTIO    = 0x0603,
    INPUT_SERIAL    = 0x0604,
};

// VirtIO features
enum VirtioFeature : uint16_t {
    VIRTIO_BLK      = 0x0700,
    VIRTIO_NET      = 0x0701,
    VIRTIO_GPU      = 0x0702,
    VIRTIO_INPUT    = 0x0703,
    VIRTIO_CONSOLE  = 0x0704,
    VIRTIO_RNG      = 0x0705,
    VIRTIO_9P       = 0x0706,
};

// Interrupt features
enum InterruptFeature : uint16_t {
    INT_PIC_8259    = 0x0800,
    INT_APIC        = 0x0801,
    INT_IOAPIC      = 0x0802,
    INT_MSI         = 0x0803,
    INT_MSIX        = 0x0804,
    INT_GIC         = 0x0805,
    INT_PLIC        = 0x0806,
};

// ================================================================
// Feature Entry
// ================================================================

struct FeatureEntry {
    uint16_t      id;           // Feature ID
    FeatureStatus status;       // Current status
    FeatureCategory category;   // Category
    const char*   name;         // Human-readable name
    const char*   details;      // Additional details (optional)
};

// ================================================================
// Report Statistics
// ================================================================

struct ReportStats {
    uint32_t totalFeatures;
    uint32_t activeFeatures;
    uint32_t presentFeatures;
    uint32_t errorFeatures;
    uint32_t disabledFeatures;
    uint32_t notPresentFeatures;
    
    // Per-category counts
    uint32_t categoryActive[CAT_COUNT];
    uint32_t categoryTotal[CAT_COUNT];
};

// ================================================================
// Public API
// ================================================================

// Initialize feature reporting subsystem
void init();

// ================================================================
// Feature Registration
// ================================================================

// Register a feature with its status
void register_feature(uint16_t id, FeatureStatus status, 
                      FeatureCategory category, const char* name);

// Register a feature with additional details
void register_feature_details(uint16_t id, FeatureStatus status,
                              FeatureCategory category, const char* name,
                              const char* details);

// Update status of a registered feature
void update_status(uint16_t id, FeatureStatus status);

// Update status with details
void update_status_details(uint16_t id, FeatureStatus status, const char* details);

// ================================================================
// Feature Queries
// ================================================================

// Get status of a feature
FeatureStatus get_status(uint16_t id);

// Check if feature is active
bool is_active(uint16_t id);

// Check if feature is present (active or just detected)
bool is_present(uint16_t id);

// Get feature entry by ID
const FeatureEntry* get_feature(uint16_t id);

// Get all features in a category
int get_features_by_category(FeatureCategory category, 
                             const FeatureEntry** entriesOut, int maxCount);

// Get report statistics
void get_stats(ReportStats* statsOut);

// ================================================================
// Report Generation
// ================================================================

// Generate full report to serial console
void print_report();

// Generate summary report (compact)
void print_summary();

// Generate report for a specific category
void print_category_report(FeatureCategory category);

// ================================================================
// Helpers for Common Initialization Patterns
// ================================================================

// Mark feature as being initialized
void begin_init(uint16_t id);

// Mark feature initialization complete (success)
void complete_init(uint16_t id);

// Mark feature initialization complete with details
void complete_init_details(uint16_t id, const char* details);

// Mark feature initialization failed
void fail_init(uint16_t id, const char* reason);

// ================================================================
// Predefined Feature Registration Macros
// ================================================================

// Helper to register standard features
void register_standard_features();

// ================================================================
// Status Strings
// ================================================================

// Get status string
const char* status_string(FeatureStatus status);

// Get category name
const char* category_name(FeatureCategory category);

} // namespace feature_report
} // namespace kernel

#endif // KERNEL_FEATURE_REPORT_H
