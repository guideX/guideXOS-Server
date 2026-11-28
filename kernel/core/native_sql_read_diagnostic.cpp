#if defined(GX_NATIVE_SQL_READ_DIAGNOSTIC) && GX_NATIVE_SQL_READ_DIAGNOSTIC

#include "include/kernel/native_sql_read_diagnostic.h"

#include "include/kernel/block_device.h"
#include "include/kernel/serial_debug.h"
#include "include/kernel/vfs.h"

#include "database_format.h"
#include "gx_index_integer_key.h"
#include "gx_native_sql_read_service.h"

namespace kernel {
namespace native_sql_read_diagnostic {
namespace {

const char kFixtureId[] = "NDB1R4P11R3AH_MULTILEVEL";
const uint32_t kPageSize = 512;
const size_t kPointResultCount = 73;
const size_t kRangeResultCount = 32;
const char kManifestPath[] = "/system/Apps/R3AI/FIXTURE.MAN";
const uint64_t kManifestExpectedBytes = 12937;
const uint32_t kManifestReadChunkBytes = 1024;

struct ExpectedLocator {
    uint16_t pageId;
    uint8_t slot;
};

const ExpectedLocator kExpectedPoint[kPointResultCount] = {
    {3,1},{3,2},{3,3},{3,4},{3,5},{3,6},{3,7},{10,0},{10,1},{10,2},
    {10,3},{10,4},{10,6},{10,7},{22,0},{22,1},{22,2},{22,3},{22,4},{22,5},
    {22,6},{22,7},{71,0},{71,1},{71,3},{71,4},{71,5},{71,6},{71,7},{130,0},
    {130,1},{130,2},{130,3},{130,4},{130,5},{130,6},{190,0},{190,1},{190,2},
    {190,3},{190,4},{190,5},{190,6},{190,7},{250,0},{250,1},{250,2},{250,3},
    {250,5},{250,6},{250,7},{308,0},{308,1},{308,2},{308,3},{308,4},{308,5},
    {308,6},{360,0},{360,1},{360,3},{360,4},{360,5},{360,6},{360,7},{422,0},
    {422,1},{422,2},{422,3},{422,4},{422,5},{422,6},{479,0}
};

const ExpectedLocator kExpectedRange[kRangeResultCount] = {
    {3,1},{10,1},{10,5},{22,1},{22,5},{71,5},{130,1},{130,5},
    {190,1},{250,1},{250,5},{308,1},{308,5},{360,6},{422,2},{422,6},
    {3,2},{3,6},{10,6},{22,2},{22,6},{71,2},{130,2},{130,6},
    {190,2},{190,6},{250,6},{308,2},{308,6},{360,3},{422,3},{422,7}
};

void printFailure(const char* code) {
    serial::puts("SQL_NATIVE_R3AQ FAIL code=");
    serial::puts(code);
    serial::puts("\nSQL_NATIVE_R3AQ END FAIL\n");
}

void printDecimal(uint64_t value) {
    char digits[20];
    size_t count = 0;
    do {
        digits[count++] = static_cast<char>('0' + (value % 10));
        value /= 10;
    } while (value != 0 && count < sizeof(digits));
    while (count != 0) serial::putc(digits[--count]);
}

enum ManifestReadCode {
    ManifestReadOk = 0,
    ManifestReadVfsUnavailable,
    ManifestReadInvalidOperations,
    ManifestReadStatFailed,
    ManifestReadNotRegular,
    ManifestReadUnexpectedLength,
    ManifestReadOpenFailed,
    ManifestReadInvalidHandle,
    ManifestReadHandleContract,
    ManifestReadFileSizeFailed,
    ManifestReadPrematureEof,
    ManifestReadIoError,
    ManifestReadUnexpectedExtraByte,
    ManifestReadEofError,
    ManifestReadCloseFailed
};

struct ManifestReadOps {
    void* context;
    vfs::Status (*stat)(void*, const char*, vfs::FileInfo*);
    uint8_t (*open)(void*, const char*, uint16_t);
    const vfs::FileHandle* (*getHandle)(void*, uint8_t);
    int64_t (*fileSize)(void*, uint8_t);
    int32_t (*read)(void*, uint8_t, void*, uint32_t);
    vfs::Status (*close)(void*, uint8_t);
    uint8_t (*mountIndexForPath)(void*, const char*);
};

struct ManifestReadReport {
    ManifestReadCode code;
    uint64_t bytesRead;
    uint32_t readCalls;
    bool eofObserved;
    bool closeAttempted;
    bool closeSucceeded;
};

const char* manifestReadCodeName(ManifestReadCode code) {
    switch (code) {
        case ManifestReadOk: return "NONE";
        case ManifestReadVfsUnavailable: return "VFS_UNAVAILABLE";
        case ManifestReadInvalidOperations: return "INVALID_OPERATIONS";
        case ManifestReadStatFailed: return "STAT_FAILED";
        case ManifestReadNotRegular: return "NOT_REGULAR";
        case ManifestReadUnexpectedLength: return "UNEXPECTED_LENGTH";
        case ManifestReadOpenFailed: return "OPEN_FAILED";
        case ManifestReadInvalidHandle: return "INVALID_HANDLE";
        case ManifestReadHandleContract: return "HANDLE_CONTRACT";
        case ManifestReadFileSizeFailed: return "FILE_SIZE_FAILED";
        case ManifestReadPrematureEof: return "PREMATURE_EOF";
        case ManifestReadIoError: return "READ_IO_ERROR";
        case ManifestReadUnexpectedExtraByte: return "UNEXPECTED_EXTRA_BYTE";
        case ManifestReadEofError: return "EOF_READ_ERROR";
        case ManifestReadCloseFailed: return "CLOSE_FAILED";
        default: return "UNKNOWN";
    }
}

vfs::Status productionStat(void*, const char* path, vfs::FileInfo* info) {
    return vfs::stat(path, info);
}

uint8_t productionOpen(void*, const char* path, uint16_t flags) {
    return vfs::open(path, flags);
}

const vfs::FileHandle* productionGetHandle(void*, uint8_t handle) {
    return vfs::get_handle(handle);
}

int64_t productionFileSize(void*, uint8_t handle) {
    return vfs::file_size(handle);
}

int32_t productionRead(void*, uint8_t handle, void* buffer, uint32_t size) {
    return vfs::read(handle, buffer, size);
}

vfs::Status productionClose(void*, uint8_t handle) {
    return vfs::close(handle);
}

uint8_t productionMountIndexForPath(void*, const char* path) {
    return vfs::mount_index_for_path(path);
}

const ManifestReadOps kProductionManifestOps = {
    nullptr,
    productionStat,
    productionOpen,
    productionGetHandle,
    productionFileSize,
    productionRead,
    productionClose,
    productionMountIndexForPath
};

bool productionReadOnlyMountReady() {
    const vfs::MountPoint* mount = vfs::get_mount("/system");
    if (!mount || !mount->active || mount->fsType != vfs::FS_TYPE_FAT32 ||
        !mount->readOnly) return false;
    const block::BlockDevice* device = block::get_device(mount->blockDevIndex);
    return device && device->readFn && !device->writeFn;
}

void printManifestBegin() {
    serial::puts("SQL_NATIVE_R3AW MANIFEST READ BEGIN path=");
    serial::puts(kManifestPath);
    serial::puts(" expected_bytes=");
    printDecimal(kManifestExpectedBytes);
    serial::puts(" access_only=1\n");
}

void printManifestReadResult(const ManifestReadReport& report) {
    serial::puts("SQL_NATIVE_R3AW MANIFEST BYTES_READ path=");
    serial::puts(kManifestPath);
    serial::puts(" bytes=");
    printDecimal(report.bytesRead);
    serial::puts(" read_calls=");
    printDecimal(report.readCalls);
    serial::puts(" eof=");
    serial::putc(report.eofObserved ? '1' : '0');
    serial::puts(" close=");
    serial::puts(report.closeAttempted ? (report.closeSucceeded ? "PASS" : "FAIL") : "NOT_OPENED");
    serial::putc('\n');
    if (report.code == ManifestReadOk) {
        serial::puts("SQL_NATIVE_R3AW MANIFEST READ PASS path=");
        serial::puts(kManifestPath);
        serial::puts(" bytes=");
        printDecimal(report.bytesRead);
        serial::puts(" eof=1 close=PASS content_identity=NOT_CHECKED\n");
    } else {
        serial::puts("SQL_NATIVE_R3AW MANIFEST READ FAIL code=");
        serial::puts(manifestReadCodeName(report.code));
        serial::puts(" bytes=");
        printDecimal(report.bytesRead);
        serial::putc('\n');
    }
}

ManifestReadReport readManifest(const ManifestReadOps& ops) {
    ManifestReadReport report = {ManifestReadInvalidOperations, 0, 0, false, false, false};
    printManifestBegin();

    if (!productionReadOnlyMountReady()) {
        report.code = ManifestReadVfsUnavailable;
        printManifestReadResult(report);
        return report;
    }
    if (!ops.stat || !ops.open || !ops.getHandle || !ops.fileSize || !ops.read ||
        !ops.close || !ops.mountIndexForPath) {
        printManifestReadResult(report);
        return report;
    }

    vfs::FileInfo info = {};
    const vfs::Status statStatus = ops.stat(ops.context, kManifestPath, &info);
    if (statStatus != vfs::VFS_OK) {
        report.code = ManifestReadStatFailed;
        printManifestReadResult(report);
        return report;
    }
    if (info.type != vfs::FILE_TYPE_REGULAR) {
        report.code = ManifestReadNotRegular;
        printManifestReadResult(report);
        return report;
    }
    if (info.size != kManifestExpectedBytes) {
        report.code = ManifestReadUnexpectedLength;
        printManifestReadResult(report);
        return report;
    }

    const uint8_t handle = ops.open(ops.context, kManifestPath, vfs::OPEN_READ);
    if (handle == 0xFF) {
        report.code = ManifestReadOpenFailed;
        printManifestReadResult(report);
        return report;
    }
    serial::puts("SQL_NATIVE_R3AW MANIFEST OPENED path=");
    serial::puts(kManifestPath);
    serial::puts(" bytes=");
    printDecimal(info.size);
    serial::puts(" mode=READ\n");

    report.code = ManifestReadOk;
    const vfs::FileHandle* opened = ops.getHandle(ops.context, handle);
    if (!opened) {
        report.code = ManifestReadInvalidHandle;
    } else {
        const uint8_t expectedMount = ops.mountIndexForPath(ops.context, kManifestPath);
        if (expectedMount == 0xFF || !opened->open ||
            opened->type != vfs::FILE_TYPE_REGULAR ||
            !(opened->flags & vfs::OPEN_READ) || (opened->flags & vfs::OPEN_WRITE) ||
            opened->mountIndex != expectedMount || opened->size != kManifestExpectedBytes) {
            report.code = ManifestReadHandleContract;
        }
    }
    if (report.code == ManifestReadOk) {
        const int64_t openedSize = ops.fileSize(ops.context, handle);
        if (openedSize < 0) report.code = ManifestReadFileSizeFailed;
        else if (static_cast<uint64_t>(openedSize) != kManifestExpectedBytes) {
            report.code = ManifestReadUnexpectedLength;
        }
    }

    uint8_t buffer[kManifestReadChunkBytes];
    while (report.code == ManifestReadOk && report.bytesRead < kManifestExpectedBytes) {
        const uint64_t remaining = kManifestExpectedBytes - report.bytesRead;
        const uint32_t request = static_cast<uint32_t>(
            remaining < sizeof(buffer) ? remaining : sizeof(buffer));
        const int32_t count = ops.read(ops.context, handle, buffer, request);
        ++report.readCalls;
        if (count < 0) {
            report.code = ManifestReadIoError;
        } else if (static_cast<uint32_t>(count) > request) {
            report.code = ManifestReadIoError;
        } else if (count == 0) {
            report.code = ManifestReadPrematureEof;
        } else {
            report.bytesRead += static_cast<uint32_t>(count);
        }
    }
    if (report.code == ManifestReadOk) {
        uint8_t eofProbe = 0;
        const int32_t count = ops.read(ops.context, handle, &eofProbe, 1);
        ++report.readCalls;
        if (count < 0) report.code = ManifestReadEofError;
        else if (count != 0) report.code = ManifestReadUnexpectedExtraByte;
        else report.eofObserved = true;
    }

    report.closeAttempted = true;
    report.closeSucceeded = ops.close(ops.context, handle) == vfs::VFS_OK;
    if (report.code == ManifestReadOk && !report.closeSucceeded) {
        report.code = ManifestReadCloseFailed;
    }
    printManifestReadResult(report);
    return report;
}

bool matches(const GxNativeSqlReadResponse& response,
             const GxNativeSqlReadLocator* actual,
             const ExpectedLocator* expected, size_t expectedCount) {
    if (response.status != GX_NATIVE_SQL_READ_OK ||
        response.rowCount != expectedCount || !actual || !expected) return false;
    for (size_t i = 0; i < expectedCount; ++i) {
        if (actual[i].pageId != expected[i].pageId ||
            actual[i].slot != expected[i].slot) return false;
    }
    return true;
}

void fillPointRequest(GxNativeSqlReadRequest* request,
                      const uint8_t* key, size_t keyLength) {
    *request = GxNativeSqlReadRequest();
    request->operation = GX_NATIVE_SQL_READ_POINT;
    request->fixture = GX_NATIVE_SQL_FIXTURE_R3AH_MULTILEVEL;
    request->index = GX_NATIVE_SQL_INDEX_ROWS_K64;
    request->consistency = GX_NATIVE_SQL_CONSISTENCY_IMMUTABLE_IMAGE;
    request->catalogRootPageId = 1;
    request->rootPageId = 234;
    request->ownerIndexId = 1;
    request->keyBytes = key;
    request->keyLength = keyLength;
}

bool runPoint(uint8_t* key, size_t* keyLength,
              GxNativeSqlReadLocator* rows,
              GxNativeSqlReadResponse* response) {
    if (gxEncodeIndexInt64Key(false, 7, key, 9, keyLength) != GxStatus::Ok) {
        return false;
    }
    GxNativeSqlReadRequest request;
    fillPointRequest(&request, key, *keyLength);
    return gxNativeSqlExecuteRead(&request, rows, GX_NATIVE_SQL_READ_MAX_RESULTS,
        response) == GX_NATIVE_SQL_READ_OK;
}

bool runRange(GxNativeSqlReadLocator* rows,
              GxNativeSqlReadResponse* response) {
    uint8_t lower[5] = {};
    uint8_t upper[5] = {};
    size_t lowerLength = 0;
    size_t upperLength = 0;
    if (gxEncodeIndexInt32Key(false, 1, lower, sizeof(lower), &lowerLength) !=
            GxStatus::Ok ||
        gxEncodeIndexInt32Key(false, 2, upper, sizeof(upper), &upperLength) !=
            GxStatus::Ok) return false;
    GxNativeSqlReadRequest request = {};
    request.operation = GX_NATIVE_SQL_READ_RANGE;
    request.fixture = GX_NATIVE_SQL_FIXTURE_R3AH_MULTILEVEL;
    request.index = GX_NATIVE_SQL_INDEX_ROWS_K32;
    request.consistency = GX_NATIVE_SQL_CONSISTENCY_IMMUTABLE_IMAGE;
    request.catalogRootPageId = 1;
    request.rootPageId = 148;
    request.ownerIndexId = 2;
    request.hasLower = 1;
    request.lowerKeyBytes = lower;
    request.lowerKeyLength = lowerLength;
    request.lowerInclusive = 1;
    request.hasUpper = 1;
    request.upperKeyBytes = upper;
    request.upperKeyLength = upperLength;
    request.upperInclusive = 1;
    return gxNativeSqlExecuteRead(&request, rows, GX_NATIVE_SQL_READ_MAX_RESULTS,
        response) == GX_NATIVE_SQL_READ_OK;
}

bool outputUnchanged(const GxNativeSqlReadLocator* actual,
                     const GxNativeSqlReadLocator* before, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        if (actual[i].pageId != before[i].pageId ||
            actual[i].slot != before[i].slot) return false;
    }
    return true;
}

} // namespace

static Result runAfterVfsMount(const ManifestReadOps& ops) {
    serial::puts("SQL_NATIVE_R3AQ BEGIN fixture=");
    serial::puts(kFixtureId);
    serial::puts("\n");

    const ManifestReadReport manifest = readManifest(ops);
    if (manifest.code != ManifestReadOk) {
        serial::puts("SQL_NATIVE_R3AQ BLOCKED reason=MANIFEST_READ_FAILURE\n");
        printFailure("MANIFEST_READ");
        return Failed;
    }

    uint8_t pointKey[9] = {};
    size_t pointKeyLength = 0;
    GxNativeSqlReadLocator rows[GX_NATIVE_SQL_READ_MAX_RESULTS] = {};
    GxNativeSqlReadResponse response = {};
    if (!runPoint(pointKey, &pointKeyLength, rows, &response) ||
        !matches(response, rows, kExpectedPoint, kPointResultCount) ||
        response.cleanupError != GX_NATIVE_SQL_IMAGE_OK) {
        printFailure("POINT_RESULTS_OR_CLEANUP");
        return Failed;
    }
    serial::puts("SQL_NATIVE_R3AQ FIXTURE_OK length=250880 page_size=512 page_count=490 catalog_root=1\n");
    serial::puts("SQL_NATIVE_R3AQ REQUEST=POINT status=OK count=73 ordered=PASS cleanup=PASS\n");

    if (!runRange(rows, &response) ||
        !matches(response, rows, kExpectedRange, kRangeResultCount) ||
        response.cleanupError != GX_NATIVE_SQL_IMAGE_OK) {
        printFailure("RANGE_RESULTS_OR_CLEANUP");
        return Failed;
    }
    serial::puts("SQL_NATIVE_R3AQ REQUEST=RANGE status=OK count=32 ordered=PASS cleanup=PASS\n");

    GxNativeSqlReadLocator before[GX_NATIVE_SQL_READ_MAX_RESULTS];
    for (size_t i = 0; i < GX_NATIVE_SQL_READ_MAX_RESULTS; ++i) {
        rows[i].pageId = 0xD00D0000ull + i;
        rows[i].slot = static_cast<uint32_t>(0xA500u + i);
        before[i] = rows[i];
    }
    uint8_t invalidKey[9] = {0x02,0,0,0,0,0,0,0,0};
    GxNativeSqlReadRequest invalidRequest;
    fillPointRequest(&invalidRequest, invalidKey, sizeof(invalidKey));
    const GxNativeSqlReadStatus invalidStatus = gxNativeSqlExecuteRead(
        &invalidRequest, rows, GX_NATIVE_SQL_READ_MAX_RESULTS, &response);
    if (invalidStatus != GX_NATIVE_SQL_READ_INVALID_REQUEST ||
        response.rowCount != 0 || response.imageError != GX_NATIVE_SQL_IMAGE_OK ||
        response.cleanupError != GX_NATIVE_SQL_IMAGE_OK ||
        !outputUnchanged(rows, before, GX_NATIVE_SQL_READ_MAX_RESULTS)) {
        printFailure("INVALID_REQUEST_OR_PARTIAL_PUBLICATION");
        return Failed;
    }
    serial::puts("SQL_NATIVE_R3AQ REQUEST=INVALID_KEY status=INVALID_REQUEST count=0 output=UNCHANGED cleanup=PASS\n");

    if (!runPoint(pointKey, &pointKeyLength, rows, &response) ||
        !matches(response, rows, kExpectedPoint, kPointResultCount) ||
        response.cleanupError != GX_NATIVE_SQL_IMAGE_OK) {
        printFailure("POST_NEGATIVE_POINT_OR_CLEANUP");
        return Failed;
    }
    serial::puts("SQL_NATIVE_R3AQ REQUEST=AFTER_NEGATIVE status=OK count=73 ordered=PASS cleanup=PASS\n");
    serial::puts("SQL_NATIVE_R3AQ END PASS\n");
    return Passed;
}

Result run_after_vfs_mount() {
    return runAfterVfsMount(kProductionManifestOps);
}

#if defined(GXOS_HOSTED_TEST)
namespace {

ManifestReadOps toInternalOps(const ManifestReadTestOps& ops) {
    ManifestReadOps result = {
        ops.context,
        ops.stat,
        ops.open,
        ops.getHandle,
        ops.fileSize,
        ops.read,
        ops.close,
        ops.mountIndexForPath
    };
    return result;
}

ManifestReadTestReport toTestReport(const ManifestReadReport& report) {
    ManifestReadTestReport result = {
        static_cast<ManifestReadTestCode>(report.code),
        report.bytesRead,
        report.readCalls,
        report.eofObserved,
        report.closeAttempted,
        report.closeSucceeded
    };
    return result;
}

} // namespace

ManifestReadTestReport read_manifest_for_test(const ManifestReadTestOps& ops) {
    return toTestReport(readManifest(toInternalOps(ops)));
}

ManifestReadTestReport read_manifest_from_vfs_for_test() {
    return toTestReport(readManifest(kProductionManifestOps));
}

Result run_after_vfs_mount_for_test(const ManifestReadTestOps& ops) {
    return runAfterVfsMount(toInternalOps(ops));
}
#endif

} // namespace native_sql_read_diagnostic
} // namespace kernel

#endif // GX_NATIVE_SQL_READ_DIAGNOSTIC
