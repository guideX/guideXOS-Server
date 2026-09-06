# guideXOS Server AARCH64-1 — First Native AArch64 Boot

Status: Outcome A — complete Phase 1 proof.

This phase adds the smallest independent AArch64 UEFI-to-kernel path. It does
not change the existing AMD64 loader or normal AMD64 kernel build.

## Implementation

The new path is intentionally separate from the current full loader:

- `guideXOSBootLoader/aarch64/phase1_loader.cpp` is a freestanding AArch64
  UEFI application. It opens `kernel.elf` through the loaded-image and simple
  filesystem protocols, validates and loads the ELF, allocates the stack and
  handoff, acquires the final memory map, and calls `ExitBootServices`.
- `aarch64/phase1/phase1_contract.h` is the fixed-width C-compatible handoff
  ABI used by both sides.
- `kernel/arch/arm64/phase1_entry.S` is the AArch64 entry and exception-level
  edge. `phase1.cpp` validates the live state and handoff; `phase1_serial.cpp`
  is the temporary QEMU-virt PL011 console.
- `kernel/arch/arm64/phase1_linker.ld` provides the fixed physical/virtual
  link policy.
- `scripts/build-aarch64-phase1.ps1`, `run-aarch64-phase1.ps1`, and
  `test-aarch64-phase1.ps1` provide build, run, three-boot, and negative
  control entry points.

The pre-existing `kernel/arch/arm64/boot.S`, `arch.cpp`, and
`serial_console.cpp` were inspected and left unchanged. They are general ARM64
scaffolding, are not active in the parent build, and contain stale interfaces;
in particular, `boot.S` refers to `_Z11early_printPKc` while the old serial
source exports C-linkage `early_print`. The Phase 1 path does not retain or
paper over that mismatch.

The ordinary AMD64 loader remains in `guideXOSBootLoader/main.cpp` with its
existing x64 project and handoff path. No full-loader refactor was needed.

## Toolchain and artifacts

The repository-local build uses the installed LLVM 22.1.8 toolchain at
`C:\Program Files\LLVM\bin`:

```text
UEFI compiler:  clang++ --target=aarch64-pc-windows-msvc
UEFI linker:    lld-link /subsystem:efi_application /machine:arm64
Kernel compiler: clang++ --target=aarch64-none-elf -march=armv8-a
Kernel linker:   ld.lld -m aarch64elf
```

The LLVM-reported normalized freestanding kernel target is
`aarch64-unknown-none-elf`.

The final rebuilt artifacts were:

| Artifact | Size | SHA-256 | Verification |
|---|---:|---|---|
| `BOOTAA64.EFI` | 8,192 bytes | `545974a9771c4bf20ea859e7421ec8a6b84adf40ca0f7098001be5bbee7dcded` | PE/COFF `IMAGE_FILE_MACHINE_ARM64`, `0xAA64`; EFI application subsystem |
| `kernel.elf` | 72,072 bytes | `26e0fd59d3698b81d2383ae5872a9a2f47a6f5531546ad5075e45afb0183a26d` | ELF64 little-endian, `EM_AARCH64`, `0xB7`; `ET_EXEC` |

The staged ESP is `out\aarch64-phase1\esp` and contains:

```text
EFI\BOOT\BOOTAA64.EFI
kernel.elf
```

LLVM inspection reported ELF entry `0x40000000` and these loadable segments:

| Offset | Virtual / physical address | File / memory size | Flags | Alignment |
|---:|---:|---:|---|---:|
| `0x10000` | `0x40000000` | `0x1000 / 0x1000` | R-X | `0x10000` |
| `0x11000` | `0x40001000` | `0x15D / 0x15D` | R-- | `0x10000` |

The build performs programmatic PE and ELF header checks and fails if the
machine fields are not ARM64/AArch64.

## Load, handoff, and execution state

Phase 1 uses a deliberate fixed-address identity-safe policy:

- The kernel is linked as `ET_EXEC` with its first `PT_LOAD` at
  `0x40000000`, the QEMU `virt` RAM base.
- The loader validates all ELF bounds, overflow cases, `PT_LOAD` file/memory
  sizes, alignment, and an executable entry point. It explicitly requires
  ELF64, little-endian, `EM_AARCH64`, and `ET_EXEC`.
- The loader uses `AllocateAddress`/`EfiLoaderCode` at `0x40000000`; there is no
  relocation and no position-independent-image assumption.
- The loader does not construct page tables. It captures `CurrentEL` and
  `SCTLR_EL1`, keeps firmware mappings long enough to reach the fixed physical
  entry, and the entry edge clears `SCTLR_EL1.M`, `.C`, and `.I` before C++
  execution. Kernel output proves `MMU: OFF`.
- The kernel stack is 16 pages (64 KiB) allocated with UEFI
  `AllocateAnyPages`. The handoff stores base, size, and top. Before printing
  `stack: OK`, kernel code reads SP and checks that it is within the owned
  allocation, equals the recorded top at entry, and is 16-byte aligned.
- The handoff is one UEFI loader-data page and contains the magic
  `GXOS_AARCH64_PHASE1_HANDOFF_MAGIC`, version, exact size, kernel range and
  entry, stack range, memory-map pointer/size/descriptor stride/count, the
  initial EL, captured loader `SCTLR_EL1`, and the temporary PL011 base.
  Kernel code validates the magic, version, size, required flags, ranges, and
  UART address before printing `firmware handoff: OK`.
- The final memory map is retained in loader-data memory. `ExitBootServices`
  retries a stale map key without allocations between the failed EBS call and
  the next `GetMemoryMap`. The EBS-complete flag is set only after
  `EFI_SUCCESS`; the kernel's `ExitBootServices: OK` line is conditional on
  that flag.

The loader observed `CurrentEL: EL1` and `loader MMU: ON` on all final QEMU
boots. Therefore no EL transition was required by the tested firmware
condition. A minimum EL2 path is present for the expected future condition: it
sets `HCR_EL2.RW`, installs the owned stack in `SP_EL1`, clears EL1 M/C/I,
installs `VBAR_EL1`, programs `SPSR_EL2` for EL1h, and returns with `eret`.
EL3 is rejected by the loader.

Copied executable bytes receive `dc cvau` over the cache-line range selected
from `CTR_EL0`, followed by `DSB SY`, `ic ivau`, `DSB SY`, and `ISB`. The
kernel then enters with EL1 MMU and caches disabled, so the Phase 1 proof does
not depend on a permanent page-table or cache policy. This is intentionally a
temporary AARCH64-1 strategy.

## Serial proof

`phase1_serial.cpp` uses the QEMU `virt` PL011 at `0x09000000`, marked as a
temporary platform-specific constant rather than a universal ARM64 UART. It
initializes polled 8N1/FIFO output, masks UART interrupts, waits for `FR.TXFF`
to clear, and emits through the data register with compiler/CPU barriers. No
interrupts, receive path, GIC, timer, or general serial framework is included.

## Firmware and QEMU

The installed firmware and emulator were used; no global toolchain or firmware
installation was performed:

- QEMU: `C:\Program Files\qemu\qemu-system-aarch64.exe`, version
  `11.0.0 (v11.0.0-12122-ga4bb4b10c9)`.
- Code firmware: `C:\Program Files\qemu\share\edk2-aarch64-code.fd`.
- Variable template: `C:\Program Files\qemu\share\edk2-arm-vars.fd`, copied
  to the ignored output directory as `edk2-aarch64-vars.fd` for writable
  repeatable runs. The firmware identifies itself as
  `edk2-stable202408-prebuilt.qemu.org`.

The exact run command used by the repository script was:

```powershell
& 'C:\Program Files\qemu\qemu-system-aarch64.exe' `
  -machine virt -cpu cortex-a53 -m 512M `
  -drive 'if=pflash,format=raw,unit=0,readonly=on,file=C:\Program Files\qemu\share\edk2-aarch64-code.fd' `
  -drive 'if=pflash,format=raw,unit=1,file=D:\dev\guideXOSServer_AARCH64\out\aarch64-phase1\edk2-aarch64-vars.fd' `
  -drive 'file=fat:rw:D:\dev\guideXOSServer_AARCH64\out\aarch64-phase1\esp,format=raw' `
  -nographic -monitor none -serial stdio -no-reboot
```

## Test evidence

Command:

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File .\scripts\test-aarch64-phase1.ps1 -TimeoutSeconds 12
```

Results from the final rebuilt/staged artifacts:

- Fresh QEMU boot 1: PASS; log `out\aarch64-phase1\logs\boot-1.log`.
- Fresh QEMU boot 2: PASS; log `out\aarch64-phase1\logs\boot-2.log`.
- Fresh QEMU boot 3: PASS; log `out\aarch64-phase1\logs\boot-3.log`.
- Negative control: a copied fixture with `e_machine = EM_X86_64 (62)` was
  rejected by the UEFI loader with
  `[A64 UEFI] ERROR: incompatible ELF` and did not execute the kernel.

All three successful serial streams contained, in order:

```text
[A64 UEFI] entry
[A64 UEFI] CurrentEL: EL1
[A64 UEFI] loader MMU: ON
[A64 UEFI] kernel opened
[A64 UEFI] ELF machine: AArch64
[A64 UEFI] segments loaded
[A64 UEFI] memory map acquired
[A64 UEFI] ExitBootServices requested
[guideXOS] AARCH64 kernel entry
[guideXOS] execution level: EL1
[guideXOS] stack: OK
[guideXOS] ExitBootServices: OK
[guideXOS] firmware handoff: OK
[guideXOS] MMU: OFF
AARCH64_PHASE1_PASS
```

The harness treats explicit Phase 1 error/failure markers as failures and
stops each halted proof kernel after the configured timeout. The timeout does
not provide the pass result; the pass result is read from the captured serial
stream emitted by guideXOS kernel code after EBS.

## AMD64 and dependency status

No AMD64 source was replaced. The normal AMD64 info gate was attempted:

```text
mingw32-make -C kernel ARCH=amd64 info
```

It remains blocked before compilation because `third_party/mbedtls` and the
generated/profile files are absent. The repository has no `.gitmodules`; its
documented `scripts\bootstrap-mbedtls.ps1` procedure is unrelated to Phase 1
and was not run or vendored. No full AMD64 build or AMD64 QEMU regression is
claimed.

## Scope and next step

The known temporary assumptions are the QEMU `virt` PL011 address, the fixed
`0x40000000` load/link address, identity-safe execution, and the lack of DTB or
platform discovery. The EL2 path is implemented but was not exercised because
the installed UEFI condition was already EL1.

AARCH64-2 should replace the temporary translation regime with controlled 4 KiB
page tables and an explicit MAIR/TCR/TTBR/SCTLR policy, then add VBAR-backed
fault diagnostics, memory-map/allocator validation, the architectural timer,
and a pinned GIC. DTB/platform discovery should replace the UART fallback only
when that controlled foundation is proven.
