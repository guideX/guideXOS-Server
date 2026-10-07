#include "include/kernel/file_association_service.h"
#include "include/kernel/vfs.h"

namespace kernel { namespace appmodel {
namespace {
const char* const kPaths[2] = { "/GXAS0.BIN", "/GXAS1.BIN" };

bool readSlot(void*, uint32_t slot, void* buffer, uint32_t size, bool* exists) {
    if (slot > 1u || !buffer || !exists) return false;
    *exists = false;
    vfs::FileInfo info{};
    const vfs::Status status = vfs::stat(kPaths[slot], &info);
    if (status == vfs::VFS_ERR_NOT_FOUND || status == vfs::VFS_ERR_NOT_MOUNT)
        return status == vfs::VFS_ERR_NOT_FOUND;
    *exists = true;
    if (status != vfs::VFS_OK || info.type != vfs::FILE_TYPE_REGULAR || info.size != size)
        return false;
    return vfs::read_file(kPaths[slot], buffer, size) == static_cast<int32_t>(size);
}

bool writeSlot(void*, uint32_t slot, const void* buffer, uint32_t size) {
    return slot < 2u && vfs::write_file(kPaths[slot], buffer, size) ==
        static_cast<int32_t>(size);
}

// write_file completes the VFS operation before returning; this VFS has no
// path-level flush operation, so the adapter reports that no extra flush is needed.
bool flushSlot(void*, uint32_t slot) { return slot < 2u; }
}

void initializeFileAssociationVfsStorage() {
    static const AssociationStorage storage = { nullptr, readSlot, writeSlot, flushSlot };
    setFileAssociationStorage(&storage);
}
} }
