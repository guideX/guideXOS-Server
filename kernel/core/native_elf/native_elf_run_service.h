//
// Bare-metal Developer Studio Run service.
//
// This is the kernel-side service boundary for the append-only NativeElf Run
// host-call capability. It owns one bounded operation and never exposes
// loader state or a raw entry address to the guest IDE.
//
#pragma once

#include "../../../sdk/include/guidexos/development_run.h"

namespace kernel {
namespace native_elf {
namespace NativeElfRunService {

gx_result prepare(const gx_development_run_request& request,
                  gx_development_run_handle* outHandle,
                  gx_development_run_snapshot* outSnapshot);
gx_result start(gx_development_run_handle handle);
gx_result poll(gx_development_run_handle handle,
               gx_development_run_snapshot* outSnapshot);
gx_result request_close(gx_development_run_handle handle);
gx_result release(gx_development_run_handle handle);

} // namespace NativeElfRunService
} // namespace native_elf
} // namespace kernel
