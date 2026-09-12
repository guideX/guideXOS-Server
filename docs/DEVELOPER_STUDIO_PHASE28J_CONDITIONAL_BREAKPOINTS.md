# Developer Studio Phase 28J — Conditional Source Breakpoints

Phase 28J builds on the Phase 28I bounded, read-only watch evaluator. It adds
one optional condition to the existing one-source-breakpoint NativeElf debug
session. The condition is evaluated only after the CPU reaches the real source
breakpoint instruction.

## Semantics

The condition is copied into debugger-owned storage when the run is prepared.
The copy is NUL-terminated and bounded to the Phase 28I maximum of 256 bytes;
the existing 64-token, 64-node, 32-operator, parse-depth-16, and
evaluation-depth-32 limits remain in force. Setup validates the grammar before
the breakpoint is installed. The object ABI remains 10.

Conditions are always evaluated against the actual execution frame 0. The
selected inspection frame used by watch/locals queries is not consulted.
Integer zero is false and every nonzero signed integer is true. Pointer scalars
use null=false and non-null=true; they are never dereferenced.

The public run request appends `debugSourceCondition` after the existing source
breakpoint fields. The debug snapshot appends bounded condition status,
error-category, scalar-result, hit-count, length, and FNV-1a expression-hash
evidence. Command 21 (`EVALUATE_EXPRESSION`) is unchanged.

## Trap and rearm flow

For a conditional source breakpoint the AMD64 path is:

1. INT3 traps at the resolved source address.
2. RIP is normalized to the breakpoint address and the original byte is
   restored.
3. A trusted paused frame-0 context is established.
4. The Phase 28I evaluator is invoked with `auxiliaryAddress = 0`.
5. A true result exposes the ordinary paused debugger state, with the distinct
   `CONDITIONAL_SOURCE_BREAKPOINT` pause reason.
6. A false result sets TF, executes the original instruction exactly once,
   accepts the owned #DB, clears TF, verifies the original byte, reinstalls
   INT3, and returns to Running without exposing a user-visible pause.

The breakpoint remains persistent for the active debug generation. Resume from
a true conditional stop uses the same restore/TF/rearm machinery. Raw
instruction Step remains a user step and is kept separate from the internal
conditional continuation state.

Condition evaluation errors (unknown/dead identifiers, divide-by-zero,
overflow, stale identity, or evaluator failure) become an inspectable paused
conditional stop; they are never silently treated as false. Cancellation,
completion, rearm failure, and target failure clear the transient TF/rearm
state and go through the existing cleanup path. Session, generation, artifact,
target-address, and original-byte checks prevent stale reinstallation.

## Evidence fixture

The focused fixture is `scripts/fixtures/phase28j/src/main.cpp`, with the
source breakpoint at `src/main.cpp:9` in `phase28j_helper`. Four deterministic
helper calls make the same machine address execute repeatedly. The primary
condition is:

```text
((input - 1) * (input - 2)) * (input - 4)
```

It evaluates false for inputs 1 and 2, true for input 3, and false again after
Resume for input 4. The GUI result is `28J GUI 50`, which depends on all four
calls completing exactly once.

The fixture also proves constant-false (`0`), constant-true (`1`), a valid
divide-by-zero condition (`1 / 0`), malformed setup syntax, stale-generation
rejection, watch equivalence, locals, Call Stack, raw Step, ordinary Run,
render, close, and cleanup.

Run the focused proof with:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\smoke-compiler-bootstrap.ps1 -Phase28JOnly -BootCount 3 -TimeoutSeconds 120
```

The QEMU proof uses the real compiler, ABI-10 object/GXSM metadata, warm-cache
build, source mapping, INT3, TF/#DB, GUI lifecycle, and serial marker audit.

## Tests and regressions

The host checks are:

* `scripts/run-native-debug-watches-test.ps1` — Phase 28I evaluator and
  conditional-expression setup grammar/limits.
* `scripts/run-native-abi-layout-test.ps1` — append-only public ABI layout.
* `scripts/run-native-debugger-runtime-test.ps1` — hosted INT3 ownership,
  original-byte restoration, TF/#DB acceptance, rebind, source Step, overlap,
  Step Over/Out, and teardown behavior.
* `-Phase28JOnly` QEMU boots — conditional false/true/error behavior and the
  Phase 28G/28H/28I inspection regressions exercised at the true pause.

The source breakpoint remains one active source breakpoint per run. Multiple
breakpoints, configurable hit-count breakpoints, logpoints, tracepoints,
side-effecting expressions, pointer dereference, function calls, assignments,
and persistent GUI breakpoint configuration remain deferred.

No Developer Studio GUI was physically mouse-driven for this milestone; the
proof uses the existing programmatic debugger/run APIs and QEMU harness.
