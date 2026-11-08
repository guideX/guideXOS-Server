# IA64-4AR1 CPU-State Runtime Requalification

**Date:** 2026-11-07
**Outcome A — `IA64_CPU_STATE_CHECKPOINT_ACCEPTED`**

The corrected static-only early-exec candidate ran once in the qualified Ski 1.5.1 system-mode environment. Its raw A/B/C/D records passed the strict post-run validator, the kernel followed its intentional `SSC_EXIT=66` path, Ski reported exit status 0 after 56,648,994 instructions and zero faults, and the runner did not time out. This accepts only the bounded CPU-state checkpoint milestone; it does not qualify interrupt delivery, exception resumption, context switching, scheduling, devices, UEFI, or desktop startup.

## Repository and artifact preservation

- Repository: `D:\dev\guideXOSServer_IA64`; branch: `IA64_SUPPORT`; task-start HEAD: `3a7aff9be24e0e60f34041580a3efe1465507939`; upstream: `origin/IA64_SUPPORT`, one commit ahead and zero behind.
- The accepted IA64-3R4 image remains `/home/guidex/src/ia64-3-ssc-3r3-register-diagnostic-20261107-d/bin/kernel.elf`, SHA-256 `a31a529df5a6f43de699a9ef1ce02e5d102375d0c3f00ee70d478f0bf32e12da`. Candidate D is distinct.
- The prior IA64-4A failed-validation evidence for Candidate C and the existing untracked `scripts/__pycache__` and `tests/__pycache__` paths were preserved. Candidate D had no prior capture directory; this phase created the separate run evidence below.
- No reset, clean, pull, rebase, merge, push, QEMU run, or QAC execution occurred. Candidate D was not rebuilt or replaced.

## Corrected candidate and static qualification

| Item | Verified value |
|---|---|
| Candidate ELF | `/home/guidex/src/ia64-4a-cpu-state-checkpoints-20261107-d/bin/kernel.elf` |
| SHA-256 | `49f0ea7b0d68390916cf6ab7b7db3561825da34aeb45e8b8efd8e8ab01106af5` |
| Manifest | `/home/guidex/src/ia64-4a-cpu-state-checkpoints-20261107-d/ia64-artifact-manifest.json` |
| Profile | `early-exec`; manifest and ELF hash match |
| Manifest execution authorization | `false`; launch used the runner's explicit authorization flag |
| Entry / GP | `_start = 0x108000`; `__gp = 0x673000` |
| Linked IVT | `0x100000`, size `0x8000`, aligned to `0x8000` |
| Stack | `[0x4C60E00, 0x4C70E00)`, 64 KiB |
| RSE backing store | `[0x4C70E00, 0x4C80E00)`, 64 KiB |
| Checkpoint records | `A=0x4890C0`, `B=0x489120`, `C=0x489180`, `D=0x4891E0`; 96 bytes each |

The candidate's three `PT_LOAD` segments are identity-mapped, non-overlapping, and not writable-executable: RX `[0x100000,0x3CED70)`, R `[0x3CF000,0x472A38)`, and RW `[0x473000,0x4C83798)`. Independent ELF/manifest validation passed; the early-exec safety gates passed; all 14 CPU-state static gates passed. The runner repeated these gates immediately before loading the private, hash-checked snapshot.

The Ski source checkout is clean at `dfc2902ea1423d9b32543a5daf8026213f2b37a1`, version 1.5.1. The installed executable SHA-256 is `ff6b19cb87dbf2db39cda4478d0e49995c9a5ec9ddd05aac83191f4ba1e40b45`. The source enforces one CPU (`SKI_NPROC=1`, `nproc=1`); the installed binary has no `-cpus` option. The existing cross-toolchain reported GCC 13.5.0 targeting `ia64-linux-gnu`. A candidate-bound loader record was created at `/home/guidex/src/ia64-4ar1-corrected-checkpoint-runtime-20261107/preflight/ia64-4ar1-ski-loader-record.json`; its PT_LOAD ranges, entry, GP, candidate hash, Ski hash, and one-CPU/system-mode loader contract were checked against the ELF and the previously qualified loader evidence.

## PSR decoding and checkpoint integrity

Intel's [Itanium Architecture Software Developer's Manual, Volume 2, Table 3-2](https://www.intel.com/content/dam/www/public/us/en/documents/manuals/itanium-architecture-software-developer-rev-2-3-vol-2-manual.pdf) defines `PSR.ic` as bit 13 and `PSR.i` as bit 14. Therefore `PSR=0x2000` means `PSR.ic=1`, `PSR.i=0`: interruption collection is enabled while external interrupts remain masked. The corrected reporter uses independent masks for bits 14 and 13, and the strict parser independently compares the two labels against those same architectural positions.

The actual linked `report_checkpoint` disassembly was reviewed and saved as `/home/guidex/src/ia64-4ar1-corrected-checkpoint-runtime-20261107/preflight/corrected-reporter-disassembly.txt`. It tests bit 14 for `PSR.i` at `0x33CF66` and bit 13 for `PSR.ic` at `0x33CFB6`. The regression fixtures cover:

| Raw PSR | `PSR.i` bit 14 | `PSR.ic` bit 13 |
|---:|---:|---:|
| `0x0000` | 0 | 0 |
| `0x2000` | 0 | 1 |
| `0x4000` | 1 | 0 |
| `0x6000` | 1 | 1 |

Reversed labels, contradictory hex/decoded text (including raw-valid `0x2000` with false labels), incomplete records, missing checkpoints, and out-of-order checkpoints are rejected. The runtime validator also compares the runner's structured records with the exact record lines in captured guest stdout.

Checkpoint storage is four contiguous 96-byte records in writable initialized `.data`, outside the early BSS clear; A/B evidence therefore survives that clear. A/B are captured inline without calls in the RSE transition window. BSPSTORE is read only at B in enforced-lazy mode; RNAT is omitted because it is unavailable/undefined in the observed modes. The C/D target readers are leaf routines with no frame allocation, call, BSPSTORE/RNAT access, or PFS/UNAT write. Their register reads copy state into general registers and store the observation; they do not write the measured CPU state. The corrected SSC bridge retains the compiler ABI contract: it maps SSC registers, restores `ar.pfs`, and returns the SSC value through `r8`.

The candidate keeps `PSR.i=0` at entry and enables only `PSR.ic` in architecture initialization. Static checks found no external-interrupt enable in the bounded path, and every checkpoint observed `PSR.i=0`. BSP is checked against the candidate's RSE range, not required to equal BSPSTORE: C-to-D BSP advances by `0x28` within the range, consistent with register-stack call-frame/NaT collection progression. No extra register reads were added.

## One authorized Ski execution

The exact one-line invocation is preserved in `/home/guidex/src/ia64-4ar1-corrected-checkpoint-runtime-20261107/preflight/invocation.txt`. It used `--mode system`, `--timeout 60`, `--authorize-kernel-guest-execution`, `prlimit --as=2147483648`, and `prlimit --cpu=120`. The runner uses `-forcesystem`, captured stdout, stderr, and instruction counts, ran the candidate from a hash-verified private snapshot, and performed process-group cleanup. No qualified Ski process remained afterward.

The capture directory is `/home/guidex/src/ia64-4ar1-corrected-checkpoint-runtime-20261107/kernel-capture/` and contains the original `stdout.log`, `stderr.log`, `instruction-counts.txt`, and `run-result.json`. The runner classified the launch as `GUEST_EXIT_ZERO`, exit code 0, no timeout, elapsed 1.016 seconds. Ski's stderr summary reports `56648994 insts (0 faults), 0.69 sec`; the runner's record contains `artifact_sha256` matching the manifest. Ski writes guest checkpoint lines to stdout and the exit/instruction summary to stderr.

The complete raw checkpoint records were:

```text
IA64_CHECKPOINT_A stage=0x1 valid=0x27 PSR=0x0 PSR.i=0 PSR.ic=0 RSC=0x0 RSC.mode=0 BSP=0x0 BSPSTORE=UNVERIFIED RNAT=UNVERIFIED FPSR=0x0 IVA=UNVERIFIED PFS=UNVERIFIED UNAT=UNVERIFIED IVT_EXPECTED=0x100000 RSE_RANGE=[0x4C70E00,0x4C80E00)
IA64_CHECKPOINT_B stage=0x2 valid=0x2F PSR=0x0 PSR.i=0 PSR.ic=0 RSC=0x0 RSC.mode=0 BSP=0x4C70E00 BSPSTORE=0x4C70E00 RNAT=UNVERIFIED FPSR=0x0 IVA=UNVERIFIED PFS=UNVERIFIED UNAT=UNVERIFIED IVT_EXPECTED=0x100000 RSE_RANGE=[0x4C70E00,0x4C80E00)
IA64_CHECKPOINT_C stage=0x3 valid=0x1E7 PSR=0x2000 PSR.i=0 PSR.ic=1 RSC=0x3 RSC.mode=3 BSP=0x4C70E18 BSPSTORE=UNVERIFIED RNAT=UNVERIFIED FPSR=0x9804C0270033F IVA=0x100000 PFS=0x184 UNAT=0x0 IVT_EXPECTED=0x100000 RSE_RANGE=[0x4C70E00,0x4C80E00)
IA64_CHECKPOINT_D stage=0x4 valid=0x1E7 PSR=0x2000 PSR.i=0 PSR.ic=1 RSC=0x3 RSC.mode=3 BSP=0x4C70E40 BSPSTORE=UNVERIFIED RNAT=UNVERIFIED FPSR=0x9804C0270033F IVA=0x100000 PFS=0x285 UNAT=0x0 IVT_EXPECTED=0x100000 RSE_RANGE=[0x4C70E00,0x4C80E00)
```

| Stage | Candidate-specific expectation | Observed |
|---|---|---|
| A | Entry has `PSR.i=0`, `PSR.ic=0`; mode-restricted fields unavailable | `PSR=0`, `RSC=0`, `BSP=0`, `FPSR=0`; BSPSTORE/RNAT/IVA/PFS/UNAT unverified |
| B | `PSR.i=0`, `PSR.ic=0`; lazy RSE mode; BSP and BSPSTORE at linked RSE base | `PSR=0`, `RSC=0`, `BSP=BSPSTORE=0x4C70E00`, `FPSR=0`; RNAT/IVA/PFS/UNAT unverified |
| C | `PSR.i=0`, `PSR.ic=1`; eager RSC mode 3; IVA and FPSR match linked/configured values; BSP inside RSE range | `PSR=0x2000`, `RSC=3`, `BSP=0x4C70E18`, `FPSR=0x9804C0270033F`, `IVA=0x100000`, `PFS=0x184`, `UNAT=0`; BSPSTORE/RNAT unverified |
| D | Same interrupt/RSE/IVA/FPSR invariants as C at pre-exit | `PSR=0x2000`, `RSC=3`, `BSP=0x4C70E40`, `FPSR=0x9804C0270033F`, `IVA=0x100000`, `PFS=0x285`, `UNAT=0`; BSPSTORE/RNAT unverified |

The IVA observations equal the linked IVT base. BSPSTORE is verified only at B, where its mode permits the read. RNAT is unverified throughout, by design. These are guest-reported register reads, not an independent Ski register dump. The source/disassembly and runner gates establish the deliberate `SSC_EXIT=66` stop path; Ski reports the guest's status as 0.

## Strict runtime validation and regression results

The first runner result already contained the four parsed records and no checkpoint errors, but the pre-run validator did not explicitly reject out-of-order records. After the only Candidate D run, the validator was extended to enforce A/B/C/D order and to verify the full runner, manifest, loader, launch hash, stdout/stderr, and fault-count evidence. It was then run against the preserved first-run files; the result is saved at `/home/guidex/src/ia64-4ar1-corrected-checkpoint-runtime-20261107/postflight/strict-runtime-validation.txt` and reports `IA64_CPU_STATE_RUNTIME_VALIDATION=PASS`. No kernel rerun was made.

Final regression results:

| Check | Result |
|---|---|
| IA-64 foundation, early-exec, Ski loader, CPU-state Python suites | PASS, 44 tests: 17 + 9 + 8 + 10 |
| Malformed-ELF/IVT negative fixtures | PASS, 3/3 |
| Combined required Python regression count | PASS, 47 tests/fixtures |
| Register/SSC diagnostics | PASS; legacy and corrected standalone diagnostic ELFs both exited 0; expected legacy formatter corruption reproduced, corrected byte-exact formatter, register, FPSR, and GETCHAR checks passed; RNAT remained unverified |
| Mbed TLS profile | PASS; Mbed TLS 4.1.0, TF-PSA-Crypto 1.1.0, 3 patches |
| Bash syntax | PASS, exit 0 for build wrapper, Linux runner, register diagnostic, Ski smoke builder, and FP-codegen script |
| `git diff --check` | PASS |

The FP-codegen harness itself was not rerun; the candidate was not rebuilt. No QEMU/QAC, asynchronous interrupt, exception-resumption, context-switch, scheduler, device, UEFI, or desktop test was performed.

## Limits and recommended IA64-4B scope

This result qualifies the bounded Ski early-exec CPU-state checkpoints only. There was no independent live Ski register snapshot or IP trace, no external interrupt delivery, and no proof of exception recovery/resumption, FP preservation across interrupts, RSE context switching, multiprocessor behavior, firmware handoff, or full kernel runtime. Preserve Candidate D and this accepted evidence as the early-exec baseline.

Recommended IA64-4B: define and statically audit a separate fail-stop exception-entry observation milestone, including its vector entry and interruption-resource expectations, then request separate authorization for one bounded probe with external interrupts still masked. Do not add exception resumption, context switching, scheduling, or desktop initialization until that evidence is accepted in a later phase.
