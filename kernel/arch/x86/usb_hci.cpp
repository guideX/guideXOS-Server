// x86 UHCI Host Controller Interface — Implementation
//
// Provides the platform-specific USB HCI backend for x86 (32-bit).
// Implements the kernel::usb::hci namespace functions declared in usb.h.
//
// UHCI controllers are typically found on the PCI bus at class 0C/03/00.
// I/O registers are accessed via port I/O (USBBASE BAR).
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/arch/usb_hci.h"
#include "include/arch/x86.h"
#include <kernel/usb.h>

namespace kernel {
namespace usb {
namespace hci {

// ================================================================
// UHCI register offsets (relative to USBBASE)
// ================================================================

static const uint16_t UHCI_USBCMD    = 0x00;  // USB Command
static const uint16_t UHCI_USBSTS    = 0x02;  // USB Status
static const uint16_t UHCI_USBINTR   = 0x04;  // USB Interrupt Enable
static const uint16_t UHCI_FRNUM     = 0x06;  // Frame Number
static const uint16_t UHCI_FRBASEADD = 0x08;  // Frame List Base Address
static const uint16_t UHCI_SOFMOD    = 0x0C;  // Start of Frame Modify
static const uint16_t UHCI_PORTSC1   = 0x10;  // Port 1 Status/Control
static const uint16_t UHCI_PORTSC2   = 0x12;  // Port 2 Status/Control

// USBCMD bits
static const uint16_t UHCI_CMD_RUN         = 0x0001;
static const uint16_t UHCI_CMD_HCRESET     = 0x0002;
static const uint16_t UHCI_CMD_GRESET      = 0x0004;
static const uint16_t UHCI_CMD_EGSM        = 0x0008; // Enter Global Suspend Mode
static const uint16_t UHCI_CMD_MAXP        = 0x0080; // Max Packet (1 = 64 bytes)

// PORTSC bits
static const uint16_t UHCI_PORT_CONNECTED  = 0x0001;
static const uint16_t UHCI_PORT_CONNECT_CHG = 0x0002;
static const uint16_t UHCI_PORT_ENABLED    = 0x0004;
static const uint16_t UHCI_PORT_ENABLE_CHG = 0x0008;
static const uint16_t UHCI_PORT_LOWSPEED   = 0x0100;
static const uint16_t UHCI_PORT_RESET      = 0x0200;
static const uint16_t UHCI_PORT_SUSPEND    = 0x1000;

// ================================================================
// PCI configuration space access
// ================================================================

static const uint16_t PCI_CONFIG_ADDR = 0x0CF8;
static const uint16_t PCI_CONFIG_DATA = 0x0CFC;

static uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset)
{
    uint32_t addr = 0x80000000u |
                    (static_cast<uint32_t>(bus)  << 16) |
                    (static_cast<uint32_t>(dev)  << 11) |
                    (static_cast<uint32_t>(func) << 8)  |
                    (offset & 0xFC);
    arch::x86::outl(PCI_CONFIG_ADDR, addr);
    return arch::x86::inl(PCI_CONFIG_DATA);
}

// ================================================================
// Internal state
// ================================================================

static bool     s_available = false;
static uint16_t s_ioBase    = 0;

// ================================================================
// Frame list (1024 entries × 4 bytes = 4 KB, must be 4 KB-aligned)
// ================================================================

alignas(4096) static uint32_t s_frameList[1024];

// ================================================================
// Transfer Descriptor (TD) — 32 bytes
// ================================================================

struct alignas(16) UHCI_TD {
    uint32_t link;       // pointer to next TD/QH
    uint32_t status;     // control and status
    uint32_t token;      // PID, device address, endpoint, data toggle, maxlen
    uint32_t buffer;     // data buffer pointer
    // Software-use fields (not read by hardware)
    uint32_t reserved[4];
};

// ================================================================
// Queue Head (QH) — 8 bytes minimum, 16-byte aligned
// ================================================================

struct alignas(16) UHCI_QH {
    uint32_t headLink;   // horizontal link to next QH
    uint32_t elementLink; // pointer to first TD
    uint32_t reserved[2];
};

// Pre-allocated TDs and QH for simple transfers
alignas(16) static UHCI_TD  s_tds[16];
alignas(16) static UHCI_QH  s_qh;

// ================================================================
// Helper: read/write UHCI I/O registers
// ================================================================

static uint16_t uhci_read16(uint16_t reg)
{
    return arch::x86::inw(static_cast<uint16_t>(s_ioBase + reg));
}

static void uhci_write16(uint16_t reg, uint16_t val)
{
    arch::x86::outw(static_cast<uint16_t>(s_ioBase + reg), val);
}

static uint32_t uhci_read32(uint16_t reg)
{
    return arch::x86::inl(static_cast<uint16_t>(s_ioBase + reg));
}

static void uhci_write32(uint16_t reg, uint32_t val)
{
    arch::x86::outl(static_cast<uint16_t>(s_ioBase + reg), val);
}

// ================================================================
// Delay helper
// ================================================================

static void delay_ms(uint32_t ms)
{
    for (volatile uint32_t i = 0; i < ms * 10000; ++i) {}
}

// ================================================================
// Scan PCI bus for a UHCI controller (class 0C, subclass 03, progif 00)
// ================================================================

static bool find_uhci_controller()
{
    for (uint16_t bus = 0; bus < 256; ++bus) {
        for (uint8_t dev = 0; dev < 32; ++dev) {
            for (uint8_t func = 0; func < 8; ++func) {
                uint32_t id = pci_read32(static_cast<uint8_t>(bus), dev, func, 0);
                if (id == 0xFFFFFFFF) continue;

                uint32_t classReg = pci_read32(static_cast<uint8_t>(bus), dev, func, 0x08);
                uint8_t baseClass = static_cast<uint8_t>(classReg >> 24);
                uint8_t subClass  = static_cast<uint8_t>(classReg >> 16);
                uint8_t progIf    = static_cast<uint8_t>(classReg >> 8);

                if (baseClass == 0x0C && subClass == 0x03 && progIf == 0x00) {
                    // Found UHCI — read BAR4 (I/O base)
                    uint32_t bar4 = pci_read32(static_cast<uint8_t>(bus), dev, func, 0x20);
                    if (bar4 & 0x01) {
                        s_ioBase = static_cast<uint16_t>(bar4 & 0xFFE0);
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

// ================================================================
// Build a TD token field
// ================================================================

static uint32_t make_token(uint8_t pid, uint8_t addr, uint8_t ep,
                           uint8_t toggle, uint16_t maxLen)
{
    uint32_t actualLen = (maxLen > 0) ? (maxLen - 1) : 0x7FF;
    return (actualLen << 21) | (static_cast<uint32_t>(toggle) << 19) |
           (static_cast<uint32_t>(ep) << 15) | (static_cast<uint32_t>(addr) << 8) |
           pid;
}

// PID tokens
static const uint8_t PID_SETUP = 0x2D;
static const uint8_t PID_IN    = 0x69;
static const uint8_t PID_OUT   = 0xE1;

// ================================================================
// Wait for a TD to complete
// ================================================================

static TransferStatus wait_td(volatile UHCI_TD* td, uint32_t timeout_loops)
{
    for (uint32_t i = 0; i < timeout_loops; ++i) {
        uint32_t st = td->status;
        if (!(st & (1u << 23))) { // Active bit cleared
            if (st & (1u << 22)) return XFER_STALL;
            if (st & (1u << 21)) return XFER_DATA_OVERRUN;
            if (st & (1u << 20)) return XFER_ERROR; // Babble
            if (st & (1u << 18)) return XFER_TIMEOUT; // CRC/Timeout
            return XFER_SUCCESS;
        }
    }
    return XFER_TIMEOUT;
}

// ================================================================
// HCI interface implementation
// ================================================================

bool init()
{
    s_available = false;

    if (!find_uhci_controller()) return false;

    // Reset the controller
    uhci_write16(UHCI_USBCMD, UHCI_CMD_HCRESET);
    delay_ms(50);

    // Wait for reset to complete
    for (int i = 0; i < 100; ++i) {
        if (!(uhci_read16(UHCI_USBCMD) & UHCI_CMD_HCRESET)) break;
        delay_ms(1);
    }

    // Clear status
    uhci_write16(UHCI_USBSTS, 0xFFFF);

    // Set up frame list — all entries point to the QH with terminate bit
    for (int i = 0; i < 1024; ++i) {
        s_frameList[i] = reinterpret_cast<uint32_t>(&s_qh) | 0x02; // QH type
    }
    s_qh.headLink    = 0x01; // terminate
    s_qh.elementLink = 0x01; // terminate

    // Set frame list base address
    uhci_write32(UHCI_FRBASEADD, reinterpret_cast<uint32_t>(s_frameList));

    // Reset frame number
    uhci_write16(UHCI_FRNUM, 0);

    // Enable max packet size = 64, and start the schedule
    uhci_write16(UHCI_USBCMD, UHCI_CMD_RUN | UHCI_CMD_MAXP);

    // Enable all ports
    for (uint16_t p = 0; p < 2; ++p) {
        uint16_t portReg = static_cast<uint16_t>(UHCI_PORTSC1 + p * 2);
        uhci_write16(portReg, uhci_read16(portReg) | UHCI_PORT_ENABLED);
    }

    s_available = true;
    return true;
}

bool is_available()
{
    return s_available;
}

DeviceSpeed port_reset(uint8_t port)
{
    if (port >= 2) return SPEED_LOW;

    uint16_t portReg = static_cast<uint16_t>(UHCI_PORTSC1 + port * 2);

    // Assert reset
    uhci_write16(portReg, UHCI_PORT_RESET);
    delay_ms(50);

    // De-assert reset
    uhci_write16(portReg, 0);
    delay_ms(10);

    // Clear status change bits and enable port
    uint16_t st = uhci_read16(portReg);
    uhci_write16(portReg, st | UHCI_PORT_ENABLED | UHCI_PORT_CONNECT_CHG | UHCI_PORT_ENABLE_CHG);

    delay_ms(10);
    st = uhci_read16(portReg);

    return (st & UHCI_PORT_LOWSPEED) ? SPEED_LOW : SPEED_FULL;
}

uint8_t port_count()
{
    return 2; // UHCI always has 2 root-hub ports
}

bool port_connected(uint8_t port)
{
    if (port >= 2) return false;
    uint16_t portReg = static_cast<uint16_t>(UHCI_PORTSC1 + port * 2);
    return (uhci_read16(portReg) & UHCI_PORT_CONNECTED) != 0;
}

TransferStatus control_transfer(uint8_t deviceAddr,
                                const SetupPacket* setup,
                                void* data,
                                uint16_t dataLen)
{
    if (!s_available) return XFER_NOT_SUPPORTED;

    uint8_t tdIdx = 0;

    // Setup TD
    UHCI_TD* setupTd = &s_tds[tdIdx++];
    setupTd->link   = reinterpret_cast<uint32_t>(&s_tds[tdIdx]) | 0x04; // depth first
    setupTd->status = (3u << 27) | (1u << 23); // 3 retries, active
    setupTd->token  = make_token(PID_SETUP, deviceAddr, 0, 0, 8);
    setupTd->buffer = reinterpret_cast<uint32_t>(setup);

    // Data TDs
    uint8_t toggle = 1;
    uint8_t* ptr = static_cast<uint8_t*>(data);
    uint16_t remaining = dataLen;
    bool dirIn = (setup->bmRequestType & 0x80) != 0;

    while (remaining > 0 && tdIdx < 14) {
        uint16_t chunk = (remaining > 64) ? 64 : remaining;
        UHCI_TD* td = &s_tds[tdIdx];

        td->link   = reinterpret_cast<uint32_t>(&s_tds[tdIdx + 1]) | 0x04;
        td->status = (3u << 27) | (1u << 23);
        td->token  = make_token(dirIn ? PID_IN : PID_OUT,
                                deviceAddr, 0, toggle, chunk);
        td->buffer = reinterpret_cast<uint32_t>(ptr);

        toggle ^= 1;
        ptr += chunk;
        remaining -= chunk;
        tdIdx++;
    }

    // Status TD (opposite direction from data phase)
    UHCI_TD* statusTd = &s_tds[tdIdx++];
    statusTd->link   = 0x01; // terminate
    statusTd->status = (3u << 27) | (1u << 23);
    statusTd->token  = make_token(dirIn ? PID_OUT : PID_IN,
                                  deviceAddr, 0, 1, 0);
    statusTd->buffer = 0;

    // Point QH to first TD
    s_qh.elementLink = reinterpret_cast<uint32_t>(&s_tds[0]);

    // Wait for all TDs to complete
    for (uint8_t i = 0; i < tdIdx; ++i) {
        TransferStatus st = wait_td(&s_tds[i], 1000000);
        if (st != XFER_SUCCESS) {
            s_qh.elementLink = 0x01; // terminate
            return st;
        }
    }

    s_qh.elementLink = 0x01;
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
    uint8_t pid   = dirIn ? PID_IN : PID_OUT;

    uint8_t* ptr = static_cast<uint8_t*>(data);
    uint16_t remaining = dataLen;
    uint16_t transferred = 0;
    uint8_t  toggle = 0;

    while (remaining > 0) {
        uint16_t chunk = (remaining > 64) ? 64 : remaining;

        UHCI_TD* td = &s_tds[0];
        td->link   = 0x01; // terminate
        td->status = (3u << 27) | (1u << 23);
        td->token  = make_token(pid, deviceAddr, ep, toggle, chunk);
        td->buffer = reinterpret_cast<uint32_t>(ptr);

        s_qh.elementLink = reinterpret_cast<uint32_t>(td);

        TransferStatus st = wait_td(td, 500000);

        s_qh.elementLink = 0x01;

        if (st != XFER_SUCCESS) {
            if (bytesTransferred) *bytesTransferred = transferred;
            return st;
        }

        uint16_t actual = static_cast<uint16_t>(((td->status + 1) & 0x7FF));
        transferred += actual;
        ptr += actual;
        remaining -= actual;
        toggle ^= 1;

        if (actual < chunk) break; // short packet
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
    // For UHCI, interrupt transfers are structurally identical to
    // bulk transfers — the scheduling difference (periodic vs async)
    // is handled by the frame list placement.  For simplicity, we
    // use the same path as bulk_transfer here.
    return bulk_transfer(deviceAddr, endpointAddr, data,
                         dataLen, bytesTransferred);
}

} // namespace hci
} // namespace usb
} // namespace kernel

// ================================================================
// Arch-local init / query wrappers
// ================================================================

namespace kernel {
namespace arch {
namespace x86 {
namespace usb_hci {

bool init()        { return kernel::usb::hci::init(); }
bool is_available() { return kernel::usb::hci::is_available(); }

} // namespace usb_hci
} // namespace x86
} // namespace arch
} // namespace kernel
