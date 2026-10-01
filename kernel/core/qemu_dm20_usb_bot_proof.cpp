#include "include/kernel/qemu_dm20_usb_bot_proof.h"

#if defined(GXOS_DM20_USB_BOT_STRESS_PROOF)

#include "include/kernel/block_device.h"
#include "include/kernel/serial_debug.h"
#include "include/kernel/usb_storage.h"

namespace kernel {
namespace qemu_dm20_usb_bot_proof {
namespace {

#if defined(GXOS_DM20_BOT_STRESS_COMMANDS)
static const uint32_t kStressCommands = GXOS_DM20_BOT_STRESS_COMMANDS;
#else
static const uint32_t kStressCommands = 1000;
#endif
static const uint32_t kSequenceRepeats = 10;
static const uint64_t kSingleLba = 32768;
static const uint64_t kLargeLba = 33024;
static const uint32_t kLargeSectors = 128;
static const uint32_t kLargeGuardedSectors = kLargeSectors + 2;

alignas(4096) static uint8_t s_singleBefore[512];
alignas(4096) static uint8_t s_singlePattern[512];
alignas(4096) static uint8_t s_singleReadback[512];
alignas(4096) static uint8_t s_largeBefore[kLargeGuardedSectors * 512];
alignas(4096) static uint8_t s_largePattern[kLargeSectors * 512];
alignas(4096) static uint8_t s_largeReadback[kLargeGuardedSectors * 512];
static uint32_t s_commandsByOpcode[7];

static bool equal_bytes(const uint8_t* a, const uint8_t* b, size_t length)
{
    for (size_t i = 0; i < length; ++i)
        if (a[i] != b[i]) return false;
    return true;
}

static void fill_pattern(uint8_t* bytes, size_t length, uint32_t seed)
{
    for (size_t i = 0; i < length; ++i)
        bytes[i] = static_cast<uint8_t>(
            (i * 37u + seed * 19u + (i >> 8)) & 0xFFu);
}

static uint8_t opcode_slot(uint8_t opcode)
{
    switch (opcode) {
        case usb_storage::SCSI_TEST_UNIT_READY: return 0;
        case usb_storage::SCSI_INQUIRY: return 1;
        case usb_storage::SCSI_READ_CAPACITY_10: return 2;
        case usb_storage::SCSI_READ_10: return 3;
        case usb_storage::SCSI_WRITE_10: return 4;
        case usb_storage::SCSI_SYNCHRONIZE_CACHE_10: return 5;
        case usb_storage::SCSI_REQUEST_SENSE: return 6;
        default: return 0xFFu;
    }
}

static bool issue(uint8_t driverIndex, uint8_t opcode, uint64_t lba,
                  void* buffer, usb::TransferStatus expected,
                  const char* sequenceName)
{
    const usb_storage::StorageDevice* before =
        usb_storage::get_device(driverIndex);
    const uint64_t sequenceBefore = before ? before->commandSequence : 0;
    const usb::TransferStatus status = usb_storage::test_run_bot_opcode(
        driverIndex, opcode, lba, buffer);
    const usb_storage::StorageDevice* after =
        usb_storage::get_device(driverIndex);
    if (after && after->commandSequence >= sequenceBefore) {
        const uint64_t delta = after->commandSequence - sequenceBefore;
        const uint8_t slot = opcode_slot(opcode);
        if (slot != 0xFFu) s_commandsByOpcode[slot] +=
            static_cast<uint32_t>(delta > 0 ? delta : 0);
    }
    if (status == expected && after) return true;

    serial::puts("[DM20-QEMU-USB-BOT] command-failure pattern=");
    serial::puts(sequenceName);
    serial::puts(" opcode=0x");
    serial::put_hex8(opcode);
    serial::puts(" status=0x");
    serial::put_hex8(static_cast<uint8_t>(status));
    serial::puts(" sequence=BOT#");
    serial::put_hex64(after ? after->commandSequence : 0);
    serial::puts(" tag=0x");
    serial::put_hex32(after ? after->botDiagnostic.cbwTag : 0);
    serial::putc('\n');
    return false;
}

static bool restore_and_verify(uint8_t driverIndex)
{
    const usb::TransferStatus restoreSingle = usb_storage::write_sectors(
        driverIndex, kSingleLba, 1, s_singleBefore);
    const usb::TransferStatus flushSingle = usb_storage::synchronize_cache(
        driverIndex);
    const usb::TransferStatus restoreLarge = usb_storage::write_sectors(
        driverIndex, kLargeLba, kLargeSectors, s_largeBefore + 512);
    const usb::TransferStatus flushLarge = usb_storage::synchronize_cache(
        driverIndex);
    const usb::TransferStatus readSingle = usb_storage::read_sectors(
        driverIndex, kSingleLba, 1, s_singleReadback);
    const usb::TransferStatus readLarge = usb_storage::read_sectors(
        driverIndex, kLargeLba - 1, kLargeGuardedSectors, s_largeReadback);
    return restoreSingle == usb::XFER_SUCCESS &&
        flushSingle == usb::XFER_SUCCESS &&
        restoreLarge == usb::XFER_SUCCESS &&
        flushLarge == usb::XFER_SUCCESS &&
        readSingle == usb::XFER_SUCCESS && readLarge == usb::XFER_SUCCESS &&
        equal_bytes(s_singleBefore, s_singleReadback, sizeof(s_singleBefore)) &&
        equal_bytes(s_largeBefore, s_largeReadback,
                    sizeof(s_largeBefore));
}

} // namespace

void run()
{
    uint8_t blockIndex = 0xFFu;
    const block::BlockDevice* blockDevice = nullptr;
    for (uint8_t i = 0; i < block::MAX_BLOCK_DEVICES; ++i) {
        const block::BlockDevice* candidate = block::get_device(i);
        if (candidate && candidate->active &&
            candidate->type == block::BDEV_USB_MASS) {
            blockIndex = i;
            blockDevice = candidate;
            break;
        }
    }
    if (!blockDevice || blockIndex == 0xFFu || !blockDevice->readFn ||
        !blockDevice->writeFn || blockDevice->sectorSize != 512 ||
        blockDevice->totalSectors <= kLargeLba + kLargeSectors) {
        serial::puts("[DM20-QEMU-USB-BOT] stress=FAIL reason=device-gate\n");
        return;
    }
    const uint8_t driverIndex = blockDevice->driverIndex;
    const usb_storage::StorageDevice* transport =
        usb_storage::get_device(driverIndex);
    if (!transport || transport->syncCacheState !=
            usb_storage::SYNC_CACHE_SUCCEEDED) {
        serial::puts("[DM20-QEMU-USB-BOT] stress=FAIL reason=trusted-flush-gate\n");
        return;
    }

    if (usb_storage::read_sectors(driverIndex, kSingleLba, 1,
            s_singleBefore) != usb::XFER_SUCCESS ||
        usb_storage::read_sectors(driverIndex, kLargeLba - 1,
            kLargeGuardedSectors, s_largeBefore) != usb::XFER_SUCCESS) {
        serial::puts("[DM20-QEMU-USB-BOT] stress=FAIL reason=backup-read\n");
        return;
    }

    fill_pattern(s_singlePattern, sizeof(s_singlePattern), 0x20u);
    fill_pattern(s_largePattern, sizeof(s_largePattern), 0x80u);
    const uint8_t opcodes[7] = {
        usb_storage::SCSI_TEST_UNIT_READY,
        usb_storage::SCSI_INQUIRY,
        usb_storage::SCSI_READ_CAPACITY_10,
        usb_storage::SCSI_READ_10,
        usb_storage::SCSI_WRITE_10,
        usb_storage::SCSI_SYNCHRONIZE_CACHE_10,
        usb_storage::SCSI_REQUEST_SENSE,
    };

    serial::puts("[DM20-QEMU-USB-BOT] minimal-reproducer=production-uhci-bot-scsI start\n");
    const uint64_t firstSequence = transport->commandSequence + 1;
    for (uint32_t commandIndex = 0; commandIndex < kStressCommands;
         ++commandIndex) {
        const uint8_t opcode = opcodes[commandIndex % 7u];
        void* buffer = nullptr;
        uint64_t lba = kSingleLba;
        if (opcode == usb_storage::SCSI_READ_10)
            buffer = s_singleReadback;
        else if (opcode == usb_storage::SCSI_WRITE_10)
            buffer = s_singlePattern;
        if (!issue(driverIndex, opcode, lba, buffer, usb::XFER_SUCCESS,
                   "opcode-stress"))
            return;
        if ((commandIndex + 1u) % 100u == 0) {
            const usb_storage::StorageDevice* current =
                usb_storage::get_device(driverIndex);
            serial::puts("[DM20-QEMU-USB-BOT] progress commands=");
            serial::put_hex32(commandIndex + 1u);
            serial::puts(" sequence=BOT#");
            serial::put_hex64(current ? current->commandSequence : 0);
            serial::puts(" tag=0x");
            serial::put_hex32(current ? current->botDiagnostic.cbwTag : 0);
            serial::putc('\n');
        }
    }

    bool sequencesPassed = true;
    for (uint32_t repeat = 0; repeat < kSequenceRepeats && sequencesPassed;
         ++repeat) {
        // A: WRITE(10) and consume its CSW.
        fill_pattern(s_singlePattern, sizeof(s_singlePattern), 0xA0u + repeat);
        sequencesPassed = issue(driverIndex, usb_storage::SCSI_WRITE_10,
            kSingleLba, s_singlePattern, usb::XFER_SUCCESS, "A-WRITE-CSW");

        // B: WRITE(10), CSW, SYNCHRONIZE CACHE, CSW.
        if (sequencesPassed) sequencesPassed = issue(driverIndex,
            usb_storage::SCSI_WRITE_10, kSingleLba, s_singlePattern,
            usb::XFER_SUCCESS, "B-WRITE-CSW");
        if (sequencesPassed) sequencesPassed = issue(driverIndex,
            usb_storage::SCSI_SYNCHRONIZE_CACHE_10, kSingleLba, nullptr,
            usb::XFER_SUCCESS, "B-SYNC-CSW");

        // C: READ(10), CSW, WRITE(10), CSW.
        if (sequencesPassed) sequencesPassed = issue(driverIndex,
            usb_storage::SCSI_READ_10, kSingleLba, s_singleReadback,
            usb::XFER_SUCCESS, "C-READ-CSW");
        if (sequencesPassed) sequencesPassed = issue(driverIndex,
            usb_storage::SCSI_WRITE_10, kSingleLba, s_singlePattern,
            usb::XFER_SUCCESS, "C-WRITE-CSW");

        // D: an illegal CDB must complete with a failed CSW, request sense,
        // then permit the next command to run with a fresh tag and CSW.
        if (sequencesPassed) sequencesPassed = issue(driverIndex, 0xFFu,
            kSingleLba, nullptr, usb::XFER_STALL, "D-FAILED-REQUEST-SENSE");
        const usb_storage::StorageDevice* afterSense =
            usb_storage::get_device(driverIndex);
        sequencesPassed = sequencesPassed && afterSense &&
            afterSense->lastSenseValid;
        if (sequencesPassed) sequencesPassed = issue(driverIndex,
            usb_storage::SCSI_TEST_UNIT_READY, kSingleLba, nullptr,
            usb::XFER_SUCCESS, "D-NEXT-COMMAND");
    }

    // E: a 64 KiB WRITE(10) spanning many production UHCI TD batches, then a
    // READ(10) and byte-for-byte comparison before restoring the original.
    for (uint32_t repeat = 0; repeat < kSequenceRepeats && sequencesPassed;
         ++repeat) {
        fill_pattern(s_largePattern, sizeof(s_largePattern), 0xE0u + repeat);
        const usb::TransferStatus writeStatus = usb_storage::write_sectors(
            driverIndex, kLargeLba, kLargeSectors, s_largePattern);
        const usb::TransferStatus readStatus = writeStatus == usb::XFER_SUCCESS
            ? usb_storage::read_sectors(driverIndex, kLargeLba,
                kLargeSectors, s_largeReadback) : usb::XFER_ERROR;
        sequencesPassed = writeStatus == usb::XFER_SUCCESS &&
            readStatus == usb::XFER_SUCCESS &&
            equal_bytes(s_largePattern, s_largeReadback,
                        sizeof(s_largePattern));
        if (!sequencesPassed) {
            serial::puts("[DM20-QEMU-USB-BOT] command-failure pattern=E-LARGE-WRITE-READ\n");
            return;
        }
    }

    const usb_storage::StorageDevice* finalTransport =
        usb_storage::get_device(driverIndex);
    const uint64_t completedCommands = finalTransport &&
        finalTransport->commandSequence >= firstSequence
            ? finalTransport->commandSequence - firstSequence + 1u : 0;
    if (!sequencesPassed || !restore_and_verify(driverIndex)) {
        serial::puts("[DM20-QEMU-USB-BOT] stress=FAIL reason=sequence-or-restore\n");
        return;
    }

    serial::puts("[DM20-QEMU-USB-BOT] opcode-count TUR=");
    serial::put_hex32(s_commandsByOpcode[0]);
    serial::puts(" INQUIRY=");
    serial::put_hex32(s_commandsByOpcode[1]);
    serial::puts(" READ-CAPACITY=");
    serial::put_hex32(s_commandsByOpcode[2]);
    serial::puts(" READ10=");
    serial::put_hex32(s_commandsByOpcode[3]);
    serial::puts(" WRITE10=");
    serial::put_hex32(s_commandsByOpcode[4]);
    serial::puts(" SYNC-CACHE=");
    serial::put_hex32(s_commandsByOpcode[5]);
    serial::puts(" REQUEST-SENSE=");
    serial::put_hex32(s_commandsByOpcode[6]);
    serial::putc('\n');
    serial::puts("[DM20-QEMU-USB-BOT] stress=PASS commands=");
    serial::put_hex32(kStressCommands);
    serial::puts(" total-bot-sequence-delta=");
    serial::put_hex64(completedCommands);
    serial::puts(" sequences-A-through-E=PASS repeated=0x");
    serial::put_hex32(kSequenceRepeats);
    serial::puts(" restored=yes readback=yes first-sequence=BOT#");
    serial::put_hex64(firstSequence);
    serial::puts(" last-sequence=BOT#");
    serial::put_hex64(finalTransport ? finalTransport->commandSequence : 0);
    serial::putc('\n');
}

} // namespace qemu_dm20_usb_bot_proof
} // namespace kernel

#endif
