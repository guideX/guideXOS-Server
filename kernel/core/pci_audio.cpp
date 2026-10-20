// PCI Audio Driver � Implementation
//
// Intel HDA controller discovery, CORB/RIRB initialisation, codec
// enumeration, and PCM stream management.
//
// The actual PCI config-space and MMIO access is delegated to
// arch-specific pci_audio backends.
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/kernel/pci_audio.h"
#include "include/kernel/arch.h"
#include "include/kernel/modern_input_diagnostics.h"

#if defined(GXOS_INPUT3D_DIAGNOSTICS)
#include "include/kernel/desktop.h"
#include "include/kernel/serial_debug.h"
#endif

namespace kernel {
namespace pci_audio {

// ================================================================
// Internal state
// ================================================================

static AudioController s_controllers[MAX_AUDIO_CONTROLLERS];
static uint8_t         s_controllerCount = 0;

#if defined(GXOS_INPUT3D_DIAGNOSTICS)
static bool s_startupAudioDiagnosticsActive = false;
static uint32_t s_startupVerbCount = 0u;
static modern_input_diagnostics::StartupOperationContext s_pciDiagnosticContext = {};

static void set_pci_diagnostic_context(
    uint8_t bus, uint8_t device, uint8_t function, uint8_t configOffset,
    uint16_t vendorId, uint16_t deviceId,
    uint8_t baseClass, uint8_t subClass, uint8_t progIf,
    bool hasVendorDevice, bool hasClass)
{
    s_pciDiagnosticContext = {};
    s_pciDiagnosticContext.bus = bus;
    s_pciDiagnosticContext.device = device;
    s_pciDiagnosticContext.function = function;
    s_pciDiagnosticContext.configOffset = configOffset;
    s_pciDiagnosticContext.vendorId = vendorId;
    s_pciDiagnosticContext.deviceId = deviceId;
    s_pciDiagnosticContext.baseClass = baseClass;
    s_pciDiagnosticContext.subClass = subClass;
    s_pciDiagnosticContext.progIf = progIf;
    s_pciDiagnosticContext.hasPciAddress = true;
    s_pciDiagnosticContext.hasConfigOffset = true;
    s_pciDiagnosticContext.hasVendorDevice = hasVendorDevice;
    s_pciDiagnosticContext.hasClass = hasClass;
}

static modern_input_diagnostics::StartupOperationContext
audio_controller_diagnostic_context(const AudioController* ctrl)
{
    modern_input_diagnostics::StartupOperationContext context = {};
    if (ctrl != nullptr) {
        context.bus = ctrl->pciBus;
        context.device = ctrl->pciDev;
        context.function = ctrl->pciFun;
        context.vendorId = ctrl->vendorId;
        context.deviceId = ctrl->deviceId;
        context.hasPciAddress = true;
        context.hasVendorDevice = true;
        context.hasClass = true;
        context.baseClass = PCI_CLASS_MULTIMEDIA;
        context.subClass = ctrl->type == AUDIO_HDA ? PCI_SUBCLASS_HDA : PCI_SUBCLASS_AUDIO;
    }
    return context;
}

static uint64_t begin_audio_diagnostic_operation(
    modern_input_diagnostics::StartupOperation operation,
    const modern_input_diagnostics::StartupOperationContext* context = nullptr)
{
    if (!s_startupAudioDiagnosticsActive) return 0u;
    return modern_input_diagnostics::begin_startup_operation(operation, context);
}

static void complete_audio_diagnostic_operation(
    uint64_t sequence, modern_input_diagnostics::StartupOperationStatus status,
    uint32_t result = 0u, bool hasResult = false,
    uint32_t errorCode = 0u, bool hasErrorCode = false)
{
    if (sequence == 0u) return;
    modern_input_diagnostics::complete_startup_operation(
        sequence, status, result, hasResult, errorCode, hasErrorCode);
}

static bool is_audio_diagnostic_context(
    const modern_input_diagnostics::StartupOperationContext& context)
{
    return context.hasClass && context.baseClass == PCI_CLASS_MULTIMEDIA &&
        (context.subClass == PCI_SUBCLASS_HDA ||
         context.subClass == PCI_SUBCLASS_AUDIO);
}

static bool is_pci_class_filter_context(
    const modern_input_diagnostics::StartupOperationContext& context,
    uint8_t configOffset)
{
    // The audio scan probes every BDF for a vendor ID. Those 65,536 ordinary
    // reads are intentionally not added to the trace. Once a real PCI ID has
    // been returned, the class-register read is a sparse, meaningful boundary
    // and carries a safe BDF/vendor/device context before the access begins.
    return configOffset == 0x08u && context.hasVendorDevice && !context.hasClass;
}

static void present_audio_diagnostic_frontier(
    const char* breadcrumb,
    const modern_input_diagnostics::StartupOperationContext* context = nullptr)
{
    if (!s_startupAudioDiagnosticsActive) return;
    // Startup is synchronous and outside driver/snapshot locks here. Use the
    // established desktop renderer so the pending operation reaches the
    // framebuffer before a potentially blocking audio access.
    serial::puts("INPUT3D: before presentation: ");
    serial::puts(breadcrumb);
    if (context != nullptr && context->hasPciAddress) {
        serial::puts(" BDF="); serial::put_hex8(context->bus);
        serial::putc(':'); serial::put_hex8(context->device);
        serial::putc('.'); serial::put_hex8(context->function);
        if (context->hasVendorDevice) {
            serial::puts(" ID="); serial::put_hex16(context->vendorId);
            serial::putc(':'); serial::put_hex16(context->deviceId);
        }
        if (context->hasClass) {
            serial::puts(" CLASS="); serial::put_hex8(context->baseClass);
            serial::putc('/'); serial::put_hex8(context->subClass);
            serial::putc('/'); serial::put_hex8(context->progIf);
        }
        if (context->hasConfigOffset) {
            serial::puts(" CFG="); serial::put_hex8(context->configOffset);
        }
    }
    serial::puts("\n");
    desktop::draw();
    serial::puts("INPUT3D: presented frontier: ");
    serial::puts(breadcrumb);
    if (context != nullptr && context->hasPciAddress) {
        serial::puts(" BDF="); serial::put_hex8(context->bus);
        serial::putc(':'); serial::put_hex8(context->device);
        serial::putc('.'); serial::put_hex8(context->function);
        if (context->hasConfigOffset) {
            serial::puts(" CFG="); serial::put_hex8(context->configOffset);
        }
    }
    serial::puts("\n");
}
#else
static void set_pci_diagnostic_context(
    uint8_t, uint8_t, uint8_t, uint8_t, uint16_t, uint16_t,
    uint8_t, uint8_t, uint8_t, bool, bool)
{
}
static modern_input_diagnostics::StartupOperationContext
audio_controller_diagnostic_context(const AudioController*)
{
    return {};
}
static bool is_pci_class_filter_context(
    const modern_input_diagnostics::StartupOperationContext&, uint8_t)
{
    return false;
}
static uint64_t begin_audio_diagnostic_operation(
    modern_input_diagnostics::StartupOperation,
    const modern_input_diagnostics::StartupOperationContext* = nullptr)
{
    return 0u;
}
static void complete_audio_diagnostic_operation(
    uint64_t, modern_input_diagnostics::StartupOperationStatus,
    uint32_t = 0u, bool = false, uint32_t = 0u, bool = false)
{
}
#endif

// Kernel physical base for DMA address translation. The kernel links at
// 0x100000 but the bootloader loads it at an arbitrary physical base
// (BootInfo.KernelPhysicalBase); device-visible addresses are
// base + (virt - 0x100000). Set via set_kernel_physical_base() at boot
// (default identity preserves historic behavior when unset).
static uint64_t s_kernelPhysicalBase = 0x100000ULL;

static uint64_t kernel_virt_to_phys(uintptr_t virt)
{
    const uint64_t v = static_cast<uint64_t>(virt);
    if (v < 0x100000ULL) return 0;
    return s_kernelPhysicalBase + (v - 0x100000ULL);
}

void set_kernel_physical_base(uint64_t physicalBase)
{
    if (physicalBase != 0) s_kernelPhysicalBase = physicalBase;
}

// ================================================================
// Helpers
// ================================================================

static void memzero(void* dst, uint32_t len)
{
    uint8_t* p = static_cast<uint8_t*>(dst);
    for (uint32_t i = 0; i < len; ++i) p[i] = 0;
}

// ================================================================
// MMIO register access (controller BAR0)
// ================================================================

static uint8_t hda_read8(const AudioController* ctrl, uint32_t offset)
{
    volatile uint8_t* reg = reinterpret_cast<volatile uint8_t*>(
        static_cast<uintptr_t>(ctrl->mmioBase + offset));
    return *reg;
}

static uint16_t hda_read16(const AudioController* ctrl, uint32_t offset)
{
    volatile uint16_t* reg = reinterpret_cast<volatile uint16_t*>(
        static_cast<uintptr_t>(ctrl->mmioBase + offset));
    return *reg;
}

static uint32_t hda_read32(const AudioController* ctrl, uint32_t offset)
{
    volatile uint32_t* reg = reinterpret_cast<volatile uint32_t*>(
        static_cast<uintptr_t>(ctrl->mmioBase + offset));
    return *reg;
}

static void hda_write8(const AudioController* ctrl, uint32_t offset, uint8_t val)
{
    volatile uint8_t* reg = reinterpret_cast<volatile uint8_t*>(
        static_cast<uintptr_t>(ctrl->mmioBase + offset));
    *reg = val;
}

static void hda_write16(const AudioController* ctrl, uint32_t offset, uint16_t val)
{
    volatile uint16_t* reg = reinterpret_cast<volatile uint16_t*>(
        static_cast<uintptr_t>(ctrl->mmioBase + offset));
    *reg = val;
}

static void hda_write32(const AudioController* ctrl, uint32_t offset, uint32_t val)
{
    volatile uint32_t* reg = reinterpret_cast<volatile uint32_t*>(
        static_cast<uintptr_t>(ctrl->mmioBase + offset));
    *reg = val;
}

// ================================================================
// Simple delay (spin loop)
// ================================================================

static void spin_delay(uint32_t iterations)
{
    for (volatile uint32_t i = 0; i < iterations; ++i) {}
}

// ================================================================
// PCI config space access (arch-specific, forward declared)
//
// These are thin wrappers that call arch::outl/inl (x86/amd64)
// or arch::mmio_read32 (IA-64/SPARC64).
// ================================================================

#if ARCH_HAS_PORT_IO

static uint32_t pci_cfg_read32(uint8_t bus, uint8_t dev,
                                uint8_t func, uint8_t offset)
{
    uint32_t addr = 0x80000000u |
                    (static_cast<uint32_t>(bus)  << 16) |
                    (static_cast<uint32_t>(dev)  << 11) |
                    (static_cast<uint32_t>(func) << 8)  |
                    (offset & 0xFC);
#if defined(GXOS_INPUT3D_DIAGNOSTICS)
    modern_input_diagnostics::StartupOperationContext context = s_pciDiagnosticContext;
    context.bus = bus;
    context.device = dev;
    context.function = func;
    context.configOffset = offset & 0xFC;
    context.hasPciAddress = true;
    context.hasConfigOffset = true;
    const bool traceTransaction = is_audio_diagnostic_context(context) ||
        is_pci_class_filter_context(context, offset & 0xFC);
    const uint64_t selectSequence = traceTransaction
        ? begin_audio_diagnostic_operation(
              modern_input_diagnostics::StartupOperation::PciConfigAddressSelect, &context)
        : 0u;
    if (traceTransaction) {
        present_audio_diagnostic_frontier("PCI config address select entered", &context);
    }
#endif
    arch::outl(0x0CF8, addr);
#if defined(GXOS_INPUT3D_DIAGNOSTICS)
    complete_audio_diagnostic_operation(selectSequence,
        modern_input_diagnostics::StartupOperationStatus::Completed, addr, true);
    const uint64_t readSequence = traceTransaction
        ? begin_audio_diagnostic_operation(
              modern_input_diagnostics::StartupOperation::PciConfigDataRead, &context)
        : 0u;
    if (traceTransaction) {
        present_audio_diagnostic_frontier("PCI config data read entered", &context);
    }
#endif
    const uint32_t result = arch::inl(0x0CFC);
#if defined(GXOS_INPUT3D_DIAGNOSTICS)
    complete_audio_diagnostic_operation(readSequence,
        modern_input_diagnostics::StartupOperationStatus::Completed, result, true);
#endif
    return result;
}

static void pci_cfg_write32(uint8_t bus, uint8_t dev,
                             uint8_t func, uint8_t offset, uint32_t val)
{
    uint32_t addr = 0x80000000u |
                    (static_cast<uint32_t>(bus)  << 16) |
                    (static_cast<uint32_t>(dev)  << 11) |
                    (static_cast<uint32_t>(func) << 8)  |
                    (offset & 0xFC);
#if defined(GXOS_INPUT3D_DIAGNOSTICS)
    modern_input_diagnostics::StartupOperationContext context = s_pciDiagnosticContext;
    context.bus = bus;
    context.device = dev;
    context.function = func;
    context.configOffset = offset & 0xFC;
    context.hasPciAddress = true;
    context.hasConfigOffset = true;
    const bool traceTransaction = is_audio_diagnostic_context(context);
    const uint64_t selectSequence = traceTransaction
        ? begin_audio_diagnostic_operation(
              modern_input_diagnostics::StartupOperation::PciConfigAddressSelect, &context)
        : 0u;
    if (traceTransaction) {
        present_audio_diagnostic_frontier("PCI config address select entered", &context);
    }
#endif
    arch::outl(0x0CF8, addr);
#if defined(GXOS_INPUT3D_DIAGNOSTICS)
    complete_audio_diagnostic_operation(selectSequence,
        modern_input_diagnostics::StartupOperationStatus::Completed, addr, true);
    const uint64_t writeSequence = traceTransaction
        ? begin_audio_diagnostic_operation(
              modern_input_diagnostics::StartupOperation::PciConfigDataWrite, &context)
        : 0u;
    if (traceTransaction) {
        present_audio_diagnostic_frontier("PCI config data write entered", &context);
    }
#endif
    arch::outl(0x0CFC, val);
#if defined(GXOS_INPUT3D_DIAGNOSTICS)
    complete_audio_diagnostic_operation(writeSequence,
        modern_input_diagnostics::StartupOperationStatus::Completed, val, true);
#endif
}

static uint16_t pci_cfg_read16(uint8_t bus, uint8_t dev,
                                uint8_t func, uint8_t offset)
{
    uint32_t val = pci_cfg_read32(bus, dev, func, offset & 0xFC);
    return static_cast<uint16_t>(val >> ((offset & 2) * 8));
}

#else // MMIO-based PCI config

// IA-64 and SPARC64 use memory-mapped config space.
// Use a well-known MMCFG base; real systems get this from ACPI MCFG.
#if defined(ARCH_IA64)
static const uint64_t PCI_MMCFG_BASE = 0xF0000000ULL;
#elif defined(ARCH_SPARC64)
static const uint64_t PCI_MMCFG_BASE = 0x1FE01000000ULL;
#else
static const uint64_t PCI_MMCFG_BASE = 0;
#endif

static uint32_t pci_cfg_read32(uint8_t bus, uint8_t dev,
                                uint8_t func, uint8_t offset)
{
    if (PCI_MMCFG_BASE == 0) return 0xFFFFFFFF;
    uint64_t addr = PCI_MMCFG_BASE |
                    (static_cast<uint64_t>(bus)  << 20) |
                    (static_cast<uint64_t>(dev)  << 15) |
                    (static_cast<uint64_t>(func) << 12) |
                    (offset & 0xFFC);
    volatile uint32_t* reg = reinterpret_cast<volatile uint32_t*>(
        static_cast<uintptr_t>(addr));
    return *reg;
}

static void pci_cfg_write32(uint8_t bus, uint8_t dev,
                             uint8_t func, uint8_t offset, uint32_t val)
{
    if (PCI_MMCFG_BASE == 0) return;
    uint64_t addr = PCI_MMCFG_BASE |
                    (static_cast<uint64_t>(bus)  << 20) |
                    (static_cast<uint64_t>(dev)  << 15) |
                    (static_cast<uint64_t>(func) << 12) |
                    (offset & 0xFFC);
    volatile uint32_t* reg = reinterpret_cast<volatile uint32_t*>(
        static_cast<uintptr_t>(addr));
    *reg = val;
}

static uint16_t pci_cfg_read16(uint8_t bus, uint8_t dev,
                                uint8_t func, uint8_t offset)
{
    uint32_t val = pci_cfg_read32(bus, dev, func, offset & 0xFC);
    return static_cast<uint16_t>(val >> ((offset & 2) * 8));
}

#endif // ARCH_HAS_PORT_IO

// ================================================================
// HDA controller reset
// ================================================================

static bool hda_reset(AudioController* ctrl)
{
    const modern_input_diagnostics::StartupOperationContext context =
        audio_controller_diagnostic_context(ctrl);
    const uint64_t resetSequence = begin_audio_diagnostic_operation(
        modern_input_diagnostics::StartupOperation::HdaResetEnter, &context);
#if defined(GXOS_INPUT3D_DIAGNOSTICS)
    present_audio_diagnostic_frontier("HDA reset MMIO entry", &context);
#endif

    // Clear CRST to enter reset
    hda_write32(ctrl, HDA_GCTL, 0);
    spin_delay(100000);

    // Set CRST to come out of reset
    hda_write32(ctrl, HDA_GCTL, HDA_GCTL_CRST);
    spin_delay(100000);

    // Wait for CRST to read back 1
    const uint64_t waitSequence = begin_audio_diagnostic_operation(
        modern_input_diagnostics::StartupOperation::HdaResetWaitCrst, &context);
#if defined(GXOS_INPUT3D_DIAGNOSTICS)
    present_audio_diagnostic_frontier("HDA reset-ready wait entered", &context);
#endif
    for (int i = 0; i < 1000; ++i) {
        if (hda_read32(ctrl, HDA_GCTL) & HDA_GCTL_CRST) {
            complete_audio_diagnostic_operation(waitSequence,
                modern_input_diagnostics::StartupOperationStatus::Completed);
#if defined(GXOS_INPUT3D_DIAGNOSTICS)
            present_audio_diagnostic_frontier("HDA reset-ready wait returned", &context);
#endif
            complete_audio_diagnostic_operation(resetSequence,
                modern_input_diagnostics::StartupOperationStatus::Completed);
            return true;
        }
        spin_delay(1000);
    }
    complete_audio_diagnostic_operation(waitSequence,
        modern_input_diagnostics::StartupOperationStatus::TimedOut,
        0u, false, 1u, true);
    complete_audio_diagnostic_operation(resetSequence,
        modern_input_diagnostics::StartupOperationStatus::ReturnedError,
        0u, false, 1u, true);
#if defined(GXOS_INPUT3D_DIAGNOSTICS)
    present_audio_diagnostic_frontier("HDA reset-ready wait timed out", &context);
#endif
    return false;
}

// ================================================================
// CORB/RIRB initialisation (simplified � uses 256-entry rings)
// ================================================================

// NOTE: HDA requires CORB/RIRB bases to be 128-byte aligned. These were
// historically only word-aligned (MC6 audit finding); misalignment is
// undefined behavior on real hardware and breaks DMA on strict engines,
// so they are now explicitly 128-byte aligned like the streaming BDL.

// MC6: 128-byte aligned per the HDA specification (was word-aligned).
alignas(128) static uint32_t s_corbBuf[256 * MAX_AUDIO_CONTROLLERS];
alignas(128) static uint64_t s_rirbBuf[256 * MAX_AUDIO_CONTROLLERS]; // response + solicited flag

static bool hda_init_corb_rirb(AudioController* ctrl, uint8_t ctrlIdx)
{
    // Use static buffers (offset by controller index)
    uint32_t* corb = &s_corbBuf[ctrlIdx * 256];
    uint64_t* rirb = &s_rirbBuf[ctrlIdx * 256];

    // Zero buffers
    for (int i = 0; i < 256; ++i) { corb[i] = 0; rirb[i] = 0; }

    // Device-visible addresses use the kernel slide translation, not the
    // raw virtual address (MC6: raw pointers fault/misdirect DMA whenever
    // the load base differs from the link base).
    ctrl->corbPhysAddr = kernel_virt_to_phys(reinterpret_cast<uintptr_t>(corb));
    ctrl->rirbPhysAddr = kernel_virt_to_phys(reinterpret_cast<uintptr_t>(rirb));
    ctrl->corbSize = 256;
    ctrl->rirbSize = 256;

    const modern_input_diagnostics::StartupOperationContext context =
        audio_controller_diagnostic_context(ctrl);
    const uint64_t initSequence = begin_audio_diagnostic_operation(
        modern_input_diagnostics::StartupOperation::HdaCorbRirbInit, &context);
#if defined(GXOS_INPUT3D_DIAGNOSTICS)
    present_audio_diagnostic_frontier("HDA CORB/RIRB MMIO entry", &context);
#endif

    // Stop CORB/RIRB
    hda_write8(ctrl, HDA_CORBCTL, 0);
    hda_write8(ctrl, HDA_RIRBCTL, 0);
    spin_delay(10000);

    // Set CORB base address
    hda_write32(ctrl, HDA_CORBLBASE, static_cast<uint32_t>(ctrl->corbPhysAddr));
    hda_write32(ctrl, HDA_CORBUBASE, static_cast<uint32_t>(ctrl->corbPhysAddr >> 32));

    // Set CORB size (select 256 entries if supported)
    uint8_t corbSizeCap = hda_read8(ctrl, HDA_CORBSIZE);
    if (corbSizeCap & 0x40) {
        hda_write8(ctrl, HDA_CORBSIZE, 0x02); // 256 entries
    } else if (corbSizeCap & 0x20) {
        hda_write8(ctrl, HDA_CORBSIZE, 0x01); // 16 entries
        ctrl->corbSize = 16;
    } else {
        hda_write8(ctrl, HDA_CORBSIZE, 0x00); // 2 entries
        ctrl->corbSize = 2;
    }

    // Reset CORB read pointer
    hda_write16(ctrl, HDA_CORBRP, 0x8000);
    spin_delay(10000);
    hda_write16(ctrl, HDA_CORBRP, 0x0000);

    // Set CORB write pointer to 0
    ctrl->corbWritePtr = 0;
    hda_write16(ctrl, HDA_CORBWP, 0);

    // Set RIRB base address
    hda_write32(ctrl, HDA_RIRBLBASE, static_cast<uint32_t>(ctrl->rirbPhysAddr));
    hda_write32(ctrl, HDA_RIRBUBASE, static_cast<uint32_t>(ctrl->rirbPhysAddr >> 32));

    // Set RIRB size
    uint8_t rirbSizeCap = hda_read8(ctrl, HDA_RIRBSIZE);
    if (rirbSizeCap & 0x40) {
        hda_write8(ctrl, HDA_RIRBSIZE, 0x02); // 256
    } else if (rirbSizeCap & 0x20) {
        hda_write8(ctrl, HDA_RIRBSIZE, 0x01); // 16
        ctrl->rirbSize = 16;
    } else {
        hda_write8(ctrl, HDA_RIRBSIZE, 0x00); // 2
        ctrl->rirbSize = 2;
    }

    // Reset RIRB write pointer
    hda_write16(ctrl, HDA_RIRBWP, 0x8000);
    ctrl->rirbReadPtr = 0;

    // Set response interrupt count
    hda_write16(ctrl, HDA_RINTCNT, 1);

    // Start CORB and RIRB
    hda_write8(ctrl, HDA_CORBCTL, HDA_CORBCTL_RUN);
    hda_write8(ctrl, HDA_RIRBCTL, HDA_RIRBCTL_RUN | HDA_RIRBCTL_OIC);

    complete_audio_diagnostic_operation(initSequence,
        modern_input_diagnostics::StartupOperationStatus::Completed);
#if defined(GXOS_INPUT3D_DIAGNOSTICS)
    present_audio_diagnostic_frontier("HDA CORB/RIRB init returned", &context);
#endif

    return true;
}

// ================================================================
// Send a verb via CORB and read response from RIRB
// ================================================================

bool hda_send_verb(uint8_t ctrlIndex, uint8_t codecAddr,
                   uint8_t nodeId, uint32_t verb,
                   uint32_t* response)
{
    if (ctrlIndex >= s_controllerCount) return false;
    AudioController* ctrl = &s_controllers[ctrlIndex];
    if (!ctrl->active || ctrl->type != AUDIO_HDA) return false;

    // Build the verb: [31:28]=codec, [27:20]=nid, [19:0]=verb
    uint32_t fullVerb = (static_cast<uint32_t>(codecAddr) << 28) |
                        (static_cast<uint32_t>(nodeId) << 20) |
                        (verb & 0xFFFFF);

    // Write to CORB
    uint16_t wp = (ctrl->corbWritePtr + 1) % ctrl->corbSize;
    uint32_t* corb = &s_corbBuf[ctrlIndex * 256];
    corb[wp] = fullVerb;
    ctrl->corbWritePtr = wp;
#if defined(GXOS_INPUT3D_DIAGNOSTICS)
    uint64_t waitSequence = 0u;
    const modern_input_diagnostics::StartupOperationContext diagnosticContext =
        audio_controller_diagnostic_context(ctrl);
    if (s_startupAudioDiagnosticsActive) {
        waitSequence = begin_audio_diagnostic_operation(
            modern_input_diagnostics::StartupOperation::HdaVerbResponseWait,
            &diagnosticContext);
        ++s_startupVerbCount;
        // The first verb boundary and sparse progress checkpoints are
        // presented. Rendering every codec verb would make enumeration
        // itself timing-sensitive on machines with large codec topologies.
        if (s_startupVerbCount == 1u || (s_startupVerbCount & 0x3FFFu) == 0u) {
            present_audio_diagnostic_frontier("HDA verb response wait entered",
                                              &diagnosticContext);
        }
    }
#endif
    hda_write16(ctrl, HDA_CORBWP, wp);

    // Poll RIRB for response
    uint64_t* rirb = &s_rirbBuf[ctrlIndex * 256];
    for (int timeout = 0; timeout < 100000; ++timeout) {
        uint16_t rirbWp = hda_read16(ctrl, HDA_RIRBWP);
        if (rirbWp != ctrl->rirbReadPtr) {
            ctrl->rirbReadPtr = (ctrl->rirbReadPtr + 1) % ctrl->rirbSize;
            uint64_t resp = rirb[ctrl->rirbReadPtr];
            if (response) {
                *response = static_cast<uint32_t>(resp);
            }
            // Spec-mandated: clear the Response Interrupt flag so the
            // controller keeps producing responses (QEMU's engine stalls
            // with RINTFL pending; real hardware treats it as status).
            hda_write8(ctrl, HDA_RIRBSTS, 0x05);
#if defined(GXOS_INPUT3D_DIAGNOSTICS)
            complete_audio_diagnostic_operation(waitSequence,
                modern_input_diagnostics::StartupOperationStatus::Completed);
#endif
            return true;
        }
        spin_delay(100);
    }

#if defined(GXOS_INPUT3D_DIAGNOSTICS)
    complete_audio_diagnostic_operation(waitSequence,
        modern_input_diagnostics::StartupOperationStatus::TimedOut,
        0u, false, 2u, true);
    if (s_startupAudioDiagnosticsActive) {
        serial::puts("INPUT3D: HDA verb response wait timed out\n");
        present_audio_diagnostic_frontier("HDA verb response wait timed out",
                                          &diagnosticContext);
    }
#endif
    return false; // timeout
}

// ================================================================
// Enumerate codecs on an HDA controller
// ================================================================

static bool hda_enumerate_codecs(AudioController* ctrl, uint8_t ctrlIdx)
{
#if defined(GXOS_INPUT3D_DIAGNOSTICS)
    const modern_input_diagnostics::StartupOperationContext context =
        audio_controller_diagnostic_context(ctrl);
    const uint64_t enumerationSequence = begin_audio_diagnostic_operation(
        modern_input_diagnostics::StartupOperation::HdaCodecEnumeration, &context);
    present_audio_diagnostic_frontier("HDA codec enumeration entered", &context);
#endif

    // STATESTS bits indicate which codec addresses responded after reset
    uint16_t statests = hda_read16(ctrl, HDA_STATESTS);
    bool allResponsesCompleted = true;

    for (uint8_t addr = 0; addr < 15 && ctrl->codecCount < MAX_CODECS; ++addr) {
        if (!(statests & (1 << addr))) continue;

        HDACodec* codec = &ctrl->codecs[ctrl->codecCount];
        memzero(codec, sizeof(HDACodec));
        codec->address = addr;

        // Get vendor/device ID (node 0, param 0x00)
        uint32_t vendorId = 0;
        if (!hda_send_verb(ctrlIdx, addr, 0,
                HDA_VERB_GET_PARAM | HDA_PARAM_VENDOR_ID, &vendorId)) {
            allResponsesCompleted = false;
            continue;
        }
        codec->vendorId = vendorId;

        // Get revision ID
        if (!hda_send_verb(ctrlIdx, addr, 0,
                HDA_VERB_GET_PARAM | HDA_PARAM_REVISION_ID, &codec->revisionId)) {
            allResponsesCompleted = false;
        }

        // Get sub-node count from the root node (node 0)
        uint32_t subNodes = 0;
        if (!hda_send_verb(ctrlIdx, addr, 0,
                HDA_VERB_GET_PARAM | HDA_PARAM_SUB_NODE_COUNT, &subNodes)) {
            allResponsesCompleted = false;
        }
        codec->startNode = static_cast<uint8_t>((subNodes >> 16) & 0xFF);
        codec->nodeCount  = static_cast<uint8_t>(subNodes & 0xFF);

        uint16_t nodeEnd = 0u;
        if (!validate_hda_child_range(codec->startNode, codec->nodeCount,
                                      &nodeEnd)) {
#if defined(GXOS_INPUT3D_DIAGNOSTICS)
            complete_audio_diagnostic_operation(enumerationSequence,
                modern_input_diagnostics::StartupOperationStatus::ReturnedError,
                static_cast<uint32_t>(codec->startNode) + codec->nodeCount,
                true, 3u, true);
            serial::puts("INPUT3D: invalid HDA node range\n");
            present_audio_diagnostic_frontier("invalid HDA node range returned", &context);
#endif
            return false;
        }

        // Walk function group nodes to find audio widgets
        for (uint16_t node = codec->startNode; node < nodeEnd; ++node) {
            const uint8_t n = static_cast<uint8_t>(node);
            uint32_t fgType = 0;
            if (!hda_send_verb(ctrlIdx, addr, n,
                    HDA_VERB_GET_PARAM | HDA_PARAM_FUNC_GROUP_TYPE, &fgType)) {
                allResponsesCompleted = false;
            }

            if ((fgType & 0xFF) == 0x01) {
                // Audio function group � get its sub-nodes
                if (codec->afgNode == 0) codec->afgNode = n;
                uint32_t afg_sub = 0;
                if (!hda_send_verb(ctrlIdx, addr, n,
                        HDA_VERB_GET_PARAM | HDA_PARAM_SUB_NODE_COUNT, &afg_sub)) {
                    allResponsesCompleted = false;
                }
                uint8_t startWidget = static_cast<uint8_t>((afg_sub >> 16) & 0xFF);
                uint8_t numWidgets  = static_cast<uint8_t>(afg_sub & 0xFF);
                uint16_t widgetEnd = 0u;
                if (!validate_hda_child_range(startWidget, numWidgets,
                                              &widgetEnd)) {
#if defined(GXOS_INPUT3D_DIAGNOSTICS)
                    complete_audio_diagnostic_operation(enumerationSequence,
                        modern_input_diagnostics::StartupOperationStatus::ReturnedError,
                        static_cast<uint32_t>(startWidget) + numWidgets,
                        true, 4u, true);
                    serial::puts("INPUT3D: invalid HDA widget range\n");
                    present_audio_diagnostic_frontier("invalid HDA widget range returned", &context);
#endif
                    return false;
                }

                for (uint16_t widget = startWidget; widget < widgetEnd; ++widget) {
                    const uint8_t w = static_cast<uint8_t>(widget);
                    uint32_t audioCaps = 0;
                    if (!hda_send_verb(ctrlIdx, addr, w,
                            HDA_VERB_GET_PARAM | HDA_PARAM_AUDIO_CAPS, &audioCaps)) {
                        allResponsesCompleted = false;
                    }
                    uint8_t widgetType = static_cast<uint8_t>((audioCaps >> 20) & 0xF);

                    if (widgetType == 0x0 && codec->dacNode == 0) {
                        codec->dacNode = w; // Audio Output (DAC)
                    }
                    else if (widgetType == 0x1 && codec->adcNode == 0) {
                        codec->adcNode = w; // Audio Input (ADC)
                    }
                    else if (widgetType == 0x4) {
                        // Pin widget � check config default
                        uint32_t pinCaps = 0;
                        if (!hda_send_verb(ctrlIdx, addr, w,
                                HDA_VERB_GET_PARAM | HDA_PARAM_PIN_CAPS, &pinCaps)) {
                            allResponsesCompleted = false;
                        }
                        if ((pinCaps & 0x10) && codec->pinOutNode == 0) {
                            codec->pinOutNode = w; // Output capable
                        }
                        if ((pinCaps & 0x20) && codec->pinInNode == 0) {
                            codec->pinInNode = w;  // Input capable
                        }
                    }
                }
            }
        }

        codec->present = true;
        ctrl->codecCount++;
    }
#if defined(GXOS_INPUT3D_DIAGNOSTICS)
    complete_audio_diagnostic_operation(enumerationSequence,
        allResponsesCompleted
            ? modern_input_diagnostics::StartupOperationStatus::Completed
            : modern_input_diagnostics::StartupOperationStatus::ReturnedError,
        ctrl->codecCount, true, allResponsesCompleted ? 0u : 6u,
        !allResponsesCompleted);
    present_audio_diagnostic_frontier("HDA codec enumeration returned", &context);
#endif
    return allResponsesCompleted;
}

// ================================================================
// HDA stream format register encoding
// ================================================================

static uint16_t encode_hda_format(uint32_t sampleRate, uint8_t bits, uint8_t channels)
{
    uint16_t fmt = 0;

    // Base rate and multiplier/divisor
    switch (sampleRate) {
        case 8000:  fmt = 0x0500; break; // 48000 / 6
        case 11025: fmt = 0x4300; break; // 44100 / 4
        case 16000: fmt = 0x0200; break; // 48000 / 3
        case 22050: fmt = 0x4100; break; // 44100 / 2
        case 32000: fmt = 0x0A00; break; // 48000 * 2 / 3
        case 44100: fmt = 0x4000; break; // 44100 base
        case 48000: fmt = 0x0000; break; // 48000 base
        case 88200: fmt = 0x4800; break; // 44100 * 2
        case 96000: fmt = 0x0800; break; // 48000 * 2
        case 176400: fmt = 0x5800; break; // 44100 * 4
        case 192000: fmt = 0x1800; break; // 48000 * 4
        default:    fmt = 0x0000; break; // 48 kHz default
    }

    // Bits per sample
    switch (bits) {
        case 8:  fmt |= 0x0000; break;
        case 16: fmt |= 0x0010; break;
        case 20: fmt |= 0x0020; break;
        case 24: fmt |= 0x0030; break;
        case 32: fmt |= 0x0040; break;
        default: fmt |= 0x0010; break;
    }

    // Channels (0-based)
    fmt |= static_cast<uint16_t>((channels - 1) & 0x0F);

    return fmt;
}

// ================================================================
// Configure a stream descriptor
// ================================================================

static bool configure_stream(AudioController* ctrl, PCMStream* stream,
                              bool isOutput,
                              uint16_t sampleRate, uint8_t bits, uint8_t channels)
{
    stream->isOutput      = isOutput;
    stream->sampleRate    = sampleRate;
    stream->bitsPerSample = bits;
    stream->channels      = channels;

    // Determine stream descriptor index. Per the HDA specification the
    // descriptors are ordered input-first: SD0..ISS-1 are capture, the
    // next OSS are playback (output n lives at index ISS+n), then
    // bidirectional. MC6 audit finding: this driver historically used
    // index 0 for playback, which programs a *capture* descriptor on any
    // controller with input streams (e.g. ICH6: 4 in + 4 out) — the DMA
    // engine for the intended direction never runs.
    // GCAP tells us the number of input/output streams
    uint16_t gcap = hda_read16(ctrl, HDA_GCAP);
    uint8_t numOutputSD = static_cast<uint8_t>((gcap >> 12) & 0x0F);
    uint8_t numInputSD  = static_cast<uint8_t>((gcap >> 8) & 0x0F);
    (void)numOutputSD;

    if (isOutput) {
        stream->streamIndex = numInputSD; // first output SD
        stream->streamTag   = 1;
    } else {
        stream->streamIndex = 0; // first input SD
        stream->streamTag   = 2;
    }

    uint32_t sdBase = HDA_SD_BASE + stream->streamIndex * HDA_SD_INTERVAL;

    // Stop the stream
    uint8_t sdCtl = hda_read8(ctrl, sdBase + HDA_SD_CTL);
    hda_write8(ctrl, sdBase + HDA_SD_CTL, sdCtl & ~0x02);
    spin_delay(10000);

    // Reset the stream
    hda_write8(ctrl, sdBase + HDA_SD_CTL, 0x01);
    spin_delay(10000);
    hda_write8(ctrl, sdBase + HDA_SD_CTL, 0x00);
    spin_delay(10000);

    // Set format
    uint16_t fmt = encode_hda_format(sampleRate, bits, channels);
    hda_write16(ctrl, sdBase + HDA_SD_FMT, fmt);

    // Set stream tag in CTL bits [23:20]
    uint32_t sdCtl32 = hda_read32(ctrl, sdBase + HDA_SD_CTL);
    sdCtl32 = (sdCtl32 & ~0x00F00000u) |
              (static_cast<uint32_t>(stream->streamTag) << 20);
    hda_write32(ctrl, sdBase + HDA_SD_CTL, sdCtl32);

    stream->active = true;
    return true;
}

// ================================================================
// PCI scan for audio controllers
// ================================================================

static void scan_pci_audio()
{
    for (uint16_t bus = 0; bus < 256; ++bus) {
        for (uint8_t dev = 0; dev < 32; ++dev) {
            for (uint8_t func = 0; func < 8; ++func) {
                set_pci_diagnostic_context(static_cast<uint8_t>(bus), dev, func,
                    0x00u, 0u, 0u, 0u, 0u, 0u, false, false);
                uint32_t id = pci_cfg_read32(static_cast<uint8_t>(bus), dev, func, 0);
                if (id == 0xFFFFFFFF || id == 0x00000000) continue;

                const uint16_t vendorId = static_cast<uint16_t>(id);
                const uint16_t deviceId = static_cast<uint16_t>(id >> 16);
                set_pci_diagnostic_context(static_cast<uint8_t>(bus), dev, func,
                    0x08u, vendorId, deviceId, 0u, 0u, 0u, true, false);

                uint32_t classReg = pci_cfg_read32(static_cast<uint8_t>(bus), dev, func, 0x08);
                uint8_t baseClass = static_cast<uint8_t>(classReg >> 24);
                uint8_t subClass  = static_cast<uint8_t>(classReg >> 16);
                uint8_t progIf = static_cast<uint8_t>(classReg >> 8);
                set_pci_diagnostic_context(static_cast<uint8_t>(bus), dev, func,
                    0x08u, vendorId, deviceId, baseClass, subClass, progIf,
                    true, true);

                bool isHda  = (baseClass == PCI_CLASS_MULTIMEDIA && subClass == PCI_SUBCLASS_HDA);
                bool isAc97 = (baseClass == PCI_CLASS_MULTIMEDIA && subClass == PCI_SUBCLASS_AUDIO);

                if (!isHda && !isAc97) {
#if defined(GXOS_INPUT3D_DIAGNOSTICS)
                    if (s_startupAudioDiagnosticsActive) {
                        const uint64_t unsupportedSequence = begin_audio_diagnostic_operation(
                            modern_input_diagnostics::StartupOperation::PciDeviceUnsupported,
                            &s_pciDiagnosticContext);
                        complete_audio_diagnostic_operation(unsupportedSequence,
                            modern_input_diagnostics::StartupOperationStatus::Unsupported,
                            classReg, true);
                    }
#endif
                    continue;
                }
                if (s_controllerCount >= MAX_AUDIO_CONTROLLERS) return;

#if defined(GXOS_INPUT3D_DIAGNOSTICS)
                if (s_startupAudioDiagnosticsActive) {
                    const uint64_t foundSequence = begin_audio_diagnostic_operation(
                        modern_input_diagnostics::StartupOperation::PciAudioDeviceFound,
                        &s_pciDiagnosticContext);
                    complete_audio_diagnostic_operation(foundSequence,
                        modern_input_diagnostics::StartupOperationStatus::Completed,
                        classReg, true);
                    present_audio_diagnostic_frontier("PCI audio-class device found",
                                                      &s_pciDiagnosticContext);
                }
#endif

                AudioController* ctrl = &s_controllers[s_controllerCount];
                memzero(ctrl, sizeof(AudioController));
                ctrl->pciBus   = static_cast<uint8_t>(bus);
                ctrl->pciDev   = dev;
                ctrl->pciFun   = func;
                ctrl->vendorId = vendorId;
                ctrl->deviceId = deviceId;

                // Read BAR0
                set_pci_diagnostic_context(static_cast<uint8_t>(bus), dev, func,
                    0x10u, vendorId, deviceId, baseClass, subClass, progIf,
                    true, true);
                uint32_t bar0 = pci_cfg_read32(static_cast<uint8_t>(bus), dev, func, 0x10);
                if (bar0 & 0x01) {
                    // I/O BAR (AC'97 NABM)
                    ctrl->ioBase = static_cast<uint16_t>(bar0 & 0xFFFE);
                    ctrl->type = AUDIO_AC97;
                } else {
                    // Memory BAR (HDA MMIO)
                    bool is64 = ((bar0 >> 1) & 0x03) == 0x02;
                    uint32_t bar0Hi = 0;
                    if (is64) {
                        set_pci_diagnostic_context(static_cast<uint8_t>(bus), dev, func,
                            0x14u, vendorId, deviceId, baseClass, subClass, progIf,
                            true, true);
                        bar0Hi = pci_cfg_read32(static_cast<uint8_t>(bus), dev, func, 0x14);
                    }
                    ctrl->mmioBase = (static_cast<uint64_t>(bar0Hi) << 32) |
                                     (bar0 & 0xFFFFFFF0u);
                    ctrl->type = isHda ? AUDIO_HDA : AUDIO_AC97;
                }

                // Enable bus master and memory/IO space in PCI command register
                set_pci_diagnostic_context(static_cast<uint8_t>(bus), dev, func,
                    0x04u, vendorId, deviceId, baseClass, subClass, progIf,
                    true, true);
                uint16_t cmd = pci_cfg_read16(static_cast<uint8_t>(bus), dev, func, 0x04);
                cmd |= 0x0007; // IO + Memory + Bus Master
                uint32_t cmd32 = pci_cfg_read32(static_cast<uint8_t>(bus), dev, func, 0x04);
                cmd32 = (cmd32 & 0xFFFF0000u) | cmd;
                pci_cfg_write32(static_cast<uint8_t>(bus), dev, func, 0x04, cmd32);

                ctrl->active = true;
                ++s_controllerCount;
            }
        }
    }
}

// ================================================================
// Public API
// ================================================================

void init()
{
#if defined(GXOS_INPUT3D_DIAGNOSTICS)
    modern_input_diagnostics::Snapshot bootSnapshot = {};
    modern_input_diagnostics::copy_snapshot(&bootSnapshot);
    s_startupAudioDiagnosticsActive = bootSnapshot.bootStage ==
        modern_input_diagnostics::BootStage::PciDiscoveryEntered;
    s_startupVerbCount = 0u;
    const uint64_t entrySequence = s_startupAudioDiagnosticsActive
        ? begin_audio_diagnostic_operation(
              modern_input_diagnostics::StartupOperation::PciAudioEntry)
        : 0u;
    if (s_startupAudioDiagnosticsActive) {
        present_audio_diagnostic_frontier("PCI audio initialization entered");
    }
#endif

    memzero(s_controllers, sizeof(s_controllers));
    s_controllerCount = 0;

    // Scan PCI (on architectures with PCI)
#if ARCH_HAS_PORT_IO || defined(ARCH_IA64) || defined(ARCH_SPARC64)
#if defined(GXOS_INPUT3D_DIAGNOSTICS)
    const uint64_t scanSequence = s_startupAudioDiagnosticsActive
        ? begin_audio_diagnostic_operation(
              modern_input_diagnostics::StartupOperation::PciAudioScanEnter)
        : 0u;
    if (s_startupAudioDiagnosticsActive) {
        present_audio_diagnostic_frontier("PCI audio PCI scan entered");
        serial::puts("INPUT3D: PCI audio scan entered; config transactions are in bounded trace only\n");
    }
#endif
    scan_pci_audio();
#if defined(GXOS_INPUT3D_DIAGNOSTICS)
    complete_audio_diagnostic_operation(scanSequence,
        modern_input_diagnostics::StartupOperationStatus::Completed,
        s_controllerCount, true);
    if (s_startupAudioDiagnosticsActive) {
        const uint64_t scanExitSequence = begin_audio_diagnostic_operation(
            modern_input_diagnostics::StartupOperation::PciAudioScanExit);
        complete_audio_diagnostic_operation(scanExitSequence,
            modern_input_diagnostics::StartupOperationStatus::Completed,
            s_controllerCount, true);
        present_audio_diagnostic_frontier("PCI audio PCI scan returned");
    }
#endif
#endif

    // Initialise each HDA controller
    for (uint8_t i = 0; i < s_controllerCount; ++i) {
        AudioController* ctrl = &s_controllers[i];
#if defined(GXOS_INPUT3D_DIAGNOSTICS)
        const modern_input_diagnostics::StartupOperationContext context =
            audio_controller_diagnostic_context(ctrl);
        const uint64_t probeSequence = s_startupAudioDiagnosticsActive
            ? begin_audio_diagnostic_operation(
                  modern_input_diagnostics::StartupOperation::PciAudioProbeEnter,
                  &context)
            : 0u;
        if (s_startupAudioDiagnosticsActive) {
            present_audio_diagnostic_frontier("PCI audio controller probe entered", &context);
        }
        bool probeCompleted = false;
        bool probeUnsupported = false;
#endif
        if (ctrl->type == AUDIO_HDA && ctrl->mmioBase != 0) {
            if (hda_reset(ctrl)) {
                hda_init_corb_rirb(ctrl, i);
                const bool codecEnumerationCompleted = hda_enumerate_codecs(ctrl, i);
#if defined(GXOS_INPUT3D_DIAGNOSTICS)
                probeCompleted = codecEnumerationCompleted;
#endif
            } else {
#if defined(GXOS_INPUT3D_DIAGNOSTICS)
                probeCompleted = false;
#endif
            }
        } else {
#if defined(GXOS_INPUT3D_DIAGNOSTICS)
            probeUnsupported = ctrl->type != AUDIO_HDA;
#endif
        }
#if defined(GXOS_INPUT3D_DIAGNOSTICS)
        if (s_startupAudioDiagnosticsActive) {
            const modern_input_diagnostics::StartupOperationStatus probeStatus =
                probeUnsupported
                    ? modern_input_diagnostics::StartupOperationStatus::Unsupported
                    : (probeCompleted
                        ? modern_input_diagnostics::StartupOperationStatus::Completed
                        : modern_input_diagnostics::StartupOperationStatus::ReturnedError);
            complete_audio_diagnostic_operation(probeSequence,
                probeStatus, i, true, probeCompleted || probeUnsupported ? 0u : 5u,
                !probeCompleted && !probeUnsupported);
            const uint64_t probeExitSequence = begin_audio_diagnostic_operation(
                modern_input_diagnostics::StartupOperation::PciAudioProbeExit, &context);
            complete_audio_diagnostic_operation(probeExitSequence,
                probeStatus, i, true, probeCompleted || probeUnsupported ? 0u : 5u,
                !probeCompleted && !probeUnsupported);
            present_audio_diagnostic_frontier("PCI audio controller probe returned", &context);
        }
#endif
    }

#if defined(GXOS_INPUT3D_DIAGNOSTICS)
    complete_audio_diagnostic_operation(entrySequence,
        modern_input_diagnostics::StartupOperationStatus::Completed,
        s_controllerCount, true);
    if (s_startupAudioDiagnosticsActive) {
        const uint64_t completeSequence = begin_audio_diagnostic_operation(
            modern_input_diagnostics::StartupOperation::PciAudioComplete);
        complete_audio_diagnostic_operation(completeSequence,
            modern_input_diagnostics::StartupOperationStatus::Completed,
            s_controllerCount, true);
        present_audio_diagnostic_frontier("PCI audio initialization returned");
    }
    s_startupAudioDiagnosticsActive = false;
#endif
}

uint8_t controller_count() { return s_controllerCount; }

const AudioController* get_controller(uint8_t index)
{
    if (index >= MAX_AUDIO_CONTROLLERS || !s_controllers[index].active)
        return nullptr;
    return &s_controllers[index];
}

// ----------------------------------------------------------------
// Volume control (HDA codec verb-based)
// ----------------------------------------------------------------

bool set_master_volume(uint8_t ctrlIndex, uint8_t percent)
{
    if (ctrlIndex >= s_controllerCount) return false;
    AudioController* ctrl = &s_controllers[ctrlIndex];
    if (!ctrl->active || ctrl->codecCount == 0) return false;

    HDACodec* codec = &ctrl->codecs[0];
    if (codec->dacNode == 0) return false;

    // HDA amp gain: 0-127 (7 bits), set output amp, left+right
    uint8_t gain = static_cast<uint8_t>((static_cast<uint32_t>(percent) * 127) / 100);
    uint32_t verb = HDA_VERB_SET_AMP_GAIN |
                    0xB000 | // set output, left+right
                    gain;

    uint32_t resp = 0;
    return hda_send_verb(ctrlIndex, codec->address, codec->dacNode, verb, &resp);
}

bool set_pcm_volume(uint8_t ctrlIndex, uint8_t percent)
{
    return set_master_volume(ctrlIndex, percent);
}

uint8_t get_master_volume(uint8_t ctrlIndex)
{
    if (ctrlIndex >= s_controllerCount) return 0;
    AudioController* ctrl = &s_controllers[ctrlIndex];
    if (!ctrl->active || ctrl->codecCount == 0) return 0;

    HDACodec* codec = &ctrl->codecs[0];
    if (codec->dacNode == 0) return 0;

    uint32_t verb = HDA_VERB_GET_AMP_GAIN | 0x8000; // output, left
    uint32_t resp = 0;
    if (!hda_send_verb(ctrlIndex, codec->address, codec->dacNode, verb, &resp))
        return 0;

    uint8_t gain = static_cast<uint8_t>(resp & 0x7F);
    return static_cast<uint8_t>((static_cast<uint32_t>(gain) * 100) / 127);
}

bool set_mute(uint8_t ctrlIndex, bool mute)
{
    if (ctrlIndex >= s_controllerCount) return false;
    AudioController* ctrl = &s_controllers[ctrlIndex];
    if (!ctrl->active || ctrl->codecCount == 0) return false;

    HDACodec* codec = &ctrl->codecs[0];
    if (codec->dacNode == 0) return false;

    uint32_t verb = HDA_VERB_SET_AMP_GAIN | 0xB000;
    if (mute) verb |= 0x80; // mute bit

    uint32_t resp = 0;
    return hda_send_verb(ctrlIndex, codec->address, codec->dacNode, verb, &resp);
}

bool get_mute(uint8_t ctrlIndex)
{
    if (ctrlIndex >= s_controllerCount) return false;
    AudioController* ctrl = &s_controllers[ctrlIndex];
    if (!ctrl->active || ctrl->codecCount == 0) return false;

    HDACodec* codec = &ctrl->codecs[0];
    if (codec->dacNode == 0) return false;

    uint32_t verb = HDA_VERB_GET_AMP_GAIN | 0x8000;
    uint32_t resp = 0;
    if (!hda_send_verb(ctrlIndex, codec->address, codec->dacNode, verb, &resp))
        return false;

    return (resp & 0x80) != 0;
}

// ----------------------------------------------------------------
// PCM streaming
// ----------------------------------------------------------------

bool configure_playback(uint8_t ctrlIndex, uint16_t sampleRate,
                        uint8_t bits, uint8_t channels)
{
    if (ctrlIndex >= s_controllerCount) return false;
    AudioController* ctrl = &s_controllers[ctrlIndex];
    if (!ctrl->active || ctrl->type != AUDIO_HDA) return false;

    return configure_stream(ctrl, &ctrl->playback, true,
                            sampleRate, bits, channels);
}

bool configure_capture(uint8_t ctrlIndex, uint16_t sampleRate,
                       uint8_t bits, uint8_t channels)
{
    if (ctrlIndex >= s_controllerCount) return false;
    AudioController* ctrl = &s_controllers[ctrlIndex];
    if (!ctrl->active || ctrl->type != AUDIO_HDA) return false;

    return configure_stream(ctrl, &ctrl->capture, false,
                            sampleRate, bits, channels);
}

bool start_playback(uint8_t ctrlIndex)
{
    if (ctrlIndex >= s_controllerCount) return false;
    AudioController* ctrl = &s_controllers[ctrlIndex];
    if (!ctrl->active || !ctrl->playback.active) return false;

    uint32_t sdBase = HDA_SD_BASE + ctrl->playback.streamIndex * HDA_SD_INTERVAL;
    uint8_t sdCtl = hda_read8(ctrl, sdBase + HDA_SD_CTL);
    hda_write8(ctrl, sdBase + HDA_SD_CTL, sdCtl | 0x02); // RUN bit
    ctrl->playback.running = true;
    return true;
}

bool stop_playback(uint8_t ctrlIndex)
{
    if (ctrlIndex >= s_controllerCount) return false;
    AudioController* ctrl = &s_controllers[ctrlIndex];
    if (!ctrl->active || !ctrl->playback.active) return false;

    uint32_t sdBase = HDA_SD_BASE + ctrl->playback.streamIndex * HDA_SD_INTERVAL;
    uint8_t sdCtl = hda_read8(ctrl, sdBase + HDA_SD_CTL);
    hda_write8(ctrl, sdBase + HDA_SD_CTL, sdCtl & ~0x02);
    ctrl->playback.running = false;
    return true;
}

bool start_capture(uint8_t ctrlIndex)
{
    if (ctrlIndex >= s_controllerCount) return false;
    AudioController* ctrl = &s_controllers[ctrlIndex];
    if (!ctrl->active || !ctrl->capture.active) return false;

    uint32_t sdBase = HDA_SD_BASE + ctrl->capture.streamIndex * HDA_SD_INTERVAL;
    uint8_t sdCtl = hda_read8(ctrl, sdBase + HDA_SD_CTL);
    hda_write8(ctrl, sdBase + HDA_SD_CTL, sdCtl | 0x02);
    ctrl->capture.running = true;
    return true;
}

bool stop_capture(uint8_t ctrlIndex)
{
    if (ctrlIndex >= s_controllerCount) return false;
    AudioController* ctrl = &s_controllers[ctrlIndex];
    if (!ctrl->active || !ctrl->capture.active) return false;

    uint32_t sdBase = HDA_SD_BASE + ctrl->capture.streamIndex * HDA_SD_INTERVAL;
    uint8_t sdCtl = hda_read8(ctrl, sdBase + HDA_SD_CTL);
    hda_write8(ctrl, sdBase + HDA_SD_CTL, sdCtl & ~0x02);
    ctrl->capture.running = false;
    return true;
}

bool submit_playback_buffer(uint8_t ctrlIndex, uint64_t physAddr, uint32_t length)
{
    if (ctrlIndex >= s_controllerCount) return false;
    AudioController* ctrl = &s_controllers[ctrlIndex];
    PCMStream* s = &ctrl->playback;
    if (!s->active || s->bdlCount >= MAX_BDL_ENTRIES) return false;

    BDLEntry& e = s->bdl[s->bdlCount];
    e.address = physAddr;
    e.length  = length;
    e.flags   = BDL_FLAG_IOC;
    s->bdlCount++;

    // Update stream descriptor BDL pointer and LVI
    uint32_t sdBase = HDA_SD_BASE + s->streamIndex * HDA_SD_INTERVAL;
    uint64_t bdlAddr = kernel_virt_to_phys(reinterpret_cast<uintptr_t>(s->bdl));
    hda_write32(ctrl, sdBase + HDA_SD_BDPL, static_cast<uint32_t>(bdlAddr));
    hda_write32(ctrl, sdBase + HDA_SD_BDPU, static_cast<uint32_t>(bdlAddr >> 32));
    hda_write16(ctrl, sdBase + HDA_SD_LVI, static_cast<uint16_t>(s->bdlCount - 1));

    // Update CBL (total bytes)
    uint32_t totalBytes = 0;
    for (uint8_t i = 0; i < s->bdlCount; ++i) totalBytes += s->bdl[i].length;
    hda_write32(ctrl, sdBase + HDA_SD_CBL, totalBytes);

    return true;
}

bool submit_capture_buffer(uint8_t ctrlIndex, uint64_t physAddr, uint32_t length)
{
    if (ctrlIndex >= s_controllerCount) return false;
    AudioController* ctrl = &s_controllers[ctrlIndex];
    PCMStream* s = &ctrl->capture;
    if (!s->active || s->bdlCount >= MAX_BDL_ENTRIES) return false;

    BDLEntry& e = s->bdl[s->bdlCount];
    e.address = physAddr;
    e.length  = length;
    e.flags   = BDL_FLAG_IOC;
    s->bdlCount++;

    uint32_t sdBase = HDA_SD_BASE + s->streamIndex * HDA_SD_INTERVAL;
    uint64_t bdlAddr = kernel_virt_to_phys(reinterpret_cast<uintptr_t>(s->bdl));
    hda_write32(ctrl, sdBase + HDA_SD_BDPL, static_cast<uint32_t>(bdlAddr));
    hda_write32(ctrl, sdBase + HDA_SD_BDPU, static_cast<uint32_t>(bdlAddr >> 32));
    hda_write16(ctrl, sdBase + HDA_SD_LVI, static_cast<uint16_t>(s->bdlCount - 1));

    uint32_t totalBytes = 0;
    for (uint8_t i = 0; i < s->bdlCount; ++i) totalBytes += s->bdl[i].length;
    hda_write32(ctrl, sdBase + HDA_SD_CBL, totalBytes);

    return true;
}

uint32_t get_playback_position(uint8_t ctrlIndex)
{
    if (ctrlIndex >= s_controllerCount) return 0;
    AudioController* ctrl = &s_controllers[ctrlIndex];
    if (!ctrl->playback.active) return 0;

    uint32_t sdBase = HDA_SD_BASE + ctrl->playback.streamIndex * HDA_SD_INTERVAL;
    return hda_read32(ctrl, sdBase + HDA_SD_LPIB);
}

uint16_t hda_rirb_wp(uint8_t ctrlIndex)
{
    if (ctrlIndex >= s_controllerCount) return 0;
    AudioController* ctrl = &s_controllers[ctrlIndex];
    if (!ctrl->active || ctrl->type != AUDIO_HDA) return 0;
    return hda_read16(ctrl, HDA_RIRBWP);
}

bool hda_rirb_entry(uint8_t ctrlIndex, uint16_t index, uint64_t* out)
{
    if (ctrlIndex >= s_controllerCount) return false;
    if (index >= 256) return false;
    AudioController* ctrl = &s_controllers[ctrlIndex];
    if (!ctrl->active || ctrl->type != AUDIO_HDA) return false;
    uint64_t* rirb = &s_rirbBuf[ctrlIndex * 256];
    if (out) *out = rirb[index % (ctrl->rirbSize ? ctrl->rirbSize : 1)];
    return true;
}

uint16_t hda_rirb_size(uint8_t ctrlIndex)
{
    if (ctrlIndex >= s_controllerCount) return 0;
    AudioController* ctrl = &s_controllers[ctrlIndex];
    if (!ctrl->active || ctrl->type != AUDIO_HDA) return 0;
    return ctrl->rirbSize;
}

uint64_t hda_corb_base(uint8_t ctrlIndex)
{
    if (ctrlIndex >= s_controllerCount) return 0;
    AudioController* ctrl = &s_controllers[ctrlIndex];
    if (!ctrl->active || ctrl->type != AUDIO_HDA) return 0;
    return ctrl->corbPhysAddr;
}

uint64_t hda_rirb_base(uint8_t ctrlIndex)
{
    if (ctrlIndex >= s_controllerCount) return 0;
    AudioController* ctrl = &s_controllers[ctrlIndex];
    if (!ctrl->active || ctrl->type != AUDIO_HDA) return 0;
    return ctrl->rirbPhysAddr;
}

// Immediate Command register offsets (no DMA; direct MMIO doorbell).
static const uint32_t HDA_IC  = 0x60;
static const uint32_t HDA_IR  = 0x64;
static const uint32_t HDA_IRS = 0x68;
static const uint16_t HDA_IRS_BUSY  = 0x01;
static const uint16_t HDA_IRS_VALID = 0x02;

bool hda_immediate_verb(uint8_t ctrlIndex, uint32_t verb, uint32_t* response)
{
    if (ctrlIndex >= s_controllerCount) return false;
    AudioController* ctrl = &s_controllers[ctrlIndex];
    if (!ctrl->active || ctrl->type != AUDIO_HDA) return false;

    hda_write32(ctrl, HDA_IC, verb);
    // Bounded poll: busy clears, then valid must set.
    for (int timeout = 0; timeout < 100000; ++timeout) {
        const uint16_t irs = hda_read16(ctrl, HDA_IRS);
        if ((irs & HDA_IRS_BUSY) == 0) {
            if ((irs & HDA_IRS_VALID) == 0) return false;
            if (response) *response = hda_read32(ctrl, HDA_IR);
            return true;
        }
        spin_delay(100);
    }
    return false;
}

} // namespace pci_audio
} // namespace kernel
