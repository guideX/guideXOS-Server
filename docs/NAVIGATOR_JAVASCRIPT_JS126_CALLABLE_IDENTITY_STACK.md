# JS126 callable identity and bounded invocation stack

**Result:** Outcome B — `JS126_CALLABLE_STACK_PARTIAL`
**Starting revision:** `a33856ee07fc2b86b5c24e0c61c6dba4babf3f63`
**Branch:** `NAVIGATOR_JAVASCRIPT_SUPPORT`
**Guest execution:** Not run; QEMU and QAC were not requested in this phase.

JS126 makes hosted Native and Host function records visible through the shared
portable lookup contract and gives function handles an explicit registry,
generation, and slot identity. Direct returned calls in the qualified portable
subset now transfer through an iterative loop, removing the repeated
`invokeFunction` native-stack frame for that path. Non-tail recursive calls
still recurse through the evaluator and remain above the known guest-stack
budget. This is a partial stack foundation, not guest stack readiness.

## Starting state and JS125 boundary

Preflight confirmed the requested repository, branch, and clean starting
worktree at the revision above. The protected `guideXOSServer.exe` was
8,398,274 bytes with SHA-256
`A5E439455E4D3E735DFB848EC296A3EC4B347822B21F5B1B736D51227046A971`.
The saved JS125 manifest described 42 generated fixtures totaling 458,372
bytes. `.phase` was absent. The JS125 report and fixture baseline were kept
separate from fresh JS126 results.

JS125 had portable ordinary/arrow invocation, explicit unsupported Native and
Host invocation, caller-backed argument spans, and a freestanding AMD64 link.
Its portable `FunctionStore` could not see the already registered hosted
Native/Host records. Its `invokeFunction` frame measured 368 bytes; at the
64-call limit, those frames alone contributed 23,552 bytes before evaluator
frames.

## Callable representation and lookup

Hosted Navigator stores all callable categories in `RuntimeContext::functions_`
as the shared 48-byte `FunctionRecord`. User records retain an AST declaration,
closure environment, and arrow lexical `this`. Native records retain a native
dispatch identifier. Host records retain a host-object handle, method index,
and receiver requirement. Hosted execution still dispatches Native and Host
records through its established evaluator and host adapter.

Before JS126, portable user functions lived in a separate caller-backed tail
store. JS126 keeps that store and adds a read-only prefix view onto the actual
hosted vector when portable execution is configured. The prefix is not copied;
the same record is authoritative for hosted dispatch and portable
identification. Once configured, additional hosted registrations are routed
to the tail store so the prefix vector cannot move. The view is scoped to the
active `RuntimeContext` generation and is discarded on reset.

`RuntimeFunctionId` is now 64 bits:

| Bits | Meaning |
| --- | --- |
| 63–32 | Per-context registry identity |
| 31–16 | Runtime generation |
| 15–0 | Function slot; `0xffff` is reserved |

The invalid all-ones handle is reserved. A process-wide atomic allocator gives
each `RuntimeContext` a distinct 32-bit registry identity; exhaustion fails
closed. `Value` remains 16 bytes because its existing payload already held 64
bits. A copied function Value, an object or Array property containing that
Value, and a closure retaining it all preserve the same handle.

The shared resolver checks registry identity, generation, slot range, prefix
or tail bounds, and record category. A foreign context returns
`RealmMismatch`; an old generation returns `StaleGeneration`; an invalid slot
or record remains an invalid reference. Runtime reset advances the existing
generation and invalidates prior handles. The 16-bit runtime generation
remains protected by the established host-generation limit; it does not wrap
and reuse a live context generation.

Portable callable identification recognizes User, Native, and Host records.
`isCallable` reports all three as callable. `invokeCallable` invokes only User
records and returns `UnsupportedOperation` for a valid Native or Host record.
Invalid, stale, foreign-realm, and noncallable values remain distinct errors.
No native callback pointer crosses the portable ABI, and no Native/Host
behavior is fabricated. Hosted Native/Host dispatch was left on its existing
path.

## Calls, receivers, closures, and bounded state

Ordinary functions use the explicitly supplied receiver; arrows use their
captured lexical `this`. Closure environments remain retained in the
caller-owned environment store, so captured bindings do not point into an
expired C++ invocation frame. The focused suite also checks functions resolved
from own and inherited data properties, object references, Array properties,
function returns, and closures. Accessors and host-object observable property
behavior remain outside the portable subset.

The public portable execution core does not allocate. Its caller supplies
bounded frame and argument arrays. Defaults remain 64 logical call levels, 64
arguments per call, and 100,000 shared execution steps. The hosted adapter
allocates its frame pool from `RuntimeLimits::maxCallDepth` and its argument
pool from `maxPortableArgumentValues` (512 `Value` entries by default). A
`PortableCallFrame` is 72 bytes, so the default 64-frame pool occupies 4,608
bytes of caller-owned memory. The default argument pool occupies 8,192 bytes.
These are bounded backing-store sizes; they are not native stack allocations.
The fixed call-depth and step checks remain active.

Direct `return callee(arguments)` calls and expression-bodied arrows whose
expression is a direct call pass their callee and arguments to an iterative
`invokeFunction` loop. Callee/argument evaluation stays left-to-right and
single-shot. Arguments are copied into the new activation bindings before the
caller-owned argument window is reused. The loop retains a frame and
environment for each logical call, so it obeys the same depth and storage
limits; this is not unbounded or constant-memory tail-call elimination. A
failed transfer propagates its typed error, and enclosing call state restores
the prior environment, receiver, argument mark, and logical depth. Successful
tail transfers preserve side effects already completed while evaluating
arguments.

Calls in other expression positions still use the ordinary nested evaluator
path. For example, recursive calls used in `return f() + 1`, assignments, or
arguments are not transformed into loop transfers. This is the main remaining
stack risk.

The relevant call paths are:

```text
Hosted:
Evaluator::evalCall -> Evaluator::invokeFunction
  -> User body | invokeNative | RuntimeContext::invokeHostMethod

Portable returned-call loop:
invokeCallable/evalCall -> invokeFunction
  -> executeStatementList -> evaluateTailCall
  -> evaluateCallOperands -> invokeFunction loop iteration

Portable non-tail call:
evalExpression -> evalCall -> invokeFunction
  -> executeStatementList -> evalExpression -> evalCall -> invokeFunction
```

The portable public entry point copies a completed return into
`PortableExecutionResult::finalValue`; failures retain a typed
`PortableExecutionError` and source location. `CallRestore` restores the
previous environment, `this`, argument mark, and depth on both success and
failure. Hosted errors continue to follow the existing evaluator's
`RuntimeErrorCode` path.

## Stack evidence and limits

The GCC 15.2.0 AMD64 `-fstack-usage` report measures the shared
`PortableObjectExecutionCore` frames as follows:

| Function | Bytes |
| --- | ---: |
| `invokeFunction` | 480 |
| `evalExpression` | 384 |
| `executeStatement` | 208 |
| `evaluateTailCall` | 192 |
| `readIdentifier` | 208 |
| `EnvironmentStore::create` | 96 |
| `EnvironmentStore::declareImpl` | 208 |

In JS125, one `invokeFunction` frame was 368 bytes and 64 nested invocations
contributed at least 23,552 bytes. For JS126's direct returned-call subset,
the iterative transfer keeps one 480-byte `invokeFunction` frame over the
logical chain instead of repeating it 64 times: 23,552 bytes to 480 bytes for
that frame component, a reduction of 23,072 bytes (about 98%). This comparison
excludes evaluator, environment, argument-expression, and stack-prologue
frames.

Non-tail recursion still creates nested `invokeFunction` frames. At the
64-call bound that term alone is at least 64 × 480 = 30,720 bytes, before
`evalExpression`, statement, identifier, environment, and other caller frames.
The known guest stack is approximately 16 KiB. Therefore F4 is partial: the
qualified direct returned-call path demonstrates a substantial
depth-dependent native-stack reduction, while general recursive invocation
remains unsafe to infer. No stack-size change was made.

## Focused tests and freestanding closure

The fresh JS126 focused suite covers shared Native/Host prefix resolution,
registry mismatch between contexts with matching local generations, stale and
invalid handles, ordinary and arrow receiver behavior, closure retention,
property and Array function lookup, call argument side effects, tail transfers,
mutual recursion, depth 63/64/65 boundaries, frame/argument exhaustion, and
recovery after errors. The final post-edit run passes **146/146**.

The AMD64 freestanding qualification compiles the shared lexer, AST, parser,
Value, environment, object store, and callable/invocation anchor as C++17 with
GCC 15.2.0. It uses `-ffreestanding`, no exceptions or RTTI, no standard
library headers/runtime, no red zone, and `-fstack-usage`; it then performs a
PE/COFF relocatable link and static archive audit. The selected component has
zero undefined symbols in both the object and archive, no TLS or constructor
sections, and no hosted import/runtime dependency. This is dependency-closure
evidence only, not a complete kernel link, boot-stack proof, guest run, QEMU
run, or QAC admission. The post-edit component is 281,162 bytes with SHA-256
`1793BF61AD9CD39AC5731AD1735916485B2069852B9AEC72F0C5870A507F511A`.

## Regression and protected-artifact qualification

The fresh complete JavaScript matrix contains **118 lanes** and reports
**19,313/19,313 assertions across 76 reporting lines**. All 118 per-lane logs
contain an explicit smoke/test PASS marker, all 118 stderr files are empty,
and the independent log validator finds no missing outputs or failed markers.
The pool's final summary incorrectly says 0/118 because its PowerShell process
objects returned an empty `ExitCode` field; a direct `Start-Process -Wait`
rerun of the JS10 lane returned exit code 0 with both test and smoke PASS
markers. The validator summary and raw per-lane logs are preserved alongside
the pool output so this collector defect is visible rather than mistaken for
test failures. Historical JS125 matrix and hosted aggregate results remain
identified as inherited evidence in the JS125 report and are not counted as
fresh JS126 runs.

The matrix includes fresh JS125 **121/121**, JS124 **1,179/1,179**, JS123
**70/70**, JS122 **67/67**, and the JS126 focused **146/146**. The separate
JS109 and JS110 hosted pages both pass against the fresh production candidate.
The complete hosted Navigator aggregate is **767/774**, with exactly the
seven established CSS failures from phases 3C, 3G, 6A, three checks in 6B,
and 6C; there are no additional hosted failures. The strict C++17 production
build passes. Its 9,382,002-byte candidate has SHA-256
`CF9ACE5AE1AD27309226B27982411AC4C5BA3277B332071EAA6D3388CA077F4F` and is
preserved outside Git. The original 8,398,274-byte executable was restored
with its protected SHA-256. All 42 generated fixtures still match the saved
hash manifest and total 458,372 bytes; `.phase` remains absent.

No portable JSON `toJSON` or reviver callback was enabled. The seven known
hosted aggregate CSS failures remain baseline failures if reproduced; any
additional failure would block hosted semantic parity. `.phase` remains
absent. No QEMU, QAC, synthetic guest image, MbedTLS change, or kernel-wide
stack change is in scope.

## Gate results

| Gate | Result | Evidence |
| --- | --- | --- |
| F0 — Architecture reconciled | Pass | Shared record model, distinct hosted dispatch boundary, and residual recursive path documented. |
| F1 — Unified generation-safe callable lookup | Pass | Registry/generation/slot resolver sees hosted prefix and portable tail records. |
| F2 — Safe invocation dispatch boundary | Pass | User calls work; valid Native/Host calls remain explicitly unsupported in portable code; hosted dispatch remains established. |
| F3 — Bounded invocation-state management | Pass for qualified subset | Caller-owned frame/argument pools, closure environments, receiver and error restoration remain bounded by existing limits. |
| F4 — Native stack-growth reduction | Partial | Direct returned calls iterate; non-tail recursion remains depth-proportional and exceeds the guest-stack budget. |
| F5 — Semantic and lifetime safety | Pass for qualified subset | JS126 focused checks pass 146/146; the complete regression logs contain no failed markers. |
| F6 — Freestanding dependency closure | Pass | AMD64 COFF link; zero undefined symbols, hosted dependencies, TLS, or constructors. |
| F7 — Hosted semantic parity | Pass with baseline | Matrix assertions and per-lane PASS markers are clean; JS109/JS110 pages pass; hosted aggregate remains 767/774 with only the seven known CSS failures; production build and protected-artifact checks pass. |
| F8 — Optional kernel-format qualification | Not run | Optional; no guest or kernel-format claim. |

## JS127 recommendation

Before JSON callback enablement or guest execution, JS127 should convert the
remaining supported recursive call positions to explicit continuation/control
state, or otherwise provide and prove a separately owned bounded invocation
stack for them. It should measure the complete evaluator call chain against
the fixed guest stack and account for retained environments and argument
storage. Keep portable Native/Host execution and JSON callbacks disabled until
those implementation paths are shared and qualified.

## Evidence location

Fresh JS126 scripts, compiler output, object/archive files, stack reports,
production candidate, hosted logs, per-lane matrix logs, and hash manifests are
preserved under:

`E:\CodexArtifacts\guideXOSServer_NAVIGATOR_JS_SUPPORT\JS126\run-20261119-202416-5d2a72dc82024dea8edbf28ea9966c68`
