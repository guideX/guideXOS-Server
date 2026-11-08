# IA64-4B Break Exception Qualification

**Date:** 2026-11-08
**Outcome B — `IA64_EXCEPTION_FOUNDATION_PARTIAL`**

IA64-4B implemented a bounded Break Instruction exception path for the Ski-only `early-exec` profile. The independent diagnostic ELF passed both slot tests in Ski. One guideXOS kernel run reached the correct Break vector twice, emitted valid records, returned through both intended continuations, and exited through SSC. The run nevertheless failed the CPU-state checkpoint contract: after the test, `PSR.ic` was 0 instead of the 1 observed before the test. The test cleanup was corrected and a new kernel candidate passed static qualification, but that candidate was not executed because this phase allowed at most one kernel execution. The exception-return behavior therefore remains partially qualified.

This result preserves the accepted [IA64-4AR1 CPU-state checkpoint](IA64-4AR1_CPU_STATE_RUNTIME_REQUALIFICATION.md) baseline. It does not qualify general exception dispatch, external interrupts, asynchronous recovery, timer delivery, scheduler operation, devices, UEFI, user processes, or desktop startup.

## Scope and preservation

- Repository: `D:\dev\guideXOSServer_IA64`; branch `IA64_SUPPORT`; starting HEAD `e82f5fc9c43e9523c95cf8caeeb59c4073b322ef`, tracking `origin/IA64_SUPPORT`.
- The accepted IA64-4AR1 candidate remains the baseline: ELF SHA-256 `49f0ea7b0d68390916cf6ab7b7db3561825da34aeb45e8b8efd8e8ab01106af5`.
- Existing Python cache files and directories were preserved and are not part of the scoped source change. No reset, clean, merge, rebase, or push occurred.
- No QEMU or QAC execution occurred. The runner and build artifacts are under `/home/guidex/src/ia64-4b-break-exception-20261108-*`; no historical candidate or capture was overwritten.
- IA64-4B authorizes at most one guideXOS kernel launch. Candidate D was the sole launch. The corrected candidate E described below remains unexecuted.

## Architecture contract

The implementation follows the Intel Itanium Software Developer's Manual and the long-standing Linux IA-64 vector layout. The Break Instruction vector is at offset `0x2C00`. On the interrupted path, the handler uses `CR.IIP` as the aligned bundle address and the saved `IPSR.RI` plus `ISR.EI` to identify the instruction slot. `CR.IIM` carries the Break immediate. The record also captures `CR.IPSR`, `CR.IFS` validity, `CR.ISR`, handler-entry `PSR`, `RSC`, and `BSP`.

The faulting instruction is not treated as though `IIP` already points after it. For the slot-1 test (`RI=1`), the handler keeps the bundle IIP and prepares `RI=2`, so `rfi` resumes at slot 2. For the slot-2 test (`RI=2`), it advances IIP by one 16-byte bundle and clears RI, so `rfi` resumes at the next bundle's slot 0. A false/true predicate pair makes the continuation count exactly one regardless of the resumed slot; the test checks the count and a distinct return marker after each fault.

The test raises `break.i 0x6A17` and `break.i 0x6A18`. The pinned Ski source checkout was `/home/guidex/src/trofi-ski-dfc2902`, commit `dfc2902ea1423d9b32543a5daf8026213f2b37a1`, Ski 1.5.1. Source inspection confirmed the system-mode SSC immediates are `0x80000` and `0x80001`, with separate Linux ABI and firmware-reserved ranges; the selected immediates fall through to the ordinary Break fault path. The probe's observed two faults, exact IIM records, and two `rfi` returns independently corroborated that dispatch.

The test enters with the interrupted state selecting bank 1 and `BN=1`; vector entry uses the alternate bank so the handler saves working registers in bank 0. It validates the saved state and uses `BSW` to verify the known values in banked `r16-r31`, along with the predicate register, GP (`r1`), branch register (`b6`), stack pointer, `RSC`, and `BSP`. Its stack spills use NaT-aware operations. The handler itself does not call C++, allocate a frame, spill/fill, or change `BSPSTORE`; it uses bounded static record storage and scratch registers. Unexpected vectors, slots, immediates, stale records, or state mismatches enter a local fail-stop loop.

The return path changes only the intended IIP/RI fields in the saved IPSR, synchronizes the control-register write, restores the predicate state, and executes `rfi`. On this test path the saved frame has `IPSR.IC=1`; handler-entry `PSR.IC` and `PSR.i` are both clear. Intel documents `rfi` as undefined with current `PSR.IC` or `PSR.i` set, and requires a complete interruption frame when saved `IPSR.IC=0`. The test does not enable `PSR.i`; it temporarily controls `PSR.ic` only to safely switch register banks during cleanup.

References:

- [Intel Itanium Architecture Software Developer's Manual, Volume 2](https://www.intel.com/content/dam/www/public/us/en/documents/manuals/itanium-architecture-software-developer-rev-2-3-vol-2-manual.pdf), Break Instruction and instruction-bundle definitions.
- [Intel Itanium Architecture Software Developer's Manual, Volume 3](https://www.intel.com/content/dam/www/public/us/en/documents/manuals/itanium-architecture-vol-3-manual.pdf), IVT, interruption registers, register banks, RSE, and `rfi` requirements.
- [Historical Linux IA-64 IVT](https://kernel.googlesource.com/pub/scm/linux/kernel/git/rric/linux/+/fba69f042ad99f68c0268ef1c012f3199f898fac/arch/ia64/kernel/ivt.S), vector organization and exception-entry patterns.

## Implementation

- `kernel/arch/ia64/boot.s` selects a branch stub to the bounded handler only for the `early-exec` profile's Break vector. It preserves the `0x8000` IVT extent and all 68 slot offsets; all other vectors and all non-early-exec Break vectors remain fail-stop.
- `kernel/arch/ia64/break_exception_entry.s` validates the two known interruption states, rejects replay/stale records, stores a fixed record, adjusts IIP/RI by slot, restores the interrupted predicates, and returns with `rfi`. Its isolated test establishes and checks the banked and callee-preserved test state.
- `kernel/arch/ia64/break_exception.cpp` performs bounded post-return validation and reports records only after the sensitive handler has returned. The record is schema version 1 and exactly 128 bytes, with an explicit validity marker, vector/immediate, saved registers, completion and return markers, continuation count, and predicates.
- The kernel Makefiles link this code only for `IA64_FP_PROFILE=early-exec`. `kernel/core/main.cpp` runs the test before the existing deliberate stop. No scheduler, device, timer, or external-interrupt path is activated.
- `scripts/validate-ia64-break-exception.py` adds source and target-disassembly gates. `scripts/validate-ia64-elf.py` now parses the conditional vector definition and checks the correct vector kind for each profile. `scripts/run-ia64-ski.py` adds an isolated `break-probe` operation, strict output/evidence parsing, candidate-bound manifest and loader checks, and kernel-run resource-limit requirements.

## Independent Ski probe

The independent assembly ELF was built in a separate directory and loaded by the qualified Ski 1.5.1 system-mode runner on one CPU. Its ELF SHA-256 was `22d3c244eb40b2bcab8290d3b51a84e935e723e9bddf778ba83b682f5321fdc8`. The Ski executable SHA-256 was `ff6b19cb87dbf2db39cda4478d0e49995c9a5ec9ddd05aac83191f4ba1e40b45` and its source was the pinned commit above.

Probe result: `BREAK_EXCEPTION_PROBE_PASS`, process exit 0, timeout false, cleanup true, two expected Break faults, and 110,162 instructions. Both slot continuations executed once and record validation passed. Captured run-result SHA-256: `2876ab02a92168d4e437a03dd7b7d5d347b159d24da2dccb5062d9baa5e406ef`. Captured stdout SHA-256: `43b1912584d444752a15cd7d945257e5d5980ab0009cfb81556950aef1cb529c`; stderr SHA-256: `83d8bdb80012bb2c39ada0276d2c1b3bd0a67f4aa61b2bf09db7a8dd527e6da3`; instruction-count evidence SHA-256: `fd8cb4fc39fe3196b45dee986921f392a31ca7831d3e32e3931de9a13a3e1967`.

The independent probe met the prerequisite for a kernel launch. Its pass did not qualify kernel checkpoints, so the kernel result below remains decisive for the phase outcome.

## One authorized kernel run

All independent ELF, artifact-manifest, loader, IVT, early-exec, CPU-state, Break-source/disassembly, probe, and runner gates passed before the one launch. Candidate D was system-mode loaded with the pinned Ski binary, one CPU, private capture, exact hash/manifest/load-range checks, address-space limit 2 GiB, CPU limit 120 seconds, and wall timeout 60 seconds. The explicit kernel-execution authorization flag was supplied. The candidate was `/home/guidex/src/ia64-4b-break-exception-20261108-d/bin/kernel.elf`, SHA-256 `7a54cad317decf5d14b930f7463ac92bd639400214b7bec2387c03c91f162e15`; the manifest SHA-256 was `ed6376e741477cba67fe55207bd671d579edd49eebb56822ac220476eedf6850`.

The guest produced two valid records at vector `0x2C00`. The slot-1 record captured immediate `0x6A17`, IIP `0x3D0490`, slot 1, IPSR `0x120000002000`, IFS-valid 0, ISR `0x20000000000`, handler PSR 0, RSC 3, BSP `0x4C72E48`, completion/return markers, continuation count 1, and PR `0x401`. The slot-2 record captured immediate `0x6A18`, IIP `0x3D0740`, slot 2, IPSR `0x140000002000`, IFS-valid 0, ISR `0x40000000000`, handler PSR 0, RSC 3, the same BSP, its completion/return markers, continuation count 1, and PR `0x401`. Guest validation printed `errors=0x0 test=0x0` and `IA64_BREAK_EXCEPTION_RESULT=PASS`.

Ski exited with status 0 after 56,674,856 instructions; there were exactly two expected Break faults and two `rfi` returns. The runner observed no timeout and successful cleanup. Thus the observed faults were attributed to the two intended Break instructions, not an unexpected exception. Run-result SHA-256: `0565b311eb748afbbfb49fed0377cca4f1b3f341e7b6376311d734dbbc2a741e`; stdout SHA-256: `d0948a35c7ca18217b4d7e9a7a240be212d6b19d5fdb6179b78292c0e3033f51`; stderr SHA-256: `0dce50fa87b135609093059fc8e2864a5f67555dd4642ba96e6c053508c4dbf5`; instruction-count evidence SHA-256: `ca3ded3fd0dc0fd57376c7b4bf04ff8d4a82bb5c8b772ca36313a360e6ff925b`.

The overall candidate result was `CPU_STATE_CHECKPOINT_FAIL`, not acceptance. Checkpoint C recorded `PSR=0x2000` (`i=0`, `ic=1`); checkpoint D after the qualification recorded `PSR.ic=0`. Other checkpoint requirements, including `PSR.i=0`, passed. The source used by Candidate D cleared `PSR.ic` before switching back to bank 0 and did not restore collection afterward. This is a post-test state-restoration defect; it does not erase the observed handler entry, valid records, continuations, or intentional exit, but it prevents acceptance of the complete CPU-state/exception-return contract.

## Corrected but unexecuted candidate

The cleanup now performs `rsm psr.ic`, switches to bank 0, restores `psr.ic`, and executes `srlz.i`, while leaving `PSR.i` clear. This preserves the caller's collection state after using `BSW` to restore the static bank. A fresh candidate E was built only after the single kernel run and passed the independent static gates. Its ELF SHA-256 is `3f6fa1c6c21bb44555621a6540664e82bf6f68aa9d472d29dbd16d6b3c3f4eae`; manifest SHA-256 is `ae3775cdb8043d8893221b8ba27864faff38a4647c7623db429f7fa03c00fc15`; candidate loader-record SHA-256 is `8b1f49a154a2caf1a052b6cd53f16cd6981aabb6a167d2c68768097cf742301b`.

Candidate E has entry `0x108000`, GP `0x675000`, IVT base `0x100000` and size `0x8000`; its PT_LOAD ranges are RX `[0x100000,0x3D0B30)`, R `[0x3D1000,0x474C88)`, and RW `[0x475000,0x4C858A0)`. Its 64 KiB stack is `[0x4C62E00,0x4C72E00)` and RSE backing store is `[0x4C72E00,0x4C82E00)`. Its manifest retains `guest_execution_authorized=false`. Candidate E was **not executed** under the one-launch constraint; no runtime conclusion is claimed for the correction.

## Tests and exclusions

The final corrected source passed 52 regression checks/fixtures:

| Suite | Result |
|---|---:|
| `tests/ia64_foundation_contract_test.py` | 17/17 passed |
| `tests/ia64_early_exec_contract_test.py` | 9/9 passed |
| `tests/ia64_ski_loader_contract_test.py` | 8/8 passed |
| `tests/ia64_cpu_state_checkpoint_test.py` | 10/10 passed |
| `tests/ia64_elf_validator_negative_test.py` against candidate E, early-exec | 3/3 passed |
| `tests/ia64_break_exception_contract_test.py` | 5/5 passed |

The corrected register diagnostic also passed, including reproduction of the legacy corruption and byte-exact corrected output. `scripts/verify-mbedtls-profile.ps1` passed for Mbed TLS 4.1.0, TF-PSA-Crypto 1.1.0, and all three tracked patches. `git diff --check` passed before documentation edits; it was rerun for the final scoped diff. No QEMU, QAC, second kernel launch, or push was performed.

## Recommended next phase

Keep IA64-4AR1 as the accepted early CPU-state baseline and IA64-4B as partial. If a later phase authorizes another kernel run, first revalidate candidate E's hashes, static gates, loader record, and exact runner limits, then perform one explicitly authorized run and require the full checkpoint plus exception evidence to pass together. Do not broaden the handler into general exception or asynchronous interrupt support until the test contract and the relevant register, RSE, FP, and processor-state preservation requirements are separately designed and qualified.
