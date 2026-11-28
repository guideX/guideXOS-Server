#pragma once

namespace kernel {
namespace protected_task {

// Runs the bounded CPL3 exit diagnostic only in a build configured with
// CPL3_DIAGNOSTIC=1. Production application loading remains on the existing
// ring-0 Native ELF route.
bool run_cpl3_diagnostic();

// Runs processor-generated CPL3 exceptions and recovery only when both
// CPL3_DIAGNOSTIC=1 and CPL3_FAULT_RECOVERY_DIAGNOSTIC=1 are selected.
bool run_cpl3_fault_recovery_diagnostic();

} // namespace protected_task
} // namespace kernel
