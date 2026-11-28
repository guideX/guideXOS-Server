# DOTNET_RECOVERY2 — Build Recovery and S22R28 Protected-Task Port Plan

November 26, 2026 (Pacific)

## Outcome

**B — `DOTNET_RECOVERY2_PARTIAL`.**

The E: checkout produced a valid ELF64 AMD64 kernel using the committed production feature profile, once the lost untracked `third_party/` tree was rebuilt from Main Integration. That kernel is useful but not trusted. Object comparison against the last qualified .NET kernel (R23) shows that 15 of the 57 Mbed TLS objects differ. The rebuilt Mbed TLS / TF-PSA-Crypto tree is therefore a compatible substitute, not the dependency the R33/R36 candidate actually used.

Named remaining dependencies:

1. **The .NET-specific `third_party/mbedtls` tree.** This was untracked and was lost with D:. It held the patched TF-PSA-Crypto sources and the `guidexos` glue that the qualified kernel was built from.
2. **PowerShell 7 (`pwsh`).** Both committed production build scripts refuse Windows PowerShell 5.1, and `pwsh` is not installed. Because of this, the managed NativeAOT half of the production candidate was not attempted.

S22R28 is a better foundation than rebuilding ISOLATION1A from memory. It is guest-qualified for real CPL3 entry, controlled exit and cleanup (S22R28R1, three cycles). It cannot be copied across as-is, though, because it depends on Main's S22R27 frame owner, BootInfo v3, and Main's exception-entry rework, and the .NET branch has none of these.

No QEMU boot or QAC request was made. Nothing in CPU Improvements, `main` or `v1.1_DOTNET_SUPPORT` was modified.

## A. Starting baseline

| Item | Value |
| --- | --- |
| Repository root | `E:/dev/guideXOSServer_DOTNET_SUPPORT` |
| Branch / HEAD at start | `v1.1_DOTNET_SUPPORT` at `97dc7b0daceef1007d84c3abf3ee03d4f2182e51` (tree `0f74667aa4c1464b4de05efb3b33eb781664ecbe`) |
| Tracking | Up to date with `origin/v1.1_DOTNET_SUPPORT`. An HTTPS `ls-remote` confirms GitHub still holds `97dc7b0d`. |
| Worktree | Clean: `git status --porcelain` and `--ignored` were both empty. No stash. |
| Object store | One pack, 52,151 objects, 689.57 MiB, no loose or garbage objects. |
| Remote | `origin = git@github.com:guideX/guideXOS-Server.git` (fetch and push). |
| Main Integration S22R28 | `14be308b7282a2131b0f124eeccd662a5378014d` (tree `b637077e…`). Present, and an ancestor of `origin/main` and `origin/CPU_IMPROVEMENTS`. |
| S22R28 guest follow-ups on main | `cd534f37` (S22R28R1 qualification), `a4c22b21`, `7c3c6a01`, `d1b2b2ce` (S22R28R2). `origin/main` = `d1b2b2ce`. |
| Divergence | Merge base `58c07f18` (2026-07-12). .NET is 479 commits ahead of it and S22R28 is 365 commits ahead. |

The only pre-existing work was the committed history, so there was nothing new to preserve. `dotnet-recovery2` was created from `97dc7b0d` with `git switch -c`. No reset, clean, rebase or stash was used.

The three generated `.obj` files that earlier reports protected are now tracked at their committed contents. They were not modified.

## B. Toolchain recovery

### What the production profile actually needs

There are two halves. They have very different requirements.

| Stage | Script | Requirements |
| --- | --- | --- |
| Freestanding kernel | `scripts/dotnet/build-c188-production-kernel.ps1` | `pwsh`, `mingw32-make`, MinGW `g++`, `ld`, `objcopy`, `nasm`, `readelf`, and the untracked `third_party/stb` and `third_party/mbedtls` trees. |
| Managed NativeAOT | `scripts/dotnet/build-c188-production-nativeaot.ps1` → `build-managed-hostlog-proof.ps1` → runtime-pack builder | `pwsh`; .NET SDK; NuGet `runtime.win-x64.microsoft.dotnet.ilcompiler` and `microsoft.dotnet.ilcompiler` **9.0.0** (locked by `runtime-pack.lock.json`, NativeAOT 9.0.0 at runtime commit `9d5a6a9a…`); MSVC `vcvars64.bat`; Python; `objdump` and `readelf`; the pinned `out\dotnet\gc-feasibility-baseline\nativeaot-runtime\src\coreclr` sources. |

.NET SDK 10.0.404 can drive the build, but it does not replace the locked 9.0.0 ILCompiler packages. The runtime-pack lock hard-checks for 9.0.0.

### Installed and discovered

| Tool | Found | Version |
| --- | --- | --- |
| g++ / gcc | `C:\mingw64\bin` | MinGW-W64 x86_64-msvcrt-posix-seh (Brecht Sanders r7) 15.2.0 |
| ld / objcopy / readelf | `C:\mingw64\bin` | GNU Binutils 2.46.0.20260210 |
| nasm | `C:\mingw64\bin\nasm.exe` | 2.16.03. `kernel/arch/amd64/Makefile.arch` already falls back to `nasm` on PATH, so `C:\Program Files\NASM` is not needed. |
| make | `C:\mingw64\bin\mingw32-make.exe` | GNU Make 4.4.1. The production script already defaults to this path. |
| .NET SDK / runtimes | `C:\Program Files\dotnet` | SDK 10.0.404; runtimes 8.0.34, 9.0.23, 10.0.15 |
| ILCompiler NuGet 9.0.0 | `%USERPROFILE%\.nuget\packages\{runtime.win-x64.,}microsoft.dotnet.ilcompiler\9.0.0` | Present (not hash-verified against the lock in this phase) |
| MSVC | `C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat` | Present. It is the first candidate that `Find-VcVars64` checks. |
| Python | `Python312\python.exe` | 3.12.10 |
| PowerShell | Windows PowerShell 5.1.26100 only | **`pwsh` missing** |
| QEMU | Not on PATH. The assignment places it at `C:\Program Files\qemu`, which this phase did not use. | — |
| `C:\msys64\mingw64\bin`, `D:\dev\guideXOS`, `D:\dev\guideXOSUEFI` | Absent. None is needed by the kernel profile. | — |

### Path findings and corrections

- **`D:\dev\guideXOSUEFI`.** `build-managed-hostlog-proof.ps1` defaults `LegacyRoot` to this path when `GUIDEXOS_LEGACY_ROOT` is unset. It is only normalized and written into the build log, never read, so it is not a real dependency. No change was made. The existing environment variable can override it if needed.
- **Make shell.** The kernel Makefile uses `cmd.exe` syntax (`if not exist … mkdir`) on Windows. Git's `usr\bin\sh.exe` is on PATH on this machine, so the build wrapper removes that directory from the child PATH and runs make through `cmd.exe`. I did not prove that `sh.exe` breaks the committed script, so I did not change it.
- **The pinned NativeAOT runtime sources are not in the fresh clone.** `out/` is git-ignored. A copy of `nativeaot-runtime\src` (117 MB) exists at `E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\gc-feasibility-baseline\nativeaot-runtime`, but it is not a Git checkout, so its commit cannot be verified. The production path reads files from it. The FP-repair and ThreadStore diagnostic paths need `git archive` from a real checkout. That copy must be verified, or the checkout re-cloned at `9d5a6a9a…`, before the managed build can be trusted.
- **No committed script or source was changed in this phase.**

## C. Production build

### First attempt (failed, kept as evidence)

`E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\dotnet-recovery2\kernel-production-20261126T2025Z`: exit 2 after 94 s.

- `core/image_adapter.cpp:62: fatal error: ../../third_party/stb/stb_image.h: No such file or directory`
- `No rule to make target '../third_party/mbedtls', needed by …/gxos_tls_foundation.o`

An earlier directory, `…T202137Z`, was stopped by my own wrapper: Windows PowerShell treated compiler stderr as a terminating error. It is not a build result.

### The missing dependency and where it came from

`/third_party` is in `.gitignore`. Commit `75633907` (an ancestor of the .NET branch) removed the tracked `third_party/stb/stb_image.h` and the early `third_party/mbedtls/guidexos/{README,mbedtls_config.h,mbedtls_sources.mk}`. From then on the kernel built against a local, untracked tree on D:. The R23 qualified kernel log (`E:\CodexArtifacts\…\r23-kernel-74770308256b49fd84a565a48a7fb8d3\kernel-build.log`) shows that tree as:

- `stb_image.h`
- Mbed TLS 4.x with TF-PSA-Crypto: 57 C sources, `-DMBEDTLS_CONFIG_FILE="third_party/mbedtls/guidexos/mbedtls_config.h"` and `-DTF_PSA_CRYPTO_CONFIG_FILE="third_party/mbedtls/guidexos/crypto_config.h"`

`crypto_config.h` and the 4.x `mbedtls_sources.mk` in that layout were never committed to any branch.

### Overlay used (git-ignored, under the repo's `/third_party`)

| Component | Source | Identity |
| --- | --- | --- |
| `third_party/stb/stb_image.h` | Git blob `9eedabedc45b…` from this branch's own history (`75633907^`) | SHA-256 `594c2fe3…b200b3`. The rebuilt `image_adapter.o` is **byte-identical** to R23's. |
| `third_party/mbedtls/**` | Copy of `E:\dev\guideXOSServer\third_party\mbedtls`, excluding `.git` | Mbed TLS `v4.1.0` `0fe989b6`, with local modifications (diff SHA-256 `494afa54…`). TF-PSA-Crypto `29160dd8` (v1.1.0), with local modifications in 15 files (diff SHA-256 `3654ca8b…`). |
| `third_party/mbedtls/guidexos/*` | Main's tracked `guidexos/` (added in `45535d62`) | `crypto_config.h` `e6bdd275…`, `mbedtls_config.h` `937999df…`, `mbedtls.lock` `ae37bcdd…`. The `mbedtls_sources.mk` paths were rewritten to this layout (`ffa28e5e…`). The diff is kept as `mbedtls_sources.overlay.diff` in the build directory. |

### Successful build

```
C:\mingw64\bin\mingw32-make.exe -C E:\dev\guideXOSServer_DOTNET_SUPPORT\kernel ARCH=amd64 MBEDTLS_GUIDEXOS_REPO_ROOT=.. BUILD_DIR=<isolated> "EXTRA_CFLAGS=<49 production defines>" -B
```

The 49 `-D` flags are taken directly from the `$nativeFeatures` list in `build-c188-production-kernel.ps1`. They are recorded in `features.txt` and `command.txt` in the build directory. The wrapper is `run-kernel-build.ps1` (SHA-256 `17e910cf…`), which reproduces the committed script's make call and its checks under PowerShell 5.1.

| Result | Value |
| --- | --- |
| Directory | `E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\dotnet-recovery2\kernel-production-20261126T2033Z` |
| Exit / time | 0 / 128 s; 0 errors, 425 warnings |
| `kernel.elf` | 2,389,104 bytes, **SHA-256 `09984b31c69399e8529b86faac7b1d3322463e285da5cc4156ab07620c6751b9`** |
| `kernel.pe` | SHA-256 `a2aa0ef9…bb13b51` |
| `kernel-build.log` | SHA-256 `0374812f…dc6900` |
| ELF header | ELF64, EXEC, AMD x86-64, entry `0x100000` |
| Trusted managed identities | `com.guidexos.apps.managed.{notes, settingscenter, calculator, taskmanager, fileexplorer}`: all present |
| Development markers | None of the 15 markers the production script rejects (CounterApp, C188R3–R12 probes and others) are present |
| Mbed TLS sources compiled | 57; the list is identical to R23's |

### Why this kernel is not trusted yet

I compared every Mbed TLS object with R23's `obj/third_party`. **42 are byte-identical and 15 differ:** `ssl_msg`, `ssl_tls`, `ssl_tls12_client`, `psa_crypto`, `psa_util`, `bignum`, `ecdsa`, `ecp`, `ecp_curves`, `psa_crypto_ecp`, `psa_crypto_rsa`, `psa_util_internal`, `pk_ecc`, `pk_wrap` and `oid`.

The symbol differences point to real configuration and patch differences, not build noise:

- R23's `psa_crypto.o` references `gxos_tls_smoke_debug_trace`. The rebuilt object does not.
- The rebuilt `ecp.o`, `oid.o` and `psa_crypto.o` reference ECDSA/ECP entry points that R23 lacked.

The NativeAOT and desktop code paths do not depend on TLS, and bare-metal HTTPS remains gated. Even so, I am not treating this kernel as equivalent to the qualified baseline. It also differs in source from R23 (`79f0036c` + proof-only flags), so no whole-kernel hash comparison is possible.

**To close this dependency, do one of the following:**

- **(a)** Recover the original `.NET` `third_party` tree from a non-D: backup, if one exists, and verify the 15 objects against R23.
- **(b)** Accept Main's Mbed TLS/TF-PSA variant on purpose for .NET Support, commit the `guidexos` glue to this branch (in Main's root-level `guidexos/` layout, or the `third_party/mbedtls/guidexos` layout above), and re-baseline TLS behavior.

Option (b) is recommended. It removes the untracked-dependency hazard that caused this loss.

The managed NativeAOT build, the ramdisk and the bootloader were not built. Without `pwsh`, no full candidate exists.

## D. S22R28 comparison

### What S22R28 / S22R28R1 established on Main

- **GDT and TSS.** Seven-slot GDT: `0x08`/`0x10` kernel, `0x1B` user data, `0x23` user code, 64-bit TSS at `0x28`. `ltr`/`str` verification, IOPB closed, RSP0 set per entry, IST1 on a 16 KiB exception stack.
- **CPL3 entry.** `protected_task.asm`: saved kernel continuation, an `iretq` frame with IF=0/IOPL=0, cleared GPRs and FS/GS. The exit is a DPL3 `int 0x80` gate that strictly validates CS/SS/RIP/RSP/op/magic/RFLAGS and the CR3 match.
- **Task model.** One static task descriptor with a lifecycle state machine. Each cycle builds a private S22R27 `TaskAddressSpace` with code RX-user, data RW-NX-user, an absent guard page, and stack RW-NX-user. Kernel mappings are supervisor-only. Teardown runs on a separate lifecycle stack after kernel CR3 is restored, and a full `FrameAccounting` equality check runs three times.
- **Guest evidence (S22R28R1).** Three CPL3 cycles passed with CS `0x23`, result `42`, cleanup verified and the task reaped. The desktop loop and PIT continued afterwards.
- **Not qualified:** user-fault recovery (CPL3 faults record and **halt**), non-cooperative or looping tasks, preemption, dynamic rejection of supervisor accesses, and whole-call-chain stack use. S22R28R2 still has an unexplained FAT root-mount failure on that diagnostic ISO.

### Equivalence to the lost ISOLATION1A R1–R3

| # | Area | S22R28 (Main) | .NET Support at `97dc7b0d` | Port assessment |
| --- | --- | --- | --- | --- |
| 1 | Real CPL3 entry and exit | Implemented and **guest-qualified** (R1) | Absent. Three-entry GDT, no TSS. | Exceeds lost R1–R3, which never showed real CPL3. Reuse the assembly and contract header directly. |
| 2 | User/kernel memory protection | S22R27 private root with U/S, R/W and NX permissions; hosted tests | One `g_current` address space; no second root | Needs adaptation (see "Mapping" below) |
| 3 | Kernel-owned stacks | 16 KiB lifecycle, RSP0 and IST1 stacks in BSS | None | Reuse. Add a separate double-fault IST, which the lost work had and S22R28 does not. |
| 4 | Fault attribution | Dispatcher checks `CS.RPL==3`, records the task, vector and CR2, then halts | Only `page_fault_stub` plus the NativeAOT expected-PF hook; no CS in the frame | Needs adaptation. Main's 32-vector stubs depend on Native ELF fault code (`native_elf_fault.h`) that .NET lacks. |
| 5 | Diagnostic task ownership | Single static descriptor, task IDs, state machine | None | Reuse as-is |
| 6 | Exactly-once cleanup | Off-stack teardown and accounting equality, three cycles | None | Reuse. Fault-path reaping is still unimplemented everywhere. |
| 7 | Mapping and address space | `physical_frame_owner` + `task_address_space`, fed by the BootInfo **v3** task-frame arena (16 MiB `EfiLoaderData`) | BootInfo **v1** with `PhysicalFrameMetadataBase/Pages`; `address_space.cpp` frame allocator with `FrameOwner` classes | **Main incompatibility.** Do not port the loader or BootInfo v3. Supply the S22R27 owner's arena by reserving a contiguous range from the .NET allocator at kernel init (new `FrameOwner::TaskArena`), keeping `physical_frame_owner.cpp` and `task_address_space.cpp` almost unchanged. |
| 8 | NativeAOT compatibility | Not considered | The NativeAOT runtime runs at CPL0 in the shared kernel address space and uses the expected-PF hook | Keep CPL3 opt-in (`GXOS_ENABLE_CPL3_DIAGNOSTIC`). The GDT change must keep `0x08`/`0x10`. Check that the expected-PF hook still fires with the new stubs. |
| 9 | Interrupts, timer and scheduler | 32 exception stubs, IRQs on IST1, aligned shadow space; syscall `STAR` corrected for `0x1B`/`0x23` | IRQ stubs with `sub $40`; no IST; the cooperative `process::schedule` is the same model as S22R25's predecessor | Port the stub alignment and IST together with the GDT/TSS. Leave the timer and scheduler unchanged; S22R28 does not need preemption. |
| 10 | Known S22R28 limits | Halt on user fault, no preemption, IF=0 in user mode, static single task | — | These carry over. CPU7B2 also found shared-IST1 nesting, `#MF`/`#AC`/`#CP` error-code mismatches and missing `cld` in these stubs. Fix those in the port rather than copying them. |

Files to bring in from `14be308b` as adapted copies (not a cherry-pick): `protected_task.cpp`, `protected_task.asm`, `amd64_protected_task_contract.h`, `protected_task.h`, the `physical_frame_owner.{h,cpp}` and `task_address_space.{h,cpp}` + `amd64_memory_foundation.{h,cpp}` from `bc2acf5a`, the GDT/TSS/IDT portions of `interrupts.cpp`, the syscall `STAR` fix, and the hosted tests (`tests/amd64_protected_task_contract_test.cpp`, `tests/amd64_memory_foundation_test.cpp` and their scripts). `native_elf_baremetal.cpp` is Main-only, and its +20 lines should not be ported.

**Source equivalence is not guest qualification.** After the port, all of the R1 evidence (three cycles, CS `0x23`, result 42, cleanup, continued desktop) has to be shown again on a .NET candidate.

## CPU Improvements compatibility (addendum, read-only)

`E:\dev\guideXOSServer_CPU_IMPROVEMENTS`: branch `CPU_IMPROVEMENTS`, HEAD `36c645023bf934e17a90e16303ef0fe65f3ec97d` (tree `ad7f2427…`), clean worktree. It is one local commit ahead of `origin/CPU_IMPROVEMENTS` (`dcd0307d`); the unpushed commit is the CPU7B2 recovery-qualification report. Reports CPU1–CPU7B2 are present in `docs/`.

**Its merge base with `main` is exactly `14be308b`.** CPU Improvements already contains S22R28 and builds on it. It adds 15 commits and changes 31 kernel/loader files. Nothing was modified there.

### Three-way comparison

| Concern | .NET Support `97dc7b0d` | Main S22R28 `14be308b` | CPU Improvements `36c64502` |
| --- | --- | --- | --- |
| GDT / TSS | 3 entries, no TSS | 7 slots, TSS `0x28`, IST1, RSP0 | S22R28's layout. Adds IST2/IST3 emergency stacks for its probe IDT (diagnostic only). |
| IDT / exceptions | Page-fault stub only, plus NativeAOT expected-PF hook | 32 stubs, Native ELF recovery, CPL3 halt, `0x80` exit gate | S22R28's stubs. Adds a detached probe IDT with an exact-site `#GP` fixup. CPU7B2 documents the entry defects listed in row 10 above. |
| PIC / PIT / LAPIC | 8259 PIC + PIT | Same | Still PIC/PIT. BSP xAPIC mapping is gated off (`ownership-unverified`). Read-only LAPIC/MADT/IOAPIC discovery, `IA32_APIC_BASE` sample. No APs. |
| Context switch | Older C++ `context_switch.cpp` (TODO: TSS.RSP0) | S22R25 asm voluntary switch (eight nonvolatile GPRs) | Same as S22R28 |
| CR3 / address space | Single current address space, BootInfo v1 | S22R27 private roots, BootInfo v3 arena | CPU4 bounded loader page tables (605 pages); changes `amd64_memory_foundation.cpp`; BootInfo adds `ACPI_INVENTORY_VALID` |
| FS/GS / per-CPU | Native TLS for NativeAOT runtime only | Entry saves and restores FS/GS bases | `per_cpu.{h,cpp}`: roster with logical CPU 0 online; no GS-based per-CPU block yet |
| Scheduler / SMP | Cooperative, single CPU | Same; no preemption | Same; BSP only. All interrupt and protected-task state is documented as single-CPU. |
| Task reclamation | — | Off-stack teardown, accounting equality | Unchanged. CPL3 controls passed in the CPU4–CPU7B2 guests. |

### Overlap and conflict points

The overlapping files are `kernel/core/interrupts.cpp` (GDT/TSS/IDT/stubs/`set_idt_entry` signature), `interrupts.h`, `amd64_memory_foundation.cpp`, `kernel/core/main.cpp` (boot hooks), `kernel/Makefile` (diagnostic gates), the bootloader `guidexOSBootInfo.h`/`main.cpp`/`paging.*`, and `TSS64` / `amd64_protected_task_contract.h` (IST offsets are asserted by CPU7B2).

### Can S22R28 be adapted without hurting CPU Improvements?

Yes, if .NET takes the code exactly as CPU Improvements and Main share it (same selectors, `TSS64` layout, contract header and IST numbering) and the .NET-only differences stay in the arena provider and the NativeAOT expected-PF hook.

What .NET must not do:

- Add a second, different GDT/TSS layout.
- Renumber IST slots. CPU7B2 uses IST2/IST3 for NMI and `#DF` in its probe, so a .NET double-fault IST should use IST3 and match.
- Add APIC mappings or timer routing.

### Single-CPU now, multicore later

The BSP-only CPL3 entry/exit, the fault-record path and the cooperative cleanup can be qualified now. Per-CPU TSS/RSP0/IST, GS-based CPU-local task pointers, LAPIC timer preemption, AP startup and cross-CPU TLB shootdown all depend on future CPU Improvements acceptance.

### Recommended ownership

- **CPU Improvements** owns GDT/TSS/IST layout, IDT/exception entry stubs, interrupt-controller and timer routing, and per-CPU state.
- **Main Integration** owns the S22R27/S22R28 protected-task lifecycle, the task address space and the frame owner.
- **.NET Support** owns only the NativeAOT integration: the expected-PF hook, the arena provider over its own allocator, and the managed diagnostic invocation. It should take shared interfaces from those owners and not fork them.

### Conflict-minimizing order

1. Port the shared contract header and the GDT/TSS layout exactly.
2. Port the stub alignment/IST changes and apply the CPU7B2 entry corrections, ideally as a CPU Improvements change that .NET then adopts.
3. Port the frame owner and task address space with the .NET arena provider.
4. Port the protected task and its diagnostic gate.
5. Later, follow the CPU Improvements per-CPU and timer milestones before any preemption work.

## E. Desktop recovery

- **`KernelCompositor::setFocus(0)`.** In .NET (`kernel/core/kernel_compositor.cpp:309`), calling it clears `WF_FOCUSED` on the old window, but `getWindow(0)` fails, so `s_focusedWindowId` keeps the stale ID. Main, CPU Improvements and Taskbar Improvements all return early on `!newWin`, so on those branches `setFocus(0)` does nothing at all, which is worse. Taskbar's version also changes minimize/activation behavior. **No committed fix exists anywhere to reuse.** The smallest fix: when `windowId == 0` or the window is not found, clear the old window's flag, mark it dirty, and set `s_focusedWindowId = 0`, without calling `bringToFront`. Add a hosted compositor test for focus → `setFocus(0)` → `getFocusedWindowId()==0` → refocus of the same ID.
- **Desktop feature-header and row-buffer repairs.** These are not recoverable from any committed source, report or artifact I could find. No ISOLATION1A record exists under `E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT`, and no `row buffer` or `feature header` reference exists in the .NET docs or `kernel/core`. They must be re-derived when the RECOVERY4 candidate builds and boots. Do not guess at them in advance.

## F. QAC tooling

Reusable committed components:

- **.NET.** `scripts/dotnet/c181-qmp.psm1`, `c187-qmp-control.py`/`c188-qmp-control.py`, `c188r12r4r5r4-qac-admission.psm1` (admission and census), `c188r11qac-ownership-predicates.ps1` (reservation ownership), the `run-c188r35-settings-launch.ps1`/`stage-…` package flow, and their `test-*` suites.
- **Main.** `scripts/qac-managed-qemu.psm1`, `qac-s22r10-uefi.psm1` and `run-qac-s22r10-uefi.ps1`: a UEFI acceptance runner with QMP prelaunch/cont and pflash/var-store handling. S22R28R1 found that its initial firmware argument was rejected and corrected it.

Missing, because it was lost:

- CRLF/LF-tolerant serial line matching
- Strict readiness and framebuffer markers
- `[CPL3]` result classification (cooperative pass versus fault-halt)
- Diagnostic command delivery and console focus
- A QEMU shutdown-race guard

**Smallest strategy:** build one PowerShell module that wraps Main's `qac-s22r10-uefi` launcher and the .NET admission/ownership predicates, and add only a normalized-line serial parser plus a CPL3 classifier, each with hosted fixture tests (CRLF and LF transcripts). Leave out interactive command delivery until RECOVERY4 needs it.

## G. Implementation roadmap

| Milestone | Scope | Verification |
| --- | --- | --- |
| **RECOVERY2b — Build trust** (new, prerequisite) | Install `pwsh` 7. Commit the Mbed TLS glue (option b) and record the dependency lock. Build the managed NativeAOT production half and the full candidate; verify the pinned runtime source copy. | Production scripts pass unmodified. Kernel, managed ELF and runtime-pack hashes recorded. Compare against R33 identities where the source is unchanged. |
| **RECOVERY3 — Protected-task foundation** | Order per the CPU Improvements section, steps 1–4. Ported behind `GXOS_ENABLE_CPL3_DIAGNOSTIC`. Production default unchanged. | Ported hosted tests pass; production-profile build with CPL3 off matches expected identities apart from GDT/TSS. Diagnostic-on build links with no relocations. |
| **RECOVERY4 — Desktop and diagnostic integration** | `setFocus(0)` fix with a test; invocation of the CPL3 diagnostic from a shell command; check the NativeAOT expected-PF path with the new stubs; re-derive the feature-header and row-buffer issues if they recur. | Hosted compositor and NativeAOT regressions |
| **RECOVERY5 — QAC qualification** | Runner from section F. One authorized boot. | Three CPL3 cycles, continued desktop and managed Notes launch. Safe fault *recording* only; fault *recovery* is a later milestone, as in Main's S22R29. |

## H. Git preservation

- **Branch:** `dotnet-recovery2`, created from `97dc7b0d`.
- **Commit:** this report only. No source or script changes.
- **Not committed (git-ignored on purpose):** the `third_party/` overlay. It is about 50 MB, and the licensing and identity decision belongs to RECOVERY2b.
- **Build outputs:** kept under `E:\CodexArtifacts\…\dotnet-recovery2\`.
- **Push status:** SSH `ssh -T git@github.com` gives `Permission denied (publickey)` with the configured `~/.ssh/github_leon2` key. The SSH push of `dotnet-recovery2` is therefore not possible from this machine as configured. HTTPS reads work, but a non-interactive HTTPS push failed with `could not read Username for 'https://github.com'` because no credential is stored. **The branch is committed locally only and is not pushed.**
- **Remaining uncommitted work:** none tracked.

## I. Next Luna prompt

> **DOTNET_RECOVERY2B — Trusted production candidate build.** Repository `E:\dev\guideXOSServer_DOTNET_SUPPORT`, branch `dotnet-recovery2` (do not touch `main`, `v1.1_DOTNET_SUPPORT` or any CPU Improvements checkout). Prerequisite: the owner installs PowerShell 7 and fixes GitHub SSH or HTTPS push access.
>
> 1. Decide on the Mbed TLS dependency. Search non-D: backups for the original `.NET` `third_party/mbedtls` tree. If it is found, verify the 15 differing objects listed in RECOVERY2 §C against R23's `obj/third_party`. Otherwise, commit Main's `guidexos` glue to this branch in a tracked layout with `mbedtls.lock`, and keep the upstream checkout pinned to `0fe989b6` / `29160dd8`, with its local patches exported as committed `.patch` files.
> 2. Run `scripts/dotnet/build-c188-production-kernel.ps1` unmodified under `pwsh` into a fresh directory below `out\dotnet\c188-external-app-identity`, and record the hash.
> 3. Verify or re-clone `dotnet/runtime` at `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3` into `out\dotnet\gc-feasibility-baseline\nativeaot-runtime`, and hash-check the NuGet 9.0.0 ILCompiler packages against `runtime-pack.lock.json`. Then run `build-c188-production-nativeaot.ps1`.
> 4. Record all identities and compare them with the R33 candidate manifest where the source is unchanged. Do not boot QEMU or reserve QAC. Commit only scripts, locks and reports. Push `dotnet-recovery2`.
>
> Outcome A only if both production halves build and the dependency identities are locked. Then proceed to RECOVERY3 using RECOVERY2 §D and the CPU Improvements section.

## Final engineering questions

1. **Can the E: checkout produce a valid production-profile kernel?** Yes. It produces a valid ELF64 kernel with all five trusted identities (`09984b31…`), but only with a reconstructed Mbed TLS tree that differs from the qualified one in 15 of 57 objects. It is not yet trustworthy.
2. **What is still unavailable?** The original .NET `third_party/mbedtls` variant, PowerShell 7, a verified Git checkout of the pinned NativeAOT runtime source, and SSH push access.
3. **What can be reused directly?** `protected_task.asm`, the contract header, `protected_task.cpp`, the GDT/TSS/IST layout, the `0x80` exit-gate validation, and the frame-owner and task-address-space logic with their hosted tests.
4. **What needs adaptation?** The arena provider (BootInfo v1 versus v3), the exception stubs (Main's Native ELF dependency, the CPU7B2 defects, and the NativeAOT expected-PF hook), `main.cpp` hooks, and the Makefile gates.
5. **Is S22R28 a better basis than recreating ISOLATION1A?** Yes. It is guest-qualified for real CPL3, which ISOLATION1A never was, and it is shared with CPU Improvements.
6. **What is the smallest next milestone?** RECOVERY2b, a trusted full production candidate. After that, RECOVERY3 step 1.
7. **Is the work committed and pushed?** It is committed on `dotnet-recovery2`. For the push, see §H.

## Continuation — PowerShell 7 installed (November 27, 2026 Pacific)

PowerShell 7.7.0 was installed as an MSIX package. It is reachable as `%LOCALAPPDATA%\Microsoft\WindowsApps\pwsh.exe`; sessions started before the install need that path or a fresh PATH. Outcome stays **B**: both production halves now build from the committed scripts, but the Mbed TLS/TF-PSA tree is still the Main-derived substitute described in §C.

### Two committed scripts needed small corrections

1. **`scripts/dotnet/build-c188-production-kernel.ps1`.** Run unmodified, make picked Git for Windows' `usr\bin\sh.exe` from PATH and every `cmd.exe`-syntax `MKDIR` recipe failed (`/usr/bin/sh: … unexpected end of file from 'if'`). The script now removes PATH directories that contain `sh.exe` from the make child process only. The make arguments and feature list are unchanged.
2. **`scripts/dotnet/build-managed-hostlog-proof.ps1`.** The PE→ELF converter guard expected raw-file SHA-256 `5BED5BBE…`. The C101 report records that value as the *lost D: worktree's* bytes: 13,709 bytes for Git blob `addce258…`, which is a mixed CRLF/LF materialization (249 of 345 lines CRLF) that cannot be reproduced from Git. The fresh `core.autocrlf=true` checkout gives `0FD299A4…` (13,805 bytes), and the blob itself gives `43C1A71F…`, which is C101's own clean-rebuild SHA. The guard now checks the LF-normalized content against `43C1A71F…`. The raw worktree hash is still recorded as `PeToElfSha256`, and the new `PeToElfCanonicalSha256` line is added to `toolchain.txt`. The converter source is unchanged.

The overlay `third_party/mbedtls/guidexos/mbedtls_sources.mk` (git-ignored) also gained `MBEDTLS_GUIDEXOS_REPO_ROOT ?= ..`, so the unmodified kernel make call enables the same 57-source subset. The overlay file is now SHA-256 `8a887fc2…`.

### Dependency staging

| Dependency | Result |
| --- | --- |
| NuGet `runtime.win-x64.microsoft.dotnet.ilcompiler\9.0.0` | All 12 hashed files in `runtime-pack.lock.json` match |
| Pinned NativeAOT runtime sources | The non-Git copy at `E:\CodexArtifacts\…\gc-feasibility-baseline\nativeaot-runtime` (7,498 files) was copied to `out\dotnet\gc-feasibility-baseline\nativeaot-runtime`. The two lock-tracked sources (`StackFrameIterator.cpp`, `CoffNativeCodeManager.cpp`) match the locked **preimage** SHA-256 and length exactly after CRLF materialization. The other files have no lock hashes, so this is partial provenance, not a Git-verified checkout. The production profile does not use FP repair or the ThreadStore paths that need `git archive`. |
| MSVC | VS 18 Community `vcvars64.bat` |
| Python | The bundled runtime Python that the script prefers |

### Results

| Stage | Command | Result |
| --- | --- | --- |
| Kernel | `pwsh -NoProfile -File scripts/dotnet/build-c188-production-kernel.ps1 -BuildRoot out\dotnet\c188-external-app-identity\recovery2b-kernel-20261128T0010Z` | **PASS.** All script checks passed (identities, dev-marker absence, freestanding flags, ELF64 x86-64). `kernel.elf` SHA-256 `09984B31C69399E8529B86FAAC7B1D3322463E285DA5CC4156AB07620C6751B9`, **byte-identical** to the RECOVERY2 wrapper build. 57 Mbed TLS sources compiled. |
| Managed NativeAOT | `pwsh -NoProfile -File scripts/dotnet/build-c188-production-nativeaot.ps1 -BuildRoot out\dotnet\c188-external-app-identity\recovery2b-nativeaot-20261128T0030Z` | **PASS.** Profile `C160Composite`, production selectors 4–8, no CounterApp descriptor or C188 selector route, persistent composite lifecycle. `HostLogProof.elf` SHA-256 `714CBDF6C99616A46F3BED354F38B6634658505FC62064A1F33D9BF033643D86` (ELF64 x86-64, seven `PT_LOAD`, entry `0x10010e8e0`). Map `47D1E272…`, runtime-pack manifest `A96EBE3B…`, build log `8A72880D…`. NuGet warning NU1903 reports a known vulnerability advisory for the locked ILCompiler 9.0.0; the lock is unchanged. |

The R33/R36 candidate used the R23 Settings-candidate profile (proof controls, C188R30 trace, n=2 descriptors), not this proof-disabled production profile, so a hash comparison against those artifacts does not apply. The ramdisk, bootloader and ESP were not built, and no QEMU or QAC was used.

### Remaining

- The Mbed TLS decision from §C (recover the original tree, or adopt Main's variant on purpose and commit the glue) is still open.
- Push status: superseded; see the Mbed TLS re-baseline closeout below.
- The next step is RECOVERY2b items 1 and 4 (§I) and then RECOVERY3. Steps 2 and 3 of the §I prompt are now done.

## Mbed TLS re-baseline (owner decision, November 27, 2026 Pacific)

Leon decided to adopt Main Integration's Mbed TLS 4.1.0 / TF-PSA-Crypto 1.1.0 implementation for .NET Support. **This is a deliberate compatibility re-baseline, not a qualified replacement for the R23 TLS objects.** Guest qualification and the remaining TLS security checks listed below are still required before release acceptance. No search for the original .NET tree was made beyond the earlier one, and no exact backup was found.

### What is now version-controlled

All files below are copied byte-for-byte from `origin/main` (`d1b2b2ce`). Their blob IDs match Main's tracked files and Main's live checkout.

| Path | Purpose |
| --- | --- |
| `guidexos/mbedtls.lock` | Upstream identity: Mbed TLS `0fe989b6…` (tag `v4.1.0`), TF-PSA-Crypto `29160dd8…` (tag `v1.1.0`) |
| `guidexos/mbedtls_profile.json` | Patch series, plus normalized SHA-256 of 17 patched sources and the three overlay files |
| `guidexos/mbedtls_config.h`, `guidexos/crypto_config.h` | guideXOS freestanding configuration pair |
| `guidexos/mbedtls_sources.mk` | The 57-source subset; the list is identical to R23's |
| `patches/mbedtls/0001-guidexos-phase8f.patch`, `0002-…-crypto-trace.patch`, `0003-…-crypto-diagnostics.patch` | Local patches (1,268 / 293 / 455 lines) |
| `scripts/bootstrap-mbedtls.ps1`, `scripts/verify-mbedtls-profile.ps1` | Reconstruct `third_party/mbedtls` from the lock and patches, and verify it |
| `third_party/stb/stb_image.h` | Force-added at blob `9eedabed…`, the same blob Main tracks |
| `tests/phase8j_ecdsa_vectors.h` | Main's P-256 / P-384 ECDSA vectors |

The upstream tree itself (`third_party/mbedtls`, about 50 MB) stays git-ignored. It is reconstructed by `bootstrap-mbedtls.ps1 -Install`, and the kernel Makefile refuses to build if the profile or layout is missing.

.NET-side edits:

- **`kernel/Makefile`** includes `../guidexos/mbedtls_sources.mk` with `MBEDTLS_GUIDEXOS_REPO_ROOT := ..`. It hard-fails on a missing profile or incomplete layout, and it tracks the `guidexos` files as rebuild dependencies.
- **`gxos_tls_foundation.cpp`** now uses `guidexos/…` for its three config and plan path strings, matching Main.

### Source verification

1. `scripts/verify-mbedtls-profile.ps1` **passes** against the tree used for these builds (`MBEDTLS_PROFILE_VERIFY=PASS`, 3 patches, normalized-LF SHA mode).
2. An independent, offline reconstruction also matched. I took `git archive` of the pinned commits (Mbed TLS, TF-PSA-Crypto, both `framework` submodules at `dff9da04`, `mldsa-native` at `5772b4f4`), applied the three tracked patches with `git apply --check --recount`, and ran the three upstream generators. The result is **content-identical** (CRLF-insensitive) to the build tree. The only exceptions are Git-symlink placeholders under `mldsa-native/examples`, which are not compiled.
3. A networked `bootstrap-mbedtls.ps1 -Install` from GitHub was not run, because the offline reconstruction covers the same inputs.

### R23 evidence preserved; no binary-equivalence claim

The R23 kernel and its `obj/` tree remain untouched at `E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\r23-kernel-74770308256b49fd84a565a48a7fb8d3`.

The re-baselined kernel's 57 Mbed TLS objects are byte-identical to the RECOVERY2 overlay build. Against R23, **42 are identical and the same 15 differ:** `ssl_msg`, `ssl_tls`, `ssl_tls12_client`, `psa_crypto`, `psa_util`, `bignum`, `ecdsa`, `ecp`, `ecp_curves`, `psa_crypto_ecp`, `psa_crypto_rsa`, `psa_util_internal`, `pk_ecc`, `pk_wrap`, `oid`.

The only kernel C++ object that changed from the RECOVERY2 build is `gxos_tls_foundation.o` (the path strings).

### Security review of the local patch series

Patch hunks outside tracing were read line by line.

| Area | Change | Assessment |
| --- | --- | --- |
| Trace hooks (all three patches) | `GXOS_TLS_SMOKE_DEBUG_TRACE` / `GXOS_TLS_CRYPTO_STAGE_TRACE` calls | No-ops unless `GXOS_BARE_METAL && GXOS_NAVIGATOR_HTTP_SMOKE_ACTIVE`. When active they log hash, signature and public-key *prefixes* and bignum limb fragments, never private keys. **Keep smoke tracing out of release profiles.** |
| `x509_crt.c` | Return value of `pk_verify*` is captured in a local before returning | No semantic change |
| `ssl_tls12_client.c` | A TLS 1.2 ServerKeyExchange may carry the TLS 1.3 `rsa_pss_rsae_*` code points (a "compatibility exception"); log text changed | The later "was not offered" check is unchanged. The client's TLS 1.2 `signature_algorithms` list does not offer PSS, so the exception cannot be reached against a conforming server. The hosted test confirms that a PSS-only server is refused. Still a deviation from upstream. |
| `ecdsa.c` (0003) | Verify-path `mod N` reductions use a new bit-serial `ecdsa_mod_fixed` instead of `mbedtls_mpi_mod_mpi` | Applies to public values only, and handles aliasing correctly. It is custom bignum arithmetic in signature verification, so it is covered by the ECDSA regressions below, and it is a candidate for upstream-equivalence fuzzing before release. |
| PSA driver wrapper template (0003) | When the p256-m driver returns `INVALID_SIGNATURE`, the built-in verifier decides | Accepting requires the built-in verifier to succeed, so this cannot accept anything the built-in rejects. It does widen the trusted surface to the built-in path. Covered by the negative tests. |
| `mbedtls_check_config.h`, `tf_psa_crypto_check_config.h` (0003) | Config-consistency checks are skipped when `GUIDEXOS_MBEDTLS_ALLOW_PARTIAL_ECC_TLS` (set in `crypto_config.h`) | Allows TLS with only P-256/P-384. This disables upstream consistency checks, so an incomplete config would not be caught at compile time. |
| `memory_buffer_alloc.c`, `pk*.c`, `rsa.c`, `bignum*.c` (0001) | Tracing plus bounded-allocator diagnostics | No verification logic changed |

### Regressions run

**`pwsh -NoProfile -File scripts/test-mbedtls-profile-hosted.ps1`** (new). It compiles the exact 57-source manifest and guideXOS config pair for the host, builds a disposable OpenSSL 3.5.9 PKI, and runs `tests/tls/mbedtls_profile_hosted_test.c`.

**`MBEDTLS_PROFILE_HOSTED_RESULT=PASS`**: 47/47 crypto and X.509 checks and 7/7 TLS cases. Output is in `out\mbedtls-hosted\20261128T012550Z`.

- **ECDSA:**
  - Main's P-256 and P-384 vectors are accepted. Corrupted r, corrupted s, a modified hash and a zero signature are all rejected; for P-256 these go through p256-m and then the built-in fallback.
  - 8 generated keys per curve with 64 signatures each: all 512 valid signatures accepted and all 512 mutated signatures rejected, on both curves.
- **Independent signer:** 24 OpenSSL signatures each for P-256, P-384 and RSA PKCS#1 v1.5 are verified through `mbedtls_pk_verify`, and wrong-hash variants are rejected. An OpenSSL RSA-PSS signature verifies, and a corrupted one is rejected.
- **X.509:**
  - Valid EC leaf → EC intermediate → RSA root, and a valid RSA leaf, both pass.
  - Hostname mismatch gives `CN_MISMATCH`, an untrusted root gives `NOT_TRUSTED`, and an expired leaf gives `EXPIRED`.
  - A tampered leaf signature, a missing intermediate, and an intermediate without the CA bit are each refused with `NOT_TRUSTED`.
- **TLS 1.2 against local `openssl s_server`:**
  - These succeed: ECDHE-ECDSA-AES128-GCM, ECDHE-RSA-AES256-GCM with PKCS#1 sigalgs, and ECDHE-RSA with OpenSSL's default (PSS-preferring) sigalgs, which falls back correctly.
  - These are refused: a PSS-only server (no shared sigalg), a wrong hostname (`0x4`), an untrusted root (`0x8`), and an expired leaf (`0x1`).

**Not run:** the in-guest kernel TLS smoke (`scripts/smoke-navigator-kernel.ps1`) and the public-HTTPS smokes. They need QEMU or the network, and neither was authorized. Also outstanding before release:

- In-guest TLS qualification of this kernel
- Upstream Mbed TLS test suites for the patched files
- Equivalence fuzzing of `ecdsa_mod_fixed` against `mbedtls_mpi_mod_mpi`
- An owner decision on the TLS 1.2 RSA-PSS exception and on `GUIDEXOS_MBEDTLS_ALLOW_PARTIAL_ECC_TLS`

### Production rebuilds (twice each, fresh directories)

| Component | Run 1 | Run 2 | Reproducible? |
| --- | --- | --- | --- |
| Kernel (`build-c188-production-kernel.ps1`) | `9C50B233BFBAE80CA91D6106375EFC4A3FE93E8EFAA429F15F11200951322ACC` | same | **Byte-identical.** All script checks passed; 57 Mbed TLS sources. |
| Managed NativeAOT (`build-c188-production-nativeaot.ps1`) | `AD7027D165C08EC2D55351D4668CD255C2BB052BE21B87B2DA84DB2F04C3B311` | `F0120D66D4A85CE14868E072D07E17C7C414DF40A849936C8A6710DC77A490BE` | **Not byte-identical.** Both PASS with the same size (2,183,168 bytes). The 39 differing bytes are the PE header timestamp, debug-directory timestamps and the PDB GUID. The link map differs only in its timestamp line, `lnk{GUID}.tmp` names and one MSVC anonymous-namespace hash; every address is identical. The earlier `714CBDF6…` build differs the same way. Cause: `link.exe /DEBUG` without `/Brepro`, plus `cl.exe` calls for `runtime_support.c` and `c102_startup.c` without `/Brepro`. |

Out of scope for this step: adding `/Brepro` to the link command and those two `cl.exe` calls in `build-managed-hostlog-proof.ps1`. Then compare hashes across two fresh roots.
