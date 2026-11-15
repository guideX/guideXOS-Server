# Navigator JavaScript JS122 — Shared Portable JSON Parser

**Target:** `JS122_PORTABLE_JSON_PARTIAL`
**Starting HEAD:** `1922828e4d6efc1ba69ce281b90fad490ec6bbb9`
**Branch:** `NAVIGATOR_JAVASCRIPT_SUPPORT`

## Outcome

JS122 moves the accepted strict `JSON.parse` grammar and construction path into
one header-included implementation shared by hosted Navigator and the
freestanding runtime. Parsed strings use `RuntimeStringStore`; parsed ordinary
objects and Arrays are created directly in the authoritative
`PortableObjectStore` established in JS121. The parser has caller-backed
scratch, explicit limits and error categories, and rolls object/string storage
back to its entry marks on parse failure.

The result is **Outcome B — `JS122_PORTABLE_JSON_PARTIAL`**. Hosted Navigator
supplies its existing binary64 number converter through an adapter. The
freestanding core has no numeric converter yet and returns
`NumericConversionUnavailable` for a numeric token when none is supplied.
Thus the shared strict parser is dependency-closed and useful for nonnumeric
JSON, but complete portable `JSON.parse` is not qualified. `JSON.stringify`,
`toJSON`, and the reviver traversal remain hosted evaluator operations; this
phase does not claim their portability.

## Starting state and JS121 checkpoint

The repository began at the accepted JS121 HEAD on branch
`NAVIGATOR_JAVASCRIPT_SUPPORT`, already one local commit ahead of upstream.
The protected `guideXOSServer.exe` matched the required 8,398,274-byte SHA-256
`A5E439455E4D3E735DFB848EC296A3EC4B347822B21F5B1B736D51227046A971`. All 42
generated fixtures (458,372 bytes) matched the saved JS120 manifest; `.phase`
was absent. Preflight evidence and the executable backup are stored outside
the repository under `E:\CodexArtifacts\guideXOSServer_NAVIGATOR_JS_SUPPORT\JS122`.

JS121 made `PortableObjectStore` authoritative for hosted and portable ordinary
objects and Arrays. Its hosted sidecar retains only iterator cursor state. JS122
therefore constructs parsed values in the same store rather than parsing into
a temporary object representation and copying afterward.

## Dependency audit and shared boundary

Before JS122, `RuntimeContext::JSON.parse` contained strict JSON tokenization,
UTF-8 and escape decoding, number conversion, and object/Array construction in
`runtime.cpp`. It was surrounded by hosted `std::vector` scratch, JavaScript
error mapping, execution-budget accounting, and the hosted reviver traversal.
`JSON.stringify` is also in `runtime.cpp`; it uses hosted strings/vectors,
property enumeration, number formatting, function invocation, and an active
object stack for cycle detection. `toJSON` dispatches through the hosted
Evaluator. The iterative reviver traversal uses hosted vectors and callback
invocation, and supports deletion by returning `undefined`.

`json_parser.h` declares the portable interface and includes
`json_parser.inl`, which owns the strict parser algorithm. Its only storage
dependencies are `Value`, `RuntimeStringStore`, and `PortableObjectStore`.
Input and decoded text are non-owning `SourceView`s; caller-provided decoded
string and per-depth property-key buffers supply all parser scratch. A step
consumer is optional. Number conversion is an explicit callback boundary.
Hosted `runtime.cpp` supplies `parseHostedJsonBinary64` from
`json_number_host.h`, preserving the previous `std::from_chars` and
overflow/underflow fallback behavior. The portable probe supplies no callback,
so a numeric token fails explicitly instead of silently using a different
algorithm.

The parser shares `decodeJsonUtf8Scalar` with hosted stringify's string
escaping path. Stringify's serializer, number formatter, `toJSON` lookup and
call, and reviver algorithm were not duplicated or ported in this milestone.

## Strict parsing and Unicode

The shared parser recognizes JSON whitespace, primitives, quoted strings,
objects, Arrays, strict number grammar, nested containers, and end-of-input.
It rejects trailing text/commas, comments, malformed numbers and escapes,
unterminated strings, unescaped control bytes, malformed UTF-8, lone UTF-16
surrogates, and invalid surrogate pairs. A valid escaped surrogate pair is
combined into UTF-8. Empty strings and embedded zero bytes retain explicit
lengths; parsed string ownership is independent of the input buffer.

Hosted numeric semantics remain the prior binary64 behavior: negative zero,
subnormals, overflow to signed infinity, and underflow to signed zero are
preserved. Freestanding nonnumeric JSON is compiled and linked, but any number
requires a portable converter before the parser can produce a number value.
This remains the principal J1/J5 capability boundary.

## Shared objects, Arrays, ordering, and special keys

Objects and Arrays are allocated directly through the caller's
`PortableObjectStore`. Parsed values carry ordinary generation-checked object
handles. Parsed Array elements occupy the shared indexed-element pool and set
the final Array length. Property creation uses the ordinary hosted data
attribute bits when called from Navigator. Duplicate keys update the original
property entry. Numeric-looking own keys enumerate before other strings using
the store's existing ordering. `__proto__` is stored as an own data property;
it does not replace the object's prototype. Object/Array prototypes are
provided by the hosted adapter when built-ins are initialized.

## Bounds, storage, and failure behavior

The portable defaults are: 64 KiB input; 64 KiB decoded string length; 4,096
runtime strings; 256 KiB total string bytes; nesting depth 32; Array length and
per-Array element cap 1,024; total Array elements 4,096; and 100,000 parser
steps. Property-name length and aggregate storage are additionally limited by
the configured object/string stores. The hosted adapter narrows these bounds
to `RuntimeContext` limits and accounts parser steps against the execution
budget.

With the existing default stores, caller-backed object and Array pools use
585,728 B, while string records and their byte arena use 327,680 B. Parser
scratch adds up to 65,536 B for decoded text and 8,192 B for 32 property-key
buffers of 256 B each. That is 987,136 B of object/string backing and parser
scratch, before store metadata, the input buffer, and the call stack. Hosted
Navigator allocates its bounded scratch with vectors; the portable caller
provides the buffers explicitly. Parsed strings and copied property names do
not retain pointers into the input or scratch buffers.

Parser scratch is caller-owned: one decoded-string buffer of the configured
maximum string length and one key buffer per nesting level, sized using the
object store's maximum property-name length. The core itself performs no
dynamic allocation. Parse failure restores both stores to their captured
entry marks and leaves the result undefined; hosted integration also restores
its iterator sidecars and string-byte accounting if later root-holder setup
fails. The reviver may intentionally mutate values after a successful parse;
callback side effects are not rolled back if a callback or later traversal
step fails, matching the hosted traversal's existing lifecycle.

The freestanding compiler reports `takeByte` at 48 B, `readJsonString` at
96 B, recursive `parseValue` at 224 B, and the anchor probe at 1,072 B.
`parseValue` is recursive and the default JSON nesting cap is 32, for 7,168 B
of parser recursion frames alone. These are static frame measurements, not an
aggregate call-chain high-water mark and not a boot-stack safety proof. The
approximate 16 KiB boot stack remains outside JS122 qualification.

## Serialization and callbacks

`JSON.stringify` continues to use the accepted hosted serializer against the
unified `PortableObjectStore`. Hosted regressions cover the established
property order, string escaping, numbers, sparse Arrays, omission/null rules,
shared references, and cycle rejection. The serializer tracks only active
object handles, so repeated noncyclic references remain serializable. Its
output builder and traversal/key snapshots allocate hosted C++ storage.

`toJSON` remains hosted-only and invokes function values through the hosted
Evaluator. `JSON.parse` reviver remains hosted-only: its iterative
post-order traversal snapshots object keys, visits Array indices, calls the
reviver with holder identity and key/value arguments, and deletes object
properties or Array elements when the callback returns `undefined`. No portable
function-dispatch adapter or portable deletion callback loop was added.

## Freestanding qualification

The qualification script compiles the actual `json_parser.h` inclusion path
and `json_parser.inl` implementation for AMD64 PE/COFF using GCC 15.2.0,
`x86_64-w64-mingw32`, C++17, `-ffreestanding -fno-exceptions -fno-rtti
-nostdlib -nostdinc++`. The qualification probe uses caller-owned storage and
does not supply the hosted number-conversion callback. Its relocatable
dependency-closed component is 56,426 bytes (SHA-256
`04FFB1DF6E651FF57E898D17440FFA13F4ADCC8B4A3F6C04BFAD680BC90A8E61`), with
zero undefined symbols, no TLS, no constructors, no dynamic allocation,
exceptions, or RTTI. This qualifies only the parser component and its probe;
it does not qualify portable stringify, callback dispatch, kernel linkage, or
guest execution. The audited translation units are `value.cpp`,
`object_storage.cpp`, and `navigator_javascript_js122_json_anchor.cpp`; the
parser implementation is included from `json_parser.inl`. The linked object
has `.text` 30,976 B (64-byte alignment), `.data` 224 B (32-byte alignment),
`.rdata` 1,168 B (16-byte alignment), `.pdata` 960 B and `.xdata` 836 B (each
4-byte alignment), and `.bss` 0 B. Additional read-only/refptr records account
for the remaining component bytes. Optional JS122 kernel-format integration
was omitted because the dependency boundary is incomplete and the mission
prohibits treating an incomplete diagnostic image as a production kernel.

## Qualification and gate status

The focused JS122 suite passes 67/67 checks. It covers strict syntax, hosted
binary64 edge cases, Unicode and embedded zero handling, shared object/Array
construction, duplicate and special keys, property ordering, storage rollback,
capacity/step failures, and the missing portable numeric converter. JS109
passes 86/86 native checks, JS110 passes 28/28, and JS121's integration suite
passes 32/32.

The full JavaScript matrix passes 114/114 lanes and reports 17,797/17,797
assertions across 72 assertion-reporting lanes. JS122's 67 checks are emitted
in their own lane format and are reported separately. Additional portable
qualification passes for the shared frontend, JS118 values/environments
(44/44), JS116 parser, JS118 freestanding core, JS119 execution, and JS120
object/Array component. The JS120 qualification used a new JS122 evidence
directory after its default path correctly refused to overwrite the existing
JS120 result.

The strict C++17 production build passes. The saved hosted candidate is
9,285,755 B with SHA-256
`CD6D985522E52E7C86DF0BE7F8C5B85377C24D5689C5D1E488366260A76F634B`.
Dedicated JS109 and JS110 Navigator page checks both pass on that exact
candidate: HTTP 200, requested URL retained, and no JavaScript error. The
hosted aggregate records **767/774** checks. Its seven failures are the
established CSS checks in phases 3C, 3G, 6A, three checks in 6B, and 6C; no
JavaScript check failed.

After hosted qualification, the original executable was restored and verified
at 8,398,274 B and SHA-256
`A5E439455E4D3E735DFB848EC296A3EC4B347822B21F5B1B736D51227046A971`. All 42
generated fixtures (458,372 B) match the preflight manifest, and `.phase`
remains absent. The candidate and logs remain outside Git under
`E:\CodexArtifacts\guideXOSServer_NAVIGATOR_JS_SUPPORT\JS122\run-20261115-final`.

| Gate | Result | Evidence |
| --- | --- | --- |
| J0 — architecture reconciled | PASS | This report documents the hosted JSON implementation and JS121 storage boundary. |
| J1 — portable strict `JSON.parse` | PARTIAL | Shared strict grammar/storage qualify; portable numeric conversion is unavailable. |
| J2 — portable `JSON.stringify` | NOT MET | Serializer remains hosted-only. |
| J3 — portable callbacks | NOT MET | `toJSON` and reviver invocation remain hosted-only. |
| J4 — bounds and failure safety | PASS for parser | Explicit limits/errors and store rollback are covered; callback mutation remains hosted semantics. |
| J5 — freestanding dependency closure | PASS for parser subset | AMD64 COFF relocatable component has zero unresolved symbols; no numeric converter is included. |
| J6 — hosted semantic parity | PASS | Full matrix 114/114; JS109/JS110 pages pass; aggregate retains the established seven CSS-only failures. |
| J7 — optional kernel format | OMITTED | No kernel integration or guest execution is claimed. |

The hosted aggregate exits nonzero because its seven established CSS checks
remain outside this JavaScript change. QEMU, QAC, guest execution, and optional
kernel-format integration were not run.

## Remaining boundary and recommended JS123 scope

The narrow next step is a freestanding binary64 JSON number converter with
documented overflow, underflow, signed-zero, and subnormal behavior, tested
against the hosted adapter before widening the portable parse claim. JS123 can
then determine a shared serializer boundary, beginning with bounded output
storage, number formatting, string escaping, enumeration snapshots, and active
object cycle tracking. Portable `toJSON` and reviver callbacks should follow
only after shared function invocation, callback mutation, deletion, and
side-effect-on-error semantics are available in the supported runtime slice.

Full details of the shared object model are in the
[JS121 report](NAVIGATOR_JAVASCRIPT_JS121_SHARED_OBJECT_MODEL.md). The main
Navigator JavaScript development notes contain the phase summary and links to
this report.
