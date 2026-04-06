// Hardware Feature Report Generator Implementation
//
// Tracks and reports hardware feature initialization status.
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/kernel/feature_report.h"
#include "include/kernel/serial_debug.h"

namespace kernel {
namespace feature_report {

// ================================================================
// Internal State
// ================================================================

static const int MAX_FEATURES = 128;
static FeatureEntry s_features[MAX_FEATURES];
static int s_featureCount = 0;
static bool s_initialized = false;

// ================================================================
// Helper Functions
// ================================================================

static void memzero(void* dst, size_t len)
{
    uint8_t* p = static_cast<uint8_t*>(dst);
    for (size_t i = 0; i < len; ++i) {
        p[i] = 0;
    }
}

static int find_feature_index(uint16_t id)
{
    for (int i = 0; i < s_featureCount; ++i) {
        if (s_features[i].id == id) {
            return i;
        }
    }
    return -1;
}

// ================================================================
// Initialization
// ================================================================

void init()
{
    if (s_initialized) return;
    
    memzero(s_features, sizeof(s_features));
    s_featureCount = 0;
    s_initialized = true;
    
    // Register standard features
    register_standard_features();
}

// ================================================================
// Feature Registration
// ================================================================

void register_feature(uint16_t id, FeatureStatus status, 
                      FeatureCategory category, const char* name)
{
    register_feature_details(id, status, category, name, nullptr);
}

void register_feature_details(uint16_t id, FeatureStatus status,
                              FeatureCategory category, const char* name,
                              const char* details)
{
    if (!s_initialized) init();
    
    // Check if already registered
    int idx = find_feature_index(id);
    if (idx >= 0) {
        // Update existing
        s_features[idx].status = status;
        s_features[idx].details = details;
        return;
    }
    
    // Add new
    if (s_featureCount >= MAX_FEATURES) {
        kernel::serial::puts("[FEATURES] Max features reached\n");
        return;
    }
    
    s_features[s_featureCount].id = id;
    s_features[s_featureCount].status = status;
    s_features[s_featureCount].category = category;
    s_features[s_featureCount].name = name;
    s_features[s_featureCount].details = details;
    ++s_featureCount;
}

void update_status(uint16_t id, FeatureStatus status)
{
    int idx = find_feature_index(id);
    if (idx >= 0) {
        s_features[idx].status = status;
    }
}

void update_status_details(uint16_t id, FeatureStatus status, const char* details)
{
    int idx = find_feature_index(id);
    if (idx >= 0) {
        s_features[idx].status = status;
        s_features[idx].details = details;
    }
}

// ================================================================
// Feature Queries
// ================================================================

FeatureStatus get_status(uint16_t id)
{
    int idx = find_feature_index(id);
    if (idx >= 0) {
        return s_features[idx].status;
    }
    return STATUS_UNKNOWN;
}

bool is_active(uint16_t id)
{
    return get_status(id) == STATUS_ACTIVE;
}

bool is_present(uint16_t id)
{
    FeatureStatus status = get_status(id);
    return status == STATUS_ACTIVE || status == STATUS_PRESENT;
}

const FeatureEntry* get_feature(uint16_t id)
{
    int idx = find_feature_index(id);
    if (idx >= 0) {
        return &s_features[idx];
    }
    return nullptr;
}

int get_features_by_category(FeatureCategory category, 
                             const FeatureEntry** entriesOut, int maxCount)
{
    int count = 0;
    for (int i = 0; i < s_featureCount && count < maxCount; ++i) {
        if (s_features[i].category == category) {
            entriesOut[count++] = &s_features[i];
        }
    }
    return count;
}

void get_stats(ReportStats* statsOut)
{
    if (statsOut == nullptr) return;
    
    memzero(statsOut, sizeof(ReportStats));
    statsOut->totalFeatures = s_featureCount;
    
    for (int i = 0; i < s_featureCount; ++i) {
        FeatureCategory cat = s_features[i].category;
        if (cat < CAT_COUNT) {
            ++statsOut->categoryTotal[cat];
        }
        
        switch (s_features[i].status) {
            case STATUS_ACTIVE:
                ++statsOut->activeFeatures;
                if (cat < CAT_COUNT) ++statsOut->categoryActive[cat];
                break;
            case STATUS_PRESENT:
                ++statsOut->presentFeatures;
                break;
            case STATUS_ERROR:
                ++statsOut->errorFeatures;
                break;
            case STATUS_DISABLED:
                ++statsOut->disabledFeatures;
                break;
            case STATUS_NOT_PRESENT:
                ++statsOut->notPresentFeatures;
                break;
            default:
                break;
        }
    }
}

// ================================================================
// Report Generation
// ================================================================

static void print_status_char(FeatureStatus status)
{
    switch (status) {
        case STATUS_ACTIVE:      kernel::serial::puts("[OK]   "); break;
        case STATUS_PRESENT:     kernel::serial::puts("[--]   "); break;
        case STATUS_ERROR:       kernel::serial::puts("[FAIL] "); break;
        case STATUS_DISABLED:    kernel::serial::puts("[OFF]  "); break;
        case STATUS_NOT_PRESENT: kernel::serial::puts("[N/A]  "); break;
        default:                 kernel::serial::puts("[???]  "); break;
    }
}

void print_report()
{
    kernel::serial::puts("\n");
    kernel::serial::puts("============================================================\n");
    kernel::serial::puts("           guideXOS Hardware Feature Report\n");
    kernel::serial::puts("============================================================\n\n");
    
    // Print by category
    for (int cat = 0; cat < CAT_COUNT; ++cat) {
        bool headerPrinted = false;
        
        for (int i = 0; i < s_featureCount; ++i) {
            if (s_features[i].category == static_cast<FeatureCategory>(cat)) {
                if (!headerPrinted) {
                    kernel::serial::puts("--- ");
                    kernel::serial::puts(category_name(static_cast<FeatureCategory>(cat)));
                    kernel::serial::puts(" ---\n");
                    headerPrinted = true;
                }
                
                print_status_char(s_features[i].status);
                kernel::serial::puts(s_features[i].name);
                
                if (s_features[i].details != nullptr) {
                    kernel::serial::puts(" (");
                    kernel::serial::puts(s_features[i].details);
                    kernel::serial::puts(")");
                }
                
                kernel::serial::puts("\n");
            }
        }
        
        if (headerPrinted) {
            kernel::serial::puts("\n");
        }
    }
    
    // Print summary
    ReportStats stats;
    get_stats(&stats);
    
    kernel::serial::puts("--- Summary ---\n");
    kernel::serial::puts("Total features:  ");
    kernel::serial::put_hex32(stats.totalFeatures);
    kernel::serial::puts("\nActive:          ");
    kernel::serial::put_hex32(stats.activeFeatures);
    kernel::serial::puts("\nPresent:         ");
    kernel::serial::put_hex32(stats.presentFeatures);
    kernel::serial::puts("\nErrors:          ");
    kernel::serial::put_hex32(stats.errorFeatures);
    kernel::serial::puts("\nDisabled:        ");
    kernel::serial::put_hex32(stats.disabledFeatures);
    kernel::serial::puts("\nNot present:     ");
    kernel::serial::put_hex32(stats.notPresentFeatures);
    kernel::serial::puts("\n\n");
    
    kernel::serial::puts("============================================================\n\n");
}

void print_summary()
{
    ReportStats stats;
    get_stats(&stats);
    
    kernel::serial::puts("[FEATURES] ");
    kernel::serial::put_hex32(stats.activeFeatures);
    kernel::serial::puts(" active / ");
    kernel::serial::put_hex32(stats.totalFeatures);
    kernel::serial::puts(" total");
    
    if (stats.errorFeatures > 0) {
        kernel::serial::puts(", ");
        kernel::serial::put_hex32(stats.errorFeatures);
        kernel::serial::puts(" errors");
    }
    
    kernel::serial::puts("\n");
}

void print_category_report(FeatureCategory category)
{
    kernel::serial::puts("[FEATURES] ");
    kernel::serial::puts(category_name(category));
    kernel::serial::puts(":\n");
    
    for (int i = 0; i < s_featureCount; ++i) {
        if (s_features[i].category == category) {
            kernel::serial::puts("  ");
            print_status_char(s_features[i].status);
            kernel::serial::puts(s_features[i].name);
            kernel::serial::puts("\n");
        }
    }
}

// ================================================================
// Initialization Helpers
// ================================================================

void begin_init(uint16_t id)
{
    update_status(id, STATUS_PRESENT);
}

void complete_init(uint16_t id)
{
    update_status(id, STATUS_ACTIVE);
}

void complete_init_details(uint16_t id, const char* details)
{
    update_status_details(id, STATUS_ACTIVE, details);
}

void fail_init(uint16_t id, const char* reason)
{
    update_status_details(id, STATUS_ERROR, reason);
}

// ================================================================
// Standard Feature Registration
// ================================================================

void register_standard_features()
{
    // CPU architectures (detect current at runtime)
#if defined(__x86_64__) || defined(_M_X64)
    register_feature(CPU_AMD64, STATUS_ACTIVE, CAT_CPU, "AMD64 (x86-64)");
#elif defined(__i386__) || defined(_M_IX86)
    register_feature(CPU_X86, STATUS_ACTIVE, CAT_CPU, "x86 (32-bit)");
#elif defined(__aarch64__)
    register_feature(CPU_ARM64, STATUS_ACTIVE, CAT_CPU, "ARM64 (AArch64)");
#elif defined(__arm__)
    register_feature(CPU_ARM32, STATUS_ACTIVE, CAT_CPU, "ARM (32-bit)");
#elif defined(__riscv) && (__riscv_xlen == 64)
    register_feature(CPU_RISCV64, STATUS_ACTIVE, CAT_CPU, "RISC-V 64");
#elif defined(__riscv)
    register_feature(CPU_RISCV32, STATUS_ACTIVE, CAT_CPU, "RISC-V 32");
#elif defined(__mips64)
    register_feature(CPU_MIPS64, STATUS_ACTIVE, CAT_CPU, "MIPS64");
#elif defined(__mips__)
    register_feature(CPU_MIPS32, STATUS_ACTIVE, CAT_CPU, "MIPS32");
#elif defined(__powerpc64__)
    register_feature(CPU_PPC64, STATUS_ACTIVE, CAT_CPU, "PowerPC 64");
#elif defined(__sparc__)
    register_feature(CPU_SPARC, STATUS_ACTIVE, CAT_CPU, "SPARC");
#elif defined(__ia64__)
    register_feature(CPU_IA64, STATUS_ACTIVE, CAT_CPU, "IA-64 (Itanium)");
#elif defined(__loongarch64)
    register_feature(CPU_LOONGARCH, STATUS_ACTIVE, CAT_CPU, "LoongArch 64");
#else
    register_feature(0x01FF, STATUS_UNKNOWN, CAT_CPU, "Unknown CPU");
#endif

    // Storage drivers (not present until init called)
    register_feature(STORAGE_ATA_PIO, STATUS_NOT_PRESENT, CAT_STORAGE, "ATA (PIO mode)");
    register_feature(STORAGE_AHCI, STATUS_NOT_PRESENT, CAT_STORAGE, "AHCI/SATA");
    register_feature(STORAGE_NVME, STATUS_NOT_PRESENT, CAT_STORAGE, "NVMe");
    register_feature(STORAGE_USB_MASS, STATUS_NOT_PRESENT, CAT_STORAGE, "USB Mass Storage");
    register_feature(STORAGE_VIRTIO_BLK, STATUS_NOT_PRESENT, CAT_STORAGE, "VirtIO Block");
    register_feature(STORAGE_RAMDISK, STATUS_NOT_PRESENT, CAT_STORAGE, "RAM Disk");
    
    // Filesystems
    register_feature(FS_FAT32, STATUS_NOT_PRESENT, CAT_FILESYSTEM, "FAT32");
    register_feature(FS_EXFAT, STATUS_NOT_PRESENT, CAT_FILESYSTEM, "exFAT");
    register_feature(FS_EXT2, STATUS_NOT_PRESENT, CAT_FILESYSTEM, "ext2");
    register_feature(FS_EXT4, STATUS_NOT_PRESENT, CAT_FILESYSTEM, "ext4");
    register_feature(FS_NTFS, STATUS_NOT_PRESENT, CAT_FILESYSTEM, "NTFS");
    register_feature(FS_XFS, STATUS_NOT_PRESENT, CAT_FILESYSTEM, "XFS");
    register_feature(FS_UFS, STATUS_NOT_PRESENT, CAT_FILESYSTEM, "UFS");
    
    // Network
    register_feature(NET_E1000, STATUS_NOT_PRESENT, CAT_NETWORK, "Intel E1000");
    register_feature(NET_VIRTIO_NET, STATUS_NOT_PRESENT, CAT_NETWORK, "VirtIO Network");
    register_feature(NET_IPV4, STATUS_NOT_PRESENT, CAT_NETWORK, "IPv4 Stack");
    register_feature(NET_IPV6, STATUS_NOT_PRESENT, CAT_NETWORK, "IPv6 Stack");
    register_feature(NET_TCP, STATUS_NOT_PRESENT, CAT_NETWORK, "TCP Protocol");
    register_feature(NET_UDP, STATUS_NOT_PRESENT, CAT_NETWORK, "UDP Protocol");
    register_feature(NET_DHCP, STATUS_NOT_PRESENT, CAT_NETWORK, "DHCP Client");
    register_feature(NET_DNS, STATUS_NOT_PRESENT, CAT_NETWORK, "DNS Client");
    
    // Graphics
    register_feature(GFX_VGA_TEXT, STATUS_NOT_PRESENT, CAT_GRAPHICS, "VGA Text Mode");
    register_feature(GFX_VBE, STATUS_NOT_PRESENT, CAT_GRAPHICS, "VESA/VBE");
    register_feature(GFX_GOP, STATUS_NOT_PRESENT, CAT_GRAPHICS, "UEFI GOP");
    register_feature(GFX_VIRTIO_GPU, STATUS_NOT_PRESENT, CAT_GRAPHICS, "VirtIO GPU");
    register_feature(GFX_RAMFB, STATUS_NOT_PRESENT, CAT_GRAPHICS, "QEMU ramfb");
    
    // Input
    register_feature(INPUT_PS2_KB, STATUS_NOT_PRESENT, CAT_INPUT, "PS/2 Keyboard");
    register_feature(INPUT_PS2_MOUSE, STATUS_NOT_PRESENT, CAT_INPUT, "PS/2 Mouse");
    register_feature(INPUT_USB_HID, STATUS_NOT_PRESENT, CAT_INPUT, "USB HID");
    register_feature(INPUT_VIRTIO, STATUS_NOT_PRESENT, CAT_INPUT, "VirtIO Input");
    register_feature(INPUT_SERIAL, STATUS_NOT_PRESENT, CAT_INPUT, "Serial Console");
    
    // VirtIO
    register_feature(VIRTIO_BLK, STATUS_NOT_PRESENT, CAT_VIRTIO, "VirtIO Block");
    register_feature(VIRTIO_NET, STATUS_NOT_PRESENT, CAT_VIRTIO, "VirtIO Network");
    register_feature(VIRTIO_GPU, STATUS_NOT_PRESENT, CAT_VIRTIO, "VirtIO GPU");
    register_feature(VIRTIO_INPUT, STATUS_NOT_PRESENT, CAT_VIRTIO, "VirtIO Input");
    register_feature(VIRTIO_CONSOLE, STATUS_NOT_PRESENT, CAT_VIRTIO, "VirtIO Console");
    
    // Interrupts
    register_feature(INT_PIC_8259, STATUS_NOT_PRESENT, CAT_INTERRUPT, "8259 PIC");
    register_feature(INT_APIC, STATUS_NOT_PRESENT, CAT_INTERRUPT, "Local APIC");
    register_feature(INT_IOAPIC, STATUS_NOT_PRESENT, CAT_INTERRUPT, "I/O APIC");
    register_feature(INT_MSI, STATUS_NOT_PRESENT, CAT_INTERRUPT, "MSI");
    register_feature(INT_MSIX, STATUS_NOT_PRESENT, CAT_INTERRUPT, "MSI-X");
    register_feature(INT_GIC, STATUS_NOT_PRESENT, CAT_INTERRUPT, "ARM GIC");
    register_feature(INT_PLIC, STATUS_NOT_PRESENT, CAT_INTERRUPT, "RISC-V PLIC");
}

// ================================================================
// Status Strings
// ================================================================

const char* status_string(FeatureStatus status)
{
    switch (status) {
        case STATUS_UNKNOWN:     return "Unknown";
        case STATUS_NOT_PRESENT: return "Not Present";
        case STATUS_PRESENT:     return "Present";
        case STATUS_ACTIVE:      return "Active";
        case STATUS_ERROR:       return "Error";
        case STATUS_DISABLED:    return "Disabled";
        default:                 return "Invalid";
    }
}

const char* category_name(FeatureCategory category)
{
    switch (category) {
        case CAT_CPU:        return "CPU";
        case CAT_MEMORY:     return "Memory";
        case CAT_STORAGE:    return "Storage";
        case CAT_FILESYSTEM: return "Filesystem";
        case CAT_NETWORK:    return "Network";
        case CAT_GRAPHICS:   return "Graphics";
        case CAT_INPUT:      return "Input";
        case CAT_VIRTIO:     return "VirtIO";
        case CAT_INTERRUPT:  return "Interrupts";
        case CAT_OTHER:      return "Other";
        default:             return "Unknown";
    }
}

} // namespace feature_report
} // namespace kernel
