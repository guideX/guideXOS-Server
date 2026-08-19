#pragma once

#include <stdint.h>

#include "tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_native_unwind_contract.h"

#ifdef __cplusplus
extern "C" {
#endif

// Registers the linked guideXOS kernel image exactly once.  The descriptor is
// derived only from linker bookends and is fully validated before publication.
int32_t GUIDEXOS_NATIVEAOT_PAL_CALL
guideXosNativeUnwindRegisterKernelModule(void);

// Bounded, read-only PC lookup used by the NativeAOT platform hook.
int32_t GUIDEXOS_NATIVEAOT_PAL_CALL
guideXosNativeUnwindLookup(
    uintptr_t control_pc,
    guidexos_nativeaot_native_unwind_lookup_result* result);

const guidexos_nativeaot_native_unwind_module*
guideXosNativeUnwindGetKernelModule(void);

uint32_t guideXosNativeUnwindRegistryCapacity(void);
uint32_t guideXosNativeUnwindRegistryCount(void);

// Startup-only focused coverage for lookup edges, malformed descriptor
// rejection, and discovery of a second genuine nontrivial native function.
int32_t GUIDEXOS_NATIVEAOT_PAL_CALL
guideXosNativeUnwindValidateFocusedCoverage(uint32_t* second_function_index);

#ifdef __cplusplus
}
#endif
