// PCI Enumeration Implementation for guideXOS Bootloader
//
// Copyright (c) 2026 guideXOS Server
//

#include "pci.h"
#include "uefi_shim.h"  // For Print macro

// Use MSVC intrinsics for I/O port access
extern "C" unsigned long __indword(unsigned short port);
extern "C" void __outdword(unsigned short port, unsigned long value);
#pragma intrinsic(__indword)
#pragma intrinsic(__outdword)

namespace guideXOS {
namespace pci {

// ================================================================
// I/O Port Access via MSVC intrinsics
// ================================================================

static inline uint32_t PortRead32(uint16_t port)
{
    return __indword(port);
}

static inline void PortWrite32(uint16_t port, uint32_t value)
{
    __outdword(port, value);
}

// ================================================================
// PCI Configuration Space Access
// ================================================================

uint32_t PciRead32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset)
{
    uint32_t addr = 0x80000000u |
                    ((uint32_t)bus  << 16) |
                    ((uint32_t)dev  << 11) |
                    ((uint32_t)func << 8)  |
                    (offset & 0xFC);
    
    PortWrite32(PCI_CONFIG_ADDR, addr);
    return PortRead32(PCI_CONFIG_DATA);
}

uint16_t PciRead16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset)
{
    uint32_t dword = PciRead32(bus, dev, func, offset & 0xFC);
    return (uint16_t)(dword >> ((offset & 2) * 8));
}

uint8_t PciRead8(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset)
{
    uint32_t dword = PciRead32(bus, dev, func, offset & 0xFC);
    return (uint8_t)(dword >> ((offset & 3) * 8));
}

void PciWrite32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint32_t value)
{
    uint32_t addr = 0x80000000u |
                    ((uint32_t)bus  << 16) |
                    ((uint32_t)dev  << 11) |
                    ((uint32_t)func << 8)  |
                    (offset & 0xFC);
    
    PortWrite32(PCI_CONFIG_ADDR, addr);
    PortWrite32(PCI_CONFIG_DATA, value);
}

void PciWrite16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint16_t value)
{
    uint32_t dword = PciRead32(bus, dev, func, offset & 0xFC);
    uint32_t shift = (offset & 2) * 8;
    dword = (dword & ~(0xFFFFu << shift)) | ((uint32_t)value << shift);
    PciWrite32(bus, dev, func, offset & 0xFC, dword);
}

// ================================================================
// PCI Device Detection
// ================================================================

bool IsSupportedNic(uint16_t vendorId, uint16_t deviceId)
{
    if (vendorId != PCI_VENDOR_INTEL) return false;
    
    return (deviceId == PCI_DEVICE_E1000 ||
            deviceId == PCI_DEVICE_E1000E ||
            deviceId == PCI_DEVICE_I217);
}

bool GetBar0Info(uint8_t bus, uint8_t dev, uint8_t func,
                 uint64_t* outPhys, uint64_t* outSize, bool* outIs64bit)
{
    if (!outPhys || !outSize || !outIs64bit) return false;
    
    *outPhys = 0;
    *outSize = 0;
    *outIs64bit = false;
    
    // Read BAR0
    uint32_t bar0 = PciRead32(bus, dev, func, 0x10);
    
    // Check if memory BAR (bit 0 = 0)
    if (bar0 & 0x01) {
        // I/O BAR, not MMIO
        return false;
    }
    
    // Check BAR type (bits 1-2)
    uint8_t barType = (bar0 >> 1) & 0x03;
    *outIs64bit = (barType == 2);  // Type 2 = 64-bit BAR
    
    // Get physical address
    uint64_t phys = bar0 & 0xFFFFFFF0u;
    
    if (*outIs64bit) {
        uint32_t bar1 = PciRead32(bus, dev, func, 0x14);
        phys |= ((uint64_t)bar1 << 32);
    }
    
    // Determine BAR size by writing all 1s and reading back
    // Save original value first
    uint32_t origBar0 = bar0;
    uint32_t origBar1 = 0;
    if (*outIs64bit) {
        origBar1 = PciRead32(bus, dev, func, 0x14);
    }
    
    // Write all 1s
    PciWrite32(bus, dev, func, 0x10, 0xFFFFFFFF);
    if (*outIs64bit) {
        PciWrite32(bus, dev, func, 0x14, 0xFFFFFFFF);
    }
    
    // Read back
    uint32_t sizeBar0 = PciRead32(bus, dev, func, 0x10);
    uint64_t size = (~(sizeBar0 & 0xFFFFFFF0u)) + 1;
    
    if (*outIs64bit) {
        uint32_t sizeBar1 = PciRead32(bus, dev, func, 0x14);
        uint64_t size64 = ((uint64_t)sizeBar1 << 32) | (sizeBar0 & 0xFFFFFFF0u);
        size = (~size64) + 1;
    }
    
    // Restore original BAR values
    PciWrite32(bus, dev, func, 0x10, origBar0);
    if (*outIs64bit) {
        PciWrite32(bus, dev, func, 0x14, origBar1);
    }
    
    // Sanity check size (E1000 is typically 128KB or 256KB)
    if (size == 0 || size > 0x1000000) {  // Max 16MB
        // Use default size for E1000
        size = 0x20000;  // 128KB
    }
    
    *outPhys = phys;
    *outSize = size;
    
    return true;
}

void EnablePciDevice(uint8_t bus, uint8_t dev, uint8_t func)
{
    // Read command register
    uint16_t cmd = PciRead16(bus, dev, func, 0x04);
    
    // Enable bus mastering (bit 2) and memory space (bit 1)
    cmd |= (1u << 2) | (1u << 1);
    
    PciWrite16(bus, dev, func, 0x04, cmd);
}

// ================================================================
// PCI Enumeration
// ================================================================

void InitPci()
{
    // Nothing to initialize for x86 I/O port access
}

uint8_t EnumeratePci(PciEnumResult* result)
{
    if (!result) return 0;
    
    // Clear result
    for (uint8_t i = 0; i < MAX_PCI_DEVICES; i++) {
        result->devices[i].found = false;
        result->devices[i].mapped = false;
    }
    result->deviceCount = 0;
    result->nic = nullptr;
    
    uint8_t nicCount = 0;
    
    // Scan buses 0-7
    for (uint8_t bus = 0; bus < 8; bus++) {
        for (uint8_t dev = 0; dev < 32; dev++) {
            // Read vendor/device ID
            uint32_t id = PciRead32(bus, dev, 0, 0x00);
            
            // Check for no device
            if (id == 0xFFFFFFFF || id == 0) continue;
            
            // Check header type for multi-function
            uint8_t headerType = PciRead8(bus, dev, 0, 0x0E);
            uint8_t maxFunc = (headerType & 0x80) ? 8 : 1;
            
            for (uint8_t func = 0; func < maxFunc; func++) {
                if (func > 0) {
                    id = PciRead32(bus, dev, func, 0x00);
                    if (id == 0xFFFFFFFF || id == 0) continue;
                }
                
                // Read class code
                uint32_t classReg = PciRead32(bus, dev, func, 0x08);
                uint8_t classCode = (uint8_t)(classReg >> 24);
                uint8_t subclass = (uint8_t)(classReg >> 16);
                uint8_t progIf = (uint8_t)(classReg >> 8);
                
                // Check for network controller
                if (classCode != PCI_CLASS_NETWORK || subclass != PCI_SUBCLASS_ETH) {
                    continue;
                }
                
                uint16_t vendorId = (uint16_t)(id & 0xFFFF);
                uint16_t deviceId = (uint16_t)(id >> 16);
                
                // Found a network controller
                if (result->deviceCount >= MAX_PCI_DEVICES) continue;
                
                PciDevice* pciDev = &result->devices[result->deviceCount];
                
                pciDev->bus = bus;
                pciDev->device = dev;
                pciDev->function = func;
                pciDev->vendorId = vendorId;
                pciDev->deviceId = deviceId;
                pciDev->classCode = classCode;
                pciDev->subclass = subclass;
                pciDev->progIf = progIf;
                
                // Read IRQ line
                pciDev->irqLine = PciRead8(bus, dev, func, 0x3C);
                
                // Get BAR0 info
                pciDev->isMemoryBar = GetBar0Info(bus, dev, func,
                                                   &pciDev->bar0Phys,
                                                   &pciDev->bar0Size,
                                                   &pciDev->is64bit);
                
                pciDev->bar0Virt = 0;  // Will be set after mapping
                pciDev->found = true;
                pciDev->mapped = false;
                
                result->deviceCount++;
                
                // Track first supported NIC
                if (result->nic == nullptr && IsSupportedNic(vendorId, deviceId)) {
                    result->nic = pciDev;
                    nicCount++;
                }
            }
        }
    }
    
    return nicCount;
}

void PrintPciDevice(EFI_SYSTEM_TABLE* ST, const PciDevice* dev)
{
    if (!ST || !dev || !dev->found) return;
    
    Print((CONST CHAR16*)L"  [%02x:%02x.%x] Vendor=%04x Device=%04x Class=%02x/%02x IRQ=%d\n",
          (UINTN)dev->bus, (UINTN)dev->device, (UINTN)dev->function,
          (UINTN)dev->vendorId, (UINTN)dev->deviceId,
          (UINTN)dev->classCode, (UINTN)dev->subclass,
          (UINTN)dev->irqLine);
    
    if (dev->isMemoryBar) {
        Print((CONST CHAR16*)L"    BAR0: Phys=%016lx Size=%lx (%s)\n",
              dev->bar0Phys, dev->bar0Size,
              dev->is64bit ? L"64-bit" : L"32-bit");
        
        if (dev->mapped) {
            Print((CONST CHAR16*)L"    Mapped to Virt=%016lx\n", dev->bar0Virt);
        }
    }
    
    if (IsSupportedNic(dev->vendorId, dev->deviceId)) {
        Print((CONST CHAR16*)L"    ** Supported Intel E1000 NIC **\n");
    }
}

} // namespace pci
} // namespace guideXOS
