#pragma once

#include <stddef.h>
#include <stdint.h>

namespace kernel {
namespace nativeaot_pal_qemu_test {

void run(const uint8_t* artifact, size_t artifactSize,
         uintptr_t installAddress, uintptr_t mainAddress,
         uintptr_t uninstallAddress);

#if defined(GXOS_NATIVEAOT_GC_STARTUP_QEMU_TEST) || defined(GXOS_NATIVEAOT_GC_FIRST_ALLOCATION_QEMU_TEST) || defined(GXOS_NATIVEAOT_GC_FIRST_REFILL_QEMU_TEST) || defined(GXOS_NATIVEAOT_GC_SEGMENT_BOUNDARY_QEMU_TEST) || defined(GXOS_NATIVEAOT_GC_SINGLE_THREAD_SUSPEND_EE_QEMU_TEST)
void runStartup(const uint8_t* artifact, size_t artifactSize,
                uintptr_t installPalAddress, uintptr_t installTableAddress,
                uintptr_t installPlatformAddress, uintptr_t mainAddress,
                uintptr_t stateAddress, uintptr_t preGcStateAddress,
                uintptr_t allocationCountAddress, uintptr_t lastAllocationSizeAddress,
                uintptr_t diagnosticStageAddress,
                uint64_t generation);

void runFirstRealAllocation(
    const uint8_t* artifact, size_t artifactSize,
    uintptr_t installPalAddress, uintptr_t installTableAddress,
    uintptr_t installPlatformAddress, uintptr_t startupMainAddress,
    uintptr_t getStateAddress, uintptr_t getPreGcStateAddress,
    uintptr_t getAllocationCountAddress, uintptr_t getLastAllocationSizeAddress,
    uintptr_t getDiagnosticStageAddress, uintptr_t managedMainAddress,
    uintptr_t finalizeAddress, uintptr_t getDiagnosticsAddress,
    uint64_t generation, uintptr_t beginExperimentAddress = 0);

#endif

} // namespace nativeaot_pal_qemu_test
} // namespace kernel
