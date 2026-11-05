# IA-64 Support: IA64-1 Architecture Audit and Build Foundation

**Status: IA64-1 is open and not complete.** This audit reached Outcome B: several build and compile-contract defects were repaired and hosted checks pass, but no IA-64 compiler, assembler, linker, simulator, firmware, or hardware runtime was available. There is no IA-64 ELF and no boot claim.

Audit date: 2026-11-04. Repository: `D:\dev\guideXOSServer_IA64`.

## Repository baseline

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
- [Intel Itanium Processor-specific ABI](https://openwatcom.org/ftp/devel/docs/ia64abi.pdf), function descriptors and GP conventions.
- [GCC IA-64 target options](https://gcc.gnu.org/onlinedocs/gcc/IA-64-Options.html).
- [GNU Binutils IA-64 assembler options](https://sourceware.org/binutils/docs/as/IA_002d64-Options.html).
- [HP Ski IA-64 Simulator Reference Manual](https://manualzilla.com/doc/7031936/hp-ia-64-user-s-manual).
