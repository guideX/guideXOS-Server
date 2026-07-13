#include "native_elf_trampoline_win64.h"

namespace gxos {
namespace apps {

#ifdef GX_ENABLE_EXPERIMENTAL_NATIVE_ELF_EXECUTION
#if defined(_WIN32) && defined(__x86_64__)

asm(
    ".globl CallNativeElfWin64Entry\n"
    "CallNativeElfWin64Entry:\n"
    "  subq $40, %rsp\n"
    "  movq %rcx, %rax\n"
    "  movq %rdx, %rcx\n"
    "  call *%rax\n"
    "  addq $40, %rsp\n"
    "  ret\n"
);

#endif
#endif

} // namespace apps
} // namespace gxos
