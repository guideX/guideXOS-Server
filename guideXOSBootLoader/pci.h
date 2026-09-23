// PCI Enumeration for guideXOS Bootloader
//
// Enumerates PCI devices to find network controllers and
// read their BAR0 (MMIO base address) for kernel use.
//
// Uses x86 I/O port access (0xCF8/0xCFC) method.
//
// Copyright (c) 2026 guideXOS Server
//

#pragma once

#include "Uefi.h"
#include <stdint.h>

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

// Multimedia / audio class codes (MC6: HDA controller discovery so the
// bootloader can identity-map the audio MMIO BAR for the kernel driver).
static const uint8_t PCI_CLASS_MULTIMEDIA = 0x04;
static const uint8_t PCI_SUBCLASS_HDA     = 0x03;

// Intel vendor and device IDs
static const uint16_t PCI_VENDOR_INTEL    = 0x8086;
static const uint16_t PCI_DEVICE_E1000    = 0x100E;  // 82540EM (QEMU default)
static const uint16_t PCI_DEVICE_E1000E   = 0x10D3;  // 82574L
static const uint16_t PCI_DEVICE_I217     = 0x153A;  // I217-LM

// Maximum devices to track
static const uint8_t MAX_PCI_DEVICES = 4;

// ================================================================
// PCI Device Information
// ================================================================

struct PciDevice {
    uint8_t  bus;
    uint8_t  device;
    uint8_t  function;
    uint16_t vendorId;
    uint16_t deviceId;
    uint8_t  classCode;
    uint8_t  subclass;
    uint8_t  progIf;
    uint8_t  irqLine;
    uint64_t bar0Phys;      // BAR0 physical address
    uint64_t bar0Size;      // BAR0 size
    uint64_t bar0Virt;      // BAR0 virtual address (after mapping)
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
    uint64_t mmioPhys;      // Physical BAR0 address
    uint64_t mmioVirt;      // Virtual address (mapped by bootloader)
    uint64_t mmioSize;      // Size of MMIO region
    uint8_t  macAddress[6]; // Placeholder MAC (kernel reads actual)
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

// Get BAR0 physical address and size
// Returns true if BAR0 is a valid memory BAR
bool GetBar0Info(uint8_t bus, uint8_t dev, uint8_t func,
                 uint64_t* outPhys, uint64_t* outSize, bool* outIs64bit);

// Enable PCI bus mastering and memory space access
void EnablePciDevice(uint8_t bus, uint8_t dev, uint8_t func);

// ================================================================
// HDA Audio Controller Info (for MMIO mapping; MC6)
// ================================================================

// Minimal discovery record for an Intel HDA controller (PCI class 0x04,
// subclass 0x03). The kernel re-discovers the controller with its own PCI
// scan; the bootloader only needs the BAR0 range for identity mapping, so
// (unlike the NIC) there is deliberately no BootInfo coupling here.
struct HdaBootInfo {
    uint16_t vendorId;
    uint16_t deviceId;
    uint8_t  bus;
    uint8_t  device;
    uint8_t  function;
    uint64_t bar0Phys;      // BAR0 physical address (MMIO)
    uint64_t bar0Size;      // BAR0 size (min 16 KiB per HDA spec)
    bool     found;
};

// Find the first HDA audio controller with a memory BAR0.
// Returns true and fills *out on success. Does not enable the device;
// the caller enables bus mastering like the NIC path.
bool FindHdaController(HdaBootInfo* out);

// Check if device is a supported NIC
bool IsSupportedNic(uint16_t vendorId, uint16_t deviceId);

// Print PCI device info (for debugging)
void PrintPciDevice(EFI_SYSTEM_TABLE* ST, const PciDevice* dev);

} // namespace pci
} // namespace guideXOS
