# IA64-3 Ski Guest Qualification

- **Date:** 2026-11-05
- **Branch:** `IA64_SUPPORT`
- **Starting HEAD:** `e30304ec84f08fa3d75f48cfee7fd18c19adb2e8`
- **Outcome:** **IA64_SKI_BLOCKED**

This phase did not execute guideXOS in Ski. The authorized IA64-2 image has incompatible Ski console-call numbers, no Ski executable is installed, and the repository runner does not provide a verified system-mode load path or bounded cleanup. The exact authorized image was preserved. A separately named corrected image was built and passed static checks for later authorization; it was not executed.

## Checkout preservation

The starting checkout was on `IA64_SUPPORT`, at the requested starting HEAD, one commit ahead of `origin/IA64_SUPPORT`. Its only untracked files at entry were Python caches under `scripts/__pycache__` and `tests/__pycache__`. Those caches and all pre-existing IA-64 build products were preserved. No reset, clean, push, QEMU launch, QAC reservation, Windows virtualization change, or system package installation was made.

The repository changes for this phase are limited to the IA-64 Ski SSC constants, their static contract checks, and this report.

## Authorized artifact identity and gates

The candidate at `/home/guidex/src/ia64-2-early-exec-final-20261105/bin/kernel.elf` retained the requested SHA-256:

```text
ced29ac94f03f1bb5440c47d95b98379c3e243fe41626682b6ca6e6ba30696c5
```

Independent validation confirmed ELF64 little-endian `ET_EXEC`, `EM_IA_64=50`, entry `_start=0x108000`, `__gp=0x672000`, three load segments with RX/R/RW permissions and no W+X segment, no unresolved symbols or relocations, the `early-exec` profile marker, and the exact 68-slot IVT layout. The IVT starts at `0x100000`, is executable, 32 KiB aligned, and exactly `0x8000` bytes. The 64 KiB memory stack and separate 64 KiB RSE backing store are aligned and contained in writable BSS. The artifact manifest matched the hash and profile and recorded `runtime_qualified=false`, `scheduler_qualified=false`, `interrupt_fp_preservation=false`, `guest_execution_authorized=false`, and the one-CPU external precondition as runtime-unverified.

The ELF/profile validator passed for this exact file. The Ski-specific target-disassembly gate failed on its SSC call codes, so the prior manifest's `static_gates=PASS` is not treated as proof of Ski compatibility.

## Ski SSC compatibility audit

The pinned Ski implementation defines `SSC_STOP=0`, `SSC_CONSOLE_INIT=20`, `SSC_GETCHAR=21`, `SSC_PUTCHAR=31`, and `SSC_EXIT=66`. The source handles `break.i 0x80000` and `break.i 0x80001` as SSCs in system mode, reads the call number from `r15`, and reads the first argument from `r32`. `SSC_STOP` calls the simulator's stop path; `SSC_EXIT` marks the simulator as exited. These facts were checked in the pinned [SSC definitions](https://github.com/trofi/ski/blob/dfc2902ea1423d9b32543a5daf8026213f2b37a1/src/ssc.h), [SSC dispatch](https://github.com/trofi/ski/blob/dfc2902ea1423d9b32543a5daf8026213f2b37a1/src/linux/syscall-linux.c), [break dispatch](https://github.com/trofi/ski/blob/dfc2902ea1423d9b32543a5daf8026213f2b37a1/src/exec.tmpl.c), and [stop/exit implementation](https://github.com/trofi/ski/blob/dfc2902ea1423d9b32543a5daf8026213f2b37a1/src/sim.c).

The authorized image used `0x20`, `0x21`, and `0x22`, which are decimal 32, 33, and 34. Its startup console-init therefore does not request Ski's `SSC_CONSOLE_INIT=20`; its C++ output/getchar calls likewise use the wrong function IDs. Its `SSC_STOP=0` is a recognized simulator-stop call, not a process exit. Because no simulator smoke proof was available and the console calls are incompatible, this image was not executed.

The wrapper's register interface itself is consistent: target disassembly of the authorized image shows `r15` receiving the function argument, `r32` receiving the first SSC argument, and `break.i 0x80000`; `getchar()` binds its return to `r8`. The pinned Ski implementation reads SSC return values from `r8`. This distinguishes the confirmed numeric mismatch from the independently checked register convention.

The source correction sets the Ski constants to the pinned values and makes the bounded `stop()` path call `SSC_EXIT=66` with status zero. The earlier artifact remains untouched.

## Startup and safety audit

Static source and target-disassembly checks established this intended sequence:

1. `_start` is the ELF entry.
2. `rsm psr.i | psr.ic` masks external interrupts and interrupt collection.
3. `gp` is set from `__gp`.
4. The memory stack is set up.
5. RSE is put in enforced-lazy mode before `loadrs` and `ar.bspstore`, then switched to eager mode.
6. FP-disable bits are cleared and the ABI FPSR value is installed.
7. The IVT is installed before exception collection is enabled.
8. BSS is cleared and `kernel_main` is called.
9. The early-exec branch runs `arch::init()`, emits its diagnostic and bounded startup messages through SSC, then requests SSC exit.

`arch::init()` enables `psr.ic` after IVT installation but does not set `psr.i`. The early-exec branch cannot reach timer, external-interrupt enable, device, desktop, thread, or scheduler initialization. IVT entries mask interruption state and loop in place; they do not return to unsupported execution. The corrected candidate's safety gate passed these source checks and its target disassembly checks. None of these static checks proves the Ski loader's initial state or runtime behavior.

## Ski provisioning and execution contract

The WSL distro is Ubuntu 24.04 on x86_64. No `ski`, `bski`, or `bskinc` executable was found on PATH or in the checked per-user `opt`/`src` trees. No simulator was installed. The maintained [trofi/ski source](https://github.com/trofi/ski/tree/dfc2902ea1423d9b32543a5daf8026213f2b37a1) was cloned to `/home/guidex/src/trofi-ski-dfc2902` and detached at commit `dfc2902ea1423d9b32543a5daf8026213f2b37a1` (source version 1.5.1, dated 2026-06-30). The intended isolated prefix `/home/guidex/opt/ski-dfc2902` was not created because the simulator could not be built with the available dependencies.

The source `INSTALL` lists a C compiler, GNU make, curses/ncurses, libelf, pkg-config, awk, and sed for a release build, with autotools, autoconf-archive, bison, flex, and gperf needed to develop from the Git checkout. In this WSL environment GCC/G++, GNU make, and `libelf-dev` were present; `pkg-config`, `libncurses-dev`, `autoconf`, and `automake` were absent. No package was installed or elevated. Therefore there is no local simulator version or binary build to report. Source build requirements are recorded in the pinned [`INSTALL`](https://github.com/trofi/ski/blob/dfc2902ea1423d9b32543a5daf8026213f2b37a1/INSTALL).

The pinned [Ski manual](https://github.com/trofi/ski/blob/dfc2902ea1423d9b32543a5daf8026213f2b37a1/doc/ski.1) distinguishes user mode from system mode and documents `-forceuser`/`-forcesystem`; system mode is required for this privileged kernel path. Its documented Linux-kernel route uses a Ski-specific bootloader and a `vmlinux` kernel. The pinned [bootloader README](https://github.com/trofi/ski/blob/dfc2902ea1423d9b32543a5daf8026213f2b37a1/ski-bootloader/README.md) likewise describes building that loader and invoking it with a kernel. The candidate-specific entry handoff, physical-memory map, console capture, and stop behavior were not smoke-tested. The README documents one processor in its example Linux guest, but no local simulator binary or actual launch configuration exists to prove this phase's one-CPU precondition. The pinned source has conditional `NEW_MP` code; without a locally built executable and recorded build configuration, that source alone is not runtime proof of the CPU count.

The repository runner is not qualified for this artifact. `scripts/run-ia64_in_linux.sh` rebuilds before launch, so it cannot execute the already-verified immutable file as requested. It selects `ski "$KERNEL"` or `bski "$KERNEL"` without a system-mode option or the documented Ski bootloader flow. The pinned manual's `ski` interface does not document a bare positional kernel ELF invocation, while the source README and manual differ in the batch executable name (`bski` versus `bskinc`). The runner also has no finite wall-clock timeout or deterministic process cleanup. Its separate `IA64_GUEST_QUALIFICATION_AUTHORIZED=yes` guard remains in place and was not bypassed. Runner command generation and cleanup therefore remain **unverified and blocking**.

## Controlled execution record

The prompt authorized at most one execution of the exact IA64-2 artifact, conditional on all preflight gates. The SSC gate failed and the simulator/runner setup was not qualified. **Zero simulator launches and zero guest executions were performed.** No Ski-only smoke proof was possible because Ski was not installed.

| Evidence | Result |
|---|---|
| Requested artifact hash | Verified; original hash unchanged |
| ELF/profile/IVT validation | PASS; `EM_IA_64`, entry, GP, load map, descriptors, IVT, stack/RSE, and manifest validated |
| Authorized artifact SSC target gate | FAIL; emitted function IDs 32/33/34 instead of 20/31/21 |
| Ski executable and simulator-only smoke | Unavailable; no binary installed |
| Entry reached / first guest instruction | Not observed |
| Startup checkpoints / PSR / GP / SP / RSE / IVA at runtime | Not observed |
| Guest console output / final IP / stop reason / exceptions | None; no guest ran |
| Guest instructions retired | 0 |

## Corrected candidate for later authorization

The corrected image was built in a new directory; the IA64-2 artifact was not rebuilt or replaced. The first isolated build attempt stopped at the Makefile's missing `pwsh` command and its partial directory was preserved. A second build completed in `/home/guidex/src/ia64-3-ssc-corrected-20261105-b` with GCC 13.5.0 and Binutils 2.41, using the existing target libgcc archive. The build output also reported that Windows PowerShell could not resolve the Makefile's Linux `/mnt/d/...` `-File` path; the pinned Mbed TLS profile verifier was then run separately with the equivalent `D:/dev/...` paths and passed. No system packages were installed.

Corrected image:

```text
/home/guidex/src/ia64-3-ssc-corrected-20261105-b/bin/kernel.elf
SHA-256 b9c6053e2acfd96935c1cf83bff76f4e7516b00d004d163d74e1f2303f61d820
Profile early-exec; guest_execution_authorized=false
```

It passed the independent ELF/manifest validator, all 12 early-exec source/disassembly gates, including target SSC values and register moves, and confirmed 160 target objects (`EM_IA_64`). It remains a static candidate only. No guest execution is authorized by its manifest or by this phase.

## Regression results

These tests ran under WSL with `PYTHONDONTWRITEBYTECODE=1` so the existing Python caches were preserved:

| Command | Result |
|---|---:|
| `python3 tests/ia64_foundation_contract_test.py` | PASS, 17/17 |
| `python3 tests/ia64_early_exec_contract_test.py` | PASS, 9/9; includes stale hash, mismatched profile, and missing guest-authorization rejection |
| `python3 tests/ia64_elf_validator_negative_test.py --elf /home/guidex/src/ia64-3-ssc-corrected-20261105-b/bin/kernel.elf --profile early-exec` | PASS, 3/3 malformed-IVT cases rejected |
| `scripts/validate-ia64-elf.py` on the exact IA64-2 artifact and manifest | PASS for ELF identity/profile; independent of the SSC failure |
| `scripts/validate-ia64-early-exec.py` on the exact IA64-2 artifact | FAIL, one SSC call-code gate |
| `scripts/validate-ia64-elf.py` and `scripts/validate-ia64-early-exec.py` on the corrected candidate | PASS; 160 IA-64 objects and all 12 static gates |
| `scripts/verify-mbedtls-profile.ps1` with the WSL-visible Windows paths | PASS |

The negative tests operate on fixtures and malformed ELF copies; no forbidden image was executed. Foundation tests confirmed compile-only runner rejection, and the early-exec tests confirmed the separate authorization guard.

## Next phase

Outcome C is limited to the guest-qualification gates: incompatible SSC calls in the authorized image, no built simulator, no verified system-mode loader/memory handoff for this ELF, no local one-CPU runtime observation, and no bounded runner cleanup. The smallest next step is to provision pinned Ski 1.5.1 in an isolated per-user prefix after resolving its documented build dependencies, then qualify a simulator-only system-mode smoke path and one-CPU configuration. Update the runner to accept an existing hash-bound artifact, use the proven loader invocation, and impose a finite timeout with deterministic process cleanup. Only then seek authorization for one run of the separately hashed corrected candidate.
