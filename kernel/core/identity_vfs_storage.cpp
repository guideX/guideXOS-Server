#include "include/kernel/identity_runtime.h"
#include "include/kernel/vfs.h"

namespace kernel {
namespace identity {
namespace {

const char* slotPath(uint8_t slot)
{
    return slot == 0u ? "/.gxos-identity-0.dat" :
        (slot == 1u ? "/.gxos-identity-1.dat" : nullptr);
}

StoreReadResult readSlot(void*, uint8_t slot, uint8_t* output,
                         size_t capacity, size_t* outBytes)
{
    if (outBytes) *outBytes = 0u;
    const char* path = slotPath(slot);
    if (!path || !output || !outBytes) return StoreReadResult::IoError;
    vfs::FileInfo info{};
    const vfs::Status stat = vfs::stat(path, &info);
    if (stat == vfs::VFS_ERR_NOT_FOUND) return StoreReadResult::Missing;
    if (stat != vfs::VFS_OK) return StoreReadResult::Unavailable;
    if (info.type != vfs::FILE_TYPE_REGULAR || info.size > capacity)
        return StoreReadResult::Corrupt;
    const int32_t read = vfs::read_file(path, output, static_cast<uint32_t>(capacity));
    if (read < 0 || static_cast<uint64_t>(read) != info.size)
        return StoreReadResult::IoError;
    *outBytes = static_cast<size_t>(read);
    return StoreReadResult::Ok;
}

StoreWriteResult writeSlot(void*, uint8_t slot, const uint8_t* bytes,
                           size_t byteCount)
{
    const char* path = slotPath(slot);
    if (!path || !bytes || byteCount == 0u || byteCount > kMaximumIdentityStoreBytes)
        return StoreWriteResult::IoError;
    const vfs::MountPoint* mount = vfs::get_mount(path);
    if (!mount || mount->fsType != vfs::FS_TYPE_FAT32)
        return StoreWriteResult::Unavailable;
    if (mount->readOnly) return StoreWriteResult::ReadOnly;
    const int32_t written = vfs::write_file(
        path, bytes, static_cast<uint32_t>(byteCount));
    return written == static_cast<int32_t>(byteCount)
        ? StoreWriteResult::Ok : StoreWriteResult::IoError;
}

} // namespace

StorageAdapter createVfsIdentityStorageAdapter()
{
    StorageAdapter adapter{};
    const vfs::MountPoint* root = vfs::get_mount("/");
    if (!root || root->fsType != vfs::FS_TYPE_FAT32) return adapter;
    adapter.context = nullptr;
    adapter.readSlot = readSlot;
    adapter.writeSlot = writeSlot;
    adapter.available = 1u;
    adapter.writable = root->readOnly ? 0u : 1u;
    return adapter;
}

} // namespace identity
} // namespace kernel
