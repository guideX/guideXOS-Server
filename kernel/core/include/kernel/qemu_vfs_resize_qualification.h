#ifndef KERNEL_QEMU_VFS_RESIZE_QUALIFICATION_H
#define KERNEL_QEMU_VFS_RESIZE_QUALIFICATION_H

namespace kernel {
namespace qemu_vfs_resize_qualification {

// Test-only production ATA/VFS/FAT32 persistence qualification, enabled only
// by GXOS_QEMU_VFS_RESIZE_QUALIFICATION.
bool run();

} // namespace qemu_vfs_resize_qualification
} // namespace kernel

#endif // KERNEL_QEMU_VFS_RESIZE_QUALIFICATION_H
