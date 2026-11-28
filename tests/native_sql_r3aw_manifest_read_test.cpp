#include "kernel/block_device.h"
#include "kernel/fs_fat.h"
#include "kernel/native_sql_read_diagnostic.h"
#include "kernel/ramdisk.h"
#include "kernel/serial_debug.h"
#include "kernel/vfs.h"

#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

std::string gSerialCapture;

namespace kernel {
namespace serial {
void hosted_write_char(char c) { gSerialCapture.push_back(c); }
} // namespace serial
} // namespace kernel

namespace {

const char kManifestPath[] = "/system/Apps/R3AI/FIXTURE.MAN";
const uint64_t kManifestBytes = 12937;
const uint32_t kManifestChunk = 1024;
int gChecks = 0;
int gFailures = 0;

void check(bool condition, const char* name) {
    ++gChecks;
    if (condition) {
        std::cout << "PASS: " << name << "\n";
    } else {
        ++gFailures;
        std::cout << "FAIL: " << name << "\n";
    }
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
    bytes.clear();
    uint8_t chunk[4096];
    bool ok = true;
    for (;;) {
        const int32_t count = kernel::vfs::read(handle, chunk, sizeof(chunk));
        if (count < 0) {
            ok = false;
            break;
        }
        if (count == 0) break;
        bytes.insert(bytes.end(), chunk, chunk + count);
    }
    const bool closed = kernel::vfs::close(handle) == kernel::vfs::VFS_OK;
    return ok && closed;
}

bool sameBytes(const std::vector<uint8_t>& left,
               const std::vector<uint8_t>& right) {
    return left.size() == right.size() &&
        (left.empty() || std::memcmp(left.data(), right.data(), left.size()) == 0);
}

uint8_t findDevice(const char* name) {
    for (uint8_t i = 0; i < kernel::block::MAX_BLOCK_DEVICES; ++i) {
        const kernel::block::BlockDevice* device = kernel::block::get_device(i);
        if (device && std::strcmp(device->name, name) == 0) return i;
    }
    return 0xFF;
}

uint32_t countOpenHandles() {
    uint32_t count = 0;
    for (uint8_t i = 0; i < kernel::vfs::VFS_MAX_OPEN_FILES; ++i) {
        const kernel::vfs::FileHandle* handle = kernel::vfs::get_handle(i);
        if (handle && handle->open) ++count;
    }
    return count;
}

enum Fault {
    FaultNone,
    FaultMissing,
    FaultWrongPath,
    FaultStatIo,
    FaultNotRegular,
    FaultStatLength,
    FaultOpen,
    FaultInvalidHandle,
    FaultHandleContract,
    FaultFileSizeIo,
    FaultFileSizeLength,
    FaultPrematureEof,
    FaultReadIo,
    FaultExtraByte,
    FaultEofIo,
    FaultClose
};

struct FakeFile {
    Fault fault;
    kernel::vfs::FileInfo info;
    kernel::vfs::FileHandle handle;
    uint64_t position;
    uint32_t maxReadRequest;
    uint32_t readCalls;
    uint32_t closeCalls;
    bool sawExactPath;
    bool sawReadOnlyOpen;
};

kernel::vfs::Status fakeStat(void* context, const char* path,
                             kernel::vfs::FileInfo* info) {
    FakeFile& fake = *static_cast<FakeFile*>(context);
    fake.sawExactPath = path && std::strcmp(path, kManifestPath) == 0;
    if (!fake.sawExactPath || fake.fault == FaultWrongPath ||
        fake.fault == FaultMissing) return kernel::vfs::VFS_ERR_NOT_FOUND;
    if (fake.fault == FaultStatIo) return kernel::vfs::VFS_ERR_IO;
    *info = fake.info;
    if (fake.fault == FaultNotRegular) info->type = kernel::vfs::FILE_TYPE_DIRECTORY;
    if (fake.fault == FaultStatLength) --info->size;
    return kernel::vfs::VFS_OK;
}

uint8_t fakeOpen(void* context, const char* path, uint16_t flags) {
    FakeFile& fake = *static_cast<FakeFile*>(context);
    fake.sawExactPath = fake.sawExactPath && path &&
        std::strcmp(path, kManifestPath) == 0;
    fake.sawReadOnlyOpen = flags == kernel::vfs::OPEN_READ;
    if (!fake.sawExactPath || !fake.sawReadOnlyOpen || fake.fault == FaultOpen) {
        return 0xFF;
    }
    fake.handle.open = true;
    return 3;
}

const kernel::vfs::FileHandle* fakeGetHandle(void* context, uint8_t handle) {
    FakeFile& fake = *static_cast<FakeFile*>(context);
    if (handle != 3 || fake.fault == FaultInvalidHandle) return nullptr;
    if (fake.fault == FaultHandleContract) {
        fake.handle.flags = kernel::vfs::OPEN_READ | kernel::vfs::OPEN_WRITE;
    }
    return &fake.handle;
}

int64_t fakeFileSize(void* context, uint8_t handle) {
    FakeFile& fake = *static_cast<FakeFile*>(context);
    if (handle != 3 || fake.fault == FaultFileSizeIo) return -1;
    if (fake.fault == FaultFileSizeLength) return static_cast<int64_t>(kManifestBytes - 1);
    return static_cast<int64_t>(kManifestBytes);
}

int32_t fakeRead(void* context, uint8_t handle, void* buffer, uint32_t size) {
    FakeFile& fake = *static_cast<FakeFile*>(context);
    if (handle != 3 || !buffer || size == 0) return kernel::vfs::VFS_ERR_INVALID;
    ++fake.readCalls;
    if (size > fake.maxReadRequest) fake.maxReadRequest = size;
    if (fake.fault == FaultReadIo && fake.position >= 2048) return kernel::vfs::VFS_ERR_IO;
    if (fake.fault == FaultPrematureEof && fake.position >= 4096) return 0;
    if (fake.position >= kManifestBytes) {
        if (fake.fault == FaultExtraByte) return 1;
        if (fake.fault == FaultEofIo) return kernel::vfs::VFS_ERR_IO;
        return 0;
    }
    uint64_t remaining = kManifestBytes - fake.position;
    uint32_t count = static_cast<uint32_t>(remaining < size ? remaining : size);
    if (fake.fault == FaultNone && fake.position == 0 && count > 73) count = 73;
    std::memset(buffer, 0x5A, count);
    fake.position += count;
    return static_cast<int32_t>(count);
}

kernel::vfs::Status fakeClose(void* context, uint8_t handle) {
    FakeFile& fake = *static_cast<FakeFile*>(context);
    if (handle != 3) return kernel::vfs::VFS_ERR_INVALID;
    ++fake.closeCalls;
    fake.handle.open = false;
    return fake.fault == FaultClose ? kernel::vfs::VFS_ERR_IO : kernel::vfs::VFS_OK;
}

uint8_t fakeMountIndex(void*, const char* path) {
    if (!path || std::strcmp(path, kManifestPath) != 0) return 0xFF;
    return kernel::vfs::mount_index_for_path(path);
}

FakeFile makeFake(Fault fault) {
    FakeFile fake = {};
    fake.fault = fault;
    fake.info.type = kernel::vfs::FILE_TYPE_REGULAR;
    fake.info.size = kManifestBytes;
    fake.handle.open = false;
    fake.handle.flags = kernel::vfs::OPEN_READ;
    fake.handle.type = kernel::vfs::FILE_TYPE_REGULAR;
    fake.handle.mountIndex = kernel::vfs::mount_index_for_path(kManifestPath);
    fake.handle.size = kManifestBytes;
    return fake;
}

kernel::native_sql_read_diagnostic::ManifestReadTestOps makeOps(FakeFile& fake) {
    kernel::native_sql_read_diagnostic::ManifestReadTestOps ops = {
        &fake, fakeStat, fakeOpen, fakeGetHandle, fakeFileSize, fakeRead,
        fakeClose, fakeMountIndex
    };
    return ops;
}

void verifyInjectedFailure(const char* name, Fault fault,
                           kernel::native_sql_read_diagnostic::ManifestReadTestCode expected) {
    using namespace kernel::native_sql_read_diagnostic;
    FakeFile fake = makeFake(fault);
    const ManifestReadTestOps ops = makeOps(fake);
    gSerialCapture.clear();
    const ManifestReadTestReport report = read_manifest_for_test(ops);
    check(report.code == expected, name);
    check(run_after_vfs_mount_for_test(ops) == Failed,
          "manifest failure makes the combined qualification fail");
    check(gSerialCapture.find("SQL_NATIVE_R3AQ BLOCKED reason=MANIFEST_READ_FAILURE") != std::string::npos &&
          gSerialCapture.find("SQL_NATIVE_R3AQ END FAIL") != std::string::npos &&
          gSerialCapture.find("SQL_NATIVE_R3AQ REQUEST=POINT") == std::string::npos &&
          gSerialCapture.find("SQL_NATIVE_R3AQ END PASS") == std::string::npos,
          "failed manifest gate blocks SQL and cannot report PASS");
    check(fake.closeCalls == (fault == FaultMissing || fault == FaultWrongPath ||
          fault == FaultStatIo || fault == FaultNotRegular || fault == FaultStatLength ||
          fault == FaultOpen ? 0u : 2u),
          "failed read closes every successfully opened manifest handle");
    check(!fake.handle.open, "manifest failure releases its hosted handle state");
}

} // namespace

int main(int argc, char** argv) {
    std::cout.setf(std::ios::unitbuf);
    if (argc != 4) {
        std::cerr << "usage: native_sql_r3aw_manifest_read_test <ramdisk> <fixture> <trusted-manifest>\n";
        return 2;
    }

    std::vector<uint8_t> candidateImage, sourceFixture, sourceManifest;
    check(readHostFile(argv[1], candidateImage), "read authoritative R3AR RAM disk");
    check(readHostFile(argv[2], sourceFixture), "read trusted SQL fixture");
    check(readHostFile(argv[3], sourceManifest), "read trusted SQL manifest");
    if (gFailures) return 1;
    check(sourceFixture.size() == 250880 && sourceManifest.size() == kManifestBytes,
          "authoritative package lengths match fixture and manifest identities");
    if (candidateImage.size() < 512 || candidateImage.size() % 512 != 0) return 1;
    const std::vector<uint8_t> candidateBefore = candidateImage;

    kernel::block::init();
    kernel::fs_fat::init();
    kernel::ramdisk::init();
    kernel::vfs::init();
    const uint8_t ramdiskIndex = kernel::ramdisk::create_readonly_at(
        candidateImage.data(), candidateImage.size(), "r3aw-fat32");
    check(ramdiskIndex != 0xFF, "attach exact candidate as a read-only production RAM disk");
    if (ramdiskIndex == 0xFF) return 1;

    gSerialCapture.clear();
    check(kernel::native_sql_read_diagnostic::run_after_vfs_mount() ==
          kernel::native_sql_read_diagnostic::Failed,
          "pre-mount manifest failure is fail-closed");
    check(gSerialCapture.find("SQL_NATIVE_R3AW MANIFEST READ FAIL code=VFS_UNAVAILABLE") != std::string::npos &&
          gSerialCapture.find("SQL_NATIVE_R3AQ END PASS") == std::string::npos,
          "pre-mount failure emits an explicit manifest failure without SQL PASS");

    const uint8_t deviceIndex = findDevice("r3aw-fat32");
    check(deviceIndex != 0xFF && kernel::vfs::mount_type(
              "/system", deviceIndex, kernel::vfs::FS_TYPE_FAT32) != 0xFF,
          "production FAT32 driver mounts the R3AR image at the fixed VFS prefix");
    const kernel::vfs::MountPoint* mount = kernel::vfs::get_mount("/system");
    const kernel::block::BlockDevice* device = mount
        ? kernel::block::get_device(mount->blockDevIndex) : nullptr;
    check(mount && mount->readOnly && device && device->readFn && !device->writeFn,
          "production mount and backing RAM disk are read-only");

    std::vector<uint8_t> vfsManifest, vfsFixture;
    check(readVfsFile(kManifestPath, vfsManifest) && sameBytes(vfsManifest, sourceManifest),
          "production VFS returns byte-identical packaged FIXTURE.MAN");
    check(readVfsFile("/system/Apps/R3AI/MULTI.GDB", vfsFixture) &&
          sameBytes(vfsFixture, sourceFixture),
          "existing SQL fixture VFS reads remain byte-identical");
    check(vfsManifest.size() == kManifestBytes,
          "production VFS reads the full expected manifest length");

    std::vector<uint8_t> wrongIdentity = vfsManifest;
    if (!wrongIdentity.empty()) wrongIdentity[0] ^= 1;
    check(!sameBytes(wrongIdentity, sourceManifest),
          "hosted package identity preflight rejects same-length wrong manifest bytes");

    const uint32_t handlesBeforeRead = countOpenHandles();
    using namespace kernel::native_sql_read_diagnostic;
    const ManifestReadTestReport first = read_manifest_from_vfs_for_test();
    const ManifestReadTestReport second = read_manifest_from_vfs_for_test();
    check(first.code == ManifestReadTestOk && second.code == ManifestReadTestOk &&
          first.bytesRead == kManifestBytes && second.bytesRead == kManifestBytes &&
          first.eofObserved && second.eofObserved,
          "production manifest reader completes repeated sequential reads and EOF probes");
    check(first.readCalls > 2 && second.readCalls > 2,
          "production reader uses multiple bounded VFS reads");
    check(first.readCalls == 14 && second.readCalls == 14 &&
          gSerialCapture.find("SQL_NATIVE_R3AW MANIFEST BYTES_READ path=/system/Apps/R3AI/FIXTURE.MAN bytes=12937 read_calls=14 eof=1 close=PASS") != std::string::npos,
          "production serial marker records the complete bounded-read count and EOF probe");
    check(countOpenHandles() == handlesBeforeRead,
          "repeated production reads leave no VFS handles open");

    FakeFile shortChunks = makeFake(FaultNone);
    const ManifestReadTestReport shortChunkReport = read_manifest_for_test(makeOps(shortChunks));
    check(shortChunkReport.code == ManifestReadTestOk &&
          shortChunkReport.bytesRead == kManifestBytes && shortChunkReport.eofObserved &&
          shortChunkReport.readCalls > 2 && shortChunks.maxReadRequest <= kManifestChunk &&
          shortChunks.sawExactPath && shortChunks.sawReadOnlyOpen &&
          shortChunks.closeCalls == 1 && !shortChunks.handle.open,
          "partial positive reads continue within bounds and close cleanly at EOF");

    FakeFile partialFailure = makeFake(FaultPrematureEof);
    const ManifestReadTestOps partialOps = makeOps(partialFailure);
    gSerialCapture.clear();
    check(run_after_vfs_mount_for_test(partialOps) == Failed &&
          gSerialCapture.find("SQL_NATIVE_R3AW MANIFEST READ FAIL code=PREMATURE_EOF") != std::string::npos &&
          gSerialCapture.find("SQL_NATIVE_R3AQ REQUEST=POINT") == std::string::npos,
          "partial manifest read causes final qualification failure before SQL");

    verifyInjectedFailure("missing manifest path is rejected", FaultMissing, ManifestReadTestStatFailed);
    verifyInjectedFailure("incorrect directory or filename is rejected", FaultWrongPath, ManifestReadTestStatFailed);
    verifyInjectedFailure("VFS stat I/O failure is rejected", FaultStatIo, ManifestReadTestStatFailed);
    verifyInjectedFailure("directory at manifest path is rejected", FaultNotRegular, ManifestReadTestNotRegular);
    verifyInjectedFailure("unexpected stat length is rejected", FaultStatLength, ManifestReadTestUnexpectedLength);
    verifyInjectedFailure("manifest open failure is rejected", FaultOpen, ManifestReadTestOpenFailed);
    verifyInjectedFailure("invalid opened VFS handle is rejected", FaultInvalidHandle, ManifestReadTestInvalidHandle);
    verifyInjectedFailure("writable or mismounted handle is rejected", FaultHandleContract, ManifestReadTestHandleContract);
    verifyInjectedFailure("file_size I/O failure is rejected", FaultFileSizeIo, ManifestReadTestFileSizeFailed);
    verifyInjectedFailure("changed opened file length is rejected", FaultFileSizeLength, ManifestReadTestUnexpectedLength);
    verifyInjectedFailure("premature EOF after partial read is rejected", FaultPrematureEof, ManifestReadTestPrematureEof);
    verifyInjectedFailure("VFS read error is rejected", FaultReadIo, ManifestReadTestIoError);
    verifyInjectedFailure("data beyond expected size is rejected", FaultExtraByte, ManifestReadTestUnexpectedExtraByte);
    verifyInjectedFailure("EOF probe I/O failure is rejected", FaultEofIo, ManifestReadTestEofError);
    verifyInjectedFailure("close failure is rejected", FaultClose, ManifestReadTestCloseFailed);

    ManifestReadTestOps invalidOps = {};
    check(read_manifest_for_test(invalidOps).code == ManifestReadTestInvalidOperations,
          "incomplete VFS operation set is rejected");

    gSerialCapture.clear();
    check(kernel::native_sql_read_diagnostic::run_after_vfs_mount() ==
          kernel::native_sql_read_diagnostic::Passed,
          "real production manifest gate recovers after controlled failures and SQL passes");
    check(gSerialCapture.find("SQL_NATIVE_R3AW MANIFEST OPENED path=/system/Apps/R3AI/FIXTURE.MAN") != std::string::npos &&
          gSerialCapture.find("SQL_NATIVE_R3AW MANIFEST BYTES_READ path=/system/Apps/R3AI/FIXTURE.MAN bytes=12937") != std::string::npos &&
          gSerialCapture.find("SQL_NATIVE_R3AW MANIFEST READ PASS path=/system/Apps/R3AI/FIXTURE.MAN bytes=12937 eof=1 close=PASS") != std::string::npos &&
          gSerialCapture.find("SQL_NATIVE_R3AQ END PASS") != std::string::npos,
          "success markers prove manifest bytes and close precede final SQL PASS");
    check(candidateImage == candidateBefore,
          "manifest and SQL qualification make zero changes to backing RAM disk bytes");
    check(kernel::vfs::unmount("/system") == kernel::vfs::VFS_OK,
          "production VFS unmounts after qualification");

    std::cout << "nativeSqlR3awManifestReadChecks=" << (gChecks - gFailures)
              << "/" << gChecks << "\n";
    return gFailures == 0 ? 0 : 1;
}
