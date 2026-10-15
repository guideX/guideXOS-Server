#include "kernel/native_database_file.h"
#include "kernel/vfs.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#ifdef SEEK_SET
#undef SEEK_SET
#endif
#ifdef SEEK_CUR
#undef SEEK_CUR
#endif
#ifdef SEEK_END
#undef SEEK_END
#endif

namespace {

struct MockHandle {
    bool open;
    bool writable;
    std::string path;
    uint64_t position;
    MockHandle() : open(false), writable(false), position(0) {}
};

std::map<std::string, std::vector<uint8_t> > g_files;
MockHandle g_handles[32];
int32_t g_statFailure = kernel::vfs::VFS_OK;
int32_t g_flushResult = kernel::vfs::VFS_OK;
int32_t g_closeResult = kernel::vfs::VFS_OK;
int32_t g_removeResult = kernel::vfs::VFS_OK;
bool g_shortRead = false;
bool g_shortWrite = false;
bool g_sizeFailure = false;
uint32_t g_checks = 0;
uint32_t g_failures = 0;

void check(bool condition, const char* name) {
    ++g_checks;
    if (!condition) {
        ++g_failures;
        std::printf("FAIL: %s\n", name);
    }
}

int32_t mockOpen(const char* path, uint16_t flags);
int32_t mockClose(uint8_t handle);
int32_t mockRead(uint8_t handle, void* buffer, uint32_t length);
int32_t mockWrite(uint8_t handle, const void* buffer, uint32_t length);
int32_t mockSeek(uint8_t handle, int64_t offset, uint8_t origin);
int64_t mockTell(uint8_t handle);
int64_t mockFileSize(uint8_t handle);
int32_t mockFlush(uint8_t handle);
int32_t mockTruncate(uint8_t handle, uint64_t size);
int32_t mockStat(const char* path, uint32_t* type, uint64_t* size);
int32_t mockUnlink(const char* path);

const GxDbNativeFileOps kMockOps = {
    mockOpen, mockClose, mockRead, mockWrite, mockSeek, mockTell,
    mockFileSize, mockFlush, mockTruncate, mockStat, mockUnlink
};

std::vector<uint8_t> expectedPageSequence(size_t length) {
    std::vector<uint8_t> bytes(length);
    for (size_t i = 0; i < length; ++i) {
        bytes[i] = static_cast<uint8_t>((i * 29u + (i >> 4) * 11u + 7u) & 0xFFu);
    }
    return bytes;
}

} // namespace

namespace kernel { namespace vfs {

uint8_t open(const char* path, uint16_t flags) {
    const int32_t result = mockOpen(path, flags);
    return result < 0 ? 0xFF : static_cast<uint8_t>(result);
}
Status close(uint8_t handle) { return static_cast<Status>(mockClose(handle)); }
int32_t read(uint8_t handle, void* buffer, uint32_t length) {
    return mockRead(handle, buffer, length);
}
int32_t write(uint8_t handle, const void* buffer, uint32_t length) {
    return mockWrite(handle, buffer, length);
}
Status seek(uint8_t handle, int64_t offset, SeekOrigin origin) {
    return static_cast<Status>(mockSeek(handle, offset, static_cast<uint8_t>(origin)));
}
int64_t tell(uint8_t handle) { return mockTell(handle); }
int64_t file_size(uint8_t handle) { return mockFileSize(handle); }
Status flush(uint8_t handle) { return static_cast<Status>(mockFlush(handle)); }
Status truncate_shrink(uint8_t handle, uint64_t size) {
    return static_cast<Status>(mockTruncate(handle, size));
}
Status stat(const char* path, FileInfo* info) {
    if (!info) return VFS_ERR_INVALID;
    uint32_t type = 0;
    uint64_t size = 0;
    const int32_t result = mockStat(path, &type, &size);
    if (result == VFS_OK) {
        info->type = static_cast<FileType>(type);
        info->size = size;
    }
    return static_cast<Status>(result);
}
Status unlink(const char* path) { return static_cast<Status>(mockUnlink(path)); }

} }

namespace {

int32_t mockOpen(const char* path, uint16_t flags) {
    if (!path) return kernel::vfs::VFS_ERR_INVALID;
    std::map<std::string, std::vector<uint8_t> >::iterator file = g_files.find(path);
    const bool exists = file != g_files.end();
    if (exists && (flags & kernel::vfs::OPEN_CREATE) &&
        (flags & kernel::vfs::OPEN_EXCL)) return kernel::vfs::VFS_ERR_EXISTS;
    if (!exists && !(flags & kernel::vfs::OPEN_CREATE)) return kernel::vfs::VFS_ERR_NOT_FOUND;
    if (!exists) file = g_files.insert(std::make_pair(
        std::string(path), std::vector<uint8_t>())).first;
    for (uint32_t i = 0; i < 32; ++i) {
        if (g_handles[i].open) continue;
        g_handles[i].open = true;
        g_handles[i].writable = (flags & kernel::vfs::OPEN_WRITE) != 0;
        g_handles[i].path = path;
        g_handles[i].position = 0;
        return static_cast<int32_t>(i);
    }
    return kernel::vfs::VFS_ERR_TOO_MANY;
}

int32_t mockClose(uint8_t handle) {
    if (handle >= 32 || !g_handles[handle].open) return kernel::vfs::VFS_ERR_INVALID;
    g_handles[handle].open = false;
    return g_closeResult;
}

int32_t mockRead(uint8_t handle, void* buffer, uint32_t length) {
    if (handle >= 32 || !g_handles[handle].open || !buffer) return kernel::vfs::VFS_ERR_INVALID;
    std::vector<uint8_t>& bytes = g_files[g_handles[handle].path];
    const size_t available = g_handles[handle].position < bytes.size()
        ? bytes.size() - static_cast<size_t>(g_handles[handle].position) : 0;
    size_t count = std::min<size_t>(available, length);
    if (g_shortRead && count > 0) --count;
    if (count) {
        std::memcpy(buffer, bytes.data() + g_handles[handle].position, count);
        g_handles[handle].position += count;
    }
    return static_cast<int32_t>(count);
}

int32_t mockWrite(uint8_t handle, const void* buffer, uint32_t length) {
    if (handle >= 32 || !g_handles[handle].open || !g_handles[handle].writable || !buffer) {
        return kernel::vfs::VFS_ERR_INVALID;
    }
    std::vector<uint8_t>& bytes = g_files[g_handles[handle].path];
    size_t count = length;
    if (g_shortWrite && count > 0) --count;
    const size_t end = static_cast<size_t>(g_handles[handle].position) + count;
    if (end > bytes.size()) bytes.resize(end, 0);
    if (count) std::memcpy(bytes.data() + g_handles[handle].position, buffer, count);
    g_handles[handle].position += count;
    return static_cast<int32_t>(count);
}

int32_t mockSeek(uint8_t handle, int64_t offset, uint8_t origin) {
    if (handle >= 32 || !g_handles[handle].open) return kernel::vfs::VFS_ERR_INVALID;
    int64_t base = 0;
    if (origin == kernel::vfs::SEEK_CUR) base = static_cast<int64_t>(g_handles[handle].position);
    else if (origin == kernel::vfs::SEEK_END) {
        base = static_cast<int64_t>(g_files[g_handles[handle].path].size());
    } else if (origin != kernel::vfs::SEEK_SET) return kernel::vfs::VFS_ERR_INVALID;
    const int64_t next = base + offset;
    if (next < 0 || next > 0xFFFFFFFFll) return kernel::vfs::VFS_ERR_INVALID;
    g_handles[handle].position = static_cast<uint64_t>(next);
    return kernel::vfs::VFS_OK;
}

int64_t mockTell(uint8_t handle) {
    if (handle >= 32 || !g_handles[handle].open) return -1;
    return static_cast<int64_t>(g_handles[handle].position);
}

int64_t mockFileSize(uint8_t handle) {
    if (handle >= 32 || !g_handles[handle].open || g_sizeFailure) return -1;
    return static_cast<int64_t>(g_files[g_handles[handle].path].size());
}

int32_t mockFlush(uint8_t handle) {
    return handle < 32 && g_handles[handle].open ? g_flushResult
                                                 : kernel::vfs::VFS_ERR_INVALID;
}

int32_t mockTruncate(uint8_t handle, uint64_t size) {
    if (handle >= 32 || !g_handles[handle].open || !g_handles[handle].writable) {
        return kernel::vfs::VFS_ERR_INVALID;
    }
    std::vector<uint8_t>& bytes = g_files[g_handles[handle].path];
    if (size > bytes.size()) return kernel::vfs::VFS_ERR_NOT_SUPPORTED;
    bytes.resize(static_cast<size_t>(size));
    return kernel::vfs::VFS_OK;
}

int32_t mockStat(const char* path, uint32_t* type, uint64_t* size) {
    if (g_statFailure != kernel::vfs::VFS_OK) return g_statFailure;
    std::map<std::string, std::vector<uint8_t> >::const_iterator file = g_files.find(path);
    if (file == g_files.end()) return kernel::vfs::VFS_ERR_NOT_FOUND;
    if (type) *type = kernel::vfs::FILE_TYPE_REGULAR;
    if (size) *size = file->second.size();
    return kernel::vfs::VFS_OK;
}

int32_t mockUnlink(const char* path) {
    if (g_removeResult != kernel::vfs::VFS_OK) return g_removeResult;
    return g_files.erase(path) ? kernel::vfs::VFS_OK : kernel::vfs::VFS_ERR_NOT_FOUND;
}

void testPathMapping() {
    char mapped[kernel::vfs::VFS_MAX_PATH];
    check(gx_db_file_map_path("/db/alpha.gxdb", mapped, sizeof(mapped)) == GX_DB_FILE_OK &&
          std::strcmp(mapped, "/db/ALPHA.GDB") == 0, "database maps deterministically to 8.3");
    check(gx_db_file_map_path("/db/alpha.gxwal", mapped, sizeof(mapped)) == GX_DB_FILE_OK &&
          std::strcmp(mapped, "/db/ALPHA.GWL") == 0, "WAL maps to a distinct 8.3 sidecar");
    check(gx_db_file_map_path("/db/beta.gxwal", mapped, sizeof(mapped)) == GX_DB_FILE_OK &&
          std::strcmp(mapped, "/db/BETA.GWL") == 0, "separate database identity gets its own WAL");
    check(gx_db_file_map_path("/db/this-name-is-too-long.gxdb", mapped, sizeof(mapped)) ==
          GX_DB_FILE_INVALID_NAME, "unsupported logical stem is rejected");
    check(gx_db_file_map_path("/longdirectory/alpha.gxdb", mapped, sizeof(mapped)) ==
          GX_DB_FILE_INVALID_NAME, "unsupported directory component is rejected");
    check(gx_db_file_map_path("/db/alpha.sqlite", mapped, sizeof(mapped)) ==
          GX_DB_FILE_INVALID_NAME, "unrecognized logical extension is rejected");
}

void testFileContract() {
    gx_db_file_bind_ops(&kMockOps);
    std::vector<uint8_t> expected = expectedPageSequence(5000);
    g_files["/db/ALPHA.GDB"] = expected;

    uint8_t exists = 0;
    check(gx_db_file_exists("/db/alpha.gxdb", &exists) == GX_DB_FILE_OK && exists == 1,
          "existing database lookup succeeds");
    check(gx_db_file_exists("/db/missing.gxdb", &exists) == GX_DB_FILE_OK && exists == 0,
          "absent database lookup is distinct from lookup failure");
    g_statFailure = kernel::vfs::VFS_ERR_IO_TIMEOUT;
    check(gx_db_file_exists("/db/missing.gxdb", &exists) == GX_DB_FILE_IO_TIMEOUT && exists == 0,
          "filesystem lookup timeout is not reported as absence");
    g_statFailure = kernel::vfs::VFS_OK;

    GxDbFileHandle data = GX_DB_FILE_INVALID_HANDLE;
    check(gx_db_file_open("/db/alpha.gxdb", GX_DB_OPEN_READ_WRITE, 0, &data) == GX_DB_FILE_OK,
          "open existing database for read/write");
    std::vector<uint8_t> exact(900);
    check(gx_db_file_read_at(data, 3700, exact.data(), exact.size()) == GX_DB_FILE_OK &&
          std::equal(exact.begin(), exact.end(), expected.begin() + 3700),
          "unaligned cross-page positional read returns exact bytes");
    check(mockTell(0) == 0,
          "positional read preserves native cursor");

    std::vector<uint8_t> patch(701);
    for (size_t i = 0; i < patch.size(); ++i) patch[i] = static_cast<uint8_t>(255u - i * 3u);
    check(gx_db_file_write_at(data, 4091, patch.data(), patch.size()) == GX_DB_FILE_OK,
          "unaligned positional write crosses a page boundary");
    std::copy(patch.begin(), patch.end(), expected.begin() + 4091);
    std::vector<uint8_t> verify(patch.size());
    check(gx_db_file_read_at(data, 4091, verify.data(), verify.size()) == GX_DB_FILE_OK &&
          verify == patch, "positional write preserves exact byte ordering");

    uint64_t size = 0;
    check(gx_db_file_get_size(data, &size) == GX_DB_FILE_OK && size == expected.size(),
          "status-bearing file-size query succeeds");
    g_sizeFailure = true;
    check(gx_db_file_get_size(data, &size) == GX_DB_FILE_IO,
          "file-size query failure remains an error");
    g_sizeFailure = false;
    std::vector<uint8_t> shortRead(20);
    check(gx_db_file_read_at(data, expected.size() - 4, shortRead.data(), shortRead.size()) ==
          GX_DB_FILE_SHORT_READ, "EOF does not become a successful exact read");

    check(gx_db_file_write_at(data, 0x100000000ull, patch.data(), 1) ==
          GX_DB_FILE_RESOURCE_LIMIT, "offset beyond FAT32 range is rejected");
    check(gx_db_file_write_at(data, 0xFFFFFFFFull, patch.data(), 1) ==
          GX_DB_FILE_RESOURCE_LIMIT, "offset plus length beyond FAT32 range is rejected");

    std::vector<uint8_t> appended(211);
    for (size_t i = 0; i < appended.size(); ++i) appended[i] = static_cast<uint8_t>(i ^ 0x5Au);
    check(gx_db_file_write_at(data, expected.size(), appended.data(), appended.size()) ==
          GX_DB_FILE_OK, "supported positional write extends the file");
    expected.insert(expected.end(), appended.begin(), appended.end());
    check(gx_db_file_get_size(data, &size) == GX_DB_FILE_OK && size == expected.size(),
          "write growth updates reported file size");

    g_shortWrite = true;
    check(gx_db_file_write_at(data, 700, patch.data(), 10) == GX_DB_FILE_SHORT_WRITE,
          "short write is returned as failure");
    g_shortWrite = false;
    check(gx_db_file_truncate_to(data, 64) == GX_DB_FILE_OK,
          "file shrinks through native FAT32 operation");
    check(gx_db_file_truncate_to(data, 65) == GX_DB_FILE_NOT_SUPPORTED,
          "truncate growth is explicitly unsupported");
    check(gx_db_file_truncate_to(data, 0) == GX_DB_FILE_OK,
          "file shrinks to zero");
    check(gx_db_file_flush(data) == GX_DB_FILE_OK, "native flush succeeds");
    g_flushResult = kernel::vfs::VFS_ERR_IO_TIMEOUT;
    check(gx_db_file_flush(data) == GX_DB_FILE_IO_TIMEOUT,
          "native flush failure propagates");
    g_flushResult = kernel::vfs::VFS_OK;

    check(gx_db_file_close(data) == GX_DB_FILE_OK, "close existing database");
    check(gx_db_file_close(data) == GX_DB_FILE_STALE_HANDLE,
          "closed bridge handle is invalidated");
    check(gx_db_file_get_size(data, &size) == GX_DB_FILE_STALE_HANDLE,
          "stale handle cannot be reused after slot recycling");

    GxDbFileHandle created = GX_DB_FILE_INVALID_HANDLE;
    check(gx_db_file_open("/db/beta.gxdb", GX_DB_OPEN_CREATE, 0, &created) == GX_DB_FILE_OK,
          "create a new database identity");
    check(gx_db_file_write_at(created, 0, patch.data(), 128) == GX_DB_FILE_OK,
          "create path accepts exact initial writes");
    check(gx_db_file_close(created) == GX_DB_FILE_OK, "close newly created file");
    GxDbFileHandle reopen = GX_DB_FILE_INVALID_HANDLE;
    check(gx_db_file_open("/db/beta.gxdb", GX_DB_OPEN_READ_ONLY, 0, &reopen) == GX_DB_FILE_OK,
          "close/reopen preserves provider file");
    std::vector<uint8_t> reopened(128);
    check(gx_db_file_read_at(reopen, 0, reopened.data(), reopened.size()) == GX_DB_FILE_OK &&
          std::equal(reopened.begin(), reopened.end(), patch.begin()),
          "reopened file bytes match independent expected sequence");
    check(gx_db_file_write_at(reopen, 0, patch.data(), 1) == GX_DB_FILE_READ_ONLY,
          "read-only file handle rejects writes");
    g_closeResult = kernel::vfs::VFS_ERR_IO_TIMEOUT;
    check(gx_db_file_close(reopen) == GX_DB_FILE_IO_TIMEOUT,
          "close-time filesystem failure propagates");
    check(gx_db_file_get_size(reopen, &size) == GX_DB_FILE_STALE_HANDLE,
          "handle invalidates even when close reports an error");
    g_closeResult = kernel::vfs::VFS_OK;

    GxDbFileHandle empty = GX_DB_FILE_INVALID_HANDLE;
    check(gx_db_file_open("/db/empty.gxdb", GX_DB_OPEN_CREATE, 0, &empty) == GX_DB_FILE_OK &&
          gx_db_file_close(empty) == GX_DB_FILE_OK, "create an empty database file");
    GxDbFileHandle duplicateEmpty = GX_DB_FILE_INVALID_HANDLE;
    uint64_t emptySize = UINT64_MAX;
    const bool emptyExisting = gx_db_file_open("/db/empty.gxdb", GX_DB_OPEN_CREATE, 0,
        &duplicateEmpty) == GX_DB_FILE_OK &&
        gx_db_file_get_size(duplicateEmpty, &emptySize) == GX_DB_FILE_OK && emptySize == 0 &&
        gx_db_file_close(duplicateEmpty) == GX_DB_FILE_OK;
    check(emptyExisting, "zero-byte create target preserves SQL provider create semantics");
    check(gx_db_file_remove("/db/empty.gxdb") == GX_DB_FILE_OK,
          "remove empty create-semantics fixture");

    GxDbFileHandle wal = GX_DB_FILE_INVALID_HANDLE;
    check(gx_db_file_open("/db/alpha.gxwal", GX_DB_OPEN_CREATE, 0, &wal) == GX_DB_FILE_OK,
          "create WAL sidecar for first database");
    std::vector<uint8_t> walHeader(64);
    for (size_t i = 0; i < walHeader.size(); ++i) walHeader[i] = static_cast<uint8_t>(0xC0u + i);
    check(gx_db_file_write_at(wal, 0, walHeader.data(), walHeader.size()) == GX_DB_FILE_OK,
          "write deterministic WAL header and record bytes");
    check(gx_db_file_flush(wal) == GX_DB_FILE_OK, "flush WAL sidecar");
    check(gx_db_file_truncate_to(wal, 64) == GX_DB_FILE_OK,
          "checkpoint shrinks WAL to its 64-byte header");
    check(gx_db_file_close(wal) == GX_DB_FILE_OK, "close WAL sidecar");
    GxDbFileHandle secondWal = GX_DB_FILE_INVALID_HANDLE;
    check(gx_db_file_open("/db/beta.gxwal", GX_DB_OPEN_CREATE, 0, &secondWal) ==
          GX_DB_FILE_OK, "create WAL sidecar for second database");
    std::vector<uint8_t> secondWalBytes(64, 0x5Cu);
    check(gx_db_file_write_at(secondWal, 0, secondWalBytes.data(), secondWalBytes.size()) ==
          GX_DB_FILE_OK && gx_db_file_close(secondWal) == GX_DB_FILE_OK,
          "second WAL is independently writable");
    check(g_files.find("/db/ALPHA.GWL") != g_files.end() &&
          g_files.find("/db/BETA.GWL") != g_files.end() &&
          g_files["/db/ALPHA.GWL"] != g_files["/db/BETA.GWL"],
          "two database identities do not share or overwrite their WALs");

    check(gx_db_file_remove("/db/beta.gxwal") == GX_DB_FILE_OK,
          "removing absent WAL succeeds without touching another identity");
    check(gx_db_file_remove("/db/alpha.gxwal") == GX_DB_FILE_OK,
          "remove WAL sidecar through provider");
    check(gx_db_file_exists("/db/alpha.gxwal", &exists) == GX_DB_FILE_OK && !exists,
          "removed sidecar is absent");
    g_removeResult = kernel::vfs::VFS_ERR_IO;
    check(gx_db_file_remove("/db/alpha.gxdb") == GX_DB_FILE_IO,
          "cleanup failure propagates from native filesystem");
    g_removeResult = kernel::vfs::VFS_OK;
}

} // namespace

int main() {
    testPathMapping();
    testFileContract();
    gx_db_file_bind_ops(nullptr);
    std::printf("native database file bridge: %u checks, %u failures\n",
                g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
