# Developer Studio COMP4M — File-Scope Byte Arrays

- **Outcome:** `COMP4M_HOSTED_ACCEPTED`
- **Qualification:** hosted only; no QEMU launch and no QAC admission.
- **Repository:** `D:\dev\guideXOSServerV0.5_DEVELOPER_STUDIO`
- **Branch:** `v0.5_DEVELOPER_STUDIO`
- **Starting HEAD:** `c77f18191a59abba76c3a84c48bf99850f2b3f0a`
- **Starting upstream divergence:** 0 behind / 5 ahead.
- **Ending validated implementation HEAD:** `a0024771af2c97a8962324896a52d6cff93ac0f2`
- **Ending implementation divergence:** 0 behind / 6 ahead.
- **Predecessor:** `COMP4L_HOSTED_ACCEPTED` (`d080d37c`).

## Scope and preflight

The task started on the required repository and branch at the expected full HEAD. The tracked worktree was clean. Existing untracked output directories from earlier phases were present and preserved; COMP4M outputs were written under `out/comp4m-validation/`. No branch switch, merge, rebase, reset, stash, clean, history rewrite, push, QEMU launch, or QAC admission was performed. Developer Studio Debugging and Tools worktrees were not touched.

The COMP4A–COMP4L reports and implementation history were reviewed. COMP4B, COMP4C, COMP4E, COMP4G, COMP4H, COMP4I, COMP4J, and COMP4L report hosted acceptance; COMP4D reports its earlier guest acceptance; COMP4F and COMP4K remain hosted-ready with guest qualification pending. COMP4D's intermittent VFS directory-creation observation remains documented as non-reproduced; COMP4M made no VFS repair.

The current compiler object format is version 2 and the internal compiler object ABI was 16 at COMP4M start. The starting branch contained five local commits beyond upstream. Pre-existing untracked `out/` and `kernel/out/` artifacts were left in place.

## Existing global-array audit

Before COMP4M, the production parser and AMD64 backend supported fixed integer globals and arrays, file-scope structures and structure arrays, and local signed and unsigned byte arrays. Local byte arrays already used one byte per element, signed loads used `MOVSX`, unsigned loads used `MOVZX`, and stores used the low byte. The parser's file-scope declaration path rejected `char` and `unsigned char` arrays, indexed global access recognized only integer arrays, and `flatten_global_data` wrote each non-structure global element at a four-byte stride. The object and linker data-signature validators likewise treated non-structure arrays as four-byte integer elements.

The existing global storage path was reusable: global addresses already use the 64-bit data-address relocation, module objects serialize global exports/imports and mutable data, the linker resolves data symbols across modules, and the ELF writer places mutable data in a writable `PT_LOAD`. COMP4M extends those paths with one-byte array signatures and offsets. It does not add a new storage section, allocation model, or linkage system.

## Supported semantics

Declarations such as `unsigned char buffer[16];` and `char message[32];` create persistent, mutable global arrays. The supported dimension is a positive integer constant from 1 through 256. Each element occupies exactly one byte; the data is contiguous and has alignment 1. Existing integer globals and arrays retain four-byte alignment. The `extern` form is supported for byte arrays when definition and declaration match in element type, signedness, count, size, and alignment.

All arrays without initializers begin as zero bytes. The module compiler resets its output object and `flatten_global_data` writes every byte-array element into mutable data. Arrays are not allocated in function frames, and a function call does not reset their contents. A 32-byte module-symbol limit and an 8,192-byte mutable-data capacity still apply to all globals combined.

Global byte-array reads and writes use the existing bounded indexed-access grammar. Signed `char` reads sign-extend; `unsigned char` reads zero-extend; integer assignments truncate to the low eight bits. Consecutive elements are addressed at byte stride. Constant negative or out-of-range indexes are rejected at their source location. Runtime indexes compare the signed index with the array extent before accessing memory. The hosted runtime-bounds fixture executed an out-of-range dynamic read and verified it returned through the bounds-failure path without reading or changing the adjacent sentinel.

Global `unsigned char` arrays can decay to the existing bounded `unsigned char*` descriptor and can be passed to a supported function parameter. The descriptor retains global-array provenance and the full byte extent; pointer-width relocations and existing runtime descriptor checks remain in use. Hosted execution verified a global array pointer passed into a function, byte indexing, mutation, and an out-of-range dereference that did not reach the neighboring scalar. Signed `char` arrays do not decay to byte pointers. Global pointer variables, pointer returns, arbitrary casts, and general C pointer safety remain unsupported.

Cross-module access is supported through matching `extern` declarations. `DataCharArray` and `DataUnsignedCharArray` symbols preserve signedness in the object metadata; element count and size are checked by the linker. The hosted suite verified read/write access across serialized modules, signedness and extent mismatches, and duplicate definitions. This remains the compiler's bounded existing `extern` mechanism, not complete C external-linkage semantics.

## Initialization, layout, and ELF behavior

The primary execution fixture emitted this module data layout:

| Object | Offset | Size | Alignment |
|---|---:|---:|---:|
| `unsigned char data[4]` | 0 | 4 | 1 |
| `int before` | 4 | 4 | 4 |
| `int after` | 8 | 4 | 4 |

The cross-module fixture also placed `unsigned char shared[4]`, `char signedShared[2]`, and an `int` guard in non-overlapping storage. The compiler's capacity check validates the remaining data capacity before advancing offsets; bounded dimensions keep per-array multiplication small. A constructed 8,960-byte aggregate was rejected with a source-located resource-limit diagnostic against the 8,192-byte module cap.

The object writer serializes zero bytes in the mutable `.data` payload. The production ELF writer emitted the fixture's mutable bytes in a writable `PT_LOAD`; the validated segment had `p_filesz == p_memsz == 12`, including the zero-initialized bytes. This output has no separate `.bss` range for these arrays. The NativeElf loader clears the mapped image before copying each file-backed `PT_LOAD` range, and applies the segment's writable permission. The hosted execution harness followed the relevant storage contract by zeroing the RW mapping, copying the final ELF segment bytes, applying the production linker's data relocations, and executing production AMD64 code against that mapping.

Generated AMD64 inspection found these byte operations:

- Signed read: `MOVSX EAX, byte ptr [RDX]` (`0F BE 02`).
- Unsigned read: `MOVZX EAX, byte ptr [RDX]` (`0F B6 02`).
- Byte write: `MOV byte ptr [RDX], AL` (`88 02`).

The final ELF was produced with the production object writer, linker, ELF writer, and validator, then executed on the host. This is hosted evidence for the production storage and code-generation path; it is not guest-loader qualification.

## Object compatibility

The public object format remains version 2. The internal compiler object ABI advances from 16 to 17 because the object symbol vocabulary now includes signed and unsigned byte-array kinds, and old signatures cannot represent their element width and signedness. Export/import deserialization accepts the new appended enum values and validates one-byte element size, exact element count and size, and alignment 1. Deterministic serialization and round-trip linking passed. A checksum-valid ABI-16 object was rejected by the ABI-17 compiler. Public Build API and NativeElf host-call ABI layouts are unchanged.

The COMP4K prepared smoke check was updated from ABI 16 to ABI 17. All later guest acceptance must freshly rebuild relevant compiler objects using ABI 17; ABI-15 and ABI-16 cached objects are incompatible.

## Unsupported forms and diagnostics

File-scope byte-array initializers are outside this phase. Brace lists, scalar-form initializers, and string initializers receive source-located unsupported diagnostics. The parser also diagnoses zero and negative dimensions, dimensions above 256, malformed declarations, unsupported element types, invalid array assignments, duplicate definitions, and statically provable out-of-range indexes. The suite exercised a 65,535-element declaration and an aggregate-storage overflow to prove oversized input is rejected before compiler-managed storage can wrap.

The global-count limit remains 32. Other existing limits remain unchanged, including at most 16 functions per translation unit, four parameters per function, 64 local array elements, the existing frame/code-generation bounds, and 8,192 bytes of module mutable data. No dynamic allocation, general C array initialization, static-storage class support, global constructors, or broader module/linkage model was added.

## Hosted qualification results

The dedicated production-compiler suite is `tests/compiler_global_byte_arrays_comp4m_host_test.cpp`, invoked by `scripts/run-compiler-global-byte-arrays-comp4m-host-test.ps1`. It passed **63 assertions** with **zero compiler warnings**. It covered:

1. Zero initialization for signed and unsigned byte arrays.
2. Indexed writes and reads, including unsigned values 128–255 and signed negative promotion.
3. Integer-to-byte truncation, consecutive elements, multiple global arrays, and adjacent integer sentinels.
4. Repeated mutation through helper calls and retrieval after calls.
5. Global byte arrays alongside existing integer globals.
6. `unsigned char*` decay, descriptor provenance, supported parameter passing, and global-pointer bounds failure.
7. Cross-module access after object serialization, with signedness, extent, and duplicate-definition rejection.
8. Invalid dimensions, malformed declarations, unsupported element types and initializers, invalid assignments, constant out-of-range indexes, maximum dimensions, and aggregate storage overflow.
9. ABI-17 object round trip, deterministic bytes, ABI-16 cache rejection, production ELF generation/validation, emitted AMD64 operations, and host execution against ELF RW data.

The existing COMP4A–COMP4L hosted regression set passed **19/19 runner invocations**, representing **21 suites** because the unsigned-character wrapper runs three component suites. It included arrays, bootstrap, Build job management, cooperative Build lifecycle, driver diagnostics, functions, globals, multi-file linking, objects, pointers, signed arithmetic, stack bounds, structures, structure arrays, character/byte suites, unsigned-byte pointers, and Native ABI layout. The COMP4L dedicated global-scalar suite passed 31 assertions with its ABI expectation updated to 17. Failed-Build artifact preservation passed in the driver-diagnostics and cooperative Build-service tests.

The 19 runner logs in `out/comp4m-validation/regressions/` report **zero warnings and zero failures**. The QAC helper/source test passed **35 assertions** in `out/comp4m-validation/qac-source/`; this only validates local acceptance helpers and source markers. It did not request QAC admission or start QEMU. Results are summarized in `out/comp4m-validation/regression-results.csv`.

## Guest qualification and COMP4N

COMP4M is accepted for hosted qualification only. No guest result is claimed. COMP4K remains `COMP4K_HOSTED_READY_GUEST_PENDING`, and .NET Support retains the QEMU baton. Add global byte arrays to the eventual guest campaign: zero initialization, signed and unsigned access, repeated cross-function mutation, adjacent-global integrity, writable ELF storage, global-array `unsigned char*` provenance, and matching cross-module declarations. That campaign must rebuild all relevant objects against ABI 17. The COMP4D intermittent VFS observation remains unchanged and unrepaired.

Recommended COMP4N scope: add a small bounded byte-buffer utility set (`memcpy`, `memset`, `memcmp`, and `strlen`) over the established byte-array and unsigned-byte-pointer subset. Keep dynamic allocation, global constructors, general C array initialization, and unrestricted pointer escape outside that scope.

## Changed files and Git closeout

The implementation commit changed:

- `docs/DEVELOPER_STUDIO_COMP4A_GUEST_COMPILATION_CONTRACT_AND_DIAGNOSTIC_FIDELITY.md`
- `docs/DEVELOPER_STUDIO_COMP4K_CONSOLIDATED_GUEST_COMPILER_QUALIFICATION.md`
- `kernel/core/compiler/compiler_ir.h`
- `kernel/core/compiler/compiler_linker.cpp`
- `kernel/core/compiler/compiler_module.cpp`
- `kernel/core/compiler/compiler_object.cpp`
- `kernel/core/compiler/compiler_parser.cpp`
- `kernel/core/native_elf/native_elf_smoke.cpp`
- `scripts/run-compiler-global-byte-arrays-comp4m-host-test.ps1`
- `scripts/smoke-compiler-bootstrap.ps1`
- `tests/compiler_character_bytes_host_test.cpp`
- `tests/compiler_global_byte_arrays_comp4m_host_test.cpp`
- `tests/compiler_global_storage_comp4l_host_test.cpp`
- `tests/compiler_unsigned_byte_pointers_host_test.cpp`
- `tests/test-developer-studio-qac-acceptance.ps1`

Validated implementation is committed locally as `a0024771af2c97a8962324896a52d6cff93ac0f2` (`feat: add COMP4M global byte arrays`). The COMP4M report is a separate documentation closeout. No push was performed. Existing phase output directories were preserved; all new validation output remains under `out/comp4m-validation/`.
