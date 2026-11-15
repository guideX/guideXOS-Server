// Synchronous bounded build service exposed to the NativeElf Developer Studio
// ABI. The service compiles saved VFS source; it never invokes a host process.
#pragma once

#include "guidexos/build.h"
#include <stdint.h>

namespace kernel {
namespace compiler {
namespace BareMetalBuildService {

struct OwnedArtifactView {
    uint64_t buildGeneration;
    uint64_t ownerIdentity;
    uint64_t artifactSize;
    uint64_t packageGeneration;
    uint8_t el0IntegerProfileValid;
    const uint8_t* bytes;
    char projectId[96];
    char artifactPath[GX_BUILD_MAX_ARTIFACT_PATH_BYTES];
    char packagePath[GX_BUILD_MAX_ARTIFACT_PATH_BYTES];
    char architecture[32];
    char abi[96];
    char sourceSha256[GX_BUILD_MAX_SHA256_BYTES];
    char artifactSha256[GX_BUILD_MAX_SHA256_BYTES];
};

gx_result start(const gx_build_request* request, gx_build_handle* outHandle);
gx_result start_for_owner(const gx_build_request* request, gx_build_handle* outHandle,
                          uint64_t owner_identity);
gx_result poll(gx_build_handle handle, gx_build_snapshot* outSnapshot);
gx_result release(gx_build_handle handle);
// Reads the latest immutable, VFS-readback Build snapshot. All supplied
// fields must match exactly, including the generation. The returned bytes are
// kernel-owned and remain stable until the next Build invocation starts.
bool current_artifact(uint64_t owner_identity, uint64_t build_generation,
                      const char* project_id, const char* artifact_path,
                      uint64_t artifact_size, const char* architecture,
                      const char* abi, const char* package_path,
                      uint64_t package_generation, const char* artifact_sha256,
                      OwnedArtifactView* output);
// Return the FAT-backed VFS path used to store a logical ELF artifact. Long
// logical names are mapped to a deterministic 8.3 basename; short names pass
// through unchanged.
bool artifact_storage_path(const char* logicalPath, char* storagePath, uint32_t capacity);

} // namespace BareMetalBuildService
} // namespace compiler
} // namespace kernel
