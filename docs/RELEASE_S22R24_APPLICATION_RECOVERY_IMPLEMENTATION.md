# Server v0.3 — S22R24 Application Recovery Implementation

**Outcome C — `APPLICATION_ISOLATION_PREREQUISITE`.** No runtime termination or scheduler change was made. The audit found no safe point from which to forcefully destroy a Native ELF execution context, and the apparent scheduler primitives do not form an active freestanding scheduler. Adding a watchdog or unconditional task kill would report or worsen the failure without making the kernel recoverable.

## Implementation decision

Native ELF currently enters synchronously from the desktop launch dispatcher and runs at CPL0 on its own 128 KiB stack in the kernel address space. `run_package()` owns a stack-local `Runtime`, one window owner, the loaded image and verified image, plus an audio owner ID. Normal return re-enters the same C++ frame and performs cleanup. An arbitrary context destroy would bypass that cleanup and may abandon a kernel callback with locks, references, handles, or partial shared-state changes.

The smallest safe S22R24 result is therefore an evidence-based architecture boundary decision, not an unsafe kill implementation:

- Preserve the existing normal-return cleanup path and accepted PacMan-only integrity policy.
- Record the existing narrow exception trampoline accurately; it is not evidence of general termination or kernel-memory protection.
- Do not start a CPU-loop guest diagnostic: the kernel has no independent control path that could stop it, and an external QEMU kill/reboot would not prove in-kernel recovery.
- Do not add a diagnostic ELF to the release allowlist. No diagnostic image or modified runtime authorization policy was created.
- Carry protected execution, scheduling, termination, and ownership into S22R25.

## Existing behavior that remains available

- **Cooperative close:** PacMan polls the host event API and exits when it handles Escape/Close. Its ordinary return releases the runtime window and audio owner, frees the app stack and images, clears `s_activeRuntime`, and returns to the desktop. The S22R21, S22R22R1, and S22R23 reports qualify this path on their exact prior candidates.
- **Cooperative desktop pumping:** Native calls to event polling/presentation pump some input, desktop drawing, and audio work. This does not run the kernel main loop concurrently and cannot stop an app that ignores events or stops calling the host API.
- **Exception escape:** The exception dispatcher records register/fault state and can redirect a fault from an active runtime back to the trampoline when its app/runtime range predicate matches. This can abandon an app context and reach normal runtime cleanup, but it can also abandon in-progress kernel callback state. No S22R24 controlled guest fault test demonstrates it safe for arbitrary exception timing.

## Why a forced-termination primitive is not safe yet

1. There is no registered Native ELF task, PID, independently saved interrupt frame, per-task kernel stack, run queue, or runnable desktop task to switch to. The Native ELF call is synchronous on the desktop caller's kernel stack; app instructions use a separate 128 KiB app stack while `run_package()` and desktop frames remain suspended beneath the trampoline.
2. The PIT's active handler advances ticks and emits heartbeat diagnostics. It does not preempt. `arch_timer_tick()` and timer programming are TODO stubs; the generic process scheduler halts; syscall yield is a no-op and syscall exit halts.
3. Current native code is ring 0 and shares kernel mappings. It can disable interrupts, alter kernel state, retain raw pointers, or corrupt ownership metadata.
4. Cleanup is a normal-return sequence, not a reaper. There is no complete registry of windows, locks, file handles, pending operations, or references that could be released after abandoning an arbitrary instruction.
5. Existing switch scaffolding saves only call-preserved registers for voluntary switches; arbitrary interrupt preemption needs a complete asynchronous frame and a correct return path. The current switch assembly expects SysV `RDI`/`RSI` arguments while the configured MinGW kernel uses Microsoft x64 `RCX`/`RDX`; it is ABI-incompatible as built. Thread startup uses shared global entry arguments, thread return halts, and TSS/FPU/task stack lifetime integration is incomplete.

## S22R24 hosted checks

All generated test outputs are under `E:\CodexArtifacts\S22R24\tests`.

| Check | Result |
|---|---|
| `scripts/test-native-elf-security-policy.ps1` | PASS; 30 policy cases, 0 failures, 4 synthetic approved entry calls |
| Native ABI layout | PASS |
| Native present validation | PASS; shared frame validation and stateless relaunch contract |
| Native debugger runtime | PASS; hosted debugger fixture only, not a kernel scheduler/lifecycle test |
| Settings native runtime | PASS; 908/908, including 100 lifecycle repetitions and 250 provider refresh cycles |
| Settings native layout | PASS; 1,037/1,037 |
| FAT/VFS persistent storage | PASS; 44/44 against a writable image copy |
| VFS mount-table integrity | PASS; 19/19 |
| Hosted VFS read | PASS; 4/4 |
| User-data-volume policy | PASS; 9/9 |
| Native filesystem contract | PASS |
| PacMan source registration | PASS; pre-audio, audio-bearing, missing implementation, and missing definitions fixtures |
| Window UI regression | PASS |

The hosted tests above cover loading policy, ABI layout, frame validation, settings, storage, and unrelated hosted debugger/window models. They do not invoke the freestanding `native_elf::launch()` path. No dedicated kernel scheduler/context-switch, Native ELF lifecycle cleanup, fault-containment, event-starvation, or non-yielding-loop regression exists in the current `tests` set. `navigator_resource_scheduler_test` exercises Navigator resource prioritization, not CPU-task scheduling.

Freestanding build and stack-budget results are recorded in the qualification report. The source-level scheduler audit is in [the execution and scheduler audit](RELEASE_S22R24_NATIVE_ELF_EXECUTION_AND_SCHEDULER_AUDIT.md).

The public-preview-profile AMD64 build and kernel symbol audit passed. The focused stack guard passed all four established paths with at least 4,520 bytes headroom. A full `.su` scan also found existing large frames outside those four guarded paths (largest: `attempt_startup_restore`, 10,272 bytes; `apply_startup_fallback`, 9,232; `NavigatorApp::submitFormForBlock`, 8,928; `desktop::load_persisted_app_shortcuts`, 8,672; `stbi__parse_png_file`, 7,952). They are present in the unchanged S22R23 kernel image, and no stack-using source was changed here. The existing guard therefore passes its declared paths but does not prove a 4,096-byte reserve for every possible kernel call chain; S22R25 should broaden this inventory before adding task stacks or interrupt frames.

## Candidate, data, and security boundary

No executable source was changed, so S22R24 did not create a new release candidate. The preserved S22R23 ISO remains 90,245,120 bytes with receipt-verified SHA-256 `8d397659e0a6ec8f1be113ae4f5be14dbcaf1ef22ccda41a6e5ee5014919cf40`; its kernel ELF SHA-256 is `9d2f17c33f157a566b6ee513196b5b74831bf8532699be78538b5979886fc166`.

The existing Native ELF policy suite passed. This confirms the existing PacMan package/source/integrity checks still behave as expected in the hosted policy harness. It does not prove memory protection, exception isolation, CPU preemption, or forced termination. The accepted data image remained byte-identical; the persistence regression modified only `E:\CodexArtifacts\S22R24\tests\fat-persistence\hosted-data-volume-copy.img`.

No QAC guest was launched. Since there is no safe independent control path for a non-yielding app and no diagnostic authorization/image was created, a guest loop test would risk wedging the guest or mislabeling an external kill as recovery. QAC coordinator ownership/health was not queried because no guest operation was attempted. The S22R23 accepted candidate and its two cold-boot persistence evidence remain preserved.

## S22R25 implementation scope

1. Correct and test the AMD64 context-switch calling convention, then add a minimal per-task scheduler with complete interrupt-frame save/restore, dedicated kernel stacks, timer integration, and a desktop context that remains runnable. First demonstrate responsiveness for a bounded CPU loop with interrupts enabled; do not claim memory isolation.
2. Move Native ELF execution to CPL3 with per-app page tables, kernel-only mappings, a TSS-managed kernel stack, validated executable/read-only/writable segment permissions, and a narrow syscall/host ABI. Prevent app-controlled `cli`, arbitrary kernel pointers, and direct device access.
3. Add lifecycle states and deferred teardown/reaping. Track app-owned windows, audio, file handles, and service references; allow cancellation only after the app leaves a kernel critical section or syscall safe point. Make cleanup idempotent and verify relaunch.
4. Build a separately identified diagnostic image containing controlled loop, input-starvation, and fault fixtures. Keep the release's PacMan-only allowlist unchanged. Use QAC only after hosted and freestanding tests prove a bounded kernel control path; attach a disposable copy of the persistent volume.
5. Requalify normal PacMan launch/input/return, loading security, Documents/AppData persistence, 16 KiB stack budget with at least 4,096 bytes static headroom, AMD64 symbol/runtime constraints, and the exact new candidate identity.

The milestone is deliberately smaller than a general-purpose process system. It is large enough to separate CPU responsiveness, safe teardown, exception containment, and arbitrary-memory-corruption protection into claims that can be independently tested.
