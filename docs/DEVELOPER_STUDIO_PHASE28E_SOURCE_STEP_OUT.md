# Developer Studio Phase 28E — Source-Aware Step Out

Phase 28E completes the basic source-stepping trio for the bounded NativeElf
debugger. From a genuine paused source location inside a user function,
`GX_DEVELOPMENT_DEBUG_STEP_SOURCE_OUT` executes the remainder of that current
frame, detects its real return, validates the caller, and pauses at the first
trustworthy mapped source location in the caller.

## 1. Phase 28D prerequisite

Phase 28D remains the prerequisite and is unchanged: source breakpoints,
AMD64 `INT3`/vector 3 handling, TF/vector 1 machine stepping, Source Step
Into, Source Step Over, generation-bound ownership, caller-frame validation,
temporary return breakpoints, GUI lifecycle, cancellation, stale-generation
rejection, and normal Run are preserved. Phase 28E adds a distinct operation;
it does not rewind or reuse a stored Step Over state.

## 2. Step Out semantics

The operation starts only from a paused, mapped, user NativeElf source frame.
It identifies the current frame, recovers its caller return address, records
the expected caller, installs one temporary return `INT3`, and resumes normal
execution. Internal machine steps are consumed by the controller. The user
sees one final `SOURCE_STEP_OUT` pause in the caller, not pauses on later lines
inside the exiting callee.

## 3. Current-frame identity

The bounded identity consists of the debugger registration/session generation,
the cooperative NativeElf execution owner and thread, current source
path/function/line/column, normalized paused RIP, RSP, RBP, loaded image range,
dedicated application stack range, recovered saved caller RBP, return address,
expected caller function/source path, and a Step Out token. The snapshot tail
records the relevant one-frame metadata; it is not a general call stack.

## 4. Compiler AMD64 frame ABI

The compiler emits the stable framed prologue `push rbp; mov rbp,rsp` and the
matching `mov rsp,rbp; pop rbp; ret` epilogue for the multi-function fixture.
The controller verifies the prologue in the loaded code before reading frame
links. A supported frame has an aligned RBP inside the application stack, RSP
inside that stack, and no more than 4096 bytes between RSP and RBP.

## 5. Return-address recovery

For a supported frame, the controller reads the ABI-defined links at `[RBP]`
(saved caller RBP) and `[RBP+8]` (architectural return address). Reads are
allowed only for aligned addresses wholly inside the runtime stack. No general
unwinder or guessed lexical caller is used.

## 6. Stack-bound validation

The runtime stack is validated with checked range arithmetic before frame-link
reads, before accepting the return trap, and during every post-return machine
step. The live QEMU stack was
`0x101F0000..0x10200000` (exclusive high bound).

## 7. Caller-function validation

The recovered return address must be inside the loaded executable image and
must resolve through the final ELF `GXSM` source map to the expected user
caller. After the return trap, the saved caller RBP, CS, stack shape, session,
owner, and function/source identity are checked again. A mismatch is a
non-success rejection.

## 8. Temporary breakpoint reuse

Phase 28E reuses the Phase 28D one-physical-breakpoint-slot patch helper. The
source breakpoint is restored before the independent return breakpoint is
installed. Its original byte, address, generation-bound token, callee frame,
expected caller, image, owner, and thread are retained in operation state.

## 9. Remaining-callee execution

The primary fixture enters `helper` with Source Step Into, then Step Out lets
the rest of `helper` run normally, including its nested `helper_tail` call and
the return epilogue. The deterministic post-resume output is `28E GUI 40`,
which depends on the helper's result and proves the remaining body was not
skipped.

## 10. Nested-call behavior

The primary helper calls `helper_tail` after the Step Out request. The return
breakpoint belongs to the helper frame's caller return address, so execution
through `helper_tail` does not complete Step Out prematurely. The secondary
leaf proof steps out of `helper_tail` to `helper`. A two-consecutive-Step-Out
session from an inner helper to an outer helper and then to `gx_main` is not
claimed.

## 11. Caller return trap

The owned vector 3 handler accepts only the expected `returnAddress + 1` raw
RIP while the operation is stepping and the target is running. It restores the
original byte exactly once, normalizes RIP to the return address, records the
raw trap and post-return context, marks the return boundary, and yields for
caller validation.

## 12. Caller source remapping

The raw return address is not treated as a source line. The controller clears
unmapped/compiler cleanup gaps through bounded TF/#DB steps and resolves the
first mapped caller identity whose source line differs from the original call
site mapping. The primary transition is `src/helper.cpp:8 helper` to
`src/main.cpp:15 gx_main`.

## 13. Root-frame behavior

`gx_main` is entered directly by the trusted NativeElf trampoline. There is no
supported user-image caller frame at that boundary. Step Out from a paused
`gx_main` deterministically rejects with
`GX_DEVELOPMENT_DEBUG_SOURCE_STEP_OUT_RESULT_NO_CALLER_FRAME`, remains paused,
and does not patch a guessed runtime return target. The focused QEMU root test
passed.

## 14. Leaf-function behavior

Leaf status does not by itself disable Step Out. The secondary fixture pauses
in `helper_tail`, recovers its frame links, returns to `helper`, and reports a
distinct `SOURCE_STEP_OUT` pause at `src/helper.cpp:10`. Unsupported or
malformed frame shapes are rejected before any patch.

## 15. Runtime/host-call behavior

The Step Out operation remains limited to user NativeElf frames and does not
single-step kernel or host-runtime implementation code. The primary flow
crosses from the user helper file back to the user main file, then Resume
drives the normal GUI host calls (`app_create`, `window_create`, render, and
close). Runtime-boundary checks remain active; a direct Step Out whose frame
cannot be proven user-owned is rejected safely.

## 16. Operation bounds

Post-return caller source advancement is capped at 128 internal instruction
steps (`STEP_OUT_LIMIT` on exhaustion). Each step must retain the expected
caller frame and remain inside the validated image/stack policy. This prevents
an infinite loop or non-returning path from hanging Developer Studio.

## 17. Cancellation

Cleanup paths retire the Step Out token, clear TF ownership, restore an owned
temporary breakpoint, and leave no active registration. The E proof exercises
independent cancellation after debugger operations and the leaf cleanup path;
the controller also handles cancellation while the operation is active. Exact
external timing of a mid-callee cancellation is not claimed because the
cooperative scheduler completes the bounded debug request synchronously.

## 18. Failure/completion handling

Invalid mapping/frame/link/image, unexpected caller, runtime boundary, source
map failure, target failure, and the 128-step limit produce explicit
non-success results and cleanup. If the target completes before a caller
return trap, the operation reports target completion rather than fabricating a
caller pause. Existing failure and completion lifecycle paths remain intact.

## 19. Stale-generation safety

Resume, cancellation, vector 3, vector 1, breakpoint ownership, and the Step
Out result all carry the active session/generation and operation token. The
primary E flow submits a stale-generation Resume after the completed Step Out
pause and receives a deterministic rejection.

## 20. Snapshot ABI change

Object ABI remains **9**; no persisted unwind metadata was needed. The debug
command value `18`, pause reason `6`, result enum, and bounded Step Out
metadata were appended after Phase 28D. The snapshot grows from **1168** to
**1576 bytes**. Existing command values and prior snapshot offsets remain
unchanged, and `tests/native_abi_layout_test.cpp` asserts the new tail.

## 21. Step Into → Step Out proof

The primary independent session is:

```text
src/main.cpp:14 gx_main
  Source Step Into
src/helper.cpp:8 helper
  Source Step Out
src/main.cpp:15 gx_main
```

The final snapshot has `SOURCE_STEP_OUT`, a completed source-step result, a
verified caller frame, and the expected cross-file caller identity.

## 22. Step Over equivalence

An independent session performs Source Step Over directly at
`src/main.cpp:14`; it reaches `src/main.cpp:15` and passes the same helper
exact-once/GUI consistency checks. The independent Step Into → Step Out path
reaches the same caller source destination.

## 23. Host tests

The host matrix passed 16 scripts: compiler functions, globals, arrays,
pointers, structs, struct arrays, multifile, object format, compiler
bootstrap, Native ABI/layout, NativeElf host/validator coverage, trampoline,
runtime, Development App Model, debugger runtime, and the Source Step
controller. The new controller test covers finite bounds, frame shape/link
validation, invalid return image, caller identity, and post-return frame
identity.

## 24. QEMU evidence

The fixture is under `scripts/fixtures/phase28e` and uses the real compiler,
object cache, final ELF/GXSM maps, linker, NativeElf loader, source breakpoint,
Source Step Into, temporary return `INT3`, AMD64 frame ABI, scheduler, and
compositor lifecycle. The focused command is:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\smoke-compiler-bootstrap.ps1 `
  -Phase28EOnly -BootCount 3 -TimeoutSeconds 120
```

All three fresh isolated boots passed build, warm cache, Step Into → Step Out,
Step Over equivalence, leaf, root rejection, raw instruction Step, normal Run,
artifact export, Resume, render, close, cleanup, and the E final marker.

## 25. Prior-phase regression results

Focused one-boot QEMU checks passed for Phase 27P, 27U, 27V, 27W, 27X, 27Y,
27Z, 28A, 28B, 28C, and 28D. The new Phase 28E check passed on three fresh
boots. No prior phase was amended or rewritten.

## 26. Limitations

The operation is intentionally bounded to compiler-emitted framed AMD64 user
NativeElf functions, one active source target, one cooperative execution
owner, and one physical temporary breakpoint slot. Indirect calls, arbitrary
unmapped frames, kernel/runtime frames, frame-pointer omission, general
recursion, and a full nested two-level Step Out proof remain outside this
phase. Cross-file Step Out is proven; cross-file Step Over remains a separate
limitation.

## 27. Deferred call-stack/locals/watches work

This phase does not add a call-stack window, general unwinding, frame
selection, locals, watches, expression evaluation, arbitrary Pause, Run to
Cursor, multiple/dynamic/conditional breakpoints, exception UX, memory or
register editors, watchpoints, attach, remote debugging, DWARF/PDB/CodeView,
or standalone Developer Studio debugger UI.

## 28. Whether actual Developer Studio GUI was mouse-driven

No. This milestone was verified through the server-side NativeElf debugger
controller, host tests, and isolated bare-metal QEMU automation. The fixture
does prove the real compositor render/close lifecycle, but no standalone
Developer Studio debugger UI was changed or physically mouse-driven.
