#include "js114_cpp_probe.h"

#include <new>

namespace {

static constexpr size_t kMaximumAlignment = 4096u;
static constexpr uint32_t kProbeValue = 0x4A533131u;

struct alignas(16) ProbeObject {
    guidex_cpp_probe_result* result;
    uint32_t value;

    explicit ProbeObject(guidex_cpp_probe_result* record) noexcept
        : result(record), value(kProbeValue) {
        result->constructed = 1u;
    }

    ~ProbeObject() noexcept {
        result->destroyed = 1u;
    }

    uint32_t read() const noexcept { return value; }
};

template <typename T>
static constexpr unsigned int cxx17_alignment_class() noexcept {
    if constexpr (alignof(T) >= 16u) return 16u;
    else return static_cast<unsigned int>(alignof(T));
}

static_assert(alignof(ProbeObject) == 16u,
              "probe object must exercise the target's 16-byte alignment");
static_assert(cxx17_alignment_class<ProbeObject>() == 16u,
              "probe must use C++17 if-constexpr semantics");

static void clear_result(guidex_cpp_probe_result* result) noexcept {
    result->status = GUIDEX_CPP_PROBE_INVALID_CONTEXT;
    result->constructed = 0u;
    result->destroyed = 0u;
    result->alignment_ok = 0u;
    result->request_bytes = 0u;
    result->request_alignment = 0u;
    result->arena_used_after = 0u;
    result->value = 0u;
}

static bool is_power_of_two(size_t value) noexcept {
    return value != 0u && (value & (value - 1u)) == 0u;
}

static unsigned int allocate_from_arena(guidex_cpp_probe_context* context,
                                        size_t bytes,
                                        size_t alignment,
                                        void** out_memory) noexcept {
    if (!context || !out_memory || !context->arena ||
        context->arena_used > context->arena_capacity) {
        return GUIDEX_CPP_PROBE_INVALID_CONTEXT;
    }
    if (bytes == 0u || bytes < sizeof(ProbeObject)) {
        return GUIDEX_CPP_PROBE_INVALID_REQUEST;
    }
    if (!is_power_of_two(alignment) || alignment < alignof(ProbeObject) ||
        alignment > kMaximumAlignment) {
        return GUIDEX_CPP_PROBE_BAD_ALIGNMENT;
    }

    const uintptr_t maximum = static_cast<uintptr_t>(-1);
    const uintptr_t base = reinterpret_cast<uintptr_t>(context->arena);
    if (context->arena_capacity > maximum - base) {
        return GUIDEX_CPP_PROBE_INVALID_CONTEXT;
    }

    const uintptr_t cursor = base + context->arena_used;
    const uintptr_t padding = static_cast<uintptr_t>(alignment - 1u);
    if (cursor > maximum - padding) {
        return GUIDEX_CPP_PROBE_INVALID_CONTEXT;
    }
    const uintptr_t aligned = (cursor + padding) & ~padding;
    const size_t offset = static_cast<size_t>(aligned - base);
    if (offset > context->arena_capacity ||
        bytes > context->arena_capacity - offset) {
        return GUIDEX_CPP_PROBE_NO_MEMORY;
    }

    context->arena_used = offset + bytes;
    *out_memory = reinterpret_cast<void*>(aligned);
    return GUIDEX_CPP_PROBE_OK;
}

} // namespace

extern "C" int guidex_cpp_probe_run(guidex_cpp_probe_context* context,
                                    unsigned int flags) {
    if (!context) return GUIDEX_CPP_PROBE_INVALID_CONTEXT;

    clear_result(&context->result);
    if (flags != 0u) {
        context->result.status = GUIDEX_CPP_PROBE_UNSUPPORTED_FLAGS;
        return static_cast<int>(context->result.status);
    }

    context->result.request_bytes = context->request_bytes;
    context->result.request_alignment = context->request_alignment;
    const size_t allocation_mark = context->arena_used;
    void* storage = nullptr;
    const unsigned int allocation_status = allocate_from_arena(
        context, context->request_bytes, context->request_alignment, &storage);
    if (allocation_status != GUIDEX_CPP_PROBE_OK) {
        context->result.status = allocation_status;
        context->result.arena_used_after = context->arena_used;
        return static_cast<int>(allocation_status);
    }

    const uintptr_t address = reinterpret_cast<uintptr_t>(storage);
    context->result.alignment_ok =
        (address & static_cast<uintptr_t>(context->request_alignment - 1u)) == 0u
            ? 1u : 0u;
    if (!context->result.alignment_ok) {
        context->arena_used = allocation_mark;
        context->result.status = GUIDEX_CPP_PROBE_BAD_ALIGNMENT;
        context->result.arena_used_after = context->arena_used;
        return static_cast<int>(context->result.status);
    }

    ProbeObject* object = ::new (storage) ProbeObject(&context->result);
    context->result.value = object->read();
    object->~ProbeObject();
    context->arena_used = allocation_mark;

    context->result.arena_used_after = context->arena_used;
    context->result.status = GUIDEX_CPP_PROBE_OK;
    return GUIDEX_CPP_PROBE_OK;
}
