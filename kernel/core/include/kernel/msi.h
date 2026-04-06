// MSI/MSI-X Interrupt Support
//
// Provides Message Signaled Interrupts (MSI) and Extended MSI (MSI-X)
// support for PCIe devices. These interrupt mechanisms are more efficient
// than legacy pin-based interrupts and are required for many modern devices.
//
// MSI Benefits:
//   - Eliminates interrupt routing issues
//   - Supports multiple interrupt vectors per device
//   - Enables better interrupt affinity control
//   - Required by many VirtIO, NVMe, and network devices
//
// Reference: PCI Local Bus Specification 3.0, PCI Express Base Specification
//
// Copyright (c) 2026 guideXOS Server
//

#ifndef KERNEL_MSI_H
#define KERNEL_MSI_H

#include "kernel/types.h"

namespace kernel {
namespace msi {

// ================================================================
// MSI Constants
// ================================================================

// PCI Capability IDs
static const uint8_t PCI_CAP_ID_MSI   = 0x05;
static const uint8_t PCI_CAP_ID_MSIX  = 0x11;

// MSI Control Register bits
static const uint16_t MSI_CTRL_ENABLE        = 0x0001;  // MSI enable
static const uint16_t MSI_CTRL_MULTI_CAP     = 0x000E;  // Multiple message capable (shift 1)
static const uint16_t MSI_CTRL_MULTI_ENABLE  = 0x0070;  // Multiple message enable (shift 4)
static const uint16_t MSI_CTRL_64BIT         = 0x0080;  // 64-bit address capable
static const uint16_t MSI_CTRL_PER_VECTOR    = 0x0100;  // Per-vector masking capable

// MSI-X Control Register bits
static const uint16_t MSIX_CTRL_TABLE_SIZE   = 0x07FF;  // Table size mask
static const uint16_t MSIX_CTRL_FUNC_MASK    = 0x4000;  // Function mask
static const uint16_t MSIX_CTRL_ENABLE       = 0x8000;  // MSI-X enable

// MSI-X Table Entry bits
static const uint32_t MSIX_ENTRY_CTRL_MASKBIT = 0x00000001;

// MSI/MSI-X Address Format (for x86/AMD64)
// Address: 0xFEE00000 | (dest << 12) | (redirect hint << 3) | (dest mode << 2)
static const uint64_t MSI_ADDR_BASE          = 0xFEE00000ULL;
static const uint64_t MSI_ADDR_DEST_MASK     = 0x000FF000ULL;
static const uint64_t MSI_ADDR_DEST_SHIFT    = 12;
static const uint64_t MSI_ADDR_REDIR_HINT    = 0x00000008ULL;  // Use lowest priority
static const uint64_t MSI_ADDR_DEST_MODE     = 0x00000004ULL;  // Logical destination

// MSI Data Format
// Data: vector | (delivery mode << 8) | (level << 14) | (trigger mode << 15)
static const uint32_t MSI_DATA_VECTOR_MASK   = 0x000000FF;
static const uint32_t MSI_DATA_DELIVERY_MASK = 0x00000700;
static const uint32_t MSI_DATA_DELIVERY_SHIFT = 8;
static const uint32_t MSI_DATA_LEVEL_ASSERT  = 0x00004000;
static const uint32_t MSI_DATA_TRIGGER_EDGE  = 0x00000000;
static const uint32_t MSI_DATA_TRIGGER_LEVEL = 0x00008000;

// Delivery Modes
static const uint32_t MSI_DELIVERY_FIXED     = 0;  // Fixed delivery
static const uint32_t MSI_DELIVERY_LOWEST    = 1;  // Lowest priority
static const uint32_t MSI_DELIVERY_SMI       = 2;  // SMI
static const uint32_t MSI_DELIVERY_NMI       = 4;  // NMI
static const uint32_t MSI_DELIVERY_INIT      = 5;  // INIT
static const uint32_t MSI_DELIVERY_EXTINT    = 7;  // ExtINT

// Maximum vectors
static const int MSI_MAX_VECTORS  = 32;   // MSI supports up to 32
static const int MSIX_MAX_VECTORS = 2048; // MSI-X supports up to 2048

// ================================================================
// MSI Status Codes
// ================================================================

enum MsiStatus : int8_t {
    MSI_OK              =  0,
    MSI_ERR_NOT_FOUND   = -1,   // No MSI/MSI-X capability
    MSI_ERR_INVALID     = -2,   // Invalid parameter
    MSI_ERR_NO_VECTORS  = -3,   // No vectors available
    MSI_ERR_UNSUPPORTED = -4,   // Operation not supported
    MSI_ERR_ALREADY     = -5,   // Already enabled
    MSI_ERR_DISABLED    = -6,   // MSI not enabled
};

// ================================================================
// MSI Capability Structures
// ================================================================

#if defined(__GNUC__) || defined(__clang__)
#define MSI_PACKED __attribute__((packed))
#else
#pragma pack(push, 1)
#define MSI_PACKED
#endif

// MSI Capability (in PCI config space)
struct MsiCapability {
    uint8_t  capId;              // Capability ID (0x05)
    uint8_t  nextPtr;            // Next capability pointer
    uint16_t control;            // Message control
    uint32_t addressLo;          // Message address (low 32 bits)
    uint32_t addressHi;          // Message address (high 32 bits, if 64-bit)
    uint16_t data;               // Message data
    uint16_t reserved;           // Reserved (if 64-bit)
    uint32_t maskBits;           // Mask bits (if per-vector masking)
    uint32_t pendingBits;        // Pending bits (if per-vector masking)
} MSI_PACKED;

// MSI-X Capability (in PCI config space)
struct MsixCapability {
    uint8_t  capId;              // Capability ID (0x11)
    uint8_t  nextPtr;            // Next capability pointer
    uint16_t control;            // Message control
    uint32_t tableOffset;        // Table offset and BAR indicator
    uint32_t pbaOffset;          // PBA offset and BAR indicator
} MSI_PACKED;

// MSI-X Table Entry (in BAR memory)
struct MsixTableEntry {
    uint32_t addrLo;             // Message address (low)
    uint32_t addrHi;             // Message address (high)
    uint32_t data;               // Message data
    uint32_t control;            // Vector control
} MSI_PACKED;

#if !defined(__GNUC__) && !defined(__clang__)
#pragma pack(pop)
#endif

#undef MSI_PACKED

// ================================================================
// Device MSI Information
// ================================================================

struct MsiInfo {
    // PCI location
    uint8_t  bus;
    uint8_t  device;
    uint8_t  function;
    
    // Capability offsets in config space
    uint8_t  msiCapOffset;       // 0 if not present
    uint8_t  msixCapOffset;      // 0 if not present
    
    // MSI capabilities
    bool     hasMsi;
    bool     has64Bit;
    bool     hasPerVectorMask;
    uint8_t  msiMaxVectors;      // Max vectors supported (1, 2, 4, 8, 16, or 32)
    
    // MSI-X capabilities
    bool     hasMsix;
    uint16_t msixTableSize;      // Number of MSI-X table entries
    uint8_t  msixTableBar;       // BAR containing MSI-X table
    uint32_t msixTableOffset;    // Offset within BAR
    uint8_t  msixPbaBar;         // BAR containing PBA
    uint32_t msixPbaOffset;      // Offset within BAR
    
    // Current state
    bool     msiEnabled;
    bool     msixEnabled;
    uint8_t  allocatedVectors;   // Number of allocated vectors
    uint8_t  baseVector;         // First vector number
};

// ================================================================
// Vector Allocation
// ================================================================

struct MsiVector {
    uint32_t vector;             // CPU vector number
    uint32_t cpuId;              // Target CPU ID
    bool     masked;             // Is vector masked
    void*    context;            // Driver context (for handler)
};

// Interrupt handler callback
typedef void (*MsiHandler)(uint32_t vector, void* context);

// ================================================================
// Public API
// ================================================================

// Initialize MSI subsystem
void init();

// ================================================================
// Device Detection
// ================================================================

// Probe a PCI device for MSI/MSI-X capability
// Fills in MsiInfo with capability details
MsiStatus probe_device(uint8_t bus, uint8_t device, uint8_t function, MsiInfo* infoOut);

// Check if device has MSI capability
bool has_msi(uint8_t bus, uint8_t device, uint8_t function);

// Check if device has MSI-X capability
bool has_msix(uint8_t bus, uint8_t device, uint8_t function);

// ================================================================
// MSI Configuration
// ================================================================

// Enable MSI for a device
// numVectors: requested number of vectors (1, 2, 4, 8, 16, or 32)
// baseVector: starting vector number to use
MsiStatus enable_msi(MsiInfo* info, uint8_t numVectors, uint8_t baseVector);

// Disable MSI for a device
MsiStatus disable_msi(MsiInfo* info);

// Configure MSI address and data for a vector
MsiStatus configure_msi_vector(MsiInfo* info, uint8_t index, uint32_t vector, uint32_t cpuId);

// Mask/unmask MSI vector (if per-vector masking supported)
MsiStatus mask_msi_vector(MsiInfo* info, uint8_t index, bool mask);

// ================================================================
// MSI-X Configuration
// ================================================================

// Enable MSI-X for a device
MsiStatus enable_msix(MsiInfo* info);

// Disable MSI-X for a device
MsiStatus disable_msix(MsiInfo* info);

// Configure MSI-X table entry
MsiStatus configure_msix_vector(MsiInfo* info, uint16_t index, 
                                uint32_t vector, uint32_t cpuId);

// Mask/unmask MSI-X vector
MsiStatus mask_msix_vector(MsiInfo* info, uint16_t index, bool mask);

// Mask all MSI-X vectors (function mask)
MsiStatus mask_all_msix(MsiInfo* info, bool mask);

// Read MSI-X pending bit
bool is_msix_pending(MsiInfo* info, uint16_t index);

// ================================================================
// Vector Allocation
// ================================================================

// Allocate interrupt vectors for a device
// Tries MSI-X first, falls back to MSI
// numVectors: requested number (may allocate fewer)
// Returns actual number allocated
int allocate_vectors(MsiInfo* info, int numVectors, MsiVector* vectorsOut);

// Free allocated vectors
void free_vectors(MsiInfo* info);

// ================================================================
// Handler Registration
// ================================================================

// Register interrupt handler for MSI/MSI-X vector
MsiStatus register_handler(uint32_t vector, MsiHandler handler, void* context);

// Unregister interrupt handler
MsiStatus unregister_handler(uint32_t vector);

// ================================================================
// Address/Data Helpers
// ================================================================

// Build MSI address for a given CPU
// cpuId: target CPU (APIC ID)
// Returns 64-bit MSI address
uint64_t build_msi_address(uint32_t cpuId);

// Build MSI data for a given vector
// vector: interrupt vector number
// Returns MSI data value
uint32_t build_msi_data(uint32_t vector);

// ================================================================
// PCI Config Access Helpers
// ================================================================

// Find capability in PCI config space
// Returns offset or 0 if not found
uint8_t find_capability(uint8_t bus, uint8_t device, uint8_t function, uint8_t capId);

// Read from PCI config space
uint8_t  pci_config_read8(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset);
uint16_t pci_config_read16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset);
uint32_t pci_config_read32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset);

// Write to PCI config space
void pci_config_write8(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint8_t value);
void pci_config_write16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint16_t value);
void pci_config_write32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint32_t value);

// ================================================================
// IOAPIC/APIC Integration
// ================================================================

// Allocate vector from system interrupt controller
// Returns vector number, or 0 on failure
uint8_t allocate_irq_vector();

// Free vector back to system
void free_irq_vector(uint8_t vector);

// Route MSI to CPU (configure APIC if needed)
MsiStatus route_msi_to_cpu(uint32_t vector, uint32_t cpuId);

// ================================================================
// Debug/Status
// ================================================================

// Print MSI capability info
void print_msi_info(const MsiInfo* info);

// Print all MSI-enabled devices
void print_all_msi_devices();

// Get status string
const char* status_string(MsiStatus status);

} // namespace msi
} // namespace kernel

#endif // KERNEL_MSI_H
