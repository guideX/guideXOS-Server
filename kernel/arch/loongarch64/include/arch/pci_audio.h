//
// Platform-specific PCI audio initialization for LoongArch 64:
//   - PCI ECAM config space access
//   - MMIO BAR access for HDA (High Definition Audio)
//
// On QEMU loongarch64-virt and real Loongson hardware, PCI devices
// are accessed via ECAM (Enhanced Configuration Access Mechanism)
// with MMIO-based configuration space.
//
// Copyright (c) 2026 guideXOS Server
//

#pragma once

#include <kernel/types.h>

namespace kernel {
namespace arch {
namespace loongarch64 {
namespace pci_audio {

// ================================================================
// PCI ECAM addresses for LoongArch
// ================================================================

// QEMU loongarch64-virt PCI ECAM (check device tree for actual values)
static const uint64_t PCI_ECAM_BASE = 0x20000000ULL;

// PCI MMIO regions
static const uint64_t PCI_MMIO_BASE = 0x40000000ULL;
static const uint64_t PCI_MMIO_SIZE = 0x40000000ULL;

// ================================================================
// HDA (High Definition Audio) definitions
// ================================================================

// HDA PCI class/subclass
static const uint8_t PCI_CLASS_MULTIMEDIA = 0x04;
static const uint8_t PCI_SUBCLASS_HDA     = 0x03;

// HDA vendor/device IDs (Intel HDA as reference)
static const uint16_t HDA_VENDOR_INTEL = 0x8086;

// ================================================================
// Public API
// ================================================================

// Initialize the PCI audio subsystem.
// Scans PCI bus for HDA controllers and initializes the first one found.
// Returns true if an HDA controller was found and initialized.
bool init();

// Check if an HDA controller is available.
bool is_available();

// Get the MMIO base address of the HDA controller.
uint64_t get_hda_mmio_base();

// Get the HDA controller vendor ID.
uint16_t get_vendor_id();

// Get the HDA controller device ID.
uint16_t get_device_id();

} // namespace pci_audio
} // namespace loongarch64
} // namespace arch
} // namespace kernel
