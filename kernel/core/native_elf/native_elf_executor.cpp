//
// Controlled AMD64 NativeElf entry invocation.
//

#include "native_elf_executor.h"

namespace kernel {
namespace native_elf {

#if defined(__x86_64__)

#if defined(__GNUC__) || defined(__clang__)
using NativeEntry = int32_t (__attribute__((ms_abi)) *)(void*);
#define GXOS_NATIVE_ELF_NOINLINE __attribute__((noinline))
#else
// MSVC's x64 ABI is the Microsoft ABI by default.
using NativeEntry = int32_t (*)(void*);
#define GXOS_NATIVE_ELF_NOINLINE __declspec(noinline)
#endif

GXOS_NATIVE_ELF_NOINLINE bool invoke_validated_native_entry(
    uint64_t entryPoint,
    void* context,
    int32_t* returnValue)
{
    if (entryPoint == 0 || !returnValue) return false;

    // This cast is intentionally confined to the post-validation wrapper.
    // The loader has already checked that entryPoint is inside an executable,
    // file-backed PT_LOAD in the reserved NativeElf window.
    NativeEntry entry = reinterpret_cast<NativeEntry>(static_cast<uintptr_t>(entryPoint));
    *returnValue = entry(context);
    return true;
}

#else

bool invoke_validated_native_entry(uint64_t, void*, int32_t*)
{
    return false;
}

#endif

} // namespace native_elf
} // namespace kernel
