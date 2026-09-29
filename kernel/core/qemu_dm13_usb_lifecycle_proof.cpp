#include "include/kernel/qemu_dm13_usb_lifecycle_proof.h"

#if defined(GXOS_DM13_QEMU_USB_WRITE_PROOF) || \
    defined(GXOS_DM13_QEMU_USB_LIFECYCLE_PROOF)

#include "include/kernel/block_device.h"
#include "include/kernel/disk_initialization.h"
#include "include/kernel/fat32_formatter.h"
#include "include/kernel/partition_operations.h"
#include "include/kernel/partition_table.h"
#include "include/kernel/serial_debug.h"
#include "include/kernel/storage_manager.h"
#include "include/kernel/usb_storage.h"
#include "include/kernel/vfs.h"

namespace kernel {
namespace qemu_dm13_usb_lifecycle_proof {
namespace {

static const char kMountPath[] = "/mnt/dm13-usb-proof";
static const char kDirectoryPath[] = "/mnt/dm13-usb-proof/dm13";
static const char kFilePath[] = "/mnt/dm13-usb-proof/dm13/proof.bin";
static const char kPartitionName[] = "DM13 QEMU USB Proof";
static const char kPayload[] = "guideXOS DM13 USB lifecycle proof 001\r\n";

// Keep large parser and transaction snapshots off the 16 KiB boot stack.
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
static storage::PartitionEntry s_partition = {};
alignas(4096) static uint8_t s_primaryGptSnapshot[34u * 512u] = {};
alignas(4096) static uint8_t s_backupGptSnapshot[33u * 512u] = {};

static bool text_equal(const char* left, const char* right)
{
    if (!left || !right) return left == right;
    while (*left && *right && *left == *right) { ++left; ++right; }
    return *left == *right;
}

static bool bytes_equal(const uint8_t* left, const uint8_t* right,
                        uint32_t count)
{
    for (uint32_t i = 0; i < count; ++i)
        if (left[i] != right[i]) return false;
    return true;
}

static bool read_gpt_snapshot(const storage::TargetIdentity& target,
                              uint64_t totalSectors, uint8_t* primary,
                              uint8_t* backup)
{
    if (!primary || !backup || totalSectors < 67u) return false;
    const uint64_t backupStart = totalSectors - 33u;
    return block::read_sectors_checked(target.globalIndex, 0, 34, primary,
               34u * 512u) == block::BLOCK_OK &&
        block::read_sectors_checked(target.globalIndex, backupStart, 33,
               backup, 33u * 512u) == block::BLOCK_OK;
}

static bool gpt_unchanged(const storage::TargetIdentity& target,
                          uint64_t totalSectors)
{
    alignas(4096) static uint8_t primaryAfter[34u * 512u] = {};
    alignas(4096) static uint8_t backupAfter[33u * 512u] = {};
    if (!read_gpt_snapshot(target, totalSectors, primaryAfter, backupAfter))
        return false;
    return bytes_equal(s_primaryGptSnapshot, primaryAfter,
                       sizeof(s_primaryGptSnapshot)) &&
        bytes_equal(s_backupGptSnapshot, backupAfter,
                    sizeof(s_backupGptSnapshot));
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

static void blocked(const char* reason)
{
    serial::puts("[DM13-QEMU-USB] destructive-lifecycle=BLOCKED reason=");
    serial::puts(reason);
    serial::putc('\n');
}

static bool find_usb_target(block::BlockDevice& device,
                            storage::TargetIdentity& target)
{
    for (uint8_t index = 0; index < block::MAX_BLOCK_DEVICES; ++index) {
        block::BlockDevice candidate = {};
        if (!block::copy_device(index, candidate) ||
            candidate.type != block::BDEV_USB_MASS) continue;
        storage::TargetIdentity identity = {};
        const bool haveIdentity =
            storage::capture_target_identity(index, identity);
        const storage::BootProtection boot = haveIdentity
            ? storage::query_boot_protection(identity)
            : storage::BootProtection{storage::BOOT_DEVICE_IDENTITY_UNKNOWN};
        serial::puts("[DM13-QEMU-USB] USB candidate index=");
        serial::put_hex8(index);
        serial::puts(" sectors=");
        serial::put_hex64(candidate.totalSectors);
        serial::puts(" controller-pci=");
        serial::puts(candidate.pciLocationValid ? "valid" : "unknown");
        serial::puts(" boot=");
        serial::put_hex8(static_cast<uint8_t>(boot.safety));
        serial::puts(" flush=");
        serial::puts(candidate.flushFn ? "available" : "unavailable");
        serial::puts(" sync-cache-state=0x");
        serial::put_hex8(candidate.usbSyncCacheState);
        serial::putc('\n');
        if (!haveIdentity || !candidate.readFn || !candidate.writeFn ||
            !candidate.flushFn || !candidate.flushSemanticsKnown ||
            candidate.sectorSize != 512 ||
            candidate.usbSyncCacheState != usb_storage::SYNC_CACHE_SUCCEEDED ||
            boot.safety != storage::BOOT_DEVICE_DEFINITELY_NOT_TARGET)
            continue;
        device = candidate;
        target = identity;
        return true;
    }
    return false;
}

static bool find_proof_partition(const storage::TargetIdentity& target)
{
    if (!storage::parse_partition_table(target.globalIndex, s_table))
        return false;
    for (uint16_t i = 0; i < s_table.partitionCount; ++i) {
        if (text_equal(s_table.partitions[i].name, kPartitionName)) {
            s_partition = s_table.partitions[i];
            return true;
        }
    }
    return false;
}

static bool read_payload()
{
    const uint8_t handle = vfs::open(kFilePath, vfs::OPEN_READ);
    if (handle == 0xFFu) return false;
    char bytes[sizeof(kPayload)] = {};
    const int32_t count = vfs::read(handle, bytes,
        static_cast<uint32_t>(sizeof(kPayload) - 1u));
    const vfs::Status closed = vfs::close(handle);
    if (count != static_cast<int32_t>(sizeof(kPayload) - 1u) ||
        closed != vfs::VFS_OK) return false;
    for (uint32_t i = 0; i < sizeof(kPayload) - 1u; ++i)
        if (bytes[i] != kPayload[i]) return false;
    return true;
}

static bool mount_verify(const storage::TargetIdentity& target,
                         bool createFile, uint8_t rootMountCount,
                         uint64_t totalSectors)
{
    if (createFile && !read_gpt_snapshot(target, totalSectors,
            s_primaryGptSnapshot, s_backupGptSnapshot)) return false;
    const vfs::PartitionMountResult mounted = vfs::mount_partition_detailed(
        kMountPath, target.globalIndex, s_partition.partitionNumber,
        target.registrationId, &s_partition);
    if (mounted.error != vfs::PARTITION_MOUNT_OK) return false;
    bool valid = true;
    if (createFile) {
        valid = vfs::mkdir(kDirectoryPath) == vfs::VFS_OK;
        const int32_t created = valid
            ? vfs::create_file(kFilePath, kPayload,
                static_cast<uint32_t>(sizeof(kPayload) - 1u))
            : vfs::VFS_ERR_IO;
        valid = valid && created == static_cast<int32_t>(sizeof(kPayload) - 1u);
    }
    valid = valid && read_payload();
    const vfs::Status unmounted = vfs::unmount(kMountPath);
    if (!valid || unmounted != vfs::VFS_OK ||
        vfs::mount_count() != rootMountCount) return false;
    if (!createFile) return true;

    const vfs::PartitionMountResult remounted = vfs::mount_partition_detailed(
        kMountPath, target.globalIndex, s_partition.partitionNumber,
        target.registrationId, &s_partition);
    const bool remountRead = remounted.error == vfs::PARTITION_MOUNT_OK &&
        vfs::mount_identity_valid(remounted.mountIndex) && read_payload();
    const vfs::Status secondUnmount = vfs::unmount(kMountPath);
    if (!remountRead || secondUnmount != vfs::VFS_OK ||
        vfs::mount_count() != rootMountCount) return false;

    for (uint8_t cycle = 0; cycle < 10; ++cycle) {
        const vfs::PartitionMountResult stressMount =
            vfs::mount_partition_detailed(kMountPath, target.globalIndex,
                s_partition.partitionNumber, target.registrationId,
                &s_partition);
        if (stressMount.error != vfs::PARTITION_MOUNT_OK ||
            !vfs::mount_identity_valid(stressMount.mountIndex) ||
            vfs::write_file(kFilePath, kPayload,
                static_cast<uint32_t>(sizeof(kPayload) - 1u)) !=
                    static_cast<int32_t>(sizeof(kPayload) - 1u) ||
            !read_payload() || vfs::unmount(kMountPath) != vfs::VFS_OK ||
            vfs::mount_count() != rootMountCount) return false;
    }
    const bool gptPreserved = gpt_unchanged(target, totalSectors);
    serial::puts(gptPreserved
        ? "[DM13-QEMU-USB] vfs-gpt-unchanged=PASS exact-primary-backup-bytes=PASS\n"
        : "[DM13-QEMU-USB] vfs-gpt-unchanged=FAIL\n");
    if (!gptPreserved) return false;
    serial::puts("[DM13-QEMU-USB] mount-write-read-unmount-stress=PASS cycles=10\n");
    return true;
}

static void print_validation(storage::InitializeDiskStatus status,
                             const storage::InitializeTargetValidation& v,
                             const storage::BootProtection& boot)
{
    serial::puts("[DM13-QEMU-USB] destructive-preflight=");
    serial::puts(storage::initialize_disk_status_name(status));
    serial::puts(" boot=");
    serial::put_hex8(static_cast<uint8_t>(boot.safety));
    serial::puts(" issues=0x");
    serial::put_hex32(v.destructiveIssues);
    serial::puts(" writable=YES persistence=trusted sync-cache=success\n");
}

} // namespace

void run()
{
    block::BlockDevice device = {};
    storage::TargetIdentity target = {};
    if (!find_usb_target(device, target)) {
        blocked("no-trusted-definitely-nonboot-writable-usb-target");
        return;
    }
    const uint8_t rootMountCount = vfs::mount_count();
    const storage::BootProtection boot = storage::query_boot_protection(target);

    if (!storage::parse_partition_table(target.globalIndex, s_table)) {
        blocked("partition-table-read-failed");
        return;
    }
    if (s_table.state == storage::DISK_STATE_VALID_GPT &&
        s_table.primaryGptValid && s_table.backupGptValid && s_table.gptCopiesAgree) {
        if (!find_proof_partition(target)) {
            blocked("verified-gpt-without-dm13-proof-partition");
            return;
        }
        const bool persisted = mount_verify(target, false, rootMountCount,
                                            device.totalSectors);
        serial::puts(persisted
            ? "[DM13-QEMU-USB] restart-persistence=PASS remount=PASS exact-file-bytes=PASS mounts-clean=PASS\n"
            : "[DM13-QEMU-USB] restart-persistence=FAIL remount-or-file-check-failed\n");
        return;
    }

    storage::InitializeTargetValidation validation = {};
    const storage::InitializeDiskStatus preflight =
        storage::probe_initialize_target(target, storage::PARTITION_SCHEME_GPT,
                                         validation);
    print_validation(preflight, validation, boot);
    if (preflight != storage::INITIALIZE_DISK_READY_FOR_CONFIRMATION) {
        blocked(storage::initialize_disk_status_name(preflight));
        return;
    }

    s_initializeRequest = {};
    s_initializeRequest.targetSnapshot = target;
    s_initializeRequest.requestedScheme = storage::PARTITION_SCHEME_GPT;
    s_initializeRequest.mbrSignaturePolicy = storage::MBR_SIGNATURE_RANDOM_NONZERO;
    s_initializeRequest.diskGuidSource = storage::DISK_GUID_SECURE_RANDOM;
    s_initializeRequest.expectedRegistryGeneration = target.registryGeneration;
    s_initializePlan = {};
    s_initializeResult = {};
    const storage::InitializeDiskStatus prepared =
        storage::prepare_initialize_disk(s_initializeRequest, s_initializePlan,
                                         s_initializeResult);
    if (prepared != storage::INITIALIZE_DISK_READY_FOR_CONFIRMATION) {
        blocked(storage::initialize_disk_status_name(prepared));
        return;
    }
    const storage::InitializeDiskStatus initialized =
        storage::execute_initialize_disk(s_initializePlan, s_initializeResult);
    if (initialized != storage::INITIALIZE_DISK_SUCCESS ||
        !s_initializeResult.verificationPassed ||
        s_initializeResult.finalStateUncertain) {
        serial::puts("[DM13-QEMU-USB] initialize=FAIL status=");
        serial::puts(storage::initialize_disk_status_name(initialized));
        serial::putc('\n');
        return;
    }
    serial::puts("[DM13-QEMU-USB] initialize=PASS verified=PASS\n");

    for (uint16_t i = 0; i < storage::MAX_UNALLOCATED_REGIONS; ++i)
        s_regions[i] = {};
    uint16_t regionCount = 0;
    if (!storage::parse_partition_table(target.globalIndex, s_table) ||
        s_table.state != storage::DISK_STATE_VALID_GPT ||
        !storage::compute_unallocated_regions(s_table, device.totalSectors,
            device.sectorSize, s_regions, storage::MAX_UNALLOCATED_REGIONS,
            regionCount) || regionCount == 0) {
        blocked("post-initialize-free-space-unavailable");
        return;
    }
    s_createRequest = {};
    s_createRequest.targetSnapshot = target;
    s_createRequest.requestedScheme = storage::PARTITION_SCHEME_GPT;
    s_createRequest.selectedRegion = s_regions[0];
    s_createRequest.useMaximumSize = true;
    s_createRequest.partitionType = storage::CREATE_PARTITION_GPT_BASIC_DATA;
    copy_text(s_createRequest.gptName, sizeof(s_createRequest.gptName),
              kPartitionName);
    s_createRequest.expectedRegistryGeneration = block::registry_generation();
    s_createResult = {};
    const storage::CreatePartitionStatus created =
        storage::create_partition(s_createRequest, s_createResult);
    if (created != storage::CREATE_PARTITION_SUCCESS ||
        !s_createResult.verificationPassed || s_createResult.finalStateUncertain) {
        serial::puts("[DM13-QEMU-USB] create-partition=FAIL status=");
        serial::puts(storage::create_partition_status_name(created));
        serial::putc('\n');
        return;
    }
    serial::puts("[DM13-QEMU-USB] create-partition=PASS gpt-copies=verified\n");
    if (!find_proof_partition(target)) {
        blocked("created-partition-not-found");
        return;
    }
    if (!read_gpt_snapshot(target, device.totalSectors,
            s_primaryGptSnapshot, s_backupGptSnapshot)) {
        blocked("pre-format-gpt-snapshot-failed");
        return;
    }

    s_formatRequest = {};
    s_formatRequest.targetSnapshot = target;
    s_formatRequest.partitionScheme = storage::PARTITION_SCHEME_GPT;
    s_formatRequest.partitionSnapshot = s_partition;
    for (uint8_t i = 0; i < sizeof(s_formatRequest.gptDiskGuid); ++i)
        s_formatRequest.gptDiskGuid[i] = s_table.primaryDiskGuid[i];
    copy_text(s_formatRequest.volumeLabel, sizeof(s_formatRequest.volumeLabel),
              "DM13PROOF");
    s_formatRequest.expectedRegistryGeneration = block::registry_generation();
    s_formatResult = {};
    const storage::Fat32FormatStatus formatted =
        storage::format_fat32_partition(s_formatRequest, s_formatResult);
    if (formatted != storage::FAT32_FORMAT_SUCCESS ||
        !s_formatResult.verificationPassed || s_formatResult.finalStateUncertain) {
        serial::puts("[DM13-QEMU-USB] format-fat32=FAIL status=");
        serial::puts(storage::fat32_format_status_name(formatted));
        serial::putc('\n');
        return;
    }
    serial::puts("[DM13-QEMU-USB] format-fat32=PASS bpb-fsinfo-fat=verified\n");
    const bool gptPreserved = gpt_unchanged(target, device.totalSectors);
    serial::puts(gptPreserved
        ? "[DM13-QEMU-USB] gpt-unchanged=PASS exact-primary-backup-bytes=PASS\n"
        : "[DM13-QEMU-USB] gpt-unchanged=FAIL\n");
    if (!gptPreserved) return;

    const bool lifecycle = find_proof_partition(target) &&
        mount_verify(target, true, rootMountCount, device.totalSectors);
    serial::puts(lifecycle
        ? "[DM13-QEMU-USB] lifecycle=PASS initialize=PASS create-partition=PASS format-fat32=PASS mount=PASS file-write=PASS file-read=PASS unmount=PASS remount-read=PASS mounts-clean=PASS\n"
        : "[DM13-QEMU-USB] lifecycle=FAIL mount-or-file-operation-failed\n");
}

} // namespace qemu_dm13_usb_lifecycle_proof
} // namespace kernel

#endif
