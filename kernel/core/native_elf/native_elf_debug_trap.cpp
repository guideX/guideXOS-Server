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

static BreakpointHandler s_handler = nullptr;
static bool s_installed = false;
static IdtEntry s_savedEntry = {};
static IdtEntry* s_liveEntry = nullptr;

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

extern "C" bool native_elf_debug_trap_dispatch(BreakpointContext* context)
{
    if (s_handler && s_handler(context)) return true;

    // The existing kernel has no vector-3 consumer. An unrelated #BP is
    // therefore reported and halted rather than being silently resumed.
    serial::puts("NativeElf: unowned vector3 breakpoint trap\n");
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
    "    call native_elf_debug_trap_dispatch\n"
    "    add $40, %rsp\n"
#else
    "    mov %rsp, %rdi\n"
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
);
#endif

} // namespace

bool install(BreakpointHandler handler)
{
#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
    if (!handler || s_installed) return false;
    Idtr idtr = {};
    asm volatile("sidt %0" : "=m"(idtr));
    if (idtr.base == 0 || idtr.limit < 3U * sizeof(IdtEntry) + sizeof(IdtEntry) - 1U) return false;
    IdtEntry* entry = reinterpret_cast<IdtEntry*>(static_cast<uintptr_t>(idtr.base)) + 3;
    copy_entry(&s_savedEntry, entry);
    s_liveEntry = entry;
    s_handler = handler;
    set_entry_target(entry, reinterpret_cast<uint64_t>(&native_elf_debug_trap_stub));
    entry->selector = 0x08;
    entry->ist = 0;
    entry->typeAttributes = 0x8E;
    entry->reserved = 0;
    asm volatile("lidt %0" : : "m"(idtr) : "memory");
    s_installed = true;
    return true;
#else
    (void)handler;
    return false;
#endif
}

void uninstall()
{
#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
    if (!s_installed) return;
    if (s_liveEntry) copy_entry(s_liveEntry, &s_savedEntry);
    asm volatile("mfence" : : : "memory");
    s_liveEntry = nullptr;
    s_handler = nullptr;
    s_installed = false;
#endif
}

} // namespace NativeElfDebugTrap
} // namespace native_elf
} // namespace kernel
