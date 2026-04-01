//
// Kernel Interrupt Infrastructure - Implementation
//
// Sets up IDT (256 entries), remaps the 8259 PIC so IRQs 0-15 map to
// vectors 32-47, and provides IRQ dispatch with handler registration.
//
// Supports both 32-bit (x86) and 64-bit (amd64) targets.
// Non-x86 architectures compile a stub implementation (interrupt
// controllers differ per platform and will be filled in per-arch).
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/kernel/interrupts.h"
#include "include/kernel/arch.h"
#include "include/kernel/serial_debug.h"

namespace kernel {
namespace interrupts {

// ================================================================
// x86 / amd64  —  IDT + 8259 PIC implementation
// ================================================================

#if ARCH_HAS_PIC_8259

// ----------------------------------------------------------------
// IDT structures — architecture-dependent
// ----------------------------------------------------------------

#if defined(__GNUC__) || defined(__clang__)
#define PACKED __attribute__((packed))
#else
#pragma pack(push, 1)
#define PACKED
#endif

#if defined(__x86_64__) || defined(_M_X64)
// 64-bit IDT entry (16 bytes)
struct IDTEntry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  ist;
    uint8_t  type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t reserved;
} PACKED;

struct IDTPtr {
    uint16_t limit;
    uint64_t base;
} PACKED;
#else
// 32-bit IDT entry (8 bytes)
struct IDTEntry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  zero;
    uint8_t  type_attr;
    uint16_t offset_high;
} PACKED;

struct IDTPtr {
    uint16_t limit;
    uint32_t base;
} PACKED;
#endif

#if !defined(__GNUC__) && !defined(__clang__)
#pragma pack(pop)
#endif

// ----------------------------------------------------------------
// GDT structures (required for interrupt handling)
// ----------------------------------------------------------------

#if defined(__x86_64__) || defined(_M_X64)
// 64-bit GDT entry (8 bytes)
struct GDTEntry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  flags_limit_high;
    uint8_t  base_high;
} PACKED;

struct GDTPtr {
    uint16_t limit;
    uint64_t base;
} PACKED;
#else
// 32-bit GDT entry (8 bytes)
struct GDTEntry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  flags_limit_high;
    uint8_t  base_high;
} PACKED;

struct GDTPtr {
    uint16_t limit;
    uint32_t base;
} PACKED;
#endif

// ----------------------------------------------------------------
// Constants
// ----------------------------------------------------------------

static const int kIDTSize           = 256;
static const uint8_t kPICMaster     = 0x20;
static const uint8_t kPICSlave      = 0xA0;
static const uint8_t kPICMasterData = 0x21;
static const uint8_t kPICSlaveData  = 0xA1;
static const uint8_t kIRQBase       = 32;

// ----------------------------------------------------------------
// Static data
// ----------------------------------------------------------------

// GDT: 3 entries - null, code, data
static GDTEntry s_gdt[3];
static GDTPtr   s_gdtPtr;

static IDTEntry s_idt[kIDTSize];
static IDTPtr   s_idtPtr;
static irq_handler_t s_handlers[16] = { 0 };

// ----------------------------------------------------------------
// GDT helpers
// ----------------------------------------------------------------

static void set_gdt_entry(int index, uint32_t base, uint32_t limit, uint8_t access, uint8_t flags)
{
    s_gdt[index].limit_low       = (uint16_t)(limit & 0xFFFF);
    s_gdt[index].base_low        = (uint16_t)(base & 0xFFFF);
    s_gdt[index].base_mid        = (uint8_t)((base >> 16) & 0xFF);
    s_gdt[index].access          = access;
    s_gdt[index].flags_limit_high = (uint8_t)(((limit >> 16) & 0x0F) | (flags & 0xF0));
    s_gdt[index].base_high       = (uint8_t)((base >> 24) & 0xFF);
}

static void gdt_init()
{
    // Entry 0: Null descriptor (required by x86)
    set_gdt_entry(0, 0, 0, 0, 0);
    
#if defined(__x86_64__) || defined(_M_X64)
    // Entry 1: 64-bit code segment (selector 0x08)
    // Access: Present (0x80) | DPL=0 (0x00) | Type=code,exec,read (0x1A) = 0x9A
    // Flags: Long mode (0x20) = 0x20 (no 4K granularity needed in 64-bit)
    set_gdt_entry(1, 0, 0xFFFFF, 0x9A, 0x20);
    
    // Entry 2: 64-bit data segment (selector 0x10)
    // Access: Present (0x80) | DPL=0 (0x00) | Type=data,read,write (0x12) = 0x92
    // Flags: 0x00 (no long mode bit for data segments)
    set_gdt_entry(2, 0, 0xFFFFF, 0x92, 0x00);
#else
    // Entry 1: 32-bit code segment (selector 0x08)
    // Access: Present (0x80) | DPL=0 (0x00) | Type=code,exec,read (0x1A) = 0x9A
    // Flags: 32-bit (0x40) | 4K granularity (0x80) = 0xC0
    set_gdt_entry(1, 0, 0xFFFFF, 0x9A, 0xC0);
    
    // Entry 2: 32-bit data segment (selector 0x10)
    // Access: Present (0x80) | DPL=0 (0x00) | Type=data,read,write (0x12) = 0x92
    // Flags: 32-bit (0x40) | 4K granularity (0x80) = 0xC0
    set_gdt_entry(2, 0, 0xFFFFF, 0x92, 0xC0);
#endif
    
    // Load the GDT
    s_gdtPtr.limit = sizeof(s_gdt) - 1;
#if defined(__x86_64__) || defined(_M_X64)
    s_gdtPtr.base = reinterpret_cast<uint64_t>(&s_gdt[0]);
#else
    s_gdtPtr.base = reinterpret_cast<uint32_t>(&s_gdt[0]);
#endif

#if defined(__GNUC__) || defined(__clang__)
#if defined(__x86_64__)
    // 64-bit: Load GDT and reload segment registers
    asm volatile (
        "lgdt %0\n"
        // Reload CS by far jumping to the next instruction
        "pushq $0x08\n"           // Push code segment selector
        "leaq 1f(%%rip), %%rax\n" // Get address of label 1
        "pushq %%rax\n"           // Push target address
        "lretq\n"                 // Far return to reload CS
        "1:\n"
        // Reload data segment registers
        "movw $0x10, %%ax\n"
        "movw %%ax, %%ds\n"
        "movw %%ax, %%es\n"
        "movw %%ax, %%fs\n"
        "movw %%ax, %%gs\n"
        "movw %%ax, %%ss\n"
        : 
        : "m"(s_gdtPtr)
        : "rax", "memory"
    );
#else
    // 32-bit: Load GDT and reload segment registers
    asm volatile (
        "lgdt %0\n"
        // Reload CS via far jump
        "ljmp $0x08, $1f\n"
        "1:\n"
        // Reload data segment registers
        "movw $0x10, %%ax\n"
        "movw %%ax, %%ds\n"
        "movw %%ax, %%es\n"
        "movw %%ax, %%fs\n"
        "movw %%ax, %%gs\n"
        "movw %%ax, %%ss\n"
        : 
        : "m"(s_gdtPtr)
        : "eax", "memory"
    );
#endif
#endif

    serial::puts("[GDT] GDT loaded with 3 entries, segments reloaded\n");
}

// ----------------------------------------------------------------
// PIC helpers
// ----------------------------------------------------------------

static void io_wait()
{
    arch::outb(0x80, 0);
}

static void pic_remap()
{
    arch::outb(kPICMaster,     0x11); io_wait();
    arch::outb(kPICSlave,      0x11); io_wait();
    arch::outb(kPICMasterData, kIRQBase); io_wait();
    arch::outb(kPICSlaveData,  kIRQBase + 8); io_wait();
    arch::outb(kPICMasterData, 0x04); io_wait();
    arch::outb(kPICSlaveData,  0x02); io_wait();
    arch::outb(kPICMasterData, 0x01); io_wait();
    arch::outb(kPICSlaveData,  0x01); io_wait();

    // Mask all IRQs after remap; register_irq() will unmask as needed
    arch::outb(kPICMasterData, 0xFF);
    arch::outb(kPICSlaveData,  0xFF);
}

static void pic_unmask_irq(uint8_t irq)
{
    if (irq < 8) {
        uint8_t mask = arch::inb(kPICMasterData);
        mask &= ~(1 << irq);
        arch::outb(kPICMasterData, mask);
    } else {
        uint8_t mmask = arch::inb(kPICMasterData);
        mmask &= ~(1 << 2);
        arch::outb(kPICMasterData, mmask);

        uint8_t smask = arch::inb(kPICSlaveData);
        smask &= ~(1 << (irq - 8));
        arch::outb(kPICSlaveData, smask);
    }
}

void eoi(uint8_t irq)
{
    if (irq >= 8) {
        arch::outb(kPICSlave, 0x20);
    }
    arch::outb(kPICMaster, 0x20);
}

// ----------------------------------------------------------------
// IDT helpers
// ----------------------------------------------------------------

#if defined(__x86_64__) || defined(_M_X64)
static void set_idt_entry(int index, uint64_t addr, uint8_t type_attr)
{
    s_idt[index].offset_low  = (uint16_t)(addr & 0xFFFF);
    s_idt[index].selector    = 0x08;
    s_idt[index].ist         = 0;
    s_idt[index].type_attr   = type_attr;
    s_idt[index].offset_mid  = (uint16_t)((addr >> 16) & 0xFFFF);
    s_idt[index].offset_high = (uint32_t)((addr >> 32) & 0xFFFFFFFF);
    s_idt[index].reserved    = 0;
}
#else
static void set_idt_entry(int index, uint32_t addr, uint8_t type_attr)
{
    s_idt[index].offset_low  = (uint16_t)(addr & 0xFFFF);
    s_idt[index].selector    = 0x08;
    s_idt[index].zero        = 0;
    s_idt[index].type_attr   = type_attr;
    s_idt[index].offset_high = (uint16_t)((addr >> 16) & 0xFFFF);
}
#endif

// ----------------------------------------------------------------
// Common IRQ dispatch (called from assembly stubs)
// ----------------------------------------------------------------

extern "C" void irq_dispatch(uint32_t irq_number)
{
    static uint32_t irq_total = 0;
    irq_total++;
    if (irq_total <= 10) {
        serial::puts("[IRQ] dispatch irq=");
        serial::put_hex8(static_cast<uint8_t>(irq_number));
        serial::putc('\n');
    }
    if (irq_number < 16 && s_handlers[irq_number]) {
        s_handlers[irq_number]();
    }
    eoi(static_cast<uint8_t>(irq_number));
}

// ----------------------------------------------------------------
// ISR stubs
// ----------------------------------------------------------------

#if defined(__GNUC__) || defined(__clang__)

#if defined(__x86_64__)

// 64-bit ISR stubs
// Stack must be 16-byte aligned before 'call'. After interrupt pushes
// SS/RSP/RFLAGS/CS/RIP (40 bytes) and we push 9 regs (72 bytes), total
// is 112 bytes. We need to sub 8 to make it 120 (divisible by 16) before call.
//
// MinGW uses Microsoft x64 ABI (first arg in RCX)
// Linux/ELF uses System V ABI (first arg in RDI)
#if defined(__MINGW32__) || defined(__MINGW64__) || defined(_WIN64)
// Microsoft x64 ABI: first argument in RCX
#define MAKE_IRQ_STUB(n)                                   \
    extern "C" void irq_stub_##n();                        \
    asm(                                                   \
        ".global irq_stub_" #n "\n"                        \
        "irq_stub_" #n ":\n"                               \
        "    push %rax\n"                                  \
        "    push %rcx\n"                                  \
        "    push %rdx\n"                                  \
        "    push %rsi\n"                                  \
        "    push %rdi\n"                                  \
        "    push %r8\n"                                   \
        "    push %r9\n"                                   \
        "    push %r10\n"                                  \
        "    push %r11\n"                                  \
        "    sub $40, %rsp\n"                              \
        "    mov $" #n ", %ecx\n"                          \
        "    call irq_dispatch\n"                          \
        "    add $40, %rsp\n"                              \
        "    pop %r11\n"                                   \
        "    pop %r10\n"                                   \
        "    pop %r9\n"                                    \
        "    pop %r8\n"                                    \
        "    pop %rdi\n"                                   \
        "    pop %rsi\n"                                   \
        "    pop %rdx\n"                                   \
        "    pop %rcx\n"                                   \
        "    pop %rax\n"                                   \
        "    iretq\n"                                      \
    );
#else
// System V ABI: first argument in RDI
#define MAKE_IRQ_STUB(n)                                   \
    extern "C" void irq_stub_##n();                        \
    asm(                                                   \
        ".global irq_stub_" #n "\n"                        \
        "irq_stub_" #n ":\n"                               \
        "    push %rax\n"                                  \
        "    push %rcx\n"                                  \
        "    push %rdx\n"                                  \
        "    push %rsi\n"                                  \
        "    push %rdi\n"                                  \
        "    push %r8\n"                                   \
        "    push %r9\n"                                   \
        "    push %r10\n"                                  \
        "    push %r11\n"                                  \
        "    sub $8, %rsp\n"                               \
        "    mov $" #n ", %edi\n"                          \
        "    call irq_dispatch\n"                          \
        "    add $8, %rsp\n"                               \
        "    pop %r11\n"                                   \
        "    pop %r10\n"                                   \
        "    pop %r9\n"                                    \
        "    pop %r8\n"                                    \
        "    pop %rdi\n"                                   \
        "    pop %rsi\n"                                   \
        "    pop %rdx\n"                                   \
        "    pop %rcx\n"                                   \
        "    pop %rax\n"                                   \
        "    iretq\n"                                      \
    );
#endif

#else // 32-bit

// 32-bit ISR stubs (cdecl: arg on stack)
#define MAKE_IRQ_STUB(n)                                   \
    extern "C" void irq_stub_##n();                        \
    asm(                                                   \
        ".global irq_stub_" #n "\n"                        \
        "irq_stub_" #n ":\n"                               \
        "    pusha\n"                                      \
        "    push $" #n "\n"                                \
        "    call irq_dispatch\n"                           \
        "    add $4, %esp\n"                                \
        "    popa\n"                                       \
        "    iret\n"                                       \
    );

#endif // arch select

MAKE_IRQ_STUB(0)
MAKE_IRQ_STUB(1)
MAKE_IRQ_STUB(2)
MAKE_IRQ_STUB(3)
MAKE_IRQ_STUB(4)
MAKE_IRQ_STUB(5)
MAKE_IRQ_STUB(6)
MAKE_IRQ_STUB(7)
MAKE_IRQ_STUB(8)
MAKE_IRQ_STUB(9)
MAKE_IRQ_STUB(10)
MAKE_IRQ_STUB(11)
MAKE_IRQ_STUB(12)
MAKE_IRQ_STUB(13)
MAKE_IRQ_STUB(14)
MAKE_IRQ_STUB(15)

typedef void (*stub_fn)();
static stub_fn s_stubs[16] = {
    irq_stub_0,  irq_stub_1,  irq_stub_2,  irq_stub_3,
    irq_stub_4,  irq_stub_5,  irq_stub_6,  irq_stub_7,
    irq_stub_8,  irq_stub_9,  irq_stub_10, irq_stub_11,
    irq_stub_12, irq_stub_13, irq_stub_14, irq_stub_15
};

static void load_idt()
{
    s_idtPtr.limit = sizeof(s_idt) - 1;
#if defined(__x86_64__)
    s_idtPtr.base  = reinterpret_cast<uint64_t>(&s_idt[0]);
#else
    s_idtPtr.base  = reinterpret_cast<uint32_t>(&s_idt[0]);
#endif
    asm volatile ("lidt %0" : : "m"(s_idtPtr));
}

#elif defined(_MSC_VER)

// MSVC: Windows-hosted dev builds don't need a real IDT
// (mouse is via Win32 WndProc). For bare-metal MSVC builds,
// link a NASM-assembled ISR trampoline object.

static void irq_stub_placeholder() {}
typedef void (*stub_fn)();
static stub_fn s_stubs[16] = {
    irq_stub_placeholder, irq_stub_placeholder, irq_stub_placeholder, irq_stub_placeholder,
    irq_stub_placeholder, irq_stub_placeholder, irq_stub_placeholder, irq_stub_placeholder,
    irq_stub_placeholder, irq_stub_placeholder, irq_stub_placeholder, irq_stub_placeholder,
    irq_stub_placeholder, irq_stub_placeholder, irq_stub_placeholder, irq_stub_placeholder
};

static void load_idt()
{
    s_idtPtr.limit = sizeof(s_idt) - 1;
#if defined(_M_X64)
    s_idtPtr.base  = reinterpret_cast<uint64_t>(&s_idt[0]);
#else
    s_idtPtr.base  = reinterpret_cast<uint32_t>(&s_idt[0]);
#endif
    (void)s_idtPtr;
}

#endif

// ----------------------------------------------------------------
// Public API  (x86 / amd64)
// ----------------------------------------------------------------

void init()
{
    // CRITICAL: Set up our own GDT first, before any interrupt handling.
    // The UEFI-provided GDT may not be accessible after ExitBootServices
    // or may not be mapped in the kernel's page tables.
    gdt_init();

    for (int i = 0; i < kIDTSize; ++i) {
        set_idt_entry(i, 0, 0);
    }

    pic_remap();

    for (int i = 0; i < 16; ++i) {
#if defined(__x86_64__) || defined(_M_X64)
        set_idt_entry(kIRQBase + i,
                      reinterpret_cast<uint64_t>(s_stubs[i]),
                      0x8E);
#else
        set_idt_entry(kIRQBase + i,
                      reinterpret_cast<uint32_t>(s_stubs[i]),
                      0x8E);
#endif
    }

    load_idt();
    arch::enable_interrupts();

    serial::puts("[IRQ] IDT loaded, PIC remapped, interrupts enabled\n");
    serial::puts("[IRQ] Master PIC mask: 0x");
    serial::put_hex8(arch::inb(kPICMasterData));
    serial::puts(" Slave PIC mask: 0x");
    serial::put_hex8(arch::inb(kPICSlaveData));
    serial::putc('\n');
}

void register_irq(uint8_t irq, irq_handler_t handler)
{
    serial::puts("[IRQ] register_irq called for IRQ ");
    serial::put_hex8(irq);
    serial::putc('\n');
    
    if (irq < 16) {
        s_handlers[irq] = handler;
        serial::puts("[IRQ] Handler stored, unmasking...\n");
        pic_unmask_irq(irq);
        serial::puts("[IRQ] Registered and unmasked IRQ ");
        serial::put_hex8(irq);
        serial::puts(" Master mask: 0x");
        serial::put_hex8(arch::inb(kPICMasterData));
        serial::puts(" Slave mask: 0x");
        serial::put_hex8(arch::inb(kPICSlaveData));
        serial::putc('\n');
    }
}

#else // !ARCH_HAS_PIC_8259

// ================================================================
// Non-x86 implementation  —  SPARC / SPARC64 / generic stub
// ================================================================

static irq_handler_t s_handlers[16] = { 0 };

void eoi(uint8_t irq)
{
#if defined(ARCH_SPARC)
    arch::slavio_eoi(static_cast<uint32_t>(irq));
#elif defined(ARCH_SPARC64)
    arch::pci_eoi(static_cast<uint32_t>(irq));
#else
    (void)irq;
#endif
}

void init()
{
    arch::enable_interrupts();
}

void register_irq(uint8_t irq, irq_handler_t handler)
{
    if (irq >= 16) return;
    s_handlers[irq] = handler;
#if defined(ARCH_SPARC)
    arch::slavio_irq_enable(static_cast<uint32_t>(irq));
#elif defined(ARCH_SPARC64)
    arch::pci_irq_enable(static_cast<uint32_t>(irq));
#else
    (void)irq;
    (void)handler;
#endif
}

// C-linkage dispatch called from boot.s (SPARC v8)
extern "C" void sparc_irq_dispatch(uint32_t irq_index)
{
    uint32_t irq = irq_index + 1;
    if (irq < 16 && s_handlers[irq]) {
        s_handlers[irq]();
    }
    eoi(static_cast<uint8_t>(irq));
}

// C-linkage dispatch called from boot.s (SPARC v9)
// tt_value is the raw trap type (0x41..0x4F for IRQ 1..15)
extern "C" void sparc64_irq_dispatch(uint64_t tt_value)
{
    uint32_t irq = static_cast<uint32_t>(tt_value) - 0x40u;
    if (irq >= 1 && irq < 16 && s_handlers[irq]) {
        s_handlers[irq]();
    }
    eoi(static_cast<uint8_t>(irq));
}

#endif // ARCH_HAS_PIC_8259

} // namespace interrupts
} // namespace kernel

