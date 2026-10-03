#ifndef KERNEL_AHCI_H
#define KERNEL_AHCI_H

#include "kernel/types.h"
#include "kernel/block_device.h"

namespace kernel {
namespace ahci {

struct DeviceInfo {
    bool active;
    uint8_t driverIndex;
    uint8_t port;
    uint8_t pciBus;
    uint8_t pciDevice;
    uint8_t pciFunction;
    uint16_t vendorId;
    uint16_t deviceId;
    uint64_t abarPhysical;
    uint32_t capabilities;
    uint32_t version;
    bool lba48;
    bool flushCache;
    bool flushCacheExt;
    uint64_t totalSectors;
    uint32_t sectorSize;
    uint32_t physicalSectorSize; // zero when IDENTIFY geometry is unavailable
    char model[41];
    char serial[21];
    char firmware[9];
};

// The current x86/AMD64 implementation uses PCI configuration mechanism 1,
// uncached MMIO, and one non-NCQ command at a time per global AHCI lock.
void set_kernel_physical_base(uint64_t physicalBase);
void init();
uint8_t device_count();
const DeviceInfo* get_device(uint8_t index);

#if defined(GXOS_DM15_QEMU_AHCI_PROOF)
// Proof-only access to the production read/write/flush commands while normal
// shared write registration is still gated off during the first QEMU proof.
block::Status proof_write(uint8_t driverIndex, uint64_t lba,
                          uint32_t count, const void* buffer);
block::Status proof_flush(uint8_t driverIndex);
#endif

} // namespace ahci
} // namespace kernel

#endif
