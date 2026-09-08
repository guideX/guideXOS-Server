# Developer Studio Phase 28B — Single-Instruction Step Into

Phase 28B adds the first machine-level Step Into primitive to the Phase 28A
source-breakpoint debugger. A real compiler-built NativeElf target pauses at a
source-selected instruction, executes one AMD64 instruction using architectural
single-step, traps through vector 1 (`#DB`), and returns to a new paused
snapshot.

## Prerequisite and scope

Phase 28A is the prerequisite. It supplies compiler/linker source mappings,
the deterministic `GXSM` trailer, one-shot `INT3` patching, normalized
breakpoint RIP, saved AMD64 state, owner-side polling, Resume, cancellation,
and normal GUI cleanup.

Phase 28B deliberately implements only architectural instruction stepping:

```text
Paused -> Step Into -> Stepping -> vector 1 #DB -> Paused
```

It does not implement source-line stepping, Step Over, Step Out, locals,
watches, expressions, call-stack UI, multiple breakpoints, or a standalone
Developer Studio debugger pane.

## Why instruction stepping comes first

The CPU already defines the exact boundary needed for the first debugger
milestone. A source-line step would require policy about mappings, calls,
branches, and compiler-generated code. Phase 28B therefore reports the
architectural post-instruction RIP and only retains source identity when the
new RIP is still inside the trusted source mapping span.

The object format remains ABI 9. Existing mapping fields (`moduleCodeOffset`
and `instructionBytes`) are sufficient for the bounded fixture proof; no
general x86-64 disassembler or ABI bump was added.

## AMD64 TF/#DB mechanism

At a genuine paused source breakpoint:

1. the one-byte `INT3` is restored;
2. the saved context RIP is already normalized to the original instruction;
3. the debugger records the context RFLAGS and sets `RFLAGS.TF`;
4. the scheduler resumes the target;
5. AMD64 retires one instruction and raises vector 1 `#DB`;
6. the vector-1 handler validates ownership, clears TF in the saved frame,
   copies the new register state, and changes the run state back to `Paused`.

The handler does not guess instruction length, place a temporary breakpoint,
or emulate the instruction. The CPU-provided post-step RIP is authoritative.

The temporary vector-1 entry stub clears live TF before calling C++ so kernel
handler code does not single-step. The saved hardware RFLAGS frame still
contains TF for provenance and is cleared by the accepted debugger-owned
handler before the target is exposed as paused.

## Vector-1 provenance

An arbitrary `#DB` is not swallowed. A vector-1 event is accepted as a Phase
28B step only when the active operation is a current debugger session in
`Stepping`, the step token is nonzero, the cooperative scheduler owns the
target, the target CS is the expected kernel user-code selector, TF is set in
the saved frame, the NativeElf runtime is still running, and the trapped RIP
and RSP lie inside the validated executable image and dedicated application
stack. Session/generation and stop-generation identity are copied into the
resulting snapshot. Rejected vector-1 events retain the strict unowned-trap
path rather than becoming a false debugger stop.

## Target context and scheduler ownership

The target context is the AMD64 scheduler context plus the fresh interrupt
frame produced by each actual trap. A fresh frame is expected: instructions
such as `push` may change RSP between steps. Provenance therefore binds the
active scheduler/runtime/generation and bounded image/stack ranges instead of
requiring the old interrupt-frame pointer to remain identical.

The owner continues to run while the target is paused and can poll the copied
snapshot. A subsequent Step arms TF in the current saved frame and pumps the
same target generation synchronously until vector 1 returns.

## Step request and invalid states

`GX_DEVELOPMENT_DEBUG_STEP_INSTRUCTION` is accepted only for a current
debug-controlled handle whose target is paused with a valid AMD64 context and
whose context does not already own TF. It is rejected for normal Run sessions,
stale handles/generations, wrong thread identity, Running or Stepping states,
completed/cancelled sessions, and invalid paused context.

The public run state `GX_DEVELOPMENT_RUN_STEPPING` and pause reason
`GX_DEVELOPMENT_DEBUG_PAUSE_REASON_SINGLE_STEP` are append-only additions.
While a step is in flight, polling reports `SINGLE_STEP_PENDING`; after vector
1 the snapshot reports `TRAP`, `SINGLE_STEP`, and the user-source step kind.

## Phase 28B proof fixture

The focused fixture is:

```text
scripts/fixtures/phase28b/guidexos.project
scripts/fixtures/phase28b/app/app.json
scripts/fixtures/phase28b/src/main.cpp
```

The source-selected location is `src/main.cpp:10` in `gx_main`. The live
fresh build produced this code at the resolved target:

```text
0x10001046: 8B 85 FC FF FF FF    mov eax,[rbp-0x4]   (6 bytes)
0x1000104C: 50                   push rax             (1 byte)
0x1000104D: B8 01 00 00 00       mov eax,1           (5 bytes)
```

The first paused snapshot has normalized RIP `0x10001046`, raw INT3 RIP
`0x10001047`, original byte `0x8B`, and the recorded start bytes above. The
three-step proof therefore expects post-step RIPs:

```text
0x1000104C -> 0x1000104D -> 0x10001052
```

The first transition is the core exact-one-instruction proof. The second
transition also verifies that a real `push`-induced RSP change does not break
vector-1 provenance. No GUI window exists before Resume. After Resume the
fixture renders `28B GUI 28`, closes normally, and tears down cleanly.

The snapshot records RFLAGS before arming, with TF armed, and after TF is
cleared. The focused smoke asserts the TF bit is absent from every paused
post-step context and that `rflagsWithTrapFlag == rflagsBeforeStep | 0x100`.
On the fresh QEMU proof the values are `0x202` before Step, `0x302` while
armed, and `0x202` after each accepted vector-1 trap. The first `mov` snapshot
has `RAX == 0x1C` (the fixture's value 28), and after `mov eax,1` the third
snapshot has `RAX == 1`. These register effects are asserted by the smoke in
addition to the RIP sequence. The middle `push rax` step is retained as a
real stack-changing instruction to exercise fresh interrupt-frame provenance;
the bounded proof does not expose a guessed stack delta as a debugger
contract.

## Source identity after a step

The post-step RIP is not converted into a guessed source line. If it remains
inside the selected mapping span, the existing source mapping is retained. If
it falls outside that span, source mapping validity is cleared while raw RIP,
function identity where trusted, and registers remain available.

## Consecutive stepping and Resume

The debug session performs three consecutive Step Into requests without
changing the session generation. Each request returns a single-step trap,
advances to the expected architectural RIP, increments stop generation, and
returns with TF cleared. Resume then clears any remaining trace state, returns
the target to ordinary Running, and does not reinsert the one-shot source
breakpoint. The same target renders, accepts normal close, reaches completion,
and releases without a second unexpected `#DB`.

## Cancellation and race ordering

Cancellation is accepted from the initial paused stop and from the paused stop
following a completed step. Once cancellation begins, no new step is armed.
The target context is replaced with a small scheduler-stack cancellation entry;
loader teardown and the normal completion path run there rather than attempting
an arbitrary `ret` on an interrupted application stack. This makes the
post-step cancel ordering deterministic and guarantees TF is cleared before
teardown. The focused test proves terminal `CANCELLED`, no GUI side effect,
complete cleanup, and releasability.

Close while paused remains a debugger-control operation: the focused session
rejects it cleanly and stays paused. Cancel is the supported termination path.

## Stale and unexpected events

The smoke sends a stale-generation Step request and a wrong-thread request;
both are rejected without changing the paused target. A vector-1 trap without
the complete active-step provenance is rejected by the strict handler and is
not converted into a user-visible step completion.

## Run and cache regressions

The Phase 28B smoke runs a warm cached build, a full debug/step lifecycle, a
cancel-after-step lifecycle, and a normal non-debug Run of the same artifact.
Normal Run continues to render `28B GUI 28` and close normally. Phase 28A's
source-breakpoint path remains covered separately by:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\smoke-compiler-bootstrap.ps1 `
    -Phase28AOnly -BootCount 1 -TimeoutSeconds 120
```

The Phase 28B focused proof is:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\smoke-compiler-bootstrap.ps1 `
    -Phase28BOnly -BootCount 3 -TimeoutSeconds 120
```

The final implementation passed three isolated fresh Phase 28B boots. The
focused Phase 28A regression also passed on a fresh boot. The requested prior
phase focused boots passed once each: 27P, 27U, 27V, 27W, 27X, 27Y, and 27Z.
The Phase 28B run also passed the warm-cache build, stale-generation and
wrong-thread negative controls, cancel-after-step cleanup, normal Run
regression, GUI render, close, and release checks.

The host debugger runtime test is:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\run-native-debugger-runtime-test.ps1
```

## Validation and limitations

Phase 28B uses the existing compiler, object/source-map serializer, linker,
NativeElf loader, cooperative owner, AMD64 exception path, and compositor
lifecycle under QEMU. It does not claim a direct-call callee-entry proof;
that is a strong later extension once the compiler fixture can select a direct
call cleanly. It also does not add Step Over, Step Out, source-line stepping,
run-to-cursor, breakpoint collections or UI, conditions, hit counts, locals,
watches, expression evaluation, stack unwinding UI, memory/register editors,
arbitrary pause, attach, remote debugging, or DWARF/PDB/CodeView support.

No physical mouse-driven Developer Studio GUI interaction was used. The
milestone proof is the real kernel/VFS/compiler/linker/NativeElf/compositor
path under fresh QEMU boots, verified through serial markers and snapshots.
