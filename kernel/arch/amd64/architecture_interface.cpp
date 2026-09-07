// Additive AMD64 backend for the common scheduler contract.
// The established desktop kernel path still uses its existing IDT/PIT code;
// this backend makes the new freestanding contract mechanically compilable
// without changing that path's behavior.

#include <stdint.h>

#include "kernel/arch_interface.h"

extern "C" void amd64_common_context_switch(void** old_context, void* new_context);
extern "C" void amd64_common_thread_start();

namespace kernel {
namespace arch {

namespace {

struct Amd64Context {
    uint64_t rbx;
    uint64_t rbp;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;
    uint64_t rsp;
    uint64_t rip;
};

static_assert(sizeof(Amd64Context) == 64, "AMD64 common context layout changed");

static void zero_bytes(void* destination, uint64_t size)
{
    uint8_t* bytes = static_cast<uint8_t*>(destination);
    for (uint64_t i = 0; i < size; ++i) bytes[i] = 0;
}

} // namespace

interrupt_state_t irq_save()
{
    uint64_t state = 0;
    __asm__ volatile("pushfq\n popq %0\n cli" : "=r"(state) : : "memory");
    return state;
}

void irq_restore(interrupt_state_t state)
{
    __asm__ volatile("pushq %0\n popfq" : : "r"(state) : "memory");
}

void irq_enable() { __asm__ volatile("sti" ::: "memory"); }
void irq_disable() { __asm__ volatile("cli" ::: "memory"); }
void idle() { __asm__ volatile("hlt" ::: "memory"); }
void memory_barrier() { __asm__ volatile("mfence" ::: "memory"); }
const char* interface_name() { return "AMD64"; }

void* context_init(uint64_t stack_base, uint64_t stack_size,
                   thread_entry_t entry, void* argument)
{
    if (!entry || stack_base == 0 || stack_size < 0x1000 ||
        stack_size > UINT64_MAX - stack_base) return 0;
    const uint64_t top = stack_base + stack_size - 0x10;
    if ((top & 0xf) != 0 || top < stack_base + 80) return 0;
    const uint64_t address = (top - 80) & ~UINT64_C(0xf);
    if (address < stack_base || address + sizeof(Amd64Context) > top) return 0;
    Amd64Context* context = reinterpret_cast<Amd64Context*>(
        static_cast<uintptr_t>(address));
    zero_bytes(context, sizeof(*context));
    context->r12 = reinterpret_cast<uint64_t>(entry);
    context->r13 = reinterpret_cast<uint64_t>(argument);
    context->rsp = address;
    context->rip = reinterpret_cast<uint64_t>(&amd64_common_thread_start);
    return context;
}

void context_switch(void** old_context, void* new_context)
{
    if (old_context && new_context) amd64_common_context_switch(old_context, new_context);
}

// The existing AMD64 exception path owns its frame ABI.  The common
// scheduler backend is therefore compile-ready for voluntary switches while
// IRQ-frame switching remains explicitly deferred rather than guessed.
bool interrupt_frame_save(void*, void*) { return false; }
bool context_to_interrupt(void*, void*) { return false; }

} // namespace arch
} // namespace kernel

#if defined(__MINGW32__) || defined(__MINGW64__) || defined(_WIN64)
#define GXOS_AMD64_OLD "%rcx"
#define GXOS_AMD64_NEW "%rdx"
#define GXOS_AMD64_ARG "%rcx"
#define GXOS_AMD64_SHADOW "sub $40, %rsp\n"
#define GXOS_AMD64_UNSHADOW "add $40, %rsp\n"
#else
#define GXOS_AMD64_OLD "%rdi"
#define GXOS_AMD64_NEW "%rsi"
#define GXOS_AMD64_ARG "%rdi"
#define GXOS_AMD64_SHADOW "sub $8, %rsp\n"
#define GXOS_AMD64_UNSHADOW "add $8, %rsp\n"
#endif

asm(
    ".global amd64_common_context_switch\n"
    "amd64_common_context_switch:\n"
    "    sub $64, %rsp\n"
    "    mov %rbx, 0(%rsp)\n"
    "    mov %rbp, 8(%rsp)\n"
    "    mov %r12, 16(%rsp)\n"
    "    mov %r13, 24(%rsp)\n"
    "    mov %r14, 32(%rsp)\n"
    "    mov %r15, 40(%rsp)\n"
    "    mov %rsp, 48(%rsp)\n"
    "    mov 64(%rsp), %rax\n"
    "    mov %rax, 56(%rsp)\n"
    "    mov %rsp, (" GXOS_AMD64_OLD ")\n"
    "    mov 48(" GXOS_AMD64_NEW "), %rsp\n"
    "    mov 0(%rsp), %rbx\n"
    "    mov 8(%rsp), %rbp\n"
    "    mov 16(%rsp), %r12\n"
    "    mov 24(%rsp), %r13\n"
    "    mov 32(%rsp), %r14\n"
    "    mov 40(%rsp), %r15\n"
    "    mov 56(%rsp), %rax\n"
    "    add $64, %rsp\n"
    "    jmp *%rax\n"
);

asm(
    ".global amd64_common_thread_start\n"
    "amd64_common_thread_start:\n"
    "    mov %r13, " GXOS_AMD64_ARG "\n"
    GXOS_AMD64_SHADOW
    "    call *%r12\n"
    GXOS_AMD64_UNSHADOW
    "    cli\n"
    ".Lamd64_common_thread_halt:\n"
    "    hlt\n"
    "    jmp .Lamd64_common_thread_halt\n"
);

#undef GXOS_AMD64_OLD
#undef GXOS_AMD64_NEW
#undef GXOS_AMD64_ARG
#undef GXOS_AMD64_SHADOW
#undef GXOS_AMD64_UNSHADOW
