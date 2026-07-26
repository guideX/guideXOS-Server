#pragma once

#include "native_app_runtime.h"

#include <string>

namespace gxos {
namespace apps {
namespace NativeBuildService {

struct NativeBuildRequest {
    std::string projectRoot;
    std::string projectId;
    std::string projectKind;
    std::string targetProfile;
    std::string buildSystem;
    std::string buildScript;
    std::string expectedArtifact;
    std::string configuration;
};

gx_result Start(NativeAppRuntimeContext& context, const NativeBuildRequest& request, gx_build_handle* outHandle);
gx_result Poll(NativeAppRuntimeContext& context, gx_build_handle handle, gx_build_snapshot* outSnapshot);
gx_result Release(NativeAppRuntimeContext& context, gx_build_handle handle);
void CancelForRuntime(uint64_t runtimeId);

} // namespace NativeBuildService
} // namespace apps
} // namespace gxos
