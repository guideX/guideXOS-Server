// Non-yielding preemption worker.  The C helper performs one unit of private
// work; this frame keeps x19-x29 live across that call so the timer can
// preempt the task at arbitrary instruction boundaries.

.section .text, "ax"
.align 4
.global phase3_preemptive_worker
.type phase3_preemptive_worker, %function
phase3_preemptive_worker:
    stp     x29, x30, [sp, #-32]!
    str     x0, [sp, #16]
    ldr     x9, [x0, #16]
    mov     x19, x9
    add     x20, x9, #1
    add     x21, x9, #2
    add     x22, x9, #3
    add     x23, x9, #4
    add     x24, x9, #5
    add     x25, x9, #6
    add     x26, x9, #7
    add     x27, x9, #8
    add     x28, x9, #9
    add     x29, x9, #10
.Lworker_loop:
    ldr     x0, [sp, #16]
    bl      phase3_preemptive_step
    ldr     x0, [sp, #16]
    ldr     x1, [x0, #16]
    cmp     x19, x1
    b.ne    .Lfailure19
    add     x2, x1, #1
    cmp     x20, x2
    b.ne    .Lfailure20
    add     x2, x1, #2
    cmp     x21, x2
    b.ne    .Lfailure21
    add     x2, x1, #3
    cmp     x22, x2
    b.ne    .Lfailure22
    add     x2, x1, #4
    cmp     x23, x2
    b.ne    .Lfailure23
    add     x2, x1, #5
    cmp     x24, x2
    b.ne    .Lfailure24
    add     x2, x1, #6
    cmp     x25, x2
    b.ne    .Lfailure25
    add     x2, x1, #7
    cmp     x26, x2
    b.ne    .Lfailure26
    add     x2, x1, #8
    cmp     x27, x2
    b.ne    .Lfailure27
    add     x2, x1, #9
    cmp     x28, x2
    b.ne    .Lfailure28
    add     x2, x1, #10
    cmp     x29, x2
    b.ne    .Lfailure29
    b       .Lworker_loop

.Lfailure19:
    mov     x2, x1
    mov     x3, x19
    mov     x1, #19
    b       .Lreport_failure
.Lfailure20:
    add     x2, x1, #1
    mov     x3, x20
    mov     x1, #20
    b       .Lreport_failure
.Lfailure21:
    add     x2, x1, #2
    mov     x3, x21
    mov     x1, #21
    b       .Lreport_failure
.Lfailure22:
    add     x2, x1, #3
    mov     x3, x22
    mov     x1, #22
    b       .Lreport_failure
.Lfailure23:
    add     x2, x1, #4
    mov     x3, x23
    mov     x1, #23
    b       .Lreport_failure
.Lfailure24:
    add     x2, x1, #5
    mov     x3, x24
    mov     x1, #24
    b       .Lreport_failure
.Lfailure25:
    add     x2, x1, #6
    mov     x3, x25
    mov     x1, #25
    b       .Lreport_failure
.Lfailure26:
    add     x2, x1, #7
    mov     x3, x26
    mov     x1, #26
    b       .Lreport_failure
.Lfailure27:
    add     x2, x1, #8
    mov     x3, x27
    mov     x1, #27
    b       .Lreport_failure
.Lfailure28:
    add     x2, x1, #9
    mov     x3, x28
    mov     x1, #28
    b       .Lreport_failure
.Lfailure29:
    add     x2, x1, #10
    mov     x3, x29
    mov     x1, #29
.Lreport_failure:
    ldr     x0, [sp, #16]
    ldr     w0, [x0, #24]
    bl      phase3_register_failure
.Lfailure_halt:
    wfi
    b       .Lfailure_halt
.size phase3_preemptive_worker, . - phase3_preemptive_worker

