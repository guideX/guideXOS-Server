# AARCH64 RUNSAFE3-A: Scheduled EL0 Child Runtime Foundation

**Outcome:** `SCHEDULED_EL0_HOSTED_READY` (Outcome A). Guest interrupt-driven scheduling, timeout, and cancellation remain unqualified.
**Baseline:** `AARCH64_SUPPORT` at `b9ddc17af754a7a092429fd6c1948949bcf6d3b0`.
**Scope:** Generic kernel-internal ARM64 child execution. This does not reconnect Developer Studio Run.

## Task ownership and lifecycle

An EL0 child is a normal `kernel::scheduler::Task` allocated by the common scheduler. The scheduler owns its kernel exception stack, task ID, runnable state, timer accounting, cancellation flag, execution budget, terminal result, and reaping of that kernel stack. The runtime record owns a unique execution ID, parent task ID, private RUNSAFE2 address-space descriptor, EL0 register context, user stack/code mappings, and cleanup state. The child's address-space owner identity is its execution ID; the scheduler's owner ID is the kernel task that launched it.

Creation leaves the child blocked in `Created`. The runtime first validates the parent, bounded image, budget, address space, mappings, initial PC/SP/PSTATE, and scheduler task. Only then may `start` prepare its interrupt context and move it to `Ready`. Timer dispatch enters the child's kernel trampoline, which activates its private root and then enters EL0. EL0 timer IRQs move it through `Running` and `Preempted`; a returned SVC/fault moves it through `Returning`. Terminal outcomes are `Completed`, `Faulted`, `TimedOut`, `Cancelled`, or `StartFailed`. `Reaped` follows successful user-space cleanup and release of the inactive kernel task stack. Terminal tasks cannot become runnable again.

Only the current kernel task may create, start, query, cancel, or reap its children. The current implementation is a single-core scheduling contract; it does not claim SMP synchronization. Runtime API metadata operations mask local IRQs while checking ownership or changing records. An owner can issue cancellation when it is scheduled. A non-cooperative child is first stopped by the normal timer IRQ and saved as preempted; the owner's subsequent cancellation request then marks it terminal and cleans it without requiring a child SVC.

## Context switching and exceptions

The common Phase 3 scheduler remains the sole scheduler. An EL0 child uses its ordinary scheduler task and protected EL1 task stack; the existing RUNSAFE2 context carries x0-x30, SP_EL0, ELR/SPSR, child/address-space identity, and the saved EL1 continuation SP. The EL1 kernel and exception vectors remain mapped in each child root, while RUNSAFE2 mapping rules expose only the child's user pages at EL0.

The scheduled initial context uses EL0 PSTATE `0x340` (IRQ unmasked, D/A/F masked). Existing synchronous qualification retains its separately validated, IRQ-masked `0x3c0` context. A lower-EL timer IRQ is acknowledged and dispatched through the existing Phase 4 registered IRQ path, then EOI is issued through the existing GIC wrapper. The timer handler captures the 31 GPRs plus ELR/SPSR/SP_EL0 into the active child context, switches TTBR0 back to the kernel root, and lets the common scheduler choose a kernel task or child. Resuming a child validates execution owner, address-space identity, user PC and stack, activates its root with the existing TTBR/TLB maintenance, then returns a tagged context to the assembly exception-return path. That path installs the selected task's EL1 stack, SP_EL0, ELR/SPSR and GPRs before `eret`. GICv2/GICv3 acknowledgement and completion remain on their existing common path.

SVC exit and synchronous aborts continue through the RUNSAFE2 lower-sync path. The dispatcher restores the kernel root before returning to the child's saved EL1 continuation. Faults retain their ESR/ELR/SPSR/FAR and fault reason; a valid exit retains the actual x0 value. No EL0 service exposes privileged function pointers.

### SIMD/FPU contract

RUNSAFE3-A does not save SIMD/FPU registers. The Phase 10 freestanding kernel is built with `-mgeneral-regs-only`, and the runtime validates each supplied image against a deliberately small integer-only instruction profile: 64-bit MOVZ, 64-bit SUBS-immediate, in-image `B`/`B.NE`, the one bounded integer load used by the protected-memory fixture, and a final `SVC #0` preceded by the exit-service value in x8. Unsupported instructions, including FP/SIMD, privileged operations, stores, indirect branches, and other system instructions, are rejected before mapping. This is a qualification payload profile, not yet a general compiled-app ISA validator.

## Budgets and terminal ordering

Budgets are expressed in microseconds at the internal API and converted with checked arithmetic to `CNTFRQ_EL0` ticks, rounding a fractional tick up. The default is 500 ms; accepted values are 1 ms through 60 s. `CNTPCT_EL0` is the monotonic counter. The scheduler charges unsigned counter deltas for each child schedule slice, from the initial entry/resume sample through the next timer IRQ sample. The counter continues advancing while other tasks run, but those intervals are not charged to the child. Each slice also includes the small EL1 entry/dispatch work before the timer sample. This is scheduled elapsed counter time; it is neither wall-clock lifetime nor a precise count of CPU cycles. Unsigned subtraction handles a short slice crossing the counter's 64-bit wrap. On an SVC/fault return the scheduler charges the final interval before accepting a result, so an exit just beyond its budget cannot be reported as successful merely because it arrived between periodic ticks.

The existing architectural timer is periodic at 100 microseconds. Timeout is enforced on a timer interrupt after the consumed counter reaches the budget; therefore termination latency is bounded by one timer quantum plus interrupt/dispatch latency, not by a host process timeout. A final SVC/fault sample can also detect that the budget was crossed before the next timer IRQ. Timeout result value is zero with result kind `TIMEOUT`; it never reuses a child exit value. A cancellation request observed while a child is actively running is checked at its next timer boundary; in the current single-core owner model, owner API calls occur after that boundary when the child is already preempted. A completed/faulted child rejects later cancellation. Timer-side cancel/timeout and final-return timeout/cancel both disable the task before cleanup and reaping.

## Address-space and resource cleanup

The active child root is never freed. Timer preemption first saves the user frame and restores the kernel root; SVC/abort handling also restores the kernel root before it returns to EL1. Timeout/cancellation cleanup accepts only an inactive prepared/preempted/terminal context with the expected owner and address-space identity. RUNSAFE2 cleanup releases owned user pages and private page-table pages, clears mapping metadata, and leaves the shared kernel root untouched. The scheduler retains the task's kernel stack until its owner reaps the terminal task after it is no longer current. Cleanup state is recorded to prevent double release; partially failed cleanup remains non-reapable rather than reporting success.

The initial runtime maps one RX code page and one RW/XN user stack page at fixed user VAs. It adds no user MMIO mapping. The protected-memory fixture faults on `0x40000000`. Page-table activation uses the existing full TTBR0 invalidation/barrier path; no address-space identity is reused by the runtime record until its prior execution is reaped.

## Kernel-internal API

`phase2_el0_task_create` accepts an already trusted kernel image that also passes the RUNSAFE3-A integer instruction validator, parent owner ID, and optional budget. `phase2_el0_task_start` schedules it; `phase2_el0_task_query` returns state, result kind/value, consumed/budget counter ticks, cancel state and cleanup state; `phase2_el0_task_cancel` requests forced termination; and `phase2_el0_task_reap` releases its scheduler stack and record. `phase2_el0_task_create_fixture` selects only the four internal qualification fixtures. These are kernel-internal functions and are not user syscalls or a Studio adapter.

The fixtures are finite integer computation followed by SVC exit 42, a non-cooperative direct branch loop, a non-cooperative cancellation loop, and a protected-memory load fault. Their bytes are internal constants; the generic create path still applies the same validator and ownership checks.

## Hosted and build evidence

The production-backed host suite compiles `common_scheduler.cpp`, `phase2_el0.cpp`, and `phase2_el0_runtime.cpp`. It uses a host allocator, mock architecture context operations, and synthetic timer/exception events. It checks the RUNSAFE2 controls plus independent child roots/contexts, owner rejection, blocked-to-ready transitions, repeated context save/restore, counter accounting, timeout and final-return timeout, cancel-before-start and cancel-after-preemption, fault/result separation, SIMD rejection, duplicate completion, cleanup/reap, stale-task rejection, and the actual exit value 42. These are hosted simulations and source contracts; they do not establish an actual lower-EL timer IRQ, native preemption, cancellation, or fault result.

The source-matched Phase 10 candidate is built with the freestanding `aarch64-none-elf` profile, `-mgeneral-regs-only`, and the runtime source included. The Studio Run fail-closed source check, ABI3 adapter check, Phase 12 SDK alignment check, GICv3 hosted model, compiler hosted regressions, native filesystem contract, and host portions of the relevant Phase 2/3/4/9/10/11/12 checks passed. The production Phase 12 build also compiled the pinned Studio sources against the pinned v0.5 SDK, linked the ARM64 Native ELF, staged the package, and emitted a candidate manifest. These results make an ARM64 candidate available for RUNSAFE3-B; they do not prove native guest behavior. No QEMU guest was launched and no QAC admission was requested in this phase.

| Validation | Result |
| --- | --- |
| Production-backed EL0 scheduler/runtime host suite | PASS, 213 assertions; explicitly host simulation |
| Phase 2 host controls and freestanding build | PASS, 51 assertions |
| Phase 3 scheduler contract and freestanding build | PASS |
| Phase 4 allocator/VFS controls, scheduler repeatability, freestanding build | PASS |
| Phase 9 lifecycle/ABI host controls and freestanding build | PASS |
| Phase 10 NativeElf host controls and ARM64/EFI build | PASS |
| Phase 11 compiler build and ARM64/AMD64 payload staging | PASS |
| Phase 12 production Studio build, adapter/gate controls, package staging | PASS |
| Studio Run gate source check | PASS; launch remains unavailable |
| ABI3 Studio adapter controls | PASS, 181 checks |
| Phase 12 SDK alignment controls | PASS |
| GICv3 hosted model | PASS, 62 assertions |
| Compiler host regressions | PASS, ARM64 and bootstrap suites |
| Native filesystem contract | PASS |
| Captured firmware DTB decode | PASS, 223 properties; previously captured input |
| `git diff --check` | PASS (Git reports repository LF-to-CRLF normalization warnings) |

Build outputs were written under distinct `out/aarch64-runsafe3a-*` folders; accepted R1e/R2A/R2B1 outputs were left in place. The Phase 10 ARM64 kernel SHA-256 was `ace767bdbb7ed936c60e56030a085818ee39582a1d34c0320135f1b4e2ba1f1c`, with EFI SHA-256 `551a1650515271323716f288b841a3b2a2956a73183d9a81a2306654c923bb47`. The production Phase 12 kernel SHA-256 was `44792972a2f3eeb699fe011839bfcf8d44f5e9645252b74128e3e1e5a6f20405`. The Phase 12 build includes `GXOS_AARCH64_PHASE12`, `GXOS_AARCH64_ABI3_STUDIO_GUEST`, and `GXOS_AARCH64_EL0_ISOLATION`. These are build identities only, not native execution evidence.

## Studio, Build-to-Run, and Phase 9 boundaries

Studio Run remains disabled by its existing fail-closed gate, with the Phase 9 loader retaining a backstop before privileged host initialization. No Studio-generated ELF is passed to this runtime and no existing trusted EL1 application is converted automatically. The R2B2 report remains authoritative for Build-to-Run identity gaps: a built artifact still needs a source-matched identity handoff, trust/validation record, and a separate Studio adapter integration before Run may use this runtime.

Legacy Phase 9 NativeElf execution still enters at EL1 in the shared kernel translation regime. Its privilege model remains a documented separate limitation and is not made safer by this generic EL0 runtime.

## RUNSAFE3-B native candidate and required evidence

RUNSAFE3-B must build and boot the exact source-matched candidate under the existing QAC supervision workflow. It must obtain guest-originated evidence for all of the following, with real timer deltas and per-task scheduler counters (never constants or host simulation):

```text
[RUNSAFE3B] FINITE PASS exit=42 preemptions=<n> timer_irqs=<n> scheduled_ticks=<n> cleanup=1 kernel_continues=1
[RUNSAFE3B] TIMEOUT PASS state=timed_out result=none budget_ticks=<n> consumed_ticks=<n> timer_irqs=<n> cleanup=1 kernel_continues=1
[RUNSAFE3B] CANCEL PASS state=cancelled result=none requested_by=<owner-id> timer_irqs=<n> cleanup=1 kernel_continues=1
[RUNSAFE3B] FAULT PASS state=faulted reason=data_abort far=0x40000000 kernel_root_restored=1 cleanup=1 kernel_continues=1
AARCH64_RUNSAFE3B_PASS source=<commit> finite_preempts=<n> timeout_enforced=1 cancellation_forced=1 fault_isolated=1 cleanup=1
```

The finite record must show a nonzero lower-EL timer preemption count and a real resumption before exit 42. The timeout and cancellation records must show no exit value and subsequent kernel scheduling. The fault record must show the saved abort metadata, kernel-root restoration, cleanup, and continued kernel execution. Preserve the exact build manifest and hashes, supervised guest logs, actual timer IRQ counts, task `preemption_count`/consumed ticks/budget ticks, and cleanup/root-restoration results. QAC admission, guest launch, boot repetition, and acceptance decisions belong to RUNSAFE3-B, not this implementation phase.

## Current outcome

Outcome A, `SCHEDULED_EL0_HOSTED_READY`, is supported by the hosted suite and regression/build evidence above. The source-matched ARM64 candidate is ready for later native qualification. This outcome does not claim guest-confirmed EL0 scheduling, preemption, timeout, cancellation, or Studio Run safety; those require RUNSAFE3-B evidence under QAC supervision.
