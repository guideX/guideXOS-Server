/*
 * Ski SSC bridge for the IA-64 ELF ABI.
 *
 * C ABI inputs: in0/r32 = SSC function, in1/r33 = argument 0.
 * Ski SSC inputs: r15 = function, r32 = argument 0.
 * C ABI return: r8, matching Ski's SSC result register.
 *
 * This leaf allocates one local register so it may rewrite in0/r32 after
 * copying arg0 from in1/r33. It saves and restores ar.pfs around that frame;
 * r15 is an ABI scratch register and need not be preserved.
 */

.section .text, "ax"
.align 16
.global guidexos_ski_ssc
.type guidexos_ski_ssc, @function
.proc guidexos_ski_ssc
guidexos_ski_ssc:
    alloc r34 = ar.pfs, 2, 1, 0, 0 ;;
    mov r15 = r32 ;;
    mov r32 = r33 ;;
    break.i 0x80000 ;;
    mov.i ar.pfs = r34 ;;
    br.ret.sptk.many b0 ;;
.endp guidexos_ski_ssc
.size guidexos_ski_ssc, .-guidexos_ski_ssc
