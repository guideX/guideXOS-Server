// NVMe Storage Driver — Implementation
//
// Scans PCI for NVMe controllers (class 01/08/02), maps BAR0,
// sets up admin and I/O queue pairs, identifies namespaces, and
// registers them with the block device layer.
//
// Uses MMIO for register access, which works on any architecture
// that exposes PCI.  On architectures without PCI the driver is
// a no-op.
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/kernel/nvme.h"
#include "include/kernel/block_device.h"
#include "include/kernel/arch.h"

namespace kernel {
namespace nvme {

// ================================================================
// Internal state
// ================================================================

static NVMeDevice s_devices[MAX_NVME_DEVICES];
static uint8_t    s_deviceCount = 0;

// ================================================================
// Helpers
// ================================================================

static void memzero(void* dst, uint32_t len)
{
    uint8_t* p = static_cast<uint8_t*>(dst);
    for (uint32_t i = 0; i < len; ++i) p[i] = 0;
}

static void memcopy(void* dst, const void* src, uint32_t len)
{
    uint8_t* d = static_cast<uint8_t*>(dst);
    const uint8_t* s = static_cast<const uint8_t*>(src);
    for (uint32_t i = 0; i < len; ++i) d[i] = s[i];
}

static void fix_nvme_string(char* str, uint32_t len)
{
    // Trim trailing spaces
    int end = static_cast<int>(len) - 1;
    while (end >= 0 && (str[end] == ' ' || str[end] == '\0')) {
        str[end--] = '\0';
    }
}

// ================================================================
// MMIO register access — architecture abstracted
//
// On x86/amd64 we use volatile pointer dereferences (works in
// freestanding C++).  On SPARC64/IA-64, arch::mmio_readXX helpers
// enforce the correct memory barrier semantics.
// ================================================================

#if ARCH_HAS_PORT_IO
// x86 / amd64: simple volatile pointer access suffices (strong ordering)

static uint32_t nvme_read32(uint64_t base, uint32_t offset)
{
    volatile uint32_t* reg = reinterpret_cast<volatile uint32_t*>(
        static_cast<uintptr_t>(base + offset));
    return *reg;
}

static void nvme_write32(uint64_t base, uint32_t offset, uint32_t val)
{
    volatile uint32_t* reg = reinterpret_cast<volatile uint32_t*>(
        static_cast<uintptr_t>(base + offset));
    *reg = val;
}

static uint64_t nvme_read64(uint64_t base, uint32_t offset)
{
    // NVMe spec allows two 32-bit reads for 64-bit registers
    uint32_t lo = nvme_read32(base, offset);
    uint32_t hi = nvme_read32(base, offset + 4);
    return static_cast<uint64_t>(hi) << 32 | lo;
}

static void nvme_write64(uint64_t base, uint32_t offset, uint64_t val)
{
    nvme_write32(base, offset,     static_cast<uint32_t>(val));
    nvme_write32(base, offset + 4, static_cast<uint32_t>(val >> 32));
}

#elif defined(ARCH_SPARC64)

static uint32_t nvme_read32(uint64_t base, uint32_t offset)
{
    return arch::sparc64::mmio_read32(base + offset);
}

static void nvme_write32(uint64_t base, uint32_t offset, uint32_t val)
{
    arch::sparc64::mmio_write32(base + offset, val);
}

static uint64_t nvme_read64(uint64_t base, uint32_t offset)
{
    return arch::sparc64::mmio_read64(base + offset);
}

static void nvme_write64(uint64_t base, uint32_t offset, uint64_t val)
{
    arch::sparc64::mmio_write64(base + offset, val);
}

#elif defined(ARCH_IA64)

// IA-64: use volatile pointer access with compiler barrier
static uint32_t nvme_read32(uint64_t base, uint32_t offset)
{
    volatile uint32_t* reg = reinterpret_cast<volatile uint32_t*>(base + offset);
    return *reg;
}

static void nvme_write32(uint64_t base, uint32_t offset, uint32_t val)
{
    volatile uint32_t* reg = reinterpret_cast<volatile uint32_t*>(base + offset);
    *reg = val;
}

static uint64_t nvme_read64(uint64_t base, uint32_t offset)
{
    volatile uint64_t* reg = reinterpret_cast<volatile uint64_t*>(base + offset);
    return *reg;
}

static void nvme_write64(uint64_t base, uint32_t offset, uint64_t val)
{
    volatile uint64_t* reg = reinterpret_cast<volatile uint64_t*>(base + offset);
    *reg = val;
}

#else
// Stub for architectures without PCI MMIO (SPARC v8, ARM)
static uint32_t nvme_read32(uint64_t, uint32_t)  { return 0; }
static void     nvme_write32(uint64_t, uint32_t, uint32_t) {}
static uint64_t nvme_read64(uint64_t, uint32_t)  { return 0; }
static void     nvme_write64(uint64_t, uint32_t, uint64_t) {}
#endif

// ================================================================
// PCI configuration access (port I/O — x86/amd64)
// For MMIO-based PCI (SPARC64, IA64) a separate accessor is used.
// ================================================================

#if ARCH_HAS_PORT_IO

static const uint16_t PCI_CONFIG_ADDR = 0x0CF8;
static const uint16_t PCI_CONFIG_DATA = 0x0CFC;

static uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset)
{
    uint32_t addr = 0x80000000u |
                    (static_cast<uint32_t>(bus)  << 16) |
                    (static_cast<uint32_t>(dev)  << 11) |
                    (static_cast<uint32_t>(func) << 8)  |
                    (offset & 0xFC);
    arch::outl(PCI_CONFIG_ADDR, addr);
    return arch::inl(PCI_CONFIG_DATA);
}

static void pci_write32(uint8_t bus, uint8_t dev, uint8_t func,
                         uint8_t offset, uint32_t val)
{
    uint32_t addr = 0x80000000u |
                    (static_cast<uint32_t>(bus)  << 16) |
                    (static_cast<uint32_t>(dev)  << 11) |
                    (static_cast<uint32_t>(func) << 8)  |
                    (offset & 0xFC);
    arch::outl(PCI_CONFIG_ADDR, addr);
    arch::outl(PCI_CONFIG_DATA, val);
}

#endif // ARCH_HAS_PORT_IO

// ================================================================
// Static queue memory (must be page-aligned)
//
// For simplicity we allocate a single admin SQ/CQ and a single
// I/O SQ/CQ statically.  A production driver would use dynamic
// memory allocation.
// ================================================================

alignas(4096) static SubmissionEntry   s_adminSQ[NVME_QUEUE_DEPTH];
alignas(4096) static CompletionEntry   s_adminCQ[NVME_QUEUE_DEPTH];
alignas(4096) static SubmissionEntry   s_ioSQ[NVME_QUEUE_DEPTH];
alignas(4096) static CompletionEntry   s_ioCQ[NVME_QUEUE_DEPTH];
alignas(4096) static uint8_t           s_identBuf[4096];

// Admin queue state
static uint16_t s_adminSQTail  = 0;
static uint16_t s_adminCQHead  = 0;
static uint8_t  s_adminCQPhase = 1;

// I/O queue state
static uint16_t s_ioSQTail  = 0;
static uint16_t s_ioCQHead  = 0;
static uint8_t  s_ioCQPhase = 1;

// Current controller BAR0
static uint64_t s_bar0   = 0;
static uint32_t s_stride = 0;  // doorbell stride (in bytes)
static uint16_t s_cmdId  = 0;

// ================================================================
// Doorbell helpers
// ================================================================

static void ring_sq_doorbell(uint16_t qid, uint16_t tail)
{
    // Submission Queue y Tail Doorbell offset = 0x1000 + (2y) * stride
    uint32_t offset = 0x1000 + (2 * qid) * s_stride;
    nvme_write32(s_bar0, offset, tail);
}

static void ring_cq_doorbell(uint16_t qid, uint16_t head)
{
    // Completion Queue y Head Doorbell offset = 0x1000 + (2y + 1) * stride
    uint32_t offset = 0x1000 + (2 * qid + 1) * s_stride;
    nvme_write32(s_bar0, offset, head);
}

// ================================================================
// Submit admin command and wait for completion (polling)
// ================================================================

static bool admin_submit_and_wait(SubmissionEntry* cmd, uint32_t timeout)
{
    cmd->commandId = s_cmdId++;
    memcopy(&s_adminSQ[s_adminSQTail], cmd, sizeof(SubmissionEntry));

    s_adminSQTail = static_cast<uint16_t>((s_adminSQTail + 1) % NVME_QUEUE_DEPTH);
    ring_sq_doorbell(0, s_adminSQTail);

    // Poll CQ
    for (uint32_t i = 0; i < timeout; ++i) {
        volatile CompletionEntry* cqe = &s_adminCQ[s_adminCQHead];
        uint16_t rawStatus = cqe->status;
        uint8_t  phase = static_cast<uint8_t>(rawStatus & 1);
        if (phase == s_adminCQPhase) {
            // Check status (bits [15:1])
            uint16_t sc = static_cast<uint16_t>((rawStatus >> 1) & 0x7FFF);

            s_adminCQHead = static_cast<uint16_t>((s_adminCQHead + 1) % NVME_QUEUE_DEPTH);
            if (s_adminCQHead == 0) s_adminCQPhase ^= 1;
            ring_cq_doorbell(0, s_adminCQHead);

            return (sc == 0);
        }
    }
    return false;
}

// ================================================================
// Submit I/O command and wait for completion (polling)
// ================================================================

static bool io_submit_and_wait(SubmissionEntry* cmd, uint32_t timeout)
{
    cmd->commandId = s_cmdId++;
    memcopy(&s_ioSQ[s_ioSQTail], cmd, sizeof(SubmissionEntry));

    s_ioSQTail = static_cast<uint16_t>((s_ioSQTail + 1) % NVME_QUEUE_DEPTH);
    ring_sq_doorbell(1, s_ioSQTail);

    for (uint32_t i = 0; i < timeout; ++i) {
        volatile CompletionEntry* cqe = &s_ioCQ[s_ioCQHead];
        uint16_t rawStatus = cqe->status;
        uint8_t  phase = static_cast<uint8_t>(rawStatus & 1);
        if (phase == s_ioCQPhase) {
            uint16_t sc = static_cast<uint16_t>((rawStatus >> 1) & 0x7FFF);

            s_ioCQHead = static_cast<uint16_t>((s_ioCQHead + 1) % NVME_QUEUE_DEPTH);
            if (s_ioCQHead == 0) s_ioCQPhase ^= 1;
            ring_cq_doorbell(1, s_ioCQHead);

            return (sc == 0);
        }
    }
    return false;
}

// ================================================================
// Block I/O callbacks for block_device layer
// ================================================================

static block::Status nvme_read_sectors(uint8_t devIdx,
                                       uint64_t lba,
                                       uint32_t count,
                                       void* buffer)
{
    if (devIdx >= MAX_NVME_DEVICES || !s_devices[devIdx].active)
        return block::BLOCK_ERR_INVALID;

    NVMeDevice& dev = s_devices[devIdx];

    // For simplicity, transfer one command at a time (up to MDTS)
    uint8_t* buf = static_cast<uint8_t*>(buffer);
    uint32_t remaining = count;
    uint64_t curLBA = lba;

    while (remaining > 0) {
        uint32_t chunk = remaining;
        // Limit by MDTS (if set)
        if (dev.mdts > 0) {
            uint32_t maxSectors = (1u << dev.mdts) * (4096u / dev.sectorSize);
            if (chunk > maxSectors) chunk = maxSectors;
        }
        if (chunk > 65536) chunk = 65536;

        SubmissionEntry cmd;
        memzero(&cmd, sizeof(cmd));
        cmd.opcode = NVME_IO_READ;
        cmd.nsid   = dev.nsid;
        cmd.prp1   = reinterpret_cast<uintptr_t>(buf);
        cmd.prp2   = 0; // TODO: PRP list for multi-page transfers
        cmd.cdw10  = static_cast<uint32_t>(curLBA);
        cmd.cdw11  = static_cast<uint32_t>(curLBA >> 32);
        cmd.cdw12  = (chunk - 1); // 0-based count

        if (!io_submit_and_wait(&cmd, 1000000))
            return block::BLOCK_ERR_IO;

        buf       += chunk * dev.sectorSize;
        curLBA    += chunk;
        remaining -= chunk;
    }
    return block::BLOCK_OK;
}

static block::Status nvme_write_sectors(uint8_t devIdx,
                                        uint64_t lba,
                                        uint32_t count,
                                        const void* buffer)
{
    if (devIdx >= MAX_NVME_DEVICES || !s_devices[devIdx].active)
        return block::BLOCK_ERR_INVALID;

    NVMeDevice& dev = s_devices[devIdx];
    const uint8_t* buf = static_cast<const uint8_t*>(buffer);
    uint32_t remaining = count;
    uint64_t curLBA = lba;

    while (remaining > 0) {
        uint32_t chunk = remaining;
        if (dev.mdts > 0) {
            uint32_t maxSectors = (1u << dev.mdts) * (4096u / dev.sectorSize);
            if (chunk > maxSectors) chunk = maxSectors;
        }
        if (chunk > 65536) chunk = 65536;

        SubmissionEntry cmd;
        memzero(&cmd, sizeof(cmd));
        cmd.opcode = NVME_IO_WRITE;
        cmd.nsid   = dev.nsid;
        cmd.prp1   = reinterpret_cast<uintptr_t>(buf);
        cmd.prp2   = 0;
        cmd.cdw10  = static_cast<uint32_t>(curLBA);
        cmd.cdw11  = static_cast<uint32_t>(curLBA >> 32);
        cmd.cdw12  = (chunk - 1);

        if (!io_submit_and_wait(&cmd, 1000000))
            return block::BLOCK_ERR_IO;

        buf       += chunk * dev.sectorSize;
        curLBA    += chunk;
        remaining -= chunk;
    }
    return block::BLOCK_OK;
}

// ================================================================
// Controller initialisation sequence
// ================================================================

static bool init_controller(uint64_t bar0)
{
    s_bar0 = bar0;

    // Read CAP to get doorbell stride
    uint64_t cap = nvme_read64(bar0, NVME_CAP);
    s_stride = 4u << ((cap >> 32) & 0xF); // DSTRD field

    // 1. Disable controller (CC.EN = 0)
    nvme_write32(bar0, NVME_CC, 0);
    for (uint32_t i = 0; i < 500000; ++i) {
        if (!(nvme_read32(bar0, NVME_CSTS) & NVME_CSTS_RDY)) break;
    }

    // 2. Configure admin queues
    memzero(s_adminSQ, sizeof(s_adminSQ));
    memzero(s_adminCQ, sizeof(s_adminCQ));
    s_adminSQTail  = 0;
    s_adminCQHead  = 0;
    s_adminCQPhase = 1;

    nvme_write64(bar0, NVME_ASQ, reinterpret_cast<uintptr_t>(s_adminSQ));
    nvme_write64(bar0, NVME_ACQ, reinterpret_cast<uintptr_t>(s_adminCQ));

    // AQA: ACQS = ASQS = NVME_QUEUE_DEPTH - 1
    uint32_t aqa = (static_cast<uint32_t>(NVME_QUEUE_DEPTH - 1) << 16) |
                   (NVME_QUEUE_DEPTH - 1);
    nvme_write32(bar0, NVME_AQA, aqa);

    // 3. Enable controller
    uint32_t cc = NVME_CC_EN | NVME_CC_CSS_NVM | NVME_CC_MPS_4K |
                  NVME_CC_AMS_RR | NVME_CC_IOSQES | NVME_CC_IOCQES;
    nvme_write32(bar0, NVME_CC, cc);

    // Wait for RDY
    for (uint32_t i = 0; i < 1000000; ++i) {
        uint32_t csts = nvme_read32(bar0, NVME_CSTS);
        if (csts & NVME_CSTS_CFS) return false;
        if (csts & NVME_CSTS_RDY) return true;
    }
    return false;
}

static bool create_io_queues()
{
    memzero(s_ioCQ, sizeof(s_ioCQ));
    memzero(s_ioSQ, sizeof(s_ioSQ));
    s_ioSQTail  = 0;
    s_ioCQHead  = 0;
    s_ioCQPhase = 1;

    // Create I/O Completion Queue (QID=1)
    {
        SubmissionEntry cmd;
        memzero(&cmd, sizeof(cmd));
        cmd.opcode = NVME_ADM_CREATE_IOCQ;
        cmd.prp1   = reinterpret_cast<uintptr_t>(s_ioCQ);
        cmd.cdw10  = ((static_cast<uint32_t>(NVME_QUEUE_DEPTH - 1)) << 16) | 1; // QID=1
        cmd.cdw11  = 0x01; // physically contiguous
        if (!admin_submit_and_wait(&cmd, 500000)) return false;
    }

    // Create I/O Submission Queue (QID=1, CQID=1)
    {
        SubmissionEntry cmd;
        memzero(&cmd, sizeof(cmd));
        cmd.opcode = NVME_ADM_CREATE_IOSQ;
        cmd.prp1   = reinterpret_cast<uintptr_t>(s_ioSQ);
        cmd.cdw10  = ((static_cast<uint32_t>(NVME_QUEUE_DEPTH - 1)) << 16) | 1; // QID=1
        cmd.cdw11  = (1u << 16) | 0x01; // CQID=1, physically contiguous
        if (!admin_submit_and_wait(&cmd, 500000)) return false;
    }

    return true;
}

static bool identify_controller()
{
    SubmissionEntry cmd;
    memzero(&cmd, sizeof(cmd));
    cmd.opcode = NVME_ADM_IDENTIFY;
    cmd.prp1   = reinterpret_cast<uintptr_t>(s_identBuf);
    cmd.cdw10  = 1; // CNS = 1 ? Identify Controller

    return admin_submit_and_wait(&cmd, 500000);
}

static bool identify_namespace(uint32_t nsid)
{
    SubmissionEntry cmd;
    memzero(&cmd, sizeof(cmd));
    cmd.opcode = NVME_ADM_IDENTIFY;
    cmd.nsid   = nsid;
    cmd.prp1   = reinterpret_cast<uintptr_t>(s_identBuf);
    cmd.cdw10  = 0; // CNS = 0 ? Identify Namespace

    return admin_submit_and_wait(&cmd, 500000);
}

// ================================================================
// PCI scan and registration
// ================================================================

#if ARCH_HAS_PORT_IO

static void scan_pci_nvme()
{
    for (uint16_t bus = 0; bus < 256; ++bus) {
        for (uint8_t dev = 0; dev < 32; ++dev) {
            for (uint8_t func = 0; func < 8; ++func) {
                if (s_deviceCount >= MAX_NVME_DEVICES) return;

                uint32_t id = pci_read32(static_cast<uint8_t>(bus), dev, func, 0);
                if (id == 0xFFFFFFFF) continue;

                uint32_t classReg = pci_read32(static_cast<uint8_t>(bus), dev, func, 0x08);
                uint8_t baseClass = static_cast<uint8_t>(classReg >> 24);
                uint8_t subClass  = static_cast<uint8_t>(classReg >> 16);
                uint8_t progIf    = static_cast<uint8_t>(classReg >> 8);

                // NVMe: class 01, subclass 08, progIF 02
                if (baseClass != 0x01 || subClass != 0x08 || progIf != 0x02)
                    continue;

                // Read BAR0 (64-bit MMIO)
                uint32_t bar0Lo = pci_read32(static_cast<uint8_t>(bus), dev, func, 0x10);
                uint32_t bar0Hi = pci_read32(static_cast<uint8_t>(bus), dev, func, 0x14);
                uint64_t bar0 = (static_cast<uint64_t>(bar0Hi) << 32) |
                                (bar0Lo & 0xFFFFFFF0u);
                if (bar0 == 0) continue;

                // Enable bus mastering and memory space
                uint32_t cmdSts = pci_read32(static_cast<uint8_t>(bus), dev, func, 0x04);
                cmdSts |= 0x06; // Memory Space + Bus Master
                pci_write32(static_cast<uint8_t>(bus), dev, func, 0x04, cmdSts);

                // Initialize controller
                if (!init_controller(bar0)) continue;
                if (!identify_controller()) continue;

                IdentifyController* ctrl = reinterpret_cast<IdentifyController*>(s_identBuf);

                if (!create_io_queues()) continue;

                // Identify namespace 1
                if (!identify_namespace(1)) continue;

                IdentifyNamespace* ns = reinterpret_cast<IdentifyNamespace*>(s_identBuf);

                uint8_t flbas = ns->flbas & 0x0F;
                uint32_t sectorSize = 1u << ns->lbaFormats[flbas].lbads;
                if (sectorSize == 0) sectorSize = 512;

                NVMeDevice& ndev = s_devices[s_deviceCount];
                memzero(&ndev, sizeof(ndev));
                ndev.active       = true;
                ndev.bar0         = bar0;
                ndev.nsid         = 1;
                ndev.totalSectors = ns->nsze;
                ndev.sectorSize   = sectorSize;
                ndev.mdts         = ctrl->mdts;

                memcopy(ndev.model, ctrl->mn, 40);
                ndev.model[40] = '\0';
                fix_nvme_string(ndev.model, 40);

                memcopy(ndev.serial, ctrl->sn, 20);
                ndev.serial[20] = '\0';
                fix_nvme_string(ndev.serial, 20);

                // Register with block layer
                block::BlockDevice bdev;
                memzero(&bdev, sizeof(bdev));
                bdev.active       = true;
                bdev.type         = block::BDEV_NVME;
                bdev.driverIndex  = s_deviceCount;
                bdev.totalSectors = ndev.totalSectors;
                bdev.sectorSize   = ndev.sectorSize;
                bdev.readFn       = nvme_read_sectors;
                bdev.writeFn      = nvme_write_sectors;

                // Name: "nvme0n1"
                bdev.name[0] = 'n'; bdev.name[1] = 'v';
                bdev.name[2] = 'm'; bdev.name[3] = 'e';
                bdev.name[4] = static_cast<char>('0' + s_deviceCount);
                bdev.name[5] = 'n'; bdev.name[6] = '1';
                bdev.name[7] = '\0';

                block::register_device(bdev);
                ++s_deviceCount;
            }
        }
    }
}

#endif // ARCH_HAS_PORT_IO

// ================================================================
// Public API
// ================================================================

void init()
{
    memzero(s_devices, sizeof(s_devices));
    s_deviceCount = 0;

#if ARCH_HAS_PORT_IO
    scan_pci_nvme();
#endif

    // TODO: MMIO-based PCI scanning for SPARC64 and IA-64
    // would be added here using arch-specific PCI config space
    // access routines (psycho/sabre on SPARC64, SAL on IA-64).
}

uint8_t device_count()
{
    return s_deviceCount;
}

const NVMeDevice* get_device(uint8_t index)
{
    if (index >= MAX_NVME_DEVICES) return nullptr;
    if (!s_devices[index].active) return nullptr;
    return &s_devices[index];
}

} // namespace nvme
} // namespace kernel
