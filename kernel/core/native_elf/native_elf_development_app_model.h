//
// Bounded bare-metal NativeElf development application registration.
//
// This is the NativeElf bridge for the kernel's App Model boundary.  It is an
// in-memory registration only: it never scans, persists, or launches an
// arbitrary path.  The production NativeElf loader remains the launch path.
//
#pragma once

#include "../../../sdk/include/guidexos/development_run.h"

namespace kernel {
namespace native_elf {
namespace NativeElfDevelopmentAppModel {

enum class RegistrationResult : uint32_t {
    Registered = 0,
    Invalid = 1,
    ApplicationIdInUse = 2,
    DeploymentAlreadyActive = 3,
};

struct RegistrationRequest {
    gx_development_run_handle handle;
    uint64_t generation;
    const char* applicationId;
    const char* displayName;
    const char* projectRoot;
    const char* artifactPath;
    uint64_t artifactSize;
    const char* artifactSha256;
};

struct Registration {
    gx_development_run_handle handle;
    uint64_t generation;
    uint64_t artifactSize;
    char applicationId[GX_DEVELOPMENT_RUN_MAX_APP_ID_BYTES];
    char displayName[GX_DEVELOPMENT_RUN_MAX_DISPLAY_NAME_BYTES];
    char projectRoot[GX_DEVELOPMENT_RUN_MAX_PROJECT_ROOT_BYTES];
    char artifactPath[GX_DEVELOPMENT_RUN_MAX_PATH_BYTES];
    char artifactSha256[GX_DEVELOPMENT_RUN_MAX_SHA256_BYTES];
};

RegistrationResult register_temporary(const RegistrationRequest& request,
                                      Registration* outRegistration);
bool resolve_temporary(gx_development_run_handle handle,
                       uint64_t generation,
                       const char* applicationId,
                       Registration* outRegistration);
bool unregister_temporary(gx_development_run_handle handle,
                          uint64_t generation,
                          const char* applicationId);
bool has_active_registration();

} // namespace NativeElfDevelopmentAppModel
} // namespace native_elf
} // namespace kernel
