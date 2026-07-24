#pragma once

// ABI-only boundary for the NativeAOT Windows PAL replacement.
//
// This header deliberately contains no guideXOS C++ class, STL type, Windows
// handle type, or compiler-specific object.  It is the only contract that a
// rebuilt MSVC NativeAOT PAL object may use to reach guideXOS facilities.

#include <stdint.h>

#if defined(_MSC_VER) && defined(_M_AMD64)
#define GUIDEXOS_NATIVEAOT_PAL_CALL __cdecl
#else
#define GUIDEXOS_NATIVEAOT_PAL_CALL
#endif

#define GUIDEXOS_NATIVEAOT_PAL_ABI_VERSION 1u

typedef void (GUIDEXOS_NATIVEAOT_PAL_CALL *guidexos_nativeaot_fls_detach_callback)(void* value);

typedef struct guidexos_nativeaot_fls_hooks {
    uint32_t size;
    uint32_t abi_version;
    void* context;
    uint32_t (GUIDEXOS_NATIVEAOT_PAL_CALL *alloc)(void* context,
                                                   guidexos_nativeaot_fls_detach_callback callback);
    int32_t (GUIDEXOS_NATIVEAOT_PAL_CALL *free_index)(void* context, uint32_t index);
    void* (GUIDEXOS_NATIVEAOT_PAL_CALL *get)(void* context, uint32_t index);
    int32_t (GUIDEXOS_NATIVEAOT_PAL_CALL *set)(void* context, uint32_t index, void* value);
} guidexos_nativeaot_fls_hooks;

typedef struct guidexos_nativeaot_pal_hooks {
    uint32_t size;
    uint32_t abi_version;
    void* context;

    uint64_t (GUIDEXOS_NATIVEAOT_PAL_CALL *current_thread_id)(void* context);
    int32_t (GUIDEXOS_NATIVEAOT_PAL_CALL *stack_bounds)(void* context,
                                                         uintptr_t* low,
                                                         uintptr_t* high,
                                                         uintptr_t* current);
    uint64_t (GUIDEXOS_NATIVEAOT_PAL_CALL *counter)(void* context);
    uint64_t (GUIDEXOS_NATIVEAOT_PAL_CALL *frequency)(void* context);
    int32_t (GUIDEXOS_NATIVEAOT_PAL_CALL *sleep_milliseconds)(void* context,
                                                               uint32_t milliseconds);
    int32_t (GUIDEXOS_NATIVEAOT_PAL_CALL *yield)(void* context);

    void* (GUIDEXOS_NATIVEAOT_PAL_CALL *virtual_alloc)(void* context,
                                                        void* preferred,
                                                        uintptr_t size,
                                                        uint32_t allocation_type,
                                                        uint32_t protection);
    int32_t (GUIDEXOS_NATIVEAOT_PAL_CALL *virtual_free)(void* context,
                                                         void* address,
                                                         uintptr_t size,
                                                         uint32_t free_type);
    int32_t (GUIDEXOS_NATIVEAOT_PAL_CALL *virtual_protect)(void* context,
                                                             void* address,
                                                             uintptr_t size,
                                                             uint32_t protection,
                                                             uint32_t* old_protection);
} guidexos_nativeaot_pal_hooks;

#ifdef __cplusplus
extern "C" {
#endif

// Installs process-scoped callbacks.  Passing null clears the hook table.
// Installation is explicit so an unconfigured PAL fails closed instead of
// silently reaching a Windows import or fabricating a platform result.
int32_t GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_pal_install_hooks(const guidexos_nativeaot_pal_hooks* hooks);

int32_t GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_pal_install_fls_hooks(const guidexos_nativeaot_fls_hooks* hooks);

int32_t GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_pal_current_thread_id(uint64_t* result);

int32_t GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_pal_stack_bounds(uintptr_t* low, uintptr_t* high, uintptr_t* current);

int32_t GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_pal_counter(uint64_t* result);

int32_t GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_pal_frequency(uint64_t* result);

int32_t GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_pal_sleep(uint32_t milliseconds);

int32_t GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_pal_yield(void);

uint32_t GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_pal_fls_alloc(guidexos_nativeaot_fls_detach_callback callback);

int32_t GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_pal_fls_free(uint32_t index);

void* GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_pal_fls_get(uint32_t index);

int32_t GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_pal_fls_set(uint32_t index, void* value);

#ifdef __cplusplus
} // extern "C"
#endif
