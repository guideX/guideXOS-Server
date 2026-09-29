#ifndef KERNEL_AHCI_LOGIC_H
#define KERNEL_AHCI_LOGIC_H

#include "kernel/block_device.h"

namespace kernel {
namespace ahci {
namespace logic {

enum PortState : uint8_t {
    PORT_NOT_IMPLEMENTED = 0,
    PORT_NO_DEVICE,
    PORT_LINK_ERROR,
    PORT_POWERED_DOWN,
    PORT_LINK_INACTIVE,
    PORT_SATA_DISK,
    PORT_SATAPI,
    PORT_SEMB,
    PORT_MULTIPLIER,
    PORT_UNSUPPORTED_SIGNATURE,
};

struct Prdt {
    uint32_t addressLow;
    uint32_t addressHigh;
    uint32_t reserved;
    uint32_t byteCountAndInterrupt;
};

struct Fis {
    uint8_t bytes[20];
};

inline bool is_ahci_pci(uint8_t baseClass, uint8_t subClass,
                        uint8_t programmingInterface)
{
    return baseClass == 0x01u && subClass == 0x06u &&
           programmingInterface == 0x01u;
}

// AHCI specifies ABAR in PCI BAR5. BAR5 cannot consume a following BAR, so
// this profile accepts only a 32-bit memory BAR and retains the result in the
// full-width type used by the MMIO mapper.
inline bool parse_abar_bar5(uint32_t bar, uint64_t& physicalAddress)
{
    physicalAddress = 0;
    if (bar == 0xFFFFFFFFu || (bar & 0x01u) != 0u ||
        (bar & 0x06u) != 0u) return false;
    const uint64_t base = static_cast<uint64_t>(bar & 0xFFFFFFF0u);
    if (base == 0 || (base & 0x0Fu) != 0u) return false;
    physicalAddress = base;
    return true;
}

inline PortState classify_port(bool implemented, uint32_t ssts,
                               uint32_t signature)
{
    if (!implemented) return PORT_NOT_IMPLEMENTED;
    const uint8_t det = static_cast<uint8_t>(ssts & 0x0Fu);
    const uint8_t ipm = static_cast<uint8_t>((ssts >> 8) & 0x0Fu);
    if (det == 0u) return PORT_NO_DEVICE;
    if (det == 1u || det == 4u) return PORT_LINK_ERROR;
    if (det != 3u) return PORT_LINK_INACTIVE;
    if (ipm != 1u) return PORT_POWERED_DOWN;
    if (signature == 0x00000101u) return PORT_SATA_DISK;
    if (signature == 0xEB140101u) return PORT_SATAPI;
    if (signature == 0xC33C0101u) return PORT_SEMB;
    if (signature == 0x96690101u) return PORT_MULTIPLIER;
    return PORT_UNSUPPORTED_SIGNATURE;
}

inline bool slot_is_free(uint32_t commandIssue, uint32_t sataActive,
                         uint8_t slotCount, uint8_t slot)
{
    if (slot >= slotCount || slot >= 32u) return false;
    const uint32_t bit = 1u << slot;
    return (commandIssue & bit) == 0u && (sataActive & bit) == 0u;
}

inline bool slot_bit(uint8_t slot, uint8_t slotCount, uint32_t& bit)
{
    bit = 0;
    if (slot >= slotCount || slot >= 32u) return false;
    bit = 1u << slot;
    return true;
}

inline uint16_t command_header_flags(bool write)
{
    return static_cast<uint16_t>(5u | (write ? (1u << 6) : 0u));
}

inline bool build_identify_fis(Fis& fis)
{
    for (uint32_t i = 0; i < sizeof(fis.bytes); ++i) fis.bytes[i] = 0;
    fis.bytes[0] = 0x27u; // Register H2D FIS
    fis.bytes[1] = 0x80u; // Command
    fis.bytes[2] = 0xECu; // IDENTIFY DEVICE
    return true;
}

inline bool build_flush_fis(Fis& fis, bool flushCacheSupported,
                            bool flushCacheExtSupported)
{
    if (!flushCacheSupported && !flushCacheExtSupported) return false;
    for (uint32_t i = 0; i < sizeof(fis.bytes); ++i) fis.bytes[i] = 0;
    fis.bytes[0] = 0x27u;
    fis.bytes[1] = 0x80u;
    fis.bytes[2] = flushCacheExtSupported ? 0xEAu : 0xE7u;
    fis.bytes[7] = 0x40u;
    return true;
}

inline bool build_data_fis(Fis& fis, uint64_t lba, uint32_t count,
                           uint64_t totalSectors, bool lba48, bool write)
{
    if (count == 0u || lba >= totalSectors ||
        static_cast<uint64_t>(count) > totalSectors - lba) return false;
    if (lba48) {
        if (lba >= (1ULL << 48) ||
            static_cast<uint64_t>(count) > (1ULL << 48) - lba ||
            count > 0xFFFFu) return false;
    } else if (lba >= (1ULL << 28) ||
               static_cast<uint64_t>(count) > (1ULL << 28) - lba ||
               count > 256u) {
        return false;
    }

    for (uint32_t i = 0; i < sizeof(fis.bytes); ++i) fis.bytes[i] = 0;
    fis.bytes[0] = 0x27u;
    fis.bytes[1] = 0x80u;
    fis.bytes[2] = lba48 ? (write ? 0x35u : 0x25u)
                         : (write ? 0xCAu : 0xC8u);
    fis.bytes[4] = static_cast<uint8_t>(lba);
    fis.bytes[5] = static_cast<uint8_t>(lba >> 8);
    fis.bytes[6] = static_cast<uint8_t>(lba >> 16);
    fis.bytes[7] = static_cast<uint8_t>(0x40u |
        (lba48 ? 0u : ((lba >> 24) & 0x0Fu)));
    if (lba48) {
        fis.bytes[8] = static_cast<uint8_t>(lba >> 24);
        fis.bytes[9] = static_cast<uint8_t>(lba >> 32);
        fis.bytes[10] = static_cast<uint8_t>(lba >> 40);
        fis.bytes[12] = static_cast<uint8_t>(count);
        fis.bytes[13] = static_cast<uint8_t>(count >> 8);
    } else {
        // ATA encodes 256 sectors as a zero sector-count byte.
        fis.bytes[12] = count == 256u ? 0u : static_cast<uint8_t>(count);
    }
    return true;
}

inline bool build_prdt(uint64_t physicalAddress, uint32_t byteCount,
                       bool supports64BitDma, Prdt& out)
{
    out.addressLow = 0;
    out.addressHigh = 0;
    out.reserved = 0;
    out.byteCountAndInterrupt = 0;
    if (byteCount == 0u || byteCount > 0x400000u ||
        physicalAddress > (~0ULL - (static_cast<uint64_t>(byteCount) - 1u)))
        return false;
    const uint64_t last = physicalAddress + byteCount - 1u;
    if (!supports64BitDma && last > 0xFFFFFFFFULL) return false;
    out.addressLow = static_cast<uint32_t>(physicalAddress);
    out.addressHigh = static_cast<uint32_t>(physicalAddress >> 32);
    out.byteCountAndInterrupt = (byteCount - 1u) | 0x80000000u;
    return true;
}

inline bool has_fatal_port_error(uint32_t portInterruptStatus,
                                 uint32_t sataError,
                                 uint32_t taskFileData)
{
    const uint32_t fatalInterrupts = (1u << 30) | (1u << 29) |
        (1u << 28) | (1u << 27) | (1u << 26) | (1u << 24);
    const uint8_t taskStatus = static_cast<uint8_t>(taskFileData);
    return (portInterruptStatus & fatalInterrupts) != 0u ||
           (sataError & 0x0000FFFFu) != 0u ||
           (taskStatus & (0x01u | 0x20u)) != 0u;
}

inline block::Status command_result(bool commandIssued, bool slotStillBusy,
                                    uint32_t portInterruptStatus,
                                    uint32_t sataError,
                                    uint32_t taskFileData,
                                    uint32_t transferredBytes,
                                    uint32_t expectedBytes,
                                    bool write, bool flush)
{
    if (!commandIssued) return block::BLOCK_ERR_NOT_READY;
    if (has_fatal_port_error(portInterruptStatus, sataError, taskFileData)) {
        if (flush) return block::BLOCK_ERR_DURABILITY_UNVERIFIED;
        return write ? block::BLOCK_ERR_WRITE_UNCERTAIN : block::BLOCK_ERR_IO;
    }
    if (slotStillBusy || (static_cast<uint8_t>(taskFileData) &
                          (0x80u | 0x08u)) != 0u) {
        if (flush) return block::BLOCK_ERR_DURABILITY_UNVERIFIED;
        return write ? block::BLOCK_ERR_WRITE_UNCERTAIN
                     : block::BLOCK_ERR_TIMEOUT;
    }
    if (transferredBytes != expectedBytes) {
        if (flush) return block::BLOCK_ERR_DURABILITY_UNVERIFIED;
        return write ? block::BLOCK_ERR_WRITE_UNCERTAIN : block::BLOCK_ERR_IO;
    }
    return block::BLOCK_OK;
}

} // namespace logic
} // namespace ahci
} // namespace kernel

#endif
