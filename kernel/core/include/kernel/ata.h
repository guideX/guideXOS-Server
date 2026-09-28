// ATA / SATA Storage Driver
//
// Supports:
//   - Legacy ATA PIO mode (port I/O, x86/amd64 only)
//   - AHCI / SATA (MMIO, available on x86/amd64/ia64/sparc64)
//   - IDENTIFY DEVICE, READ SECTORS, WRITE SECTORS
//   - 28-bit and 48-bit LBA addressing
//
// On architectures without PCI or port I/O the driver compiles
// but init() returns immediately (no devices registered).
//
// Reference: ATA/ATAPI-8 (ACS-3), Serial ATA AHCI 1.3.1
//
// Copyright (c) 2026 guideXOS Server
//

#ifndef KERNEL_ATA_H
#define KERNEL_ATA_H

#include "kernel/types.h"
#include "kernel/block_device.h"

namespace kernel {
namespace ata {

// ================================================================
// ATA register offsets (primary / secondary channel)
// ================================================================

static const uint16_t ATA_PRIMARY_IO   = 0x1F0;
static const uint16_t ATA_PRIMARY_CTRL = 0x3F6;
static const uint16_t ATA_SECONDARY_IO = 0x170;
static const uint16_t ATA_SECONDARY_CTRL = 0x376;

// Offsets from I/O base
static const uint16_t ATA_REG_DATA       = 0x00;
static const uint16_t ATA_REG_ERROR      = 0x01;
static const uint16_t ATA_REG_FEATURES   = 0x01;
static const uint16_t ATA_REG_SECCOUNT   = 0x02;
static const uint16_t ATA_REG_LBA_LO     = 0x03;
static const uint16_t ATA_REG_LBA_MID    = 0x04;
static const uint16_t ATA_REG_LBA_HI     = 0x05;
static const uint16_t ATA_REG_DRIVE_HEAD = 0x06;
static const uint16_t ATA_REG_STATUS     = 0x07;
static const uint16_t ATA_REG_COMMAND    = 0x07;

// ATA status bits
static const uint8_t ATA_SR_BSY  = 0x80;
static const uint8_t ATA_SR_DRDY = 0x40;
static const uint8_t ATA_SR_DF   = 0x20;
static const uint8_t ATA_SR_DSC  = 0x10;
static const uint8_t ATA_SR_DRQ  = 0x08;
static const uint8_t ATA_SR_CORR = 0x04;
static const uint8_t ATA_SR_IDX  = 0x02;
static const uint8_t ATA_SR_ERR  = 0x01;

// ATA commands
static const uint8_t ATA_CMD_IDENTIFY      = 0xEC;
static const uint8_t ATA_CMD_READ_PIO      = 0x20;  // READ SECTORS (28-bit)
static const uint8_t ATA_CMD_READ_PIO_EXT  = 0x24;  // READ SECTORS EXT (48-bit)
static const uint8_t ATA_CMD_WRITE_PIO     = 0x30;  // WRITE SECTORS (28-bit)
static const uint8_t ATA_CMD_WRITE_PIO_EXT = 0x34;  // WRITE SECTORS EXT (48-bit)
static const uint8_t ATA_CMD_CACHE_FLUSH   = 0xE7;
static const uint8_t ATA_CMD_CACHE_FLUSH_EXT = 0xEA;
// Five seconds per command phase tolerates QEMU/host scheduling jitter while
// still bounding a failed or disconnected device.
// PIT runs at 100 Hz. Allow ordinary PIO phases up to 30 seconds; cache flush
// can take longer because the device may need to commit its volatile cache.
static const uint32_t ATA_COMMAND_TIMEOUT_TICKS = 3000u;
static const uint32_t ATA_FLUSH_TIMEOUT_TICKS = 6000u;

struct AtaFlushSupport {
    bool flushCache;
    bool flushCacheExt;
};

enum AtaOperationKind : uint8_t {
    ATA_OPERATION_NONE = 0,
    ATA_OPERATION_READ,
    ATA_OPERATION_WRITE,
    ATA_OPERATION_FLUSH,
};

enum AtaOperationStage : uint8_t {
    ATA_STAGE_NONE = 0,
    ATA_STAGE_SELECT_DEVICE,
    ATA_STAGE_WAIT_READY,
    ATA_STAGE_PROGRAM_LBA,
    ATA_STAGE_ISSUE_COMMAND,
    ATA_STAGE_WAIT_DRQ,
    ATA_STAGE_TRANSFER_DATA,
    ATA_STAGE_WAIT_COMPLETION,
    ATA_STAGE_FLUSH_WAIT_READY,
    ATA_STAGE_FLUSH_ISSUE,
    ATA_STAGE_FLUSH_WAIT_COMPLETION,
};

const char* ata_operation_stage_name(AtaOperationStage stage);

// Fixed-size latest-command evidence. Register bytes are valid only on failure.
struct AtaIoDiagnostic {
    bool valid;
    AtaOperationKind operation;
    AtaOperationStage stage;
    uint64_t requestedLba;
    uint64_t failingLba;
    uint32_t requestedSectors;
    uint32_t completedSectors;
    uint32_t dataSectorsTransferred;
    uint8_t statusRegister;
    uint8_t errorRegister;
    bool errorRegisterValid;
    block::Status result;
};

struct AtaPioIoOps {
    void* context;
    uint8_t (*read8)(void* context, uint16_t port);
    void (*write8)(void* context, uint16_t port, uint8_t value);
    uint16_t (*read16)(void* context, uint16_t port);
    void (*write16)(void* context, uint16_t port, uint16_t value);
    void (*delay400ns)(void* context, uint16_t ctrlBase);
    uint64_t (*ticks)(void* context);
};

static inline AtaFlushSupport ata_flush_support_from_identify_word83(uint16_t word83)
{
    const bool valid = (word83 & 0xC000u) == 0x4000u;
    AtaFlushSupport support;
    support.flushCache = valid && (word83 & (1u << 12)) != 0;
    support.flushCacheExt = valid && (word83 & (1u << 13)) != 0;
    return support;
}


// Port-I/O seam used by the PIO transport and deterministic command tests.
struct AtaFlushIoOps {
    void* context;
    uint8_t (*read8)(void* context, uint16_t port);
    void (*write8)(void* context, uint16_t port, uint8_t value);
    uint64_t (*ticks)(void* context);
};

namespace detail {

static inline bool ata_poll_expired(uint64_t (*ticks)(void*), void* context,
    uint64_t startTicks, bool& clockAdvanced, uint64_t poll,
    uint32_t pollLimit, uint32_t timeoutTicks)
{
    if (ticks) {
        const uint64_t now = ticks(context);
        if (now != startTicks) clockAdvanced = true;
        if (clockAdvanced && now - startTicks >= timeoutTicks) return true;
    }
    return !clockAdvanced && poll >= pollLimit;
}

static inline void ata_set_failure(const AtaPioIoOps& io, uint16_t ioBase,
                                   AtaIoDiagnostic& diagnostic,
                                   AtaOperationStage stage,
                                   block::Status result, uint8_t status)
{
    diagnostic.stage = stage;
    diagnostic.statusRegister = status;
    diagnostic.errorRegisterValid = (status & (ATA_SR_ERR | ATA_SR_DF)) != 0;
    diagnostic.errorRegister = diagnostic.errorRegisterValid && io.read8
        ? io.read8(io.context, static_cast<uint16_t>(ioBase + ATA_REG_ERROR))
        : 0;
    diagnostic.result = result;
}

static inline block::Status ata_wait_ready(const AtaPioIoOps& io,
    uint16_t ioBase, uint32_t pollLimit, AtaIoDiagnostic& diagnostic,
    AtaOperationStage stage)
{
    uint8_t lastStatus = 0;
    const uint64_t startTicks = io.ticks ? io.ticks(io.context) : 0;
    bool clockAdvanced = false;
    for (uint64_t i = 0;; ++i) {
        if (ata_poll_expired(io.ticks, io.context, startTicks, clockAdvanced, i,
                             pollLimit, ATA_COMMAND_TIMEOUT_TICKS)) break;
        const uint8_t status = io.read8(io.context,
            static_cast<uint16_t>(ioBase + ATA_REG_STATUS));
        lastStatus = status;
        if (status == 0 || status == 0xFFu) {
            ata_set_failure(io, ioBase, diagnostic, stage,
                            block::BLOCK_ERR_NO_MEDIA, status);
            return block::BLOCK_ERR_NO_MEDIA;
        }
        if (status & ATA_SR_BSY) continue;
        if (status & ATA_SR_ERR) {
            ata_set_failure(io, ioBase, diagnostic, stage,
                            block::BLOCK_ERR_IO, status);
            return block::BLOCK_ERR_IO;
        }
        if (status & ATA_SR_DF) {
            ata_set_failure(io, ioBase, diagnostic, stage,
                            block::BLOCK_ERR_IO, status);
            return block::BLOCK_ERR_IO;
        }
        if (status & ATA_SR_DRQ) continue;
        if (status & ATA_SR_DRDY) return block::BLOCK_OK;
    }
    const block::Status result = (lastStatus & ATA_SR_BSY)
        ? block::BLOCK_ERR_TIMEOUT : block::BLOCK_ERR_NOT_READY;
    ata_set_failure(io, ioBase, diagnostic, stage, result, lastStatus);
    return result;
}

static inline block::Status ata_wait_drq(const AtaPioIoOps& io,
    uint16_t ioBase, uint32_t pollLimit, AtaIoDiagnostic& diagnostic)
{
    uint8_t lastStatus = 0;
    const uint64_t startTicks = io.ticks ? io.ticks(io.context) : 0;
    bool clockAdvanced = false;
    for (uint64_t i = 0;; ++i) {
        if (ata_poll_expired(io.ticks, io.context, startTicks, clockAdvanced, i,
                             pollLimit, ATA_COMMAND_TIMEOUT_TICKS)) break;
        const uint8_t status = io.read8(io.context,
            static_cast<uint16_t>(ioBase + ATA_REG_STATUS));
        lastStatus = status;
        if (status == 0 || status == 0xFFu) {
            ata_set_failure(io, ioBase, diagnostic, ATA_STAGE_WAIT_DRQ,
                            block::BLOCK_ERR_NO_MEDIA, status);
            return block::BLOCK_ERR_NO_MEDIA;
        }
        if (status & ATA_SR_BSY) continue;
        if (status & ATA_SR_ERR) {
            ata_set_failure(io, ioBase, diagnostic, ATA_STAGE_WAIT_DRQ,
                            block::BLOCK_ERR_IO, status);
            return block::BLOCK_ERR_IO;
        }
        if (status & ATA_SR_DF) {
            ata_set_failure(io, ioBase, diagnostic, ATA_STAGE_WAIT_DRQ,
                            block::BLOCK_ERR_IO, status);
            return block::BLOCK_ERR_IO;
        }
        if (status & ATA_SR_DRQ)
            return block::BLOCK_OK;
    }
    const block::Status result = (lastStatus & ATA_SR_BSY)
        ? block::BLOCK_ERR_TIMEOUT : block::BLOCK_ERR_NOT_READY;
    ata_set_failure(io, ioBase, diagnostic, ATA_STAGE_WAIT_DRQ,
                    result, lastStatus);
    return result;
}

static inline block::Status ata_wait_completion(const AtaPioIoOps& io,
    uint16_t ioBase, uint32_t pollLimit, AtaIoDiagnostic& diagnostic)
{
    uint8_t lastStatus = 0;
    const uint64_t startTicks = io.ticks ? io.ticks(io.context) : 0;
    bool clockAdvanced = false;
    for (uint64_t i = 0;; ++i) {
        if (ata_poll_expired(io.ticks, io.context, startTicks, clockAdvanced, i,
                             pollLimit, ATA_COMMAND_TIMEOUT_TICKS)) break;
        const uint8_t status = io.read8(io.context,
            static_cast<uint16_t>(ioBase + ATA_REG_STATUS));
        lastStatus = status;
        if (status == 0 || status == 0xFFu) {
            ata_set_failure(io, ioBase, diagnostic,
                            ATA_STAGE_WAIT_COMPLETION,
                            block::BLOCK_ERR_NO_MEDIA, status);
            return block::BLOCK_ERR_NO_MEDIA;
        }
        if (status & ATA_SR_BSY) continue;
        if (status & ATA_SR_ERR) {
            ata_set_failure(io, ioBase, diagnostic,
                            ATA_STAGE_WAIT_COMPLETION,
                            block::BLOCK_ERR_IO, status);
            return block::BLOCK_ERR_IO;
        }
        if (status & ATA_SR_DF) {
            ata_set_failure(io, ioBase, diagnostic,
                            ATA_STAGE_WAIT_COMPLETION,
                            block::BLOCK_ERR_IO, status);
            return block::BLOCK_ERR_IO;
        }
        if (status & ATA_SR_DRQ) continue;
        if (status & ATA_SR_DRDY) return block::BLOCK_OK;
        break;
    }
    const block::Status result = (lastStatus & ATA_SR_BSY)
        ? block::BLOCK_ERR_TIMEOUT : block::BLOCK_ERR_NOT_READY;
    ata_set_failure(io, ioBase, diagnostic, ATA_STAGE_WAIT_COMPLETION,
                    result, lastStatus);
    return result;
}

} // namespace detail

// Execute one sector through the same command sequence used by ATA PIO.
// The driver loops this for multi-sector callbacks so each command's completion
// and any partial progress remain explicit.
static inline block::Status ata_pio_transfer_sector_with_io(
    uint16_t ioBase, uint16_t ctrlBase, bool isMaster, bool lba48Supported,
    uint32_t sectorSize, uint64_t lba, bool write, void* buffer,
    const AtaPioIoOps& io, AtaIoDiagnostic& diagnostic,
    uint32_t pollLimit = 50000000u)
{
    if (!diagnostic.valid) {
        diagnostic.valid = true;
        diagnostic.operation = write ? ATA_OPERATION_WRITE : ATA_OPERATION_READ;
        diagnostic.requestedLba = lba;
        diagnostic.failingLba = lba;
        diagnostic.requestedSectors = 1;
        diagnostic.result = block::BLOCK_OK;
    }
    if (!buffer || !io.read8 || !io.write8 || !io.read16 || !io.write16 ||
        !io.delay400ns || pollLimit == 0 || sectorSize < 512 ||
        sectorSize > 4096 || (sectorSize & (sectorSize - 1)) != 0 ||
        (!lba48Supported && lba > 0x0FFFFFFFull) || lba > 0x0000FFFFFFFFFFFFull) {
        diagnostic.valid = true;
        diagnostic.stage = ATA_STAGE_PROGRAM_LBA;
        diagnostic.failingLba = lba;
        diagnostic.result = block::BLOCK_ERR_INVALID;
        return block::BLOCK_ERR_INVALID;
    }

    const bool extended = lba48Supported && lba > 0x0FFFFFFFull;
    diagnostic.failingLba = lba;
    diagnostic.stage = ATA_STAGE_SELECT_DEVICE;
    io.write8(io.context, static_cast<uint16_t>(ioBase + ATA_REG_DRIVE_HEAD),
        extended ? static_cast<uint8_t>(isMaster ? 0x40u : 0x50u)
                 : static_cast<uint8_t>((isMaster ? 0xE0u : 0xF0u) |
                       ((lba >> 24) & 0x0Fu)));
    io.delay400ns(io.context, ctrlBase);

    block::Status status = detail::ata_wait_ready(io, ioBase, pollLimit,
        diagnostic, ATA_STAGE_WAIT_READY);
    if (status != block::BLOCK_OK) return status;

    diagnostic.stage = ATA_STAGE_PROGRAM_LBA;
    if (extended) {
        // LBA48 task-file bytes are written high first, followed by low bytes.
        io.write8(io.context, static_cast<uint16_t>(ioBase + ATA_REG_SECCOUNT), 0);
        io.write8(io.context, static_cast<uint16_t>(ioBase + ATA_REG_LBA_LO),
                  static_cast<uint8_t>(lba >> 24));
        io.write8(io.context, static_cast<uint16_t>(ioBase + ATA_REG_LBA_MID),
                  static_cast<uint8_t>(lba >> 32));
        io.write8(io.context, static_cast<uint16_t>(ioBase + ATA_REG_LBA_HI),
                  static_cast<uint8_t>(lba >> 40));
        io.write8(io.context, static_cast<uint16_t>(ioBase + ATA_REG_SECCOUNT), 1);
        io.write8(io.context, static_cast<uint16_t>(ioBase + ATA_REG_LBA_LO),
                  static_cast<uint8_t>(lba));
        io.write8(io.context, static_cast<uint16_t>(ioBase + ATA_REG_LBA_MID),
                  static_cast<uint8_t>(lba >> 8));
        io.write8(io.context, static_cast<uint16_t>(ioBase + ATA_REG_LBA_HI),
                  static_cast<uint8_t>(lba >> 16));
    } else {
        io.write8(io.context, static_cast<uint16_t>(ioBase + ATA_REG_SECCOUNT), 1);
        io.write8(io.context, static_cast<uint16_t>(ioBase + ATA_REG_LBA_LO),
                  static_cast<uint8_t>(lba));
        io.write8(io.context, static_cast<uint16_t>(ioBase + ATA_REG_LBA_MID),
                  static_cast<uint8_t>(lba >> 8));
        io.write8(io.context, static_cast<uint16_t>(ioBase + ATA_REG_LBA_HI),
                  static_cast<uint8_t>(lba >> 16));
    }

    diagnostic.stage = ATA_STAGE_ISSUE_COMMAND;
    io.write8(io.context, static_cast<uint16_t>(ioBase + ATA_REG_COMMAND),
        write ? (extended ? ATA_CMD_WRITE_PIO_EXT : ATA_CMD_WRITE_PIO)
              : (extended ? ATA_CMD_READ_PIO_EXT : ATA_CMD_READ_PIO));

    status = detail::ata_wait_drq(io, ioBase, pollLimit, diagnostic);
    if (status != block::BLOCK_OK) return status;

    diagnostic.stage = ATA_STAGE_TRANSFER_DATA;
    uint8_t* bytes = static_cast<uint8_t*>(buffer);
    for (uint32_t i = 0; i < sectorSize / 2; ++i) {
        if (write) {
            const uint16_t word = static_cast<uint16_t>(bytes[i * 2]) |
                static_cast<uint16_t>(static_cast<uint16_t>(bytes[i * 2 + 1]) << 8);
            io.write16(io.context, static_cast<uint16_t>(ioBase + ATA_REG_DATA), word);
        } else {
            const uint16_t word = io.read16(io.context,
                static_cast<uint16_t>(ioBase + ATA_REG_DATA));
            bytes[i * 2] = static_cast<uint8_t>(word);
            bytes[i * 2 + 1] = static_cast<uint8_t>(word >> 8);
        }
    }
    ++diagnostic.dataSectorsTransferred;

    status = detail::ata_wait_completion(io, ioBase, pollLimit, diagnostic);
    if (status != block::BLOCK_OK) return status;

    ++diagnostic.completedSectors;
    diagnostic.stage = ATA_STAGE_NONE;
    diagnostic.statusRegister = 0;
    diagnostic.errorRegister = 0;
    diagnostic.errorRegisterValid = false;
    diagnostic.result = block::BLOCK_OK;
    return block::BLOCK_OK;
}

static inline void ata_flush_failure(const AtaFlushIoOps& io, uint16_t ioBase,
    AtaIoDiagnostic* diagnostic, AtaOperationStage stage,
    block::Status result, uint8_t status)
{
    if (!diagnostic) return;
    diagnostic->stage = stage;
    diagnostic->statusRegister = status;
    diagnostic->errorRegisterValid = (status & (ATA_SR_ERR | ATA_SR_DF)) != 0;
    diagnostic->errorRegister = diagnostic->errorRegisterValid
        ? io.read8(io.context, static_cast<uint16_t>(ioBase + ATA_REG_ERROR)) : 0;
    diagnostic->result = result;
}

static inline block::Status ata_flush_command_with_io(
    uint16_t ioBase, uint16_t ctrlBase, bool isMaster,
    bool flushCacheSupported, bool flushCacheExtSupported,
    const AtaFlushIoOps& io, uint32_t pollLimit = 50000000u,
    AtaIoDiagnostic* diagnostic = nullptr)
{
    if (diagnostic) {
        *diagnostic = {};
        diagnostic->valid = true;
        diagnostic->operation = ATA_OPERATION_FLUSH;
        diagnostic->result = block::BLOCK_OK;
    }
    if (!flushCacheSupported && !flushCacheExtSupported) {
        if (diagnostic) diagnostic->result = block::BLOCK_ERR_UNSUPPORTED;
        return block::BLOCK_ERR_UNSUPPORTED;
    }
    if (!io.read8 || !io.write8 || pollLimit == 0) {
        if (diagnostic) diagnostic->result = block::BLOCK_ERR_INVALID;
        return block::BLOCK_ERR_INVALID;
    }

    if (diagnostic) diagnostic->stage = ATA_STAGE_SELECT_DEVICE;
    io.write8(io.context, static_cast<uint16_t>(ioBase + ATA_REG_DRIVE_HEAD),
              isMaster ? 0xA0u : 0xB0u);
    for (uint8_t i = 0; i < 4; ++i)
        (void)io.read8(io.context, ctrlBase);

    bool ready = false;
    uint8_t lastStatus = 0;
    if (diagnostic) diagnostic->stage = ATA_STAGE_FLUSH_WAIT_READY;
    const uint64_t readyStartTicks = io.ticks ? io.ticks(io.context) : 0;
    bool readyClockAdvanced = false;
    for (uint64_t i = 0;; ++i) {
        if (detail::ata_poll_expired(io.ticks, io.context, readyStartTicks,
                readyClockAdvanced, i, pollLimit,
                ATA_COMMAND_TIMEOUT_TICKS)) break;
        const uint8_t status = io.read8(io.context,
            static_cast<uint16_t>(ioBase + ATA_REG_STATUS));
        lastStatus = status;
        if (status == 0 || status == 0xFFu) {
            ata_flush_failure(io, ioBase, diagnostic, ATA_STAGE_FLUSH_WAIT_READY,
                              block::BLOCK_ERR_NO_MEDIA, status);
            return block::BLOCK_ERR_NO_MEDIA;
        }
        if (status & ATA_SR_BSY) continue;
        if (status & ATA_SR_ERR) {
            ata_flush_failure(io, ioBase, diagnostic, ATA_STAGE_FLUSH_WAIT_READY,
                              block::BLOCK_ERR_IO, status);
            return block::BLOCK_ERR_IO;
        }
        if (status & ATA_SR_DF) {
            ata_flush_failure(io, ioBase, diagnostic, ATA_STAGE_FLUSH_WAIT_READY,
                              block::BLOCK_ERR_IO, status);
            return block::BLOCK_ERR_IO;
        }
        if (!(status & (ATA_SR_BSY | ATA_SR_DRQ)) && (status & ATA_SR_DRDY)) {
            ready = true;
            break;
        }
    }
    if (!ready) {
        const block::Status result = (lastStatus & ATA_SR_BSY)
            ? block::BLOCK_ERR_TIMEOUT : block::BLOCK_ERR_NOT_READY;
        ata_flush_failure(io, ioBase, diagnostic, ATA_STAGE_FLUSH_WAIT_READY,
                          result, lastStatus);
        return result;
    }

    if (diagnostic) diagnostic->stage = ATA_STAGE_FLUSH_ISSUE;
    io.write8(io.context, static_cast<uint16_t>(ioBase + ATA_REG_COMMAND),
        flushCacheExtSupported ? ATA_CMD_CACHE_FLUSH_EXT : ATA_CMD_CACHE_FLUSH);

    if (diagnostic) diagnostic->stage = ATA_STAGE_FLUSH_WAIT_COMPLETION;
    const uint64_t flushStartTicks = io.ticks ? io.ticks(io.context) : 0;
    bool flushClockAdvanced = false;
    for (uint64_t i = 0;; ++i) {
        if (detail::ata_poll_expired(io.ticks, io.context, flushStartTicks,
                flushClockAdvanced, i, pollLimit,
                ATA_FLUSH_TIMEOUT_TICKS)) break;
        const uint8_t status = io.read8(io.context,
            static_cast<uint16_t>(ioBase + ATA_REG_STATUS));
        lastStatus = status;
        if (status == 0 || status == 0xFFu) {
            ata_flush_failure(io, ioBase, diagnostic,
                ATA_STAGE_FLUSH_WAIT_COMPLETION, block::BLOCK_ERR_NO_MEDIA, status);
            return block::BLOCK_ERR_NO_MEDIA;
        }
        if (status & ATA_SR_BSY) continue;
        if (status & ATA_SR_ERR) {
            ata_flush_failure(io, ioBase, diagnostic,
                ATA_STAGE_FLUSH_WAIT_COMPLETION, block::BLOCK_ERR_IO, status);
            return block::BLOCK_ERR_IO;
        }
        if (status & ATA_SR_DF) {
            ata_flush_failure(io, ioBase, diagnostic,
                ATA_STAGE_FLUSH_WAIT_COMPLETION, block::BLOCK_ERR_IO, status);
            return block::BLOCK_ERR_IO;
        }
        if (status & ATA_SR_DRQ) continue;
        if (status & ATA_SR_DRDY) {
            if (diagnostic) {
                diagnostic->stage = ATA_STAGE_NONE;
                diagnostic->statusRegister = 0;
                diagnostic->errorRegister = 0;
                diagnostic->errorRegisterValid = false;
                diagnostic->result = block::BLOCK_OK;
            }
            return block::BLOCK_OK;
        }
        ata_flush_failure(io, ioBase, diagnostic,
            ATA_STAGE_FLUSH_WAIT_COMPLETION, block::BLOCK_ERR_NOT_READY, status);
        return block::BLOCK_ERR_NOT_READY;
    }
    ata_flush_failure(io, ioBase, diagnostic, ATA_STAGE_FLUSH_WAIT_COMPLETION,
        (lastStatus & ATA_SR_BSY) ? block::BLOCK_ERR_TIMEOUT : block::BLOCK_ERR_NOT_READY,
        lastStatus);
    if (diagnostic) return diagnostic->result;
    return block::BLOCK_ERR_TIMEOUT;
}

// ================================================================
// AHCI register offsets (HBA memory-mapped)
// ================================================================

// Generic Host Control registers (offsets from ABAR)
static const uint32_t AHCI_CAP       = 0x00;
static const uint32_t AHCI_GHC       = 0x04;
static const uint32_t AHCI_IS        = 0x08;
static const uint32_t AHCI_PI        = 0x0C;
static const uint32_t AHCI_VS        = 0x10;

// GHC bits
static const uint32_t AHCI_GHC_AE    = 0x80000000u;  // AHCI Enable
static const uint32_t AHCI_GHC_HR    = 0x00000001u;  // HBA Reset

// Per-port registers (offset from ABAR + 0x100 + port * 0x80)
static const uint32_t AHCI_PxCLB     = 0x00;
static const uint32_t AHCI_PxCLBU    = 0x04;
static const uint32_t AHCI_PxFB      = 0x08;
static const uint32_t AHCI_PxFBU     = 0x0C;
static const uint32_t AHCI_PxIS      = 0x10;
static const uint32_t AHCI_PxIE      = 0x14;
static const uint32_t AHCI_PxCMD     = 0x18;
static const uint32_t AHCI_PxTFD     = 0x20;
static const uint32_t AHCI_PxSIG     = 0x24;
static const uint32_t AHCI_PxSSTS    = 0x28;
static const uint32_t AHCI_PxCI      = 0x38;

// Port CMD bits
static const uint32_t AHCI_PxCMD_ST  = 0x0001;
static const uint32_t AHCI_PxCMD_FRE = 0x0010;
static const uint32_t AHCI_PxCMD_FR  = 0x4000;
static const uint32_t AHCI_PxCMD_CR  = 0x8000;

// SATA device signatures
static const uint32_t SATA_SIG_ATA   = 0x00000101;
static const uint32_t SATA_SIG_ATAPI = 0xEB140101;

// ================================================================
// IDENTIFY DEVICE response (selected fields)
// ================================================================

struct IdentifyData {
    uint16_t generalConfig;       // word 0
    uint16_t reserved1[9];        // words 1-9
    char     serial[20];          // words 10-19
    uint16_t reserved2[3];        // words 20-22
    char     firmware[8];         // words 23-26
    char     model[40];           // words 27-46
    uint16_t reserved3[13];       // words 47-59
    uint32_t lba28Sectors;        // words 60-61
    uint16_t reserved4[21];       // words 62-82
    uint16_t commandSets83;       // word 83 — bit 10 = LBA48 supported
    uint16_t reserved5[16];       // words 84-99
    uint64_t lba48Sectors;        // words 100-103
    uint16_t reserved6[2];        // words 104-105
    uint16_t logicalSectorInfo;   // word 106
    uint16_t reserved7[10];       // words 107-116
    uint32_t logicalSectorWords;  // words 117-118
    uint16_t reserved8[137];      // words 119-255
};

// ================================================================
// ATA device descriptor
// ================================================================

struct ATADevice {
    bool     active;
    uint16_t ioBase;              // I/O port base (PIO)
    uint16_t ctrlBase;            // control port base (PIO)
    uint8_t  isMaster;            // 1 = master, 0 = slave
    bool     isAHCI;              // true = AHCI port, false = PIO
    uint8_t  ahciPort;            // AHCI port number (if AHCI)
    uint64_t abar;                // AHCI Base Address (MMIO)
    bool     lba48;               // supports 48-bit LBA
    uint16_t identifyCommandSets83;
    bool     flushCache;          // IDENTIFY word 83 advertises FLUSH CACHE
    bool     flushCacheExt;       // IDENTIFY word 83 advertises FLUSH CACHE EXT
    uint64_t totalSectors;
    uint32_t sectorSize;          // almost always 512
    char     model[41];           // null-terminated model string
    char     serial[21];          // null-terminated serial string
    char     name[32];            // human-readable, e.g. "ata0m"
};

static const uint8_t MAX_ATA_DEVICES = 8;

// ================================================================
// Public API
// ================================================================

// Scan for ATA/SATA controllers and register discovered drives
// with the block device layer.
void init();

// Return the number of discovered ATA/SATA drives.
uint8_t device_count();

// Return device info by driver-local index.
const ATADevice* get_device(uint8_t index);

} // namespace ata
} // namespace kernel

#endif // KERNEL_ATA_H
