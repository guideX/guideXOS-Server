# Navigator JavaScript JS118 — Portable Values and Lexical Environments

**Phase:** JS118
**Target:** `JS118_PORTABLE_VALUES_ENVIRONMENTS_VERIFIED`
**Starting HEAD:** `cb7a5a913055959c2910fcdf0ad82a77632ed8e8`
**Branch:** `NAVIGATOR_JAVASCRIPT_SUPPORT`
**Execution:** Native/hosted qualification and AMD64 freestanding compilation; no guest execution.

## Architecture Before JS118

The shared runtime was already the implementation used by hosted Navigator. `Value` was a tagged structure with separate fields for Boolean, binary64 Number, and several runtime IDs. That duplicated payload storage and kept accessors as inline field reads. String Values held indices into a `RuntimeContext` vector of `std::string`; object, Array, and function Values held context-local indices into their owning vectors. Host objects already used generation-bearing opaque IDs.

`Environment` owned `std::vector<Binding>`, and each `Binding` owned its identifier in `std::string`. Environment IDs indexed a context vector. The evaluator walked parent IDs and used the environment's vector lookup. Parameter and body scopes were already distinct for non-simple parameter lists. The runtime retained environment records through a realm so closures could refer to their captured environment; `RuntimeContext::reset()` cleared the vectors. Runtime allocation failures were handled at hosted boundaries with exceptions, while environment operations returned an `EnvironmentError` category.

The smallest shared change was to put strings and bindings in caller-backed bounded stores and keep hosted `RuntimeContext` as the owner of those arrays. The evaluator and test adapters now use the same `EnvironmentStore` and `RuntimeStringStore` operations as the freestanding qualification probe. No guest-only Value, binding model, or evaluator was added.

## Value and Reference Representation

`Value` is now a 16-byte tagged union with one payload. It retains Undefined, Null, Boolean, Number, String, Function, Object, HostObject, Symbol, and the existing namespace values. Number remains an IEEE-754 binary64 payload; tests cover negative zero, NaN, and both infinities. Values are trivially copyable and copying them performs no allocation.

String, function, object/Array, and environment IDs are 32-bit handles with a 16-bit nonzero realm generation and 16-bit slot. Slot `0xffff`, generation zero, and the all-ones ID are reserved. Lookup checks generation and slot against the owning store before dereferencing. A reset advances the runtime generation, so reusing slot zero cannot revive an old Value or environment ID. The runtime caps its generation at `0xfffe` instead of wrapping. Host-object references retain their existing 64-bit generation-plus-registry-slot representation.

These IDs are opaque tokens, not pointers. Runtime object and function records can live in hosted vectors that move; Values still identify their records by the bounded slot and current generation. The records remain owned by `RuntimeContext` and are released at realm reset.

## String Ownership

`RuntimeStringStore` uses caller-owned `RuntimeStringRecord[]` and `char[]`. Each record stores an offset and byte length. String bytes are copied as-is from the accepted UTF-8 input; lookup returns a length-aware `SourceView`, so empty strings and embedded zero bytes are preserved. A Value owns only a generation-checked string handle and never points into lexer, parser, or temporary source storage.

Configuration checks pointer ranges, arithmetic overflow, and metadata alignment. Creation checks record capacity, byte capacity, and the per-string bound before copying or publishing a handle. Failed creation leaves counts unchanged. Marks and generation-checked restore support existing transactional JSON parsing rollback. Reset invalidates handles by advancing the store generation and starts both append-only counters at zero. Reconfiguration and direct reset require a strictly newer generation; same-generation and backwards reuse leave the store invalid, so an old slot cannot become live again.

## Environment and Binding Storage

`EnvironmentStore` is a shared, caller-backed store over `EnvironmentRecord[]`, `Binding[]`, and a copied-name byte arena. Environment records carry a parent handle, first/last binding indices, binding count and per-environment limits, kind, generation, and active state. Bindings carry an owned-name `SourceView`, a `Value`, an index link, mutability, lexical-declaration metadata, and a separate initialized bit. The initialized bit distinguishes TDZ from a binding initialized to JavaScript `undefined`.

Bindings and names are append-only within a realm. Each environment has an index-linked binding list in one shared binding pool. This keeps every binding and copied name stable while a closure retains an environment. Parent links are handles, and `setParent` validates references and rejects cycles. The maximum parent traversal is bounded by 2,048 environments; all local binding scans together are bounded by the 16,384-entry pool.

`Environment` is now a thin hosted adapter to the same store. `RuntimeContext` reserves stable adapter slots for its non-root environments, while the root has a dedicated adapter. The shared stores themselves contain no `std::vector`, `std::string`, or exception dependency.

## Binding Semantics and Errors

The store implements creation, root creation, parent assignment, declaration, initialization, local and parent-chain resolution, read, assignment, reset, and generation checks. `var` redeclaration is idempotent when it meets an existing non-lexical binding; lexical conflicts return `DuplicateLexicalBinding`. A lexical binding is published uninitialized when needed, then initialized once. A read or assignment finds the nearest binding first; an uninitialized inner declaration therefore raises TDZ even if an outer binding is initialized. Immutable assignment returns a distinct `ImmutableAssignment` error.

Portable operations return `bool` plus an explicit error record. Categories distinguish invalid storage/alignment/input, invalid or stale reference, missing and uninitialized binding, duplicate declaration, immutable assignment, per-environment and pool exhaustion, oversized names, arithmetic overflow, parent cycles, and invariant failure. The hosted evaluator maps these categories to its established JavaScript errors, including UnknownIdentifier, UninitializedBinding, TypeError, DuplicateBinding, BindingLimitExceeded, BindingNameTooLong, and AllocationFailure.

Parameter and function-body kinds retain JS105's storage relationships. Non-simple parameter lists use a `Parameters` environment parented by the closure. All parameter names are declared before defaults are evaluated, so earlier parameters are readable and later parameters remain in TDZ. The body receives a separate `FunctionBody` child created after parameter evaluation, keeping body-only declarations out of default evaluation. Rest Array construction remains hosted runtime behavior. Simple parameters retain their established single function environment.

## Bounds, Alignment, and Lifetime

At defaults, the portable store capacities are:

| Storage | Capacity | Record/element size | Backing bytes |
|---|---:|---:|---:|
| Environments | 2,048 | 24 | 49,152 |
| Bindings | 16,384 | 40 | 655,360 |
| Copied binding names | 1 MiB | 1 byte | 1,048,576 |
| Runtime string records | 4,096 | 16 | 65,536 |
| Runtime string bytes | 256 KiB | 1 byte | 262,144 |
| **Portable backing total** |  |  | **2,080,768 bytes** |

The separate hosted environment-adapter reserve is 2,047 × 32 = 65,504 bytes. `EnvironmentStore` metadata is 88 bytes and `RuntimeStringStore` metadata is 64 bytes on AMD64. Vector control blocks and allocator bookkeeping are not included. The pool contains one binding array rather than reserving 256 bindings in every environment.

The prior per-environment limits still allow a theoretical 2,048 × 256 = 524,288 bindings. Reserving that entire theoretical set would consume 20,971,520 bytes for 40-byte binding records alone. At the 256-byte per-name maximum, that same theoretical population would need another 134,217,728 name bytes. JS118 instead caps actual binding records at 16,384 and copied names at 1 MiB. The actual configured bounded backing is the table above; capacities are established by the host vectors during runtime initialization. This accounting excludes all other runtime objects, arrays, properties, AST/source storage, allocator overhead, and process state.

AMD64 record layouts are: `Value` 16 bytes/alignment 8; `RuntimeStringRecord` 16/8; `EnvironmentRecord` 24/4; `Binding` 40/8; `EnvironmentStore` metadata 88; `RuntimeStringStore` metadata 64; and `Environment` adapter 32. The freestanding anchor asserts these target sizes and alignments. No over-aligned record is required by the 8-byte kernel allocator.

The focused AMD64 `-fstack-usage` report measures the qualification anchor at 576 bytes, the largest environment-store operation (`declareImpl`) at 208 bytes, parent-chain resolution at 160 bytes, and string-store operations at at most 32 bytes. This is static compiler evidence only. Existing hosted JS74/JS76 stack probes measured event-depth-16 maxima of 323,633/321,233 bytes on a Windows thread with a 1 MiB reserve; those measurements are not guest stack evidence. The JS118 component does not call the parser or evaluator from the 16 KiB boot stack and does not establish guest stack safety.

## Freestanding Compilation and Dependency Closure

The actual `value.cpp`, `environment.cpp`, and a C ABI anchor that invokes shared operations compile with MinGW GCC 15.2.0 for `x86_64-w64-mingw32`, C++17, the accepted AMD64 freestanding flags, explicit cross-target C++ headers, exceptions and RTTI disabled, `-nostdlib`, and `-nostdinc++`. A relocatable `ld -r --no-undefined` link and static archive are produced. `nm` reports no undefined symbols in either component or archive. The component retains the probe, string creation, and environment declaration operations; its symbols show no C++ standard-library, exception-runtime, allocator, or platform-import dependencies. `objdump` identifies AMD64 PE/COFF, zero BSS bytes, and no TLS or constructor sections.

This establishes compile and dependency closure for the Value/string/environment component and the called operations. It does not establish NativeElf conversion, kernel integration, a complete production image, guest memory safety, bootability, or guest execution.

## Qualification Results

JS118 is **Outcome A — `JS118_PORTABLE_VALUES_ENVIRONMENTS_VERIFIED`**. Required gates V0–V4 pass. The focused C++17 portable test uses the actual shared stores and C ABI probe and passes 44/44 checks (22 Value and 22 Environment checks). The test reports Value 16 bytes, string record 16, environment record 24, Binding 40, EnvironmentStore metadata 88, RuntimeStringStore metadata 64, and hosted Environment adapter 32 bytes. JS115 portable lexer/AST qualification and JS116 lexer/AST/parser qualification pass on the final source. The full 111-lane JavaScript matrix passes 111/111 lanes and 17,656/17,656 reported assertions across 70 assertion-reporting lanes. JS104 passed 31/31, JS105 32/32, JS109 86/86, JS110 28/28, and JS106–JS108 regressions passed.

The final production `build.bat` hosted build succeeds. The fresh 9,217,483-byte executable has SHA-256 `324E250F0424DC7CD20EF71AA22BFA1BF54F8FEF944478260555A6DB0A5F934C` and is preserved outside the checkout. That build passes both hosted JSON pages: JS109 and JS110 each load through Navigator with HTTP 200 and report no JavaScript runtime failure. The full hosted aggregate records 767/774 checks; its seven failures are exactly the established CSS checks in phases 3C, 3G, 6A, three checks in 6B, and 6C. The aggregate script consequently exits 1 for those baseline failures; no new JavaScript failure is present.

The final regression pass also updated stale layout assertions to the smaller portable `Value` and Array-element records. JS91 exposed a real rollback bug: descriptor-construction cleanup compared a generation-packed object handle as though it were a vector index. Cleanup now compares the decoded slot, and all four injected descriptor-field insertion failures pass with no leaked object or properties. JS84 diagnostics now include the global environment record in their high-water count.

The protected original `guideXOSServer.exe` was verified before the build and backed up under `E:\CodexArtifacts\guideXOSServer_NAVIGATOR_JS_SUPPORT\JS118\preflight`. After hosted qualification, the original 8,398,274-byte executable was restored and verified at SHA-256 `A5E439455E4D3E735DFB848EC296A3EC4B347822B21F5B1B736D51227046A971`. The saved JS111 manifest records all 42 generated fixtures (458,372 bytes); all 42 match after hosted qualification. `.phase` remains absent. No QEMU, guest execution, QAC admission, or optional V5 kernel-object-format qualification was performed.

## Remaining Dependencies and JS119

The Value, string, and environment stores are portable; `RuntimeContext`, the evaluator, functions, arrays, properties, JSON, host objects, and Navigator DOM adapters still depend on hosted standard-library containers and exception boundaries. The 16 KiB boot-stack constraint remains open, and no interpreter evaluation is permitted from that stack based on these results.

Recommended JS119 scope: qualify the runtime record pools that own objects, Arrays, functions, and properties as shared bounded storage, preserving identity and generation rules. Keep expression evaluation, JSON semantics, host adapters, and guest integration out until those storage pools have an independently closed AMD64 component and measured memory/stack bounds.
