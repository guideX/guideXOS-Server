# Developer Studio COMP4A — Guest Compilation Contract & Diagnostic Fidelity

**Status:** Hosted COMP4A implementation and qualification complete. Genuine guest qualification was not rerun in this phase and remains pending.

## Repository preflight

- Repository: `D:\dev\guideXOSServerV0.5_DEVELOPER_STUDIO`
- Branch: `v0.5_DEVELOPER_STUDIO`
- Starting HEAD: `e24ee589a0f13132d42487e1b04532a6e6d3cde3`
- Starting upstream divergence: `0 behind / 0 ahead`
- The tracked worktree was clean. Existing untracked `out/` and `kernel/out/` COMP2/COMP3 validation folders were preserved.
- No `.phase` marker or `AGENTS.md` file was present in this checkout. No phase marker was created or changed.

The latest implementation history at preflight was the COMP3 stack-frame bound fix (`e24ee589`), preceded by COMP3R4 desktop-boundary continuation wiring (`9594543f`), the kernel-owned build-job foundation (`f2aa5977`), and the COMP3 diagnostic-stage preservation work (`5718de57`). The latest published compiler-feature reports are Phase 27U; Phases 27V–27Z document shared declarations, project Run, and later Build/Run/Debug integration.

## Verified compilation architecture

```text
Build API request
  -> BareMetalBuildService reads project/manifest/source registration through VFS
  -> compiler_driver prepares the bounded source and quoted local declarations
  -> compiler_module: lexer -> parser/type checks -> target-neutral IR
  -> AMD64 backend emits code and relocations
  -> compiler_object serializes/reopens bounded ELF64 ET_REL objects
  -> compiler_linker resolves project symbols and relocations
  -> elf_writer emits static ELF64 ET_EXEC
  -> compiler_driver writes, closes, reopens, hashes, and validates the artifact
  -> Build service independently validates NativeElf and publishes its identity
  -> Run service accepts only the successful, validated BuildResult identity
```

The guest compiler path is implemented in guideXOS. `BareMetalBuildService` reads sources from VFS and calls `compile_project_incremental`; it does not create a process or invoke PowerShell, Clang, GCC, LLVM, `ld`, or LLD. The front end is in `kernel/core/compiler/compiler_lexer.cpp`, `compiler_parser.cpp`, and `compiler_module.cpp`; AMD64 lowering is in `kernel/arch/amd64/compiler_backend.cpp`; object handling, linking, and final ELF construction use the existing `compiler_object`, `compiler_linker`, and `elf_writer` modules.

The separate hosted `NativeBuildService` is host compilation. It resolves `clang++.exe` and `ld.lld.exe`, launches Windows PowerShell with the project build script using `CreateProcessW`, captures host stdout/stderr, and counts textual warning/error records. Host test executables also compile the production compiler C++ files with host `g++` and fake VFS implementations. Those tests exercise production compiler code but are not genuine guest execution evidence.

Project preparation in the guest build service validates the target profile, ABI, architecture, entry-point declaration, project manifest, artifact path, and registered source set. The compiler driver selects `.c`/`.cpp` sources, loads bounded quoted local declaration files through VFS, maps expanded diagnostics to their source locations, and reuses persistent objects only after validation. The linker is guideXOS's bounded linker; it is not LLD.

## Supported language matrix

“Implemented” describes the bounded contract below, not general ISO C or C++ support. Evidence combines production parser/backend code and the named tests; fixture text alone was not treated as proof.

| Area | Status | Verified contract and limits | Source and test evidence |
|---|---|---|---|
| Integer types and arithmetic | Partial | Signed 32-bit `int`, integer literals, unary minus, `+`, `-`, `*`, signed `/` and `%`, signed comparisons, and short-circuit `&&`/`||`. Division truncates toward zero and remainder follows the dividend sign. No general `char`, unsigned/wider integers, floating point, or bitwise arithmetic. | Lexer/parser/IR/AMD64 backend; bootstrap/functions tests; COMP4C signed arithmetic hosted execution and code-generation test; Phase 27I report. |
| Variables and assignments | Partial | Bounded locals and globals; scalar and indexed assignment; pointer and supported struct-field assignment; fixed storage limits. No general C declaration, cast, compound-assignment, or lifetime rules. | Parser, IR, backend; globals, arrays, pointers, structs tests. |
| Conditional statements | Implemented | `if`/`else` with integer conditions, comparisons, and short-circuit logical expressions. | Parser/backend; bootstrap and functions tests; Phase 27I report. |
| Loops | Implemented | `while`, `break`, and `continue`, under bounded nesting and generated-code limits. | Parser/backend; bootstrap/functions tests; Phase 27J report. |
| Functions and parameters | Partial | Multiple translation-unit functions, prototypes, up to 16 functions per unit and four parameters per function; bounded integer, pointer, struct-pointer, string-pointer, and guideXOS context forms. | Parser/module/linker; functions and multifile tests. |
| Function calls and returns | Partial | Direct declared/defined calls, argument validation, recursion with a bounded runtime stack guard, and signed `int` returns. Pointer and aggregate returns, function pointers, varargs, and general C++ call features are unsupported. | Parser/backend/linker; functions and multifile tests; Phase 27M report. |
| Pointers and address operations | Partial | Bounded `int*` and typed struct pointers; address-of, supported dereference, field `.`/`->`, and checked/scaled pointer arithmetic. Global pointer variables, pointer returns, casts, and unrestricted pointer escape are unsupported. | Parser/IR/backend; pointer and struct tests; Phase 27U report. |
| Arrays | Partial | Fixed-size integer arrays and arrays of structs, with bounded storage/indexing and checked pointer provenance. Array parameters and array fields are unsupported; this is not general C array decay. | Parser/backend/object/linker; arrays, struct-arrays, and pointer tests; Phase 27U report. |
| Structures | Partial | Bounded named structs with integer fields, addressable fields, arrays of structs, and typed struct pointers. Nested/aggregate fields, by-value parameters/returns, and whole-struct assignment are unsupported. | Parser/object/linker/backend; structs and struct-arrays tests; Phase 27T/27U reports. |
| Global and static storage | Partial | Mutable integer, integer-array, and struct globals; supported `extern` declarations; persistent object symbols and relocations. The `static` storage-class keyword and global pointer variables are unsupported. | Parser/module/object/linker; globals, object, and multifile tests; Phase 27P report. |
| Character and string literals | Partial | Bounded printable string literals with `\\n`, `\\\\`, and `\\"` escapes for supported host calls. Character literals are not tokenized as values; `char*` is a narrow recognized ABI type, not a general character type. | Lexer/parser/backend; bootstrap/functions tests; Phase 27X report. |
| C and freestanding C++ constructs | Partial | `.c` and `.cpp` inputs use the same bounded C-like grammar. Quoted local declaration includes, comments, prototypes, and the constructs above are supported. This is not general C/C++; macros, angle includes, STL/`std::`, classes, templates, namespaces, exceptions, RTTI, and general preprocessing are unsupported. | Build service, driver include loader, lexer/parser; multifile/object tests; Phase 27V report. |

The guest target is AMD64, `guidexos-c-abi-v1`, `gx_main`, and static NativeElf. The host compiler tests are compiled on the host and cannot establish guest runtime behavior or a different target backend.

## Diagnostic contract and changes

### Before COMP4A

The compiler already preserved source locations and distinguished broad driver stages such as source read, front end, object generation, linker, ELF writer, artifact validation, and publication. However, lexing, syntax, semantic/type errors, unsupported constructs, and backend rejection could all arrive as `Frontend`; the guest Build API returned `GX_BUILD_ERROR_COMPILER_FAILED` for every compiler pipeline failure. The kernel build service also left `warningCount` at zero even though the guest compiler has no warning-record/counting path.

### After COMP4A

- Diagnostics carry a deterministic kind: project validation, source read, lexical, syntax, unsupported construct, semantic/type, resource limit, code generation, object generation, linker, artifact, internal, or unknown.
- The driver maps known failures to precise stages while retaining the old broad `Frontend` value as a fallback. Backend rejection now reports `CodeGeneration`.
- Build output preserves GCC-style source locations and adds a bounded category marker, for example `src/main.cpp(1,42): error: [syntax] ...`. Linker errors retain the linker prefix and location contract.
- The guest Build API appends specific error values for source read, lexical, syntax, unsupported language, semantic, resource limit, code generation, object, linker, ELF writer, artifact validation/publication, and compiler internal failures. Existing values 0–20 and the `gx_build_snapshot` layout/API version remain unchanged. `GX_BUILD_ERROR_COMPILER_FAILED` remains the last-resort fallback for an unclassified failure.
- `GX_BUILD_COUNT_UNAVAILABLE` (`0xFFFFFFFF`) marks warning counts the guest backend cannot report. The hosted service continues to report its counted host diagnostics. The Build API snapshot layout is unchanged.
- Diagnostic messages remain bounded to 127 bytes plus NUL; each compiler diagnostic set is capped at 16 entries. Build output remains bounded by the existing 32 lines of 256 bytes.

Errors are still errors; no warning records are synthesized. Failure status maps to the compiler stage/category rather than implying that all failures were generic compiler failures.

## Build/Run integration validation

The compiler still reports success only after it writes the final ELF through VFS, closes/reopens it, verifies readback identity, and passes the existing ELF checks. `BareMetalBuildService` then independently re-reads the artifact and runs NativeElf validation before setting `artifactValid`, path, size, entry point, and SHA-256.

The incremental driver intentionally retains the last successful ELF when a later compile fails. A failed current `BuildSnapshot` has no validated artifact identity; `BuildJobManager::complete_synchronous_job` rejects a success result without source membership and a validated artifact; Run preparation requires the successful BuildResult identity and revalidates the file size, hash, manifest, entry point, and NativeElf image immediately before launch. Existing Phase 27W source evidence covers compiler/link failures blocking Run while the prior ELF remains on disk. COMP4A hosted driver tests additionally assert that a failed attempt has no success flag, new output size/hash, or reopened hash and does not overwrite the prior artifact bytes.

Build generation/owner handles and source membership remain under the existing job manager. No Build/Run struct layout or run-owner contract was changed. The existing Run gating smoke was updated to expect the new linker-specific error code.

The guest Build call is still synchronous: `BareMetalBuildService::start_owned` executes the compiler before returning. The bounded single active job and owner lifecycle remain intact, but independent Build progress polling and cancellation are not implemented. A dedicated COMP4B responsiveness/cancellation phase should address cooperative build steps, progress/stage reporting, cancellation boundaries, and cleanup of owned compiler work without changing Debugging or Tools ownership.

## Hosted test evidence

All 12 hosted compiler suites passed using isolated output directories under the system temporary directory:

1. `run-compiler-driver-diagnostics-host-test.ps1` — simple/multi-file success, valid calls, source-read/lexical/syntax/unsupported/semantic classifications, link failure, artifact validation failure, source mapping, prior-artifact preservation, repeated incremental builds, and bounded diagnostics.
2. `run-compiler-build-job-manager-host-test.ps1` — Build API/job ownership, generation, stale handles, terminal result, and warning-count-unavailable propagation.
3. `run-compiler-bootstrap-host-test.ps1`.
4. `run-compiler-arrays-host-test.ps1`.
5. `run-compiler-functions-host-test.ps1`.
6. `run-compiler-globals-host-test.ps1`.
7. `run-compiler-pointers-host-test.ps1`.
8. `run-compiler-structs-host-test.ps1`.
9. `run-compiler-struct-arrays-host-test.ps1`.
10. `run-compiler-object-host-test.ps1`.
11. `run-compiler-multifile-host-test.ps1`.
12. `run-compiler-stack-frame-bound-test.ps1`.

Additional compile checks passed for `compiler_build_service.cpp` and `native_build_service.cpp` with host `g++` syntax-only compilation. The hosted service check emitted one existing enum/non-enum conditional warning at `native_build_service.cpp:731`; it is unrelated to COMP4A.

No QEMU boot or genuine guest Build/Run was performed for COMP4A. Historical guest proofs remain recorded in the Phase 27B/27E/27U/27W/27X/27Y/27Z documents, but they are not new COMP4A qualification. Developer Studio Debugging Phase 30B remains separately owned and unaccepted.

## Remaining deficiencies and next milestone

- The language is a bounded bootstrap subset, not a C or C++ implementation. It lacks general preprocessing, full declarations/type conversions, many scalar types, unsigned and wider integer arithmetic, bitwise arithmetic, static storage, global pointers, array parameters, aggregate assignment, and standard-library support.
- The guest backend is AMD64-only. No general relocatable linking, external libraries, or arbitrary OS-hosted compiler invocation exists in guest runtime.
- Diagnostic classification is precise for the explicitly tagged front-end paths and existing pipeline stages; unclassified internal conditions intentionally retain the generic fallback.
- The guest compiler has no warning generation/counting capability; consumers must treat `GX_BUILD_COUNT_UNAVAILABLE` as unavailable, not as zero.
- Guest Build responsiveness, progress, and cancellation remain future work because the current build call runs synchronously.
- The COMP4A diagnostic categories and Build error-code mapping still need genuine guest/QEMU qualification.

**Recommended COMP4B:** guest Build responsiveness and cancellation, beginning with a dedicated bounded cooperative Build job lifecycle and explicit progress/cancellation contracts, followed by a fresh guest Build/Run regression. Keep this work separate from Debugging and Tools ownership.

## Git closeout

The COMP4A implementation, report, hosted tests, and final commit are recorded by the task's completion summary. No push was performed.
