# IA64-2 Qualification Report

**Date:** 2026-11-05
**Repository:** `D:\dev\guideXOSServer_IA64`
**Branch:** `IA64_SUPPORT`
**Starting HEAD:** `f193c63e0bce66eb6f378a3d7fa09263cb56bb42`
**Outcome:** **A — static early-execution foundation established; guest execution remains blocked.**

This closes the IA64-2 foundation milestone only. It does not qualify a successful boot, IA-64 interrupts, scheduler operation, user-mode execution, or general guest runtime. The user explicitly did not authorize a QAC reservation or a QEMU launch; neither was made.

## What changed

- Added `IA64_FP_PROFILE=early-exec`, a distinct SKI-only build identity. It compiles with ABI-preserved FP registers fixed (`f2-f5`, `f16-f31`) and ABI scratch registers available. The linker records a separate early-exec marker; compile-only and early-exec images cannot share the same profile metadata.
- The early-exec `kernel_main` path performs architecture initialization, writes a short SKI console message, calls SKI's stop service, and falls back to a halt loop if that service returns. It does not call interrupt initialization, the desktop tick, timers, thread creation, or scheduling.
- Startup now enters enforced-lazy RSE mode before `loadrs` and writing `ar.bspstore`, then selects eager RSE mode. Startup also clears `psr.dfl` and `psr.dfh`, and sets `ar.fpsr` to the IA-64 ABI default before entering C.
- Added an aligned, compile-checked storage layout for 126 mutable FP registers plus FPSR. It is storage only: no register save/restore instructions or per-thread allocation are implemented. The existing voluntary and exception context structures are now labeled incomplete and non-operational.
- Added ELF profile markers, SHA-256-bound artifact manifests, static source/disassembly gates, and fail-closed runner checks. The compile-only runner guard remains in place. Early-exec needs both passing metadata and `IA64_GUEST_QUALIFICATION_AUTHORIZED=yes` before the Ski runner will build and proceed to launch.

## Runtime-state ownership audit

The ordinary procedure-call rules below come from Intel's [IA-64 Software Conventions and Runtime Architecture Guide](https://portal.cs.umbc.edu/help/architecture/24525601.pdf). They describe P64 calling convention behavior, not a full trap frame or operating-system context switch.

| State | Ordinary C procedure call | Synchronous voluntary switch | Asynchronous interruption / exception |
|---|---|---|---|
| General registers | `r0` is constant. `r2-r3`, `r8-r11`, `r14-r31`, and output registers are scratch/caller-save. `r4-r7` are callee-save, including their NaT state. `r32-r127` are stacked: input/local state is automatic across calls; output registers are scratch. | At a call boundary, the compiler preserves live caller-save values around the call. The switch must retain `r4-r7`, stacked frame state, the call return point, and all resources required to resume the suspended call. | Save all mutable registers, including scratch registers and NaT state. ABI caller-save status is irrelevant when interruption can occur between any two instructions. |
| GP, SP, TP, IP, descriptors | `r1` is GP: valid on procedure entry and restored for the returning procedure; calls may change it. `r12` is the 16-byte-aligned memory SP. `r13` is the ABI-specific thread pointer. A function pointer is an IA-64 descriptor/plabel carrying entry and GP. A call places its return address in `b0`. | Preserve the task's GP, SP, TP, return point, and function-descriptor semantics. Rebuild descriptors with the target GP when creating entry frames. | Capture interrupted IP/slot state, GP, SP, and TP. A kernel-to-user return must supply a valid user entry point, GP, stack, TP, and privilege/status image. |
| Floating point | `f0/f1` are constants. `f2-f5` and `f16-f31` are callee-save. `f6-f15` and `f32-f127` are scratch; `f8-f15` also carry FP args/returns. FPSR control fields follow the ABI; status-field 1 must have standard call-boundary controls. | Save preserved FP values and FPSR. Scratch state live across the call is caller-save. A scheduler design that owns full state may save all mutable FP registers eagerly. | Preserve all mutable `f2-f127` and FPSR before any path can resume interrupted code. This includes compiler-generated integer arithmetic using FP registers. Preserve PSR FP-disable/dirty state in the trap/task state as required by the selected ownership policy. |
| Predicates and branches | `p0` is true; `p1-p5` and rotating `p16-p63` are preserved; `p6-p15` are scratch. `b0` is the return link/scratch; `b1-b5` are preserved; `b6-b7` are scratch. | Preserve ABI-preserved predicates and branches, plus the actual return branch register. | Capture all mutable predicate and branch state; scratch registers may contain live interrupted state. |
| Application and interruption state | `ar.unat` and `ar.lc` are preserved; `ar.ec` is part of PFS; `ar.ccv` is scratch. `ar.bsp`, `ar.bspstore`, and `ar.rsc` describe RSE state and are not ordinary C locals. | Preserve PFS, LC/EC, UNAT, FPSR, RSC, BSP/BSPSTORE, and RNAT state needed to resume the register stack. | Capture the full RSE state, including NaT collection state, and the interruption control image (`CR.IIP`, `IPSR`, `IFS`, plus vector-specific `ISR`, `IIM`, `IFA`, `IHA`, and related state). `PSR` interruption-control bits determine whether entry/return is safe. |

Intel's [architecture manual, Volume 2](https://www.intel.com/content/dam/www/public/us/en/documents/manuals/itanium-architecture-software-developer-rev-2-3-vol-2-manual.pdf) states that `loadrs`, and reads or writes of BSPSTORE/RNAT, require `RSC.mode=0` (enforced-lazy mode). The startup order now matches that rule. Historical Linux IA-64 startup and switch code use the same lazy-mode setup and explicitly save switch-stack state; see [Linux `head.S`](https://kernel.googlesource.com/pub/scm/linux/kernel/git/jic23/iio/+/1da177e4c3f41524e886b7f1b8a0c1fc7321cac2/arch/ia64/kernel/head.S) and [Linux `entry.S`](https://git.zx2c4.com/linux-dev/tree/arch/ia64/kernel/entry.S?id=ca0e68e21aae10220eff71a297e7d794425add77). Those files are reference implementations, not code imported into this kernel.

For a full context switch, preserving only `r4-r7` and the ABI-preserved FP bank is insufficient for an asynchronous entry. The interrupted compiler may have live caller-save GRs, FP scratch registers, predicates, branches, NaT bits, and an in-progress stacked frame. IA64-2 leaves these paths blocked.

### Single-CPU early-boot contract

The artifact is explicitly for the HP Ski IA-64 ELF path: one CPU enters `_start`, one kernel execution context continues, no scheduler or thread creation, no timer preemption, external interrupts remain masked (`PSR.i=0`), and exceptions use fail-stop IVT entries. `PSR.ic` is enabled only to route exceptions to those entries. An unimplemented exception masks interruption collection and spins without resuming.

The code and disassembly gates establish that this image creates no additional context and that its reachable `kernel_main` does not activate scheduler/interrupt/timer paths. **They cannot observe or enforce Ski's configured processor count.** The artifact manifest records `exactly-one-CPU-external-SKI-precondition-runtime-unverified`; the next guest phase must configure and verify one CPU. Runtime qualification is therefore still blocked. No EFI or guideXOS Hypervisor assumptions are folded into this SKI path.

## FP allocation and context policy

GCC 13.5.0's runtime-reserved profile (`-mfixed-range=f2-f127`) still fails reload for integer high-part multiplication. The reproduced Mbed TLS `% 400` case lowers through `umuldi3_highpart`; making only ABI scratch registers available compiles the repro with `setf.sig`, `xmpy.hu`, and `getf.sig` on `f6-f7`. The wider bignum case uses `f6-f9`. The compiler harness reproduced 8 expected failures and 13 expected successful compile cases across the tested profiles; the output is saved at `/home/guidex/src/ia64-2-fp-codegen-20261105/report.md`.

The early profile uses these scratch registers only on the single, non-preemptible flow. Its external interrupts stay masked; any synchronous exception enters a terminal fail-stop vector and cannot return to code with clobbered state. This is an ABI-based restriction, not proof of runtime behavior. The compile-only profile remains prohibited from execution. The production/runtime profile still reserves all allocatable FP registers and does not build because of the GCC reload ICE.

For full runtime, prefer an eager per-thread policy initially: save all mutable `f2-f127` images plus FPSR, and save/restore the related PSR FP ownership/disable state with interrupt exclusion. The typed `FpContextState` layout is 126 16-byte register slots followed by FPSR and padding: offset 2016, size 2032, alignment 16. Its target C++ static assertions passed during the complete IA-64 build. It does not include an executable save/restore implementation, and is not yet a complete task frame. A later lazy policy would need a first-use trap, explicit CPU/thread ownership, high-register (`f32-f127`) handling, and exception-safe save/restore; it is not implemented here.

## Boot-contract separation

- **HP Ski:** the artifact is ELF64 `EM_IA_64`, linked at 1 MiB, enters through this repository's `_start`, initializes a Ski SSC console, and calls `kernel_main(0, 0)`. Its console uses `break.i 0x80000`. SSC interception, loader entry state, and one-CPU configuration require guest qualification.
- **IA-64 UEFI:** this ELF/startup is not a UEFI image. UEFI's Itanium procedure interfaces use P64 C calling conventions, but an IA-64 UEFI image has a PE32+ entry/plabel GP and receives `ImageHandle` and `SystemTable`; the UEFI specification also warns that `f32-f127` are not ordinarily preserved across its conventions. See [UEFI Specification 2.9](https://uefi.org/sites/default/files/resources/UEFI%20Spec_2_9_2021_03_18.pdf). A separate PE/COFF loader, BootInfo, GP/descriptor setup, memory-map handoff, and post-`ExitBootServices` transition are required.
- **guideXOS Hypervisor:** treat its vCPU state, console, memory, and entry contract as a third interface. No Hypervisor repository or vCPU launch path was accessed or inferred in this phase.

## Artifact and verification evidence

The fresh artifact is `/home/guidex/src/ia64-2-early-exec-final-20261105/bin/kernel.elf`; it has the distinct `early-exec` linker marker. SHA-256 is `ced29ac94f03f1bb5440c47d95b98379c3e243fe41626682b6ca6e6ba30696c5`. Its manifest binds that hash to profile, single-CPU precondition, and safety-gate status. It records `runtime_qualified=false`, `scheduler_qualified=false`, `interrupt_fp_preservation=false`, and `guest_execution_authorized=false`.

The prior compile-only ELF was left unchanged: `/home/guidex/src/ia64-1R2-compile-only-verified-20261105/bin/kernel.elf`, SHA-256 `1283b0b93cbd1c273cd106c727f98b28bd9406a669fd9f841b994e88e98e87e1`.

The early-exec ELF passed independent validation as ELF64 little-endian `EM_IA_64=50`, ET_EXEC, `_start=0x108000`, `__gp=0x672000`, three RX/R/RW load segments, W^X separation, one read-only IA-64 unwind segment, read-only function descriptors, no unresolved symbols or relocations, 68 exact IVT slots, and distinct aligned 64 KiB memory-stack and RSE regions. All 160 target objects (95 core C++ objects) were ELF64 little-endian `EM_IA_64`. The static early-exec gate passed source and target-disassembly checks for RSE/FP initialization, the interrupt mask, and the bounded entry call path.

Commands and results:

| Command / gate | Result |
|---|---:|
| `IA64_FP_PROFILE=early-exec IA64_TOOLCHAIN_PREFIX=/home/guidex/src/ia64-2-toolchain-shim/ia64-linux IA64_RUNTIME_LIBGCC=/home/guidex/src/gcc-13.5.0-build/ia64-linux-gnu/libgcc/libgcc.a IA64_BUILD_DIR=/home/guidex/src/ia64-2-early-exec-final-20261105 IA64_JOBS=8 bash scripts/build-ia64.sh` | PASS; GCC 13.5.0, Binutils 2.41; 160 target objects |
| `python tests/ia64_foundation_contract_test.py` | PASS, 17/17 |
| `python tests/ia64_early_exec_contract_test.py` | PASS, 8/8 |
| `python tests/ia64_elf_validator_negative_test.py --elf /home/guidex/src/ia64-2-early-exec-final-20261105/bin/kernel.elf --profile early-exec` | PASS, 3/3 malformed-IVT cases rejected |
| `scripts/test-ia64-fp-codegen.sh /home/guidex/src/ia64-2-fp-codegen-20261105` with GCC 13.5.0 / Binutils 2.41 | PASS; 13 expected compiles, 8 expected allocation failures reproduced |
| `pwsh -NoProfile -ExecutionPolicy Bypass -File scripts/test-build-wrapper.ps1` | PASS |
| `bash -n scripts/build-ia64.sh scripts/run-ia64_in_linux.sh` | PASS when checked individually |
| `git diff --check` | PASS |

The runner-negative test confirms that early-exec is rejected before build unless `IA64_GUEST_QUALIFICATION_AUTHORIZED=yes`; metadata still must pass after that gate. Compile-only execution rejection remains tested. No guest, QAC, QEMU, firmware, or Hypervisor was launched.

## Remaining blockers and recommended next phase

The remaining IA64-2-to-runtime blockers are: verify the SKI entry/SSC behavior on a configured one-CPU guest; implement and test exception-state capture and return; implement FP owner/save/restore for asynchronous exceptions and threads; implement RSE/NaT-aware voluntary switching; and add a separate boot contract for UEFI or the guideXOS Hypervisor if either is selected.

Recommended first guest-qualification phase: authorize an SKI-only, one-CPU launch of this exact SHA-256 artifact. Confirm entry mode and `PSR.i/PSR.ic`, observe the bounded console report and Ski stop, and record any fail-stop vector. Keep UEFI and Hypervisor bring-up out of that phase. The runner remains blocked until a separately authorized guest-qualification phase is initiated.
