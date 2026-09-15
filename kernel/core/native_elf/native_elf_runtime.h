//
// Trusted NativeElf application-runtime bootstrap contract.
//
// This is deliberately an application invocation context, not a process
// abstraction.  It is fixed-size, single-instance, and reusable across runs.
//
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "../../../sdk/include/guidexos/app.h"
#include "native_elf_contract.h"
#include "native_elf_stack_policy.h"

namespace kernel {
namespace native_elf {

static const uint64_t APPLICATION_STACK_SIZE = NATIVE_ELF_APPLICATION_STACK_SIZE;
// The bootloader identity-maps the complete NativeElf window.  Keep the
// dedicated application stack in its tail, beyond the loader's 1 MiB image
// limit, so it cannot overlap either the generated image or the kernel's
// handoff stack.
static const uint64_t APPLICATION_STACK_BASE =
    guidexos::native_elf::IMAGE_BASE + guidexos::native_elf::REGION_SIZE -
    APPLICATION_STACK_SIZE;
static const uint64_t APPLICATION_STACK_ALIGNMENT = 16ULL;
static const uint32_t APPLICATION_STACK_GUARD_BYTES = 0U;
static const uint32_t NATIVE_APP_MAX_LOG_BYTES = 255U;
static const uint32_t NATIVE_APP_MAX_LOG_LINES = 16U;
static const uint32_t NATIVE_APP_MAX_LOG_LINE_BYTES = NATIVE_APP_MAX_LOG_BYTES + 1U;
// Bootstrap compiler output remains capped by MAX_ELF_FILE_BYTES. The
// Developer Studio is a prelinked NativeElf application. Its fixed-size
// debugger/editor storage is intentionally large, so the loader buffer is
// bounded at 2 MiB while the mapped image remains bounded by REGION_SIZE.
static const uint32_t NATIVE_APP_MAX_ELF_FILE_BYTES = 2U * 1024U * 1024U;
// Nested debug execution must restore the active prelinked image, including
// its zero-filled data segment. Keep that parent snapshot bounded separately
// from the file buffer; the packaged Developer Studio image is ~14 MiB when
// mapped and this leaves bounded growth headroom without reserving the full
// 512 MiB NativeElf window in kernel .bss.
static const uint32_t NATIVE_APP_MAX_MAPPED_IMAGE_BYTES = 16U * 1024U * 1024U;

static_assert(APPLICATION_STACK_SIZE < guidexos::native_elf::REGION_SIZE,
              "application stack must fit inside the reserved NativeElf window");
static_assert(NATIVE_APP_MAX_MAPPED_IMAGE_BYTES <= guidexos::native_elf::MAX_MAPPED_BYTES,
              "nested parent snapshot must fit inside the NativeElf window");

enum class NativeAppExecutionState : uint8_t {
    Empty,
    Loaded,
    Prepared,
    Running,
    Returned,
    Failed,
    Cleaned
};

// This is a runtime failure channel, not an application exit value.  The
// compiler currently emits the bounded call-depth failure below; keeping the
// enum extensible allows later NativeElf safety checks to use the same channel.
enum class NativeRuntimeStatus : uint32_t {
    None = 0,
    CallDepthExceeded = 1,
    ArrayBoundsExceeded = 2,
    InvalidPointerDereference = 3,
    PointerOutOfBounds = 4,
};

inline const char* native_runtime_status_name(NativeRuntimeStatus status)
{
    switch (status) {
    case NativeRuntimeStatus::None: return "None";
    case NativeRuntimeStatus::CallDepthExceeded: return "CallDepthExceeded";
    case NativeRuntimeStatus::ArrayBoundsExceeded: return "ArrayBoundsExceeded";
    case NativeRuntimeStatus::InvalidPointerDereference: return "InvalidPointerDereference";
    case NativeRuntimeStatus::PointerOutOfBounds: return "PointerOutOfBounds";
    }
    return "Unknown";
}

struct NativeAppStackLayout {
    uint64_t base;
    uint64_t size;
    uint64_t top;
};

struct NativeElfTrampolineResult {
    int32_t returnValue;
    uint32_t reserved;
    uint64_t applicationRsp;
    NativeRuntimeStatus runtimeStatus;
    uint32_t runtimeCallDepth;
};

struct NativeAppExecutionContext {
    uint64_t imageBase;
    uint64_t imageSize;
    uint64_t entryPoint;

    uint64_t readOnlyDataBase;
    uint64_t readOnlyDataSize;

    uint64_t stackBase;
    uint64_t stackSize;
    uint64_t stackTop;

    gx_app_context appContext;
    gx_host_calls hostCalls;

    int32_t result;
    NativeAppExecutionState state;
    bool hostLogObserved;
    uint32_t hostLogBytes;
    uint32_t hostLogCount;
    bool hostLogTruncated;
    char hostLog[NATIVE_APP_MAX_LOG_LINES][NATIVE_APP_MAX_LOG_LINE_BYTES];

    uint64_t kernelRspBefore;
    uint64_t applicationRsp;
    uint64_t kernelRspAfter;
    const char* error;

    // Append-only runtime diagnostics.  Keep all legacy context offsets above
    // this point stable for existing host-call and lifecycle code.
    NativeRuntimeStatus runtimeStatus;
    uint32_t runtimeCallDepth;
};

// These assertions pin the generated-code ABI to the SDK definition.  The
// compiler backend uses the same constants rather than undocumented offsets.
static_assert(offsetof(gx_app_context, host) == 8, "gx_app_context host offset changed");
static_assert(offsetof(gx_app_context, userData) == 16, "gx_app_context userData offset changed");
static_assert(sizeof(gx_app_context) == 24, "gx_app_context size changed");
static_assert(offsetof(gx_host_calls, log) == 8, "gx_host_calls log offset changed");
static_assert(offsetof(gx_host_calls, get_api_version) == 16, "gx_host_calls version offset changed");
static_assert(offsetof(gx_host_calls, native_window_create) == 352,
              "gx_host_calls native window create offset changed");
static_assert(offsetof(gx_host_calls, native_window_set_text) == 360,
              "gx_host_calls native window text offset changed");
static_assert(offsetof(gx_host_calls, native_window_destroy) == 368,
              "gx_host_calls native window destroy offset changed");
static_assert(offsetof(gx_host_calls, native_window_run) == 376,
              "gx_host_calls native window run offset changed");
    static_assert(offsetof(gx_host_calls, bare_metal_development_run_cancel) == 384,
              "gx_host_calls bare-metal run cancel offset changed");
    static_assert(offsetof(gx_host_calls, bare_metal_development_debug) == 392,
                  "gx_host_calls bare-metal development debug offset changed");
    static_assert(offsetof(gx_host_calls, development_debug_call_stack) == 400,
                  "gx_host_calls hosted call stack offset changed");
    static_assert(offsetof(gx_host_calls, bare_metal_development_debug_call_stack) == 408,
                  "gx_host_calls bare-metal call stack offset changed");
    static_assert(offsetof(gx_host_calls, development_debug_inspect_variables) == 416,
                  "gx_host_calls hosted variable inspection offset changed");
    static_assert(offsetof(gx_host_calls, bare_metal_development_debug_inspect_variables) == 424,
                  "gx_host_calls bare-metal variable inspection offset changed");
    static_assert(offsetof(gx_host_calls, development_debug_evaluate_expression) == 432,
                  "gx_host_calls hosted expression offset changed");
    static_assert(offsetof(gx_host_calls, bare_metal_development_debug_evaluate_expression) == 440,
                  "gx_host_calls bare-metal expression offset changed");
    static_assert(sizeof(gx_host_calls) == 448, "gx_host_calls size changed");

inline bool calculate_application_stack_layout(uint64_t base,
                                               uint64_t size,
                                               NativeAppStackLayout* output)
{
    if (!output || base == 0 || size == 0 ||
        (base & (guidexos::native_elf::PAGE_SIZE - 1ULL)) != 0 ||
        (size & (guidexos::native_elf::PAGE_SIZE - 1ULL)) != 0 ||
        (base > ~static_cast<uint64_t>(0) - size)) {
        return false;
    }

    const uint64_t end = base + size;
    const uint64_t top = end & ~(APPLICATION_STACK_ALIGNMENT - 1ULL);
    if (top <= base || top - base < APPLICATION_STACK_ALIGNMENT) return false;

    output->base = base;
    output->size = size;
    output->top = top;
    return true;
}

inline bool native_app_pointer_in_range(uint64_t pointer,
                                        uint64_t base,
                                        uint64_t size)
{
    if (pointer == 0 || size == 0 || pointer < base ||
        base > ~static_cast<uint64_t>(0) - size) {
        return false;
    }
    return pointer - base < size;
}

inline bool native_app_log_pointer_range(uint64_t pointer,
                                         uint64_t dataBase,
                                         uint64_t dataSize,
                                         uint32_t* maximumReadableBytes)
{
    if (!maximumReadableBytes || !native_app_pointer_in_range(pointer, dataBase, dataSize)) {
        return false;
    }

    const uint64_t remaining = dataSize - (pointer - dataBase);
    if (remaining == 0) return false;
    *maximumReadableBytes = static_cast<uint32_t>(
        remaining < (static_cast<uint64_t>(NATIVE_APP_MAX_LOG_BYTES) + 1ULL)
            ? remaining
            : (static_cast<uint64_t>(NATIVE_APP_MAX_LOG_BYTES) + 1ULL));
    return *maximumReadableBytes != 0;
}

struct NativeElfRunReport;

bool native_elf_execution_active();
const NativeAppExecutionContext* native_elf_runtime_context();
const NativeAppExecutionContext* native_elf_debug_runtime_context();
bool native_elf_host_call_validation_smoke();

// Interactive nested debugging swaps the paused child image out while the
// parent NativeElf UI continues to run in the shared fixed image window.
bool native_elf_nested_prepare_for_scheduler();
bool native_elf_nested_capture_after_scheduler();
bool native_elf_nested_enter_for_host();
bool native_elf_nested_leave_for_host();

// Runs one child NativeElf operation while the active NativeElf host
// application remains suspended. The child uses a separate stack and the
// parent image/page permissions are restored before this returns. Only one
// nested operation is supported and it is intentionally synchronous.
bool run_file_nested(const char* path, int32_t* returnValue, NativeElfRunReport* report = nullptr);

} // namespace native_elf
} // namespace kernel
