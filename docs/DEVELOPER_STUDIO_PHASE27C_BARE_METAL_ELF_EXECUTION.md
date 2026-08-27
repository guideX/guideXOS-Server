# Developer Studio Phase 27C — Bare-Metal ELF Execution

Phase 27C extends the Phase 27B self-hosted compiler proof by loading the ELF
written by the guest compiler into a reserved executable image window, calling
its `gx_main(void*)` entry point, collecting its `int32_t` result, and returning
to guideXOS. The compiler and loader are separate subsystems; the `runelf`
shell command is only a diagnostic route.

## Bootstrap contract

The current NativeElf contract is intentionally narrow:

| Item | Contract |
| --- | --- |
| Architecture | AMD64, little-endian ELF64 |
| File type | `ET_EXEC` only |
| Image window | `0x10000000` through `0x10200000` (2 MiB) |
| Maximum file | 8192 bytes |
| Maximum mapped image | 1 MiB |
| Maximum program headers/load segments | 4 |
| Page size | 4096 bytes |
| Entry | File-backed address inside an executable `PT_LOAD` |
| Segments | Non-overlapping, page-aligned `PT_LOAD`; R is required; W+X is rejected |

The validator also requires the fixed-address `PT_LOAD` physical and virtual
addresses to match, rejects section metadata, and rejects every unsupported
program-header type. This rejects `ET_DYN`, `PT_INTERP`, `PT_DYNAMIC`, runtime
relocations, PIE, shared-library dependencies, and other features outside the
bootstrap format. The source of truth is
`kernel/core/native_elf/native_elf_contract.h`, with validation in
`kernel/core/native_elf/native_elf_validator.cpp`.

## Memory architecture audit and address decision

The AMD64 boot path enters long mode with 4 KiB identity mappings constructed
by the UEFI bootloader. The bootloader's page-table builder exposes the PML4
physical address to the kernel, but the kernel did not have a general physical
page allocator, virtual-address reservation API, or page-table mapping API
that could safely create arbitrary application mappings after boot. The kernel
uses a fixed static heap, so it is not a suitable substitute for an executable
fixed-address image allocator.

Phase 27B's original `0x200000` image base was not safe: the current handoff
stack is allocated at `0x200000`, and the low identity-mapped kernel address
space includes that range. A first replacement at `0x04000000` was also inside
the kernel's measured identity-visible span. Phase 27C therefore made the
smallest coherent contract migration to the fixed `0x10000000` window. The
bootloader reserves that exact 2 MiB range with `AllocateAddress`, clears it,
maps it in the final identity page tables, and passes its bounds plus the PML4
root through `BootInfo`. If the reservation or address check fails, bootloader
handoff fails closed rather than overwriting an existing allocation.

The compiler now emits image base `0x10000000` and entry `0x10001000` through
the shared contract. The migration is reflected in
`kernel/core/compiler/elf_writer.h` and the Phase 27B documentation; no loader
silently relocates the fixed-address ELF.

## Loader pipeline

The loader follows this sequence:

```text
VFS file
  -> bounded read into the static loader buffer
  -> NativeElf validator
  -> PT_LOAD range and overlap checks
  -> temporary writable, non-executable image permissions
  -> zero the mapped image
  -> copy each PT_LOAD's p_filesz bytes
  -> retain zero-fill for p_memsz - p_filesz
  -> install final per-segment permissions
  -> resolve validated executable entry
  -> controlled gx_main invocation
```

`kernel/core/native_elf/native_elf_loader.cpp` performs the VFS read, calls
the validator, walks the existing PML4/PDPT/PD/PT hierarchy, and requires each
destination PTE to map the expected identity page. The reusable window is
zeroed on every run, so BSS-style tails cannot retain data from a previous
image. The loader does not allocate an image page per invocation and does not
grow the kernel heap.

During population, mapped pages are writable and NX when hardware NX is
available. After copying, writable permission is removed for executable
segments and NX is cleared; non-executable segments retain NX. Each changed
page is invalidated with `invlpg`. NX is enabled through EFER when CPUID
reports support. No global NX disable or unrelated kernel mapping change is
used. The bootloader's fixed reservation means the loader can reuse the same
identity-mapped pages after each return. `kernel/arch/amd64/arch.cpp` contains
the CPUID/MSR/NX and TLB primitives.

## Entry ABI and trampoline

The guideXOS SDK contract is `guidexos-c-abi-v1`: AMD64 generated entries use
the Microsoft x64 ABI (`ms_abi` in the GCC/Clang kernel build). The compiler's
generated function is the constrained bootstrap body:

```asm
mov eax, immediate
ret
```

`kernel/core/native_elf/native_elf_executor.cpp` uses a typed, no-inline
`ms_abi` function-pointer wrapper rather than an unchecked jump. The wrapper
passes `nullptr` as the context argument, allowing the ABI call sequence to
place it in RCX and provide the compiler-required Microsoft x64 shadow space.
The compiler is built with `-mno-red-zone`; the wrapper returns normally to
its kernel caller and captures the 32-bit integer result. No application host
call table or full `gx_app_context` is created in this phase.

The invocation uses the current kernel handoff stack. The existing boot path
provides a bounded 64 KiB stack at `0x200000`, and the generated bootstrap body
does not allocate stack storage. This is sufficient for the trusted
`mov`/`ret` proof, but it is deliberately not presented as a general
application-stack design. A dedicated application stack, guard policy, and
execution context belong to the next phase.

## Shell and smoke routes

Compilation and execution remain separate:

```text
compile /r42.c /r42.elf
runelf /r42.elf
```

`runelf` is implemented in `kernel/core/shell.cpp` and delegates to the
loader; shell parsing does not contain ELF mapping logic. The opt-in
`GXOS_COMPILER_BOOTSTRAP_SMOKE_ACTIVE` route in
`kernel/core/native_elf/native_elf_smoke.cpp` performs the complete in-guest
42, 41, repeat, alternate-build, invalid-image, and post-execution VFS
proofs.

## Validation evidence

The fresh-boot harness is:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\smoke-compiler-bootstrap.ps1 -BootCount 3 -TimeoutSeconds 60
```

The final run completed all three fresh QEMU boots with these markers:

```text
Compiler: Phase 27B smoke PASS
phase27c_compile42=PASS
phase27c_execute42=PASS
phase27c_compile41=PASS
phase27c_execute41=PASS
phase27c_repeat_execution=PASS
phase27c_invalid_elf=PASS
phase27c_alternate_build_run=PASS
phase27c_kernel_survival=PASS
phase27c=PASS
ELF Loader: Phase 27C smoke PASS
```

The invalid-image smoke covers bad magic, wrong machine, invalid entry,
entry outside the executable segment, truncated program headers, invalid
segment file bounds, and a forbidden address. Hosted validator tests also
cover wrong class, `ET_DYN`, overlapping segments, address arithmetic
overflow, and zero-fill/mapped-size behavior.

For the return-42 artifact, the guest reported source FNV-1a64
`6FA8F0A7A9E94988`, code bytes `B8 2A 00 00 00 C3`, ELF size 4102 bytes,
ELF FNV-1a64 `7E33D36B65580046`, image base `0x10000000`, entry
`0x10001000`, mapped bytes 8192, NX enabled, and runtime result 42.

For the return-41 artifact, the guest reported source FNV-1a64
`41A9B51249E518BD`, code bytes `B8 29 00 00 00 C3`, ELF size 4102 bytes,
ELF FNV-1a64 `8AE1B495E2A5B443`, the same image base/entry/mapped size, NX
enabled, and runtime result 41. The smoke then rebuilt and reran 42, proving
that the observed result came from the newly written artifact.

After the generated code returned, the kernel read the ELF through VFS and
continued into its ordinary main loop. Physical hardware was not tested;
these are hosted/static tests and QEMU bare-metal kernel proofs.

## Boundary and security limitation

Phase 27C executes trusted bootstrap code in a controlled kernel-owned
execution context. It is not yet a security boundary or userspace sandbox.
The generated input is trusted and restricted to the current compiler's
return-constant body; executable code can still be kernel-impacting. There is
no scheduler, process lifecycle, fork/exec, syscall ABI, application
permission model, IPC, dynamic linking, shared libraries, relocations, PIE,
debugger integration, Developer Studio Run integration, or physical isolation.

The bounded recommendation for Phase 27D is to introduce a real application
execution context and lifecycle around NativeElf: a dedicated application
stack/context, explicit cleanup and fault policy, and the beginnings of the
bare-metal `gx_app_context` runtime contract. That work should remain separate
from the compiler and should not be inferred from the Phase 27C bootstrap
success.
