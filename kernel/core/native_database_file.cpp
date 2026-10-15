#include "include/kernel/native_database_file.h"

#include "include/kernel/vfs.h"

namespace {

using kernel::vfs::FileInfo;
using kernel::vfs::Status;

static int32_t nativeOpen(const char* path, uint16_t flags) {
    return static_cast<int32_t>(kernel::vfs::open(path, flags));
}
static int32_t nativeClose(uint8_t handle) {
    return static_cast<int32_t>(kernel::vfs::close(handle));
}
static int32_t nativeRead(uint8_t handle, void* buffer, uint32_t length) {
    return kernel::vfs::read(handle, buffer, length);
}
static int32_t nativeWrite(uint8_t handle, const void* buffer, uint32_t length) {
    return kernel::vfs::write(handle, buffer, length);
}
static int32_t nativeSeek(uint8_t handle, int64_t offset, uint8_t origin) {
    return static_cast<int32_t>(kernel::vfs::seek(
        handle, offset, static_cast<kernel::vfs::SeekOrigin>(origin)));
}
static int64_t nativeTell(uint8_t handle) {
    return kernel::vfs::tell(handle);
}
static int64_t nativeFileSize(uint8_t handle) {
    return kernel::vfs::file_size(handle);
}
static int32_t nativeFlush(uint8_t handle) {
    return static_cast<int32_t>(kernel::vfs::flush(handle));
}
static int32_t nativeTruncateShrink(uint8_t handle, uint64_t size) {
    return static_cast<int32_t>(kernel::vfs::truncate_shrink(handle, size));
}
static int32_t nativeStat(const char* path, uint32_t* type, uint64_t* size) {
    FileInfo info;
    const Status status = kernel::vfs::stat(path, &info);
    if (status == kernel::vfs::VFS_OK) {
        if (type) *type = static_cast<uint32_t>(info.type);
        if (size) *size = info.size;
    }
    return static_cast<int32_t>(status);
}
static int32_t nativeUnlink(const char* path) {
    return static_cast<int32_t>(kernel::vfs::unlink(path));
}

const GxDbNativeFileOps kProductionOps = {
    nativeOpen,
    nativeClose,
    nativeRead,
    nativeWrite,
    nativeSeek,
    nativeTell,
    nativeFileSize,
    nativeFlush,
    nativeTruncateShrink,
    nativeStat,
    nativeUnlink,
};

GxDbNativeFileOps s_ops = kProductionOps;

struct BridgeFile {
    uint8_t active;
    uint8_t nativeHandle;
    uint8_t readOnly;
    uint32_t generation;
};

BridgeFile s_files[32] = {};

static bool asciiEqualIgnoreCase(char a, char b) {
    if (a >= 'a' && a <= 'z') a = static_cast<char>(a - 'a' + 'A');
    if (b >= 'a' && b <= 'z') b = static_cast<char>(b - 'a' + 'A');
    return a == b;
}

static bool isNameChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_';
}

static char toUpperAscii(char c) {
    return c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c;
}

static GxDbFileStatus mapVfsStatus(int32_t status) {
    if (status >= 0) return status == kernel::vfs::VFS_OK
        ? GX_DB_FILE_OK : GX_DB_FILE_IO;
    switch (status) {
        case kernel::vfs::VFS_ERR_NOT_FOUND: return GX_DB_FILE_NOT_FOUND;
        case kernel::vfs::VFS_ERR_EXISTS: return GX_DB_FILE_ALREADY_EXISTS;
        case kernel::vfs::VFS_ERR_IS_DIR: return GX_DB_FILE_IS_DIRECTORY;
        case kernel::vfs::VFS_ERR_READ_ONLY: return GX_DB_FILE_READ_ONLY;
        case kernel::vfs::VFS_ERR_NO_SPACE: return GX_DB_FILE_NO_SPACE;
        case kernel::vfs::VFS_ERR_NOT_SUPPORTED: return GX_DB_FILE_NOT_SUPPORTED;
        case kernel::vfs::VFS_ERR_RESOURCE_LIMIT: return GX_DB_FILE_RESOURCE_LIMIT;
        case kernel::vfs::VFS_ERR_BUSY: return GX_DB_FILE_BUSY;
        case kernel::vfs::VFS_ERR_CORRUPT_CHAIN:
        case kernel::vfs::VFS_ERR_CORRUPT_DIRECTORY: return GX_DB_FILE_CORRUPT;
        case kernel::vfs::VFS_ERR_IO_TIMEOUT: return GX_DB_FILE_IO_TIMEOUT;
        case kernel::vfs::VFS_ERR_INVALID:
        case kernel::vfs::VFS_ERR_NOT_DIR: return GX_DB_FILE_INVALID_ARGUMENT;
        default: return GX_DB_FILE_IO;
    }
}

static GxDbFileStatus mapPathStatus(int32_t status) {
    if (status == kernel::vfs::VFS_ERR_NOT_FOUND) return GX_DB_FILE_NOT_FOUND;
    return mapVfsStatus(status);
}

static GxDbFileStatus validateHandle(GxDbFileHandle handle, BridgeFile*& out) {
    if (handle == GX_DB_FILE_INVALID_HANDLE) return GX_DB_FILE_STALE_HANDLE;
    const uint32_t index = handle & 31u;
    const uint32_t generation = handle >> 5;
    if (generation == 0 || !s_files[index].active ||
        s_files[index].generation != generation) {
        return GX_DB_FILE_STALE_HANDLE;
    }
    out = &s_files[index];
    return GX_DB_FILE_OK;
}

static GxDbFileHandle makeHandle(uint32_t index, uint32_t generation) {
    return (generation << 5) | index;
}

static bool validRange(uint64_t offset, size_t length) {
    if (offset > 0xFFFFFFFFull) return false;
    const uint64_t count = static_cast<uint64_t>(length);
    return count <= 0xFFFFFFFFull - offset;
}

static GxDbFileStatus statPath(const char* path, uint32_t* type, uint64_t* size) {
    return mapPathStatus(s_ops.stat(path, type, size));
}

static GxDbFileStatus restorePosition(uint8_t nativeHandle, int64_t position) {
    return mapVfsStatus(s_ops.seek(nativeHandle, position,
                                   static_cast<uint8_t>(kernel::vfs::SEEK_SET)));
}

} // namespace

extern "C" void gx_db_file_bind_ops(const GxDbNativeFileOps* ops) {
    if (!ops) {
        s_ops = kProductionOps;
        return;
    }
    s_ops = *ops;
}

extern "C" GxDbFileStatus gx_db_file_map_path(const char* logicalPath,
                                                char* outPath,
                                                size_t outPathSize) {
    if (!logicalPath || !outPath || outPathSize == 0) {
        return GX_DB_FILE_INVALID_ARGUMENT;
    }
    outPath[0] = '\0';
    size_t length = 0;
    while (logicalPath[length] && length < kernel::vfs::VFS_MAX_PATH) ++length;
    if (logicalPath[length] || length < 2 || logicalPath[0] != '/') {
        return GX_DB_FILE_INVALID_NAME;
    }

    size_t suffixLength = 0;
    const char* physicalExtension = nullptr;
    if (length >= 5 && logicalPath[length - 5] == '.' &&
        asciiEqualIgnoreCase(logicalPath[length - 4], 'g') &&
        asciiEqualIgnoreCase(logicalPath[length - 3], 'x') &&
        asciiEqualIgnoreCase(logicalPath[length - 2], 'd') &&
        asciiEqualIgnoreCase(logicalPath[length - 1], 'b')) {
        suffixLength = 5;
        physicalExtension = ".GDB";
    } else if (length >= 6 && logicalPath[length - 6] == '.' &&
               asciiEqualIgnoreCase(logicalPath[length - 5], 'g') &&
               asciiEqualIgnoreCase(logicalPath[length - 4], 'x') &&
               asciiEqualIgnoreCase(logicalPath[length - 3], 'w') &&
               asciiEqualIgnoreCase(logicalPath[length - 2], 'a') &&
               asciiEqualIgnoreCase(logicalPath[length - 1], 'l')) {
        suffixLength = 6;
        physicalExtension = ".GWL";
    } else {
        return GX_DB_FILE_INVALID_NAME;
    }

    size_t basenameStart = length;
    while (basenameStart > 0 && logicalPath[basenameStart - 1] != '/') --basenameStart;
    if (basenameStart == 0) return GX_DB_FILE_INVALID_NAME;
    const size_t stemEnd = length - suffixLength;
    const size_t stemLength = stemEnd - basenameStart;
    if (stemLength == 0 || stemLength > 8) return GX_DB_FILE_INVALID_NAME;

    // Every parent component is one 8.3-compatible short directory name.
    size_t componentStart = 1;
    for (size_t i = 1; i < basenameStart; ++i) {
        if (logicalPath[i] == '/') {
            const size_t componentLength = i - componentStart;
            if (componentLength == 0 || componentLength > 8) {
                return GX_DB_FILE_INVALID_NAME;
            }
            for (size_t j = componentStart; j < i; ++j) {
                if (!isNameChar(logicalPath[j])) return GX_DB_FILE_INVALID_NAME;
            }
            componentStart = i + 1;
        }
    }
    if (componentStart < basenameStart) {
        const size_t componentLength = basenameStart - componentStart;
        if (componentLength > 8) return GX_DB_FILE_INVALID_NAME;
        for (size_t i = componentStart; i < basenameStart; ++i) {
            if (!isNameChar(logicalPath[i])) return GX_DB_FILE_INVALID_NAME;
        }
    }
    for (size_t i = basenameStart; i < stemEnd; ++i) {
        if (!isNameChar(logicalPath[i])) return GX_DB_FILE_INVALID_NAME;
    }

    const size_t resultLength = stemEnd + 4;
    if (resultLength + 1 > outPathSize || resultLength >= kernel::vfs::VFS_MAX_PATH) {
        return GX_DB_FILE_RESOURCE_LIMIT;
    }
    // Keep directory and mount-point spelling unchanged for VFS routing;
    // FAT lookup folds those components to its short-name representation.
    for (size_t i = 0; i < stemEnd; ++i) {
        outPath[i] = i < basenameStart ? logicalPath[i] : toUpperAscii(logicalPath[i]);
    }
    outPath[stemEnd] = physicalExtension[0];
    outPath[stemEnd + 1] = physicalExtension[1];
    outPath[stemEnd + 2] = physicalExtension[2];
    outPath[stemEnd + 3] = physicalExtension[3];
    outPath[resultLength] = '\0';
    return GX_DB_FILE_OK;
}

extern "C" GxDbFileStatus gx_db_file_open(const char* logicalPath,
                                            GxDbFileOpenMode mode,
                                            uint8_t truncateExisting,
                                            GxDbFileHandle* outHandle) {
    if (!outHandle) return GX_DB_FILE_INVALID_ARGUMENT;
    *outHandle = GX_DB_FILE_INVALID_HANDLE;
    if (mode != GX_DB_OPEN_READ_ONLY && mode != GX_DB_OPEN_READ_WRITE &&
        mode != GX_DB_OPEN_CREATE) return GX_DB_FILE_INVALID_ARGUMENT;

    char path[kernel::vfs::VFS_MAX_PATH];
    GxDbFileStatus result = gx_db_file_map_path(logicalPath, path, sizeof(path));
    if (result != GX_DB_FILE_OK) return result;

    uint32_t type = 0;
    uint64_t existingSize = 0;
    result = statPath(path, &type, &existingSize);
    const bool exists = result == GX_DB_FILE_OK;
    if (!exists && result != GX_DB_FILE_NOT_FOUND) return result;
    if (exists && type == GX_DB_NATIVE_FILE_DIRECTORY) return GX_DB_FILE_IS_DIRECTORY;
    if (!exists && mode != GX_DB_OPEN_CREATE) return GX_DB_FILE_NOT_FOUND;
    if (exists && mode == GX_DB_OPEN_CREATE && !truncateExisting && existingSize != 0) {
        return GX_DB_FILE_ALREADY_EXISTS;
    }

    uint16_t flags = mode == GX_DB_OPEN_READ_ONLY ? kernel::vfs::OPEN_READ
                                                   : kernel::vfs::OPEN_RDWR;
    if (!exists && mode == GX_DB_OPEN_CREATE) {
        flags = static_cast<uint16_t>(flags | kernel::vfs::OPEN_CREATE |
                                      kernel::vfs::OPEN_EXCL);
    }
    const int32_t nativeHandle = s_ops.open(path, flags);
    if (nativeHandle < 0 || nativeHandle >= 32) {
        return nativeHandle == kernel::vfs::VFS_ERR_READ_ONLY
            ? GX_DB_FILE_READ_ONLY : GX_DB_FILE_IO;
    }

    if (exists && mode == GX_DB_OPEN_CREATE && truncateExisting) {
        const int32_t truncateStatus = s_ops.truncate_shrink(
            static_cast<uint8_t>(nativeHandle), 0);
        if (truncateStatus != kernel::vfs::VFS_OK) {
            (void)s_ops.close(static_cast<uint8_t>(nativeHandle));
            return mapVfsStatus(truncateStatus);
        }
    }

    uint32_t index = 32;
    for (uint32_t i = 0; i < 32; ++i) {
        if (!s_files[i].active) {
            index = i;
            break;
        }
    }
    if (index == 32) {
        (void)s_ops.close(static_cast<uint8_t>(nativeHandle));
        return GX_DB_FILE_BUSY;
    }
    BridgeFile& slot = s_files[index];
    if (slot.generation == 0) slot.generation = 1;
    slot.nativeHandle = static_cast<uint8_t>(nativeHandle);
    slot.readOnly = mode == GX_DB_OPEN_READ_ONLY;
    slot.active = 1;
    *outHandle = makeHandle(index, slot.generation);
    return GX_DB_FILE_OK;
}

extern "C" GxDbFileStatus gx_db_file_get_size(GxDbFileHandle handle,
                                                uint64_t* outSize) {
    if (!outSize) return GX_DB_FILE_INVALID_ARGUMENT;
    BridgeFile* slot = nullptr;
    GxDbFileStatus result = validateHandle(handle, slot);
    if (result != GX_DB_FILE_OK) return result;
    const int64_t size = s_ops.file_size(slot->nativeHandle);
    if (size < 0) return GX_DB_FILE_IO;
    if (static_cast<uint64_t>(size) > 0xFFFFFFFFull) return GX_DB_FILE_RESOURCE_LIMIT;
    *outSize = static_cast<uint64_t>(size);
    return GX_DB_FILE_OK;
}

extern "C" GxDbFileStatus gx_db_file_read_at(GxDbFileHandle handle,
                                               uint64_t offset,
                                               void* buffer, size_t length) {
    BridgeFile* slot = nullptr;
    GxDbFileStatus result = validateHandle(handle, slot);
    if (result != GX_DB_FILE_OK) return result;
    if ((buffer == nullptr && length != 0) || !validRange(offset, length)) {
        return buffer == nullptr && length != 0
            ? GX_DB_FILE_INVALID_ARGUMENT : GX_DB_FILE_RESOURCE_LIMIT;
    }
    if (length == 0) return GX_DB_FILE_OK;
    const int64_t original = s_ops.tell(slot->nativeHandle);
    if (original < 0) return GX_DB_FILE_IO;
    result = mapVfsStatus(s_ops.seek(slot->nativeHandle, static_cast<int64_t>(offset),
                                     static_cast<uint8_t>(kernel::vfs::SEEK_SET)));
    int32_t count = -1;
    if (result == GX_DB_FILE_OK) {
        count = s_ops.read(slot->nativeHandle, buffer, static_cast<uint32_t>(length));
        if (count < 0) result = mapVfsStatus(count);
        else if (static_cast<size_t>(count) != length) result = GX_DB_FILE_SHORT_READ;
    }
    const GxDbFileStatus restored = restorePosition(slot->nativeHandle, original);
    if (restored != GX_DB_FILE_OK && result == GX_DB_FILE_OK) return restored;
    return result;
}

extern "C" GxDbFileStatus gx_db_file_write_at(GxDbFileHandle handle,
                                                uint64_t offset,
                                                const void* buffer, size_t length) {
    BridgeFile* slot = nullptr;
    GxDbFileStatus result = validateHandle(handle, slot);
    if (result != GX_DB_FILE_OK) return result;
    if (slot->readOnly) return GX_DB_FILE_READ_ONLY;
    if ((buffer == nullptr && length != 0) || !validRange(offset, length)) {
        return buffer == nullptr && length != 0
            ? GX_DB_FILE_INVALID_ARGUMENT : GX_DB_FILE_RESOURCE_LIMIT;
    }
    if (length == 0) return GX_DB_FILE_OK;
    const int64_t original = s_ops.tell(slot->nativeHandle);
    if (original < 0) return GX_DB_FILE_IO;
    result = mapVfsStatus(s_ops.seek(slot->nativeHandle, static_cast<int64_t>(offset),
                                     static_cast<uint8_t>(kernel::vfs::SEEK_SET)));
    int32_t count = -1;
    if (result == GX_DB_FILE_OK) {
        count = s_ops.write(slot->nativeHandle, buffer, static_cast<uint32_t>(length));
        if (count < 0) result = mapVfsStatus(count);
        else if (static_cast<size_t>(count) != length) result = GX_DB_FILE_SHORT_WRITE;
    }
    const GxDbFileStatus restored = restorePosition(slot->nativeHandle, original);
    if (restored != GX_DB_FILE_OK && result == GX_DB_FILE_OK) return restored;
    return result;
}

extern "C" GxDbFileStatus gx_db_file_truncate_to(GxDbFileHandle handle,
                                                   uint64_t size) {
    BridgeFile* slot = nullptr;
    GxDbFileStatus result = validateHandle(handle, slot);
    if (result != GX_DB_FILE_OK) return result;
    if (slot->readOnly) return GX_DB_FILE_READ_ONLY;
    if (size > 0xFFFFFFFFull) return GX_DB_FILE_RESOURCE_LIMIT;
    uint64_t currentSize = 0;
    result = gx_db_file_get_size(handle, &currentSize);
    if (result != GX_DB_FILE_OK) return result;
    if (size > currentSize) return GX_DB_FILE_NOT_SUPPORTED;
    return mapVfsStatus(s_ops.truncate_shrink(slot->nativeHandle, size));
}

extern "C" GxDbFileStatus gx_db_file_flush(GxDbFileHandle handle) {
    BridgeFile* slot = nullptr;
    GxDbFileStatus result = validateHandle(handle, slot);
    if (result != GX_DB_FILE_OK) return result;
    return mapVfsStatus(s_ops.flush(slot->nativeHandle));
}

extern "C" GxDbFileStatus gx_db_file_close(GxDbFileHandle handle) {
    BridgeFile* slot = nullptr;
    GxDbFileStatus result = validateHandle(handle, slot);
    if (result != GX_DB_FILE_OK) return result;
    const int32_t nativeStatus = s_ops.close(slot->nativeHandle);
    slot->active = 0;
    slot->readOnly = 0;
    slot->nativeHandle = 0xFF;
    slot->generation = (slot->generation + 1u) & 0x07FFFFFFu;
    if (slot->generation == 0) slot->generation = 1;
    return mapVfsStatus(nativeStatus);
}

extern "C" GxDbFileStatus gx_db_file_exists(const char* logicalPath,
                                              uint8_t* outExists) {
    if (!outExists) return GX_DB_FILE_INVALID_ARGUMENT;
    *outExists = 0;
    char path[kernel::vfs::VFS_MAX_PATH];
    GxDbFileStatus result = gx_db_file_map_path(logicalPath, path, sizeof(path));
    if (result != GX_DB_FILE_OK) return result;
    uint32_t type = 0;
    uint64_t size = 0;
    result = statPath(path, &type, &size);
    if (result == GX_DB_FILE_NOT_FOUND) return GX_DB_FILE_OK;
    if (result != GX_DB_FILE_OK) return result;
    *outExists = 1;
    return GX_DB_FILE_OK;
}

extern "C" GxDbFileStatus gx_db_file_remove(const char* logicalPath) {
    char path[kernel::vfs::VFS_MAX_PATH];
    GxDbFileStatus result = gx_db_file_map_path(logicalPath, path, sizeof(path));
    if (result != GX_DB_FILE_OK) return result;
    uint32_t type = 0;
    uint64_t size = 0;
    result = statPath(path, &type, &size);
    if (result == GX_DB_FILE_NOT_FOUND) return GX_DB_FILE_OK;
    if (result != GX_DB_FILE_OK) return result;
    if (type == GX_DB_NATIVE_FILE_DIRECTORY) return GX_DB_FILE_IS_DIRECTORY;
    result = mapVfsStatus(s_ops.unlink(path));
    if (result == GX_DB_FILE_NOT_FOUND) {
        uint32_t checkType = 0;
        uint64_t checkSize = 0;
        GxDbFileStatus checked = statPath(path, &checkType, &checkSize);
        return checked == GX_DB_FILE_NOT_FOUND ? GX_DB_FILE_OK :
               checked == GX_DB_FILE_OK ? GX_DB_FILE_IO : checked;
    }
    if (result != GX_DB_FILE_OK) return result;
    uint32_t checkType = 0;
    uint64_t checkSize = 0;
    GxDbFileStatus checked = statPath(path, &checkType, &checkSize);
    if (checked == GX_DB_FILE_NOT_FOUND) return GX_DB_FILE_OK;
    return checked == GX_DB_FILE_OK ? GX_DB_FILE_IO : checked;
}
