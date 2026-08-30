// PCI Enumeration for guideXOS Bootloader
//
// Enumerates PCI devices to find network controllers and
// read the selected register BAR (MMIO base address) for kernel use.
//
// Uses x86 I/O port access (0xCF8/0xCFC) method.
//
// Copyright (c) 2026 guideXOS Server
//

#pragma once

#include "Uefi.h"
#include <stdint.h>

// Phase 5 is a deliberately small compile-time isolation selector.  The
// same macro is passed to the kernel build so the loader and kernel stop at
// the same I219 stage.  Existing NICs ignore this selector.
#ifndef GXOS_AIDA_I219_PHASE5_STAGE
#define GXOS_AIDA_I219_PHASE5_STAGE 8
#endif

#if GXOS_AIDA_I219_PHASE5_STAGE < 0 || GXOS_AIDA_I219_PHASE5_STAGE > 8
#error GXOS_AIDA_I219_PHASE5_STAGE must be in the range 0..8
#endif

namespace guideXOS {
namespace pci {

// ================================================================
// PCI Constants
// ================================================================

// I/O ports for PCI configuration access
static const uint16_t PCI_CONFIG_ADDR = 0x0CF8;
static const uint16_t PCI_CONFIG_DATA = 0x0CFC;

// PCI class codes
static const uint8_t PCI_CLASS_NETWORK    = 0x02;
static const uint8_t PCI_SUBCLASS_ETH     = 0x00;

// Intel vendor and device IDs
static const uint16_t PCI_VENDOR_INTEL    = 0x8086;
static const uint16_t PCI_DEVICE_E1000    = 0x100E;  // 82540EM (QEMU default)
static const uint16_t PCI_DEVICE_E1000E   = 0x10D3;  // 82574L
static const uint16_t PCI_DEVICE_I217     = 0x153A;  // I217-LM
static const uint16_t PCI_DEVICE_I219_LM  = 0x156F;  // I219-LM/PCH

// Read-only discovery cannot determine a BAR's resource size without the
// destructive all-ones sizing transaction.  The approved E1000-family
// register set used by the kernel fits in this bounded mapping window.
static const uint64_t PCI_NIC_MMIO_WINDOW_SIZE = 0x6000;

// Maximum devices to track
static const uint8_t MAX_PCI_DEVICES = 8;

// ================================================================
// PCI Device Information
// ================================================================

struct PciDevice {
    uint8_t  bus;
    uint8_t  device;
    uint8_t  function;
    uint16_t vendorId;
    uint16_t deviceId;
    uint16_t subsystemVendorId;
    uint16_t subsystemDeviceId;
    uint8_t  revisionId;
    uint8_t  classCode;
    uint8_t  subclass;
    uint8_t  progIf;
    uint8_t  irqLine;
    uint64_t bar0Phys;      // Selected register BAR physical address
    uint64_t bar0Size;      // Bounded kernel register-window mapping size
    uint64_t bar0Virt;      // Selected register BAR virtual address (after mapping)
    uint8_t  barIndex;      // Selected BAR number (0..5), 0xFF if unavailable
    bool     isMemoryBar;   // true = MMIO, false = I/O port
    bool     is64bit;       // true = 64-bit BAR
    bool     found;
    bool     mapped;
};

// ================================================================
// NIC Device Info (for BootInfo)
// ================================================================

struct NicBootInfo {
    uint16_t vendorId;
    uint16_t deviceId;
    uint8_t  bus;
    uint8_t  device;
    uint8_t  function;
    uint8_t  irqLine;
    uint64_t mmioPhys;      // Physical selected register BAR address
    uint64_t mmioVirt;      // Virtual address (mapped by bootloader)
    uint64_t mmioSize;      // Size of MMIO region
    uint8_t  macAddress[6]; // Zero until the kernel reads the hardware RAR
    uint8_t  reserved[2];
    uint32_t flags;         // Bit 0: found, Bit 1: mapped, Bit 2: active
};

// Flags for NicBootInfo
static const uint32_t NIC_FLAG_FOUND  = (1u << 0);
static const uint32_t NIC_FLAG_MAPPED = (1u << 1);
static const uint32_t NIC_FLAG_ACTIVE = (1u << 2);

// ================================================================
// PCI Enumeration Results
// ================================================================

struct PciEnumResult {
    PciDevice devices[MAX_PCI_DEVICES];
    uint8_t   deviceCount;
    PciDevice* nic;         // Pointer to first NIC found (or nullptr)
};

// ================================================================
// PCI Functions
// ================================================================

// Initialize PCI access (call once at bootloader start)
void InitPci();

// Enumerate PCI buses 0-7 and find network controllers
// Returns number of NICs found
uint8_t EnumeratePci(PciEnumResult* result);

// Read PCI configuration space
uint32_t PciRead32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset);
uint16_t PciRead16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset);
uint8_t  PciRead8(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset);

// Write PCI configuration space
void PciWrite32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint32_t value);
void PciWrite16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint16_t value);

// Get the first valid register memory BAR physical address and a bounded
// kernel register-window mapping size.  This function is read-only: it never
// writes a BAR or PCI command register to discover resource sizing.
bool GetRegisterBarInfo(uint8_t bus, uint8_t dev, uint8_t func,
                        uint64_t* outPhys, uint64_t* outSize, bool* outIs64bit,
                        uint8_t* outBarIndex);

// Enable PCI bus mastering and memory space access
void EnablePciDevice(uint8_t bus, uint8_t dev, uint8_t func);

// Check if device is a supported NIC
bool IsSupportedNic(uint16_t vendorId, uint16_t deviceId);

// Print PCI device info (for debugging)
void PrintPciDevice(EFI_SYSTEM_TABLE* ST, const PciDevice* dev);

} // namespace pci
} // namespace guideXOS
