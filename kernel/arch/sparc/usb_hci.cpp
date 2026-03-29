// SPARC v8 OHCI Host Controller Interface — Implementation
//
// Provides the platform-specific USB HCI backend for SPARC v8 (Sun4m).
// Implements the kernel::usb::hci namespace functions declared in usb.h.
//
// OHCI controllers on Sun4m are memory-mapped.  The base address
// depends on the SBus slot and is typically discovered via OpenBoot
// device tree.  For QEMU SS-5, USB is not natively available, but
// this driver provides the framework for SBus USB cards.
//
// OHCI register set is a standard 4 KB MMIO region.
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/arch/usb_hci.h"
#include "include/arch/sparc.h"
#include <kernel/usb.h>

namespace kernel {
namespace usb {
namespace hci {

// ================================================================
// OHCI register offsets
// ================================================================

static const uint32_t OHCI_REVISION     = 0x00;
static const uint32_t OHCI_CONTROL      = 0x04;
static const uint32_t OHCI_CMDSTATUS    = 0x08;
static const uint32_t OHCI_INTRSTATUS   = 0x0C;
static const uint32_t OHCI_INTRENABLE   = 0x10;
static const uint32_t OHCI_INTRDISABLE  = 0x14;
static const uint32_t OHCI_HCCA         = 0x18;
static const uint32_t OHCI_PERIOD_CUR   = 0x1C;
static const uint32_t OHCI_CTRL_HEAD    = 0x20;
static const uint32_t OHCI_CTRL_CUR     = 0x24;
static const uint32_t OHCI_BULK_HEAD    = 0x28;
static const uint32_t OHCI_BULK_CUR     = 0x2C;
static const uint32_t OHCI_DONE_HEAD    = 0x30;
static const uint32_t OHCI_FMINTERVAL   = 0x34;
static const uint32_t OHCI_FMREMAINING  = 0x38;
static const uint32_t OHCI_FMNUMBER     = 0x3C;
static const uint32_t OHCI_PERIODICSTART = 0x40;
static const uint32_t OHCI_LSTHRESHOLD  = 0x44;
static const uint32_t OHCI_RHDESC_A     = 0x48;
static const uint32_t OHCI_RHDESC_B     = 0x4C;
static const uint32_t OHCI_RHSTATUS     = 0x50;
static const uint32_t OHCI_RHPORTSTATUS = 0x54; // + 4 * port_index

// OHCI control register bits
static const uint32_t OHCI_CTRL_CBSR_MASK = 0x03;
static const uint32_t OHCI_CTRL_PLE       = (1u << 2);  // Periodic list enable
static const uint32_t OHCI_CTRL_IE        = (1u << 3);  // Isochronous enable
static const uint32_t OHCI_CTRL_CLE       = (1u << 4);  // Control list enable
static const uint32_t OHCI_CTRL_BLE       = (1u << 5);  // Bulk list enable
static const uint32_t OHCI_CTRL_HCFS_MASK = (3u << 6);
static const uint32_t OHCI_CTRL_HCFS_RST  = (0u << 6);
static const uint32_t OHCI_CTRL_HCFS_RES  = (1u << 6);  // Resume
static const uint32_t OHCI_CTRL_HCFS_OPER = (2u << 6);  // Operational
static const uint32_t OHCI_CTRL_HCFS_SUSP = (3u << 6);  // Suspend

// OHCI command status bits
static const uint32_t OHCI_CMD_HCR = (1u << 0); // Host controller reset
static const uint32_t OHCI_CMD_CLF = (1u << 1); // Control list filled
static const uint32_t OHCI_CMD_BLF = (1u << 2); // Bulk list filled

// Root hub port status bits
static const uint32_t OHCI_PORT_CCS  = (1u << 0);  // Current connect status
static const uint32_t OHCI_PORT_PES  = (1u << 1);  // Port enable status
static const uint32_t OHCI_PORT_PSS  = (1u << 2);  // Port suspend status
static const uint32_t OHCI_PORT_PRS  = (1u << 4);  // Port reset status
static const uint32_t OHCI_PORT_PPS  = (1u << 8);  // Port power status
static const uint32_t OHCI_PORT_LSDA = (1u << 9);  // Low speed device attached
static const uint32_t OHCI_PORT_CSC  = (1u << 16); // Connect status change
static const uint32_t OHCI_PORT_PESC = (1u << 17); // Port enable status change
static const uint32_t OHCI_PORT_PRSC = (1u << 20); // Port reset status change

// ================================================================
// OHCI data structures
// ================================================================

// Endpoint Descriptor (16 bytes, 16-byte aligned)
struct alignas(16) OHCI_ED {
    uint32_t control;   // FA[6:0], EN[10:7], D[12:11], S, K, F, MPS[26:16]
    uint32_t tailTD;
    uint32_t headTD;
    uint32_t nextED;
};

// General Transfer Descriptor (16 bytes, 16-byte aligned)
struct alignas(16) OHCI_TD {
    uint32_t control;   // R, DP[20:19], DI[23:21], T[25:24], EC[27:26], CC[31:28]
    uint32_t cbp;       // Current buffer pointer
    uint32_t nextTD;
    uint32_t be;        // Buffer end
};

// HCCA (Host Controller Communication Area, 256 bytes, 256-byte aligned)
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
static uint32_t s_ohciBase  = 0;
static uint8_t  s_numPorts  = 0;

alignas(256) static OHCI_HCCA s_hcca;
alignas(16)  static OHCI_ED   s_ctrlED;
alignas(16)  static OHCI_ED   s_bulkED;
alignas(16)  static OHCI_TD   s_tds[8];

// ================================================================
// MMIO helpers
// ================================================================

static uint32_t ohci_read(uint32_t reg)
{
    return arch::sparc::mmio_read32(s_ohciBase + reg);
}

static void ohci_write(uint32_t reg, uint32_t val)
{
    arch::sparc::mmio_write32(s_ohciBase + reg, val);
}

static void delay_ms(uint32_t ms)
{
    for (volatile uint32_t i = 0; i < ms * 5000; ++i) {}
}

// ================================================================
// Helpers: zero-fill
// ================================================================

static void memzero(void* dst, uint32_t len)
{
    uint8_t* p = static_cast<uint8_t*>(dst);
    for (uint32_t i = 0; i < len; ++i) p[i] = 0;
}

// ================================================================
// Wait for a TD to complete (check CC field)
// ================================================================

static TransferStatus wait_td(volatile OHCI_TD* td, uint32_t timeout_loops)
{
    for (uint32_t i = 0; i < timeout_loops; ++i) {
        uint32_t cc = (td->control >> 28) & 0x0F;
        if (cc != 0x0F) { // not "not accessed"
            switch (cc) {
                case 0: return XFER_SUCCESS;
                case 4: return XFER_STALL;
                case 2: // bit stuffing
                case 3: // toggle mismatch
                case 5: // timeout
                    return XFER_TIMEOUT;
                case 8: return XFER_DATA_OVERRUN;
                case 9: return XFER_DATA_UNDERRUN;
                default: return XFER_ERROR;
            }
        }
    }
    return XFER_TIMEOUT;
}

// ================================================================
// Probe for OHCI controller
//
// On Sun4m, USB (if present) would be on an SBus card.
// We check a well-known address range.  If no controller is found,
// the USB subsystem gracefully degrades.
// ================================================================

static bool find_ohci_controller()
{
    // Try common SBus slot addresses for USB cards
    static const uint32_t candidates[] = {
        0x30000000u, // SBus slot 0
        0x32000000u, // SBus slot 1
        0x34000000u, // SBus slot 2
    };

    for (int i = 0; i < 3; ++i) {
        uint32_t base = candidates[i];
        uint32_t rev = arch::sparc::mmio_read32(base + OHCI_REVISION);
        // OHCI revision should be 0x10 (1.0) or 0x11 (1.1)
        if ((rev & 0xFF) == 0x10 || (rev & 0xFF) == 0x11) {
            s_ohciBase = base;
            return true;
        }
    }
    return false;
}

// ================================================================
// HCI interface implementation
// ================================================================

bool init()
{
    s_available = false;

    if (!find_ohci_controller()) return false;

    // Host controller reset
    ohci_write(OHCI_CMDSTATUS, OHCI_CMD_HCR);
    delay_ms(10);

    for (int i = 0; i < 100; ++i) {
        if (!(ohci_read(OHCI_CMDSTATUS) & OHCI_CMD_HCR)) break;
        delay_ms(1);
    }

    // Set up HCCA
    memzero(&s_hcca, sizeof(s_hcca));
    ohci_write(OHCI_HCCA, reinterpret_cast<uint32_t>(&s_hcca));

    // Set up frame interval (12000 - 1 = 0x2EDF for full-speed)
    ohci_write(OHCI_FMINTERVAL, (0x2778u << 16) | 0x2EDFu);
    ohci_write(OHCI_PERIODICSTART, 0x2A2Fu);
    ohci_write(OHCI_LSTHRESHOLD, 0x0628u);

    // Initialize ED lists
    memzero(&s_ctrlED, sizeof(s_ctrlED));
    memzero(&s_bulkED, sizeof(s_bulkED));
    ohci_write(OHCI_CTRL_HEAD, reinterpret_cast<uint32_t>(&s_ctrlED));
    ohci_write(OHCI_BULK_HEAD, reinterpret_cast<uint32_t>(&s_bulkED));

    // Move to operational state
    ohci_write(OHCI_CONTROL, OHCI_CTRL_HCFS_OPER | OHCI_CTRL_CLE |
                              OHCI_CTRL_BLE | OHCI_CTRL_PLE);

    // Read number of downstream ports
    uint32_t rhdescA = ohci_read(OHCI_RHDESC_A);
    s_numPorts = static_cast<uint8_t>(rhdescA & 0xFF);
    if (s_numPorts > 15) s_numPorts = 15;

    // Power on all ports
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

    // Assert reset
    ohci_write(portReg, OHCI_PORT_PRS);
    delay_ms(50);

    // Wait for reset complete
    for (int i = 0; i < 100; ++i) {
        if (ohci_read(portReg) & OHCI_PORT_PRSC) break;
        delay_ms(1);
    }

    // Clear status change bits
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

    // Setup TD
    OHCI_TD* setupTD = &s_tds[0];
    setupTD->control = (0x0F << 28) | (0 << 19) | (2 << 24); // CC=not accessed, DP=SETUP, T=DATA0
    setupTD->cbp     = reinterpret_cast<uint32_t>(setup);
    setupTD->be      = reinterpret_cast<uint32_t>(setup) + 7;
    setupTD->nextTD  = reinterpret_cast<uint32_t>(&s_tds[1]);

    uint8_t tdIdx = 1;
    bool dirIn = (setup->bmRequestType & 0x80) != 0;

    // Data TDs
    if (dataLen > 0 && data != nullptr) {
        uint8_t* ptr = static_cast<uint8_t*>(data);
        uint16_t remaining = dataLen;
        uint8_t toggle = 1; // DATA1 after SETUP

        while (remaining > 0 && tdIdx < 6) {
            uint16_t chunk = (remaining > 64) ? 64 : remaining;
            OHCI_TD* td = &s_tds[tdIdx];

            uint32_t dp = dirIn ? 2 : 1; // 2=IN, 1=OUT
            td->control = (0x0F << 28) | (dp << 19) |
                          (static_cast<uint32_t>(toggle + 2) << 24);
            td->cbp     = reinterpret_cast<uint32_t>(ptr);
            td->be      = reinterpret_cast<uint32_t>(ptr) + chunk - 1;
            td->nextTD  = reinterpret_cast<uint32_t>(&s_tds[tdIdx + 1]);

            toggle ^= 1;
            ptr += chunk;
            remaining -= chunk;
            tdIdx++;
        }
    }

    // Status TD
    OHCI_TD* statusTD = &s_tds[tdIdx];
    uint32_t statusDP = dirIn ? 1 : 2; // opposite direction
    statusTD->control = (0x0F << 28) | (statusDP << 19) | (3 << 24); // T=DATA1
    statusTD->cbp     = 0;
    statusTD->be      = 0;
    statusTD->nextTD  = 0;
    tdIdx++;

    // Set up the ED
    memzero(&s_ctrlED, sizeof(s_ctrlED));
    s_ctrlED.control = (static_cast<uint32_t>(deviceAddr) & 0x7F) |
                       (64u << 16); // MPS = 64
    s_ctrlED.headTD  = reinterpret_cast<uint32_t>(&s_tds[0]);
    s_ctrlED.tailTD  = reinterpret_cast<uint32_t>(&s_tds[tdIdx]);
    s_ctrlED.nextED  = 0;

    ohci_write(OHCI_CTRL_HEAD, reinterpret_cast<uint32_t>(&s_ctrlED));
    ohci_write(OHCI_CMDSTATUS, OHCI_CMD_CLF);

    // Wait for all TDs
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
        s_tds[0].cbp     = reinterpret_cast<uint32_t>(ptr);
        s_tds[0].be      = reinterpret_cast<uint32_t>(ptr) + chunk - 1;
        s_tds[0].nextTD  = 0;

        memzero(&s_bulkED, sizeof(OHCI_ED));
        s_bulkED.control = (static_cast<uint32_t>(deviceAddr) & 0x7F) |
                           (static_cast<uint32_t>(ep) << 7) |
                           (dirIn ? (2u << 11) : (1u << 11)) |
                           (64u << 16);
        s_bulkED.headTD  = reinterpret_cast<uint32_t>(&s_tds[0]);
        s_bulkED.tailTD  = reinterpret_cast<uint32_t>(&s_tds[1]);
        s_bulkED.nextED  = 0;

        ohci_write(OHCI_BULK_HEAD, reinterpret_cast<uint32_t>(&s_bulkED));
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
    // OHCI periodic transfers use the same TD structure; for simplicity
    // we route through the bulk path.
    return bulk_transfer(deviceAddr, endpointAddr, data,
                         dataLen, bytesTransferred);
}

} // namespace hci
} // namespace usb
} // namespace kernel

namespace kernel {
namespace arch {
namespace sparc {
namespace usb_hci {

bool init()         { return kernel::usb::hci::init(); }
bool is_available() { return kernel::usb::hci::is_available(); }

} // namespace usb_hci
} // namespace sparc
} // namespace arch
} // namespace kernel
