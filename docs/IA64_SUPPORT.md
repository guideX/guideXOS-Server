# IA-64 Support: IA64-1 Architecture Audit and Build Foundation

**Current status (2026-11-05): Outcome A for the IA64-1R2 compile/ELF milestone.** The IVT now has the architectural 68-slot layout, and the complete kernel plus pinned Mbed TLS profile builds into a full IA-64 ELF under an explicitly marked **compile-only** FP profile. Independent ELF validation passes. The default runtime profile still reproduces GCC 13.5.0 reload ICEs, and the compile-only ELF is prohibited from execution. This closes IA64-1's documented IVT and full-ELF build acceptance; it does not claim bootability, runtime safety, working interrupt handlers, or a functional scheduler. Those require a separate runtime qualification phase.

**IA64-2 status (2026-11-05): Outcome A for the static early-execution foundation.** A distinct Ski-only early-exec profile builds and passes target-disassembly and ELF/metadata gates. Its runner remains blocked pending an explicitly authorized guest-qualification phase. It does not qualify a Ski boot, prove the simulator was configured with one CPU, or qualify UEFI, scheduler, interrupt-resume, or general runtime behavior. See [IA64-2 qualification report](IA64-2_QUALIFICATION.md).

Prior audit date: 2026-11-04. Repository: `D:\dev\guideXOSServer_IA64`.

## IA64-1R1 continuation (2026-11-05, historical snapshot)

This section records the state before IA64-1R2. Its build blocker, IVT defect, and open acceptance list were superseded by the IA64-1R2 results below.

### Provisioning and provenance

- The checkout started at `f4fc89f477090b33f74914cab6f4cf29f46794ec` on `IA64_SUPPORT`, tracking `origin/IA64_SUPPORT` and one commit ahead. The worktree was clean. Existing `build/` products were preserved; the separate Hypervisor repository was not accessed.
- Ubuntu 24.04.5 LTS was installed in WSL2 after operator approval. The WSL kernel reported `6.18.33.2-microsoft-standard-WSL2` on `x86_64`. Approved packages included GNU Make 4.3, Git 2.43.0, xz 5.6.1, GnuPG 2.4.4, Python 3.12.3, `file`, and `curl`.
- The kernel.org x86_64-hosted IA-64 bundle `x86_64-gcc-13.2.0-nolibc-ia64-linux.tar.xz` was fetched from the [official 13.2.0 IA-64 index](https://www.kernel.org/pub/tools/crosstool/files/bin/x86_64/13.2.0/), installed outside the checkout at `/home/guidex/opt/ia64-gcc-13.2.0/gcc-13.2.0-nolibc/ia64-linux/`.
- The archive SHA-256 is `7611004606e79185247babeb7b1fbb9387068ca67457e109533298d26d1b8d12`, matching the checksum list whose signature verified against kernel.org autosigner fingerprint `B8868C80BA62A1FFFAF5FDA9632D3A06589DA6B1`. The archive's detached signature also verified against Arnd Bergmann fingerprint `88AFCD206B1611957187F16B60AB47FFC9095227`, fetched from the kernel.org keyring. The isolated GnuPG trust database did not establish owner trust; both full fingerprints were compared directly with the kernel.org keyring sources.
- The kernel.org bundle provides target `ia64-linux-gcc` 13.2.0 (`-dumpmachine` target `ia64-linux`) and GNU assembler/linker/readelf Binutils 2.41, but is C-only (`--enable-languages=c`) and has no `ia64-linux-g++`. Its direct C++ probe reported `error: language c++ not recognized`.
- After separate operator approval, GCC 13.5.0 source was downloaded from the official [GNU GCC release directory](https://gcc.gnu.org/pub/gcc/releases/gcc-13.5.0/). The detached signature verified with the official GNU keyring; the archive SHA-256 is `ec3df0015ed01411f91f9a9cd5b4da3070eb1222b02fadf4133c30c090399855`. The signing key's primary fingerprint is `13975A70E63C361C73AE69EF6EEB81F8981C74C7` (Richard Guenther signing subkey `3AB00996FC26A641`). The GCC [13.5 release announcement](https://gcc.gnu.org/pipermail/gcc-announce/2026/000194.html) identifies it as a bug-fix release.
- Ubuntu build dependencies installed after approval: `build-essential`, `bison`, `flex`, `texinfo`, `libgmp-dev`, `libmpfr-dev`, `libmpc-dev`, and `zlib1g-dev` (Ubuntu GCC/G++ 13.3.0, Bison 3.8.2, Flex 2.6.4, Make 4.3). GCC was configured for `ia64-linux-gnu`, C and C++, `--disable-bootstrap --disable-multilib --without-headers`, using the verified Binutils 2.41. GCC 13.5.0 installed at `/home/guidex/opt/ia64-gcc-13.5.0-cxx/`; source and build trees are `/home/guidex/src/gcc-13.5.0/` and `/home/guidex/src/gcc-13.5.0-build/`. The installed target compiler has no sysroot. Target assembler/linker/readelf remain from the verified GCC 13.2.0 kernel.org bundle.
- The bundle provides no target sysroot. Freestanding probes use `-ffreestanding -nostdlib -nostdinc -fno-builtin`; no host x86 object or library was used as a target substitute.

### Mbed TLS profile

- The original profile guard failed because `third_party/mbedtls` was absent. The approved repository bootstrap restored Mbed TLS commit `0fe989b6b514192783c469039edd325fd0989806` and TF-PSA-Crypto commit `29160dd877d29658279fd683b2ae57b320ddcf09`, including pinned nested submodules and all three tracked patches.
- `scripts/verify-mbedtls-profile.ps1 -DependencyRoot third_party/mbedtls -RepoRoot .` independently reports `MBEDTLS_PROFILE_VERIFY=PASS`, `MBEDTLS_VERSION=4.1.0`, `TF_PSA_CRYPTO_VERSION=1.1.0`, `MBEDTLS_PATCH_COUNT=3`, and `MBEDTLS_PROFILE_SHA_MODE=normalized-UTF8-LF`. The same verifier also passes from WSL via the user-local `pwsh` path adapter in `/home/guidex/bin`; no additional PowerShell package was installed.

### Target probes and ELF evidence

- A freestanding C probe compiled with the real GCC 13.2.0 target frontend after removing GCC's unsupported Linux `-mlp64` option and passing `-Wa,-xexplicit` to GAS. Its object is ELF64 little-endian `EM_IA_64` with `e_flags=0x50`, `.sdata`, `.IA_64.unwind_info`, and `R_IA64_GPREL22` relocation. The relocation resolved when linked.
- The repository's actual `kernel/arch/ia64/boot.s` assembled with target Binutils 2.41 using `-mlp64 -mle -mconstant-gp -xexplicit` and no diagnostics after adding the missing stop between `cmp.geu` and its predicated BSS store. `-xexplicit` is required because GNU as otherwise ignores the file's explicit `;;` stop markers; see the [GNU Binutils IA-64 assembler options](https://sourceware.org/binutils/docs/as/IA_002d64-Options.html).
- A freestanding C++ probe compiled with GCC 13.5.0 using the real IA-64 target. It exercises C++ code, GP-relative data, a C-linkage function call, a callback/function pointer, and unwind metadata; the target object contains IA-64 GP-relative and function-pointer relocations. The repository's actual `boot.s` and this C++ object linked with `kernel/arch/ia64/linker.ld` into a bounded ET_EXEC probe. It is **not** a full kernel ELF. The probe SHA-256 is `d2e0d6c29585209d80f3356274c1f7e466ce4e9c576b198cf4eb8bebd258246e`. Saved evidence in WSL: `/home/guidex/src/ia64-probes-2026-11-05/repository-boot-cxx-probe.readelf.txt`, `.validator.txt`, and `.sha256`.
- Independent validation passes ELF64/LSB/`EM_IA_64=50`, `ET_EXEC`, `_start=0x105000`, three separate RX/R/RW `PT_LOAD` segments, no writable-executable segment, read-only `.opd`, GP `0x308000`, one read-only `PT_IA_64_UNWIND`, no remaining relocations or unresolved symbols, and distinct 64 KiB stack (`0x109000..0x119000`) and RSE backing store (`0x119000..0x129000`) in `.bss`.
- The same validator correctly **fails** the repository IVT: `.ivt` is at `0x100000` and aligned to `0x8000`, but has size `0x4430` rather than `0x8000`; the source enumerates 69 entries instead of 68, and early entries advance by `0x100` rather than the required `0x400`. The validator also compares every source vector label against the architectural offset map. A linkable image therefore does not qualify the IVT.
- The original `-mlp64` compiler flag was rejected by this Linux target GCC (`ia64-linux-gcc: error: unrecognized command-line option '-mlp64'`). GCC documents `-mlp64` as HP-UX-specific; Linux target ABI is already LP64. `kernel/arch/ia64/Makefile.arch` now relies on that ABI for compiler flags while retaining the valid GAS `-mlp64` assembler option.

### Build and verification state

- The complete build ran with GCC 13.5.0 in the fresh output directory `/tmp/guidexos-ia64-1R1-kernel-20261105-a`; it reached Mbed TLS `library/x509.c` before failing. The exact first hard failure is preserved in `/home/guidex/src/ia64-probes-2026-11-05/full-kernel-build.log`: GCC reports `unable to generate reloads` / `internal compiler error: in find_reloads, at reload.cc:3844`, during RTL `reload`, for `{umuldi3_highpart}` generated from `year % 400` at `x509.c:596`. The same failure was reproduced with the GCC 13.2.0 C frontend and GCC 13.5.0 at `-O0`, `-O1`, and with `-mno-inline-int-divide` / `-fno-tree-ccp`.
- A bounded compiler experiment identified `-mfixed-range=f2-f127` as the constraint: with FP registers available, the same source compiles but GCC emits `setf.sig`, `xmpy.hu`, and `getf.sig` using `f6`/`f7` to implement the integer high multiply. The flag is retained because `kernel/arch/ia64/context_switch.cpp` still has TODO context-switch code, sets `fpu_state` to null, and does not preserve per-thread FPU state. Relaxing the reservation would therefore introduce unqualified FPU use into kernel code. Do not retry the full build with that reservation removed until the IA-64 kernel has a sound FPU ownership/preservation policy or the compiler backend has a verified non-FPU expansion.
- The complete build therefore produced **no full-kernel ELF**. The linked C++ probe is a bounded compiler/linker milestone only; its independent validation reports all checks passing except the two expected IVT failures.
- `scripts/build-ia64.sh` now records compiler/assembler/linker/readelf versions, full `readelf` output, SHA-256, and an independent ELF/object validation report. `kernel/arch/ia64/linker.ld` defines RX, R-only, and RW load segments; `.opd` and unwind data stay read-only. The validator checks allocated-section mapping and permissions, unwind segments, target object architecture, entry symbol, GP, function descriptors, IVT geometry and source vector offsets, relocations, unresolved symbols, and stack/RSE bounds.
- Regression checks on 2026-11-05: `python tests/ia64_foundation_contract_test.py` PASS (13 tests); `pwsh -NoProfile -ExecutionPolicy Bypass -File scripts/test-build-wrapper.ps1` PASS (exit 0); `bash -n scripts/build-ia64.sh` and `bash -n scripts/run-ia64_in_linux.sh` PASS; `scripts/verify-mbedtls-profile.ps1` PASS; `git diff --check` PASS. Independent validation of the linked C++ probe and partial kernel object directory reports 73 target objects (57 core C++ objects), all ELF64 little-endian `EM_IA_64`; it exits 1 only for the two IVT failures above. The shared `scripts/test-build.sh` was not run because it cleans the shared x86 build output. No QEMU, simulator, guest, or unmanaged runtime was launched.

### Open acceptance items and next phase

1. Decide and implement the IA-64 FPU policy before retrying the complete build: preserve/restore floating-point state in context switches, disable scheduling until a single-owner FPU policy exists, or fix GCC's IA-64 high-part multiply expansion so it honors reserved registers. Qualify generated instructions and runtime state transitions; do not simply remove `-mfixed-range=f2-f127`.
2. Fix the IA-64 IVT to the 20-by-`0x400` plus 48-by-`0x100` architectural layout, then rerun the linked vector-label validator. Keep the first-stage build claim separate from IVT qualification.
3. Define separate HP Ski and IA-64 UEFI entry contracts; implement the missing IA-64 BootInfo handoff and loader before firmware claims.
4. Implement scheduler/context-switch RSE and function-pointer/GP state, and qualify interrupts and timer delivery before enabling scheduling.
5. Keep the Hypervisor vCPU and console contract separate from Ski and firmware. No IA64-1 close or boot claim is justified by the current probes.

## IA64-1R2 continuation (2026-11-05)

### Outcome and baseline

- **Outcome A:** the architectural IVT is corrected and independently verified; a complete kernel ELF was built and independently validated; the compiler policy has a dedicated compile-only qualification; no runtime claim is made.
- The starting branch was `IA64_SUPPORT` at `fc7e736f94777f33da5357cde19e4f13026b71a2`. The starting foundation suite passed all 13 tests; there were no baseline test failures. Existing build outputs and untracked Python caches were preserved. No reset, `git clean`, push, QEMU, or guest execution was performed.
- IA64-1 closes for its IVT and compile/ELF acceptance. Boot and runtime qualification remain open work for the next phase.

### IVT correction

- `kernel/arch/ia64/boot.s` now emits exactly 68 fail-stop slots at explicit offsets: 20 slots of `0x400` bytes from `0x0000` through `0x4c00`, followed by 48 slots of `0x100` bytes from `0x5000` through `0x7f00`. Reserved entries remain at their specified slots. Offsets follow Intel Table 5-7 and the historical [Linux IA-64 IVT](https://git.zx2c4.com/linux-rng/tree/arch/ia64/kernel/ivt.S?id=a9903f04e0a4ea522d959c2f287cdf0ab029e324). The `.ivt` extent is exactly `0x8000` bytes and is aligned to `0x8000`.
- Each slot has a handler start and end symbol, and the assembler checks that the handler stays inside its slot. The linker asserts IVT base, alignment, section size, and end-symbol bounds. Placeholders disable interrupts and halt; they are not interrupt handling.
- The independent validator compares all slot names and addresses with Intel Table 5-7 and checks each linked handler's section and bounds. Three malformed-IVT negative cases (`bad-size`, `bad-handler-boundary`, and `bad-source-offset`) are rejected.

### GCC 13.5.0 reload ICE and FP policy

- With the default `IA64_FP_PROFILE=runtime`, `-mfixed-range=f2-f127` reserves all allocatable FP registers. The complete build fails in pinned Mbed TLS at both `tf-psa-crypto/platform/memory_buffer_alloc.c:214` (`{muldi3}`) and `library/x509.c:596` (`{umuldi3_highpart}` for `year % 400`), during GCC RTL `reload` with `internal compiler error: in find_reloads, at reload.cc:3844`. `-mno-inline-int-divide` does not fix the modulo expansion; bounded attempts also reproduced it at `-O0`. The runtime build log is `/home/guidex/src/ia64-1R2-evidence/runtime-build.log`.
- The standalone reproducer is `tests/ia64_x509_modulo_repro.c`. The compiler-option harness records 21 candidate/source combinations: 13 compile and 8 intentionally reproduce allocation failures. Runtime-reserved variants fail for the reproducer, `x509.c`, and bignum; adding `-mno-inline-int-divide` leaves those results unchanged. Freeing only `f6-f7` compiles the reproducer and `x509.c` but bignum still fails with an `FR_REGS` allocation error. The report is `/home/guidex/src/ia64-1R2-fp-regression-final-20261105/report.md`.
- The working compile-only policy is `-mfixed-range=f2-f5,f16-f31`: this keeps ABI callee-preserved FP registers fixed and makes caller-saved scratch ranges available. GCC generates `setf.sig`, `xmpy.hu`, and `getf.sig` using `f6`/`f7` for the standalone integer high multiply without an external helper. `x509.c` also compiles; the bignum probe compiles using `f6-f9` and retains its normal Mbed TLS/memory dependencies. The broad unrestricted candidate is recorded only as a comparison.
- Procedure-call preservation does not cover asynchronous interrupts, exceptions, or preemptive context switches. The kernel still lacks FP ownership and save/restore, including required status and high-register handling; voluntary switching is also incomplete. The compile-only ELF records `RUNTIME_QUALIFIED=no`, `SCHEDULER_QUALIFIED=no`, and `INTERRUPT_FP_PRESERVATION=unimplemented`. The runner rejects compile-only images and any build without an explicit runtime qualification record. Do not execute this ELF.

### Full build and ELF validation

- A fresh output directory was used: `/home/guidex/src/ia64-1R2-compile-only-verified-20261105`. The full build compiled all 160 target objects, including 95 core C++ objects and all pinned Mbed TLS/TF-PSA-Crypto components. The independent Mbed TLS profile verifier passed for Mbed TLS 4.1.0, TF-PSA-Crypto 1.1.0, and all three tracked patches.
- GCC 13.5.0 was configured without target headers and did not install `libgcc.a`. The matching archive was built from the GCC 13.5.0 target sources with `CFLAGS_FOR_TARGET="-O2 -mconstant-gp -mlittle-endian"`, then passed explicitly through `IA64_RUNTIME_LIBGCC=/home/guidex/src/gcc-13.5.0-build/ia64-linux-gnu/libgcc/libgcc.a`. The previously installed GCC 13.2 archive failed to link because its objects were not built for the constant-GP model. Future builds should provide a target-compatible libgcc built for the selected GP profile when the compiler does not install one.
- ELF SHA-256: `1283b0b93cbd1c273cd106c727f98b28bd9406a669fd9f841b994e88e98e87e1`.
- Independent validation reports `ET_EXEC`, ELF64 little-endian `EM_IA_64=50`, `_start=0x108000`, `__gp=0x672000`, and three non-overlapping-permission load segments: RX at `0x100000`, R at `0x3ce000`, and RW at `0x472000`. W^X separation, allocated-section mapping, and the read-only IA-64 unwind segment pass.
- The IVT is at `0x100000`, size/alignment `0x8000`, with all 68 exact offsets and bounded handler symbols. The read-only `.opd` contains 132 valid 16-byte function descriptors with GP matching `0x672000`. Stack is `0x4c5fe00..0x4c6fe00` (64 KiB); RSE backing store is `0x4c6fe00..0x4c7fe00` (64 KiB), distinct from the stack. There are no remaining relocations or unresolved symbols; all 160 objects are ELF64 little-endian `EM_IA_64`. Full report: `/home/guidex/src/ia64-1R2-compile-only-verified-20261105/ia64-elf-validation.txt`.

### Source/build repairs and verification

- Fixed the kernel object list so `.s` files are handled by the assembly-object rule rather than passed through the `.asm` substitution.
- Moved `.sbss` next to `.sdata` to keep IA-64 short-data references in range; aligned `.opd` to 16 bytes so descriptors are read and validated from the actual table boundary.
- Repaired narrow IA-64 cross-build incompatibilities without omitting subsystems: Ski's stacked-register argument setup, unsupported IA-64 sleep/nop paths, architecture-specific framebuffer and PS/2 calls, and guarded port-I/O/MSI/RNG bridge code. Unsupported operations return or remain disabled on IA-64.
- Verification results: starting foundation suite 13/13 pass; updated foundation suite 17/17 pass; malformed-IVT negative suite 3/3 pass; FP codegen harness pass (13 expected compiles, 8 reproduced failures); build-wrapper suite pass; Mbed TLS dependency verification pass; Bash syntax checks for the build, runner, and FP harness pass; `git diff --check` pass. No QEMU, simulator, or guest was started.

### Remaining runtime blockers and next phase

The IVT entries are safe fail-stop placeholders, not operational exception or interrupt handlers. The Ski entry/console is not an IA-64 UEFI loader or guideXOS Hypervisor interface. The repository still has no validated BootInfo handoff, firmware console, exception-state capture, interrupt delivery, timer path, scheduler context switch, RSE/GP switching, or FP save/restore policy. Runtime-profile GCC ICEs also remain. The next phase should implement and qualify one explicit IA-64 execution profile, including boot handoff, exception/interrupt entry, FP state ownership and preservation, scheduler/RSE context switching, and timer delivery, before enabling execution or scheduling.

The IA64-1R1 section above and the sections below preserve earlier audit snapshots. Use this IA64-1R2 continuation for the current IVT, compile, and ELF results.

## Repository baseline (2026-11-04 audit snapshot, historical)

- Starting branch: `IA64_SUPPORT`; starting HEAD: `d3cd0f12b07e6028f5c41d4fa874586872415a77`.
- `origin` is `git@github.com:guideX/guideXOS-Server.git`; the branch tracked `origin/IA64_SUPPORT` at the same commit, with no ahead/behind markers. The starting worktree was clean.
- Existing phase documentation uses named files such as `docs/PHASE*.md` and architecture/project phase documents. No tracked `.phase` marker was present. No phase marker or release marker was changed or added.
- `build/` already contained unrelated Pacman and native ABI artifacts. The audit did not remove, clean, or write into them.

## Inventory and evidence boundary

File presence is not feature acceptance. The classifications below describe what the source currently does; none is a target-runtime qualification.

| Area | Evidence | Classification |
|---|---|---|
| Architecture selection | `kernel/core/include/kernel/arch.h` defines `ARCH_IA64`, 64-bit sizing, and several device capability flags. | Source path exists. No IA-64 target compilation or device validation was performed. Capability macros are not proof of working device support. |
| Boot and linker | [`boot.s`](../kernel/arch/ia64/boot.s), [`linker.ld`](../kernel/arch/ia64/linker.ld), and `arch.cpp` define an entry, a fixed load address, GP setup, a stack, RSE backing-store area, BSS clear, and a direct call to `kernel_main`. | Partial source implementation. Entry state, load semantics, section layout, GP setup, and runtime behavior remain unverified. |
| IVT | `boot.s` emits halt handlers plus a special external-interrupt handler. | Present but architecturally mislaid and miscounted; see the IVT audit below. It is not accepted as an installed usable IVT. |
| Architecture primitives | `arch.cpp` contains inline IA-64 instructions for interrupt state, PSR, CR, cache, and RSE operations. | Source only. The compiler and assembler have not checked these instructions. |
| Register access API | `ia64.h` now makes instruction-immediate operands compile-time template parameters; current call sites use CR 0, 2, and 20. | Narrow compile-contract repair. Target assembly syntax and generated ELF flags still need target verification. |
| Context switching and scheduler hooks | [`context_switch.cpp`](../kernel/arch/ia64/context_switch.cpp) has no-op `switch_context` / `save_full_context`, a hanging `restore_full_context`, zero GP/RSE state in new contexts, and TODO timer hooks. | Unimplemented. Real scheduling must stay disabled. `s_pending_entry` and `s_pending_arg` are process-global shared state, so creating multiple contexts is not reentrant or thread-safe. |
| Console | [`ski_console.cpp`](../kernel/arch/ia64/ski_console.cpp) uses `r15`, `r32`, and `break.i 0x80000` for SSC calls. | Simulator-specific source. It is not a UEFI console or a guideXOS Hypervisor console. |
| Device code | IA-64 directories contain graphics, USB HCI, PCI audio, and syscall source files. | Unverified source, not accepted device or interrupt support. No target build or execution trace exists. |
| Core entry | `kernel_main(void*, uint32_t)` has a non-x86 path which ignores both arguments, initializes the IA-64 architecture, prints an HP ski banner, then enters an idle loop. | A simulator-oriented path. It does not consume the BootInfo v2 handoff. |
| BootInfo and loader | [`guidexOSBootInfo.h`](../guideXOSBootLoader/guidexOSBootInfo.h) defines a packed v2 BootInfo contract (`Magic=0x49425847`, `Version=2`, `Size`, flags, checksum, memory map, framebuffer, ACPI, ramdisk, NIC, and kernel physical base). The checked-in loader project and handoff trampolines are x64: the project selects x64 and the trampoline uses the Microsoft x64 register convention and x64 page-table state. | No IA-64 firmware loader or IA-64 handoff exists. The data structure alone does not make the loader ABI compatible. |
| Hypervisor | No IA-64 vCPU-entry or hypercall/console contract was found in this repository. | Not implemented or verified for IA-64. |

### Startup and ABI findings

- The startup file calls `kernel_main` with two zero values. That is consistent with the current IA-64 core path only because it discards the arguments; it does not pass the BootInfo pointer expected by the x64 loader path. The canonical structure has a v2 validator for magic, version, exact size, framebuffer bounds, and checksum, but defining that validator does not supply an IA-64 handoff.
- The code sets a single GP value, while the IA-64 ABI normally associates a GP value with a function descriptor. The flags now request constant-GP code, but the compiler's emitted descriptors, symbol values, direct branch relocations, and callback/function-pointer behavior have not been inspected. The unfinished context-switch code stores a C++ function pointer in `rp`; that must be audited against the selected ABI before scheduling is attempted. See the [Intel Itanium processor-specific ABI](https://openwatcom.org/ftp/devel/docs/ia64abi.pdf).
- The startup stack is aligned to 16 bytes and a separate 64 KiB RSE backing-store region is reserved in `.bss`, but BSS clearing, `ar.bspstore`, `loadrs`, `ar.rsc`, stack lifetime, and call-frame state have not been validated on a target. Treat them as unverified.
- The global pending-entry/argument pair in `context_switch.cpp` is overwritten by each `init_context` and consumed by the first wrapper to run. Together with the no-op switch routine and empty RSE state, this is not a scheduler implementation.

### IVT audit

Intel's Itanium Architecture Software Developer's Manual, Volume 2, §5.7 / Table 5-7 specifies a 32 KiB IVT aligned to a 32 KiB boundary. The first 20 vectors occupy 64 bundles each (0x400 bytes per slot); the remaining 48 occupy 16 bundles each (0x100 bytes per slot). In particular, offsets 0x0000–0x4c00 are spaced by 0x400, then 0x5000–0x7f00 are spaced by 0x100. See the [Intel manual](https://www.intel.com/content/dam/www/public/us/en/documents/manuals/itanium-architecture-software-developer-rev-2-3-vol-2-manual.pdf).

The current source aligns each macro-generated entry to 256 bytes, assigns the first vector comments offsets in 0x400 increments, includes a separate external-interrupt entry, and emits 68 macro entries plus that special entry (69 total). These placements do not implement the architectural slot sizes/count. The linker section is 32 KiB aligned, but that alone does not make its contents a valid IVT. The IVT rewrite is deferred; do not enable interrupts on the basis of this table.

### Execution profiles

**HP ski.** The current entry and console use Ski SSC (Simulator System Call) operations, including `break.i 0x80000`. These work only when the HP Ski simulator recognizes its SSC interface. The [HP Ski IA-64 Simulator Reference Manual](https://manualzilla.com/doc/7031936/hp-ia-64-user-s-manual) describes SSC as a system-mode simulator interface.

**Real IA-64 UEFI firmware.** UEFI firmware does not implement Ski SSC. A `break.i` is an architectural break instruction there, not a request to print a character. A firmware boot needs an IA-64 EFI image, an IA-64 ABI-compatible entry, explicit UEFI memory-map and `ExitBootServices` handling, a defined kernel handoff, and a firmware-capable console. The checked-in loader's x64 entry/trampoline and the kernel's argument-ignoring IA-64 path do not currently provide that contract.

**Future guideXOS Hypervisor execution.** A hypervisor profile needs its own documented vCPU initial state, memory mappings, interrupt and timer delivery, image-loading contract, console or hypercall interface, and firmware-service policy. It cannot inherit Ski SSC behavior or be inferred from the BootInfo structure. No such IA-64 contract is present here.

## Defects repaired in IA64-1

1. `scripts/build-ia64.sh` previously ran `make ARCH=ia64` from the repository root. The root `Makefile` is a quick-command wrapper and has no IA-64 build target. The wrapper now anchors to its own location and invokes `make -C "$REPO_ROOT/kernel"`.
2. The old wrapper ran `make ARCH=ia64 clean` and used the shared default build path. It now never cleans, uses a unique temporary output directory by default, accepts `IA64_BUILD_DIR`, anchors relative overrides at the repository root, and rejects files/non-empty directories before building. The runner shares that same isolated directory with the build.
3. The old `boot.s` used C-preprocessor `#define` macros even though the kernel make rule sends lowercase `.s` files directly to GNU `as`. The constants and halt-entry macro are now GAS `.equ` / `.macro` definitions, and calls use GAS macro syntax.
4. `kernel/arch/ia64/Makefile.arch` did not set `LINKER_SCRIPT_FLAG`, so the kernel make rule passed `arch/ia64/linker.ld` to `ld` as an input instead of as a linker script. It now passes `-T`.
5. The common kernel makefile invoked `powershell` on every host, while Linux PowerShell 7 installs as `pwsh`. It now selects `powershell` on Windows and `pwsh` elsewhere.
6. The IA-64 headers used `<cstdint>` although the kernel build sets `-nostdinc++`; they now use the kernel's C-header-backed `kernel/types.h`. The AR/CR/break wrappers also supplied runtime function parameters to GCC's immediate-only inline-assembly constraints. They now express those operands as compile-time template parameters, with current call sites updated.
7. The architecture flags claimed `-mno-pic` meant only “non-PIC.” GCC's IA-64 documentation says that option generates code without a GP register and violates the IA-64 ABI. It was removed. The C++ flags now explicitly select little-endian output, and GAS receives `-mle`, matching the linker script's little-endian ELF format. `-mconstant-gp` remains because GCC documents it as useful for kernel code.

These repairs are source/build-contract changes only. They do not establish that the remaining IA-64 code compiles or boots.

## Build instructions and current blockers

### Reproducible invocation

Run from a Linux host with Bash, GNU Make, PowerShell 7 (`pwsh`), an IA-64 cross toolchain, and an empty isolated output directory:

```bash
export PATH="$HOME/opt/ia64-toolchain/bin:$PATH"
export IA64_TOOLCHAIN_PREFIX=ia64-elf
export IA64_BUILD_DIR="$(mktemp -d "${TMPDIR:-/tmp}/guidexos-ia64-1.XXXXXX")"
pwsh -NoProfile -ExecutionPolicy Bypass -File scripts/bootstrap-mbedtls.ps1 -Install
scripts/build-ia64.sh
```

`IA64_TOOLCHAIN_PREFIX` may be `ia64-linux-gnu` if those target tools are installed. The compiler target must report `ia64` / `ia64-*`; the assembler and linker must report IA-64 support. The build wrapper prints compiler version, target triple, sysroot, assembler/linker versions, flags, and output path. It rejects an existing non-empty `IA64_BUILD_DIR` and never removes prior output.

The expected image is `$IA64_BUILD_DIR/bin/kernel.elf`. After linking, the wrapper checks ELF64, little-endian data, `EM_IA_64`, and a nonzero entry point, then prints sections, segments, and relocations using `readelf -S -l -r`. A host x64 compiler is rejected; it is never substituted.

The kernel makefile currently requires the tracked Mbed TLS profile and the imported Mbed TLS 4.1.0 / TF-PSA-Crypto 1.1.0 sources. In this audit, `third_party/mbedtls` and its version header were absent and the make profile evaluated `MBEDTLS_GUIDEXOS_LAYOUT_READY=0`. The bootstrap command above is necessary on a clean checkout before `make` can parse the kernel build. It was not run during this audit.

### Candidate toolchain provisioning

No target toolchain was present on the audit host. Debian's package index currently lists IA-64 cross compiler packages under its unofficial `debports` IA-64 port; do not assume `apt install g++-ia64-linux-gnu` works on an ordinary amd64 Debian host. Check your configured repository first: [`g++-ia64-linux-gnu`](https://packages.debian.org/sid/g%2B%2B-ia64-linux-gnu), [`gcc-ia64-linux-gnu`](https://packages.debian.org/sid/gcc-ia64-linux-gnu).

For a Linux x86_64 host, the following pinned-source recipe is a candidate way to provision an `ia64-elf` cross toolchain. It was not executed or qualified in IA64-1. Verify both GNU release signatures with the GNU release keyring before building.

```bash
sudo apt-get update
sudo apt-get install -y build-essential bison flex gawk texinfo xz-utils curl ca-certificates gnupg

export PREFIX="$HOME/opt/ia64-toolchain"
export WORK="$HOME/src/guidexos-ia64-toolchain"
mkdir -p "$WORK" "$PREFIX"
cd "$WORK"

curl -fLO https://ftp.gnu.org/gnu/binutils/binutils-2.41.tar.xz
curl -fLO https://ftp.gnu.org/gnu/binutils/binutils-2.41.tar.xz.sig
curl -fLO https://ftp.gnu.org/gnu/gcc/gcc-13.2.0/gcc-13.2.0.tar.xz
curl -fLO https://ftp.gnu.org/gnu/gcc/gcc-13.2.0/gcc-13.2.0.tar.xz.sig
gpg --verify binutils-2.41.tar.xz.sig binutils-2.41.tar.xz
gpg --verify gcc-13.2.0.tar.xz.sig gcc-13.2.0.tar.xz

tar -xf binutils-2.41.tar.xz
mkdir "$WORK/build-binutils"
cd "$WORK/build-binutils"
"$WORK/binutils-2.41/configure" --target=ia64-elf --prefix="$PREFIX" --disable-nls --disable-werror
make -j"$(nproc)"
make install

cd "$WORK"
tar -xf gcc-13.2.0.tar.xz
cd "$WORK/gcc-13.2.0"
./contrib/download_prerequisites
mkdir "$WORK/build-gcc"
cd "$WORK/build-gcc"
"$WORK/gcc-13.2.0/configure" --target=ia64-elf --prefix="$PREFIX" \
  --enable-languages=c,c++ --without-headers --disable-nls --disable-shared --disable-threads
make -j"$(nproc)" all-gcc all-target-libgcc
make install-gcc install-target-libgcc

export PATH="$PREFIX/bin:$PATH"
ia64-elf-g++ -dumpmachine
ia64-elf-g++ -print-sysroot
ia64-elf-as --version
ia64-elf-ld -V
```

This creates a freestanding compiler without a Linux sysroot. The kernel build disables standard C++ include/library use; the build itself still must prove every required header, compiler flag, instruction, relocation, and helper-library reference is satisfied. GCC's [IA-64 target options](https://gcc.gnu.org/onlinedocs/gcc/IA-64-Options.html), [cross-compiler configuration guide](https://gcc.gnu.org/install/configure.html), and GNU Binutils [IA-64 assembler options](https://sourceware.org/binutils/docs/as/IA_002d64-Options.html) are the relevant tool references.

### Audit-host toolchain and attempt

The audit ran on Windows x64 with Git Bash and MinGW-w64 host tools. Available host tools were MinGW `g++` 15.2.0 (`x86_64-w64-mingw32`, empty `-print-sysroot`), GNU `as`, `ld`, and `readelf` 2.46.0.20260210, `mingw32-make` 4.4.1, and PowerShell 7.6.5. The host linker advertised only `i386pep` and `i386pe` emulations. Those compiler/binutils target the host and are not IA-64 substitutes.

| Tool | Resolved path | Version / target evidence |
|---|---|---|
| C++ compiler | `C:\mingw64\bin\g++.exe` | MinGW-w64 15.2.0; `x86_64-w64-mingw32`; empty sysroot |
| Assembler | `C:\mingw64\bin\as.exe` | GNU Binutils 2.46.0.20260210; MinGW x86-64 |
| Linker | `C:\mingw64\bin\ld.exe` | GNU Binutils 2.46.0.20260210; emulations `i386pep`, `i386pe` |
| ELF reader | `C:\mingw64\bin\readelf.exe` | GNU Binutils 2.46.0.20260210 |
| Make | `C:\mingw64\bin\mingw32-make.exe` | GNU Make 4.4.1 |
| PowerShell 7 | `C:\Users\guideX\.cache\codex-runtimes\codex-primary-runtime\dependencies\native\powershell\pwsh.exe` | 7.6.5 |

The IA-64 compiler, assembler, linker, and simulator names listed below were not resolved on `PATH`; therefore no IA-64 target triple or sysroot was available to report.

The following IA-64 tools were absent: `ia64-linux-gnu-g++`, `ia64-linux-gnu-as`, `ia64-linux-gnu-ld`, `ia64-elf-g++`, `ia64-elf-as`, and `ia64-elf-ld`; no IA-64 `readelf`, `ski`, or `bski` was found. WSL had no installed distribution. The isolated build wrapper therefore returned `TOOLCHAIN_BLOCKED` before invoking Make. A separate `mingw32-make -C ... ARCH=ia64 ... -n info` parse attempt returned exit 2 because the Mbed TLS dependency/profile guard is not satisfied. No target compile, link, ELF inspection, or emulator launch occurred.

## Verification record

| Check | Result | Evidence |
|---|---|---|
| Hosted IA-64 foundation contract tests | PASS: 10 tests, 0 failures | `python tests/ia64_foundation_contract_test.py` |
| Existing build-wrapper self-test | PASS, exit 0 | `pwsh -NoProfile -File scripts/test-build-wrapper.ps1` |
| `scripts/build-ia64.sh` Bash syntax | PASS, exit 0 | `C:\Program Files\Git\bin\bash.exe -n scripts/build-ia64.sh` |
| `scripts/run-ia64_in_linux.sh` Bash syntax | PASS, exit 0 | `C:\Program Files\Git\bin\bash.exe -n scripts/run-ia64_in_linux.sh` |
| Absent target compiler negative test | PASS, expected exit 2 | Run from a temporary caller directory with `IA64_TOOLCHAIN_PREFIX=ia64-absent-contract-test`; wrapper resolved the repository from the script path and reported the missing target tool. |
| Malformed output path negative test | PASS, expected exit 2 | `IA64_BUILD_DIR=kernel/Makefile`; wrapper rejected a file before checking or invoking the toolchain. |
| Existing non-empty output path negative test | PASS, expected exit 2 | `IA64_BUILD_DIR=kernel`; wrapper rejected it without touching contents. |
| Kernel Makefile dry-run | BLOCKED, exit 2 | `mingw32-make -C D:/dev/guideXOSServer_IA64/kernel ARCH=ia64 BUILD_DIR=D:/temp/guidexos-ia64-contract-output -n info`; Mbed TLS profile guard stopped parsing before the target rule. |
| IA-64 target compile/link and ELF checks | BLOCKED | No IA-64 cross compiler, assembler, or linker. No ELF exists to inspect for class, endianness, entry, sections, segments, relocations, or `EM_IA_64`. |
| IA-64 execution | NOT RUN | No Ski runtime or IA-64 machine was available. No QEMU was launched. |

No IA-64-specific automated tests existed before this change. The broad `scripts/test-build.sh` was not run because it begins with `make ARCH=x86 clean`, modifies the shared x86 build output, and is unrelated to IA-64 hosted contracts. No existing user artifacts were cleaned.

## Next phase

1. Provision an IA-64 toolchain and the repository's pinned Mbed TLS dependencies in a Linux build environment. Run the isolated compile/link and inspect the target ELF. Resolve any remaining compile errors from evidence, including the GP/link model and IA-64 relocation set.
2. Define separate startup contracts for HP Ski and real IA-64 UEFI. Add an IA-64 loader and explicit BootInfo/entry ABI before claiming firmware support. Keep Ski SSC confined to the Ski profile.
3. In a dedicated IVT phase, implement the architectural 20-by-0x400 plus 48-by-0x100 layout, verify the vector offsets and total 32 KiB extent, and validate the linked address against `cr.iva` requirements.
4. In a separate scheduler/RSE phase, implement and validate register-stack save/restore, GP/function descriptors, per-thread entry arguments, stack/backing-store lifetime, interrupt state, and timer delivery. Do not enable scheduling until reentrancy and multi-context tests pass.
5. Define the future Hypervisor vCPU/entry/console contract separately from firmware and Ski, then qualify each profile only in its matching runtime.

## References

- [Intel Itanium Architecture Software Developer's Manual, Volume 2](https://www.intel.com/content/dam/www/public/us/en/documents/manuals/itanium-architecture-software-developer-rev-2-3-vol-2-manual.pdf), §5.7 and Table 5-7 (IVT), plus Register Stack Engine sections.
- [Historical Linux IA-64 IVT implementation](https://git.zx2c4.com/linux-rng/tree/arch/ia64/kernel/ivt.S?id=a9903f04e0a4ea522d959c2f287cdf0ab029e324).
- [Intel Itanium Processor-specific ABI](https://openwatcom.org/ftp/devel/docs/ia64abi.pdf), function descriptors and GP conventions.
- [GCC IA-64 target options](https://gcc.gnu.org/onlinedocs/gcc/IA-64-Options.html).
- [GNU Binutils IA-64 assembler options](https://sourceware.org/binutils/docs/as/IA_002d64-Options.html).
- [HP Ski IA-64 Simulator Reference Manual](https://manualzilla.com/doc/7031936/hp-ia-64-user-s-manual).
