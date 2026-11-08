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
// Return the FAT-backed VFS path used to store a logical ELF artifact. Long
// logical names are mapped to a deterministic 8.3 basename; short names pass
// through unchanged.
bool artifact_storage_path(const char* logicalPath, char* storagePath, uint32_t capacity);

} // namespace BareMetalBuildService
} // namespace compiler
} // namespace kernel
