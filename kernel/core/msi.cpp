// MSI/MSI-X Interrupt Support Implementation
//
// Provides Message Signaled Interrupt support for PCIe devices.
//
// Implementation Notes:
// - Uses x86 PCI config space access (port I/O)
// - Assumes APIC is available for MSI routing
// - Vector allocation managed by kernel interrupt subsystem
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/kernel/msi.h"
#include "include/kernel/serial_debug.h"

#if defined(_MSC_VER)
#define GXOS_MSVC_STUB 1
#else
#define GXOS_MSVC_STUB 0
#endif

namespace kernel {
namespace msi {

// ================================================================
// Internal State
// ================================================================

static bool s_initialized = false;

// Vector allocation tracking
static const int MAX_MSI_VECTORS = 256;
static bool s_vectorAllocated[MAX_MSI_VECTORS];
static MsiHandler s_vectorHandlers[MAX_MSI_VECTORS];
static void* s_vectorContexts[MAX_MSI_VECTORS];

// First available vector for MSI (above legacy IRQs)
static const uint8_t VECTOR_BASE = 48;

// ================================================================
// Port I/O Helpers (x86/AMD64)
// ================================================================

#if !GXOS_MSVC_STUB

static inline void outl(uint16_t port, uint32_t value)
{
    asm volatile("outl %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint32_t inl(uint16_t port)
{
    uint32_t value;
    asm volatile("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void outw(uint16_t port, uint16_t value)
{
    asm volatile("outw %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint16_t inw(uint16_t port)
{
    uint16_t value;
    asm volatile("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void outb(uint16_t port, uint8_t value)
{
    asm volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port)
{
    uint8_t value;
    asm volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

#else

// MSVC stubs
static inline void outl(uint16_t port, uint32_t value) { (void)port; (void)value; }
static inline uint32_t inl(uint16_t port) { (void)port; return 0; }
static inline void outw(uint16_t port, uint16_t value) { (void)port; (void)value; }
static inline uint16_t inw(uint16_t port) { (void)port; return 0; }
static inline void outb(uint16_t port, uint8_t value) { (void)port; (void)value; }
static inline uint8_t inb(uint16_t port) { (void)port; return 0; }

#endif

// PCI config space ports
static const uint16_t PCI_CONFIG_ADDR = 0x0CF8;
static const uint16_t PCI_CONFIG_DATA = 0x0CFC;

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

// Build PCI config address
static inline uint32_t pci_addr(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset)
{
    return 0x80000000 |
           (static_cast<uint32_t>(bus) << 16) |
           (static_cast<uint32_t>(dev) << 11) |
           (static_cast<uint32_t>(func) << 8) |
           (offset & 0xFC);
}

// ================================================================
// Initialization
// ================================================================

void init()
{
    if (s_initialized) return;
    
    kernel::serial::puts("[MSI] Initializing MSI/MSI-X subsystem...\n");
    
    // Initialize vector tracking
    for (int i = 0; i < MAX_MSI_VECTORS; ++i) {
        s_vectorAllocated[i] = false;
        s_vectorHandlers[i] = nullptr;
        s_vectorContexts[i] = nullptr;
    }
    
    // Reserve vectors 0-47 (legacy IRQs and exceptions)
    for (int i = 0; i < VECTOR_BASE; ++i) {
        s_vectorAllocated[i] = true;
    }
    
    s_initialized = true;
    
    kernel::serial::puts("[MSI] MSI subsystem initialized\n");
}

// ================================================================
// PCI Config Access
// ================================================================

uint8_t pci_config_read8(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset)
{
    outl(PCI_CONFIG_ADDR, pci_addr(bus, dev, func, offset));
    return inb(PCI_CONFIG_DATA + (offset & 3));
}

uint16_t pci_config_read16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset)
{
    outl(PCI_CONFIG_ADDR, pci_addr(bus, dev, func, offset));
    return inw(PCI_CONFIG_DATA + (offset & 2));
}

uint32_t pci_config_read32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset)
{
    outl(PCI_CONFIG_ADDR, pci_addr(bus, dev, func, offset));
    return inl(PCI_CONFIG_DATA);
}

void pci_config_write8(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint8_t value)
{
    outl(PCI_CONFIG_ADDR, pci_addr(bus, dev, func, offset));
    outb(PCI_CONFIG_DATA + (offset & 3), value);
}

void pci_config_write16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint16_t value)
{
    outl(PCI_CONFIG_ADDR, pci_addr(bus, dev, func, offset));
    outw(PCI_CONFIG_DATA + (offset & 2), value);
}

void pci_config_write32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint32_t value)
{
    outl(PCI_CONFIG_ADDR, pci_addr(bus, dev, func, offset));
    outl(PCI_CONFIG_DATA, value);
}

// ================================================================
// Capability Finding
// ================================================================

uint8_t find_capability(uint8_t bus, uint8_t device, uint8_t function, uint8_t capId)
{
    // Check if device has capabilities
    uint16_t status = pci_config_read16(bus, device, function, 0x06);
    if (!(status & 0x10)) {  // Capabilities List bit
        return 0;
    }
    
    // Get capabilities pointer
    uint8_t capPtr = pci_config_read8(bus, device, function, 0x34) & 0xFC;
    
    // Walk capability list
    int maxCaps = 48;  // Prevent infinite loop
    while (capPtr != 0 && maxCaps-- > 0) {
        uint8_t id = pci_config_read8(bus, device, function, capPtr);
        if (id == capId) {
            return capPtr;
        }
        capPtr = pci_config_read8(bus, device, function, capPtr + 1) & 0xFC;
    }
    
    return 0;
}

// ================================================================
// Device Detection
// ================================================================

MsiStatus probe_device(uint8_t bus, uint8_t device, uint8_t function, MsiInfo* infoOut)
{
    if (infoOut == nullptr) {
        return MSI_ERR_INVALID;
    }
    
    memzero(infoOut, sizeof(MsiInfo));
    infoOut->bus = bus;
    infoOut->device = device;
    infoOut->function = function;
    
    // Look for MSI capability
    uint8_t msiCap = find_capability(bus, device, function, PCI_CAP_ID_MSI);
    if (msiCap != 0) {
        infoOut->hasMsi = true;
        infoOut->msiCapOffset = msiCap;
        
        // Parse MSI control register
        uint16_t control = pci_config_read16(bus, device, function, msiCap + 2);
        infoOut->has64Bit = (control & MSI_CTRL_64BIT) != 0;
        infoOut->hasPerVectorMask = (control & MSI_CTRL_PER_VECTOR) != 0;
        
        // Max vectors = 2^((control >> 1) & 7)
        uint8_t multiCap = (control >> 1) & 7;
        infoOut->msiMaxVectors = 1 << multiCap;
        
        kernel::serial::puts("[MSI] Found MSI capability at offset ");
        kernel::serial::put_hex8(msiCap);
        kernel::serial::puts(", max vectors: ");
        kernel::serial::put_hex8(infoOut->msiMaxVectors);
        kernel::serial::putc('\n');
    }
    
    // Look for MSI-X capability
    uint8_t msixCap = find_capability(bus, device, function, PCI_CAP_ID_MSIX);
    if (msixCap != 0) {
        infoOut->hasMsix = true;
        infoOut->msixCapOffset = msixCap;
        
        // Parse MSI-X control register
        uint16_t control = pci_config_read16(bus, device, function, msixCap + 2);
        infoOut->msixTableSize = (control & MSIX_CTRL_TABLE_SIZE) + 1;
        
        // Table location
        uint32_t tableOffset = pci_config_read32(bus, device, function, msixCap + 4);
        infoOut->msixTableBar = tableOffset & 0x07;
        infoOut->msixTableOffset = tableOffset & ~0x07;
        
        // PBA location
        uint32_t pbaOffset = pci_config_read32(bus, device, function, msixCap + 8);
        infoOut->msixPbaBar = pbaOffset & 0x07;
        infoOut->msixPbaOffset = pbaOffset & ~0x07;
        
        kernel::serial::puts("[MSI] Found MSI-X capability at offset ");
        kernel::serial::put_hex8(msixCap);
        kernel::serial::puts(", table size: ");
        kernel::serial::put_hex16(infoOut->msixTableSize);
        kernel::serial::putc('\n');
    }
    
    if (!infoOut->hasMsi && !infoOut->hasMsix) {
        return MSI_ERR_NOT_FOUND;
    }
    
    return MSI_OK;
}

bool has_msi(uint8_t bus, uint8_t device, uint8_t function)
{
    return find_capability(bus, device, function, PCI_CAP_ID_MSI) != 0;
}

bool has_msix(uint8_t bus, uint8_t device, uint8_t function)
{
    return find_capability(bus, device, function, PCI_CAP_ID_MSIX) != 0;
}

// ================================================================
// MSI Configuration
// ================================================================

MsiStatus enable_msi(MsiInfo* info, uint8_t numVectors, uint8_t baseVector)
{
    if (info == nullptr || !info->hasMsi) {
        return MSI_ERR_INVALID;
    }
    
    if (info->msiEnabled) {
        return MSI_ERR_ALREADY;
    }
    
    // Validate vector count (must be power of 2, <= max)
    if (numVectors == 0 || numVectors > info->msiMaxVectors) {
        numVectors = info->msiMaxVectors;
    }
    if (numVectors > 32) numVectors = 32;
    
    // Find log2 of numVectors
    uint8_t multiMsg = 0;
    uint8_t temp = numVectors;
    while (temp > 1) {
        temp >>= 1;
        ++multiMsg;
    }
    numVectors = 1 << multiMsg;  // Actual allocated
    
    uint8_t cap = info->msiCapOffset;
    
    // Build MSI address and data
    uint64_t addr = build_msi_address(0);  // Target CPU 0
    uint32_t data = build_msi_data(baseVector);
    
    // Write address
    pci_config_write32(info->bus, info->device, info->function, cap + 4,
                       static_cast<uint32_t>(addr));
    
    if (info->has64Bit) {
        pci_config_write32(info->bus, info->device, info->function, cap + 8,
                           static_cast<uint32_t>(addr >> 32));
        pci_config_write16(info->bus, info->device, info->function, cap + 12,
                           static_cast<uint16_t>(data));
    } else {
        pci_config_write16(info->bus, info->device, info->function, cap + 8,
                           static_cast<uint16_t>(data));
    }
    
    // Enable MSI with requested vector count
    uint16_t control = pci_config_read16(info->bus, info->device, info->function, cap + 2);
    control &= ~MSI_CTRL_MULTI_ENABLE;  // Clear multi-message enable
    control |= (multiMsg << 4);          // Set multi-message enable
    control |= MSI_CTRL_ENABLE;          // Enable MSI
    pci_config_write16(info->bus, info->device, info->function, cap + 2, control);
    
    info->msiEnabled = true;
    info->allocatedVectors = numVectors;
    info->baseVector = baseVector;
    
    kernel::serial::puts("[MSI] Enabled MSI with ");
    kernel::serial::put_hex8(numVectors);
    kernel::serial::puts(" vectors starting at ");
    kernel::serial::put_hex8(baseVector);
    kernel::serial::putc('\n');
    
    return MSI_OK;
}

MsiStatus disable_msi(MsiInfo* info)
{
    if (info == nullptr || !info->hasMsi) {
        return MSI_ERR_INVALID;
    }
    
    if (!info->msiEnabled) {
        return MSI_ERR_DISABLED;
    }
    
    uint8_t cap = info->msiCapOffset;
    
    // Disable MSI
    uint16_t control = pci_config_read16(info->bus, info->device, info->function, cap + 2);
    control &= ~MSI_CTRL_ENABLE;
    pci_config_write16(info->bus, info->device, info->function, cap + 2, control);
    
    info->msiEnabled = false;
    info->allocatedVectors = 0;
    
    kernel::serial::puts("[MSI] Disabled MSI\n");
    
    return MSI_OK;
}

MsiStatus configure_msi_vector(MsiInfo* info, uint8_t index, uint32_t vector, uint32_t cpuId)
{
    /*
     * Configure MSI vector address/data
     * 
     * Note: For MSI (not MSI-X), all vectors share the same base address.
     * The vector number is derived from base data + index.
     */
    
    if (info == nullptr || !info->hasMsi || !info->msiEnabled) {
        return MSI_ERR_INVALID;
    }
    
    if (index >= info->allocatedVectors) {
        return MSI_ERR_INVALID;
    }
    
    // For MSI, we can only configure the base vector
    // All other vectors are base + index
    if (index == 0) {
        uint8_t cap = info->msiCapOffset;
        
        uint64_t addr = build_msi_address(cpuId);
        uint32_t data = build_msi_data(vector);
        
        pci_config_write32(info->bus, info->device, info->function, cap + 4,
                           static_cast<uint32_t>(addr));
        
        if (info->has64Bit) {
            pci_config_write32(info->bus, info->device, info->function, cap + 8,
                               static_cast<uint32_t>(addr >> 32));
            pci_config_write16(info->bus, info->device, info->function, cap + 12,
                               static_cast<uint16_t>(data));
        } else {
            pci_config_write16(info->bus, info->device, info->function, cap + 8,
                               static_cast<uint16_t>(data));
        }
        
        info->baseVector = static_cast<uint8_t>(vector);
    }
    
    return MSI_OK;
}

MsiStatus mask_msi_vector(MsiInfo* info, uint8_t index, bool mask)
{
    if (info == nullptr || !info->hasMsi || !info->hasPerVectorMask) {
        return MSI_ERR_UNSUPPORTED;
    }
    
    if (index >= info->allocatedVectors) {
        return MSI_ERR_INVALID;
    }
    
    uint8_t cap = info->msiCapOffset;
    uint8_t maskOffset = info->has64Bit ? (cap + 16) : (cap + 12);
    
    uint32_t maskBits = pci_config_read32(info->bus, info->device, info->function, maskOffset);
    
    if (mask) {
        maskBits |= (1u << index);
    } else {
        maskBits &= ~(1u << index);
    }
    
    pci_config_write32(info->bus, info->device, info->function, maskOffset, maskBits);
    
    return MSI_OK;
}

// ================================================================
// MSI-X Configuration
// ================================================================

MsiStatus enable_msix(MsiInfo* info)
{
    if (info == nullptr || !info->hasMsix) {
        return MSI_ERR_INVALID;
    }
    
    if (info->msixEnabled) {
        return MSI_ERR_ALREADY;
    }
    
    uint8_t cap = info->msixCapOffset;
    
    // Enable MSI-X (also sets function mask initially)
    uint16_t control = pci_config_read16(info->bus, info->device, info->function, cap + 2);
    control |= MSIX_CTRL_ENABLE | MSIX_CTRL_FUNC_MASK;
    pci_config_write16(info->bus, info->device, info->function, cap + 2, control);
    
    info->msixEnabled = true;
    info->allocatedVectors = info->msixTableSize;
    
    kernel::serial::puts("[MSI-X] Enabled with ");
    kernel::serial::put_hex16(info->msixTableSize);
    kernel::serial::puts(" vectors\n");
    
    return MSI_OK;
}

MsiStatus disable_msix(MsiInfo* info)
{
    if (info == nullptr || !info->hasMsix) {
        return MSI_ERR_INVALID;
    }
    
    if (!info->msixEnabled) {
        return MSI_ERR_DISABLED;
    }
    
    uint8_t cap = info->msixCapOffset;
    
    // Disable MSI-X
    uint16_t control = pci_config_read16(info->bus, info->device, info->function, cap + 2);
    control &= ~MSIX_CTRL_ENABLE;
    pci_config_write16(info->bus, info->device, info->function, cap + 2, control);
    
    info->msixEnabled = false;
    info->allocatedVectors = 0;
    
    kernel::serial::puts("[MSI-X] Disabled\n");
    
    return MSI_OK;
}

MsiStatus configure_msix_vector(MsiInfo* info, uint16_t index, 
                                uint32_t vector, uint32_t cpuId)
{
    /*
     * STUB: Configure MSI-X table entry
     * 
     * Full implementation would:
     * 1. Map MSI-X table BAR if not already mapped
     * 2. Write address and data to table entry
     * 3. Clear mask bit
     */
    
    if (info == nullptr || !info->hasMsix || !info->msixEnabled) {
        return MSI_ERR_INVALID;
    }
    
    if (index >= info->msixTableSize) {
        return MSI_ERR_INVALID;
    }
    
    // Would write to MSI-X table in BAR memory
    // For now, just log
    kernel::serial::puts("[MSI-X] Configure vector ");
    kernel::serial::put_hex16(index);
    kernel::serial::puts(" -> IRQ ");
    kernel::serial::put_hex32(vector);
    kernel::serial::puts(", CPU ");
    kernel::serial::put_hex32(cpuId);
    kernel::serial::puts(" (stub)\n");
    
    return MSI_OK;
}

MsiStatus mask_msix_vector(MsiInfo* info, uint16_t index, bool mask)
{
    /*
     * STUB: Mask/unmask MSI-X vector
     * 
     * Full implementation would write to control field of table entry.
     */
    
    if (info == nullptr || !info->hasMsix || !info->msixEnabled) {
        return MSI_ERR_INVALID;
    }
    
    if (index >= info->msixTableSize) {
        return MSI_ERR_INVALID;
    }
    
    (void)mask;
    
    return MSI_OK;
}

MsiStatus mask_all_msix(MsiInfo* info, bool mask)
{
    if (info == nullptr || !info->hasMsix) {
        return MSI_ERR_INVALID;
    }
    
    uint8_t cap = info->msixCapOffset;
    uint16_t control = pci_config_read16(info->bus, info->device, info->function, cap + 2);
    
    if (mask) {
        control |= MSIX_CTRL_FUNC_MASK;
    } else {
        control &= ~MSIX_CTRL_FUNC_MASK;
    }
    
    pci_config_write16(info->bus, info->device, info->function, cap + 2, control);
    
    return MSI_OK;
}

bool is_msix_pending(MsiInfo* info, uint16_t index)
{
    /*
     * STUB: Check MSI-X pending bit
     * 
     * Full implementation would read from PBA in BAR memory.
     */
    
    (void)info;
    (void)index;
    
    return false;
}

// ================================================================
// Vector Allocation
// ================================================================

int allocate_vectors(MsiInfo* info, int numVectors, MsiVector* vectorsOut)
{
    if (info == nullptr || vectorsOut == nullptr || numVectors <= 0) {
        return 0;
    }
    
    // Prefer MSI-X if available
    if (info->hasMsix) {
        if (numVectors > static_cast<int>(info->msixTableSize)) {
            numVectors = info->msixTableSize;
        }
    } else if (info->hasMsi) {
        if (numVectors > info->msiMaxVectors) {
            numVectors = info->msiMaxVectors;
        }
    } else {
        return 0;
    }
    
    // Allocate vectors from pool
    int allocated = 0;
    for (int i = 0; i < numVectors; ++i) {
        uint8_t vector = allocate_irq_vector();
        if (vector == 0) break;
        
        vectorsOut[allocated].vector = vector;
        vectorsOut[allocated].cpuId = 0;  // Default to CPU 0
        vectorsOut[allocated].masked = true;
        vectorsOut[allocated].context = nullptr;
        ++allocated;
    }
    
    // Enable MSI or MSI-X
    if (allocated > 0) {
        if (info->hasMsix) {
            enable_msix(info);
            mask_all_msix(info, false);  // Unmask after setup
        } else {
            enable_msi(info, static_cast<uint8_t>(allocated), vectorsOut[0].vector);
        }
    }
    
    return allocated;
}

void free_vectors(MsiInfo* info)
{
    if (info == nullptr) return;
    
    // Disable MSI/MSI-X first
    if (info->msixEnabled) {
        disable_msix(info);
    }
    if (info->msiEnabled) {
        disable_msi(info);
    }
    
    // Free allocated vectors
    for (uint8_t i = 0; i < info->allocatedVectors; ++i) {
        free_irq_vector(info->baseVector + i);
    }
}

// ================================================================
// Handler Registration
// ================================================================

MsiStatus register_handler(uint32_t vector, MsiHandler handler, void* context)
{
    if (vector >= MAX_MSI_VECTORS || handler == nullptr) {
        return MSI_ERR_INVALID;
    }
    
    s_vectorHandlers[vector] = handler;
    s_vectorContexts[vector] = context;
    
    return MSI_OK;
}

MsiStatus unregister_handler(uint32_t vector)
{
    if (vector >= MAX_MSI_VECTORS) {
        return MSI_ERR_INVALID;
    }
    
    s_vectorHandlers[vector] = nullptr;
    s_vectorContexts[vector] = nullptr;
    
    return MSI_OK;
}

// ================================================================
// Address/Data Helpers
// ================================================================

uint64_t build_msi_address(uint32_t cpuId)
{
    // Format for x86/AMD64:
    // 0xFEE00000 | (destination ID << 12)
    return MSI_ADDR_BASE | ((cpuId & 0xFF) << MSI_ADDR_DEST_SHIFT);
}

uint32_t build_msi_data(uint32_t vector)
{
    // Format: vector number in bits 0-7, edge triggered, fixed delivery
    return (vector & MSI_DATA_VECTOR_MASK) | MSI_DATA_TRIGGER_EDGE;
}

// ================================================================
// IRQ Vector Management
// ================================================================

uint8_t allocate_irq_vector()
{
    for (int i = VECTOR_BASE; i < MAX_MSI_VECTORS; ++i) {
        if (!s_vectorAllocated[i]) {
            s_vectorAllocated[i] = true;
            return static_cast<uint8_t>(i);
        }
    }
    return 0;  // No vectors available
}

void free_irq_vector(uint8_t vector)
{
    if (vector >= VECTOR_BASE && vector < MAX_MSI_VECTORS) {
        s_vectorAllocated[vector] = false;
        s_vectorHandlers[vector] = nullptr;
        s_vectorContexts[vector] = nullptr;
    }
}

MsiStatus route_msi_to_cpu(uint32_t vector, uint32_t cpuId)
{
    /*
     * STUB: Configure APIC for MSI delivery
     * 
     * Full implementation would configure Local APIC or I/O APIC
     * to route the interrupt to the specified CPU.
     */
    
    (void)vector;
    (void)cpuId;
    
    return MSI_OK;
}

// ================================================================
// Debug/Status
// ================================================================

void print_msi_info(const MsiInfo* info)
{
    if (info == nullptr) {
        kernel::serial::puts("[MSI] No device info\n");
        return;
    }
    
    kernel::serial::puts("[MSI] Device ");
    kernel::serial::put_hex8(info->bus);
    kernel::serial::putc(':');
    kernel::serial::put_hex8(info->device);
    kernel::serial::putc('.');
    kernel::serial::put_hex8(info->function);
    kernel::serial::putc('\n');
    
    if (info->hasMsi) {
        kernel::serial::puts("  MSI: cap@");
        kernel::serial::put_hex8(info->msiCapOffset);
        kernel::serial::puts(", max vectors=");
        kernel::serial::put_hex8(info->msiMaxVectors);
        kernel::serial::puts(info->has64Bit ? ", 64-bit" : ", 32-bit");
        kernel::serial::puts(info->hasPerVectorMask ? ", per-vector mask" : "");
        kernel::serial::puts(info->msiEnabled ? ", ENABLED" : ", disabled");
        kernel::serial::putc('\n');
    }
    
    if (info->hasMsix) {
        kernel::serial::puts("  MSI-X: cap@");
        kernel::serial::put_hex8(info->msixCapOffset);
        kernel::serial::puts(", table size=");
        kernel::serial::put_hex16(info->msixTableSize);
        kernel::serial::puts(", table BAR");
        kernel::serial::put_hex8(info->msixTableBar);
        kernel::serial::puts(info->msixEnabled ? ", ENABLED" : ", disabled");
        kernel::serial::putc('\n');
    }
    
    if (info->msiEnabled || info->msixEnabled) {
        kernel::serial::puts("  Allocated: ");
        kernel::serial::put_hex8(info->allocatedVectors);
        kernel::serial::puts(" vectors from ");
        kernel::serial::put_hex8(info->baseVector);
        kernel::serial::putc('\n');
    }
}

void print_all_msi_devices()
{
    kernel::serial::puts("[MSI] Scanning for MSI-capable devices...\n");
    
    int foundMsi = 0;
    int foundMsix = 0;
    
    // Scan PCI bus
    for (int bus = 0; bus < 256; ++bus) {
        for (int dev = 0; dev < 32; ++dev) {
            for (int func = 0; func < 8; ++func) {
                uint16_t vendorId = pci_config_read16(bus, dev, func, 0);
                if (vendorId == 0xFFFF) continue;
                
                MsiInfo info;
                if (probe_device(bus, dev, func, &info) == MSI_OK) {
                    if (info.hasMsi) ++foundMsi;
                    if (info.hasMsix) ++foundMsix;
                }
            }
        }
    }
    
    kernel::serial::puts("[MSI] Found ");
    kernel::serial::put_hex32(foundMsi);
    kernel::serial::puts(" MSI devices, ");
    kernel::serial::put_hex32(foundMsix);
    kernel::serial::puts(" MSI-X devices\n");
}

const char* status_string(MsiStatus status)
{
    switch (status) {
        case MSI_OK:              return "OK";
        case MSI_ERR_NOT_FOUND:   return "Not found";
        case MSI_ERR_INVALID:     return "Invalid parameter";
        case MSI_ERR_NO_VECTORS:  return "No vectors available";
        case MSI_ERR_UNSUPPORTED: return "Not supported";
        case MSI_ERR_ALREADY:     return "Already enabled";
        case MSI_ERR_DISABLED:    return "Not enabled";
        default:                  return "Unknown error";
    }
}

} // namespace msi
} // namespace kernel
