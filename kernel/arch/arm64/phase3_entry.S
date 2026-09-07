// Phase-3 ARM64 entry and exception-return bridge.
// Loader contract: x0=Phase-2 handoff, x1=owned stack top, x2=zero.

.section .text.entry, "ax"
.align 4
.global _start
.type _start, %function
.extern phase3_main
.extern phase3_exception_dispatch
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
    mov     x0, #(1 << 31)
    msr     hcr_el2, x0
    mov     x0, x20
    msr     sp_el1, x0
    mrs     x0, sctlr_el1
    bic     x0, x0, #(1 << 0)
    bic     x0, x0, #(1 << 2)
    bic     x0, x0, #(1 << 12)
    msr     sctlr_el1, x0
    adr     x0, phase3_vectors
    msr     vbar_el1, x0
    mov     x0, #0x3c5
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
    adr     x0, phase3_vectors
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
    bl      phase3_main
.Lhalt:
    wfi
    b       .Lhalt
.size _start, . - _start

.section .vectors, "ax"
.align 11
.global phase3_vectors
.type phase3_vectors, %function
phase3_vectors:
    // Current EL with SP_EL0.
    b       .Lsync_sp0
    .space 124
    b       .Lirq_sp0
    .space 124
    b       .Lfatal_vector
    .space 124
    b       .Lfatal_vector
    .space 124
    // Current EL with SP_ELx.
    b       .Lsync_spx
    .space 124
    b       .Lirq_spx
    .space 124
    b       .Lfatal_vector
    .space 124
    b       .Lfatal_vector
    .space 124
    // Lower EL, AArch64 and AArch32.
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
.size phase3_vectors, . - phase3_vectors

.section .text, "ax"
.align 4
// Do not overwrite a live register before the frame is saved.  The vector
// class is selected by the entry label after all x0-x30 have been captured.
.Lsync_spx:  b .Lexception_entry_sync
.Lirq_spx:   b .Lexception_entry_irq
.Lsync_sp0:  b .Lexception_entry_sync_sp0
.Lirq_sp0:   b .Lexception_entry_irq_sp0
.Lfatal_vector: b .Lexception_entry_fatal

.Lexception_entry_sync:
    sub     sp, sp, #16
    str     x16, [sp]
    mov     x16, #0
    sub     sp, sp, #256
    b       .Lsave_common
.Lexception_entry_irq:
    sub     sp, sp, #16
    str     x16, [sp]
    mov     x16, #1
    sub     sp, sp, #256
    b       .Lsave_common
.Lexception_entry_sync_sp0:
    sub     sp, sp, #16
    str     x16, [sp]
    mov     x16, #2
    sub     sp, sp, #256
    b       .Lsave_common
.Lexception_entry_irq_sp0:
    sub     sp, sp, #16
    str     x16, [sp]
    mov     x16, #3
    sub     sp, sp, #256
    b       .Lsave_common
.Lexception_entry_fatal:
    sub     sp, sp, #16
    str     x16, [sp]
    mov     x16, #4
    sub     sp, sp, #256
    b       .Lsave_common

.Lsave_common:
    // The 16-byte scratch prefix preserves the interrupted x16 while x16
    // carries the vector class.  The full 31-register frame stays in x0..x30
    // order at offsets 0..240.
    stp     x0, x1, [sp, #0]
    stp     x2, x3, [sp, #16]
    stp     x4, x5, [sp, #32]
    stp     x6, x7, [sp, #48]
    stp     x8, x9, [sp, #64]
    stp     x10, x11, [sp, #80]
    stp     x12, x13, [sp, #96]
    stp     x14, x15, [sp, #112]
    stp     x17, x18, [sp, #136]
    ldr     x17, [sp, #256]
    str     x17, [sp, #128]
    stp     x19, x20, [sp, #152]
    stp     x21, x22, [sp, #168]
    stp     x23, x24, [sp, #184]
    stp     x25, x26, [sp, #200]
    stp     x27, x28, [sp, #216]
    stp     x29, x30, [sp, #232]
    str     x16, [sp, #248]
    mov     x0, sp
    // The original x16 is in the frame at [sp,#128]; the vector class is in
    // the spare word at [sp,#248].
    ldr     x1, [sp, #248]
    bl      phase3_exception_dispatch
    cbz     x0, .Lrestore_current
    b       phase3_restore_irq_context

.Lrestore_current:
    ldr     x0, [sp, #0]
    ldp     x1, x2, [sp, #8]
    ldp     x3, x4, [sp, #24]
    ldp     x5, x6, [sp, #40]
    ldp     x7, x8, [sp, #56]
    ldp     x9, x10, [sp, #72]
    ldp     x11, x12, [sp, #88]
    ldp     x13, x14, [sp, #104]
    ldr     x15, [sp, #120]
    ldr     x16, [sp, #128]
    ldp     x17, x18, [sp, #136]
    ldp     x19, x20, [sp, #152]
    ldp     x21, x22, [sp, #168]
    ldp     x23, x24, [sp, #184]
    ldp     x25, x26, [sp, #200]
    ldp     x27, x28, [sp, #216]
    ldp     x29, x30, [sp, #232]
    add     sp, sp, #256
    add     sp, sp, #16
    eret

.global phase3_restore_irq_context
.type phase3_restore_irq_context, %function
phase3_restore_irq_context:
    // x0 = Arm64InterruptContext*.  Switch to the destination task's stack
    // and materialize the opaque context into an exception-return frame.
    // x30 is used as the source pointer until the final saved register is
    // copied, so no source field is confused with the frame itself.
    mov     x30, x0
    ldr     x9, [x30, #248]
    sub     x9, x9, #256
    mov     sp, x9
    ldr     x10, [x30, #256]
    msr     elr_el1, x10
    ldr     x10, [x30, #264]
    msr     spsr_el1, x10

    // Copy x0-x30 into the destination frame before loading them.  The
    // source object is not on the destination stack and cannot be read from
    // the frame address by the subsequent restore sequence.
    ldp     x0, x1, [x30, #0]
    stp     x0, x1, [sp, #0]
    ldp     x2, x3, [x30, #16]
    stp     x2, x3, [sp, #16]
    ldp     x4, x5, [x30, #32]
    stp     x4, x5, [sp, #32]
    ldp     x6, x7, [x30, #48]
    stp     x6, x7, [sp, #48]
    ldp     x8, x9, [x30, #64]
    stp     x8, x9, [sp, #64]
    ldp     x10, x11, [x30, #80]
    stp     x10, x11, [sp, #80]
    ldp     x12, x13, [x30, #96]
    stp     x12, x13, [sp, #96]
    ldp     x14, x15, [x30, #112]
    stp     x14, x15, [sp, #112]
    ldp     x16, x17, [x30, #128]
    stp     x16, x17, [sp, #128]
    ldp     x18, x19, [x30, #144]
    stp     x18, x19, [sp, #144]
    ldp     x20, x21, [x30, #160]
    stp     x20, x21, [sp, #160]
    ldp     x22, x23, [x30, #176]
    stp     x22, x23, [sp, #176]
    ldp     x24, x25, [x30, #192]
    stp     x24, x25, [sp, #192]
    ldp     x26, x27, [x30, #208]
    stp     x26, x27, [sp, #208]
    ldr     x28, [x30, #224]
    str     x28, [sp, #224]
    ldr     x29, [x30, #232]
    str     x29, [sp, #232]
    ldr     x9, [x30, #240]
    str     x9, [sp, #240]

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
.size phase3_restore_irq_context, . - phase3_restore_irq_context
