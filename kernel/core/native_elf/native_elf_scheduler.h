//
// Cooperative ownership bridge for the bare-metal NativeElf Run service.
//
#pragma once

namespace kernel {
namespace native_elf {
namespace NativeElfRunService {

// Called by the running NativeElf application at a cooperative boundary.  It
// returns only after the owner has been scheduled again, or false when no
// asynchronous NativeElf session owns the current execution context.
bool native_elf_scheduler_yield();

// Called by the kernel/owner side to give one bounded execution opportunity to
// the active NativeElf session.  It never blocks waiting for application exit.
bool native_elf_scheduler_pump();

// True only while the active run owns the current context.  This prevents a
// target-side desktop tick from recursively scheduling itself.
bool native_elf_scheduler_in_target();

} // namespace NativeElfRunService
} // namespace native_elf
} // namespace kernel
