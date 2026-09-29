#include "include/kernel/qemu_dm13_usb_write_proof.h"

#if defined(GXOS_DM13_QEMU_USB_WRITE_PROOF)

#include "include/kernel/block_device.h"
#include "include/kernel/qemu_dm13_usb_lifecycle_proof.h"
#include "include/kernel/serial_debug.h"
#include "include/kernel/storage_manager.h"
#include "include/kernel/usb_storage.h"

namespace kernel {
namespace qemu_dm13_usb_write_proof {
namespace {

static const uint64_t kTestLba = 32768;
static const uint32_t kMaxSectors = 128;
alignas(4096) static uint8_t s_before[(kMaxSectors + 2) * 512];
alignas(4096) static uint8_t s_after[(kMaxSectors + 2) * 512];
alignas(4096) static uint8_t s_pattern[kMaxSectors * 512];

static bool read_group(uint8_t index, uint32_t count, uint8_t* out)
{
    return block::read_sectors_checked(index, kTestLba - 1, 1,
               out, 512) == block::BLOCK_OK &&
        block::read_sectors_checked(index, kTestLba, count, out + 512,
               static_cast<size_t>(count) * 512) == block::BLOCK_OK &&
        block::read_sectors_checked(index, kTestLba + count, 1,
               out + static_cast<size_t>(count + 1) * 512, 512) == block::BLOCK_OK;
}

static bool restore(uint8_t driverIndex, uint8_t blockIndex, uint32_t count)
{
    const usb::TransferStatus written = usb_storage::write_sectors(
        driverIndex, kTestLba, count, s_before + 512);
    const usb::TransferStatus synced = usb_storage::synchronize_cache(driverIndex);
    const bool readOk = read_group(blockIndex, count, s_after);
    const size_t bytes = static_cast<size_t>(count + 2) * 512;
    bool same = readOk;
    for (size_t i = 0; same && i < bytes; ++i)
        same = s_after[i] == s_before[i];
    return written == usb::XFER_SUCCESS && synced == usb::XFER_SUCCESS && same;
}

static bool cycle(uint8_t blockIndex, uint8_t driverIndex, uint32_t count,
                  uint32_t seed)
{
    if (count == 0 || count > kMaxSectors || !read_group(blockIndex, count, s_before))
        return false;
    const size_t dataBytes = static_cast<size_t>(count) * 512;
    for (size_t i = 0; i < dataBytes; ++i)
        s_pattern[i] = static_cast<uint8_t>((i * 37u + seed * 19u + (i >> 8)) & 0xFFu);

    const usb::TransferStatus writeStatus = usb_storage::write_sectors(
        driverIndex, kTestLba, count, s_pattern);
    const bool commandCompleted = writeStatus == usb::XFER_SUCCESS;
    const usb::TransferStatus syncStatus = commandCompleted
        ? usb_storage::synchronize_cache(driverIndex) : usb::XFER_ERROR;
    const bool afterSyncRead = commandCompleted && read_group(
        blockIndex, count, s_after);
    bool patternMatches = afterSyncRead;
    for (size_t i = 0; patternMatches && i < dataBytes; ++i)
        patternMatches = s_after[512 + i] == s_pattern[i];
    bool canariesMatch = afterSyncRead;
    const size_t finalOffset = static_cast<size_t>(count + 1) * 512;
    for (size_t i = 0; canariesMatch && i < 512; ++i)
        canariesMatch = s_after[i] == s_before[i] &&
                        s_after[finalOffset + i] == s_before[finalOffset + i];

    const bool restored = restore(driverIndex, blockIndex, count);
    return commandCompleted && syncStatus == usb::XFER_SUCCESS &&
           patternMatches && canariesMatch && restored;
}

static bool shared_callback_cycle(uint8_t blockIndex)
{
    alignas(4096) uint8_t original[512] = {};
    alignas(4096) uint8_t observed[512] = {};
    alignas(4096) uint8_t replacement[512] = {};
    const uint64_t lba = kTestLba + 256;
    if (block::read_sectors_checked(blockIndex, lba, 1, original,
                                    sizeof(original)) != block::BLOCK_OK)
        return false;
    for (size_t i = 0; i < sizeof(replacement); ++i)
        replacement[i] = static_cast<uint8_t>((i * 13u + 0x5Au) & 0xFFu);
    const block::Status writeStatus = block::write_sectors_checked(
        blockIndex, lba, 1, replacement, sizeof(replacement));
    const block::FlushReport flush = block::flush_with_result(blockIndex);
    const bool readOk = block::read_sectors_checked(blockIndex, lba, 1,
        observed, sizeof(observed)) == block::BLOCK_OK;
    bool replacementMatches = readOk;
    for (size_t i = 0; replacementMatches && i < sizeof(observed); ++i)
        replacementMatches = observed[i] == replacement[i];

    const block::Status restoreStatus = block::write_sectors_checked(
        blockIndex, lba, 1, original, sizeof(original));
    const block::FlushReport restoreFlush = block::flush_with_result(blockIndex);
    const bool restoreReadOk = block::read_sectors_checked(blockIndex, lba, 1,
        observed, sizeof(observed)) == block::BLOCK_OK;
    bool originalMatches = restoreReadOk;
    for (size_t i = 0; originalMatches && i < sizeof(observed); ++i)
        originalMatches = observed[i] == original[i];
    return writeStatus == block::BLOCK_OK && flush.status == block::BLOCK_OK &&
        flush.semanticsKnown && replacementMatches &&
        restoreStatus == block::BLOCK_OK && restoreFlush.status == block::BLOCK_OK &&
        restoreFlush.semanticsKnown && originalMatches;
}

static bool write_protect_cycle(uint8_t blockIndex, uint8_t driverIndex,
                                const block::BlockDevice& device,
                                const usb_storage::StorageDevice& transport)
{
    alignas(4096) uint8_t original[512] = {};
    alignas(4096) uint8_t observed[512] = {};
    alignas(4096) uint8_t replacement[512] = {};
    if (!transport.writeProtected || device.writeFn ||
        block::read_sectors_checked(blockIndex, kTestLba, 1, original,
                                    sizeof(original)) != block::BLOCK_OK)
        return false;
    for (size_t i = 0; i < sizeof(replacement); ++i)
        replacement[i] = static_cast<uint8_t>((i * 29u + 0xA7u) & 0xFFu);
    const usb::TransferStatus privateWrite = usb_storage::write_sectors(
        driverIndex, kTestLba, 1, replacement);
    const block::Status sharedWrite = block::write_sectors_checked(
        blockIndex, kTestLba, 1, replacement, sizeof(replacement));
    const block::Status readStatus = block::read_sectors_checked(
        blockIndex, kTestLba, 1, observed, sizeof(observed));
    bool unchanged = readStatus == block::BLOCK_OK;
    for (size_t i = 0; unchanged && i < sizeof(observed); ++i)
        unchanged = observed[i] == original[i];
    return privateWrite == usb::XFER_READ_ONLY &&
        sharedWrite == block::BLOCK_ERR_UNSUPPORTED && unchanged;
}

static void print_result(const char* label, bool passed)
{
    serial::puts("[DM13-QEMU-USB] ");
    serial::puts(label);
    serial::puts(passed ? "=PASS\n" : "=FAIL\n");
}

} // namespace

void run()
{
    uint8_t blockIndex = 0xFFu;
    const block::BlockDevice* device = nullptr;
    for (uint8_t i = 0; i < block::MAX_BLOCK_DEVICES; ++i) {
        const block::BlockDevice* candidate = block::get_device(i);
        if (!candidate || !candidate->active ||
            candidate->type != block::BDEV_USB_MASS) continue;
        blockIndex = i;
        device = candidate;
        break;
    }
    if (!device || !device->readFn || device->sectorSize != 512 ||
        device->totalSectors <= kTestLba + 130) {
        serial::puts("[DM13-QEMU-USB] proof=FAIL reason=private-stage-device-gate\n");
        return;
    }
    const usb_storage::StorageDevice* transport =
        usb_storage::get_device(device->driverIndex);
    if (!transport) {
        serial::puts("[DM13-QEMU-USB] proof=FAIL reason=missing-usb-transport\n");
        return;
    }
    if (transport->writeProtected || !device->writeFn) {
        const bool protectedPath = write_protect_cycle(blockIndex,
            device->driverIndex, *device, *transport);
        print_result("write-protect", protectedPath);
        serial::puts(protectedPath
            ? "[DM13-QEMU-USB] write-protect=PASS reads=PASS private-write-blocked=PASS common-write-blocked=PASS\n"
            : "[DM13-QEMU-USB] write-protect=FAIL\n");
        return;
    }
    if (transport->syncCacheState != usb_storage::SYNC_CACHE_SUCCEEDED) {
        serial::puts("[DM13-QEMU-USB] proof=FAIL reason=write-protection-or-flush-gate\n");
        return;
    }

    serial::puts("[DM13-QEMU-USB] target=PASS lba=");
    serial::put_hex64(kTestLba);
    serial::puts(" sectors=1..128 common-write-callback=yes sync-cache=trusted\n");
    storage::TargetIdentity target = {};
    const bool targetCaptured = storage::capture_target_identity(blockIndex, target);
    const storage::BootProtection boot = targetCaptured
        ? storage::query_boot_protection(target)
        : storage::BootProtection{storage::BOOT_DEVICE_IDENTITY_UNKNOWN};
    serial::puts("[DM13-QEMU-USB] boot-provenance=");
    serial::put_hex8(static_cast<uint8_t>(boot.safety));
    serial::puts(" controller-pci=");
    serial::puts(device->pciLocationValid ? "valid" : "unknown");
    serial::putc('\n');

    bool passed = true;
    for (uint32_t cycleIndex = 0; cycleIndex < 100; ++cycleIndex)
        passed = cycle(blockIndex, device->driverIndex, 1, cycleIndex + 1) && passed;
    print_result("hundred-write-sync-readback-restore-cycles", passed);

    const bool two = cycle(blockIndex, device->driverIndex, 2, 0x21);
    const bool several = cycle(blockIndex, device->driverIndex, 7, 0x37);
    const bool boundary = cycle(blockIndex, device->driverIndex, 128, 0x80);
    print_result("two-sector-write-and-canaries", two);
    print_result("seven-sector-write-and-canaries", several);
    print_result("64k-boundary-write-and-canaries", boundary);
    passed = passed && two && several && boundary;

    serial::puts(passed
        ? "[DM13-QEMU-USB] proof=PASS private-write=PASS sync-cache=PASS readback=PASS adjacent-canaries=PASS restored=PASS\n"
        : "[DM13-QEMU-USB] proof=FAIL private-write-or-restore-failed\n");
    const bool commonWrite = passed && shared_callback_cycle(blockIndex);
    print_result("common-write-callback-readback-restore", commonWrite);
#if defined(GXOS_DM13_QEMU_USB_LIFECYCLE_PROOF)
    if (commonWrite) qemu_dm13_usb_lifecycle_proof::run();
#endif
}

} // namespace qemu_dm13_usb_write_proof
} // namespace kernel

#endif
