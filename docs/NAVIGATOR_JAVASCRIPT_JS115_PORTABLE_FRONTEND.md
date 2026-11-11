# Navigator JavaScript JS115 Portable Frontend

**Outcome B — `JS115_PORTABLE_FRONTEND_PARTIAL`**

- **Starting commit:** `85f7c909a4132422a94827f06e99106d9477bf3e`
- **Branch:** `NAVIGATOR_JAVASCRIPT_SUPPORT`
- **Host:** `LEON2_RDT`
- **Evidence root:** `E:\CodexArtifacts\guideXOSServer_NAVIGATOR_JS_SUPPORT\JS115\run-20261108-01`

## 1. Disposition

JS115 ports the existing shared lexer to caller-owned fixed token storage and
adds an external-storage mode to the existing AST implementation. The hosted
lexer API and hosted owning AST remain adapters around those same tokenization
and AST operations. The actual lexer and AST translation units compile for the
AMD64 freestanding target with exceptions, RTTI, standard C++ containers, and
the C++ runtime libraries disabled.

The parser remains hosted-only. Its header and implementation depend on
`std::vector`, including `std::vector<bool>`, and its public parse boundary
catches allocation exceptions. The real target compiler still rejects those
dependencies. No guest parser, simplified grammar, guest runtime, NativeElf
application, or guest execution is claimed.

The highest independently verified milestone is **P3 — shared AST
compilation**. The JS115 parser tranche is incomplete, so this is Outcome B.

## 2. Starting state and preflight

The checkout matched the requested starting commit and branch. It had no local
changes, tracked or untracked, and was one commit ahead of
`origin/NAVIGATOR_JAVASCRIPT_SUPPORT`. The active source tree was
`D:\dev\guideXOSServer_NAVIGATOR_JS_SUPPORT`.

The workstation is `LEON2_RDT`. The guest compiler is MinGW GCC 15.2.0 at
`C:\mingw64\bin\x86_64-w64-mingw32-g++.exe`, target
`x86_64-w64-mingw32`. JS112, JS113, and JS114 reports were read before editing.
The source inventory and JavaScript test runners are under `navigator_javascript`,
`tests`, and `scripts`.

Before any build, `guideXOSServer.exe` was 8,398,274 bytes with SHA-256
`A5E439455E4D3E735DFB848EC296A3EC4B347822B21F5B1B736D51227046A971`. A
byte-for-byte copy was saved under the JS115 evidence root. The preflight found
42 generated Navigator fixtures totaling 458,372 bytes; all 42 matched the
saved JS111 manifest with zero changed or missing files. `.phase` was absent.

JS114 selected Option C: one shared interpreter core, bounded storage, and
explicit error propagation. JS115 applies that decision to the lexer and AST
storage boundary only. It does not repeat JS114's diagnostic link as a guest
acceptance claim.

## 3. Frontend dependency audit

| Component | Existing dependency and role | JS115 result |
|---|---|---|
| `SourceView` | Non-owning pointer and byte length. Tokens and AST locations refer to these bytes. | Kept as the shared source-view contract. Callers retain the bytes for the lifetime of tokens and AST text access. |
| Lexer | One scanner; previously appended tokens into `std::vector<Token>`. Hosted wrapper could throw while growing that vector. | Scanner now emits into caller-owned `TokenBuffer`. Hosted `LexResult` uses the same scanner through a vector adapter. Freestanding compilation excludes the adapter. |
| AST | Integer node IDs and contiguous child-ID ranges backed by two vectors. Hosted growth and `appendFrom` used exception handling. | Same `AstNode` and IDs. Hosted mode owns vectors; freestanding mode receives fixed node and child arrays through `AstStorageView`. |
| Parser | Many local vectors for statements, declarations, arguments, parameters, properties, arrays, scope traversal, and duplicate-binding checks. It also uses `std::vector<bool>`, `std::char_traits`, and an allocation-exception adapter. | Not ported. The actual target compile still fails at the hosted-only vector header and unavailable throw helpers. |
| Value and runtime | `std::string`, vectors, dynamic object storage, algorithms, numeric conversion, and many exception paths. | Out of scope and remain hosted-only. |

No `std::optional` or `std::variant` use was found in the frontend. Token and
AST relationships already use integer IDs, so stable node identity does not
require pointer relocation. Standard containers and exception translation were
implementation choices for storage and allocation, not part of the grammar.
The scanner compares and classifies source bytes directly; it does not own or
format token strings, and numeric token recognition does not require a hosted
conversion routine. AST literal and identifier text remains a borrowed source
slice. Parser diagnostics are fixed records with codes and locations rather
than dynamically formatted strings, but the parser still needs vector-backed
temporary work lists and catches allocation failure at its public boundary.
`value.cpp` and `runtime.cpp` retain their hosted string/container use and
numeric/runtime conversions; they are outside this tranche.

## 4. Shared-source design

`Lexer::tokenizeInto` is the common scanner entry point. It accepts a
`SourceView` and a caller-owned `TokenBuffer` with a pointer, fixed capacity,
and published count. The scanner no longer needs a standard container,
standard algorithm, or dynamic growth. The hosted `Lexer::tokenize` adapter
resizes a vector before scanning, calls `tokenizeInto`, then publishes the
result or clears the vector. It maps hosted allocation exceptions to
`LexerErrorCode::AllocationFailure`.

`Ast` now has two storage policies while retaining one `AstNode` model and one
set of node, child, root, source-slice, and append operations. The hosted
default constructor owns vectors for compatibility. The freestanding
constructor receives an `AstStorageView` over caller-owned fixed arrays. Node
IDs and child IDs remain indices; they do not depend on stable C++ object
addresses. Freestanding `addNode`, `setChildren`, and `appendFrom` fail before
publishing invalid IDs or partial child lists when storage is insufficient.

`FrontendArena` supplies bounded caller-owned byte allocation for arrays or
other frontend records. It checks power-of-two alignment, pointer and size
arithmetic, capacity, and mark generations. It does not call the kernel
allocator or own its backing range. The lexer and AST accept explicit storage
views; they do not silently create a guest heap.

The hosted adapter boundary is narrow: the vector-owning `LexResult` and
`Lexer::tokenize` exist only when
`GXOS_NAVIGATOR_FRONTEND_FREESTANDING` is not defined. Hosted AST vectors and
their exception catches are similarly excluded from the target object. The
scanner, AST node kinds, IDs, source locations, and fixed-storage operations
are shared.

## 5. Bounded storage and errors

Current accepted defaults remain 1 MiB of source, 8,192 tokens including EOF,
and 16,384 AST nodes. These capacities are not raised. `TokenBuffer::count` is
reset before each call and remains zero on failure, so partially written
entries are not published as a token stream. The portable lexer distinguishes
configured token exhaustion from caller-buffer exhaustion and invalid output
storage.

`FrontendArena` returns explicit `FrontendStorageError` values for invalid
storage, invalid alignment, arithmetic overflow, exhaustion, and an invalid
mark. A zero-byte allocation succeeds with a null result and consumes no
space; alignment is still checked. Restoring a mark from a previous reset or a
future offset fails without changing the current offset. Reset invalidates old
marks and resets the high-water counter.

AST construction uses the existing invalid-node sentinel and Boolean status
from `setRoot`/`setChildren`; `appendFrom` returns failure and leaves the
destination unchanged for validation, offset-overflow, or capacity failures.
Hosted vector allocation errors remain contained in the hosted adapter. The
parser's explicit diagnostics are not yet available in the freestanding
profile because `parser.h` remains coupled to `std::vector`.

## 6. Source lifetime

Token lexemes and AST source spans borrow their source bytes. The caller must
retain those bytes until all token and AST reads finish. The portable storage
API does not copy source text or make a temporary buffer persistent.

The hosted `RuntimeContext` owns the source used by `execute`. For same-realm
execution, it copies the first script's source into `realmSourceStorage_`
before replacing `sourceStorage_`, appends later sources with a separator, and
rebases each appended AST with `Ast::appendFrom`. The runtime regression added
in JS115 retains a closure, executes two later scripts, then calls the old
closure and reads its original identifier. This passes, confirming that the
shared AST storage change preserves the existing hosted source-lifetime fix.

Freestanding callers still need to provide source-buffer lifetime and realm
ownership. This phase does not port `RuntimeContext` or its `std::string`
source buffers.

## 7. Grammar and semantic coverage

The hosted parser remains the authoritative grammar and was not rewritten. Its
existing tests cover declarations, functions, arrows, default/rest parameters,
blocks and control flow, property and call expressions, object and Array
literals, operators and precedence, malformed syntax, error offsets, and
resource limits. The lexer fixtures cover keywords, identifiers, numbers,
strings, comments, punctuation, overlapping operators, CR/LF locations,
malformed input, and explicit source lengths. The strict JSON reader remains
separate and was not changed.

JS115 adds a bounded-vs-hosted token comparison to the existing lexer
qualification fixtures. It compares token kinds, byte spans, line/column
locations, lexeme bytes, error categories, error locations, and offending
bytes. A hand-authored `var x=1;` AST fixture compares the hosted parser's
canonical nodes and child IDs with the same `Ast` operations over fixed arrays;
it does not use another parser as an oracle. The target objects are compiled
but are not executed.

The target parser does not compile yet, so hosted grammar coverage is semantic
preservation evidence, not guest parser parity.

## 8. Freestanding compilation and link boundary

The target qualification script derives the MinGW header paths from the
installed compiler and uses the JS114 AMD64 kernel profile:

```text
-std=c++17 -ffreestanding -O2 -Wall -Wextra -Werror -pedantic
-fno-exceptions -fno-rtti -nostdlib -nostdinc++ -fno-builtin
-DGXOS_BARE_METAL -DGXOS_NAVIGATOR_FRONTEND_FREESTANDING
-m64 -mcmodel=large -mno-red-zone -mno-mmx -mno-sse -mno-sse2
-fno-pic -fno-pie -fno-PIC -fno-PIE -mno-stack-arg-probe
-fno-stack-check -fno-stack-protector -D__x86_64__
```

The highest milestone table is:

| Milestone | Result | Evidence boundary |
|---|---|---|
| P0 — baseline characterization | Pass | Hosted lexer/parser tests passed before edits. Guest lexer and parser compilation failed on the existing vector dependency. |
| P1 — portable storage foundation | Partial | Source, token buffer, AST storage view, arena, lexer errors, and AST storage operations compile. Parser diagnostics remain behind a header that includes `std::vector`. |
| P2 — shared lexer compilation | Pass | Actual `lexer.cpp` and portable test translation unit compile for AMD64 with exceptions disabled. |
| P3 — shared AST compilation | Pass | Actual `ast.cpp` compiles with fixed caller-owned storage and no undefined symbols. |
| P4 — shared parser compilation | Not passed | `parser.cpp` is rejected by the freestanding vector header and unavailable `__throw_bad_alloc`/`__throw_bad_array_new_length` helpers. |
| P5 — hosted semantic parity | Pass | The shared lexer/AST changes pass focused regressions, the full 111-lane matrix (17,656/17,656 reported assertions), JS109/JS110 hosted pages, and the full hosted aggregate with its same seven CSS failures. |
| P6 — dependency-closed frontend component | Not passed | No parser object or complete frontend link exists. No NativeElf entry or guest application is claimed. |

Translation units compiled under the target flags: `lexer.cpp`, `ast.cpp`, and
`tests/navigator_javascript_portable_lexer_test.cpp`. The lexer and AST
implementation objects have no undefined symbols. The test object has expected
references to those implementations and MinGW's test entry shim; it was not
linked. No static component link, PE-to-ELF conversion, QEMU launch, or QAC
admission was attempted.

## 9. Hosted regression results

Focused lexer, parser, runtime, and portable-frontend harnesses pass after the
shared changes. JS104 passes 31/31, JS105 32/32, JS109 86/86, and JS110 28/28.
The full matrix passes 111/111 lanes with 17,656/17,656 reported assertions
across 70 assertion-reporting lanes. The original sequential matrix process
was interrupted while JS50 was starting; its saved prefix contains 47
completed lanes, and the resume runner completed the remaining 64/64 lanes.
The two logs and a combined evidence transcript are under the JS115 run log.

The production build succeeded. Its 9,182,493-byte executable had SHA-256
`9CBE2CAB027F5FD08CE7BFA6E61CDDB9430BC7588DB844935798811C806CE200`. That
exact binary loaded the hosted JS109 and JS110 pages with HTTP 200 and no
JavaScript error. The full hosted Navigator aggregate completed 767/774
checks. Its seven failures match the accepted CSS baseline exactly: phase 3C,
phase 3G, phase 6A, three phase 6B checks, and phase 6C. No new CSS or
JavaScript failure appeared.

The portable frontend harness tests the caller-owned lexer and AST directly
on the host, including maximum source/token/node capacities. It separately
compiles those implementations for the freestanding target. This is a hosted
semantic check plus target static-compilation evidence, not guest execution.

## 10. Stack and memory measurements

With MinGW GCC 15.2 and `-fstack-usage`, the target-compiled lexer reports a
304-byte `tokenizeInto` frame, a 96-byte string-scanning frame, and an 80-byte
numeric-scanning frame. The target-compiled AST reports 352 bytes for
`appendFrom` and 40 bytes for `setChildren`. These are compiler frame reports
for the translation units, not guest runtime stack acceptance.

The hosted parser's `-O2 -fstack-usage` report has a 688-byte maximum
single-frame entry in `validateScope` and the `Parser::parse` wrapper, a
656-byte `parseStatement` frame, and a 496-byte `parseAssignmentExpression`
frame. The parser retains bounded recursion counters (256 parser depth, 256
expression nesting, and 128 block nesting), but the grammar still uses
recursive descent. These hosted frame sizes do not establish a guest stack
budget or maximum recursive call-chain usage.

MinGW x86-64 layout measurements are:

| Record | Size | Alignment |
|---|---:|---:|
| `Token` | 56 B | 8 B |
| `AstNode` | 128 B | 8 B |
| `Ast` hosted owning mode | 128 B | 8 B |
| `Ast` freestanding view mode | 80 B | 8 B |
| `AstStorageView` | 32 B | 8 B |
| `TokenBuffer` | 24 B | 8 B |
| `FrontendArena` | 48 B | 8 B |
| `Parser` hosted wrapper | 72 B | 8 B |

`AstNode` remains 128 bytes, matching the existing JS104 record measurement.
The new storage fields increase the hosted `Ast` object itself; the
freestanding view omits its two owning vectors. At the current default limits,
the source (1,048,576 B), token slots (458,752 B), AST nodes (2,097,152 B), and
an illustrative 16,384 child-ID slots (65,536 B) total 3,670,016 B before
alignment padding, arena metadata, parser scratch, and runtime state. The
child capacity and parser scratch have no integrated freestanding allocation
policy yet, so this sum is a planning estimate, not a reserved or accepted
guest footprint. The largest test arrays are static test storage, never kernel
stack arrays.

The parser row is the hosted `Parser` wrapper's layout only. The parser's
private traversal state and its temporary vectors have no accepted bounded
guest representation or target layout measurement because `parser.cpp` does
not compile under the freestanding profile. `AstStorageView` callers must
provide typed arrays aligned for `AstNode` and `AstNodeId` (8 bytes in the
measured AMD64 ABI); the arena can satisfy those alignments when used to obtain
the backing range. These are compiler and layout measurements, not guest heap
or stack acceptance.

## 11. Protected artifacts and execution restrictions

The original 8,398,274-byte executable was restored from the protected copy
and verified at SHA-256
`A5E439455E4D3E735DFB848EC296A3EC4B347822B21F5B1B736D51227046A971`. The 42
generated fixtures (458,372 bytes) matched the saved preflight snapshot with
zero changes or missing files after the hosted aggregate. `.phase` remained
absent.

JS115 is compile/link and hosted-test qualification only. QEMU was not
launched, QAC admission was not requested, and no other guideXOS guest session
was touched.

## 12. Remaining work and JS116 recommendation

The remaining JS115 parser work is not a compiler-flag adjustment. It needs a
bounded token view, a parser scratch strategy for the many local traversal
lists, an AST workspace that is passed through the existing parser, explicit
failure checks for every temporary-list operation, and a parser diagnostic
result that compiles without the hosted vector and exception headers. That
work must preserve the existing recursive-descent grammar and prove a bounded
scratch requirement before it is assigned to a guest stack or arena.

JS116 should port the existing parser against caller-owned token, AST, child,
and scratch storage, using the same parser source and error locations for the
hosted engine. Its gates should include the complete JS grammar corpus,
capacity and partial-failure tests, nesting/stack measurements, retained
closure source spans, a dependency audit of the resulting frontend objects,
and a `gx_main(gx_app_context*)` integration only in the later guest
integration phase.
