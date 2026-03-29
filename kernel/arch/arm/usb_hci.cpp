// ARM DWC OTG Host Controller Interface — Implementation
//
// Provides the platform-specific USB HCI backend for ARM.
// Implements the kernel::usb::hci namespace functions declared in usb.h.
//
// The DesignWare Core USB 2.0 OTG controller is found on many ARM
// SoCs (BCM2835/6/7 on Raspberry Pi, STM32, etc.).  It uses MMIO
// registers for host channel programming and FIFO-based data transfer.
//
// DWC OTG MMIO base varies by SoC:
//   BCM2835 (RPi 1/Zero):  0x20980000
//   BCM2836/7 (RPi 2/3):   0x3F980000
//   BCM2711 (RPi 4):        0xFE980000  (xHCI on separate controller)
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/arch/usb_hci.h"
#include "include/arch/arm.h"
#include <kernel/usb.h>

namespace kernel {
namespace usb {
namespace hci {

// ================================================================
// DWC OTG register offsets (host-mode subset)
// ================================================================

static const uint32_t DWC_OTG_BASE = 0x20980000u; // BCM2835 default

// Core global registers
static const uint32_t GOTGCTL   = 0x000;
static const uint32_t GAHBCFG   = 0x008;
static const uint32_t GUSBCFG   = 0x00C;
static const uint32_t GRSTCTL   = 0x010;
static const uint32_t GINTSTS   = 0x014;
static const uint32_t GINTMSK   = 0x018;
static const uint32_t GRXFSIZ   = 0x024;
static const uint32_t GNPTXFSIZ = 0x028;
static const uint32_t GHWCFG2   = 0x048;

// Host-mode registers
static const uint32_t HCFG      = 0x400;
static const uint32_t HFIR      = 0x404;
static const uint32_t HFNUM     = 0x408;
static const uint32_t HPTXSTS   = 0x410;
static const uint32_t HAINT     = 0x414;
static const uint32_t HAINTMSK  = 0x418;
static const uint32_t HPRT      = 0x440;

// Host channel registers (8 channels, 0x20 spacing)
static const uint32_t HC_BASE   = 0x500;
static const uint32_t HCCHAR    = 0x00;
static const uint32_t HCSPLT    = 0x04;
static const uint32_t HCINT     = 0x08;
static const uint32_t HCINTMSK  = 0x0C;
static const uint32_t HCTSIZ    = 0x10;
static const uint32_t HCDMA     = 0x14;

// HPRT bits
static const uint32_t HPRT_CONNECTED   = (1u << 0);
static const uint32_t HPRT_CONNECT_DET = (1u << 1);
static const uint32_t HPRT_ENABLED     = (1u << 2);
static const uint32_t HPRT_ENABLE_CHG  = (1u << 3);
static const uint32_t HPRT_OVERCURRENT = (1u << 4);
static const uint32_t HPRT_RESET       = (1u << 8);
static const uint32_t HPRT_POWER       = (1u << 12);
static const uint32_t HPRT_SPEED_MASK  = (3u << 17);
static const uint32_t HPRT_SPEED_HIGH  = (0u << 17);
static const uint32_t HPRT_SPEED_FULL  = (1u << 17);
static const uint32_t HPRT_SPEED_LOW   = (2u << 17);

// HCCHAR bits
static const uint32_t HCCHAR_ENABLE    = (1u << 31);
static const uint32_t HCCHAR_DISABLE   = (1u << 30);
static const uint32_t HCCHAR_EPDIR_IN  = (1u << 15);

// HCINT bits
static const uint32_t HCINT_XFERCOMP   = (1u << 0);
static const uint32_t HCINT_HALT       = (1u << 1);
static const uint32_t HCINT_STALL      = (1u << 3);
static const uint32_t HCINT_NAK        = (1u << 4);
static const uint32_t HCINT_ACK        = (1u << 5);
static const uint32_t HCINT_XACTERR    = (1u << 7);
static const uint32_t HCINT_BBLERR     = (1u << 8);
static const uint32_t HCINT_DATATGLERR = (1u << 10);

// ================================================================
// MMIO access helpers
// ================================================================

static volatile uint32_t* reg(uint32_t offset)
{
    return reinterpret_cast<volatile uint32_t*>(DWC_OTG_BASE + offset);
}

static uint32_t mmio_read(uint32_t offset)
{
    return *reg(offset);
}

static void mmio_write(uint32_t offset, uint32_t val)
{
    *reg(offset) = val;
}

static uint32_t hc_reg_off(uint8_t ch, uint32_t regOff)
{
    return HC_BASE + static_cast<uint32_t>(ch) * 0x20 + regOff;
}

// ================================================================
// Internal state
// ================================================================

static bool s_available = false;
static const uint8_t NUM_CHANNELS = 8;

// ================================================================
// Delay helper
// ================================================================

static void delay_ms(uint32_t ms)
{
    for (volatile uint32_t i = 0; i < ms * 10000; ++i) {}
}

// ================================================================
// Wait for host channel transfer to complete
// ================================================================

static TransferStatus wait_channel(uint8_t ch, uint32_t timeout_loops)
{
    for (uint32_t i = 0; i < timeout_loops; ++i) {
        uint32_t intr = mmio_read(hc_reg_off(ch, HCINT));

        if (intr & HCINT_XFERCOMP) {
            mmio_write(hc_reg_off(ch, HCINT), 0xFFFFFFFF);
            return XFER_SUCCESS;
        }
        if (intr & HCINT_STALL) {
            mmio_write(hc_reg_off(ch, HCINT), 0xFFFFFFFF);
            return XFER_STALL;
        }
        if (intr & HCINT_BBLERR) {
            mmio_write(hc_reg_off(ch, HCINT), 0xFFFFFFFF);
            return XFER_DATA_OVERRUN;
        }
        if (intr & HCINT_XACTERR) {
            mmio_write(hc_reg_off(ch, HCINT), 0xFFFFFFFF);
            return XFER_ERROR;
        }
        if (intr & HCINT_HALT) {
            mmio_write(hc_reg_off(ch, HCINT), 0xFFFFFFFF);
            return XFER_ERROR;
        }
    }
    return XFER_TIMEOUT;
}

// ================================================================
// Program a host channel for a transfer
// ================================================================

static void program_channel(uint8_t ch, uint8_t devAddr, uint8_t ep,
                            bool dirIn, uint8_t epType,
                            uint16_t maxPkt, uint16_t xferLen,
                            uint8_t pid, void* dmaAddr)
{
    // Clear all interrupts
    mmio_write(hc_reg_off(ch, HCINT), 0xFFFFFFFF);

    // Enable all interrupt bits
    mmio_write(hc_reg_off(ch, HCINTMSK),
               HCINT_XFERCOMP | HCINT_HALT | HCINT_STALL |
               HCINT_NAK | HCINT_XACTERR | HCINT_BBLERR);

    // Channel characteristics
    uint32_t charVal = (static_cast<uint32_t>(maxPkt) << 0) |
                       (static_cast<uint32_t>(ep) << 11) |
                       (dirIn ? HCCHAR_EPDIR_IN : 0u) |
                       (static_cast<uint32_t>(epType) << 18) |
                       (static_cast<uint32_t>(devAddr) << 22);
    mmio_write(hc_reg_off(ch, HCCHAR), charVal);

    // Transfer size
    uint32_t pktCount = (xferLen > 0) ? ((xferLen + maxPkt - 1) / maxPkt) : 1;
    uint32_t tsiz = (xferLen & 0x7FFFF) |
                    (pktCount << 19) |
                    (static_cast<uint32_t>(pid) << 29);
    mmio_write(hc_reg_off(ch, HCTSIZ), tsiz);

    // DMA address
    mmio_write(hc_reg_off(ch, HCDMA),
               static_cast<uint32_t>(reinterpret_cast<uintptr_t>(dmaAddr)));

    // Enable the channel
    charVal = mmio_read(hc_reg_off(ch, HCCHAR));
    charVal |= HCCHAR_ENABLE;
    charVal &= ~HCCHAR_DISABLE;
    mmio_write(hc_reg_off(ch, HCCHAR), charVal);
}

// DWC OTG PID tokens
static const uint8_t DWC_PID_DATA0 = 0;
static const uint8_t DWC_PID_DATA1 = 2;
static const uint8_t DWC_PID_DATA2 = 1;
static const uint8_t DWC_PID_SETUP = 3;

// ================================================================
// HCI interface implementation
// ================================================================

bool init()
{
    s_available = false;

    // Check OTG ID register for DWC core presence
    uint32_t gotgctl = mmio_read(GOTGCTL);
    (void)gotgctl;

    // Perform core soft reset
    mmio_write(GRSTCTL, 1u); // Core soft reset
    delay_ms(100);

    // Wait for AHB idle
    for (int i = 0; i < 100; ++i) {
        if (mmio_read(GRSTCTL) & (1u << 31)) break; // AHB idle
        delay_ms(1);
    }

    // Configure AHB: enable DMA, burst length
    mmio_write(GAHBCFG, (1u << 0) | (1u << 5)); // Global interrupt mask | DMA enable

    // Force host mode
    uint32_t gusbcfg = mmio_read(GUSBCFG);
    gusbcfg &= ~(1u << 30); // Clear force device mode
    gusbcfg |= (1u << 29);  // Set force host mode
    mmio_write(GUSBCFG, gusbcfg);
    delay_ms(50);

    // Configure host
    mmio_write(HCFG, 0x01); // FS/LS PHY clock select

    // Set Rx FIFO size and Tx FIFO sizes
    mmio_write(GRXFSIZ, 1024);
    mmio_write(GNPTXFSIZ, (512u << 16) | 1024u);

    // Power on the port
    uint32_t hprt = mmio_read(HPRT);
    hprt &= ~(HPRT_CONNECT_DET | HPRT_ENABLE_CHG | HPRT_ENABLED); // W1C bits
    hprt |= HPRT_POWER;
    mmio_write(HPRT, hprt);
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
    (void)port; // single root port

    uint32_t hprt = mmio_read(HPRT);
    hprt &= ~(HPRT_CONNECT_DET | HPRT_ENABLE_CHG | HPRT_ENABLED);
    hprt |= HPRT_RESET;
    mmio_write(HPRT, hprt);
    delay_ms(60);

    hprt = mmio_read(HPRT);
    hprt &= ~(HPRT_CONNECT_DET | HPRT_ENABLE_CHG | HPRT_ENABLED | HPRT_RESET);
    mmio_write(HPRT, hprt);
    delay_ms(20);

    hprt = mmio_read(HPRT);
    uint32_t speed = (hprt & HPRT_SPEED_MASK) >> 17;

    if (speed == 0) return SPEED_HIGH;
    if (speed == 1) return SPEED_FULL;
    return SPEED_LOW;
}

uint8_t port_count()
{
    return 1; // DWC OTG has a single root-hub port
}

bool port_connected(uint8_t port)
{
    (void)port;
    return (mmio_read(HPRT) & HPRT_CONNECTED) != 0;
}

TransferStatus control_transfer(uint8_t deviceAddr,
                                const SetupPacket* setup,
                                void* data,
                                uint16_t dataLen)
{
    if (!s_available) return XFER_NOT_SUPPORTED;

    // Use channel 0 for control transfers
    const uint8_t ch = 0;
    const uint16_t maxPkt = 64;

    // SETUP phase
    program_channel(ch, deviceAddr, 0, false, 0 /*control*/,
                    maxPkt, 8, DWC_PID_SETUP,
                    const_cast<SetupPacket*>(setup));

    TransferStatus st = wait_channel(ch, 1000000);
    if (st != XFER_SUCCESS) return st;

    // DATA phase
    bool dirIn = (setup->bmRequestType & 0x80) != 0;
    uint8_t toggle = 1; // DATA1 after SETUP

    if (dataLen > 0 && data != nullptr) {
        uint8_t* ptr = static_cast<uint8_t*>(data);
        uint16_t remaining = dataLen;

        while (remaining > 0) {
            uint16_t chunk = (remaining > maxPkt) ? maxPkt : remaining;
            uint8_t pid = (toggle == 0) ? DWC_PID_DATA0 : DWC_PID_DATA1;

            program_channel(ch, deviceAddr, 0, dirIn, 0,
                            maxPkt, chunk, pid, ptr);

            st = wait_channel(ch, 500000);
            if (st != XFER_SUCCESS) return st;

            ptr += chunk;
            remaining -= chunk;
            toggle ^= 1;
        }
    }

    // STATUS phase (opposite direction)
    uint8_t statusPid = DWC_PID_DATA1;
    program_channel(ch, deviceAddr, 0, !dirIn, 0,
                    maxPkt, 0, statusPid, nullptr);

    return wait_channel(ch, 500000);
}

TransferStatus bulk_transfer(uint8_t deviceAddr,
                             uint8_t endpointAddr,
                             void* data,
                             uint16_t dataLen,
                             uint16_t* bytesTransferred)
{
    if (!s_available) return XFER_NOT_SUPPORTED;

    const uint8_t ch = 1; // use channel 1 for bulk
    uint8_t ep    = endpointAddr & 0x0F;
    bool    dirIn = (endpointAddr & 0x80) != 0;
    const uint16_t maxPkt = 64;

    uint8_t* ptr = static_cast<uint8_t*>(data);
    uint16_t remaining = dataLen;
    uint16_t transferred = 0;
    uint8_t  toggle = 0;

    while (remaining > 0) {
        uint16_t chunk = (remaining > maxPkt) ? maxPkt : remaining;
        uint8_t pid = (toggle == 0) ? DWC_PID_DATA0 : DWC_PID_DATA1;

        program_channel(ch, deviceAddr, ep, dirIn, 2 /*bulk*/,
                        maxPkt, chunk, pid, ptr);

        TransferStatus st = wait_channel(ch, 500000);
        if (st != XFER_SUCCESS) {
            if (bytesTransferred) *bytesTransferred = transferred;
            return st;
        }

        // Read actual transfer size from HCTSIZ
        uint32_t tsiz = mmio_read(hc_reg_off(ch, HCTSIZ));
        uint32_t xferRemaining = tsiz & 0x7FFFF;
        uint16_t actual = (chunk > xferRemaining) ?
                          static_cast<uint16_t>(chunk - xferRemaining) : chunk;

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
    if (!s_available) return XFER_NOT_SUPPORTED;

    const uint8_t ch = 2; // use channel 2 for interrupt
    uint8_t ep    = endpointAddr & 0x0F;
    bool    dirIn = (endpointAddr & 0x80) != 0;
    const uint16_t maxPkt = (dataLen < 64) ? dataLen : 64;

    program_channel(ch, deviceAddr, ep, dirIn, 3 /*interrupt*/,
                    maxPkt, dataLen, DWC_PID_DATA0, data);

    TransferStatus st = wait_channel(ch, 500000);

    if (st == XFER_SUCCESS && bytesTransferred) {
        uint32_t tsiz = mmio_read(hc_reg_off(ch, HCTSIZ));
        uint32_t rem = tsiz & 0x7FFFF;
        *bytesTransferred = (dataLen > rem) ?
                            static_cast<uint16_t>(dataLen - rem) : 0;
    } else if (bytesTransferred) {
        *bytesTransferred = 0;
    }

    return st;
}

} // namespace hci
} // namespace usb
} // namespace kernel

namespace kernel {
namespace arch {
namespace arm {
namespace usb_hci {

bool init()         { return kernel::usb::hci::init(); }
bool is_available() { return kernel::usb::hci::is_available(); }

} // namespace usb_hci
} // namespace arm
} // namespace arch
} // namespace kernel
