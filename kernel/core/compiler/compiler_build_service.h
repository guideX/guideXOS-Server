// Synchronous bounded build service exposed to the NativeElf Developer Studio
// ABI. The service compiles saved VFS source; it never invokes a host process.
#pragma once

#include "guidexos/build.h"

namespace kernel {
namespace compiler {
namespace BareMetalBuildService {

gx_result start(const gx_build_request* request, gx_build_handle* outHandle);
gx_result poll(gx_build_handle handle, gx_build_snapshot* outSnapshot);
gx_result release(gx_build_handle handle);

} // namespace BareMetalBuildService
} // namespace compiler
} // namespace kernel
