# Navigator JavaScript JS129 Recovery — Unified Call Entry and Preflight

**Recovery result:** reconstructed JS129 behavior, verified on `NAVIGATOR_JAVASCRIPT_SUPPORT`

**Implementation commit:** `ff727380a13bf4ed367b895330ea87449c35de63`

**Parent:** JS128 `86ee961d3d14ca27bec8cbae25a00d237c64fd92`

**Reported original JS129 commit:** `f9aaba0f7427f5f38600233e1fe70e7539764d85` (not recovered)

## Recovery finding

The fresh E: checkout began clean at JS128. The reported JS129 commit was not
present in reachable or dangling local Git objects, the clone's refs or
reflogs, the inspected accessible E: guideXOS checkouts, or the inspected
JS129 artifact tree. GitHub's target branch was confirmed at JS128; an exact
lookup of the reported commit did not resolve. The historical
`D:\dev\guideXOSServer_NAVIGATOR_JS_SUPPORT` path was inaccessible when
checked once.

The existing `E:\CodexArtifacts\guideXOSServer_NAVIGATOR_JS_SUPPORT\JS129`
folder contains historical qualification outputs, object files, binaries,
and hosted scripts, but no original JS129 C++ source, focused test source, or
the reported qualification report. These artifacts are preserved as evidence;
they are not treated as interchangeable with source. The old hosted scripts
refer to the unavailable D: checkout and modify a production executable, so
they were not rerun. No original-source equivalence or original-commit identity
is claimed.

The recovery adds a new commit directly on JS128. It does not replace,
recreate, amend, or rewrite the reported JS129 hash.

## Reconstructed behavior

`PortableExecutionCore::run()` now uses bounded iterative capability preflight
when caller-owned execution-frame storage is configured, then runs supported
programs through the resumable execution machine. External
`PortableExecutionCore::invokeCallable()` enters that same machine with a real
function activation. Ordinary and arrow functions retain their argument,
closure, receiver, lexical `this`, return, and error behavior. Nested calls
remain bounded by the existing logical-depth and execution-frame limits.

Capability preflight also uses the caller-owned frame arena. It accounts for
AST records, child-list scans, callback declaration lookup scans, and relevant
parameter/binding checks against one work budget. The configured AST depth is
256 and the preflight work limit is 65,536 units. Preflight storage is cleared
on success and failure. The hosted evaluator remains the fallback for syntax
rejected before portable execution begins; a machine error after successful
preflight is returned directly so completed side effects cannot be replayed.
Native and Host dispatch remains explicitly unsupported in the portable
entry.

The implementation reuses the existing portable `Value`, AST, environment,
function store, and object store. It does not add JSON `toJSON` or reviver
callbacks. The recursive hosted evaluator remains outside the freestanding
stack-flattening claim, and no guest execution or whole-interpreter stack
qualification was performed.

## Limits and memory accounting

The reconstructed source retains the reported operating limits:

| Limit | Value |
|---|---:|
| Logical JavaScript calls | 64 |
| Execution frames | 4,096 × 112 B = 458,752 B |
| Live argument Values | 512 × 16 B = 8,192 B |
| Execution steps | 100,000 |
| AST traversal depth | 256 |
| Preflight work units | 65,536 |
| Invocation call frames | 64 × 80 B = 5,120 B |
| Configured invocation backing | 472,064 B |
| Maximum active preflight path | 257 × 112 B = 28,784 B |

Preflight shares the 4,096-record execution arena; the 28,784-byte figure is
the maximum active path at the configured AST depth, not an additional arena.
The 472,064-byte invocation figure is the sum of the configured call-frame,
execution-frame, and argument-Value backing.

The historic JS129 artifact reported 448 execution frames (50,176 B) at
depth 64, 55,328 B of combined live invocation state, and 83 preflight records
at depth 80. Those remain historical observations. The reconstructed focused
suite observed a 386-frame execution high-water (43,232 B), 83 preflight
records (9,296 B), call depth 64, and two live argument Values. The results
are not presented as measurements of the unavailable JS129 binary.

## Verification of the reconstruction

The post-commit focused suite passes **16/16** checks. It covers ordinary and
arrow callback activation, captured environments, explicit receiver and
lexical `this`, argument binding, object side effects, the 64-call boundary,
failure cleanup and reuse, iterative preflight depth limits, and work-budget
exhaustion during callback declaration lookup.

Regression runs on this implementation passed JS125 **121/121**, JS126
**146/146**, JS127 **17/17**, JS128 **19/19**, portable execution **109/109**,
and the Navigator JavaScript runtime test. The JS128 regression observed a
453-frame execution high-water (50,736 B).

The post-commit AMD64 freestanding qualification passes using MinGW GCC
15.2.0 (`x86_64-w64-mingw32`, C++17, `-ffreestanding`, `-fno-exceptions`, and
`-fno-rtti`). The AMD64 PE/COFF relocatable component and static archive have
zero undefined symbols. The audit found no prohibited hosted/platform
symbols, TLS, or constructor sections. Current `-fstack-usage` measurements
are:

| Entry | Bytes |
|---|---:|
| C ABI callable-entry probe (calling `invokeCallable()`) | 448 |
| `runExecutionMachine` | 448 |
| `preflightAst` | 256 |
| `machineRunExpression` | 464 |

These are static compiler measurements for the reconstructed source. The
historical 512/352/288/576-byte figures are not reused. Static frame reports
do not qualify the complete guest execution stack.

Persistent post-commit evidence is in:

- `E:\CodexArtifacts\guideXOSServer_NAVIGATOR_JS_SUPPORT\JS129\reconstructed-20261126-recovery1\post-commit-focused-hosted`
- `E:\CodexArtifacts\guideXOSServer_NAVIGATOR_JS_SUPPORT\JS129\reconstructed-20261126-recovery1\post-commit-freestanding`

## Qualification boundary

This is a verified reconstruction of the documented JS129 call-entry behavior,
not recovered original source. JS130 portable JSON callback integration has
not started. The JS130 artifact directory contains planning material but no
confirmed completion commit or qualification result. Proceed to JS130 only
from this committed JS129 reconstruction after its requirements are reviewed.
