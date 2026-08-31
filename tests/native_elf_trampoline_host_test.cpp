#include "core/native_elf/native_elf_runtime.h"

#include <cstdio>
#include <cstdint>

using namespace kernel::native_elf;

extern "C" bool invoke_native_entry_on_stack(void* entry,
                                                void* context,
                                                uint64_t stackTop,
                                                NativeElfTrampolineResult* result);
extern "C" int native_elf_trampoline_test_entry(void* context);

int main()
{
#if defined(__GNUC__) || defined(__clang__)
    alignas(16) uint8_t applicationStack[NATIVE_ELF_APPLICATION_STACK_SIZE] = {};
    gx_app_context context = {};
    NativeElfTrampolineResult result = {};
    uint64_t afterR14 = 0;
    uint64_t afterR15 = 0;
    uint32_t invoked = 0;

    // Keep the register setup, call, observation, and restoration in one
    // compiler boundary so the test observes the trampoline's actual ABI.
    asm volatile(
        "movabs $0xA1B2C3D4E5F60718, %%r14\n\t"
        "movabs $0x81706F5E4D3C2B1A, %%r15\n\t"
        "leaq %[result], %%r9\n\t"
        "subq $32, %%rsp\n\t"
        "movq %[entry], %%rcx\n\t"
        "movq %[context], %%rdx\n\t"
        "movq %[stackTop], %%r8\n\t"
        "call *%[invoke]\n\t"
        "addq $32, %%rsp\n\t"
        "movq %%r14, %[afterR14]\n\t"
        "movq %%r15, %[afterR15]\n\t"
        "movl %%eax, %[invoked]\n\t"
        "movabs $0xA1B2C3D4E5F60718, %%r14\n\t"
        "movabs $0x81706F5E4D3C2B1A, %%r15\n\t"
        : [afterR14] "=m" (afterR14), [afterR15] "=m" (afterR15),
          [invoked] "=m" (invoked)
        : [entry] "r" (&native_elf_trampoline_test_entry),
          [context] "r" (&context),
          [stackTop] "r" (reinterpret_cast<uint64_t>(applicationStack) +
                            sizeof(applicationStack)),
          [result] "m" (result),
          [invoke] "r" (&invoke_native_entry_on_stack)
        : "rcx", "rdx", "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15", "memory");

    const bool pass = invoked == 1U && result.returnValue == 42 &&
        result.runtimeStatus == NativeRuntimeStatus::None &&
        result.runtimeCallDepth == 0U &&
        afterR14 == 0xA1B2C3D4E5F60718ULL &&
        afterR15 == 0x81706F5E4D3C2B1AULL &&
        native_app_pointer_in_range(result.applicationRsp,
                                    reinterpret_cast<uint64_t>(applicationStack),
                                    sizeof(applicationStack));
    if (!pass) {
        std::fprintf(stderr, "FAIL: trampoline nonvolatile/private-register preservation invoked=%u result=%d status=%u depth=%u r14=%llx r15=%llx rsp=%llx\n",
                     invoked, result.returnValue, static_cast<unsigned>(result.runtimeStatus),
                     result.runtimeCallDepth, static_cast<unsigned long long>(afterR14),
                     static_cast<unsigned long long>(afterR15),
                     static_cast<unsigned long long>(result.applicationRsp));
        return 1;
    }
    std::puts("trampoline nonvolatile state = PASS");
    std::puts("native_elf_trampoline_host_test: PASS");
    return 0;
#else
    std::puts("native_elf_trampoline_host_test: SKIP");
    return 0;
#endif
}
