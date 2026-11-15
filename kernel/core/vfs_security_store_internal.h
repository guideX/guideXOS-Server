#ifndef KERNEL_VFS_SECURITY_STORE_INTERNAL_H
#define KERNEL_VFS_SECURITY_STORE_INTERNAL_H

#include "include/kernel/vfs.h"

// Internal native-runtime adapter entry point. This is kept out of the public
// kernel include tree; ordinary VFS mutation calls reject the reserved paths.
namespace kernel {
namespace vfs {
namespace internal {

int32_t writeSecurityStoreFile(const char* path, const void* buffer,
                               uint32_t size);

} // namespace internal
} // namespace vfs
} // namespace kernel

#endif
