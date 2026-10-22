#include "kernel/vfs.h"
#include "kernel/block_device.h"
#include "kernel/fs_fat.h"
#include "kernel/fs_ext4.h"

#include <cstring>
#include <iostream>

namespace {
int g_checks = 0;
int g_failures = 0;
int g_fatUnmounts = 0;
kernel::block::BlockDevice g_devices[2]{};

void check(bool condition, const char* label)
{
    ++g_checks;
    if (condition) {
        std::cout << "PASS: " << label << "\n";
    } else {
        ++g_failures;
        std::cout << "FAIL: " << label << "\n";
    }
}

kernel::block::Status writable_device(uint8_t, uint64_t, uint32_t, const void*)
{
    return kernel::block::BLOCK_OK;
}

bool table_count_matches_active_slots()
{
    uint8_t active = 0;
    for (uint8_t index = 0; index < kernel::vfs::VFS_MAX_MOUNTS; ++index) {
        if (kernel::vfs::get_mount_by_index(index)) ++active;
    }
    return active == kernel::vfs::mount_count();
}
}

namespace kernel {
namespace block {
const BlockDevice* get_device(uint8_t index)
{
    return index < 2 ? &g_devices[index] : nullptr;
}

uint64_t device_generation(uint8_t index)
{
    return index < 2 && g_devices[index].active ? 41u + index : 0;
}

Status read_sectors(uint8_t, uint64_t, uint32_t, void*)
{
    return BLOCK_OK;
}
}

namespace arch {
namespace amd64 {
uint8_t inb(uint16_t) { return 0x20; }
void outb(uint16_t, uint8_t) {}
}
}

namespace fs_fat {
uint8_t mount(uint8_t blockDevIndex)
{
    return blockDevIndex;
}

void unmount(uint8_t)
{
    ++g_fatUnmounts;
}

void close_file(uint8_t) {}
uint32_t write_file(uint8_t, const void*, uint32_t) { return 0; }
bool open_root_dir(uint8_t) { return false; }
bool open_dir(uint8_t, uint32_t) { return false; }
bool read_dir(uint8_t, DirEntry*) { return false; }
void close_dir(uint8_t) {}
uint8_t open_file(uint8_t, uint32_t, uint32_t, uint8_t) { return 0xFF; }
uint32_t read_file(uint8_t, void*, uint32_t) { return 0; }
bool seek_file(uint8_t, uint32_t) { return false; }
bool delete_path(uint8_t, const char*, bool) { return false; }
bool rename_path(uint8_t, const char*, const char*) { return false; }
block::Status last_io_status() { return block::BLOCK_OK; }
DeleteStatus last_delete_status() { return DELETE_OK; }
TraversalStatus last_traversal_status() { return TRAVERSAL_OK; }
FileWriteStatus overwrite_path_status(uint8_t, const char*, const void*, uint32_t,
                                       block::Status* status)
{
    if (status) *status = block::BLOCK_OK;
    return FILE_WRITE_OK;
}
const char* file_write_status_name(FileWriteStatus) { return "stub"; }
FileWriteStatus create_file_path_status(uint8_t, const char*, const void*, uint32_t,
                                         block::Status* status)
{
    if (status) *status = block::BLOCK_OK;
    return FILE_WRITE_OK;
}
DirectoryCreateStatus create_directory_path_status(uint8_t, const char*,
                                                   block::Status* status)
{
    if (status) *status = block::BLOCK_OK;
    return DIRECTORY_CREATE_OK;
}
const char* directory_create_status_name(DirectoryCreateStatus) { return "stub"; }
FileWriteStatus update_file_size_path_status(uint8_t, const char*, uint32_t,
                                             block::Status* status)
{
    if (status) *status = block::BLOCK_OK;
    return FILE_WRITE_OK;
}
bool flush(uint8_t, block::Status* status)
{
    if (status) *status = block::BLOCK_OK;
    return true;
}

bool lookup_path(uint8_t, const char* path, DirEntry* out)
{
    if (!path || !out) return false;
    while (*path == '/') ++path;
    if (std::strcmp(path, "Apps") != 0 &&
        std::strcmp(path, "Apps/Navigator") != 0) return false;
    std::memset(out, 0, sizeof(*out));
    std::strcpy(out->name, std::strcmp(path, "Apps") == 0 ? "Apps" : "Navigator");
    out->isDir = true;
    return true;
}
}

namespace fs_ext4 {
uint8_t mount(uint8_t blockDevIndex)
{
    return blockDevIndex;
}

void unmount(uint8_t)
{
}

bool open_root_dir(uint8_t) { return false; }
bool read_dir(uint8_t, Ext4DirEntry*) { return false; }
}

}

int main()
{
    for (uint8_t index = 0; index < 2; ++index) {
        g_devices[index].active = true;
        g_devices[index].writeFn = writable_device;
    }

    kernel::vfs::init();
    kernel::vfs::init();
    check(kernel::vfs::mount_count() == 0 && table_count_matches_active_slots(),
          "repeated initialization preserves an empty mount table");

    const uint8_t root = kernel::vfs::mount_type("/", 0, kernel::vfs::FS_TYPE_FAT32);
    check(root == 0 && kernel::vfs::mount_count() == 1 && table_count_matches_active_slots(),
          "root registration activates one slot and increments the count once");
    check(kernel::vfs::get_mount("/Apps") == kernel::vfs::get_mount_by_index(root),
          "root path resolves through its registered slot");

    check(kernel::vfs::mount_type("/", 1, kernel::vfs::FS_TYPE_FAT32) == 0xFF &&
              kernel::vfs::mount_count() == 1 && table_count_matches_active_slots(),
          "duplicate path rejection leaves table and count unchanged");

    const uint8_t system = kernel::vfs::mount_type("/system", 1, kernel::vfs::FS_TYPE_FAT32);
    check(system == 1 && kernel::vfs::mount_count() == 2 && table_count_matches_active_slots(),
          "second mount occupies the next free slot");

    const uint8_t config = kernel::vfs::mount_alias("/config", "/Apps");
    check(config == 2 && kernel::vfs::mount_count() == 3 && table_count_matches_active_slots(),
          "alias registration occupies a slot and increments the count once");
    const kernel::vfs::MountPoint* configMount = kernel::vfs::get_mount_by_index(config);
    check(configMount && configMount->active && configMount->alias &&
              configMount->blockDevIndex == 0 && configMount->blockDeviceGeneration == 41 &&
              configMount->fsVolumeIndex == 0 && std::strcmp(configMount->sourcePrefix, "/Apps") == 0,
          "alias owns inline path metadata and retains its source device generation");

    const uint8_t navigator = kernel::vfs::mount_alias("/config/navigator", "/Apps/Navigator");
    check(navigator == 3 && kernel::vfs::mount_count() == 4 && table_count_matches_active_slots(),
          "nested alias registers independently");
    check(kernel::vfs::get_mount("/config/navigator/home.html") ==
              kernel::vfs::get_mount_by_index(navigator),
          "longest matching mount path selects the nested alias");
    check(kernel::vfs::mount_alias("/config", "/Apps") == 0xFF &&
              kernel::vfs::mount_count() == 4 && table_count_matches_active_slots(),
          "duplicate alias rejection leaves table and count unchanged");

    kernel::vfs::init();
    check(kernel::vfs::mount_count() == 4 &&
              kernel::vfs::get_mount("/config/navigator/home.html") ==
                  kernel::vfs::get_mount_by_index(navigator) && table_count_matches_active_slots(),
          "initialization after registration preserves the live mount table");

    const char* extraPaths[] = { "/slot4", "/slot5", "/slot6", "/slot7" };
    bool allRemainingSlotsRegistered = true;
    for (const char* path : extraPaths) {
        allRemainingSlotsRegistered =
            kernel::vfs::mount_type(path, 0, kernel::vfs::FS_TYPE_FAT32) != 0xFF &&
            allRemainingSlotsRegistered;
    }
    check(allRemainingSlotsRegistered && kernel::vfs::mount_count() == kernel::vfs::VFS_MAX_MOUNTS &&
              table_count_matches_active_slots(),
          "mount table accepts exactly its eight valid slots");
    check(kernel::vfs::get_mount_by_index(kernel::vfs::VFS_MAX_MOUNTS) == nullptr &&
              kernel::vfs::mount_type("/overflow", 0, kernel::vfs::FS_TYPE_FAT32) == 0xFF &&
              kernel::vfs::mount_count() == kernel::vfs::VFS_MAX_MOUNTS &&
              table_count_matches_active_slots(),
          "out-of-range inspection and full-table rejection preserve invariants");

    check(kernel::vfs::unmount("/system") == kernel::vfs::VFS_OK &&
              kernel::vfs::mount_count() == kernel::vfs::VFS_MAX_MOUNTS - 1 &&
              !kernel::vfs::get_mount_by_index(system) &&
              table_count_matches_active_slots() && g_fatUnmounts == 1,
          "unmount deactivates one direct mount and decrements the count once");
    check(kernel::vfs::unmount("/system") == kernel::vfs::VFS_ERR_NOT_MOUNT &&
              kernel::vfs::mount_count() == kernel::vfs::VFS_MAX_MOUNTS - 1 &&
              table_count_matches_active_slots() &&
              g_fatUnmounts == 1,
          "repeated unmount does not decrement the count or unmount twice");

    const uint8_t remountedSystem = kernel::vfs::mount_type("/system", 1, kernel::vfs::FS_TYPE_FAT32);
    check(remountedSystem == system && kernel::vfs::mount_count() == kernel::vfs::VFS_MAX_MOUNTS &&
              table_count_matches_active_slots() &&
              kernel::vfs::get_mount_by_index(remountedSystem)->blockDeviceGeneration == 42,
          "remount reuses the free slot with the current device generation");

    std::cout << "vfsMountTableChecks=" << (g_checks - g_failures) << "/" << g_checks << "\n";
    return g_failures == 0 ? 0 : 1;
}
