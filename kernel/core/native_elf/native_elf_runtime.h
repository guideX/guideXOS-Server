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
// Developer Studio proof app is a prelinked NativeElf application and gets a
// separate bounded loader buffer while still using the same validator and
// fixed NativeElf region.
static const uint32_t NATIVE_APP_MAX_ELF_FILE_BYTES = 256U * 1024U;

static_assert(APPLICATION_STACK_SIZE < guidexos::native_elf::REGION_SIZE,
              "application stack must fit inside the reserved NativeElf window");

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
};

inline const char* native_runtime_status_name(NativeRuntimeStatus status)
{
    switch (status) {
    case NativeRuntimeStatus::None: return "None";
    case NativeRuntimeStatus::CallDepthExceeded: return "CallDepthExceeded";
    case NativeRuntimeStatus::ArrayBoundsExceeded: return "ArrayBoundsExceeded";
    case NativeRuntimeStatus::InvalidPointerDereference: return "InvalidPointerDereference";
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
bool native_elf_host_call_validation_smoke();

// Runs one child NativeElf operation while the active NativeElf host
// application remains suspended. The child uses a separate stack and the
// parent image/page permissions are restored before this returns. Only one
// nested operation is supported and it is intentionally synchronous.
bool run_file_nested(const char* path, int32_t* returnValue, NativeElfRunReport* report = nullptr);

} // namespace native_elf
} // namespace kernel
