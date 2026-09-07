// Assembly-assisted callee-saved register integrity probe.
// x19-x29 are loaded with a task-specific sequence across a real yield.

.section .text, "ax"
.align 4
.global phase3_register_probe
.type phase3_register_probe, %function
phase3_register_probe:
    // Preserve the caller's complete callee-saved set.  The patterns are
    // live across scheduler_yield, but must not leak into the C caller.
    sub     sp, sp, #112
    stp     x19, x20, [sp, #0]
    stp     x21, x22, [sp, #16]
    stp     x23, x24, [sp, #32]
    stp     x25, x26, [sp, #48]
    stp     x27, x28, [sp, #64]
    stp     x29, x30, [sp, #80]
    stp     x0, x1, [sp, #96]
    mov     x19, x1
    add     x20, x1, #1
    add     x21, x1, #2
    add     x22, x1, #3
    add     x23, x1, #4
    add     x24, x1, #5
    add     x25, x1, #6
    add     x26, x1, #7
    add     x27, x1, #8
    add     x28, x1, #9
    add     x29, x1, #10
    bl      scheduler_yield

    ldr     x0, [sp, #96]
    ldr     x1, [sp, #104]
    cmp     x19, x1
    b.ne    .Lfail19
    add     x2, x1, #1
    cmp     x20, x2
    b.ne    .Lfail20
    add     x2, x1, #2
    cmp     x21, x2
    b.ne    .Lfail21
    add     x2, x1, #3
    cmp     x22, x2
    b.ne    .Lfail22
    add     x2, x1, #4
    cmp     x23, x2
    b.ne    .Lfail23
    add     x2, x1, #5
    cmp     x24, x2
    b.ne    .Lfail24
    add     x2, x1, #6
    cmp     x25, x2
    b.ne    .Lfail25
    add     x2, x1, #7
    cmp     x26, x2
    b.ne    .Lfail26
    add     x2, x1, #8
    cmp     x27, x2
    b.ne    .Lfail27
    add     x2, x1, #9
    cmp     x28, x2
    b.ne    .Lfail28
    add     x2, x1, #10
    cmp     x29, x2
    b.ne    .Lfail29
    ldp     x19, x20, [sp, #0]
    ldp     x21, x22, [sp, #16]
    ldp     x23, x24, [sp, #32]
    ldp     x25, x26, [sp, #48]
    ldp     x27, x28, [sp, #64]
    ldp     x29, x30, [sp, #80]
    add     sp, sp, #112
    ret

.Lfail19:
    ldr     x0, [sp, #96]
    mov     x2, x1
    mov     x3, x19
    mov     x1, #19
    b       .Lreport
.Lfail20:
    ldr     x0, [sp, #96]
    add     x2, x1, #1
    mov     x3, x20
    mov     x1, #20
    b       .Lreport
.Lfail21:
    ldr     x0, [sp, #96]
    add     x2, x1, #2
    mov     x3, x21
    mov     x1, #21
    b       .Lreport
.Lfail22:
    ldr     x0, [sp, #96]
    add     x2, x1, #3
    mov     x3, x22
    mov     x1, #22
    b       .Lreport
.Lfail23:
    ldr     x0, [sp, #96]
    add     x2, x1, #4
    mov     x3, x23
    mov     x1, #23
    b       .Lreport
.Lfail24:
    ldr     x0, [sp, #96]
    add     x2, x1, #5
    mov     x3, x24
    mov     x1, #24
    b       .Lreport
.Lfail25:
    ldr     x0, [sp, #96]
    add     x2, x1, #6
    mov     x3, x25
    mov     x1, #25
    b       .Lreport
.Lfail26:
    ldr     x0, [sp, #96]
    add     x2, x1, #7
    mov     x3, x26
    mov     x1, #26
    b       .Lreport
.Lfail27:
    ldr     x0, [sp, #96]
    add     x2, x1, #8
    mov     x3, x27
    mov     x1, #27
    b       .Lreport
.Lfail28:
    ldr     x0, [sp, #96]
    add     x2, x1, #9
    mov     x3, x28
    mov     x1, #28
    b       .Lreport
.Lfail29:
    ldr     x0, [sp, #96]
    add     x2, x1, #10
    mov     x3, x29
    mov     x1, #29
.Lreport:
    ldp     x19, x20, [sp, #0]
    ldp     x21, x22, [sp, #16]
    ldp     x23, x24, [sp, #32]
    ldp     x25, x26, [sp, #48]
    ldp     x27, x28, [sp, #64]
    ldp     x29, x30, [sp, #80]
    add     sp, sp, #112
    b       phase3_register_failure
.size phase3_register_probe, . - phase3_register_probe
