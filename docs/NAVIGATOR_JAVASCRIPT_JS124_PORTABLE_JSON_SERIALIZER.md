# JS124 shared portable JSON serializer

**Outcome A — `JS124_PORTABLE_JSON_SERIALIZER_VERIFIED`**

The callback-free `JSON.stringify` subset now uses one bounded serializer in
hosted and freestanding source. Hosted `toJSON` and reviver behavior remain in
the qualified hosted adapter. No guest execution is claimed.

## Starting checkpoint

Work began on `NAVIGATOR_JAVASCRIPT_SUPPORT` at
`6628332857aa52b2ab8107bb5163973ce6be75e9`. The upstream branch matched the
starting commit. The tracked worktree was clean. The protected
`guideXOSServer.exe` was 8,398,274 bytes, SHA-256
`A5E439455E4D3E735DFB848EC296A3EC4B347822B21F5B1B736D51227046A971`. All 42
generated Navigator fixtures (458,372 bytes) matched their saved baseline;
`.phase` was absent.

JS123 supplied the shared `ffc.h` decimal parser and a bounded output writer.
It did not yet provide portable Number formatting or Value/object/Array
traversal. Its documented baseline was JS123 70/70, JS122 67/67, JS121 32/32,
JS109 86/86, JS110 28/28, matrix 115/115, and hosted aggregate 767/774 with
seven established CSS failures.

## Shared serializer

`serializePortableJson` in `json_serializer.h`/`.inl` is the one callback-free
serializer used by hosted Navigator and available to freestanding callers. It
consumes the existing `Value`, authoritative `PortableObjectStore`, and
`RuntimeStringStore`, and writes through JS123's `PortableJsonOutputWriter`.
Callers provide output, traversal frames, and Number scratch; the core makes no
dynamic allocation and performs no recursive container traversal.

The hosted adapter prepares a bounded output vector and 32 traversal frames,
then invokes the same core. A small adapter predicate routes known hosted
special objects and iterators to the prior hosted implementation. Callback-free
ordinary values use the shared core. If an inherited or own callable `toJSON`
is encountered, the core returns `UnsupportedCallback`; the adapter discards
the incomplete pass, restores its consumed execution budget, releases scratch,
and invokes the existing callback-capable implementation exactly once. The
portable core never invokes callbacks and never reports partial output as a
successful result.

## Number formatting and provenance

`formatPortableJsonBinary64` uses the proven Ryu binary64 `d2s` shortest
round-trip algorithm, pinned to upstream `ulfjack/ryu` tag `v2.0`, commit
`72e18e8d14d4392a4a3261f37cde248e4fbedbec`. The source is licensed under Boost
Software License 1.0; the license, pin, adaptations, and upstream/local hashes
are recorded in [Ryu provenance](../third_party/ryu/PROVENANCE.md).

Ryu's shortest digits pass through a shared ECMAScript presentation layer:
fixed notation is used from `1e-6` through values below `1e21`, otherwise
scientific notation uses lowercase `e`, a plus sign for positive exponents,
and no exponent leading zeroes. Both zero signs serialize as `0`. Non-finite
Numbers are reported to the serializer and become JSON `null`. The formatter
uses caller-owned scratch and no locale, printf, heap allocation, or C++
runtime.

The expected output table is generated independently by Node.js v24.22.1
`JSON.stringify`, then checked into
`tests/navigator_javascript_js124_number_expected.h`; Navigator does not
generate its own expected values. It contains 555 unique bit-pattern fixtures,
including deterministic random values and boundary-neighbor values around
zero, subnormals, the normal boundary, notation thresholds, powers of ten,
integer precision boundaries, and maximum finite Numbers. Each finite fixture
also round-trips through the shared `ffc.h` parser and compares exact bits.

Representative exact outputs include `0 -> 0`, `-0 -> 0`, `1e-6 -> 0.000001`,
`1e-7 -> 1e-7`, `1e20 -> 100000000000000000000`, and `1e21 -> 1e+21`;
`NaN` and both infinities serialize as `null`.

## Object and Array behavior

The object-store cursor returns own string properties in ECMAScript order:
canonical array-index keys in ascending numeric order, then other string keys
in insertion order. Deleted properties are omitted, symbol keys are excluded,
and enumerability/attribute metadata is checked before emission. The cursor
uses the existing store APIs; it does not expose storage slots or build a
parallel object representation.

Object properties whose values are `undefined`, functions, or symbols are
omitted without emitting a comma. At top level those values produce the
distinct `NoResult` status. Arrays are visited over their logical length using
property lookup, so holes, deleted slots, explicit `undefined`, functions, and
symbols emit `null`; inherited indexed values are visible through the existing
lookup API. Non-finite Numbers in Arrays and objects also emit `null`.
Non-index Array properties do not extend the indexed traversal.

The traversal is iterative. Its active frame path is also the cycle set:
encountering an ancestor handle reports `CycleDetected`, while a repeated
reference that is no longer an active ancestor is serialized again. Handles
remain generation-checked by `PortableObjectStore`. Errors clear all active
frames, reset the writer, and return an empty output view; a subsequent valid
serialization can reuse the writer and storage.

## Limits and callback boundary

The shared defaults and hosted caps are:

| Resource | Bound |
|---|---:|
| Output | 64 KiB, also capped by runtime string limit |
| Nesting depth | 32 |
| Array length | 1,024, additionally capped by object-store/runtime limits |
| Serializer steps | 100,000 portable default; hosted execution budget applies |
| Own string properties per object | 256 in the object store |
| Number scratch | 384 bytes in the hosted adapter |

The core distinguishes output capacity, Array limit, traversal capacity, depth,
execution-step, cycle, stale handle, unsupported value/attribute, invalid
string, Number formatting, and store failures. The hosted mapping preserves
`StringLimitExceeded`, `ArrayLimitExceeded`, `JsonNestingLimitExceeded`,
`ExecutionBudgetExceeded`, and `TypeError` for the corresponding conditions.
No successful partial output is exposed.

Portable `toJSON`, replacer, reviver, and other callback execution remain
outside JS124. Detection of callable own or inherited `toJSON` is explicit; it
causes an unsupported status for portable callers and one hosted legacy pass.
There is no speculative callback invocation in the shared pass. Existing
`JSON.parse` reviver behavior is not changed; JS110 remains its regression
authority.

## Memory and stack accounting

Existing object/Array backing remains 585,728 bytes and string backing remains
327,680 bytes. These are persistent configured stores. At maximum hosted
stringify bounds, the temporary output vector is at most 65,536 bytes and the
32-frame vector is 2,304 bytes (`PortableJsonTraversalFrame` is 72 bytes on the
qualified AMD64 target). The adapter's 384-byte Number scratch is stack-local.
The header-only core and formatter allocate nothing. The hosted peak for these
listed stores plus stringify scratch is therefore 981,248 bytes, excluding
vector bookkeeping and unrelated runtime storage. The existing JSON parser's
up-to 73,728-byte scratch is used by parsing, not concurrently by stringify.

The AMD64 freestanding `-fstack-usage` report measures
`formatPortableJsonBinary64` at 240 bytes, `appendQuotedUtf8` at 112 bytes,
the writer step callback at 48 bytes, and the serializer qualification probe
at 512 bytes. The shared traversal is iterative. These are individual static
frames, not a complete call-chain proof. The adapter's hosted
`RuntimeContext::Evaluator::stringifyJson` has a 1,184-byte static frame in
its strict compile report. No claim is made that the whole path fits the
approximately 16 KiB kernel boot stack.

## Freestanding AMD64 evidence

The qualification compiles the actual shared serializer, object store, Value,
string store, bounded writer, and Ryu formatter using MinGW GCC 15.2.0,
`x86_64-w64-mingw32`, C++17, AMD64 PE/COFF, exceptions and RTTI disabled,
`-nostdlib`, `-nostdinc++`, and explicit headers. Three translation units are
relocatable-linked with `ld -r --no-undefined`. The resulting component is
78,828 bytes, SHA-256
`F176D1FD3CD5145A66D431C1C011B37F153F0D8FF07068AA8D0652B06289D11B`, with
zero undefined symbols and no allocation, C++ runtime, Windows import, TLS,
constructor, exception, or RTTI dependency. Its principal sections are `.text`
33,280 B, `.rdata` 11,712 B, `.data` 0 B, `.bss` 0 B, `.pdata` 984 B, and
`.xdata` 832 B. Ryu's formatter text contributes 5,632 B. This is compile,
link, and dependency-closure evidence only; there is no kernel link or guest
execution.

## Focused qualification

The JS124 focused suite passes **1,179/1,179** checks with strict C++17 flags
(`-Wall -Wextra -Werror -pedantic`). It covers all 555 independent Node number
strings and parser bit-round-trips; exact primitive/string/object/Array output;
property ordering, update and deletion/reinsertion; omitted values; sparse
Arrays; nested graphs; shared references; direct and indirect cycles; callback
boundaries; special hosted objects; depth/output/Array/step/frame bounds;
stale handles; error reset; and writer reuse. The new JS109 output-limit case
uses a three-byte cap because ECMAScript canonical spelling for `1e-7` is four
bytes; its original failure-atomicity assertion remains intact.

Focused JS109 passes **86/86**, including the hosted callback path, and JS110
passes **28/28**. The complete JavaScript matrix passes **116/116 lanes** and
reports **19,046/19,046 assertions across 74 reporting lanes**. The aggregate
matrix result is in the external JS124 evidence directory. These matrix totals
do not add the separately formatted JS109, JS110, JS121, JS122, JS123, or JS124
focused-suite counts.

The strict C++17 `build.bat` production build completed successfully. It
emitted 213 warnings across the full target; the changed JS124 serializer,
number formatter, writer, runtime adapter, and object-store files emitted
none.
The separately saved candidate is 9,374,467 bytes, SHA-256
`CE50BA38A5C9D2AF90E09471E2298C2C87C5461430038D479147C0B5DF158317`. Both
hosted page checks loaded through this exact candidate: JS109 and JS110 each
reported HTTP 200 and no JavaScript runtime error.

The full hosted Navigator aggregate reports **767 passed / 7 failed / 774
total**, matching the established baseline exactly. All seven failures are CSS
checks: phase 3C, phase 3G, phase 6A, three checks in phase 6B, and phase 6C.
There are no JavaScript failures in the aggregate.

After hosted qualification, the protected executable was restored byte for
byte and verified at 8,398,274 bytes, SHA-256
`A5E439455E4D3E735DFB848EC296A3EC4B347822B21F5B1B736D51227046A971`. All 42
generated Navigator fixtures still match the saved baseline manifest (458,372
bytes total); `.phase` remains absent. The candidate, logs, matrix lane
outputs, preflight, and postflight manifests are outside Git at
`E:\CodexArtifacts\guideXOSServer_NAVIGATOR_JS_SUPPORT\JS124\run-20261117-063813-d02315cd515e4380860d513081433e7c`.
No QEMU, QAC, guest execution, kernel-format integration, or MbedTLS change was
performed.

## Gate status at time of initial report

| Gate | Current status |
|---|---|
| S0 — architecture reconciled | Pass |
| S1 — portable Number formatting | Pass |
| S2 — shared object traversal | Pass |
| S3 — shared Array traversal | Pass |
| S4 — cycles and bounds | Pass |
| S5 — callback boundary | Pass; JS109 verifies hosted callback compatibility |
| S6 — freestanding dependency closure | Pass |
| S7 — hosted semantic parity | Pass; matrix and hosted page results match baseline |
| S8 — optional kernel format | Omitted |

The callback-free core is qualified for the shared values and limits described
above. It does not execute portable callbacks or establish boot-stack safety
for the whole runtime call chain.

## Follow-up

JS125 should take up portable callback invocation only after portable function
calls, observable property accessors/mutation, callback order, error
propagation, and side-effect semantics are explicitly supported. It should
then integrate `toJSON`, replacer and reviver callbacks without adding another
serializer or parser. Kernel admission and guest execution remain separate
qualification work.
