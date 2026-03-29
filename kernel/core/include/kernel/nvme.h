// NVMe Storage Driver
//
// Supports:
//   - NVMe 1.0+ over PCIe (MMIO-based)
//   - Admin queue: IDENTIFY CONTROLLER, IDENTIFY NAMESPACE
//   - I/O queue:   READ, WRITE
//   - Single I/O submission / completion queue pair
//
// Functional on architectures with PCI MMIO access:
//   x86, amd64, ia64, sparc64
// Stub-only on architectures without PCI:
//   sparc (SBus only), arm (no PCI in current board model)
//
// Reference: NVM Express Base Specification 1.4
//
// Copyright (c) 2026 guideXOS Server
//

#ifndef KERNEL_NVME_H
#define KERNEL_NVME_H

#include "kernel/types.h"
#include "kernel/block_device.h"

namespace kernel {
namespace nvme {

// ================================================================
// NVMe controller register offsets (from BAR0)
// ================================================================

static const uint32_t NVME_CAP    = 0x00;  // Controller Capabilities (64-bit)
static const uint32_t NVME_VS     = 0x08;  // Version
static const uint32_t NVME_INTMS  = 0x0C;  // Interrupt Mask Set
static const uint32_t NVME_INTMC  = 0x10;  // Interrupt Mask Clear
static const uint32_t NVME_CC     = 0x14;  // Controller Configuration
static const uint32_t NVME_CSTS   = 0x1C;  // Controller Status
static const uint32_t NVME_AQA    = 0x24;  // Admin Queue Attributes
static const uint32_t NVME_ASQ    = 0x28;  // Admin Submission Queue Base (64-bit)
static const uint32_t NVME_ACQ    = 0x30;  // Admin Completion Queue Base (64-bit)

// CC bits
static const uint32_t NVME_CC_EN      = 0x00000001;
static const uint32_t NVME_CC_CSS_NVM = 0x00000000; // NVM Command Set
static const uint32_t NVME_CC_MPS_4K  = (0 << 7);   // Memory Page Size = 4 KiB
static const uint32_t NVME_CC_AMS_RR  = (0 << 11);  // Round Robin arbitration
static const uint32_t NVME_CC_IOSQES  = (6 << 16);  // I/O SQ entry size = 64
static const uint32_t NVME_CC_IOCQES  = (4 << 20);  // I/O CQ entry size = 16

// CSTS bits
static const uint32_t NVME_CSTS_RDY  = 0x00000001;
static const uint32_t NVME_CSTS_CFS  = 0x00000002;

// ================================================================
// NVMe command opcodes
// ================================================================

// Admin opcodes
static const uint8_t NVME_ADM_IDENTIFY     = 0x06;
static const uint8_t NVME_ADM_CREATE_IOSQ  = 0x01;
static const uint8_t NVME_ADM_CREATE_IOCQ  = 0x05;

// I/O opcodes
static const uint8_t NVME_IO_READ  = 0x02;
static const uint8_t NVME_IO_WRITE = 0x01;

// ================================================================
// Submission Queue Entry (64 bytes)
// ================================================================

#if defined(__GNUC__) || defined(__clang__)
#define NVME_PACKED __attribute__((packed))
#else
#pragma pack(push, 1)
#define NVME_PACKED
#endif

struct SubmissionEntry {
    uint8_t  opcode;
    uint8_t  flags;
    uint16_t commandId;
    uint32_t nsid;
    uint64_t reserved1;
    uint64_t mptr;
    uint64_t prp1;
    uint64_t prp2;
    uint32_t cdw10;
    uint32_t cdw11;
    uint32_t cdw12;
    uint32_t cdw13;
    uint32_t cdw14;
    uint32_t cdw15;
} NVME_PACKED;

// ================================================================
// Completion Queue Entry (16 bytes)
// ================================================================

struct CompletionEntry {
    uint32_t result;
    uint32_t reserved;
    uint16_t sqHead;
    uint16_t sqId;
    uint16_t commandId;
    uint16_t status;       // bit 0 = phase, bits [15:1] = status
} NVME_PACKED;

#if !defined(__GNUC__) && !defined(__clang__)
#pragma pack(pop)
#endif

#undef NVME_PACKED

// ================================================================
// Identify Controller — selected fields (bytes 0-4095)
// ================================================================

struct IdentifyController {
    uint16_t vid;           // PCI Vendor ID
    uint16_t ssvid;         // PCI Subsystem Vendor ID
    char     sn[20];        // Serial Number
    char     mn[40];        // Model Number
    char     fr[8];         // Firmware Revision
    uint8_t  rab;           // Recommended Arbitration Burst
    uint8_t  ieee[3];       // IEEE OUI
    uint8_t  cmic;
    uint8_t  mdts;          // Maximum Data Transfer Size (2^mdts pages)
    uint8_t  reserved[4014]; // remainder of 4096 bytes
};

// ================================================================
// Identify Namespace — selected fields (bytes 0-4095)
// ================================================================

struct IdentifyNamespace {
    uint64_t nsze;          // Namespace Size (total blocks)
    uint64_t ncap;          // Namespace Capacity
    uint64_t nuse;          // Namespace Utilization
    uint8_t  nsfeat;
    uint8_t  nlbaf;         // Number of LBA Formats (0-based)
    uint8_t  flbas;         // Formatted LBA Size index
    uint8_t  reserved1[101];
    // LBA format descriptors start at offset 128
    // Each is 4 bytes: [RP(2) | LBADS(8) | MS(16)]
    struct {
        uint16_t ms;        // Metadata Size
        uint8_t  lbads;     // LBA Data Size (2^lbads bytes)
        uint8_t  rp;        // Relative Performance
    } lbaFormats[16];
    uint8_t reserved2[3904]; // remainder of 4096 bytes
};

// ================================================================
// NVMe device descriptor
// ================================================================

struct NVMeDevice {
    bool     active;
    uint64_t bar0;          // BAR0 (MMIO base)
    uint32_t nsid;          // namespace ID (usually 1)
    uint64_t totalSectors;
    uint32_t sectorSize;
    uint8_t  mdts;          // maximum data transfer size exponent
    char     model[41];
    char     serial[21];
};

static const uint8_t MAX_NVME_DEVICES = 4;

// Queue depth
static const uint16_t NVME_QUEUE_DEPTH = 64;

// ================================================================
// Public API
// ================================================================

// Scan PCI for NVMe controllers and register discovered namespaces
// with the block device layer.
void init();

// Return the number of discovered NVMe namespaces.
uint8_t device_count();

// Return device info by driver-local index.
const NVMeDevice* get_device(uint8_t index);

} // namespace nvme
} // namespace kernel

#endif // KERNEL_NVME_H
