//
// Bounded AMD64 vector-3 gate for NativeElf entry debugging.
//

#include "native_elf_debug_trap.h"

#include "arch/amd64.h"
#include "kernel/serial_debug.h"

namespace kernel {
namespace native_elf {
namespace NativeElfDebugTrap {
namespace {

#if defined(__x86_64__)
struct IdtEntry {
    uint16_t offsetLow;
    uint16_t selector;
    uint8_t ist;
    uint8_t typeAttributes;
    uint16_t offsetMiddle;
    uint32_t offsetHigh;
    uint32_t reserved;
} __attribute__((packed));

struct Idtr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

static BreakpointHandler s_breakpointHandler = nullptr;
static BreakpointHandler s_singleStepHandler = nullptr;
static bool s_installed = false;
static IdtEntry s_savedBreakpointEntry = {};
static IdtEntry s_savedSingleStepEntry = {};
static IdtEntry* s_liveBreakpointEntry = nullptr;
static IdtEntry* s_liveSingleStepEntry = nullptr;

static void copy_entry(IdtEntry* destination, const IdtEntry* source)
{
    if (!destination || !source) return;
    const uint8_t* from = reinterpret_cast<const uint8_t*>(source);
    uint8_t* to = reinterpret_cast<uint8_t*>(destination);
    for (uint32_t i = 0; i < sizeof(IdtEntry); ++i) to[i] = from[i];
}

static void set_entry_target(IdtEntry* entry, uint64_t address)
{
    entry->offsetLow = static_cast<uint16_t>(address & 0xFFFFU);
    entry->offsetMiddle = static_cast<uint16_t>((address >> 16) & 0xFFFFU);
    entry->offsetHigh = static_cast<uint32_t>((address >> 32) & 0xFFFFFFFFU);
}

extern "C" void native_elf_debug_trap_stub();
extern "C" void native_elf_single_step_trap_stub();

extern "C" bool native_elf_debug_trap_dispatch(BreakpointContext* context,
                                                uint32_t vector)
{
    BreakpointHandler handler = vector == 1U ? s_singleStepHandler : s_breakpointHandler;
    if (handler && handler(context)) return true;

    // The existing kernel has no vector-1/vector-3 consumer. An unrelated
    // debug exception is therefore reported and halted rather than being
    // silently resumed.
    serial::puts(vector == 1U
        ? "NativeElf: unowned vector1 debug trap\n"
        : "NativeElf: unowned vector3 breakpoint trap\n");
    for (;;) arch::halt();
}

asm(
    ".global native_elf_debug_trap_stub\n"
#if defined(__ELF__)
    ".type native_elf_debug_trap_stub, @function\n"
#endif
    "native_elf_debug_trap_stub:\n"
    // Hardware has pushed RIP, CS, and RFLAGS. The target remains at CPL0,
    // so no privilege-change RSP/SS pair is present.
    "    push %r15\n"
    "    push %r14\n"
    "    push %r13\n"
    "    push %r12\n"
    "    push %r11\n"
    "    push %r10\n"
    "    push %r9\n"
    "    push %r8\n"
    "    push %rbp\n"
    "    push %rdi\n"
    "    push %rsi\n"
    "    push %rdx\n"
    "    push %rcx\n"
    "    push %rbx\n"
    "    push %rax\n"
    // Keep the interrupted application RSP as a copied field in the frame.
    "    sub $8, %rsp\n"
    // The hardware frame starts at offset 128.  The interrupted target RSP
    // is 24 bytes above it because vector 3 pushes RFLAGS, CS, and RIP.
    "    lea 152(%rsp), %rax\n"
    "    mov %rax, 0(%rsp)\n"
#if defined(__MINGW32__) || defined(__MINGW64__) || defined(_WIN64)
    "    sub $40, %rsp\n"
    "    lea 40(%rsp), %rcx\n"
    "    mov $3, %edx\n"
    "    call native_elf_debug_trap_dispatch\n"
    "    add $40, %rsp\n"
#else
    "    mov %rsp, %rdi\n"
    "    mov $3, %esi\n"
    "    call native_elf_debug_trap_dispatch\n"
#endif
    "    add $8, %rsp\n"
    "    pop %rax\n"
    "    pop %rbx\n"
    "    pop %rcx\n"
    "    pop %rdx\n"
    "    pop %rsi\n"
    "    pop %rdi\n"
    "    pop %rbp\n"
    "    pop %r8\n"
    "    pop %r9\n"
    "    pop %r10\n"
    "    pop %r11\n"
    "    pop %r12\n"
    "    pop %r13\n"
    "    pop %r14\n"
    "    pop %r15\n"
    "    iretq\n"
#if defined(__ELF__)
    ".size native_elf_debug_trap_stub, .-native_elf_debug_trap_stub\n"
#endif

    ".global native_elf_single_step_trap_stub\n"
#if defined(__ELF__)
    ".type native_elf_single_step_trap_stub, @function\n"
#endif
    "native_elf_single_step_trap_stub:\n"
    // AMD64 #DB has the same no-error-code hardware frame as #BP. Clear the
    // live TF before entering C so the owner/kernel continuation cannot
    // inherit target tracing semantics. The saved target RFLAGS remains in
    // the copied hardware frame and is cleared by the run-service handler
    // before the target is exposed as Paused.
    "    pushfq\n"
    "    andq $~0x100, (%rsp)\n"
    "    popfq\n"
    "    push %r15\n"
    "    push %r14\n"
    "    push %r13\n"
    "    push %r12\n"
    "    push %r11\n"
    "    push %r10\n"
    "    push %r9\n"
    "    push %r8\n"
    "    push %rbp\n"
    "    push %rdi\n"
    "    push %rsi\n"
    "    push %rdx\n"
    "    push %rcx\n"
    "    push %rbx\n"
    "    push %rax\n"
    "    sub $8, %rsp\n"
    "    lea 152(%rsp), %rax\n"
    "    mov %rax, 0(%rsp)\n"
#if defined(__MINGW32__) || defined(__MINGW64__) || defined(_WIN64)
    "    sub $40, %rsp\n"
    "    lea 40(%rsp), %rcx\n"
    "    mov $1, %edx\n"
    "    call native_elf_debug_trap_dispatch\n"
    "    add $40, %rsp\n"
#else
    "    mov %rsp, %rdi\n"
    "    mov $1, %esi\n"
    "    call native_elf_debug_trap_dispatch\n"
#endif
    "    add $8, %rsp\n"
    "    pop %rax\n"
    "    pop %rbx\n"
    "    pop %rcx\n"
    "    pop %rdx\n"
    "    pop %rsi\n"
    "    pop %rdi\n"
    "    pop %rbp\n"
    "    pop %r8\n"
    "    pop %r9\n"
    "    pop %r10\n"
    "    pop %r11\n"
    "    pop %r12\n"
    "    pop %r13\n"
    "    pop %r14\n"
    "    pop %r15\n"
    "    iretq\n"
#if defined(__ELF__)
    ".size native_elf_single_step_trap_stub, .-native_elf_single_step_trap_stub\n"
#endif
);
#endif

} // namespace

bool install(BreakpointHandler breakpointHandler,
             BreakpointHandler singleStepHandler)
{
#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
    if (!breakpointHandler || !singleStepHandler || s_installed) return false;
    Idtr idtr = {};
    asm volatile("sidt %0" : "=m"(idtr));
    if (idtr.base == 0 || idtr.limit < 3U * sizeof(IdtEntry) + sizeof(IdtEntry) - 1U) return false;
    IdtEntry* table = reinterpret_cast<IdtEntry*>(static_cast<uintptr_t>(idtr.base));
    IdtEntry* breakpointEntry = table + 3;
    IdtEntry* singleStepEntry = table + 1;
    copy_entry(&s_savedBreakpointEntry, breakpointEntry);
    copy_entry(&s_savedSingleStepEntry, singleStepEntry);
    s_liveBreakpointEntry = breakpointEntry;
    s_liveSingleStepEntry = singleStepEntry;
    s_breakpointHandler = breakpointHandler;
    s_singleStepHandler = singleStepHandler;
    set_entry_target(breakpointEntry, reinterpret_cast<uint64_t>(&native_elf_debug_trap_stub));
    set_entry_target(singleStepEntry, reinterpret_cast<uint64_t>(&native_elf_single_step_trap_stub));
    breakpointEntry->selector = 0x08;
    breakpointEntry->ist = 0;
    breakpointEntry->typeAttributes = 0x8E;
    breakpointEntry->reserved = 0;
    singleStepEntry->selector = 0x08;
    singleStepEntry->ist = 0;
    singleStepEntry->typeAttributes = 0x8E;
    singleStepEntry->reserved = 0;
    asm volatile("lidt %0" : : "m"(idtr) : "memory");
    s_installed = true;
    return true;
#else
    (void)breakpointHandler;
    (void)singleStepHandler;
    return false;
#endif
}

void uninstall()
{
#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
    if (!s_installed) return;
    if (s_liveBreakpointEntry) copy_entry(s_liveBreakpointEntry, &s_savedBreakpointEntry);
    if (s_liveSingleStepEntry) copy_entry(s_liveSingleStepEntry, &s_savedSingleStepEntry);
    asm volatile("mfence" : : : "memory");
    s_liveBreakpointEntry = nullptr;
    s_liveSingleStepEntry = nullptr;
    s_breakpointHandler = nullptr;
    s_singleStepHandler = nullptr;
    s_installed = false;
#endif
}

} // namespace NativeElfDebugTrap
} // namespace native_elf
} // namespace kernel
