#ifndef GUIDEX_JS114_CPP_PROBE_H
#define GUIDEX_JS114_CPP_PROBE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum guidex_cpp_probe_status {
    GUIDEX_CPP_PROBE_OK = 0,
    GUIDEX_CPP_PROBE_INVALID_CONTEXT = 1,
    GUIDEX_CPP_PROBE_INVALID_REQUEST = 2,
    GUIDEX_CPP_PROBE_BAD_ALIGNMENT = 3,
    GUIDEX_CPP_PROBE_NO_MEMORY = 4,
    GUIDEX_CPP_PROBE_UNSUPPORTED_FLAGS = 5
};

typedef struct guidex_cpp_probe_result {
    unsigned int status;
    unsigned int constructed;
    unsigned int destroyed;
    unsigned int alignment_ok;
    size_t request_bytes;
    size_t request_alignment;
    size_t arena_used_after;
    uint32_t value;
} guidex_cpp_probe_result;

/*
 * The caller owns the arena and context for the entire call. The probe makes
 * one temporary monotonic allocation and restores arena_used before return.
 * A non-null arena pointer must identify arena_capacity writable bytes.
 */
typedef struct guidex_cpp_probe_context {
    void* arena;
    size_t arena_capacity;
    size_t arena_used;
    size_t request_bytes;
    size_t request_alignment;
    guidex_cpp_probe_result result;
} guidex_cpp_probe_context;

/* flags must be zero. Returns one of guidex_cpp_probe_status. */
int guidex_cpp_probe_run(guidex_cpp_probe_context* context,
                         unsigned int flags);

/* Compiled as C to verify the unmangled C ABI boundary. */
int guidex_cpp_probe_c_abi_check(guidex_cpp_probe_context* context);

#ifdef __cplusplus
}
#endif

#endif
