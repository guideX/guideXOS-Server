# Developer Studio Phase 28C — Source-Aware Step Into

Phase 28C adds source-aware Step Into to the Phase 28A/28B NativeElf
debugger. The existing machine-level Step Into remains available: Phase 28C
adds a separate request that performs bounded AMD64 TF/#DB steps until the
trusted source identity changes.

## Scope and contract

Phase 28A remains the prerequisite. It supplies the compiler-emitted source
mapping records, the deterministic `GXSM` trailer, source breakpoints, saved
AMD64 state, run ownership, freshness checks, and normal GUI lifecycle.
Phase 28B supplies the real architectural instruction-step primitive and its
vector-1 (`#DB`) validation. Phase 28C reuses that primitive instead of
replacing it with a guessed instruction decoder or a second breakpoint path.

The public development-debug command is
`GX_DEVELOPMENT_DEBUG_STEP_SOURCE_INTO` (`16`). A source step reports
`GX_DEVELOPMENT_DEBUG_PAUSE_REASON_SOURCE_STEP` (`4`) and one of the explicit
source-step results: completed, bounded-limit, target-completed,
target-failed, cancelled, no-source-mapping, invalid-source-map, unsafe
runtime-boundary, or stale. The appended snapshot fields record the source
step result/count/limit, starting and final RIP, and the starting source
identity. The legacy fields and the Phase 28A/28B object ABI remain stable;
the native debug snapshot is now 1000 bytes.

The source-step state machine is:

```text
Paused at trusted source mapping
  -> Source Step Into
  -> bounded TF/#DB instruction steps
  -> Paused at a different trusted source identity
```

Each request is tied to the active debugger generation, operation owner,
target, context, and validated image/stack ranges. Resume and cancellation
clear source-step state. A stale owner or stale generation cannot mutate the
current operation.

## Trusted source identity and lookup

The stopping identity is the tuple:

```text
canonical project-relative path, function name, source line,
source column when both sides provide a meaningful non-zero column
```

The current RIP is mapped only through the final executable's trusted `GXSM`
records. The lookup validates the trailer, header, hash, counts, code range,
function ranges, and mapping ranges, then accepts an address only when it is
inside an exact `[finalOffset, finalOffset + instructionBytes)` record. A
valid executable gap is not assigned to the previous or next source line.
There is no source-name, line, function, or cross-file identity invented from
RIP arithmetic. If the starting location is unmapped or the trailer is
invalid, the request ends with an explicit failure result.

Instructions that share the starting source identity are allowed to run. A
new line, canonical path, or function ends the request. A missing mapping
between two instructions does not silently become a source transition. The
policy follows the actual executed path, so branches and loops are bounded by
the same instruction budget rather than by a static source-line guess.

## Bound and safety policy

`GX_DEVELOPMENT_DEBUG_SOURCE_STEP_MAX_INSTRUCTIONS` is 128 instructions per
request. This is deliberately a low-hundreds bound: it covers ordinary
compiler-generated instructions and short same-line control-flow sequences
while guaranteeing that a loop, malformed mapping, or unexpected execution
path cannot run indefinitely. The controller and the pure host policy test
both assert the exact 128-step limit.

Native code is the only source-aware execution domain. At a host/runtime call
boundary, the target may retire one instruction and produce a vector-1 trap
outside the application image. While and only while an active source-step
operation has valid target provenance, that one boundary trap is accepted so
the debugger can stop deterministically. The handler clears TF in the saved
context, clears the exposed source mapping, returns
`UNSAFE_RUNTIME_BOUNDARY`, and does not invent a kernel or runtime source
identity. The caller can then cancel or resume through the existing lifecycle;
cleanup always clears the step state. Runtime code is never single-stepped as
if it belonged to the user source map.

## End-to-end fixture

The Phase 28C fixture is a real compiler-built GUI application. Its source
starts in `gx_main` on line 10 and has several same-line machine instructions
spread across lines 10–13, followed by a direct call on line 14 to:

```text
int helper(int value) {       // line 23
    int adjusted = value + 1; // line 25
    return adjusted;           // line 26
}
```

The QEMU controller proves three source transitions within `gx_main`, then a
fourth request steps into `helper` and observes the callee's trusted path
`src/main.cpp` / `helper` / lines 23–26. A separate call-boundary session
starts at the GUI host call and proves the deterministic
`UNSAFE_RUNTIME_BOUNDARY` result with TF cleared and no source mapping.

The same smoke run also proves:

- warm-cache rebuild and final-ELF `GXSM` validation;
- owner, target, generation, and stale-request rejection;
- source-step cancellation and fresh-operation cleanup;
- ordinary Run, GUI rendering (`28C GUI 56`), close, and teardown;
- Phase 28A source-breakpoint behavior and Phase 28B raw instruction Step
  regression.

The fixture is driven by the existing in-kernel smoke controller. It does not
claim a separate cross-file or dedicated branch fixture; the mapping and
identity implementation itself is path/file/function agnostic, while the
proof fixture exercises a same-file function transition and actual generated
control flow.

## Verification

Focused host checks:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\run-native-source-step-controller-test.ps1
powershell -ExecutionPolicy Bypass -File .\scripts\run-native-abi-layout-test.ps1
powershell -ExecutionPolicy Bypass -File .\scripts\run-compiler-object-host-test.ps1
```

Focused bare-metal checks:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\smoke-compiler-bootstrap.ps1 `
  -Phase28COnly -BootCount 3 -TimeoutSeconds 120
powershell -ExecutionPolicy Bypass -File .\scripts\smoke-compiler-bootstrap.ps1 `
  -Phase28AOnly -BootCount 1 -TimeoutSeconds 120
powershell -ExecutionPolicy Bypass -File .\scripts\smoke-compiler-bootstrap.ps1 `
  -Phase28BOnly -BootCount 1 -TimeoutSeconds 120
```

The completed run passed three isolated fresh Phase 28C boots, plus one fresh
Phase 28A regression boot and one fresh Phase 28B regression boot. The
standalone `D:\dev\guideXOS_Developer_Studio` repository was not changed.

Phase 28C intentionally does not add Step Over, Step Out, locals, watches,
expressions, a call-stack UI, multiple breakpoints, or a standalone debugger
pane.
