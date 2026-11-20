// Shared production ATA PIO cache-flush state machine.
//
// The driver supplies the port I/O and clock operations. Keeping the command
// lifecycle here lets the same production state machine run against a bounded
// fake device in the hosted regression.

#ifndef KERNEL_ATA_PIO_FLUSH_H
#define KERNEL_ATA_PIO_FLUSH_H

#include "kernel/ata.h"

namespace kernel {
namespace ata {
namespace pio {

enum class FlushTimeoutSource : uint8_t {
    None = 0,
    PitDeadline,
    PollGuard,
};

struct FlushStatusSample {
    uint64_t poll;
    uint8_t status;
};

static const uint8_t ATA_FLUSH_MAX_STATUS_SAMPLES = 8;

struct FlushTrace {
    uint8_t statusBefore = 0;
    uint8_t deviceHeadSelected = 0;
    uint8_t statusSelected = 0;
    uint8_t statusAfterIssue = 0;
    uint8_t statusFinal = 0;
    uint8_t errorBefore = 0;
    uint8_t errorAfter = 0;
    uint64_t startTicks = 0;
    uint64_t endTicks = 0;
    uint64_t polls = 0;
    FlushTimeoutSource timeoutSource = FlushTimeoutSource::None;
    uint8_t sampleCount = 0;
    FlushStatusSample samples[ATA_FLUSH_MAX_STATUS_SAMPLES] = {};
    bool deadlineAvailable = false;
    bool selectedTarget = false;
    bool selectionMatchesTarget = false;
    bool commandIssued = false;
    bool observedBusy = false;
};

inline uint8_t device_select_value(bool isMaster)
{
    // ATA device/head register: DEV selects master/slave; the remaining bits
    // use the established IDENTIFY convention for a non-addressed command.
    return isMaster ? 0xA0 : 0xB0;
}

inline bool select_cache_flush_opcode(const ATADevice& device, uint8_t* opcode)
{
    if (!opcode) return false;
    if (device.flushCacheExt) {
        *opcode = ATA_CMD_CACHE_FLUSH_EXT;
        return true;
    }
    if (device.flushCache) {
        *opcode = ATA_CMD_CACHE_FLUSH;
        return true;
    }
    *opcode = 0;
    return false;
}

inline bool status_is_absent(uint8_t status)
{
    // A floating legacy IDE bus reads as all ones; an absent device commonly
    // reads zero after selection.
    return status == 0x00 || status == 0xFF;
}

inline bool elapsed_at_least(uint64_t start, uint64_t now, uint64_t budget)
{
    // Unsigned subtraction remains correct across a uint64_t wrap as long as
    // the timeout interval is far shorter than half the counter range.
    return static_cast<uint64_t>(now - start) >= budget;
}

inline void record_status_transition(FlushTrace* trace,
                                     uint64_t poll,
                                     uint8_t status)
{
    if (!trace) return;
    if (trace->sampleCount != 0 &&
        trace->samples[trace->sampleCount - 1].status == status) {
        return;
    }
    if (trace->sampleCount >= ATA_FLUSH_MAX_STATUS_SAMPLES) return;
    FlushStatusSample& sample = trace->samples[trace->sampleCount++];
    sample.poll = poll;
    sample.status = status;
}

// Ops must implement:
//   read_status(ioBase), read_error(ioBase), select_device(ioBase, value),
//   read_device_head(ioBase), delay_400ns(ctrlBase),
//   issue_command(ioBase, opcode), ticks(), and
//   deadline_available().
//
// The deadline is the operational timeout. The poll guard remains as a hard
// bound if the timer stops progressing unexpectedly. With no usable timer,
// fallbackPollLimit preserves a conservative bounded polling path.
template <typename Ops>
block::Status execute_cache_flush(const ATADevice& device,
                                  uint8_t opcode,
                                  Ops& ops,
                                  uint64_t timeoutTicks,
                                  uint64_t fallbackPollLimit,
                                  uint64_t emergencyPollGuard,
                                  FlushTrace* trace)
{
    FlushTrace localTrace = {};
    FlushTrace* record = trace ? trace : &localTrace;
    *record = FlushTrace{};
    const uint64_t startTicks = ops.ticks();
    const bool deadlineAvailable = ops.deadline_available();
    record->startTicks = startTicks;
    record->deadlineAvailable = deadlineAvailable;

    auto finish = [&](block::Status result,
                      uint8_t finalStatus,
                      FlushTimeoutSource timeoutSource = FlushTimeoutSource::None) {
        record->statusFinal = finalStatus;
        record->endTicks = ops.ticks();
        record->timeoutSource = timeoutSource;
        return result;
    };

    auto timed_out = [&]() {
        if (deadlineAvailable &&
            elapsed_at_least(startTicks, ops.ticks(), timeoutTicks)) {
            record->timeoutSource = FlushTimeoutSource::PitDeadline;
            return true;
        }
        const uint64_t limit = deadlineAvailable
            ? emergencyPollGuard
            : fallbackPollLimit;
        if (record->polls >= limit) {
            record->timeoutSource = FlushTimeoutSource::PollGuard;
            return true;
        }
        return false;
    };

    uint8_t status = ops.read_status(device.ioBase);
    record->statusBefore = status;
    record_status_transition(record, 0, status);

    // Do not change DEV while a command on the currently selected device is
    // still active. ATA task-file status is shared by master and slave.
    while (!status_is_absent(status) && (status & (ATA_SR_BSY | ATA_SR_DRQ))) {
        if (status & ATA_SR_BSY) record->observedBusy = true;
        if (timed_out()) {
            return finish(block::BLOCK_ERR_TIMEOUT, status, record->timeoutSource);
        }
        status = ops.read_status(device.ioBase);
        ++record->polls;
        record_status_transition(record, record->polls, status);
    }

    ops.select_device(device.ioBase, device_select_value(device.isMaster != 0));
    ops.delay_400ns(device.ctrlBase);
    record->deviceHeadSelected = ops.read_device_head(device.ioBase);
    record->selectionMatchesTarget =
        (record->deviceHeadSelected & 0x10) == (device.isMaster ? 0 : 0x10);
    record->selectedTarget = record->selectionMatchesTarget;
    status = ops.read_status(device.ioBase);
    record->statusSelected = status;
    record_status_transition(record, record->polls, status);
    if (!record->selectionMatchesTarget) {
        return finish(block::BLOCK_ERR_IO, status);
    }

    // A selected device may still be settling. Errors from its previous
    // command are retained, but a stale ERR alone does not make this new flush
    // successful or prevent the ATA-specified retry-after-error path.
    while (true) {
        if (status_is_absent(status)) {
            return finish(block::BLOCK_ERR_NOT_READY, status);
        }
        // While BSY is set, the remaining status bits are not a valid command
        // completion report and must not be interpreted as ERR/DF.
        if (status & ATA_SR_BSY) {
            record->observedBusy = true;
            if (timed_out()) {
                return finish(block::BLOCK_ERR_TIMEOUT, status, record->timeoutSource);
            }
            status = ops.read_status(device.ioBase);
            ++record->polls;
            record_status_transition(record, record->polls, status);
            continue;
        }
        if (status & ATA_SR_DF) {
            record->errorBefore = ops.read_error(device.ioBase);
            return finish(block::BLOCK_ERR_IO, status);
        }
        if (status & ATA_SR_ERR) {
            record->errorBefore = ops.read_error(device.ioBase);
        }
        if (!(status & ATA_SR_DRQ)) {
            if (!(status & ATA_SR_DRDY)) {
                return finish(block::BLOCK_ERR_NOT_READY, status);
            }
            if (timed_out()) {
                return finish(block::BLOCK_ERR_TIMEOUT, status, record->timeoutSource);
            }
            break;
        }
        if (timed_out()) {
            return finish(block::BLOCK_ERR_TIMEOUT, status, record->timeoutSource);
        }
        status = ops.read_status(device.ioBase);
        ++record->polls;
        record_status_transition(record, record->polls, status);
    }

    ops.issue_command(device.ioBase, opcode);
    record->commandIssued = true;
    status = ops.read_status(device.ioBase);
    record->statusAfterIssue = status;
    record_status_transition(record, record->polls, status);

    while (true) {
        if (status_is_absent(status)) {
            return finish(block::BLOCK_ERR_NOT_READY, status);
        }
        if (status & ATA_SR_BSY) {
            record->observedBusy = true;
            if (timed_out()) {
                return finish(block::BLOCK_ERR_TIMEOUT, status, record->timeoutSource);
            }
            status = ops.read_status(device.ioBase);
            ++record->polls;
            record_status_transition(record, record->polls, status);
            continue;
        }
        if (status & (ATA_SR_ERR | ATA_SR_DF)) {
            record->errorAfter = ops.read_error(device.ioBase);
            return finish(block::BLOCK_ERR_IO, status);
        }
        // FLUSH CACHE is non-data. DRQ set at completion is a protocol
        // failure, not a successful completion.
        if (status & ATA_SR_DRQ) {
            return finish(block::BLOCK_ERR_IO, status);
        }
        if (!(status & ATA_SR_DRDY)) {
            return finish(block::BLOCK_ERR_NOT_READY, status);
        }
        if (timed_out()) {
            return finish(block::BLOCK_ERR_TIMEOUT, status, record->timeoutSource);
        }
        return finish(block::BLOCK_OK, status);
    }
}

} // namespace pio
} // namespace ata
} // namespace kernel

#endif // KERNEL_ATA_PIO_FLUSH_H
