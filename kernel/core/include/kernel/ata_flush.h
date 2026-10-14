// Shared ATA PIO cache-flush command/status logic.
//
// This header intentionally uses only freestanding types and callback
// templates so hosted tests exercise the same polling and command sequence as
// the kernel driver.

#ifndef KERNEL_ATA_FLUSH_H
#define KERNEL_ATA_FLUSH_H

#include "kernel/block_device.h"

namespace kernel {
namespace ata {
namespace flush_detail {

static const uint16_t ATA_ID_COMMAND_SET_FLUSH_CACHE = 0x0020;
static const uint16_t ATA_ID_COMMAND_SET_LBA48 = 0x0400;
static const uint16_t ATA_ID_COMMAND_SET_FLUSH_CACHE_EXT = 0x1000;
static const uint16_t ATA_ID_COMMAND_SET_VALIDITY_MASK = 0xC000;
static const uint16_t ATA_ID_COMMAND_SET_VALID = 0x4000;

static const uint8_t ATA_FLUSH_STATUS_BSY = 0x80;
static const uint8_t ATA_FLUSH_STATUS_DF = 0x20;
static const uint8_t ATA_FLUSH_STATUS_DRQ = 0x08;
static const uint8_t ATA_FLUSH_STATUS_ERR = 0x01;

static const uint8_t ATA_FLUSH_CMD_CACHE = 0xE7;
static const uint8_t ATA_FLUSH_CMD_CACHE_EXT = 0xEA;

enum FlushResultCode : uint8_t {
    FLUSH_SUCCESS = 0,
    FLUSH_TIMEOUT,
    FLUSH_ATA_ERR,
    FLUSH_DEVICE_FAULT,
    FLUSH_ATA_ERR_AND_DEVICE_FAULT,
    FLUSH_PROTOCOL_ERROR,
    FLUSH_TRANSPORT_ERROR,
    FLUSH_UNSUPPORTED,
};

struct FlushResult {
    FlushResultCode code;
    uint8_t status;
    uint8_t command;
    bool commandIssued;
};

struct IdentifyCapabilities {
    bool lba48;
    bool flushCache;
    bool flushCacheExt;
};

inline IdentifyCapabilities decode_identify_capabilities(uint16_t commandSets82,
                                                          uint16_t commandSets83)
{
    const bool commandSetWordsValid =
        (commandSets83 & ATA_ID_COMMAND_SET_VALIDITY_MASK) ==
        ATA_ID_COMMAND_SET_VALID;
    IdentifyCapabilities capabilities;
    capabilities.lba48 = (commandSets83 & ATA_ID_COMMAND_SET_LBA48) != 0;
    capabilities.flushCache = commandSetWordsValid &&
        (commandSets82 & ATA_ID_COMMAND_SET_FLUSH_CACHE) != 0;
    capabilities.flushCacheExt =
        commandSetWordsValid &&
        (commandSets83 & ATA_ID_COMMAND_SET_FLUSH_CACHE_EXT) != 0;
    return capabilities;
}

inline bool choose_flush_command(const IdentifyCapabilities& capabilities,
                                 uint8_t* command)
{
    if (!command) return false;
    if (capabilities.lba48 && capabilities.flushCacheExt) {
        *command = ATA_FLUSH_CMD_CACHE_EXT;
        return true;
    }
    if (capabilities.flushCache) {
        *command = ATA_FLUSH_CMD_CACHE;
        return true;
    }
    return false;
}

inline uint8_t drive_select_value(bool isMaster)
{
    // Non-LBA command selection: 0xA0 for master, 0xB0 for slave.
    return isMaster ? 0xA0 : 0xB0;
}

inline FlushResult make_result(FlushResultCode code, uint8_t status,
                               uint8_t command, bool commandIssued)
{
    FlushResult result;
    result.code = code;
    result.status = status;
    result.command = command;
    result.commandIssued = commandIssued;
    return result;
}

template <typename ReadStatus>
inline FlushResult wait_until_ready(uint32_t timeout, uint8_t command,
                                    ReadStatus readStatus)
{
    uint8_t lastStatus = 0;
    for (uint32_t i = 0; i < timeout; ++i) {
        const uint8_t status = readStatus();
        lastStatus = status;
        if (status == 0 || status == 0xFF) {
            return make_result(FLUSH_TRANSPORT_ERROR, status, command, false);
        }
        // ERR/DF can be left over from an earlier command. They describe a
        // completion only after this flush command is issued. A selected
        // device is ready once BSY and DRQ are both clear.
        if (!(status & (ATA_FLUSH_STATUS_BSY | ATA_FLUSH_STATUS_DRQ))) {
            return make_result(FLUSH_SUCCESS, status, command, false);
        }
    }
    return make_result(FLUSH_TIMEOUT, lastStatus, command, false);
}

template <typename ReadStatus>
inline FlushResult wait_for_completion(uint32_t timeout, uint8_t command,
                                       ReadStatus readStatus)
{
    uint8_t lastStatus = 0;
    for (uint32_t i = 0; i < timeout; ++i) {
        const uint8_t status = readStatus();
        lastStatus = status;
        if (status == 0 || status == 0xFF) {
            return make_result(FLUSH_TRANSPORT_ERROR, status, command, true);
        }
        if (status & ATA_FLUSH_STATUS_BSY) continue;
        const bool hasError = (status & ATA_FLUSH_STATUS_ERR) != 0;
        const bool hasDeviceFault = (status & ATA_FLUSH_STATUS_DF) != 0;
        if (hasError && hasDeviceFault) {
            return make_result(FLUSH_ATA_ERR_AND_DEVICE_FAULT,
                               status, command, true);
        }
        if (hasError) {
            return make_result(FLUSH_ATA_ERR, status, command, true);
        }
        if (hasDeviceFault) {
            return make_result(FLUSH_DEVICE_FAULT, status, command, true);
        }
        if (status & ATA_FLUSH_STATUS_DRQ) {
            return make_result(FLUSH_PROTOCOL_ERROR, status, command, true);
        }
        return make_result(FLUSH_SUCCESS, status, command, true);
    }
    return make_result(FLUSH_TIMEOUT, lastStatus, command, true);
}

template <typename SelectDrive, typename Delay400ns, typename ReadStatus,
          typename WriteCommand>
inline FlushResult issue_cache_flush(const IdentifyCapabilities& capabilities,
                                     bool isMaster, uint32_t timeout,
                                     SelectDrive selectDrive,
                                     Delay400ns delay400ns,
                                     ReadStatus readStatus,
                                     WriteCommand writeCommand)
{
    uint8_t command = 0;
    if (!choose_flush_command(capabilities, &command)) {
        return make_result(FLUSH_UNSUPPORTED, 0, 0, false);
    }

    selectDrive(drive_select_value(isMaster));
    delay400ns();

    const FlushResult ready = wait_until_ready(timeout, command, readStatus);
    if (ready.code != FLUSH_SUCCESS) return ready;

    writeCommand(command);
    delay400ns();
    return wait_for_completion(timeout, command, readStatus);
}

inline block::Status to_block_status(FlushResultCode code)
{
    switch (code) {
        case FLUSH_SUCCESS: return block::BLOCK_OK;
        case FLUSH_TIMEOUT: return block::BLOCK_ERR_TIMEOUT;
        case FLUSH_UNSUPPORTED: return block::BLOCK_ERR_UNSUPPORTED;
        default: return block::BLOCK_ERR_IO;
    }
}

inline const char* result_name(FlushResultCode code)
{
    switch (code) {
        case FLUSH_SUCCESS: return "success";
        case FLUSH_TIMEOUT: return "timeout";
        case FLUSH_ATA_ERR: return "ATA ERR";
        case FLUSH_DEVICE_FAULT: return "ATA DF";
        case FLUSH_ATA_ERR_AND_DEVICE_FAULT: return "ATA ERR+DF";
        case FLUSH_PROTOCOL_ERROR: return "unexpected DRQ";
        case FLUSH_TRANSPORT_ERROR: return "command/transport failure";
        case FLUSH_UNSUPPORTED: return "flush unsupported";
        default: return "unknown";
    }
}

} // namespace flush_detail
} // namespace ata
} // namespace kernel

#endif // KERNEL_ATA_FLUSH_H
