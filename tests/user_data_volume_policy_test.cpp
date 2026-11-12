#include "../kernel/core/include/kernel/user_data_volume_policy.h"

#include <stdio.h>
#include <string.h>

using namespace kernel;
using namespace kernel::user_data_volume::policy;

static block::Status read_sector(uint8_t, uint64_t, uint32_t, void*) { return block::BLOCK_OK; }
static block::Status write_sector(uint8_t, uint64_t, uint32_t, const void*) { return block::BLOCK_OK; }
static block::Status flush_device(uint8_t) { return block::BLOCK_OK; }

static unsigned int failures = 0;

static void expect(bool condition, const char* name)
{
    if (condition) {
        printf("PASS %s\n", name);
    } else {
        printf("FAIL %s\n", name);
        ++failures;
    }
}

static block::BlockDevice valid_data_device()
{
    block::BlockDevice device{};
    device.active = true;
    device.type = block::BDEV_ATA_PIO;
    device.totalSectors = 1024u * 1024u;
    device.sectorSize = 512u;
    strcpy(device.name, "ata1s");
    strcpy(device.stableId, kRequiredStableId);
    device.readFn = read_sector;
    device.writeFn = write_sector;
    device.flushFn = flush_device;
    return device;
}

int main()
{
    block::BlockDevice trusted{};
    trusted.active = true;
    trusted.type = block::BDEV_RAMDISK;
    strcpy(trusted.stableId, "RELEASE-IMAGE");
    block::BlockDevice unrelated = valid_data_device();
    strcpy(unrelated.stableId, "OTHER-DISK");
    block::BlockDevice data = valid_data_device();

    const block::BlockDevice* devices[] = { &trusted, &unrelated, &data };
    Selection selected = select_device(devices, 3u, 0u);
    expect(selected.status == SELECT_OK && selected.deviceIndex == 2u,
           "serial identity selects the intended device independent of its slot");

    selected = select_device(devices, 2u, 0u);
    expect(selected.status == SELECT_MISSING && selected.deviceIndex == 0xFFu,
           "missing identity does not fall back to another writable disk");

    const block::BlockDevice* duplicateDevices[] = { &trusted, &data, &data };
    selected = select_device(duplicateDevices, 3u, 0u);
    expect(selected.status == SELECT_AMBIGUOUS,
           "duplicate stable identities fail closed");

    selected = select_device(devices, 3u, 2u);
    expect(selected.status == SELECT_SYSTEM_SOURCE,
           "trusted system backing device cannot also be selected as user data");

    block::BlockDevice readOnly = valid_data_device();
    readOnly.writeFn = nullptr;
    const block::BlockDevice* readOnlyDevices[] = { &trusted, &readOnly };
    selected = select_device(readOnlyDevices, 2u, 0u);
    expect(selected.status == SELECT_READ_ONLY,
           "read-only matching device is reported without attempting writes");

    block::BlockDevice noFlush = valid_data_device();
    noFlush.flushFn = nullptr;
    const block::BlockDevice* noFlushDevices[] = { &trusted, &noFlush };
    selected = select_device(noFlushDevices, 2u, 0u);
    expect(selected.status == SELECT_NO_FLUSH,
           "device without cache-flush support is rejected");

    block::BlockDevice unsupported = valid_data_device();
    unsupported.type = block::BDEV_AHCI;
    const block::BlockDevice* unsupportedDevices[] = { &trusted, &unsupported };
    selected = select_device(unsupportedDevices, 2u, 0u);
    expect(selected.status == SELECT_UNSUPPORTED_DEVICE,
           "unqualified controller type is rejected by the preview contract");

    block::BlockDevice invalid = valid_data_device();
    invalid.sectorSize = 4096u;
    const block::BlockDevice* invalidDevices[] = { &trusted, &invalid };
    selected = select_device(invalidDevices, 2u, 0u);
    expect(selected.status == SELECT_INVALID_DEVICE,
           "unsupported sector geometry is rejected");

    block::BlockDevice inactive = valid_data_device();
    inactive.active = false;
    const block::BlockDevice* inactiveDevices[] = { &trusted, &inactive };
    selected = select_device(inactiveDevices, 2u, 0u);
    expect(selected.status == SELECT_MISSING,
           "stale inactive registry entries are ignored");

    printf("user_data_volume_policy_test: %s (%u failure(s))\n",
           failures == 0u ? "PASS" : "FAIL", failures);
    return failures == 0u ? 0 : 1;
}
