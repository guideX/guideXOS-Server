#include "include/kernel/qemu_native_database_qualification.h"

#if defined(GXOS_QEMU_NATIVE_DATABASE_QUALIFICATION)

#include "include/kernel/native_database_file.h"
#include "include/kernel/serial_debug.h"
#include "include/kernel/vfs.h"

namespace kernel {
namespace qemu_native_database_qualification {
namespace {

static const char kVolume[] = "/ndb1r2";
static const char kAlpha[] = "/ndb1r2/alpha.gxdb";
static const char kBeta[] = "/ndb1r2/beta.gxdb";
static const char kAlphaWal[] = "/ndb1r2/alpha.gxwal";
static const char kBetaWal[] = "/ndb1r2/beta.gxwal";
static const char kTemporaryWal[] = "/ndb1r2/temp.gxwal";
static const uint32_t kAlphaSize = 8192;
static const uint32_t kBetaSize = 4096;
static const uint32_t kWalSize = 64 + 640;
static const uint32_t kBetaWalSize = 128;
static uint8_t s_alpha[kAlphaSize];
static uint8_t s_beta[kBetaSize];
static uint8_t s_wal[kWalSize];
static uint8_t s_betaWal[kBetaWalSize];

static void fill(uint8_t* bytes, uint32_t size, uint8_t seed) {
    for (uint32_t i = 0; i < size; ++i)
        bytes[i] = static_cast<uint8_t>(i * 37u + seed * 19u + (i >> 3));
}

static bool equal(const char* a, const char* b) {
    if (!a || !b) return false;
    while (*a && *b && *a == *b) { ++a; ++b; }
    return *a == *b;
}

static bool bytesEqual(const uint8_t* a, const uint8_t* b, uint32_t size) {
    for (uint32_t i = 0; i < size; ++i) if (a[i] != b[i]) return false;
    return true;
}

static void report(const char* name, bool passed) {
    serial::puts(passed ? "[NDB1R3-BRIDGE] PASS: " : "[NDB1R3-BRIDGE] FAIL: ");
    serial::puts(name);
    serial::putc('\n');
}

static bool writeWhole(const char* path, const uint8_t* bytes, uint32_t size) {
    GxDbFileHandle handle = GX_DB_FILE_INVALID_HANDLE;
    if (gx_db_file_open(path, GX_DB_OPEN_CREATE, 0, &handle) != GX_DB_FILE_OK) return false;
    bool ok = gx_db_file_write_at(handle, 0, bytes, size) == GX_DB_FILE_OK;
    ok = ok && gx_db_file_flush(handle) == GX_DB_FILE_OK;
    const GxDbFileStatus closed = gx_db_file_close(handle);
    return ok && closed == GX_DB_FILE_OK;
}

static bool verify(const char* path, const uint8_t* expected, uint32_t size) {
    GxDbFileHandle handle = GX_DB_FILE_INVALID_HANDLE;
    if (gx_db_file_open(path, GX_DB_OPEN_READ_ONLY, 0, &handle) != GX_DB_FILE_OK) return false;
    uint64_t actualSize = UINT64_MAX;
    uint8_t actual[kAlphaSize];
    const bool sizeOk = gx_db_file_get_size(handle, &actualSize) == GX_DB_FILE_OK &&
        actualSize == size;
    const bool bytesOk = gx_db_file_read_at(handle, 0, actual, size) == GX_DB_FILE_OK &&
        bytesEqual(actual, expected, size);
    const bool closeOk = gx_db_file_close(handle) == GX_DB_FILE_OK;
    return sizeOk && bytesOk && closeOk;
}

static bool testNameIdentity() {
    char alphaPath[vfs::VFS_MAX_PATH];
    char betaPath[vfs::VFS_MAX_PATH];
    const bool mapped = gx_db_file_map_path(kAlpha, alphaPath, sizeof(alphaPath)) == GX_DB_FILE_OK &&
        gx_db_file_map_path(kBeta, betaPath, sizeof(betaPath)) == GX_DB_FILE_OK &&
        equal(alphaPath, "/ndb1r2/ALPHA.GDB") && equal(betaPath, "/ndb1r2/BETA.GDB") &&
        !equal(alphaPath, betaPath);
    report("mounted VFS path spelling is retained and database names map distinctly", mapped);
    return mapped;
}

static bool createAndCheck() {
    fill(s_alpha, sizeof(s_alpha), 0x31);
    fill(s_beta, sizeof(s_beta), 0x92);
    fill(s_wal, sizeof(s_wal), 0xA7);
    fill(s_betaWal, sizeof(s_betaWal), 0x4D);

    bool ok = writeWhole(kAlpha, s_alpha, sizeof(s_alpha));
    report("native bridge creates and writes deterministic database pages", ok);
    if (!ok) return false;
    ok = writeWhole(kBeta, s_beta, sizeof(s_beta));
    report("second database identity remains a separate file", ok);
    if (!ok) return false;

    GxDbFileHandle wal = GX_DB_FILE_INVALID_HANDLE;
    ok = gx_db_file_open(kAlphaWal, GX_DB_OPEN_CREATE, 0, &wal) == GX_DB_FILE_OK;
    ok = ok && gx_db_file_write_at(wal, 0, s_wal, sizeof(s_wal)) == GX_DB_FILE_OK;
    ok = ok && gx_db_file_flush(wal) == GX_DB_FILE_OK;
    ok = ok && gx_db_file_truncate_to(wal, 64) == GX_DB_FILE_OK;
    ok = ok && gx_db_file_flush(wal) == GX_DB_FILE_OK;
    ok = gx_db_file_close(wal) == GX_DB_FILE_OK && ok;
    report("native bridge writes, flushes and shrinks WAL to its exact 64-byte header", ok);
    if (!ok) return false;

    ok = writeWhole(kBetaWal, s_betaWal, sizeof(s_betaWal));
    report("second database receives an independent WAL sidecar", ok);
    if (!ok) return false;

    GxDbFileHandle temporary = GX_DB_FILE_INVALID_HANDLE;
    ok = gx_db_file_open(kTemporaryWal, GX_DB_OPEN_CREATE, 0, &temporary) == GX_DB_FILE_OK;
    ok = ok && gx_db_file_write_at(temporary, 0, s_wal, 64) == GX_DB_FILE_OK;
    ok = ok && gx_db_file_flush(temporary) == GX_DB_FILE_OK;
    ok = gx_db_file_close(temporary) == GX_DB_FILE_OK && ok;
    ok = ok && gx_db_file_remove(kTemporaryWal) == GX_DB_FILE_OK;
    uint8_t temporaryExists = 1;
    ok = ok && gx_db_file_exists(kTemporaryWal, &temporaryExists) == GX_DB_FILE_OK &&
        temporaryExists == 0;
    report("native bridge removes a temporary WAL and reports it absent", ok);
    if (!ok) return false;

    const bool bytes = verify(kAlpha, s_alpha, sizeof(s_alpha)) &&
        verify(kBeta, s_beta, sizeof(s_beta)) && verify(kAlphaWal, s_wal, 64) &&
        verify(kBetaWal, s_betaWal, sizeof(s_betaWal));
    report("same-process close/reopen returns exact bytes and lengths", bytes);
    return bytes;
}

static bool readback() {
    fill(s_alpha, sizeof(s_alpha), 0x31);
    fill(s_beta, sizeof(s_beta), 0x92);
    fill(s_wal, sizeof(s_wal), 0xA7);
    fill(s_betaWal, sizeof(s_betaWal), 0x4D);
    const bool data = verify(kAlpha, s_alpha, sizeof(s_alpha)) &&
        verify(kBeta, s_beta, sizeof(s_beta));
    const bool wal = verify(kAlphaWal, s_wal, 64) &&
        verify(kBetaWal, s_betaWal, sizeof(s_betaWal));
    uint8_t alpha = 0, beta = 0, alphaWal = 0, betaWal = 0, temporary = 1;
    const bool lookup = gx_db_file_exists(kAlpha, &alpha) == GX_DB_FILE_OK && alpha &&
        gx_db_file_exists(kBeta, &beta) == GX_DB_FILE_OK && beta &&
        gx_db_file_exists(kAlphaWal, &alphaWal) == GX_DB_FILE_OK && alphaWal &&
        gx_db_file_exists(kBetaWal, &betaWal) == GX_DB_FILE_OK && betaWal &&
        gx_db_file_exists(kTemporaryWal, &temporary) == GX_DB_FILE_OK && !temporary;
    report("fresh QEMU process verifies exact database and WAL bytes", data && wal);
    report("fresh QEMU process verifies both sidecar identities and cleanup", lookup);
    return data && wal && lookup;
}

} // namespace

bool run() {
    serial::puts("[NDB1R3-BRIDGE] START production VFS database file bridge\n");
    if (!vfs::get_mount(kVolume)) {
        serial::puts("[NDB1R3-BRIDGE] FAIL: NDB1R2 disposable FAT32 mount is unavailable\n");
        return false;
    }
    if (!testNameIdentity()) return false;

    uint8_t alpha = 0, beta = 0, alphaWal = 0, betaWal = 0;
    bool found = gx_db_file_exists(kAlpha, &alpha) == GX_DB_FILE_OK &&
        gx_db_file_exists(kBeta, &beta) == GX_DB_FILE_OK &&
        gx_db_file_exists(kAlphaWal, &alphaWal) == GX_DB_FILE_OK &&
        gx_db_file_exists(kBetaWal, &betaWal) == GX_DB_FILE_OK;
    if (!found || alpha != beta || alpha != alphaWal || alpha != betaWal) return false;
    const bool passed = alpha ? readback() : createAndCheck();
    if (!passed) return false;
    serial::puts(alpha ? "[NDB1R3-BRIDGE] CROSS_PROCESS_READBACK PASS\n"
                       : "[NDB1R3-BRIDGE] WRITE_PHASE PASS\n");
    return true;
}

} // namespace qemu_native_database_qualification
} // namespace kernel

#endif
