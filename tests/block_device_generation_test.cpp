#include "kernel/block_device.h"

#include <iostream>

namespace {
int checks = 0;
int failures = 0;

void check(bool condition, const char* name)
{
    ++checks;
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << name << "\n";
    }
}

kernel::block::BlockDevice makeDevice(uint8_t driverIndex, const char* name)
{
    kernel::block::BlockDevice device{};
    device.active = true;
    device.type = kernel::block::BDEV_ATA_PIO;
    device.driverIndex = driverIndex;
    device.totalSectors = 4096;
    device.sectorSize = 512;
    size_t i = 0;
    while (name[i] && i + 1 < sizeof(device.name)) {
        device.name[i] = name[i];
        ++i;
    }
    device.name[i] = '\0';
    return device;
}
}

int main()
{
    using namespace kernel::block;
    init();
    check(device_generation(0) == 0 && get_device(0) == nullptr,
        "empty registry slot has no live generation");

    const uint8_t first = register_device(makeDevice(0, "disk0"));
    const uint64_t firstGeneration = device_generation(first);
    check(first == 0 && firstGeneration != 0 && get_device(first) != nullptr,
        "registering a block device creates a nonzero slot generation");

    unregister_device(first);
    check(device_generation(first) == 0 && get_device(first) == nullptr,
        "unregister removes the live slot identity");

    const uint8_t reused = register_device(makeDevice(1, "disk1"));
    const uint64_t reusedGeneration = device_generation(reused);
    check(reused == first && reusedGeneration != firstGeneration,
        "reusing a registry slot cannot reuse the previous device generation");

    init();
    check(device_generation(reused) == 0 && get_device(reused) == nullptr,
        "registry reinitialization invalidates previously active slot identities");

    std::cout << "Block device generation tests: " << (checks - failures) << "/" << checks << " passed\n";
    return failures == 0 ? 0 : 1;
}
