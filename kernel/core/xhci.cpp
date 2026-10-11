//
// xHCI Controller Bring-up — Implementation
//
// Bounded xHCI controller bring-up following the xHCI specification
// revision 1.1.  This implementation covers:
//   - PCI/BAR validation
//   - BIOS/OS ownership handoff
//   - Safe halt/reset/start state machine
//   - DMA/controller structures (DCBAA, command ring, event ring)
//   - Command ring foundation
//   - Event ring/interrupter (polling mode)
//   - Root port enumeration/status
//   - Port status change events
//
// INPUT2 does NOT implement full USB enumeration, device addressing,
// endpoint configuration, or HID report delivery.
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/kernel/xhci.h"
#include "include/kernel/arch.h"
#include "include/kernel/modern_input_diagnostics.h"
#include "include/kernel/mmio.h"
#include "include/kernel/serial_debug.h"

namespace kernel {
namespace usb {
namespace xhci {
namespace controller {

extern "C" char __kernel_start[];
extern "C" char __kernel_end[];

namespace {

#if ARCH_HAS_PORT_IO

// ================================================================
// PCI configuration space access
// ================================================================

static const uint16_t PCI_CONFIG_ADDR = 0x0CF8;
static const uint16_t PCI_CONFIG_DATA = 0x0CFC;

static uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t func,
                           uint8_t offset)
{
    const uint32_t address = 0x80000000u |
                             (static_cast<uint32_t>(bus)  << 16) |
                             (static_cast<uint32_t>(dev)  << 11) |
                             (static_cast<uint32_t>(func) << 8)  |
                             (offset & 0xFCu);
    kernel::arch::outl(PCI_CONFIG_ADDR, address);
    return kernel::arch::inl(PCI_CONFIG_DATA);
}

static uint16_t pci_read16(uint8_t bus, uint8_t dev, uint8_t func,
                           uint8_t offset)
{
    const uint32_t address = 0x80000000u |
                             (static_cast<uint32_t>(bus)  << 16) |
                             (static_cast<uint32_t>(dev)  << 11) |
                             (static_cast<uint32_t>(func) << 8)  |
                             (offset & 0xFCu);
    kernel::arch::outl(PCI_CONFIG_ADDR, address);
    return kernel::arch::inw(static_cast<uint16_t>(PCI_CONFIG_DATA + (offset & 0x02u)));
}

static void pci_write32(uint8_t bus, uint8_t dev, uint8_t func,
                        uint8_t offset, uint32_t value)
{
    const uint32_t address = 0x80000000u |
                             (static_cast<uint32_t>(bus)  << 16) |
                             (static_cast<uint32_t>(dev)  << 11) |
                             (static_cast<uint32_t>(func) << 8)  |
                             (offset & 0xFCu);
    kernel::arch::outl(PCI_CONFIG_ADDR, address);
    kernel::arch::outl(PCI_CONFIG_DATA, value);
}

static void pci_write16(uint8_t bus, uint8_t dev, uint8_t func,
                        uint8_t offset, uint16_t value)
{
    const uint32_t address = 0x80000000u |
                             (static_cast<uint32_t>(bus)  << 16) |
                             (static_cast<uint32_t>(dev)  << 11) |
                             (static_cast<uint32_t>(func) << 8)  |
                             (offset & 0xFCu);
    kernel::arch::outl(PCI_CONFIG_ADDR, address);
    kernel::arch::outw(static_cast<uint16_t>(PCI_CONFIG_DATA + (offset & 0x02u)), value);
}

// ================================================================
// Serial helpers
// ================================================================

static void serial_u32_dec(uint32_t value)
{
    char tmp[10];
    uint32_t n = 0u;
    if (value == 0u) {
        kernel::serial::putc('0');
        return;
    }
    while (value > 0u) {
        tmp[n++] = static_cast<char>('0' + (value % 10u));
        value /= 10u;
    }
    while (n > 0u) {
        kernel::serial::putc(tmp[--n]);
    }
}

static void serial_hex_n(uint32_t value, uint32_t digits)
{
    const char* hex = "0123456789abcdef";
    for (uint32_t i = 0u; i < digits; ++i) {
        const uint32_t shift = (digits - 1u - i) * 4u;
        kernel::serial::putc(hex[(value >> shift) & 0xFu]);
    }
}

static void serial_puts(const char* s)
{
    kernel::serial::puts(s);
}

// ================================================================
// Internal state
// ================================================================

static bool s_initialized = false;
static bool s_running = false;
static XhciControllerState s_state = XhciControllerState::Idle;
static XhciOwnershipState s_ownership = XhciOwnershipState::NoCapability;
static uint64_t s_mmioPhysicalBase = 0;
static uint64_t s_mmioBase = 0; // Verified kernel virtual mapping.
static uint32_t s_pciBar0 = 0u;
static uint32_t s_pciBar1 = 0u;
static uint16_t s_pciCommand = 0u;
static uint8_t s_bus = 0;
static uint8_t s_dev = 0;
static uint8_t s_func = 0;
static int32_t s_diagnosticControllerIndex = -1;
static bool s_dmaReady = false;
static uint8_t s_selectedPort = 0u;
static XhciCapabilities s_caps = {};
static uint8_t s_portCount = 0;

// DMA structures (statically allocated for INPUT2)
static const uint32_t COMMAND_RING_SIZE = 64;
static const uint32_t EVENT_RING_SIZE = 64;
static const uint32_t ERST_ENTRIES = 1;

alignas(64) static XhciTrb s_commandRing[COMMAND_RING_SIZE];
alignas(64) static XhciEventTrb s_eventRing[EVENT_RING_SIZE];
alignas(64) static XhciErstEntry s_erst[ERST_ENTRIES];
alignas(64) static uint64_t s_dcbaa[256];

static const uint32_t MAX_CONTEXT_SIZE   = 64;
static const uint32_t INPUT_CTX_SIZE     = 3 * MAX_CONTEXT_SIZE;
static const uint32_t DEVICE_CTX_SIZE    = 2 * MAX_CONTEXT_SIZE;
static const uint32_t EP0_RING_SIZE      = 8;
static const uint32_t CONTROL_BUF_SIZE   = 256;

alignas(64) static uint8_t  s_inputCtx[INPUT_CTX_SIZE];
alignas(64) static uint8_t  s_deviceCtx[DEVICE_CTX_SIZE];
alignas(64) static XhciTrb  s_ep0Ring[EP0_RING_SIZE];
alignas(64) static uint8_t  s_controlBuf[CONTROL_BUF_SIZE];

static uint32_t s_commandRingIndex = 0;
static uint32_t s_commandRingCycle = 1;
static uint32_t s_eventRingIndex = 0;
static uint32_t s_eventRingCycle = 1;

// ================================================================
// MMIO access helpers
// ================================================================

static volatile uint32_t* mmio_op(uint32_t offset)
{
    return reinterpret_cast<volatile uint32_t*>(
        static_cast<uintptr_t>(s_mmioBase + s_caps.opBase + offset));
}

static volatile uint32_t* mmio_rt(uint32_t offset)
{
    return reinterpret_cast<volatile uint32_t*>(
        static_cast<uintptr_t>(s_mmioBase + s_caps.rtBase + offset));
}

static volatile uint32_t* mmio_port(uint32_t portIndex, uint32_t offset)
{
    return reinterpret_cast<volatile uint32_t*>(
        static_cast<uintptr_t>(s_mmioBase + s_caps.opBase + 0x400 +
                               (portIndex - 1) * 0x10 + offset));
}

static uint32_t op_read32(uint32_t offset)
{
    return *mmio_op(offset);
}

static void op_write32(uint32_t offset, uint32_t value)
{
    *mmio_op(offset) = value;
}

static uint32_t rt_read32(uint32_t offset)
{
    return *mmio_rt(offset);
}

static void rt_write32(uint32_t offset, uint32_t value)
{
    *mmio_rt(offset) = value;
}

static uint32_t port_read32(uint32_t portIndex, uint32_t offset)
{
    return *mmio_port(portIndex, offset);
}

static void port_write32(uint32_t portIndex, uint32_t offset, uint32_t value)
{
    *mmio_port(portIndex, offset) = value;
}

// ================================================================
// DMA address translation (Section 12)
//
// The kernel links at 0x100000 but the bootloader may load it at an
// arbitrary physical base (BootInfo.KernelPhysicalBase).  Device-visible
// DMA addresses are base + (virt - 0x100000).  On Multiboot the load base
// equals the link base, so the identity default (0x100000) is correct.
// ================================================================

static uint64_t s_kernelPhysicalBase = 0x100000ULL;
static bool s_kernelPhysicalBaseValid = true; // Multiboot loads this image at its link base.

static uint64_t kernel_virt_to_phys(uintptr_t virt)
{
    const uint64_t v = static_cast<uint64_t>(virt);
    if (!s_kernelPhysicalBaseValid || v < 0x100000ULL) {
        return 0;
    }
    const uint64_t offset = v - 0x100000ULL;
    if (offset > (~static_cast<uint64_t>(0) - s_kernelPhysicalBase)) {
        return 0;
    }
    return s_kernelPhysicalBase + offset;
}

static bool dma_region_address(const void* pointer, uint32_t size,
                               uint32_t alignment, uint64_t* physicalOut)
{
    if (!s_kernelPhysicalBaseValid || pointer == nullptr) return false;
    return translate_kernel_dma_address(
        static_cast<uint64_t>(reinterpret_cast<uintptr_t>(pointer)),
        static_cast<uint64_t>(reinterpret_cast<uintptr_t>(__kernel_start)),
        static_cast<uint64_t>(reinterpret_cast<uintptr_t>(__kernel_end)),
        s_kernelPhysicalBase, size, alignment, s_caps.ac64, physicalOut);
}

static void diagnostic_stage(modern_input_diagnostics::Stage stage,
                             modern_input_diagnostics::Status status,
                             uint32_t errorCode = 0u,
                             bool hasErrorCode = false,
                             uint32_t controllerStatus = 0u,
                             bool hasControllerStatus = false,
                             uint8_t slotId = 0u, uint8_t portId = 0u,
                             uint8_t endpointId = 0u,
                             uint8_t completionCode = 0u,
                             bool hasCompletionCode = false,
                             const char* detail = nullptr)
{
    modern_input_diagnostics::record_stage(
        s_diagnosticControllerIndex, stage, status, errorCode, hasErrorCode,
        controllerStatus, hasControllerStatus, slotId, portId, endpointId,
        completionCode, hasCompletionCode, detail);
}

static bool validate_dma_prerequisites()
{
    if (s_caps.maxScratchpadBufs != 0u) {
        diagnostic_stage(modern_input_diagnostics::Stage::DmaPrerequisites,
                         modern_input_diagnostics::Status::Unsupported,
                         s_caps.maxScratchpadBufs, true);
        serial_puts("[XHCI] DMA blocked: scratchpad buffers are required but not implemented\n");
        return false;
    }

    uint64_t ignored = 0u;
    const bool valid =
        dma_region_address(s_commandRing, sizeof(s_commandRing), 64u, &ignored) &&
        dma_region_address(s_eventRing, sizeof(s_eventRing), 64u, &ignored) &&
        dma_region_address(s_erst, sizeof(s_erst), 64u, &ignored) &&
        dma_region_address(s_dcbaa, sizeof(s_dcbaa), 64u, &ignored) &&
        dma_region_address(s_inputCtx, sizeof(s_inputCtx), 64u, &ignored) &&
        dma_region_address(s_deviceCtx, sizeof(s_deviceCtx), 64u, &ignored) &&
        dma_region_address(s_ep0Ring, sizeof(s_ep0Ring), 64u, &ignored) &&
        dma_region_address(s_controlBuf, sizeof(s_controlBuf), 64u, &ignored);
    if (!valid) {
        diagnostic_stage(modern_input_diagnostics::Stage::DmaPrerequisites,
                         modern_input_diagnostics::Status::Blocked,
                         s_kernelPhysicalBaseValid ? 2u : 1u, true);
        serial_puts("[XHCI] DMA blocked: physical translation, alignment, or controller address width is invalid\n");
        return false;
    }

    s_dmaReady = true;
    diagnostic_stage(modern_input_diagnostics::Stage::DmaPrerequisites,
                     modern_input_diagnostics::Status::Pass);
    serial_puts("[XHCI] DMA buffer translation/alignment validated; IOMMU state unknown\n");
    return true;
}

static void op_write64(uint32_t offset, uint64_t value)
{
    op_write32(offset + 4u, static_cast<uint32_t>(value >> 32));
    op_write32(offset, static_cast<uint32_t>(value & 0xFFFFFFFFu));
}

static void rt_write64(uint32_t offset, uint64_t value)
{
    rt_write32(offset + 4u, static_cast<uint32_t>(value >> 32));
    rt_write32(offset, static_cast<uint32_t>(value & 0xFFFFFFFFu));
}

// ================================================================
// Doorbell access
// ================================================================

static void ring_doorbell(uint32_t doorbell, uint32_t value)
{
    volatile uint32_t* db = reinterpret_cast<volatile uint32_t*>(
        static_cast<uintptr_t>(s_mmioBase + s_caps.dbBase + doorbell * 4u));
    *db = value;
}

// ================================================================
// Bounded delay
// ================================================================

static void delay_ms(uint32_t ms)
{
    for (volatile uint32_t i = 0; i < ms * 10000; ++i) {}
}

// ================================================================
// Wait helpers with bounded timeout
// ================================================================

static bool wait_for_halted(uint32_t timeoutMs)
{
    for (uint32_t i = 0; i < timeoutMs; ++i) {
        if (op_read32(XHCI_OP_USBSTS) & XHCI_USBSTS_HCHALTED) {
            return true;
        }
        delay_ms(1);
    }
    return false;
}

static bool wait_for_reset_complete(uint32_t timeoutMs)
{
    for (uint32_t i = 0; i < timeoutMs; ++i) {
        if (!(op_read32(XHCI_OP_USBCMD) & XHCI_USBCMD_HCRST)) {
            return true;
        }
        delay_ms(1);
    }
    return false;
}

static bool wait_for_cnr_clear(uint32_t timeoutMs)
{
    for (uint32_t i = 0; i < timeoutMs; ++i) {
        if (!(op_read32(XHCI_OP_USBSTS) & XHCI_USBSTS_CNR)) {
            return true;
        }
        delay_ms(1);
    }
    return false;
}

static bool wait_for_running(uint32_t timeoutMs)
{
    for (uint32_t i = 0; i < timeoutMs; ++i) {
        if (!(op_read32(XHCI_OP_USBSTS) & XHCI_USBSTS_HCHALTED)) {
            return true;
        }
        delay_ms(1);
    }
    return false;
}

// ================================================================
// PCI discovery
// ================================================================

static bool find_xhci_controller()
{
    for (uint16_t bus = 0u; bus < 256u; ++bus) {
        for (uint8_t dev = 0u; dev < 32u; ++dev) {
            for (uint8_t func = 0u; func < 8u; ++func) {
                const uint32_t id = pci_read32(static_cast<uint8_t>(bus),
                                               dev, func, 0x00u);
                if (id == 0xFFFFFFFFu || id == 0u) {
                    continue;
                }

                const uint32_t classReg = pci_read32(
                    static_cast<uint8_t>(bus), dev, func, 0x08u);
                const uint8_t baseClass = static_cast<uint8_t>((classReg >> 24) & 0xFFu);
                const uint8_t subClass  = static_cast<uint8_t>((classReg >> 16) & 0xFFu);
                const uint8_t progIf    = static_cast<uint8_t>((classReg >> 8) & 0xFFu);

                if (baseClass == 0x0Cu && subClass == 0x03u && progIf == 0x30u) {
                    s_bus = static_cast<uint8_t>(bus);
                    s_dev = dev;
                    s_func = func;
                    s_diagnosticControllerIndex =
                        modern_input_diagnostics::find_controller(s_bus, s_dev, s_func);
                    if (s_diagnosticControllerIndex < 0) {
                        modern_input_diagnostics::record_stage(
                            -1, modern_input_diagnostics::Stage::PciDiscovery,
                            modern_input_diagnostics::Status::Blocked,
                            modern_input_diagnostics::MAX_CONTROLLERS, true,
                            0u, false, 0u, 0u, 0u, 0u, false);
                        serial_puts("[XHCI] diagnostic snapshot full; refusing untracked controller initialization\n");
                        return false;
                    }
                    modern_input_diagnostics::select_controller(s_diagnosticControllerIndex);
                    return true;
                }
            }
        }
    }
    return false;
}

// ================================================================
// BAR validation
// ================================================================

static bool validate_bar()
{
    uint64_t requiredSpan = 0u;
    if (!xhci_required_mmio_span(s_caps, &requiredSpan)) {
        static const char* reason = "capability offsets do not produce a representable xHCI MMIO span";
        modern_input_diagnostics::record_mmio_mapping(
            s_diagnosticControllerIndex, "xHCI register-span calculation",
            0u, false, 0u, 0u, reason);
        diagnostic_stage(modern_input_diagnostics::Stage::BarMmio,
                         modern_input_diagnostics::Status::Fail, 0u, false,
                         0u, false, 0u, 0u, 0u, 0u, false, reason);
        return false;
    }

    const XhciBarInfo bar = validate_mmio_bar(s_pciBar0, s_pciBar1, requiredSpan);
    if (!bar.valid) {
        modern_input_diagnostics::record_mmio_base(s_diagnosticControllerIndex,
                                                   bar.base);
        modern_input_diagnostics::record_mmio_mapping(
            s_diagnosticControllerIndex, "PCI BAR decode and span validation",
            requiredSpan, false, 0u, 0u, bar.reason);
        serial_puts("[XHCI] MMIO BAR/span validation failed: ");
        serial_puts(bar.reason);
        serial_puts("\n");
        diagnostic_stage(modern_input_diagnostics::Stage::BarMmio,
                         modern_input_diagnostics::Status::Fail,
                         0u, false, 0u, false, 0u, 0u, 0u, 0u, false,
                         bar.reason);
        return false;
    }

    const uint16_t command = pci_read16(s_bus, s_dev, s_func, 0x04u);
    s_pciCommand = command;
    if ((command & 0x0002u) == 0u) {
        modern_input_diagnostics::record_mmio_configuration(
            s_diagnosticControllerIndex, s_pciBar0, s_pciBar1, command,
            bar.type, bar.ioSpace, bar.is64, bar.prefetchable);
        modern_input_diagnostics::record_mmio_mapping(
            s_diagnosticControllerIndex, "PCI command memory-enable readback",
            requiredSpan, false, 0u, 0u,
            "PCI memory-space decoding is disabled after enable attempt");
        diagnostic_stage(modern_input_diagnostics::Stage::BarMmio,
                         modern_input_diagnostics::Status::Fail,
                         0u, false, 0u, false, 0u, 0u, 0u, 0u, false,
                         "PCI memory-space decoding is disabled");
        return false;
    }

    uint64_t mappedVirtual = 0u;
    kernel::mmio::MappingReport mapping = {};
    const uint32_t mapFlags = kernel::mmio::MAP_FLAG_NON_USER |
                              kernel::mmio::MAP_FLAG_NO_EXEC |
                              kernel::mmio::MAP_FLAG_UNCACHED;
    const bool mappingSucceeded = kernel::mmio::mapForDevice(
        bar.base, requiredSpan, &mappedVirtual, &mapping, mapFlags);
    const XhciMmioAccess access = validate_mmio_access(
        bar, (command & 0x0002u) != 0u, requiredSpan,
        mappingSucceeded && mapping.success, mapping.physicalBase,
        mappedVirtual, mapping.length);
    const char* reason = access.valid ? mapping.reason :
        (!mappingSucceeded && mapping.reason != nullptr
            ? mapping.reason : xhci_mmio_access_failure_name(access.failure));
    modern_input_diagnostics::record_mmio_mapping(
        s_diagnosticControllerIndex, "full xHCI physical-to-virtual mapping",
        requiredSpan, access.valid,
        access.valid ? mappedVirtual : 0u, mapping.length, reason);
    if (!access.valid) {
        serial_puts("[XHCI] MMIO mapping blocked: ");
        serial_puts(reason);
        serial_puts("\n");
        diagnostic_stage(modern_input_diagnostics::Stage::BarMmio,
                         modern_input_diagnostics::Status::Fail,
                         0u, false, 0u, false, 0u, 0u, 0u, 0u, false,
                         reason);
        return false;
    }

    s_mmioPhysicalBase = bar.base;
    s_mmioBase = mappedVirtual;
    modern_input_diagnostics::record_mmio_base(s_diagnosticControllerIndex, bar.base);
    diagnostic_stage(modern_input_diagnostics::Stage::BarMmio,
                     modern_input_diagnostics::Status::Pass);
    serial_puts("[XHCI] MMIO mapped physical=0x");
    kernel::serial::put_hex64(s_mmioPhysicalBase);
    serial_puts(" virtual=0x");
    kernel::serial::put_hex64(s_mmioBase);
    serial_puts(bar.is64 ? " (64-bit)\n" : " (32-bit)\n");
    return true;
}

static bool enable_pci_memory_space(const XhciBarInfo& bar)
{
    uint16_t command = pci_read16(s_bus, s_dev, s_func, 0x04u);
    if ((command & 0x0002u) == 0u) {
        // Use a 16-bit PCI config write so the adjacent status register's
        // write-one-to-clear bits are never echoed back from a dword read.
        pci_write16(s_bus, s_dev, s_func, 0x04u,
                    static_cast<uint16_t>(command | 0x0002u));
        command = pci_read16(s_bus, s_dev, s_func, 0x04u);
    }
    s_pciCommand = command;
    modern_input_diagnostics::record_mmio_configuration(
        s_diagnosticControllerIndex, s_pciBar0, s_pciBar1, command,
        bar.type, bar.ioSpace, bar.is64, bar.prefetchable);
    return (command & 0x0002u) != 0u;
}

static bool map_initial_capabilities(const XhciBarInfo& bar)
{
    static const uint64_t kInitialCapabilitySpan =
        static_cast<uint64_t>(XHCI_CAP_DWORD_COUNT) * sizeof(uint32_t);
    uint64_t mappedVirtual = 0u;
    kernel::mmio::MappingReport mapping = {};
    const uint32_t mapFlags = kernel::mmio::MAP_FLAG_NON_USER |
                              kernel::mmio::MAP_FLAG_NO_EXEC |
                              kernel::mmio::MAP_FLAG_UNCACHED;
    const bool mappingSucceeded = kernel::mmio::mapForDevice(
        bar.base, kInitialCapabilitySpan, &mappedVirtual, &mapping, mapFlags);
    const XhciMmioAccess access = validate_mmio_access(
        bar, (s_pciCommand & 0x0002u) != 0u, kInitialCapabilitySpan,
        mappingSucceeded && mapping.success, mapping.physicalBase,
        mappedVirtual, mapping.length);
    const char* reason = access.valid ? mapping.reason :
        (!mappingSucceeded && mapping.reason != nullptr
            ? mapping.reason : xhci_mmio_access_failure_name(access.failure));
    modern_input_diagnostics::record_mmio_mapping(
        s_diagnosticControllerIndex, "initial capability-register mapping",
        kInitialCapabilitySpan, access.valid,
        access.valid ? mappedVirtual : 0u, mapping.length, reason);
    if (!access.valid) {
        diagnostic_stage(modern_input_diagnostics::Stage::BarMmio,
                         modern_input_diagnostics::Status::Fail,
                         0u, false, 0u, false, 0u, 0u, 0u, 0u, false,
                         reason);
        serial_puts("[XHCI] initial capability mapping blocked: ");
        serial_puts(reason);
        serial_puts("\n");
        return false;
    }

    s_mmioPhysicalBase = bar.base;
    s_mmioBase = mappedVirtual;
    modern_input_diagnostics::record_mmio_base(s_diagnosticControllerIndex, bar.base);
    diagnostic_stage(modern_input_diagnostics::Stage::BarMmio,
                     modern_input_diagnostics::Status::Pass);
    return true;
}

// ================================================================
// Capability parsing
// ================================================================

static bool parse_capabilities()
{
    volatile const uint32_t* mmio =
        reinterpret_cast<volatile const uint32_t*>(
            static_cast<uintptr_t>(s_mmioBase));

    uint32_t capWords[XHCI_CAP_DWORD_COUNT];
    for (uint32_t i = 0u; i < XHCI_CAP_DWORD_COUNT; ++i) {
        capWords[i] = mmio[i];
    }

    s_caps = parse_xhci_capabilities(capWords, XHCI_CAP_DWORD_COUNT);
    if (!s_caps.valid) {
        serial_puts("[XHCI] capability registers invalid\n");
        diagnostic_stage(modern_input_diagnostics::Stage::Capabilities,
                         modern_input_diagnostics::Status::Fail,
                         capWords[0], true);
        return false;
    }

    modern_input_diagnostics::record_capabilities(
        s_diagnosticControllerIndex, s_caps.caplength, s_caps.hciVersion,
        s_caps.maxSlots, s_caps.maxPorts, s_caps.csz ? 64u : 32u,
        s_caps.ac64, 0u, s_caps.maxScratchpadBufs);
    diagnostic_stage(modern_input_diagnostics::Stage::Capabilities,
                     modern_input_diagnostics::Status::Pass);

    s_portCount = s_caps.maxPorts;

    serial_puts("[XHCI] capability registers valid\n");
    serial_puts("[XHCI]   caplen=0x");
    serial_hex_n(s_caps.caplength, 2u);
    serial_puts(" hciver=0x");
    serial_hex_n(s_caps.hciVersion, 4u);
    serial_puts(" slots=");
    serial_u32_dec(s_caps.maxSlots);
    serial_puts(" ports=");
    serial_u32_dec(s_caps.maxPorts);
    serial_puts(" dboff=0x");
    serial_hex_n(s_caps.dboff, 4u);
    serial_puts(" rtsoff=0x");
    serial_hex_n(s_caps.rtsoff, 4u);
    serial_puts(" xecp=0x");
    serial_hex_n(s_caps.xecp, 4u);
    serial_puts("\n");

    return true;
}

// ================================================================
// Ownership handoff
// ================================================================

static bool acquire_ownership()
{
    if (s_caps.xecp == 0u) {
        s_ownership = XhciOwnershipState::NoCapability;
        modern_input_diagnostics::record_ownership_status(
            s_diagnosticControllerIndex,
            modern_input_diagnostics::OwnershipStatus::NotRequired);
        serial_puts("[XHCI] ownership not required (no xECP)\n");
        diagnostic_stage(modern_input_diagnostics::Stage::BiosOwnership,
                         modern_input_diagnostics::Status::Pass);
        return true;
    }

    volatile const uint32_t* mmio =
        reinterpret_cast<volatile const uint32_t*>(
            static_cast<uintptr_t>(s_mmioBase));

    const uint32_t legsup = mmio[s_caps.xecp];
    const uint32_t capId = legsup & 0xFFu;

    if (capId != XHCI_USBLEGSUP_CAPID) {
        s_ownership = XhciOwnershipState::NoCapability;
        modern_input_diagnostics::record_ownership_status(
            s_diagnosticControllerIndex,
            modern_input_diagnostics::OwnershipStatus::NotRequired);
        serial_puts("[XHCI] ownership not required (no legacy support cap)\n");
        diagnostic_stage(modern_input_diagnostics::Stage::BiosOwnership,
                         modern_input_diagnostics::Status::Pass,
                         capId, true);
        return true;
    }

    const XhciOwnershipState initial = detect_ownership(legsup);
    if (initial == XhciOwnershipState::OsOwned) {
        s_ownership = XhciOwnershipState::OsOwned;
        modern_input_diagnostics::record_ownership_status(
            s_diagnosticControllerIndex,
            modern_input_diagnostics::OwnershipStatus::OsOwned);
        serial_puts("[XHCI] ownership already OS-owned\n");
        diagnostic_stage(modern_input_diagnostics::Stage::BiosOwnership,
                         modern_input_diagnostics::Status::Pass,
                         legsup, true, legsup, true);
        return true;
    }

    if (initial == XhciOwnershipState::NoCapability) {
        s_ownership = XhciOwnershipState::NoCapability;
        modern_input_diagnostics::record_ownership_status(
            s_diagnosticControllerIndex,
            modern_input_diagnostics::OwnershipStatus::NotRequired);
        serial_puts("[XHCI] ownership not required\n");
        diagnostic_stage(modern_input_diagnostics::Stage::BiosOwnership,
                         modern_input_diagnostics::Status::Pass,
                         legsup, true, legsup, true);
        return true;
    }

    // BIOS-owned: request OS ownership
    modern_input_diagnostics::record_ownership_status(
        s_diagnosticControllerIndex,
        modern_input_diagnostics::OwnershipStatus::BiosOwned);
    serial_puts("[XHCI] ownership BIOS-owned, requesting handoff\n");

    volatile uint32_t* legsupPtr = const_cast<volatile uint32_t*>(&mmio[s_caps.xecp]);
    *legsupPtr = legsup | XHCI_USBLEGSUP_OS_OWNED;

    // Wait for OS ownership semaphore with bounded timeout
    for (uint32_t i = 0u; i < 1000u; ++i) {
        const uint32_t current = *legsupPtr;
        if (current & XHCI_USBLEGSUP_HC_OS_OWNED_SEM) {
            s_ownership = XhciOwnershipState::OsOwned;
            modern_input_diagnostics::record_ownership_status(
                s_diagnosticControllerIndex,
                modern_input_diagnostics::OwnershipStatus::OsOwned);
            serial_puts("[XHCI] ownership acquired\n");
            diagnostic_stage(modern_input_diagnostics::Stage::BiosOwnership,
                             modern_input_diagnostics::Status::Pass,
                             current, true, current, true);
            return true;
        }
        delay_ms(1);
    }

    s_ownership = XhciOwnershipState::HandoffTimeout;
    modern_input_diagnostics::record_ownership_status(
        s_diagnosticControllerIndex,
        modern_input_diagnostics::OwnershipStatus::HandoffTimeout);
    serial_puts("[XHCI] ownership handoff timed out\n");
    diagnostic_stage(modern_input_diagnostics::Stage::BiosOwnership,
                     modern_input_diagnostics::Status::Timeout,
                     legsup, true, *legsupPtr, true);
    return false;
}

// ================================================================
// Controller halt
// ================================================================

static bool halt_controller()
{
    uint32_t usbcmd = op_read32(XHCI_OP_USBCMD);
    if (!(usbcmd & XHCI_USBCMD_RUN_STOP)) {
        // Already halted
        s_state = XhciControllerState::Halted;
        serial_puts("[XHCI] controller already halted\n");
        diagnostic_stage(modern_input_diagnostics::Stage::ControllerHalt,
                         modern_input_diagnostics::Status::Pass,
                         usbcmd, true, op_read32(XHCI_OP_USBSTS), true);
        return true;
    }

    // Clear Run/Stop
    op_write32(XHCI_OP_USBCMD, usbcmd & ~XHCI_USBCMD_RUN_STOP);

    if (!wait_for_halted(1000u)) {
        s_state = XhciControllerState::Error;
        serial_puts("[XHCI] halt timeout\n");
        diagnostic_stage(modern_input_diagnostics::Stage::ControllerHalt,
                         modern_input_diagnostics::Status::Timeout,
                         op_read32(XHCI_OP_USBCMD), true,
                         op_read32(XHCI_OP_USBSTS), true);
        return false;
    }

    s_state = XhciControllerState::Halted;
    serial_puts("[XHCI] controller halted\n");
    diagnostic_stage(modern_input_diagnostics::Stage::ControllerHalt,
                     modern_input_diagnostics::Status::Pass,
                     op_read32(XHCI_OP_USBCMD), true,
                     op_read32(XHCI_OP_USBSTS), true);
    return true;
}

// ================================================================
// Controller reset
// ================================================================

static bool reset_controller()
{
    // Ensure halted first
    if (s_state != XhciControllerState::Halted) {
        if (!halt_controller()) {
            return false;
        }
    }

    // Set HCRST
    op_write32(XHCI_OP_USBCMD, op_read32(XHCI_OP_USBCMD) | XHCI_USBCMD_HCRST);

    if (!wait_for_reset_complete(1000u)) {
        s_state = XhciControllerState::Error;
        serial_puts("[XHCI] reset timeout\n");
        diagnostic_stage(modern_input_diagnostics::Stage::ControllerReset,
                         modern_input_diagnostics::Status::Timeout,
                         op_read32(XHCI_OP_USBCMD), true,
                         op_read32(XHCI_OP_USBSTS), true);
        return false;
    }
    diagnostic_stage(modern_input_diagnostics::Stage::ControllerReset,
                     modern_input_diagnostics::Status::Pass,
                     op_read32(XHCI_OP_USBCMD), true,
                     op_read32(XHCI_OP_USBSTS), true);

    // Wait for CNR to clear
    if (!wait_for_cnr_clear(1000u)) {
        s_state = XhciControllerState::Error;
        serial_puts("[XHCI] CNR timeout\n");
        diagnostic_stage(modern_input_diagnostics::Stage::ControllerNotReady,
                         modern_input_diagnostics::Status::Timeout,
                         op_read32(XHCI_OP_USBCMD), true,
                         op_read32(XHCI_OP_USBSTS), true);
        return false;
    }

    diagnostic_stage(modern_input_diagnostics::Stage::ControllerNotReady,
                     modern_input_diagnostics::Status::Pass,
                     op_read32(XHCI_OP_USBCMD), true,
                     op_read32(XHCI_OP_USBSTS), true);

    s_state = XhciControllerState::Reset;
    serial_puts("[XHCI] controller reset complete\n");
    return true;
}

// ================================================================
// Ring initialization
// ================================================================

static void init_command_ring()
{
    for (uint32_t i = 0u; i < COMMAND_RING_SIZE; ++i) {
        s_commandRing[i].parameter = 0;
        s_commandRing[i].status = 0;
        s_commandRing[i].control = 0;
    }

    // Link TRB at the end to wrap the ring
    const uint64_t ringBase = kernel_virt_to_phys(
        reinterpret_cast<uintptr_t>(s_commandRing));
    s_commandRing[COMMAND_RING_SIZE - 1].parameter = ringBase;
    s_commandRing[COMMAND_RING_SIZE - 1].status = 0;
    s_commandRing[COMMAND_RING_SIZE - 1].control =
        (TRB_TYPE_LINK << 10) | (1u << 1) | s_commandRingCycle;

    s_commandRingIndex = 0;
    s_commandRingCycle = 1;

    // Program CRCR
    const uint64_t crcr = ringBase | XHCI_CRCR_RCS;
    op_write64(XHCI_OP_CRCR, crcr);

    serial_puts("[XHCI] command ring configured\n");
}

static void init_event_ring()
{
    for (uint32_t i = 0u; i < EVENT_RING_SIZE; ++i) {
        s_eventRing[i].eventData = 0;
        s_eventRing[i].status = 0;
        s_eventRing[i].control = 0;
    }

    // ERST
    const uint64_t eventRingBase = kernel_virt_to_phys(
        reinterpret_cast<uintptr_t>(s_eventRing));
    s_erst[0].ringSegmentBaseAddr = eventRingBase;
    s_erst[0].ringSegmentSize = EVENT_RING_SIZE;
    s_erst[0].reserved = 0;

    // Program ERSTSZ
    rt_write32(XHCI_RT_ERSTSZ, ERST_ENTRIES);

    // Program ERSTBA
    const uint64_t erstba = kernel_virt_to_phys(reinterpret_cast<uintptr_t>(s_erst));
    rt_write64(XHCI_RT_ERSTBA, erstba);

    // Program ERDP
    const uint64_t erdp = eventRingBase;
    rt_write64(XHCI_RT_ERDP, erdp);

    s_eventRingIndex = 0;
    s_eventRingCycle = 1;

    serial_puts("[XHCI] event ring configured\n");
}

// ================================================================
// DCBAA initialization
// ================================================================

static void init_dcbaa()
{
    for (uint32_t i = 0u; i < 256u; ++i) {
        s_dcbaa[i] = 0;
    }

    // Program DCBAAP
    const uint64_t dcbaap = kernel_virt_to_phys(
                                reinterpret_cast<uintptr_t>(s_dcbaa)) &
                            XHCI_DCBAAP_PTR_MASK;
    op_write64(XHCI_OP_DCBAAP, dcbaap);

    serial_puts("[XHCI] DCBAA configured\n");
}

// ================================================================
// Interrupter configuration
// ================================================================

static void init_interrupter()
{
    // Clear IMAN
    rt_write32(XHCI_RT_IMAN, 0);

    // Set IMOD (interrupt moderation)
    rt_write32(XHCI_RT_IMOD, 0);

    serial_puts("[XHCI] interrupter/polling configured\n");
}

// ================================================================
// Page size validation
// ================================================================

static bool validate_page_size()
{
    const uint32_t pageSize = op_read32(XHCI_OP_PAGESIZE);
    // xHCI supports 4K, 8K, 16K, 32K, 64K pages
    // We require 4K pages for simplicity
    if ((pageSize & 0x01u) == 0u) {
        serial_puts("[XHCI] page size not 4K\n");
        diagnostic_stage(modern_input_diagnostics::Stage::DmaPrerequisites,
                         modern_input_diagnostics::Status::Blocked,
                         pageSize, true);
        return false;
    }
    serial_puts("[XHCI] page size validated (4K)\n");
    return true;
}

// ================================================================
// MaxSlots configuration
// ================================================================

static bool configure_max_slots()
{
    const uint8_t maxSlots = s_caps.maxSlots;
    if (maxSlots == 0u || maxSlots > 255u) {
        serial_puts("[XHCI] invalid MaxSlots\n");
        return false;
    }

    op_write32(XHCI_OP_CONFIG, maxSlots);
    serial_puts("[XHCI] MaxSlots configured: ");
    serial_u32_dec(maxSlots);
    serial_puts("\n");
    return true;
}

// ================================================================
// Start controller
// ================================================================

static bool start_controller()
{
    // Set Run/Stop
    op_write32(XHCI_OP_USBCMD, op_read32(XHCI_OP_USBCMD) | XHCI_USBCMD_RUN_STOP);

    if (!wait_for_running(1000u)) {
        s_state = XhciControllerState::Error;
        serial_puts("[XHCI] start timeout (HCHalted did not clear)\n");
        diagnostic_stage(modern_input_diagnostics::Stage::ControllerRunning,
                         modern_input_diagnostics::Status::Timeout,
                         op_read32(XHCI_OP_USBCMD), true,
                         op_read32(XHCI_OP_USBSTS), true);
        return false;
    }

    s_state = XhciControllerState::Running;
    s_running = true;
    serial_puts("[XHCI] controller running\n");
    diagnostic_stage(modern_input_diagnostics::Stage::ControllerRunning,
                     modern_input_diagnostics::Status::Pass,
                     op_read32(XHCI_OP_USBCMD), true,
                     op_read32(XHCI_OP_USBSTS), true);
    return true;
}

// ================================================================
// Event processing
// ================================================================

static bool process_one_event(XhciEventTrb* eventOut)
{
    if (eventOut == nullptr) {
        return false;
    }

    const XhciEventTrb& evt = s_eventRing[s_eventRingIndex];
    const uint32_t cycleBit = evt.control & 0x01u;

    if (cycleBit != s_eventRingCycle) {
        return false; // No more events
    }

    *eventOut = evt;
    ++s_eventRingIndex;

    if (s_eventRingIndex >= EVENT_RING_SIZE) {
        s_eventRingIndex = 0;
        s_eventRingCycle ^= 1u;
    }
    return true;
}

static void update_erdp()
{
    const uint64_t erdp = kernel_virt_to_phys(
        reinterpret_cast<uintptr_t>(&s_eventRing[s_eventRingIndex]));
    rt_write64(XHCI_RT_ERDP, erdp);
}

static uint32_t process_events()
{
    uint32_t processed = 0u;
    XhciEventTrb evt;

    while (process_one_event(&evt)) {
        const uint32_t type = trb_type(reinterpret_cast<const XhciTrb&>(evt));
        const uint32_t cc = trb_completion_code(evt);

        if (type == TRB_TYPE_PORT_STATUS_CHANGE) {
            const XhciPortStatusChangeEvent& psc =
                reinterpret_cast<const XhciPortStatusChangeEvent&>(evt);
            const uint8_t portId = trb_port_id(psc);
            serial_puts("[XHCI] port status change port=");
            serial_u32_dec(portId);
            serial_puts(" cc=");
            serial_puts(completion_code_name((uint8_t)cc));
            serial_puts("\n");
        } else if (type == TRB_TYPE_COMMAND_COMP) {
            serial_puts("[XHCI] command completion slot=");
            serial_u32_dec(event_cmd_comp_slot_id(evt));
            serial_puts(" cc=");
            serial_puts(completion_code_name((uint8_t)cc));
            serial_puts("\n");
        } else if (type == TRB_TYPE_TRANSFER_EVENT) {
            serial_puts("[XHCI] transfer event slot=");
            serial_u32_dec(event_transfer_slot_id(evt));
            serial_puts(" ep=");
            serial_u32_dec(event_transfer_ep_id(evt));
            serial_puts(" len=");
            serial_u32_dec(event_transfer_length(evt));
            serial_puts(" cc=");
            serial_puts(completion_code_name((uint8_t)cc));
            serial_puts("\n");
        } else {
            serial_puts("[XHCI] event type=");
            serial_u32_dec(type);
            serial_puts(" cc=");
            serial_puts(completion_code_name((uint8_t)cc));
            serial_puts("\n");
        }

        ++processed;
    }

    update_erdp();
    return processed;
}

// ================================================================
// Enumeration DMA structures (Section 12)
// ================================================================

static uint32_t s_ep0RingIndex = 0;
static uint32_t s_ep0RingCycle = 1;

static uint32_t context_size()
{
    return s_caps.csz ? XHCI_CONTEXT_SIZE_64 : XHCI_CONTEXT_SIZE_32;
}

// ================================================================
// Command submission and event waiting (Sections 6, 14)
// ================================================================

struct CommandResult {
    bool     valid;
    uint8_t  completionCode;
    uint8_t  slotId;
    uint64_t trbPtr;
};

static bool submit_command(const XhciTrb& cmd, CommandResult* result, uint32_t timeoutMs)
{
    if (result != nullptr) {
        result->valid = false;
        result->completionCode = 0;
        result->slotId = 0;
        result->trbPtr = 0;
    }

    XhciTrb trb = cmd;
    trb.control &= ~0x01u;
    trb.control |= s_commandRingCycle;
    s_commandRing[s_commandRingIndex] = trb;

    ++s_commandRingIndex;
    if (s_commandRingIndex >= COMMAND_RING_SIZE) {
        s_commandRingIndex = 0;
        s_commandRingCycle ^= 1u;
    }

    ring_doorbell(0u, 0u);

    uint32_t elapsed = 0u;
    while (elapsed < timeoutMs) {
        XhciEventTrb evt;
        while (process_one_event(&evt)) {
            if (trb_type(reinterpret_cast<const XhciTrb&>(evt)) == TRB_TYPE_COMMAND_COMP) {
                if (result != nullptr) {
                    result->valid = true;
                    result->completionCode = static_cast<uint8_t>(trb_completion_code(evt));
                    result->slotId = event_cmd_comp_slot_id(evt);
                    result->trbPtr = event_cmd_comp_trb_ptr(evt);
                }
                update_erdp();
                return true;
            }
        }
        delay_ms(1u);
        ++elapsed;
    }

    update_erdp();
    return false;
}

// ================================================================
// Enable Slot (Section 6)
// ================================================================

static bool enable_slot(uint8_t* slotIdOut)
{
    if (slotIdOut != nullptr) {
        *slotIdOut = 0;
    }

    CommandResult result;
    if (!submit_command(trb_enable_slot(0), &result, 1000u)) {
        serial_puts("[XHCI] Enable Slot timeout\n");
        diagnostic_stage(modern_input_diagnostics::Stage::EnableSlot,
                         modern_input_diagnostics::Status::Timeout,
                         1000u, true);
        return false;
    }

    if (result.completionCode != CC_SUCCESS) {
        serial_puts("[XHCI] Enable Slot failed cc=");
        serial_puts(completion_code_name(result.completionCode));
        serial_puts("\n");
        diagnostic_stage(modern_input_diagnostics::Stage::EnableSlot,
                         modern_input_diagnostics::Status::Fail,
                         result.completionCode, false, 0u, false,
                         0u, 0u, 0u, result.completionCode, true);
        return false;
    }

    if (result.slotId == 0u || result.slotId > s_caps.maxSlots) {
        serial_puts("[XHCI] Enable Slot invalid slot id=");
        serial_u32_dec(result.slotId);
        serial_puts("\n");
        diagnostic_stage(modern_input_diagnostics::Stage::EnableSlot,
                         modern_input_diagnostics::Status::Fail,
                         result.slotId, true, 0u, false,
                         result.slotId, 0u, 0u, result.completionCode,
                         result.completionCode != 0u);
        return false;
    }

    if (slotIdOut != nullptr) {
        *slotIdOut = result.slotId;
    }

    serial_puts("[XHCI] Enable Slot -> slot ");
    serial_u32_dec(result.slotId);
    serial_puts("\n");
    diagnostic_stage(modern_input_diagnostics::Stage::EnableSlot,
                     modern_input_diagnostics::Status::Pass,
                     0u, false, 0u, false, result.slotId, 0u,
                     0u, result.completionCode, true);
    return true;
}

// ================================================================
// Port reset (Section 5)
// ================================================================

static bool reset_port(uint8_t portIndex)
{
    uint32_t portsc = port_read32(portIndex, XHCI_PORT_SC);
    // PORTSC change bits are write-one-to-clear. Preserve only Port Power
    // when requesting reset so this operation does not erase unrelated
    // connect/link change evidence collected by the diagnostic snapshot.
    const uint32_t resetRequest = (portsc & XHCI_PORTSC_PP) | XHCI_PORTSC_PR;
    port_write32(portIndex, XHCI_PORT_SC, resetRequest);

    for (uint32_t i = 0u; i < 500u; ++i) {
        portsc = port_read32(portIndex, XHCI_PORT_SC);
        if ((portsc & XHCI_PORTSC_PRC) != 0u) {
            serial_puts("[XHCI] port reset complete port=");
            serial_u32_dec(portIndex);
            serial_puts("\n");
            diagnostic_stage(modern_input_diagnostics::Stage::PortReset,
                             modern_input_diagnostics::Status::Pass,
                             0u, false, portsc, true, 0u, portIndex);
            return true;
        }
        delay_ms(1u);
    }

    serial_puts("[XHCI] port reset timeout port=");
    serial_u32_dec(portIndex);
    serial_puts("\n");
    diagnostic_stage(modern_input_diagnostics::Stage::PortReset,
                     modern_input_diagnostics::Status::Timeout,
                     0u, false, portsc, true, 0u, portIndex);
    return false;
}

// ================================================================
// Address Device (Section 8)
// ================================================================

static uint32_t initial_max_packet_size(uint8_t speed)
{
    switch (speed) {
        case XHCI_SPEED_LOW:
        case XHCI_SPEED_FULL:       return 8u;
        case XHCI_SPEED_HIGH:       return 64u;
        case XHCI_SPEED_SUPER:
        case XHCI_SPEED_SUPER_PLUS: return 512u;
        default:                    return 8u;
    }
}

static bool address_device(uint8_t slotId, uint8_t port, uint8_t speed)
{
    const uint32_t ctxSize = context_size();

    for (uint32_t i = 0u; i < INPUT_CTX_SIZE; ++i) {
        s_inputCtx[i] = 0;
    }

    uint32_t* icc = reinterpret_cast<uint32_t*>(s_inputCtx);
    icc[0] = 0u;
    icc[1] = XHCI_ADD_CONTEXT_FLAG_SLOT | XHCI_ADD_CONTEXT_FLAG_EP0;

    uint32_t* slotCtx = reinterpret_cast<uint32_t*>(s_inputCtx + ctxSize);
    slotCtx[0] = slot_ctx_set_speed(0u, speed) | slot_ctx_set_context_entries(0u, 1u);
    slotCtx[1] = slot_ctx_set_root_port(0u, port);
    slotCtx[4] = slot_ctx_set_interrupter_target(0u, 0u);

    uint32_t* ep0Ctx = reinterpret_cast<uint32_t*>(s_inputCtx + 2u * ctxSize);
    const uint32_t mps = initial_max_packet_size(speed);
    ep0Ctx[0] = ep_ctx_set_max_packet_size(0u, mps);
    ep0Ctx[1] = ep_ctx_set_type(0u, XHCI_EP_TYPE_CONTROL) | ep_ctx_set_cerr(0u, 3u);
    const uint64_t ep0RingPhys = kernel_virt_to_phys(
        reinterpret_cast<uintptr_t>(s_ep0Ring));
    ep0Ctx[2] = static_cast<uint32_t>(ep0RingPhys & 0xFFFFFFFFu);
    ep0Ctx[3] = static_cast<uint32_t>(ep0RingPhys >> 32);

    for (uint32_t i = 0u; i < EP0_RING_SIZE; ++i) {
        s_ep0Ring[i].parameter = 0;
        s_ep0Ring[i].status = 0;
        s_ep0Ring[i].control = 0;
    }
    s_ep0Ring[EP0_RING_SIZE - 1u].parameter = ep0RingPhys;
    s_ep0Ring[EP0_RING_SIZE - 1u].control =
        (TRB_TYPE_LINK << 10) | (1u << 1) | s_ep0RingCycle;
    s_ep0RingIndex = 0;
    s_ep0RingCycle = 1;

    const uint64_t devCtxPhys = kernel_virt_to_phys(
        reinterpret_cast<uintptr_t>(s_deviceCtx));
    if (slotId < 256u) {
        s_dcbaa[slotId] = devCtxPhys & XHCI_DCBAAP_PTR_MASK;
    }

    const uint64_t inputCtxPhys = kernel_virt_to_phys(
        reinterpret_cast<uintptr_t>(s_inputCtx));
    CommandResult result;
    if (!submit_command(trb_address_device(inputCtxPhys, false, slotId), &result, 1000u)) {
        serial_puts("[XHCI] Address Device timeout slot=");
        serial_u32_dec(slotId);
        serial_puts("\n");
        diagnostic_stage(modern_input_diagnostics::Stage::AddressDevice,
                         modern_input_diagnostics::Status::Timeout,
                         1000u, true, 0u, false, slotId, port);
        return false;
    }

    if (result.completionCode != CC_SUCCESS) {
        serial_puts("[XHCI] Address Device failed slot=");
        serial_u32_dec(slotId);
        serial_puts(" cc=");
        serial_puts(completion_code_name(result.completionCode));
        serial_puts("\n");
        diagnostic_stage(modern_input_diagnostics::Stage::AddressDevice,
                         modern_input_diagnostics::Status::Fail,
                         result.completionCode, false, 0u, false,
                         slotId, port, 0u, result.completionCode, true);
        return false;
    }

    serial_puts("[XHCI] Address Device slot=");
    serial_u32_dec(slotId);
    serial_puts(" success\n");
    diagnostic_stage(modern_input_diagnostics::Stage::AddressDevice,
                     modern_input_diagnostics::Status::Pass,
                     0u, false, 0u, false, slotId, port,
                     0u, result.completionCode, true);
    return true;
}

// ================================================================
// EP0 control transfer (Section 9)
// ================================================================

struct ControlTransferResult {
    bool     valid;
    uint8_t  completionCode;
    uint32_t bytesTransferred;
};

static bool ep0_control_transfer(uint8_t slotId, const UsbControlRequest& req,
                                  const void* dataOut, void* dataIn,
                                  ControlTransferResult* result)
{
    if (result != nullptr) {
        result->valid = false;
        result->completionCode = 0;
        result->bytesTransferred = 0;
    }

    const bool hasDataStage = (req.wLength > 0u);
    const bool dataInDir = req.dataIn;

    usb::SetupPacket setup;
    setup.bmRequestType = req.bmRequestType;
    setup.bRequest      = req.bRequest;
    setup.wValue        = req.wValue;
    setup.wIndex        = req.wIndex;
    setup.wLength       = req.wLength;

    const uint8_t trt = hasDataStage ? (dataInDir ? 2u : 3u) : 0u;

    for (uint32_t i = 0u; i < EP0_RING_SIZE; ++i) {
        s_ep0Ring[i].parameter = 0;
        s_ep0Ring[i].status = 0;
        s_ep0Ring[i].control = 0;
    }

    uint32_t idx = 0u;
    s_ep0Ring[idx++] = trb_setup_stage(setup, trt, false);

    if (hasDataStage) {
        const uint64_t bufPhys = kernel_virt_to_phys(
            reinterpret_cast<uintptr_t>(dataInDir ? dataIn : dataOut));
        s_ep0Ring[idx++] = trb_data_stage(bufPhys, req.wLength, dataInDir, false, true);
    }

    const bool statusIn = hasDataStage ? !dataInDir : true;
    s_ep0Ring[idx] = trb_status_stage(statusIn, true);

    for (uint32_t i = 0u; i <= idx; ++i) {
        s_ep0Ring[i].control &= ~0x01u;
        s_ep0Ring[i].control |= s_ep0RingCycle;
    }

    const uint64_t ep0RingPhys = kernel_virt_to_phys(
        reinterpret_cast<uintptr_t>(s_ep0Ring));
    s_ep0Ring[EP0_RING_SIZE - 1u].parameter = ep0RingPhys;
    s_ep0Ring[EP0_RING_SIZE - 1u].control =
        (TRB_TYPE_LINK << 10) | (1u << 1) | s_ep0RingCycle;

    s_ep0RingIndex = 0;
    s_ep0RingCycle = 1;

    ring_doorbell(slotId, 1u);

    uint32_t elapsed = 0u;
    while (elapsed < req.timeoutMs) {
        XhciEventTrb evt;
        while (process_one_event(&evt)) {
            if (trb_type(reinterpret_cast<const XhciTrb&>(evt)) == TRB_TYPE_TRANSFER_EVENT) {
                const uint8_t evtSlot = event_transfer_slot_id(evt);
                const uint8_t evtEp   = event_transfer_ep_id(evt);
                const uint8_t cc      = static_cast<uint8_t>(trb_completion_code(evt));
                const uint32_t residual = event_transfer_length(evt);

                if (evtSlot == slotId && evtEp == 1u) {
                    update_erdp();
                    if (result != nullptr) {
                        result->valid = (cc == CC_SUCCESS || cc == CC_SHORT_PACKET);
                        result->completionCode = cc;
                        result->bytesTransferred = req.wLength - residual;
                    }
                    if (cc == CC_SUCCESS || cc == CC_SHORT_PACKET) {
                        diagnostic_stage(modern_input_diagnostics::Stage::ControlTransfer,
                                         modern_input_diagnostics::Status::Pass,
                                         residual, true, 0u, false, slotId,
                                         0u, evtEp, cc, true);
                        return true;
                    }
                    diagnostic_stage(modern_input_diagnostics::Stage::ControlTransfer,
                                     modern_input_diagnostics::Status::Fail,
                                     residual, true, 0u, false, slotId,
                                     0u, evtEp, cc, true);
                    serial_puts("[XHCI] control transfer failed cc=");
                    serial_puts(completion_code_name(cc));
                    serial_puts("\n");
                    return false;
                }
            }
        }
        delay_ms(1u);
        ++elapsed;
    }

    update_erdp();
    serial_puts("[XHCI] control transfer timeout slot=");
    serial_u32_dec(slotId);
    serial_puts("\n");
    diagnostic_stage(modern_input_diagnostics::Stage::ControlTransfer,
                     modern_input_diagnostics::Status::Timeout,
                     req.timeoutMs, true, 0u, false, slotId,
                     0u, 1u);
    return false;
}

// ================================================================
// GET_DESCRIPTOR (Section 10)
// ================================================================

static bool get_device_descriptor(uint8_t slotId, uint8_t speed,
                                    uint8_t* buf, uint32_t len,
                                    UsbDeviceDescriptorInfo* info)
{
    if (buf == nullptr || info == nullptr) {
        return false;
    }

    UsbControlRequest req = usb_get_device_descriptor_request(static_cast<uint16_t>(len));
    ControlTransferResult result;

    if (!ep0_control_transfer(slotId, req, nullptr, buf, &result)) {
        serial_puts("[XHCI] GET_DESCRIPTOR(Device) failed slot=");
        serial_u32_dec(slotId);
        serial_puts(" cc=");
        serial_puts(completion_code_name(result.completionCode));
        serial_puts("\n");
        diagnostic_stage(modern_input_diagnostics::Stage::DeviceDescriptor,
                         result.completionCode == 0u
                             ? modern_input_diagnostics::Status::Timeout
                             : modern_input_diagnostics::Status::Fail,
                         0u, false, 0u, false, slotId,
                         s_selectedPort,
                         1u, result.completionCode,
                         result.completionCode != 0u);
        return false;
    }

    if (len < 18u) {
        const bool validPrefix = validate_usb_device_descriptor_prefix(
            buf, result.bytesTransferred, speed);
        modern_input_diagnostics::record_device_descriptor(
            s_diagnosticControllerIndex, true, false, 0u, 0u);
        diagnostic_stage(modern_input_diagnostics::Stage::DeviceDescriptor,
                         validPrefix ? modern_input_diagnostics::Status::Pass
                                     : modern_input_diagnostics::Status::Fail,
                         result.bytesTransferred, !validPrefix, 0u, false,
                         slotId, 0u, 1u);
        if (!validPrefix) {
            serial_puts("[XHCI] GET_DESCRIPTOR(Device) invalid 8-byte prefix\n");
            return false;
        }
        return true;
    }

    *info = validate_usb_device_descriptor(buf, result.bytesTransferred, speed);
    modern_input_diagnostics::record_device_descriptor(
        s_diagnosticControllerIndex, true, info->valid,
        info->valid ? info->idVendor : 0u,
        info->valid ? info->idProduct : 0u);
    if (!info->valid) {
        serial_puts("[XHCI] GET_DESCRIPTOR(Device) invalid descriptor\n");
        diagnostic_stage(modern_input_diagnostics::Stage::DeviceDescriptor,
                         modern_input_diagnostics::Status::Fail,
                         result.bytesTransferred, true, 0u, false,
                         slotId, 0u, 1u);
        return false;
    }

    serial_puts("[XHCI] GET_DESCRIPTOR(Device) success\n");
    diagnostic_stage(modern_input_diagnostics::Stage::DeviceDescriptor,
                     modern_input_diagnostics::Status::Pass,
                     result.bytesTransferred, true, 0u, false,
                     slotId, 0u, 1u);
    return true;
}

// ================================================================
// Enumeration orchestration (Section 5)
// ================================================================

static bool enumerate_first_device(EnumeratedDevice* out)
{
    if (out != nullptr) {
        out->valid = false;
    }

    if (!s_running) {
        serial_puts("[XHCI] enumerate: controller not running\n");
        return false;
    }
    if (!s_dmaReady) {
        serial_puts("[XHCI] enumerate: DMA prerequisites were not satisfied\n");
        diagnostic_stage(modern_input_diagnostics::Stage::DmaPrerequisites,
                         modern_input_diagnostics::Status::Blocked);
        return false;
    }

    uint8_t port = 0u;
    uint32_t connectedPorts = 0u;
    for (uint8_t p = 1u; p <= s_portCount; ++p) {
        XhciPortStatus ps;
        if (port_status(p, &ps)) {
            if (ps.connected) {
                ++connectedPorts;
                if (port == 0u) port = p;
            }
        }
    }
    s_selectedPort = port;
    modern_input_diagnostics::record_root_ports(
        s_diagnosticControllerIndex, s_portCount, connectedPorts, port);
    diagnostic_stage(modern_input_diagnostics::Stage::RootPorts,
                     modern_input_diagnostics::Status::Pass,
                     connectedPorts, true);

    if (port == 0u) {
        serial_puts("[XHCI] enumerate: no connected port\n");
        return false;
    }

    serial_puts("[XHCI] enumerate: port ");
    serial_u32_dec(port);
    serial_puts(" connected speed=");
    serial_puts(speed_name(portsc_get_speed(port_read32(port, XHCI_PORT_SC))));
    serial_puts("\n");

    if (!reset_port(port)) {
        return false;
    }

    uint8_t slotId = 0u;
    if (!enable_slot(&slotId)) {
        return false;
    }

    XhciPortStatus ps;
    port_status(port, &ps);
    const uint8_t speed = ps.speed;

    if (!address_device(slotId, port, speed)) {
        return false;
    }

    UsbDeviceDescriptorInfo info;

    if (!get_device_descriptor(slotId, speed, s_controlBuf, 8u, &info)) {
        return false;
    }

    if (!get_device_descriptor(slotId, speed, s_controlBuf, 18u, &info)) {
        return false;
    }

    if (out != nullptr) {
        out->valid = true;
        out->slotId = slotId;
        out->port = port;
        out->speed = speed;
        out->descriptor = info;
    }

    serial_puts("[USB] VID=");
    serial_hex_n(info.idVendor, 4u);
    serial_puts(" PID=");
    serial_hex_n(info.idProduct, 4u);
    serial_puts(" class=");
    serial_hex_n(info.deviceClass, 2u);
    serial_puts(" mps0=");
    serial_u32_dec(info.maxPacketSize0);
    serial_puts(" configs=");
    serial_u32_dec(info.numConfigurations);
    serial_puts("\n");

    if (info.deviceClass == 0x09u) {
        serial_puts("[USB] device is a hub; downstream enumeration deferred to INPUT4\n");
    }

    return true;
}

#endif // ARCH_HAS_PORT_IO

} // anonymous namespace

// ================================================================
// Public interface
// ================================================================

bool init()
{
    if (s_initialized) {
        return s_running;
    }

    serial_puts("[XHCI] init begin\n");

#if ARCH_HAS_PORT_IO
    if (!find_xhci_controller()) {
        serial_puts("[XHCI] PCI device not found\n");
        return false;
    }
    serial_puts("[XHCI] PCI device found\n");

    s_pciBar0 = pci_read32(s_bus, s_dev, s_func, 0x10u);
    s_pciBar1 = pci_read32(s_bus, s_dev, s_func, 0x14u);
    s_pciCommand = pci_read16(s_bus, s_dev, s_func, 0x04u);
    const bool barIoSpace = (s_pciBar0 & 0x01u) != 0u;
    const uint8_t barType = static_cast<uint8_t>((s_pciBar0 >> 1) & 0x03u);
    const bool barIs64 = !barIoSpace && barType == 0x02u;
    const bool barPrefetchable = !barIoSpace && (s_pciBar0 & 0x08u) != 0u;
    modern_input_diagnostics::record_mmio_configuration(
        s_diagnosticControllerIndex, s_pciBar0, s_pciBar1, s_pciCommand,
        barType, barIoSpace, barIs64, barPrefetchable);

    static const uint64_t kInitialCapabilitySpan =
        static_cast<uint64_t>(XHCI_CAP_DWORD_COUNT) * sizeof(uint32_t);
    const XhciBarInfo initialBar = validate_mmio_bar(
        s_pciBar0, s_pciBar1, kInitialCapabilitySpan);
    if (!initialBar.valid) {
        modern_input_diagnostics::record_mmio_mapping(
            s_diagnosticControllerIndex, "initial PCI BAR decode",
            kInitialCapabilitySpan, false,
            0u, 0u, initialBar.reason);
        serial_puts("[XHCI] initial BAR validation failed: ");
        serial_puts(initialBar.reason);
        serial_puts("\n");
        diagnostic_stage(modern_input_diagnostics::Stage::BarMmio,
                         modern_input_diagnostics::Status::Fail,
                         0u, false, 0u, false, 0u, 0u, 0u, 0u, false,
                         initialBar.reason);
        return false;
    }
    modern_input_diagnostics::record_mmio_base(s_diagnosticControllerIndex,
                                               initialBar.base);
    if (!enable_pci_memory_space(initialBar)) {
        static const char* reason = "PCI memory-space decoding is disabled after enable attempt";
        modern_input_diagnostics::record_mmio_mapping(
            s_diagnosticControllerIndex, "PCI command memory-enable readback",
            kInitialCapabilitySpan, false,
            0u, 0u, reason);
        diagnostic_stage(modern_input_diagnostics::Stage::BarMmio,
                         modern_input_diagnostics::Status::Fail,
                         0u, false, 0u, false, 0u, 0u, 0u, 0u, false,
                         reason);
        serial_puts("[XHCI] PCI memory-space decoding could not be enabled\n");
        return false;
    }
    if (!map_initial_capabilities(initialBar)) {
        return false;
    }

    if (!parse_capabilities()) {
        return false;
    }

    if (!validate_bar()) {
        return false;
    }

    // Do not hand DMA addresses to hardware until every static ring/context
    // address has a verified physical translation, alignment, and address
    // width. Scratchpad support is deliberately a hard prerequisite.
    if (!validate_dma_prerequisites()) {
        return false;
    }

    if (!acquire_ownership()) {
        return false;
    }

    if (!halt_controller()) {
        return false;
    }

    if (!reset_controller()) {
        return false;
    }

    if (!validate_page_size()) {
        return false;
    }

    if (!configure_max_slots()) {
        diagnostic_stage(modern_input_diagnostics::Stage::RingInitialization,
                         modern_input_diagnostics::Status::Blocked,
                         s_caps.maxSlots, true);
        return false;
    }

    init_dcbaa();
    init_command_ring();
    init_event_ring();
    init_interrupter();
    diagnostic_stage(modern_input_diagnostics::Stage::RingInitialization,
                     modern_input_diagnostics::Status::Pass);

    if (!start_controller()) {
        return false;
    }

    uint32_t connectedPorts = 0u;
    uint8_t firstConnectedPort = 0u;
    for (uint8_t port = 1u; port <= s_portCount; ++port) {
        XhciPortStatus status;
        if (port_status(port, &status) && status.connected) {
            ++connectedPorts;
            if (firstConnectedPort == 0u) firstConnectedPort = port;
        }
    }
    modern_input_diagnostics::record_root_ports(
        s_diagnosticControllerIndex, s_portCount, connectedPorts,
        firstConnectedPort);
    diagnostic_stage(modern_input_diagnostics::Stage::RootPorts,
                     modern_input_diagnostics::Status::Pass,
                     connectedPorts, true);

    s_initialized = true;
    serial_puts("[XHCI] init complete\n");
    return true;
#else
    serial_puts("[XHCI] not supported on this architecture\n");
    return false;
#endif
}

bool is_running()
{
    return s_running;
}

uint32_t poll_events()
{
#if ARCH_HAS_PORT_IO
    if (!s_running) {
        return 0u;
    }
    return process_events();
#else
    return 0u;
#endif
}

XhciControllerState get_state()
{
    return s_state;
}

XhciOwnershipState get_ownership_state()
{
    return s_ownership;
}

uint8_t port_count()
{
    return s_portCount;
}

bool port_status(uint8_t portIndex, XhciPortStatus* out)
{
    if (out == nullptr || portIndex == 0u || portIndex > s_portCount) {
        return false;
    }

#if ARCH_HAS_PORT_IO
    const uint32_t portsc = port_read32(portIndex, XHCI_PORT_SC);
    *out = decode_port_status(portIndex, portsc);
    return true;
#else
    return false;
#endif
}

uint64_t mmio_base()
{
    return s_mmioPhysicalBase;
}

const XhciCapabilities* capabilities()
{
    return &s_caps;
}

// Set the kernel physical base for DMA address translation.  Must be called
// before enumerate_device() on UEFI boot (see pci_audio::set_kernel_physical_base).
void set_kernel_physical_base(uint64_t physicalBase)
{
    if (physicalBase != 0u) {
        s_kernelPhysicalBase = physicalBase;
        s_kernelPhysicalBaseValid = true;
    } else {
        s_kernelPhysicalBaseValid = false;
    }
}

// Enumerate the first connected USB device: port reset, Enable Slot,
// Address Device, GET_DESCRIPTOR(Device).  Returns true on success.
bool enumerate_device(EnumeratedDevice* out)
{
#if ARCH_HAS_PORT_IO
    return enumerate_first_device(out);
#else
    (void)out;
    return false;
#endif
}

} // namespace controller
} // namespace xhci
} // namespace usb
} // namespace kernel
