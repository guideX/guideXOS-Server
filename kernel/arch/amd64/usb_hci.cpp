// AMD64 UHCI Host Controller Interface — Implementation
//
// Provides the platform-specific USB HCI backend for amd64 (64-bit).
// Implements the kernel::usb::hci namespace functions declared in usb.h.
//
// Structurally identical to the x86 UHCI driver but uses 64-bit
// addressing for PCI configuration and physical pointers.
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/arch/usb_hci.h"
#include "include/arch/amd64.h"
#include <kernel/usb.h>
#include <kernel/serial_debug.h>

namespace kernel {
namespace usb {
namespace hci {

namespace {

// ================================================================
// UHCI register offsets
// ================================================================

static const uint16_t UHCI_USBCMD    = 0x00;
static const uint16_t UHCI_USBSTS    = 0x02;
static const uint16_t UHCI_USBINTR   = 0x04;
static const uint16_t UHCI_FRNUM     = 0x06;
static const uint16_t UHCI_FRBASEADD = 0x08;
static const uint16_t UHCI_SOFMOD    = 0x0C;
static const uint16_t UHCI_PORTSC1   = 0x10;
static const uint16_t UHCI_PORTSC2   = 0x12;

static const uint16_t UHCI_CMD_RUN         = 0x0001;
static const uint16_t UHCI_CMD_HCRESET     = 0x0002;
static const uint16_t UHCI_CMD_MAXP        = 0x0080;

static const uint16_t UHCI_PORT_CONNECTED  = 0x0001;
static const uint16_t UHCI_PORT_CONNECT_CHG = 0x0002;
static const uint16_t UHCI_PORT_ENABLED    = 0x0004;
static const uint16_t UHCI_PORT_ENABLE_CHG = 0x0008;
static const uint16_t UHCI_PORT_LOWSPEED   = 0x0100;
static const uint16_t UHCI_PORT_RESET      = 0x0200;

// ================================================================
// PCI configuration space access (port I/O method)
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
    arch::amd64::outl(PCI_CONFIG_ADDR, addr);
    return arch::amd64::inl(PCI_CONFIG_DATA);
}

static void pci_write16(uint8_t bus, uint8_t dev, uint8_t func,
                        uint8_t offset, uint16_t value)
{
    uint32_t addr = 0x80000000u |
                    (static_cast<uint32_t>(bus)  << 16) |
                    (static_cast<uint32_t>(dev)  << 11) |
                    (static_cast<uint32_t>(func) << 8)  |
                    (offset & 0xFC);
    arch::amd64::outl(PCI_CONFIG_ADDR, addr);
    arch::amd64::outw(static_cast<uint16_t>(PCI_CONFIG_DATA + (offset & 0x02)), value);
}

// ================================================================
// Internal state
// ================================================================

static bool     s_available = false;
static uint16_t s_ioBase    = 0;
static uint64_t s_kernelPhysicalBase = 0x100000ULL;

// Frame list — must be in the low 4 GB for UHCI (32-bit DMA).
// Using alignas for C++14 compatibility.
alignas(4096) static volatile uint32_t s_frameList[1024];

struct alignas(16) UHCI_TD {
    uint32_t link;
    uint32_t status;
    uint32_t token;
    uint32_t buffer;
    uint32_t reserved[4];
};

struct alignas(16) UHCI_QH {
    uint32_t headLink;
    uint32_t elementLink;
    uint32_t reserved[2];
};

alignas(16) static volatile UHCI_TD s_tds[16];
alignas(16) static volatile UHCI_QH s_qh;
alignas(16) static usb::SetupPacket s_dmaSetup;
alignas(16) static volatile uint8_t s_controlPackets[13][64];
alignas(16) static volatile uint8_t s_bulkPacket[64];
static uint8_t s_dataToggle[128][usb::MAX_ENDPOINTS * 2];

static inline void dma_compiler_barrier()
{
    asm volatile("" ::: "memory");
}

// ================================================================
// Helper: UHCI register access
// ================================================================

static uint16_t uhci_read16(uint16_t reg)
{
    return arch::amd64::inw(static_cast<uint16_t>(s_ioBase + reg));
}

static void uhci_write16(uint16_t reg, uint16_t val)
{
    arch::amd64::outw(static_cast<uint16_t>(s_ioBase + reg), val);
}

static uint32_t uhci_read32(uint16_t reg)
{
    return arch::amd64::inl(static_cast<uint16_t>(s_ioBase + reg));
}

static void uhci_write32(uint16_t reg, uint32_t val)
{
    arch::amd64::outl(static_cast<uint16_t>(s_ioBase + reg), val);
}

static void delay_ms(uint32_t ms)
{
    for (volatile uint32_t i = 0; i < ms * 10000; ++i) {}
}

// ================================================================
// PCI bus scan for UHCI (class 0C/03/00)
// ================================================================

static bool find_uhci_controller()
{
    for (uint16_t bus = 0; bus < 256; ++bus) {
        for (uint8_t dev = 0; dev < 32; ++dev) {
            for (uint8_t func = 0; func < 8; ++func) {
                const uint32_t id = pci_read32(static_cast<uint8_t>(bus), dev, func, 0);
                if (id == 0xFFFFFFFFu) continue;

                const uint32_t classReg = pci_read32(static_cast<uint8_t>(bus), dev, func, 0x08);
                const uint8_t baseClass = static_cast<uint8_t>(classReg >> 24);
                const uint8_t subClass  = static_cast<uint8_t>(classReg >> 16);
                const uint8_t progIf    = static_cast<uint8_t>(classReg >> 8);
                if (baseClass != 0x0C || subClass != 0x03 || progIf != 0x00) continue;

                const uint32_t bar4 = pci_read32(static_cast<uint8_t>(bus), dev, func, 0x20);
                if ((bar4 & 0x01u) == 0) continue;
                s_ioBase = static_cast<uint16_t>(bar4 & 0xFFE0u);
                uint16_t command = static_cast<uint16_t>(
                    pci_read32(static_cast<uint8_t>(bus), dev, func, 0x04));
                command = static_cast<uint16_t>(command | 0x0005u); // I/O decode + bus master.
                pci_write16(static_cast<uint8_t>(bus), dev, func, 0x04, command);
                const uint16_t enabledCommand = static_cast<uint16_t>(
                    pci_read32(static_cast<uint8_t>(bus), dev, func, 0x04));
                serial::puts("[USB-UHCI] controller-found pci=");
                serial::put_hex8(static_cast<uint8_t>(bus));
                serial::putc(':');
                serial::put_hex8(dev);
                serial::putc('.');
                serial::put_hex8(func);
                serial::puts(" io-base=");
                serial::put_hex16(s_ioBase);
                serial::puts(" command=");
                serial::put_hex16(enabledCommand);
                serial::putc('\n');
                return (enabledCommand & 0x0005u) == 0x0005u;
            }
        }
    }
    return false;
}

// ================================================================
// Token builder and PID constants
// ================================================================

static const uint8_t PID_SETUP = 0x2D;
static const uint8_t PID_IN    = 0x69;
static const uint8_t PID_OUT   = 0xE1;

static uint32_t make_token(uint8_t pid, uint8_t addr, uint8_t ep,
                           uint8_t toggle, uint16_t maxLen)
{
    uint32_t actualLen = (maxLen > 0) ? (maxLen - 1) : 0x7FF;
    return (actualLen << 21) | (static_cast<uint32_t>(toggle) << 19) |
           (static_cast<uint32_t>(ep) << 15) | (static_cast<uint32_t>(addr) << 8) |
           pid;
}

// Pointer cast helper — truncates 64-bit pointer to 32-bit for UHCI DMA
static uint32_t ptr32(const volatile void* p)
{
    const uint64_t virt = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(p));
    uint64_t physical = virt;
    if (virt >= 0x100000ULL) {
        const uint64_t offset = virt - 0x100000ULL;
        if (offset > UINT64_MAX - s_kernelPhysicalBase) return 0;
        physical = s_kernelPhysicalBase + offset;
    }
    return physical <= 0xFFFFFFFFULL ? static_cast<uint32_t>(physical) : 0;
}

// ================================================================
// Wait for TD completion
// ================================================================

static TransferStatus wait_td(volatile UHCI_TD* td, uint32_t timeout_loops)
{
    for (uint32_t i = 0; i < timeout_loops; ++i) {
        uint32_t st = td->status;
        if (!(st & (1u << 23))) {
            if (st & (1u << 22)) return XFER_STALL;
            if (st & (1u << 21)) return XFER_DATA_OVERRUN;
            if (st & (1u << 20)) return XFER_ERROR;
            if (st & (1u << 18)) return XFER_TIMEOUT;
            return XFER_SUCCESS;
        }
    }
    return XFER_TIMEOUT;
}

} // anonymous namespace

// ================================================================
// HCI interface implementation
// ================================================================

void set_kernel_physical_base(uint64_t physicalBase)
{
    if (physicalBase != 0) s_kernelPhysicalBase = physicalBase;
    serial::puts("[USB-UHCI] dma-base=");
    serial::put_hex64(s_kernelPhysicalBase);
    serial::putc('\n');
}

bool init()
{
    s_available = false;
    for (uint8_t addr = 0; addr < 128; ++addr)
        for (uint8_t ep = 0; ep < usb::MAX_ENDPOINTS * 2; ++ep)
            s_dataToggle[addr][ep] = 0;

    if (!find_uhci_controller()) return false;

    uhci_write16(UHCI_USBCMD, UHCI_CMD_HCRESET);
    delay_ms(50);

    for (int i = 0; i < 100; ++i) {
        if (!(uhci_read16(UHCI_USBCMD) & UHCI_CMD_HCRESET)) break;
        delay_ms(1);
    }

    uhci_write16(UHCI_USBSTS, 0xFFFF);

    for (int i = 0; i < 1024; ++i) {
        s_frameList[i] = ptr32(&s_qh) | 0x02;
    }
    dma_compiler_barrier();
    s_qh.headLink    = 0x01;
    s_qh.elementLink = 0x01;

    uhci_write32(UHCI_FRBASEADD, ptr32(s_frameList));
    uhci_write16(UHCI_FRNUM, 0);
    dma_compiler_barrier();
    uhci_write16(UHCI_USBCMD, UHCI_CMD_RUN | UHCI_CMD_MAXP);

    for (uint16_t p = 0; p < 2; ++p) {
        uint16_t portReg = static_cast<uint16_t>(UHCI_PORTSC1 + p * 2);
        uhci_write16(portReg, uhci_read16(portReg) | UHCI_PORT_ENABLED);
    }

    s_available = true;
    serial::puts("[USB-UHCI] schedule command=");
    serial::put_hex16(uhci_read16(UHCI_USBCMD));
    serial::puts(" status=");
    serial::put_hex16(uhci_read16(UHCI_USBSTS));
    serial::puts(" frame-base=");
    serial::put_hex32(uhci_read32(UHCI_FRBASEADD));
    serial::puts(" expected=");
    serial::put_hex32(ptr32(s_frameList));
    serial::puts(" frame-virt=");
    serial::put_hex64(reinterpret_cast<uint64_t>(s_frameList));
    serial::puts(" frame-entry=");
    serial::put_hex32(s_frameList[0]);
    serial::puts(" qh-phys=");
    serial::put_hex32(ptr32(&s_qh));
    serial::puts(" port0=");
    serial::put_hex16(uhci_read16(UHCI_PORTSC1));
    serial::puts(" port1=");
    serial::put_hex16(uhci_read16(UHCI_PORTSC2));
    serial::putc('\n');
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

    uhci_write16(portReg, UHCI_PORT_RESET);
    delay_ms(50);
    uhci_write16(portReg, 0);
    delay_ms(10);

    uint16_t st = uhci_read16(portReg);
    uhci_write16(portReg, st | UHCI_PORT_ENABLED | UHCI_PORT_CONNECT_CHG | UHCI_PORT_ENABLE_CHG);
    delay_ms(10);
    st = uhci_read16(portReg);

    serial::puts("[USB-UHCI] port-reset port=");
    serial::put_hex8(port);
    serial::puts(" status=");
    serial::put_hex16(st);
    serial::putc('\n');
    return (st & UHCI_PORT_LOWSPEED) ? SPEED_LOW : SPEED_FULL;
}

uint8_t port_count()
{
    return 2;
}

bool port_connected(uint8_t port)
{
    if (port >= 2) return false;
    uint16_t portReg = static_cast<uint16_t>(UHCI_PORTSC1 + port * 2);
    return (uhci_read16(portReg) & UHCI_PORT_CONNECTED) != 0;
}

bool port_connection_changed(uint8_t port)
{
    if (port >= 2) return false;
    const uint16_t portReg = static_cast<uint16_t>(UHCI_PORTSC1 + port * 2);
    const uint16_t status = uhci_read16(portReg);
    if ((status & UHCI_PORT_CONNECT_CHG) == 0) return false;
    // UHCI change bits are write-one-to-clear; preserve the current enable bit.
    uhci_write16(portReg, static_cast<uint16_t>(
        (status & UHCI_PORT_ENABLED) | UHCI_PORT_CONNECT_CHG));
    return true;
}

TransferStatus control_transfer(uint8_t deviceAddr,
                                const SetupPacket* setup,
                                void* data,
                                uint16_t dataLen)
{
    if (!s_available) return XFER_NOT_SUPPORTED;
    if (!setup || (dataLen != 0 && !data) || dataLen > sizeof(s_controlPackets))
        return XFER_BUFFER_ERROR;

    const uint8_t* setupSource = reinterpret_cast<const uint8_t*>(setup);
    volatile uint8_t* setupDestination = reinterpret_cast<volatile uint8_t*>(&s_dmaSetup);
    for (uint8_t i = 0; i < sizeof(s_dmaSetup); ++i)
        setupDestination[i] = setupSource[i];
    volatile UHCI_TD* setupTd = &s_tds[0];
    setupTd->link   = ptr32(&s_tds[1]) | 0x04;
    setupTd->status = (3u << 27) | (1u << 23);
    setupTd->token  = make_token(PID_SETUP, deviceAddr, 0, 0, 8);
    setupTd->buffer = ptr32(&s_dmaSetup);
    if (setupTd->buffer == 0) return XFER_BUFFER_ERROR;

    uint8_t tdIdx = 1;
    uint8_t packetCount = 0;
    uint16_t packetLengths[13] = {};
    uint16_t remaining = dataLen;
    uint16_t offset = 0;
    bool dirIn = (setup->bmRequestType & 0x80) != 0;
    uint8_t toggle = 1;
    const uint8_t* source = static_cast<const uint8_t*>(data);

    while (remaining > 0 && tdIdx < 14 && packetCount < 13) {
        const uint16_t chunk = remaining > 64 ? 64 : remaining;
        for (uint16_t i = 0; i < chunk; ++i)
            s_controlPackets[packetCount][i] = dirIn ? 0 : source[offset + i];
        volatile UHCI_TD* td = &s_tds[tdIdx];
        td->link   = ptr32(&s_tds[tdIdx + 1]) | 0x04;
        td->status = (3u << 27) | (1u << 23);
        td->token  = make_token(dirIn ? PID_IN : PID_OUT,
                                deviceAddr, 0, toggle, chunk);
        td->buffer = ptr32(s_controlPackets[packetCount]);
        if (td->buffer == 0) return XFER_BUFFER_ERROR;
        packetLengths[packetCount] = chunk;
        toggle ^= 1;
        offset = static_cast<uint16_t>(offset + chunk);
        remaining = static_cast<uint16_t>(remaining - chunk);
        ++packetCount;
        ++tdIdx;
    }
    if (remaining != 0) return XFER_BUFFER_ERROR;

    volatile UHCI_TD* statusTd = &s_tds[tdIdx++];
    statusTd->link   = 0x01;
    statusTd->status = (3u << 27) | (1u << 23);
    statusTd->token  = make_token(dirIn ? PID_OUT : PID_IN,
                                  deviceAddr, 0, 1, 0);
    statusTd->buffer = 0;

    s_qh.elementLink = ptr32(&s_tds[0]);
    if (s_qh.elementLink == 0) return XFER_BUFFER_ERROR;
    dma_compiler_barrier();
    for (uint8_t i = 0; i < tdIdx; ++i) {
        const TransferStatus status = wait_td(&s_tds[i], 1000000);
        if (status != XFER_SUCCESS) {
            s_qh.elementLink = 0x01;
            serial::puts("[USB-UHCI] control-transfer-failed td=");
            serial::put_hex8(i);
            serial::puts(" status=");
            serial::put_hex8(static_cast<uint8_t>(status));
            serial::puts(" hw=");
            serial::put_hex32(s_tds[i].status);
            serial::puts(" qh-element=");
            serial::put_hex32(s_qh.elementLink);
            serial::puts(" frame-num=");
            serial::put_hex16(uhci_read16(UHCI_FRNUM));
            serial::putc('\n');
            return status;
        }
    }
    dma_compiler_barrier();
    s_qh.elementLink = 0x01;
    dma_compiler_barrier();

    if (dirIn && data) {
        uint8_t* destination = static_cast<uint8_t*>(data);
        uint16_t destinationOffset = 0;
        for (uint8_t packet = 0; packet < packetCount; ++packet) {
            const uint16_t actual = static_cast<uint16_t>(
                (s_tds[packet + 1].status + 1u) & 0x7FFu);
            if (actual > packetLengths[packet]) return XFER_DATA_OVERRUN;
            for (uint16_t i = 0; i < actual; ++i)
                destination[destinationOffset + i] = s_controlPackets[packet][i];
            destinationOffset = static_cast<uint16_t>(destinationOffset + actual);
            if (actual < packetLengths[packet]) break;
        }
    }

    if (setup->bRequest == REQ_SET_ADDRESS && deviceAddr == 0 &&
        setup->wValue < 128) {
        for (uint8_t ep = 0; ep < usb::MAX_ENDPOINTS * 2; ++ep)
            s_dataToggle[setup->wValue][ep] = 0;
    } else if (setup->bRequest == REQ_CLEAR_FEATURE &&
               (setup->bmRequestType & 0x1Fu) == 0x02u &&
               setup->wValue == 0 && setup->wIndex < 256) {
        const uint8_t epAddr = static_cast<uint8_t>(setup->wIndex);
        const uint8_t slot = static_cast<uint8_t>(((epAddr & 0x0Fu) * 2u) +
            ((epAddr & 0x80u) ? 1u : 0u));
        s_dataToggle[deviceAddr][slot] = 0;
    }
    return XFER_SUCCESS;
}
TransferStatus bulk_transfer(uint8_t deviceAddr,
                             uint8_t endpointAddr,
                             void* data,
                             uint16_t dataLen,
                             uint16_t* bytesTransferred)
{
    if (bytesTransferred) *bytesTransferred = 0;
    if (!s_available) return XFER_NOT_SUPPORTED;
    const uint8_t ep = endpointAddr & 0x0F;
    const bool dirIn = (endpointAddr & 0x80) != 0;
    const uint8_t pid = dirIn ? PID_IN : PID_OUT;
    if (deviceAddr >= 128 || ep >= usb::MAX_ENDPOINTS ||
        (dataLen != 0 && data == nullptr)) return XFER_BUFFER_ERROR;
    if (dataLen == 0) return XFER_SUCCESS;

    uint8_t* p = static_cast<uint8_t*>(data);
    uint16_t remaining = dataLen;
    uint16_t transferred = 0;
    const uint8_t slot = static_cast<uint8_t>(ep * 2u + (dirIn ? 1u : 0u));
    uint8_t toggle = s_dataToggle[deviceAddr][slot];
    uint16_t maxPacket = 64;
    const usb::Device* descriptor = kernel::usb::get_device(deviceAddr);
    if (descriptor) {
        for (uint8_t i = 0; i < usb::MAX_ENDPOINTS * 2; ++i)
            if (descriptor->endpoints[i].active &&
                descriptor->endpoints[i].address == endpointAddr &&
                descriptor->endpoints[i].maxPacketSize != 0) {
                maxPacket = descriptor->endpoints[i].maxPacketSize;
                break;
            }
    }
    if (maxPacket > sizeof(s_bulkPacket)) return XFER_BUFFER_ERROR;

    while (remaining > 0) {
        const uint16_t chunk = remaining > maxPacket ? maxPacket : remaining;
        if (!dirIn)
            for (uint16_t i = 0; i < chunk; ++i) s_bulkPacket[i] = p[i];

        volatile UHCI_TD* td = &s_tds[0];
        td->link   = 0x01;
        td->status = (3u << 27) | (1u << 23);
        td->token  = make_token(pid, deviceAddr, ep, toggle, chunk);
        td->buffer = ptr32(s_bulkPacket);
        if (td->buffer == 0) return XFER_BUFFER_ERROR;
        s_qh.elementLink = ptr32(td);
        if (s_qh.elementLink == 0) return XFER_BUFFER_ERROR;
        dma_compiler_barrier();

        const TransferStatus status = wait_td(td, 500000);
        s_qh.elementLink = 0x01;
        if (status != XFER_SUCCESS) {
            if (bytesTransferred) *bytesTransferred = transferred;
            serial::puts("[USB-UHCI] bulk-transfer-failed addr=");
            serial::put_hex8(deviceAddr);
            serial::puts(" endpoint=");
            serial::put_hex8(endpointAddr);
            serial::puts(" status=");
            serial::put_hex8(static_cast<uint8_t>(status));
            serial::puts(" hw=");
            serial::put_hex32(td->status);
            serial::putc('\n');
            return status;
        }
        dma_compiler_barrier();

        const uint16_t actual = static_cast<uint16_t>((td->status + 1u) & 0x7FFu);
        if (actual > chunk) {
            if (bytesTransferred) *bytesTransferred = transferred;
            return XFER_DATA_OVERRUN;
        }
        if (dirIn)
            for (uint16_t i = 0; i < actual; ++i) p[i] = s_bulkPacket[i];
        transferred = static_cast<uint16_t>(transferred + actual);
        p += actual;
        remaining = static_cast<uint16_t>(remaining - actual);
        toggle ^= 1;
        s_dataToggle[deviceAddr][slot] = toggle;

        // A short packet terminates this USB transfer; BOT validates its CSW
        // residue against the byte count returned here.
        if (actual < chunk) break;
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
namespace amd64 {
namespace usb_hci {

bool init()         { return kernel::usb::hci::init(); }
bool is_available() { return kernel::usb::hci::is_available(); }

} // namespace usb_hci
} // namespace amd64
} // namespace arch
} // namespace kernel
