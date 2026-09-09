# Developer Studio Phase 28D — Source-Aware Step Over

Phase 28D adds a distinct source-aware Step Over request to the NativeElf
debugger. It preserves Phase 28B machine Step Into, Phase 28C source Step
Into, Resume, source breakpoints, and normal Run. It does not add Step Out,
locals, watches, expressions, a call-stack UI, or a standalone debugger pane.

## Public contract

The append-only command is
`GX_DEVELOPMENT_DEBUG_STEP_SOURCE_OVER` (`17`). The existing
`GX_DEVELOPMENT_DEBUG_STEP_OVER_CALL` (`11`) is accepted as the native alias
for callers that already use the Step Over command slot. A completed source
over reports `GX_DEVELOPMENT_DEBUG_PAUSE_REASON_SOURCE_STEP_OVER` (`5`).

The debug snapshot grows from the Phase 28C size of 1000 bytes to 1168 bytes.
The new tail records the source-over result/count/limit, detected call RIP,
callee target, return address, starting and final RSP/RBP, raw return-trap
RIP, nested-return and temporary-breakpoint counts, internal machine-step
count, caller-frame verification, callee function name, and the original
return-byte evidence. The existing run/object ABI remains unchanged at ABI 9.

## Direct user-call path

At a paused trusted source mapping, the controller restores the source
breakpoint and classifies only a bounded, compiler-trusted direct `E8 rel32`
call in the current source mapping span. The displacement target must remain
inside the final executable code range and resolve, through the trusted GXSM
source map, to a mapped user function different from the caller. Calls using
the runtime/host-call form, ambiguous source spans, malformed displacements,
or unmapped targets are not treated as direct user calls; they use the normal
source-step fallback or return an explicit source-map failure.

For a valid direct user call, the controller:

1. records the call and the architectural return address;
2. patches one temporary INT3 at that return address, retaining its original
   byte and a generation-bound internal breakpoint token;
3. resumes the target without exposing the callee's compiler-generated
   prologue/body instructions as user stops;
4. accepts only the owned return trap at `returnAddress + 1`, normalizes RIP
   back to the return address, restores the exact original byte, and verifies
   the caller's saved RBP frame;
5. uses the existing bounded TF/#DB instruction primitive until the next
   trusted source identity in the same caller function.

The temporary return breakpoint and every vector-1 step are validated against
the active scheduler owner, debug generation, target CS, executable image,
and dedicated application stack. The source-over controller has the same
128-instruction bound as Phase 28C. A caller-frame mismatch, runtime-boundary
escape, invalid source map, target completion/failure, or exhausted bound
produces an explicit non-success result and never claims a user source stop.

## Ordinary-location fallback

If the paused source operation does not contain one validated direct user call,
Step Over delegates to the proven Phase 28C source-step controller. The
command and pause reason remain distinct (`SOURCE_STEP_OVER`), while the
source-step result/count and TF/#DB safety behavior are retained. This makes
ordinary source locations behave like source Step Into's non-call path without
entering runtime code or exposing a false callee stop.

## Fixture and evidence

The focused fixture is:

```text
scripts/fixtures/phase28d/guidexos.project
scripts/fixtures/phase28d/app/app.json
scripts/fixtures/phase28d/src/main.cpp
```

It starts at `src/main.cpp:14`, calls `helper` on that line, then has three
caller-side source operations on lines 15–17. The QEMU controller proves:

- the preserved source Step Into path from the line-14 call site into `helper`;
- direct-call classification, return target, temporary return INT3, return
  trap, caller-frame verification, and callee name `helper`;
- the helper's exact-once effect through the deterministic `28D GUI 40`
  result, with no user-visible helper pause;
- three consecutive source-over requests, with line 14 using the direct-call
  path and lines 15–16 using the ordinary-location fallback;
- Resume, GUI rendering as `28D GUI 40`, normal close, teardown, and a fresh
  normal Run regression.

The serial proof markers include
`DEVELOPER_STUDIO_PHASE28D_CALL_CLASSIFY_PASS`,
`DEVELOPER_STUDIO_PHASE28D_RETURN_TRAP`,
`DEVELOPER_STUDIO_PHASE28D_CALLER_FRAME_PASS`, and
`DEVELOPER_STUDIO_PHASE28D_THREE_SOURCE_STEPS_PASS`.

## Verification

Focused host checks:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\run-native-source-step-controller-test.ps1
powershell -ExecutionPolicy Bypass -File .\scripts\run-native-abi-layout-test.ps1
```

Focused bare-metal checks:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\smoke-compiler-bootstrap.ps1 `
  -Phase28DOnly -BootCount 3 -TimeoutSeconds 120
powershell -ExecutionPolicy Bypass -File .\scripts\smoke-compiler-bootstrap.ps1 `
  -Phase28COnly -BootCount 1 -TimeoutSeconds 120
```

Phase 28D intentionally does not claim general compiler-generated control-flow
coverage, indirect-call stepping, nested thread support, or a user-visible
locals/call-stack surface. Those remain later scope.
