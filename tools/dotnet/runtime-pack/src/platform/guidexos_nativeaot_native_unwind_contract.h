#pragma once

// Stable, pointer-free ABI between the guideXOS kernel native-unwind module
// registry and the NativeAOT PAL/runtime image.  The provider is deliberately
// not an ICodeManager and does not expose managed metadata.

#include <stdint.h>

#include "guidexos_nativeaot_pal_contract.h"

#define GUIDEXOS_NATIVEAOT_NATIVE_UNWIND_MAGIC UINT64_C(0x47584E554E573031)
#define GUIDEXOS_NATIVEAOT_NATIVE_UNWIND_ABI_VERSION 1u
#define GUIDEXOS_NATIVEAOT_NATIVE_UNWIND_ENCODING_BASE_RVA 1u

#pragma pack(push, 8)
typedef struct guidexos_nativeaot_native_unwind_module {
    uint64_t module_base;
    uint64_t executable_start;
    uint64_t executable_end;
    uint64_t pdata_start;
    uint64_t pdata_end;
    uint64_t xdata_start;
    uint64_t xdata_end;
    uint64_t runtime_function_table;
    uint32_t runtime_function_count;
    uint32_t table_sorted_by_begin;
    uint32_t encoding;
    uint32_t validation_state;
} guidexos_nativeaot_native_unwind_module;

typedef struct guidexos_nativeaot_native_unwind_lookup_result {
    uint64_t module_base;
    uint64_t executable_start;
    uint64_t executable_end;
    uint64_t pdata_start;
    uint64_t pdata_end;
    uint64_t xdata_start;
    uint64_t xdata_end;
    uint64_t runtime_function;
    uint64_t unwind_info;
    uint32_t begin_address;
    uint32_t end_address;
    uint32_t unwind_data;
    uint32_t table_index;
    uint32_t unwind_version;
    uint32_t unwind_flags;
    uint32_t prologue_size;
    uint32_t unwind_code_count;
    uint32_t frame_register;
    uint32_t frame_offset;
} guidexos_nativeaot_native_unwind_lookup_result;
#pragma pack(pop)

typedef int32_t (GUIDEXOS_NATIVEAOT_PAL_CALL *
    guidexos_nativeaot_native_unwind_lookup_hook)(
        uintptr_t control_pc,
        guidexos_nativeaot_native_unwind_lookup_result* result);

#define GUIDEXOS_NATIVEAOT_NATIVE_UNWIND_PLATFORM_RESERVED_INDEX 1u

typedef struct guidexos_nativeaot_runtime_function {
    uint32_t begin_address;
    uint32_t end_address;
    uint32_t unwind_data;
} guidexos_nativeaot_runtime_function;

#ifdef __cplusplus
static_assert(sizeof(guidexos_nativeaot_runtime_function) == 12,
              "native unwind runtime-function ABI drift");
static_assert(sizeof(guidexos_nativeaot_native_unwind_module) == 80,
              "native unwind module ABI drift");
static_assert(sizeof(guidexos_nativeaot_native_unwind_lookup_result) == 112,
              "native unwind lookup ABI drift");
#endif
