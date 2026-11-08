#include "js114_cpp_probe.h"

#include <cstdio>

namespace {

int check(bool condition, const char* name) {
    if (condition) return 0;
    std::fprintf(stderr, "FAIL %s\n", name);
    return 1;
}

void initialize(guidex_cpp_probe_context* context,
                void* arena,
                size_t capacity,
                size_t bytes,
                size_t alignment) {
    context->arena = arena;
    context->arena_capacity = capacity;
    context->arena_used = 0u;
    context->request_bytes = bytes;
    context->request_alignment = alignment;
}

} // namespace

int main() {
    alignas(64) unsigned char storage[512] = {};
    storage[0] = 0xA5u;
    storage[sizeof(storage) - 1u] = 0x5Au;
    int failures = 0;

    guidex_cpp_probe_context success{};
    initialize(&success, storage + 1, sizeof(storage) - 1u,
               sizeof(void*) + sizeof(uint32_t), 16u);
    /* ProbeObject is private; request ample bounded storage. */
    success.request_bytes = 64u;
    success.arena_used = 3u;
    failures += check(guidex_cpp_probe_c_abi_check(&success) == GUIDEX_CPP_PROBE_OK,
                      "C ABI success");
    failures += check(success.result.constructed == 1u && success.result.destroyed == 1u,
                      "construction and destruction");
    failures += check(success.result.alignment_ok == 1u &&
                      success.result.value == 0x4A533131u,
                      "16-byte alignment and deterministic value");
    failures += check(success.arena_used == 3u && success.result.arena_used_after == 3u,
                      "temporary allocation released to arena mark");
    failures += check(storage[0] == 0xA5u && storage[sizeof(storage) - 1u] == 0x5Au,
                      "arena guard bytes preserved");

    guidex_cpp_probe_context over_aligned{};
    initialize(&over_aligned, storage + 1, sizeof(storage) - 1u, 96u, 64u);
    failures += check(guidex_cpp_probe_run(&over_aligned, 0u) == GUIDEX_CPP_PROBE_OK &&
                      over_aligned.result.alignment_ok == 1u,
                      "64-byte aligned allocation");

    guidex_cpp_probe_context exhausted{};
    initialize(&exhausted, storage + 1, 1u, 64u, 16u);
    failures += check(guidex_cpp_probe_run(&exhausted, 0u) == GUIDEX_CPP_PROBE_NO_MEMORY &&
                      exhausted.result.constructed == 0u && exhausted.arena_used == 0u,
                      "arena exhaustion");

    guidex_cpp_probe_context bad_alignment{};
    initialize(&bad_alignment, storage, sizeof(storage), 64u, 3u);
    failures += check(guidex_cpp_probe_run(&bad_alignment, 0u) == GUIDEX_CPP_PROBE_BAD_ALIGNMENT,
                      "invalid alignment");

    guidex_cpp_probe_context too_small{};
    initialize(&too_small, storage, sizeof(storage), 1u, 16u);
    failures += check(guidex_cpp_probe_run(&too_small, 0u) == GUIDEX_CPP_PROBE_INVALID_REQUEST,
                      "undersized object request");

    guidex_cpp_probe_context zero_size{};
    initialize(&zero_size, storage, sizeof(storage), 0u, 16u);
    failures += check(guidex_cpp_probe_run(&zero_size, 0u) == GUIDEX_CPP_PROBE_INVALID_REQUEST,
                      "zero-size request rejected");

    guidex_cpp_probe_context size_overflow{};
    initialize(&size_overflow, storage + 1, sizeof(storage) - 1u,
               static_cast<size_t>(-1), 16u);
    failures += check(guidex_cpp_probe_run(&size_overflow, 0u) == GUIDEX_CPP_PROBE_NO_MEMORY &&
                      size_overflow.arena_used == 0u,
                      "oversized request without arithmetic wrap");

    guidex_cpp_probe_context range_overflow{};
    initialize(&range_overflow,
               reinterpret_cast<void*>(static_cast<uintptr_t>(-8)),
               64u, 64u, 16u);
    failures += check(guidex_cpp_probe_run(&range_overflow, 0u) == GUIDEX_CPP_PROBE_INVALID_CONTEXT,
                      "arena address range overflow");

    guidex_cpp_probe_context unsupported{};
    initialize(&unsupported, storage, sizeof(storage), 64u, 16u);
    failures += check(guidex_cpp_probe_run(&unsupported, 1u) == GUIDEX_CPP_PROBE_UNSUPPORTED_FLAGS,
                      "unsupported flags return explicitly");

    if (failures != 0) return 1;
    std::puts("PASS JS114 C++17 arena, alignment, lifetime, and error contract");
    return 0;
}
