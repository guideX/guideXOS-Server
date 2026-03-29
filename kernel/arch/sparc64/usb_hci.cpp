// SPARC v9 OHCI Host Controller Interface — Implementation
//
// Provides the platform-specific USB HCI backend for SPARC64 (Sun4u).
// Implements the kernel::usb::hci namespace functions declared in usb.h.
//
// On Sun4u, USB controllers are PCI devices.  The OHCI controller
// MMIO base is obtained from the PCI BAR.  QEMU sun4u provides a
// USB OHCI controller on the PCI bus.
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/arch/usb_hci.h"
#include "include/arch/sparc64.h"
#include <kernel/usb.h>

namespace kernel {
namespace usb {
namespace hci {

namespace {

// ================================================================
// OHCI register offsets (same as SPARC v8 OHCI)
// ================================================================

static const uint32_t OHCI_REVISION     = 0x00;
static const uint32_t OHCI_CONTROL      = 0x04;
static const uint32_t OHCI_CMDSTATUS    = 0x08;
static const uint32_t OHCI_INTRSTATUS   = 0x0C;
static const uint32_t OHCI_INTRENABLE   = 0x10;
static const uint32_t OHCI_REG_HCCA     = 0x18;
static const uint32_t OHCI_CTRL_HEAD    = 0x20;
static const uint32_t OHCI_BULK_HEAD    = 0x28;
static const uint32_t OHCI_FMINTERVAL   = 0x34;
static const uint32_t OHCI_PERIODICSTART = 0x40;
static const uint32_t OHCI_LSTHRESHOLD  = 0x44;
static const uint32_t OHCI_RHDESC_A     = 0x48;
static const uint32_t OHCI_RHSTATUS     = 0x50;
static const uint32_t OHCI_RHPORTSTATUS = 0x54;

// Control register bits
static const uint32_t OHCI_CTRL_HCFS_OPER = (2u << 6);
static const uint32_t OHCI_CTRL_CLE       = (1u << 4);
static const uint32_t OHCI_CTRL_BLE       = (1u << 5);
static const uint32_t OHCI_CTRL_PLE       = (1u << 2);

static const uint32_t OHCI_CMD_HCR = (1u << 0);
static const uint32_t OHCI_CMD_CLF = (1u << 1);
static const uint32_t OHCI_CMD_BLF = (1u << 2);

// Root hub port status bits
static const uint32_t OHCI_PORT_CCS  = (1u << 0);
static const uint32_t OHCI_PORT_PES  = (1u << 1);
static const uint32_t OHCI_PORT_PRS  = (1u << 4);
static const uint32_t OHCI_PORT_PPS  = (1u << 8);
static const uint32_t OHCI_PORT_LSDA = (1u << 9);
static const uint32_t OHCI_PORT_CSC  = (1u << 16);
static const uint32_t OHCI_PORT_PRSC = (1u << 20);

// ================================================================
// OHCI data structures (32-bit DMA-addressable)
// ================================================================

struct alignas(16) OHCI_ED {
    uint32_t control;
    uint32_t tailTD;
    uint32_t headTD;
    uint32_t nextED;
};

struct alignas(16) OHCI_TD {
    uint32_t control;
    uint32_t cbp;
    uint32_t nextTD;
    uint32_t be;
};

struct alignas(256) OHCI_HCCA {
    uint32_t interruptTable[32];
    uint16_t frameNumber;
    uint16_t pad1;
    uint32_t doneHead;
    uint8_t  reserved[120];
};

// ================================================================
// Internal state
// ================================================================

static bool     s_available = false;
static uint64_t s_ohciBase  = 0;
static uint8_t  s_numPorts  = 0;

alignas(256) static OHCI_HCCA s_hcca;
alignas(16)  static OHCI_ED   s_ctrlED;
alignas(16)  static OHCI_ED   s_bulkED;
alignas(16)  static OHCI_TD   s_tds[8];

// ================================================================
// MMIO helpers (64-bit address, 32-bit register access)
// ================================================================

static uint32_t ohci_read(uint32_t reg)
{
    return arch::sparc64::mmio_read32(s_ohciBase + reg);
}

static void ohci_write(uint32_t reg, uint32_t val)
{
    arch::sparc64::mmio_write32(s_ohciBase + reg, val);
}

static void delay_ms(uint32_t ms)
{
    for (volatile uint32_t i = 0; i < ms * 5000; ++i) {}
}

static void memzero(void* dst, uint32_t len)
{
    uint8_t* p = static_cast<uint8_t*>(dst);
    for (uint32_t i = 0; i < len; ++i) p[i] = 0;
}

// 64-bit pointer to 32-bit for DMA structures
static uint32_t ptr32(const void* p)
{
    return static_cast<uint32_t>(reinterpret_cast<uint64_t>(p));
}

// ================================================================
// PCI configuration space access for Sun4u
//
// On QEMU sun4u the PCI host bridge (psycho/sabre) maps config space
// at 0x1FE01000000.  We use Type 0 config transactions.
// ================================================================

static const uint64_t PCI_CFG_BASE = 0x1FE01000000ULL;

static uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset)
{
    uint64_t addr = PCI_CFG_BASE |
                    (static_cast<uint64_t>(bus) << 16) |
                    (static_cast<uint64_t>(dev) << 11) |
                    (static_cast<uint64_t>(func) << 8) |
                    (offset & 0xFC);
    return arch::sparc64::mmio_read32(addr);
}

// ================================================================
// Scan PCI for OHCI controller (class 0C/03/10)
// ================================================================

static bool find_ohci_controller()
{
    for (uint16_t bus = 0; bus < 4; ++bus) { // Sun4u typically has few buses
        for (uint8_t dev = 0; dev < 32; ++dev) {
            for (uint8_t func = 0; func < 8; ++func) {
                uint32_t id = pci_read32(static_cast<uint8_t>(bus), dev, func, 0);
                if (id == 0xFFFFFFFF || id == 0) continue;

                uint32_t classReg = pci_read32(static_cast<uint8_t>(bus), dev, func, 0x08);
                uint8_t baseClass = static_cast<uint8_t>(classReg >> 24);
                uint8_t subClass  = static_cast<uint8_t>(classReg >> 16);
                uint8_t progIf    = static_cast<uint8_t>(classReg >> 8);

                if (baseClass == 0x0C && subClass == 0x03 && progIf == 0x10) {
                    // OHCI — read BAR0 (MMIO base)
                    uint32_t bar0 = pci_read32(static_cast<uint8_t>(bus), dev, func, 0x10);
                    if (!(bar0 & 0x01)) { // memory-mapped
                        s_ohciBase = bar0 & 0xFFFFF000u;
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

// ================================================================
// Wait for TD completion
// ================================================================

static TransferStatus wait_td(volatile OHCI_TD* td, uint32_t timeout_loops)
{
    for (uint32_t i = 0; i < timeout_loops; ++i) {
        uint32_t cc = (td->control >> 28) & 0x0F;
        if (cc != 0x0F) {
            switch (cc) {
                case 0: return XFER_SUCCESS;
                case 4: return XFER_STALL;
                case 5: return XFER_TIMEOUT;
                case 8: return XFER_DATA_OVERRUN;
                case 9: return XFER_DATA_UNDERRUN;
                default: return XFER_ERROR;
            }
        }
    }
    return XFER_TIMEOUT;
}

} // anonymous namespace

// ================================================================
// HCI interface implementation
// ================================================================

bool init()
{
    s_available = false;

    if (!find_ohci_controller()) return false;

    // Reset
    ohci_write(OHCI_CMDSTATUS, OHCI_CMD_HCR);
    delay_ms(10);

    for (int i = 0; i < 100; ++i) {
        if (!(ohci_read(OHCI_CMDSTATUS) & OHCI_CMD_HCR)) break;
        delay_ms(1);
    }

    // HCCA
    memzero(&s_hcca, sizeof(s_hcca));
    ohci_write(OHCI_REG_HCCA, ptr32(&s_hcca));

    // Frame interval
    ohci_write(OHCI_FMINTERVAL, (0x2778u << 16) | 0x2EDFu);
    ohci_write(OHCI_PERIODICSTART, 0x2A2Fu);

    // ED lists
    memzero(&s_ctrlED, sizeof(s_ctrlED));
    memzero(&s_bulkED, sizeof(s_bulkED));
    ohci_write(OHCI_CTRL_HEAD, ptr32(&s_ctrlED));
    ohci_write(OHCI_BULK_HEAD, ptr32(&s_bulkED));

    // Operational
    ohci_write(OHCI_CONTROL, OHCI_CTRL_HCFS_OPER | OHCI_CTRL_CLE |
                              OHCI_CTRL_BLE | OHCI_CTRL_PLE);

    // Port count
    uint32_t rhdescA = ohci_read(OHCI_RHDESC_A);
    s_numPorts = static_cast<uint8_t>(rhdescA & 0xFF);
    if (s_numPorts > 15) s_numPorts = 15;

    // Power on ports
    for (uint8_t p = 0; p < s_numPorts; ++p) {
        ohci_write(OHCI_RHPORTSTATUS + p * 4, OHCI_PORT_PPS);
    }
    delay_ms(100);

    s_available = true;
    return true;
}

bool is_available()
{
    return s_available;
}

DeviceSpeed port_reset(uint8_t port)
{
    if (port >= s_numPorts) return SPEED_LOW;

    uint32_t portReg = OHCI_RHPORTSTATUS + port * 4;
    ohci_write(portReg, OHCI_PORT_PRS);
    delay_ms(50);

    for (int i = 0; i < 100; ++i) {
        if (ohci_read(portReg) & OHCI_PORT_PRSC) break;
        delay_ms(1);
    }
    ohci_write(portReg, OHCI_PORT_PRSC);
    delay_ms(10);

    uint32_t st = ohci_read(portReg);
    return (st & OHCI_PORT_LSDA) ? SPEED_LOW : SPEED_FULL;
}

uint8_t port_count()
{
    return s_numPorts;
}

bool port_connected(uint8_t port)
{
    if (port >= s_numPorts) return false;
    return (ohci_read(OHCI_RHPORTSTATUS + port * 4) & OHCI_PORT_CCS) != 0;
}

TransferStatus control_transfer(uint8_t deviceAddr,
                                const SetupPacket* setup,
                                void* data,
                                uint16_t dataLen)
{
    if (!s_available) return XFER_NOT_SUPPORTED;

    memzero(s_tds, sizeof(s_tds));

    OHCI_TD* setupTD = &s_tds[0];
    setupTD->control = (0x0F << 28) | (0 << 19) | (2 << 24);
    setupTD->cbp     = ptr32(setup);
    setupTD->be      = ptr32(setup) + 7;
    setupTD->nextTD  = ptr32(&s_tds[1]);

    uint8_t tdIdx = 1;
    bool dirIn = (setup->bmRequestType & 0x80) != 0;

    if (dataLen > 0 && data != nullptr) {
        uint8_t* ptr = static_cast<uint8_t*>(data);
        uint16_t remaining = dataLen;
        uint8_t toggle = 1;

        while (remaining > 0 && tdIdx < 6) {
            uint16_t chunk = (remaining > 64) ? 64 : remaining;
            OHCI_TD* td = &s_tds[tdIdx];

            uint32_t dp = dirIn ? 2 : 1;
            td->control = (0x0F << 28) | (dp << 19) |
                          (static_cast<uint32_t>(toggle + 2) << 24);
            td->cbp     = ptr32(ptr);
            td->be      = ptr32(ptr) + chunk - 1;
            td->nextTD  = ptr32(&s_tds[tdIdx + 1]);

            toggle ^= 1;
            ptr += chunk;
            remaining -= chunk;
            tdIdx++;
        }
    }

    OHCI_TD* statusTD = &s_tds[tdIdx];
    uint32_t statusDP = dirIn ? 1 : 2;
    statusTD->control = (0x0F << 28) | (statusDP << 19) | (3 << 24);
    statusTD->cbp     = 0;
    statusTD->be      = 0;
    statusTD->nextTD  = 0;
    tdIdx++;

    memzero(&s_ctrlED, sizeof(s_ctrlED));
    s_ctrlED.control = (static_cast<uint32_t>(deviceAddr) & 0x7F) | (64u << 16);
    s_ctrlED.headTD  = ptr32(&s_tds[0]);
    s_ctrlED.tailTD  = ptr32(&s_tds[tdIdx]);
    s_ctrlED.nextED  = 0;

    ohci_write(OHCI_CTRL_HEAD, ptr32(&s_ctrlED));
    ohci_write(OHCI_CMDSTATUS, OHCI_CMD_CLF);

    for (uint8_t i = 0; i < tdIdx; ++i) {
        TransferStatus st = wait_td(&s_tds[i], 1000000);
        if (st != XFER_SUCCESS) return st;
    }

    return XFER_SUCCESS;
}

TransferStatus bulk_transfer(uint8_t deviceAddr,
                             uint8_t endpointAddr,
                             void* data,
                             uint16_t dataLen,
                             uint16_t* bytesTransferred)
{
    if (!s_available) return XFER_NOT_SUPPORTED;

    uint8_t ep    = endpointAddr & 0x0F;
    bool    dirIn = (endpointAddr & 0x80) != 0;
    uint32_t dp   = dirIn ? 2 : 1;

    uint8_t* ptr = static_cast<uint8_t*>(data);
    uint16_t remaining = dataLen;
    uint16_t transferred = 0;

    while (remaining > 0) {
        uint16_t chunk = (remaining > 64) ? 64 : remaining;

        memzero(&s_tds[0], sizeof(OHCI_TD));
        s_tds[0].control = (0x0F << 28) | (dp << 19);
        s_tds[0].cbp     = ptr32(ptr);
        s_tds[0].be      = ptr32(ptr) + chunk - 1;
        s_tds[0].nextTD  = 0;

        memzero(&s_bulkED, sizeof(OHCI_ED));
        s_bulkED.control = (static_cast<uint32_t>(deviceAddr) & 0x7F) |
                           (static_cast<uint32_t>(ep) << 7) |
                           (dirIn ? (2u << 11) : (1u << 11)) |
                           (64u << 16);
        s_bulkED.headTD  = ptr32(&s_tds[0]);
        s_bulkED.tailTD  = ptr32(&s_tds[1]);
        s_bulkED.nextED  = 0;

        ohci_write(OHCI_BULK_HEAD, ptr32(&s_bulkED));
        ohci_write(OHCI_CMDSTATUS, OHCI_CMD_BLF);

        TransferStatus st = wait_td(&s_tds[0], 500000);
        if (st != XFER_SUCCESS) {
            if (bytesTransferred) *bytesTransferred = transferred;
            return st;
        }

        transferred += chunk;
        ptr += chunk;
        remaining -= chunk;
    }

    if (bytesTransferred) *bytesTransferred = transferred;
    return XFER_SUCCESS;
}

TransferStatus interrupt_transfer(uint8_t deviceAddr,
                                  uint8_t endpointAddr,
                                  void* data,
                                  uint16_t dataLen,
                                  uint16_t* bytesTransferred)
{
    return bulk_transfer(deviceAddr, endpointAddr, data,
                         dataLen, bytesTransferred);
}

} // namespace hci
} // namespace usb
} // namespace kernel

namespace kernel {
namespace arch {
namespace sparc64 {
namespace usb_hci {

bool init()         { return kernel::usb::hci::init(); }
bool is_available() { return kernel::usb::hci::is_available(); }

} // namespace usb_hci
} // namespace sparc64
} // namespace arch
} // namespace kernel
