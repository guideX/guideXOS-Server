#include "kernel/ata_pio_flush.h"

#include <stdint.h>
#include <cstdio>
#include <string>
#include <vector>

using kernel::ata::ATADevice;
using kernel::ata::ATA_CMD_CACHE_FLUSH;
using kernel::ata::ATA_CMD_CACHE_FLUSH_EXT;
using kernel::ata::ATA_REG_COMMAND;
using kernel::ata::ATA_SR_BSY;
using kernel::ata::ATA_SR_DF;
using kernel::ata::ATA_SR_DRDY;
using kernel::ata::ATA_SR_DRQ;
using kernel::ata::ATA_SR_ERR;
using kernel::ata::ATA_PRIMARY_CTRL;
using kernel::ata::ATA_PRIMARY_IO;
using kernel::ata::ATA_SECONDARY_CTRL;
using kernel::ata::ATA_SECONDARY_IO;
using kernel::ata::pio::FlushTimeoutSource;
using kernel::ata::pio::FlushTrace;
using kernel::ata::pio::execute_cache_flush;
using kernel::ata::pio::select_cache_flush_opcode;
using kernel::block::Status;

namespace {

int g_checks = 0;
int g_failures = 0;

void check(bool condition, const char* name)
{
    ++g_checks;
    if (!condition) {
        ++g_failures;
        std::printf("FAIL %s\n", name);
    }
}

struct FakeAta {
    std::vector<uint8_t> statuses;
    std::vector<std::string> events;
    std::vector<uint16_t> statusPorts;
    std::vector<uint8_t> selectedValues;
    std::vector<uint16_t> selectedPorts;
    std::vector<uint16_t> settledPorts;
    std::vector<uint16_t> settledControlPorts;
    std::vector<uint8_t> commands;
    std::vector<uint16_t> commandPorts;
    size_t nextStatus = 0;
    uint8_t lastStatus = ATA_SR_DRDY;
    uint8_t error = 0;
    uint8_t deviceHeadReadback = 0;
    bool overrideDeviceHeadReadback = false;
    uint64_t clock = 0;
    uint64_t tickAdvancePerStatusRead = 0;
    bool deadline = true;

    uint8_t read_status(uint16_t ioBase)
    {
        statusPorts.push_back(ioBase);
        events.push_back("status");
        if (nextStatus < statuses.size()) lastStatus = statuses[nextStatus++];
        clock += tickAdvancePerStatusRead;
        return lastStatus;
    }

    uint8_t read_error(uint16_t ioBase)
    {
        events.push_back("error");
        check(ioBase == expectedIoBase, "error read from target controller");
        return error;
    }

    void select_device(uint16_t ioBase, uint8_t value)
    {
        selectedPorts.push_back(ioBase);
        selectedValues.push_back(value);
        if (!overrideDeviceHeadReadback) deviceHeadReadback = value;
        events.push_back("select");
    }

    uint8_t read_device_head(uint16_t ioBase)
    {
        events.push_back("device-head");
        check(ioBase == expectedIoBase, "device/head read from target controller");
        return deviceHeadReadback;
    }

    void delay_400ns(uint16_t ctrlBase)
    {
        settledControlPorts.push_back(ctrlBase);
        events.push_back("settle");
    }

    void issue_command(uint16_t ioBase, uint8_t opcode)
    {
        commandPorts.push_back(ioBase);
        commands.push_back(opcode);
        events.push_back("command");
    }

    uint64_t ticks() const { return clock; }
    bool deadline_available() const { return deadline; }

    uint16_t expectedIoBase = ATA_PRIMARY_IO;
};

ATADevice make_device(bool master = true,
                     uint16_t ioBase = ATA_PRIMARY_IO,
                     uint16_t ctrlBase = ATA_PRIMARY_CTRL)
{
    ATADevice device = {};
    device.active = true;
    device.ioBase = ioBase;
    device.ctrlBase = ctrlBase;
    device.isMaster = master ? 1 : 0;
    device.flushCache = true;
    return device;
}

void test_flush_command_capability_selection()
{
    ATADevice device = make_device();
    uint8_t opcode = 0;
    device.lba48 = true;
    device.flushCache = true;
    device.flushCacheExt = false;
    check(select_cache_flush_opcode(device, &opcode) && opcode == ATA_CMD_CACHE_FLUSH,
          "LBA48 without FLUSH CACHE EXT uses advertised base command");

    device.flushCacheExt = true;
    check(select_cache_flush_opcode(device, &opcode) && opcode == ATA_CMD_CACHE_FLUSH_EXT,
          "advertised FLUSH CACHE EXT selects extended opcode");

    device.flushCache = false;
    device.flushCacheExt = false;
    check(!select_cache_flush_opcode(device, &opcode) && opcode == 0,
          "unadvertised cache flush commands fail closed");
    check(!select_cache_flush_opcode(make_device(), nullptr),
          "null opcode output is rejected");
}

void test_target_selection_and_status_polling()
{
    FakeAta io;
    io.statuses = {ATA_SR_DRDY, ATA_SR_DRDY,
                   static_cast<uint8_t>(ATA_SR_BSY), ATA_SR_DRDY};
    FlushTrace trace;
    const Status result = execute_cache_flush(
        make_device(true), ATA_CMD_CACHE_FLUSH, io, 6000, 500000, 500000000, &trace);

    check(result == kernel::block::BLOCK_OK, "master flush completes");
    check(io.selectedValues.size() == 1 && io.selectedValues[0] == 0xA0,
          "master device selection value");
    check(io.selectedPorts.size() == 1 && io.selectedPorts[0] == ATA_PRIMARY_IO,
          "master uses primary command block");
    check(trace.deviceHeadSelected == 0xA0 && trace.selectionMatchesTarget,
          "master device/head readback confirms selected target");
    check(io.settledControlPorts.size() == 1 && io.settledControlPorts[0] == ATA_PRIMARY_CTRL,
          "master selection uses primary control block settling delay");
    check(trace.observedBusy && trace.polls == 1, "busy transition counted once");
    check(trace.sampleCount == 3, "status trace records transitions only");
    check(io.events.size() >= 6 && io.events[1] == "select" && io.events[2] == "settle",
          "target selection follows idle status and includes 400ns settling");
    check(io.commands.size() == 1 && io.commands[0] == ATA_CMD_CACHE_FLUSH,
          "flush opcode issued after selection");
}

void test_master_slave_and_controller_transitions()
{
    FakeAta io;
    io.statuses = {ATA_SR_DRDY, ATA_SR_DRDY, ATA_SR_DRDY,
                   ATA_SR_DRDY, ATA_SR_DRDY, ATA_SR_DRDY};
    FlushTrace first;
    FlushTrace second;
    Status result = execute_cache_flush(
        make_device(true), ATA_CMD_CACHE_FLUSH, io, 6000, 500000, 500000000, &first);
    check(result == kernel::block::BLOCK_OK, "primary master transition flush succeeds");

    io.expectedIoBase = ATA_PRIMARY_IO;
    result = execute_cache_flush(
        make_device(false), ATA_CMD_CACHE_FLUSH, io, 6000, 500000, 500000000, &second);
    check(result == kernel::block::BLOCK_OK, "primary slave transition flush succeeds");
    check(io.selectedValues.size() == 2 && io.selectedValues[0] == 0xA0 &&
          io.selectedValues[1] == 0xB0, "master-to-slave transition selects each target");

    io.expectedIoBase = ATA_SECONDARY_IO;
    FlushTrace third;
    result = execute_cache_flush(
        make_device(true, ATA_SECONDARY_IO, ATA_SECONDARY_CTRL),
        ATA_CMD_CACHE_FLUSH, io, 6000, 500000, 500000000, &third);
    check(result == kernel::block::BLOCK_OK, "secondary-channel transition flush succeeds");
    check(io.selectedPorts.size() == 3 && io.selectedPorts[2] == ATA_SECONDARY_IO,
          "secondary target uses its command block");
    check(io.settledControlPorts.size() == 3 &&
          io.settledControlPorts[2] == ATA_SECONDARY_CTRL,
          "secondary target uses its control block settling delay");
    check(io.commandPorts.size() == 3 && io.commandPorts[2] == ATA_SECONDARY_IO,
          "secondary flush command goes to the selected controller channel");
}

void test_mismatched_device_head_is_rejected()
{
    FakeAta io;
    io.overrideDeviceHeadReadback = true;
    io.deviceHeadReadback = 0xA0;
    io.statuses = {ATA_SR_DRDY, ATA_SR_DRDY};
    FlushTrace trace;
    const Status result = execute_cache_flush(
        make_device(false), ATA_CMD_CACHE_FLUSH, io, 6000, 500000, 500000000,
        &trace);
    check(result == kernel::block::BLOCK_ERR_IO,
          "mismatched device/head selection fails visibly");
    check(!trace.selectionMatchesTarget && io.commands.empty(),
          "flush is not issued to a different selected drive");
}

void test_waits_for_existing_channel_command_before_select()
{
    FakeAta io;
    io.statuses = {static_cast<uint8_t>(ATA_SR_BSY),
                   static_cast<uint8_t>(ATA_SR_BSY), ATA_SR_DRDY,
                   ATA_SR_DRDY, ATA_SR_DRDY};
    FlushTrace trace;
    const Status result = execute_cache_flush(
        make_device(false), ATA_CMD_CACHE_FLUSH, io, 6000, 500000, 500000000, &trace);
    check(result == kernel::block::BLOCK_OK, "flush after prior channel command succeeds");
    check(io.events.size() > 3 && io.events[0] == "status" &&
          io.events[1] == "status" && io.events[2] == "status" &&
          io.events[3] == "select", "driver waits for BSY to clear before changing device");
    check(trace.polls == 2, "prior channel BSY polls are included");
}

void test_ignores_status_bits_while_busy()
{
    FakeAta io;
    io.statuses = {ATA_SR_DRDY,
                   static_cast<uint8_t>(ATA_SR_BSY | ATA_SR_ERR | ATA_SR_DRDY),
                   ATA_SR_DRDY,
                   static_cast<uint8_t>(ATA_SR_BSY | ATA_SR_DF),
                   ATA_SR_DRDY};
    FlushTrace trace;
    const Status result = execute_cache_flush(
        make_device(), ATA_CMD_CACHE_FLUSH, io, 6000, 500000, 500000000,
        &trace);
    check(result == kernel::block::BLOCK_OK,
          "ERR and DF bits are ignored while BSY makes status invalid");
    check(trace.polls == 2 && trace.errorBefore == 0 && trace.errorAfter == 0,
          "busy status transitions do not read stale error state");
}

void test_err_df_and_unexpected_completion()
{
    FakeAta io;
    io.error = 0x04;
    io.statuses = {ATA_SR_DRDY, static_cast<uint8_t>(ATA_SR_DRDY | ATA_SR_ERR),
                   static_cast<uint8_t>(ATA_SR_DRDY | ATA_SR_ERR)};
    FlushTrace trace;
    Status result = execute_cache_flush(
        make_device(), ATA_CMD_CACHE_FLUSH, io, 6000, 500000, 500000000, &trace);
    check(result == kernel::block::BLOCK_ERR_IO, "ERR is returned as device I/O failure");
    check(trace.errorBefore == 0x04 && trace.errorAfter == 0x04,
          "ATA error register retained before and after issue");
    check(io.commands.size() == 1, "stale ERR does not masquerade as current success");

    FakeAta df;
    df.error = 0x40;
    df.statuses = {ATA_SR_DRDY,
                   static_cast<uint8_t>(ATA_SR_DRDY | ATA_SR_DF)};
    FlushTrace dfTrace;
    result = execute_cache_flush(
        make_device(), ATA_CMD_CACHE_FLUSH, df, 6000, 500000, 500000000, &dfTrace);
    check(result == kernel::block::BLOCK_ERR_IO, "DF is returned as device I/O failure");
    check(dfTrace.errorBefore == 0x40 && df.commands.empty(),
          "DF error register retained without issuing another command");

    FakeAta dfAfter;
    dfAfter.error = 0x40;
    dfAfter.statuses = {ATA_SR_DRDY, ATA_SR_DRDY,
                        static_cast<uint8_t>(ATA_SR_DRDY | ATA_SR_DF)};
    FlushTrace dfAfterTrace;
    result = execute_cache_flush(
        make_device(), ATA_CMD_CACHE_FLUSH, dfAfter, 6000, 500000, 500000000,
        &dfAfterTrace);
    check(result == kernel::block::BLOCK_ERR_IO,
          "DF after flush submission is returned as device I/O failure");
    check(dfAfterTrace.errorAfter == 0x40,
          "post-command DF preserves the error register");

    FakeAta drq;
    drq.statuses = {ATA_SR_DRDY, ATA_SR_DRDY,
                    static_cast<uint8_t>(ATA_SR_DRDY | ATA_SR_DRQ)};
    FlushTrace drqTrace;
    result = execute_cache_flush(
        make_device(), ATA_CMD_CACHE_FLUSH, drq, 6000, 500000, 500000000, &drqTrace);
    check(result == kernel::block::BLOCK_ERR_IO,
          "non-data flush with DRQ set is not reported successful");
}

void test_missing_device_and_completion()
{
    FakeAta missing;
    missing.statuses = {ATA_SR_DRDY, 0x00};
    FlushTrace missingTrace;
    Status result = execute_cache_flush(
        make_device(), ATA_CMD_CACHE_FLUSH, missing, 6000, 500000, 500000000,
        &missingTrace);
    check(result == kernel::block::BLOCK_ERR_NOT_READY,
          "missing selected target reports not ready");
    check(missing.commands.empty(), "missing target receives no flush command");

    FakeAta immediate;
    immediate.statuses = {ATA_SR_DRDY, ATA_SR_DRDY, ATA_SR_DRDY};
    FlushTrace immediateTrace;
    result = execute_cache_flush(
        make_device(), ATA_CMD_CACHE_FLUSH, immediate, 6000, 500000, 500000000,
        &immediateTrace);
    check(result == kernel::block::BLOCK_OK, "immediate completion is accepted");
    check(immediateTrace.polls == 0 && immediateTrace.statusFinal == ATA_SR_DRDY,
          "immediate completion has no status wait polls");
}

void test_deadline_and_fallback_timeout()
{
    FakeAta beforeDeadline;
    beforeDeadline.clock = 100;
    beforeDeadline.tickAdvancePerStatusRead = 1;
    beforeDeadline.statuses = {ATA_SR_DRDY, ATA_SR_DRDY,
                              static_cast<uint8_t>(ATA_SR_BSY), ATA_SR_DRDY};
    FlushTrace beforeTrace;
    Status result = execute_cache_flush(
        make_device(), ATA_CMD_CACHE_FLUSH, beforeDeadline, 5, 500000, 500000000,
        &beforeTrace);
    check(result == kernel::block::BLOCK_OK,
          "completion immediately before deadline is accepted");
    check(beforeTrace.timeoutSource == FlushTimeoutSource::None,
          "pre-deadline completion is not marked timeout");

    FakeAta atDeadline;
    atDeadline.tickAdvancePerStatusRead = 2;
    atDeadline.statuses = {ATA_SR_DRDY, ATA_SR_DRDY, ATA_SR_DRDY};
    FlushTrace atDeadlineTrace;
    result = execute_cache_flush(
        make_device(), ATA_CMD_CACHE_FLUSH, atDeadline, 6, 500000, 500000000,
        &atDeadlineTrace);
    check(result == kernel::block::BLOCK_ERR_TIMEOUT,
          "completion first observed at deadline remains a timeout");
    check(atDeadlineTrace.timeoutSource == FlushTimeoutSource::PitDeadline,
          "late ready status records the PIT deadline timeout");

    FakeAta expired;
    expired.clock = 200;
    expired.tickAdvancePerStatusRead = 1;
    expired.statuses = {ATA_SR_DRDY, ATA_SR_DRDY,
                        static_cast<uint8_t>(ATA_SR_BSY),
                        static_cast<uint8_t>(ATA_SR_BSY),
                        static_cast<uint8_t>(ATA_SR_BSY)};
    FlushTrace timeoutTrace;
    result = execute_cache_flush(
        make_device(), ATA_CMD_CACHE_FLUSH, expired, 5, 500000, 500000000,
        &timeoutTrace);
    check(result == kernel::block::BLOCK_ERR_TIMEOUT, "PIT deadline expires bounded wait");
    check(timeoutTrace.timeoutSource == FlushTimeoutSource::PitDeadline,
          "deadline timeout is distinguished from poll guard");

    FakeAta noClock;
    noClock.deadline = false;
    noClock.statuses = {ATA_SR_DRDY, ATA_SR_DRDY,
                        static_cast<uint8_t>(ATA_SR_BSY),
                        static_cast<uint8_t>(ATA_SR_BSY),
                        static_cast<uint8_t>(ATA_SR_BSY)};
    FlushTrace fallbackTrace;
    result = execute_cache_flush(
        make_device(), ATA_CMD_CACHE_FLUSH, noClock, 5, 2, 100,
        &fallbackTrace);
    check(result == kernel::block::BLOCK_ERR_TIMEOUT,
          "unavailable timer uses bounded poll fallback");
    check(fallbackTrace.timeoutSource == FlushTimeoutSource::PollGuard,
          "fallback timeout is distinguished from PIT deadline");

    FakeAta stalledPit;
    stalledPit.statuses = {ATA_SR_DRDY, ATA_SR_DRDY,
                           static_cast<uint8_t>(ATA_SR_BSY),
                           static_cast<uint8_t>(ATA_SR_BSY),
                           static_cast<uint8_t>(ATA_SR_BSY)};
    FlushTrace stalledTrace;
    result = execute_cache_flush(
        make_device(), ATA_CMD_CACHE_FLUSH, stalledPit, 5, 500000, 2,
        &stalledTrace);
    check(result == kernel::block::BLOCK_ERR_TIMEOUT,
          "stalled PIT remains bounded by the emergency poll guard");
    check(stalledTrace.timeoutSource == FlushTimeoutSource::PollGuard &&
          stalledTrace.polls == 2,
          "stalled PIT reports its distinct emergency guard source");

    check(kernel::ata::pio::elapsed_at_least(UINT64_MAX - 2, 2, 5),
          "elapsed deadline comparison handles counter wrap");
}

void test_retry_after_flush_error()
{
    FakeAta io;
    io.error = 0x04;
    // The first command fails with ERR. On the next call the device still
    // exposes that completed error status, then accepts a fresh FLUSH CACHE.
    io.statuses = {ATA_SR_DRDY, ATA_SR_DRDY,
                   static_cast<uint8_t>(ATA_SR_DRDY | ATA_SR_ERR),
                   static_cast<uint8_t>(ATA_SR_DRDY | ATA_SR_ERR),
                   static_cast<uint8_t>(ATA_SR_DRDY | ATA_SR_ERR),
                   ATA_SR_DRDY};
    FlushTrace failedTrace;
    Status result = execute_cache_flush(
        make_device(), ATA_CMD_CACHE_FLUSH, io, 6000, 500000, 500000000,
        &failedTrace);
    check(result == kernel::block::BLOCK_ERR_IO, "first flush device error propagates");

    FlushTrace retryTrace;
    result = execute_cache_flush(
        make_device(), ATA_CMD_CACHE_FLUSH, io, 6000, 500000, 500000000,
        &retryTrace);
    check(result == kernel::block::BLOCK_OK,
          "subsequent flush can recover after prior ERR completion");
    check(io.commands.size() == 2, "failed command does not suppress bounded caller retry");
    check(retryTrace.errorBefore == 0x04,
          "prior error is retained while retry result is evaluated independently");
}

} // namespace

int main()
{
    test_flush_command_capability_selection();
    test_target_selection_and_status_polling();
    test_master_slave_and_controller_transitions();
    test_mismatched_device_head_is_rejected();
    test_waits_for_existing_channel_command_before_select();
    test_ignores_status_bits_while_busy();
    test_err_df_and_unexpected_completion();
    test_missing_device_and_completion();
    test_deadline_and_fallback_timeout();
    test_retry_after_flush_error();

    std::printf("ATA PIO flush regression: %d checks, %d failures\n",
                g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
