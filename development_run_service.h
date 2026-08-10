#pragma once

#include "native_app_runtime.h"

#include <string>

namespace gxos {
namespace apps {
namespace DevelopmentRunService {

gx_result Prepare(NativeAppRuntimeContext& owner, const gx_development_run_request& request,
                  gx_development_run_handle* outHandle, gx_development_run_snapshot* outSnapshot);
gx_result Start(NativeAppRuntimeContext& owner, gx_development_run_handle handle);
gx_result Poll(NativeAppRuntimeContext& owner, gx_development_run_handle handle,
               gx_development_run_snapshot* outSnapshot);
gx_result RequestClose(NativeAppRuntimeContext& owner, gx_development_run_handle handle);
gx_result Release(NativeAppRuntimeContext& owner, gx_development_run_handle handle);
gx_result Debug(NativeAppRuntimeContext& owner, const gx_development_debug_request& request,
                gx_development_debug_snapshot* outSnapshot);

// Called by NativeAppRuntime owner teardown and Server shutdown. This removes
// temporary App Model registrations even when the Studio does not release its
// handle normally. It never kills an unrelated or arbitrary process.
void ReleaseOwner(uint64_t ownerRuntimeId);
void Shutdown();

} // namespace DevelopmentRunService
} // namespace apps
} // namespace gxos
