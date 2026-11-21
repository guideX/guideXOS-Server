#pragma once

namespace kernel {
namespace protected_task {

// Runs the fixed, non-faulting CPL3 diagnostic only in a build configured with
// CPL3_DIAGNOSTIC=1. Production application loading remains on the existing
// ring-0 Native ELF route.
bool run_cpl3_diagnostic();

} // namespace protected_task
} // namespace kernel
