#include "include/kernel/qemu_dm9_storage_proof.h"

#if defined(GXOS_DM9_QEMU_STORAGE_PROOF) || \
    defined(GXOS_DM15_QEMU_AHCI_PROOF) || \
    defined(GXOS_DM16_QEMU_NVME_PROOF)

#include "include/kernel/block_device.h"
#include "include/kernel/ata.h"
#if defined(GXOS_DM15_QEMU_AHCI_PROOF)
#include "include/kernel/ahci.h"
#endif
#if defined(GXOS_DM16_QEMU_NVME_PROOF)
#include "include/kernel/nvme.h"
#endif
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

#if defined(GXOS_DM16_QEMU_NVME_PROOF)
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

static bool bytes_equal(const char* left, const char* right, uint32_t count)
{
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
#if defined(GXOS_DM16_QEMU_NVME_PROOF)
    if (!block::copy_device(index, out) || !out.active ||
        out.type != block::BDEV_NVME || !out.pciLocationValid ||
        out.namespaceId != 1u || !out.readFn ||
        !text_equal(out.serial, "GXOSDM16NVME") ||
        !storage::capture_target_identity(index, identity)) return false;
#if defined(GXOS_DM16_NVME_PRIVATE_PROOF)
    if (out.writeFn || out.flushFn || out.flushSemanticsKnown) return false;
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
#if defined(GXOS_DM16_QEMU_NVME_PROOF)
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
#if defined(GXOS_DM16_QEMU_NVME_PROOF)
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
#if defined(GXOS_DM16_QEMU_NVME_PROOF)
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
        serial::puts(" flush=");
        serial::puts(candidate.flushFn ? "yes" : "no");
#if defined(GXOS_DM16_QEMU_NVME_PROOF)
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
            !patternMatches || restoreStatus != block::BLOCK_OK ||
            restoreFlushStatus != block::BLOCK_OK || !restored) {
            serial::puts(QEMU_PROOF_TAG " private-cycle=FAIL cycle=");
            serial::put_hex32(cycle + 1u);
            serial::puts(" write="); serial::put_hex8(static_cast<uint8_t>(writeStatus));
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

    char bytes[sizeof(kPayload)] = {};
    const int32_t count = vfs::read(handle, bytes,
        static_cast<uint32_t>(sizeof(kPayload) - 1u));
    const vfs::Status closeStatus = vfs::close(handle);
    return count == static_cast<int32_t>(sizeof(kPayload) - 1u) &&
        bytes_equal(bytes, kPayload,
            static_cast<uint32_t>(sizeof(kPayload) - 1u)) &&
        closeStatus == vfs::VFS_OK;
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
        contentsValid = vfs::mkdir(kProofDirectoryPath) == vfs::VFS_OK &&
            vfs::create_file(kProofFilePath, kPayload,
                static_cast<uint32_t>(sizeof(kPayload) - 1u)) ==
                static_cast<int32_t>(sizeof(kPayload) - 1u) &&
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

static void run_rediscovery(const storage::TargetIdentity& identity,
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
        return;
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
        return;
    }

#if defined(GXOS_DM15_QEMU_AHCI_PROOF) || defined(GXOS_DM16_QEMU_NVME_PROOF)
    serial::puts(QEMU_PROOF_TAG " reboot-rediscovery=BLOCKED reason=proof-partition-not-found\n");
#else
    serial::puts(QEMU_PROOF_TAG " reboot-rediscovery=BLOCKED reason=proof-partition-not-found\n");
#endif
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
    return mountAndFileIo;
}

} // namespace

void run(bool rootStorageMounted)
{
#if defined(GXOS_DM16_QEMU_NVME_PROOF)
    serial::puts(QEMU_PROOF_TAG " proof=START transport=NVMe compile-time-opt-in=yes\n");
#elif defined(GXOS_DM15_QEMU_AHCI_PROOF)
    serial::puts(QEMU_PROOF_TAG " proof=START transport=AHCI compile-time-opt-in=yes\n");
#else
    serial::puts(QEMU_PROOF_TAG " proof=START compile-time-opt-in=yes\n");
#endif
#if !defined(GXOS_DM15_QEMU_AHCI_PROOF) && !defined(GXOS_DM16_QEMU_NVME_PROOF)
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
#else
        serial::puts(QEMU_PROOF_TAG " proof=BLOCKED reason=expected-unmounted-QEMU-ATA-secondary-not-found-or-boot-identity-unknown\n");
#endif
        return;
    }

#if defined(GXOS_DM15_QEMU_AHCI_PROOF) || defined(GXOS_DM16_QEMU_NVME_PROOF)
    serial::puts(QEMU_PROOF_TAG " target=selected name=");
#else
    serial::puts(QEMU_PROOF_TAG " target=selected name=");
#endif
    serial::puts(device.name);
    serial::puts(" model=");
    serial::puts(device.model);
    serial::puts(" registrationId=");
    serial::put_hex64(identity.registrationId);
    serial::puts(" sectors=");
    serial::put_hex64(device.totalSectors);
#if defined(GXOS_DM16_QEMU_NVME_PROOF)
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

#if defined(GXOS_DM15_QEMU_AHCI_PROOF)
    serial::puts(QEMU_PROOF_TAG " target-table-parse=start\n");
#else
    serial::puts(QEMU_PROOF_TAG " target-table-parse=start\n");
#endif
    if (!storage::parse_partition_table(identity.globalIndex, s_table)) {
#if defined(GXOS_DM15_QEMU_AHCI_PROOF)
        serial::puts(QEMU_PROOF_TAG " proof=BLOCKED reason=target-table-unreadable\n");
#else
        serial::puts(QEMU_PROOF_TAG " proof=BLOCKED reason=target-table-unreadable\n");
#endif
        return;
    }
#if defined(GXOS_DM15_QEMU_AHCI_PROOF)
    serial::puts(QEMU_PROOF_TAG " target-table-parse=complete state=");
#else
    serial::puts(QEMU_PROOF_TAG " target-table-parse=complete state=");
#endif
    serial::puts(storage::disk_state_name(s_table.state));
    serial::putc('\n');

#if defined(GXOS_DM16_QEMU_NVME_PROOF) && \
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

    run_rediscovery(identity, s_table, rootMountCount);
}

} // namespace qemu_dm9_storage_proof
} // namespace kernel

#endif
