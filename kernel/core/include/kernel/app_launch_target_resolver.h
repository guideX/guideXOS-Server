#ifndef KERNEL_APP_LAUNCH_TARGET_RESOLVER_H
#define KERNEL_APP_LAUNCH_TARGET_RESOLVER_H

#include "app_launch_target.h"
#include "file_association_service.h"

namespace kernel {
namespace appmodel {

typedef void (*LaunchTargetDiagnosticWriter)(const char*);

// The normalized extension table is fixed, shared by native and managed
// File Explorer, and maps only regular files to canonical App Model IDs.
FileAssociationResolution resolveFileAssociation(
    const char* path, bool isRegularFile, bool isDirectory);
FileAssociationResolution resolveFileAssociationFromVfs(const char* path);
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
