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
#include "include/kernel/native_elf_fault.h"
#include "include/kernel/serial_debug.h"
#include "include/kernel/amd64_protected_task_contract.h"

#if defined(GXOS_ENABLE_BSP_APIC_BASE_PROBE_DIAGNOSTIC)
#include "arch/bsp_apic_base_probe.h"
#include "arch/bsp_xapic_admission.h"
#include "../../guideXOSBootLoader/bootinfo_acpi.h"
#endif

namespace kernel {
namespace interrupts {

#if defined(__x86_64__)
extern "C" void gxos_amd64_user_exit_stub();
extern "C" uint8_t gxos_amd64_user_entry_stack_bottom[];
extern "C" uint8_t gxos_amd64_user_entry_stack_top[];
#endif

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

struct TSS64 {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomapBase;
} PACKED;
static_assert(sizeof(TSS64) == sizeof(amd64_protected_task::Tss64Layout),
              "TSS64 hardware layout must match the privilege contract");

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

#if defined(__x86_64__) || defined(_M_X64)
// GDT: null, kernel code/data, user data/code, and a 64-bit TSS (two slots).
static GDTEntry s_gdt[7];
static TSS64 s_tss;
static bool s_user_selectors_valid = false;
static bool s_tss_loaded = false;
static bool s_user_exit_gate_valid = false;
#else
// GDT: 3 entries - null, code, data
static GDTEntry s_gdt[3];
#endif
static GDTPtr   s_gdtPtr;

static IDTEntry s_idt[kIDTSize];
static IDTPtr   s_idtPtr;
static irq_handler_t s_handlers[16] = { 0 };

#if defined(GXOS_ENABLE_BSP_APIC_BASE_PROBE_DIAGNOSTIC)
using arch::amd64::ApicBaseProbeStatus;
static_assert(sizeof(IDTEntry) == 16 && sizeof(IDTPtr) == 10, "AMD64 probe IDT layout");
static_assert(__builtin_offsetof(TSS64, ist2) == 44 && __builtin_offsetof(TSS64, ist3) == 52,
              "AMD64 probe emergency IST layout");
static_assert(amd64_protected_task::kKernelCodeSelector == 0x08 &&
              amd64_protected_task::kKernelDataSelector == 0x10, "AMD64 probe fixup selectors");
extern "C" ApicBaseProbeStatus gxos_amd64_read_boot_apic_base(
    uint64_t* value, const IDTPtr* probe_idt, const IDTPtr* kernel_idt, uint32_t msr_index);
extern "C" void gxos_amd64_apic_base_gp();
extern "C" void gxos_amd64_apic_base_emergency_stop();
#if defined(GXOS_ENABLE_BSP_APIC_BASE_GP_QUALIFICATION_DIAGNOSTIC)
struct ApicBaseGpEvidence {
    uint64_t vector;
    uint64_t error_code;
    uint64_t instruction_rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
    uint64_t continuation_rip;
    uint64_t continuation_reached;
};
static_assert(sizeof(ApicBaseGpEvidence) == 72, "AMD64 GP evidence layout");
extern "C" volatile ApicBaseGpEvidence gxos_amd64_apic_base_gp_evidence;
extern "C" volatile uint64_t apic_base_probe_rsp;
extern "C" volatile uint32_t apic_base_probe_msr;
extern "C" void gxos_amd64_apic_base_rdmsr();
extern "C" void gxos_amd64_apic_base_fault();
#endif
static IDTEntry s_apic_probe_idt[kIDTSize];
alignas(16) static uint8_t s_apic_probe_nmi_stack[4096];
alignas(16) static uint8_t s_apic_probe_double_fault_stack[4096];
#endif

#if defined(__x86_64__) || defined(_M_X64)
// Exception diagnostics must not use the interrupted Native ELF stack.  The
// app stack is intentionally isolated from this bounded kernel-owned stack.
extern "C" uint8_t gxos_exception_stack[16u * 1024u] = {};
#endif

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

#if defined(__x86_64__) || defined(_M_X64)
static bool segment_descriptor_matches(int index, uint8_t access, uint8_t flags)
{
    return index >= 0 && index < 5 && s_gdt[index].base_low == 0u &&
           s_gdt[index].base_mid == 0u && s_gdt[index].base_high == 0u &&
           s_gdt[index].limit_low == 0xFFFFu &&
           s_gdt[index].access == access &&
           s_gdt[index].flags_limit_high ==
               static_cast<uint8_t>(0x0Fu | (flags & 0xF0u));
}

static bool tss_descriptor_matches()
{
    const uint64_t base = static_cast<uint64_t>(s_gdt[5].base_low) |
        (static_cast<uint64_t>(s_gdt[5].base_mid) << 16) |
        (static_cast<uint64_t>(s_gdt[5].base_high) << 24) |
        (static_cast<uint64_t>(s_gdt[6].limit_low) << 32) |
        (static_cast<uint64_t>(s_gdt[6].base_low) << 48);
    return base == reinterpret_cast<uintptr_t>(&s_tss) &&
           s_gdt[5].limit_low == sizeof(TSS64) - 1u &&
           s_gdt[5].flags_limit_high == 0u &&
           s_gdt[5].access == 0x89u &&
           s_gdt[6].base_mid == 0u && s_gdt[6].access == 0u &&
           s_gdt[6].flags_limit_high == 0u && s_gdt[6].base_high == 0u;
}

static void set_tss_descriptor(int index, uint64_t base, uint32_t limit)
{
    set_gdt_entry(index, static_cast<uint32_t>(base), limit, 0x89, 0x00);
    s_gdt[index + 1].limit_low = static_cast<uint16_t>((base >> 32) & 0xFFFFu);
    s_gdt[index + 1].base_low = static_cast<uint16_t>((base >> 48) & 0xFFFFu);
    s_gdt[index + 1].base_mid = 0;
    s_gdt[index + 1].access = 0;
    s_gdt[index + 1].flags_limit_high = 0;
    s_gdt[index + 1].base_high = 0;
}
#endif

static void gdt_init()
{
    // Entry 0: Null descriptor (required by x86)
    set_gdt_entry(0, 0, 0, 0, 0);
    
#if defined(__x86_64__) || defined(_M_X64)
    // Entry 1: 64-bit code segment (selector 0x08)
    // Access: Present (0x80) | DPL=0 (0x00) | Type=code,exec,read (0x1A) = 0x9A
    // Flags: Long mode (0x20) = 0x20 (no 4K granularity needed in 64-bit)
    set_gdt_entry(1, 0, 0xFFFFF,
                  amd64_protected_task::kKernelCodeAccess,
                  amd64_protected_task::kLongCodeFlags);
    
    // Entry 2: 64-bit data segment (selector 0x10)
    // Access: Present (0x80) | DPL=0 (0x00) | Type=data,read,write (0x12) = 0x92
    // Flags: 0x00 (no long mode bit for data segments)
    set_gdt_entry(2, 0, 0xFFFFF,
                  amd64_protected_task::kKernelDataAccess,
                  amd64_protected_task::kLongDataFlags);

    // Entry 3: user data segment (selector 0x18, RPL3 selector 0x1B).
    set_gdt_entry(3, 0, 0xFFFFF,
                  amd64_protected_task::kUserDataAccess, 0xC0);

    // Entry 4: 64-bit user code segment (selector 0x20, RPL3 selector 0x23).
    set_gdt_entry(4, 0, 0xFFFFF,
                  amd64_protected_task::kUserCodeAccess,
                  amd64_protected_task::kLongCodeFlags);

    s_tss = {};
    s_tss.iomapBase = sizeof(TSS64);
    s_tss.ist1 = (reinterpret_cast<uint64_t>(gxos_exception_stack) + sizeof(gxos_exception_stack)) & ~0x0Full;
#if defined(GXOS_ENABLE_BSP_APIC_BASE_PROBE_DIAGNOSTIC)
    s_tss.ist2 = reinterpret_cast<uint64_t>(s_apic_probe_nmi_stack + sizeof(s_apic_probe_nmi_stack));
    s_tss.ist3 = reinterpret_cast<uint64_t>(s_apic_probe_double_fault_stack + sizeof(s_apic_probe_double_fault_stack));
#endif
    set_tss_descriptor(5, reinterpret_cast<uint64_t>(&s_tss), sizeof(TSS64) - 1u);
    s_user_selectors_valid =
        amd64_protected_task::valid_selector_layout() &&
        segment_descriptor_matches(1,
            amd64_protected_task::kKernelCodeAccess,
            amd64_protected_task::kLongCodeFlags) &&
        segment_descriptor_matches(2,
            amd64_protected_task::kKernelDataAccess,
            amd64_protected_task::kLongDataFlags) &&
        segment_descriptor_matches(3,
            amd64_protected_task::kUserDataAccess, 0xC0u) &&
        segment_descriptor_matches(4,
            amd64_protected_task::kUserCodeAccess,
            amd64_protected_task::kLongCodeFlags) &&
        tss_descriptor_matches() &&
        s_tss.iomapBase == sizeof(TSS64);
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

#if defined(__x86_64__)
    const uint16_t tssSelector = amd64_protected_task::kTssSelector;
    asm volatile ("ltr %0" : : "r"(tssSelector) : "memory");
    uint16_t loadedTaskRegister = 0u;
    asm volatile ("str %0" : "=rm"(loadedTaskRegister));
    s_tss_loaded = loadedTaskRegister == amd64_protected_task::kTssSelector &&
                   s_gdt[5].access == 0x8Bu &&
                   (s_gdtPtr.limit == sizeof(s_gdt) - 1u);
    serial::puts("[GDT] GDT loaded with code/data/TSS entries, segments reloaded\n");
#else
    serial::puts("[GDT] GDT loaded with 3 entries, segments reloaded\n");
#endif
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
#if defined(GXOS_ENABLE_BSP_APIC_BASE_PROBE_DIAGNOSTIC)
static void set_idt_entry(int index, uint64_t addr, uint8_t type_attr, uint8_t ist = 0,
                          IDTEntry* idt = s_idt)
#else
static void set_idt_entry(int index, uint64_t addr, uint8_t type_attr, uint8_t ist = 0)
#endif
{
#if !defined(GXOS_ENABLE_BSP_APIC_BASE_PROBE_DIAGNOSTIC)
    auto* idt = s_idt;
#endif
    idt[index].offset_low  = (uint16_t)(addr & 0xFFFF);
    idt[index].selector    = 0x08;
    idt[index].ist         = ist & 0x07u;
    idt[index].type_attr   = type_attr;
    idt[index].offset_mid  = (uint16_t)((addr >> 16) & 0xFFFF);
    idt[index].offset_high = (uint32_t)((addr >> 32) & 0xFFFFFFFF);
    idt[index].reserved    = 0;
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
// The configured IST stack top is 16-byte aligned. A 64-bit stack switch
// pushes SS/RSP/RFLAGS/CS/RIP, then this stub saves volatile GPRs. Align each
// ABI call explicitly and restore the exact interrupted frame pointer.
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
        "    sub $32, %rsp\n"                              \
        "    mov $" #n ", %ecx\n"                          \
        "    call irq_dispatch\n"                          \
        "    add $32, %rsp\n"                              \
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
        "    push %r12\n"                                  \
        "    mov %rsp, %r12\n"                             \
        "    and $-16, %rsp\n"                             \
        "    mov $" #n ", %edi\n"                          \
        "    call irq_dispatch\n"                          \
        "    mov %r12, %rsp\n"                             \
        "    pop %r12\n"                                   \
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

#if defined(__x86_64__)

// Every amd64 CPU exception gets a real IDT target.  The stubs normalize the
// error-code and no-error forms, save all general-purpose registers, and
// switch to the kernel-owned exception stack before entering C++.
#define MAKE_EXCEPTION_STUB_NO_ERROR(n)                    \
    extern "C" void exception_stub_##n();                 \
    asm(                                                    \
        ".intel_syntax noprefix\n"                       \
        ".global exception_stub_" #n "\n"                 \
        "exception_stub_" #n ":\n"                      \
        "    push 0\n"                                    \
        "    push " #n "\n"                                \
        "    push r15\n"                                   \
        "    push r14\n"                                   \
        "    push r13\n"                                   \
        "    push r12\n"                                   \
        "    push r11\n"                                   \
        "    push r10\n"                                   \
        "    push r9\n"                                    \
        "    push r8\n"                                    \
        "    push rdi\n"                                   \
        "    push rsi\n"                                   \
        "    push rbp\n"                                   \
        "    push rbx\n"                                   \
        "    push rdx\n"                                   \
        "    push rcx\n"                                   \
        "    push rax\n"                                   \
        "    mov r12, rsp\n"                               \
        "    lea rsp, [rip + gxos_exception_stack + 16384]\n" \
        "    and rsp, -16\n"                               \
        "    sub rsp, 32\n"                                \
        "    mov rcx, r12\n"                               \
        "    call gxos_native_exception_dispatch\n"         \
        "    add rsp, 32\n"                                \
        "    test rax, rax\n"                              \
        "    jnz .native_exception_recover_" #n "\n"       \
        "    mov rsp, r12\n"                               \
        "    pop rax\n"                                   \
        "    pop rcx\n"                                   \
        "    pop rdx\n"                                   \
        "    pop rbx\n"                                   \
        "    pop rbp\n"                                   \
        "    pop rsi\n"                                   \
        "    pop rdi\n"                                   \
        "    pop r8\n"                                    \
        "    pop r9\n"                                    \
        "    pop r10\n"                                   \
        "    pop r11\n"                                   \
        "    pop r12\n"                                   \
        "    pop r13\n"                                   \
        "    pop r14\n"                                   \
        "    pop r15\n"                                   \
        "    add rsp, 16\n"                                \
        "    iretq\n"                                      \
        ".native_exception_recover_" #n ":\n"             \
        "    mov rsp, [rip + gxos_native_fault_recovery_rsp]\n" \
        "    jmp [rip + gxos_native_fault_recovery_rip]\n"   \
        ".att_syntax prefix\n"                            \
    );

#define MAKE_EXCEPTION_STUB_WITH_ERROR(n)                  \
    extern "C" void exception_stub_##n();                 \
    asm(                                                    \
        ".intel_syntax noprefix\n"                       \
        ".global exception_stub_" #n "\n"                 \
        "exception_stub_" #n ":\n"                      \
        "    push " #n "\n"                                \
        "    push r15\n"                                   \
        "    push r14\n"                                   \
        "    push r13\n"                                   \
        "    push r12\n"                                   \
        "    push r11\n"                                   \
        "    push r10\n"                                   \
        "    push r9\n"                                    \
        "    push r8\n"                                    \
        "    push rdi\n"                                   \
        "    push rsi\n"                                   \
        "    push rbp\n"                                   \
        "    push rbx\n"                                   \
        "    push rdx\n"                                   \
        "    push rcx\n"                                   \
        "    push rax\n"                                   \
        "    mov r12, rsp\n"                               \
        "    lea rsp, [rip + gxos_exception_stack + 16384]\n" \
        "    and rsp, -16\n"                               \
        "    sub rsp, 32\n"                                \
        "    mov rcx, r12\n"                               \
        "    call gxos_native_exception_dispatch\n"         \
        "    add rsp, 32\n"                                \
        "    test rax, rax\n"                              \
        "    jnz .native_exception_recover_" #n "\n"       \
        "    mov rsp, r12\n"                               \
        "    pop rax\n"                                   \
        "    pop rcx\n"                                   \
        "    pop rdx\n"                                   \
        "    pop rbx\n"                                   \
        "    pop rbp\n"                                   \
        "    pop rsi\n"                                   \
        "    pop rdi\n"                                   \
        "    pop r8\n"                                    \
        "    pop r9\n"                                    \
        "    pop r10\n"                                   \
        "    pop r11\n"                                   \
        "    pop r12\n"                                   \
        "    pop r13\n"                                   \
        "    pop r14\n"                                   \
        "    pop r15\n"                                   \
        "    add rsp, 8\n"                                 \
        "    iretq\n"                                      \
        ".native_exception_recover_" #n ":\n"             \
        "    mov rsp, [rip + gxos_native_fault_recovery_rsp]\n" \
        "    jmp [rip + gxos_native_fault_recovery_rip]\n"   \
        ".att_syntax prefix\n"                            \
    );

MAKE_EXCEPTION_STUB_NO_ERROR(0)
MAKE_EXCEPTION_STUB_NO_ERROR(1)
MAKE_EXCEPTION_STUB_NO_ERROR(2)
MAKE_EXCEPTION_STUB_NO_ERROR(3)
MAKE_EXCEPTION_STUB_NO_ERROR(4)
MAKE_EXCEPTION_STUB_NO_ERROR(5)
MAKE_EXCEPTION_STUB_NO_ERROR(6)
MAKE_EXCEPTION_STUB_NO_ERROR(7)
MAKE_EXCEPTION_STUB_WITH_ERROR(8)
MAKE_EXCEPTION_STUB_NO_ERROR(9)
MAKE_EXCEPTION_STUB_WITH_ERROR(10)
MAKE_EXCEPTION_STUB_WITH_ERROR(11)
MAKE_EXCEPTION_STUB_WITH_ERROR(12)
MAKE_EXCEPTION_STUB_WITH_ERROR(13)
MAKE_EXCEPTION_STUB_WITH_ERROR(14)
MAKE_EXCEPTION_STUB_NO_ERROR(15)
MAKE_EXCEPTION_STUB_WITH_ERROR(16)
MAKE_EXCEPTION_STUB_NO_ERROR(17)
MAKE_EXCEPTION_STUB_NO_ERROR(18)
MAKE_EXCEPTION_STUB_NO_ERROR(19)
MAKE_EXCEPTION_STUB_NO_ERROR(20)
MAKE_EXCEPTION_STUB_NO_ERROR(21)
MAKE_EXCEPTION_STUB_NO_ERROR(22)
MAKE_EXCEPTION_STUB_NO_ERROR(23)
MAKE_EXCEPTION_STUB_NO_ERROR(24)
MAKE_EXCEPTION_STUB_NO_ERROR(25)
MAKE_EXCEPTION_STUB_NO_ERROR(26)
MAKE_EXCEPTION_STUB_NO_ERROR(27)
MAKE_EXCEPTION_STUB_NO_ERROR(28)
MAKE_EXCEPTION_STUB_WITH_ERROR(29)
MAKE_EXCEPTION_STUB_WITH_ERROR(30)
MAKE_EXCEPTION_STUB_NO_ERROR(31)

typedef void (*exception_stub_fn)();
static exception_stub_fn s_exception_stubs[32] = {
    exception_stub_0,  exception_stub_1,  exception_stub_2,  exception_stub_3,
    exception_stub_4,  exception_stub_5,  exception_stub_6,  exception_stub_7,
    exception_stub_8,  exception_stub_9,  exception_stub_10, exception_stub_11,
    exception_stub_12, exception_stub_13, exception_stub_14, exception_stub_15,
    exception_stub_16, exception_stub_17, exception_stub_18, exception_stub_19,
    exception_stub_20, exception_stub_21, exception_stub_22, exception_stub_23,
    exception_stub_24, exception_stub_25, exception_stub_26, exception_stub_27,
    exception_stub_28, exception_stub_29, exception_stub_30, exception_stub_31
};

#endif // defined(__x86_64__)

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

#if defined(GXOS_ENABLE_BSP_APIC_BASE_PROBE_DIAGNOSTIC)
static ApicBaseProbeStatus read_boot_apic_base(uint64_t& value)
{
    // Only the cold BSP interrupt initialization can call this reader. Once
    // consumed, even a later reinitialization cannot arm it again.
    static bool attempted = false;
    uint16_t task_register = 0;
    asm volatile ("str %0" : "=rm"(task_register));
    if (attempted || !s_user_selectors_valid || !s_tss_loaded || s_tss.rsp0 != 0 ||
        task_register != amd64_protected_task::kTssSelector) {
        return ApicBaseProbeStatus::InvalidContext;
    }
    attempted = true;

    // Build a detached IDT. The live IDT is never edited and the assembly
    // restores the original IDTR before exposing a result.
    for (int i = 0; i < kIDTSize; ++i) s_apic_probe_idt[i] = s_idt[i];
    const auto emergency = reinterpret_cast<uint64_t>(gxos_amd64_apic_base_emergency_stop);
    set_idt_entry(2, emergency, 0x8E, 2, s_apic_probe_idt);
    set_idt_entry(8, emergency, 0x8E, 3, s_apic_probe_idt);
    set_idt_entry(13, reinterpret_cast<uint64_t>(gxos_amd64_apic_base_gp),
                  0x8E, 0, s_apic_probe_idt);
    const IDTPtr probe_idtr{sizeof(s_apic_probe_idt) - 1u,
                            reinterpret_cast<uint64_t>(s_apic_probe_idt)};
#if defined(GXOS_ENABLE_BSP_APIC_BASE_GP_QUALIFICATION_DIAGNOSTIC)
    const auto cleanup_verified = []() {
        IDTPtr current{};
        asm volatile("sidt %0" : "=m"(current));
        return current.limit == s_idtPtr.limit && current.base == s_idtPtr.base &&
               apic_base_probe_rsp == 0 && apic_base_probe_msr == 0;
    };
    const auto fail_closed = []() {
        serial::puts("[CPU7B2R1] probe-cleanup=FAILED fail-stop=1\n");
        gxos_amd64_apic_base_emergency_stop();
        for (;;) arch::halt();
    };

    // This reserved MSR read exists only in the explicitly enabled R1 guest
    // lane. Its destination is a sentinel and the assembly never publishes
    // a value for this index, even if a hypervisor incorrectly returns one.
    constexpr uint32_t kQualificationInvalidMsr = 0xFFFFFFFFu;
    constexpr uint64_t kSuppressedResult = 0xD15EA5EDBAD0C0DEull;
    uint64_t suppressed_result = kSuppressedResult;
    const auto fault_status = gxos_amd64_read_boot_apic_base(
        &suppressed_result, &probe_idtr, &s_idtPtr, kQualificationInvalidMsr);
    if (!cleanup_verified() || suppressed_result != kSuppressedResult) fail_closed();

    ApicBaseGpEvidence evidence{};
    evidence.vector = gxos_amd64_apic_base_gp_evidence.vector;
    evidence.error_code = gxos_amd64_apic_base_gp_evidence.error_code;
    evidence.instruction_rip = gxos_amd64_apic_base_gp_evidence.instruction_rip;
    evidence.cs = gxos_amd64_apic_base_gp_evidence.cs;
    evidence.rflags = gxos_amd64_apic_base_gp_evidence.rflags;
    evidence.rsp = gxos_amd64_apic_base_gp_evidence.rsp;
    evidence.ss = gxos_amd64_apic_base_gp_evidence.ss;
    evidence.continuation_rip = gxos_amd64_apic_base_gp_evidence.continuation_rip;
    evidence.continuation_reached = gxos_amd64_apic_base_gp_evidence.continuation_reached;
    const uint64_t rejected_flags = (1ull << 8) | (1ull << 9) | (1ull << 10) |
                                    (1ull << 14) | (1ull << 17);
    const bool delivered = fault_status == ApicBaseProbeStatus::GeneralProtection &&
        evidence.vector == 13 && evidence.error_code == 0 &&
        evidence.instruction_rip == reinterpret_cast<uint64_t>(gxos_amd64_apic_base_rdmsr) &&
        evidence.cs == amd64_protected_task::kKernelCodeSelector &&
        evidence.ss == amd64_protected_task::kKernelDataSelector && evidence.rsp != 0 &&
        (evidence.rflags & rejected_flags) == 0 &&
        evidence.continuation_rip == reinterpret_cast<uint64_t>(gxos_amd64_apic_base_fault) &&
        evidence.continuation_reached == 1;

    serial::puts("[CPU7B2R1] invalid-msr-gp=");
    if (delivered) {
        serial::puts("delivered vector=13 error=0 rip=0x");
        serial::put_hex64(evidence.instruction_rip);
        serial::puts(" cs=0x");
        serial::put_hex64(evidence.cs);
        serial::puts(" rsp=0x");
        serial::put_hex64(evidence.rsp);
        serial::puts(" rflags=0x");
        serial::put_hex64(evidence.rflags);
        serial::puts(" ss=0x");
        serial::put_hex64(evidence.ss);
        serial::puts(" continuation=0x");
        serial::put_hex64(evidence.continuation_rip);
        serial::puts(" reached=1 result=suppressed cleanup=clean\n");
    } else if (fault_status == ApicBaseProbeStatus::Observed) {
        serial::puts("not-delivered returned-value=suppressed cleanup=clean\n");
    } else {
        serial::puts("rejected status=");
        serial::put_hex64(static_cast<uint64_t>(fault_status));
        serial::puts(" result=suppressed cleanup=clean\n");
    }

    const auto followup = gxos_amd64_read_boot_apic_base(
        &value, &probe_idtr, &s_idtPtr, 0x1Bu);
    if (!cleanup_verified()) fail_closed();
    serial::puts("[CPU7B2R1] followup-apic-base=");
    serial::puts(followup == ApicBaseProbeStatus::Observed ? "observed" : "failed");
    serial::puts(" cleanup=clean\n");
    return followup;
#else
    return gxos_amd64_read_boot_apic_base(&value, &probe_idtr, &s_idtPtr, 0x1Bu);
#endif
}

static void init_pic(const guideXOS::BootInfo* probe_boot)
#else
void init()
#endif
{
    // CRITICAL: Set up our own GDT first, before any interrupt handling.
    // The UEFI-provided GDT may not be accessible after ExitBootServices
    // or may not be mapped in the kernel's page tables.
    gdt_init();

    for (int i = 0; i < kIDTSize; ++i) {
        set_idt_entry(i, 0, 0);
    }

#if defined(__x86_64__)
    for (int i = 0; i < 32; ++i) {
        set_idt_entry(i, reinterpret_cast<uint64_t>(s_exception_stubs[i]), 0x8E);
    }
#endif

    pic_remap();

    for (int i = 0; i < 16; ++i) {
#if defined(__x86_64__) || defined(_M_X64)
        set_idt_entry(kIRQBase + i,
                      reinterpret_cast<uint64_t>(s_stubs[i]),
                      0x8E, 1);
#else
        set_idt_entry(kIRQBase + i,
                      reinterpret_cast<uint32_t>(s_stubs[i]),
                      0x8E);
#endif
    }

#if defined(__x86_64__) && defined(GXOS_ENABLE_CPL3_DIAGNOSTIC)
    set_idt_entry(0x80, reinterpret_cast<uint64_t>(gxos_amd64_user_exit_stub),
                  0xEE); // Present, DPL3, 64-bit interrupt gate.
    s_user_exit_gate_valid = s_idt[0x80].selector ==
            amd64_protected_task::kKernelCodeSelector &&
        s_idt[0x80].type_attr == 0xEEu && s_idt[0x80].reserved == 0u &&
        s_idt[0x80].offset_low ==
            static_cast<uint16_t>(reinterpret_cast<uintptr_t>(gxos_amd64_user_exit_stub)) &&
        s_idt[0x80].offset_mid == static_cast<uint16_t>(
            reinterpret_cast<uintptr_t>(gxos_amd64_user_exit_stub) >> 16) &&
        s_idt[0x80].offset_high == static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(gxos_amd64_user_exit_stub) >> 32);
#endif

    load_idt();
#if defined(GXOS_ENABLE_BSP_APIC_BASE_PROBE_DIAGNOSTIC)
    if (probe_boot) {
        const auto& roster = arch::amd64::boot_per_cpu_state();
        const auto sample = arch::amd64::sample_bootstrap_apic_base(roster, read_boot_apic_base);
        serial::puts("[CPU7B2] apic-base-msr=");
        serial::puts(arch::amd64::apic_base_probe_status_name(sample.status));
        if (sample.observation()) {
            serial::puts(" raw=0x");
            serial::put_hex64(sample.value);
        }
        serial::putc('\n');
        arch::amd64::report_bsp_xapic_admission(roster,
            guideXOS::bootinfo_acpi_interrupt_topology(probe_boot), sample.observation());
    }
#endif
    arch::enable_interrupts();

    serial::puts("[IRQ] IDT loaded, PIC remapped, interrupts enabled\n");
    serial::puts("[IRQ] Master PIC mask: 0x");
    serial::put_hex8(arch::inb(kPICMasterData));
    serial::puts(" Slave PIC mask: 0x");
    serial::put_hex8(arch::inb(kPICSlaveData));
    serial::putc('\n');
}

#if defined(GXOS_ENABLE_BSP_APIC_BASE_PROBE_DIAGNOSTIC)
void init()
{
    init_pic(nullptr);
}

void init_with_bsp_apic_base_probe(const guideXOS::BootInfo* boot)
{
    init_pic(boot);
}
#endif

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

bool set_user_entry_stack(uint64_t stackBase, uint64_t stackBytes)
{
#if defined(__x86_64__)
    const uint64_t expectedBase =
        reinterpret_cast<uintptr_t>(gxos_amd64_user_entry_stack_bottom);
    const uint64_t expectedTop =
        reinterpret_cast<uintptr_t>(gxos_amd64_user_entry_stack_top);
    if (!s_user_selectors_valid || !s_tss_loaded || expectedTop <= expectedBase ||
        stackBase != expectedBase || stackBytes != expectedTop - expectedBase ||
        (expectedBase & 0xFu) != 0u || (expectedTop & 0xFu) != 0u ||
        stackBytes < 4096u) {
        return false;
    }
    s_tss.rsp0 = expectedTop;
    return s_tss.rsp0 == expectedTop;
#else
    (void)stackBase;
    (void)stackBytes;
    return false;
#endif
}

bool user_selectors_valid()
{
#if defined(__x86_64__)
    return s_user_selectors_valid && s_tss_loaded;
#else
    return false;
#endif
}

bool user_exit_gate_valid()
{
#if defined(__x86_64__) && defined(GXOS_ENABLE_CPL3_DIAGNOSTIC)
    return s_user_exit_gate_valid && user_selectors_valid();
#else
    return false;
#endif
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
