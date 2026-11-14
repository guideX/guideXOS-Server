# Server v0.3 — S22R25 Scheduler and Context Implementation

**Result:** the dormant AMD64 voluntary context switch now has an explicit Microsoft x64 ABI implementation, bounded initial stack construction, and an executable hosted regression. No scheduler or timer-driven switching is enabled. The distinction between a kernel function-call context and a preemptible/protected application context remains explicit in code and this report.

## What changed

### Microsoft x64 voluntary switch

`kernel/arch/amd64/context_switch_asm.asm` is the sole AMD64 switch/startup assembly implementation. The former inline switch used SysV `RDI`/`RSI` argument positions, omitted Microsoft-nonvolatile `RDI`/`RSI`, relied on a process-global pending entry/argument, and exported incomplete full-context save/restore routines. The corrected contract is:

| Contract item | Implemented call-boundary behavior |
|---|---|
| Arguments | Microsoft x64: `RCX = SwitchContext** old_ctx`, `RDX = SwitchContext* new_ctx`; declaration carries `ms_abi` for GCC/Clang. |
| Return | A `void` call resumes after the saved call site. `RAX` remains volatile. Stack arguments are caller-cleaned by the Microsoft x64 convention. |
| Nonvolatile GPRs | Save/restore `RBX`, `RBP`, `RDI`, `RSI`, and `R12`–`R15`. Caller-volatiles are not part of an ordinary call-boundary context. |
| Stack | Save the caller's post-return `RSP` and return `RIP`; a resumed caller sees its normal 16-byte call-site alignment. |
| Shadow space | A new entry starts with a synthetic return slot and 32 bytes of shadow space. Its trampoline aligns the call site, sets `RCX` from the context-owned argument, calls the context-owned entry point, then reaches a halt stub if it returns. |
| Interrupt state | No interrupt frame, `RFLAGS`, vector/error code, segment state, privilege transition, or `iretq` path is represented. This function must not be called as an IRQ-frame switch. |
| FPU/SIMD | No FPU/XMM save is performed. The freestanding C/C++ contract disables MMX/SSE and a compile-time guard rejects a bare-metal object built with them enabled. SIMD-capable tasks remain unsupported until explicit per-task state exists. |

`SwitchContext` is statically fixed at 80 bytes: eight nonvolatile GPRs followed by saved `RSP`/`RIP`, with offset assertions matching the NASM operands. The invalid fake `FullContext` API was removed rather than left available as if it were a working interrupt context.

### Stack and context ownership

`init_context(stack_base, stack_size, entry, arg)` rejects a null entry, a stack smaller than the required context/shadow frame, and an overflowing or misaligned range. It creates the initial state inside the caller-owned stack and stores entry/argument in the context's saved `R12`/`R13`; there is no global pending startup state.

`ArchThreadData` now records caller-owned stack base/size as well as the saved stack pointer. `arch_thread_destroy()` clears metadata only and documents that the active stack cannot be destroyed there. The assembly return stub halts; freeing an active stack is deferred until a scheduler-owned reaper can switch away safely. This does not yet provide a task registry, bounded application stack policy, page-level guard, or memory reclamation.

### Scheduler and timer disposition

The AMD64 `arch_timer_tick()`/`arch_set_timer()` hooks remain inactive. The active PIT still performs timekeeping/heartbeat work. No run queue, task states, idle context, full interrupt frame, TSS stack update, deferred scheduling protocol, lock ownership, or reaper is connected to the switcher. The existing desktop loop and Native ELF launch path are unchanged.

The switcher is therefore a tested low-level context primitive, not a scheduler and not protected Native ELF execution. It is not a replacement for the hosted `scheduler.cpp`, and it does not preempt a Native ELF or provide kernel survivability.

## Validation performed

### Hosted Microsoft x64 execution test

The new `scripts/test-amd64-context-switch.ps1` assembles the production NASM object and a test-only probe with NASM `-f win64`, compiles with MinGW and the freestanding no-SIMD flags, and executes the x64 test binary on the host. The test covers:

- rejection of null entry, undersized stack, and overflowing stack range;
- context size/offset and initial stack layout;
- actual entry argument delivery and Microsoft x64 entry `RSP mod 16 = 8`;
- repeated switching in both directions;
- preservation of all eight Microsoft x64 nonvolatile GPRs for both the originating and task contexts.

Final hosted test result: **PASS**. Executable SHA-256 `e17512487b2639c0306bc42e70d16993c0883c87b85de3dc955511c1bebdb3b6`; test-only probe object SHA-256 `bdb1283c462ebc6a68cd2941118491ddfa4cb4844e33a3611e0782b68dd62e98`.

This is real execution of the Microsoft x64 ABI switch code under a hosted MinGW binary; it is not proof that the image boots or that guest interrupt scheduling works.

### Freestanding AMD64 image and stack usage

The public-preview-profile freestanding build completed into the separate S22R25 evidence directory with `-fstack-usage`, `-fcallgraph-info=su`, `-Wframe-larger-than=4096`, and `-Wstack-usage=8192`. No existing `kernel/build`, ISO, `dist`, or readiness artifact was overwritten.

- Kernel ELF: 2,812,296 bytes, SHA-256 `854712b5b2396a993829910998b60f682e5f1d067d839d1a187d120c3c1e8bb1`.
- AMD64 symbol audit: **PASS**, 50 undefined-symbol rows, 22 distinct `.symtab` entries, no relocations/imports.
- New context helper C++ stack estimates: `init_context` 8 bytes, `arch_thread_create` 48, `arch_thread_destroy` 8, `arch_switch_to` 64, timer stubs 8 bytes each.
- These are per-function static estimates, not proof of whole-kernel stack safety. Existing larger stack frames remain outside the context change and must be reviewed before the kernel runs more work on separate task stacks.

The failed first link exposed a source/object basename collision when the NASM file was initially named `context_switch.asm`. The production file now has a distinct basename (`context_switch_asm.asm`), and the final freestanding link includes its symbols. Hosted-only register/alignment probes live under `tests/` and are not linked into the kernel.

## Compatibility and unchanged paths

- The existing PacMan-only policy still requires the exact `guidexos-c-abi-v1`; no Native ELF ABI table was renamed or reinterpreted.
- No app was moved to the new switcher. No executable, manifest, package, `ESP`, data volume, or accepted candidate was replaced.
- Normal PacMan lifecycle and S22R23 persistence evidence remain evidence for their exact previously qualified candidate only; they are not inherited by the new 2,812,296-byte kernel image.
- No guest/QAC run was made because no safe scheduler or protected task can be entered in this milestone. QAC ownership/state was not queried.

See [the architecture report](RELEASE_S22R25_PROTECTED_EXECUTION_ARCHITECTURE.md) for the protection blockers and [the qualification report](RELEASE_S22R25_PROTECTED_EXECUTION_QUALIFICATION.md) for regressions, candidate preservation, and S22R26 scope.
