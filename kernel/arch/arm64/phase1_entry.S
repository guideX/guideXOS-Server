// AARCH64-1 entry and exception-level normalization.
//
// The loader passes:
//   x0 = gxos_aarch64_phase1_handoff*
//   x1 = top of the loader-allocated guideXOS stack
//   x2 = reserved (zero)
//
// The image is linked at its physical load address.  For Phase 1 the EL1
// translation regime is deliberately reduced to physical/identity execution
// by clearing SCTLR_EL1.M/C/I before C++ is called.

.section .text.entry, "ax"
.align 4
.global _start
.type _start, %function
.extern phase1_main
_start:
    msr     daifset, #0xf
    mov     x19, x0
    mov     x20, x1
    mov     x21, xzr
    mrs     x21, CurrentEL
    lsr     x21, x21, #2

    cmp     x21, #2
    b.eq    .Lfrom_el2
    cmp     x21, #1
    b.eq    .Lfrom_el1
    // EL3 is deliberately unsupported.  Keep the stack and call C so the
    // serial path can emit a useful failure instead of claiming EL1 success.
    b       .Lenter_c

.Lenter_c:
    b       .Lel1_entry

.Lfrom_el2:
    // Return to EL1h with the loader-owned stack in SP_EL1.  No GIC/timer or
    // virtual-machine setup is needed for the masked, synchronous proof.
    mov     x0, #(1 << 31)
    msr     hcr_el2, x0
    mov     x0, x20
    msr     sp_el1, x0

    // Disable the firmware's EL1 translation regime before eret.  The kernel
    // is linked at 0x40000000 and the loader allocated it at that address.
    mrs     x0, sctlr_el1
    bic     x0, x0, #(1 << 0)
    bic     x0, x0, #(1 << 2)
    bic     x0, x0, #(1 << 12)
    msr     sctlr_el1, x0
    dsb     sy
    isb

    adr     x0, phase1_vectors
    msr     vbar_el1, x0
    mov     x0, #0x3c5
    msr     spsr_el2, x0
    adr     x0, .Lel1_entry
    msr     elr_el2, x0
    isb
    eret

.Lfrom_el1:
    // If firmware entered the UEFI application/kernel with MMU enabled, the
    // next instructions are valid because the fixed load policy is identity
    // safe.  Keep the final kernel state deterministic.
    mrs     x0, sctlr_el1
    bic     x0, x0, #(1 << 0)
    bic     x0, x0, #(1 << 2)
    bic     x0, x0, #(1 << 12)
    msr     sctlr_el1, x0
    dsb     sy
    isb
    b       .Lel1_entry

.Lel1_entry:
    mov     sp, x20
    adr     x0, phase1_vectors
    msr     vbar_el1, x0
    isb

    // Zero BSS using the identity-linked addresses.  This is intentionally
    // before the C++ call and does not use the UEFI stack.
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
    bl      phase1_main
.Lhalt:
    wfi
    b       .Lhalt
.size _start, . - _start

.section .vectors, "ax"
.align 11
.global phase1_vectors
phase1_vectors:
.rept 16
    b       .Lexception_halt
    .space 124
.endr

.section .text
.align 4
.Lexception_halt:
    msr     daifset, #0xf
    b       .Lhalt
