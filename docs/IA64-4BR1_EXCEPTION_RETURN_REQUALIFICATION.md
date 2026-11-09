# IA64-4BR1 Break Exception Return Requalification

**Date:** 2026-11-08

**Outcome A — `IA64_BREAK_EXCEPTION_RETURN_ACCEPTED`**

The immutable Candidate E completed one authorized Ski 1.5.1 system-mode run. Both intended Break Instruction faults entered vector `0x2C00`, produced distinct valid records, and returned through their expected continuations once each. Checkpoints A–D passed strict validation; D observed `PSR=0x2000` (`PSR.i=0`, `PSR.ic=1`). Ski reported exactly the two expected Break faults, exited through the intentional SSC path, did not time out, and was reaped by the runner.

This accepts only the bounded, single-CPU Ski `early-exec` Break Instruction test. It does not qualify arbitrary synchronous exceptions, nested faults, external or asynchronous interrupts, user-mode return, FP/RSE context preservation, scheduling, devices, firmware, or general kernel runtime.

## Repository and preservation

- Repository: `D:\dev\guideXOSServer_IA64`; branch: `IA64_SUPPORT`; starting HEAD: `19b150692928e559e5c4f1bd040c419e205f241b`; tracking `origin/IA64_SUPPORT`, one commit ahead at task start.
- Earlier kernel images, simulator captures, reports, and untracked Python caches were preserved. No reset, clean, merge, rebase, pull, or push occurred.
- Candidate E was not rebuilt, replaced, or instrumented. The original IA64-4B report remains historical evidence.
- No QEMU or QAC activity occurred.

## Candidate and preflight

Candidate E was selected from the IA64-4B report and matching artifact manifest, not by guessing a build path.

| Item | Verified value |
|---|---|
| ELF | `/home/guidex/src/ia64-4b-break-exception-20261108-e/bin/kernel.elf` |
| SHA-256 | `3f6fa1c6c21bb44555621a6540664e82bf6f68aa9d472d29dbd16d6b3c3f4eae` |
| Manifest SHA-256 | `ae3775cdb8043d8893221b8ba27864faff38a4647c7623db429f7fa03c00fc15` |
| Loader record SHA-256 | `8b1f49a154a2caf1a052b6cd53f16cd6981aabb6a167d2c68768097cf742301b` |
| Profile / authorization | Ski-only `early-exec`; manifest execution authorization is `false` |
| ELF | ELF64 little-endian `EM_IA_64`, `ET_EXEC`; entry `0x108000`; GP `0x675000` |
| IVT | `0x100000`, size `0x8000`, aligned; 68 slots; Break vector at `+0x2C00` |
| Stack / RSE | stack `[0x4C62E00,0x4C72E00)`; RSE `[0x4C72E00,0x4C82E00)` |
| PT_LOAD | RX `[0x100000,0x3D0B30)`; R `[0x3D1000,0x474C88)`; RW `[0x475000,0x4C858A0)` |

All candidate-bound preflight gates passed: ELF and manifest, early-exec isolation, Break handler and target disassembly, CPU-state checkpoint layout/readers, exact corrected cleanup, both break sites and continuations, direct-system ELF ranges, loader record, prior isolated Break probe, and runner resource/authorization checks. Static validators report `IA64_ELF_VALIDATION=PASS`, `IA64_EARLY_EXEC_STATIC_GATES=PASS`, `IA64_BREAK_EXCEPTION_STATIC_GATES=PASS`, and `IA64_CPU_STATE_STATIC_GATES=PASS count=14`.

The installed Ski 1.5.1 binary SHA-256 is `ff6b19cb87dbf2db39cda4478d0e49995c9a5ec9ddd05aac83191f4ba1e40b45`; its clean source checkout is pinned at `dfc2902ea1423d9b32543a5daf8026213f2b37a1`. The loader record confirms direct `-forcesystem` ELF loading and one source-enforced CPU. The run used a 2 GiB address-space limit, 120-second CPU limit, and 60-second wall timeout.

## Architectural return-state audit

The Intel Itanium manuals define `PSR.i` as bit 14 and `PSR.ic` as bit 13. With interruption collection enabled, the interrupted PSR is saved in IPSR; IIP identifies the interrupted bundle and RI/EI identify its slot. `rfi` restores instruction state from IIP and processor state from IPSR. It must execute with the handler's `PSR.i=0` and `PSR.ic=0`; a complete interruption frame is required when saved `IPSR.ic=0`. See the [Intel Itanium Software Developer's Manual, Volume 2](https://www.intel.com/content/dam/www/public/us/en/documents/manuals/itanium-architecture-software-developer-rev-2-3-vol-2-manual.pdf) and [Volume 3](https://www.intel.com/content/dam/www/public/us/en/documents/manuals/itanium-architecture-vol-3-manual.pdf).

The observed and expected states were:

| Point | `PSR.i` | `PSR.ic` | Evidence |
|---|---:|---:|---|
| Startup A/B | 0 | 0 | Raw checkpoint records |
| Before controlled exceptions, C | 0 | 1 | `PSR=0x2000` |
| Handler entry | 0 | 0 | Both records report `handler.PSR=0x0` |
| Saved interrupted state | 0 | 1 | Slot-specific IPSR values below |
| Immediately after each `rfi` | 0 | 1 expected | No direct read exists in Candidate E before cleanup |
| Final checkpoint D | 0 | 1 | `PSR=0x2000` after test cleanup |

Target disassembly was inspected from the exact immutable ELF and saved under `preflight/ia64_break_exception_entry.disassembly.txt` and `preflight/ia64_break_exception_test.disassembly.txt` in the qualification evidence directory. The handler validates the incoming saved state, preserves IPSR except for the IIP/RI adjustment, synchronizes the IPSR write, and executes `rfi` with handler PSR clear. The incoming IPSR values have `IC=1`, `i=0`, `BN=1`, privileged CPL, and non-VM state. On this path `IFS.valid=0`; the manual's RFI frame rule for saved `IPSR.ic=0` does not apply.

For slot 1, the handler preserves bundle IIP and sets RI to 2 to resume in slot 2. For slot 2, it advances IIP by 16 and clears RI to resume at the next bundle. ISR.EI and IPSR.RI agree with each observed slot. The two continuations use separate records and markers, reject stale/duplicate records, and each report count 1.

The target test cleanup is `rsm 0x2000; bsw.0; ssm 0x2000; srlz.i`; it switches back to bank 0 and then restores interruption collection while leaving external interrupts masked. It does not clear IC after re-enabling it. This explains why final D is `0x2000`, but also means D is not a direct measurement immediately after `rfi`: cleanup deliberately changes IC before D is sampled. The runner recorded `break_exception_post_rfi_psr_observations: null`, and disassembly confirms there are no direct post-return PSR reads before cleanup. No candidate instrumentation was added.

The return is accepted on the combined evidence: live execution of both `rfi` continuations, incoming IPSR values with IC set, handler code/disassembly preserving the saved PSR fields while adjusting only the return location, the architectural RFI semantics, and final D satisfying the corrected post-cleanup contract. The precise PSR value immediately after each individual `rfi` remains unobserved and is explicitly outside the direct runtime evidence.

## The one authorized kernel run

The saved invocation is `preflight/kernel-run-command.txt`; explicit `--authorize-kernel-guest-execution` was supplied. The runner used `-forcesystem`, the exact SHA-checked Candidate E snapshot, and the limits above. No second kernel invocation occurred.

The guest's two records were:

```text
IA64_BREAK_RECORD slot1 valid=0xBEEFC0DE4B56414C vector=0x2C00 immediate=0x6A17 IIP=0x3D0490 slot=0x1 IPSR=0x120000002000 IFS.valid=0x0 ISR=0x20000000000 handler.PSR=0x0 RSC=0x3 BSP=0x4C72E48 completion=0xBEEFC0DE4B000001 return=0xBEEFC0DE4B100001 count=0x1 PR=0x401
IA64_BREAK_RECORD slot2 valid=0xBEEFC0DE4B56414C vector=0x2C00 immediate=0x6A18 IIP=0x3D0740 slot=0x2 IPSR=0x140000002000 IFS.valid=0x0 ISR=0x40000000000 handler.PSR=0x0 RSC=0x3 BSP=0x4C72E48 completion=0xBEEFC0DE4B000002 return=0xBEEFC0DE4B100002 count=0x1 PR=0x401
```

The guest printed `IA64_BREAK_EXCEPTION_VALIDATION errors=0x0 test=0x0` and `IA64_BREAK_EXCEPTION_RESULT=PASS`. The raw checkpoints show C `PSR=0x2000`, D `PSR=0x2000`, `PSR.i=0`, `PSR.ic=1`; A/B are both `PSR=0x0`. The strict validator confirmed the ordered raw A/B/C/D records match the runner's structured records and the candidate's linked IVT/RSE bounds.

| Runtime result | Observed |
|---|---|
| Runner classification / process status | `BREAK_EXCEPTION_KERNEL_PASS`; exit 0 |
| Ski output | `56674913 insts (2 faults)`, 0.66 s, 18,896,532 cycles |
| Fault attribution | Exactly two expected Break faults; no unexpected fault markers |
| `rfi` count | 2, as shown in the captured Ski instruction statistics |
| Guest termination | Intentional `SSC_EXIT=66` path; Ski reports `kernel exited with status 0` |
| Timeout / cleanup | No timeout; process-group cleanup complete; simulator process reaped |
| Direct post-RFI PSR reads | None; run-result field is null |

Run-result SHA-256 is `3a49b55a98ce7919844afd5be042ed6872ca51b2917bfe60069ff11cd2e87b14`; stdout SHA-256 is `4286b94e49293bb4388a3d6fe479d66b616ebf62dec113407e7032c00abc704d`; stderr SHA-256 is `2ca5bd530fc0d097b0e7859b203ebab09dd9c600dccd037a6a07ba8ea1dd2ca3`; instruction-count capture SHA-256 is `23d97001e033bedc74542475629cdefcf04255e3e6c748250bff1bf51d7a1337`. Raw logs, run result, strict-validation output, authorization record, loader/manifest copies, and cleanup evidence are preserved under `/home/guidex/src/ia64-4br1-exception-return-20261108-a/`.

## Strict validation and regressions

The saved postflight command revalidated the actual preserved capture and reported `IA64_CPU_STATE_RUNTIME_VALIDATION=PASS`, `BREAK_EXCEPTION_EVIDENCE=PASS`, two total/expected Break faults and zero unexpected faults, intentional exit, no timeout, and successful cleanup. It also rechecked the Candidate E, manifest, loader record, invocation, Ski identity, and captured-log hashes. The exit status was 0.

| Suite/check | Result |
|---|---:|
| Foundation contracts | PASS, 17/17, exit 0 |
| Early-exec contracts | PASS, 9/9, exit 0 |
| Ski loader contracts, including prelaunch rejection and timeout cleanup | PASS, 8/8, exit 0 |
| CPU-state checkpoint/runtime evidence contracts | PASS, 12/12, exit 0 |
| Break exception/return contracts | PASS, 6/6, exit 0 |
| Malformed ELF/IVT negative fixtures | PASS, 3/3, exit 0 |
| Combined Python tests and negative fixtures | PASS, 55 checks |
| Standalone Ski register diagnostic | PASS; legacy and corrected programs both exit 0; register/FPSR/SSC checks pass, expected legacy `0x77` formatter corruption reproduced, corrected byte is `0x51` |
| Mbed TLS profile | PASS; Mbed TLS 4.1.0, TF-PSA-Crypto 1.1.0, three patches |
| IA-64 shell syntax | PASS, 6/6 targeted shell files |
| Broad tracked-shell syntax scan | 12 unrelated pre-existing failures outside the IA-64 target set; no shell files were changed |
| `git diff --check` | PASS |

The static validators were rerun against Candidate E after the runtime. Runner tests cover absent authorization, wrong candidate/simulator identity, unsafe ranges, timeout stream capture, and process-group termination. The test runs used `PYTHONDONTWRITEBYTECODE=1`; existing untracked Python caches remain untouched.

## Remaining qualification work

This result does not generalize the handler beyond the two controlled Break immediates and the single-CPU Ski `early-exec` profile. A follow-on phase should add a separately qualified instrumented candidate if immediate post-RFI PSR sampling is required, then design and qualify other synchronous vectors, nested-entry behavior and fault escalation, complete interruption-state preservation, hardware interrupt delivery with appropriate PSR.i policy, user-mode return, and RSE/FP context preservation before any scheduler or broader runtime claim. Keep UEFI, devices, SMP, QEMU, and QAC outside this accepted boundary.
