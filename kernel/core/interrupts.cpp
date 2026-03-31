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

static IDTEntry s_idt[kIDTSize];
static IDTPtr   s_idtPtr;
static irq_handler_t s_handlers[16] = { 0 };

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

// 64-bit ISR stubs (System V ABI: first arg in %rdi)
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
        "    mov $" #n ", %edi\n"                           \
        "    call irq_dispatch\n"                           \
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
    if (irq < 16) {
        s_handlers[irq] = handler;
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

