#ifndef KERNEL_USER_DATA_VOLUME_H
#define KERNEL_USER_DATA_VOLUME_H

#include "kernel/types.h"

namespace kernel {
namespace user_data_volume {

enum MountStatus : uint8_t {
    MOUNT_NOT_ATTEMPTED = 0,
    MOUNT_OK,
    MOUNT_ALREADY_READY,
    MOUNT_SYSTEM_SOURCE_UNAVAILABLE,
    MOUNT_DEVICE_MISSING,
    MOUNT_DEVICE_AMBIGUOUS,
    MOUNT_DEVICE_IS_SYSTEM_SOURCE,
    MOUNT_DEVICE_UNSUPPORTED,
    MOUNT_DEVICE_READ_ONLY,
    MOUNT_DEVICE_NO_FLUSH,
    MOUNT_DEVICE_INVALID,
    MOUNT_PATH_BUSY,
    MOUNT_FILESYSTEM_FAILED,
    MOUNT_NOT_FAT32,
    MOUNT_GEOMETRY_MISMATCH,
    MOUNT_LABEL_MISMATCH,
    MOUNT_MARKER_MISSING,
    MOUNT_MARKER_INVALID,
    MOUNT_APPDATA_MISSING,
    MOUNT_APPDATA_NOT_DIRECTORY,
    MOUNT_ALIAS_FAILED,
    MOUNT_STATE_CHANGED,
};

bool mount_documents();
MountStatus last_mount_status();
const char* mount_status_name(MountStatus status);

} // namespace user_data_volume
} // namespace kernel

#endif // KERNEL_USER_DATA_VOLUME_H
