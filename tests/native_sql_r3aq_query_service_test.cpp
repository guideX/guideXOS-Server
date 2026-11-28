#include "kernel/block_device.h"
#include "kernel/fs_fat.h"
#include "kernel/native_database_file.h"
#include "kernel/native_sql_read_diagnostic.h"
#include "kernel/ramdisk.h"
#include "kernel/vfs.h"

#include "database_format.h"
#include "database_checksum.h"
#include "gx_index_boolean_key.h"
#include "gx_index_integer_key.h"
#include "gx_index_text_key.h"
#include "gx_native_sql_read_service.h"
#include "../sdk/samples/sqlread/edge32_oracle.h"
#include "../sdk/samples/sqlread/edge64_oracle.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace {

int checks = 0;
int failures = 0;
bool failNextRead = false;
bool failNextClose = false;
bool failReadAtOffset = false;
uint64_t readFailureOffset = 0;
uint8_t candidateDevice = 0xFF;

struct ExpectedLocator { uint16_t pageId; uint8_t slot; };
const ExpectedLocator kPointExpected[73] = {
    {3,1},{3,2},{3,3},{3,4},{3,5},{3,6},{3,7},{10,0},{10,1},{10,2},
    {10,3},{10,4},{10,6},{10,7},{22,0},{22,1},{22,2},{22,3},{22,4},{22,5},
    {22,6},{22,7},{71,0},{71,1},{71,3},{71,4},{71,5},{71,6},{71,7},{130,0},
    {130,1},{130,2},{130,3},{130,4},{130,5},{130,6},{190,0},{190,1},{190,2},
    {190,3},{190,4},{190,5},{190,6},{190,7},{250,0},{250,1},{250,2},{250,3},
    {250,5},{250,6},{250,7},{308,0},{308,1},{308,2},{308,3},{308,4},{308,5},
    {308,6},{360,0},{360,1},{360,3},{360,4},{360,5},{360,6},{360,7},{422,0},
    {422,1},{422,2},{422,3},{422,4},{422,5},{422,6},{479,0}
};
const ExpectedLocator kRangeExpected[32] = {
    {3,1},{10,1},{10,5},{22,1},{22,5},{71,5},{130,1},{130,5},
    {190,1},{250,1},{250,5},{308,1},{308,5},{360,6},{422,2},{422,6},
    {3,2},{3,6},{10,6},{22,2},{22,6},{71,2},{130,2},{130,6},
    {190,2},{190,6},{250,6},{308,2},{308,6},{360,3},{422,3},{422,7}
};
const ExpectedLocator kBooleanTrueExpected[34] = {
    {3,2},{3,4},{3,6},{10,0},{10,2},{10,4},{22,0},{22,2},{22,4},{22,6},
    {71,0},{71,2},{71,6},{130,0},{130,2},{130,4},{130,6},{190,0},{190,4},
    {190,6},{250,0},{250,2},{250,4},{250,6},{308,2},{308,4},{308,6},
    {360,1},{360,3},{360,5},{422,1},{422,3},{422,5},{422,7}
};
const ExpectedLocator kBooleanFalseExpected[34] = {
    {3,1},{3,3},{3,5},{10,1},{10,3},{10,5},{10,7},{22,1},{22,3},{22,7},
    {71,1},{71,3},{71,5},{71,7},{130,1},{130,5},{130,7},{190,1},{190,3},
    {190,5},{190,7},{250,3},{250,5},{250,7},{308,1},{308,3},{308,5},
    {360,2},{360,4},{360,6},{422,0},{422,2},{422,4},{479,0}
};
const ExpectedLocator kBooleanNullExpected[12] = {
    {3,0},{3,7},{10,6},{22,5},{71,4},{130,3},
    {190,2},{250,1},{308,0},{360,0},{360,7},{422,6}
};
const ExpectedLocator kTextNullExpected[8] = {
    {3,0},{10,3},{22,6},{130,1},{190,4},{250,7},
    {360,3},{422,6}
};
const ExpectedLocator kTextEmbeddedNulExpected[24] = {
    {3,3},{3,6},{10,1},{10,4},{10,7},{22,2},
    {22,5},{71,0},{71,3},{71,6},{130,4},{130,7},
    {190,2},{190,5},{250,0},{250,3},{250,6},{308,1},
    {308,4},{360,0},{360,6},{422,1},{422,4},{422,7}
};
const ExpectedLocator kTextOtherExpected[48] = {
    {3,1},{3,2},{3,4},{3,5},{3,7},{10,0},
    {10,2},{10,5},{10,6},{22,0},{22,1},{22,3},
    {22,4},{22,7},{71,1},{71,2},{71,4},{71,5},
    {71,7},{130,0},{130,2},{130,3},{130,5},{130,6},
    {190,0},{190,1},{190,3},{190,6},{190,7},{250,1},
    {250,2},{250,4},{250,5},{308,0},{308,2},{308,3},
    {308,5},{308,6},{360,1},{360,2},{360,4},{360,5},
    {360,7},{422,0},{422,2},{422,3},{422,5},{479,0}
};

void check(bool value, const char* name) {
    ++checks;
    if (value) std::cout << "PASS: " << name << "\n";
    else { ++failures; std::cout << "FAIL: " << name << "\n"; }
}

bool readHostFile(const char* path, std::vector<uint8_t>& bytes) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return false;
    bytes.assign(std::istreambuf_iterator<char>(stream),
                 std::istreambuf_iterator<char>());
    return !stream.bad();
}

bool readVfsFile(const char* path, std::vector<uint8_t>& bytes) {
    const uint8_t handle = kernel::vfs::open(path, kernel::vfs::OPEN_READ);
    if (handle == 0xFF) return false;
    const int64_t size = kernel::vfs::file_size(handle);
    if (size < 0 || static_cast<uint64_t>(size) > 1024 * 1024) {
        (void)kernel::vfs::close(handle);
        return false;
    }
    bytes.resize(static_cast<size_t>(size));
    size_t offset = 0;
    while (offset < bytes.size()) {
        const uint32_t remaining = static_cast<uint32_t>(bytes.size() - offset);
        const int32_t count = kernel::vfs::read(handle, bytes.data() + offset,
                                                remaining);
        if (count <= 0 || static_cast<size_t>(count) > bytes.size() - offset) {
            (void)kernel::vfs::close(handle);
            return false;
        }
        offset += static_cast<size_t>(count);
    }
    return kernel::vfs::close(handle) == kernel::vfs::VFS_OK;
}

bool allVfsHandlesClosed() {
    for (uint8_t i = 0; i < kernel::vfs::VFS_MAX_OPEN_FILES; ++i) {
        const kernel::vfs::FileHandle* handle = kernel::vfs::get_handle(i);
        if (handle && handle->open) return false;
    }
    return true;
}

uint8_t attachReadonlyVolume(const std::vector<uint8_t>& image,
                             const char* name) {
    if (image.empty() || image.size() % kernel::ramdisk::RAMDISK_SECTOR_SIZE != 0)
        return 0xFF;
    return kernel::ramdisk::create_readonly_at(image.data(), image.size(), name);
}

bool mountSystem(const std::vector<uint8_t>& image, const char* name) {
    const uint8_t device = attachReadonlyVolume(image, name);
    if (device == 0xFF) return false;
    if (std::string(name) == "r3aqvalid") candidateDevice = device;
    return kernel::vfs::mount_type("/system", device,
        kernel::vfs::FS_TYPE_FAT32) != 0xFF;
}

bool mountWritableSystem(std::vector<uint8_t>& image) {
    if (image.empty() || image.size() % kernel::ramdisk::RAMDISK_SECTOR_SIZE != 0)
        return false;
    const uint8_t device = kernel::ramdisk::create_at(
        image.data(), image.size(), "r3aqwritable");
    return device != 0xFF && kernel::vfs::mount_type(
        "/system", device, kernel::vfs::FS_TYPE_FAT32) != 0xFF;
}

bool remountCandidate() {
    return candidateDevice != 0xFF && kernel::vfs::mount_type(
        "/system", candidateDevice, kernel::vfs::FS_TYPE_FAT32) != 0xFF;
}

void fillPoint(GxNativeSqlReadRequest& request, const uint8_t* key,
               size_t keyLength) {
    request = GxNativeSqlReadRequest();
    request.operation = GX_NATIVE_SQL_READ_POINT;
    request.fixture = GX_NATIVE_SQL_FIXTURE_R3AH_MULTILEVEL;
    request.index = GX_NATIVE_SQL_INDEX_ROWS_K64;
    request.consistency = GX_NATIVE_SQL_CONSISTENCY_IMMUTABLE_IMAGE;
    request.catalogRootPageId = 1;
    request.rootPageId = 234;
    request.ownerIndexId = 1;
    request.keyBytes = key;
    request.keyLength = keyLength;
}

bool makePoint(int64_t value, uint8_t key[9], size_t& keyLength,
               GxNativeSqlReadRequest& request) {
    if (gxEncodeIndexInt64Key(false, value, key, 9, &keyLength) != GxStatus::Ok)
        return false;
    fillPoint(request, key, keyLength);
    return true;
}

bool makeEdge32Point(int32_t value, uint8_t key[5], size_t& keyLength,
                     GxNativeSqlReadRequest& request) {
    if (gxEncodeIndexInt32Key(false, value, key, 5, &keyLength) != GxStatus::Ok)
        return false;
    request = GxNativeSqlReadRequest();
    request.operation = GX_NATIVE_SQL_READ_POINT;
    request.fixture = GX_NATIVE_SQL_FIXTURE_R3AH_MULTILEVEL;
    request.index = GX_NATIVE_SQL_INDEX_ROWS_EDGE32;
    request.consistency = GX_NATIVE_SQL_CONSISTENCY_IMMUTABLE_IMAGE;
    request.catalogRootPageId = 1;
    request.rootPageId = 259;
    request.ownerIndexId = 6;
    request.keyBytes = key;
    request.keyLength = keyLength;
    return true;
}

bool makeEdge64Point(int64_t value, uint8_t key[9], size_t& keyLength,
                     GxNativeSqlReadRequest& request) {
    if (gxEncodeIndexInt64Key(false, value, key, 9, &keyLength) != GxStatus::Ok)
        return false;
    request = GxNativeSqlReadRequest();
    request.operation = GX_NATIVE_SQL_READ_POINT;
    request.fixture = GX_NATIVE_SQL_FIXTURE_R3AH_MULTILEVEL;
    request.index = GX_NATIVE_SQL_INDEX_ROWS_EDGE64;
    request.consistency = GX_NATIVE_SQL_CONSISTENCY_IMMUTABLE_IMAGE;
    request.catalogRootPageId = 1;
    request.rootPageId = 218;
    request.ownerIndexId = 5;
    request.keyBytes = key;
    request.keyLength = keyLength;
    return true;
}

bool makeBooleanPoint(bool isNull, bool value, uint8_t key[2],
                      size_t& keyLength, GxNativeSqlReadRequest& request) {
    if (gxEncodeIndexBooleanKey(isNull, value, key, 2, &keyLength) !=
        GxStatus::Ok) return false;
    request = GxNativeSqlReadRequest();
    request.operation = GX_NATIVE_SQL_READ_POINT;
    request.fixture = GX_NATIVE_SQL_FIXTURE_R3AH_MULTILEVEL;
    request.index = GX_NATIVE_SQL_INDEX_ROWS_KBOOL;
    request.consistency = GX_NATIVE_SQL_CONSISTENCY_IMMUTABLE_IMAGE;
    request.catalogRootPageId = 1;
    request.rootPageId = 124;
    request.ownerIndexId = 3;
    request.keyBytes = key;
    request.keyLength = keyLength;
    return true;
}

bool makeTextPoint(bool isNull, const uint8_t* textBytes, size_t textLength,
                   uint8_t key[8], size_t& keyLength,
                   GxNativeSqlReadRequest& request) {
    if (isNull) {
        if (textLength != 0) return false;
        key[0] = 0;
        keyLength = 1;
    } else if (gxEncodeIndexTextKey(textBytes, textLength, key, 8,
                                    &keyLength) != GxStatus::Ok) {
        return false;
    }
    request = GxNativeSqlReadRequest();
    request.operation = GX_NATIVE_SQL_READ_POINT;
    request.fixture = GX_NATIVE_SQL_FIXTURE_R3AH_MULTILEVEL;
    request.index = GX_NATIVE_SQL_INDEX_ROWS_KTEXT;
    request.consistency = GX_NATIVE_SQL_CONSISTENCY_IMMUTABLE_IMAGE;
    request.catalogRootPageId = 1;
    request.rootPageId = 375;
    request.ownerIndexId = 4;
    request.keyBytes = key;
    request.keyLength = keyLength;
    return true;
}

bool makeRange(int32_t lowerValue, int32_t upperValue,
               bool lowerInclusive, bool upperInclusive,
               uint8_t lower[5], uint8_t upper[5],
               GxNativeSqlReadRequest& request) {
    size_t lowerLength = 0;
    size_t upperLength = 0;
    if (gxEncodeIndexInt32Key(false, lowerValue, lower, 5, &lowerLength) !=
            GxStatus::Ok ||
        gxEncodeIndexInt32Key(false, upperValue, upper, 5, &upperLength) !=
            GxStatus::Ok) return false;
    request = GxNativeSqlReadRequest();
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
    request.lowerInclusive = lowerInclusive ? 1 : 0;
    request.hasUpper = 1;
    request.upperKeyBytes = upper;
    request.upperKeyLength = upperLength;
    request.upperInclusive = upperInclusive ? 1 : 0;
    return true;
}

bool rowsMatch(const GxNativeSqlReadResponse& response,
               const GxNativeSqlReadLocator* actual,
               const ExpectedLocator* expected, size_t count) {
    if (response.status != GX_NATIVE_SQL_READ_OK || response.rowCount != count)
        return false;
    for (size_t i = 0; i < count; ++i) {
        if (actual[i].pageId != expected[i].pageId ||
            actual[i].slot != expected[i].slot) return false;
    }
    return true;
}

bool unchanged(const GxNativeSqlReadLocator* rows,
               const GxNativeSqlReadLocator* before, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        if (rows[i].pageId != before[i].pageId || rows[i].slot != before[i].slot)
            return false;
    }
    return true;
}

int32_t bridgeOpen(const char* path, uint16_t flags) {
    const uint8_t handle = kernel::vfs::open(path, flags);
    return handle == 0xFF ? static_cast<int32_t>(kernel::vfs::VFS_ERR_NOT_FOUND)
                          : static_cast<int32_t>(handle);
}
int32_t bridgeClose(uint8_t handle) {
    const int32_t status = kernel::vfs::close(handle);
    if (failNextClose) { failNextClose = false; return kernel::vfs::VFS_ERR_IO; }
    return status;
}
int32_t bridgeRead(uint8_t handle, void* buffer, uint32_t length) {
    if (failNextRead) { failNextRead = false; return kernel::vfs::VFS_ERR_IO; }
    if (failReadAtOffset && kernel::vfs::tell(handle) ==
            static_cast<int64_t>(readFailureOffset)) {
        failReadAtOffset = false;
        return length == 0 ? 0 : static_cast<int32_t>(length - 1u);
    }
    return kernel::vfs::read(handle, buffer, length);
}
int32_t bridgeWrite(uint8_t handle, const void* buffer, uint32_t length) {
    return kernel::vfs::write(handle, buffer, length);
}
int32_t bridgeSeek(uint8_t handle, int64_t offset, uint8_t origin) {
    return kernel::vfs::seek(handle, offset,
        static_cast<kernel::vfs::SeekOrigin>(origin));
}
int64_t bridgeTell(uint8_t handle) { return kernel::vfs::tell(handle); }
int64_t bridgeSize(uint8_t handle) { return kernel::vfs::file_size(handle); }
int32_t bridgeFlush(uint8_t handle) { return kernel::vfs::flush(handle); }
int32_t bridgeTruncate(uint8_t handle, uint64_t size) {
    return kernel::vfs::truncate_shrink(handle, size);
}
int32_t bridgeStat(const char* path, uint32_t* type, uint64_t* size) {
    kernel::vfs::FileInfo info;
    const kernel::vfs::Status status = kernel::vfs::stat(path, &info);
    if (status == kernel::vfs::VFS_OK) {
        if (type) *type = static_cast<uint32_t>(info.type);
        if (size) *size = info.size;
    }
    return status;
}
int32_t bridgeUnlink(const char* path) { return kernel::vfs::unlink(path); }

const GxDbNativeFileOps kFaultOps = {
    bridgeOpen, bridgeClose, bridgeRead, bridgeWrite, bridgeSeek, bridgeTell,
    bridgeSize, bridgeFlush, bridgeTruncate, bridgeStat, bridgeUnlink
};

bool corruptFixturePage(std::vector<uint8_t>& disk,
                        const std::vector<uint8_t>& fixture,
                        uint64_t pageId) {
    std::vector<uint8_t>::const_iterator found = std::search(
        disk.begin(), disk.end(), fixture.begin(), fixture.end());
    if (found == disk.end()) return false;
    if (pageId > (UINT64_MAX - 32u) / 512u) return false;
    const uint64_t relativeOffset = pageId * 512u + 32u;
    const size_t baseOffset = static_cast<size_t>(found - disk.begin());
    if (baseOffset > disk.size() ||
        relativeOffset >= static_cast<uint64_t>(disk.size() - baseOffset)) {
        return false;
    }
    const size_t offset = baseOffset + static_cast<size_t>(relativeOffset);
    disk[offset] ^= 0x80u;
    return true;
}

bool corruptFixtureRoot(std::vector<uint8_t>& disk,
                        const std::vector<uint8_t>& fixture) {
    return corruptFixturePage(disk, fixture, 148u);
}

bool corruptFixtureIndexOwner(std::vector<uint8_t>& disk,
                              const std::vector<uint8_t>& fixture,
                              uint64_t pageId, uint32_t ownerIndexId) {
    std::vector<uint8_t>::const_iterator found = std::search(
        disk.begin(), disk.end(), fixture.begin(), fixture.end());
    if (found == disk.end() || pageId > (UINT64_MAX - 512u) / 512u) return false;
    const size_t baseOffset = static_cast<size_t>(found - disk.begin());
    const uint64_t relativeOffset = pageId * 512u;
    if (baseOffset > disk.size() || relativeOffset > disk.size() - baseOffset ||
        512u > disk.size() - baseOffset - static_cast<size_t>(relativeOffset)) {
        return false;
    }
    uint8_t* page = disk.data() + baseOffset +
        static_cast<size_t>(relativeOffset);
    const size_t ownerOffset = 32u + 20u;
    for (uint32_t byte = 0; byte < 4; ++byte) {
        page[ownerOffset + byte] =
            static_cast<uint8_t>(ownerIndexId >> (byte * 8u));
    }
    const uint8_t zero[4] = {};
    uint32_t crc = gxos::db::crc32Init();
    crc = gxos::db::crc32Update(crc, page, 24u);
    crc = gxos::db::crc32Update(crc, zero, sizeof(zero));
    crc = gxos::db::crc32Update(crc, page + 28u, 512u - 28u);
    const uint32_t pageCrc = gxos::db::crc32Final(crc);
    for (uint32_t byte = 0; byte < 4; ++byte) {
        page[24u + byte] = static_cast<uint8_t>(pageCrc >> (byte * 8u));
    }
    return true;
}

bool corruptFixtureCatalog(std::vector<uint8_t>& disk,
                           const std::vector<uint8_t>& fixture) {
    return corruptFixturePage(disk, fixture, 1u);
}

} // namespace

int main(int argc, char** argv) {
    std::cout.setf(std::ios::unitbuf);
    if (argc != 4) {
        std::cerr << "usage: native_sql_r3aq_query_service_test <candidate-ramdisk> "
                     "<missing-fixture-ramdisk> <source-fixture>\n";
        return 2;
    }

    std::vector<uint8_t> candidateImage, missingImage, sourceFixture;
    check(readHostFile(argv[1], candidateImage), "read phase candidate FAT image");
    check(readHostFile(argv[2], missingImage), "read missing-fixture FAT image");
    check(readHostFile(argv[3], sourceFixture), "read trusted source fixture");
    if (failures) return 1;
    const std::vector<uint8_t> candidateBefore = candidateImage;

    kernel::block::init();
    kernel::fs_fat::init();
    kernel::ramdisk::init();
    kernel::vfs::init();
    check(mountSystem(candidateImage, "r3aqvalid"),
          "mount candidate using production read-only RAM disk and FAT VFS");
    if (failures) return 1;
    const kernel::vfs::MountPoint* mount = kernel::vfs::get_mount("/system");
    const kernel::block::BlockDevice* device = mount
        ? kernel::block::get_device(mount->blockDevIndex) : nullptr;
    check(mount && mount->readOnly && device && device->readFn && !device->writeFn,
          "candidate fixture is on a production read-only VFS/block path");

    const GxNativeSqlIndexCatalogEntry* k64Metadata =
        gxNativeSqlGetSupportedIndexAt(0);
    const GxNativeSqlIndexCatalogEntry* k32Metadata =
        gxNativeSqlGetSupportedIndexAt(1);
    const GxNativeSqlIndexCatalogEntry* kBoolMetadata =
        gxNativeSqlGetSupportedIndexAt(2);
    const GxNativeSqlIndexCatalogEntry* kTextMetadata =
        gxNativeSqlGetSupportedIndexAt(3);
    const GxNativeSqlIndexCatalogEntry* edge64Metadata =
        gxNativeSqlGetSupportedIndexAt(4);
    const GxNativeSqlIndexCatalogEntry* edge32Metadata =
        gxNativeSqlGetSupportedIndexAt(5);
    check(gxNativeSqlGetSupportedIndexCount() == 6 && k64Metadata &&
          k32Metadata && kBoolMetadata && kTextMetadata && edge64Metadata &&
          edge32Metadata &&
          k64Metadata->identity == GX_NATIVE_SQL_INDEX_ROWS_K64 &&
          std::string(k64Metadata->name) == "IX_Rows_K64" &&
          k64Metadata->keyType == GX_NATIVE_SQL_KEY_INT64 &&
          k64Metadata->capabilities == GX_NATIVE_SQL_INDEX_CAP_POINT &&
          k32Metadata->identity == GX_NATIVE_SQL_INDEX_ROWS_K32 &&
          std::string(k32Metadata->name) == "IX_Rows_K32" &&
          k32Metadata->keyType == GX_NATIVE_SQL_KEY_INT32 &&
          k32Metadata->capabilities == GX_NATIVE_SQL_INDEX_CAP_RANGE &&
          kBoolMetadata->identity == GX_NATIVE_SQL_INDEX_ROWS_KBOOL &&
          std::string(kBoolMetadata->name) == "IX_Rows_KBool" &&
          kBoolMetadata->keyType == GX_NATIVE_SQL_KEY_BOOLEAN &&
          kBoolMetadata->capabilities == GX_NATIVE_SQL_INDEX_CAP_POINT &&
          kBoolMetadata->comparisonSemantics ==
              GX_NATIVE_SQL_COMPARE_BOOLEAN_ASCENDING_NULLS_FIRST &&
          kTextMetadata->identity == GX_NATIVE_SQL_INDEX_ROWS_KTEXT &&
          std::string(kTextMetadata->name) == "IX_Rows_KText" &&
          kTextMetadata->keyType == GX_NATIVE_SQL_KEY_TEXT &&
          kTextMetadata->capabilities == GX_NATIVE_SQL_INDEX_CAP_POINT &&
          kTextMetadata->comparisonSemantics ==
              GX_NATIVE_SQL_COMPARE_TEXT_BYTES_ASCENDING_NULLS_FIRST &&
          edge64Metadata->identity == GX_NATIVE_SQL_INDEX_ROWS_EDGE64 &&
          std::string(edge64Metadata->name) == "IX_Rows_Edge64" &&
          edge64Metadata->keyType == GX_NATIVE_SQL_KEY_INT64 &&
          edge64Metadata->capabilities == GX_NATIVE_SQL_INDEX_CAP_POINT &&
          edge64Metadata->comparisonSemantics ==
              GX_NATIVE_SQL_COMPARE_SIGNED_ASCENDING_NO_NULLS &&
          edge32Metadata->identity == GX_NATIVE_SQL_INDEX_ROWS_EDGE32 &&
          std::string(edge32Metadata->name) == "IX_Rows_Edge32" &&
          edge32Metadata->keyType == GX_NATIVE_SQL_KEY_INT32 &&
          edge32Metadata->capabilities == GX_NATIVE_SQL_INDEX_CAP_POINT &&
          edge32Metadata->comparisonSemantics ==
              GX_NATIVE_SQL_COMPARE_SIGNED_ASCENDING_NULLS_FIRST &&
          k64Metadata->maximumResults == GX_NATIVE_SQL_READ_MAX_RESULTS &&
          k32Metadata->maximumResults == GX_NATIVE_SQL_READ_MAX_RESULTS &&
          kBoolMetadata->maximumResults == GX_NATIVE_SQL_READ_MAX_RESULTS &&
          kTextMetadata->maximumResults == GX_NATIVE_SQL_READ_MAX_RESULTS &&
          edge64Metadata->maximumResults == GX_NATIVE_SQL_READ_MAX_RESULTS &&
          edge32Metadata->maximumResults == GX_NATIVE_SQL_READ_MAX_RESULTS,
          "trusted reader metadata exposes all six indexes in identity order");
    uint64_t hiddenCatalogRoot = 0, hiddenRoot = 0;
    uint32_t hiddenOwner = 0;
    check(gxNativeSqlGetReadIndexIdentity(GX_NATIVE_SQL_INDEX_ROWS_EDGE64,
              &hiddenCatalogRoot, &hiddenRoot, &hiddenOwner) &&
          hiddenCatalogRoot == 1 && hiddenRoot == 218 && hiddenOwner == 5,
          "Edge64 identity maps to the trusted root and owner internally");
    GxNativeSqlImageError catalogImageError = GX_NATIVE_SQL_IMAGE_OK;
    GxNativeSqlImageError catalogCleanupError = GX_NATIVE_SQL_IMAGE_OK;
    check(gxNativeSqlValidateCatalog(GX_NATIVE_SQL_FIXTURE_R3AH_MULTILEVEL,
              &catalogImageError, &catalogCleanupError) == GX_NATIVE_SQL_READ_OK &&
          catalogImageError == GX_NATIVE_SQL_IMAGE_OK &&
          catalogCleanupError == GX_NATIVE_SQL_IMAGE_OK && allVfsHandlesClosed(),
          "catalog preflight validates every advertised index root through production VFS");

    std::vector<uint8_t> beforeFixture, afterFixture;
    check(readVfsFile("/system/Apps/R3AI/MULTI.GDB", beforeFixture) &&
          beforeFixture == sourceFixture,
          "production VFS returns byte-identical trusted fixture");

    GxNativeSqlReadLocator rows[GX_NATIVE_SQL_READ_MAX_RESULTS];
    GxNativeSqlReadLocator saved[GX_NATIVE_SQL_READ_MAX_RESULTS];
    for (size_t i = 0; i < GX_NATIVE_SQL_READ_MAX_RESULTS; ++i) {
        rows[i] = {0xDEAD0000ull + i, static_cast<uint32_t>(0xA000u + i)};
        saved[i] = rows[i];
    }
    GxNativeSqlReadResponse response = {};
    uint8_t pointKey[9] = {};
    size_t pointKeyLength = 0;
    GxNativeSqlReadRequest request = {};
    check(makePoint(7, pointKey, pointKeyLength, request),
          "encode point query with production Int64 key encoder");
    check(gxNativeSqlExecuteRead(&request, rows, GX_NATIVE_SQL_READ_MAX_RESULTS,
              &response) == GX_NATIVE_SQL_READ_OK &&
          rowsMatch(response, rows, kPointExpected, 73) &&
          response.cleanupError == GX_NATIVE_SQL_IMAGE_OK,
          "valid point returns all 73 ordered locators and closes provider");
    check(gxNativeSqlExecuteRead(&request, rows, GX_NATIVE_SQL_READ_MAX_RESULTS,
              &response) == GX_NATIVE_SQL_READ_OK &&
          rowsMatch(response, rows, kPointExpected, 73),
          "repeated sequential point invocation returns the same result");

    uint8_t booleanKey[2] = {};
    size_t booleanKeyLength = 0;
    check(makeBooleanPoint(false, true, booleanKey, booleanKeyLength, request) &&
          gxNativeSqlExecuteRead(&request, rows, GX_NATIVE_SQL_READ_MAX_RESULTS,
              &response) == GX_NATIVE_SQL_READ_OK &&
          rowsMatch(response, rows, kBooleanTrueExpected, 34) &&
          response.cleanupError == GX_NATIVE_SQL_IMAGE_OK,
          "Boolean point lookup returns all 34 independently decoded ordered locators");
    check(makeBooleanPoint(false, false, booleanKey, booleanKeyLength, request) &&
          gxNativeSqlExecuteRead(&request, rows, GX_NATIVE_SQL_READ_MAX_RESULTS,
              &response) == GX_NATIVE_SQL_READ_OK &&
          rowsMatch(response, rows, kBooleanFalseExpected, 34),
          "Boolean false point lookup returns all 34 independently decoded ordered locators");
    check(makeBooleanPoint(true, false, booleanKey, booleanKeyLength, request) &&
          gxNativeSqlExecuteRead(&request, rows, GX_NATIVE_SQL_READ_MAX_RESULTS,
              &response) == GX_NATIVE_SQL_READ_OK &&
          rowsMatch(response, rows, kBooleanNullExpected, 12),
          "Boolean NULL point lookup returns the 12 NULL-first locators");
    check(makeBooleanPoint(false, true, booleanKey, booleanKeyLength, request),
          "prepare malformed Boolean query");
    booleanKey[1] = 2;
    for (size_t i = 0; i < GX_NATIVE_SQL_READ_MAX_RESULTS; ++i) rows[i] = saved[i];
    check(gxNativeSqlExecuteRead(&request, rows, GX_NATIVE_SQL_READ_MAX_RESULTS,
              &response) == GX_NATIVE_SQL_READ_INVALID_REQUEST &&
          response.rowCount == 0 &&
          unchanged(rows, saved, GX_NATIVE_SQL_READ_MAX_RESULTS),
          "malformed Boolean key encoding is rejected without publishing rows");
    check(makeBooleanPoint(false, true, booleanKey, booleanKeyLength, request) &&
          gxNativeSqlExecuteRead(&request, rows, GX_NATIVE_SQL_READ_MAX_RESULTS,
              &response) == GX_NATIVE_SQL_READ_OK &&
          rowsMatch(response, rows, kBooleanTrueExpected, 34),
          "Boolean point query succeeds after malformed-key rejection");
    check(makeBooleanPoint(false, true, booleanKey, booleanKeyLength, request),
          "prepare Boolean capacity case");
    for (size_t i = 0; i < GX_NATIVE_SQL_READ_MAX_RESULTS; ++i) rows[i] = saved[i];
    check(gxNativeSqlExecuteRead(&request, rows, 33, &response) ==
              GX_NATIVE_SQL_READ_RESOURCE_EXHAUSTED && response.rowCount == 0 &&
          unchanged(rows, saved, GX_NATIVE_SQL_READ_MAX_RESULTS),
          "Boolean capacity exhaustion does not truncate or publish locators");

    const uint8_t embeddedNulText[] = {0x61u, 0x00u, 0x62u};
    const uint8_t otherText[] = {0x6Fu, 0x74u, 0x68u, 0x65u, 0x72u};
    const uint8_t missingText[] = {0x61u, 0x62u, 0x73u, 0x65u, 0x6Eu, 0x74u};
    uint8_t textKey[8] = {};
    size_t textKeyLength = 0;
    check(makeTextPoint(true, nullptr, 0, textKey, textKeyLength, request) &&
          gxNativeSqlExecuteRead(&request, rows, GX_NATIVE_SQL_READ_MAX_RESULTS,
              &response) == GX_NATIVE_SQL_READ_OK &&
          rowsMatch(response, rows, kTextNullExpected, 8) &&
          response.cleanupError == GX_NATIVE_SQL_IMAGE_OK,
          "Text NULL lookup returns all 8 independently decoded ordered locators");
    check(makeTextPoint(false, embeddedNulText, sizeof(embeddedNulText),
              textKey, textKeyLength, request) && textKeyLength == 4 &&
          textKey[0] == 1 && textKey[1] == 0x61u && textKey[2] == 0 &&
          textKey[3] == 0x62u &&
          gxNativeSqlExecuteRead(&request, rows, GX_NATIVE_SQL_READ_MAX_RESULTS,
              &response) == GX_NATIVE_SQL_READ_OK &&
          rowsMatch(response, rows, kTextEmbeddedNulExpected, 24),
          "embedded-NUL Text lookup preserves exact bytes and returns 24 locators");
    check(makeTextPoint(false, otherText, sizeof(otherText), textKey,
              textKeyLength, request) && textKeyLength == 6 &&
          gxNativeSqlExecuteRead(&request, rows, GX_NATIVE_SQL_READ_MAX_RESULTS,
              &response) == GX_NATIVE_SQL_READ_OK &&
          rowsMatch(response, rows, kTextOtherExpected, 48),
          "other Text lookup returns all 48 independently decoded locators");
    check(makeTextPoint(false, missingText, sizeof(missingText), textKey,
              textKeyLength, request), "prepare Text no-match query");
    for (size_t i = 0; i < GX_NATIVE_SQL_READ_MAX_RESULTS; ++i) rows[i] = saved[i];
    check(gxNativeSqlExecuteRead(&request, rows, GX_NATIVE_SQL_READ_MAX_RESULTS,
              &response) == GX_NATIVE_SQL_READ_NO_MATCH && response.rowCount == 0 &&
          unchanged(rows, saved, GX_NATIVE_SQL_READ_MAX_RESULTS),
          "Text no-match reports zero rows without publishing locator storage");
    check(makeTextPoint(false, otherText, sizeof(otherText), textKey,
              textKeyLength, request), "prepare Text capacity case");
    for (size_t i = 0; i < GX_NATIVE_SQL_READ_MAX_RESULTS; ++i) rows[i] = saved[i];
    check(gxNativeSqlExecuteRead(&request, rows, 47, &response) ==
              GX_NATIVE_SQL_READ_RESOURCE_EXHAUSTED && response.rowCount == 0 &&
          unchanged(rows, saved, GX_NATIVE_SQL_READ_MAX_RESULTS),
          "Text capacity below 48 fails without truncation or partial publication");
    check(makeTextPoint(false, embeddedNulText, sizeof(embeddedNulText),
              textKey, textKeyLength, request), "prepare malformed Text encoding case");
    const uint8_t malformedTextKey[2] = {0, 1};
    request.keyBytes = malformedTextKey;
    request.keyLength = sizeof(malformedTextKey);
    for (size_t i = 0; i < GX_NATIVE_SQL_READ_MAX_RESULTS; ++i) rows[i] = saved[i];
    check(gxNativeSqlExecuteRead(&request, rows, GX_NATIVE_SQL_READ_MAX_RESULTS,
              &response) == GX_NATIVE_SQL_READ_INVALID_REQUEST && response.rowCount == 0 &&
          unchanged(rows, saved, GX_NATIVE_SQL_READ_MAX_RESULTS),
          "malformed Text NULL encoding is rejected without publishing rows");
    check(makeTextPoint(false, embeddedNulText, sizeof(embeddedNulText),
              textKey, textKeyLength, request), "prepare overlong Text span case");
    request.keyLength = 9;
    check(gxNativeSqlExecuteRead(&request, rows, GX_NATIVE_SQL_READ_MAX_RESULTS,
              &response) == GX_NATIVE_SQL_READ_INVALID_REQUEST && response.rowCount == 0 &&
          unchanged(rows, saved, GX_NATIVE_SQL_READ_MAX_RESULTS),
          "Text encoded span beyond the fixed inline bound is rejected safely");
    request.operation = GX_NATIVE_SQL_READ_RANGE;
    for (size_t i = 0; i < GX_NATIVE_SQL_READ_MAX_RESULTS; ++i) rows[i] = saved[i];
    check(gxNativeSqlExecuteRead(&request, rows, GX_NATIVE_SQL_READ_MAX_RESULTS,
              &response) == GX_NATIVE_SQL_READ_INVALID_REQUEST && response.rowCount == 0 &&
          unchanged(rows, saved, GX_NATIVE_SQL_READ_MAX_RESULTS),
          "production read service rejects unsupported Text range requests");
    check(makeTextPoint(false, otherText, sizeof(otherText), textKey,
              textKeyLength, request) &&
          gxNativeSqlExecuteRead(&request, rows, GX_NATIVE_SQL_READ_MAX_RESULTS,
              &response) == GX_NATIVE_SQL_READ_OK &&
          rowsMatch(response, rows, kTextOtherExpected, 48),
          "Text lookup recovers after malformed input and capacity rejection");

    bool edge32InventoryMatches =
        kSqlReadEdge32OracleCount == 80u;
    bool sawMinimum = false, sawMaximum = false, sawNegative = false;
    bool sawZero = false, sawPositive = false;
    uint32_t matchedSeparators = 0;
    uint8_t edge32Key[5] = {};
    size_t edge32KeyLength = 0;
    for (uint32_t i = 0; i < kSqlReadEdge32OracleCount; ++i) {
        const SqlReadEdge32OracleEntry& expected = kSqlReadEdge32Oracle[i];
        if (i != 0 && kSqlReadEdge32Oracle[i - 1].key >= expected.key) {
            edge32InventoryMatches = false;
        }
        sawMinimum = sawMinimum || expected.key == INT32_MIN;
        sawMaximum = sawMaximum || expected.key == INT32_MAX;
        sawNegative = sawNegative || expected.key < 0;
        sawZero = sawZero || expected.key == 0;
        sawPositive = sawPositive || expected.key > 0;
        for (uint32_t boundary = 0;
             boundary < kSqlReadEdge32InternalSeparatorCount; ++boundary) {
            if (expected.key == kSqlReadEdge32InternalSeparators[boundary]) {
                ++matchedSeparators;
                break;
            }
        }
        if (!makeEdge32Point(expected.key, edge32Key, edge32KeyLength,
                             request) ||
            gxNativeSqlExecuteRead(&request, rows,
                GX_NATIVE_SQL_READ_MAX_RESULTS, &response) !=
                GX_NATIVE_SQL_READ_OK || response.rowCount != 1 ||
            rows[0].pageId != expected.pageId ||
            rows[0].slot != expected.slot ||
            response.cleanupError != GX_NATIVE_SQL_IMAGE_OK) {
            edge32InventoryMatches = false;
        }
    }
    check(edge32InventoryMatches && sawMinimum && sawMaximum && sawNegative &&
          sawZero && sawPositive &&
          matchedSeparators == kSqlReadEdge32InternalSeparatorCount &&
          allVfsHandlesClosed(),
          "Edge32 production VFS point lookup matches all 80 independent signed key:locator pairs and every internal separator");

    check(makeEdge32Point(kSqlReadEdge32FirstAbsentKey, edge32Key,
              edge32KeyLength, request) &&
          gxNativeSqlExecuteRead(&request, rows, GX_NATIVE_SQL_READ_MAX_RESULTS,
              &response) == GX_NATIVE_SQL_READ_NO_MATCH && response.rowCount == 0,
          "Edge32 signed negative nonmatch returns deterministic no-match");
    check(makeEdge32Point(40, edge32Key, edge32KeyLength, request) &&
          gxNativeSqlExecuteRead(&request, rows, GX_NATIVE_SQL_READ_MAX_RESULTS,
              &response) == GX_NATIVE_SQL_READ_NO_MATCH && response.rowCount == 0,
          "Edge32 positive key 40 remains absent");
    check(makeEdge32Point(-38, edge32Key, edge32KeyLength, request),
          "prepare Edge32 wrong key type case");
    uint8_t wrongEdge32Type[9] = {};
    size_t wrongEdge32TypeLength = 0;
    (void)gxEncodeIndexInt64Key(false, -38, wrongEdge32Type,
                                sizeof(wrongEdge32Type),
                                &wrongEdge32TypeLength);
    request.keyBytes = wrongEdge32Type;
    request.keyLength = wrongEdge32TypeLength;
    for (size_t i = 0; i < GX_NATIVE_SQL_READ_MAX_RESULTS; ++i) rows[i] = saved[i];
    check(gxNativeSqlExecuteRead(&request, rows, GX_NATIVE_SQL_READ_MAX_RESULTS,
              &response) == GX_NATIVE_SQL_READ_INVALID_REQUEST &&
          response.rowCount == 0 &&
          unchanged(rows, saved, GX_NATIVE_SQL_READ_MAX_RESULTS),
          "Edge32 rejects a valid Int64 encoding without publishing rows");
    check(makeEdge32Point(-38, edge32Key, edge32KeyLength, request),
          "prepare malformed Edge32 key case");
    const uint8_t malformedEdge32Key[4] = {1, 0, 0, 0};
    request.keyBytes = malformedEdge32Key;
    request.keyLength = sizeof(malformedEdge32Key);
    check(gxNativeSqlExecuteRead(&request, rows, GX_NATIVE_SQL_READ_MAX_RESULTS,
              &response) == GX_NATIVE_SQL_READ_INVALID_REQUEST &&
          response.rowCount == 0,
          "Edge32 rejects a truncated tagged Int32 representation");
    check(makeEdge32Point(-38, edge32Key, edge32KeyLength, request),
          "prepare Edge32 insufficient capacity case");
    for (size_t i = 0; i < GX_NATIVE_SQL_READ_MAX_RESULTS; ++i) rows[i] = saved[i];
    check(gxNativeSqlExecuteRead(&request, rows, 0, &response) ==
              GX_NATIVE_SQL_READ_RESOURCE_EXHAUSTED && response.rowCount == 0 &&
          unchanged(rows, saved, GX_NATIVE_SQL_READ_MAX_RESULTS),
          "Edge32 insufficient result capacity fails without partial publication");
    request.operation = GX_NATIVE_SQL_READ_RANGE;
    check(gxNativeSqlExecuteRead(&request, rows, GX_NATIVE_SQL_READ_MAX_RESULTS,
              &response) == GX_NATIVE_SQL_READ_UNSUPPORTED_OPERATION &&
          response.rowCount == 0,
          "Edge32 bounded range requests are explicitly unsupported");

    uint8_t edge64Key[9] = {};
    size_t edge64KeyLength = 0;
    bool edge64InventoryMatches = kSqlReadEdge64OracleCount == 80u;
    bool edge64SawMinimum = false, edge64SawMaximum = false;
    bool edge64SawNegative = false, edge64SawZero = false, edge64SawPositive = false;
    for (uint32_t i = 0; i < kSqlReadEdge64OracleCount; ++i) {
        const SqlReadEdge64OracleEntry& expected = kSqlReadEdge64Oracle[i];
        if (i != 0 && kSqlReadEdge64Oracle[i - 1].key >= expected.key) {
            edge64InventoryMatches = false;
        }
        edge64SawMinimum = edge64SawMinimum || expected.key == INT64_MIN;
        edge64SawMaximum = edge64SawMaximum || expected.key == INT64_MAX;
        edge64SawNegative = edge64SawNegative || expected.key < 0;
        edge64SawZero = edge64SawZero || expected.key == 0;
        edge64SawPositive = edge64SawPositive || expected.key > 0;
        if (!makeEdge64Point(expected.key, edge64Key, edge64KeyLength, request) ||
            gxNativeSqlExecuteRead(&request, rows, GX_NATIVE_SQL_READ_MAX_RESULTS,
                &response) != GX_NATIVE_SQL_READ_OK || response.rowCount != 1 ||
            rows[0].pageId != expected.page_id || rows[0].slot != expected.slot ||
            response.cleanupError != GX_NATIVE_SQL_IMAGE_OK) {
            edge64InventoryMatches = false;
        }
    }
    check(edge64InventoryMatches && edge64SawMinimum && edge64SawMaximum &&
          edge64SawNegative && edge64SawZero && edge64SawPositive &&
          allVfsHandlesClosed(),
          "Edge64 production VFS point lookup matches all 80 independent signed key:locator pairs");

    const int64_t edge64AbsentKeys[] = {
        (-9223372036854775807ll), 39ll, 9223372036854775806ll
    };
    bool edge64AbsentMatches = true;
    for (size_t i = 0; i < sizeof(edge64AbsentKeys) / sizeof(edge64AbsentKeys[0]); ++i) {
        if (!makeEdge64Point(edge64AbsentKeys[i], edge64Key, edge64KeyLength,
                             request)) {
            edge64AbsentMatches = false;
            break;
        }
        for (size_t row = 0; row < GX_NATIVE_SQL_READ_MAX_RESULTS; ++row) {
            rows[row] = saved[row];
        }
        if (gxNativeSqlExecuteRead(&request, rows, GX_NATIVE_SQL_READ_MAX_RESULTS,
                &response) != GX_NATIVE_SQL_READ_NO_MATCH || response.rowCount != 0 ||
            !unchanged(rows, saved, GX_NATIVE_SQL_READ_MAX_RESULTS)) {
            edge64AbsentMatches = false;
            break;
        }
    }
    check(edge64AbsentMatches,
          "Edge64 signed boundary gaps return no-match without changing rows");

    check(makeEdge64Point(0, edge64Key, edge64KeyLength, request),
          "prepare Edge64 wrong key type case");
    uint8_t wrongEdge64Type[5] = {};
    size_t wrongEdge64TypeLength = 0;
    (void)gxEncodeIndexInt32Key(false, 0, wrongEdge64Type,
                                sizeof(wrongEdge64Type), &wrongEdge64TypeLength);
    request.keyBytes = wrongEdge64Type;
    request.keyLength = wrongEdge64TypeLength;
    for (size_t row = 0; row < GX_NATIVE_SQL_READ_MAX_RESULTS; ++row) rows[row] = saved[row];
    check(gxNativeSqlExecuteRead(&request, rows, GX_NATIVE_SQL_READ_MAX_RESULTS,
              &response) == GX_NATIVE_SQL_READ_INVALID_REQUEST && response.rowCount == 0 &&
          unchanged(rows, saved, GX_NATIVE_SQL_READ_MAX_RESULTS),
          "Edge64 rejects Int32 encoding without publishing rows");

    check(makeEdge64Point(INT64_MIN, edge64Key, edge64KeyLength, request),
          "prepare Edge64 insufficient capacity case");
    for (size_t row = 0; row < GX_NATIVE_SQL_READ_MAX_RESULTS; ++row) rows[row] = saved[row];
    check(gxNativeSqlExecuteRead(&request, rows, 0, &response) ==
              GX_NATIVE_SQL_READ_RESOURCE_EXHAUSTED && response.rowCount == 0 &&
          unchanged(rows, saved, GX_NATIVE_SQL_READ_MAX_RESULTS),
          "Edge64 insufficient capacity fails without partial publication");

    request.operation = GX_NATIVE_SQL_READ_RANGE;
    check(gxNativeSqlExecuteRead(&request, rows, GX_NATIVE_SQL_READ_MAX_RESULTS,
              &response) == GX_NATIVE_SQL_READ_UNSUPPORTED_OPERATION &&
          response.rowCount == 0,
          "Edge64 range requests are explicitly unsupported");
    check(makeEdge64Point(kSqlReadEdge64Oracle[40].key, edge64Key,
              edge64KeyLength, request) &&
          gxNativeSqlExecuteRead(&request, rows, GX_NATIVE_SQL_READ_MAX_RESULTS,
              &response) == GX_NATIVE_SQL_READ_OK && response.rowCount == 1 &&
          rows[0].pageId == kSqlReadEdge64Oracle[40].page_id &&
          rows[0].slot == kSqlReadEdge64Oracle[40].slot && allVfsHandlesClosed(),
          "Edge64 lookup recovers after rejected requests");
    check(makeEdge32Point(INT32_MIN, edge32Key, edge32KeyLength, request) &&
          gxNativeSqlExecuteRead(&request, rows, GX_NATIVE_SQL_READ_MAX_RESULTS,
              &response) == GX_NATIVE_SQL_READ_OK && response.rowCount == 1 &&
          rows[0].pageId == kSqlReadEdge32Oracle[0].pageId &&
          rows[0].slot == kSqlReadEdge32Oracle[0].slot &&
          allVfsHandlesClosed(),
          "Edge32 minimum lookup succeeds after rejected requests");

    uint8_t lower[5] = {}, upper[5] = {};
    check(makeRange(1, 2, true, true, lower, upper, request) &&
          gxNativeSqlExecuteRead(&request, rows, GX_NATIVE_SQL_READ_MAX_RESULTS,
              &response) == GX_NATIVE_SQL_READ_OK &&
          rowsMatch(response, rows, kRangeExpected, 32) &&
          response.cleanupError == GX_NATIVE_SQL_IMAGE_OK,
          "inclusive range returns all 32 ordered locators");
    check(kernel::native_sql_read_diagnostic::run_after_vfs_mount() ==
              kernel::native_sql_read_diagnostic::Passed && allVfsHandlesClosed(),
          "refactored fixed diagnostic remains a passing regression case");
    check(makeRange(1, 2, true, false, lower, upper, request) &&
          gxNativeSqlExecuteRead(&request, rows, GX_NATIVE_SQL_READ_MAX_RESULTS,
              &response) == GX_NATIVE_SQL_READ_OK &&
          rowsMatch(response, rows, kRangeExpected, 16),
          "exclusive upper range bound omits key 2 locators");
    check(makeRange(1, 2, false, true, lower, upper, request) &&
          gxNativeSqlExecuteRead(&request, rows, GX_NATIVE_SQL_READ_MAX_RESULTS,
              &response) == GX_NATIVE_SQL_READ_OK &&
          response.rowCount == 16 && rowsMatch(response, rows,
              kRangeExpected + 16, 16),
          "exclusive lower range bound omits key 1 locators");

    check(makePoint(9999, pointKey, pointKeyLength, request) &&
          gxNativeSqlExecuteRead(&request, rows, GX_NATIVE_SQL_READ_MAX_RESULTS,
              &response) == GX_NATIVE_SQL_READ_NO_MATCH && response.rowCount == 0,
          "missing point key returns explicit no-match status");
    check(makeRange(2, 1, false, true, lower, upper, request) &&
          gxNativeSqlExecuteRead(&request, rows, GX_NATIVE_SQL_READ_MAX_RESULTS,
              &response) == GX_NATIVE_SQL_READ_NO_MATCH && response.rowCount == 0,
          "empty range returns explicit no-match status");

    check(makePoint(7, pointKey, pointKeyLength, request), "prepare invalid-request cases");
    request.operation = static_cast<GxNativeSqlReadOperation>(99);
    for (size_t i = 0; i < GX_NATIVE_SQL_READ_MAX_RESULTS; ++i) rows[i] = saved[i];
    check(gxNativeSqlExecuteRead(&request, rows, GX_NATIVE_SQL_READ_MAX_RESULTS,
              &response) == GX_NATIVE_SQL_READ_UNSUPPORTED_OPERATION &&
          unchanged(rows, saved, GX_NATIVE_SQL_READ_MAX_RESULTS),
          "invalid request type is rejected without publishing rows");
    check(makePoint(7, pointKey, pointKeyLength, request), "prepare identity cases");
    request.fixture = static_cast<GxNativeSqlReadFixture>(99);
    check(gxNativeSqlExecuteRead(&request, rows, GX_NATIVE_SQL_READ_MAX_RESULTS,
              &response) == GX_NATIVE_SQL_READ_INVALID_REQUEST,
          "wrong trusted fixture identity is rejected");
    check(makePoint(7, pointKey, pointKeyLength, request), "prepare index identity case");
    request.index = GX_NATIVE_SQL_INDEX_ROWS_K32;
    check(gxNativeSqlExecuteRead(&request, rows, GX_NATIVE_SQL_READ_MAX_RESULTS,
              &response) == GX_NATIVE_SQL_READ_INVALID_REQUEST,
          "wrong index identity is rejected");
    check(makePoint(7, pointKey, pointKeyLength, request), "prepare root identity case");
    request.rootPageId = 233;
    check(gxNativeSqlExecuteRead(&request, rows, GX_NATIVE_SQL_READ_MAX_RESULTS,
              &response) == GX_NATIVE_SQL_READ_INVALID_REQUEST,
          "invalid root identity is rejected");
    check(makePoint(7, pointKey, pointKeyLength, request), "prepare owner identity case");
    request.ownerIndexId = 2;
    check(gxNativeSqlExecuteRead(&request, rows, GX_NATIVE_SQL_READ_MAX_RESULTS,
              &response) == GX_NATIVE_SQL_READ_INVALID_REQUEST,
          "invalid owner identity is rejected");
    check(makePoint(7, pointKey, pointKeyLength, request), "prepare live-access case");
    request.consistency = GX_NATIVE_SQL_CONSISTENCY_LIVE_SINGLE_INSTANCE;
    check(gxNativeSqlExecuteRead(&request, rows, GX_NATIVE_SQL_READ_MAX_RESULTS,
              &response) == GX_NATIVE_SQL_READ_UNSUPPORTED_OPERATION,
          "unsupported live-access request fails closed");

    check(makePoint(7, pointKey, pointKeyLength, request), "prepare key encoding case");
    pointKey[0] = 2;
    for (size_t i = 0; i < GX_NATIVE_SQL_READ_MAX_RESULTS; ++i) rows[i] = saved[i];
    check(gxNativeSqlExecuteRead(&request, rows, GX_NATIVE_SQL_READ_MAX_RESULTS,
              &response) == GX_NATIVE_SQL_READ_INVALID_REQUEST && response.rowCount == 0 &&
          unchanged(rows, saved, GX_NATIVE_SQL_READ_MAX_RESULTS),
          "invalid key encoding is rejected with no partial result publication");

    check(makePoint(7, pointKey, pointKeyLength, request), "prepare capacity case");
    for (size_t i = 0; i < GX_NATIVE_SQL_READ_MAX_RESULTS; ++i) rows[i] = saved[i];
    check(gxNativeSqlExecuteRead(&request, rows, 4, &response) ==
              GX_NATIVE_SQL_READ_RESOURCE_EXHAUSTED && response.rowCount == 0 &&
          unchanged(rows, saved, GX_NATIVE_SQL_READ_MAX_RESULTS),
          "insufficient result capacity reports exhaustion without truncation");

    std::vector<uint8_t> writableImage = candidateBefore;
    check(kernel::vfs::unmount("/system") == kernel::vfs::VFS_OK &&
          mountWritableSystem(writableImage),
          "mount an isolated writable FAT copy for fail-closed check");
    check(gxNativeSqlExecuteRead(&request, rows, GX_NATIVE_SQL_READ_MAX_RESULTS,
              &response) == GX_NATIVE_SQL_READ_UNSUPPORTED_OPERATION &&
          allVfsHandlesClosed(),
          "service refuses the trusted path on a writable VFS/block mount");
    check(kernel::vfs::unmount("/system") == kernel::vfs::VFS_OK &&
          remountCandidate(),
          "restore read-only candidate after writable-mount rejection");

    gx_db_file_bind_ops(&kFaultOps);
    failNextRead = true;
    const GxNativeSqlReadStatus catalogStorageFailure =
        gxNativeSqlValidateCatalog(GX_NATIVE_SQL_FIXTURE_R3AH_MULTILEVEL,
            &catalogImageError, &catalogCleanupError);
    gx_db_file_bind_ops(nullptr);
    check(catalogStorageFailure == GX_NATIVE_SQL_READ_STORAGE_FAILURE &&
          catalogImageError == GX_NATIVE_SQL_IMAGE_READ_FAILURE &&
          catalogCleanupError == GX_NATIVE_SQL_IMAGE_OK && allVfsHandlesClosed(),
          "catalog validation closes provider after an injected production read failure");

    gx_db_file_bind_ops(&kFaultOps);
    failNextRead = true;
    const GxNativeSqlReadStatus storageFailure = gxNativeSqlExecuteRead(
        &request, rows, GX_NATIVE_SQL_READ_MAX_RESULTS, &response);
    gx_db_file_bind_ops(nullptr);
    check(storageFailure == GX_NATIVE_SQL_READ_STORAGE_FAILURE &&
          allVfsHandlesClosed(),
          "injected production-VFS read failure closes its handle");
    check(gxNativeSqlExecuteRead(&request, rows, GX_NATIVE_SQL_READ_MAX_RESULTS,
              &response) == GX_NATIVE_SQL_READ_OK &&
          rowsMatch(response, rows, kPointExpected, 73),
          "valid query succeeds immediately after storage failure");

    makeBooleanPoint(false, true, booleanKey, booleanKeyLength, request);
    gx_db_file_bind_ops(&kFaultOps);
    failNextRead = true;
    const GxNativeSqlReadStatus booleanStorageFailure = gxNativeSqlExecuteRead(
        &request, rows, GX_NATIVE_SQL_READ_MAX_RESULTS, &response);
    gx_db_file_bind_ops(nullptr);
    check(booleanStorageFailure == GX_NATIVE_SQL_READ_STORAGE_FAILURE &&
          allVfsHandlesClosed(),
          "Boolean query closes provider after a failed production VFS read");
    check(gxNativeSqlExecuteRead(&request, rows, GX_NATIVE_SQL_READ_MAX_RESULTS,
              &response) == GX_NATIVE_SQL_READ_OK &&
          rowsMatch(response, rows, kBooleanTrueExpected, 34),
          "Boolean query succeeds immediately after VFS read failure");

    for (size_t i = 0; i < GX_NATIVE_SQL_READ_MAX_RESULTS; ++i) rows[i] = saved[i];
    gx_db_file_bind_ops(&kFaultOps);
    failReadAtOffset = true;
    readFailureOffset = 124u * 512u;
    const GxNativeSqlReadStatus booleanShortRead = gxNativeSqlExecuteRead(
        &request, rows, GX_NATIVE_SQL_READ_MAX_RESULTS, &response);
    gx_db_file_bind_ops(nullptr);
    check(booleanShortRead == GX_NATIVE_SQL_READ_CORRUPT_INDEX &&
          response.imageError == GX_NATIVE_SQL_IMAGE_TRUNCATED &&
          response.rowCount == 0 &&
          unchanged(rows, saved, GX_NATIVE_SQL_READ_MAX_RESULTS) &&
          allVfsHandlesClosed(),
          "short read of the Boolean root page is corrupt-index failure without publication");
    check(gxNativeSqlExecuteRead(&request, rows, GX_NATIVE_SQL_READ_MAX_RESULTS,
              &response) == GX_NATIVE_SQL_READ_OK &&
          rowsMatch(response, rows, kBooleanTrueExpected, 34),
          "Boolean query succeeds immediately after truncated-page rejection");

    gx_db_file_bind_ops(&kFaultOps);
    failNextClose = true;
    const GxNativeSqlReadStatus closeFailure = gxNativeSqlExecuteRead(
        &request, rows, GX_NATIVE_SQL_READ_MAX_RESULTS, &response);
    gx_db_file_bind_ops(nullptr);
    check(closeFailure == GX_NATIVE_SQL_READ_STORAGE_FAILURE &&
          response.cleanupError == GX_NATIVE_SQL_IMAGE_CLOSE_FAILURE &&
          allVfsHandlesClosed(),
          "close failure is reported and consumed handle is verified closed");
    check(gxNativeSqlExecuteRead(&request, rows, GX_NATIVE_SQL_READ_MAX_RESULTS,
              &response) == GX_NATIVE_SQL_READ_OK,
          "valid query succeeds immediately after close failure");

    std::vector<uint8_t> corruptImage = candidateBefore;
    check(corruptFixtureRoot(corruptImage, sourceFixture),
          "prepare a copied FAT image with a corrupt index root page");
    check(kernel::vfs::unmount("/system") == kernel::vfs::VFS_OK &&
          mountSystem(corruptImage, "r3aqcorrupt"),
          "mount isolated corrupt fixture copy");
    check(gxNativeSqlValidateCatalog(GX_NATIVE_SQL_FIXTURE_R3AH_MULTILEVEL,
              &catalogImageError, &catalogCleanupError) ==
              GX_NATIVE_SQL_READ_CORRUPT_INDEX &&
          catalogImageError == GX_NATIVE_SQL_IMAGE_CORRUPT_PAGE &&
          catalogCleanupError == GX_NATIVE_SQL_IMAGE_OK && allVfsHandlesClosed(),
          "catalog preflight rejects a corrupt advertised index root and closes provider resources");
    makeRange(1, 2, true, true, lower, upper, request);
    check(gxNativeSqlExecuteRead(&request, rows, GX_NATIVE_SQL_READ_MAX_RESULTS,
              &response) == GX_NATIVE_SQL_READ_CORRUPT_INDEX &&
          allVfsHandlesClosed(),
          "corrupt index root page is rejected and provider resources close");
    check(kernel::vfs::unmount("/system") == kernel::vfs::VFS_OK &&
          remountCandidate(),
          "restore trusted fixture after isolated corruption test");
    makePoint(7, pointKey, pointKeyLength, request);
    check(gxNativeSqlExecuteRead(&request, rows, GX_NATIVE_SQL_READ_MAX_RESULTS,
              &response) == GX_NATIVE_SQL_READ_OK,
          "valid request succeeds after corrupt-page failure");

    check(kernel::vfs::unmount("/system") == kernel::vfs::VFS_OK &&
          corruptFixturePage(candidateImage, sourceFixture, 124u) &&
          kernel::vfs::mount_type("/system", candidateDevice,
              kernel::vfs::FS_TYPE_FAT32) != 0xFF,
          "mount the copied candidate with a corrupted KBool root page");
    check(gxNativeSqlValidateCatalog(GX_NATIVE_SQL_FIXTURE_R3AH_MULTILEVEL,
              &catalogImageError, &catalogCleanupError) ==
              GX_NATIVE_SQL_READ_CORRUPT_INDEX &&
          catalogImageError == GX_NATIVE_SQL_IMAGE_CORRUPT_PAGE &&
          catalogCleanupError == GX_NATIVE_SQL_IMAGE_OK && allVfsHandlesClosed(),
          "catalog rejects a corrupt KBool root before advertising it");
    makeBooleanPoint(false, true, booleanKey, booleanKeyLength, request);
    check(gxNativeSqlExecuteRead(&request, rows, GX_NATIVE_SQL_READ_MAX_RESULTS,
              &response) == GX_NATIVE_SQL_READ_CORRUPT_INDEX &&
          allVfsHandlesClosed(),
          "production KBool query rejects its corrupt persisted root");
    check(kernel::vfs::unmount("/system") == kernel::vfs::VFS_OK &&
          candidateImage.size() == candidateBefore.size(),
          "restore the approved fixture after KBool checksum corruption");
    std::copy(candidateBefore.begin(), candidateBefore.end(), candidateImage.begin());
    check(remountCandidate(), "remount the restored candidate after KBool checksum corruption");
    check(gxNativeSqlExecuteRead(&request, rows, GX_NATIVE_SQL_READ_MAX_RESULTS,
              &response) == GX_NATIVE_SQL_READ_OK &&
          rowsMatch(response, rows, kBooleanTrueExpected, 34),
          "KBool query recovers after corrupt-root rejection");

    check(kernel::vfs::unmount("/system") == kernel::vfs::VFS_OK &&
          corruptFixtureIndexOwner(candidateImage, sourceFixture, 124u, 4u) &&
          kernel::vfs::mount_type("/system", candidateDevice,
              kernel::vfs::FS_TYPE_FAT32) != 0xFF,
          "mount the candidate with a checksum-valid foreign KBool owner id");
    check(gxNativeSqlValidateCatalog(GX_NATIVE_SQL_FIXTURE_R3AH_MULTILEVEL,
              &catalogImageError, &catalogCleanupError) ==
              GX_NATIVE_SQL_READ_CORRUPT_INDEX &&
          catalogImageError == GX_NATIVE_SQL_IMAGE_INVALID_ROOT &&
          catalogCleanupError == GX_NATIVE_SQL_IMAGE_OK && allVfsHandlesClosed(),
          "catalog root validation rejects a checksum-valid foreign KBool owner");
    check(gxNativeSqlExecuteRead(&request, rows, GX_NATIVE_SQL_READ_MAX_RESULTS,
              &response) == GX_NATIVE_SQL_READ_CORRUPT_INDEX &&
          allVfsHandlesClosed(),
          "production KBool query rejects a foreign persisted owner id");
    check(kernel::vfs::unmount("/system") == kernel::vfs::VFS_OK &&
          candidateImage.size() == candidateBefore.size(),
          "unmount checksum-valid KBool owner-corruption fixture");
    std::copy(candidateBefore.begin(), candidateBefore.end(), candidateImage.begin());
    check(remountCandidate(), "restore approved fixture after KBool owner-corruption test");
    check(gxNativeSqlExecuteRead(&request, rows, GX_NATIVE_SQL_READ_MAX_RESULTS,
              &response) == GX_NATIVE_SQL_READ_OK &&
          rowsMatch(response, rows, kBooleanTrueExpected, 34),
          "KBool query succeeds after owner-validation rejection");

    check(kernel::vfs::unmount("/system") == kernel::vfs::VFS_OK &&
          corruptFixturePage(candidateImage, sourceFixture, 259u) &&
          kernel::vfs::mount_type("/system", candidateDevice,
              kernel::vfs::FS_TYPE_FAT32) != 0xFF,
          "mount the candidate with a checksum-invalid Edge32 root page");
    check(gxNativeSqlValidateCatalog(GX_NATIVE_SQL_FIXTURE_R3AH_MULTILEVEL,
              &catalogImageError, &catalogCleanupError) ==
              GX_NATIVE_SQL_READ_CORRUPT_INDEX &&
          catalogImageError == GX_NATIVE_SQL_IMAGE_CORRUPT_PAGE &&
          catalogCleanupError == GX_NATIVE_SQL_IMAGE_OK && allVfsHandlesClosed(),
          "catalog preflight rejects the corrupted Edge32 root metadata");
    makeEdge32Point(-38, edge32Key, edge32KeyLength, request);
    check(gxNativeSqlExecuteRead(&request, rows, GX_NATIVE_SQL_READ_MAX_RESULTS,
              &response) == GX_NATIVE_SQL_READ_CORRUPT_INDEX &&
          response.rowCount == 0 && allVfsHandlesClosed(),
          "Edge32 lookup rejects its corrupted persisted root");
    check(kernel::vfs::unmount("/system") == kernel::vfs::VFS_OK &&
          candidateImage.size() == candidateBefore.size(),
          "unmount checksum-invalid Edge32 root fixture");
    std::copy(candidateBefore.begin(), candidateBefore.end(), candidateImage.begin());
    check(remountCandidate(), "restore the approved fixture after Edge32 root corruption");
    check(gxNativeSqlExecuteRead(&request, rows, GX_NATIVE_SQL_READ_MAX_RESULTS,
              &response) == GX_NATIVE_SQL_READ_OK && response.rowCount == 1 &&
          allVfsHandlesClosed(),
          "Edge32 lookup recovers after corrupt-root rejection");

    check(kernel::vfs::unmount("/system") == kernel::vfs::VFS_OK &&
          corruptFixtureIndexOwner(candidateImage, sourceFixture, 259u, 5u) &&
          kernel::vfs::mount_type("/system", candidateDevice,
              kernel::vfs::FS_TYPE_FAT32) != 0xFF,
          "mount the candidate with a checksum-valid foreign Edge32 owner id");
    check(gxNativeSqlValidateCatalog(GX_NATIVE_SQL_FIXTURE_R3AH_MULTILEVEL,
              &catalogImageError, &catalogCleanupError) ==
              GX_NATIVE_SQL_READ_CORRUPT_INDEX &&
          catalogImageError == GX_NATIVE_SQL_IMAGE_INVALID_ROOT &&
          catalogCleanupError == GX_NATIVE_SQL_IMAGE_OK && allVfsHandlesClosed(),
          "catalog root validation rejects a foreign Edge32 owner id");
    check(gxNativeSqlExecuteRead(&request, rows, GX_NATIVE_SQL_READ_MAX_RESULTS,
              &response) == GX_NATIVE_SQL_READ_CORRUPT_INDEX &&
          response.rowCount == 0 && allVfsHandlesClosed(),
          "Edge32 lookup rejects a checksum-valid foreign page owner");
    check(kernel::vfs::unmount("/system") == kernel::vfs::VFS_OK &&
          candidateImage.size() == candidateBefore.size(),
          "unmount checksum-valid Edge32 owner-corruption fixture");
    std::copy(candidateBefore.begin(), candidateBefore.end(), candidateImage.begin());
    check(remountCandidate(), "restore the approved fixture after Edge32 owner corruption");
    check(gxNativeSqlExecuteRead(&request, rows, GX_NATIVE_SQL_READ_MAX_RESULTS,
              &response) == GX_NATIVE_SQL_READ_OK && response.rowCount == 1 &&
          allVfsHandlesClosed(),
          "Edge32 lookup recovers after owner-validation rejection");

    check(kernel::vfs::unmount("/system") == kernel::vfs::VFS_OK &&
          mountSystem(missingImage, "r3aqmissing"),
          "mount missing-fixture image through production FAT VFS");
    check(gxNativeSqlExecuteRead(&request, rows, GX_NATIVE_SQL_READ_MAX_RESULTS,
              &response) == GX_NATIVE_SQL_READ_STORAGE_FAILURE &&
          allVfsHandlesClosed(),
          "missing trusted fixture is a closed storage failure");
    check(kernel::vfs::unmount("/system") == kernel::vfs::VFS_OK &&
          remountCandidate(),
          "restore the trusted fixture after missing-file test");
    check(gxNativeSqlExecuteRead(&request, rows, GX_NATIVE_SQL_READ_MAX_RESULTS,
              &response) == GX_NATIVE_SQL_READ_OK,
          "valid request succeeds immediately after missing-fixture failure");

    check(readVfsFile("/system/Apps/R3AI/MULTI.GDB", afterFixture) &&
          afterFixture == beforeFixture && afterFixture == sourceFixture,
          "fixture bytes remain unchanged after service requests");
    check(candidateBefore == candidateImage,
          "the mounted trusted candidate image was not modified");
    check(allVfsHandlesClosed(), "all service and VFS file handles are closed");

    std::cout << "nativeSqlR3aqServiceChecks=" << (checks - failures) << "/"
              << checks << "\n";
    return failures == 0 ? 0 : 1;
}
