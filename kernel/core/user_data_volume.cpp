#include "include/kernel/user_data_volume.h"
#include "include/kernel/block_device.h"
#include "include/kernel/fs_fat.h"
#include "include/kernel/ramdisk.h"
#include "include/kernel/serial_debug.h"
#include "include/kernel/user_data_volume_policy.h"
#include "include/kernel/vfs.h"

namespace kernel {
namespace user_data_volume {
namespace {

static const char kDocumentsMount[] = "/Documents";
static const char kAppDataAlias[] = "/AppData";
static const char kAppDataDirectory[] = "/Documents/APPDATA";
static const char kMarkerPath[] = "/Documents/GXOSDAT.ID";
static const char kMarkerContents[] = "guideXOS-user-data-v1\n";
static MountStatus s_lastMountStatus = MOUNT_NOT_ATTEMPTED;

static bool same_text(const char* left, const char* right)
{
    if (!left || !right) return false;
    uint32_t index = 0;
    while (left[index] && right[index] && left[index] == right[index]) ++index;
    return left[index] == '\0' && right[index] == '\0';
}

static bool volume_label_matches(const char label[12])
{
    static const char expected[] = "GXOSDATA";
    for (uint32_t index = 0; index < sizeof(expected) - 1u; ++index) {
        char actual = label[index];
        if (actual >= 'a' && actual <= 'z') actual = static_cast<char>(actual - 'a' + 'A');
        if (actual != expected[index]) return false;
    }
    for (uint32_t index = sizeof(expected) - 1u; index < 11u; ++index) {
        if (label[index] != ' ' && label[index] != '\0') return false;
    }
    return true;
}

static MountStatus selection_mount_status(policy::SelectionStatus status)
{
    switch (status) {
        case policy::SELECT_OK: return MOUNT_OK;
        case policy::SELECT_MISSING: return MOUNT_DEVICE_MISSING;
        case policy::SELECT_AMBIGUOUS: return MOUNT_DEVICE_AMBIGUOUS;
        case policy::SELECT_SYSTEM_SOURCE: return MOUNT_DEVICE_IS_SYSTEM_SOURCE;
        case policy::SELECT_UNSUPPORTED_DEVICE: return MOUNT_DEVICE_UNSUPPORTED;
        case policy::SELECT_READ_ONLY: return MOUNT_DEVICE_READ_ONLY;
        case policy::SELECT_NO_FLUSH: return MOUNT_DEVICE_NO_FLUSH;
        case policy::SELECT_INVALID_DEVICE: return MOUNT_DEVICE_INVALID;
        default: return MOUNT_DEVICE_INVALID;
    }
}

static bool verify_existing_mount(uint8_t deviceIndex, uint64_t generation)
{
    const vfs::MountPoint* documents = vfs::get_mount(kDocumentsMount);
    const vfs::MountPoint* appData = vfs::get_mount(kAppDataAlias);
    if (!documents || !appData || !same_text(documents->path, kDocumentsMount) ||
        !same_text(appData->path, kAppDataAlias) || !appData->alias ||
        !same_text(appData->sourcePrefix, "/APPDATA") ||
        documents->blockDevIndex != deviceIndex || appData->blockDevIndex != deviceIndex ||
        documents->blockDeviceGeneration != generation || appData->blockDeviceGeneration != generation ||
        documents->readOnly || appData->readOnly || documents->fsType != vfs::FS_TYPE_FAT32 ||
        appData->fsType != vfs::FS_TYPE_FAT32) {
        return false;
    }
    return true;
}

static void fail_cleanup(bool createdDocuments, bool createdAlias)
{
    if (createdAlias) (void)vfs::unmount(kAppDataAlias);
    if (createdDocuments) (void)vfs::unmount(kDocumentsMount);
}

static bool fail(MountStatus status, bool createdDocuments = false,
                 bool createdAlias = false)
{
    s_lastMountStatus = status;
    fail_cleanup(createdDocuments, createdAlias);
    serial::puts("[USERDATA] mount=failed reason=");
    serial::puts(mount_status_name(status));
    serial::putc('\n');
    return false;
}

} // namespace

bool mount_documents()
{
    s_lastMountStatus = MOUNT_NOT_ATTEMPTED;

    const vfs::MountPoint* apps = vfs::get_mount("/Apps");
    if (!apps || !same_text(apps->path, "/") || apps->alias || !apps->readOnly ||
        apps->fsType != vfs::FS_TYPE_FAT32 ||
        !ramdisk::is_readonly_release_image_device(apps->blockDevIndex,
                                                   apps->blockDeviceGeneration)) {
        return fail(MOUNT_SYSTEM_SOURCE_UNAVAILABLE);
    }

    const block::BlockDevice* devices[block::MAX_BLOCK_DEVICES] = {};
    for (uint8_t index = 0; index < block::MAX_BLOCK_DEVICES; ++index) {
        devices[index] = block::get_device(index);
    }
    const policy::Selection selection = policy::select_device(
        devices, block::MAX_BLOCK_DEVICES, apps->blockDevIndex);
    if (selection.status != policy::SELECT_OK) {
        return fail(selection_mount_status(selection.status));
    }

    const uint8_t deviceIndex = selection.deviceIndex;
    const block::BlockDevice* device = block::get_device(deviceIndex);
    const uint64_t generation = block::device_generation(deviceIndex);
    if (!device || generation == 0 || deviceIndex == apps->blockDevIndex) {
        return fail(deviceIndex == apps->blockDevIndex
            ? MOUNT_DEVICE_IS_SYSTEM_SOURCE : MOUNT_DEVICE_INVALID);
    }

    const vfs::MountPoint* currentDocuments = vfs::get_mount(kDocumentsMount);
    if (currentDocuments && same_text(currentDocuments->path, kDocumentsMount)) {
        if (verify_existing_mount(deviceIndex, generation)) {
            s_lastMountStatus = MOUNT_ALREADY_READY;
            serial::puts("[USERDATA] mount=already-ready path=/Documents device=");
            serial::puts(device->name);
            serial::puts(" stableId=");
            serial::puts(device->stableId);
            serial::putc('\n');
            return true;
        }
        return fail(MOUNT_PATH_BUSY);
    }

    serial::puts("[USERDATA] selecting device=");
    serial::puts(device->name);
    serial::puts(" stableId=");
    serial::puts(device->stableId);
    serial::puts(" index=");
    serial::put_hex8(deviceIndex);
    serial::puts(" generation=");
    serial::put_hex64(generation);
    serial::puts(" sectorSize=");
    serial::put_hex32(device->sectorSize);
    serial::puts(" sectors=");
    serial::put_hex64(device->totalSectors);
    serial::putc('\n');

    const uint8_t mountIndex = vfs::mount_type(kDocumentsMount, deviceIndex,
                                               vfs::FS_TYPE_FAT32);
    if (mountIndex == 0xFFu) return fail(MOUNT_FILESYSTEM_FAILED);
    bool createdDocuments = true;

    const vfs::MountPoint* documents = vfs::get_mount(kDocumentsMount);
    if (!documents || !same_text(documents->path, kDocumentsMount) ||
        documents->blockDevIndex != deviceIndex ||
        documents->blockDeviceGeneration != generation || documents->readOnly ||
        block::device_generation(deviceIndex) != generation) {
        return fail(MOUNT_STATE_CHANGED, createdDocuments);
    }

    const fs_fat::FATVolume* volume = fs_fat::get_volume(documents->fsVolumeIndex);
    if (!volume || volume->type != fs_fat::FAT_TYPE_FAT32) {
        return fail(MOUNT_NOT_FAT32, createdDocuments);
    }
    if (volume->partitionOffset != 0 || volume->bytesPerSector != device->sectorSize ||
        volume->totalSectors != device->totalSectors) {
        return fail(MOUNT_GEOMETRY_MISMATCH, createdDocuments);
    }
    if (!volume_label_matches(volume->volumeLabel)) {
        return fail(MOUNT_LABEL_MISMATCH, createdDocuments);
    }

    vfs::FileInfo markerInfo{};
    if (vfs::stat(kMarkerPath, &markerInfo) != vfs::VFS_OK ||
        markerInfo.type != vfs::FILE_TYPE_REGULAR ||
        markerInfo.size != sizeof(kMarkerContents) - 1u) {
        return fail(MOUNT_MARKER_MISSING, createdDocuments);
    }
    char markerContents[sizeof(kMarkerContents)] = {};
    const int32_t markerBytes = vfs::read_file(kMarkerPath, markerContents,
                                               sizeof(markerContents) - 1u);
    if (markerBytes != static_cast<int32_t>(sizeof(kMarkerContents) - 1u) ||
        !same_text(markerContents, kMarkerContents)) {
        return fail(MOUNT_MARKER_INVALID, createdDocuments);
    }

    vfs::FileInfo appDataInfo{};
    if (vfs::stat(kAppDataDirectory, &appDataInfo) != vfs::VFS_OK) {
        return fail(MOUNT_APPDATA_MISSING, createdDocuments);
    }
    if (appDataInfo.type != vfs::FILE_TYPE_DIRECTORY) {
        return fail(MOUNT_APPDATA_NOT_DIRECTORY, createdDocuments);
    }

    if (vfs::mount_alias(kAppDataAlias, kAppDataDirectory) == 0xFFu) {
        return fail(MOUNT_ALIAS_FAILED, createdDocuments);
    }
    bool createdAlias = true;

    if (block::device_generation(deviceIndex) != generation ||
        !verify_existing_mount(deviceIndex, generation)) {
        return fail(MOUNT_STATE_CHANGED, createdDocuments, createdAlias);
    }

    s_lastMountStatus = MOUNT_OK;
    serial::puts("[USERDATA] mount=ready path=/Documents appData=/AppData device=");
    serial::puts(device->name);
    serial::puts(" stableId=");
    serial::puts(device->stableId);
    serial::puts(" generation=");
    serial::put_hex64(generation);
    serial::puts(" filesystem=FAT32 label=GXOSDATA partitionOffset=0\n");
    return true;
}

MountStatus last_mount_status()
{
    return s_lastMountStatus;
}

const char* mount_status_name(MountStatus status)
{
    switch (status) {
        case MOUNT_NOT_ATTEMPTED: return "not-attempted";
        case MOUNT_OK: return "ok";
        case MOUNT_ALREADY_READY: return "already-ready";
        case MOUNT_SYSTEM_SOURCE_UNAVAILABLE: return "trusted-system-source-unavailable";
        case MOUNT_DEVICE_MISSING: return "data-device-missing";
        case MOUNT_DEVICE_AMBIGUOUS: return "data-device-identity-ambiguous";
        case MOUNT_DEVICE_IS_SYSTEM_SOURCE: return "data-device-is-system-source";
        case MOUNT_DEVICE_UNSUPPORTED: return "data-device-transport-unsupported";
        case MOUNT_DEVICE_READ_ONLY: return "data-device-read-only";
        case MOUNT_DEVICE_NO_FLUSH: return "data-device-flush-unavailable";
        case MOUNT_DEVICE_INVALID: return "data-device-invalid";
        case MOUNT_PATH_BUSY: return "documents-path-already-owned";
        case MOUNT_FILESYSTEM_FAILED: return "filesystem-invalid-or-unsupported";
        case MOUNT_NOT_FAT32: return "filesystem-not-fat32";
        case MOUNT_GEOMETRY_MISMATCH: return "filesystem-geometry-mismatch";
        case MOUNT_LABEL_MISMATCH: return "volume-label-mismatch";
        case MOUNT_MARKER_MISSING: return "volume-marker-missing";
        case MOUNT_MARKER_INVALID: return "volume-marker-incompatible";
        case MOUNT_APPDATA_MISSING: return "appdata-directory-missing";
        case MOUNT_APPDATA_NOT_DIRECTORY: return "appdata-path-not-directory";
        case MOUNT_ALIAS_FAILED: return "appdata-alias-failed";
        case MOUNT_STATE_CHANGED: return "device-or-mount-changed";
        default: return "unknown";
    }
}

} // namespace user_data_volume
} // namespace kernel
