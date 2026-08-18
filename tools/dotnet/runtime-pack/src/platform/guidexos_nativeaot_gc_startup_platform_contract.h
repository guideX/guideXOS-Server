#pragma once

// Startup-only platform seam for the NativeAOT Workstation GC.  The PAL v1
// table remains the owner of thread/FLS/callback services.  This extension is
// deliberately separate because the already-proven PAL table has a frozen
// 232-byte layout and does not expose GC reservation/event operations.

#include <stdint.h>
#include <stddef.h>

#include "guidexos_nativeaot_pal_contract.h"

#define GUIDEXOS_NATIVEAOT_GC_PLATFORM_MAGIC UINT64_C(0x47584743504C5431)
#define GUIDEXOS_NATIVEAOT_GC_PLATFORM_ABI_VERSION 1u

#define GUIDEXOS_NATIVEAOT_GC_CAP_EVENTS (UINT64_C(1) << 0)
#define GUIDEXOS_NATIVEAOT_GC_CAP_VIRTUAL_MEMORY (UINT64_C(1) << 1)
#define GUIDEXOS_NATIVEAOT_GC_CAP_MEMORY_FACTS (UINT64_C(1) << 2)
#define GUIDEXOS_NATIVEAOT_GC_CAP_REQUIRED \
    (GUIDEXOS_NATIVEAOT_GC_CAP_EVENTS | \
     GUIDEXOS_NATIVEAOT_GC_CAP_VIRTUAL_MEMORY | \
     GUIDEXOS_NATIVEAOT_GC_CAP_MEMORY_FACTS)

typedef void* (GUIDEXOS_NATIVEAOT_PAL_CALL *guidexos_nativeaot_gc_create_event_hook)(
    uint32_t manual_reset, uint32_t initial_state);
typedef int32_t (GUIDEXOS_NATIVEAOT_PAL_CALL *guidexos_nativeaot_gc_event_hook)(
    void* handle);
typedef int32_t (GUIDEXOS_NATIVEAOT_PAL_CALL *guidexos_nativeaot_gc_wait_event_hook)(
    void* handle, uint32_t timeout_milliseconds);

typedef void* (GUIDEXOS_NATIVEAOT_PAL_CALL *guidexos_nativeaot_gc_reserve_hook)(
    uintptr_t size, uintptr_t alignment, uint32_t flags, uint16_t node);
typedef int32_t (GUIDEXOS_NATIVEAOT_PAL_CALL *guidexos_nativeaot_gc_memory_hook)(
    void* address, uintptr_t size, uint16_t node);
typedef int32_t (GUIDEXOS_NATIVEAOT_PAL_CALL *guidexos_nativeaot_gc_reset_hook)(
    void* address, uintptr_t size, uint32_t unlock);
typedef uint32_t (GUIDEXOS_NATIVEAOT_PAL_CALL *guidexos_nativeaot_gc_bool_hook)(void);

typedef uint64_t (GUIDEXOS_NATIVEAOT_PAL_CALL *guidexos_nativeaot_gc_u64_hook)(void);
typedef uint32_t (GUIDEXOS_NATIVEAOT_PAL_CALL *guidexos_nativeaot_gc_u32_hook)(void);
typedef uint64_t (GUIDEXOS_NATIVEAOT_PAL_CALL *guidexos_nativeaot_gc_limit_hook)(
    uint32_t* restricted);
typedef void (GUIDEXOS_NATIVEAOT_PAL_CALL *guidexos_nativeaot_gc_memory_status_hook)(
    uint64_t restricted_limit, uint32_t* memory_load,
    uint64_t* available_physical, uint64_t* available_page_file);
typedef void (GUIDEXOS_NATIVEAOT_PAL_CALL *guidexos_nativeaot_gc_native_continuation_hook)(
    uintptr_t recovered_rip, uintptr_t recovered_rsp, uintptr_t recovered_rbp);

#pragma pack(push, 8)
typedef struct guidexos_nativeaot_gc_startup_platform_table_v1 {
    uint64_t magic;
    uint32_t abi_version;
    uint32_t structure_size;
    uint64_t capability_bits;
    uint64_t installation_generation;

    guidexos_nativeaot_gc_create_event_hook create_event;
    guidexos_nativeaot_gc_event_hook set_event;
    guidexos_nativeaot_gc_event_hook reset_event;
    guidexos_nativeaot_gc_wait_event_hook wait_event;
    guidexos_nativeaot_gc_event_hook close_event;

    guidexos_nativeaot_gc_reserve_hook reserve;
    guidexos_nativeaot_gc_memory_hook commit;
    guidexos_nativeaot_gc_memory_hook decommit;
    guidexos_nativeaot_gc_memory_hook release;
    guidexos_nativeaot_gc_reset_hook reset;

    guidexos_nativeaot_gc_u32_hook page_size;
    guidexos_nativeaot_gc_u32_hook allocation_granularity;
    guidexos_nativeaot_gc_u64_hook virtual_memory_limit;
    guidexos_nativeaot_gc_limit_hook physical_memory_limit;
    guidexos_nativeaot_gc_memory_status_hook memory_status;

    uint64_t reserved[8];
} guidexos_nativeaot_gc_startup_platform_table_v1;
#pragma pack(pop)

#ifdef __cplusplus
static_assert(sizeof(guidexos_nativeaot_gc_startup_platform_table_v1) == 216,
              "GC startup platform table ABI drift");
static_assert(offsetof(guidexos_nativeaot_gc_startup_platform_table_v1,
                       create_event) == 32,
              "GC startup platform table offset drift");
#endif

#ifdef __cplusplus
extern "C" {
#endif

int32_t GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_gc_install_startup_platform_hooks(
    const guidexos_nativeaot_gc_startup_platform_table_v1* table);

int32_t GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_gc_uninstall_startup_platform_hooks(void);

uint64_t GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_gc_startup_platform_generation(void);

uintptr_t GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_gc_get_native_continuation_hook(void);

void* GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_gc_create_event(uint32_t manual_reset, uint32_t initial_state);
int32_t GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_gc_set_event(void* handle);
int32_t GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_gc_reset_event(void* handle);
int32_t GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_gc_wait_event(void* handle, uint32_t timeout_milliseconds);
int32_t GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_gc_close_event(void* handle);

void* GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_gc_reserve(uintptr_t size, uintptr_t alignment,
                              uint32_t flags, uint16_t node);
int32_t GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_gc_commit(void* address, uintptr_t size, uint16_t node);
int32_t GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_gc_decommit(void* address, uintptr_t size);
int32_t GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_gc_release(void* address, uintptr_t size);
int32_t GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_gc_reset(void* address, uintptr_t size, uint32_t unlock);
uint32_t GUIDEXOS_NATIVEAOT_PAL_CALL guidexos_nativeaot_gc_page_size(void);
uint32_t GUIDEXOS_NATIVEAOT_PAL_CALL guidexos_nativeaot_gc_allocation_granularity(void);
uint64_t GUIDEXOS_NATIVEAOT_PAL_CALL guidexos_nativeaot_gc_virtual_memory_limit(void);
uint64_t GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_gc_physical_memory_limit(uint32_t* restricted);
void GUIDEXOS_NATIVEAOT_PAL_CALL guidexos_nativeaot_gc_memory_status(
    uint64_t restricted_limit, uint32_t* memory_load,
    uint64_t* available_physical, uint64_t* available_page_file);

#ifdef __cplusplus
}
#endif
