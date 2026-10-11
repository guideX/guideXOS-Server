#include "kernel/ata.h"
#include "kernel/ata_flush.h"

#include <cstddef>
#include <iostream>
#include <string>
#include <vector>

static_assert(sizeof(kernel::ata::IdentifyData) == 512,
              "IDENTIFY response must stay exactly one sector");
static_assert(offsetof(kernel::ata::IdentifyData, commandSets82) == 164,
              "IDENTIFY word 82 offset changed");
static_assert(offsetof(kernel::ata::IdentifyData, commandSets83) == 166,
              "IDENTIFY word 83 offset changed");

namespace {
using namespace kernel::ata::flush_detail;

int checks = 0;
int failures = 0;

void check(bool condition, const char* name)
{
    ++checks;
    if (condition) {
        std::cout << "PASS: " << name << "\n";
    } else {
        ++failures;
        std::cerr << "FAIL: " << name << "\n";
    }
}

struct Rig {
    std::vector<uint8_t> statuses;
    std::vector<char> events;
    size_t nextStatus = 0;
    uint8_t selectedDrive = 0;
    uint8_t issuedCommand = 0;
    uint32_t delayCount = 0;

    uint8_t read_status()
    {
        events.push_back('R');
        if (nextStatus >= statuses.size()) return 0xFF;
        return statuses[nextStatus++];
    }
};

FlushResult run_flush(const IdentifyCapabilities& capabilities, bool isMaster,
                      uint32_t timeout, Rig& rig)
{
    return issue_cache_flush(
        capabilities, isMaster, timeout,
        [&rig](uint8_t value) {
            rig.selectedDrive = value;
            rig.events.push_back('S');
        },
        [&rig]() {
            ++rig.delayCount;
            rig.events.push_back('D');
        },
        [&rig]() { return rig.read_status(); },
        [&rig](uint8_t command) {
            rig.issuedCommand = command;
            rig.events.push_back('C');
        });
}
}

int main()
{
    const IdentifyCapabilities lba48Basic = decode_identify_capabilities(0x0020, 0x4400);
    const IdentifyCapabilities lba48Ext = decode_identify_capabilities(0x0000, 0x5400);
    const IdentifyCapabilities basicOnly = decode_identify_capabilities(0x0020, 0x4000);
    const IdentifyCapabilities extWithoutLba48 = decode_identify_capabilities(0x0020, 0x5000);
    const IdentifyCapabilities unsupported = decode_identify_capabilities(0x0000, 0x1000);
    uint8_t command = 0;

    check(lba48Basic.lba48 && lba48Basic.flushCache && !lba48Basic.flushCacheExt &&
        choose_flush_command(lba48Basic, &command) && command == ATA_FLUSH_CMD_CACHE,
        "IDENTIFY capability bits select FLUSH CACHE when EXT is not advertised");
    check(lba48Ext.lba48 && lba48Ext.flushCacheExt &&
        choose_flush_command(lba48Ext, &command) && command == ATA_FLUSH_CMD_CACHE_EXT,
        "IDENTIFY capability bits select FLUSH CACHE EXT when supported");
    check(basicOnly.flushCache && !basicOnly.lba48 &&
        choose_flush_command(basicOnly, &command) && command == ATA_FLUSH_CMD_CACHE,
        "28-bit device selects FLUSH CACHE");
    check(extWithoutLba48.flushCacheExt && !extWithoutLba48.lba48 &&
        choose_flush_command(extWithoutLba48, &command) && command == ATA_FLUSH_CMD_CACHE,
        "EXT bit without LBA48 never blindly selects FLUSH CACHE EXT");
    check(!choose_flush_command(unsupported, &command),
        "device without an advertised flush command is rejected");

    Rig normal;
    normal.statuses = { 0x01, 0x80, 0x40 };
    FlushResult result = run_flush(lba48Ext, false, 3, normal);
    check(result.code == FLUSH_SUCCESS && result.commandIssued &&
        result.command == ATA_FLUSH_CMD_CACHE_EXT &&
        to_block_status(result.code) == kernel::block::BLOCK_OK,
        "completed flush succeeds and maps to block success");
    check(normal.selectedDrive == 0xB0 && normal.issuedCommand == ATA_FLUSH_CMD_CACHE_EXT,
        "slave drive and EXT command are issued to the intended target");
    check(normal.events.size() == 7 && normal.events[0] == 'S' &&
        normal.events[1] == 'D' && normal.events[2] == 'R' &&
        normal.events[3] == 'C' && normal.events[4] == 'D' &&
        normal.events[5] == 'R' && normal.events[6] == 'R',
        "selection, pre-command readiness, command issue, delay, and completion polling are ordered");
    check(normal.delayCount == 2,
        "400ns selection and post-command delays both occur");

    Rig master;
    master.statuses = { 0x40, 0x40 };
    result = run_flush(basicOnly, true, 2, master);
    check(result.code == FLUSH_SUCCESS && master.selectedDrive == 0xA0 &&
        master.issuedCommand == ATA_FLUSH_CMD_CACHE,
        "master drive uses the non-EXT command when only basic flush is supported");

    Rig ataError;
    ataError.statuses = { 0x40, 0x01 };
    result = run_flush(basicOnly, true, 2, ataError);
    check(result.code == FLUSH_ATA_ERR && result.status == 0x01 &&
        result.commandIssued && to_block_status(result.code) == kernel::block::BLOCK_ERR_IO,
        "completion ERR fails and maps to block I/O error");

    Rig deviceFault;
    deviceFault.statuses = { 0x40, 0x20 };
    result = run_flush(basicOnly, true, 2, deviceFault);
    check(result.code == FLUSH_DEVICE_FAULT && result.status == 0x20 &&
        to_block_status(result.code) == kernel::block::BLOCK_ERR_IO,
        "completion DF fails and maps to block I/O error");

    Rig bothErrors;
    bothErrors.statuses = { 0x40, 0x21 };
    result = run_flush(basicOnly, true, 2, bothErrors);
    check(result.code == FLUSH_ATA_ERR_AND_DEVICE_FAULT && result.status == 0x21,
        "completion with ERR and DF is distinguished and rejected");

    Rig timeout;
    timeout.statuses = { 0x40, 0x80, 0x80 };
    result = run_flush(basicOnly, true, 2, timeout);
    check(result.code == FLUSH_TIMEOUT && result.status == 0x80 && result.commandIssued &&
        to_block_status(result.code) == kernel::block::BLOCK_ERR_TIMEOUT,
        "BSY timeout fails and maps to block timeout");

    Rig finalAllowedSample;
    finalAllowedSample.statuses = { 0x40, 0x80, 0x40 };
    result = run_flush(basicOnly, true, 2, finalAllowedSample);
    check(result.code == FLUSH_SUCCESS && finalAllowedSample.nextStatus == 3,
        "completion on the last status sample inside the timeout is accepted");

    Rig justAfterTimeout;
    justAfterTimeout.statuses = { 0x40, 0x80, 0x80, 0x40 };
    result = run_flush(basicOnly, true, 2, justAfterTimeout);
    check(result.code == FLUSH_TIMEOUT && justAfterTimeout.nextStatus == 3,
        "completion after the timeout boundary is rejected without an extra poll");

    Rig readyTimeout;
    readyTimeout.statuses = { 0x80, 0x80 };
    result = run_flush(basicOnly, true, 2, readyTimeout);
    check(result.code == FLUSH_TIMEOUT && !result.commandIssued &&
        readyTimeout.issuedCommand == 0,
        "busy device before command times out without issuing the flush");

    Rig unsupportedRig;
    result = run_flush(unsupported, true, 2, unsupportedRig);
    check(result.code == FLUSH_UNSUPPORTED && !result.commandIssued &&
        unsupportedRig.events.empty() &&
        to_block_status(result.code) == kernel::block::BLOCK_ERR_UNSUPPORTED,
        "unsupported flush returns failure without touching ATA registers");

    Rig staleStatus;
    staleStatus.statuses = { 0x21, 0x40 };
    result = run_flush(basicOnly, true, 1, staleStatus);
    check(result.code == FLUSH_SUCCESS && result.commandIssued && result.status == 0x40,
        "stale pre-command ERR/DF bits are ignored and completion status is read after issue");

    Rig noDevice;
    noDevice.statuses = { 0x00 };
    result = run_flush(basicOnly, true, 2, noDevice);
    check(result.code == FLUSH_TRANSPORT_ERROR && !result.commandIssued &&
        to_block_status(result.code) == kernel::block::BLOCK_ERR_IO,
        "missing device status fails as a transport error");

    Rig unexpectedDrq;
    unexpectedDrq.statuses = { 0x40, 0x48 };
    result = run_flush(basicOnly, true, 2, unexpectedDrq);
    check(result.code == FLUSH_PROTOCOL_ERROR &&
        "unexpected DRQ" == std::string(result_name(result.code)),
        "unexpected completion DRQ is rejected as a protocol failure");

    std::cout << "ATA flush status checks=" << (checks - failures) << "/" << checks << "\n";
    return failures == 0 ? 0 : 1;
}
