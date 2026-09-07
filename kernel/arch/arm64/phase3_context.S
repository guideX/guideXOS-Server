// AAPCS64 kernel-thread context.
//
// Layout at the context pointer:
//   x19..x30 (12 words), SP (one word).  The frame is always 16-byte aligned.

.section .text, "ax"
.align 4
.global phase3_context_switch
.type phase3_context_switch, %function
phase3_context_switch:
    // x0 = void** old_context, x1 = new_context
    sub     sp, sp, #112
    stp     x19, x20, [sp, #0]
    stp     x21, x22, [sp, #16]
    stp     x23, x24, [sp, #32]
    stp     x25, x26, [sp, #48]
    stp     x27, x28, [sp, #64]
    stp     x29, x30, [sp, #80]
    add     x9, sp, #112
    str     x9, [sp, #96]
    mov     x9, sp
    str     x9, [x0]

    ldp     x19, x20, [x1, #0]
    ldp     x21, x22, [x1, #16]
    ldp     x23, x24, [x1, #32]
    ldp     x25, x26, [x1, #48]
    ldp     x27, x28, [x1, #64]
    ldp     x29, x30, [x1, #80]
    ldr     x9, [x1, #96]
    mov     sp, x9
    ret
.size phase3_context_switch, . - phase3_context_switch

.global phase3_thread_start
.type phase3_thread_start, %function
phase3_thread_start:
    // x19 = entry, x20 = argument.  AAPCS64 requires SP % 16 == 0.
    mov     x0, x20
    blr     x19
    bl      scheduler_thread_exit
.Lthread_exit_halt:
    wfi
    b       .Lthread_exit_halt
.size phase3_thread_start, . - phase3_thread_start
