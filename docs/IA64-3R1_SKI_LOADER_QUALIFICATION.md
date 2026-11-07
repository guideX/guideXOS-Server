# guideXOS Server IA-64 — IA64-3R1 Ski Loader Qualification

**Date:** 2026-11-06
**Repository:** `D:\dev\guideXOSServer_IA64`
**Branch:** `IA64_SUPPORT`
**Starting commit:** `1bbfc9a377f33143e3fcdb5dde4366a88a27b732`
**Outcome:** **A — `SKI_LOADER_READY`**

This phase qualified the pinned HP Ski simulator in system mode and selected Ski's direct system-mode ELF loader for guideXOS. A separate, minimal IA-64 smoke ELF executed privileged instructions, used Ski SSC console and exit services, and stopped under the bounded runner. The corrected guideXOS kernel was inspected and validated statically. **It was not loaded or executed.** The record keeps `kernel_execution_authorized` false.

The stock Linux bootloader was not used: its actual load range overlaps the candidate kernel's IVT and first executable segment. The selected direct loader runs inside the host simulator, so it has no live guest code, stack, or data range to overwrite. Its less complete ELF checks are covered by the runner's independent hash-bound preflight.

## Repository and artifact preservation

- The starting branch was `IA64_SUPPORT`, at the requested commit, tracking `origin/IA64_SUPPORT` with zero commits ahead or behind. No reset, clean, merge, rebase, pull, or push was performed.
- The pre-existing untracked `scripts/__pycache__` and `tests/__pycache__` artifacts were left untouched. Test runs used `PYTHONDONTWRITEBYTECODE=1`.
- The corrected image remained `/home/guidex/src/ia64-3-ssc-corrected-20261105-b/bin/kernel.elf`, SHA-256 `b9c6053e2acfd96935c1cf83bff76f4e7516b00d004d163d74e1f2303f61d820`. It was only read by hash, ELF inspection, and static validators. No Ski command received this image.
- No QAC reservation or QEMU launch was made.

## Simulator provisioning

The source checkout `/home/guidex/src/trofi-ski-dfc2902` is clean at commit `dfc2902ea1423d9b32543a5daf8026213f2b37a1`, described as `v1.5.1-32-gdfc2902`. Ski reports package version `1.5.1`. The installed prefix is `/home/guidex/opt/ski-dfc2902`.

The build used an isolated Git archive of the pinned commit, `/usr/bin/gcc` 13.3.0 with `CFLAGS='-O2 -g'`, and `./configure --prefix=/home/guidex/opt/ski-dfc2902 --disable-netdev --with-bfd=no`. It did not change the system compiler or the guideXOS cross toolchains. The installed `ski` binary SHA-256 is `ff6b19cb87dbf2db39cda4478d0e49995c9a5ec9ddd05aac83191f4ba1e40b45`.

Ubuntu packages installed after operator approval were `autoconf` 2.71-3, `autoconf-archive` 20220903-3, `automake` 1:1.16.5-1.3ubuntu1, `libtool` 2.4.7-7build1, `gperf` 3.1-1build1, `pkg-config` 1.8.1-2build1, and `libncurses-dev` 6.4+20240113-1ubuntu2.2. `libelf-dev` 0.190-1.1ubuntu0.1, GCC, make, bison, and flex were already available. Ski's configure checks passed for ncurses and libelf; optional BFD and IA-64 unwind-library support were disabled or unavailable.

The local build produced `ski`, `ski-fake-xterm`, and symlinks `bski` and `bskinc` to `ski`. The first `bski` batch attempt printed `Unable to create console window` and stopped after five simulator instructions without reaching the guest entry or producing a guest marker. The qualified headless command is `bskinc -forcesystem`: the `bskinc` alias sets its default no-console and user-mode flags, and Ski's mode selection gives the explicit `-forcesystem` option precedence. The smoke's successful privileged PSR operation and zero-fault instruction trace verify that this invocation selected system mode.

The build and identity details are also recorded in [the loader qualification record](ia64-3r1-ski-loader-record.json).

## One-CPU system-mode smoke

The pinned Ski source hard-codes `SKI_NPROC` to `1` in `src/program.h` and initializes `nproc` to `1` in `src/state.c`. `NEW_MP` is not enabled in this build, so the `-cpus` option is absent. This is a source-enforced one-CPU simulator build, rather than a runtime CPU-count guess.

The standalone program in `tests/ia64_ski_smoke/smoke.S` is an ELF64 little-endian `EM_IA_64` executable at entry `0x100000`. It executes `rsm psr.i` and `mov r8=psr`, initializes the SSC console with function 20, writes `SKI_SYSTEM_MODE_SMOKE_PASS` using function 31, then calls SSC exit 66 with status zero. It does not link guideXOS code or use the guideXOS candidate.

The bounded runner invocation returned:

```text
SKI_SYSTEM_MODE_SMOKE_PASS
kernel exited with status 0
269 insts (0 faults), 0.00 sec, 61 cycles
```

The run completed in 1.173 seconds. `-icnt` recorded 358 retired instructions, including one `rsm` and one privileged PSR read; Ski reported 180 cycles and zero faults. Full stdout, stderr, instruction counts, and `run-result.json` are at `/home/guidex/src/ia64-3r1-ski-smoke-qualified-20261106/capture`.

Ski can record instruction counts but this build exposes no enforceable instruction ceiling in batch mode. The runner therefore uses a hard process timeout (30 seconds by default for smoke, 60 for kernels, 300 maximum), starts a new process group, and terminates the full group on timeout or exit. A negative test spawned a child process and proved timeout cleanup. No simulator child remained running.

## Stock Linux bootloader audit

The pinned Ski bootloader source is explicitly for Linux `vmlinux`, builds Linux firmware parameters, and transfers control through `jmp_to_kernel`. Its generated ELF was built from the pinned source with the existing IA-64 GCC 13.5.0 C++ compiler driver forced into C mode for `.c` files and assembler mode for `.S`, plus the existing Binutils 2.41 linker. The source checkout was not modified. `ld` warned that the resulting single PT_LOAD segment is RWE. The image SHA-256 is `ed4d32686d40f3017c79945cfa4e4501c065d72adac0b0c3bb2d2d74e12ebd2c`.

| Image / segment | Physical interval, end exclusive | File / memory size | Flags |
|---|---:|---:|---|
| Stock Ski bootloader PT_LOAD | `0x100000–0x10a990` | `0x4d30 / 0xa990` | RWE |
| guideXOS kernel PT_LOAD 0 | `0x100000–0x3cde30` | `0x2cde30 / 0x2cde30` | RX |
| guideXOS kernel PT_LOAD 1 | `0x3ce000–0x471788` | `0xa3788 / 0xa3788` | R |
| guideXOS kernel PT_LOAD 2 | `0x472000–0x4c82798` | `0x16130 / 0x4810798` | RW |

The bootloader entry and load address are both `0x100000`. Its single RWE segment contains its live code, GP-relative data, BSS, stack, ELF buffers, and firmware emulator state. The first kernel segment overwrites the entire bootloader interval `[0x100000, 0x10a990)`, including `_start`, the stack, and data, before the loader can complete its Linux handoff. This is a confirmed hard blocker for the stock bootloader, not an inferred risk.

The bootloader uses `__pa()` to clear the IA-64 region bits in `p_paddr`, reads ELF fields from a fixed 4 KiB buffer, checks the ELF magic, ET_EXEC type, and EM_IA_64 machine, then copies each PT_LOAD and zeroes `p_memsz - p_filesz`. It does not validate ELF class/data, header and program-header bounds, segment file bounds, short payload reads, size underflow, or address overflow. It calls `sys_fw_init()` for Linux firmware parameters, then passes the parameter pointer in `r28`, the entry in `b7`, and branches through `b7`. That register state and firmware protocol are Linux-specific. The guideXOS early entry expects no Linux boot parameters.

## Selected guideXOS loading strategy

The qualified strategy is **Ski's directly supported system-mode ELF path**: invoke the selected ET_EXEC image itself using `bskinc -forcesystem`. No guest-resident bootloader is loaded. Ski's system-mode ELF loader is host code, so its code, stack, temporary buffers, and BSS do not occupy the guest physical addresses where guideXOS is placed.

The pinned Ski loader reads ELF64 PT_LOAD segments at `p_vaddr` with ET_EXEC bias zero and zero-fills the memory-sized buffer before copying the file bytes. It checks `p_filesz <= p_memsz`, but the pinned implementation does not fully validate the image: it uses unchecked address addition for segment ends and accepts any non-error `read()` result, including a short read. The guideXOS runner performs the missing checks before Ski opens the image, then runs from a read-only private snapshot whose SHA-256 is rechecked. The checks include ELF64/LSB/EM_IA_64/ET_EXEC identity, entry inside executable PT_LOAD, program-header and file-image bounds, `p_filesz <= p_memsz`, checked address arithmetic, the simulator's 63-bit physical-address bound, power-of-two alignment, `p_vaddr == p_paddr` because Ski loads by virtual address, no PT_LOAD overlap, and no writable-executable segment.

The candidate's three PT_LOAD intervals are disjoint and identity-mapped (`p_vaddr == p_paddr`). All lie below `0x4c82798`, inside Ski's default `-palen 63` range. Ski's direct ELF loader has no fixed guest-physical reserved interval; its system-mode memory model maps simulated physical pages on demand. The selected path therefore has no live guest loader ranges to overlap. The runner still rejects segment overlap and addresses outside the implemented PA width before any guest launch.

The handoff is a raw branch to ELF `e_entry=0x108000`, not a function-descriptor call. Ski does not preload GP or call a C entry point. The guideXOS assembly `_start` immediately masks `PSR.i` and `PSR.ic`, loads `__gp=0x672000`, selects the 64 KiB memory stack at `0x4c5fe00..0x4c6fe00`, resets RSE state in enforced-lazy mode before `loadrs` and `ar.bspstore`, then selects eager mode and initializes the ABI FPSR before C. It installs its own IVT before enabling exception collection; external interrupts remain masked. These are static entry-contract findings; the guideXOS kernel startup has not been exercised.

The direct path passes no Linux boot parameters and makes no UEFI or Hypervisor claim. Its only platform services are Ski SSC calls. The guideXOS early-exec entry ignores the simulator's incoming general registers and supplies null C arguments itself after its own startup setup. The raw `_start` entry needs no function descriptor or preloaded GP.

## Bounded runner and authorization boundary

[`scripts/run-ia64-ski.py`](../scripts/run-ia64-ski.py) accepts an explicitly selected ELF and never rebuilds it. Kernel mode requires an exact SHA-256, an exact profile matching the manifest, the loader record above bound to both kernel and Ski hashes, independent ELF validation, early-exec source/disassembly gates, and the explicit `--authorize-kernel-guest-execution` flag. `compile-only` is rejected unconditionally. The simulator mode is fixed to `-forcesystem`; there is no user-mode selection.

The runner captures complete stdout and stderr, instruction counts, the invocation, exit status, elapsed time, and fault/timeout classification. A private read-only snapshot closes the validation-to-load race and leaves the selected source artifact unchanged. Process-group cleanup is deterministic on success and timeout.

The old build wrapper still supports `--build-only`. Its execution path delegates to the new runner and cannot launch a kernel unless the separate authorization environment flag is set and all runner gates pass. This phase did not invoke that kernel path. The loader record itself remains `kernel_execution_authorized: false`.

## Qualification results

| Check | Result |
|---|---:|
| Pinned Ski source identity, configure, build, install, and SHA-256 | PASS |
| Ski system-mode selection and privileged PSR instruction smoke | PASS; 358 instructions, 180 cycles, zero faults |
| SSC console initialization, marker output, and exit 66/status 0 | PASS |
| Single-CPU Ski build contract | PASS; source-enforced `SKI_NPROC=1` |
| Stock Linux bootloader and candidate memory overlap audit | PASS; confirmed overlap; stock loader rejected |
| Direct Ski PT_LOAD address, bounds, arithmetic, entry, and permission checks | PASS |
| Bounded timeout and child-process cleanup | PASS |
| Invalid hash, profile mismatch, unsafe memory map, missing loader record, wrong mode, absent authorization, and compile-only rejection | PASS; all rejected before simulator launch |
| `tests/ia64_foundation_contract_test.py` | PASS, 17/17 |
| `tests/ia64_early_exec_contract_test.py` | PASS, 9/9 |
| `tests/ia64_ski_loader_contract_test.py` | PASS, 8 test methods including seven pre-launch rejection cases |
| `tests/ia64_elf_validator_negative_test.py` on corrected candidate | PASS, 3/3 malformed IVT fixtures rejected |
| `scripts/validate-ia64-elf.py` on corrected candidate and manifest | PASS |
| `scripts/validate-ia64-early-exec.py` on corrected candidate | PASS, 12 static source/disassembly gates |
| Mbed TLS profile verification | PASS; Mbed TLS 4.1.0, TF-PSA-Crypto 1.1.0, three patches |
| Corrected guideXOS kernel guest execution | **NOT RUN** |

## Requirements for IA64-3R2

IA64-3R2 is the first separately authorized guideXOS instruction execution. Before that phase, preserve the candidate SHA-256 and run the selected existing ELF through the hash/profile/manifest/loader-record checks. Then request or receive explicit authorization for that exact kernel execution and invoke the runner with `kernel`, the exact `--expected-sha256`, the matching manifest and loader record, and `--authorize-kernel-guest-execution`. Keep `compile-only` ineligible. Capture the first kernel's SSC output, stop reason, and simulator register state; a smoke PASS is not a kernel boot.
