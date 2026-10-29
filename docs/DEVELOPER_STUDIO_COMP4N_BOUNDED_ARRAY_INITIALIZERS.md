# Developer Studio COMP4N — Bounded Global Byte-Array Initializers

- **Outcome:** `COMP4N_HOSTED_ACCEPTED`
- **Qualification:** hosted only; no QEMU launch and no QAC admission.
- **Repository:** `D:\dev\guideXOSServerV0.5_DEVELOPER_STUDIO`
- **Branch:** `v0.5_DEVELOPER_STUDIO`
- **Starting HEAD:** `b341c6ac85aee1cbca9efc78f92fef87352ba6d6`
- **Starting upstream divergence:** 0 behind / 0 ahead.
- **Ending validated implementation HEAD:** `4025b328837b44aaf50343b5749f78fa4e6108e3`.
- **Predecessor:** `COMP4M_HOSTED_ACCEPTED`.

The implementation is committed locally. The report is a separate documentation closeout commit. The completion summary records the final repository HEAD after that closeout.

## Preflight and scope

The requested repository, branch, and starting HEAD matched. The tracked working tree was clean; the branch was even with upstream. Existing untracked artifacts under `out/` and `kernel/out/` were preserved. There was no `AGENTS.md` or phase marker in this checkout. No other checkout, branch, remote, QEMU instance, or QAC admission was used.

The COMP4M implementation and report, compiler parser, IR, module builder, object serializer/deserializer, linker, ELF writer, NativeElf loader contract, hosted test runners, and existing compiler regression outputs were reviewed before implementation. The public object format was version 2 and internal compiler object ABI was 17 at both start and end.

## Existing initializer and storage path

Before COMP4N, file-scope scalar `int`, `char`, and `unsigned char` initializers accepted bounded integer and character-literal constants. Fixed integer arrays already used a bounded brace-list parser. `parse_global_initializer_value` already handled integer literals, unary-negative integer literals, and supported character literals. Its values were stored in `GlobalSymbolIR.initialValues`.

COMP4M added one-byte signed and unsigned global arrays, indexed access, distinct serialized symbol kinds, cross-module declarations, and zero initialization. Its parser rejected byte-array initializers before the existing list parser. `flatten_global_data` wrote scalar and array values into the module's mutable data image; object serialization stored those bytes, and the linker placed the mutable image in the final writable ELF `PT_LOAD`.

The initializer values themselves are parser IR only. No initializer metadata is serialized. The persistent object already carries typed array symbols and final mutable bytes, so this phase required no object schema or ABI change.

## Supported grammar and conversion

File-scope signed and unsigned byte arrays accept bounded constant lists such as:

```c
unsigned char values[4] = {1, 2, 3, 255};
char letters[4] = {'A', 'B', 'C', '\0'};
char signed_data[3] = {-1, 127, 128};
unsigned char unsigned_data[3] = {-1, 255, 256};
```

The grammar accepts supported integer literals, unary-negative integer literals, and supported character literals. Whitespace is ignored by the existing lexer. Empty lists and a trailing comma are supported. Lists cannot contain more values than the declared bound.

Each array element stores exactly the low eight bits of its initializer value. This preserves the existing byte narrowing rules: `char` reads sign-extend stored bytes and `unsigned char` reads zero-extend them. Thus the signed example reads `-1, 127, -128`; the unsigned example reads `255, 255, 0`. Existing integer promotion behavior is unchanged, and generated code uses the established signed `MOVSX` and unsigned `MOVZX` loads.

An omitted initializer list, an empty list, and every omitted tail element produce zero bytes. For example, `{10, 20, 30}` in an eight-element array stores `10, 20, 30, 0, 0, 0, 0, 0`. Arrays remain mutable after startup. The compiler emits no runtime initialization code for these constants.

## Bounds, diagnostics, and unsupported forms

The existing per-array limit remains 256 elements, the translation-unit global limit remains 32 objects, and all module globals share the existing 8,192-byte mutable-data cap. Size multiplication remains bounded by the validated 256-element count and one-byte element size. The hosted test reached exactly 8,192 bytes and rejected an 8,196-byte aggregate as a source-located resource-limit error. A maximum-size 256-value initializer compiled in order; 257 values were rejected at the first excess element.

The parser rejects zero, unsized, oversized, and very large dimensions before storage layout. Tests also cover missing separators, unterminated lists, malformed character escapes, scalar-form and string-form array initialization, runtime calls, runtime-dependent variables, arithmetic expressions, nested lists, and excess values. Diagnostics remain source-located and retain lexical, syntax, unsupported, and resource-limit categories. Diagnostic count and message size remain bounded by the existing compiler limits.

Local byte-array initialization is deferred. General C99 aggregate initialization, arrays of structs with initializers, nested lists, string-array initialization, arbitrary arithmetic constant expressions, dynamic initialization, constructors, casts, and a new constant-expression evaluator were not added.

## Data, objects, linker, and ELF

`compile_module_from_source` clears the destination module before building it. Global flattening now writes each explicit array element from the initializer IR and explicitly writes zero for every omitted element. Uninitialized arrays therefore remain deterministic even if the caller's previous module buffer contained nonzero bytes.

The production storage test verified this 36-byte image and the expected offsets: the initial `int` is at 0; byte arrays are contiguous at offsets 4 through 31; the trailing `int` is aligned at 32. The initialized bytes, omitted zeros, padding, and adjacent integer sentinels matched byte-for-byte. No initialized and uninitialized object ranges overlap.

The production object writer and reader round-tripped the initialized bytes and signed/unsigned array symbols. Repeated serialization was byte-deterministic. Object format remains version 2 and internal compiler object ABI remains 17. ABI 17 already includes `DataCharArray` and `DataUnsignedCharArray`, and its object `.data` payload already represents initialized mutable bytes. No new persistent metadata, symbol kind, relocation, or object layout was introduced. ABI-16 cached objects remain incompatible and are rejected by the existing identity check. Public Build API and NativeElf host-call layout are unchanged.

The production linker resolved initialized provider arrays against matching `extern` declarations. The referencing module allocated no duplicate storage; the linked program kept one signedness-aware export and correct data relocations. Hosted execution observed the initial provider bytes, updated shared elements across functions, and validated deterministic diagnostics for an incompatible signedness declaration.

The production ELF writer and validator placed the complete initialized image in the writable, file-backed `PT_LOAD` (`p_filesz == p_memsz == 36` for the primary fixture). The hosted execution harness mapped that segment, copied its ELF bytes, applied the production linker's global-data relocations, and executed the production AMD64 code. The entry returned 42, and the post-run data showed persistent byte mutations with both adjacent integer sentinels intact. This is hosted evidence, not guest-loader qualification.

## Hosted qualification

The dedicated production-compiler suite is `tests/compiler_global_byte_array_initializers_comp4n_host_test.cpp`, invoked by `scripts/run-compiler-global-byte-array-initializers-comp4n-host-test.ps1`. It passed **47 assertions** with no compiler warnings. It checks full, partial, and empty initialization; integer and character literals; trailing commas; element order; signed and unsigned conversions and load instructions; exact storage bytes, alignment, bounds, and adjacent globals; repeated mutation and typed pointer interoperability; object round-trip and deterministic serialization; cross-module exports, imports and relocations; ELF bytes and validation; hosted execution; malformed and unsupported forms; maximum counts and total storage limits.

The cooperative production Build-service suite was extended to build an initialized byte array successfully and to reject an overfull initializer while preserving the previously published artifact. That suite passed.

The serial COMP4A–COMP4M hosted regression matrix passed **21 suites across 19 runner invocations**. With the new suite, the final matrix passed **22 suites across 20 runner invocations**, with **0 failures**. The Build-service test translation unit produced two existing `-Wunused-function` compiler warnings for helper functions inherited from its included diagnostic test source; the other runner logs had no warnings. The QAC source/helper assertions passed **35 checks**. These assertions did not request QAC admission.

Regression outputs and the per-runner result table are preserved under `out/comp4n-validation/regressions/`; the dedicated final suite output is under `out/comp4n-validation/regressions/comp4n-global-byte-array-initializers/`.

## Guest backlog and preserved observations

No QEMU was launched and no QAC admission was requested. .NET Support retains primary QEMU priority. COMP4K remains `HOSTED_READY_GUEST_PENDING`. The consolidated guest backlog now includes COMP4N's initialized and zero-initialized signed and unsigned arrays, list order, empty and partial zero-fill, narrowing and loads, repeated writes, adjacent storage, final ELF bytes, pointer provenance, and cross-module definitions. The COMP4K fixtures and all cached compiler objects must be rebuilt against ABI 17 before future guest qualification.

The previously documented COMP4D observation—one intermittent `VFS_ERR_NOT_SUPPORTED` while creating `/P27W/build/obj/amd64`, which did not recur—remains unchanged. COMP4N made no speculative VFS repair.

## Recommended COMP4O scope

Consider a small freestanding byte/string utility subset over the established bounded `unsigned char*` representation: `memcpy`, `memset`, `memcmp`, and `strlen`. Keep dynamic allocation, global constructors, general aggregate initialization, and unrestricted pointer escape outside that scope. COMP4O was not started.

## Changed implementation files

- `kernel/core/compiler/compiler_parser.cpp`
- `kernel/core/compiler/compiler_module.cpp`
- `tests/compiler_global_byte_array_initializers_comp4n_host_test.cpp`
- `scripts/run-compiler-global-byte-array-initializers-comp4n-host-test.ps1`
- `tests/compiler_global_byte_arrays_comp4m_host_test.cpp`
- `tests/compiler_build_service_cooperative_host_test.cpp`
- `docs/DEVELOPER_STUDIO_COMP4A_GUEST_COMPILATION_CONTRACT_AND_DIAGNOSTIC_FIDELITY.md`
- `docs/DEVELOPER_STUDIO_COMP4K_CONSOLIDATED_GUEST_COMPILER_QUALIFICATION.md`

The implementation commit is local and unpushed. No unrelated tracked changes were present, and all pre-existing untracked artifacts were preserved.
