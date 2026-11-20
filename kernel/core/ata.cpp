// ATA / SATA Storage Driver — Implementation
//
// Scans PCI for IDE controllers (class 01/01) and AHCI controllers
// (class 01/06), identifies attached drives, and registers them
// with the block device layer.
//
// PIO-mode ATA uses port I/O and is only available on x86/amd64.
// AHCI uses MMIO and works on any architecture with PCI.
// On architectures without PCI the driver is a no-op.
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/kernel/ata.h"
#include "include/kernel/ata_pio_flush.h"
#include "include/kernel/block_device.h"
#include "include/kernel/arch.h"
#if defined(ARCH_X86) || defined(ARCH_AMD64) || defined(__i386__) || defined(__x86_64__)
#include "include/kernel/pit.h"
#endif

// Define ARCH_HAS_PORT_IO for x86/amd64 architectures
#if defined(ARCH_X86) || defined(ARCH_AMD64) || defined(__i386__) || defined(__x86_64__)
    #define ARCH_HAS_PORT_IO 1
#else
    #define ARCH_HAS_PORT_IO 0
#endif

// For debug output
#if defined(__GNUC__) || defined(__clang__)
#include "include/kernel/serial_debug.h"
#endif
#if defined(_MSC_VER)
#include <intrin.h>
#endif

#if defined(ATA_QUALIFICATION_DIAGNOSTICS) && ARCH_HAS_PORT_IO && \
    (defined(__GNUC__) || defined(__clang__))
#define ATA_QUAL_DIAG 1
#else
#define ATA_QUAL_DIAG 0
#endif

namespace kernel {
namespace ata {

// ================================================================
// Internal state
// ================================================================

static ATADevice s_devices[MAX_ATA_DEVICES];
static uint8_t   s_deviceCount = 0;
#if ARCH_HAS_PORT_IO
static volatile uint32_t s_ataChannelLocks[2] = {};

static uint8_t ata_channel_lock_index(const ATADevice& dev)
{
    return dev.ioBase == ATA_SECONDARY_IO ? 1 : 0;
}

// ATA task-file registers are shared by master/slave devices on a channel.
// Keep each complete PIO transaction serialized; IRQ handlers do not issue
// block-device calls, and the current kernel scheduler is cooperative.
class ATAChannelGuard {
public:
    explicit ATAChannelGuard(const ATADevice& dev)
        : m_index(ata_channel_lock_index(dev))
    {
#if defined(__GNUC__) || defined(__clang__)
        while (__sync_lock_test_and_set(&s_ataChannelLocks[m_index], 1u)) {
            asm volatile("pause" ::: "memory");
        }
#elif defined(_MSC_VER)
        while (_InterlockedExchange(
                   reinterpret_cast<volatile long*>(&s_ataChannelLocks[m_index]), 1) != 0) {
            _mm_pause();
        }
#endif
    }

    ~ATAChannelGuard()
    {
#if defined(__GNUC__) || defined(__clang__)
        __sync_lock_release(&s_ataChannelLocks[m_index]);
#elif defined(_MSC_VER)
        _InterlockedExchange(
            reinterpret_cast<volatile long*>(&s_ataChannelLocks[m_index]), 0);
#endif
    }

private:
    uint8_t m_index;
};
#endif

#if ATA_QUAL_DIAG
static uint32_t s_writeCommandSequence = 0;
static uint32_t s_flushCommandSequence = 0;
static uint32_t s_lastWriteIssued[MAX_ATA_DEVICES] = {};
static uint32_t s_lastWriteCompleted[MAX_ATA_DEVICES] = {};
static uint64_t s_lastWriteLba[MAX_ATA_DEVICES] = {};
static bool s_lastWriteValid[MAX_ATA_DEVICES] = {};
static uint32_t s_flushDiagnosticRecordCount = 0;
static bool s_flushDiagnosticCapReported = false;
static const uint32_t ATA_FLUSH_MAX_RECORDS = 128;

static void ata_diag_print_bool(const char* name, bool value)
{
    serial::puts(name);
    serial::putc(value ? '1' : '0');
}

static void ata_diag_print_flush(uint32_t sequence,
                                 const ATADevice& dev,
                                 uint8_t opcode,
                                 block::Status result,
                                 const pio::FlushTrace& trace,
                                 uint8_t devIdx)
{
    if (s_flushDiagnosticRecordCount >= ATA_FLUSH_MAX_RECORDS) {
        if (!s_flushDiagnosticCapReported) {
            serial::puts("[ATA-DIAG] flush trace cap reached; later records suppressed\n");
            s_flushDiagnosticCapReported = true;
        }
        return;
    }
    ++s_flushDiagnosticRecordCount;

    serial::puts("[ATA-DIAG] flush seq=0x");
    serial::put_hex32(sequence);
    serial::puts(" device=");
    serial::puts(dev.name);
    serial::puts(" controller=legacy-ide channel=");
    serial::puts(dev.ioBase == ATA_SECONDARY_IO ? "secondary" : "primary");
    serial::puts(" io=0x");
    serial::put_hex16(dev.ioBase);
    serial::puts(" ctrl=0x");
    serial::put_hex16(dev.ctrlBase);
    serial::puts(" target=");
    serial::puts(dev.isMaster ? "master" : "slave");
    serial::puts(" opcode=0x");
    serial::put_hex8(opcode);
    serial::puts(" device_head_selected=0x");
    serial::put_hex8(trace.deviceHeadSelected);
    serial::puts(" selected_target=");
    serial::putc(trace.selectionMatchesTarget ? '1' : '0');
    serial::puts(" operation=cache-flush completion=status-ready\n");
    serial::puts("[ATA-DIAG] status_before=0x");
    serial::put_hex8(trace.statusBefore);
    serial::puts(" status_after_select=0x");
    serial::put_hex8(trace.statusSelected);
    serial::puts(" status_after_issue=0x");
    serial::put_hex8(trace.statusAfterIssue);
    serial::puts(" status_final=0x");
    serial::put_hex8(trace.statusFinal);
    serial::puts(" error_before=0x");
    serial::put_hex8(trace.errorBefore);
    serial::puts(" error_after=0x");
    serial::put_hex8(trace.errorAfter);
    serial::puts(" final_bits=");
    ata_diag_print_bool("BSY", (trace.statusFinal & ATA_SR_BSY) != 0);
    serial::putc(',');
    ata_diag_print_bool("DRQ", (trace.statusFinal & ATA_SR_DRQ) != 0);
    serial::putc(',');
    ata_diag_print_bool("DRDY", (trace.statusFinal & ATA_SR_DRDY) != 0);
    serial::putc(',');
    ata_diag_print_bool("ERR", (trace.statusFinal & ATA_SR_ERR) != 0);
    serial::putc(',');
    ata_diag_print_bool("DF", (trace.statusFinal & ATA_SR_DF) != 0);
    serial::puts("\n[ATA-DIAG] poll_count=0x");
    serial::put_hex64(trace.polls);
    serial::puts(" deadline_available=");
    serial::putc(trace.deadlineAvailable ? '1' : '0');
    serial::puts(" timeout_source=");
    serial::puts(trace.timeoutSource == pio::FlushTimeoutSource::PitDeadline
                     ? "pit-deadline"
                     : (trace.timeoutSource == pio::FlushTimeoutSource::PollGuard
                            ? "poll-guard" : "none"));
    serial::puts(" start_pit_ticks=0x");
    serial::put_hex64(trace.startTicks);
    serial::puts(" end_pit_ticks=0x");
    serial::put_hex64(trace.endTicks);
    serial::puts(" elapsed_pit_ticks=0x");
    serial::put_hex64(trace.endTicks - trace.startTicks);
    serial::puts(" timeout_ticks=0x");
    serial::put_hex64(6000);
    serial::puts(" retry_count=0 command_issued=");
    serial::putc(trace.commandIssued ? '1' : '0');
    serial::puts(" observed_bsy=");
    serial::putc(trace.observedBusy ? '1' : '0');
    serial::puts(" result=");
    serial::puts(result == block::BLOCK_OK ? "BLOCK_OK"
                 : (result == block::BLOCK_ERR_TIMEOUT ? "BLOCK_ERR_TIMEOUT"
                    : (result == block::BLOCK_ERR_NOT_READY ? "BLOCK_ERR_NOT_READY"
                       : (result == block::BLOCK_ERR_UNSUPPORTED ? "BLOCK_ERR_UNSUPPORTED"
                          : "BLOCK_ERR_IO"))));
    serial::puts("\n[ATA-DIAG] last_write_valid=");
    serial::putc(s_lastWriteValid[devIdx] ? '1' : '0');
    serial::puts(" issued_seq=0x");
    serial::put_hex32(s_lastWriteIssued[devIdx]);
    serial::puts(" completed_seq=0x");
    serial::put_hex32(s_lastWriteCompleted[devIdx]);
    serial::puts(" lba=0x");
    serial::put_hex64(s_lastWriteLba[devIdx]);
    serial::puts(" writes_complete=");
    serial::putc(s_lastWriteValid[devIdx] &&
                 s_lastWriteIssued[devIdx] == s_lastWriteCompleted[devIdx] ? '1' : '0');
    serial::puts(" recovery=none\n");

    for (uint32_t i = 0; i < trace.sampleCount; ++i) {
        serial::puts("[ATA-DIAG] flush_sample seq=0x");
        serial::put_hex32(sequence);
        serial::puts(" poll=0x");
        serial::put_hex64(trace.samples[i].poll);
        serial::puts(" status=0x");
        serial::put_hex8(trace.samples[i].status);
        serial::putc('\n');
    }
}
#endif

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

static void delay_400ns(uint16_t ctrlBase)
{
    (void)ctrlBase;
#if ARCH_HAS_PORT_IO
    // Reading the alternate status register 4 times provides ~400 ns delay
    arch::inb(ctrlBase);
    arch::inb(ctrlBase);
    arch::inb(ctrlBase);
    arch::inb(ctrlBase);
#else
    for (volatile int i = 0; i < 100; ++i) {}
#endif
}

// Swap byte pairs in ATA identify strings (stored as big-endian words)
static void fix_ata_string(char* str, uint32_t len)
{
    for (uint32_t i = 0; i < len; i += 2) {
        char tmp = str[i];
        str[i] = str[i + 1];
        str[i + 1] = tmp;
    }
    // Trim trailing spaces
    int end = static_cast<int>(len) - 1;
    while (end >= 0 && str[end] == ' ') str[end--] = '\0';
    str[len] = '\0';
}

// ================================================================
// PCI configuration (port I/O method — x86/amd64 only)
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

// ================================================================
// ATA PIO — wait for BSY clear, DRQ set, or error
// ================================================================

static bool ata_wait_bsy(uint16_t ioBase, uint32_t timeout)
{
    for (uint32_t i = 0; i < timeout; ++i) {
        uint8_t st = arch::inb(static_cast<uint16_t>(ioBase + ATA_REG_STATUS));
        if (!(st & ATA_SR_BSY)) return true;
    }
    return false;
}

static bool ata_interrupts_enabled()
{
#if defined(__GNUC__) || defined(__clang__)
    uintptr_t flags = 0;
#if defined(__x86_64__)
    asm volatile("pushfq; popq %0" : "=r"(flags));
#elif defined(__i386__)
    asm volatile("pushfl; popl %0" : "=r"(flags));
#else
    return false;
#endif
    return (flags & (static_cast<uintptr_t>(1) << 9)) != 0;
#elif defined(_MSC_VER) && defined(_M_X64)
    return (__readeflags() & (static_cast<unsigned __int64>(1) << 9)) != 0;
#else
    return false;
#endif
}

static block::Status ata_status_error(uint16_t ioBase, uint8_t status)
{
    if (pio::status_is_absent(status)) return block::BLOCK_ERR_NOT_READY;
    if (!(status & (ATA_SR_ERR | ATA_SR_DF))) return block::BLOCK_OK;

    const uint8_t error = arch::inb(static_cast<uint16_t>(ioBase + ATA_REG_ERROR));
#if defined(__GNUC__) || defined(__clang__)
    serial::puts("[ATA] device command error status=0x");
    serial::put_hex8(status);
    serial::puts(" error=0x");
    serial::put_hex8(error);
    serial::putc('\n');
#endif
    return block::BLOCK_ERR_IO;
}

static block::Status ata_wait_command_idle(uint16_t ioBase, uint32_t timeout)
{
    for (uint32_t i = 0; i < timeout; ++i) {
        uint8_t st = arch::inb(static_cast<uint16_t>(ioBase + ATA_REG_STATUS));
        if (pio::status_is_absent(st)) return block::BLOCK_ERR_NOT_READY;
        if (st & ATA_SR_BSY) continue;
        block::Status error = ata_status_error(ioBase, st);
        if (error != block::BLOCK_OK) return error;
        if (!(st & (ATA_SR_BSY | ATA_SR_DRQ))) return block::BLOCK_OK;
    }
    return block::BLOCK_ERR_TIMEOUT;
}

static block::Status ata_wait_drq(uint16_t ioBase, uint32_t timeout)
{
    for (uint32_t i = 0; i < timeout; ++i) {
        uint8_t st = arch::inb(static_cast<uint16_t>(ioBase + ATA_REG_STATUS));
        if (pio::status_is_absent(st)) return block::BLOCK_ERR_NOT_READY;
        if (st & ATA_SR_BSY) continue;
        block::Status error = ata_status_error(ioBase, st);
        if (error != block::BLOCK_OK) return error;
        if (st & ATA_SR_DRQ) return block::BLOCK_OK;
    }
    return block::BLOCK_ERR_TIMEOUT;
}

// ================================================================
// ATA PIO — IDENTIFY DEVICE
// ================================================================

static bool ata_identify(uint16_t ioBase, uint16_t ctrlBase,
                         bool master, IdentifyData* id)
{
    uint8_t drv = master ? 0xA0 : 0xB0;
    arch::outb(static_cast<uint16_t>(ioBase + ATA_REG_DRIVE_HEAD), drv);
    delay_400ns(ctrlBase);

    // Zero out the sector count / LBA registers
    arch::outb(static_cast<uint16_t>(ioBase + ATA_REG_SECCOUNT), 0);
    arch::outb(static_cast<uint16_t>(ioBase + ATA_REG_LBA_LO), 0);
    arch::outb(static_cast<uint16_t>(ioBase + ATA_REG_LBA_MID), 0);
    arch::outb(static_cast<uint16_t>(ioBase + ATA_REG_LBA_HI), 0);

    arch::outb(static_cast<uint16_t>(ioBase + ATA_REG_COMMAND), ATA_CMD_IDENTIFY);
    delay_400ns(ctrlBase);

    uint8_t st = arch::inb(static_cast<uint16_t>(ioBase + ATA_REG_STATUS));
    if (st == 0) return false; // no device

    if (!ata_wait_bsy(ioBase, 100000)) return false;

    // Check for ATAPI — LBA_MID/HI become non-zero
    if (arch::inb(static_cast<uint16_t>(ioBase + ATA_REG_LBA_MID)) != 0 ||
        arch::inb(static_cast<uint16_t>(ioBase + ATA_REG_LBA_HI))  != 0) {
        return false; // ATAPI or SATA — skip for PIO driver
    }

    if (ata_wait_drq(ioBase, 100000) != block::BLOCK_OK) return false;

    // Read 256 words (512 bytes)
    uint16_t* buf = reinterpret_cast<uint16_t*>(id);
    for (int i = 0; i < 256; ++i) {
        buf[i] = arch::inw(static_cast<uint16_t>(ioBase + ATA_REG_DATA));
    }
    return true;
}

// ================================================================
// ATA PIO — read sectors (28-bit LBA)
// ================================================================

static block::Status ata_pio_read(uint8_t devIdx,
                                  uint64_t lba,
                                  uint32_t count,
                                  void* buffer)
{
    if (devIdx >= MAX_ATA_DEVICES || !s_devices[devIdx].active)
        return block::BLOCK_ERR_INVALID;

    ATADevice& dev = s_devices[devIdx];
    ATAChannelGuard channelGuard(dev);
    uint8_t* buf = static_cast<uint8_t*>(buffer);

    for (uint32_t sec = 0; sec < count; ++sec) {
        uint64_t curLBA = lba + sec;

        if (dev.lba48 && curLBA >= 0x10000000ULL) {
            // 48-bit LBA
            uint8_t drv = dev.isMaster ? 0x40 : 0x50;
            arch::outb(static_cast<uint16_t>(dev.ioBase + ATA_REG_DRIVE_HEAD), drv);
            delay_400ns(dev.ctrlBase);

            // High bytes first
            arch::outb(static_cast<uint16_t>(dev.ioBase + ATA_REG_SECCOUNT),
                       0); // sector count high = 0
            arch::outb(static_cast<uint16_t>(dev.ioBase + ATA_REG_LBA_LO),
                       static_cast<uint8_t>(curLBA >> 24));
            arch::outb(static_cast<uint16_t>(dev.ioBase + ATA_REG_LBA_MID),
                       static_cast<uint8_t>(curLBA >> 32));
            arch::outb(static_cast<uint16_t>(dev.ioBase + ATA_REG_LBA_HI),
                       static_cast<uint8_t>(curLBA >> 40));

            // Low bytes
            arch::outb(static_cast<uint16_t>(dev.ioBase + ATA_REG_SECCOUNT), 1);
            arch::outb(static_cast<uint16_t>(dev.ioBase + ATA_REG_LBA_LO),
                       static_cast<uint8_t>(curLBA));
            arch::outb(static_cast<uint16_t>(dev.ioBase + ATA_REG_LBA_MID),
                       static_cast<uint8_t>(curLBA >> 8));
            arch::outb(static_cast<uint16_t>(dev.ioBase + ATA_REG_LBA_HI),
                       static_cast<uint8_t>(curLBA >> 16));

            arch::outb(static_cast<uint16_t>(dev.ioBase + ATA_REG_COMMAND),
                       ATA_CMD_READ_PIO_EXT);
        } else {
            // 28-bit LBA
            uint8_t drv = (dev.isMaster ? 0xE0 : 0xF0) |
                          static_cast<uint8_t>((curLBA >> 24) & 0x0F);
            arch::outb(static_cast<uint16_t>(dev.ioBase + ATA_REG_DRIVE_HEAD), drv);
            delay_400ns(dev.ctrlBase);

            arch::outb(static_cast<uint16_t>(dev.ioBase + ATA_REG_SECCOUNT), 1);
            arch::outb(static_cast<uint16_t>(dev.ioBase + ATA_REG_LBA_LO),
                       static_cast<uint8_t>(curLBA));
            arch::outb(static_cast<uint16_t>(dev.ioBase + ATA_REG_LBA_MID),
                       static_cast<uint8_t>(curLBA >> 8));
            arch::outb(static_cast<uint16_t>(dev.ioBase + ATA_REG_LBA_HI),
                       static_cast<uint8_t>(curLBA >> 16));

            arch::outb(static_cast<uint16_t>(dev.ioBase + ATA_REG_COMMAND),
                       ATA_CMD_READ_PIO);
        }

        const block::Status waitStatus = ata_wait_drq(dev.ioBase, 500000);
        if (waitStatus != block::BLOCK_OK) return waitStatus;

        uint16_t* wbuf = reinterpret_cast<uint16_t*>(buf + sec * dev.sectorSize);
        for (uint32_t w = 0; w < dev.sectorSize / 2; ++w) {
            wbuf[w] = arch::inw(static_cast<uint16_t>(dev.ioBase + ATA_REG_DATA));
        }
    }
    return block::BLOCK_OK;
}

// ================================================================
// ATA PIO — write sectors (28-bit LBA)
// ================================================================

static block::Status ata_pio_write(uint8_t devIdx,
                                   uint64_t lba,
                                   uint32_t count,
                                   const void* buffer)
{
    if (devIdx >= MAX_ATA_DEVICES || !s_devices[devIdx].active)
        return block::BLOCK_ERR_INVALID;

    ATADevice& dev = s_devices[devIdx];
    ATAChannelGuard channelGuard(dev);
    const uint8_t* buf = static_cast<const uint8_t*>(buffer);

    for (uint32_t sec = 0; sec < count; ++sec) {
        uint64_t curLBA = lba + sec;

#if ATA_QUAL_DIAG
        const uint32_t writeSequence = ++s_writeCommandSequence;
        s_lastWriteIssued[devIdx] = writeSequence;
        s_lastWriteLba[devIdx] = curLBA;
        s_lastWriteValid[devIdx] = true;
#endif

        if (dev.lba48 && curLBA >= 0x10000000ULL) {
            uint8_t drv = dev.isMaster ? 0x40 : 0x50;
            arch::outb(static_cast<uint16_t>(dev.ioBase + ATA_REG_DRIVE_HEAD), drv);
            delay_400ns(dev.ctrlBase);

            arch::outb(static_cast<uint16_t>(dev.ioBase + ATA_REG_SECCOUNT), 0);
            arch::outb(static_cast<uint16_t>(dev.ioBase + ATA_REG_LBA_LO),
                       static_cast<uint8_t>(curLBA >> 24));
            arch::outb(static_cast<uint16_t>(dev.ioBase + ATA_REG_LBA_MID),
                       static_cast<uint8_t>(curLBA >> 32));
            arch::outb(static_cast<uint16_t>(dev.ioBase + ATA_REG_LBA_HI),
                       static_cast<uint8_t>(curLBA >> 40));

            arch::outb(static_cast<uint16_t>(dev.ioBase + ATA_REG_SECCOUNT), 1);
            arch::outb(static_cast<uint16_t>(dev.ioBase + ATA_REG_LBA_LO),
                       static_cast<uint8_t>(curLBA));
            arch::outb(static_cast<uint16_t>(dev.ioBase + ATA_REG_LBA_MID),
                       static_cast<uint8_t>(curLBA >> 8));
            arch::outb(static_cast<uint16_t>(dev.ioBase + ATA_REG_LBA_HI),
                       static_cast<uint8_t>(curLBA >> 16));

            arch::outb(static_cast<uint16_t>(dev.ioBase + ATA_REG_COMMAND),
                       ATA_CMD_WRITE_PIO_EXT);
        } else {
            uint8_t drv = (dev.isMaster ? 0xE0 : 0xF0) |
                          static_cast<uint8_t>((curLBA >> 24) & 0x0F);
            arch::outb(static_cast<uint16_t>(dev.ioBase + ATA_REG_DRIVE_HEAD), drv);
            delay_400ns(dev.ctrlBase);

            arch::outb(static_cast<uint16_t>(dev.ioBase + ATA_REG_SECCOUNT), 1);
            arch::outb(static_cast<uint16_t>(dev.ioBase + ATA_REG_LBA_LO),
                       static_cast<uint8_t>(curLBA));
            arch::outb(static_cast<uint16_t>(dev.ioBase + ATA_REG_LBA_MID),
                       static_cast<uint8_t>(curLBA >> 8));
            arch::outb(static_cast<uint16_t>(dev.ioBase + ATA_REG_LBA_HI),
                       static_cast<uint8_t>(curLBA >> 16));

            arch::outb(static_cast<uint16_t>(dev.ioBase + ATA_REG_COMMAND),
                       ATA_CMD_WRITE_PIO);
        }

        block::Status waitStatus = ata_wait_drq(dev.ioBase, 500000);
        if (waitStatus != block::BLOCK_OK) return waitStatus;

        const uint16_t* wbuf = reinterpret_cast<const uint16_t*>(
            buf + sec * dev.sectorSize);
        for (uint32_t w = 0; w < dev.sectorSize / 2; ++w) {
            arch::outw(static_cast<uint16_t>(dev.ioBase + ATA_REG_DATA), wbuf[w]);
        }
        // Complete one PIO write command before programming the next sector.
        waitStatus = ata_wait_command_idle(dev.ioBase, 500000);
        if (waitStatus != block::BLOCK_OK) return waitStatus;

#if ATA_QUAL_DIAG
        s_lastWriteCompleted[devIdx] = writeSequence;
#endif

    }

    return block::BLOCK_OK;
}

// Cache flush is deliberately separate from a block write. FAT commits call
// this after data and metadata are complete, instead of paying for a device
// cache flush after every cluster and FAT-entry update.
#if ARCH_HAS_PORT_IO
static const uint64_t ATA_FLUSH_TIMEOUT_TICKS = 6000; // 60 s at the kernel's 100 Hz PIT rate
static const uint64_t ATA_FLUSH_FALLBACK_POLL_LIMIT = 500000;
static const uint64_t ATA_FLUSH_EMERGENCY_POLL_GUARD = 5000000;

class ATAFlushPortOps {
public:
    uint8_t read_status(uint16_t ioBase)
    {
        return arch::inb(static_cast<uint16_t>(ioBase + ATA_REG_STATUS));
    }

    uint8_t read_error(uint16_t ioBase)
    {
        return arch::inb(static_cast<uint16_t>(ioBase + ATA_REG_ERROR));
    }

    void select_device(uint16_t ioBase, uint8_t value)
    {
        arch::outb(static_cast<uint16_t>(ioBase + ATA_REG_DRIVE_HEAD), value);
    }

    uint8_t read_device_head(uint16_t ioBase)
    {
        return arch::inb(static_cast<uint16_t>(ioBase + ATA_REG_DRIVE_HEAD));
    }

    void delay_400ns(uint16_t ctrlBase)
    {
        kernel::ata::delay_400ns(ctrlBase);
    }

    void issue_command(uint16_t ioBase, uint8_t opcode)
    {
        arch::outb(static_cast<uint16_t>(ioBase + ATA_REG_COMMAND), opcode);
    }

    uint64_t ticks() const
    {
        return pit::ticks();
    }

    bool deadline_available() const
    {
        return pit::heartbeat_seen() && ata_interrupts_enabled();
    }
};
#endif

static block::Status ata_pio_flush(uint8_t devIdx)
{
    if (devIdx >= MAX_ATA_DEVICES || !s_devices[devIdx].active)
        return block::BLOCK_ERR_INVALID;

    ATADevice& dev = s_devices[devIdx];
    ATAChannelGuard channelGuard(dev);
#if ARCH_HAS_PORT_IO
    pio::FlushTrace trace;
    uint8_t opcode = 0;
    block::Status result = block::BLOCK_ERR_UNSUPPORTED;
    if (pio::select_cache_flush_opcode(dev, &opcode)) {
        ATAFlushPortOps ops;
        result = pio::execute_cache_flush(
            dev, opcode, ops, ATA_FLUSH_TIMEOUT_TICKS,
            ATA_FLUSH_FALLBACK_POLL_LIMIT, ATA_FLUSH_EMERGENCY_POLL_GUARD, &trace);
    }
#if ATA_QUAL_DIAG
    const uint32_t flushSequence = ++s_flushCommandSequence;
    ata_diag_print_flush(flushSequence, dev, opcode, result, trace, devIdx);
#else
    if (result != block::BLOCK_OK) {
#if defined(__GNUC__) || defined(__clang__)
        serial::puts("[ATA] cache flush failed device=");
        serial::puts(dev.name);
        serial::puts(" status=0x");
        serial::put_hex8(trace.statusFinal);
        serial::puts(" error_before=0x");
        serial::put_hex8(trace.errorBefore);
        serial::puts(" error_after=0x");
        serial::put_hex8(trace.errorAfter);
        serial::puts(" polls=0x");
        serial::put_hex64(trace.polls);
        serial::puts(result == block::BLOCK_ERR_TIMEOUT ? " result=BLOCK_ERR_TIMEOUT\n"
                     : (result == block::BLOCK_ERR_NOT_READY ? " result=BLOCK_ERR_NOT_READY\n"
                        : (result == block::BLOCK_ERR_UNSUPPORTED
                               ? " result=BLOCK_ERR_UNSUPPORTED\n"
                               : " result=BLOCK_ERR_IO\n")));
#endif
    }
#endif
    return result;
#else
    (void)dev;
    return block::BLOCK_ERR_UNSUPPORTED;
#endif
}

// ================================================================
// Probe a single ATA channel (master + slave)
// ================================================================

static void probe_channel(uint16_t ioBase, uint16_t ctrlBase,
                          const char* prefix, uint8_t chanIdx)
{
#if defined(__GNUC__) || defined(__clang__)
    serial::puts("[ATA] Probing channel ");
    serial::puts(prefix);
    serial::puts(" (ioBase=0x");
    serial::put_hex16(ioBase);
    serial::puts(")\n");
#endif

    for (uint8_t drive = 0; drive < 2; ++drive) {
        if (s_deviceCount >= MAX_ATA_DEVICES) return;

        IdentifyData id;
        memzero(&id, sizeof(id));

        bool isMaster = (drive == 0);
        
#if defined(__GNUC__) || defined(__clang__)
        serial::puts("[ATA]   Checking ");
        serial::puts(isMaster ? "master" : "slave");
        serial::puts("...\n");
#endif

        if (!ata_identify(ioBase, ctrlBase, isMaster, &id)) {
#if defined(__GNUC__) || defined(__clang__)
            serial::puts("[ATA]   No device\n");
#endif
            continue;
        }

#if defined(__GNUC__) || defined(__clang__)
        serial::puts("[ATA]   Found device!\n");
#endif

        ATADevice& dev = s_devices[s_deviceCount];
        memzero(&dev, sizeof(dev));
        dev.active     = true;
        dev.ioBase     = ioBase;
        dev.ctrlBase   = ctrlBase;
        dev.isMaster   = isMaster ? 1 : 0;
        dev.isAHCI     = false;
        dev.sectorSize = 512;

        // Command-set bits are reported in IDENTIFY DEVICE word 83.
        dev.lba48 = (id.commandSets83 & (1 << 10)) != 0;
        dev.flushCache = (id.commandSets83 & (1 << 12)) != 0;
        dev.flushCacheExt = (id.commandSets83 & (1 << 13)) != 0;

#if ATA_QUAL_DIAG
        serial::puts("[ATA-DIAG] identify device=");
        serial::puts(dev.name);
        serial::puts(" identify_word83=0x");
        serial::put_hex16(id.commandSets83);
        serial::puts(" lba48=");
        serial::putc(dev.lba48 ? '1' : '0');
        serial::puts(" flush_cache_supported=");
        serial::putc((id.commandSets83 & (1 << 12)) ? '1' : '0');
        serial::puts(" flush_cache_ext_supported=");
        serial::putc((id.commandSets83 & (1 << 13)) ? '1' : '0');
        serial::puts(" flush_opcode=0x");
        uint8_t flushOpcode = 0;
        pio::select_cache_flush_opcode(dev, &flushOpcode);
        serial::put_hex8(flushOpcode);
        serial::putc('\n');
#endif

        if (dev.lba48) {
            dev.totalSectors = id.lba48Sectors;
        } else {
            dev.totalSectors = id.lba28Sectors;
        }

        // Copy and fix model / serial strings
        memcopy(dev.model, id.model, 40);
        fix_ata_string(dev.model, 40);
        memcopy(dev.serial, id.serial, 20);
        fix_ata_string(dev.serial, 20);

        // Build device name: "ata0m", "ata0s", "ata1m", "ata1s"
        dev.name[0] = 'a'; dev.name[1] = 't'; dev.name[2] = 'a';
        dev.name[3] = static_cast<char>('0' + chanIdx);
        dev.name[4] = isMaster ? 'm' : 's';
        dev.name[5] = '\0';

        // Register with block layer
        block::BlockDevice bdev;
        memzero(&bdev, sizeof(bdev));
        bdev.active       = true;
        bdev.type         = block::BDEV_ATA_PIO;
        bdev.driverIndex  = s_deviceCount;
        bdev.totalSectors = dev.totalSectors;
        bdev.sectorSize   = dev.sectorSize;
        bdev.readFn       = ata_pio_read;
        bdev.writeFn      = ata_pio_write;
        bdev.flushFn      = ata_pio_flush;
        memcopy(bdev.name, dev.name, 6);
        memcopy(bdev.stableId, dev.serial, sizeof(dev.serial));

        block::register_device(bdev);
        ++s_deviceCount;
    }

    (void)prefix; // used for debug output in future
}

// ================================================================
// PCI scan for IDE controllers (class 01 / subclass 01)
// ================================================================

static bool scan_pci_ide()
{
    bool found = false;
    for (uint16_t bus = 0; bus < 256; ++bus) {
        for (uint8_t dev = 0; dev < 32; ++dev) {
            for (uint8_t func = 0; func < 8; ++func) {
                uint32_t id = pci_read32(static_cast<uint8_t>(bus), dev, func, 0);
                if (id == 0xFFFFFFFF) continue;

                uint32_t classReg = pci_read32(static_cast<uint8_t>(bus), dev, func, 0x08);
                uint8_t baseClass = static_cast<uint8_t>(classReg >> 24);
                uint8_t subClass  = static_cast<uint8_t>(classReg >> 16);

                if (baseClass == 0x01 && subClass == 0x01) {
                    // IDE controller found — use standard channel ports
                    probe_channel(ATA_PRIMARY_IO, ATA_PRIMARY_CTRL, "pri", 0);
                    probe_channel(ATA_SECONDARY_IO, ATA_SECONDARY_CTRL, "sec", 1);
                    found = true;
                    return found; // one IDE controller is enough for now
                }
            }
        }
    }
    return found;
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
    s_ataChannelLocks[0] = 0;
    s_ataChannelLocks[1] = 0;
#endif
#if ATA_QUAL_DIAG
    s_writeCommandSequence = 0;
    s_flushCommandSequence = 0;
    memzero(s_lastWriteIssued, sizeof(s_lastWriteIssued));
    memzero(s_lastWriteCompleted, sizeof(s_lastWriteCompleted));
    memzero(s_lastWriteLba, sizeof(s_lastWriteLba));
    memzero(s_lastWriteValid, sizeof(s_lastWriteValid));
    s_flushDiagnosticRecordCount = 0;
    s_flushDiagnosticCapReported = false;
#endif

#if defined(__GNUC__) || defined(__clang__)
    serial::puts("[ATA] Initializing ATA driver...\n");
#endif

#if ARCH_HAS_PORT_IO
    // x86 / amd64: scan PCI for IDE controllers, probe ATA PIO
    bool foundIDE = scan_pci_ide();
    
#if defined(__GNUC__) || defined(__clang__)
    serial::puts("[ATA] PCI scan ");
    serial::puts(foundIDE ? "found" : "did not find");
    serial::puts(" IDE controller\n");
#endif
    
    // If no IDE controller found via PCI, try probing standard ports anyway
    // This handles cases like QEMU's ISA IDE or legacy systems
    if (!foundIDE) {
#if defined(__GNUC__) || defined(__clang__)
        serial::puts("[ATA] Trying standard IDE ports...\n");
#endif
        // Try standard IDE ports directly
        probe_channel(ATA_PRIMARY_IO, ATA_PRIMARY_CTRL, "pri", 0);
        probe_channel(ATA_SECONDARY_IO, ATA_SECONDARY_CTRL, "sec", 1);
    }
#endif

#if defined(__GNUC__) || defined(__clang__)
    serial::puts("[ATA] Init complete. Devices: ");
    serial::putc('0' + s_deviceCount);
    serial::puts("\n");
#endif

    // AHCI support (MMIO-based, all architectures with PCI) would
    // be added here in a future iteration: scan PCI for class 01/06,
    // map ABAR, enumerate ports, send IDENTIFY via FIS.
}

uint8_t device_count()
{
    return s_deviceCount;
}

const ATADevice* get_device(uint8_t index)
{
    if (index >= MAX_ATA_DEVICES) return nullptr;
    if (!s_devices[index].active) return nullptr;
    return &s_devices[index];
}

} // namespace ata
} // namespace kernel
