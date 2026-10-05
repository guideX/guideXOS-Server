#ifndef KERNEL_APP_LAUNCH_TARGET_RESOLVER_H
#define KERNEL_APP_LAUNCH_TARGET_RESOLVER_H

#include "app_launch_target.h"

namespace kernel {
namespace appmodel {

typedef void (*LaunchTargetDiagnosticWriter)(const char*);

enum class FileAssociationStatus : uint32_t {
    Resolved = 0,
    Unsupported = 1,
    InvalidPath = 2,
    PathTooLong = 3,
    Directory = 4,
    NotRegularFile = 5,
    NotFound = 6,
    IoFailure = 7,
};

struct FileAssociationResolution {
    FileAssociationStatus status;
    const char* applicationId;
};

static constexpr uint32_t kFileAssociationCapacity = 16u;
static constexpr uint32_t kFileAssociationPathCapacity = 96u;

// The normalized extension table is fixed, shared by native and managed
// File Explorer, and maps only regular files to canonical App Model IDs.
FileAssociationResolution resolveFileAssociation(
    const char* path, bool isRegularFile, bool isDirectory);
FileAssociationResolution resolveFileAssociationFromVfs(const char* path);
uint32_t fileAssociationUsedCount();
uint32_t fileAssociationCapacity();
uint32_t fileAssociationTableBytes();
bool runC164FileAssociationTests(uint32_t* outCases,
                                uint32_t* outFailureMask);

gxos::apps::LaunchTarget resolveLaunchTarget(const char* label);
void printLaunchTargetDiagnostic(const gxos::apps::LaunchTarget& target, LaunchTargetDiagnosticWriter write);
void printLaunchTargetDiagnostic(const char* label, LaunchTargetDiagnosticWriter write);
const char* legacyDispatchStringForLaunchTarget(const gxos::apps::LaunchTarget& target, const char** status, const char** reason);
void printLaunchTargetAdapterDiagnostic(const char* label, LaunchTargetDiagnosticWriter write);
void printLaunchTargetShadowSmokeDiagnostic(LaunchTargetDiagnosticWriter write);
void printLaunchTargetComparisonDiagnostic(LaunchTargetDiagnosticWriter write);
void printLaunchStorageDiagnostic(LaunchTargetDiagnosticWriter write);
void printLaunchStoragePreviewDiagnostic(LaunchTargetDiagnosticWriter write);
void printLaunchStoragePreviewComparisonDiagnostic(LaunchTargetDiagnosticWriter write);

} // namespace appmodel
} // namespace kernel

#endif // KERNEL_APP_LAUNCH_TARGET_RESOLVER_H
