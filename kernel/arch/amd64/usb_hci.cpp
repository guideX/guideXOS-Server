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
#include "include/arch/uhci_transfer_logic.h"
#include <kernel/usb.h>
#include <kernel/serial_debug.h>
#include <kernel/pit.h>
#include <stddef.h>

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
static const uint16_t UHCI_STS_HOST_SYSTEM_ERROR = 1u << 3;
static const uint16_t UHCI_STS_HOST_PROCESS_ERROR = 1u << 4;
static const uint16_t UHCI_STS_HALTED = 1u << 5;

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
static uint8_t  s_controllerBus = 0;
static uint8_t  s_controllerDevice = 0;
static uint8_t  s_controllerFunction = 0;
static uint64_t s_kernelPhysicalBase = 0x100000ULL;

// Frame list — must be in the low 4 GB for UHCI (32-bit DMA).
// Using alignas for C++14 compatibility.
alignas(4096) static volatile uint32_t s_frameList[1024];

typedef arch::amd64::uhci::TransferDescriptor UHCI_TD;
typedef arch::amd64::uhci::QueueHead UHCI_QH;

alignas(16) static volatile UHCI_TD s_tds[16];
alignas(16) static volatile UHCI_QH s_qh;
alignas(16) static usb::SetupPacket s_dmaSetup;
alignas(16) static volatile uint8_t s_controlPackets[13][64];
alignas(16) static volatile uint8_t s_bulkPackets[13][64];
static uint8_t s_dataToggle[128][usb::MAX_ENDPOINTS * 2];
#if defined(GXOS_DM14_QEMU_USB_HOTPLUG_PROOF)
static bool s_testBulkOutDisconnectGate = false;
#endif

static inline void dma_compiler_barrier()
{
    // UHCI owns descriptors in coherent WB memory. MFENCE publishes descriptor
    // stores before the controller can fetch them and orders CPU reads after
    // controller writes; the memory clobber also prevents compiler motion.
    asm volatile("mfence" ::: "memory");
}

static void yield_to_controller()
{
    uint64_t flags = 0;
    asm volatile("pushfq; popq %0" : "=r"(flags));
    if (flags & (1ULL << 9)) {
        // Let timer and host-async completions run while waiting. UHCI frames
        // and QEMU block I/O completion are event driven; a guest busy loop can
        // keep the vCPU running without giving either event a chance to fire.
        asm volatile("sti; hlt" ::: "memory");
    } else {
        asm volatile("pause" ::: "memory");
    }
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
                s_controllerBus = static_cast<uint8_t>(bus);
                s_controllerDevice = dev;
                s_controllerFunction = func;
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
    return arch::amd64::uhci::make_token(pid, addr, ep, toggle, maxLen);
}

static const uint32_t UHCI_TD_STATUS_ACTIVE = arch::amd64::uhci::TD_ACTIVE;
static const uint32_t UHCI_FRAME_MASK = arch::amd64::uhci::FRAME_NUMBER_MASK;
static const uint32_t UHCI_CONTROL_TIMEOUT_FRAMES = 250u;
static const uint32_t UHCI_BULK_TIMEOUT_FRAMES = 1000u;

static void initialize_td(volatile UHCI_TD* td, uint32_t link,
                          uint32_t token, uint32_t buffer,
                          bool shortPacketDetect = false)
{
    // The caller owns this TD and the schedule does not reference it yet.
    // Keep the controller-owned status inactive while replacing its payload,
    // then set ACTIVE as the final descriptor store.
    arch::amd64::uhci::prepare_descriptor(td, link, token, buffer);
    dma_compiler_barrier();
    td->status = arch::amd64::uhci::initial_active_status(shortPacketDetect);
}

// Pointer cast helper — truncates 64-bit pointer to 32-bit for UHCI DMA
static uint32_t ptr32(const volatile void* p)
{
    const uint64_t virt = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(p));
    uint32_t physical = 0;
    return arch::amd64::uhci::virtual_to_dma(
        s_kernelPhysicalBase, virt, physical) ? physical : 0;
}

// ================================================================
// Wait for TD completion
// ================================================================

static TransferStatus decode_td_status(uint32_t status)
{
    using namespace arch::amd64::uhci;
    const Observation result = observe(status, 0, 0, 0, 0);
    switch (result) {
        case OBSERVATION_STALL: return XFER_STALL;
        case OBSERVATION_DATA_BUFFER_ERROR: return XFER_DATA_OVERRUN;
        case OBSERVATION_BABBLE:
        case OBSERVATION_BITSTUFF: return XFER_ERROR;
        case OBSERVATION_CRC_TIMEOUT: return XFER_TIMEOUT;
        case OBSERVATION_NAK: return XFER_NAK;
        default: return XFER_SUCCESS;
    }
}

static TransferStatus wait_td(volatile UHCI_TD* td, uint32_t timeoutFrames,
                                  uint8_t monitoredAddress = 0xFFu)
{
    const uint16_t startFrame = static_cast<uint16_t>(
        uhci_read16(UHCI_FRNUM) & UHCI_FRAME_MASK);
    const uint32_t tdPhysical = ptr32(td);
    const uint32_t maxPollsWithoutFrameProgress = 5000000u;
    const uint32_t controllerPollInterval = 0x100u;

    // FRNUM reads sample controller progress. Periodically yielding through
    // HLT also gives the timer and asynchronous device I/O completions time to
    // run; a memory or port-I/O busy loop alone can starve those events.
    for (uint32_t poll = 0; poll < maxPollsWithoutFrameProgress; ++poll) {
        dma_compiler_barrier();
        const uint32_t status = td->status;
        if ((status & UHCI_TD_STATUS_ACTIVE) == 0) {
            const TransferStatus decoded = decode_td_status(status);
            if (decoded != XFER_SUCCESS) {
                serial::puts("[USB-UHCI] td-completion-error status=0x");
                serial::put_hex32(status);
                serial::puts(" decoded=");
                serial::put_hex8(static_cast<uint8_t>(decoded));
                serial::putc('\n');
            }
            return decoded;
        }

        if ((poll & (controllerPollInterval - 1u)) == 0) {
            if (monitoredAddress != 0xFFu &&
                !kernel::usb::device_online(monitoredAddress))
                return XFER_CANCELLED;
            const uint16_t controllerStatus = uhci_read16(UHCI_USBSTS);
            if (controllerStatus & (UHCI_STS_HOST_SYSTEM_ERROR |
                                    UHCI_STS_HOST_PROCESS_ERROR |
                                    UHCI_STS_HALTED)) {
                serial::puts("[USB-UHCI] controller-error status=0x");
                serial::put_hex16(controllerStatus);
                serial::puts(" io-base=0x");
                serial::put_hex16(s_ioBase);
                serial::puts(" command=0x");
                serial::put_hex16(uhci_read16(UHCI_USBCMD));
                serial::puts(" pci-id=0x");
                serial::put_hex32(pci_read32(0, 3, 0, 0));
                serial::puts(" pci-command=0x");
                serial::put_hex16(static_cast<uint16_t>(
                    pci_read32(0, 3, 0, 0x04)));
                serial::puts(" pci-bar4=0x");
                serial::put_hex32(pci_read32(0, 3, 0, 0x20));
                serial::puts(" interrupt-enable=0x");
                serial::put_hex16(uhci_read16(UHCI_USBINTR));
                serial::puts(" frnum=0x");
                serial::put_hex16(uhci_read16(UHCI_FRNUM));
                serial::puts(" frame-base=0x");
                serial::put_hex32(uhci_read32(UHCI_FRBASEADD));
                serial::puts(" sof-mod=0x");
                serial::put_hex16(uhci_read16(UHCI_SOFMOD));
                serial::puts(" port0=0x");
                serial::put_hex16(uhci_read16(UHCI_PORTSC1));
                serial::puts(" port1=0x");
                serial::put_hex16(uhci_read16(UHCI_PORTSC2));
                serial::puts(" qh-element=0x");
                serial::put_hex32(s_qh.elementLink);
                serial::puts(" td-token=0x");
                serial::put_hex32(td->token);
                serial::putc('\n');
                return XFER_ERROR;
            }

            const uint16_t currentFrame = static_cast<uint16_t>(
                uhci_read16(UHCI_FRNUM) & UHCI_FRAME_MASK);
            if (arch::amd64::uhci::frame_deadline_expired(
                    startFrame, currentFrame,
                    static_cast<uint16_t>(timeoutFrames))) {
                serial::puts("[USB-UHCI] td-frame-timeout start=");
                serial::put_hex16(startFrame);
                serial::puts(" current=");
                serial::put_hex16(currentFrame);
                serial::puts(" qh-element=0x");
                serial::put_hex32(s_qh.elementLink);
                serial::puts(" td-status=0x");
                serial::put_hex32(status);
                serial::puts(" td-link=0x");
                serial::put_hex32(td->link);
                serial::putc('\n');
                return XFER_TIMEOUT;
            }

            const uint32_t queueElement = s_qh.elementLink;
            const arch::amd64::uhci::Observation result =
                arch::amd64::uhci::observe(status, queueElement,
                    tdPhysical, td->link, controllerStatus);
            if (result == arch::amd64::uhci::OBSERVATION_CONTROLLER_ERROR) {
                serial::puts("[USB-UHCI] controller-error status=0x");
                serial::put_hex16(controllerStatus);
                serial::puts(" td-status=0x");
                serial::put_hex32(status);
                serial::putc('\n');
                return XFER_ERROR;
            }
            if (result == arch::amd64::uhci::OBSERVATION_QH_ADVANCED_ACTIVE) {
                serial::puts("[USB-UHCI] qh-advanced-active qh=0x");
                serial::put_hex32(queueElement);
                serial::puts(" td=0x");
                serial::put_hex32(tdPhysical);
                serial::puts(" link=0x");
                serial::put_hex32(td->link);
                serial::putc('\n');
                return XFER_ERROR;
            }
            yield_to_controller();
        }

        asm volatile("pause" ::: "memory");
    }
    serial::puts("[USB-UHCI] td-poll-limit start=");
    serial::put_hex16(startFrame);
    serial::puts(" current=");
    serial::put_hex16(static_cast<uint16_t>(
        uhci_read16(UHCI_FRNUM) & UHCI_FRAME_MASK));
    serial::puts(" qh-element=0x");
    serial::put_hex32(s_qh.elementLink);
    serial::puts(" td-status=0x");
    serial::put_hex32(td->status);
    serial::puts(" td-link=0x");
    serial::put_hex32(td->link);
    serial::putc('\n');
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
        s_frameList[i] = arch::amd64::uhci::frame_list_qh_link(ptr32(&s_qh));
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

bool get_controller_pci_location(uint32_t* segment, uint8_t* bus,
                                 uint8_t* device, uint8_t* function)
{
    if (!s_available || !segment || !bus || !device || !function)
        return false;
    *segment = 0;
    *bus = s_controllerBus;
    *device = s_controllerDevice;
    *function = s_controllerFunction;
    return true;
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

bool port_connection_change_pending(uint8_t port)
{
    if (port >= 2) return false;
    const uint16_t portReg = static_cast<uint16_t>(UHCI_PORTSC1 + port * 2);
    return (uhci_read16(portReg) & UHCI_PORT_CONNECT_CHG) != 0;
}

#if defined(GXOS_DM14_QEMU_USB_HOTPLUG_PROOF)
void test_arm_bulk_out_disconnect_gate()
{
    s_testBulkOutDisconnectGate = true;
}

bool test_wait_for_disconnect(uint8_t deviceAddr, const char* marker)
{
    serial::puts("[DM14-QEMU] wait=");
    serial::puts(marker ? marker : "disconnect");
    serial::putc('\n');
    const uint64_t deadline = kernel::pit::ticks() + 1000u;
    while (kernel::usb::device_online(deviceAddr) &&
           kernel::pit::ticks() < deadline)
        yield_to_controller();
    return !kernel::usb::device_online(deviceAddr);
}
#endif

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
    const uint32_t setupLink = arch::amd64::uhci::td_depth_link(
        ptr32(&s_tds[1]));
    const uint32_t setupBuffer = ptr32(&s_dmaSetup);
    if (setupLink == 0x04 || setupBuffer == 0) return XFER_BUFFER_ERROR;
    initialize_td(setupTd, setupLink,
        make_token(PID_SETUP, deviceAddr, 0, 0, 8), setupBuffer);

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
        const uint32_t nextLink = arch::amd64::uhci::td_depth_link(
            ptr32(&s_tds[tdIdx + 1]));
        const uint32_t packetBuffer = ptr32(s_controlPackets[packetCount]);
        if (nextLink == 0x04 || packetBuffer == 0) return XFER_BUFFER_ERROR;
        initialize_td(td, nextLink,
            make_token(dirIn ? PID_IN : PID_OUT,
                       deviceAddr, 0, toggle, chunk), packetBuffer, dirIn);
        packetLengths[packetCount] = chunk;
        toggle ^= 1;
        offset = static_cast<uint16_t>(offset + chunk);
        remaining = static_cast<uint16_t>(remaining - chunk);
        ++packetCount;
        ++tdIdx;
    }
    if (remaining != 0) return XFER_BUFFER_ERROR;

    volatile UHCI_TD* statusTd = &s_tds[tdIdx++];
    initialize_td(statusTd, 0x01,
        make_token(dirIn ? PID_OUT : PID_IN, deviceAddr, 0, 1, 0), 0);

    const uint32_t firstTdPhysical = ptr32(&s_tds[0]);
    if (firstTdPhysical == 0 || (firstTdPhysical & 0x0Fu) != 0)
        return XFER_BUFFER_ERROR;
    dma_compiler_barrier();
    s_qh.elementLink = firstTdPhysical;
    dma_compiler_barrier();
    for (uint8_t i = 0; i < tdIdx; ++i) {
        const TransferStatus status = wait_td(
            &s_tds[i], UHCI_CONTROL_TIMEOUT_FRAMES,
            deviceAddr != 0 && kernel::usb::get_device(deviceAddr)
                ? deviceAddr : 0xFFu);
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
    const uint8_t initialToggle = toggle;
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
    if (maxPacket > sizeof(s_bulkPackets[0])) return XFER_BUFFER_ERROR;

    while (remaining > 0) {
        uint16_t batchLengths[13] = {};
        uint8_t batchCount = 0;
        uint16_t batchBytes = 0;
        uint8_t batchToggle = toggle;

        // Queue a bounded packet chain. Keeping every TD and packet buffer
        // distinct lets UHCI advance through a multi-packet BOT phase without
        // the CPU having to recycle one descriptor between adjacent frames.
        while (batchBytes < remaining && batchCount < 13u) {
            const uint16_t left = static_cast<uint16_t>(remaining - batchBytes);
            const uint16_t chunk = left > maxPacket ? maxPacket : left;
            volatile UHCI_TD* td = &s_tds[batchCount];
            const uint32_t tdPhysical = ptr32(td);
            const uint32_t tdBuffer = ptr32(s_bulkPackets[batchCount]);
            if (tdPhysical == 0 || tdBuffer == 0 ||
                (tdPhysical & 0x0Fu) != 0) return XFER_BUFFER_ERROR;

            if (!dirIn) {
                for (uint16_t i = 0; i < chunk; ++i)
                    s_bulkPackets[batchCount][i] =
                        p[transferred + batchBytes + i];
            }
            const bool hasNext = batchCount + 1u < 13u &&
                batchBytes + chunk < remaining;
            const uint32_t nextLink = hasNext
                ? arch::amd64::uhci::td_depth_link(ptr32(&s_tds[batchCount + 1u]))
                : 0x01u;
            initialize_td(td, nextLink,
                make_token(pid, deviceAddr, ep, batchToggle, chunk), tdBuffer,
                dirIn);
            batchLengths[batchCount] = chunk;
            batchBytes = static_cast<uint16_t>(batchBytes + chunk);
            batchToggle ^= 1u;
            ++batchCount;
        }

        dma_compiler_barrier();
        const uint32_t firstTdPhysical = ptr32(&s_tds[0]);
        if (firstTdPhysical == 0) return XFER_BUFFER_ERROR;
        s_qh.elementLink = firstTdPhysical;
        dma_compiler_barrier();
#if defined(GXOS_DM14_QEMU_USB_HOTPLUG_PROOF)
        if (!dirIn && s_testBulkOutDisconnectGate) {
            s_testBulkOutDisconnectGate = false;
            serial::puts("[DM14-QEMU] wait=data-out-active\n");
        }
#endif

        bool shortPacket = false;
        for (uint8_t packet = 0; packet < batchCount; ++packet) {
            volatile UHCI_TD* td = &s_tds[packet];
            const TransferStatus status = wait_td(
                td, UHCI_BULK_TIMEOUT_FRAMES, deviceAddr);
            if (status != XFER_SUCCESS) {
                const uint32_t failedQhElement = s_qh.elementLink;
                const uint32_t failedTdPhysical = ptr32(td);
                const uint32_t failedTdStatus = td->status;
                const uint32_t failedTdToken = td->token;
                const uint32_t failedTdBuffer = td->buffer;
                const uint16_t failedFrame = uhci_read16(UHCI_FRNUM);
                const uint16_t failedControllerStatus =
                    uhci_read16(UHCI_USBSTS);
                const uint16_t failedControllerCommand =
                    uhci_read16(UHCI_USBCMD);
                const uint16_t failedPort0 = uhci_read16(UHCI_PORTSC1);
                const uint16_t failedPort1 = uhci_read16(UHCI_PORTSC2);
                uint8_t failedBufferPrefix[13] = {};
                for (uint8_t i = 0; i < sizeof(failedBufferPrefix); ++i)
                    failedBufferPrefix[i] = s_bulkPackets[packet][i];
                s_qh.elementLink = 0x01;
                dma_compiler_barrier();
                if (bytesTransferred) *bytesTransferred = transferred;
                serial::puts("[USB-UHCI] bulk-transfer-failed addr=");
                serial::put_hex8(deviceAddr);
                serial::puts(" endpoint=");
                serial::put_hex8(endpointAddr);
                serial::puts(" status=");
                serial::put_hex8(static_cast<uint8_t>(status));
                serial::puts(" requested=");
                serial::put_hex16(dataLen);
                serial::puts(" transferred=");
                serial::put_hex16(transferred);
                serial::puts(" batch-packet=");
                serial::put_hex8(packet);
                serial::puts(" toggle-start=");
                serial::put_hex8(initialToggle);
                serial::puts(" toggle-next=");
                serial::put_hex8(toggle);
                serial::puts(" hw=");
                serial::put_hex32(failedTdStatus);
                serial::puts(" qh-va=0x");
                serial::put_hex64(reinterpret_cast<uint64_t>(&s_qh));
                serial::puts(" qh-pa=0x");
                serial::put_hex32(ptr32(&s_qh));
                serial::puts(" qh-head=0x");
                serial::put_hex32(s_qh.headLink);
                serial::puts(" qh-element=0x");
                serial::put_hex32(failedQhElement);
                serial::puts(" td-va=0x");
                serial::put_hex64(reinterpret_cast<uint64_t>(td));
                serial::puts(" td-pa=0x");
                serial::put_hex32(failedTdPhysical);
                serial::puts(" td-token=0x");
                serial::put_hex32(failedTdToken);
                serial::puts(" td-buffer-pa=0x");
                serial::put_hex32(failedTdBuffer);
                serial::puts(" dma-buffer-va=0x");
                serial::put_hex64(reinterpret_cast<uint64_t>(
                    s_bulkPackets[packet]));
                serial::puts(" dma-buffer-pa=0x");
                serial::put_hex32(ptr32(s_bulkPackets[packet]));
                serial::puts(" max-packet=0x");
                serial::put_hex16(maxPacket);
                serial::puts(" actual=0x");
                serial::put_hex16(arch::amd64::uhci::decode_actual_length(
                    failedTdStatus));
                serial::puts(" timeout-frames=0x");
                serial::put_hex32(UHCI_BULK_TIMEOUT_FRAMES);
                serial::puts(" frnum=0x");
                serial::put_hex16(failedFrame);
                serial::puts(" usbsts=0x");
                serial::put_hex16(failedControllerStatus);
                serial::puts(" usbcmd=0x");
                serial::put_hex16(failedControllerCommand);
                serial::puts(" port0=0x");
                serial::put_hex16(failedPort0);
                serial::puts(" port1=0x");
                serial::put_hex16(failedPort1);
                serial::puts(" dma-first13=");
                for (uint8_t i = 0; i < 13; ++i)
                    serial::put_hex8(failedBufferPrefix[i]);
                serial::putc('\n');
                return status;
            }
            dma_compiler_barrier();

            const uint16_t actual =
                arch::amd64::uhci::decode_actual_length(td->status);
            if (actual > batchLengths[packet]) {
                s_qh.elementLink = 0x01;
                if (bytesTransferred) *bytesTransferred = transferred;
                return XFER_DATA_OVERRUN;
            }
            if (dirIn) {
                for (uint16_t i = 0; i < actual; ++i)
                    p[transferred + i] = s_bulkPackets[packet][i];
            }
            transferred = static_cast<uint16_t>(transferred + actual);
            remaining = static_cast<uint16_t>(remaining - actual);
            toggle ^= 1u;
            s_dataToggle[deviceAddr][slot] = toggle;
            if (actual < batchLengths[packet]) {
                shortPacket = true;
                break;
            }
        }

        dma_compiler_barrier();
        s_qh.elementLink = 0x01;
        dma_compiler_barrier();
        if (shortPacket) break;
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
