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

bool GetRegisterBarInfo(uint8_t bus, uint8_t dev, uint8_t func,
                        uint64_t* outPhys, uint64_t* outSize, bool* outIs64bit,
                        uint8_t* outBarIndex)
{
    if (!outPhys || !outSize || !outIs64bit || !outBarIndex) return false;

    *outPhys = 0;
    *outSize = 0;
    *outIs64bit = false;
    *outBarIndex = 0xFF;

    // Some Intel-compatible devices expose a legacy I/O BAR before the
    // register MMIO BAR (QEMU's e1000 is one example). For the exact
    // supported Ethernet match, inspect all conventional PCI BAR slots and
    // select the first valid memory BAR instead of assuming BAR0.
    for (uint8_t barIndex = 0; barIndex < 6; ++barIndex) {
        const uint8_t offset = static_cast<uint8_t>(0x10 + barIndex * 4);
        const uint32_t bar = PciRead32(bus, dev, func, offset);
        if (bar & 0x01u) continue; // I/O BAR; not the register MMIO window.

        const uint8_t barType = static_cast<uint8_t>((bar >> 1) & 0x03u);
        if (barType == 1 || barType == 3) continue; // reserved encoding

        const bool is64bit = barType == 2;
        if (is64bit && barIndex == 5) continue; // no upper BAR slot

        const uint32_t upperBar = is64bit
            ? PciRead32(bus, dev, func, static_cast<uint8_t>(offset + 4)) : 0;
        if (is64bit && upperBar == 0xFFFFFFFFu) {
            ++barIndex;
            continue;
        }
        const uint64_t phys = (static_cast<uint64_t>(bar & 0xFFFFFFF0u) |
                               (static_cast<uint64_t>(upperBar) << 32));
        if (phys == 0 || phys == 0xFFFFFFFFFFFFFFFFULL) {
            if (is64bit) ++barIndex;
            continue;
        }

        *outPhys = phys;
        // Use only the register range consumed by the kernel.  The actual BAR
        // resource size is intentionally not guessed by writing all ones.
        *outSize = PCI_NIC_MMIO_WINDOW_SIZE;
        *outIs64bit = is64bit;
        *outBarIndex = barIndex;
        return true;
    }

    return false;
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
                
                // Keep every PCI network controller in the bounded inventory.
                // Wireless controllers commonly use a non-Ethernet subclass
                // (for example 0x80), so filtering on subclass here would
                // hide the hardware we need to identify next.
                if (classCode != PCI_CLASS_NETWORK) {
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
                pciDev->revisionId = PciRead8(bus, dev, func, 0x08);
                pciDev->subsystemVendorId = PciRead16(bus, dev, func, 0x2C);
                pciDev->subsystemDeviceId = PciRead16(bus, dev, func, 0x2E);
                
                // Read IRQ line
                pciDev->irqLine = PciRead8(bus, dev, func, 0x3C);
                
                // Only select a register BAR for a driver-compatible Ethernet
                // device.  GetRegisterBarInfo is read-only; unsupported
                // controllers remain identity-only and are never touched.
                if (subclass == PCI_SUBCLASS_ETH && IsSupportedNic(vendorId, deviceId)) {
                    pciDev->isMemoryBar = GetRegisterBarInfo(bus, dev, func,
                                                              &pciDev->bar0Phys,
                                                              &pciDev->bar0Size,
                                                              &pciDev->is64bit,
                                                              &pciDev->barIndex);
                } else {
                    uint32_t bar0 = PciRead32(bus, dev, func, 0x10);
                    pciDev->isMemoryBar = (bar0 & 0x01u) == 0;
                    pciDev->is64bit = pciDev->isMemoryBar && (((bar0 >> 1) & 0x03u) == 2);
                    pciDev->bar0Phys = bar0 & 0xFFFFFFF0u;
                    if (pciDev->is64bit) {
                        pciDev->bar0Phys |= static_cast<uint64_t>(PciRead32(bus, dev, func, 0x14)) << 32;
                    }
                    pciDev->bar0Size = 0;
                    pciDev->barIndex = 0xFF;
                }
                
                pciDev->bar0Virt = 0;  // Will be set after mapping
                pciDev->found = true;
                pciDev->mapped = false;
                
                result->deviceCount++;
                
                // Count every exact supported Ethernet match, while keeping
                // the existing first-match binding policy for the kernel.
                if (subclass == PCI_SUBCLASS_ETH &&
                    IsSupportedNic(vendorId, deviceId)) {
                    nicCount++;
                    if (result->nic == nullptr) {
                        result->nic = pciDev;
                    }
                }
            }
        }
    }
    
    return nicCount;
}

void PrintPciDevice(EFI_SYSTEM_TABLE* ST, const PciDevice* dev)
{
    if (!ST || !dev || !dev->found) return;
    
    Print((CONST CHAR16*)L"  [%02x:%02x.%x] Vendor=%04x Device=%04x Subsystem=%04x:%04x Class=%02x/%02x ProgIF=%02x Rev=%02x IRQ=%u\n",
          (UINT32)dev->bus, (UINT32)dev->device, (UINT32)dev->function,
          (UINT32)dev->vendorId, (UINT32)dev->deviceId,
          (UINT32)dev->subsystemVendorId, (UINT32)dev->subsystemDeviceId,
          (UINT32)dev->classCode, (UINT32)dev->subclass,
          (UINT32)dev->progIf, (UINT32)dev->revisionId,
          (UINT32)dev->irqLine);
    
    if (dev->isMemoryBar) {
        Print((CONST CHAR16*)L"    BAR%u: Phys=%016lx Size=%lx (%s)\n",
              (UINT32)dev->barIndex,
              dev->bar0Phys, dev->bar0Size,
              dev->is64bit ? L"64-bit" : L"32-bit");
        
        if (dev->mapped) {
            Print((CONST CHAR16*)L"    Mapped to Virt=%016lx\n", dev->bar0Virt);
        }
    } else {
        Print((CONST CHAR16*)L"    BAR0: I/O space or unavailable (base=%016lx)\n",
              dev->bar0Phys);
    }
    
    if (dev->classCode == PCI_CLASS_NETWORK &&
        dev->subclass == PCI_SUBCLASS_ETH &&
        IsSupportedNic(dev->vendorId, dev->deviceId)) {
        Print((CONST CHAR16*)L"    Driver: intel-e1000 family (supported)\n");
    } else {
        Print((CONST CHAR16*)L"    Driver: unsupported (identity only; no binding)\n");
    }
}

} // namespace pci
} // namespace guideXOS
