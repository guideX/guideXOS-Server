# Navigator JavaScript JS116 Portable Parser

**Outcome A — `JS116_PORTABLE_PARSER_VERIFIED`**

- **Starting HEAD:** `3b5c3e45cd7392e020ef1815835f3e40cea91e21`
- **Branch:** `NAVIGATOR_JAVASCRIPT_SUPPORT`
- **Target:** `x86_64-w64-mingw32` (MinGW GCC 15.2.0)
- **Evidence root:** `E:\CodexArtifacts\guideXOSServer_NAVIGATOR_JS_SUPPORT\JS116\run-20261111-01`
- **Guest execution:** Not performed or claimed

## 1. Result

JS116 connects the existing parser to JS115's fixed-storage lexer and AST.
Hosted and freestanding callers now enter the same `ParserState` and grammar.
The portable boundary accepts a non-owning token view, caller-owned AST node
and child arrays, a bounded caller-owned scratch arena, and an explicit error
record. The target parser compiles with exceptions and RTTI disabled, without
implicit C++ include search or runtime libraries. The lexer, AST, and parser
objects also form an AMD64 COFF relocatable component with no unresolved
symbols.

This closes P4 and demonstrates P6 component dependency closure for the tested
MinGW COFF target profile. It does not demonstrate NativeElf/ELF compatibility,
integration with the kernel's guest Navigator adapter, or execution in a guest.

## 2. Baseline and JS115 reconciliation

The checkout began at the requested commit and branch, with the expected one
local commit ahead of `origin/NAVIGATOR_JAVASCRIPT_SUPPORT`. The protected
executable and fixture state were recorded before the production build. The
starting executable was 8,398,274 bytes with SHA-256
`A5E439455E4D3E735DFB848EC296A3EC4B347822B21F5B1B736D51227046A971`. The
pre-build fixture manifest contains 42 files totaling 458,372 bytes; `.phase`
was absent.

JS115's P1 result was partial because the portable storage pieces had not yet
been connected to the vector-backed parser. JS116 completes the parser-specific
storage path using the existing arena and fixed AST storage. It does not add a
general collection library. The arena validates storage, size arithmetic,
power-of-two alignment and generation-tagged marks. AST node IDs remain stable
indices; AST child lists are copied into the persistent caller-owned child
array before parser scratch is released. Parser failure resets the AST root and
node count and restores scratch through bounded list lifetimes.

## 3. Dependency inventory and implementation decision

| Parser dependency | Role before JS116 | JS116 treatment |
|---|---|---|
| Statement, declaration, parameter, argument, property and Array element vectors | Temporary parser lists | Replaced by parser-local `ScratchVector<T>` views backed by `FrontendArena`; each list has an explicit limit and mark-based release. |
| `std::vector<bool>` and `std::char_traits` | Duplicate/name validation and byte comparison | Replaced by byte arrays allocated from the arena and a shared byte comparison loop. |
| AST allocation and child ownership | Persistent parse result | Existing `AstNodeId` model is retained; fixed node and child storage is supplied by the caller. |
| Token vector | Parser input | Freestanding path takes `TokenView` directly; hosted vector overload adapts to that view. |
| `std::vector<unsigned char>` scratch and `std::bad_alloc` catch | Hosted boundary only | Kept in the hosted adapter. `parseInto` itself reports explicit failures and has no exception path. |
| Token lexeme and AST source text | Borrowed source slices | Kept as `SourceView` slices; the caller owns the source for the lifetime of tokens and AST text access. |
| Runtime `Value`, environments, closures, JSON, DOM | Execution engine | Not linked or migrated by this phase. |

The target parser source uses placement construction for arena-backed records,
but no throwing allocation, hosted container, dynamic string, exception helper,
or runtime execution facility. Its hosted adapter remains a compatibility
boundary around the same parser; it does not parse a second time with a second
grammar.

## 4. Shared token, parser and AST interfaces

`Lexer::tokenizeInto` remains the scanner entry point. Each call advances the
`TokenBuffer` generation. `makeTokenView` captures the buffer pointer, count,
source view and generation. `parseInto` rejects a stale view if that token
buffer has since been reused, validates token records and enforces the source
and token limits before parsing.

The AST owns no backing storage in freestanding mode. The caller supplies
`AstStorageView` with node and child arrays. Child relationships, function and
parameter metadata, declaration flags, operator kinds, source spans and node
text are represented by the same `AstNode` fields and stable IDs used by the
hosted build. Scratch addresses never become AST identity.

Hosted `Parser::parse` allocates bounded scratch storage and invokes `parseInto`
on the same parser state. `std::bad_alloc` is translated to
`ParserErrorCode::AllocationFailure` at this adapter. The hosted lexer vector
and AST owning APIs remain similarly gated from the freestanding build.

## 5. Bounded scratch, AST children and failure behavior

The parser uses `ScratchVector<T>` over the existing bump arena. It grows only
up to its configured item count, checks multiplication and arena allocation,
and reports `ScratchStorageLimitExceeded` instead of truncating. A vector's
arena mark is restored when its scope ends. Growth may retain old backing
blocks until that list scope ends, so the arena cap accounts for bounded
temporary allocation, not just the final vector capacity. List caps include
4,096 statements, 64 parameters, 64 call arguments, 256 object properties,
1,024 Array elements, and at most the configured AST node count for declaration
and validation work. Parser, expression and block nesting are capped at 256,
256 and 128 respectively.

After syntax construction, scratch is reset before lexical-scope validation.
At the maximum 16,384-node AST, validation reserves four `BindingNameRef`
arrays (262,144 bytes total), `ScopeVisit` records (131,072 bytes), a pending
node-ID array (65,536 bytes), and two visited-byte arrays (32,768 bytes). This
is 491,520 bytes before alignment; the configured scratch backing is 524,352
bytes (512 KiB plus 64 bytes of headroom). The parser's helper sizes scratch
from token count using a bounded 2x-node estimate, capped at the AST maximum.

Persistent child IDs are copied into AST-owned child storage. If node, child,
token, scratch, syntax-list, nesting or stack capacity is exceeded, the parser
returns a specific `ParserErrorCode` and a token index/location where
applicable. Invalid syntax and resource exhaustion remain distinct. The public
error record includes the category, source offset/length/line/column, actual
and expected token types, expected-token flag, and token index. On every
failure, no root is published and the AST is reset; tests cover a failed parse
followed by successful parses using the same buffers.

## 6. Lookahead, rollback, and source lifetime

Arrow-parameter lookahead inspects tokens without constructing provisional AST
nodes. Parenthesized grouping is recognized from the token stream, and deeply
nested grouping is consumed iteratively rather than recursively. Nested
statement, literal and expression paths use bounded depth and stack guards.
Scratch vectors use generation-checked arena marks; parser phase resets happen
after the corresponding list scopes end. No unrelated arena generation is
restored. Syntax and scope-validation errors unwind the list scopes and leave
the reusable arena at zero used bytes.

`TokenView` and `Ast` are non-owning with respect to source bytes. The caller
must retain each source buffer while its tokens or AST text spans are in use.
The parser does not retain a reference to token scratch as AST data. Regression
coverage parses two distinct sources into separate fixed ASTs, then verifies
the first AST still resolves its identifier and literal text after the second
parse. Reusing a token buffer invalidates an older generation-tagged token
view. Runtime-wide source retention and guest realm teardown remain execution
engine responsibilities outside this phase.

## 7. Grammar and differential qualification

The portable parser is the existing parser refactored behind `ParserState`,
not a parallel guest grammar. The focused differential corpus compares hosted
and fixed-storage lexer results (token kind, span, location and lexeme), then
compares parser success, error record, every AST node field, child order, and
source text. Hand-authored checks cover precedence and exact tree shape,
locations, statement order, default/rest parameter metadata, Array elisions,
object property order, and malformed syntax. It includes an expression shaped
like the JS100 regression, nested calls/objects/Arrays, 200 parenthesis levels,
250 nested `if` statements, a 30-level nested Array parse and a controlled
stack-budget rejection at greater nesting.

The new scratch-boundary case parses an Array with all 1,024 permitted
elements, rejects 1,025 with `TooManyArrayElements`, verifies the error has a
valid token index, empty AST and zero scratch usage, then successfully reuses
the token, AST and arena buffers three times. Other paths cover stale token
generation, source/token limits, malformed token records, AST node and child
storage exhaustion, scratch exhaustion, parser stack budget and retained
source text.

The two focused hosted executables pass under C++17 with `-Wall -Wextra
-Werror -pedantic`. The freestanding test translation units also compile, but
they are not run as target binaries. The full JavaScript matrix remains the
runtime-semantic regression oracle; no target parser execution is claimed.

## 8. Freestanding compile, symbol audit and P6

The compiler is `C:\mingw64\bin\x86_64-w64-mingw32-g++.exe`, GCC 15.2.0,
triple `x86_64-w64-mingw32`. The focused qualification script records the
complete commands and logs. The core flags include:

```text
-std=c++17 -ffreestanding -O2 -Wall -Wextra -Werror -pedantic
-fno-exceptions -fno-rtti -nostdlib -nostdinc++ -fno-builtin
-DGXOS_BARE_METAL -DGXOS_NAVIGATOR_FRONTEND_FREESTANDING
-m64 -mcmodel=large -mno-red-zone -mno-mmx -mno-sse -mno-sse2
-fno-pic -fno-pie -fno-stack-protector -fstack-usage
```

The script explicitly supplies the target toolchain's C++ header directories
for scalar declarations such as `cstddef`, `cstdint` and placement `new`;
`-nostdinc++` disables implicit C++ include discovery. The actual translation
units compiled are `navigator_javascript/lexer.cpp`, `ast.cpp`, and `parser.cpp`,
plus the portable lexer/AST and parser test translation units. There is no
runtime, value, JSON, DOM, or Navigator guest adapter in this component.

Lexer and AST objects have no undefined symbols; the parser object has only
the expected references to AST methods before component linking. `nm -u -C`
reports no undefined symbols for the three implementation objects after
`ld -r`. A static archive is also created successfully. Object format reports
AMD64 PE/COFF (`i386:x86-64`) for the MinGW profile. The COFF relocatable object
and archive establish dependency closure for these frontend objects under
this compiler profile. They do not establish ELF format compatibility,
NativeElf loader acceptance, final kernel linkage, or guest execution.

The highest gates are **P4 — shared parser compilation: passed** and
**P6 — dependency-closed frontend component: passed for the tested COFF
profile**. P0 baseline, P1 parser storage follow-through, P2 lexer compilation,
P3 AST compilation and P5 hosted semantic regressions also pass. No fabricated
symbols, entry point, exception stubs or production linker changes were used.

## 9. Stack and storage measurements

The target GCC 15.2 `-O2 -fstack-usage` report measures `parseInto` at 688
bytes, `parseAssignmentExpression` at 1,328 bytes, `parsePrimaryExpression`
at 256 bytes, `parseBlock` at 432 bytes, `parseObjectLiteral` at 448 bytes,
`parseArrayLiteral` at 384 bytes, and `validateScope` at 288 bytes. Recursive
grammar edges are kept visible to the report. The parser has an aggregate
96 KiB estimated stack budget with a 4 KiB entry/caller reserve and explicit
nesting limits. The compiler frames and in-parser budget are evidence for
bounded design; they are not a measured whole-call-chain or guest-stack proof.

Target record sizes, aligned to 8 bytes where applicable, are: `Token` 56,
`TokenBuffer` 32, `TokenView` 48, `AstNode` 128, `Ast` 80, parser state 320,
`ParserError` 56, `ParserLimits` 80, and arena metadata 48 bytes. The maximum
alignment used by these records is 8 bytes. At configured maxima, fixed backing
for 8,192 tokens (458,752 bytes), 16,384 AST nodes (2,097,152 bytes), 16,384
child IDs (65,536 bytes), and parser scratch (524,352 bytes) totals 3,145,792
bytes. The source cap is 1 MiB but the source is caller-owned and excluded
from that total. Descriptor records and parser stack frames are also excluded.

The target object section audit reports 0 `.data` and 0 `.bss` bytes for
lexer/parser and 128 `.data` bytes and 0 `.bss` bytes for AST; the linked
frontend component also has 128 `.data` and 0 `.bss` bytes. The parser's
read-only constants and AMD64 COFF unwind metadata appear in read-only and
unwind sections. The section report is retained as
`qualification-repair-02/logs/frontend-sections.txt`.

For the tested maximum Array, 1,024 literal elements require 2,051 tokens,
1,027 AST nodes, and 1,025 persistent child IDs (Program plus Array children).
Those counts follow the controlled test input and AST shape. The stack and
backing estimates do not include allocator or runtime state because the parser
uses supplied storage and does not allocate through a guest allocator.

## 10. Hosted regression, production build and artifact restoration

The fresh full JavaScript matrix passes **111/111 lanes** and **17,656/17,656
reported assertions across 70 assertion-reporting lanes**. It includes the
focused JS104 (31/31), JS105 (32/32), JS106–JS108 DOM suites, JS109 (86/86),
and JS110 (28/28). The standalone JS109 and JS110 hosted JSON pages each
returned HTTP 200 with no JavaScript runtime errors.

The production `build.bat` completed with existing warnings in unrelated
sources. The exact candidate used for hosted qualification was 9,217,119 bytes,
SHA-256 `4A1C9AEBF0317C218977F9E69E1728D8BE1A068B8D4ED44F1417405A2C8D4AA3`.
The full hosted Navigator aggregate reported **767 passed, 7 failed, 774
total**, and exited with its expected failure marker. All seven failures match
the accepted JS115 CSS baseline: phase 3C; phase 3G; phase 6A; the three phase
6B checks; and phase 6C. No additional JavaScript failure appeared.

The candidate executable is preserved in the evidence directory. After
qualification, the original executable was restored byte-for-byte and its
recorded creation/write times and Archive attribute were reinstated. Its size
and SHA-256 match the protected prebuild copy. All 42 generated fixtures match
the saved prebuild manifest (458,372 bytes total), and `.phase` remains absent.
The final restoration verification is recorded alongside the prebuild
metadata.

Evidence paths include:

- Portable qualification: `qualification-repair-02/metadata/portable-parser-qualification.json`
- Full JavaScript matrix: `logs/javascript-matrix-repair.log`
- Hosted aggregate: `hosted/aggregate/navigator-hosted-smoke-20261111-140901.log`
- Production build: `logs/production-build.log`
- Binary and fixture baseline: `metadata/pre-production-build.json`
- Final restoration check: `metadata/restoration-verification.json`

## 11. Remaining work and JS117 recommendation

The parser and its storage are not yet connected to the freestanding runtime.
`Value`, environments, invocation, closure execution, native Arrays, JSON,
Navigator host objects, DOM projection and event dispatch remain hosted
dependencies and are intentionally outside JS116. The MinGW COFF component
also needs a separately justified path to the kernel's eventual object/loader
format before it can be used in a guest image.

JS117 should first qualify an explicit frontend ABI and object-format path for
the actual freestanding kernel build, then integrate this frontend behind a
small guest-facing parsing interface. It should preserve the source-buffer
lifetime contract and keep runtime execution, JSON, DOM and event dispatch as
separate measured tranches. Guest admission and execution require a later
authorized phase; JS116 made no QEMU launch, QAC request or EFI package.
