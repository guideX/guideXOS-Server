# Navigator JavaScript JS128 — General resumable execution

**Outcome:** `JS128_GENERAL_EXECUTION_PARTIAL`
**Starting HEAD:** `3c142f24649380dd24ce022cf8d6179f393bb3a3`
**Branch:** `NAVIGATOR_JAVASCRIPT_SUPPORT`
**Qualification:** Hosted execution and freestanding AMD64 COFF compile/link; no guest execution.

## Starting state and JS127 result

The checkout started at the requested JS127 commit on a clean worktree, with
zero upstream divergence. The protected `guideXOSServer.exe` was 8,398,274
bytes and matched SHA-256
`A5E439455E4D3E735DFB848EC296A3EC4B347822B21F5B1B736D51227046A971`.
`.phase` was absent. The JS125 saved manifest contains 42 generated fixtures
totalling 458,372 bytes; it is copied into the JS128 evidence directory for
post-run verification.

JS127's outcome was `JS127_NONTAIL_CONTINUATIONS_PARTIAL`. Its 80-byte
`PortableCallFrame::continuationNode` handled the specific self-recursive form
`n && f(n - k) OP c` (and the matching `||` form). JS127's report measured a
560-byte `invokeFunction` target frame and identified depth-proportional
native invocation frames for other non-tail positions. Its focused suite was
17/17, the repaired matrix runner had trustworthy child exit codes, and no
guest execution was attempted.

## Existing call graph and JS128 path

Before JS128, portable script execution used this synchronous chain:

```text
executeStatementList -> executeStatement -> evalExpression
  -> evalBinary / evalLogical / evalAssignment / evalCall
       -> evaluateCallOperands -> invokeFunction
            -> executeStatementList / evalExpression
```

The recursive evaluator methods retained native frames whenever a nested
JavaScript function returned into an unfinished expression or statement.
JS127 intercepted one narrow self-recursive arithmetic pattern inside this
chain.

JS128 adds a caller-backed `PortableExecutionFrame[]` to
`PortableExecutionStorageView`. `PortableExecutionCore::run()` and the
object-enabled core use the resumable machine when that arena is supplied.
`RuntimeContext` provisions the arena and routes supported portable programs
through it. The portable preflight still runs before execution; unsupported
programs stay on the established hosted evaluator without replaying effects.

The execution path is now:

```text
PortableExecutionCore::run
  -> runExecutionMachine (one native executor loop)
       -> StatementList / Statement / Expression / FunctionBoundary frames
       -> pushFunctionActivation (one activation setup at a time)
       -> deliver Value or abrupt completion to the waiting frame
```

The machine reuses the accepted AST, Value, RuntimeStringStore,
EnvironmentStore, FunctionStore, PortableObjectStore, error types, and shared
step counter. It does not add bytecode, a parser, a Value representation, or a
binding table.

The `invokeCallable()` callback entry remains on the established synchronous
`invokeFunction()` path, including JS127's qualified specialized continuation.
Hosted Native/Host invocation and the hosted evaluator fallback also retain
their existing paths. Those entries remain native-recursive at unsupported or
non-tail call positions and are outside the JS128 stack-flattening claim.

## Execution-frame layout and stages

`PortableExecutionFrame` is 112 bytes with 8-byte alignment on the qualified
AMD64 target. It contains a tagged frame kind, stage and flags, stable AST node
IDs, a child cursor, argument-arena offset, call-frame index, saved lexical
environment, and four 16-byte Values. Frames hold offsets and handles, never
pointers into movable arrays or native temporary storage.

The four frame kinds are:

- **StatementList:** current statement index, lexical-scope restoration, and
  normal completion.
- **Statement:** variable initialization cursor, assignment/return position,
  or pending expression result.
- **Expression:** operator stage, completed operand Values, call target,
  receiver, argument cursor, property key, or pending assignment destination.
- **FunctionBoundary:** activation index, return destination, and argument
  region to release when the callee finishes.

Binary and comparison expressions evaluate the left child, retain its Value,
then evaluate the right child and apply the shared operator implementation.
Logical expressions retain the left Value and either short-circuit or push the
right child. Unary expressions resume after their operand. Assignments retain
an identifier or property destination while the RHS runs. Property targets
evaluate receiver and computed key once, before the RHS.

Calls resolve an identifier or supported member target first, retain a member
receiver when present, reserve a checked argument-arena region, and evaluate
arguments from left to right. Each completed argument is stored by arena
offset. Once operands are ready, `pushFunctionActivation()` validates the
generation-checked callable, creates the ordinary function environment,
binds parameters, and sets `this`. The caller's call-expression frame remains
on the execution arena while the function boundary and body execute. A return
Value is delivered to that waiting call-expression frame exactly once.

Function-body statement lists preserve their cursor. A variable initializer,
assignment RHS, expression statement, nested argument, or return expression
can therefore suspend and resume without restarting earlier work. Function
declarations and lexical bindings still use the accepted EnvironmentStore
predeclaration and initialization paths, preserving TDZ, shadowing, and const
checks.

## Returns, tail calls, errors, and cleanup

An ordinary return sets an abrupt-return state. The executor discards pending
expression and statement frames until the nearest function boundary, restores
the caller environment and `this`, releases the callee's argument window, and
delivers the return Value. A direct returned call uses an iterative transfer
through the same frame manager. The logical depth limit remains 64: every tail
transfer consumes a caller-owned call frame and contributes to the existing
depth accounting, even though it does not retain another native invocation
frame.

Errors remain typed failures. The executor stops before later statements,
clears active execution-frame records and call-frame continuation fields,
restores the prior environment, receiver, call depth, and argument top, and
keeps earlier object/environment mutations. It does not roll back the script.
The focused error tests include a failure after a property mutation and a
fresh appended program in the same realm after cleanup.

The machine uses the existing 100,000-step, 64-call, and 512-live-argument
limits, plus a separate 4,096-frame execution arena. Exhaustion returns the
explicit `ExecutionFrameCapacityExhausted` code, mapped to the hosted
`PortableCapacityExceeded` category. It does not silently lower the logical
call-depth limit. The arena bound is finite and can fail before maximum call
depth for sufficiently nested combinations; the maximum nesting product is
not claimed as available merely because the call-depth limit is 64.

The caller owns the frame and argument arrays. The runtime allocates a
4,096-entry vector at configuration time; the portable core does not allocate
while executing. The JS128 test peak is **453 execution frames** (50,736
bytes), logical call depth **64**, and **2** live argument Values. The configured
execution-frame backing is 4096 × 112 = **458,752 bytes**. Call-frame backing
is 64 × 80 = **5,120 bytes** and argument backing is 512 × 16 = **8,192
bytes**. Combined configured invocation scratch is **472,064 bytes**. These
are separate from persistent environment, function, string, object, Array,
frontend, parser, and JSON stores.

## Evaluation order and language coverage

The generic script path covers the accepted portable expression and statement
categories: primitive literals and identifier reads; unary, arithmetic,
comparison, and logical operators; variable initialization; lexical
assignment; expression statements; blocks; returns; ordinary and arrow
functions; nested calls and calls in arguments; property reads/writes and
property-held calls in the object-enabled core; object/Array literals; and
closures. Existing portable syntax limits remain in effect. The JS128 suite
records left-to-right argument side effects, statement order, exactly-once
calls, short-circuiting through existing portable regressions, member receiver
identity, arrow lexical `this`, and mutation before error.

Function aliases are accepted when the current static capability preflight
recognizes their declaration. General dynamic invocation of arbitrary
parameters and Native/Host callables remains unsupported in the portable core.
Control-flow syntax outside the current portable subset remains on the hosted
path.

## Native-stack evidence and remaining recursion

GCC 15.2.0 `-fstack-usage` for the actual shared machine reports:

| Function | AMD64 frame |
| --- | ---: |
| `runExecutionMachine` | 368 bytes |
| `machineRunExpression` | 576 bytes |
| `machineRunStatement` | 368 bytes |
| `machineRunStatementList` | 256 bytes |
| `machineInvokeCall` | 272 bytes |
| `machineTailReplace` | 480 bytes |
| `pushFunctionActivation` | 256 bytes |
| Legacy `invokeFunction` | 560 bytes |

In the qualified `run()` path, nested JavaScript calls are scheduled by the
single `runExecutionMachine()` loop. Source call-graph inspection shows that
the expression and statement methods return to the loop before it advances to
the child activation; they are not recursively retained per JavaScript call.
The named executor/expression/call/activation subtotal is 1,472 bytes on the
ordinary call-start path; the tail-transfer path adds its measured helper frame.
These sums are source-level path estimates, not a complete interpreter stack
bound, and omit surrounding callers, stores, parser/preflight work, JSON, and
host integrations.

Native recursion remains in the legacy `invokeCallable()`/`invokeFunction()`
path, in the hosted evaluator fallback, and in bounded AST capability
preflight walks (syntactic nesting, not JavaScript logical call depth). JS128
does not establish whole-interpreter stack safety or safety for the 16 KiB
kernel boot stack.

## Freestanding AMD64 qualification

The qualification builds the actual shared `PortableExecutionCore` template
and an anchor that supplies the new execution-frame arena to `run()`. GCC
15.2.0 targets AMD64 COFF with C++17, exceptions and RTTI disabled,
`-nostdlib`, `-nostdinc++`, freestanding mode, and `-fstack-usage`. The
dependency-closed component includes the parser, AST, values, environments,
object store, and JS128 anchor. The link uses `ld -r --no-undefined`.

The final audit result is recorded in the qualification evidence directory.
It checks AMD64 PE/COFF format, zero undefined symbols in the component and
archive, absence of TLS/constructor sections and prohibited runtime/platform
symbols, frame sizes/alignment, and target stack records for the execution
machine. This is compile/link evidence only; no synthetic guest, QEMU, QAC,
kernel-format integration, or guest execution was performed.

## Qualification status

| Gate | Result | Notes |
| --- | --- | --- |
| R0 — General execution architecture | Pass | Existing recursive and new loop-based call paths are inventoried above. |
| R1 — Resumable expression evaluation | Pass | Binary, comparison, logical, unary, assignment, member, and call stages resume through stable frames. |
| R2 — Resumable statement evaluation | Pass | Initializers, assignments, sequential statements, returns, and lexical scopes retain their positions. |
| R3 — Resumable arguments and receivers | Pass for script execution | Ordered nested arguments and supported property receivers use the shared frame machine; callback `invokeCallable()` remains legacy. |
| R4 — General nested-call behavior | Pass for qualified subset | Non-tail, mutual, mixed, and direct tail paths use generic activation/return state, without function-name or AST-shape dispatch in `run()`. |
| R5 — Error, lifetime, and capacity safety | Pass for qualified subset | Explicit unwind, side-effect retention, frame/argument/depth/step errors, and reuse are tested. |
| R6 — Native-stack flattening | Partial | `run()` script calls flatten; callback invocation, hosted fallback, and syntax-preflight recursion remain. |
| R7 — Freestanding dependency closure | Pass | Actual shared machine anchor compiles/links with zero undefined symbols under the accepted AMD64 profile. |
| R8 — Hosted semantic parity | Pass | The unique full JavaScript matrix passes 120/120 with 19,349/19,349 reported assertions; hosted JS109/JS110 pass, and the aggregate remains at its 767/774 baseline with only the seven established CSS failures. |
| R9 — Qualification runner reliability | Pass | All 121 completed child-process records have verified exit code 0, stdout/stderr files, and per-lane metadata. The unique 120-script inventory is complete. One earlier JS27 attempt was stopped before result collection and is recorded separately as indeterminate; its filtered-shard retry passed with exit code 0. |
| R10 — Optional kernel-format qualification | Not run | Optional; excluded from JS128. |

Focused suites currently pass JS128 **19/19**, JS127 **17/17**, JS126
**146/146**, JS125 **121/121**, and portable execution **109/109**. JS115's
portable frontend hosted checks and AMD64 object audit pass; JS118 passes
**44/44**. The full matrix passes **120/120 unique lanes** and reports
**19,349/19,349 assertions across 77 lanes**. The runner completed 121 child
processes (including one repeated JS26 run); all 121 have verified exit code
0. One earlier JS27 child was stopped during the switch from one serial matrix
to filtered shards. Its missing exit code and unavailable stdout/stderr are
classified `Indeterminate`, never as a pass. JS27 was rerun in shard 1 and
passed with exit code 0. The combined per-lane evidence records every unique
lane's actual exit code, timeout and launch status, stdout, and stderr.

Standalone JS109 and JS110 hosted pages both pass against the strict C++17
production candidate. The hosted Navigator aggregate is **767/774**, with
exactly the seven established CSS failures in phases 3C, 3G, 6A, three in 6B,
and 6C. The strict production build exits 0 and produces a 9,429,318-byte
candidate with SHA-256
`E5537268BABF3F381255EC2CAE9B514D9B56476199371F1D3DDE450A35095501`; its
stderr contains 213 compiler warning diagnostics, none naming the changed
portable JavaScript core/runtime files. The original executable is restored,
and a post-run manifest comparison confirms all 42 generated fixtures and
458,372 bytes match baseline; `.phase` is absent.

## Artifact and guest boundaries

The original 8,398,274-byte executable is backed up outside Git and restored
with baseline SHA-256
`A5E439455E4D3E735DFB848EC296A3EC4B347822B21F5B1B736D51227046A971`. The
42-file fixture manifest is preserved outside the repository; all 42 files
match their baseline sizes and hashes and total 458,372 bytes. `.phase` is
absent. The production candidate, per-lane stdout/stderr and process records,
matrix combination and indeterminate-attempt audit, AMD64 objects, symbols,
sections, stack reports, and hashes live under the unique JS128 directory in
`E:\CodexArtifacts\guideXOSServer_NAVIGATOR_JS_SUPPORT\JS128`.

No kernel boot-stack, scheduler, MbedTLS, linker-script, guest NavigatorApp,
QEMU, or QAC changes are part of JS128.

## Recommended JS129 scope

Move the public portable `invokeCallable()` entry onto the same execution
machine while preserving its explicit receiver and callback ABI. Then make the
execution-frame capacity configurable through runtime limits and derive its
default from the accepted simultaneous AST nesting and logical call-depth
budget. Add capacity tests at combined nesting boundaries and target
high-water instrumentation for the actual hosted/guest execution context
before any guest integration or JSON callback enablement.
