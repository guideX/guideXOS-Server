# JS125 portable callback foundation

## Outcome

**Outcome B — JS125_PORTABLE_CALLBACK_FOUNDATION_PARTIAL.** JS125 adds and
qualifies bounded invocation of portable user functions and ordered property
snapshots over the shared Value, EnvironmentStore, FunctionStore, and
PortableObjectStore. It does not enable portable JSON callbacks.

The remaining boundary is specific: pre-existing hosted native and host
function records live in RuntimeContext::functions_, while the portable
FunctionStore view begins at a later slot. The portable API recognizes Native
and Host records when supplied through its own store and returns
UnsupportedOperation for their invocation, but it cannot resolve all actual
hosted function handles. Also, the measured depth-64 call path exceeds the
known 16 KiB guest stack even before evaluator caller frames are included. The
component has not been integrated into or run in a guest.

## Starting state and accepted JS124 boundary

The repository started at commit
2e90975e47c5ff9b0db9d361b64cd6e19b3afacc on
NAVIGATOR_JAVASCRIPT_SUPPORT, one commit ahead of upstream. Tracked files were
clean, .phase was absent, and the protected executable matched the recorded
original SHA-256. A saved manifest verified all 42 generated fixtures
(458,372 bytes). A separate copy of the original executable and manifest are
in the external JS125 run directory.

JS124 remains the accepted boundary: callback-free JSON.stringify uses the
shared bounded serializer; hosted toJSON and JS110 reviver behavior continue
through the established hosted evaluator. JS125 does not route either JSON
callback through the portable core.

## Existing callback dependency map

Hosted callback entry points in navigator_javascript/runtime.cpp call the
existing Evaluator::invokeFunction:

- JSON.parse reviver invocation: runtime.cpp near line 2962.
- JSON.stringify toJSON invocation: runtime.cpp near line 3265.
- invokeFunction near line 1482 dispatches user functions, Native functions,
  and Host methods through the existing hosted implementations.

The hosted callback paths first obtain a method value through their established
runtime property logic, decide whether it is callable, then invoke with an
explicit receiver and argument vector. Reviver traversal snapshots keys or
Array length at its defined points and reads values as it advances. Runtime
errors are translated into the hosted ScriptResult; earlier completed side
effects remain visible.

JS125's portable prerequisites use the same bounded Value handles, environment
records, function records, and authoritative object store. The portable core
does not contain a second function dispatcher. Its public invokeCallable(Value
callable, Value receiver, const Value* arguments, size_t argumentCount,
PortableExecutionResult&) resolves a generation-checked record and enters the
existing private invokeFunction path. The input span is borrowed for the
synchronous call. Nested script calls share the same frame limit and step
counter.

The portable syntax preflight still accepts only identifier callees in its
CallExpression path. General object.method() source expressions therefore
remain hosted-only. Future callback walkers can use the explicit callback API:
read a property once, retain its actual value and found status, determine
callability, then invoke with the intended holder/value receiver. No portable
JSON traversal is part of JS125.

| Operation | JS125 classification | Boundary |
| --- | --- | --- |
| Ordinary/arrow function lookup | Portable and qualified | Generation-checked FunctionStore user records |
| Native/Host callable classification | Portable record-kind check | Actual hosted prefix records are not visible through the portable store view |
| Ordinary/arrow invocation | Portable and qualified | Bounded user-function subset; explicit receiver for ordinary functions, lexical this for arrows |
| Native/Host invocation | Explicitly unsupported | Returns UnsupportedOperation; no fake result or second dispatcher |
| Own and inherited property lookup | Portable and qualified | Data properties in PortableObjectStore; prototype depth is bounded |
| Dynamic property read | Portable and qualified | Returns actual Value and found from one store read |
| Accessors and host-object properties | Hosted-only | No portable getter/setter side effects are invented |
| Property create/update/delete | Portable and qualified | Uses the shared store and its configured quotas |
| Own-key snapshot | Portable and qualified | Caller-owned ordered string-key and byte buffers |
| Array length snapshot | Portable and qualified | Caller captures the bounded scalar with arrayLength() |
| Live value read after snapshot | Portable and qualified | Caller reads with getProperty() or indexed Array lookup at processing time |
| JSON callback traversal | Not implemented | JS126 or later; hosted JS109/JS110 paths remain active |

## Callable and invocation contract

PortableExecutionCoreImpl::isCallable returns false for primitive values and
noncallable objects. It recognizes User, Native, and Host records present in
the supplied FunctionStore. Invalid same-generation handles and stale
generation handles produce explicit errors. A recognized Native or Host
record is not invoked by the portable implementation; invokeCallable returns
UnsupportedOperation.

For User records, invocation checks the argument limit, caller-provided frame
storage, call depth, environment capacity, and shared execution-step budget.
The core creates a bounded function environment and binds parameters from the
borrowed input span in order. Missing parameters become undefined. Return
values preserve undefined, null, booleans, numbers, strings, objects, Arrays,
and function handles. A successful undefined return is not deletion; the
caller performs property deletion separately.

Ordinary functions use the receiver supplied to invokeCallable. Arrow
functions use their retained lexical this and ignore the supplied receiver.
Both paths preserve Value identity. Property-derived own and inherited
callback tests retrieve the callable from PortableObjectStore and then invoke
it with the correct holder. Existing hosted holder.callback() behavior was
checked separately against the portable explicit-receiver result.

The supported portable function subset remains deliberately narrow: simple
identifier parameters, bounded function bodies, and expressions accepted by
PortableExecutionCore::supports. Defaults, rest parameters, native/host
dispatch, arbitrary member-call syntax, and general ECMAScript semantics are
not claimed.

## Observable property operations

PortableObjectStore::getProperty is the shared prototype-chain data read. It
returns the actual Value and a separate found flag, distinguishing own or
inherited undefined from a missing property. Invalid objects, invalid
prototypes, stale generations, prototype cycles, and prototype-depth
exhaustion remain explicit errors. The store contains data properties only;
there are no portable accessors or implicit host-object callbacks.

Mutation continues through the same store:

- setOwnProperty creates a property or replaces its Value.
- updateOwnProperty updates the current own record and attributes.
- deleteOwnProperty removes an own property; repeated or missing deletion
  reports no second removal.
- Array indexed set/delete operations preserve sparse holes and release
  capacity. Deletion leaves Array length unchanged; explicit length reduction
  removes elements beyond the new length.

The JS125 snapshot API,
snapshotOwnEnumerableStringProperties, copies ordered own enumerable string
keys into caller-provided entry and byte buffers. It excludes symbols,
non-enumerable properties, Array holes, and the Array length property. Array
indices are emitted as decimal string keys. Numeric index keys sort before
other strings; other strings keep insertion order. Deleting and reinserting a
string key moves it to the insertion-order tail.

Snapshot creation makes a capacity pass and a copy pass. This is safe for the
current store because properties are data-only and both passes are
side-effect-free; it cannot call a getter. A future observable adapter must
not reuse this double-read pattern for accessors or host objects.

Callers snapshot an Array's length with arrayLength() and retain the scalar.
They then read each snapshotted key/index when its turn arrives. Tests mutate a
later value, delete a later key, and add a new key after the key snapshot. The
copied key set stays stable; the live read observes the changed value, reports
the deleted value as missing, and does not add the new key to the snapshot.
An Array hole stays distinct from an own element whose Value is undefined.

## Errors, side effects, and lifetime

PortableExecutionResult carries completion, return Value, source location, and
a typed portable error. Focused checks exercise noncallable TypeError,
missing-binding ReferenceError class, immutable assignment, explicit
unsupported Native/Host capability, invalid/stale handles, environment
capacity, argument overflow, call-depth exhaustion, and step exhaustion. No
portable operation converts these failures into a successful undefined.
Portable invocation uses caller-owned pools and does not allocate heap memory
per call, so AllocationFailure is not produced by the current callback entry
path; the hosted evaluator retains its existing allocation-failure handling.

A callback that writes an object property and then fails on a missing binding
leaves the completed write intact. Invocation frames unwind so a later
supported callback succeeds. There is no whole-script rollback and no
object-arena rewind on callback failure.

Function Values retain their shared generation-checked handle. Closure
environments are retained in EnvironmentStore, so a returned closure still
reads its captured binding after the factory frame has returned and across
repeated calls. A second script in the same realm retains an earlier callback
handle; a full RuntimeContext reset advances the generation and invalidates
that handle. Object, Array, function, and string Values remain handles to the
same bounded stores, not copied identities. Source and AST lifetime remain
owned by the caller/realm and must outlive retained function records.

## Limits, memory, and stack

Portable defaults relevant to JS125 are 64 nested calls, 64 arguments per
call, 100,000 execution steps, 4,096 User function Values, 256 bindings per
function environment, and caller-provided invocation storage. The existing
hosted runtime allocates storage sized from configured maxima; JS125 adds no
per-invocation heap allocation.

AMD64 layouts measured by the qualification build:

| Record | Bytes |
| --- | ---: |
| Value | 16 |
| FunctionRecord | 48 |
| EnvironmentRecord | 24 |
| Binding | 40 |
| PortableCallFrame | 72 |
| PortablePropertyKeySnapshotEntry | 8 |

The runtime's theoretical portable function backing is bounded by the
remaining User, Native, and Host-method quotas, up to 4,224 records (202,752
bytes) before handle-space limits. Sixty-four call frames use 4,608 bytes;
512 argument Values use 8,192 bytes. These are configured backing maxima, not
the focused test's peak use.

The focused fixture actually allocates 18,944 bytes for its 128 FunctionRecord
slots, 64 call frames, and 512 Value argument slots. Its 256 environment
records, 1,024 bindings, 65,536 binding-name bytes, 128 runtime-string records,
and 16,384 runtime-string bytes add 131,072 bytes, for 150,016 bytes across
those function/call/environment/string pools. This excludes object-store
backing, frontend/AST vectors, and test harness objects. A representative
mutation test keeps 352 bytes of snapshot entry/key buffers live at once;
callback invocation itself adds no heap allocation or object/Array backing
copy.

Snapshot entry/key storage is caller-owned and is not allocated by
PortableObjectStore. With default limits, a conservative simultaneous
snapshot maximum across 4,096 ordinary properties and 4,096 Array elements is
8,192 entries (65,536 bytes) and up to 278,528 key bytes, or 344,064 bytes
combined. JS125 tests use small local buffers; the runtime does not reserve
that theoretical maximum. A future walker must size its own arena and include
it in total backing accounting.

JS124 reported 981,248 bytes of stringify backing, including object/Array
storage, runtime strings, output storage, and serializer frames. That number
does not cover callback frames or snapshot arenas and overlaps storage already
accounted in earlier runtime reports; do not sum cross-phase totals as though
they were independent allocations. JS118's configured environment, binding,
name, runtime-string-record, and runtime-string-byte pools totaled 2,080,768
bytes. Returned callback Values reuse these stores and add no object copy.

GCC -fstack-usage reports these target frames:

| Function | Bytes |
| --- | ---: |
| Freestanding callback probe anchor | 2,272 |
| PortableExecutionCoreImpl::invokeFunction | 368 |
| EnvironmentStore::create | 96 |
| EnvironmentStore::declareImpl | 208 |
| PortableObjectStore::getProperty (string key) | 176 |
| PortableObjectStore::setOwnPropertyImpl | 208 |
| PortableObjectStore::deleteOwnPropertyImpl | 176 |
| PortableObjectStore::snapshotOwnEnumerableStringProperties | 240 |
| PortableObjectStore::setArrayElement | 160 |

At depth 64, invokeFunction alone contributes a 23,552-byte repeated-frame
lower bound. Evaluator and environment caller frames add to it. This exceeds
the known approximately 16 KiB guest stack, so guest stack readiness is not
established. JS125 did not change the kernel stack and performed no guest
execution. A dedicated stack-reduction/readiness phase is required before
considering this recursive path for a guest.

## Freestanding qualification

The actual shared lexer, AST, parser, Value, environment, object store, and
JS125 callback probe compiled using GCC 15.2.0 for x86_64-w64-mingw32 as
C++17 AMD64 COFF. The build used -ffreestanding, -fno-exceptions, -fno-rtti,
-nostdlib, -nostdinc++, disabled builtins, TLS/constructor inspection, and
-fstack-usage. The relocatable component hash was
25E3D0F50DDF01F9E61DE946E2C4F441034A01FC75161E065F74A8027A60C672.

The object and static archive each had zero undefined symbols. The COFF
metadata was AMD64; no TLS or global-constructor section was found, and no
standard C++ runtime, Windows process, or exception runtime dependency was
reported. This qualifies only the selected shared component and its dependency
closure. It is not a kernel link, QEMU run, guest callback test, or stack-safety
claim.

## Qualification results

The strict C++17 JS125 focused suite passes **121/121** checks. It covers
callable kinds, invalid/stale handles, direct and property-derived invocation,
ordinary/arrow receivers, ordered arguments, return categories, closures,
property lookup and mutation, snapshots/live reads, Array holes/length,
capacity failures, errors, side-effect retention, same-realm callback
retention, and stale-handle rejection after full realm reset.

The complete JavaScript matrix passes **117/117 lanes**, with
**19,167/19,167 reported assertions across 75 reporting lanes**. It includes
the requested JS94–JS102, JS104/JS105, JS106–JS110, JS121–JS125, and all
other numbered lanes present in the matrix. JS109 and JS110 each pass their
focused suite at **86/86** and **28/28**, and their hosted pages returned HTTP
200 through Navigator with empty JavaScript error fields.

The direct JS115 frontend run passed its hosted checks and AMD64 freestanding
compile/symbol audit. JS116's lexer/AST and shared-parser host executables both
returned zero; the lexer, AST, and parser compiled as AMD64 freestanding COFF,
and the linked frontend had zero undefined symbols. JS118 passed **44/44**
Value/Environment core checks (22 Value and 22 Environment checks) and its
freestanding dependency audit. JS119's direct portable execution suite passes
**109/109**, with its separate freestanding component audit passing. JS120's
AMD64 object/Array compile, link, and dependency audit passes. JS121's hosted
unification suite passes **32/32**. JS122, JS123, JS124, and JS125 pass their
matrix focused suites at **67/67**, **70/70**, **1,179/1,179**, and
**121/121**, respectively.

The strict C++17 production build passed. Its separately saved candidate is
9,375,699 bytes with SHA-256
`98AC12A7A112FAAC6C49520E5745A675EA08104D55E2D071DB793C9961F5A72C`.
The hosted aggregate returned **767/774** checks. Its seven failures are the
established CSS checks in phases 3C, 3G, 6A, three checks in 6B, and 6C; the
aggregate contains no JavaScript failures.

The original **8,398,274-byte** `guideXOSServer.exe` was restored and verified
at SHA-256
`A5E439455E4D3E735DFB848EC296A3EC4B347822B21F5B1B736D51227046A971`. All 42
generated fixtures (458,372 bytes) match the saved manifest, and `.phase`
remains absent. No QEMU, QAC, guest execution, or kernel-format integration
was performed.

## Gate table

| Gate | Current result | Evidence / remaining prerequisite |
| --- | --- | --- |
| C0 — Dependency architecture | Partial | Hosted paths are mapped; actual hosted native/host prefix records are not resolved by the portable store view. |
| C1 — Portable callable invocation | Partial | User ordinary/arrow subset passes; actual hosted Native/Host handles are not fully visible to the portable callable query. |
| C2 — Observable property operations | Pass | Shared object-store own/inherited reads, mutation, deletion, identity, and explicit unsupported accessor boundary. |
| C3 — Snapshot/live-read semantics | Pass | Ordered copied key snapshots, captured Array length, and mutation-order live reads pass. |
| C4 — Side effects and error propagation | Pass | Typed errors propagate; completed mutation survives; frame recovery passes. |
| C5 — Lifetime and resource safety | Partial | Handles, closures, limits, reset pass; depth-64 stack lower bound exceeds 16 KiB. |
| C6 — Freestanding dependency closure | Pass | AMD64 COFF link; zero undefined symbols/imports; no TLS or constructors. |
| C7 — Hosted semantic parity | Pass with baseline | 117/117 matrix lanes; JS109/JS110 hosted pages pass; aggregate is 767/774 with the same seven established CSS failures; strict production build passes. |
| C8 — Optional kernel-format integration | Not run | Optional; no kernel probe, QEMU, QAC, or guest execution. |

## Remaining prerequisites and JS126 scope

JS126 should first provide one shared generation-checked function lookup view
that can see the actual hosted native/host prefix records without duplicating
the evaluator's dispatcher. Native/host invocation should remain an explicit
unsupported result until a shared accepted implementation exists. Then add a
separately qualified JSON callback adapter that performs each effectful
property lookup once, supplies the holder/value receiver and key argument,
uses the existing execution budget, preserves earlier effects on failure,
and selects the hosted path before observable work when portable capability
cannot be established. Portable toJSON and reviver execution remain future
work.

Before any guest integration, reduce or otherwise qualify the recursive stack
path against the fixed guest stack. Do not raise the kernel stack as part of
JS126.
