# Navigator JavaScript JS120 — Portable Objects and Arrays

**Phase:** JS120
**Outcome:** `JS120_PORTABLE_OBJECT_ARRAY_PARTIAL`
**Starting HEAD:** `75759117d491c3169caa5d7da75b1fac762f9b62`
**Branch:** `NAVIGATOR_JAVASCRIPT_SUPPORT`

## Result

JS120 adds bounded caller-backed object and Array storage and enables the
existing portable evaluator to execute selected object and Array expressions.
The focused harness and AMD64 freestanding dependency audit pass. The hosted
runtime and its established compatibility matrix remain unchanged.

This is **Outcome B**, not a verified single-model migration. `RuntimeContext`
continues to own its accepted `std::vector<RuntimeObject>` and per-object
property/element vectors. The new `PortableObjectStore` is exercised by the
portable executor and freestanding probe, but no hosted adapter routes
`RuntimeContext` object operations through it. Thus JS120 does not yet satisfy
the one-authoritative-model requirement. Keeping that boundary avoids changing
hosted property descriptors, built-ins, Array callbacks, JSON, and DOM behavior
without a complete adapter and parity proof.

This report records the JS120 state at its accepted commit. JS121 subsequently
routes the hosted `RuntimeContext` through `PortableObjectStore`; see the
[JS121 shared object model report](NAVIGATOR_JAVASCRIPT_JS121_SHARED_OBJECT_MODEL.md)
for the migrated architecture and its separate qualification results.

The portable evaluator is a specialization of the JS119 evaluator, not a
separate test evaluator. `PortableExecutionCore` keeps object operations
disabled for the existing hosted dispatch. `PortableObjectExecutionCore`
enables object literal, Array literal, member access, and member assignment
using the caller-provided store. The production Navigator build does not
compile or link `object_storage.cpp`.

## Starting architecture and integration boundary

The shared `Value` is a 16-byte tagged value. Object references are
generation/slot handles; copying a `Value` copies the handle and preserves
identity. JS119 already shares `Value`, strings, environments, functions, AST,
and execution logic between the portable slice and `RuntimeContext`.

The hosted object model has accepted behavior that the new store does not yet
provide: native/symbol property handling, built-in prototypes and methods,
Array callbacks and traversal, JSON parse/stringify/reviver, host objects, and
DOM interactions. Its `RuntimeObject` owns vector-backed properties and
sparse elements. The portable store is a second storage backend until a narrow
hosted adapter is implemented. Matching handle types alone does not make the
two stores interchangeable.

## Portable storage

`PortableObjectStore` receives all storage from its caller. It does not allocate
memory. Object, property, element, and name pools have explicit capacities;
all public operations report `PortableObjectErrorCode` and initialize their
outputs on failure.

| Record or pool | Default capacity | AMD64 record size | Maximum backing |
| --- | ---: | ---: | ---: |
| Object/Array records | 1,024 | 28 B | 28,672 B |
| Named properties | 4,096 aggregate; 256 per object | 40 B | 163,840 B |
| Sparse Array elements | 4,096 aggregate; 1,024 per Array | 32 B | 131,072 B |
| Copied property-name arena | 262,144 B; max key 256 B | — | 262,144 B |
| **Caller-backed pool total** |  |  | **585,728 B** |

`PortableObjectStore` metadata is 200 B on the qualified AMD64 target. The
object record holds the prototype handle, property and element list heads,
Array length, counts, and object/Array state. Property and Array-element
records are linked from shared flat pools. Array elements are sorted by index;
holes consume no element record. Property names are copied to a bounded arena,
so they do not retain parser or temporary source-buffer pointers. Deleting a
configurable named property releases its record and compacts the live name
bytes. Repeated assignment updates the existing record and does not duplicate
the name.

Configuration checks capacities, integer-width limits, pointer alignment,
address-range overflow, required pointers, and overlap among backing regions.
The arena and record pools are reset together. Objects are append-only within a
realm, so a same-generation object slot is never reused. Reset requires a
strictly newer nonzero generation and invalidates all previous handles. There
is no object deletion or garbage collection; unreachable objects and failed
partial literals consume bounded records until reset.

## Properties, lookup, and mutation

The store supports own-property existence/read, inherited read, own-property
write/update, configurable deletion, and own-property enumeration. A separate
`found` output distinguishes a missing key from a present key containing
`undefined`. Property values hold scalars or the existing opaque string,
function, host-object, or object handles; copying a reference does not clone
the referenced value.

Property attributes are bounded data-property flags. Existing non-writable
properties are not changed, and non-configurable properties are not deleted.
The `delete` result distinguishes a successful no-op from a removed property.
The portable storage API implements deletion, but JavaScript `delete`
expression syntax is not enabled in `PortableObjectExecutionCore` yet.

Canonical integer-like own keys are enumerated before insertion-ordered string
keys. Array element entries are enumerated in numeric order, and Array
`length` is exposed as a non-enumerable own entry. The implemented
`__proto__` key remains an ordinary own data property in object literals and
assignments; it does not mutate a prototype.

Prototype links use generation-checked handles. Link creation rejects a cycle
and invalid handles, and prototype walks stop at the configured depth (32 by
default). The portable evaluator does not install the hosted `Object.prototype`
or `Array.prototype`, and it does not provide inherited built-in methods.
Prototype lookup in this slice is limited to explicitly connected portable
records.

## Arrays

Arrays share the object pool and object handle namespace, with a record flag
and a logical length. Numeric elements use a shared sparse pool. Limits match
the existing accepted runtime defaults: length at most 1,024, numeric index at
most 1,023, 1,024 element records per Array, and 4,096 element records across
the realm. Canonical numeric index parsing is checked against the accepted
index range; out-of-range numeric writes return an explicit error.

Writing index `2` to an empty Array sets length to three and leaves indices 0
and 1 absent. A hole and an own element whose value is `undefined` remain
distinct. Deleting an element frees its slot but preserves length. Shrinking
length releases elements at or above the new length; expanding length creates
holes without allocating element records. Named non-index properties use the
property pool. `length` is writable, non-enumerable, and non-configurable in
the storage model. No Array methods are added here.

## Portable evaluator coverage and limits

The same AST and evaluator implementation now has an object-enabled
specialization. It supports object/Array literals, named and computed member
reads, named and indexed writes, assignment results, supported primitive
expressions, and the existing JS119 simple functions/closures. The harness
checks objects passed to functions, returned from functions, retained by
closures, and copied by identity. It also checks computed-key and receiver/RHS
evaluation order.

Portable syntax coverage does not include object methods, destructuring,
`delete`, general prototype mutation, host calls, built-in Array operations,
symbol-keyed properties, or JSON. The hosted evaluator remains the source of
truth for these accepted capabilities. Partial literal construction is not
transactional: completed earlier side effects remain, and allocated records
remain valid but may be unreachable after a later capacity error.

## Limits and errors

Default portable object/Array limits are 1,024 objects/Arrays, 4,096 named
properties total, 256 named properties per object, 256 bytes per property
name, 256 KiB for copied property names, 1,024 Array elements per Array,
4,096 Array elements total, maximum Array length 1,024, maximum numeric index
1,023, and prototype depth 32. The JS119 execution limits remain 100,000
steps, 64 active calls, and AST depth 256. These pools are bounded shared
pools; the design does not reserve 256 property records for every object.

Errors distinguish invalid storage/object/Array/property, stale generation,
invalid index/length/prototype, prototype cycle/depth, name-too-long,
object/property/Array capacity exhaustion, name-arena exhaustion, arithmetic
overflow, and internal invariant failure. The portable execution layer maps
these to explicit execution errors; `RuntimeContext` error names include the
new stale-object case. Error mapping is preparatory and does not itself route
hosted object operations through the store.

## Qualification results

### Focused and historical portable checks

- Portable object/Array execution harness: **109/109 checks passed**.
- Observed execution high-water: 5 environments, 9 bindings, 1 string using
  9 bytes, 3 functions, call depth 3, and 3 argument values.
- Observed object/Array high-water during script execution: 2 objects, 2 named
  properties, 11 property-name bytes, and 3 sparse Array elements.
- JS115 frontend qualification: passed.
- JS116 portable parser qualification: passed.
- JS118 storage harness: **44/44 checks passed**.
- JS119 portable execution qualification: passed with the JS120-updated
  304-byte evaluator metadata size.
- Full JavaScript matrix: **112/112 lanes and 17,765/17,765 assertions** passed
  across 71 assertion-reporting lanes.

The object/Array high-water is measured after each program execution in the
portable harness. Direct-store stress checks that mutate the store afterward
are not included in that representative high-water figure.

### Freestanding AMD64 component

The final audit compiles the actual lexer, AST, parser, Value, environment,
`object_storage.cpp`, and a C ABI anchor that instantiates the object-enabled
execution core. It uses MinGW GCC 15.2.0, target `x86_64-w64-mingw32`, C++17,
warnings as errors, exceptions and RTTI disabled, no standard C++ headers or
libraries, and the established freestanding AMD64 flags. `ld -r --no-undefined`
creates an AMD64 PE/COFF component, then `ar` creates an archive.

Both the relocatable object and archive have **no undefined symbols**. The
section audit finds 168,688 B of text, 9,408 B of read-only data, 352 B of
initialized data, 2,208 B of pdata, 2,264 B of xdata, and 0 B of BSS. No TLS,
constructor, standard-library, exception, allocator, Windows import, or
platform dependency was found. Component SHA-256:
`63864E81A114AA2916DA56D0F485C575CB21D0B319CA05FDEC64FAD2D7D8BDDB`.

The anchor is compiled and linked but not executed as a target binary. The
hosted warning-clean C++17 harness executes the selected shared operations.
This closes dependencies for this selected component only, not the complete
hosted runtime or Navigator guest.

### Memory and stack risk

At configured maxima, simultaneous caller-backed portable storage is:

| Storage | Maximum backing |
| --- | ---: |
| JS116 frontend arrays and parser scratch | 3,145,792 B |
| JS118 Value, string, and environment stores | 2,080,768 B |
| JS119 function records, call frames, and arguments | 215,552 B |
| Source storage | 1,048,576 B |
| JS120 object, property, name, and Array pools | 585,728 B |
| **Combined caller-backed maximum** | **7,076,416 B** |

This excludes small store/core metadata objects, hosted vectors and allocator
overhead, the rest of the process, and application/runtime state. The 243-byte
observed object-pool use (two object records, two properties, three element
records, and 11 copied name bytes) is representative harness use, not a
production workload estimate. All portable pools coexist for the life of the
realm; they cannot be overlaid while the realm is executing.

GCC `-O2 -fstack-usage` reports 688 B for parser entry `parseInto` and 1,328 B
for recursive `parseAssignmentExpression`. In the object-enabled evaluator it
reports 192 B for `supportsExpression`, 176 B for `supportsStatement`, 352 B
for `evalExpression`, 208 B for `executeStatement`, 208 B for
`evalPropertyReference`, 368 B for `evalAssignment`, 256 B for
`evalObjectLiteral`, 208 B for `evalArrayLiteral`, and 384 B for
`invokeFunction`. Store frames include 176 B for inherited `getProperty`,
192 B for `setOwnProperty`, and 160 B for `setArrayElement`.

AST traversal is bounded at depth 256 and function calls at depth 64, but
recursive evaluator and parser frames can multiply. Sixty-four simultaneously
active 384-byte `invokeFunction` frames alone account for 24,576 B before
their evaluator callers and temporaries. Parser and execution occur in
separate phases, but no aggregate maximum or runtime high-water was measured.
The prior JS116 parser analysis estimates about 96 KiB of parser stack. These
figures rule out a 16 KiB boot-stack safety claim; this phase does not change
the kernel stack or establish guest stack sufficiency.

## Hosted parity and protected artifacts

**Final matrix status:** 112/112 lanes and 17,765/17,765 assertions passed
across 71 assertion-reporting lanes, including the JS94–JS110 object/Array,
function, DOM, and JSON regressions. The strict C++17 production build passed.
The separately saved candidate is 9,262,740 B with SHA-256
`B118DB238B6183B09D387BC707D7F3AEA116E09BCD92DA81436AE8C60C5665D0`.
Dedicated JS109 and JS110 hosted page checks passed on that candidate.

The full hosted Navigator smoke completed 767/774 checks and returned failure
for the same seven established CSS checks: phases 3C, 3G, 6A, three checks in
6B, and 6C. This matches the pre-JS120 baseline; no JavaScript check failed.
The protected executable was restored at 8,398,274 B with SHA-256
`A5E439455E4D3E735DFB848EC296A3EC4B347822B21F5B1B736D51227046A971`. All 42
generated fixtures match the preflight manifest (458,372 B total), and `.phase`
remained absent. Qualification did not run QEMU, QAC, or a guest.

The original Windows executable is required to remain at 8,398,274 bytes with
SHA-256 `A5E439455E4D3E735DFB848EC296A3EC4B347822B21F5B1B736D51227046A971`.
The JS118 preflight manifest covers all 42 generated fixtures (458,372 B).
`.phase` must remain absent. JS120 does not run QEMU, request QAC admission,
launch a guest, or perform optional O7 kernel-format integration. No MbedTLS
configuration changes are made.

## Completion gates

| Gate | Result | Evidence |
| --- | --- | --- |
| O0 — architecture verified | **Partial** | Existing hosted vector model documented; no single authoritative store yet. |
| O1 — portable object storage | **Partial** | Bounded identity/property APIs pass the focused harness; hosted adapter is absent. |
| O2 — portable Array storage | **Partial** | Sparse bounded slots and length behavior pass; hosted adapter is absent. |
| O3 — portable execution integration | **Pass for selected subset** | Object-enabled JS119 evaluator uses the portable store. |
| O4 — freestanding compilation | **Pass** | Actual shared source compiles as AMD64 PE/COFF. |
| O5 — dependency closure | **Pass** | Relocatable object and archive have no undefined symbols or prohibited imports. |
| O6 — hosted semantic parity | **Partial** | Production build and JS109/JS110 pages pass; the aggregate matches its existing 767/774 baseline with seven CSS failures and no JavaScript failures. |
| O7 — optional kernel format | **Not run** | Optional; no kernel format claim. |

## Remaining work and JS121

JS121 should build a narrow `RuntimeContext` adapter over one bounded object
store, retaining existing descriptors, symbol keys, Array methods/callbacks,
JSON traversal, host-object interactions, and DOM semantics. It must reconcile
object generations and identity across Value namespaces and preserve hosted
error behavior. Add hosted/portable interoperability tests for object identity,
Array mutation/enumeration, and retained closures before removing the legacy
vector model. Qualify aggregate stack and memory after that adapter exists;
then bring the JSON pipeline into the portable dependency closure.

**Current status:** JS121 completed this hosted adapter and removed the second
authoritative ordinary-object/Array store. See the
[JS121 shared object model report](NAVIGATOR_JAVASCRIPT_JS121_SHARED_OBJECT_MODEL.md)
for the verified unified result; this document remains the historical JS120
qualification record.
