#include "include/kernel/qemu_dm14_usb_hotplug_proof.h"

#if defined(GXOS_DM14_QEMU_USB_HOTPLUG_PROOF)

#include "include/kernel/block_device.h"
#include "include/kernel/partition_block_view.h"
#include "include/kernel/partition_table.h"
#include "include/kernel/pit.h"
#include "include/kernel/serial_debug.h"
#include "include/kernel/storage_manager.h"
#include "include/kernel/usb.h"
#include "include/kernel/usb_storage.h"
#include "include/kernel/vfs.h"

namespace kernel {
namespace qemu_dm14_usb_hotplug_proof {
namespace {

static const char kMountPath[] = "/mnt/dm14";
static const char kDirectoryPath[] = "/mnt/dm14/dm13";
static const char kFilePath[] = "/mnt/dm14/dm13/proof.bin";
static const char kPartitionName[] = "DM13 QEMU USB Proof";
static const char kPayloadA[] = "guideXOS DM13 USB lifecycle proof 001\r\n";
static const char kPayloadB[] = "guideXOS DM14 USB revisionB proof 002\r\n";
static const uint64_t kRawTestLba = 90000;
static const uint32_t kRawTestSectors = 128;

// Keep parser and BOT transfer buffers out of the small boot stack.
static storage::PartitionTableModel s_table = {};
alignas(4096) static uint8_t s_largeWrite[kRawTestSectors * 512u] = {};
alignas(4096) static uint8_t s_readBuffer[512] = {};

static void result(const char* name, bool passed)
{
    serial::puts("[DM14-QEMU-USB] ");
    serial::puts(name);
    serial::puts(passed ? "=PASS\n" : "=FAIL\n");
}

static bool bytes_equal(const uint8_t* left, const char* right,
                        uint32_t length)
{
    for (uint32_t i = 0; i < length; ++i)
        if (left[i] != static_cast<uint8_t>(right[i])) return false;
    return true;
}

static bool text_equal(const char* left, const char* right)
{
    if (!left || !right) return left == right;
    while (*left && *right && *left == *right) { ++left; ++right; }
    return *left == *right;
}

static bool find_usb_target(block::BlockDevice& device,
                            storage::TargetIdentity& identity)
{
    for (uint8_t i = 0; i < block::MAX_BLOCK_DEVICES; ++i) {
        block::BlockDevice candidate = {};
        if (!block::copy_device(i, candidate) ||
            candidate.type != block::BDEV_USB_MASS) continue;
        storage::TargetIdentity snapshot = {};
        if (!storage::capture_target_identity(i, snapshot) ||
            !candidate.readFn || !candidate.writeFn || !candidate.flushFn ||
            candidate.sectorSize != 512 || !candidate.flushSemanticsKnown)
            continue;
        device = candidate;
        identity = snapshot;
        return true;
    }
    return false;
}

static bool find_partition(const storage::TargetIdentity& identity,
                           storage::PartitionEntry& partition)
{
    if (!storage::parse_partition_table(identity.globalIndex, s_table))
        return false;
    for (uint16_t i = 0; i < s_table.partitionCount; ++i) {
        if (text_equal(s_table.partitions[i].name, kPartitionName)) {
            partition = s_table.partitions[i];
            return true;
        }
    }
    return false;
}

static bool mount_and_read(const block::BlockDevice& device,
                           const storage::TargetIdentity& identity,
                           const storage::PartitionEntry& partition,
                           const char* expected, uint32_t expectedLength)
{
    const vfs::PartitionMountResult mounted = vfs::mount_partition_detailed(
        kMountPath, identity.globalIndex, partition.partitionNumber,
        identity.registrationId, &partition);
    if (mounted.error != vfs::PARTITION_MOUNT_OK) return false;
    const vfs::HandleToken handle = vfs::open(kFilePath, vfs::OPEN_READ);
    uint8_t contents[64] = {};
    const int32_t count = handle == 0xFFu ? vfs::VFS_ERR_IO
        : vfs::read(handle, contents, expectedLength);
    const vfs::Status closed = handle == 0xFFu ? vfs::VFS_ERR_INVALID
        : vfs::close(handle);
    const bool dataMatches = count == static_cast<int32_t>(expectedLength) &&
        bytes_equal(contents, expected, expectedLength) &&
        closed == vfs::VFS_OK;
    const vfs::Status unmounted = vfs::unmount(kMountPath);
    return dataMatches && unmounted == vfs::VFS_OK &&
        block::device_count() != 0 &&
        device.registrationId == identity.registrationId;
}

static bool wait_for_new_usb(uint64_t oldRegistration,
                             block::BlockDevice& device,
                             storage::TargetIdentity& identity)
{
    const uint64_t deadline = pit::ticks() + 1500u;
    while (pit::ticks() < deadline) {
        usb::poll();
        if (find_usb_target(device, identity) &&
            identity.registrationId != oldRegistration)
            return true;
        asm volatile("sti; hlt" ::: "memory");
    }
    return false;
}

static bool wait_for_disconnect(const block::BlockDevice& device,
                                const char* marker)
{
    const usb_storage::StorageDevice* transport =
        usb_storage::get_device(device.driverIndex);
    return transport && usb::hci::test_wait_for_disconnect(
        transport->usbAddress, marker);
}

static void retire_port()
{
    usb::poll();
}

static void run_proof()
{
    block::BlockDevice firstDevice = {};
    storage::TargetIdentity firstIdentity = {};
    storage::PartitionEntry firstPartition = {};
    const bool foundFirst = find_usb_target(firstDevice, firstIdentity);
    const bool foundPartition = foundFirst &&
        find_partition(firstIdentity, firstPartition);
    result("initial-usb-and-gpt", foundPartition);
    if (!foundPartition) return;

    const uint32_t payloadLength = sizeof(kPayloadA) - 1u;
    const vfs::PartitionMountResult idleMount =
        vfs::mount_partition_detailed(kMountPath, firstIdentity.globalIndex,
            firstPartition.partitionNumber, firstIdentity.registrationId,
            &firstPartition);
    block::BlockEndpoint staleEndpoint = {};
    const bool endpointCaptured = block::make_device_endpoint(
        firstIdentity.globalIndex, staleEndpoint);
    const bool mountedIdle = idleMount.error == vfs::PARTITION_MOUNT_OK &&
        endpointCaptured;
    result("mounted-idle-no-handles", mountedIdle);
    if (!mountedIdle) return;

    const bool idleUnplugDetected = wait_for_disconnect(firstDevice,
        "idle-mounted-no-handles");
    retire_port();
    const vfs::Status idleBackingStatus = vfs::mount_backing_status(
        idleMount.mountIndex);
    const vfs::Status idleUnmount = vfs::unmount(kMountPath);
    const bool idleOldTargetInvalid = storage::revalidate_target_identity(
        firstIdentity) != storage::TARGET_VALID;
    result("idle-mounted-unplug-detected", idleUnplugDetected);
    result("idle-mounted-stale-cleanup",
        idleBackingStatus == vfs::VFS_ERR_DEVICE_REMOVED &&
        idleUnmount == vfs::VFS_ERR_DEVICE_REMOVED && idleOldTargetInvalid);
    if (!idleUnplugDetected || !idleOldTargetInvalid) return;

    block::BlockDevice sameMedia = {};
    storage::TargetIdentity sameIdentity = {};
    const bool sameMediaReinserted = wait_for_new_usb(
        firstIdentity.registrationId, sameMedia, sameIdentity);
    storage::PartitionEntry firstReinsertPartition = {};
    const bool samePartitionFound = sameMediaReinserted &&
        find_partition(sameIdentity, firstReinsertPartition);
    const bool sameDataRead = samePartitionFound && mount_and_read(
        sameMedia, sameIdentity, firstReinsertPartition, kPayloadA, payloadLength);
    const block::Status oldEndpointOnSameMedia = block::read_endpoint(
        staleEndpoint, 0, 1, s_readBuffer);
    result("same-media-reinsert-fresh-incarnation",
        sameDataRead && sameIdentity.registrationId !=
            firstIdentity.registrationId &&
        oldEndpointOnSameMedia == block::BLOCK_ERR_NO_MEDIA);
    if (!sameDataRead) return;

    const vfs::PartitionMountResult initialMount =
        vfs::mount_partition_detailed(kMountPath, sameIdentity.globalIndex,
            firstReinsertPartition.partitionNumber, sameIdentity.registrationId,
            &firstReinsertPartition);
    const vfs::HandleToken file = initialMount.error == vfs::PARTITION_MOUNT_OK
        ? vfs::open(kFilePath, vfs::OPEN_RDWR) : 0xFFu;
    const vfs::HandleToken directory = initialMount.error == vfs::PARTITION_MOUNT_OK
        ? vfs::opendir(kDirectoryPath) : 0xFFu;
    uint8_t initialBytes[64] = {};
    const int32_t initialRead = file == 0xFFu ? vfs::VFS_ERR_IO
        : vfs::read(file, initialBytes, payloadLength);
    const vfs::Status resetPosition = file == 0xFFu ? vfs::VFS_ERR_INVALID
        : vfs::seek(file, 0, vfs::SEEK_SET);
    storage::TargetIdentity staleSnapshot = sameIdentity;
    const bool mountedAndOpen = initialMount.error == vfs::PARTITION_MOUNT_OK &&
        file != 0xFFu && directory != 0xFFu &&
        initialRead == static_cast<int32_t>(payloadLength) &&
        bytes_equal(initialBytes, kPayloadA, payloadLength) &&
        resetPosition == vfs::VFS_OK;
    result("mounted-file-and-directory-open", mountedAndOpen);
    if (!mountedAndOpen) return;

    const bool openHandleUnplugDetected = wait_for_disconnect(sameMedia,
        "idle-open-handles");
    retire_port();
    const int32_t staleRead = vfs::read(file, s_readBuffer, 1);
    const uint8_t writeByte = 0xD4u;
    const int32_t staleWrite = vfs::write(file, &writeByte, 1);
    bool hasNext = false;
    vfs::DirEntry nextEntry = {};
    const vfs::Status staleDirectory = vfs::readdir_detailed(
        directory, &nextEntry, hasNext);
    const vfs::Status busyUnmount = vfs::unmount(kMountPath);
    const vfs::Status closeFile = vfs::close(file);
    vfs::closedir(directory);
    const vfs::Status staleUnmount = vfs::unmount(kMountPath);
    const bool openHandleTargetInvalid = storage::revalidate_target_identity(
        staleSnapshot) != storage::TARGET_VALID;
    result("open-handle-unplug-detected", openHandleUnplugDetected);
    result("stale-file-directory-and-mount-safe",
        staleRead == vfs::VFS_ERR_DEVICE_REMOVED &&
        staleWrite == vfs::VFS_ERR_DEVICE_REMOVED &&
        staleDirectory == vfs::VFS_ERR_DEVICE_REMOVED && !hasNext &&
        busyUnmount == vfs::VFS_ERR_BUSY &&
        closeFile == vfs::VFS_ERR_DEVICE_REMOVED &&
        staleUnmount == vfs::VFS_ERR_DEVICE_REMOVED &&
        openHandleTargetInvalid);
    if (!openHandleUnplugDetected || !openHandleTargetInvalid) return;

    block::BlockDevice sameMediaReinsertedDevice = {};
    storage::TargetIdentity sameIdentityReinserted = {};
    const bool sameMediaReinsertedAgain = wait_for_new_usb(
        sameIdentity.registrationId, sameMediaReinsertedDevice,
        sameIdentityReinserted);
    storage::PartitionEntry samePartition = {};
    const bool samePartitionFoundAgain = sameMediaReinsertedAgain &&
        find_partition(sameIdentityReinserted, samePartition);
    const bool sameDataReadAgain = samePartitionFoundAgain && mount_and_read(
        sameMediaReinsertedDevice, sameIdentityReinserted, samePartition,
        kPayloadA, payloadLength);
    const block::Status oldEndpointAfterOpenHandleRemoval = block::read_endpoint(
        staleEndpoint, 0, 1, s_readBuffer);
    result("same-media-reinsert-fresh-incarnation",
        sameDataReadAgain && sameIdentityReinserted.registrationId !=
            sameIdentity.registrationId &&
        sameIdentityReinserted.registrationId != firstIdentity.registrationId &&
        oldEndpointAfterOpenHandleRemoval == block::BLOCK_ERR_NO_MEDIA);
    if (!sameDataReadAgain) return;

    const bool replaceWithBDone = wait_for_disconnect(sameMediaReinsertedDevice,
                                                       "replace-same-media-with-b");
    retire_port();
    block::BlockDevice mediaB = {};
    storage::TargetIdentity identityB = {};
    const bool mediaBInserted = replaceWithBDone && wait_for_new_usb(
        sameIdentityReinserted.registrationId, mediaB, identityB);
    storage::PartitionEntry partitionB = {};
    const bool partitionBFound = mediaBInserted &&
        find_partition(identityB, partitionB);
    const bool payloadBRead = partitionBFound && mount_and_read(
        mediaB, identityB, partitionB, kPayloadB, payloadLength);
    block::BlockEndpoint endpointA = staleEndpoint;
    const block::Status oldEndpointOnB = block::read_endpoint(
        endpointA, 0, 1, s_readBuffer);
    result("same-port-different-media-replacement",
        payloadBRead && identityB.registrationId !=
            sameIdentityReinserted.registrationId &&
        identityB.totalLogicalSectors == sameIdentityReinserted.totalLogicalSectors &&
        identityB.logicalSectorSize == sameIdentityReinserted.logicalSectorSize &&
        identityB.usbVendorId == sameIdentityReinserted.usbVendorId &&
        identityB.usbProductId == sameIdentityReinserted.usbProductId &&
        identityB.usbPort == sameIdentityReinserted.usbPort &&
        oldEndpointOnB == block::BLOCK_ERR_NO_MEDIA);
    if (!payloadBRead) return;

    const vfs::PartitionMountResult vfsWriteMount =
        vfs::mount_partition_detailed(kMountPath, identityB.globalIndex,
            partitionB.partitionNumber, identityB.registrationId,
            &partitionB);
    const vfs::HandleToken writeHandle = vfsWriteMount.error == vfs::PARTITION_MOUNT_OK
        ? vfs::open(kFilePath, vfs::OPEN_RDWR) : 0xFFu;
    static uint8_t s_vfsWrite[4096];
    for (uint32_t i = 0; i < sizeof(s_vfsWrite); ++i)
        s_vfsWrite[i] = static_cast<uint8_t>((i * 31u + 0x6Bu) & 0xFFu);
    if (writeHandle != 0xFFu)
        (void)vfs::seek(writeHandle, 0, vfs::SEEK_SET);
    usb_storage::test_arm_data_out_disconnect_gate();
    const int32_t interruptedVfsWrite = writeHandle == 0xFFu
        ? vfs::VFS_ERR_IO
        : vfs::write(writeHandle, s_vfsWrite, sizeof(s_vfsWrite));
    retire_port();
    const bool vfsRemovalDetected = storage::revalidate_target_identity(
        identityB) != storage::TARGET_VALID;
    const vfs::Status writeHandleClose = writeHandle == 0xFFu
        ? vfs::VFS_ERR_INVALID : vfs::close(writeHandle);
    const vfs::Status staleVfsUnmount = vfs::unmount(kMountPath);
    result("vfs-write-removal-reports-failure",
        vfsWriteMount.error == vfs::PARTITION_MOUNT_OK &&
        interruptedVfsWrite < 0 && vfsRemovalDetected &&
        writeHandleClose != vfs::VFS_OK &&
        staleVfsUnmount == vfs::VFS_ERR_DEVICE_REMOVED);
    if (!vfsRemovalDetected) return;

    block::BlockDevice mediaAAgain = {};
    storage::TargetIdentity identityAAgain = {};
    const bool mediaAAgainInserted = wait_for_new_usb(
        identityB.registrationId, mediaAAgain, identityAAgain);
    if (!mediaAAgainInserted) {
        result("replacement-a-after-vfs-write", false);
        return;
    }
    result("replacement-a-after-vfs-write", true);

    for (uint32_t i = 0; i < sizeof(s_largeWrite); ++i)
        s_largeWrite[i] = static_cast<uint8_t>((i * 17u + 0xA9u) & 0xFFu);
    usb_storage::test_arm_data_out_disconnect_gate();
    const block::Status interruptedBlockWrite = block::write_sectors_checked(
        identityAAgain.globalIndex, kRawTestLba, kRawTestSectors,
        s_largeWrite, sizeof(s_largeWrite));
    block::OperationDiagnostic writeDiagnostic = {};
    const bool uncertainWriteOutcome = block::last_operation_diagnostic(
        writeDiagnostic) && writeDiagnostic.transportDiagnostic.valid &&
        writeDiagnostic.transportDiagnostic.usbWriteOutcome ==
            block::USB_WRITE_REMOVED_UNKNOWN;
    retire_port();
    const bool dataOutRemovalDetected = storage::revalidate_target_identity(
        identityAAgain) != storage::TARGET_VALID;
    result("multi-td-data-out-removal-uncertain",
        interruptedBlockWrite == block::BLOCK_ERR_WRITE_UNCERTAIN &&
        uncertainWriteOutcome && dataOutRemovalDetected);
    if (!dataOutRemovalDetected) return;

    block::BlockDevice mediaBAgain = {};
    storage::TargetIdentity identityBAgain = {};
    const bool mediaBAgainInserted = wait_for_new_usb(
        identityAAgain.registrationId, mediaBAgain, identityBAgain);
    if (!mediaBAgainInserted) {
        result("replacement-b-after-data-out", false);
        return;
    }
    const block::Status seedWrite = block::write_sectors_checked(
        identityBAgain.globalIndex, kRawTestLba, 1, s_largeWrite, 512);
    usb_storage::test_arm_sync_cache_disconnect_gate();
    const block::Status syncRemoval = block::flush(identityBAgain.globalIndex);
    retire_port();
    const bool syncRemovalDetected = storage::revalidate_target_identity(
        identityBAgain) != storage::TARGET_VALID;
    result("sync-cache-removal-not-durable",
        seedWrite == block::BLOCK_OK &&
        syncRemoval == block::BLOCK_ERR_DURABILITY_UNVERIFIED &&
        syncRemovalDetected &&
        block::get_device(identityBAgain.globalIndex) == nullptr);
}

} // namespace

void run()
{
    serial::puts("[DM14-QEMU-USB] proof=START topology=PIIX3-UHCI-direct-root-port\n");
    run_proof();
    serial::puts("[DM14-QEMU-USB] proof=END\n");
}

} // namespace qemu_dm14_usb_hotplug_proof
} // namespace kernel

#endif
