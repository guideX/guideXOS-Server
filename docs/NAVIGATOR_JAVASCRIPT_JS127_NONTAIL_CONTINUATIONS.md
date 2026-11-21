# Navigator JavaScript JS127 — Bounded non-tail continuation slice

**Outcome:** `JS127_NONTAIL_CONTINUATIONS_PARTIAL`
**Starting HEAD:** `7ff8e2c031a094241c502b1a311fdc82600314e4`
**Branch:** `NAVIGATOR_JAVASCRIPT_SUPPORT`
**Guest execution:** Not run; no QEMU or QAC.

## Starting state and protected artifacts

The requested root, branch, and HEAD matched the checkout. The worktree was
clean, the local branch was three commits ahead of the cached
`origin/NAVIGATOR_JAVASCRIPT_SUPPORT` reference, and `.phase` was absent.
The protected `guideXOSServer.exe` matched the accepted 8,398,274-byte file
and SHA-256
`A5E439455E4D3E735DFB848EC296A3EC4B347822B21F5B1B736D51227046A971`.
The 42 generated Navigator fixtures matched the saved JS125 manifest at
458,372 bytes. A byte-for-byte copy of the executable was saved outside Git
for restoration after production qualification.

JS126's 146/146 focused tests and its documented stack estimate were the
starting callable baseline. JS126 had already flattened direct returned calls;
its ordinary non-tail paths still retained the synchronous evaluator and
invocation chain.

## Call inventory and source call graph

The portable capability preflight accepts the existing shared AST subset.
Calls can appear in these positions:

| Position | Portable status | JS127 handling |
| --- | --- | --- |
| Direct `return f(args)` | Supported | Existing iterative tail dispatcher |
| Direct call in an expression-bodied arrow | Supported | Existing iterative tail dispatcher |
| `n && f(n - 1) + 1` (or the matching `||` form) in a one-parameter self-recursive function | Supported | New bounded explicit continuation loop for the exact shape below |
| Other binary or comparison operands | Supported | Existing native-recursive evaluator |
| Unary and logical operands | Supported | Existing evaluator; only the exact recursive shape above is flattened |
| Expression statement | Supported | Existing native-recursive evaluator |
| `let`/`const` initializer | Supported | Existing native-recursive evaluator |
| Identifier assignment RHS | Supported | Existing native-recursive evaluator |
| Object property assignment RHS | Supported by the object-enabled core | Existing native-recursive evaluator |
| Another call's arguments and nested arguments | Supported for statically recognized identifier calls | Existing native-recursive evaluator |
| Calls within blocks and sequential statements | Supported | Existing statement-list evaluator; callers remain suspended natively |
| Calls through closure-held identifiers | Supported when static capability analysis recognizes the declaration | Existing dispatch except for the exact self-recursive shape |
| Calls through a member expression such as `obj.method()` | Unsupported by portable execution preflight; hosted-only | No portable continuation path |
| Calls in `if`, loop, or conditional-expression positions | Unsupported by the current portable statement/expression subset | No portable continuation path |
| Native or Host callback invocation | Recognized as callable identity, but portable invocation is unsupported | Hosted dispatch remains unchanged |

The source call graph is:

```text
Hosted fallback:
  Evaluator::executeStatementList -> executeStatement -> evalExpression
    -> evalCall -> invokeFunction -> executeStatementList/evalExpression

Portable direct returned call (JS126):
  executeStatement(Return) -> evaluateTailCall -> evaluateCallOperands
    -> invokeFunction loop -> next activation

Portable ordinary non-tail call (still recursive):
  evalExpression -> evalCall -> evaluateCallOperands -> invokeFunction
    -> executeStatementList/evalExpression -> evalCall -> invokeFunction

JS127 qualified self-recursive expression:
  evalExpression -> evalCall -> matchesSelfArithmeticContinuation
    -> invokeSelfArithmeticContinuation loop
       -> evaluateCallOperands -> pushFunctionActivation (shared with invokeFunction)
       -> condition evaluation
       -> return-value unwind: evalExpression(right) -> applyBinaryValues
```

The JS127 path matches a single function return expression of the form
`n && f(n - k) OP c` or `n || f(n - k) OP c`, where `n` is the sole parameter,
the recursive call is the left operand of the binary expression, `k` and `c`
are numeric literals, and `OP` is `+`, `-`, `*`, or `/`. The callee must
resolve at runtime to the active function. If it resolves to another
function, the already evaluated callee and argument are dispatched through
the existing invocation path without reevaluation. Mutual recursion therefore
retains the original native-recursive path.

The pattern is deliberately narrow. It is a bounded trampoline in the shared
`PortableExecutionCoreImpl`, not a second parser, AST, value model, binding
system, or test-only interpreter. Both the ordinary dispatcher and the
continuation loop enter activations through the same
`pushFunctionActivation` routine. The common binary operator implementation
is shared with `evalBinary`.

## Continuation and frame ownership

`PortableCallFrame` grew from 72 to 80 bytes. The appended `continuationNode`
is a stable `AstNodeId`; the existing `previousEnvironment` and `previousThis`
fields hold the parent resume context. The frame remains caller-owned through
`PortableExecutionStorageView::frames`, backed by the hosted context's
`portableCallFrameStorage_` vector or the freestanding caller's supplied
storage. The continuation stores no pointer into a native stack frame or
movable argument span.

For each simulated recursive call, the child frame owns the caller's pending
binary AST node until return. The argument expression is evaluated once into
the existing bounded `Value` argument arena. The argument is copied to a
single `Value` before the arena slot is reused and then bound in the child's
ordinary `EnvironmentStore` activation. On unwind, the saved parent
environment and receiver are restored, the right numeric literal is
evaluated, and the shared binary operation produces the parent's result.
Completed nodes are not restarted. `CallRestore` clears continuation IDs on
abrupt unwind; the normal unwind clears them as each frame is popped.

Maximum continuation count for this path is at most the active call depth. At
depth 64, at most 63 frames carry a pending binary operation and one frame is
at the base case. No separate continuation array is allocated. Logical call
depth, frame capacity, argument count, argument arena capacity, environment
capacity, and the shared execution-step budget remain enforced.

Ordinary recursive calls still pass `undefined` as their bare-call receiver.
Arrow activation setup still installs its stored lexical `this`. The
qualified expression uses a numeric condition and numeric operands, so it
does not read the receiver or mutate a closure. Arrow self-recursion is
covered for result/depth behavior; receiver observability through a suspended
expression is not part of this slice.

## Evaluation, returns, and errors

The continuation performs callee resolution before argument evaluation, with
arguments left-to-right through the existing `evaluateCallOperands`. The
supported call has one argument. Its recursive subtraction is side-effect
free by the matcher constraint. The right operand is a numeric literal and is
evaluated only after the recursive result arrives. The logical operator keeps
its normal short-circuit result at the base case. Other calls retain the
existing evaluator's ordering and may retain native frames.

The helper uses the shared `consumeStep` budget at the corresponding AST
stages and never resets the shared counter. Return values are carried as a
normal `Value`; the focused recursive path returns a Number. Other invocation
paths retain the existing Undefined, Null, Boolean, String, Object, Array,
and Function-reference behavior from JS126. Call-depth, frame, and argument
capacity errors remain typed failures. Earlier effects and general error
categories continue through the existing core; JS127 adds tests for recovery
after depth failure and preserves the JS126 error/side-effect suite. Error
injection after mutation inside the new narrow pattern is not applicable
because its matched condition, recursive argument, and pending operand are
restricted to side-effect-free expressions.

## Memory and native stack evidence

Default caller-owned invocation scratch after this change is:

| Storage | Capacity | Bytes each | Maximum bytes |
| --- | ---: | ---: | ---: |
| Call/continuation frames | 64 | 80 | 5,120 |
| Argument Values | 512 | 16 | 8,192 |
| **Combined invocation scratch** |  |  | **13,312** |

The frame extension adds 512 bytes over JS126 at the default depth. The new
continuation is stored in that frame; it does not add a second 64-entry pool.
Persistent environments and closure records remain owned by the existing
realm stores and are retained until reset, as in JS126.

The accepted JS126 GCC 15.2.0 AMD64 `-fstack-usage` record measured
`invokeFunction` at 480 bytes. Its depth-64 non-tail invocation contribution
was at least 64 × 480 = 30,720 bytes, before evaluator frames. The JS127
freestanding build measures:

| Function | Bytes |
| --- | ---: |
| `invokeFunction` | 560 |
| `invokeSelfArithmeticContinuation` | 656 |
| `pushFunctionActivation` | 256 |
| `evalExpression` | 384 |
| `executeStatement` | 208 |
| `applyBinaryValues` | 320 |

For the qualified self-recursive path, the 560-byte invocation frame remains
once while the continuation loop handles all logical calls; its helper and
shared activation-setup frames execute sequentially inside that loop. The
named invocation/continuation/helper subtotal at the activation-setup point
is 560 + 656 + 256 = 1,472 bytes, before evaluator, environment-store, and
surrounding caller frames. This is not a total-stack bound. It removes the
depth-proportional `invokeFunction` term for the qualified shape, while other
supported non-tail positions and mutual recursion still retain depth-
dependent invocation frames (the invocation-frame term alone was 30,720
bytes at the 64-call cap before JS127), plus evaluator frames.

The front-end parser, nested AST evaluation outside this path, JSON parsing,
serialization, and the hosted evaluator have separate stack demands. JS127
does not establish total interpreter stack safety, boot-stack safety, or
guest execution. No kernel stack, scheduling, loader, or MbedTLS change was
made.

## Qualification and gates

The JS127 focused suite uses the shared portable evaluator and passes
**17/17**: self-recursive depths 1, 16, 32, 63, and 64; depth 65 rejection;
frame cleanup and reuse; caller-frame exhaustion; argument-arena exhaustion;
arrow self-recursion; mutual recursion on its existing path; and evaluation
order for nested arguments, arithmetic, assignment, comparison, and logical
short-circuiting. The unchanged JS126 focused suite remains **146/146**.
JS125's accepted baseline is **121/121**.

The pooled JS126 matrix had missing process-exit fields, so JS127 adds
`scripts/qualify-navigator-javascript-js127-matrix.ps1` as a bounded runner.
It starts each lane in its own PowerShell process, captures stdout and stderr
separately, records the actual exit code, applies a per-lane timeout, marks
launch/timeout/missing-output/collection failures indeterminate, and fails
closed when the exit code or output is missing. The final matrix passes
**119/119 lanes**, with **19,330/19,330 reported assertions across 76 lanes**;
all 119 child process exit codes are verified as zero. JS125, JS126, and JS127
report 121/121, 146/146, and 17/17 respectively.

The actual shared parser, stores, execution core, and JS126 invocation anchor
compile and relocatable-link under the accepted GCC 15.2.0 AMD64 COFF
freestanding profile. The JS127 audit checks that the continuation and
activation setup symbols are present, the call frame is 80 bytes, target
stack-usage records are emitted, and both component and archive have zero
undefined symbols. The audit is dependency-closure evidence only; no guest
link, boot, QEMU, or QAC run is claimed.

The strict C++17 production build exits 0 and creates a separate
9,395,086-byte candidate with SHA-256
`8D225C8A5CF8F25DA04354801CE3D56F6C18FB64222AAEB518F707D4F20988A6`.
Standalone JS109 and JS110 hosted-page checks both pass against that candidate:
their child processes exit 0, receive HTTP 200, retain the requested URL, and
report no JavaScript runtime error. The hosted Navigator aggregate reports
**767/774**, exactly the established seven CSS failures: phase 3C; phase 3G;
phase 6A; three checks in phase 6B; and phase 6C. Its process exits 1 because
the aggregate intentionally reports those known failures; no new JavaScript
failure appears.

After qualification, `guideXOSServer.exe` is restored to 8,398,274 bytes with
the protected SHA-256
`A5E439455E4D3E735DFB848EC296A3EC4B347822B21F5B1B736D51227046A971`. All 42
generated fixtures match the saved JS125 manifest with zero missing, extra,
or changed files and total 458,372 bytes. `.phase` remains absent. Production
candidate, stdout/stderr logs, per-lane process records, hosted outputs, and
artifact-verification records are stored outside Git under
`E:\CodexArtifacts\guideXOSServer_NAVIGATOR_JS_SUPPORT\JS127\run-20261121-7380b5b8b3964b47bee8a2e2161309d7`.

| Gate | Result | Qualification |
| --- | --- | --- |
| T0 — Architecture and call graph | Pass | All portable/hosted/unsupported call positions and recursive paths are inventoried above. |
| T1 — Explicit continuation state | Partial | One self-recursive logical/arithmetic shape uses bounded frame continuations. |
| T2 — Non-tail invocation correctness | Partial | Depth and nested-operation behavior pass for the documented shape; other call positions retain the existing evaluator. |
| T3 — Error and lifecycle safety | Partial | Depth/frame/argument exhaustion and cleanup/reuse pass; broad continuation error injection is outside the matched shape. |
| T4 — Native stack-growth reduction | Partial | Depth-proportional invocation frames are removed only for the qualified shape. |
| T5 — Bounds and resource enforcement | Partial | Call/frame/argument caps and shared step accounting remain in force; no cap was raised. |
| T6 — Freestanding dependency closure | Pass | AMD64 COFF link and continuation symbol/stack/dependency audit pass. |
| T7 — Hosted semantic parity | Pass | 119/119 matrix lanes and 19,330/19,330 reported assertions pass; JS109/JS110 hosted pages pass, and the aggregate matches the historical 767/774 CSS-only baseline. |
| T8 — Process qualification reliability | Pass | Every matrix lane has a recorded, verified exit code of 0; the expected-baseline aggregate exit code 1 is separately captured with stdout and stderr. |
| T9 — Optional kernel-format integration | Not run | Optional; no guest or kernel integration was attempted. |

## Recommended JS128 scope

Replace the pattern matcher with a general resumable execution machine in the
same shared core. Continuations need explicit expression stages, statement
program counters, call destinations, argument cursors, receiver values, and
abrupt-completion destinations. Size the caller-backed arena from the
configured AST and call-depth limits with checked arithmetic. Add generic
coverage for binary/logical/unary expressions, declarations, assignment,
nested arguments, property receivers, sequential statements, mutual
recursion, and error unwind before broadening portable callbacks. Keep a
separately owned bounded execution stack as a documented fallback if the
complete evaluator conversion cannot be proven within the available arena;
do not infer guest safety from an enlarged stack or this partial trampoline.
