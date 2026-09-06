// AARCH64-2 entry, EL1 normalization, and VBAR-backed exception vectors.
// Loader contract: x0=phase2 handoff, x1=owned stack top, x2=zero.

.section .text.entry, "ax"
.align 4
.global _start
.type _start, %function
.extern phase2_main
.extern phase2_exception_dispatch
_start:
    msr     daifset, #0xf
    mov     x19, x0
    mov     x20, x1
    mrs     x21, CurrentEL
    lsr     x21, x21, #2
    cmp     x21, #2
    b.eq    .Lfrom_el2
    cmp     x21, #1
    b.eq    .Lfrom_el1
    b       .Lel1_entry

.Lfrom_el2:
    mov     x0, #(1 << 31)       // HCR_EL2.RW: AArch64 at EL1
    msr     hcr_el2, x0
    mov     x0, x20
    msr     sp_el1, x0
    mrs     x0, sctlr_el1
    bic     x0, x0, #(1 << 0)
    bic     x0, x0, #(1 << 2)
    bic     x0, x0, #(1 << 12)
    msr     sctlr_el1, x0
    adr     x0, phase2_vectors
    msr     vbar_el1, x0
    mov     x0, #0x3c5           // EL1h, mask D/A/I/F on eret
    msr     spsr_el2, x0
    adr     x0, .Lel1_entry
    msr     elr_el2, x0
    isb
    eret

.Lfrom_el1:
    mrs     x0, sctlr_el1
    bic     x0, x0, #(1 << 0)
    bic     x0, x0, #(1 << 2)
    bic     x0, x0, #(1 << 12)
    msr     sctlr_el1, x0
    dsb     sy
    isb

.Lel1_entry:
    mov     sp, x20
    adr     x0, phase2_vectors
    msr     vbar_el1, x0
    isb

    adrp    x0, __bss_start
    add     x0, x0, :lo12:__bss_start
    adrp    x1, __bss_end
    add     x1, x1, :lo12:__bss_end
.Lbss_loop:
    cmp     x0, x1
    b.hs    .Lbss_done
    str     xzr, [x0], #8
    b       .Lbss_loop
.Lbss_done:
    mov     x0, x19
    mov     x1, x21
    bl      phase2_main
.Lhalt:
    wfi
    b       .Lhalt
.size _start, . - _start

.section .vectors, "ax"
.align 11
.global phase2_vectors
.type phase2_vectors, %function
phase2_vectors:
    // Current EL with SP_EL0.
    .rept 1
    b       .Lsync_sp0
    .space 124
    b       .Lirq_sp0
    .space 124
    b       .Lfatal_vector
    .space 124
    b       .Lfatal_vector
    .space 124
    .endr
    // Current EL with SP_ELx (the normal kernel class).
    b       .Lsync_spx
    .space 124
    b       .Lirq_spx
    .space 124
    b       .Lfatal_vector
    .space 124
    b       .Lfatal_vector
    .space 124
    // Lower EL, AArch64.
    .rept 2
    b       .Lfatal_vector
    .space 124
    b       .Lfatal_vector
    .space 124
    b       .Lfatal_vector
    .space 124
    b       .Lfatal_vector
    .space 124
    .endr
.size phase2_vectors, . - phase2_vectors

.section .text, "ax"
.align 4
.Lsync_spx:
    mov     x1, #0
    b       .Lexception_entry
.Lirq_spx:
    mov     x1, #1
    b       .Lexception_entry
.Lsync_sp0:
    mov     x1, #2
    b       .Lexception_entry
.Lirq_sp0:
    mov     x1, #3
    b       .Lexception_entry
.Lfatal_vector:
    mov     x1, #4

.Lexception_entry:
    sub     sp, sp, #256
    stp     x0, x1, [sp, #0]
    stp     x2, x3, [sp, #16]
    stp     x4, x5, [sp, #32]
    stp     x6, x7, [sp, #48]
    stp     x8, x9, [sp, #64]
    stp     x10, x11, [sp, #80]
    stp     x12, x13, [sp, #96]
    stp     x14, x15, [sp, #112]
    stp     x16, x17, [sp, #128]
    stp     x18, x19, [sp, #144]
    stp     x20, x21, [sp, #160]
    stp     x22, x23, [sp, #176]
    stp     x24, x25, [sp, #192]
    stp     x26, x27, [sp, #208]
    stp     x28, x29, [sp, #224]
    str     x30, [sp, #240]
    mov     x0, sp
    // x1 still contains the vector class; the frame has captured it.
    bl      phase2_exception_dispatch
    ldp     x0, x1, [sp, #0]
    ldp     x2, x3, [sp, #16]
    ldp     x4, x5, [sp, #32]
    ldp     x6, x7, [sp, #48]
    ldp     x8, x9, [sp, #64]
    ldp     x10, x11, [sp, #80]
    ldp     x12, x13, [sp, #96]
    ldp     x14, x15, [sp, #112]
    ldp     x16, x17, [sp, #128]
    ldp     x18, x19, [sp, #144]
    ldp     x20, x21, [sp, #160]
    ldp     x22, x23, [sp, #176]
    ldp     x24, x25, [sp, #192]
    ldp     x26, x27, [sp, #208]
    ldp     x28, x29, [sp, #224]
    ldr     x30, [sp, #240]
    add     sp, sp, #256
    eret
