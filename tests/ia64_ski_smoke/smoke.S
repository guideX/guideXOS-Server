/* A small, independent system-mode Ski qualification program. */
.equ SSC_CONSOLE_INIT, 20
.equ SSC_PUTCHAR, 31
.equ SSC_EXIT, 66

.section .text.start, "ax"
.align 32
.global _start
.proc _start
_start:
    /* A privileged PSR operation proves system-mode privilege, then leave
       external interrupts masked for the duration of this smoke program. */
    rsm psr.i ;;
    mov r8 = psr ;;

    mov r32 = r0 ;;
    mov r15 = SSC_CONSOLE_INIT ;;
    break.i 0x80000 ;;

    movl r12 = smoke_message ;;
1:
    ld1 r32 = [r12], 1 ;;
    cmp.eq p6, p7 = r32, r0 ;;
(p6) br.cond.sptk.few 2f ;;
    mov r15 = SSC_PUTCHAR ;;
    break.i 0x80000 ;;
    br.sptk.few 1b ;;

2:
    mov r32 = r0 ;;
    mov r15 = SSC_EXIT ;;
    break.i 0x80000 ;;
3:
    hint @pause ;;
    br.sptk.few 3b ;;
.endp _start

.section .rodata, "a"
.align 8
smoke_message:
    .asciz "SKI_SYSTEM_MODE_SMOKE_PASS\r\n"
