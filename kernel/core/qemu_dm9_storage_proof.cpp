#include "include/kernel/qemu_dm9_storage_proof.h"

#if defined(GXOS_DM9_QEMU_STORAGE_PROOF) || \
    defined(GXOS_DM15_QEMU_AHCI_PROOF) || \
    defined(GXOS_DM16_QEMU_NVME_PROOF) || \
    defined(GXOS_DM24_QEMU_FAT32_4KN_PROOF)

#include "include/kernel/block_device.h"
#include "include/kernel/ata.h"
#if defined(GXOS_DM15_QEMU_AHCI_PROOF)
#include "include/kernel/ahci.h"
#endif
#if defined(GXOS_DM16_QEMU_NVME_PROOF)
#include "include/kernel/nvme.h"
#endif
#include "include/kernel/pit.h"
#include "include/kernel/disk_initialization.h"
#include "include/kernel/disk_manager_model.h"
#include "include/kernel/fat32_formatter.h"
#include "include/kernel/partition_operations.h"
#include "include/kernel/partition_table.h"
#include "include/kernel/serial_debug.h"
#include "include/kernel/storage_manager.h"
#include "include/kernel/vfs.h"

namespace kernel {
namespace qemu_dm9_storage_proof {
namespace {

#if defined(GXOS_DM24_QEMU_FAT32_4KN_PROOF)
#define QEMU_PROOF_TAG "[DM24-QEMU]"
#elif defined(GXOS_DM25_QEMU_PARTITION_DELETE_PROOF)
#define QEMU_PROOF_TAG "[DM25-QEMU]"
#elif defined(GXOS_DM22_QEMU_FAT32_PROOF)
#define QEMU_PROOF_TAG "[DM22-QEMU]"
#elif defined(GXOS_DM17_SINGLE_BLOCK_PROOF) || \
    defined(GXOS_DM17_QEMU_NVME_LIFECYCLE_PROOF)
#define QEMU_PROOF_TAG "[DM17-QEMU]"
#elif defined(GXOS_DM16_QEMU_NVME_PROOF)
#define QEMU_PROOF_TAG "[DM16-QEMU]"
#elif defined(GXOS_DM15_QEMU_AHCI_PROOF)
#define QEMU_PROOF_TAG "[DM15-QEMU]"
#else
#define QEMU_PROOF_TAG "[DM9-QEMU]"
#endif

static const char kProofMountPath[] = "/mnt/dm9-proof";
static const char kProofDirectoryPath[] = "/mnt/dm9-proof/dm9";
static const char kProofFilePath[] = "/mnt/dm9-proof/dm9/proof.bin";
static const char kPartitionName[] = "DM9 QEMU Proof";
static const char kPayload[] = "guideXOS DM9 QEMU proof 001\r\n";
#if defined(GXOS_DM22_QEMU_FAT32_PROOF) || \
    defined(GXOS_DM24_QEMU_FAT32_4KN_PROOF)
static const uint32_t kDm22PayloadBytes = 96u * 1024u;
static uint8_t s_dm22Payload[kDm22PayloadBytes];
#endif

// The integrated kernel enters on a 16 KiB boot stack. Keep the parser model
// and operation snapshots out of that stack; PartitionTableModel alone is
// larger than the available stack.
static storage::PartitionTableModel s_table = {};
static storage::UnallocatedRegion s_regions[
    storage::MAX_UNALLOCATED_REGIONS] = {};
static storage::InitializeDiskRequest s_initializeRequest = {};
static storage::InitializeDiskPlan s_initializePlan = {};
static storage::InitializeDiskResult s_initializeResult = {};
static storage::CreatePartitionRequest s_createRequest = {};
static storage::CreatePartitionResult s_createResult = {};
static storage::Fat32FormatRequest s_formatRequest = {};
static storage::Fat32FormatResult s_formatResult = {};
static storage::PartitionEntry s_proofPartition = {};
static storage::DeletePartitionRequest s_deleteRequest = {};
static storage::DeletePartitionPlan s_deletePlan = {};
static storage::DeletePartitionResult s_deleteResult = {};
static storage::PartitionTableModel s_deleteBeforeTable = {};
alignas(4096) static uint8_t s_proofSectorA[storage::MAX_LOGICAL_SECTOR_SIZE];
alignas(4096) static uint8_t s_proofSectorB[storage::MAX_LOGICAL_SECTOR_SIZE];
alignas(4096) static uint8_t s_deleteDataBefore[storage::MAX_LOGICAL_SECTOR_SIZE];
alignas(4096) static uint8_t s_deleteDataAfter[storage::MAX_LOGICAL_SECTOR_SIZE];

static bool text_equal(const char* left, const char* right)
{
    if (!left || !right) return left == right;
    while (*left && *right && *left == *right) { ++left; ++right; }
    return *left == *right;
}

static bool text_starts_with(const char* text, const char* prefix)
{
    if (!text || !prefix) return false;
    while (*prefix) {
        if (*text++ != *prefix++) return false;
    }
    return true;
}

static void copy_text(char* destination, uint32_t capacity,
                      const char* source)
{
    if (!destination || capacity == 0u) return;
    uint32_t index = 0;
    if (source) {
        while (source[index] != '\0' && index + 1u < capacity) {
            destination[index] = source[index];
            ++index;
        }
    }
    destination[index] = '\0';
}

static bool bytes_equal(const void* leftBytes, const void* rightBytes,
                        uint32_t count)
{
    const uint8_t* left = static_cast<const uint8_t*>(leftBytes);
    const uint8_t* right = static_cast<const uint8_t*>(rightBytes);
    for (uint32_t i = 0; i < count; ++i) {
        if (left[i] != right[i]) return false;
    }
    return true;
}

static void print_block_io_diagnostic(const block::OperationDiagnostic& io)
{
    serial::puts("[DM10-IO] operation=");
    serial::puts(io.operation == block::OPERATION_WRITE ? "write" :
        io.operation == block::OPERATION_READ ? "read" :
        io.operation == block::OPERATION_FLUSH ? "flush" : "none");
    serial::puts(" globalIndex=");
    serial::put_hex8(io.globalIndex);
    serial::puts(" driverIndex=");
    serial::put_hex8(io.driverIndex);
    serial::puts(" registrationId=");
    serial::put_hex64(io.registrationId);
    serial::puts(" transport=");
    serial::put_hex8(static_cast<uint8_t>(io.transport));
    serial::puts(" sectorSize=");
    serial::put_hex32(io.logicalSectorSize);
    serial::puts(" lba=");
    serial::put_hex64(io.requestedLba);
    serial::puts(" count=");
    serial::put_hex32(io.requestedSectors);
    serial::puts(" callback=");
    serial::puts(io.callbackInvoked ? "yes" : "no");
    serial::puts(" blockStatus=0x");
    serial::put_hex8(static_cast<uint8_t>(io.status));
    if (io.transportDiagnostic.valid) {
#if defined(GXOS_DM16_QEMU_NVME_PROOF)
        serial::puts(" nvmeQueue=");
        serial::put_hex16(io.transportDiagnostic.nvmeQueueId);
        serial::puts(" nvmeCid=");
        serial::put_hex16(io.transportDiagnostic.nvmeCommandId);
        serial::puts(" nvmeSctSc=");
        serial::put_hex8(io.transportDiagnostic.nvmeStatusCodeType);
        serial::putc('/');
        serial::put_hex8(io.transportDiagnostic.nvmeStatusCode);
        serial::puts(" nvmeCsts=");
        serial::put_hex32(io.transportDiagnostic.nvmeControllerStatus);
#elif defined(GXOS_DM15_QEMU_AHCI_PROOF)
        serial::puts(" ahciCommand=0x");
        serial::put_hex8(io.transportDiagnostic.commandOpcode);
        serial::puts(" ahciSlot=");
        serial::put_hex8(io.transportDiagnostic.commandSlot);
        serial::puts(" ahciTFD=0x");
        serial::put_hex32(io.transportDiagnostic.transportStatus);
        serial::puts(" ahciSERR=0x");
        serial::put_hex32(io.transportDiagnostic.transportError);
#else
        serial::puts(" ataStage=");
        serial::puts(ata::ata_operation_stage_name(
            static_cast<ata::AtaOperationStage>(io.transportDiagnostic.stage)));
        serial::puts(" ataLba=");
        serial::put_hex64(io.transportDiagnostic.failingLba);
        serial::puts(" ataStatus=0x");
        serial::put_hex8(io.transportDiagnostic.statusRegister);
        serial::puts(" ataErrorValid=");
        serial::puts(io.transportDiagnostic.errorRegisterValid ? "yes" : "no");
        if (io.transportDiagnostic.errorRegisterValid) {
            serial::puts(" ataError=0x");
            serial::put_hex8(io.transportDiagnostic.errorRegister);
        }
#endif
        serial::puts(" completedSectors=");
        serial::put_hex32(io.transportDiagnostic.completedSectors);
        serial::puts(" dataSectorsTransferred=");
        serial::put_hex32(io.transportDiagnostic.dataSectorsTransferred);
    }
    serial::putc('\n');
}

static bool qemu_secondary_target(uint8_t index, block::BlockDevice& out,
                                  storage::TargetIdentity& identity)
{
#if defined(GXOS_DM24_QEMU_FAT32_4KN_PROOF)
    if (!block::copy_device(index, out) || !out.active ||
        out.type != block::BDEV_USB_MASS || !out.usbIdentityValid ||
        !text_equal(out.serial, "DM24USB01") || !out.readFn ||
        !out.writeFn || !out.flushFn || !out.flushSemanticsKnown ||
        !storage::capture_target_identity(index, identity)) return false;
#elif defined(GXOS_DM16_QEMU_NVME_PROOF)
    if (!block::copy_device(index, out) || !out.active ||
        out.type != block::BDEV_NVME || !out.pciLocationValid ||
        out.namespaceId != 1u || !out.readFn ||
        !text_equal(out.serial, "GXOSDM16NVME") ||
        !storage::capture_target_identity(index, identity)) return false;
#if defined(GXOS_DM16_NVME_PRIVATE_PROOF)
#if defined(GXOS_DM17_COMMON_WRITE_PROOF)
    if (!out.writeFn || out.flushFn || out.flushSemanticsKnown) return false;
#else
    if (out.writeFn || out.flushFn || out.flushSemanticsKnown) return false;
#endif
#else
    if (!out.writeFn || !out.flushFn || !out.flushSemanticsKnown) return false;
#endif
#elif defined(GXOS_DM15_QEMU_AHCI_PROOF)
    if (!block::copy_device(index, out) || !out.active ||
        out.type != block::BDEV_AHCI || !out.ahciPortValid ||
        out.ahciPort != 1u || !text_starts_with(out.model, "QEMU HARDDISK") ||
        !out.readFn || !out.flushFn ||
        !storage::capture_target_identity(index, identity)) return false;
#if defined(GXOS_DM15_AHCI_PRIVATE_PROOF)
    if (out.writeFn) return false;
#else
    if (!out.writeFn) return false;
#endif
#else
    if (!block::copy_device(index, out) || !out.active ||
        out.type != block::BDEV_ATA_PIO || !out.ataTargetValid ||
        out.ataChannel != 0u || out.ataTarget != 1u ||
        !text_starts_with(out.model, "QEMU HARDDISK") ||
        !out.readFn || !out.writeFn || !out.flushFn ||
        !storage::capture_target_identity(index, identity)) return false;
#endif

    const storage::BootProtection boot =
        storage::query_boot_protection(identity);
    return boot.safety == storage::BOOT_DEVICE_DEFINITELY_NOT_TARGET;
}

static bool find_qemu_secondary(block::BlockDevice& device,
                                storage::TargetIdentity& identity)
{
    for (uint8_t index = 0; index < block::MAX_BLOCK_DEVICES; ++index) {
        block::BlockDevice candidate = {};
#if defined(GXOS_DM24_QEMU_FAT32_4KN_PROOF)
        if (!block::copy_device(index, candidate) ||
            candidate.type != block::BDEV_USB_MASS) continue;
#elif defined(GXOS_DM16_QEMU_NVME_PROOF)
        if (!block::copy_device(index, candidate) ||
            candidate.type != block::BDEV_NVME) continue;
#elif defined(GXOS_DM15_QEMU_AHCI_PROOF)
        if (!block::copy_device(index, candidate) ||
            candidate.type != block::BDEV_AHCI) continue;
#else
        if (!block::copy_device(index, candidate) ||
            candidate.type != block::BDEV_ATA_PIO) continue;
#endif
        storage::TargetIdentity candidateIdentity = {};
        const bool haveIdentity =
            storage::capture_target_identity(index, candidateIdentity);
        const storage::BootProtection boot = haveIdentity
            ? storage::query_boot_protection(candidateIdentity)
            : storage::BootProtection{storage::BOOT_DEVICE_IDENTITY_UNKNOWN};
#if defined(GXOS_DM24_QEMU_FAT32_4KN_PROOF)
        serial::puts(QEMU_PROOF_TAG " USB candidate name=");
#elif defined(GXOS_DM16_QEMU_NVME_PROOF)
        serial::puts(QEMU_PROOF_TAG " NVMe candidate name=");
#elif defined(GXOS_DM15_QEMU_AHCI_PROOF)
        serial::puts(QEMU_PROOF_TAG " AHCI candidate name=");
#else
        serial::puts(QEMU_PROOF_TAG " ATA candidate name=");
#endif
        serial::puts(candidate.name);
        serial::puts(" globalIndex=");
        serial::put_hex8(index);
        serial::puts(" driverIndex=");
        serial::put_hex8(candidate.driverIndex);
        serial::puts(" registrationId=");
        serial::put_hex64(candidate.registrationId);
        serial::puts(" model=");
        serial::puts(candidate.model);
        serial::puts(" serial=");
        serial::puts(candidate.serial);
        serial::puts(" sectors=");
        serial::put_hex64(candidate.totalSectors);
        serial::puts(" sectorSize=");
        serial::put_hex32(candidate.sectorSize);
#if defined(GXOS_DM24_QEMU_FAT32_4KN_PROOF)
        serial::puts(" usbIdentity=");
        serial::puts(candidate.usbIdentityValid ? "yes" : "no");
        serial::puts(" usbPort="); serial::put_hex8(candidate.usbPort);
        serial::puts(" interface="); serial::put_hex8(candidate.usbInterface);
        serial::puts(" lun="); serial::put_hex8(candidate.usbLun);
#elif defined(GXOS_DM16_QEMU_NVME_PROOF)
        serial::puts(" nsid="); serial::put_hex32(candidate.namespaceId);
        serial::puts(" mdts="); serial::put_hex8(candidate.nvmeMdts);
        serial::puts(" vwc="); serial::put_hex8(candidate.nvmeVwcState);
#elif defined(GXOS_DM15_QEMU_AHCI_PROOF)
        serial::puts(" port=");
        serial::put_hex8(candidate.ahciPort);
#else
        serial::puts(" channel=");
        serial::put_hex8(candidate.ataChannel);
        serial::puts(" target=");
        serial::put_hex8(candidate.ataTarget);
#endif
        serial::puts(" pci-valid=");
        serial::puts(candidate.pciLocationValid ? "yes" : "no");
        serial::puts(" boot=");
        serial::puts(boot.safety == storage::BOOT_DEVICE_DEFINITELY_NOT_TARGET
            ? "DefinitelyNotBoot"
            : boot.safety == storage::BOOT_DEVICE_IS_TARGET
                ? "DefinitelyBoot" : "Unknown");
        serial::puts(" write=");
        serial::puts(candidate.writeFn ? "yes" : "no");
        serial::puts(" flush=");
        serial::puts(candidate.flushFn ? "yes" : "no");
#if defined(GXOS_DM24_QEMU_FAT32_4KN_PROOF)
        serial::puts(" usbSyncCacheState=");
        serial::put_hex8(candidate.usbSyncCacheState);
#elif defined(GXOS_DM16_QEMU_NVME_PROOF)
        serial::puts(" pci=");
        serial::put_hex8(candidate.pciBus); serial::putc(':');
        serial::put_hex8(candidate.pciDevice); serial::putc('.');
        serial::put_hex8(candidate.pciFunction);
        serial::puts(" namespaceNguid=");
        serial::puts(candidate.namespaceNguidValid ? "present" : "absent");
        serial::puts(" namespaceEui64=");
        serial::puts(candidate.namespaceEui64Valid ? "present" : "absent");
#elif defined(GXOS_DM15_QEMU_AHCI_PROOF)
        const ahci::DeviceInfo* ahciDevice =
            ahci::get_device(candidate.driverIndex);
        if (ahciDevice) {
            serial::puts(" lba48=");
            serial::puts(ahciDevice->lba48 ? "yes" : "no");
            serial::puts(" flushCache=");
            serial::puts(ahciDevice->flushCache ? "yes" : "no");
            serial::puts(" flushCacheExt=");
            serial::puts(ahciDevice->flushCacheExt ? "yes" : "no");
        }
#else
        const ata::ATADevice* ataDevice = ata::get_device(candidate.driverIndex);
        if (ataDevice) {
            serial::puts(" lba48=");
            serial::puts(ataDevice->lba48 ? "yes" : "no");
            serial::puts(" identifyWord83=0x");
            serial::put_hex16(ataDevice->identifyCommandSets83);
            serial::puts(" flushCache=");
            serial::puts(ataDevice->flushCache ? "yes" : "no");
            serial::puts(" flushCacheExt=");
            serial::puts(ataDevice->flushCacheExt ? "yes" : "no");
        }
#endif
        serial::putc('\n');
        if (qemu_secondary_target(index, device, identity)) return true;
    }
    return false;
}

#if (defined(GXOS_DM16_QEMU_NVME_PROOF) && \
     defined(GXOS_DM16_NVME_PRIVATE_PROOF))
static bool verify_private_nvme_write_flush_stress(
    uint8_t globalIndex, const block::BlockDevice& device)
{
    static uint8_t original[9u * 4096u];
    static uint8_t pattern[9u * 4096u];
    static uint8_t readback[9u * 4096u];
    const uint32_t sectorSize = device.sectorSize;
    const uint32_t count = 9u;
    const uint32_t byteCount = sectorSize * count;
    if (sectorSize < 512u || sectorSize > 4096u ||
        device.totalSectors < count + 1u || byteCount > sizeof(original))
        return false;
    const uint64_t lba = device.totalSectors - count;
    if (block::read_sectors(globalIndex, lba, count, original) !=
        block::BLOCK_OK) return false;
    for (uint32_t i = 0; i < byteCount; ++i) {
        if (original[i] != 0u) {
            serial::puts(QEMU_PROOF_TAG " private-proof=BLOCKED reason=canary-range-not-zero\n");
            return false;
        }
    }

    bool allCyclesPassed = true;
    uint32_t completedCycles = 0u;
    for (uint32_t cycle = 0u; cycle < 100u; ++cycle) {
        for (uint32_t i = 0; i < byteCount; ++i)
            pattern[i] = static_cast<uint8_t>(
                (i * 37u + cycle * 13u + 0x5Au) & 0xFFu);
        const block::Status writeStatus = nvme::proof_write(
            device.driverIndex, lba, count, pattern);
        const block::Status preFlushReadStatus =
            writeStatus == block::BLOCK_OK
                ? block::read_sectors(globalIndex, lba, count, readback)
                : block::BLOCK_ERR_NOT_READY;
        const bool preFlushPatternMatches =
            preFlushReadStatus == block::BLOCK_OK &&
            bytes_equal(reinterpret_cast<const char*>(pattern),
                        reinterpret_cast<const char*>(readback), byteCount);
        const block::Status flushStatus = writeStatus == block::BLOCK_OK
            ? nvme::proof_flush(device.driverIndex) : block::BLOCK_ERR_NOT_READY;
        const block::Status readStatus = flushStatus == block::BLOCK_OK
            ? block::read_sectors(globalIndex, lba, count, readback)
            : block::BLOCK_ERR_NOT_READY;
        const bool patternMatches = readStatus == block::BLOCK_OK &&
            bytes_equal(reinterpret_cast<const char*>(pattern),
                        reinterpret_cast<const char*>(readback), byteCount);
        const block::Status restoreStatus = nvme::proof_write(
            device.driverIndex, lba, count, original);
        const block::Status restoreFlushStatus = restoreStatus == block::BLOCK_OK
            ? nvme::proof_flush(device.driverIndex) : block::BLOCK_ERR_NOT_READY;
        const block::Status restoreReadStatus =
            restoreFlushStatus == block::BLOCK_OK
                ? block::read_sectors(globalIndex, lba, count, readback)
                : block::BLOCK_ERR_NOT_READY;
        const bool restored = restoreReadStatus == block::BLOCK_OK &&
            bytes_equal(reinterpret_cast<const char*>(original),
                        reinterpret_cast<const char*>(readback), byteCount);
        if (writeStatus != block::BLOCK_OK || flushStatus != block::BLOCK_OK ||
            !preFlushPatternMatches || !patternMatches ||
            restoreStatus != block::BLOCK_OK ||
            restoreFlushStatus != block::BLOCK_OK || !restored) {
            serial::puts(QEMU_PROOF_TAG " private-cycle=FAIL cycle=");
            serial::put_hex32(cycle + 1u);
            serial::puts(" write="); serial::put_hex8(static_cast<uint8_t>(writeStatus));
            serial::puts(" preRead=");
            serial::put_hex8(static_cast<uint8_t>(preFlushReadStatus));
            serial::puts(" preMatches=");
            serial::puts(preFlushPatternMatches ? "yes" : "no");
            serial::puts(" flush="); serial::put_hex8(static_cast<uint8_t>(flushStatus));
            serial::puts(" read="); serial::put_hex8(static_cast<uint8_t>(readStatus));
            serial::puts(" restore="); serial::put_hex8(static_cast<uint8_t>(restoreStatus));
            serial::puts(" restoreFlush=");
            serial::put_hex8(static_cast<uint8_t>(restoreFlushStatus));
            serial::puts(" restoreRead=");
            serial::put_hex8(static_cast<uint8_t>(restoreReadStatus));
            serial::puts(" patternMatches=");
            serial::puts(patternMatches ? "yes" : "no");
            serial::puts(" restored=");
            serial::puts(restored ? "yes" : "no");
            serial::putc('\n');
            allCyclesPassed = false;
            break;
        }
        if (cycle == 0u) {
            serial::puts(QEMU_PROOF_TAG " private-cycle=PASS cycle=00000001 preRead=");
            serial::put_hex8(static_cast<uint8_t>(preFlushReadStatus));
            serial::puts(" preMatches=");
            serial::puts(preFlushPatternMatches ? "yes" : "no");
            serial::puts(" flush=");
            serial::put_hex8(static_cast<uint8_t>(flushStatus));
            serial::puts(" postRead=");
            serial::put_hex8(static_cast<uint8_t>(readStatus));
            serial::puts(" postMatches=");
            serial::puts(patternMatches ? "yes" : "no");
            serial::putc('\n');
        }
        ++completedCycles;
        if (((cycle + 1u) % 10u) == 0u) {
            serial::puts(QEMU_PROOF_TAG " private-stress=progress cycles=");
            serial::put_hex32(cycle + 1u);
            serial::putc('\n');
        }
    }

    const bool finallyRestored = allCyclesPassed && completedCycles == 100u &&
        block::read_sectors(globalIndex, lba, count, readback) == block::BLOCK_OK &&
        bytes_equal(reinterpret_cast<const char*>(original),
                    reinterpret_cast<const char*>(readback), byteCount);
    serial::puts(QEMU_PROOF_TAG " private-write-flush-stress=");
    serial::puts(finallyRestored ? "PASS" : "FAIL");
    serial::puts(" cycles="); serial::put_hex32(completedCycles);
    serial::puts(" lba="); serial::put_hex64(lba);
    serial::puts(" sectors="); serial::put_hex32(count);
    serial::puts(" bytes="); serial::put_hex32(byteCount);
    serial::puts(" restored="); serial::puts(finallyRestored ? "yes" : "no");
    serial::putc('\n');
    return finallyRestored;
}
#endif

#if defined(GXOS_DM17_SINGLE_BLOCK_PROOF) && \
    defined(GXOS_DM16_NVME_PRIVATE_PROOF)
static const uint32_t kDm17MaximumTransferBlocks = 256u;
static const uint32_t kDm17SectorBytes = 512u;

static void fill_dm17_nvme_pattern(uint8_t* block, uint32_t generation,
                                   uint64_t lba, uint32_t blockIndex)
{
    block[0] = 'D'; block[1] = 'M'; block[2] = '1'; block[3] = '7';
    for (uint32_t i = 0; i < 4u; ++i) {
        block[4u + i] = static_cast<uint8_t>(generation >> (i * 8u));
        block[16u + i] = static_cast<uint8_t>(blockIndex >> (i * 8u));
    }
    for (uint32_t i = 0; i < 8u; ++i)
        block[8u + i] = static_cast<uint8_t>(lba >> (i * 8u));
    for (uint32_t i = 20u; i < kDm17SectorBytes; ++i) {
        const uint64_t value = static_cast<uint64_t>(generation) * 29u +
            static_cast<uint64_t>(lba) * 17u +
            static_cast<uint64_t>(blockIndex) * 53u +
            static_cast<uint64_t>(i) * 37u + 0xC3u;
        block[i] = static_cast<uint8_t>(value);
    }
}

static bool dm17_bytes_are_zero(const uint8_t* bytes, uint32_t length)
{
    if (!bytes) return false;
    for (uint32_t i = 0; i < length; ++i)
        if (bytes[i] != 0u) return false;
    return true;
}

static void dm17_host_inspection_gate(const char* phase,
                                      uint32_t generation, uint64_t lba,
                                      uint32_t blocks, uint32_t sourceOffset)
{
    serial::puts("[DM17-QEMU] host-inspect phase="); serial::puts(phase);
    serial::puts(" generation="); serial::put_hex32(generation);
    serial::puts(" lba="); serial::put_hex64(lba);
    serial::puts(" blocks="); serial::put_hex32(blocks);
    serial::puts(" blockSize="); serial::put_hex32(kDm17SectorBytes);
    serial::puts(" sourceOffset="); serial::put_hex32(sourceOffset);
    serial::putc('\n');
    const uint64_t start = pit::ticks();
    while (pit::ticks() - start < 100u) { }
}

static bool verify_dm17_nvme_transfer_case(
    uint8_t globalIndex, const block::BlockDevice& device, uint64_t lba,
    uint32_t blocks, uint32_t generation, uint32_t sourceOffset)
{
    static uint8_t original[kDm17MaximumTransferBlocks * kDm17SectorBytes];
    static uint8_t readback[kDm17MaximumTransferBlocks * kDm17SectorBytes];
    static uint8_t sourceSnapshot[kDm17MaximumTransferBlocks * kDm17SectorBytes];
#if defined(_MSC_VER)
    __declspec(align(4096)) static uint8_t source[
        kDm17MaximumTransferBlocks * kDm17SectorBytes + 4096u];
#else
    static uint8_t source[
        kDm17MaximumTransferBlocks * kDm17SectorBytes + 4096u]
        __attribute__((aligned(4096)));
#endif
    if (device.sectorSize != kDm17SectorBytes || blocks == 0u ||
        blocks > kDm17MaximumTransferBlocks ||
        sourceOffset > 4096u ||
        static_cast<uint64_t>(sourceOffset) +
            static_cast<uint64_t>(blocks) * kDm17SectorBytes > sizeof(source) ||
        !nvme::get_device(device.driverIndex) ||
        device.totalSectors < blocks || lba > device.totalSectors - blocks)
        return false;

    const uint32_t byteCount = blocks * kDm17SectorBytes;
    if (block::read_sectors(globalIndex, lba, blocks, original) !=
        block::BLOCK_OK) return false;
    for (uint32_t i = 0; i < byteCount; ++i) {
        if (original[i] != 0u) {
            serial::puts("[DM17-QEMU] integrity=BLOCKED reason=range-not-zero\n");
            return false;
        }
    }

    uint8_t* pattern = source + sourceOffset;
    for (uint32_t blockIndex = 0; blockIndex < blocks; ++blockIndex)
        fill_dm17_nvme_pattern(pattern + blockIndex * kDm17SectorBytes,
                               generation, lba + blockIndex, blockIndex);
    for (uint32_t i = 0; i < byteCount; ++i)
        sourceSnapshot[i] = pattern[i];

    const block::Status writeStatus = nvme::proof_write(
        device.driverIndex, lba, blocks, pattern);
    if (writeStatus != block::BLOCK_OK) {
        serial::puts("[DM17-QEMU] transfer=FAIL phase=write status=");
        serial::put_hex8(static_cast<uint8_t>(writeStatus));
        serial::puts(" lba="); serial::put_hex64(lba);
        serial::puts(" blocks="); serial::put_hex32(blocks);
        serial::putc('\n');
        return false;
    }
    dm17_host_inspection_gate("write", generation, lba, blocks, sourceOffset);

    const block::Status readStatus = block::read_sectors(
        globalIndex, lba, blocks, readback);
    const bool patternMatches = readStatus == block::BLOCK_OK &&
        bytes_equal(reinterpret_cast<const char*>(sourceSnapshot),
                    reinterpret_cast<const char*>(readback), byteCount);
    bool sourcePreserved = true;
    for (uint32_t i = 0; i < byteCount; ++i)
        if (pattern[i] != sourceSnapshot[i]) {
            sourcePreserved = false;
            break;
        }

    const block::Status restoreStatus = nvme::proof_write(
        device.driverIndex, lba, blocks, original);
    if (restoreStatus != block::BLOCK_OK) {
        serial::puts("[DM17-QEMU] transfer=FAIL phase=restore status=");
        serial::put_hex8(static_cast<uint8_t>(restoreStatus));
        serial::puts(" lba="); serial::put_hex64(lba);
        serial::puts(" blocks="); serial::put_hex32(blocks);
        serial::putc('\n');
        return false;
    }
    dm17_host_inspection_gate("restore", generation, lba, blocks,
                              sourceOffset);
    const block::Status restoreReadStatus = block::read_sectors(
        globalIndex, lba, blocks, readback);
    const bool restored = restoreReadStatus == block::BLOCK_OK &&
        bytes_equal(reinterpret_cast<const char*>(original),
                    reinterpret_cast<const char*>(readback), byteCount);
    serial::puts("[DM17-QEMU] transfer-case=");
    serial::puts(patternMatches && sourcePreserved && restored ? "PASS" : "FAIL");
    serial::puts(" generation="); serial::put_hex32(generation);
    serial::puts(" lba="); serial::put_hex64(lba);
    serial::puts(" blocks="); serial::put_hex32(blocks);
    serial::puts(" bytes="); serial::put_hex32(byteCount);
    serial::puts(" writeStatus="); serial::put_hex8(static_cast<uint8_t>(writeStatus));
    serial::puts(" readStatus="); serial::put_hex8(static_cast<uint8_t>(readStatus));
    serial::puts(" match="); serial::puts(patternMatches ? "yes" : "no");
    serial::puts(" sourcePreserved=");
    serial::puts(sourcePreserved ? "yes" : "no");
    serial::puts(" restoreStatus=");
    serial::put_hex8(static_cast<uint8_t>(restoreStatus));
    serial::puts(" restoreReadStatus=");
    serial::put_hex8(static_cast<uint8_t>(restoreReadStatus));
    serial::puts(" restored="); serial::puts(restored ? "yes" : "no");
    serial::putc('\n');
    return patternMatches && sourcePreserved && restored;
}

#if defined(GXOS_DM17_COMMON_WRITE_PROOF)
static bool verify_dm17_common_write_stage(
    uint8_t globalIndex, const block::BlockDevice& device)
{
    static uint8_t original[kDm17SectorBytes];
    static uint8_t pattern[kDm17SectorBytes];
    static uint8_t readback[kDm17SectorBytes];
    const uint64_t lba = 0x5001u;
    const uint32_t generation = 0x180u;
    if (device.totalSectors <= lba + 1u ||
        block::read_sectors(globalIndex, lba, 1u, original) != block::BLOCK_OK ||
        !dm17_bytes_are_zero(original, sizeof(original)))
        return false;
    fill_dm17_nvme_pattern(pattern, generation, lba, 0u);
    const block::Status writeStatus = block::write_sectors(
        globalIndex, lba, 1u, pattern);
    if (writeStatus == block::BLOCK_OK)
        dm17_host_inspection_gate("write", generation, lba, 1u, 128u);
    const block::Status readStatus = writeStatus == block::BLOCK_OK
        ? block::read_sectors(globalIndex, lba, 1u, readback)
        : block::BLOCK_ERR_NOT_READY;
    const bool matches = readStatus == block::BLOCK_OK &&
        bytes_equal(reinterpret_cast<const char*>(pattern),
                    reinterpret_cast<const char*>(readback),
                    kDm17SectorBytes);
    const block::Status flushStatus = block::flush(globalIndex);

    const block::Status restoreStatus = block::write_sectors(
        globalIndex, lba, 1u, original);
    if (restoreStatus == block::BLOCK_OK)
        dm17_host_inspection_gate("restore", generation, lba, 1u, 128u);
    const block::Status restoreReadStatus =
        restoreStatus == block::BLOCK_OK
            ? block::read_sectors(globalIndex, lba, 1u, readback)
            : block::BLOCK_ERR_NOT_READY;
    const bool restored = restoreReadStatus == block::BLOCK_OK &&
        bytes_equal(reinterpret_cast<const char*>(original),
                    reinterpret_cast<const char*>(readback),
                    kDm17SectorBytes);
    const bool flushBlockedAsUnknown = !device.flushSemanticsKnown &&
        flushStatus == block::BLOCK_ERR_UNSUPPORTED;
    const bool passed = writeStatus == block::BLOCK_OK && matches &&
        flushBlockedAsUnknown && restoreStatus == block::BLOCK_OK && restored;
    serial::puts("[DM17-QEMU] common-write-stage=");
    serial::puts(passed ? "PASS" : "FAIL");
    serial::puts(" write="); serial::put_hex8(static_cast<uint8_t>(writeStatus));
    serial::puts(" read="); serial::put_hex8(static_cast<uint8_t>(readStatus));
    serial::puts(" match="); serial::puts(matches ? "yes" : "no");
    serial::puts(" flush="); serial::put_hex8(static_cast<uint8_t>(flushStatus));
    serial::puts(" flushBlockedAsUnknown=");
    serial::puts(flushBlockedAsUnknown ? "yes" : "no");
    serial::puts(" restore="); serial::put_hex8(static_cast<uint8_t>(restoreStatus));
    serial::puts(" restoreRead=");
    serial::put_hex8(static_cast<uint8_t>(restoreReadStatus));
    serial::puts(" restored="); serial::puts(restored ? "yes" : "no");
    serial::puts(" writeCallback=enabled flushCallback=disabled persistence=unknown\n");
    return passed;
}
#endif

static bool verify_dm17_nvme_flush_and_restart(
    uint8_t globalIndex, const block::BlockDevice& device);
static bool verify_dm17_nvme_restart_recovery(
    uint8_t globalIndex, const block::BlockDevice& device, bool& pending);

static bool verify_dm17_nvme_data_integrity(
    uint8_t globalIndex, const block::BlockDevice& device)
{
    if (device.sectorSize != kDm17SectorBytes || device.totalSectors <= 0x5000u)
        return false;

    bool restartPending = false;
    if (!verify_dm17_nvme_restart_recovery(globalIndex, device,
                                            restartPending))
        return false;
    if (restartPending) return true;

    uint32_t singlePassed = 0u;
    for (uint32_t cycle = 0; cycle < 10u; ++cycle) {
        const uint32_t sourceOffset = cycle % 3u == 0u ? 128u :
            (cycle % 3u == 1u ? 3584u : 3968u);
        if (!verify_dm17_nvme_transfer_case(globalIndex, device,
                0x1001u + static_cast<uint64_t>(cycle) * 8u, 1u,
                cycle + 1u, sourceOffset)) break;
        ++singlePassed;
    }
    serial::puts("[DM17-QEMU] single-block-gate=");
    serial::puts(singlePassed == 10u ? "PASS" : "FAIL");
    serial::puts(" cycles="); serial::put_hex32(singlePassed);
    serial::puts(" required=0000000A\n");
    if (singlePassed != 10u) return false;

    static const uint32_t matrixBlocks[] = { 2u, 7u, 8u, 9u };
    for (uint32_t i = 0; i < sizeof(matrixBlocks) / sizeof(matrixBlocks[0]); ++i) {
        if (!verify_dm17_nvme_transfer_case(globalIndex, device,
                0x2001u + static_cast<uint64_t>(i) * 32u,
                matrixBlocks[i], 0x20u + i,
                4096u - 128u)) return false;
    }

    uint32_t multiPassed = 0u;
    for (uint32_t cycle = 0; cycle < 10u; ++cycle) {
        if (!verify_dm17_nvme_transfer_case(globalIndex, device,
                0x3001u + static_cast<uint64_t>(cycle) * 16u, 7u,
                0x40u + cycle, 4096u - 256u)) break;
        ++multiPassed;
    }
    serial::puts("[DM17-QEMU] multi-block-gate=");
    serial::puts(multiPassed == 10u ? "PASS" : "FAIL");
    serial::puts(" cycles="); serial::put_hex32(multiPassed);
    serial::puts(" blocks=00000007 required=0000000A\n");
    if (multiPassed != 10u) return false;

    if (!verify_dm17_nvme_transfer_case(globalIndex, device,
            0x4001u, kDm17MaximumTransferBlocks, 0x80u, 3968u))
        return false;
    if (!verify_dm17_nvme_transfer_case(globalIndex, device,
            device.totalSectors - 1u, 1u, 0x100u, 3584u))
        return false;

    serial::puts("[DM17-QEMU] data-integrity-gate=PASS single=10/10 multi=10/10 page-crossing=PASS maxBlocks=00000100 lastLba=PASS shared-write=disabled\n");
#if defined(GXOS_DM17_COMMON_WRITE_PROOF)
    if (!verify_dm17_common_write_stage(globalIndex, device)) return false;
#endif
    return verify_dm17_nvme_flush_and_restart(globalIndex, device);
}

static const uint64_t kDm17FlushStressLba = 0x6001u;
static const uint64_t kDm17RestartLba = 0x7001u;
static const uint32_t kDm17FlushCycles = 100u;
static const uint32_t kDm17FlushPatternGeneration = 0x300u;

static bool verify_dm17_nvme_flush_and_restart(
    uint8_t globalIndex, const block::BlockDevice& device)
{
    static uint8_t original[kDm17SectorBytes];
    static uint8_t readback[kDm17SectorBytes];
    static uint8_t pattern[kDm17SectorBytes];
    if (device.totalSectors <= kDm17RestartLba + 1u ||
        block::read_sectors(globalIndex, kDm17FlushStressLba, 1u, original) !=
            block::BLOCK_OK || !dm17_bytes_are_zero(original, sizeof(original))) {
        serial::puts("[DM17-QEMU] flush-gate=FAIL reason=stress-range-not-zero-or-readable\n");
        return false;
    }

    uint32_t completedCycles = 0u;
    for (uint32_t cycle = 0u; cycle < kDm17FlushCycles; ++cycle) {
        const uint32_t generation = 0x200u + cycle;
        fill_dm17_nvme_pattern(pattern, generation, kDm17FlushStressLba, 0u);
        const block::Status writeStatus = nvme::proof_write(
            device.driverIndex, kDm17FlushStressLba, 1u, pattern);
        if (writeStatus == block::BLOCK_OK)
            dm17_host_inspection_gate("write", generation,
                kDm17FlushStressLba, 1u, 128u);
        const block::Status flushStatus = writeStatus == block::BLOCK_OK
            ? nvme::proof_flush(device.driverIndex)
            : block::BLOCK_ERR_NOT_READY;
        const block::Status readStatus = flushStatus == block::BLOCK_OK
            ? block::read_sectors(globalIndex, kDm17FlushStressLba, 1u,
                                  readback)
            : block::BLOCK_ERR_NOT_READY;
        const bool patternMatches = readStatus == block::BLOCK_OK &&
            bytes_equal(reinterpret_cast<const char*>(pattern),
                        reinterpret_cast<const char*>(readback),
                        kDm17SectorBytes);

        const block::Status restoreStatus = nvme::proof_write(
            device.driverIndex, kDm17FlushStressLba, 1u, original);
        if (restoreStatus == block::BLOCK_OK)
            dm17_host_inspection_gate("restore", generation,
                kDm17FlushStressLba, 1u, 128u);
        const block::Status restoreFlushStatus =
            restoreStatus == block::BLOCK_OK
                ? nvme::proof_flush(device.driverIndex)
                : block::BLOCK_ERR_NOT_READY;
        const block::Status restoreReadStatus =
            restoreFlushStatus == block::BLOCK_OK
                ? block::read_sectors(globalIndex, kDm17FlushStressLba, 1u,
                                      readback)
                : block::BLOCK_ERR_NOT_READY;
        const bool restored = restoreReadStatus == block::BLOCK_OK &&
            bytes_equal(reinterpret_cast<const char*>(original),
                        reinterpret_cast<const char*>(readback),
                        kDm17SectorBytes);
        if (writeStatus != block::BLOCK_OK || flushStatus != block::BLOCK_OK ||
            !patternMatches || restoreStatus != block::BLOCK_OK ||
            restoreFlushStatus != block::BLOCK_OK || !restored) {
            serial::puts("[DM17-QEMU] flush-cycle=FAIL cycle=");
            serial::put_hex32(cycle + 1u);
            serial::puts(" write=");
            serial::put_hex8(static_cast<uint8_t>(writeStatus));
            serial::puts(" flush=");
            serial::put_hex8(static_cast<uint8_t>(flushStatus));
            serial::puts(" read=");
            serial::put_hex8(static_cast<uint8_t>(readStatus));
            serial::puts(" match=");
            serial::puts(patternMatches ? "yes" : "no");
            serial::puts(" restore=");
            serial::put_hex8(static_cast<uint8_t>(restoreStatus));
            serial::puts(" restoreFlush=");
            serial::put_hex8(static_cast<uint8_t>(restoreFlushStatus));
            serial::puts(" restoreRead=");
            serial::put_hex8(static_cast<uint8_t>(restoreReadStatus));
            serial::puts(" restored=");
            serial::puts(restored ? "yes" : "no");
            serial::putc('\n');
            return false;
        }
        ++completedCycles;
        if ((completedCycles % 10u) == 0u) {
            serial::puts("[DM17-QEMU] flush-stress=progress cycles=");
            serial::put_hex32(completedCycles);
            serial::putc('\n');
        }
    }

    if (completedCycles != kDm17FlushCycles ||
        block::read_sectors(globalIndex, kDm17RestartLba, 1u, original) !=
            block::BLOCK_OK || !dm17_bytes_are_zero(original, sizeof(original))) {
        serial::puts("[DM17-QEMU] flush-gate=FAIL reason=stress-or-restart-range\n");
        return false;
    }
    fill_dm17_nvme_pattern(pattern, kDm17FlushPatternGeneration,
                           kDm17RestartLba, 0u);
    const block::Status restartWriteStatus = nvme::proof_write(
        device.driverIndex, kDm17RestartLba, 1u, pattern);
    if (restartWriteStatus == block::BLOCK_OK)
        dm17_host_inspection_gate("write", kDm17FlushPatternGeneration,
            kDm17RestartLba, 1u, 128u);
    const block::Status restartFlushStatus =
        restartWriteStatus == block::BLOCK_OK
            ? nvme::proof_flush(device.driverIndex)
            : block::BLOCK_ERR_NOT_READY;
    const block::Status restartReadStatus = restartFlushStatus == block::BLOCK_OK
        ? block::read_sectors(globalIndex, kDm17RestartLba, 1u, readback)
        : block::BLOCK_ERR_NOT_READY;
    const bool restartDataMatches = restartReadStatus == block::BLOCK_OK &&
        bytes_equal(reinterpret_cast<const char*>(pattern),
                    reinterpret_cast<const char*>(readback),
                    kDm17SectorBytes);
    serial::puts("[DM17-QEMU] flush-gate=");
    serial::puts(restartWriteStatus == block::BLOCK_OK &&
        restartFlushStatus == block::BLOCK_OK && restartDataMatches
            ? "PASS" : "FAIL");
    serial::puts(" cycles="); serial::put_hex32(completedCycles);
    serial::puts(" write="); serial::put_hex8(static_cast<uint8_t>(restartWriteStatus));
    serial::puts(" flush="); serial::put_hex8(static_cast<uint8_t>(restartFlushStatus));
    serial::puts(" read="); serial::put_hex8(static_cast<uint8_t>(restartReadStatus));
    serial::puts(" match="); serial::puts(restartDataMatches ? "yes" : "no");
    serial::puts(" restartLba="); serial::put_hex64(kDm17RestartLba);
    serial::putc('\n');
    if (restartWriteStatus != block::BLOCK_OK ||
        restartFlushStatus != block::BLOCK_OK || !restartDataMatches)
        return false;

    serial::puts("[DM17-QEMU] restart-proof=READY generation=");
    serial::put_hex32(kDm17FlushPatternGeneration);
    serial::puts(" lba="); serial::put_hex64(kDm17RestartLba);
    serial::putc('\n');
    return true;
}

static bool verify_dm17_nvme_restart_recovery(
    uint8_t globalIndex, const block::BlockDevice& device, bool& pending)
{
    static uint8_t actual[kDm17SectorBytes];
    static uint8_t expected[kDm17SectorBytes];
    static uint8_t zero[kDm17SectorBytes];
    static uint8_t readback[kDm17SectorBytes];
    pending = false;
    if (block::read_sectors(globalIndex, kDm17RestartLba, 1u, actual) !=
        block::BLOCK_OK) {
        serial::puts("[DM17-QEMU] restart-proof=FAIL reason=marker-read-failed\n");
        return false;
    }
    fill_dm17_nvme_pattern(expected, kDm17FlushPatternGeneration,
                           kDm17RestartLba, 0u);
    if (bytes_equal(reinterpret_cast<const char*>(actual),
                    reinterpret_cast<const char*>(expected),
                    kDm17SectorBytes)) {
        pending = true;
        serial::puts("[DM17-QEMU] restart-read=PASS generation=");
        serial::put_hex32(kDm17FlushPatternGeneration);
        serial::puts(" lba="); serial::put_hex64(kDm17RestartLba);
        serial::putc('\n');
        const block::Status restoreStatus = nvme::proof_write(
            device.driverIndex, kDm17RestartLba, 1u, zero);
        if (restoreStatus == block::BLOCK_OK)
            dm17_host_inspection_gate("restore", kDm17FlushPatternGeneration,
                kDm17RestartLba, 1u, 128u);
        const block::Status flushStatus = restoreStatus == block::BLOCK_OK
            ? nvme::proof_flush(device.driverIndex)
            : block::BLOCK_ERR_NOT_READY;
        const block::Status readStatus = flushStatus == block::BLOCK_OK
            ? block::read_sectors(globalIndex, kDm17RestartLba, 1u, readback)
            : block::BLOCK_ERR_NOT_READY;
        const bool restored = readStatus == block::BLOCK_OK &&
            dm17_bytes_are_zero(readback, kDm17SectorBytes);
        serial::puts("[DM17-QEMU] restart-proof=");
        serial::puts(restoreStatus == block::BLOCK_OK &&
            flushStatus == block::BLOCK_OK && restored ? "PASS" : "FAIL");
        serial::puts(" restore="); serial::put_hex8(static_cast<uint8_t>(restoreStatus));
        serial::puts(" flush="); serial::put_hex8(static_cast<uint8_t>(flushStatus));
        serial::puts(" read="); serial::put_hex8(static_cast<uint8_t>(readStatus));
        serial::puts(" restored="); serial::puts(restored ? "yes" : "no");
        serial::putc('\n');
        return restoreStatus == block::BLOCK_OK &&
            flushStatus == block::BLOCK_OK && restored;
    }
    if (dm17_bytes_are_zero(actual, kDm17SectorBytes)) return true;
    serial::puts("[DM17-QEMU] restart-proof=FAIL reason=unexpected-marker-data\n");
    return false;
}
#endif

#if defined(GXOS_DM15_QEMU_AHCI_PROOF) && \
    defined(GXOS_DM15_AHCI_PRIVATE_PROOF)
static bool verify_private_ahci_write_and_flush(
    uint8_t globalIndex, const block::BlockDevice& device)
{
    static uint8_t original[4096];
    static uint8_t pattern[4096];
    static uint8_t readback[4096];
    if (device.sectorSize < 512u || device.sectorSize > sizeof(original) ||
        device.totalSectors < 2u) return false;
    const uint64_t lba = device.totalSectors - 1u;
    if (block::read_sectors(globalIndex, lba, 1u, original) !=
        block::BLOCK_OK) return false;
    for (uint32_t i = 0; i < device.sectorSize; ++i) {
        if (original[i] != 0u) {
            serial::puts(QEMU_PROOF_TAG " private-write=BLOCKED reason=target-sector-not-zero\n");
            return false;
        }
        pattern[i] = static_cast<uint8_t>((i * 37u + 0x5Au) & 0xFFu);
    }

    bool writeAttempted = false;
    bool dataPassed = false;
    block::Status writeStatus = block::BLOCK_ERR_NOT_READY;
    block::Status flushStatus = block::BLOCK_ERR_NOT_READY;
    block::Status readStatus = block::BLOCK_ERR_NOT_READY;
    writeAttempted = true;
    writeStatus = ahci::proof_write(device.driverIndex, lba, 1u, pattern);
    if (writeStatus == block::BLOCK_OK)
        flushStatus = block::flush(globalIndex);
    if (writeStatus == block::BLOCK_OK && flushStatus == block::BLOCK_OK) {
        readStatus = block::read_sectors(globalIndex, lba, 1u, readback);
        dataPassed = readStatus == block::BLOCK_OK &&
            bytes_equal(reinterpret_cast<const char*>(pattern),
                        reinterpret_cast<const char*>(readback),
                        device.sectorSize);
    }

    const block::Status restoreStatus = writeAttempted
        ? ahci::proof_write(device.driverIndex, lba, 1u, original)
        : block::BLOCK_ERR_NOT_READY;
    const block::Status restoreFlushStatus = restoreStatus == block::BLOCK_OK
        ? block::flush(globalIndex) : block::BLOCK_ERR_NOT_READY;
    const block::Status restoreReadStatus = restoreFlushStatus == block::BLOCK_OK
        ? block::read_sectors(globalIndex, lba, 1u, readback)
        : block::BLOCK_ERR_NOT_READY;
    const bool restored = restoreReadStatus == block::BLOCK_OK &&
        bytes_equal(reinterpret_cast<const char*>(original),
                    reinterpret_cast<const char*>(readback),
                    device.sectorSize);

    serial::puts(QEMU_PROOF_TAG " private-write=");
    serial::puts(dataPassed && restored ? "PASS" : "FAIL");
    serial::puts(" lba="); serial::put_hex64(lba);
    serial::puts(" bytes="); serial::put_hex32(device.sectorSize);
    serial::puts(" writeStatus=0x"); serial::put_hex8(static_cast<uint8_t>(writeStatus));
    serial::puts(" flushStatus=0x"); serial::put_hex8(static_cast<uint8_t>(flushStatus));
    serial::puts(" readStatus=0x"); serial::put_hex8(static_cast<uint8_t>(readStatus));
    serial::puts(" restoreStatus=0x"); serial::put_hex8(static_cast<uint8_t>(restoreStatus));
    serial::puts(" restoreFlushStatus=0x");
    serial::put_hex8(static_cast<uint8_t>(restoreFlushStatus));
    serial::puts(" restored="); serial::puts(restored ? "yes" : "no");
    serial::putc('\n');
    return dataPassed && restored;
}
#endif

static bool read_proof_file()
{
    const uint8_t handle = vfs::open(kProofFilePath, vfs::OPEN_READ);
    if (handle == 0xFFu) return false;

#if defined(GXOS_DM22_QEMU_FAT32_PROOF) || \
    defined(GXOS_DM24_QEMU_FAT32_4KN_PROOF)
    uint8_t bytes[4096];
    uint32_t offset = 0;
    bool matches = true;
    while (offset < kDm22PayloadBytes) {
        const uint32_t requested =
            kDm22PayloadBytes - offset < sizeof(bytes)
                ? kDm22PayloadBytes - offset
                : static_cast<uint32_t>(sizeof(bytes));
        const int32_t count = vfs::read(handle, bytes, requested);
        if (count != static_cast<int32_t>(requested)) {
            matches = false;
            break;
        }
        for (uint32_t i = 0; i < requested; ++i) {
            const uint32_t index = offset + i;
            const uint8_t expected = static_cast<uint8_t>(
                index * 37u + (index >> 8) * 13u + 0x5Au);
            if (bytes[i] != expected) matches = false;
        }
        offset += requested;
    }
    const vfs::Status closeStatus = vfs::close(handle);
    return matches && offset == kDm22PayloadBytes &&
        closeStatus == vfs::VFS_OK;
#else
    char bytes[sizeof(kPayload)] = {};
    const int32_t count = vfs::read(handle, bytes,
        static_cast<uint32_t>(sizeof(kPayload) - 1u));
    const vfs::Status closeStatus = vfs::close(handle);
    return count == static_cast<int32_t>(sizeof(kPayload) - 1u) &&
        bytes_equal(bytes, kPayload,
            static_cast<uint32_t>(sizeof(kPayload) - 1u)) &&
        closeStatus == vfs::VFS_OK;
#endif
}

static bool mount_proof_partition(const storage::TargetIdentity& identity,
                                  const storage::PartitionEntry& partition,
                                  bool expectNewLifecycle,
                                  uint8_t rootMountCount)
{
    const vfs::PartitionMountResult mounted = vfs::mount_partition_detailed(
        kProofMountPath, identity.globalIndex, partition.partitionNumber,
        identity.registrationId, &partition);
    if (mounted.error != vfs::PARTITION_MOUNT_OK) return false;

    bool contentsValid = true;
    if (expectNewLifecycle) {
#if defined(GXOS_DM22_QEMU_FAT32_PROOF) || \
    defined(GXOS_DM24_QEMU_FAT32_4KN_PROOF)
        for (uint32_t i = 0; i < kDm22PayloadBytes; ++i)
            s_dm22Payload[i] = static_cast<uint8_t>(
                i * 37u + (i >> 8) * 13u + 0x5Au);
        const void* payload = s_dm22Payload;
        const uint32_t payloadBytes = kDm22PayloadBytes;
#else
        const void* payload = kPayload;
        const uint32_t payloadBytes =
            static_cast<uint32_t>(sizeof(kPayload) - 1u);
#endif
        contentsValid = vfs::mkdir(kProofDirectoryPath) == vfs::VFS_OK &&
            vfs::create_file(kProofFilePath, payload, payloadBytes) ==
                static_cast<int32_t>(payloadBytes) &&
            read_proof_file();
    } else {
        contentsValid = read_proof_file();
    }

    const vfs::Status firstUnmount = vfs::unmount(kProofMountPath);
    if (!contentsValid || firstUnmount != vfs::VFS_OK ||
        vfs::mount_count() != rootMountCount) return false;

    if (!expectNewLifecycle) return true;

    const vfs::PartitionMountResult remounted = vfs::mount_partition_detailed(
        kProofMountPath, identity.globalIndex, partition.partitionNumber,
        identity.registrationId, &partition);
    const bool remountRead = remounted.error == vfs::PARTITION_MOUNT_OK &&
        vfs::mount_identity_valid(remounted.mountIndex) && read_proof_file();
    const vfs::Status secondUnmount = vfs::unmount(kProofMountPath);
    return remountRead && secondUnmount == vfs::VFS_OK &&
        vfs::mount_count() == rootMountCount;
}

#if defined(GXOS_DM25_QEMU_PARTITION_DELETE_PROOF)
static bool run_delete_partition(const storage::TargetIdentity& identity,
                                 const storage::PartitionTableModel& before,
                                 uint8_t rootMountCount)
{
    s_deleteBeforeTable = before;
    storage::PartitionEntry selected = {};
    bool found = false;
    for (uint16_t i = 0; i < before.partitionCount; ++i) {
        if (!text_equal(before.partitions[i].name, kPartitionName)) continue;
        selected = before.partitions[i];
        found = true;
        break;
    }
    if (!found || before.scheme != storage::PARTITION_SCHEME_GPT ||
        before.state != storage::DISK_STATE_VALID_GPT ||
        !before.primaryGptValid || !before.backupGptValid ||
        !before.gptCopiesAgree || vfs::mount_count() != rootMountCount ||
        block::read_sectors(identity.globalIndex, selected.startLba, 1,
            s_deleteDataBefore) != block::BLOCK_OK) {
        serial::puts(QEMU_PROOF_TAG " delete=FAIL reason=preflight-or-data-snapshot\n");
        return false;
    }

    s_deleteRequest = {};
    s_deleteRequest.targetSnapshot = identity;
    s_deleteRequest.partitionScheme = storage::PARTITION_SCHEME_GPT;
    s_deleteRequest.partitionSnapshot = selected;
    s_deleteRequest.expectedRegistryGeneration = identity.registryGeneration;
    s_deletePlan = {};
    s_deleteResult = {};
    const storage::DeletePartitionStatus prepared =
        storage::prepare_delete_partition(s_deleteRequest, s_deletePlan,
                                          s_deleteResult);
    if (prepared != storage::DELETE_PARTITION_READY_FOR_CONFIRMATION) {
        serial::puts(QEMU_PROOF_TAG " delete=FAIL prepare=");
        serial::puts(storage::delete_partition_status_name(prepared));
        serial::putc('\n');
        return false;
    }

    const storage::DeletePartitionStatus deleted =
        storage::execute_delete_partition(s_deletePlan, s_deleteResult);
    serial::puts(QEMU_PROOF_TAG " delete-result status=");
    serial::puts(storage::delete_partition_status_name(deleted));
    serial::puts(" stage=");
    serial::puts(storage::delete_partition_stage_name(s_deleteResult.lastStage));
    serial::puts(" sectorsRead=");
    serial::put_hex32(s_deleteResult.logicalSectorsRead);
    serial::puts(" sectorsWritten=");
    serial::put_hex32(s_deleteResult.logicalSectorsWritten);
    serial::puts(" flushAttempts=");
    serial::put_hex32(s_deleteResult.flushAttempts);
    serial::puts(" flushStatus=0x");
    serial::put_hex8(static_cast<uint8_t>(s_deleteResult.flushStatus));
    serial::puts(" verified=");
    serial::puts(s_deleteResult.verificationPassed ? "yes" : "no");
    serial::puts(" rollback=");
    serial::puts(s_deleteResult.rollbackAttempted ? "yes" : "no");
    serial::puts(" ranges=");
    serial::put_hex8(s_deleteResult.writeRangeCount);
    for (uint8_t i = 0; i < s_deleteResult.writeRangeCount; ++i) {
        serial::puts(" [");
        serial::put_hex64(s_deleteResult.writeRanges[i].startLba);
        serial::putc(',');
        serial::put_hex32(s_deleteResult.writeRanges[i].sectorCount);
        serial::putc(']');
    }
    serial::putc('\n');
    if (deleted != storage::DELETE_PARTITION_SUCCESS ||
        !s_deleteResult.verificationPassed ||
        s_deleteResult.finalStateUncertain ||
        s_deleteResult.rollbackAttempted ||
        s_deleteResult.logicalSectorsWritten != 4 ||
        s_deleteResult.writeRangeCount != 4 ||
        s_deleteResult.flushAttempts < 3 ||
        s_deleteResult.flushStatus != block::BLOCK_OK ||
        block::read_sectors(identity.globalIndex, selected.startLba, 1,
            s_deleteDataAfter) != block::BLOCK_OK ||
        !bytes_equal(s_deleteDataBefore, s_deleteDataAfter,
                     before.scheme == storage::PARTITION_SCHEME_GPT
                         ? identity.logicalSectorSize : 512u) ||
        !storage::parse_partition_table(identity.globalIndex, s_table) ||
        s_table.state != storage::DISK_STATE_VALID_GPT ||
        !s_table.primaryGptValid || !s_table.backupGptValid ||
        !s_table.gptCopiesAgree ||
        s_table.partitionCount + 1 != s_deleteBeforeTable.partitionCount ||
        vfs::mount_count() != rootMountCount) {
        serial::puts(QEMU_PROOF_TAG " delete=FAIL reason=write-verify-rescan-or-data-check\n");
        return false;
    }
    for (uint16_t i = 0; i < s_table.partitionCount; ++i) {
        if (bytes_equal(s_table.partitions[i].uniqueGuid,
                selected.uniqueGuid, 16)) {
            serial::puts(QEMU_PROOF_TAG " delete=FAIL reason=deleted-GUID-still-present\n");
            return false;
        }
    }
    storage::UnallocatedRegion gaps[storage::MAX_UNALLOCATED_REGIONS] = {};
    uint16_t gapCount = 0;
    if (!storage::compute_unallocated_regions(s_table,
            identity.totalLogicalSectors, identity.logicalSectorSize, gaps,
            storage::MAX_UNALLOCATED_REGIONS, gapCount) || gapCount == 0) {
        serial::puts(QEMU_PROOF_TAG " delete=FAIL reason=gap-rescan\n");
        return false;
    }
    for (uint8_t i = 0; i < s_deleteResult.writeRangeCount; ++i) {
        const storage::DeletePartitionWriteRange& range =
            s_deleteResult.writeRanges[i];
        const uint64_t end = range.startLba + range.sectorCount - 1;
        if (range.sectorCount == 0 ||
            !(end < selected.startLba || range.startLba > selected.endLba)) {
            serial::puts(QEMU_PROOF_TAG " delete=FAIL reason=metadata-write-overlapped-partition-data\n");
            return false;
        }
    }
    serial::puts(QEMU_PROOF_TAG " delete=PASS scheme=GPT entry-removed=yes copies-valid=yes data-sector-sample=unchanged gaps=");
    serial::put_hex32(gapCount);
    serial::puts(" independent-full-data-image-diff=pending-host-verifier\n");
    return true;
}
#endif

static bool run_rediscovery(const storage::TargetIdentity& identity,
                            const storage::PartitionTableModel& table,
                            uint8_t rootMountCount)
{
    if (table.state != storage::DISK_STATE_VALID_GPT ||
        !table.primaryGptValid || !table.backupGptValid ||
        !table.gptCopiesAgree) {
#if defined(GXOS_DM15_QEMU_AHCI_PROOF) || defined(GXOS_DM16_QEMU_NVME_PROOF)
        serial::puts(QEMU_PROOF_TAG " reboot-rediscovery=BLOCKED reason=existing-disk-is-not-a-verified-GPT\n");
#else
        serial::puts(QEMU_PROOF_TAG " reboot-rediscovery=BLOCKED reason=existing-disk-is-not-a-verified-GPT\n");
#endif
        return false;
    }

    for (uint16_t i = 0; i < table.partitionCount; ++i) {
        const storage::PartitionEntry& partition = table.partitions[i];
        if (!text_equal(partition.name, kPartitionName)) continue;
        const bool mountedAndRead = mount_proof_partition(identity,
            partition, false, rootMountCount);
#if defined(GXOS_DM15_QEMU_AHCI_PROOF) || defined(GXOS_DM16_QEMU_NVME_PROOF)
        serial::puts(mountedAndRead
            ? QEMU_PROOF_TAG " reboot-rediscovery=PASS explicit-remount=PASS file-bytes=PASS mounts-clean=PASS\n"
            : QEMU_PROOF_TAG " reboot-rediscovery=FAIL explicit-remount-or-file-check-failed\n");
#else
        serial::puts(mountedAndRead
            ? QEMU_PROOF_TAG " reboot-rediscovery=PASS explicit-remount=PASS file-bytes=PASS mounts-clean=PASS\n"
            : QEMU_PROOF_TAG " reboot-rediscovery=FAIL explicit-remount-or-file-check-failed\n");
#endif
        return mountedAndRead;
    }

#if defined(GXOS_DM15_QEMU_AHCI_PROOF) || defined(GXOS_DM16_QEMU_NVME_PROOF)
    serial::puts(QEMU_PROOF_TAG " reboot-rediscovery=BLOCKED reason=proof-partition-not-found\n");
#else
    serial::puts(QEMU_PROOF_TAG " reboot-rediscovery=BLOCKED reason=proof-partition-not-found\n");
#endif
    return false;
}

static bool run_fresh_lifecycle(const block::BlockDevice& device,
                                const storage::TargetIdentity& identity,
                                uint8_t rootMountCount)
{
    s_initializeRequest = {};
    s_initializeRequest.targetSnapshot = identity;
    s_initializeRequest.requestedScheme = storage::PARTITION_SCHEME_GPT;
    s_initializeRequest.mbrSignaturePolicy = storage::MBR_SIGNATURE_RANDOM_NONZERO;
    s_initializeRequest.diskGuidSource = storage::DISK_GUID_SECURE_RANDOM;
    s_initializeRequest.expectedRegistryGeneration = identity.registryGeneration;

    s_initializePlan = {};
    s_initializeResult = {};
    serial::puts(QEMU_PROOF_TAG " initialize=prepare-start\n");
    const storage::InitializeDiskStatus prepared =
        storage::prepare_initialize_disk(s_initializeRequest, s_initializePlan,
                                         s_initializeResult);
    serial::puts(QEMU_PROOF_TAG " initialize=prepare-complete status=");
    serial::puts(storage::initialize_disk_status_name(prepared));
    serial::puts(" stage=");
    serial::puts(storage::initialize_disk_stage_name(s_initializeResult.stage));
    serial::putc('\n');
    if (prepared != storage::INITIALIZE_DISK_READY_FOR_CONFIRMATION) {
        serial::puts(QEMU_PROOF_TAG " initialize=BLOCKED status=");
        serial::puts(storage::initialize_disk_status_name(prepared));
        serial::putc('\n');
        return false;
    }
    serial::puts("[DM10-TRACE] init-preflight=PASS\n");
    serial::puts(QEMU_PROOF_TAG " initialize=execute-start\n");
    const storage::InitializeDiskStatus initialized =
        storage::execute_initialize_disk(s_initializePlan, s_initializeResult);
    serial::puts(QEMU_PROOF_TAG " initialize=execute-complete status=");
    serial::puts(storage::initialize_disk_status_name(initialized));
    serial::putc('\n');
    if (initialized != storage::INITIALIZE_DISK_SUCCESS ||
        !s_initializeResult.verificationPassed ||
        s_initializeResult.finalStateUncertain) {
        serial::puts(QEMU_PROOF_TAG " initialize=FAIL status=");
        serial::puts(storage::initialize_disk_status_name(initialized));
        serial::puts(" stage=");
        serial::puts(storage::initialize_disk_stage_name(s_initializeResult.stage));
        serial::puts(" writeAttempted=");
        serial::puts(s_initializeResult.writeAttempted ? "yes" : "no");
        serial::puts(" writesCompleted=");
        serial::put_hex32(s_initializeResult.writesCompleted);
        serial::puts(" writeMayHaveReachedMedia=");
        serial::puts(s_initializeResult.writeMayHaveReachedMedia ? "yes" : "no");
        serial::puts(" writeStages=0x");
        serial::put_hex32(s_initializeResult.writeStagesCompleted);
        serial::puts(" rollbackAttempted=");
        serial::puts(s_initializeResult.rollbackAttempted ? "yes" : "no");
        serial::puts(" rollbackSucceeded=");
        serial::puts(s_initializeResult.rollbackSucceeded ? "yes" : "no");
        serial::puts(" finalUncertain=");
        serial::puts(s_initializeResult.finalStateUncertain ? "yes" : "no");
        serial::puts(" finalState=");
        serial::puts(storage::disk_state_name(s_initializeResult.finalDetectedState));
        serial::puts(" flushAttempted=");
        serial::puts(s_initializeResult.flushAttempted ? "yes" : "no");
        if (s_initializeResult.flushAttempted) {
            serial::puts(" flushOutcome=0x");
            serial::put_hex8(static_cast<uint8_t>(s_initializeResult.flushOutcome));
            serial::puts(" flushStatus=0x");
            serial::put_hex8(static_cast<uint8_t>(s_initializeResult.flushStatus));
        }
        serial::puts(" firstFailedStage=");
        serial::puts(storage::initialize_disk_stage_name(
            s_initializeResult.firstFailedStage));
        serial::puts(" lastStage=");
        serial::puts(storage::initialize_disk_stage_name(
            s_initializeResult.lastStage));
        serial::puts(" failureOutcome=0x");
        serial::put_hex8(static_cast<uint8_t>(s_initializeResult.failureOutcome));
        serial::puts(" rollbackStage=");
        serial::puts(storage::initialize_disk_stage_name(
            s_initializeResult.rollbackStage));
        serial::puts(" rollbackWriteAttempted=");
        serial::puts(s_initializeResult.rollbackWriteAttempted ? "yes" : "no");
        if (s_initializeResult.rollbackWriteAttempted) {
            serial::puts(" rollbackWriteStatus=0x");
            serial::put_hex8(static_cast<uint8_t>(s_initializeResult.rollbackWriteStatus));
        }
        serial::puts(" rollbackFlushAttempted=");
        serial::puts(s_initializeResult.rollbackFlushAttempted ? "yes" : "no");
        if (s_initializeResult.rollbackFlushAttempted) {
            serial::puts(" rollbackFlushStatus=0x");
            serial::put_hex8(static_cast<uint8_t>(s_initializeResult.rollbackFlushStatus));
        }
        serial::puts(" rollbackVerified=");
        serial::puts(!s_initializeResult.rollbackAttempted ? "not-needed" :
            s_initializeResult.rollbackVerificationPassed ? "yes" : "no");
        serial::puts(" detail=");
        serial::puts(s_initializeResult.diagnostic);
        serial::putc('\n');
        if (s_initializeResult.failedBlockDiagnosticValid)
            print_block_io_diagnostic(s_initializeResult.failedBlockDiagnostic);
        return false;
    }
    serial::puts("[DM10-TRACE] GPT-writes-complete flush-and-verify=PASS\n");

    if (!storage::parse_partition_table(identity.globalIndex, s_table) ||
        s_table.state != storage::DISK_STATE_VALID_GPT ||
        !s_table.primaryGptValid || !s_table.backupGptValid ||
        !s_table.gptCopiesAgree) {
        serial::puts(QEMU_PROOF_TAG " initialize=PASS parse-after-initialize=FAIL\n");
        return false;
    }

    for (uint16_t i = 0; i < storage::MAX_UNALLOCATED_REGIONS; ++i)
        s_regions[i] = {};
    uint16_t regionCount = 0;
    if (!storage::compute_unallocated_regions(s_table, device.totalSectors,
            device.sectorSize, s_regions, storage::MAX_UNALLOCATED_REGIONS,
            regionCount) || regionCount == 0) {
        serial::puts(QEMU_PROOF_TAG " initialize=PASS free-space-rescan=FAIL\n");
        return false;
    }

    s_createRequest = {};
    s_createRequest.targetSnapshot = identity;
    s_createRequest.requestedScheme = storage::PARTITION_SCHEME_GPT;
    s_createRequest.selectedRegion = s_regions[0];
    s_createRequest.useMaximumSize = true;
    s_createRequest.partitionType = storage::CREATE_PARTITION_GPT_BASIC_DATA;
    copy_text(s_createRequest.gptName, sizeof(s_createRequest.gptName),
              kPartitionName);
    s_createRequest.expectedRegistryGeneration = block::registry_generation();

    s_createResult = {};
    serial::puts("[DM10-TRACE] create-partition=start\n");
    const storage::CreatePartitionStatus created = storage::create_partition(
        s_createRequest, s_createResult);
    if (created != storage::CREATE_PARTITION_SUCCESS ||
        !s_createResult.verificationPassed || s_createResult.finalStateUncertain) {
        serial::puts(QEMU_PROOF_TAG " create-partition=FAIL status=");
        serial::puts(storage::create_partition_status_name(created));
        serial::puts(" firstFailedStage=");
        serial::puts(storage::create_partition_stage_name(
            s_createResult.firstFailedStage));
        serial::puts(" writesCompleted=");
        serial::put_hex32(s_createResult.writesCompleted);
        serial::puts(" writeMayHaveReachedMedia=");
        serial::puts(s_createResult.writeMayHaveReachedMedia ? "yes" : "no");
        serial::puts(" flushAttempted=");
        serial::puts(s_createResult.flushAttempted ? "yes" : "no");
        if (s_createResult.flushAttempted) {
            serial::puts(" flushStatus=0x");
            serial::put_hex8(static_cast<uint8_t>(s_createResult.flushStatus));
        }
        serial::puts(" rollbackAttempted=");
        serial::puts(s_createResult.rollbackAttempted ? "yes" : "no");
        if (s_createResult.rollbackWriteAttempted) {
            serial::puts(" rollbackWriteStatus=0x");
            serial::put_hex8(static_cast<uint8_t>(s_createResult.rollbackWriteStatus));
        }
        serial::puts(" rollbackVerified=");
        serial::puts(!s_createResult.rollbackAttempted ? "not-needed" :
            s_createResult.rollbackVerificationPassed ? "yes" : "no");
        serial::putc('\n');
        if (s_createResult.failedBlockDiagnostic.valid)
            print_block_io_diagnostic(s_createResult.failedBlockDiagnostic);
        return false;
    }
    serial::puts("[DM10-TRACE] create-partition=PASS\n");

    if (!storage::parse_partition_table(identity.globalIndex, s_table) ||
        s_table.state != storage::DISK_STATE_VALID_GPT ||
        !s_table.primaryGptValid || !s_table.backupGptValid ||
        !s_table.gptCopiesAgree) {
        serial::puts(QEMU_PROOF_TAG " create-partition=PASS rescan=FAIL\n");
        return false;
    }

    s_formatRequest = {};
    s_formatRequest.targetSnapshot = identity;
    s_formatRequest.partitionScheme = storage::PARTITION_SCHEME_GPT;
    s_formatRequest.partitionSnapshot = s_createResult.createdPartition;
    for (uint8_t i = 0; i < sizeof(s_formatRequest.gptDiskGuid); ++i)
        s_formatRequest.gptDiskGuid[i] = s_table.primaryDiskGuid[i];
    copy_text(s_formatRequest.volumeLabel, sizeof(s_formatRequest.volumeLabel),
              "DM9PROOF");
    s_formatRequest.expectedRegistryGeneration = block::registry_generation();

    s_formatResult = {};
    serial::puts("[DM10-TRACE] format-fat32=start\n");
    const storage::Fat32FormatStatus formatted =
        storage::format_fat32_partition(s_formatRequest, s_formatResult);
    if (formatted != storage::FAT32_FORMAT_SUCCESS ||
        !s_formatResult.verificationPassed || s_formatResult.finalStateUncertain) {
        serial::puts(QEMU_PROOF_TAG " format-fat32=FAIL status=");
        serial::puts(storage::fat32_format_status_name(formatted));
        serial::puts(" firstFailedStage=");
        serial::puts(storage::fat32_format_stage_name(
            s_formatResult.firstFailedStage));
        serial::puts(" sectorsWritten=");
        serial::put_hex32(s_formatResult.sectorsWritten);
        serial::puts(" writeMayHaveReachedMedia=");
        serial::puts(s_formatResult.writeMayHaveReachedMedia ? "yes" : "no");
        serial::puts(" flushAttempted=");
        serial::puts(s_formatResult.flushAttempted ? "yes" : "no");
        if (s_formatResult.flushAttempted) {
            serial::puts(" flushStatus=0x");
            serial::put_hex8(static_cast<uint8_t>(s_formatResult.flushStatus));
        }
        serial::puts(" rollbackAttempted=");
        serial::puts(s_formatResult.rollbackAttempted ? "yes" : "no");
        if (s_formatResult.rollbackWriteAttempted) {
            serial::puts(" rollbackWriteStatus=0x");
            serial::put_hex8(static_cast<uint8_t>(s_formatResult.rollbackWriteStatus));
        }
        serial::puts(" rollbackVerified=");
        serial::puts(!s_formatResult.rollbackAttempted ? "not-needed" :
            s_formatResult.rollbackVerificationPassed ? "yes" : "no");
        serial::putc('\n');
        if (s_formatResult.failedBlockDiagnostic.valid)
            print_block_io_diagnostic(s_formatResult.failedBlockDiagnostic);
        return false;
    }
    serial::puts("[DM10-TRACE] format-fat32=PASS\n");
#if defined(GXOS_DM22_QEMU_FAT32_PROOF) || \
    defined(GXOS_DM24_QEMU_FAT32_4KN_PROOF)
    serial::puts(QEMU_PROOF_TAG " format-geometry=PASS sectors=");
    serial::put_hex32(s_formatResult.geometry.totalSectors);
    serial::puts(" bps=");
    serial::put_hex32(s_formatResult.geometry.bytesPerSector);
    serial::puts(" spc=");
    serial::put_hex32(s_formatResult.geometry.sectorsPerCluster);
    serial::puts(" clusterBytes=");
    serial::put_hex32(s_formatResult.geometry.clusterSizeBytes);
    serial::puts(" firstData=");
    serial::put_hex32(s_formatResult.geometry.firstDataSector);
    serial::puts(" rootCluster=");
    serial::put_hex32(s_formatResult.geometry.rootCluster);
    serial::puts(" clusters=");
    serial::put_hex32(s_formatResult.geometry.clusterCount);
    serial::puts(" fatSectors=");
    serial::put_hex32(s_formatResult.geometry.fatSizeSectors);
    serial::puts(" metadataWrites=");
    serial::put_hex64(s_formatResult.sectorsWritten);
    serial::puts(" rollbackBytes=");
    serial::put_hex32(s_formatResult.rollbackRecordBytes);
    serial::puts(" scanComplete=");
    serial::puts(s_formatResult.scanCoverageComplete ? "yes" : "no");
    serial::puts(" scannedSectors=");
    serial::put_hex64(s_formatResult.scanZeroVerifiedSectors);
    serial::puts(" scanBytes=");
    serial::put_hex64(s_formatResult.scanBytesRead);
    serial::puts(" scanRequests=");
    serial::put_hex32(s_formatResult.scanReadRequests);
    serial::puts(" scanLargestRequest=");
    serial::put_hex32(s_formatResult.scanLargestRequestBytes);
    serial::puts(" scanTicks=");
    serial::put_hex64(s_formatResult.scanElapsedTicks);
    serial::putc('\n');
#endif

    s_proofPartition = {};
    bool partitionFound = false;
    if (!storage::parse_partition_table(identity.globalIndex, s_table)) return false;
    for (uint16_t i = 0; i < s_table.partitionCount; ++i) {
        if (text_equal(s_table.partitions[i].name, kPartitionName)) {
            s_proofPartition = s_table.partitions[i];
            partitionFound = true;
            break;
        }
    }
    if (!partitionFound) return false;

#if defined(GXOS_DM22_QEMU_FAT32_PROOF) || \
    defined(GXOS_DM24_QEMU_FAT32_4KN_PROOF)
#if defined(GXOS_DM24_QEMU_FAT32_4KN_PROOF)
    const uint64_t minimumProofBytes = 600ull * 1024u * 1024u;
    const uint32_t allocationStart = 70001u;
    if (device.sectorSize != 4096u ||
        static_cast<uint64_t>(s_proofPartition.sectorCount) *
            device.sectorSize < minimumProofBytes ||
        s_formatResult.geometry.clusterCount <= allocationStart) {
        serial::puts(QEMU_PROOF_TAG " large-volume=FAIL reason=4Kn-geometry-or-capacity\n");
        return false;
    }
#else
    const uint64_t minimumProofBytes = 8ull * 1024u * 1024u * 1024u;
    const uint32_t allocationStart = 120001u;
    if (static_cast<uint64_t>(s_proofPartition.sectorCount) *
            device.sectorSize < minimumProofBytes ||
        s_formatResult.geometry.clusterCount <= allocationStart) {
        serial::puts(QEMU_PROOF_TAG " large-volume=FAIL reason=partition-under-8GiB\n");
        return false;
    }
#endif
    uint8_t* boot = s_proofSectorA;
    uint8_t* fsinfo = s_proofSectorA;
    uint8_t* backupFsinfo = s_proofSectorB;
    if (block::read_sectors(identity.globalIndex, s_proofPartition.startLba,
            1, boot) != block::BLOCK_OK ||
        static_cast<uint16_t>(boot[11] |
            (static_cast<uint16_t>(boot[12]) << 8)) != device.sectorSize) {
        serial::puts(QEMU_PROOF_TAG " allocation-hint=FAIL reason=boot-read-or-bps\n");
        return false;
    }
    const uint16_t fsInfoSector =
        static_cast<uint16_t>(boot[48] | (static_cast<uint16_t>(boot[49]) << 8));
    const uint16_t backupBootSector =
        static_cast<uint16_t>(boot[50] | (static_cast<uint16_t>(boot[51]) << 8));
    const uint64_t backupFsInfoLba = s_proofPartition.startLba +
        backupBootSector + fsInfoSector;
    if (fsInfoSector == 0 || fsInfoSector >=
            static_cast<uint16_t>(boot[14] |
                (static_cast<uint16_t>(boot[15]) << 8)) ||
        block::read_sectors(identity.globalIndex,
            s_proofPartition.startLba + fsInfoSector, 1, fsinfo) !=
                block::BLOCK_OK ||
        block::read_sectors(identity.globalIndex, backupFsInfoLba, 1,
                            backupFsinfo) != block::BLOCK_OK ||
        fsinfo[0] != 0x52 || fsinfo[1] != 0x52 ||
        fsinfo[2] != 0x61 || fsinfo[3] != 0x41 ||
        fsinfo[484] != 0x72 || fsinfo[485] != 0x72 ||
        fsinfo[486] != 0x41 || fsinfo[487] != 0x61 ||
        fsinfo[492] != 0x03 || fsinfo[493] != 0x00 ||
        fsinfo[494] != 0x00 || fsinfo[495] != 0x00 ||
        fsinfo[508] != 0x00 || fsinfo[509] != 0x00 ||
        fsinfo[510] != 0x55 || fsinfo[511] != 0xAA ||
        !bytes_equal(fsinfo, backupFsinfo, device.sectorSize)) {
        serial::puts(QEMU_PROOF_TAG " allocation-hint=FAIL reason=FSInfo-invalid\n");
        return false;
    }
    for (uint32_t i = 0; i < 4; ++i) {
        const uint8_t value = static_cast<uint8_t>(allocationStart >> (i * 8u));
        fsinfo[492 + i] = value;
        backupFsinfo[492 + i] = value;
    }
    if (block::write_sectors(identity.globalIndex,
            s_proofPartition.startLba + fsInfoSector, 1, fsinfo) !=
                block::BLOCK_OK ||
        block::write_sectors(identity.globalIndex, backupFsInfoLba, 1,
                             backupFsinfo) != block::BLOCK_OK ||
        block::flush(identity.globalIndex) != block::BLOCK_OK) {
        serial::puts(QEMU_PROOF_TAG " allocation-hint=FAIL reason=write-or-flush\n");
        return false;
    }
    if (block::read_sectors(identity.globalIndex,
            s_proofPartition.startLba + fsInfoSector, 1, fsinfo) !=
                block::BLOCK_OK ||
        block::read_sectors(identity.globalIndex, backupFsInfoLba, 1,
                            backupFsinfo) != block::BLOCK_OK ||
        fsinfo[492] != static_cast<uint8_t>(allocationStart) ||
        fsinfo[493] != static_cast<uint8_t>(allocationStart >> 8) ||
        fsinfo[494] != static_cast<uint8_t>(allocationStart >> 16) ||
        fsinfo[495] != static_cast<uint8_t>(allocationStart >> 24) ||
        !bytes_equal(fsinfo, backupFsinfo, device.sectorSize)) {
        serial::puts(QEMU_PROOF_TAG " allocation-hint=FAIL reason=readback\n");
        return false;
    }
    serial::puts(QEMU_PROOF_TAG " allocation-hint=PASS cluster=");
    serial::put_hex32(allocationStart);
    serial::putc('\n');
    serial::puts(QEMU_PROOF_TAG " partition-bytes=");
    serial::put_hex64(static_cast<uint64_t>(s_proofPartition.sectorCount) *
                      device.sectorSize);
    serial::putc('\n');
#endif

    serial::puts("[DM10-TRACE] mount-file-roundtrip=start\n");
    const bool mountAndFileIo = mount_proof_partition(identity,
        s_proofPartition, true, rootMountCount);
    serial::puts(mountAndFileIo
        ? "[DM10-TRACE] mount-file-unmount-remount-persistent-read=PASS\n"
        : "[DM10-TRACE] mount-file-unmount-remount-persistent-read=FAIL\n");
#if defined(GXOS_DM15_QEMU_AHCI_PROOF)
    serial::puts(mountAndFileIo
        ? QEMU_PROOF_TAG " lifecycle=PASS initialize=PASS create-partition=PASS format-fat32=PASS mount=PASS mkdir=PASS file-write=PASS file-read=PASS unmount=PASS remount-read=PASS mounts-clean=PASS\n"
        : QEMU_PROOF_TAG " lifecycle=FAIL mount-or-file-operation-failed\n");
#else
    serial::puts(mountAndFileIo
        ? QEMU_PROOF_TAG " lifecycle=PASS initialize=PASS create-partition=PASS format-fat32=PASS mount=PASS mkdir=PASS file-write=PASS file-read=PASS unmount=PASS remount-read=PASS mounts-clean=PASS\n"
        : QEMU_PROOF_TAG " lifecycle=FAIL mount-or-file-operation-failed\n");
#endif
#if defined(GXOS_DM25_QEMU_PARTITION_DELETE_PROOF)
    serial::puts(mountAndFileIo
        ? QEMU_PROOF_TAG " delete-ready=PASS lifecycle=verified-and-unmounted\n"
        : QEMU_PROOF_TAG " delete-ready=FAIL lifecycle=not-verified\n");
#endif
    return mountAndFileIo;
}

} // namespace

void run(bool rootStorageMounted)
{
#if defined(GXOS_DM24_QEMU_FAT32_4KN_PROOF)
    serial::puts(QEMU_PROOF_TAG " proof=START transport=USB-MASS logicalSectorSize=4096 compile-time-opt-in=yes\n");
#elif defined(GXOS_DM16_QEMU_NVME_PROOF)
    serial::puts(QEMU_PROOF_TAG " proof=START transport=NVMe compile-time-opt-in=yes\n");
#elif defined(GXOS_DM15_QEMU_AHCI_PROOF)
    serial::puts(QEMU_PROOF_TAG " proof=START transport=AHCI compile-time-opt-in=yes\n");
#else
    serial::puts(QEMU_PROOF_TAG " proof=START compile-time-opt-in=yes\n");
#endif
#if !defined(GXOS_DM15_QEMU_AHCI_PROOF) && !defined(GXOS_DM16_QEMU_NVME_PROOF) && \
    !defined(GXOS_DM24_QEMU_FAT32_4KN_PROOF)
    if (!rootStorageMounted || vfs::mount_count() == 0) {
#if defined(GXOS_DM15_QEMU_AHCI_PROOF) || defined(GXOS_DM16_QEMU_NVME_PROOF)
        serial::puts(QEMU_PROOF_TAG " proof=BLOCKED reason=root-storage-not-mounted\n");
#else
        serial::puts(QEMU_PROOF_TAG " proof=BLOCKED reason=root-storage-not-mounted\n");
#endif
        return;
    }
#else
    (void)rootStorageMounted;
#endif

    block::BlockDevice device = {};
    storage::TargetIdentity identity = {};
    if (!find_qemu_secondary(device, identity)) {
#if defined(GXOS_DM16_QEMU_NVME_PROOF)
        serial::puts(QEMU_PROOF_TAG " proof=BLOCKED reason=expected-QEMU-NVMe-namespace-or-boot-identity-unknown\n");
#elif defined(GXOS_DM15_QEMU_AHCI_PROOF)
        serial::puts(QEMU_PROOF_TAG " proof=BLOCKED reason=expected-unmounted-QEMU-AHCI-port1-or-boot-identity-unknown\n");
#elif defined(GXOS_DM24_QEMU_FAT32_4KN_PROOF)
        serial::puts(QEMU_PROOF_TAG " proof=BLOCKED reason=expected-unmounted-DM24-USB-mass-storage-or-boot-identity-unknown\n");
#else
        serial::puts(QEMU_PROOF_TAG " proof=BLOCKED reason=expected-unmounted-QEMU-ATA-secondary-not-found-or-boot-identity-unknown\n");
#endif
        return;
    }

    serial::puts(QEMU_PROOF_TAG " target=selected name=");
    serial::puts(device.name);
    serial::puts(" model=");
    serial::puts(device.model);
    serial::puts(" registrationId=");
    serial::put_hex64(identity.registrationId);
    serial::puts(" sectors=");
    serial::put_hex64(device.totalSectors);
    serial::puts(" logicalSectorSize=");
    serial::put_hex32(device.sectorSize);
    serial::puts(" physicalSectorSize=");
    if (device.physicalSectorSize != 0)
        serial::put_hex32(device.physicalSectorSize);
    else
        serial::puts("unknown");
    serial::puts(" capacityBytes=");
    serial::put_hex64(device.totalSectors * device.sectorSize);
#if defined(GXOS_DM24_QEMU_FAT32_4KN_PROOF)
    serial::puts(" boot=DefinitelyNotBoot shared-write=");
    serial::puts(device.writeFn ? "enabled" : "disabled");
    serial::putc('\n');
    serial::puts(QEMU_PROOF_TAG " secondary-disk-detected transport=USB-MASS boot-provenance=DefinitelyNotBoot\n");
#elif defined(GXOS_DM16_QEMU_NVME_PROOF)
    serial::puts(" nsid="); serial::put_hex32(device.namespaceId);
    serial::puts(" pci="); serial::put_hex8(device.pciBus);
    serial::putc(':'); serial::put_hex8(device.pciDevice);
    serial::putc('.'); serial::put_hex8(device.pciFunction);
    serial::puts(" boot=DefinitelyNotBoot shared-write=");
    serial::puts(device.writeFn ? "enabled" : "disabled");
    serial::putc('\n');
    serial::puts(QEMU_PROOF_TAG " secondary-disk-detected boot-provenance=DefinitelyNotBoot\n");
#elif defined(GXOS_DM15_QEMU_AHCI_PROOF)
    serial::puts(" port="); serial::put_hex8(device.ahciPort);
    serial::puts(" boot=DefinitelyNotBoot shared-write=");
    serial::puts(device.writeFn ? "enabled" : "disabled");
    serial::putc('\n');
    serial::puts(QEMU_PROOF_TAG " secondary-disk-detected boot-provenance=DefinitelyNotBoot\n");
#else
    serial::puts(" boot=DefinitelyNotBoot\n");
    serial::puts("[DM10-TRACE] secondary-disk-detected boot-provenance=DefinitelyNotBoot\n");
#endif

#if defined(GXOS_DM24_QEMU_FAT32_4KN_PROOF)
    if (device.type != block::BDEV_USB_MASS || device.sectorSize != 4096u) {
        serial::puts(QEMU_PROOF_TAG " geometry=FAIL reason=USB-READ-CAPACITY-did-not-report-4096\n");
        return;
    }
    serial::puts(QEMU_PROOF_TAG " geometry=PASS transport=USB-MASS logicalSectorSize=4096 capacityLba=");
    serial::put_hex64(device.totalSectors);
    serial::puts(" capacityBytes=");
    serial::put_hex64(device.totalSectors * device.sectorSize);
    serial::putc('\n');
#endif

    serial::puts(QEMU_PROOF_TAG " target-table-parse=start\n");
    if (!storage::parse_partition_table(identity.globalIndex, s_table)) {
        serial::puts(QEMU_PROOF_TAG " proof=BLOCKED reason=target-table-unreadable\n");
        return;
    }
    serial::puts(QEMU_PROOF_TAG " target-table-parse=complete state=");
    serial::puts(storage::disk_state_name(s_table.state));
    serial::putc('\n');

#if defined(GXOS_DM25_QEMU_PARTITION_DELETE_PROOF)
    if (s_table.state == storage::DISK_STATE_VALID_GPT &&
        s_table.primaryGptValid && s_table.backupGptValid &&
        s_table.gptCopiesAgree && s_table.partitionCount == 0) {
        serial::puts(QEMU_PROOF_TAG " delete-restart=PASS valid-empty-GPT=yes mounts-clean=yes\n");
        return;
    }
#endif

#if defined(GXOS_DM17_SINGLE_BLOCK_PROOF) && \
    defined(GXOS_DM16_NVME_PRIVATE_PROOF)
    if (s_table.state != storage::DISK_STATE_NOT_INITIALIZED) {
        serial::puts(QEMU_PROOF_TAG " private-proof=FAIL reason=target-not-blank\n");
        return;
    }
    const bool privateProof = verify_dm17_nvme_data_integrity(
        identity.globalIndex, device);
    serial::puts(privateProof
        ? QEMU_PROOF_TAG " private-proof=PASS raw-write=PASS readback=PASS restore=PASS flush=PASS shared-write-stage=separate\n"
        : QEMU_PROOF_TAG " private-proof=FAIL data-integrity-gate-failed\n");
    return;
#elif defined(GXOS_DM16_QEMU_NVME_PROOF) && \
    defined(GXOS_DM16_NVME_PRIVATE_PROOF)
    if (s_table.state != storage::DISK_STATE_NOT_INITIALIZED) {
        serial::puts(QEMU_PROOF_TAG " private-proof=FAIL reason=target-not-blank\n");
        return;
    }
    const bool privateProof = verify_private_nvme_write_flush_stress(
        identity.globalIndex, device);
    serial::puts(privateProof
        ? QEMU_PROOF_TAG " private-proof=PASS raw-write=PASS readback=PASS flush=PASS restore=PASS stressCycles=100 pageCrossing=PASS shared-write=disabled\n"
        : QEMU_PROOF_TAG " private-proof=FAIL raw-write-flush-or-restore-failed\n");
    return;
#elif defined(GXOS_DM15_QEMU_AHCI_PROOF) && \
    defined(GXOS_DM15_AHCI_PRIVATE_PROOF)
    if (s_table.state != storage::DISK_STATE_NOT_INITIALIZED) {
        serial::puts(QEMU_PROOF_TAG " private-proof=FAIL reason=target-not-blank\n");
        return;
    }
    const bool privateProof = verify_private_ahci_write_and_flush(
        identity.globalIndex, device);
    serial::puts(privateProof
        ? QEMU_PROOF_TAG " private-proof=PASS raw-write=PASS readback=PASS flush=PASS restored=PASS shared-write=disabled\n"
        : QEMU_PROOF_TAG " private-proof=FAIL raw-write-or-restore-failed\n");
    return;
#endif

    const uint8_t rootMountCount = vfs::mount_count();
    if (s_table.state == storage::DISK_STATE_NOT_INITIALIZED) {
        (void)run_fresh_lifecycle(device, identity, rootMountCount);
        return;
    }

    const bool rediscovered = run_rediscovery(identity, s_table, rootMountCount);
#if defined(GXOS_DM25_QEMU_PARTITION_DELETE_PROOF)
    if (!rediscovered ||
        !run_delete_partition(identity, s_table, rootMountCount))
        serial::puts(QEMU_PROOF_TAG " delete=FAIL reason=rediscovery-or-transaction\n");
#else
    (void)rediscovered;
#endif
}

} // namespace qemu_dm9_storage_proof
} // namespace kernel

#endif
