# JS123 portable JSON numbers and bounded writer

**Outcome B — `JS123_PORTABLE_JSON_NUMERIC_SERIALIZER_PARTIAL`**

JS123 completes shared, correctly rounded decimal-to-binary64 conversion for
the existing strict JSON parser and adds a bounded JSON output writer shared
by hosted serialization code. It does not yet add the portable Value/object/
Array serializer, JavaScript Number-to-string formatting, or a portable
callback boundary. This is a partial serializer milestone, not portable
`JSON.stringify` compatibility.

## Starting state

The work began on branch `NAVIGATOR_JAVASCRIPT_SUPPORT` at
`0e5af83abaa77b073923868a7efee9966edf529c`, with the branch two commits ahead
and zero behind its upstream. The tracked worktree was clean. The protected
`guideXOSServer.exe` was 8,398,274 bytes with SHA-256
`A5E439455E4D3E735DFB848EC296A3EC4B347822B21F5B1B736D51227046A971`.
`.phase` was absent. The 42 generated Navigator fixtures matched the saved
manifest and totaled 458,372 bytes.

JS122's accepted partial checkpoint provided one strict shared JSON parser,
portable string/object/Array stores, parse rollback, a 64 KiB input bound, and
an AMD64 freestanding parser component. Its focused suite passed 67/67; JS109
86/86; JS110 28/28; JS121 32/32; and the baseline matrix 114/114 lanes with
17,797/17,797 reported assertions across 72 lanes. The hosted aggregate was
767/774 with seven established CSS failures.

## Numeric conversion

`json_parser.inl` remains authoritative for JSON number grammar. Once it has
validated a token span, it calls `parsePortableJsonBinary64` from
`json_number_impl.inl`. The hosted adapter in `json_number_host.h` delegates to
that same function; `std::from_chars` and its approximate out-of-range
fallback are no longer separate authorities.

The implementation vendors the C99 `ffc.h` decimal parser from
[`kolemannix/ffc.h`](https://github.com/kolemannix/ffc.h), revision
`7e85c6d11a0289d5eead7c1404de8203c52f8cf8`, under MIT. The selected algorithm
uses bounded integer arithmetic and exact digit comparison to direct
round-to-nearest, ties-to-even. The source, license, revision, and target-only
header changes are documented in [ffc provenance](../third_party/ffc/PROVENANCE.md).
The C++ fast_float headers were also evaluated but required hosted standard
library headers/runtime support in this target profile, so they were not
adopted.

The wrapper accepts only nonempty token spans of at most 64 KiB and requires
the converter to consume the entire span. It accepts the converter's
out-of-range result so syntactically valid JSON numbers retain JavaScript
Number behavior: overflow becomes signed infinity, underflow becomes signed
zero, and `-0` preserves its sign bit. The parser reports `InvalidNumber` only
if conversion itself fails; syntax remains independently rejected by the
shared JSON grammar. Conversion is locale independent and makes no heap
allocation. The implementation's bounded bigint scratch is local to the
converter; its largest measured parser slow-path frame is 3,696 bytes.

The 70-check JS123 suite uses independently authored expected 64-bit patterns
for signed zero, ordinary values, large integers, adjacent values, exact
midpoints on both sides of one in both signs, subnormal thresholds, the normal
boundary, maximum finite values, overflow, and underflow. It also checks
top-level/object/Array/nested parser integration, rollback on injected numeric
failure and trailing input, strict invalid spellings, locale independence
where a decimal-comma locale is available, and 64 KiB mantissa and exponent
tokens. The focused suite passes **70/70**.

## Bounded writer tranche

`PortableJsonOutputWriter` in `json_writer.h`/`.inl` writes into caller-owned
storage. It supports checked byte/span appends, escaped validated UTF-8,
capacity queries, sticky errors, complete-only-on-success, and reset/reuse.
Escaping covers quotes, backslashes, controls, NUL, and multibyte UTF-8. A
capacity or UTF-8 failure can leave bytes in the caller buffer, but `complete`
withholds the partial output. Tests compare exact hand-authored bytes, exact
capacity, one-byte-short capacity, invalid UTF-8, step exhaustion, and reuse.

Hosted `stringifyJson` now uses this writer and a caller-owned vector capped at
the existing 64 KiB/runtime-string limit. Its per-scalar work still uses the
runtime step budget. Capacity failures keep the existing `StringLimitExceeded`
mapping; malformed internal UTF-8 maps to `TypeError`. The hosted traversal,
property preparation, callbacks, cycle handling, and number formatting remain
in `runtime.cpp`.

This tranche does **not** provide shared Value classification or traversal,
portable primitive serialization, portable shortest Number formatting,
portable property enumeration, sparse/undefined serialization, or active-path
cycle detection. In particular, the `ffc.h` fixed-decimal formatter is not a
JavaScript shortest-round-trip formatter and is not used for JSON output. The
portable serializer therefore cannot yet claim canonical ECMAScript number
strings or object/Array behavior.

There is no new portable traversal path that could ignore `toJSON`; portable
`toJSON` and reviver execution remain unimplemented. The hosted path keeps its
existing callable `toJSON`, receiver/key handling, substitutions, and JS110
post-order reviver behavior. JS109 (86/86) and JS110 (28/28) both pass after
the shared writer integration. An explicit unsupported-callback result belongs
with the future portable traversal entry point.

## Limits, memory, and stack

The parser keeps JS122's 64 KiB input, 64 KiB decoded-string, 4,096 string
entry, 256 KiB string-byte, depth-32, Array-length-1,024, aggregate-Array-4,096,
and 100,000-step limits. The writer has explicit caller-supplied capacity; the
host adapter caps it at 64 KiB. It never reports partial bytes as a completed
JSON string.

The existing portable object/Array backing is 585,728 bytes; string backing is
327,680 bytes; parser scratch can reach 73,728 bytes. Numeric conversion adds
bounded local stack scratch rather than a caller-visible heap buffer. The
freestanding stack report measures `ffc_digit_comp` at 3,696 bytes and the
shared parser's recursive `parseValue` frame at 224 bytes (32 frames alone are
7,168 bytes). These are separate reported frames; their call-chain sum plus
surrounding runtime frames has not been accepted as safe for the approximately
16 KiB kernel boot stack. No boot-stack change or whole-chain safety claim is
made. No portable serializer traversal/cycle storage exists yet.

## Freestanding evidence

The JS123 qualification script compiles the shared object/string stores, the
real shared parser plus numeric converter, and a writer probe using MinGW GCC
15.2.0 targeting `x86_64-w64-mingw32`, C++17, `-ffreestanding`, `-fno-exceptions`,
`-fno-rtti`, `-nostdlib`, `-nostdinc++`, and the established AMD64 COFF flags.
It relocatable-links with `ld -r --no-undefined` and audits symbols, sections,
and stack-usage output.

The selected component passed as PE-x86-64 COFF, 113,045 bytes, with zero
undefined symbols and no C++ runtime, allocator, Windows import, TLS, or static
constructor dependency. Its reported sections include 69,184 bytes of `.text`,
13,760 bytes of `.rdata`, 224 bytes of `.data`, and zero `.bss`; PE `.pdata` and
`.xdata` unwind metadata are present (1,344 and 1,248 bytes). Exceptions and
RTTI are disabled, and no exception/unwind runtime symbols are undefined. This
is compile/link/dependency evidence only; it is not guest execution or proof of
numeric runtime behavior on hardware.

## Regression and artifact qualification

The focused runs pass JS123 70/70, JS122 67/67, JS121 32/32, JS109 86/86, and
JS110 28/28. JS121 passes 32/32. The complete matrix passes **115/115 lanes**
and reports **17,867/17,867 assertions across 73 reporting lanes**. These
matrix totals do not include the separately reported focused JS123 70/70,
JS122 67/67, JS121 32/32, JS109 86/86, and JS110 28/28 suites. The JS109 and
JS110 hosted pages each loaded with HTTP 200, no inline JavaScript error, and
zero resource failures.

The strict C++17 production build completed successfully. Its separately
preserved candidate is 9,336,464 bytes with SHA-256
`6BD7E9CB9A1DFEBA1FA6A2D8B1332D283AFD87D760F6B9FD850C6BC2EC9E442B`. The
hosted Navigator aggregate recorded **767 passed / 7 failed / 774 total**; the
seven failures are exactly the established CSS checks in phases 3C, 3G, 6A,
three checks in 6B, and 6C. The aggregate has no new JavaScript failures.

After hosted qualification, the original protected executable was restored
byte-for-byte and verified at 8,398,274 bytes with SHA-256
`A5E439455E4D3E735DFB848EC296A3EC4B347822B21F5B1B736D51227046A971`. All 42
generated fixtures match the saved preflight manifest (458,372 bytes total,
no extra files), and `.phase` remains absent. Final artifact verification is
saved outside Git at
`E:\CodexArtifacts\guideXOSServer_NAVIGATOR_JS_SUPPORT\JS123\run-20261116-730dfd776900449abfcdffb383bfd18b\final-artifact-verification.json`.

The JS123 work is committed locally on `NAVIGATOR_JAVASCRIPT_SUPPORT`; no push,
QEMU, QAC admission, guest execution, kernel-format integration, or MbedTLS
change is part of this phase.

## Gate results

| Gate | Result | Evidence |
|---|---|---|
| N0 — Architecture reconciled | Pass | Shared converter and hosted/portable boundary documented above. |
| N1 — Portable binary64 conversion | Pass | 70/70 independent bit-pattern, boundary, grammar, and long-token checks. |
| N2 — Strict JSON.parse numbers | Pass | Shared parser converts top-level, object, Array, and nested numbers with rollback. |
| N3 — Shared bounded serialization | Partial | Bounded writer and hosted escaping path are shared; portable traversal and shortest number formatting remain. |
| N4 — Explicit callback boundary | Partial | Hosted callbacks preserved; no portable traversal/callback result yet. |
| N5 — Bounds and failure safety | Partial | Parser conversion and writer capacity/UTF-8/step failures are bounded; serializer traversal limits remain. |
| N6 — Freestanding dependency closure | Pass | AMD64 compile/link and zero-undefined-symbol audit for parser, converter, stores, and writer probe. |
| N7 — Hosted semantic parity | Pass | Focused JS109–JS123, full matrix, production build, and hosted qualifications recorded in this report. |
| N8 — Kernel-format integration | Omitted | Optional and not run. |

## Remaining work and JS124

JS124 should add one shared callback-free serializer over `Value`,
`PortableObjectStore`, and `RuntimeStringStore`, with an explicit adapter hook
for hosted callbacks and an unsupported-callback result for portable callers.
It must include ECMAScript-compatible shortest Number-to-string formatting,
object key ordering, sparse Arrays, undefined/function context rules, active
ancestor cycle detection, generation-checked identities, iterative or
measured-bounded traversal state, and capacity/error cleanup tests. Only after
those pieces pass cross-path tests should the hosted runtime delegate its full
callback-free traversal to that core.

The intended result remains **Outcome B**: portable numeric parsing is
complete, while JSON serialization is only partially unified.
