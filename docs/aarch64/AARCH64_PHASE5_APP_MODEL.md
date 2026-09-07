# AARCH64-5 NativeElf App Model and First ARM64 Application

## Result

Phase 5 launches the first real ARM64 guideXOS application through the App
Model path:

```text
/Apps -> app.json -> ARM64 architecture entry -> VFS executable -> NativeElf -> gx_main
```

The proof application returns `42`, and the kernel verifies the return value,
application stack canaries, image permissions, instruction-cache visibility,
and allocator state after cleanup.  The normal proof is relaunched 100 times;
the final durability marker is emitted only when every launch succeeds and the
allocator delta is zero:

```text
AARCH64_PHASE5_PASS
```

The target remains QEMU `virt,gic-version=2,acpi=off` with `cortex-a53` and
`512M` of RAM.  The implementation is a freestanding kernel proof, not a
userspace process model: the application executes in the kernel's identity
address space on a dedicated non-executable stack.

## Package and ABI contract

The staged package is:

```text
/Apps/Phase5Arm64Proof/app.json
/Apps/Phase5Arm64Proof/bin/arm64/phase5-arm64-proof.elf
```

Its manifest declares `kind: NativeElf`, `runtime: native-elf`, the
`arm64` architecture entry, `gx_main`, and `guidexos-c-abi-v1`.  Discovery
enumerates `/Apps`, parses each package manifest, checks the architecture and
entry-point contract, and validates the executable path with the common VFS.

The public C ABI is in `sdk/include/guidexos/abi.h`:

* `gx_app_context` carries the ABI version, host-call table, and user data;
* `gx_host_calls.log` provides the proof application's log service;
* `gx_host_calls.exit` is reserved for the application exit contract;
* `gx_main(gx_app_context*)` returns an integer result;
* the ABI name is `guidexos-c-abi-v1`, version `0`.

ARM64 uses the normal AAPCS64 calling convention.  The assembly bridge saves
the kernel callee-saved registers, switches to the application stack, calls
`gx_main`, preserves its return value, and restores the kernel stack.

## NativeElf loader policy

The ARM64 loader accepts only ELF64 little-endian `ET_EXEC` images with
`e_machine = EM_AARCH64`.  It validates ELF and program-header bounds,
`filesz <= memsz`, virtual-address overflow, segment alignment, entry-point
coverage, segment overlap, and W+X rejection.  The first proof image is
linked as a fixed-address executable at `0x50000000`; relocations are not
implemented or silently applied.

Each `PT_LOAD` is copied from the VFS image, zero-filled through `memsz`, and
mapped with permissions derived from ELF flags.  The loader performs the
required data-cache clean and instruction-cache invalidation sequence before
the first call.  The permission transition is isolated in the Phase-5-only
MMU source so the Phase-1 through Phase-4 kernel image layout remains stable.

The application stack is four pages, 16-byte aligned, canary-protected, and
mapped read/write and non-executable.  Image pages are restored to writable,
non-executable permissions before release so the same fixed address can be
reclaimed safely on the next launch.

## Negative control and durability proof

`Phase5WrongMachine` contains a deliberately wrong-machine ELF payload while
its manifest advertises ARM64.  Discovery succeeds for the package, but the
NativeElf validator rejects the payload with:

```text
[guideXOS] NativeElf wrong architecture: rejected
```

The normal application is launched 100 times.  The proof checks that every
call returns `42`, that the app stack and image allocations are released, and
that `allocated_pages()` returns to its pre-launch value after every call.
The App Model worker temporarily serializes its VFS transaction against the
Phase-4 filesystem worker because the existing FAT/VFS scratch state is
single-threaded.

## Build and test

From the repository root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-aarch64-phase5.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\test-aarch64-phase5.ps1 -TimeoutSeconds 240
```

The test performs the Phase-5 host negative controls, checks application ELF
metadata and manifest staging, runs three fresh UEFI/QEMU boots, verifies the
marker order, counts at least 100 `gx_main` entries per boot, and rejects
unexpected IRQs, exceptions, allocator leaks, and phase error markers.

The same test can skip the historical suites while iterating on Phase 5:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\test-aarch64-phase5.ps1 -TimeoutSeconds 240 -SkipHistoricalRegressions
```

Historical Phase-1 through Phase-4 scripts remain the regression entry points;
use a longer timeout on slower hosts because each run intentionally leaves
QEMU in its post-proof WFI state:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\test-aarch64-phase1.ps1 -TimeoutSeconds 120
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\test-aarch64-phase2.ps1 -TimeoutSeconds 120
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\test-aarch64-phase3.ps1 -TimeoutSeconds 120
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\test-aarch64-phase4.ps1 -TimeoutSeconds 240
```
