# Developer Studio COMP4L — File-Scope Global Storage

**Outcome:** `COMP4L_HOSTED_ACCEPTED`

**Qualification:** Hosted only. No QEMU launch, QAC admission, or human guest test was performed.

## Git and preflight

- Repository: `D:\dev\guideXOSServerV0.5_DEVELOPER_STUDIO`
- Branch: `v0.5_DEVELOPER_STUDIO`
- Starting HEAD: `a5cfdbda6e8474116cdb902e62debb2276b6fc8e`
- Ending implementation HEAD: `d080d37c` (`feat: add COMP4L file-scope byte globals`)
- Starting upstream divergence: 0 behind / 3 ahead.
- The tracked worktree was clean at preflight. Existing untracked `out/` and `kernel/out/` artifacts were preserved. COMP4L validation output is under `out/comp4l-validation/`.
- The starting compiler object format was version 2 and internal object ABI was version 15.

The required report is committed in a documentation-only closeout commit after the implementation HEAD above. The final repository HEAD and final upstream divergence are recorded in the task completion summary.

## Existing storage audit

Before COMP4L, the production parser and AMD64 backend already handled mutable file-scope `int` globals, bounded `int` arrays, and global structures/structure arrays. The object pipeline serialized data symbols and relocations, the project linker resolved them across compilation units, and the ELF writer placed mutable data in a writable load segment. Uninitialized integer storage was represented by zero bytes in compiler-owned mutable data.

Character support already existed for local values, byte arrays, function signatures, and mixed-width structure fields. File-scope `char` and `unsigned char` scalars were rejected. The global data flattener aligned each object as a 4-byte value, generated global reads as 32-bit loads, and generated stores as 32-bit writes. Object data signatures did not distinguish signed and unsigned byte globals. Those gaps meant parser acceptance alone could not provide correct byte storage or cross-module type identity.

COMP4L keeps the existing integer and structure paths. It adds byte-sized global layout, byte-aware code generation, distinct object symbol kinds, and source-located rejection for unsupported storage forms.

## Supported declarations and initialization

The bounded file-scope contract is:

| Declaration | Result |
|---|---|
| `int value;` | 4-byte aligned, zero-initialized scalar |
| `int value = -27;` | 4-byte scalar initialized from one integer literal |
| `int values[8];` | Existing bounded integer array support, zero-initialized |
| `char state;` | 1-byte aligned, zero-initialized signed byte |
| `char state = 'A';` | One byte initialized from an integer or ordinary character literal |
| `unsigned char flags = 255;` | One byte initialized from an integer or ordinary character literal |
| `extern` declaration of a supported scalar | Imports the typed global symbol for linking |

Byte initialization stores the low eight bits. Reads promote signed `char` with sign extension and `unsigned char` with zero extension. Assignments write one byte, so neighboring global values are not overwritten. Initializer expressions, runtime-dependent initialization, pointer initializers, file-scope `static`, function-local `static`, and file-scope byte arrays are explicitly unsupported. Byte arrays remain an optional extension and are diagnosed at the array declarator.

An omitted initializer produces explicit zero bytes in compiler-owned storage. Integer globals and arrays remain four-byte aligned; byte scalars use one-byte alignment. The mixed-width execution fixture placed its globals at offsets 0, 4, 5, 6, 7, 8, 12, 13, and 16, for a 20-byte data image.

## Data sections, AMD64 access, and linking

`flatten_global_data` builds bounded mutable data. The production ELF writer emits it in a writable `PT_LOAD`; COMP4L verified `p_filesz == p_memsz == 20` for the fixture and compared all file-backed bytes, including zero-initialized values. The tested image has no `.bss`-style zero-fill range: the zeros are present in the mutable data bytes. The hosted execution harness maps the final ELF RW segment into writable host memory, loads its file bytes, applies the program's address relocations, and executes the production AMD64 code.

Global addresses continue through the existing object relocation and linker path. AMD64 byte reads use signed `movsx eax, byte ptr [rdx]` (`0F BE 02`) or unsigned `movzx eax, byte ptr [rdx]` (`0F B6 02`); byte stores use `mov byte ptr [rdx], al` (`88 02`). Integer accesses retain their existing 32-bit operations. The hosted fixture executed global reads from helper functions, repeated mutations, truncating stores, and cross-module accesses.

Duplicate definitions and declaration signature mismatches are rejected by the production linker. Signed and unsigned byte globals carry distinct `DataChar` and `DataUnsignedChar` object symbol kinds, so their identity and width survive serialization and linking. The object file format remains version 2. The internal compiler object ABI advances from 15 to 16 so old cached objects are rejected; a checksum-valid ABI-15 object was explicitly rejected. Public Build API and NativeElf host-call ABI layouts are unchanged.

## Static-storage boundaries

COMP4L does not add function-local static lifetime, internal-linkage name mangling, global constructors, runtime initialization, thread-local storage, global pointers, general C constant-expression evaluation, or byte-array globals. File-scope and function-local `static` declarations receive explicit unsupported diagnostics. These forms remain separate language and runtime work.

## Hosted qualification

The dedicated production-compiler test is `tests/compiler_global_storage_comp4l_host_test.cpp`, run by `scripts/run-compiler-global-storage-comp4l-host-test.ps1`. It passed **31 assertions** covering:

- Existing integer global behavior, initialized and zero-initialized scalar data, byte offsets and alignment.
- Signed and unsigned byte initialization, signedness, `128`/`255` boundaries, low-byte truncation, repeated mutation, and neighboring sentinels.
- Deterministic object serialization, format-2/ABI-16 metadata, and ABI-15 cache rejection.
- Writable file-backed ELF data, generated AMD64 byte operations, and execution of the linked final ELF against that writable segment.
- Cross-module byte definitions/declarations, typed access, mutation, duplicate definitions, and type mismatch rejection.
- Unsupported runtime and expression initializers, byte arrays, both `static` forms, and the existing global-count limit.

The existing hosted regression matrix finished with **19/19 suites passing**: 17 runner invocations, consisting of 16 direct suites and one unsigned-character wrapper that ran three component suites. It includes compiler globals, arrays, functions, structures and structure arrays, pointers, cross-module compilation, objects, signed arithmetic, stack bounds, bootstrap, diagnostic handling, Build job management, cooperative Build lifecycle/publication rollback, and NativeElf ABI layout. The failed-Build/artifact-preservation behavior remains covered by the existing driver-diagnostic and cooperative Build suites.

The QAC acceptance helper/source checks passed **35 assertions**; they did not perform admission or launch QEMU. The new COMP4L hosted test emitted no compiler warnings. Two existing `-Wunused-function` warnings came from `tests/compiler_driver_diagnostic_host_test.cpp` lines 252 and 258 during the hosted regression run. There were **zero build errors**.

The maintained compiler capability matrix and COMP4I/COMP4J/COMP4K reports were updated for file-scope byte globals and ABI 16. COMP4K remains `COMP4K_HOSTED_READY_GUEST_PENDING`; its guest campaign must rebuild fresh ABI-16 objects. The COMP4D intermittent VFS observation remains recorded as before; COMP4L made no speculative VFS repair.

## Remaining guest qualification

No guest acceptance is claimed. A future QEMU slot should rebuild COMP4K's prepared fixtures with ABI 16, then add COMP4L scalar global initialization, signed/unsigned loads, repeated mutation, cross-module access, and adjacent-data integrity to the consolidated guest campaign. .NET Support retains QEMU scheduling priority.

## Recommended COMP4M

I recommend a small bounded native memory/string library, beginning with `memcpy`, `memset`, `memcmp`, and `strlen` over the compiler's supported byte-pointer and string representations. Function-local static storage should remain a separate milestone with its own lifetime and initialization contract. COMP4M was not started.

## Commit closeout

The validated source, tests, and maintained-document updates are in `d080d37c`. The required report follows as a documentation-only commit. No push was performed.
