# S22R28 — Protected Task Exit and Fault Handling

## Controlled exit

The development-only IDT installs vector `0x80` as a present DPL3 64-bit interrupt gate (`0xEE`). The gate is absent from the ordinary build. A CPL3 `int 0x80` causes the processor to load the current TSS `RSP0` before entering the assembly stub. The stub saves all 15 general-purpose registers and passes a pointer to the hardware return frame using the Microsoft x64 ABI and its required 32-byte shadow space.

The kernel accepts only the fixed diagnostic request. It requires the active task and CR3 to match; CS=`0x23`, SS=`0x1B`; the exact instruction-after-`int` RIP and expected user RSP; operation `1`; magic `0x53325228`; IF=0; and no TF, DF, IOPL, NT, or VM flags. It then verifies that the owned data frame contains result `42` and the live CS written by the task. The full validated GPR and hardware return frame is retained in the task descriptor.

On acceptance, the dispatcher transitions `Running -> Exited`, clears the active-task pointer, restores the saved kernel CR3, and returns through assembly to the kernel continuation saved before entry. The assembly restores the kernel FS/GS bases, kernel data selectors, and saved kernel RFLAGS, then resumes without IRETQ to the exiting task. Cleanup therefore runs on the separate lifecycle stack after the task CR3 is no longer active. Three bounded task cycles must return all frame-owner accounting fields to their baseline before the diagnostic reports success.

This is one fixed service-entry contract, not a general syscall interface. It accepts no user pointer or kernel address and does not dispatch to a privileged callback.

## Fault policy

The AMD64 exception dispatcher classifies the hardware-saved CS privilege level before the existing Native ELF recovery path. A CPL3 exception records the active task ID, vector, error code, RIP, and CR2, marks the task `Faulted`, logs `policy=halt-no-recovery`, and halts. It never jumps through the ring-0 Native ELF recovery trampoline. Kernel-origin exceptions continue through the existing kernel/Native ELF diagnostics; no blanket exception escape was added.

The diagnostic payload is deliberately fixed and non-faulting. No user page-fault experiment was run. Faulted task cleanup and resume-to-kernel recovery are not implemented or qualified: a CPL3 fault currently stops the machine after recording the fault. This is the explicit safety limit until a separate fault-return/reaper boundary is designed and guest-tested.

The ordinary CPU exception stubs normalize exceptions with and without hardware error codes and preserve the same register-frame prefix. For user-origin exceptions, hardware first switches from the user stack to the configured TSS `RSP0`; the stub saves the interrupted state and changes to the dedicated kernel exception stack before C++ dispatch. IRQ gates use TSS IST1. The corrected exception dispatcher reserves exactly 32 bytes of Microsoft x64 shadow space; IRQ stubs preserve their interrupted frame and align their ABI call sites.

## Teardown and ownership

The exit stub abandons the temporary RSP0 entry frame by restoring the saved lifecycle-stack continuation. Only then does the task code destroy its private address space, remove private mappings, reclaim page-table frames, and release the owned code/data/stack frames. It confirms that the task address space is no longer active and compares the complete `FrameAccounting` record with the pre-cycle baseline. The test repeats creation and teardown three times.

No active task stack is reclaimed. The RSP0 and lifecycle stacks are fixed kernel BSS stacks and remain allocated. Shared supervisor mappings are not owned by a task and remain in the kernel page tables. Hosted tests cover permission propagation, owner validation, mapping rollback, page-table cleanup, and repeated frame-count restoration; these tests do not exercise hardware TLB behavior after a real CR3 switch.

## Evidence boundary

The hosted exit-frame contract, memory-foundation regression, voluntary Microsoft x64 context-switch regression, Native ELF security policy, diagnostic freestanding build, and default diagnostic-off freestanding build passed. No guest CPL3 entry/exit evidence is available in this release record. The QAC coordinator reported an owner-verified `guideXOSServer` acceptance reservation already running, with one managed QEMU process, zero foreign QEMU processes, zero queued reservations, and zero blocked requests. The active guest was not inspected, stopped, or replaced. The computer-use UI endpoint timed out; QAC's own CLI was used for these read-only health/reservation checks.

Consequently this phase makes no claim that the desktop remained responsive after an actual CPL3 task, that a real guest's BootInfo map is safe, or that a CPL3 fault can be recovered. S22R28 is a foundation implementation pending exclusive guest qualification.
