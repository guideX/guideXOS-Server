#pragma once

// ABI-only boundary for the NativeAOT Windows PAL replacement.
//
// This header deliberately contains no guideXOS C++ class, STL type, Windows
// handle type, or compiler-specific object.  It is the only contract that a
// rebuilt MSVC NativeAOT PAL object may use to reach guideXOS facilities.

#include <stdint.h>

#if defined(_MSC_VER) && defined(_M_AMD64)
#define GUIDEXOS_NATIVEAOT_PAL_CALL __cdecl
#elif defined(__GNUC__) && defined(__x86_64__)
#define GUIDEXOS_NATIVEAOT_PAL_CALL __attribute__((ms_abi))
#else
#define GUIDEXOS_NATIVEAOT_PAL_CALL
#endif

#define GUIDEXOS_NATIVEAOT_PAL_ABI_VERSION 2u

#define GUIDEXOS_NATIVEAOT_PAL_HOOK_MAGIC UINT64_C(0x475850414C483031)
#define GUIDEXOS_NATIVEAOT_PAL_HOOK_ABI_VERSION 1u
#define GUIDEXOS_NATIVEAOT_PAL_CAP_CURRENT_THREAD (UINT64_C(1) << 0)
#define GUIDEXOS_NATIVEAOT_PAL_CAP_STACK_BOUNDS (UINT64_C(1) << 1)
#define GUIDEXOS_NATIVEAOT_PAL_CAP_FLS (UINT64_C(1) << 2)
#define GUIDEXOS_NATIVEAOT_PAL_CAP_WORKER_THREAD (UINT64_C(1) << 3)
#define GUIDEXOS_NATIVEAOT_PAL_CAP_WIN64_CALLBACK (UINT64_C(1) << 4)
#define GUIDEXOS_NATIVEAOT_PAL_CAP_THREADSTORE (UINT64_C(1) << 5)
#define GUIDEXOS_NATIVEAOT_PAL_CAP_TIMING (UINT64_C(1) << 6)
#define GUIDEXOS_NATIVEAOT_PAL_CAP_SLEEP_YIELD (UINT64_C(1) << 7)
#define GUIDEXOS_NATIVEAOT_PAL_CAP_FAIL_FAST (UINT64_C(1) << 8)
#define GUIDEXOS_NATIVEAOT_PAL_CAP_REQUIRED ( \
    GUIDEXOS_NATIVEAOT_PAL_CAP_CURRENT_THREAD | \
    GUIDEXOS_NATIVEAOT_PAL_CAP_STACK_BOUNDS | \
    GUIDEXOS_NATIVEAOT_PAL_CAP_FLS | \
    GUIDEXOS_NATIVEAOT_PAL_CAP_WORKER_THREAD | \
    GUIDEXOS_NATIVEAOT_PAL_CAP_WIN64_CALLBACK | \
    GUIDEXOS_NATIVEAOT_PAL_CAP_THREADSTORE | \
    GUIDEXOS_NATIVEAOT_PAL_CAP_TIMING | \
    GUIDEXOS_NATIVEAOT_PAL_CAP_SLEEP_YIELD | \
    GUIDEXOS_NATIVEAOT_PAL_CAP_FAIL_FAST)

// Values crossing this boundary are intentionally not Win32 BOOL/HANDLE
// types.  A callback returns 0 for failure and a nonzero value for success;
// opaque handles are represented as raw pointers or uintptr_t values and are
// owned by the implementation that created them.
typedef uintptr_t guidexos_nativeaot_pal_opaque_handle;

typedef uintptr_t (GUIDEXOS_NATIVEAOT_PAL_CALL *guidexos_nativeaot_thread_entry)(void* context);

typedef void (GUIDEXOS_NATIVEAOT_PAL_CALL *guidexos_nativeaot_fls_detach_callback)(void* value);

typedef void (GUIDEXOS_NATIVEAOT_PAL_CALL *guidexos_nativeaot_pal_win64_detach_callback)(void* value);
typedef uintptr_t (GUIDEXOS_NATIVEAOT_PAL_CALL *guidexos_nativeaot_pal_win64_worker_entry)(void* context);

#pragma pack(push, 8)
typedef struct guidexos_nativeaot_pal_stack_bounds_value {
    uint64_t low;
    uint64_t high;
    uint64_t current;
} guidexos_nativeaot_pal_stack_bounds_value;

typedef struct guidexos_nativeaot_pal_worker_handle {
    uint32_t slot;
    uint32_t generation;
    uint32_t domain_generation;
    uint32_t reserved;
} guidexos_nativeaot_pal_worker_handle;

typedef uint64_t (GUIDEXOS_NATIVEAOT_PAL_CALL *guidexos_nativeaot_pal_current_thread_id_hook)(void);
typedef int32_t (GUIDEXOS_NATIVEAOT_PAL_CALL *guidexos_nativeaot_pal_query_stack_bounds_hook)(guidexos_nativeaot_pal_stack_bounds_value* bounds);
typedef uint32_t (GUIDEXOS_NATIVEAOT_PAL_CALL *guidexos_nativeaot_pal_fls_allocate_hook)(guidexos_nativeaot_pal_win64_detach_callback callback);
typedef int32_t (GUIDEXOS_NATIVEAOT_PAL_CALL *guidexos_nativeaot_pal_fls_release_hook)(uint32_t index);
typedef void* (GUIDEXOS_NATIVEAOT_PAL_CALL *guidexos_nativeaot_pal_fls_get_hook)(uint32_t index);
typedef int32_t (GUIDEXOS_NATIVEAOT_PAL_CALL *guidexos_nativeaot_pal_fls_set_hook)(uint32_t index, void* value);
typedef int32_t (GUIDEXOS_NATIVEAOT_PAL_CALL *guidexos_nativeaot_pal_create_worker_hook)(guidexos_nativeaot_pal_win64_worker_entry entry, void* context, uintptr_t stack_size, guidexos_nativeaot_pal_worker_handle* handle);
typedef int32_t (GUIDEXOS_NATIVEAOT_PAL_CALL *guidexos_nativeaot_pal_join_worker_hook)(guidexos_nativeaot_pal_worker_handle handle, uint32_t timeout_milliseconds, uintptr_t* result);
typedef int32_t (GUIDEXOS_NATIVEAOT_PAL_CALL *guidexos_nativeaot_pal_destroy_worker_handle_hook)(guidexos_nativeaot_pal_worker_handle handle);
typedef uint64_t (GUIDEXOS_NATIVEAOT_PAL_CALL *guidexos_nativeaot_pal_counter_hook)(void);
typedef uint64_t (GUIDEXOS_NATIVEAOT_PAL_CALL *guidexos_nativeaot_pal_frequency_hook)(void);
typedef uint64_t (GUIDEXOS_NATIVEAOT_PAL_CALL *guidexos_nativeaot_pal_monotonic_milliseconds_hook)(void);
typedef void (GUIDEXOS_NATIVEAOT_PAL_CALL *guidexos_nativeaot_pal_sleep_milliseconds_hook)(uint32_t milliseconds);
typedef void (GUIDEXOS_NATIVEAOT_PAL_CALL *guidexos_nativeaot_pal_yield_hook)(void);
typedef void (GUIDEXOS_NATIVEAOT_PAL_CALL *guidexos_nativeaot_pal_fail_fast_hook)(uint32_t reason, uintptr_t detail);

typedef struct guidexos_nativeaot_pal_hook_table_v1 {
    uint64_t magic;
    uint32_t abi_version;
    uint32_t structure_size;
    uint64_t capability_bits;
    uint64_t installation_generation;
    uint64_t artifact_base;
    uint64_t artifact_size;
    guidexos_nativeaot_pal_current_thread_id_hook current_thread_id;
    guidexos_nativeaot_pal_query_stack_bounds_hook query_current_stack_bounds;
    guidexos_nativeaot_pal_fls_allocate_hook fls_allocate;
    guidexos_nativeaot_pal_fls_release_hook fls_release;
    guidexos_nativeaot_pal_fls_get_hook fls_get;
    guidexos_nativeaot_pal_fls_set_hook fls_set;
    guidexos_nativeaot_pal_create_worker_hook create_worker;
    guidexos_nativeaot_pal_join_worker_hook join_worker;
    guidexos_nativeaot_pal_destroy_worker_handle_hook destroy_worker_handle;
    guidexos_nativeaot_pal_counter_hook query_counter;
    guidexos_nativeaot_pal_frequency_hook query_counter_frequency;
    guidexos_nativeaot_pal_monotonic_milliseconds_hook monotonic_milliseconds;
    guidexos_nativeaot_pal_sleep_milliseconds_hook sleep_milliseconds;
    guidexos_nativeaot_pal_yield_hook yield_thread;
    guidexos_nativeaot_pal_fail_fast_hook fail_fast;
    uint64_t reserved[8];
} guidexos_nativeaot_pal_hook_table_v1;
#pragma pack(pop)

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

    int32_t (GUIDEXOS_NATIVEAOT_PAL_CALL *get_last_error)(void* context);
    void (GUIDEXOS_NATIVEAOT_PAL_CALL *set_last_error)(void* context,
                                                        int32_t value);
    void* (GUIDEXOS_NATIVEAOT_PAL_CALL *current_process)(void* context);
    void* (GUIDEXOS_NATIVEAOT_PAL_CALL *current_thread)(void* context);
    int32_t (GUIDEXOS_NATIVEAOT_PAL_CALL *duplicate_handle)(
        void* context,
        void* source_process,
        void* source_handle,
        void* target_process,
        void** target_handle,
        uint32_t access,
        int32_t inherit,
        uint32_t options);

    void* (GUIDEXOS_NATIVEAOT_PAL_CALL *static_module_from_pointer)(
        void* context,
        const void* pointer);
    void* (GUIDEXOS_NATIVEAOT_PAL_CALL *static_resolve)(
        void* context,
        void* module,
        const char* name);

    int32_t (GUIDEXOS_NATIVEAOT_PAL_CALL *create_thread)(
        void* context,
        guidexos_nativeaot_thread_entry entry,
        void* entry_context,
        uint32_t stack_size,
        int32_t high_priority,
        guidexos_nativeaot_pal_opaque_handle* result);
    int32_t (GUIDEXOS_NATIVEAOT_PAL_CALL *join_thread)(
        void* context,
        guidexos_nativeaot_pal_opaque_handle handle,
        uint32_t timeout_milliseconds,
        uintptr_t* result);
    int32_t (GUIDEXOS_NATIVEAOT_PAL_CALL *close_thread)(
        void* context,
        guidexos_nativeaot_pal_opaque_handle handle);

    void* (GUIDEXOS_NATIVEAOT_PAL_CALL *create_event)(
        void* context,
        int32_t manual_reset,
        int32_t initial_state);
    int32_t (GUIDEXOS_NATIVEAOT_PAL_CALL *wait_any)(
        void* context,
        uint32_t timeout_milliseconds,
        uint32_t count,
        void* const* handles,
        int32_t alertable);
    int32_t (GUIDEXOS_NATIVEAOT_PAL_CALL *close_event)(
        void* context,
        void* handle);

    // This callback must not return.  Implementations are expected to enter
    // the guideXOS fail-fast path and preserve the reason code.
    void (GUIDEXOS_NATIVEAOT_PAL_CALL *fail_fast)(void* context,
                                                  uint32_t reason);

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
#include <stddef.h>
static_assert(sizeof(uint32_t) == 4, "NativeAOT PAL ABI requires 32-bit uint32_t");
static_assert(sizeof(uint64_t) == 8, "NativeAOT PAL ABI requires 64-bit uint64_t");
static_assert(sizeof(uintptr_t) == 8, "NativeAOT PAL ABI requires AMD64 pointers");
static_assert(alignof(guidexos_nativeaot_pal_hooks) == alignof(void*),
              "NativeAOT PAL hook alignment must be pointer alignment");
static_assert(offsetof(guidexos_nativeaot_pal_hooks, current_thread_id) ==
                  sizeof(uint32_t) * 2 + sizeof(void*),
              "NativeAOT PAL hook prefix layout changed");
static_assert(sizeof(guidexos_nativeaot_pal_stack_bounds_value) == 24,
              "PAL stack bounds layout changed");
static_assert(sizeof(guidexos_nativeaot_pal_worker_handle) == 16,
              "PAL worker handle layout changed");
static_assert(alignof(guidexos_nativeaot_pal_hook_table_v1) == 8,
              "PAL hook table alignment changed");
static_assert(sizeof(guidexos_nativeaot_pal_hook_table_v1) == 232,
              "PAL hook table size changed");
static_assert(offsetof(guidexos_nativeaot_pal_hook_table_v1, current_thread_id) == 48,
              "PAL hook table thread offset changed");
static_assert(offsetof(guidexos_nativeaot_pal_hook_table_v1, fls_allocate) == 64,
              "PAL hook table FLS offset changed");
static_assert(offsetof(guidexos_nativeaot_pal_hook_table_v1, create_worker) == 96,
              "PAL hook table worker offset changed");
static_assert(offsetof(guidexos_nativeaot_pal_hook_table_v1, query_counter) == 120,
              "PAL hook table timing offset changed");
#endif

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
guidexos_nativeaot_pal_install_hook_table(
    const guidexos_nativeaot_pal_hook_table_v1* table);

int32_t GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_pal_uninstall_hook_table(void);

uint64_t GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_pal_hook_table_generation(void);

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

void* GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_pal_virtual_alloc(void* preferred,
                                     uintptr_t size,
                                     uint32_t allocation_type,
                                     uint32_t protection);

int32_t GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_pal_virtual_free(void* address,
                                    uintptr_t size,
                                    uint32_t free_type);

int32_t GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_pal_virtual_protect(void* address,
                                       uintptr_t size,
                                       uint32_t protection,
                                       uint32_t* old_protection);

int32_t GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_pal_get_last_error(void);

void GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_pal_set_last_error(int32_t value);

void* GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_pal_current_process(void);

void* GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_pal_current_thread(void);

int32_t GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_pal_duplicate_handle(void* source_process,
                                        void* source_handle,
                                        void* target_process,
                                        void** target_handle,
                                        uint32_t access,
                                        int32_t inherit,
                                        uint32_t options);

void* GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_pal_static_module_from_pointer(const void* pointer);

void* GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_pal_static_resolve(void* module, const char* name);

int32_t GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_pal_create_thread(guidexos_nativeaot_thread_entry entry,
                                      void* entry_context,
                                      uint32_t stack_size,
                                      int32_t high_priority,
                                      guidexos_nativeaot_pal_opaque_handle* result);

int32_t GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_pal_join_thread(guidexos_nativeaot_pal_opaque_handle handle,
                                    uint32_t timeout_milliseconds,
                                    uintptr_t* result);

int32_t GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_pal_close_thread(guidexos_nativeaot_pal_opaque_handle handle);

// Compatibility shims used only while compiling the locked NativeAOT
// thread.cpp source.  They remain opaque C ABI calls and never expose a
// guideXOS handle or C++ object to the replacement object.
int32_t GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_pal_close_handle(void* handle);

uint8_t* GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_pal_current_teb(void);

void* GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_pal_create_event(int32_t manual_reset, int32_t initial_state);

int32_t GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_pal_wait_any(uint32_t timeout_milliseconds,
                                uint32_t count,
                                void* const* handles,
                                int32_t alertable);

int32_t GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_pal_close_event(void* handle);

[[noreturn]] void GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_pal_fail_fast(uint32_t reason);

[[noreturn]] void GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_pal_fail_fast_default(void);

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
