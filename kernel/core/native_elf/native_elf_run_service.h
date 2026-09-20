//
// Bare-metal Developer Studio Run service.
//
// This is the kernel-side service boundary for the append-only NativeElf Run
// host-call capability. It owns one bounded operation and never exposes
// loader state or a raw entry address to the guest IDE.
//
#pragma once

#include "../../../sdk/include/guidexos/development_run.h"
#include "../../../sdk/include/guidexos/development_debug.h"
#include "native_elf_debug_trap.h"

namespace kernel {
namespace native_elf {
namespace NativeElfRunService {

gx_result prepare(const gx_development_run_request& request,
                  gx_development_run_handle* outHandle,
                  gx_development_run_snapshot* outSnapshot);
gx_result start(gx_development_run_handle handle);
gx_result pump(gx_development_run_handle handle);
gx_result poll(gx_development_run_handle handle,
               gx_development_run_snapshot* outSnapshot);
gx_result request_close(gx_development_run_handle handle);
gx_result cancel(gx_development_run_handle handle);
gx_result release(gx_development_run_handle handle);
gx_result debug(const gx_development_debug_request& request,
                gx_development_debug_snapshot* outSnapshot);
gx_result call_stack(const gx_development_debug_request& request,
                     gx_development_debug_call_stack* outResult);
gx_result inspect_variables(const gx_development_debug_request& request,
                            gx_development_debug_variables* outResult);
gx_result evaluate_expression(const gx_development_debug_request& request,
                              gx_development_debug_expression* outResult);

// Loader/debug-trap integration hooks. The loader supplies the validated
// entry address; the Run service installs the bounded user set or the legacy
// entry control point and owns semantic dispatch.
bool native_elf_debug_entry_breakpoint_requested();
bool native_elf_debug_breakpoint_target(uint64_t* targetAddress);
bool native_elf_debug_breakpoint_installed(uint64_t targetAddress, uint8_t originalByte);
bool native_elf_debug_install_breakpoints(uint64_t entryPoint);
bool native_elf_debug_breakpoint_exception(NativeElfDebugTrap::BreakpointContext* context);
bool native_elf_debug_single_step_exception(NativeElfDebugTrap::BreakpointContext* context);
void native_elf_debug_breakpoint_install_failed();

// Cooperative execution boundary used by the debugger pause request.  The
// AMD64 implementation is an assembly shim so the saved context includes all
// target GPRs, RIP, CS, RFLAGS, and the logical post-yield RSP.
extern "C" bool native_elf_scheduler_yield();

} // namespace NativeElfRunService
} // namespace native_elf
} // namespace kernel
