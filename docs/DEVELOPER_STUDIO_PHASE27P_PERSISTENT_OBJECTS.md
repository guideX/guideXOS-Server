# Developer Studio Phase 27P — Persistent Relocatable Objects

Phase 27P makes the in-OS Developer Studio compiler incremental. Source is
read from guideXOS VFS, compiled into persistent ELF64 relocatable objects,
reopened and validated from VFS, linked by the existing in-OS linker, and
executed through the existing NativeElf launch path.

## Previous boundary

Before Phase 27P, `compile_project` read each source into the kernel compiler,
created a bounded `CompiledModule` in memory, passed those modules directly to
`link_modules`, emitted the final NativeElf image, and validated the image by
closing and reopening the final ELF. The earlier object helper was a
guideXOS-specific bounded GXO byte format; it was not a standard ELF object,
and the production link still depended on the live `CompiledModule` values.

Phase 27P keeps that compiler and linker representation. It adds a
serialization boundary around it rather than creating a second compiler or
linker:

```text
VFS source
  -> compile_module_from_source
  -> serialize ELF64 ET_REL .o
  -> validate serialized bytes
  -> VFS write and close
  -> discard CompiledModule
  -> reopen and deserialize .o
  -> link_modules
  -> emit/validate/reopen final ELF
  -> NativeElf launch
```

The clean and changed-source paths both cross the reopen boundary. A newly
compiled module is published, the corresponding in-memory module is cleared,
and the object is loaded again before linking.

## Persistent object format

Objects are genuine little-endian ELF64 relocatable files:

```text
ELFCLASS64
ELFDATA2LSB
e_type   = ET_REL (1)
e_machine = EM_X86_64 (62)
```

The emitted section set is derived from the module contents:

| Section | Use |
| --- | --- |
| `.text` | Emitted only when the translation unit has code. Executable module text. |
| `.rodata` | String-literal data when present. |
| `.data` | Initialized mutable global data when present. |
| `.rela.text` | Emitted only when text relocations exist. |
| `.symtab` | Null symbol, local section symbols, and module globals/imports. |
| `.strtab` | Symbol names. |
| `.shstrtab` | Section names. |
| `.gx.meta` | Bounded guideXOS compiler/linker metadata not represented by standard ELF fields. |

No empty placeholder section is emitted. The current language has no separate
uninitialized-data representation requiring `.bss` in this object boundary.
The standard ELF header is 64 bytes; `.gx.meta` has a fixed 96-byte header.
All integer fields are written explicitly little-endian, with bounded offset,
count, size, alignment, and overflow checks.

The metadata header records the object format version, target architecture and
ABI, compiler-object ABI version, source size/hash/path, code/data sizes,
function/global/export/import/relocation counts, entry offset, and checksum.
The metadata payload preserves the existing compiler contract: recursion and
local-storage information, call graph, struct descriptors, exports/imports,
and relocation source locations.

## Symbols and relocations

The existing `CompiledModule` symbol model is represented directly in ELF:

- local `STT_SECTION` symbols identify emitted sections;
- defined functions are global `STT_FUNC` symbols in `.text`;
- defined globals are global `STT_OBJECT` symbols in `.data`;
- cross-file references are global undefined symbols with `SHN_UNDEF`;
- the existing `gx_main` entry remains the single authoritative entry model,
  with its entry flag retained in `.gx.meta` and its ELF function symbol.

Only relocation forms emitted by the current AMD64 backend are supported:

- `R_X86_64_PC32` for `CallRel32`, with the backend's `-4` addend;
- `R_X86_64_64` for read-only data addresses (`DataAddress64`);
- `R_X86_64_64` for global/undefined data addresses
  (`GlobalDataAddress64`).

The parser checks relocation section linkage, entry size, symbol indexes,
target section, type, addend, patch width, and patch range before producing a
`CompiledModule` for the existing linker.

## Naming, identity, and invalidation

For a project-relative source such as `src/math.cpp`, the build service uses:

```text
build/obj/amd64/src/math.o
```

The source-relative directory is preserved below `build/obj/amd64`, and the
source extension is replaced with `.o`. Directories are created through the
guideXOS VFS. The final artifact remains the project manifest's
`build/bin/amd64/<name>.elf` path.

Cache reuse is content based, not timestamp based. The build reads the current
source bytes and computes the existing bounded deterministic FNV-1a64 source
hash. An object is reusable only when all of these are true:

1. the object exists as a regular VFS file within the object-size bound;
2. ELF structure and all referenced ranges validate;
3. format version, target architecture, target ABI, and compiler-object ABI
   match the current compiler;
4. metadata checksum and required compiler metadata validate; and
5. persisted source-relative path, source byte count, and source hash match
   the current source.

FNV-1a64 is used here for the existing small deterministic identity/checksum
implementation; it is not presented as a cryptographic integrity mechanism.
The fixed bare-metal target/profile is the compilation mode for this phase,
and the explicit compiler-object ABI version covers code-generation contract
changes. A future object-producing semantic or code-generation change must
bump `COMPILER_OBJECT_ABI_VERSION` in
`kernel/core/compiler/compiler_ir.h`; a structural metadata/serialization
change must also bump `COMPILER_OBJECT_FORMAT_VERSION` as appropriate.

Missing, malformed, truncated, wrong-architecture, wrong-version, checksum-
invalid, or source-mismatched objects are treated as cache misses. The serial
path records bounded reasons such as `malformed ELF object` and `source
identity mismatch`.

## Build and failure behavior

The ordinary bare-metal Developer Studio build service now calls
`compile_project_incremental`; it is not a test-only compiler command. It
reports compact module status and counters while detailed ELF/cache
diagnostics remain in the serial path. Every successful project build relinks
the complete selected source set; Phase 27P optimizes selective compilation,
not necessarily relinking.

New objects are serialized and deserialized in memory before being staged to a
temporary sibling object path. The temporary bytes are written through VFS,
then the object is installed by rename when the destination is absent or by a
validated VFS overwrite when it already exists. A failed source compile or
object validation therefore does not install a new reusable object.

The build service no longer removes the previous final executable before
compilation. Compile, object-load, symbol-resolution, link, and generated-ELF
validation failures occur before the final artifact write, so the last valid
artifact survives the tested failure paths. FAT currently exposes a
non-replacing rename rather than an atomic replace operation; consequently the
existing final artifact write is performed only after successful link and
close/reopen validation, but replacement itself is not journaled atomically.
An atomic final-artifact replace belongs with a future VFS primitive.

## Phase 27P proof

The focused proof is in `native_elf_smoke.cpp` under
`GXOS_PHASE27P_SMOKE`. It writes a three-source project directly to guideXOS
VFS, invokes `BareMetalBuildService::start/poll/release` for each transition,
and launches the generated artifact with NativeElf. The proof does not invoke
clang, GCC, `ld`, `lld`, PowerShell compilation, or the standalone Developer
Studio repository.

The authoritative QEMU command was:

```powershell
.\scripts\smoke-compiler-bootstrap.ps1 -Phase27POnly -BootCount 1
```

The resulting serial proof passed `phase27p=PASS` and
`ELF Loader: Phase 27P persistent object smoke PASS`.

### Quantitative transitions

| Transition | Compiled | Reused | Sources/objects | Link | Native result |
| --- | ---: | ---: | ---: | --- | --- |
| Clean build | 3 | 0 | 3 / 3 | 3 modules | baseline 42 |
| Deterministic clean regeneration | 3 | 0 | 3 / 3 | 3 modules | identical object bytes |
| No-op build | 0 | 3 | 3 / 3 | 3 modules | artifact remains valid |
| `src/math.cpp` function edit | 1 | 2 | 3 / 3 | 3 modules | 45 |
| `src/state.cpp` global initializer edit | 1 | 2 | 3 / 3 | 3 modules | 46 |
| Restore state source | 1 | 2 | 3 / 3 | 3 modules | 45 |
| Restore math source | 1 | 2 | 3 / 3 | 3 modules | 42 |
| Rename/remove `add_two` in math | 1 | 2 | 3 / 3 | expected failure | old artifact preserved |
| Restore renamed symbol | 1 | 2 | 3 / 3 | 3 modules | 42 |
| Delete `main.o` | 1 | 2 | 3 / 3 | 3 modules | 42 |
| Corrupt `math.o` ELF byte | 1 | 2 | 3 / 3 | 3 modules | 42 |
| Corrupt `.gx.meta` version | 1 | 2 | 3 / 3 | 3 modules | 42 |
| Syntax failure in math | 0 | 2 before failure | 3 / 3 | expected failure | old artifact preserved |
| Restore exact valid source | 0 | 3 | 3 / 3 | 3 modules | 42 |
| Recreated build service | 0 | 3 | 3 / 3 | 3 modules | 42 |

The clean run emitted `phase27p_object_format=PASS`,
`phase27p_persistent_objects=PASS`, and
`[DeveloperStudio] relocatable object reopen: PASS` only after the object was
read from VFS and parsed. The no-op and edit assertions use the service
snapshot counters and the compiler's actual cache-hit/compile paths; they are
not inferred from printed labels.

The stale-symbol transition reused the unchanged consumer, rebuilt the changed
defining module, failed with undefined external function `add_two`, and
preserved the prior artifact. The corrupt and missing-object transitions
reused the other two objects and rebuilt only the affected module. The final
launch returned 42, completed NativeElf teardown, emitted artifact evidence,
and left the kernel alive.

## Persistence and resource behavior

Each build uses a fresh `BareMetalBuildService` job lifecycle. The proof
performs 15 sequential start/poll/release build transitions, including the
post-failure recovery and final no-op recreation checks. The linker consumes
reopened objects on every successful incremental build. No dedicated allocator
stress instrument exists in this phase; the repeated bounded lifecycle passed
without a leak, hang, or state carry-over in the QEMU proof.

The current smoke script creates a fresh test disk for each QEMU boot. It does
not preserve the writable project volume across a reboot, so cross-reboot
object reuse was not claimed or tested. The filesystem/storage stack was not
redesigned for this phase.

## Regression coverage and scope

The following existing host suites passed after the object boundary change:

```text
run-compiler-bootstrap-host-test.ps1
run-compiler-functions-host-test.ps1
run-compiler-multifile-host-test.ps1
run-compiler-globals-host-test.ps1
run-compiler-arrays-host-test.ps1
run-compiler-pointers-host-test.ps1
run-compiler-structs-host-test.ps1
run-compiler-object-host-test.ps1
```

The object host test covers deterministic serialization, ELF identity,
round-trip linker equivalence, recursive metadata, wrong magic, checksum,
version, architecture, ABI, and relocation-bound rejection. The focused QEMU
run also retained `phase27c=PASS` and all Phase 27D runtime/lifecycle markers.

The language is deliberately unchanged: this phase does not add structs,
floating point, optimization, debug information, libraries, dynamic linking,
parallel compilation, or another architecture. AMD64 is explicit in both
ELF and guideXOS metadata; ARM64 object/code generation remains out of scope.

## Recommended Phase 27Q scope

Phase 27Q should preserve the current language and object ABI and add exactly
the next persistence/tooling layer:

1. define a compact project build manifest mapping normalized source paths to
   `.o` paths, source identity, object compatibility tuple, and dependency/
   export fingerprints;
2. persist and validate the manifest through the existing Developer Studio
   build service, including malformed-manifest recovery; and
3. extend the QEMU harness with an explicitly preserved writable project disk
   to prove clean Boot A followed by zero-compile Boot B reuse.

If atomic replacement is required for the final ELF, Phase 27Q may add a
small VFS replace/journal primitive and test it. It should not expand the
source language, add parallel/distributed builds, or redesign the filesystem.
