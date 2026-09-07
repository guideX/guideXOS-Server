// AAPCS64 NativeElf launch bridge.
// x0 = gx_main, x1 = gx_app_context, x2 = app stack top (16-byte aligned).

.section .text, "ax"
.align 4
.global gxos_native_call_on_stack
.type gxos_native_call_on_stack, %function
gxos_native_call_on_stack:
    // Preserve the kernel caller's callee-saved state and stack before
    // switching to the independently owned application stack.
    sub     sp, sp, #112
    stp     x19, x20, [sp, #0]
    stp     x21, x22, [sp, #16]
    stp     x23, x24, [sp, #32]
    stp     x25, x26, [sp, #48]
    stp     x27, x28, [sp, #64]
    stp     x29, x30, [sp, #80]
    mov     x19, x0
    mov     x20, x1
    mov     x21, sp
    mov     sp, x2
    mov     x0, x20
    blr     x19
    mov     x22, x0
    mov     sp, x21
    mov     x0, x22
    ldp     x19, x20, [sp, #0]
    ldp     x21, x22, [sp, #16]
    ldp     x23, x24, [sp, #32]
    ldp     x25, x26, [sp, #48]
    ldp     x27, x28, [sp, #64]
    ldp     x29, x30, [sp, #80]
    add     sp, sp, #112
    ret
.size gxos_native_call_on_stack, . - gxos_native_call_on_stack
