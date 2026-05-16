#include "native_elf_trampoline_win64.h"

namespace gxos {
namespace apps {

#ifdef GX_ENABLE_EXPERIMENTAL_NATIVE_ELF_EXECUTION
#if defined(_WIN32) && defined(__x86_64__)

gx_result CallNativeElfWin64Entry(NativeElfWin64Entry entry, NativeGxAppContext* context) {
    gx_result result = GX_ERROR_INVALID_ARGUMENT;
    asm volatile(
        "subq $40, %%rsp\n\t"
        "movq %[context], %%rcx\n\t"
        "call *%[entry]\n\t"
        "addq $40, %%rsp\n\t"
        : "=a"(result)
        : [entry] "r"(entry), [context] "r"(context)
        : "rcx", "rdx", "r8", "r9", "r10", "r11", "memory");
    return result;
}

#endif
#endif

} // namespace apps
} // namespace gxos
