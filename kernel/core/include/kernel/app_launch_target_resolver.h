#ifndef KERNEL_APP_LAUNCH_TARGET_RESOLVER_H
#define KERNEL_APP_LAUNCH_TARGET_RESOLVER_H

#include "app_launch_target.h"

namespace kernel {
namespace appmodel {

typedef void (*LaunchTargetDiagnosticWriter)(const char*);

gxos::apps::LaunchTarget resolveLaunchTarget(const char* label);
void printLaunchTargetDiagnostic(const gxos::apps::LaunchTarget& target, LaunchTargetDiagnosticWriter write);
void printLaunchTargetDiagnostic(const char* label, LaunchTargetDiagnosticWriter write);
const char* legacyDispatchStringForLaunchTarget(const gxos::apps::LaunchTarget& target, const char** status, const char** reason);
void printLaunchTargetAdapterDiagnostic(const char* label, LaunchTargetDiagnosticWriter write);
void printLaunchTargetComparisonDiagnostic(LaunchTargetDiagnosticWriter write);
void printLaunchStorageDiagnostic(LaunchTargetDiagnosticWriter write);
void printLaunchStoragePreviewDiagnostic(LaunchTargetDiagnosticWriter write);
void printLaunchStoragePreviewComparisonDiagnostic(LaunchTargetDiagnosticWriter write);

} // namespace appmodel
} // namespace kernel

#endif // KERNEL_APP_LAUNCH_TARGET_RESOLVER_H
