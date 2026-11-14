# Server v0.3 — S22R26 Protected Task Implementation

**Disposition: Outcome C — `USER_MODE_EXECUTION_PREREQUISITE`.** S22R26 completed the architecture and ABI audit and repeated the existing hosted switch regression and freestanding AMD64 build. It did not add or activate a user task: the kernel has no physical-page ownership and per-task address-space foundation to support a safe hardware transition. No scheduler, task exit, or CPL3 fault recovery is claimed.

## Starting state and preserved behavior

Starting branch was `main` at `f5cb9f4c50aabec24e5c3b42b1a0247bba3605c4`. The S22R25 switch correction was already present. The sole pre-existing untracked artifact, `assets/Backgrounds/troncity.png`, was preserved with SHA-256 `444ac64211907d80f0266e2283960cefaa932803d0aee7e0ccfd414f6fae2fda`.

No production launch path or policy was changed. PacMan remains on the existing synchronous, ring-0 Native ELF route. Its package identity, read-only `/Apps` source, resource integrity verification, ABI compatibility checks, and unauthorized-package rejection remain in place. The accepted S22R23 ISO and persistent-data evidence were not used for diagnostics or modified.

## S22R25 context-switch contract audit

`kernel/arch/amd64/context_switch_asm.asm` implements an ordinary Microsoft x64 call-boundary switch. It saves and restores all eight nonvolatile GPRs: RBX, RBP, RDI, RSI, and R12–R15. RCX and RDX carry the two arguments (`SwitchContext** old_ctx`, `SwitchContext* new_ctx`); RAX remains volatile. The saved 80-byte structure contains those registers, caller RSP after return, and the continuation RIP. A new context starts with a synthetic return address and 32 bytes of Microsoft shadow space; the trampoline calls the task entry with its argument in RCX and observes the ABI's entry alignment.

RFLAGS are not saved. The `sub rsp, 80` instruction changes arithmetic flags, and the Microsoft x64 function ABI does not promise that condition flags survive a call. A later privilege or interrupt frame must construct sanitized user RFLAGS separately. RIP is a call continuation, not an interrupt return address. The saved stack and stack storage remain caller-owned; neither can be reclaimed while active. The switcher is not an `iretq` path and is not connected to a freestanding run queue.

The freestanding AMD64 build uses `-mno-mmx -mno-sse -mno-sse2`, and `context_switch.cpp` has a compile-time guard for those options. Thus the current kernel call-boundary contract has no compiler-generated XMM/FPU state to switch. Microsoft x64 normally makes XMM6–XMM15 nonvolatile; this implementation would violate that general ABI if used by SIMD-enabled code. A later SIMD-capable scheduler must initialize FPU/SSE state and save/restore each task's required x87, XMM, MXCSR, and any enabled XSAVE components. The current hosted GPR test is not FPU/SIMD evidence.

The context switcher remains dormant in arbitrary kernel and IRQ paths. A corrected function-call ABI alone does not provide asynchronous state, RFLAGS, segment state, a task stack owner, CR3-safe mapping lifetime, or deferred reaping.

## Task lifecycle and controlled entry disposition

No protected task descriptor or lifecycle state machine was added. The repository has no implemented `Created → Ready → Running → Exited/Faulted → Reaped` path, owned user CR3, task-specific kernel entry stack, bounded exit trap, or off-stack reaper. The only current Native ELF context saves a runtime stack and invokes a verified ELF entry at CPL0. The application and desktop dispatch remain synchronously coupled.

No controlled CPL3 entry/exit or fault-recovery mechanism was added because the missing frame owner is a hard prerequisite. The current syscall stub is not used: it returns `-ENOSYS`, its `exit` handler halts, `yield` is a no-op, and its STAR selectors conflict with the loaded TSS selector. The present exception handler reports ring-0 Native ELF faults and has no validated user-task identity or safe termination/reap path. Kernel-mode page faults must continue to be treated as kernel faults.

## Scheduler disposition

The existing switcher remains a voluntary kernel call-boundary primitive. The freestanding PIT path increments time and reports heartbeat state but makes no scheduling decision. There is no AMD64 ready queue, full asynchronous context, reschedule boundary, per-task RSP0 update, or task reaper. S22R26 demonstrates no synchronous, cooperative, or preemptive user scheduling. In particular, it makes no claim that an infinite loop can be interrupted or terminated.

## PacMan mediated-service contract

The current `gx_app_context` carries a pointer to `gx_host_calls` and a `userData` pointer. In the current bare-metal runtime, the table entries are direct kernel function pointers and `userData` points into the kernel-owned `Runtime`. Those values cannot be handed to CPL3 as callable pointers.

The smallest future broker should use an operation-number trap ABI, not a user-visible table of kernel addresses. Each service must validate task identity and generation-tagged handles; copy bounded input strings and buffers into kernel-owned storage; validate the full user range, direction, alignment, arithmetic overflow, and page permissions; and copy results back only to mapped writable task pages. It must not retain unpinned pointers.

| Current service | Required protected behavior |
| --- | --- |
| Window creation, drawing, and frame presentation | Return task-owned handles; validate geometry and bounded strings; copy frame pixels after full range, stride, format, and byte-cap checks (current runtime cap: 16 MiB). |
| Input/event polling | Validate writable output storage and copy events out; route events to the task-owned window handle. |
| `/Apps` resources | Copy and bound package-relative paths; keep executable identity and resource-integrity checks; copy only bounded read results to user memory. |
| Audio | Validate and copy bounded PCM input; enforce the app permission; retain playback under an owner handle for teardown. |
| Exit, ticks, and logging | Use explicit syscall numbers and documented results. Exit marks the task terminal and defers resource release to a reaper. |
| Memory and resource ownership | Keep kernel allocations and runtime pointers private; account every frame, window, audio, and VFS reference to one task owner. |

Keep the public `guidexos-c-abi-v1` contract and PacMan route unchanged. A syscall-backed host API needs a separately versioned ABI and matching package migration; ABI v1 must not be accepted for CPL3 until its semantics are fully mediated.

## Validation performed

| Check | Result | Evidence / limit |
| --- | --- | --- |
| Hosted AMD64 context ABI | **PASS** | `scripts/test-amd64-context-switch.ps1`: stack bounds, initial layout, entry argument and alignment, shadow space, repeated switching, and eight nonvolatile GPRs. Test executable SHA-256 `9297837e57cc1bc32aa25889e7d17a92a66fb4f33ce6877d57e1f4a160ea6f6f`. This is hosted execution, not CPL3. |
| Freestanding AMD64 kernel build | **PASS** | Isolated output `C:\Users\guideX\AppData\Local\guideXOS\S22R26\kernel-amd64`; ELF64 size 2,806,352 bytes, SHA-256 `fe73801461cf63c2e6e56926610579bd67618f4d2f8f9e0f10210fb98c3a01ec`. |
| AMD64 symbol audit | **PASS** | 49 undefined rows / 22 distinct `.symtab` entries; no relocations or imports. Report is outside the repository at `C:\Users\guideX\AppData\Local\guideXOS\S22R26\kernel-amd64-symbol-audit.md`. |
| Stack audit | **PASS for the established guarded paths** | A separate freestanding build with `-fstack-usage`, `-fcallgraph-info=su`, `-Wframe-larger-than=4096`, and `-Wstack-usage=8192` produced 159 `.su` files. `tests/boot_stack_budget_guard_test.ps1` passed with its minimum 4,096-byte headroom. This does not prove whole-kernel stack safety. |
| Native ELF policy | **PASS** | 30 cases, zero failures, four synthetic approved-entry calls. This verifies existing package policy, not CPL3 isolation. |
| Native ABI layout | **PASS** | Existing hosted ABI layout regression. |
| VFS | **PASS** | Mount table 19/19; hosted read 4/4. |
| Settings | **PASS** | Native layout 1,037/1,037 (including 1,000 resize transitions); native runtime 908/908, with 100 lifecycle repetitions, 250 provider refresh cycles, and 1,000 notification/state transitions. |
| FAT/VFS persistence | **PASS on a fresh disposable copy** | 44/44 checks. The input accepted data image was hash-verified (`3ee67553ab9bece4130e4aa84ec4ed0d341cb29d7fd78798884c5ae3b0b7b148`); the script mutated only its new output copy. A first attempt copied an older, already-used S22R25 writable test image and failed seven clean-start/free-space expectations; neither source image was changed. The clean-input rerun passed. |

The preserved 16 KiB AMD64 boot stack remains unchanged. The largest current static frames include `gxos::display::{anonymous}::attempt_startup_restore` at 10,272 bytes, `apply_startup_fallback` at 9,232 bytes, `NavigatorApp::submitFormForBlock` at 8,928 bytes, and desktop persisted-shortcut load/save at 8,672/8,304 bytes. The stack guard covers a defined set of call paths; these large frames remain outside a whole-kernel proof. No claim is made that every existing call chain fits a 16 KiB stack or that a new task stack has been qualified.

## Protected-task regressions still blocked

No memory-owner, address-space, TSS-entry, task-exit, or user-fault implementation exists in this image. Consequently, creation/teardown ownership, full-path U/S/W^X checks, stack-guard behavior, TSS `RSP0`, user-entry RFLAGS, controlled exit, exception classification, repeated task reaping, and partial-initialization rollback have **no implementation and no passing regression result**. They are blocked by the prerequisite recorded in `docs/RELEASE_S22R26_USER_MODE_MEMORY_AND_TSS.md`; hosted metadata fixtures alone would not demonstrate hardware privilege enforcement.

## Scope boundary

The hosted ABI regression and freestanding build validate the existing baseline and its voluntary context correction. They do not test user/supervisor hardware enforcement, page allocation or teardown, TSS user entry, `iretq`, exceptions from CPL3, or task scheduling. The missing memory ownership contract and TSS/entry requirements are recorded in `docs/RELEASE_S22R26_USER_MODE_MEMORY_AND_TSS.md`; guest disposition is in `docs/RELEASE_S22R26_CPL3_GUEST_QUALIFICATION.md`.
