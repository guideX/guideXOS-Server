#ifndef KERNEL_USB_BOT_DIAGNOSTICS_H
#define KERNEL_USB_BOT_DIAGNOSTICS_H

#include "kernel/usb.h"

namespace kernel {
namespace usb {

struct BotWriteCommandContext {
    uint64_t lba;
    uint32_t blockCount;
};

inline uint16_t diagnostic_be16(const uint8_t* bytes)
{
    return static_cast<uint16_t>((static_cast<uint16_t>(bytes[0]) << 8) |
                                 bytes[1]);
}

inline uint32_t diagnostic_be32(const uint8_t* bytes)
{
    return (static_cast<uint32_t>(bytes[0]) << 24) |
           (static_cast<uint32_t>(bytes[1]) << 16) |
           (static_cast<uint32_t>(bytes[2]) << 8) |
           bytes[3];
}

inline uint64_t diagnostic_be64(const uint8_t* bytes)
{
    return (static_cast<uint64_t>(diagnostic_be32(bytes)) << 32) |
           diagnostic_be32(bytes + 4);
}

inline bool decode_write_command(const uint8_t* cdb, uint8_t cdbLength,
                                 BotWriteCommandContext& command)
{
    if (!cdb) return false;
    if (cdb[0] == 0x2Au && cdbLength == 10u) {
        command.lba = diagnostic_be32(cdb + 2);
        command.blockCount = diagnostic_be16(cdb + 7);
        return command.blockCount != 0;
    }
    if (cdb[0] == 0x8Au && cdbLength == 16u) {
        command.lba = diagnostic_be64(cdb + 2);
        command.blockCount = diagnostic_be32(cdb + 10);
        return command.blockCount != 0;
    }
    return false;
}

inline bool expected_block_bytes(uint32_t blockCount, uint32_t blockSize,
                                 uint32_t& bytes)
{
    if (blockCount == 0 || blockSize == 0 ||
        blockCount > UINT32_MAX / blockSize) return false;
    bytes = blockCount * blockSize;
    return true;
}

inline uint32_t bulk_td_count(uint32_t bytes, uint16_t maxPacket)
{
    return bytes == 0 || maxPacket == 0
        ? 0 : 1u + (bytes - 1u) / maxPacket;
}

inline bool accumulate_completed_packet_bytes(uint32_t previousBytes,
                                              uint32_t requestedBytes,
                                              uint32_t actualBytes,
                                              uint32_t& totalBytes)
{
    if (actualBytes > requestedBytes ||
        previousBytes > UINT32_MAX - actualBytes) return false;
    totalBytes = previousBytes + actualBytes;
    return true;
}

inline uint8_t toggle_after_completed_packets(uint8_t startToggle,
                                               uint32_t completedPackets)
{
    return static_cast<uint8_t>((startToggle ^ completedPackets) & 1u);
}

static const uint8_t BOT_DIAGNOSTIC_RING_CAPACITY = 64;
static const uint8_t BOT_COMMAND_HISTORY_CAPACITY = 16;

struct BotDiagnosticRecord {
    uint64_t diagnosticSequence;
    uint64_t commandSequence;
    uint64_t incarnation;
    uint64_t lba;
    uint32_t cbwTag;
    uint32_t blockCount;
    uint32_t transferLength;
    uint32_t qhPhysical;
    uint32_t qhElement;
    uint32_t tdPhysical;
    uint32_t tdStatus;
    uint32_t tdToken;
    uint32_t tdLink;
    uint32_t tdBufferPhysical;
    uint16_t frameNumber;
    uint8_t opcode;
    uint8_t phase;
    uint8_t endpoint;
    uint8_t direction;
    uint8_t toggle;
};

struct BotDiagnosticRing {
    BotDiagnosticRecord records[BOT_DIAGNOSTIC_RING_CAPACITY];
    uint64_t nextSequence;
    uint8_t next;
    uint8_t count;
};

inline void clear_bot_diagnostic_ring(BotDiagnosticRing& ring)
{
    ring.nextSequence = 1;
    ring.next = 0;
    ring.count = 0;
}

inline uint64_t record_bot_diagnostic(BotDiagnosticRing& ring,
                                      BotDiagnosticRecord record)
{
    if (ring.nextSequence == 0) clear_bot_diagnostic_ring(ring);
    record.diagnosticSequence = ring.nextSequence;
    if (ring.nextSequence != UINT64_MAX) ++ring.nextSequence;
    ring.records[ring.next] = record;
    ring.next = static_cast<uint8_t>(
        (ring.next + 1u) % BOT_DIAGNOSTIC_RING_CAPACITY);
    if (ring.count < BOT_DIAGNOSTIC_RING_CAPACITY) ++ring.count;
    return record.diagnosticSequence;
}

inline const BotDiagnosticRecord& bot_diagnostic_at(
    const BotDiagnosticRing& ring, uint8_t index)
{
    const uint8_t first = ring.count == BOT_DIAGNOSTIC_RING_CAPACITY
        ? ring.next : 0u;
    return ring.records[static_cast<uint8_t>(
        (first + index) % BOT_DIAGNOSTIC_RING_CAPACITY)];
}

enum BotCommandResult : uint8_t {
    BOT_COMMAND_PENDING = 0,
    BOT_COMMAND_PASSED,
    BOT_COMMAND_SCSI_FAILED,
    BOT_COMMAND_TRANSPORT_FAILED,
    BOT_COMMAND_CSW_TIMEOUT,
    BOT_COMMAND_CSW_INVALID,
};

struct BotCommandHistoryRecord {
    uint64_t commandSequence;
    uint64_t incarnation;
    uint64_t lba;
    uint64_t blockRegistrationId;
    uint32_t cbwTag;
    uint32_t blockCount;
    uint32_t logicalBlockSize;
    uint32_t expectedBytes;
    uint32_t actualBytes;
    uint32_t expectedCswTag;
    uint32_t dataOutTdCount;
    uint32_t firstDataOutTd;
    uint32_t lastDataOutTd;
    uint32_t cswTd;
    uint16_t cbwSubmitFrame;
    uint16_t cbwCompleteFrame;
    uint16_t dataOutStartFrame;
    uint16_t dataOutCompleteFrame;
    uint16_t cswSubmitFrame;
    uint16_t cswCompleteFrame;
    uint8_t opcode;
    uint8_t cdbLength;
    uint8_t direction;
    uint8_t result;
    uint8_t dataOutStartToggle;
    uint8_t dataOutFinalToggle;
    uint8_t expectedCswToggle;
    uint8_t finalCswToggle;
};

struct BotCommandHistory {
    BotCommandHistoryRecord records[BOT_COMMAND_HISTORY_CAPACITY];
    uint8_t next;
    uint8_t count;
};

inline void clear_bot_command_history(BotCommandHistory& history)
{
    history.next = 0;
    history.count = 0;
}

inline BotCommandHistoryRecord* record_bot_command(
    BotCommandHistory& history, const BotCommandHistoryRecord& record)
{
    BotCommandHistoryRecord* stored = &history.records[history.next];
    *stored = record;
    history.next = static_cast<uint8_t>(
        (history.next + 1u) % BOT_COMMAND_HISTORY_CAPACITY);
    if (history.count < BOT_COMMAND_HISTORY_CAPACITY) ++history.count;
    return stored;
}

inline BotCommandHistoryRecord* find_bot_command(
    BotCommandHistory& history, uint64_t commandSequence)
{
    for (uint8_t i = 0; i < history.count; ++i) {
        const uint8_t first = history.count == BOT_COMMAND_HISTORY_CAPACITY
            ? history.next : 0u;
        BotCommandHistoryRecord* record = &history.records[
            static_cast<uint8_t>((first + i) % BOT_COMMAND_HISTORY_CAPACITY)];
        if (record->commandSequence == commandSequence) return record;
    }
    return nullptr;
}

inline const BotCommandHistoryRecord& bot_command_history_at(
    const BotCommandHistory& history, uint8_t index)
{
    const uint8_t first = history.count == BOT_COMMAND_HISTORY_CAPACITY
        ? history.next : 0u;
    return history.records[static_cast<uint8_t>(
        (first + index) % BOT_COMMAND_HISTORY_CAPACITY)];
}

} // namespace usb
} // namespace kernel

#endif
