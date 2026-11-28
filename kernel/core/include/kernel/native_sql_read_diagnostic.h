#ifndef KERNEL_NATIVE_SQL_READ_DIAGNOSTIC_H
#define KERNEL_NATIVE_SQL_READ_DIAGNOSTIC_H

#if defined(GXOS_HOSTED_TEST)
#include "kernel/vfs.h"
#endif

namespace kernel {
namespace native_sql_read_diagnostic {

enum Result {
    Failed = 0,
    Passed = 1
};

// Runs only in the explicit R3AI qualification build, after boot media has
// been attached and the Server has attempted its normal VFS mounts.
Result run_after_vfs_mount();

#if defined(GXOS_HOSTED_TEST)
enum ManifestReadTestCode {
    ManifestReadTestOk = 0,
    ManifestReadTestVfsUnavailable,
    ManifestReadTestInvalidOperations,
    ManifestReadTestStatFailed,
    ManifestReadTestNotRegular,
    ManifestReadTestUnexpectedLength,
    ManifestReadTestOpenFailed,
    ManifestReadTestInvalidHandle,
    ManifestReadTestHandleContract,
    ManifestReadTestFileSizeFailed,
    ManifestReadTestPrematureEof,
    ManifestReadTestIoError,
    ManifestReadTestUnexpectedExtraByte,
    ManifestReadTestEofError,
    ManifestReadTestCloseFailed
};

struct ManifestReadTestOps {
    void* context;
    vfs::Status (*stat)(void*, const char*, vfs::FileInfo*);
    uint8_t (*open)(void*, const char*, uint16_t);
    const vfs::FileHandle* (*getHandle)(void*, uint8_t);
    int64_t (*fileSize)(void*, uint8_t);
    int32_t (*read)(void*, uint8_t, void*, uint32_t);
    vfs::Status (*close)(void*, uint8_t);
    uint8_t (*mountIndexForPath)(void*, const char*);
};

struct ManifestReadTestReport {
    ManifestReadTestCode code;
    uint64_t bytesRead;
    uint32_t readCalls;
    bool eofObserved;
    bool closeAttempted;
    bool closeSucceeded;
};

ManifestReadTestReport read_manifest_for_test(const ManifestReadTestOps& ops);
ManifestReadTestReport read_manifest_from_vfs_for_test();
Result run_after_vfs_mount_for_test(const ManifestReadTestOps& ops);
#endif

} // namespace native_sql_read_diagnostic
} // namespace kernel

#endif
