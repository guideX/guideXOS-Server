#pragma once

#include "guidexos_nativeaot_pal_contract.h"

// SysV/native guideXOS code calls these C symbols.  The callback parameter is
// explicitly Microsoft x64 ABI, so GCC emits the shadow-space/register bridge
// at this narrow boundary instead of treating a Win64 pointer as SysV.
extern "C" uintptr_t
guidexos_nativeaot_pal_bridge_invoke_worker(
    guidexos_nativeaot_pal_win64_worker_entry entry,
    void* context);

extern "C" void
guidexos_nativeaot_pal_bridge_invoke_detach(
    guidexos_nativeaot_pal_win64_detach_callback callback,
    void* value);
