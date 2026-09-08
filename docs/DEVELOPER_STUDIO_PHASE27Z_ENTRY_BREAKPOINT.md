# Developer Studio Phase 27Z — Native Entry Breakpoint Debugging

Phase 27Z is the first bounded bare-metal debugger step after Phase 27Y. It
adds one deterministic entry stop to the existing asynchronous NativeElf
execution owner. The result is Outcome A: Developer Studio can start the real
compiler-built GUI application in Debug mode, stop the target before its first
user operation, inspect a bounded snapshot while the owner remains active,
Resume it, and complete the normal Phase 27Y GUI lifecycle.

## Phase 27Y prerequisite

Phase 27Y supplies the production path used here: compiler build and ELF
validation, deployment through the VFS, development App Model registration,
the dedicated NativeElf stack, cooperative AMD64 context switching, external
close, cancellation, terminal-result persistence, and generation-bound run
handles. Phase 27Z does not create a second runtime or a debugger-only
application path.

## Scope and Run versus Debug

Normal Run is unchanged. It uses the same build/deploy/loader path and enters
`gx_main` without installing a breakpoint. Debug is selected by the
append-only `GX_DEVELOPMENT_RUN_FLAG_DEBUG_CONTROLLED` flag. The run service
then owns one debug generation and supports only `POLL`, `RESUME`, and
paused `CANCEL_EXECUTION`.

The supported session states remain the Phase 27Y states plus `PAUSED`:

`REGISTERED → LAUNCHING → RUNNING → PAUSED → RUNNING → COMPLETED`

or, for the paused-cancel path:

`REGISTERED → LAUNCHING → RUNNING → PAUSED → CLOSING → CANCELLED`.

Failure and cleanup continue to use the existing `FAILED` and cleaning-up
states.

## Breakpoint target and metadata

The target is the final validated ELF entry address. The compiler/linker
contract fixes that entry to the `gx_main` function boundary at
`0x10001000` for the Phase 27Z fixture. The loader obtains the final `e_entry`
from the validated in-memory ELF image, and the run service requires that it
matches the runtime entry point and the validated image range. No source-line
table, DWARF/PDB data, arbitrary breakpoint table, or guessed address is used.
The bounded snapshot labels this one compiler-known function as `gx_main`.

## Chosen mechanism and trap handling

Phase 27Z uses a temporary in-memory `INT3` patch:

1. The ELF is built, deployed, validated, mapped, and made executable.
2. The loader confirms the final entry address and temporarily changes only
   the containing validated image page to writable, saves its byte, writes
   `0xCC`, flushes the instruction cache line, fences, and restores RX/NX
   permissions.
3. The service temporarily replaces IDT vector 3 with a bounded gate and
   saves the exact prior gate bytes. The gate saves the AMD64 general-purpose
   registers and hardware frame, calls the service-owned handler, restores the
   frame, and returns with `iretq`.
4. A valid stop requires the active debug generation, active target scheduler,
   target ownership, CPL0 code selector `0x08`, runtime entry identity, and
   raw hardware RIP equal to `targetAddress + 1`, which is the AMD64 `#BP`
   post-INT3 RIP.
5. On Resume or cancellation, the saved original byte is restored before the
   target is allowed to continue. The vector-3 gate is restored when the
   execution generation ends.

Unowned vector-3 traps are not swallowed: the temporary dispatch reports them
and halts. Page permissions, image bounds, and overflow checks prevent writes
outside the validated executable image. The patch is restored on Resume,
cancellation, loader teardown, failure, release, and normal completion.

## Stopped-state snapshot

The snapshot is a bounded copied SDK structure. It contains the debug status
and breakpoint kind, session/native-runtime/thread/binding identity, target
address, original and installed bytes, instruction pointer, pause reason,
`gx_main` function name, target stack bounds, and an AMD64 context containing
RIP, RSP, RBP, RFLAGS, and the saved general-purpose registers. The context
also carries the session generation and monotonically increasing stop
generation. It is not a memory inspector and does not permit register edits.

## Resume and restoration

Resume is accepted only for the current handle, generation, artifact identity,
thread, binding, target address, and stop generation, and only while the
session is `PAUSED`. It restores the original entry byte, clears the paused
binding, returns the target to `RUNNING`, and pumps the existing cooperative
owner. The target resumes at the original entry address, so the first
instruction executes once; it is neither skipped nor duplicated.

## Proof of real target stopping

The fixture's first user operation emits
`DEVELOPER_STUDIO_PHASE27Z_FIRST_USER_OPERATION_ONCE`, creates its window, and
renders `27Z GUI 27`. The QEMU serial order is:

`DEBUG_START → BREAKPOINT_INSTALLED → BREAKPOINT_HIT → PAUSED → snapshot /
owner checks → RESUME → first user operation → GUI render → external close →
gx_main return → teardown/cleanup`.

Before Resume, the runtime reports no active application, no window, no
render, no compositor window, and no first-operation marker. The owner runs a
benign heartbeat and successfully polls the stopped snapshot while the target
remains paused. After Resume, the marker occurs exactly once and the expected
GUI text renders.

## Cancellation while paused

Paused cancellation is accepted without Resume. The service restores the
entry byte, marks the generation closing/cancel-requested, resumes the target
only through a kernel cancellation return trampoline, and then runs the normal
loader teardown. The resulting terminal state is `CANCELLED`, the output
count is zero, no App Model registration remains, and no compositor window
remains. Because the target never reaches `gx_main`'s first instruction, the
fixture's user-operation marker is absent.

The smoke then starts a fresh normal Run after that cancellation and verifies
that it can again render, close, and clean up.

## Generation safety and negative coverage

Every debug command validates the current run handle plus optional strict
identity fields: session generation, native runtime ID, thread, binding,
target address, artifact SHA-256, and stop generation. The smoke passes prior
normal and debug handles into later sessions and verifies stale requests,
wrong target addresses, and wrong generations are rejected while the current
paused target is unchanged. Poll/Resume after the wrong state is rejected by
the state machine. A wrong artifact is rejected before a debug session can
start, so a stale executable cannot be debugged.

The normal Run fixture is exercised independently and still renders, closes,
completes, and cleans up. The existing hosted `NativeAppDebugger` tests remain
the host-side coverage for debugger identity, invalid addresses, stale
bindings, restoration bookkeeping, stepping, and terminal behavior. The
bare-metal-specific trap and context-switch claim is validated in QEMU.

## QEMU and regression evidence

The focused command is:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/smoke-compiler-bootstrap.ps1 `
  -Phase27ZOnly -BootCount 1 -TimeoutSeconds 120
```

It passed on three fresh boots after the corrected trap-frame layout and
included the external audit of the guest-generated `z27main.elf` on each boot.
The focused serial
proof includes build, stale-artifact rejection, normal Run regression, entry
breakpoint installation/hit, pause, exact identity, pre-user-code state,
owner activity, Resume/GUI/close/cleanup, paused cancellation, artifact
export, and `DEVELOPER_STUDIO_PHASE27Z_PASS`.

The applicable host checks are the Native ABI layout, NativeElf validator,
NativeElf trampoline, NativeElf runtime, development App Model, and hosted
debugger runtime tests. The compiler function/global/array/pointer/struct/
struct-array/multifile/object/bootstrap suites all passed. Focused QEMU boots
for Phase 27P, 27U, 27V, 27W, 27X, and 27Y each passed once; the Phase 27Z
focused proof passed three fresh boots. Prior Phase 27P–27Y history is
retained and is not rewritten.

## Deferred features and limitations

This phase intentionally does not implement user-selected or multiple
breakpoints, source-line stops, stepping, pause-anywhere, watch variables,
locals, register or memory editing, call-stack UI, expression evaluation,
source maps, full debug symbols, PDB/DWARF, attach, remote debugging, a
debugger console, or standalone debugger panes. Only AMD64 bare-metal
NativeElf `gx_main` entry debugging is supported. The temporary vector-3 gate
is intentionally scoped to the one active debug generation and must preserve
the existing IDT contract when the generation ends.

Developer Studio itself was not physically mouse-driven for this milestone;
the repository and QEMU proof were operated through the terminal automation
path.
