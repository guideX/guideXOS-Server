# Developer Studio Phase 27B: Bare-Metal Compiler Bootstrap

## Purpose

Phase 27B proves the smallest authentic source-to-native-ELF path inside a running guideXOS AMD64 kernel. The generated application is deliberately not executed. The kernel reads source bytes through its VFS, tokenizes and parses them, lowers the meaning to a target-neutral IR, emits AMD64 bytes, constructs a new ELF64 executable, writes it through the VFS, closes and reopens it, reads it back, and validates the readback image.

This is a bounded bootstrap compiler, not a general C or C++ implementation and not a linker.

## Supported language at the Phase 27B checkpoint

This section records the historical Phase 27B literal-only proof shape. The
current bootstrap compiler is a strict bounded superset; local variables,
expressions, assignments, and multiple host calls are documented in
`DEVELOPER_STUDIO_PHASE27G_BOOTSTRAP_LANGUAGE_EXPANSION.md`.

The accepted grammar is:

```text
function := "int" "gx_main" "(" "void" "*" identifier ")"
            "{" "return" ["-"] integer_literal ";" "}"
```

Whitespace, `//` comments, and `/* ... */` comments are accepted. Integer literals are decimal signed 32-bit values; `-2147483648` through `2147483647` are supported. A single function is required, the parameter is ignored, and all other syntax is rejected.

## Pipeline and implementation

```text
source
  -> compiler_lexer.cpp
  -> compiler_parser.cpp
  -> FunctionIR in compiler_ir.h
  -> arch/amd64/compiler_backend.cpp
  -> compiler/elf_writer.cpp
  -> vfs::write_file
  -> close / reopen / read
  -> validate_bootstrap_elf
```

The driver is `kernel/core/compiler/compiler_driver.cpp`. The Phase 27B
parser never emitted instructions; it produced the historical
`FunctionIR { name, returnConstant }` view. The current parser/backend
contract is the expanded target-neutral IR described by the Phase 27G
document.

## Resource limits

The implementation uses fixed kernel-resident buffers and no STL, exceptions, RTTI, libc streams, or host filesystem APIs during a guest build.

* source: 64 KiB plus one terminator byte
* tokens: 256, including EOF
* diagnostics: 8
* generated ELF: 12288 bytes maximum (the Phase 27D read-only-data extension)
* generated bootstrap body: 6 bytes

The driver uses one fixed workspace, so this first command is not reentrant or concurrent.

## AMD64 code generation

For `return N`, the backend emits `mov eax, imm32; ret`, which returns the C `int` in `EAX`. The ignored `void*` argument remains in the normal first-argument register but is not read.

```text
return 42: B8 2A 00 00 00 C3
return 41: B8 29 00 00 00 C3
```

The immediate comes from `FunctionIR.returnConstant`; the backend does not inspect the original source text.

## ELF layout

The writer emits ELF64, little-endian, `ET_EXEC`, AMD64 (`e_machine = 62`) with one deterministic `PT_LOAD` segment. There is no `PT_INTERP`, `PT_DYNAMIC`, section table, symbol table, relocation, or debug information.

At the Phase 27B commit, the bootstrap image base was `0x200000` and the entry was `0x201000`. Phase 27C's bare-metal memory audit found that the UEFI handoff stack occupies that low region and that the kernel's identity-visible virtual span can also cover `0x200000`. The shared NativeElf contract therefore migrated minimally to `0x10000000` with entry `0x10001000`; the current compiler, loader, bootloader, and regression fixtures all use that audited contract. The historical SDK note mentioning `0x400000` is not used, and the Developer Studio hosted recipe's `0x20000000` is not a native loader contract.

The single segment starts at file offset 0 and virtual address `0x10000000`, is readable and executable, and is aligned to `0x1000`. The six-byte body is placed at file offset `0x1000`, so the deterministic entry point is `0x10001000`. For the first two examples the output is `0x1006` bytes. The validator checks header identity, bounds, segment ranges, alignment, forbidden dynamic/interpreter segments, expected base, entry placement, and exact generated code bytes.

## Filesystem strategy

Source input uses `vfs::stat` and `vfs::read_file`. Output uses the existing FAT-oriented path-level `vfs::write_file`, because streaming `OPEN_CREATE` and handle extension were incomplete at the start of this phase. This still performs the write through the guideXOS filesystem driver, not through a host API. After writing, the driver explicitly opens and closes the output handle, reopens it, reads the complete file, validates the readback ELF, and recomputes its hash.

No broad VFS redesign was made. The command uses simple root-level 8.3-compatible paths for the QEMU proof. Directory creation, general streaming writes, seek-cursor repair, and other filesystem formats remain outside this phase.

## Diagnostics and determinism

Diagnostics go to the existing serial facility and include line, column, byte offset, message, and token kind. Hashes are FNV-1a 64-bit, explicitly a small non-cryptographic deterministic checksum used as reproducibility evidence. No timestamps, random data, machine identifiers, or unstable addresses are placed in the ELF.

The shell route is:

```text
compile <source-path> <output-path>
```

The QEMU proof uses the opt-in `GXOS_COMPILER_BOOTSTRAP_SMOKE_ACTIVE` startup hook and compiles `/r42.c`, `/r41.c` (the same source with only the return literal changed), and malformed `/bad.c` from the guest filesystem.

## Validation and boundary

`tests/compiler_bootstrap_host_test.cpp` covers focused lexer/parser/backend/ELF checks, but it is not the authenticity proof. Run `scripts/run-compiler-bootstrap-host-test.ps1` for those checks, then run `scripts/smoke-compiler-bootstrap.ps1` for fresh UEFI/QEMU boots. The smoke script uses host tools only to build the test kernel/bootloader, launch QEMU, and inspect the resulting artifact after the guest proof; those tools are never available to the guest compiler operation.

The generated `gx_main` is never called, mapped executable by a user loader, or jumped to. There is no process model, user stack, syscall ABI, executable-memory mapping, general loader, relocatable object format, relocation processing, symbol resolution, libraries, multiple translation units, headers, variables, calls, DWARF, or general C/C++ syntax. Those are later phases; the next bounded recommendation is bare-metal ELF loading and executable-memory support.
