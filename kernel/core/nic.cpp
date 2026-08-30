// NIC Driver - Implementation
//
// Scans PCI for network controllers (class 02/00), initialises the
// first supported Intel E1000 found, sets up RX/TX descriptor rings,
// and provides raw Ethernet frame send/receive.
//
// Uses MMIO-mapped BAR0 registers.  On x86/amd64 the PCI config
// space is accessed via port-I/O (0xCF8/0xCFC).  On architectures
// without PCI the driver is a no-op stub.
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/kernel/nic.h"
#include "include/kernel/arch.h"
#include "include/kernel/serial_debug.h"

#if ARCH_HAS_PIC_8259
#include "include/kernel/interrupts.h"
#endif

namespace kernel {
namespace nic {

// ================================================================
// Internal helpers
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

// ================================================================
// Internal state
// ================================================================

static NICDevice   s_device;
static bool        s_initialised = false;
static uint64_t    s_kernelPhysicalBase = 0x100000;

// Descriptor rings (statically allocated, 16-byte aligned)
#if defined(__GNUC__) || defined(__clang__)
static RxDescriptor s_rxDescs[NUM_RX_DESC] __attribute__((aligned(16)));
static TxDescriptor s_txDescs[NUM_TX_DESC] __attribute__((aligned(16)));
static uint8_t s_rxBuffers[NUM_RX_DESC][RX_BUFFER_SIZE] __attribute__((aligned(16)));
static uint8_t s_txBuffer[ETH_FRAME_MAX] __attribute__((aligned(16)));
#else
__declspec(align(16)) static RxDescriptor s_rxDescs[NUM_RX_DESC];
__declspec(align(16)) static TxDescriptor s_txDescs[NUM_TX_DESC];
__declspec(align(16)) static uint8_t s_rxBuffers[NUM_RX_DESC][RX_BUFFER_SIZE];
__declspec(align(16)) static uint8_t s_txBuffer[ETH_FRAME_MAX];
#endif

// Current descriptor indices
static uint16_t s_rxCur = 0;
static uint16_t s_txCur = 0;

// Hardware waits in the I219 path are deliberately iteration-bounded.  The
// bound does not depend on PIT/IRQ progress because this code runs before the
// input/main-loop readiness checkpoints.
static const uint32_t I219_PHASE5_HW_WAIT_LIMIT = 100000u;

static void mask_nic_interrupts(uint64_t mmioBase);
static inline void mmio_write32(uint64_t base, uint32_t reg, uint32_t val);
static inline uint32_t mmio_read32(uint64_t base, uint32_t reg);

static bool is_i219_device(uint16_t deviceId)
{
    return deviceId == PCI_DEVICE_I219_LM;
}

static bool i219_phase7_path_selected()
{
    // Phase 5/6 selectors retain their historical isolation behavior. The
    // permanent PCH path is selected only by the default Phase 5 boundary
    // with no Phase 6 micro-stage active.
    return GXOS_AIDA_I219_PHASE5_STAGE == 8 &&
           GXOS_AIDA_I219_PHASE6_STAGE == 0;
}

static void set_init_failure(InitStage stage, const char* reason,
                             bool allowHardwareMask = true)
{
    s_device.initStage = stage;
    s_device.active = false;
    s_device.nicRegistered = false;
    s_device.pollingEnabled = false;
    s_device.irqRegistered = false;
    s_device.interruptsEnabled = false;
    if (is_i219_device(s_device.deviceId)) s_device.phase5Stopped = true;

    // Best-effort hardware masking is part of the fail-closed contract.  The
    // state flags above remain authoritative if the device no longer answers
    // MMIO reads/writes.
    // Stages 0-2 are intentionally non-invasive: stopping at those stages
    // must not add a write merely to report the stop.  From Stage 3 onward,
    // fail-closed masking is required after every attempted hardware access.
    if (allowHardwareMask && s_device.mmioMapped && s_device.mmioBase != 0 &&
        (!is_i219_device(s_device.deviceId) || s_device.phase5Stage >= 3u)) {
        mask_nic_interrupts(s_device.mmioBase);
    }

    uint32_t i = 0;
    if (reason != nullptr) {
        while (reason[i] != '\0' && i + 1 < sizeof(s_device.lastInitFailure)) {
            s_device.lastInitFailure[i] = reason[i];
            ++i;
        }
    }
    s_device.lastInitFailure[i] = '\0';

    serial::puts(is_i219_device(s_device.deviceId) ? "[NIC] I219 init failed: "
                                                     : "[NIC] E1000 init failed: ");
    serial::puts(s_device.lastInitFailure);
    serial::putc('\n');
}

static void phase5_stage_number(uint8_t stage)
{
    serial::putc(static_cast<char>('0' + (stage <= 9u ? stage : 9u)));
}

static void phase5_stage_enter(uint8_t stage)
{
    serial::puts("[AIDA-I219-P5] stage=");
    phase5_stage_number(stage);
    serial::puts(" enter\n");
}

static void phase5_stage_complete(uint8_t stage)
{
    serial::puts("[AIDA-I219-P5] stage=");
    phase5_stage_number(stage);
    serial::puts(" complete\n");
}

static void phase5_stage_failed(uint8_t stage)
{
    serial::puts("[AIDA-I219-P5] stage=");
    phase5_stage_number(stage);
    serial::puts(" failed; NIC abandoned\n");
}

static bool phase5_stop(uint8_t stage, InitStage initStage, const char* reason)
{
    set_init_failure(initStage, reason);
    serial::puts("[AIDA-I219-P5] stage=");
    phase5_stage_number(stage);
    serial::puts(" complete; bring-up intentionally stopped\n");
    return false;
}

static void phase6_stage_enter(uint8_t stage, const char* name)
{
    serial::puts("[AIDA-I219-P6] stage=");
    phase5_stage_number(stage);
    serial::puts(" enter name=");
    serial::puts(name);
    serial::puts("\n");
}

static void phase6_stage_complete(uint8_t stage)
{
    serial::puts("[AIDA-I219-P6] stage=");
    phase5_stage_number(stage);
    serial::puts(" complete\n");
}

static void phase6_stage_failed(uint8_t stage, const char* reason)
{
    serial::puts("[AIDA-I219-P6] stage=");
    phase5_stage_number(stage);
    serial::puts(" failed reason=");
    serial::puts(reason);
    serial::puts("\n");
}

// A diagnostic stage may intentionally stop immediately after a write or
// reset.  Do not perform the normal fail-closed MMIO mask in that path: an
// extra access would move the observed physical boundary past the operation
// under test.  The software state is still fail-closed and the device never
// reaches the NIC registration or interrupt-enable path.
static bool phase6_stop(uint8_t stage, InitStage initStage, const char* reason)
{
    set_init_failure(initStage, reason, false);
    serial::puts("[AIDA-I219-P6] stage=");
    phase5_stage_number(stage);
    serial::puts(" bring-up intentionally stopped\n");
    return false;
}

static void phase6_trace_begin(const char* operation)
{
    serial::puts("[AIDA-I219-P6] op=");
    serial::puts(operation);
    serial::puts(" begin\n");
}

static void phase6_trace_complete(const char* operation)
{
    serial::puts("[AIDA-I219-P6] op=");
    serial::puts(operation);
    serial::puts(" complete\n");
}

static void phase6_trace_write(uint64_t mmioBase, uint32_t reg, uint32_t value,
                               const char* operation)
{
    phase6_trace_begin(operation);
    mmio_write32(mmioBase, reg, value);
    phase6_trace_complete(operation);
    serial::puts("[AIDA-I219-P6] op=");
    serial::puts(operation);
    serial::puts(" reg=0x");
    serial::put_hex32(reg);
    serial::puts(" value=0x");
    serial::put_hex32(value);
    serial::putc('\n');
}

static uint32_t phase6_trace_read(uint64_t mmioBase, uint32_t reg,
                                  const char* operation)
{
    phase6_trace_begin(operation);
    const uint32_t value = mmio_read32(mmioBase, reg);
    phase6_trace_complete(operation);
    serial::puts("[AIDA-I219-P6] op=");
    serial::puts(operation);
    serial::puts(" reg=0x");
    serial::put_hex32(reg);
    serial::puts(" value=0x");
    serial::put_hex32(value);
    serial::putc('\n');
    return value;
}

// Linux e1000e supplies real sleeps around the PCH reset.  guideXOS currently
// has no calibrated microsecond delay API, so use the existing x86 port-80
// delay primitive as a bounded approximation.  This is a sequencing guard,
// not reset-completion detection; completion remains a separate finite MMIO
// poll below.
static void pch_reset_delay_ms(uint32_t milliseconds)
{
#if ARCH_HAS_PORT_IO
    for (uint32_t ms = 0; ms < milliseconds; ++ms) {
        for (uint32_t i = 0; i < 10000u; ++i) {
            arch::outb(0x80u, 0u);
        }
    }
#else
    (void)milliseconds;
#endif
}

static void phase7_print_mac_marker(const uint8_t* mac)
{
    serial::puts("[AIDA-I219-P7] mac=");
    for (uint8_t i = 0; i < ETH_ALEN; ++i) {
        if (i > 0) serial::putc(':');
        serial::put_hex8(mac[i]);
    }
    serial::puts(" valid=yes source=RAL0/RAH0\n");
}

static void phase7_stage_enter(const char* name)
{
    serial::puts("[AIDA-I219-P7] stage=");
    serial::put_hex8(GXOS_AIDA_I219_PHASE7_STAGE);
    serial::puts(" enter name=");
    serial::puts(name);
    serial::puts("\n");
}

static void phase7_stage_complete(const char* name)
{
    serial::puts("[AIDA-I219-P7] ");
    serial::puts(name);
    serial::puts("=ready\n");
}

static bool phase7_stop(InitStage initStage, const char* reason)
{
    set_init_failure(initStage, reason);
    serial::puts("[AIDA-I219-P7] stage=");
    serial::puts(phase7_stage_name(GXOS_AIDA_I219_PHASE7_STAGE));
    serial::puts(" complete; bring-up intentionally stopped\n");
    return false;
}

// Permanent exact-I219 reset path. This is the smallest physically proven
// PCH repair: it deliberately omits Linux's ownership/SWFLAG/FWSM/PHY-reset
// machinery until guideXOS has an evidence-backed need for those operations.
static bool i219_pch_reset(uint64_t mmioBase)
{
    s_device.initStage = NIC_INIT_RESET;

    // Phase 6 physical evidence requires causes to be masked and drained
    // before quiescing the engines.
    mask_nic_interrupts(mmioBase);
    mmio_write32(mmioBase, E1000_RCTL, 0u);
    mmio_write32(mmioBase, E1000_TCTL, E1000_TCTL_PSP);

    // Flush posted writes through STATUS before the reset request. Do not
    // perform a post-reset STATUS/CTRL flush until the finite delay elapses.
    const uint32_t status = mmio_read32(mmioBase, E1000_STATUS);
    s_device.statusValue = status;
    if (status == 0xFFFFFFFFu) {
        serial::puts("[AIDA-I219-P7] reset=FAIL reason=status-all-ones\n");
        set_init_failure(NIC_INIT_MMIO, "I219 STATUS flush returned all-ones");
        return false;
    }

    pch_reset_delay_ms(10u);

    const uint32_t ctrl = mmio_read32(mmioBase, E1000_CTRL);
    s_device.ctrlValue = ctrl;
    if (ctrl == 0xFFFFFFFFu) {
        serial::puts("[AIDA-I219-P7] reset=FAIL reason=ctrl-all-ones\n");
        set_init_failure(NIC_INIT_MMIO, "I219 CTRL read returned all-ones");
        return false;
    }

    // Preserve the hardware's CTRL state exactly as in the proven candidate;
    // only the reset bit is added.
    mmio_write32(mmioBase, E1000_CTRL, ctrl | E1000_CTRL_RST);

    // Immediate post-reset reads were part of the unsafe generic boundary.
    // The first post-write MMIO read is intentionally delayed and is only the
    // bounded completion poll below.
    pch_reset_delay_ms(20u);

    bool resetComplete = false;
    for (uint32_t i = 0; i < I219_PHASE5_HW_WAIT_LIMIT; ++i) {
        const uint32_t observedCtrl = mmio_read32(mmioBase, E1000_CTRL);
        s_device.ctrlValue = observedCtrl;
        if (observedCtrl == 0xFFFFFFFFu) {
            serial::puts("[AIDA-I219-P7] reset=FAIL reason=poll-all-ones\n");
            set_init_failure(NIC_INIT_RESET, "I219 reset poll returned all-ones");
            return false;
        }
        if ((observedCtrl & E1000_CTRL_RST) == 0u) {
            resetComplete = true;
            break;
        }
    }
    if (!resetComplete) {
        serial::puts("[AIDA-I219-P7] reset=FAIL reason=poll-timeout\n");
        set_init_failure(NIC_INIT_RESET, "I219 PCH reset completion timeout");
        return false;
    }

    // Linux re-masks and drains causes after the PCH reset. This is the last
    // operation in the permanent reset helper and still never enables IMS.
    mask_nic_interrupts(mmioBase);
    serial::puts("[AIDA-I219-P7] reset=PASS\n");
    return true;
}

static bool run_i219_phase6_micro_stage(uint64_t mmioBase)
{
#if GXOS_AIDA_I219_PHASE6_STAGE == 0
    (void)mmioBase;
    return true;
#else
    const uint8_t stage = static_cast<uint8_t>(GXOS_AIDA_I219_PHASE6_STAGE);
    uint32_t ctrl = 0;

    phase6_stage_enter(stage,
#if GXOS_AIDA_I219_PHASE6_STAGE == 1
                       "mask-only"
#elif GXOS_AIDA_I219_PHASE6_STAGE == 2
                       "mask-plus-rctl-disable"
#elif GXOS_AIDA_I219_PHASE6_STAGE == 3
                       "mask-plus-rctl-tctl-disable"
#elif GXOS_AIDA_I219_PHASE6_STAGE == 4
                       "ctrl-read"
#elif GXOS_AIDA_I219_PHASE6_STAGE == 5
                       "ctrl-rst-write"
#else
                       "pch-reset-candidate"
#endif
    );

    // Stages 1-5 deliberately preserve the operation values of the failed
    // Phase 5 boundary so each added write/read can be tested independently.
    if (stage <= 5u) {
        phase6_trace_write(mmioBase, E1000_IMC, 0xFFFFFFFFu, "imc-write");
        const uint32_t icr = phase6_trace_read(mmioBase, E1000_ICR, "icr-read-clear");
        serial::puts("[AIDA-I219-P6] op=icr-read-clear observed=0x");
        serial::put_hex32(icr);
        serial::putc('\n');
    }
    if (stage >= 2u && stage <= 5u) {
        phase6_trace_write(mmioBase, E1000_RCTL, 0u, "rctl-disable");
    }
    if (stage >= 3u && stage <= 5u) {
        phase6_trace_write(mmioBase, E1000_TCTL, 0u, "tctl-disable");
    }
    if (stage >= 4u && stage <= 5u) {
        ctrl = phase6_trace_read(mmioBase, E1000_CTRL, "ctrl-read");
        s_device.ctrlValue = ctrl;
        if (ctrl == 0xFFFFFFFFu) {
            phase6_stage_failed(stage, "CTRL returned all-ones");
            return phase6_stop(stage, NIC_INIT_MMIO,
                               "Phase 6 CTRL read returned all-ones");
        }
    }
    if (stage == 1u) {
        phase6_stage_complete(stage);
        return phase6_stop(stage, NIC_INIT_RESET,
                           "Phase 6 mask-only diagnostic stop");
    }
    if (stage == 2u) {
        phase6_stage_complete(stage);
        return phase6_stop(stage, NIC_INIT_RESET,
                           "Phase 6 RCTL-disable diagnostic stop");
    }
    if (stage == 3u) {
        phase6_stage_complete(stage);
        return phase6_stop(stage, NIC_INIT_RESET,
                           "Phase 6 TCTL-disable diagnostic stop");
    }
    if (stage == 4u) {
        phase6_stage_complete(stage);
        return phase6_stop(stage, NIC_INIT_RESET,
                           "Phase 6 CTRL-read diagnostic stop");
    }
    if (stage == 5u) {
        const uint32_t resetValue = ctrl | E1000_CTRL_RST;
        phase6_trace_write(mmioBase, E1000_CTRL, resetValue, "ctrl-rst-write");
        s_device.ctrlValue = resetValue;
        phase6_stage_complete(stage);
        return phase6_stop(stage, NIC_INIT_RESET,
                           "Phase 6 stopped immediately after CTRL.RST write");
    }

    // Candidate reset flow, limited to the MAC/reset boundary.  This follows
    // the PCH ordering that Linux e1000e uses, but intentionally does not
    // touch PHY/MDIC, NVM, DMA, or interrupts beyond keeping IMC masked.
    phase6_trace_write(mmioBase, E1000_IMC, 0xFFFFFFFFu, "reset-imc-write");
    phase6_trace_write(mmioBase, E1000_RCTL, 0u, "reset-rctl-disable");
    phase6_trace_write(mmioBase, E1000_TCTL, E1000_TCTL_PSP, "reset-tctl-psp");
    const uint32_t flushValue = phase6_trace_read(mmioBase, E1000_STATUS, "reset-status-flush");
    (void)flushValue;
    phase6_trace_begin("reset-pre-delay-10ms");
    pch_reset_delay_ms(10u);
    phase6_trace_complete("reset-pre-delay-10ms");

    ctrl = phase6_trace_read(mmioBase, E1000_CTRL, "reset-ctrl-read");
    s_device.ctrlValue = ctrl;
    if (ctrl == 0xFFFFFFFFu) {
        phase6_stage_failed(stage, "reset CTRL returned all-ones");
        return phase6_stop(stage, NIC_INIT_MMIO,
                           "Phase 6 reset CTRL read returned all-ones");
    }

    const uint32_t resetValue = ctrl | E1000_CTRL_RST;
    phase6_trace_write(mmioBase, E1000_CTRL, resetValue, "reset-ctrl-rst-write");
    phase6_trace_begin("reset-post-delay-20ms");
    pch_reset_delay_ms(20u);
    phase6_trace_complete("reset-post-delay-20ms");

    bool resetComplete = false;
    uint32_t iterations = 0;
    phase6_trace_begin("reset-poll");
    for (; iterations < I219_PHASE5_HW_WAIT_LIMIT; ++iterations) {
        ctrl = mmio_read32(mmioBase, E1000_CTRL);
        s_device.ctrlValue = ctrl;
        if (ctrl == 0xFFFFFFFFu) {
            phase6_trace_complete("reset-poll");
            phase6_stage_failed(stage, "reset poll returned all-ones");
            return phase6_stop(stage, NIC_INIT_RESET,
                               "Phase 6 reset poll returned all-ones");
        }
        if ((ctrl & E1000_CTRL_RST) == 0u) {
            resetComplete = true;
            ++iterations;
            break;
        }
    }
    phase6_trace_complete("reset-poll");
    serial::puts("[AIDA-I219-P6] op=reset-poll iterations=");
    serial::put_hex32(iterations);
    serial::putc('\n');
    if (!resetComplete) {
        phase6_stage_failed(stage, "reset poll timeout");
        return phase6_stop(stage, NIC_INIT_RESET,
                           "Phase 6 PCH reset completion timeout");
    }

    // Linux re-masks and drains causes after reset.  Keep this final mask in
    // the candidate image, but do not enable IMS or proceed to PHY/DMA.
    phase6_trace_write(mmioBase, E1000_IMC, 0xFFFFFFFFu, "reset-post-imc-write");
    const uint32_t postResetIcr = phase6_trace_read(mmioBase, E1000_ICR,
                                                     "reset-post-icr-read-clear");
    serial::puts("[AIDA-I219-P6] op=reset-post-icr-read-clear observed=0x");
    serial::put_hex32(postResetIcr);
    serial::putc('\n');
    phase6_stage_complete(stage);
    return phase6_stop(stage, NIC_INIT_RESET,
                       "Phase 6 candidate reset diagnostic stop");
#endif
}

static bool dma_address(const void* ptr, uint64_t* physicalOut)
{
    if (ptr == nullptr || physicalOut == nullptr) return false;

    const uint64_t virt = reinterpret_cast<uint64_t>(ptr);
    // The linker places kernel static storage at virtual 0x100000.  The
    // bootloader supplies the physical base for that image; do not silently
    // treat an unrelated virtual address as DMA-capable physical memory.
    if (virt < 0x100000) return false;

    const uint64_t offset = virt - 0x100000;
    if (s_kernelPhysicalBase > (~0ULL - offset)) return false;

    const uint64_t physical = s_kernelPhysicalBase + offset;
    if (physical == 0) return false;

    *physicalOut = physical;
    return true;
}

static bool dma_address_range(const void* ptr, uint64_t length,
                              uint64_t alignment, uint64_t* physicalOut)
{
    if (length == 0u || !dma_address(ptr, physicalOut)) return false;
    if (alignment != 0u && ((*physicalOut & (alignment - 1u)) != 0u)) {
        return false;
    }
    if (*physicalOut > (~0ULL - (length - 1u))) return false;
    return true;
}

static bool dma_ranges_overlap(uint64_t first, uint64_t firstLength,
                               uint64_t second, uint64_t secondLength)
{
    if (firstLength == 0u || secondLength == 0u) return false;
    const uint64_t firstEnd = first + firstLength;
    const uint64_t secondEnd = second + secondLength;
    return first < secondEnd && second < firstEnd;
}

// All DMA objects are static storage in the loaded kernel image. The UEFI
// loader maps that complete image and the physical translation below uses the
// same image base supplied in BootInfo. Validate the complete small layout
// before handing any ring to hardware so a bad translation fails closed.
static bool validate_dma_layout()
{
    static_assert(sizeof(RxDescriptor) == 16u, "RX descriptor size changed");
    static_assert(sizeof(TxDescriptor) == 16u, "TX descriptor size changed");
    static_assert((NUM_RX_DESC * sizeof(RxDescriptor)) % 128u == 0u,
                  "RX ring length must be 128-byte aligned");
    static_assert((NUM_TX_DESC * sizeof(TxDescriptor)) % 128u == 0u,
                  "TX ring length must be 128-byte aligned");

    uint64_t rxDescPhys = 0;
    uint64_t txDescPhys = 0;
    uint64_t txBufferPhys = 0;
    if (!dma_address_range(&s_rxDescs[0],
                           NUM_RX_DESC * sizeof(RxDescriptor), 16u,
                           &rxDescPhys) ||
        !dma_address_range(&s_txDescs[0],
                           NUM_TX_DESC * sizeof(TxDescriptor), 16u,
                           &txDescPhys) ||
        !dma_address_range(&s_txBuffer[0], sizeof(s_txBuffer), 16u,
                           &txBufferPhys)) {
        return false;
    }

    if (dma_ranges_overlap(rxDescPhys, NUM_RX_DESC * sizeof(RxDescriptor),
                           txDescPhys, NUM_TX_DESC * sizeof(TxDescriptor)) ||
        dma_ranges_overlap(rxDescPhys, NUM_RX_DESC * sizeof(RxDescriptor),
                           txBufferPhys, sizeof(s_txBuffer)) ||
        dma_ranges_overlap(txDescPhys, NUM_TX_DESC * sizeof(TxDescriptor),
                           txBufferPhys, sizeof(s_txBuffer))) {
        return false;
    }

    uint64_t rxBufferPhys[NUM_RX_DESC];
    for (uint16_t i = 0; i < NUM_RX_DESC; ++i) {
        if (!dma_address_range(&s_rxBuffers[i][0], RX_BUFFER_SIZE, 16u,
                               &rxBufferPhys[i])) {
            return false;
        }
        if (dma_ranges_overlap(rxDescPhys, NUM_RX_DESC * sizeof(RxDescriptor),
                               rxBufferPhys[i], RX_BUFFER_SIZE) ||
            dma_ranges_overlap(txDescPhys, NUM_TX_DESC * sizeof(TxDescriptor),
                               rxBufferPhys[i], RX_BUFFER_SIZE) ||
            dma_ranges_overlap(txBufferPhys, sizeof(s_txBuffer),
                               rxBufferPhys[i], RX_BUFFER_SIZE)) {
            return false;
        }
        for (uint16_t previous = 0; previous < i; ++previous) {
            if (dma_ranges_overlap(rxBufferPhys[previous], RX_BUFFER_SIZE,
                                   rxBufferPhys[i], RX_BUFFER_SIZE)) {
                return false;
            }
        }
    }
    return true;
}

static inline void dma_memory_barrier()
{
#if defined(__GNUC__) || defined(__clang__)
    asm volatile("" ::: "memory");
#endif
}

// ================================================================
// MMIO register access
//
// In a real kernel these would use volatile MMIO pointers.
// For the MSVC build path (host-side simulation) we provide
// stub implementations.
// ================================================================

static inline void mmio_write32(uint64_t base, uint32_t reg, uint32_t val)
{
    volatile uint32_t* addr = reinterpret_cast<volatile uint32_t*>(
        static_cast<uintptr_t>(base + reg));
    *addr = val;
}

static inline uint32_t mmio_read32(uint64_t base, uint32_t reg)
{
    volatile uint32_t* addr = reinterpret_cast<volatile uint32_t*>(
        static_cast<uintptr_t>(base + reg));
    return *addr;
}

static void mask_nic_interrupts(uint64_t mmioBase)
{
    if (mmioBase == 0) return;
    mmio_write32(mmioBase, E1000_IMC, 0xFFFFFFFFu);
    mmio_read32(mmioBase, E1000_ICR);
}

static bool is_supported_nic(uint16_t vendor, uint16_t device)
{
    if (vendor != PCI_VENDOR_INTEL) return false;
    return (device == PCI_DEVICE_E1000 ||
            device == PCI_DEVICE_E1000E ||
            device == PCI_DEVICE_I217 ||
            device == PCI_DEVICE_I219_LM);
}

// ================================================================
// PCI configuration space (port-I/O method - x86/amd64)
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
                        uint8_t offset, uint32_t value)
{
    uint32_t addr = 0x80000000u |
                    (static_cast<uint32_t>(bus)  << 16) |
                    (static_cast<uint32_t>(dev)  << 11) |
                    (static_cast<uint32_t>(func) << 8)  |
                    (offset & 0xFC);
    arch::outl(PCI_CONFIG_ADDR, addr);
    arch::outl(PCI_CONFIG_DATA, value);
}

static uint16_t pci_read16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset)
{
    uint32_t dword = pci_read32(bus, dev, func, offset & 0xFC);
    return static_cast<uint16_t>(dword >> ((offset & 2) * 8));
}

// ================================================================
// Read station address from the device
// ================================================================

static uint16_t eeprom_read(uint64_t mmioBase, uint8_t addr)
{
    uint32_t val = (static_cast<uint32_t>(addr) << E1000_EERD_ADDR_SHIFT) |
                   E1000_EERD_START;
    mmio_write32(mmioBase, E1000_EERD, val);

    // Poll for DONE
    for (uint32_t i = 0; i < I219_PHASE5_HW_WAIT_LIMIT; ++i) {
        uint32_t rd = mmio_read32(mmioBase, E1000_EERD);
        if (rd & E1000_EERD_DONE) {
            return static_cast<uint16_t>(rd >> E1000_EERD_DATA_SHIFT);
        }
    }
    return 0;
}

static bool read_rar0_address(uint64_t mmioBase, uint8_t* mac, bool requireValidBit)
{
    if (!mac) return false;

    const uint32_t ral = mmio_read32(mmioBase, E1000_RAL0);
    const uint32_t rah = mmio_read32(mmioBase, E1000_RAH0);
    if (ral == 0xFFFFFFFFu || rah == 0xFFFFFFFFu) return false;
    if (requireValidBit && (rah & E1000_RAH_AV) == 0u) return false;

    mac[0] = static_cast<uint8_t>(ral);
    mac[1] = static_cast<uint8_t>(ral >> 8);
    mac[2] = static_cast<uint8_t>(ral >> 16);
    mac[3] = static_cast<uint8_t>(ral >> 24);
    mac[4] = static_cast<uint8_t>(rah);
    mac[5] = static_cast<uint8_t>(rah >> 8);
    return is_valid_station_mac(mac);
}

static bool read_mac_address(uint64_t mmioBase, uint8_t* mac)
{
    if (!mac) return false;

    // I219 is the PCH LAN controller.  Its station address is loaded from
    // NVM into RAL0/RAH0; use that hardware state directly and do not issue
    // the older discrete-controller EEPROM command sequence.
    if (is_i219_device(s_device.deviceId)) {
        return read_rar0_address(mmioBase, mac, true);
    }

    // Preserve the existing EEPROM-first behavior for the older supported
    // E1000-family devices, but accept only a valid station address.
    const uint16_t w0 = eeprom_read(mmioBase, 0);
    const uint16_t w1 = eeprom_read(mmioBase, 1);
    const uint16_t w2 = eeprom_read(mmioBase, 2);
    mac[0] = static_cast<uint8_t>(w0);
    mac[1] = static_cast<uint8_t>(w0 >> 8);
    mac[2] = static_cast<uint8_t>(w1);
    mac[3] = static_cast<uint8_t>(w1 >> 8);
    mac[4] = static_cast<uint8_t>(w2);
    mac[5] = static_cast<uint8_t>(w2 >> 8);
    if (is_valid_station_mac(mac)) return true;

    return read_rar0_address(mmioBase, mac, false);
}

static bool mdic_read(uint64_t mmioBase, uint8_t phyAddress,
                      uint8_t registerAddress, uint16_t* valueOut)
{
    if (!valueOut) return false;

    if (is_i219_device(s_device.deviceId)) {
        serial::puts(i219_phase7_path_selected()
                     ? "[AIDA-I219-P7] mdic begin phy="
                     : "[AIDA-I219-P5] mdic begin phy=");
        serial::put_hex8(phyAddress);
        serial::puts(" reg=");
        serial::put_hex8(registerAddress);
        serial::putc('\n');
    }

    const uint32_t command = E1000_MDIC_OP_READ |
                             (static_cast<uint32_t>(phyAddress) << E1000_MDIC_PHY_SHIFT) |
                             (static_cast<uint32_t>(registerAddress) << E1000_MDIC_REG_SHIFT);
    mmio_write32(mmioBase, E1000_MDIC, command);

    for (uint32_t i = 0; i < I219_PHASE5_HW_WAIT_LIMIT; ++i) {
        const uint32_t mdic = mmio_read32(mmioBase, E1000_MDIC);
        s_device.mdicValue = mdic;
        if ((mdic & E1000_MDIC_READY) == 0u) continue;
        if ((mdic & E1000_MDIC_ERROR) != 0u) {
            serial::puts(i219_phase7_path_selected()
                         ? "[AIDA-I219-P7] mdic error phy="
                         : "[AIDA-I219-P5] mdic error phy=");
            serial::put_hex8(phyAddress);
            serial::puts(" reg=");
            serial::put_hex8(registerAddress);
            serial::putc('\n');
            return false;
        }
        const uint16_t value = static_cast<uint16_t>(mdic & E1000_MDIC_DATA_MASK);
        if (value == 0xFFFFu) {
            serial::puts(i219_phase7_path_selected()
                         ? "[AIDA-I219-P7] mdic invalid phy="
                         : "[AIDA-I219-P5] mdic invalid phy=");
            serial::put_hex8(phyAddress);
            serial::puts(" reg=");
            serial::put_hex8(registerAddress);
            serial::putc('\n');
            return false;
        }
        *valueOut = value;
        if (is_i219_device(s_device.deviceId)) {
            serial::puts(i219_phase7_path_selected()
                         ? "[AIDA-I219-P7] mdic complete phy="
                         : "[AIDA-I219-P5] mdic complete phy=");
            serial::put_hex8(phyAddress);
            serial::puts(" reg=");
            serial::put_hex8(registerAddress);
            serial::putc('\n');
        }
        return true;
    }

    serial::puts(i219_phase7_path_selected()
                 ? "[AIDA-I219-P7] mdic timeout phy="
                 : "[AIDA-I219-P5] mdic timeout phy=");
    serial::put_hex8(phyAddress);
    serial::puts(" reg=");
    serial::put_hex8(registerAddress);
    serial::putc('\n');
    return false;
}

static bool read_i219_phy_status(uint64_t mmioBase)
{
    uint16_t phyId1 = 0;
    uint16_t phyId2 = 0;
    uint16_t phyStatus = 0;
    if (!mdic_read(mmioBase, I219_PHY_ADDRESS, I219_PHY_ID1_REG, &phyId1) ||
        !mdic_read(mmioBase, I219_PHY_ADDRESS, I219_PHY_ID2_REG, &phyId2) ||
        !mdic_read(mmioBase, I219_PHY_ADDRESS, I219_PHY_STATUS_REG, &phyStatus)) {
        s_device.phyAccess = NIC_PHY_FAILED;
        return false;
    }

    // The integrated PCH PHY is conventionally reached at MDI address 1;
    // registers 2/3 are the IEEE PHY identifier and register 26 is the
    // family-specific link/status value. These are read-only assumptions in
    // this stage: no PHY programming or semaphore acquisition is attempted.
    if (phyId1 == 0u || phyId2 == 0u ||
        (phyId1 == 0xFFFFu && phyId2 == 0xFFFFu)) {
        s_device.phyAccess = NIC_PHY_FAILED;
        return false;
    }

    s_device.phyId1 = phyId1;
    s_device.phyId2 = phyId2;
    s_device.phyStatusValue = phyStatus;
    s_device.phyAddress = I219_PHY_ADDRESS;
    s_device.phyAccess = NIC_PHY_OK;
    s_device.link = (phyStatus & I219_PHY_STATUS_LINK) ? NIC_LINK_UP : NIC_LINK_DOWN;
    if (s_device.link == NIC_LINK_DOWN) {
        s_device.negotiatedSpeed = 0;
        s_device.negotiatedFullDuplex = false;
        return true;
    }
    switch (phyStatus & I219_PHY_STATUS_SPEED_MASK) {
        case (0u << 8): s_device.negotiatedSpeed = 10; break;
        case (1u << 8): s_device.negotiatedSpeed = 100; break;
        case (2u << 8): s_device.negotiatedSpeed = 1000; break;
        default: s_device.negotiatedSpeed = 0; break;
    }
    s_device.negotiatedFullDuplex = (phyStatus & I219_PHY_STATUS_DUPLEX) != 0u;
    return true;
}

// ================================================================
// Initialise RX descriptor ring
// ================================================================

static bool init_rx(uint64_t mmioBase)
{
    if (!validate_dma_layout()) return false;

    // Initialise each RX descriptor to point at its buffer
    for (uint16_t i = 0; i < NUM_RX_DESC; ++i) {
        memzero(&s_rxDescs[i], sizeof(RxDescriptor));
        uint64_t bufferPhys = 0;
        if (!dma_address_range(&s_rxBuffers[i][0], RX_BUFFER_SIZE, 16u,
                               &bufferPhys)) return false;
        s_rxDescs[i].bufferAddr = bufferPhys;
        s_rxDescs[i].status     = 0;
    }

    // Program the RX descriptor ring base address
    uint64_t rxDescPhys = 0;
    if (!dma_address_range(&s_rxDescs[0],
                           NUM_RX_DESC * sizeof(RxDescriptor), 16u,
                           &rxDescPhys)) {
        return false;
    }
    mmio_write32(mmioBase, E1000_RDBAL, static_cast<uint32_t>(rxDescPhys & 0xFFFFFFFF));
    mmio_write32(mmioBase, E1000_RDBAH, static_cast<uint32_t>(rxDescPhys >> 32));

    // Descriptor ring length (in bytes)
    mmio_write32(mmioBase, E1000_RDLEN, NUM_RX_DESC * sizeof(RxDescriptor));

    // Head = 0, Tail = NUM_RX_DESC - 1 (all descriptors available)
    mmio_write32(mmioBase, E1000_RDH, 0);
    mmio_write32(mmioBase, E1000_RDT, NUM_RX_DESC - 1);
    s_rxCur = 0;

    // Enable receiver
    uint32_t rctl = E1000_RCTL_EN |
                    E1000_RCTL_BAM |          // accept broadcast
                    E1000_RCTL_BSIZE_2048 |   // 2048-byte buffers
                    E1000_RCTL_SECRC;          // strip CRC
    dma_memory_barrier();
    mmio_write32(mmioBase, E1000_RCTL, rctl);
    s_device.rxRingInitialized = true;
    return true;
}

// ================================================================
// Initialise TX descriptor ring
// ================================================================

static bool init_tx(uint64_t mmioBase)
{
    if (!validate_dma_layout()) return false;

    for (uint16_t i = 0; i < NUM_TX_DESC; ++i) {
        memzero(&s_txDescs[i], sizeof(TxDescriptor));
        s_txDescs[i].status = E1000_TXD_STAT_DD; // mark as done (available)
    }

    // Program the TX descriptor ring base address
    uint64_t txDescPhys = 0;
    if (!dma_address_range(&s_txDescs[0],
                           NUM_TX_DESC * sizeof(TxDescriptor), 16u,
                           &txDescPhys)) {
        return false;
    }
    mmio_write32(mmioBase, E1000_TDBAL, static_cast<uint32_t>(txDescPhys & 0xFFFFFFFF));
    mmio_write32(mmioBase, E1000_TDBAH, static_cast<uint32_t>(txDescPhys >> 32));

    // Descriptor ring length (in bytes)
    mmio_write32(mmioBase, E1000_TDLEN, NUM_TX_DESC * sizeof(TxDescriptor));

    // Head = Tail = 0 (empty ring)
    mmio_write32(mmioBase, E1000_TDH, 0);
    mmio_write32(mmioBase, E1000_TDT, 0);
    s_txCur = 0;

    // Enable transmitter
    // CT  = 0x0F (collision threshold)
    // COLD = 0x040 (collision distance for full duplex)
    uint32_t tctl = E1000_TCTL_EN |
                    E1000_TCTL_PSP |
                    (0x0F << E1000_TCTL_CT_SHIFT) |
                    (0x040 << E1000_TCTL_COLD_SHIFT);
    mmio_write32(mmioBase, E1000_TCTL, tctl);

    // Set inter-packet gap (TIPG)
    mmio_write32(mmioBase, E1000_TIPG, E1000_TIPG_DEFAULT);
    s_device.txRingInitialized = true;
    return true;
}

void set_kernel_physical_base(uint64_t physicalBase)
{
    if (physicalBase != 0) {
        s_kernelPhysicalBase = physicalBase;
    }
}

// ================================================================
// Enable interrupts on the NIC
// ================================================================

static void enable_nic_interrupts(uint64_t mmioBase)
{
    // Clear any pending interrupts
    mmio_read32(mmioBase, E1000_ICR);

    // Enable RX timer, RX descriptor min threshold, link status change
    mmio_write32(mmioBase, E1000_IMS,
                 E1000_ICR_RXT0 |
                 E1000_ICR_RXDMT0 |
                 E1000_ICR_LSC);
}

uint8_t enumerate_network_controllers(NetworkControllerInfo* out,
                                      uint8_t capacity)
{
    if (!out || capacity == 0) return 0;

#if ARCH_HAS_PORT_IO
    uint8_t found = 0;
    for (uint16_t bus = 0; bus < 8; ++bus) {
        for (uint8_t dev = 0; dev < 32; ++dev) {
            uint8_t bus8 = static_cast<uint8_t>(bus);
            uint32_t id = pci_read32(bus8, dev, 0, 0);
            if (id == 0xFFFFFFFFu || id == 0) continue;

            uint32_t headerReg = pci_read32(bus8, dev, 0, 0x0C);
            uint8_t headerType = static_cast<uint8_t>(headerReg >> 16);
            uint8_t maxFunc = (headerType & 0x80) ? 8 : 1;

            for (uint8_t func = 0; func < maxFunc; ++func) {
                if (func > 0) {
                    id = pci_read32(bus8, dev, func, 0);
                    if (id == 0xFFFFFFFFu || id == 0) continue;
                }

                uint32_t classReg = pci_read32(bus8, dev, func, 0x08);
                uint8_t baseClass = static_cast<uint8_t>(classReg >> 24);
                if (baseClass != PCI_CLASS_NETWORK) continue;

                // The diagnostic surface has a fixed bound. Continue the
                // read-only scan so no write-only probing is introduced.
                if (found >= capacity) continue;

                NetworkControllerInfo& info = out[found++];
                info.pciBus = bus8;
                info.pciSlot = dev;
                info.pciFunc = func;
                info.vendorId = static_cast<uint16_t>(id & 0xFFFFu);
                info.deviceId = static_cast<uint16_t>(id >> 16);
                info.subsystemVendorId = pci_read16(bus8, dev, func, 0x2C);
                info.subsystemDeviceId = pci_read16(bus8, dev, func, 0x2E);
                info.revisionId = static_cast<uint8_t>(classReg & 0xFFu);
                info.classCode = baseClass;
                info.subclass = static_cast<uint8_t>(classReg >> 16);
                info.progIf = static_cast<uint8_t>((classReg >> 8) & 0xFFu);
                info.supportedEthernet =
                    info.subclass == PCI_SUBCLASS_ETH &&
                    is_supported_nic(info.vendorId, info.deviceId);
            }
        }
    }
    return found;
#else
    return 0;
#endif
}

// ================================================================
// Reset and initialise the E1000 hardware
// ================================================================

static bool init_e1000(uint64_t mmioBase)
{
    if (mmioBase == 0 || s_device.mmioSize < E1000_MMIO_MIN_SIZE) {
        set_init_failure(NIC_INIT_MMIO, "MMIO BAR is unavailable or too small");
        return false;
    }

    const bool i219 = is_i219_device(s_device.deviceId);
    const bool i219P7 = i219 && i219_phase7_path_selected();
    uint32_t ctrl = 0;

    if (i219P7) {
        if (!i219_pch_reset(mmioBase)) return false;
        if (GXOS_AIDA_I219_PHASE7_STAGE == 0) {
            // The default I219 production path stops at the physically proven
            // reset boundary until the later hardware stages are qualified.
            return phase7_stop(NIC_INIT_RESET,
                               "I219 PCH reset-only default; later bring-up is opt-in");
        }
    } else {
        // Preserve the old generic sequence only for the historical Phase 5
        // isolation selectors. It is never used by the Phase 7/default I219
        // path, and all non-I219 devices retain this exact legacy behavior.
        if (i219) {
            phase5_stage_enter(3);
            serial::puts("[AIDA-I219-P5] reset begin\n");
        }

        // Disable all interrupts and engines before reset. The reset wait is
        // finite and observes the device's self-clearing CTRL.RST bit.
        s_device.initStage = NIC_INIT_RESET;
        mmio_write32(mmioBase, E1000_RCTL, 0);
        mmio_write32(mmioBase, E1000_TCTL, 0);
        mmio_write32(mmioBase, E1000_IMC, 0xFFFFFFFF);
        mmio_read32(mmioBase, E1000_ICR);

        ctrl = mmio_read32(mmioBase, E1000_CTRL);
        s_device.ctrlValue = ctrl;
        if (ctrl == 0xFFFFFFFFu) {
            if (i219) {
                serial::puts("[AIDA-I219-P5] reset CTRL read returned all-ones\n");
                phase5_stage_failed(3);
            }
            set_init_failure(NIC_INIT_MMIO, "MMIO CTRL read returned all-ones");
            return false;
        }

        mmio_write32(mmioBase, E1000_CTRL, ctrl | E1000_CTRL_RST);
        bool resetComplete = false;
        for (uint32_t i = 0; i < I219_PHASE5_HW_WAIT_LIMIT; ++i) {
            ctrl = mmio_read32(mmioBase, E1000_CTRL);
            s_device.ctrlValue = ctrl;
            if (ctrl == 0xFFFFFFFFu) {
                if (i219) phase5_stage_failed(3);
                set_init_failure(NIC_INIT_RESET, "reset read returned all-ones");
                return false;
            }
            if ((ctrl & E1000_CTRL_RST) == 0u) {
                resetComplete = true;
                break;
            }
        }
        if (!resetComplete) {
            if (i219) {
                serial::puts("[AIDA-I219-P5] reset timeout\n");
                phase5_stage_failed(3);
            }
            set_init_failure(NIC_INIT_RESET, "reset timeout");
            return false;
        }
        if (!i219) serial::puts("[NIC] MAC reset: complete (bounded)\n");

        // Keep interrupts masked until all state and rings are ready.
        mask_nic_interrupts(mmioBase);
    }

    if (i219 && !i219P7) {
        serial::puts("[AIDA-I219-P5] reset complete\n");
        phase5_stage_complete(3);
        if (GXOS_AIDA_I219_PHASE5_STAGE == 3) {
            return phase5_stop(3, NIC_INIT_RESET,
                               "Phase 5 stage 3 intentionally stopped after reset");
        }

        phase5_stage_enter(4);
        s_device.initStage = NIC_INIT_MAC;
        if (!read_mac_address(mmioBase, s_device.macAddress)) {
            phase5_stage_failed(4);
            set_init_failure(NIC_INIT_MAC, "invalid MAC in RAL0/RAH0");
            return false;
        }
        serial::puts("[NIC] MAC acquisition: RAL0/RAH0 valid\n");
        phase5_stage_complete(4);
        if (GXOS_AIDA_I219_PHASE5_STAGE == 4) {
            return phase5_stop(4, NIC_INIT_MAC,
                               "Phase 5 stage 4 intentionally stopped after MAC acquisition");
        }

        phase5_stage_enter(5);
        // Let the PHY negotiate.  This is the existing I219 setup; no
        // speculative PHY writes are introduced by the isolation path.
        ctrl = mmio_read32(mmioBase, E1000_CTRL);
        if (ctrl == 0xFFFFFFFFu) {
            phase5_stage_failed(5);
            set_init_failure(NIC_INIT_PHY, "PHY CTRL read returned all-ones");
            return false;
        }
        ctrl |= E1000_CTRL_SLU | E1000_CTRL_ASDE;
        mmio_write32(mmioBase, E1000_CTRL, ctrl);
        s_device.ctrlValue = mmio_read32(mmioBase, E1000_CTRL);
        s_device.statusValue = mmio_read32(mmioBase, E1000_STATUS);
        s_device.initStage = NIC_INIT_PHY;
        if (s_device.ctrlValue == 0xFFFFFFFFu || s_device.statusValue == 0xFFFFFFFFu) {
            phase5_stage_failed(5);
            set_init_failure(NIC_INIT_PHY, "PHY CTRL/STATUS read returned all-ones");
            return false;
        }
        if (!read_i219_phy_status(mmioBase)) {
            phase5_stage_failed(5);
            set_init_failure(NIC_INIT_PHY, "PHY MDIC timeout or invalid response");
            return false;
        }
        serial::puts("[NIC] I219 PHY discovery: MDIC address=1 status=26 valid\n");
        phase5_stage_complete(5);
        if (GXOS_AIDA_I219_PHASE5_STAGE == 5) {
            return phase5_stop(5, NIC_INIT_PHY,
                               "Phase 5 stage 5 intentionally stopped after PHY/MDIC");
        }

        phase5_stage_enter(6);
    } else if (i219P7) {
        phase7_stage_enter("mac");
        s_device.initStage = NIC_INIT_MAC;
        if (!read_mac_address(mmioBase, s_device.macAddress)) {
            serial::puts("[AIDA-I219-P7] mac-invalid source=RAL0/RAH0\n");
            set_init_failure(NIC_INIT_MAC, "I219 RAL0/RAH0 contains an invalid station address");
            return false;
        }
        phase7_print_mac_marker(s_device.macAddress);
        phase7_stage_complete("mac");
        if (GXOS_AIDA_I219_PHASE7_STAGE == 1) {
            return phase7_stop(NIC_INIT_MAC,
                               "Phase 7 MAC diagnostic stop");
        }

        phase7_stage_enter("phy");
        s_device.initStage = NIC_INIT_PHY;
        if (!read_i219_phy_status(mmioBase)) {
            serial::puts("[AIDA-I219-P7] phy-timeout\n");
            set_init_failure(NIC_INIT_PHY, "I219 PHY MDIC read timed out or returned an invalid response");
            return false;
        }
        serial::puts("[AIDA-I219-P7] phy-id=0x");
        serial::put_hex32(s_device.phyId1);
        serial::puts(":0x");
        serial::put_hex32(s_device.phyId2);
        serial::puts(" address=0x");
        serial::put_hex8(s_device.phyAddress);
        serial::puts(" valid=yes\n");
        serial::puts("[AIDA-I219-P7] phy-status=0x");
        serial::put_hex32(s_device.phyStatusValue);
        serial::puts(" link=");
        serial::puts(s_device.link == NIC_LINK_UP ? "up" : "down");
        serial::puts("\n");
        phase7_stage_complete("phy");
        if (GXOS_AIDA_I219_PHASE7_STAGE == 2) {
            return phase7_stop(NIC_INIT_PHY,
                               "Phase 7 PHY diagnostic stop");
        }
    } else {
        // Preserve the existing E1000/E1000E sequence exactly for QEMU and
        // previously supported discrete Intel devices.
        ctrl = mmio_read32(mmioBase, E1000_CTRL);
        ctrl |= E1000_CTRL_SLU | E1000_CTRL_ASDE | E1000_CTRL_FD;
        mmio_write32(mmioBase, E1000_CTRL, ctrl);
        s_device.ctrlValue = mmio_read32(mmioBase, E1000_CTRL);
        s_device.statusValue = mmio_read32(mmioBase, E1000_STATUS);

        s_device.initStage = NIC_INIT_MAC;
        if (!read_mac_address(mmioBase, s_device.macAddress)) {
            set_init_failure(NIC_INIT_MAC, "invalid MAC in RAL0/RAH0");
            return false;
        }
        serial::puts("[NIC] MAC acquisition: EERD/RAR valid\n");

        s_device.link = (s_device.statusValue & E1000_STATUS_LU)
                        ? NIC_LINK_UP : NIC_LINK_DOWN;
    }

    // Clear the Multicast Table Array (128 dwords).  For I219 this is the
    // first operation in Stage 6, after the reset/MAC/PHY boundary.
    for (uint32_t i = 0; i < 128; ++i) {
        mmio_write32(mmioBase, E1000_MTA + (i * 4), 0);
    }

    // Initialise RX and TX descriptor rings.  The static storage is part of
    // the kernel image and translated through the supplied physical base;
    // no stack memory is ever handed to the device.
    s_device.initStage = NIC_INIT_RX_RING;
    if (!init_rx(mmioBase)) {
        if (i219P7) {
            serial::puts("[AIDA-I219-P7] dma-address-invalid\n");
        } else if (i219) {
            phase5_stage_failed(6);
        }
        set_init_failure(NIC_INIT_RX_RING, "RX ring DMA address unavailable");
        return false;
    }
    if (i219P7) {
        serial::puts("[AIDA-I219-P7] rx-ring=ready\n");
    } else if (!i219) {
        serial::puts("[NIC] RX ring setup: 32 descriptors ready\n");
    }

    s_device.initStage = NIC_INIT_TX_RING;
    if (!init_tx(mmioBase)) {
        if (i219P7) {
            serial::puts("[AIDA-I219-P7] dma-address-invalid\n");
        } else if (i219) {
            phase5_stage_failed(6);
        }
        set_init_failure(NIC_INIT_TX_RING, "TX ring DMA address unavailable");
        return false;
    }
    if (i219P7) {
        serial::puts("[AIDA-I219-P7] tx-ring=ready\n");
    } else if (!i219) {
        serial::puts("[NIC] TX ring setup: 8 descriptors ready\n");
    }

    if (i219P7) {
        // DMA engines are intentionally left configured for the physical
        // stage, but causes remain masked and no IRQ is enabled.
        mask_nic_interrupts(mmioBase);
        if (GXOS_AIDA_I219_PHASE7_STAGE == 3) {
            return phase7_stop(NIC_INIT_TX_RING,
                               "Phase 7 DMA diagnostic stop");
        }
    } else if (i219) {
        serial::puts("[NIC] RX ring setup: 32 descriptors ready\n");
        serial::puts("[NIC] TX ring setup: 8 descriptors ready\n");
        mask_nic_interrupts(mmioBase);
        phase5_stage_complete(6);
        if (GXOS_AIDA_I219_PHASE5_STAGE == 6) {
            return phase5_stop(6, NIC_INIT_TX_RING,
                               "Phase 5 stage 6 intentionally stopped after DMA rings");
        }
    }

    // Keep NIC interrupt causes masked until the kernel has installed the
    // device IRQ handler.  Enabling them here leaves a real device a window
    // in which it can assert an IRQ before the handler is registered.

    return true;
}

// ================================================================
// PCI bus scan for network controllers
// ================================================================

static bool scan_pci_nic()
{
    // Scan only common bus/device ranges to avoid excessive PCI reads
    for (uint16_t bus = 0; bus < 8; ++bus) {
        for (uint8_t dev = 0; dev < 32; ++dev) {
            // Only check function 0 first, then others if multi-function
            uint32_t id = pci_read32(static_cast<uint8_t>(bus), dev, 0, 0);
            if (id == 0xFFFFFFFF || id == 0) continue;

            // Check header type to see if multi-function
            uint32_t headerReg = pci_read32(static_cast<uint8_t>(bus), dev, 0, 0x0C);
            uint8_t headerType = static_cast<uint8_t>(headerReg >> 16);
            uint8_t maxFunc = (headerType & 0x80) ? 8 : 1;

            for (uint8_t func = 0; func < maxFunc; ++func) {
                if (func > 0) {
                    id = pci_read32(static_cast<uint8_t>(bus), dev, func, 0);
                    if (id == 0xFFFFFFFF || id == 0) continue;
                }

                uint32_t classReg = pci_read32(static_cast<uint8_t>(bus), dev, func, 0x08);
                uint8_t baseClass = static_cast<uint8_t>(classReg >> 24);
                uint8_t subClass  = static_cast<uint8_t>(classReg >> 16);

                if (baseClass != PCI_CLASS_NETWORK)
                    continue;

                uint16_t vendor = static_cast<uint16_t>(id & 0xFFFF);
                uint16_t device = static_cast<uint16_t>(id >> 16);
                uint8_t revision = static_cast<uint8_t>(classReg & 0xFF);
                uint16_t subsystemVendor = pci_read16(static_cast<uint8_t>(bus), dev, func, 0x2C);
                uint16_t subsystemDevice = pci_read16(static_cast<uint8_t>(bus), dev, func, 0x2E);

                serial::puts("[NIC] PCI network controller ");
                serial::put_hex8(static_cast<uint8_t>(bus));
                serial::putc(':');
                serial::put_hex8(dev);
                serial::putc('.');
                serial::put_hex8(func);
                serial::puts(" vendor=");
                serial::put_hex32(vendor);
                serial::puts(" device=");
                serial::put_hex32(device);
                serial::puts(" subsystem=");
                serial::put_hex32(subsystemVendor);
                serial::putc(':');
                serial::put_hex32(subsystemDevice);
                serial::puts(" class=");
                serial::put_hex8(baseClass);
                serial::putc('/');
                serial::put_hex8(subClass);
                serial::puts(" progif=");
                serial::put_hex8(static_cast<uint8_t>((classReg >> 8) & 0xFF));
                serial::puts(" rev=");
                serial::put_hex8(revision);
                serial::putc('\n');

                if (subClass != PCI_SUBCLASS_ETH || !is_supported_nic(vendor, device)) {
                    serial::puts("[NIC] Driver: unsupported (identity only; no binding)\n");
                    continue;
                }

                // Found a supported NIC - read the selected register BAR
                uint32_t bar0 = pci_read32(static_cast<uint8_t>(bus), dev, func, 0x10);
                if (bar0 & 0x01) {
                    serial::puts("[NIC] BAR0 is I/O space, skipping\n");
                    continue;
                }

                uint64_t mmioBase = bar0 & 0xFFFFFFF0u;

                // 64-bit BAR: read upper 32 bits
                if ((bar0 & 0x06) == 0x04) {
                    uint32_t bar1 = pci_read32(static_cast<uint8_t>(bus), dev, func, 0x14);
                    mmioBase |= (static_cast<uint64_t>(bar1) << 32);
                }

                serial::puts("[NIC] MMIO base: ");
                serial::put_hex32(static_cast<uint32_t>(mmioBase >> 32));
                serial::put_hex32(static_cast<uint32_t>(mmioBase));
                serial::putc('\n');

                // SAFETY CHECK: MMIO base must be mapped
                // The bootloader doesn't map arbitrary MMIO regions.
                // For now, we'll record the device info but skip hardware init
                // unless the MMIO region is in a known-mapped range.
                // 
                // In QEMU, the E1000 is typically at 0xFEBC0000 or similar,
                // which is NOT mapped by the bootloader's page tables.
                //
                // We need the bootloader to map this region, or we need
                // to implement dynamic page table updates.
                
                // Read interrupt line
                uint32_t intReg = pci_read32(static_cast<uint8_t>(bus), dev, func, 0x3C);
                uint8_t irqLine = static_cast<uint8_t>(intReg & 0xFF);

                serial::puts("[NIC] IRQ line: ");
                serial::put_hex8(irqLine);
                serial::putc('\n');

                // Populate device info (without MMIO access)
                s_device.pciBus   = static_cast<uint8_t>(bus);
                s_device.pciSlot  = dev;
                s_device.pciFunc  = func;
                s_device.vendorId = vendor;
                s_device.deviceId = device;
                s_device.subsystemVendorId = subsystemVendor;
                s_device.subsystemDeviceId = subsystemDevice;
                s_device.revisionId = revision;
                s_device.classCode = baseClass;
                s_device.subclass = subClass;
                s_device.progIf = static_cast<uint8_t>((classReg >> 8) & 0xFF);
                s_device.mmioBase = mmioBase;
                s_device.mmioPhys = mmioBase;
                s_device.mmioSize = 0;
                s_device.irqLine  = irqLine;
                s_device.pciCommand = pci_read16(static_cast<uint8_t>(bus), dev, func, 0x04);
                s_device.driverBound = true;
                s_device.initStage = NIC_INIT_BOUND;
                s_device.phyAccess = NIC_PHY_NOT_ATTEMPTED;
                s_device.link = NIC_LINK_UNKNOWN;

                // Set device name
                s_device.name[0] = 'e'; s_device.name[1] = 't';
                s_device.name[2] = 'h'; s_device.name[3] = '0';
                s_device.name[4] = '\0';

                // The legacy scan has no safe kernel MMIO mapping handoff.
                // Keep the bound identity visible, but do not invent a MAC
                // or claim that the device is ready.
                set_init_failure(NIC_INIT_MMIO, "MMIO not mapped by bootloader");
                s_device.active = false;  // Not active until MMIO is mapped
                
                return true;  // Found a device, even if not initialized
            }
        }
    }
    return false;
}

#endif // ARCH_HAS_PORT_IO

#if !ARCH_HAS_PORT_IO
uint8_t enumerate_network_controllers(NetworkControllerInfo* out,
                                      uint8_t capacity)
{
    (void)out;
    (void)capacity;
    return 0;
}
#endif

// ================================================================
// Initialize from BootInfo (bootloader provides mapped MMIO)
// ================================================================

bool init_from_bootinfo(const NicBootInfo* nicInfo)
{
    if (!nicInfo) {
        serial::puts("[NIC] init_from_bootinfo: null pointer\n");
        return false;
    }
    
    // Check if NIC was found by bootloader
    if (!(nicInfo->flags & NIC_BOOT_FLAG_FOUND)) {
        serial::puts("[NIC] init_from_bootinfo: NIC not found by bootloader\n");
        return false;
    }

    // Preserve the bound identity even if a later hardware stage fails.  The
    // shell can then report the exact frontier instead of collapsing back to
    // "no NIC device structure".
    memzero(&s_device, sizeof(s_device));
    memzero(s_rxDescs, sizeof(s_rxDescs));
    memzero(s_txDescs, sizeof(s_txDescs));
    s_rxCur = 0;
    s_txCur = 0;
    s_initialised = true;

    serial::puts("[NIC] Initializing from BootInfo:\n");
    serial::puts("[NIC]   Vendor: ");
    serial::put_hex32(nicInfo->vendorId);
    serial::puts(" Device: ");
    serial::put_hex32(nicInfo->deviceId);
    serial::putc('\n');
    serial::puts("[NIC]   MMIO Phys: ");
    serial::put_hex32(static_cast<uint32_t>(nicInfo->mmioPhys >> 32));
    serial::put_hex32(static_cast<uint32_t>(nicInfo->mmioPhys));
    serial::putc('\n');
    serial::puts("[NIC]   MMIO Virt: ");
    serial::put_hex32(static_cast<uint32_t>(nicInfo->mmioVirt >> 32));
    serial::put_hex32(static_cast<uint32_t>(nicInfo->mmioVirt));
    serial::putc('\n');
    
    // Populate device info from BootInfo
    s_device.pciBus = nicInfo->bus;
    s_device.pciSlot = nicInfo->device;
    s_device.pciFunc = nicInfo->function;
    s_device.vendorId = nicInfo->vendorId;
    s_device.deviceId = nicInfo->deviceId;
    s_device.subsystemVendorId = nicInfo->subsystemVendorId;
    s_device.subsystemDeviceId = nicInfo->subsystemDeviceId;
    s_device.revisionId = nicInfo->revisionId;
    s_device.classCode = nicInfo->classCode;
    s_device.subclass = nicInfo->subclass;
    s_device.progIf = nicInfo->progIf;
    s_device.mmioBase = nicInfo->mmioVirt;  // Use virtual address for MMIO access
    s_device.mmioPhys = nicInfo->mmioPhys;
    s_device.mmioSize = nicInfo->mmioSize;
    s_device.irqLine = nicInfo->irqLine;
    s_device.mmioMapped = (nicInfo->flags & NIC_BOOT_FLAG_MAPPED) != 0u;
    const bool exactDriverMatch = is_supported_nic(s_device.vendorId, s_device.deviceId) &&
                                  s_device.subclass == PCI_SUBCLASS_ETH;
    const bool i219 = is_i219_device(s_device.deviceId);
    s_device.phase5Stage = i219 ? static_cast<uint8_t>(GXOS_AIDA_I219_PHASE5_STAGE) : 0xFFu;
    s_device.phase6Stage = i219 ? static_cast<uint8_t>(GXOS_AIDA_I219_PHASE6_STAGE) : 0xFFu;
    s_device.phase7Stage = i219 ? static_cast<uint8_t>(GXOS_AIDA_I219_PHASE7_STAGE) : 0xFFu;
    s_device.phase5Stopped = false;
    s_device.interruptsEnabled = false;
    s_device.driverBound = exactDriverMatch;
    s_device.initStage = NIC_INIT_BOUND;
    s_device.phyAccess = NIC_PHY_NOT_ATTEMPTED;
    s_device.link = NIC_LINK_UNKNOWN;

    serial::puts(exactDriverMatch
                 ? (is_i219_device(s_device.deviceId)
                    ? "[NIC] PCI match: accepted; driver=intel-i219-lm (PCH)\n"
                    : "[NIC] PCI match: accepted; driver=intel-e1000 family\n")
                 : "[NIC] PCI match: rejected; no exact Ethernet driver\n");

    if (!exactDriverMatch) {
        set_init_failure(NIC_INIT_BOUND, "PCI identity is outside the exact driver match");
        return false;
    }

    // Set device name
    s_device.name[0] = 'e'; s_device.name[1] = 't';
    s_device.name[2] = 'h'; s_device.name[3] = '0';
    s_device.name[4] = '\0';
    
#if ARCH_HAS_PORT_IO
    // Stage 0 is deliberately handled before any BAR, PCI command, or MMIO
    // work.  The loader has copied only identity fields into BootInfo for this
    // configuration, so stopping here also proves that binding alone is safe.
    if (i219) {
        phase5_stage_enter(0);
        serial::puts("[AIDA-I219-P5] bind=8086:156F driver=intel-i219-lm\n");
        phase5_stage_complete(0);
        if (GXOS_AIDA_I219_PHASE5_STAGE == 0) {
            return phase5_stop(0, NIC_INIT_BOUND,
                               "Phase 5 stage 0 intentionally stopped after bind");
        }
    }

    // The UEFI handoff is the normal bare-metal path.  It must provide a
    // nonzero BAR, a mapped virtual address, and enough bytes for the
    // registers used by this driver before any MMIO access is attempted.
    if (!s_device.mmioMapped || s_device.mmioBase == 0) {
        if (i219) phase5_stage_failed(1);
        set_init_failure(NIC_INIT_MMIO, "MMIO BAR unavailable or not mapped");
        return false;
    }
    if (s_device.mmioSize < E1000_MMIO_MIN_SIZE) {
        if (i219) phase5_stage_failed(1);
        set_init_failure(NIC_INIT_MMIO, "MMIO BAR is smaller than the register window");
        return false;
    }

    if (i219) {
        phase5_stage_enter(1);
        // Read the selected conventional BAR without sizing writes.  A 64-bit
        // BAR consumes the following slot, exactly as the loader does.
        uint64_t discoveredBar = 0;
        uint8_t discoveredBarIndex = 0xFFu;
        for (uint8_t barIndex = 0; barIndex < 6; ++barIndex) {
            const uint8_t barOffset = static_cast<uint8_t>(0x10u + barIndex * 4u);
            const uint32_t barLow = pci_read32(s_device.pciBus, s_device.pciSlot,
                                               s_device.pciFunc, barOffset);
            if ((barLow & 0x1u) != 0u) continue;
            const uint8_t barType = static_cast<uint8_t>((barLow >> 1) & 0x3u);
            if (barType == 1u || barType == 3u) continue;
            const bool is64bit = barType == 2u;
            if (is64bit && barIndex == 5u) continue;
            const uint32_t barHigh = is64bit
                ? pci_read32(s_device.pciBus, s_device.pciSlot,
                             s_device.pciFunc, static_cast<uint8_t>(barOffset + 4u))
                : 0u;
            if (is64bit) ++barIndex;
            discoveredBar = static_cast<uint64_t>(barLow & 0xFFFFFFF0u) |
                            (static_cast<uint64_t>(barHigh) << 32);
            if (discoveredBar == 0 || discoveredBar == 0xFFFFFFFFFFFFFFFFULL) {
                discoveredBar = 0;
                continue;
            }
            discoveredBarIndex = static_cast<uint8_t>(barIndex - (is64bit ? 1u : 0u));
            break;
        }
        if (discoveredBar == 0) {
            phase5_stage_failed(1);
            set_init_failure(NIC_INIT_MMIO, "I219 has no usable memory BAR");
            return false;
        }
        serial::puts("[AIDA-I219-P5] bar=");
        serial::put_hex32(static_cast<uint32_t>(discoveredBar >> 32));
        serial::put_hex32(static_cast<uint32_t>(discoveredBar));
        serial::puts(" mapped=");
        serial::put_hex32(static_cast<uint32_t>(s_device.mmioBase));
        serial::puts(" size=");
        serial::put_hex32(static_cast<uint32_t>(s_device.mmioSize));
        serial::puts(" index=");
        serial::put_hex8(discoveredBarIndex);
        serial::putc('\n');
        if (discoveredBar == 0 || discoveredBar != s_device.mmioPhys) {
            phase5_stage_failed(1);
            set_init_failure(NIC_INIT_MMIO, "I219 BAR handoff does not match PCI BAR");
            return false;
        }

        s_device.initStage = NIC_INIT_PCI;
        uint16_t command = pci_read16(s_device.pciBus, s_device.pciSlot,
                                      s_device.pciFunc, 0x04);
        const uint16_t requiredCommand = static_cast<uint16_t>((1u << 1) | (1u << 2));
        if ((command & requiredCommand) != requiredCommand) {
            uint32_t commandReg = pci_read32(s_device.pciBus, s_device.pciSlot,
                                              s_device.pciFunc, 0x04);
            pci_write32(s_device.pciBus, s_device.pciSlot, s_device.pciFunc, 0x04,
                        (commandReg & 0xFFFF0000u) |
                        static_cast<uint32_t>(command | requiredCommand));
        }
        s_device.pciCommand = pci_read16(s_device.pciBus, s_device.pciSlot,
                                         s_device.pciFunc, 0x04);
        serial::puts("[AIDA-I219-P5] pci-command=");
        serial::put_hex32(s_device.pciCommand);
        serial::putc('\n');
        if ((s_device.pciCommand & requiredCommand) != requiredCommand) {
            phase5_stage_failed(1);
            set_init_failure(NIC_INIT_PCI, "PCI memory-space/bus-master enable failed");
            return false;
        }
        phase5_stage_complete(1);
        if (GXOS_AIDA_I219_PHASE5_STAGE == 1) {
            return phase5_stop(1, NIC_INIT_PCI,
                               "Phase 5 stage 1 intentionally stopped after BAR/PCI");
        }

        phase5_stage_enter(2);
        // STATUS is the least invasive identity/readiness register used by
        // this driver.  Stage 2 performs no reset, PHY access, or write.
        s_device.statusValue = mmio_read32(s_device.mmioBase, E1000_STATUS);
        serial::puts("[AIDA-I219-P5] mmio-status=");
        serial::put_hex32(s_device.statusValue);
        serial::putc('\n');
        if (s_device.statusValue == 0xFFFFFFFFu) {
            phase5_stage_failed(2);
            set_init_failure(NIC_INIT_MMIO, "I219 STATUS read returned all-ones");
            return false;
        }
        phase5_stage_complete(2);
        if (GXOS_AIDA_I219_PHASE5_STAGE == 2) {
            return phase5_stop(2, NIC_INIT_MMIO,
                               "Phase 5 stage 2 intentionally stopped after MMIO probe");
        }
        if (GXOS_AIDA_I219_PHASE6_STAGE != 0) {
            return run_i219_phase6_micro_stage(s_device.mmioBase);
        }
    } else {
        serial::puts("[NIC] MMIO mapping: accepted size=");
        serial::put_hex32(s_device.mmioSize);
        serial::puts(" bytes\n");

        s_device.initStage = NIC_INIT_PCI;
        uint16_t command = pci_read16(s_device.pciBus, s_device.pciSlot,
                                      s_device.pciFunc, 0x04);
        command = static_cast<uint16_t>(command | (1u << 1) | (1u << 2));
        pci_write32(s_device.pciBus, s_device.pciSlot, s_device.pciFunc, 0x04,
                    (pci_read32(s_device.pciBus, s_device.pciSlot, s_device.pciFunc, 0x04) &
                     0xFFFF0000u) | command);
        s_device.pciCommand = pci_read16(s_device.pciBus, s_device.pciSlot,
                                         s_device.pciFunc, 0x04);
        serial::puts("[NIC] PCI command: ");
        serial::put_hex32(s_device.pciCommand);
        serial::puts(" (memory+bus-master enabled)\n");
        if ((s_device.pciCommand & ((1u << 1) | (1u << 2))) != ((1u << 1) | (1u << 2))) {
            set_init_failure(NIC_INIT_PCI, "PCI memory-space/bus-master enable failed");
            return false;
        }
    }

    serial::puts("[NIC] Initializing E1000-family hardware...\n");

    if (!init_e1000(s_device.mmioBase)) {
        return false;
    }

    serial::puts("[NIC] MAC: ");
    for (uint8_t i = 0; i < 6; ++i) {
        if (i > 0) serial::putc(':');
        serial::put_hex8(s_device.macAddress[i]);
    }
    serial::putc('\n');
    
    serial::puts("[NIC] Link: ");
    serial::puts(s_device.link == NIC_LINK_UP ? "UP" :
                 (s_device.link == NIC_LINK_DOWN ? "DOWN" : "UNKNOWN"));
    serial::putc('\n');
    
    if (i219) {
        phase5_stage_enter(7);
        // Stage 6 and this registration boundary are always interrupt-masked.
        mask_nic_interrupts(s_device.mmioBase);
    }
    s_device.active = true;
    s_device.pollingEnabled = true;
    s_device.nicRegistered = true;
    s_device.initStage = NIC_INIT_READY;
    serial::puts("[NIC] NIC registration: complete; interrupt causes remain masked\n");

    if (i219) {
        phase5_stage_complete(7);
    }
    
    serial::puts(is_i219_device(s_device.deviceId)
                 ? "[NIC] I219-LM initialization complete\n"
                 : "[NIC] E1000 initialization complete\n");
    return true;
#else
    set_init_failure(NIC_INIT_PCI, "PCI port-I/O support is unavailable");
    return false;
#endif
}

// ================================================================
// Public API
// ================================================================

void init()
{
    memzero(&s_device, sizeof(s_device));
    memzero(s_rxDescs, sizeof(s_rxDescs));
    memzero(s_txDescs, sizeof(s_txDescs));
    s_initialised = false;
    s_rxCur = 0;
    s_txCur = 0;

#if ARCH_HAS_PORT_IO
    serial::puts("[NIC] Scanning PCI bus for network controllers...\n");

    if (scan_pci_nic()) {
        s_initialised = true;
        serial::puts("[NIC] Found ");
        serial::puts(s_device.name);
        serial::puts("  vendor=");
        serial::put_hex32(s_device.vendorId);
        serial::puts(" device=");
        serial::put_hex32(s_device.deviceId);
        serial::putc('\n');
        
        if (s_device.active) {
            serial::puts("[NIC] MAC=");
            for (int i = 0; i < 6; ++i) {
                if (i > 0) serial::putc(':');
                serial::put_hex8(s_device.macAddress[i]);
            }
            serial::puts("  Link=");
            serial::puts(s_device.link == NIC_LINK_UP ? "UP" : "DOWN");
            serial::putc('\n');
        } else {
            serial::puts("[NIC] Device found but not active (MMIO not mapped)\n");
        }
    } else {
        serial::puts("[NIC] No supported NIC found\n");
    }
#else
    // Architectures without PCI port-I/O: stub - MMIO PCI ECAM
    // enumeration would go here for ia64/sparc64/riscv64.
#endif
}

bool is_active()
{
    return s_initialised && s_device.active;
}

const NICDevice* get_device()
{
    return s_initialised ? &s_device : nullptr;
}

const uint8_t* get_mac_address()
{
    return s_initialised ? s_device.macAddress : nullptr;
}

LinkState get_link_state()
{
    if (!s_initialised) return NIC_LINK_DOWN;

    // This is a status query, not a hardware transaction.  The desktop
    // network widget and shell diagnostics may call it from redraw paths;
    // those paths must never perform MMIO or a potentially long MDIC poll.
    // The cached value is populated during bounded initialisation and updated
    // by the NIC IRQ handler when link-change events are acknowledged.
    return s_device.link;
}

void set_irq_registered(bool registered)
{
    if (!s_initialised) return;

    s_device.irqRegistered = registered;

#if ARCH_HAS_PORT_IO
    if (s_device.active && s_device.mmioMapped) {
        if (registered) {
            if (is_i219_device(s_device.deviceId)) {
                // Stages 0-7 must be physically quiet.  Stage 8 records the
                // handler boundary here but defers IMS until main-loop-ready.
                mask_nic_interrupts(s_device.mmioBase);
                if (s_device.phase5Stage < 8u) {
                    serial::puts("[AIDA-I219-P5] interrupts remain masked\n");
                } else if (s_device.phase7Stage == 4u) {
                    // Registration diagnostics deliberately stop before any
                    // I219 IMS write, even after the shared IRQ handler is
                    // installed.
                    serial::puts("[AIDA-I219-P7] registered=yes interrupts=masked\n");
                } else {
                    phase5_stage_enter(8);
                    serial::puts("[AIDA-I219-P5] stage=8 handler registered; enable deferred\n");
                }
            } else {
                enable_nic_interrupts(s_device.mmioBase);
                s_device.interruptsEnabled = true;
            }
        } else {
            mask_nic_interrupts(s_device.mmioBase);
            s_device.interruptsEnabled = false;
        }
    }
#endif
}

void enable_deferred_interrupts()
{
#if ARCH_HAS_PORT_IO
    if (!s_initialised || !s_device.active || !s_device.mmioMapped ||
        !s_device.irqRegistered || !is_i219_device(s_device.deviceId) ||
        s_device.phase5Stage != 8u || s_device.phase7Stage == 4u ||
        s_device.interruptsEnabled) {
        return;
    }

    enable_nic_interrupts(s_device.mmioBase);
    s_device.interruptsEnabled = true;
    phase5_stage_complete(8);
    serial::puts("[AIDA-I219-P5] NIC interrupt mask enabled after main-loop-ready\n");
#endif
}

// ================================================================
// send_frame - transmit a raw Ethernet frame
// ================================================================

Status send_frame(const uint8_t* data, uint16_t len)
{
    if (!s_initialised || !s_device.active) {
        serial::puts("[NIC] send_frame: not initialized or not active\n");
        return NIC_ERR_NO_DEVICE;
    }
    if (!data || len < ETH_HLEN) {
        serial::puts("[NIC] send_frame: frame too small\n");
        return NIC_ERR_FRAME_TOO_LARGE; // too small
    }
    if (len > ETH_FRAME_MAX) {
        serial::puts("[NIC] send_frame: frame too large\n");
        return NIC_ERR_FRAME_TOO_LARGE;
    }

#if ARCH_HAS_PORT_IO
    s_device.stats.txAttempted++;

    // Check that the current TX descriptor is available
    if (!(s_txDescs[s_txCur].status & E1000_TXD_STAT_DD)) {
        serial::puts("[NIC] send_frame: TX descriptor not available (TX ring full)\n");
        s_device.stats.txDropped++;
        return NIC_ERR_TX_FULL;
    }

    // Copy frame data to TX buffer
    memcopy(s_txBuffer, data, static_cast<uint32_t>(len));

    // Set up the descriptor
    uint64_t bufAddr = 0;
    if (!dma_address(s_txBuffer, &bufAddr)) {
        serial::puts("[NIC] send_frame: TX buffer DMA address unavailable\n");
        s_device.stats.txErrors++;
        return NIC_ERR_INIT_FAIL;
    }
    dma_memory_barrier();
    s_txDescs[s_txCur].bufferAddr = bufAddr;
    s_txDescs[s_txCur].length     = static_cast<uint16_t>(len);
    s_txDescs[s_txCur].cmd        = E1000_TXD_CMD_EOP |
                                    E1000_TXD_CMD_IFCS |
                                    E1000_TXD_CMD_RS;
    s_txDescs[s_txCur].status     = 0;
    dma_memory_barrier();

    // Advance tail pointer to submit the descriptor
    uint16_t oldTx = s_txCur;
    s_txCur = (s_txCur + 1) % NUM_TX_DESC;
    mmio_write32(s_device.mmioBase, E1000_TDT, s_txCur);

    // Wait for transmission to complete (busy-poll descriptor status)
    for (uint32_t i = 0; i < 1000000; ++i) {
        if (s_txDescs[oldTx].status & E1000_TXD_STAT_DD) {
            s_device.stats.txFrames++;
            s_device.stats.txBytes += static_cast<uint32_t>(len);
            return NIC_OK;
        }
    }

    serial::puts("[NIC] TX TIMEOUT! Descriptor status=0x");
    serial::put_hex8(s_txDescs[oldTx].status);
    serial::putc('\n');
    s_device.stats.txErrors++;
    return NIC_ERR_INIT_FAIL;
#else
    (void)data;
    (void)len;
    return NIC_ERR_NO_DEVICE;
#endif
}

// ================================================================
// receive_frame - read the next pending Ethernet frame
// ================================================================

Status receive_frame(uint8_t* buffer, uint16_t max_len, uint16_t* received)
{
    if (!s_initialised || !s_device.active) return NIC_ERR_NO_DEVICE;
    if (received) *received = 0;

#if ARCH_HAS_PORT_IO
    // Check if current RX descriptor has a completed frame
    if (!(s_rxDescs[s_rxCur].status & E1000_RXD_STAT_DD)) {
        return NIC_ERR_RX_EMPTY;
    }

    s_device.stats.rxObserved++;
    dma_memory_barrier();

    // Verify end-of-packet flag
    if (!(s_rxDescs[s_rxCur].status & E1000_RXD_STAT_EOP)) {
        // Multi-descriptor frames not supported; drop and advance
        s_rxDescs[s_rxCur].status = 0;
        uint16_t oldRx = s_rxCur;
        s_rxCur = (s_rxCur + 1) % NUM_RX_DESC;
        mmio_write32(s_device.mmioBase, E1000_RDT, oldRx);
        s_device.stats.rxDropped++;
        s_device.stats.rxMalformed++;
        return NIC_ERR_RX_EMPTY;
    }

    uint16_t frameLen = s_rxDescs[s_rxCur].length;

    if (frameLen > max_len) {
        // Caller's buffer too small; drop frame
        s_rxDescs[s_rxCur].status = 0;
        uint16_t oldRx = s_rxCur;
        s_rxCur = (s_rxCur + 1) % NUM_RX_DESC;
        mmio_write32(s_device.mmioBase, E1000_RDT, oldRx);
        s_device.stats.rxDropped++;
        s_device.stats.rxMalformed++;
        return NIC_ERR_BUFFER_TOO_SMALL;
    }

    // Check for receive errors
    if (s_rxDescs[s_rxCur].errors) {
        s_rxDescs[s_rxCur].status = 0;
        uint16_t oldRx = s_rxCur;
        s_rxCur = (s_rxCur + 1) % NUM_RX_DESC;
        mmio_write32(s_device.mmioBase, E1000_RDT, oldRx);
        s_device.stats.rxErrors++;
        s_device.stats.rxMalformed++;
        return NIC_ERR_RX_EMPTY;
    }

    // Copy frame to caller's buffer
    memcopy(buffer, s_rxBuffers[s_rxCur], frameLen);
    if (received) *received = frameLen;

    // Recycle the descriptor
    s_rxDescs[s_rxCur].status = 0;
    uint16_t oldRx = s_rxCur;
    s_rxCur = (s_rxCur + 1) % NUM_RX_DESC;
    mmio_write32(s_device.mmioBase, E1000_RDT, oldRx);

    s_device.stats.rxFrames++;
    s_device.stats.rxBytes += frameLen;
    return NIC_OK;
#else
    (void)buffer;
    (void)max_len;
    (void)received;
    return NIC_ERR_NO_DEVICE;
#endif
}

// ================================================================
// IRQ handler - called from interrupt dispatch
// ================================================================

void irq_handler()
{
#if ARCH_HAS_PORT_IO
    if (!s_initialised) return;

    // Read interrupt cause (auto-clears on read)
    uint32_t icr = mmio_read32(s_device.mmioBase, E1000_ICR);

    s_device.stats.interrupts++;

    if (icr & E1000_ICR_LSC) {
        // Link status changed - update cached state
        if (is_i219_device(s_device.deviceId)) {
            // Never issue a multi-transaction MDIC poll from interrupt
            // context.  A link-change storm before the input loop is ready
            // would otherwise repeatedly consume the entire bounded wait
            // budget and starve timer/input dispatch.  STATUS is the cached
            // interrupt-time observation; the explicit MDIC path remains in
            // the staged boot-time probe.
            uint32_t status = mmio_read32(s_device.mmioBase, E1000_STATUS);
            s_device.statusValue = status;
            s_device.link = (status == 0xFFFFFFFFu)
                            ? NIC_LINK_UNKNOWN
                            : ((status & E1000_STATUS_LU) ? NIC_LINK_UP : NIC_LINK_DOWN);
        } else {
            uint32_t status = mmio_read32(s_device.mmioBase, E1000_STATUS);
            s_device.statusValue = status;
            s_device.link = (status & E1000_STATUS_LU) ? NIC_LINK_UP : NIC_LINK_DOWN;
        }
        serial::puts("[NIC] Link status changed: ");
        serial::puts(s_device.link == NIC_LINK_UP ? "UP" :
                     (s_device.link == NIC_LINK_DOWN ? "DOWN" : "UNKNOWN"));
        serial::putc('\n');
    }

    // RX interrupt: frames are available - the main loop will call
    // receive_frame() to drain them.  No action needed here beyond
    // acknowledging the interrupt.

    // Send EOI to PIC
#if ARCH_HAS_PIC_8259
    interrupts::eoi(s_device.irqLine);
#endif

#endif // ARCH_HAS_PORT_IO
}

// ================================================================
// Statistics
// ================================================================

const NetStats* get_stats()
{
    return s_initialised ? &s_device.stats : nullptr;
}

} // namespace nic
} // namespace kernel
