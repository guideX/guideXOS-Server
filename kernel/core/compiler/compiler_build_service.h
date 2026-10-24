//
// Bare-metal Developer Studio build service.
//
// This is a deliberately bounded, synchronous kernel job. It consumes the
// public build request/snapshot shape, reads project sources through VFS, and
// never starts a host process or invokes a host toolchain.
//
#pragma once

#include "../../../sdk/include/guidexos/build.h"

namespace kernel {
namespace compiler {
namespace BareMetalBuildService {

gx_result start(const gx_build_request* request, gx_build_handle* outHandle);
gx_result poll(gx_build_handle handle, gx_build_snapshot* outSnapshot);
gx_result release(gx_build_handle handle);

// NativeElf callbacks use an identity assigned by the trusted loader. The
// owner-aware calls keep a guessed public handle from crossing app lifetimes.
gx_result start_owned(const gx_build_request* request, uint64_t ownerIdentity,
                      gx_build_handle* outHandle);
gx_result poll_owned(uint64_t ownerIdentity, gx_build_handle handle,
                     gx_build_snapshot* outSnapshot);
gx_result release_owned(uint64_t ownerIdentity, gx_build_handle handle);
gx_result owner_closed(uint64_t ownerIdentity);
gx_result shutdown();

} // namespace BareMetalBuildService
} // namespace compiler
} // namespace kernel
