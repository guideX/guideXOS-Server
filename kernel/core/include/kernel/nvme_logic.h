// Pure NVMe command/queue rules shared by the production driver and storage tests.
#ifndef KERNEL_NVME_LOGIC_H
#define KERNEL_NVME_LOGIC_H

#include "kernel/nvme.h"

namespace kernel {
namespace nvme {
namespace logic {

enum QueueBeginResult : uint8_t {
    QUEUE_BEGIN_OK = 0,
    QUEUE_BEGIN_BUSY,
    QUEUE_BEGIN_POISONED,
    QUEUE_BEGIN_INVALID,
};

enum CompletionKind : uint8_t {
    COMPLETION_NOT_READY = 0,
    COMPLETION_SUCCESS,
    COMPLETION_ERROR,
    COMPLETION_CORRUPT,
};

enum CompletionErrorClass : uint8_t {
    COMPLETION_ERROR_NONE = 0,
    COMPLETION_ERROR_INVALID_OPCODE,
    COMPLETION_ERROR_INVALID_FIELD,
    COMPLETION_ERROR_COMMAND_ID_CONFLICT,
    COMPLETION_ERROR_DATA_TRANSFER,
    COMPLETION_ERROR_INTERNAL,
    COMPLETION_ERROR_NAMESPACE,
    COMPLETION_ERROR_ABORTED,
    COMPLETION_ERROR_OTHER,
};

struct QueueOwnership {
    uint16_t submissionTail;
    uint16_t completionHead;
    uint16_t nextCommandId;
    uint16_t expectedCommandId;
    uint16_t expectedSubmissionHead;
    uint8_t completionPhase;
    bool outstanding;
    bool poisoned;
};

struct CompletionResult {
    CompletionKind kind;
    uint16_t rawStatus;
    uint16_t commandId;
    uint16_t submissionQueueId;
    uint16_t submissionHead;
    uint8_t statusCodeType;
    uint8_t statusCode;
    bool doNotRetry;
    CompletionErrorClass errorClass;
};

inline CompletionErrorClass classify_status(uint8_t statusCodeType,
                                             uint8_t statusCode)
{
    if (statusCodeType != 0u) return COMPLETION_ERROR_OTHER;
    switch (statusCode) {
        case 0u: return COMPLETION_ERROR_NONE;
        case 0x01u: return COMPLETION_ERROR_INVALID_OPCODE;
        case 0x02u: return COMPLETION_ERROR_INVALID_FIELD;
        case 0x03u: return COMPLETION_ERROR_COMMAND_ID_CONFLICT;
        case 0x04u: return COMPLETION_ERROR_DATA_TRANSFER;
        case 0x06u: return COMPLETION_ERROR_INTERNAL;
        case 0x07u: case 0x08u: case 0x09u: case 0x0Au:
            return COMPLETION_ERROR_ABORTED;
        case 0x0Bu: case 0x82u:
            return COMPLETION_ERROR_NAMESPACE;
        default: return COMPLETION_ERROR_OTHER;
    }
}

inline uint16_t advance(uint16_t index, uint16_t depth)
{
    return static_cast<uint16_t>((index + 1u) % depth);
}

inline void reset_queue(QueueOwnership& queue)
{
    queue = {};
    queue.completionPhase = 1u;
}

inline QueueBeginResult begin_command(QueueOwnership& queue, uint16_t depth,
                                      uint16_t& commandId,
                                      uint16_t& submissionSlot,
                                      uint16_t& newTail)
{
    if (depth < 2u || queue.submissionTail >= depth ||
        queue.completionHead >= depth) return QUEUE_BEGIN_INVALID;
    if (queue.poisoned) return QUEUE_BEGIN_POISONED;
    if (queue.outstanding) return QUEUE_BEGIN_BUSY;

    commandId = queue.nextCommandId++;
    submissionSlot = queue.submissionTail;
    newTail = advance(queue.submissionTail, depth);
    queue.expectedCommandId = commandId;
    queue.expectedSubmissionHead = newTail;
    queue.submissionTail = newTail;
    queue.outstanding = true;
    return QUEUE_BEGIN_OK;
}

inline void poison_queue(QueueOwnership& queue)
{
    queue.poisoned = true;
}

inline CompletionResult consume_completion(QueueOwnership& queue,
                                           const CompletionEntry& entry,
                                           uint16_t queueId,
                                           uint16_t depth)
{
    CompletionResult result = {};
    result.kind = COMPLETION_NOT_READY;
    result.rawStatus = entry.status;
    result.commandId = entry.commandId;
    result.submissionQueueId = entry.sqId;
    result.submissionHead = entry.sqHead;
    result.statusCode = static_cast<uint8_t>((entry.status >> 1) & 0xFFu);
    result.statusCodeType = static_cast<uint8_t>((entry.status >> 9) & 0x07u);
    result.doNotRetry = (entry.status & 0x8000u) != 0u;
    result.errorClass = classify_status(result.statusCodeType,
                                         result.statusCode);

    if (depth < 2u || !queue.outstanding || queue.poisoned ||
        queue.completionHead >= depth || queue.completionPhase > 1u) {
        result.kind = COMPLETION_CORRUPT;
        queue.poisoned = true;
        return result;
    }

    if ((entry.status & 1u) != queue.completionPhase)
        return result;

    if (entry.commandId != queue.expectedCommandId || entry.sqId != queueId ||
        entry.sqHead >= depth || entry.sqHead != queue.expectedSubmissionHead) {
        result.kind = COMPLETION_CORRUPT;
        queue.poisoned = true;
        return result;
    }

    queue.completionHead = advance(queue.completionHead, depth);
    if (queue.completionHead == 0u) queue.completionPhase ^= 1u;
    queue.outstanding = false;
    if (result.errorClass == COMPLETION_ERROR_NONE)
        result.kind = COMPLETION_SUCCESS;
    else
        result.kind = COMPLETION_ERROR;
    return result;
}

inline void build_flush_command(SubmissionEntry& command, uint32_t nsid,
                                uint16_t commandId)
{
    command = {};
    command.opcode = NVME_IO_FLUSH;
    command.commandId = commandId;
    command.nsid = nsid;
}

inline bool build_prp(uint64_t physicalBuffer, uint32_t byteCount,
                      uint64_t& prp1, uint64_t& prp2);

inline bool build_rw_command(SubmissionEntry& command, uint8_t opcode,
                             uint32_t nsid, uint16_t commandId,
                             uint64_t lba, uint32_t blockCount,
                             uint64_t physicalBuffer, uint32_t byteCount)
{
    command = {};
    uint64_t prp1 = 0u;
    uint64_t prp2 = 0u;
    if (blockCount == 0u || byteCount % blockCount != 0u)
        return false;
    const uint32_t bytesPerBlock = byteCount / blockCount;
    if ((opcode != NVME_IO_READ && opcode != NVME_IO_WRITE) || nsid == 0u ||
        blockCount > 65536u || bytesPerBlock < 512u ||
        bytesPerBlock > 4096u ||
        (bytesPerBlock & (bytesPerBlock - 1u)) != 0u ||
        !build_prp(physicalBuffer, byteCount, prp1, prp2))
        return false;

    command.opcode = opcode;
    command.commandId = commandId;
    command.nsid = nsid;
    command.prp1 = prp1;
    command.prp2 = prp2;
    command.cdw10 = static_cast<uint32_t>(lba);
    command.cdw11 = static_cast<uint32_t>(lba >> 32);
    command.cdw12 = blockCount - 1u;
    return true;
}

inline uint32_t transfer_chunk(uint32_t remainingBlocks,
                               uint32_t logicalBlockSize)
{
    if (remainingBlocks == 0u || logicalBlockSize < 512u ||
        logicalBlockSize > 4096u ||
        (logicalBlockSize & (logicalBlockSize - 1u)) != 0u ||
        logicalBlockSize > 4096u) return 0u;
    const uint32_t blocksPerCommand = 4096u / logicalBlockSize;
    return remainingBlocks < blocksPerCommand
        ? remainingBlocks : blocksPerCommand;
}

inline bool valid_namespace_geometry(uint64_t nsze, uint64_t ncap,
                                     uint8_t nlbaf, uint8_t flbas,
                                     uint8_t lbads, uint16_t metadataSize,
                                     uint64_t& capacityBytes,
                                     uint32_t& logicalBlockSize)
{
    capacityBytes = 0u;
    logicalBlockSize = 0u;
    if (nsze == 0u || ncap == 0u || ncap > nsze || nlbaf >= 16u ||
        flbas > nlbaf || flbas >= 16u || lbads < 9u || lbads > 12u ||
        metadataSize != 0u)
        return false;
    logicalBlockSize = 1u << lbads;
    if (nsze > UINT64_MAX / logicalBlockSize) {
        logicalBlockSize = 0u;
        return false;
    }
    capacityBytes = nsze * logicalBlockSize;
    return true;
}

inline bool checked_lba_range(uint64_t totalBlocks, uint64_t lba,
                              uint32_t blockCount)
{
    return blockCount != 0u && lba < totalBlocks &&
        static_cast<uint64_t>(blockCount) <= totalBlocks - lba;
}

inline bool build_prp(uint64_t physicalBuffer, uint32_t byteCount,
                      uint64_t& prp1, uint64_t& prp2)
{
    prp1 = 0u;
    prp2 = 0u;
    if (physicalBuffer == 0u || byteCount == 0u || byteCount > 4096u ||
        (physicalBuffer & 0xFFFu) != 0u ||
        byteCount > UINT64_MAX - physicalBuffer)
        return false;
    prp1 = physicalBuffer;
    return true;
}

inline bool is_nonzero_identifier(const uint8_t* bytes, uint32_t length)
{
    if (!bytes || length == 0u) return false;
    for (uint32_t i = 0; i < length; ++i)
        if (bytes[i] != 0u) return true;
    return false;
}

inline bool command_fits_mdts(uint8_t mdts, uint8_t minimumPageSize,
                              uint32_t bytes)
{
    if (bytes == 0u || bytes > 4096u || minimumPageSize > 0u) return false;
    if (mdts == 0u) return true; // Zero means no controller-declared limit.
    if (mdts >= 52u) return true; // The declared limit is at least 2^64 bytes.
    const uint64_t controllerLimit = (1ULL << mdts) * 4096ULL;
    return bytes <= controllerLimit;
}

inline bool flush_proves_durable(bool flushSupported, bool completionSuccess)
{
    return flushSupported && completionSuccess;
}

inline block::NvmeVwcState classify_vwc_state(bool present, bool enabledKnown,
                                               bool enabled)
{
    if (!present) return block::NVME_VWC_NOT_PRESENT;
    if (!enabledKnown) return block::NVME_VWC_UNKNOWN;
    return enabled ? block::NVME_VWC_PRESENT_ENABLED
                   : block::NVME_VWC_PRESENT_DISABLED;
}

inline block::Status read_result(const CompletionResult& result,
                                 bool controllerAvailable,
                                 bool submitted = true)
{
    if (!submitted)
        return controllerAvailable ? block::BLOCK_ERR_NOT_READY
                                   : block::BLOCK_ERR_NO_MEDIA;
    if (!controllerAvailable || result.kind == COMPLETION_CORRUPT)
        return block::BLOCK_ERR_NO_MEDIA;
    if (result.kind == COMPLETION_NOT_READY)
        return block::BLOCK_ERR_TIMEOUT;
    return result.kind == COMPLETION_SUCCESS
        ? block::BLOCK_OK : block::BLOCK_ERR_IO;
}

inline block::Status write_result(const CompletionResult& result,
                                  bool controllerAvailable,
                                  bool submitted = true)
{
    if (!submitted)
        return controllerAvailable ? block::BLOCK_ERR_NOT_READY
                                   : block::BLOCK_ERR_NO_MEDIA;
    if (!controllerAvailable || result.kind == COMPLETION_CORRUPT ||
        result.kind == COMPLETION_NOT_READY || result.kind == COMPLETION_ERROR)
        return block::BLOCK_ERR_WRITE_UNCERTAIN;
    return block::BLOCK_OK;
}

inline block::Status flush_result(const CompletionResult& result,
                                  bool controllerAvailable)
{
    if (!controllerAvailable || result.kind != COMPLETION_SUCCESS)
        return block::BLOCK_ERR_DURABILITY_UNVERIFIED;
    return block::BLOCK_OK;
}

} // namespace logic
} // namespace nvme
} // namespace kernel

#endif // KERNEL_NVME_LOGIC_H
