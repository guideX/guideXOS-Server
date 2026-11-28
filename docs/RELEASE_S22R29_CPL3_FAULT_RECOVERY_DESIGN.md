# S22R29 CPL3 Fault Recovery Design

**Classification:** diagnostic engineering evidence; not a public release.

## Scope

S22R29 adds synchronous recovery for a tightly bounded protected diagnostic task. It demonstrates recovery only when a processor exception arrives from that task, the saved context and active address space match its descriptor, the exact expected fault is reported, and the independent lifecycle continuation is still valid. Any failed or ambiguous check remains fatal.

This is not timer preemption, a watchdog, general kernel-fault containment, or production Native ELF CPL3 execution. PacMan retains its existing authorized CPL0 lifecycle and package policy.

## Baseline provenance

The source checkout began on `main` at `02422a86d44319e6e6473342af7e8b1e29c55ff9`, matching `origin/main`, with a clean tracked and untracked worktree. The accepted S22R28R3R3 artifacts remain separate and unchanged:

| Configuration | Accepted ISO SHA-256 | Manifested guest result |
| --- | --- | --- |
| CPL3 off | `07185d8cfc3d90598ac2e8c3abbb540d4e4519f5c8465add2c80bef0c326720e` | QAC-managed boot, VFS/Navigator and desktop input, clean shutdown |
| CPL3 on | `30909e3ec5325fec8458a5223ffbd49863f79abe6c15b162a83bef7d7f4532dc` | Three protected cycles, filesystem/desktop input, clean shutdown |

The manifests identify the BootInfo v3 loader, 1,024-page tracking/audit capacity, corrected RAMDISK virtual alias, and unchanged supervisor mapping permissions. The accepted public-preview ISO is retained independently; S22R29 does not overwrite it or alter `dist`, signing, publication, or release-readiness data.

The repository's Mbed TLS profile verifies Mbed TLS 4.1.0 at commit `0fe989b6b514192783c469039edd325fd0989806` and TF-PSA-Crypto 1.1.0 at pinned submodule commit `29160dd877d29658279fd683b2ae57b320ddcf09`, with the three tracked guideXOS patches. The expected profile-patched dependency worktree is left intact.

## Existing entry and frame contract

AMD64 exceptions use real IDT gates and one stub for each vector 0 through 31. The processor supplies an error code for #DF, #TS, #NP, #SS, #GP, #PF, #AC, #CP, #VC, and #SX; the remaining installed exception stubs normalize a zero error-code word. The stubs save all 15 general-purpose registers, then expose this normalized prefix:

| Offset | Word |
| ---: | --- |
| 0–112 | RAX, RCX, RDX, RBX, RBP, RSI, RDI, R8–R15 |
| 120 | vector |
| 128 | error code (zero for no-error-code exceptions) |
| 136 | RIP |
| 144 | CS |
| 152 | RFLAGS |
| 160 | user RSP, only when saved CS has CPL3 |
| 168 | user SS, only when saved CS has CPL3 |

CR2 and CR3 are sampled separately at exception dispatch. C++ classifies saved CS before reading the optional privilege-transition RSP/SS words. The stub's return path removes both normalized words before `IRETQ`; a user fault selected for recovery never returns through `IRETQ` at all. Its exception frame is abandoned.

The TSS RSP0 is the static kernel entry stack configured by the protected-task path. The stub preserves the hardware context there, detects nesting, then switches to the independent 16 KiB exception-dispatch stack before C++. Recovery then changes CR3 to the saved kernel root and branches to a continuation on the separate 16 KiB lifecycle stack. Nested exceptions capture vector, error code, RIP, and CS in dedicated diagnostics and halt; they do not reuse a possibly active dispatch stack.

The C++ dispatcher is entered with Microsoft x64 shadow space and 16-byte call-site alignment. Exception-stub assembly preserves the register context in memory; the kernel continuation resumes through the saved assembly entry label and its existing nonvolatile-register save frame. The continuation's RSP, RIP, RFLAGS, FS base, and GS base are validated before recovery.

## Origin and recovery policy

The dispatcher trusts the saved processor CS, never a historical “task was running” flag. A CPL0 fault while the protected-task pointer is active is logged as a kernel fault and halts. Unsupported vectors, an absent or mismatched task, an inconsistent CR3 or address-space owner, a malformed frame, bad selectors, code or stack addresses outside the task's bounded regions, or an invalid continuation all reject recovery and halt with diagnostics.

The bounded recovery contract accepts only #UD (6), #GP (13), or #PF (14), and only when the exact per-task expected vector and error code match. For #PF, the sampled CR2 must equal the configured unmapped user guard address. The user selectors, sanitized RFLAGS, task ID, address-space task ID, task root CR3, active CR3, kernel CR3, continuation RIP, continuation stack, and saved kernel state must agree.

## Continuation and task lifecycle

The current synchronous supervisor is already on the lifecycle stack when it enters CPL3. On a valid CPL3 fault, dispatch copies the complete normalized frame plus CR2, active task CR3, and task ID into the task descriptor. It marks the task `Running -> Faulted`, clears the active-task pointer, switches to the saved kernel CR3, and returns a dedicated assembly continuation result. The stub restores kernel segments and FS/GS bases, restores the continuation's interrupt flags, switches off the exception stack, and jumps to the validated assembly resume label. That label restores the existing nonvolatile registers and returns to the supervisor.

Only after the continuation has regained control does the supervisor call the existing address-space owner to destroy the task. The owner verifies the task CR3 is no longer active, validates the private page-table tree, invalidates task translations, clears and releases task-private mappings and page-table frames, preserves shared kernel entries, and releases the remaining unmapped code/data/stack frames. Frame accounting is compared against the pre-task baseline before `Faulted -> Reaped` completes. The saved terminal fault remains in the descriptor through that verification.

The supervisor then runs another fresh CPL3 task. Its normal `int 0x80` exit must restore kernel CR3, return value 42, and the same baseline frame accounting. This verifies useful protected-task execution after fault cleanup.

## Bounded processor-generated diagnostics

Each task is independently identified and the diagnostic sequence is finite:

1. Read an intentionally unmapped CPL3 guard address and expect #PF with present=0, write=0, user=1 (`error=0x4`) and the guard address in CR2.
2. Run a normal CPL3 task and exit through `int 0x80` with result 42.
3. Execute `UD2` at CPL3 and expect #UD without a hardware error code.
4. Run another normal protected task.
5. Execute `int 0x81` where the IDT entry is absent and has kernel-only DPL; expect processor #GP with the IDT-selector error `0x40A`.
6. Run another normal protected task.
7. Repeat the #PF recovery and follow it with another normal protected task.

The #GP test uses normal unprivileged software-interrupt instruction semantics to request delivery through an absent gate. It does not execute a privileged instruction or synthesize a frame.

## Qualification boundary

Hosted tests validate the frame contract, classification predicate, lifecycle transitions, ownership teardown, repeated cleanup, context-switch ABI, production Native ELF policy, FAT/VFS persistence, RAMDISK mounting, and App Model persistence. Freestanding compilation, symbol audits, and stack guards validate the built execution paths. None of these alone proves CPU exception delivery or guest survivability.

Guest recovery remains qualified only after a QAC-managed QEMU guest shows actual processor-generated exception markers, task reaping, continued kernel operation, a subsequent result-42 task, usable desktop input, and mounted `/` and `/Apps`. A QEMU restart or an exception log followed by a halt is not recovery. If QAC is occupied by another owner, no diagnostic guest may be launched outside its reservation workflow.

Even after successful exception recovery, a task that loops forever without exiting or faulting remains non-cooperative. Timer-driven preemption and termination are reserved for S22R30.
