# Developer Studio Phase 28F — Native Call Stack Inspection

Phase 28F adds the first NativeElf inspection operation to the Developer
Studio debugger. A compiler-built NativeElf target can now be genuinely
paused inside nested user code and return a bounded, source-aware call stack
without resuming or modifying the target.

## 1. Prerequisite and outcome

This phase builds on the Phase 28E source-aware Step Out implementation and
its AMD64 frame-pointer and return-address validation. The result is Outcome
A: the supported compiler frame chain is walked from the paused function to
the `gx_main` user root, with function and `GXSM` source identity preserved.

The operation is intentionally inspection-only. It does not implement frame
selection, locals, arguments, watches, expressions, register editing, stack
editing, arbitrary Pause, or a standalone Developer Studio Call Stack pane.
The Developer Studio UI lifecycle is exercised by the native smoke harness;
no mouse-driven GUI work or standalone-repository change was required.

## 2. Public ABI

Object ABI remains 9. Debug command 19,
`GX_DEVELOPMENT_DEBUG_CALL_STACK`, is appended after the existing Phase 28E
command 18. The result is the separate fixed-size
`gx_development_debug_call_stack` structure, rather than enlarging the
existing paused snapshot.

Each result contains the request/session identity, stop generation, exact
runtime stack bounds, status, error text, and a fixed array of
`GX_DEVELOPMENT_DEBUG_MAX_CALL_STACK_FRAMES` (16) records. The frame record is
272 bytes and includes depth, flags, instruction/stack/frame pointers, return
address, line/column, a 64-byte function name, and a 160-byte source path.
The complete result is 4,568 bytes. The two host ABI callback slots are
append-only after the Phase 28E table, and all layout assertions were updated.

Statuses distinguish success, safe truncation, stale identity, no paused
context, invalid frame, invalid return address, rejected request, and an
unsupported compiler frame or metadata boundary.

## 3. Paused-only and user-only policy

A request is accepted only for the current debugger-controlled generation and
the exact handle, artifact identity, runtime identity, thread, session, and
stop generation. The runtime must be paused with a complete user context; a
running, stepping, internal-trap, closing, completed, cancelled, failed, or
stale target is rejected. The hosted path additionally requires a retained
copy of the final ELF bytes so the validated `GXSM` metadata is tied to the
current image.

The walker never sets TF, resumes execution, installs or removes a breakpoint,
changes registers, writes stack/code memory, creates a step token, or changes
source context. Repeated requests read the same paused context and are
expected to return identical frame data.

## 4. Supported frame ABI and bounds

The supported compiler-generated AMD64 frame is the Phase 28E contract:

```text
push rbp
mov  rbp, rsp
...
mov  rsp, rbp
pop  rbp
ret
```
For every supported frame, `[RBP]` is the saved caller RBP and `[RBP+8]` is
the architectural return address. Each read is preceded by exact owned-stack
range validation and alignment/overflow checks. RBP must be an aligned frame
pointer whose complete link is in the runtime stack, and the caller RBP must
move toward higher stack addresses without exceeding the shared 4,096-byte
maximum frame distance. A fixed 16-entry seen-RBP array rejects self-cycles,
multi-frame cycles, and repeated frame locations. A valid chain longer than
16 entries returns the first 16 frames with `truncated = 1` and the
`TRUNCATED` status.

This is not arbitrary unwind support. Frame-pointer-omitted functions,
unknown binaries, kernel frames, runtime/trampoline frames, and malformed
chains are not exposed as trusted user frames.

## 5. Frame semantics and provenance

Frame 0 is the actual paused register context: normalized RIP, RSP, RBP,
function, and exact source mapping when available. Its saved `[RBP+8]` is
reported as its validated return address, but it is never used to derive the
top instruction pointer.

For each caller, the walker validates the saved RBP and return address before
constructing the next record. The return address must be in an executable
segment of the exact NativeElf image and resolve to a known user function.
Walking stops before an address that would expose the kernel or NativeElf
runtime as a user frame.

Caller instruction identity prefers the existing compiler-known direct-call
policy: `returnAddress - 5` must be an executable `E8 rel32` call site whose
target resolves forward to the expected callee and whose call site has
authoritative `GXSM` mapping. When that proof is unavailable, the result can
retain a trusted function with `SOURCE_UNAVAILABLE`; it does not guess a
source line. Root `gx_main` is marked with `ROOT`, has a valid user source
mapping, and terminates the user stack with no additional runtime frame.

Function/source records come from the current final image and validated GXSM
identity. Cross-file source paths are retained as canonical project paths;
the primary proof reports `src/helper.cpp` for the nested helper frames and
`src/main.cpp` for `gx_main`.

## 6. Hosted and bare-metal routing

The hosted callback copies and validates command 19 through
`DevelopmentRunService` into `NativeAppDebugger::CallStack`. The hosted
debugger retains the loaded ELF bytes and performs bounded reads only within
the registered runtime stack and executable image.

The bare-metal callback copies the request, routes it through
`NativeElfRunService::call_stack`, and copies the fixed result back to the
application. Both paths use the shared frame policy from
`native_elf_frame_policy.h`; Step Out and Call Stack therefore agree on frame
shape, stack direction, and caller-link limits.

## 7. Validation and integration proof

The Phase 28F fixture is under
`scripts/fixtures/phase28f`. Its real compiler-built call chain is:

```text
gx_main (src/main.cpp)
  -> helper (src/helper.cpp)
    -> helper_tail (src/helper.cpp)
```

The focused native smoke sequence pauses in `helper_tail`, queries:

```text
#0 helper_tail — src/helper.cpp:3
#1 helper      — src/helper.cpp:9 (direct call site)
#2 gx_main     — src/main.cpp:13 (direct call site, ROOT)
```

It checks ordered depths, function/source identity, frame pointers, return
addresses, stack bounds, repeat-query equality, paused register stability
(including RFLAGS), stale generation rejection, Running rejection, and the
Step Out integration. After one Step Out the live stack is verified as:

```text
#0 helper — src/helper.cpp
#1 gx_main — src/main.cpp (ROOT)
```

The same sequence then resumes normally, renders the GUI text `28F GUI 40`,
closes, completes, and cleans up. The smoke harness supports
`-Phase28FOnly` and performs a warm-cache build plus an external ELF/readelf
and objdump audit. It was run across three fresh isolated boots; every boot
passed the Phase 28F build, warm-cache, nested stack, source/order,
read-only, paused, Step Out shrink, stale, Running rejection, GUI,
completion/cleanup, and normal-Run markers.

Focused host validation passed:

```text
native_call_stack_controller_test: PASS
Native ABI layout test PASS
Native debugger runtime build and test PASS
native_source_step_controller_test: PASS
PowerShell parse PASS
git diff --check — no whitespace errors
```

The controller test covers valid framed shape, alignment, stack/link bounds,
monotonic and maximum frame distance, invalid image return addresses, cycle
detection, recursive-shaped distinct frame locations, and the fixed-capacity
boundary. The existing debugger runtime, ABI, and source-step suites remain
green, preserving the Phase 28C/28D/28E stepping path and normal Run
regression.

## 8. Cache, generation, and limitations

Warm-cache debug uses the reopened current ABI-9 artifact and identical
GXSM/function/source resolution. The result carries session and stop
generation, so an old request cannot interpret a new target generation. The
focused proof rejects a deliberately stale generation and a live Running
query.

Remaining limitations are deliberate: only the known compiler frame-pointer
ABI is supported; indirect calls and source-less call-site provenance are not
promoted to exact source locations; arbitrary DWARF/PDB/CodeView unwinding,
runtime/kernel call stacks, live-stack sampling, frame selection, locals,
watches, expressions, and stack/register editing remain deferred.

## 9. Reproduction commands

From the server repository:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/run-native-call-stack-controller-test.ps1
powershell -ExecutionPolicy Bypass -File scripts/run-native-abi-layout-test.ps1
powershell -ExecutionPolicy Bypass -File scripts/run-native-debugger-runtime-test.ps1
powershell -ExecutionPolicy Bypass -File scripts/run-native-source-step-controller-test.ps1
powershell -ExecutionPolicy Bypass -File scripts/smoke-compiler-bootstrap.ps1 `
    -Phase28FOnly -BootCount 3 -TimeoutSeconds 180
```
