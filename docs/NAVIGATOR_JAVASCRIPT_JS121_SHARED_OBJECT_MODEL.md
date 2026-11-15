# Navigator JavaScript JS121 — Shared Hosted Object and Array Model

**Target:** `JS121_SHARED_OBJECT_MODEL_UNIFIED`
**Starting HEAD:** `b05a673c72b901d0c5b50f2a87c609c449159856`
**Branch:** `NAVIGATOR_JAVASCRIPT_SUPPORT`

## Outcome

JS121 routes the hosted `RuntimeContext` object and Array state through the
same `PortableObjectStore` used by the JS120 portable evaluator. The store is
the only authoritative owner of ordinary object properties, prototypes,
Array lengths, and sparse indexed elements. `RuntimeContext` retains a small
sidecar for the hosted Array iterator's cursor state.

The JS120 report remains historically accurate: JS120 itself ended with
`JS120_PORTABLE_OBJECT_ARRAY_PARTIAL`. JS121 closes that specific hosted
integration gap. It does not add language features or claim guest execution.

## Starting models and compatibility matrix

Before removing the hosted representation, the two models were compared at
their behavioral boundaries:

| Concern | JS120 portable store | Hosted `RuntimeContext` before JS121 | JS121 integration |
| --- | --- | --- | --- |
| Object identity | Generation/slot `RuntimeObjectId` handles | The same handle type indexed a vector of `RuntimeObject` records | Both paths resolve the same store record; copying `Value` copies only a handle |
| Object/Array record | Caller-backed fixed record pool | Per-object record included prototype, Array kind/length, properties, and elements | `PortableObjectRecord` is authoritative; hosted sidecar contains iterator cursor fields only |
| Named properties | Flat linked property pool and copied name arena | Per-object `std::vector<RuntimeProperty>` containing owned `std::string` keys | Hosted reads, writes, updates, deletion, descriptor reads, and enumeration use store operations |
| Symbols and attributes | Symbol identity and bounded attribute bits in property records | Typed symbols and partially known six-bit descriptor state | Store lookup/update accepts `WellKnownSymbol`; hosted six-bit attributes are retained |
| Array slots | Sorted sparse element pool; holes use no slot | Per-Array vector of sparse index/value records | Creation, indexed access, deletion, length, traversal, and methods use shared records |
| Enumeration | Numeric keys first, then insertion-ordered strings; symbols remain distinct | Hosted evaluator sorted numeric keys and preserved remaining insertion order | Existing ordering is retained through store enumeration and temporary snapshots |
| Prototypes | Generation-checked links, cycle rejection, bounded traversal | Hosted prototype chains included built-ins and inherited numeric values | Built-in prototype links live in the store; hosted lookup retains cycle/depth errors |
| Functions and callbacks | Values can carry function and object handles | Native/user function dispatch and closures were hosted | Invocation remains hosted; object and Array Values resolve through shared records |
| Host objects and DOM | No full DOM representation | Host references use a separate registry and DOM adapter | Host identity remains in the existing registry; ordinary JS state is not mirrored there |
| JSON | Not implemented by the portable object evaluator | Hosted parser/stringifier/reviver used `RuntimeContext` properties and Arrays | Existing hosted JSON operations read and mutate the shared records |
| Limits and failures | Explicit errors and caller-provided pool capacities | `RuntimeErrorCode` and hosted allocation/failure paths | Store errors map to existing hosted errors; partial script effects follow the previous runtime contract |
| Reset/lifetime | New generation invalidates old handles; no GC | Realm reset cleared object vectors and invalidated the same handles | Store and iterator sidecars reset together; no object reuse or general GC is introduced |

The hosted model had behaviors missing from the portable evaluator itself:
native methods and callbacks, typed symbols, descriptor metadata, inherited
built-ins and numeric properties, JSON parse/stringify/reviver traversal, and
Navigator host-object calls. These are not duplicated in a second store. The
hosted evaluator remains responsible for dispatch and semantics while its
ordinary object/Array data operations call the shared store.

## Architecture and ownership

`RuntimeContext::initializePortableStorage` allocates bounded backing vectors
and configures `PortableObjectStore` with views into them. Store object slots
remain aligned with the generation/slot handles used by `Value`. Ordinary
object allocation and every property or Array mutation update those records
directly; there is no copy-back or periodic synchronization step.

The `RuntimeObject` sidecar is retained only for Array iterator state
(`arrayIterator`, source, mode, index, and exhaustion). It is 16 bytes on the
qualified AMD64 hosted build and is reserved once per possible object. It does
not own a property vector, element vector, prototype, Array flag, or Array
length. `RuntimeProperty` and `RuntimeArrayElement` remain transient adapter
types used for constructor input or short-lived enumeration snapshots; they
are not persistent stores.

Object and Array handles continue to be copied in `Value`, preserving identity
through variables, properties, Arrays, function arguments and returns, and
closures. Reset advances the runtime generation and resets the object store
and iterator sidecars; a prior handle cannot resolve to a new object.

## Hosted operations moved to the store

- Object literals, JSON-created objects, Array literals, native result Arrays,
  iterator-result objects, and host-created Event records allocate shared
  object slots.
- Own and inherited property reads, writes, host-owned internal updates,
  deletion, own-property checks, descriptors, and key snapshots use the store.
- Array length and indexed element reads/writes/deletion use the Array record
  and sparse element pool. A missing slot remains distinct from an own slot
  whose `Value` is `undefined`.
- Array callbacks and methods, `for...of`, `Object.keys`, JSON traversal, and
  reviver traversal consume the shared state. Short-lived key/element vectors
  are staging buffers only and are not authoritative object state.
- Event metadata remains read-only to script. The host's dispatch-phase update
  uses the store's internal update operation, which intentionally updates a
  host-owned value even when the script-facing descriptor is non-writable.
- DOM objects and methods retain their host-managed identity and external
  lifetime. They are not copied into ordinary object records.

The source no longer contains `RuntimeContext::objects_` or persistent
`RuntimeObject::properties`, `elements`, `prototype`, `array`, or
`arrayLength` fields. `build.bat` and hosted smoke link lists now include
`navigator_javascript/object_storage.cpp`, so the production runtime links the
implementation it consumes.

## Semantics and failure behavior

Property reads retain the separate `found` result, so missing properties do
not collapse into own `undefined` properties. The shared store enumerates
canonical numeric keys in numeric order, then ordinary strings in insertion
order; deleting and recreating a string property moves it to the tail. Typed
symbols remain outside `Object.keys`.

Prototype lookup still follows the store links and returns own properties
before inherited ones. Invalid links, cycles, and excessive depth continue to
produce the established errors. Array traversal still observes inherited
numeric values and callback mutation according to the hosted method
implementation.

Hosted errors map from `PortableObjectErrorCode` to existing
`RuntimeErrorCode` values. Object, property, key-byte, and sparse-slot limits
remain bounded at the established defaults. Supported deletion releases its
property or element slot; Array element deletion does not shrink `length`.
Array length expansion creates holes, while shrinking releases slots beyond
the new length.

Object/Array construction cleanup uses a tail rollback for unpublished slots
when a multi-step allocation fails. A failed script is not a transaction:
already completed JavaScript side effects remain visible. JSON reviver
processing retains its accepted post-order behavior; returning `undefined`
deletes a property or Array element through the shared store, and earlier
callback side effects are not rolled back if a later callback fails.

## JSON, functions, closures, and host boundaries

The hosted JSON implementation is unchanged. Its parser creates ordinary
objects and Arrays through the same allocation path as script literals; the
stringifier, cycle detector, `toJSON` calls, and reviver read properties and
elements through the store. Reviver object-property and Array-element deletion
releases the corresponding shared slot while preserving Array length and the
existing holder identity.

Function dispatch, lexical environments, ordinary receiver rules, and arrow
`this` handling remain in the hosted evaluator. Function Values and closure
bindings continue storing generation-checked references; captured and returned
objects therefore keep the same identity without a new object representation.

Navigator `document`, dynamic elements, event callbacks, and other host
objects remain behind `NavigatorScriptHostAdapter` and its host-object
registry. Their external state and lifecycle do not become JavaScript object
properties. Event wrapper properties that are ordinary runtime data are stored
in `PortableObjectStore`.

## Storage and memory accounting

The shared backing pools use the JS120 defaults:

| Pool | Capacity | AMD64 record/entry size | Maximum backing |
| --- | ---: | ---: | ---: |
| Object and Array records | 1,024 | 28 B | 28,672 B |
| Named property records | 4,096 aggregate | 40 B | 163,840 B |
| Sparse Array element records | 4,096 aggregate | 32 B | 131,072 B |
| Property-name arena | 262,144 B | — | 262,144 B |
| **Shared object-store backing** |  |  | **585,728 B** |
| Hosted iterator sidecars | 1,024 | 16 B | 16,384 B |
| **Object store plus hosted sidecars** |  |  | **602,112 B** |

Hosted initialization reserves the fixed pools: the focused test observed 4
initialized objects and 27 built-in properties. Its representative hosted
scripts reached 16 objects, 37 properties, 200 property-name bytes, and 21
sparse Array elements. These are harness high-water figures, not a Navigator
workload estimate.

The JS120 combined caller-backed baseline for JS116 frontend/parser arrays,
JS118 values/strings/environments, source bytes, JS119 execution records, and
JS120 object/Array pools is 7,076,416 B. JS121 adds the 16,384 B iterator
sidecar pool, for a corresponding 7,092,800 B combined baseline. This excludes
store/core metadata, allocator overhead, hosted DOM and application state,
other hosted vectors, process memory, and stack. The prior per-object property
and element vectors and 88-byte hosted object records are eliminated as
persistent authoritative object/Array storage. JS121 does not add garbage
collection or reclaim unreachable objects.

## Freestanding and stack boundary

JS118, JS119, and JS120 qualification scripts compile and link their actual
freestanding AMD64 components with GCC 15.2.0 and audit dependencies. JS120's
component includes `object_storage.cpp`; it has no undefined references in the
qualified component. The hosted adapter remains hosted-only. These results do
not establish full Navigator kernel integration or guest compatibility.

The JS90 strict stack-usage lane measured changed representative frames,
including `PortableObjectStore::createArray` at 80 B,
`RuntimeContext::createArray` at 176 B, `RuntimeContext::readArrayIndex` at
288 B, `RuntimeContext::setArrayLength` at 112 B, and
`Evaluator::createObjectKeys` at 560 B. These are compiler-reported static
frames, not aggregate high-water measurements. The existing approximate
96 KiB parser-stack estimate and approximately 16 KiB guest boot stack remain
unchanged. No JS121 path runs in boot code, and no QEMU or QAC admission was
performed.

## Qualification

The JS121 hosted suite exercises actual `RuntimeContext` operations and reads
the same records directly through its `PortableObjectStore`. It covers shared
identity, properties, symbols, enumeration order, sparse Arrays and explicit
`undefined`, prototypes, functions/closures, callbacks, JSON parse/stringify,
reviver deletion, capacity exhaustion/recovery, and reset invalidation.

| Qualification | Result |
| --- | --- |
| JS121 hosted shared-store suite | 32/32 |
| JS118 AMD64 freestanding runtime core | PASS |
| JS119 AMD64 portable execution | PASS |
| JS120 AMD64 portable object/Array component | PASS |
| Full JavaScript matrix | 113/113 lanes; 17,797/17,797 assertions across 72 reporting lanes |
| JS109 and JS110 hosted Navigator pages | PASS on the production candidate; HTTP 200 and no JavaScript errors |
| Hosted Navigator aggregate | 767/774; matches the established seven CSS-only failures, with no JavaScript failures |
| Strict C++17 and production hosted build | PASS; candidate 9,296,043 B, SHA-256 `ACA9FE50A084FC0734557E8373C6883FF3EE2F31B12AE506EB26983BF88F47A0` |
| Protected executable restore | PASS; 8,398,274 B, SHA-256 `A5E439455E4D3E735DFB848EC296A3EC4B347822B21F5B1B736D51227046A971` |
| Generated fixture manifest | PASS; all 42 files and 458,372 B match the JS120 baseline |
| `.phase` marker | Absent |

The hosted aggregate process exits nonzero because its seven established CSS
checks remain outside this JavaScript change. The final candidate run recorded
failures only in phases 3C, 3G, 6A, three checks in 6B, and 6C. An initial run
also showed one transient JS11 click-check failure; a diagnostic rerun and the
final candidate run both passed every JavaScript check, including JS11.

## Remaining boundaries and JS122

There is no remaining second authoritative store for ordinary JavaScript
objects or Arrays in `RuntimeContext`. Remaining hosted-only pieces are the
small iterator-state sidecar, transient enumeration/constructor buffers,
function dispatch, environment storage, host-object registry, and DOM services.
The sidecar has no ordinary property or Array payload.

Recommended JS122 scope: port the accepted JSON pipeline only after defining
caller-backed reviver/cycle-detection scratch requirements and testing the
same key ordering, callback holder identity, and failure semantics against
this unified store. Keep full DOM portability, guest boot, garbage collection,
new syntax, and unrelated allocator changes out of that scope.
