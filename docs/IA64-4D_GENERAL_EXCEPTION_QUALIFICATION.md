# IA64-4D General Exception Qualification

**Outcome A — `IA64_GENERAL_EXCEPTION_RETURN_ACCEPTED` (2026-11-10).** One freshly built `early-exec` kernel ran once in pinned Ski system mode. The single deliberate Illegal Operation at vector `0x5400` matched its authorized instruction site, produced a valid versioned frame, resumed once at the exact continuation, passed the immediate post-`rfi` PSR and RSE checks, preserved both accepted IA64-4C Break paths, and exited through the intentional SSC stop. Strict postflight found exactly three expected Ski faults: two Break Instructions and one General Exception.

This accepts only the selected `mov r14=ar.bspstore` fault in the single-CPU Ski `early-exec` profile. It does not qualify arbitrary General Exceptions, nested exceptions, interrupts, scheduling, user mode, firmware, or hypervisor execution.

## Baseline and preservation

- Repository: `D:\dev\guideXOSServer_IA64`, branch `IA64_SUPPORT`.
- Starting HEAD: `d612d3614d9915be129652bae1dcb93231bb88c3`; upstream was `origin/IA64_SUPPORT`, with the local branch already one commit ahead.
- The accepted IA64-4C kernel image was preserved at SHA-256 `13a81925da8661ecea0f5751bebb11675337e8c7b2a55eb3987ea51da611f3f8`.
- Earlier IA64-4B, IA64-4BR1, and IA64-4C reports and artifacts remain intact. Existing untracked Python caches were left untouched.
- No reset, clean, pull, merge, rebase, or push was run. No automatic retry was made after the one authorized guideXOS kernel execution.

## Fault stimulus and architectural basis

The test executes the single bundle instruction `mov r14=ar.bspstore` while the early-exec RSE configuration has `RSC=3`. The Intel Itanium architecture specifies that reading `ar.bspstore` while `RSC.mode` is nonzero raises Illegal Operation before the destination GR is updated. The saved interruption state targets the General Exception vector at IVT offset `0x5400`. This instruction and configuration were first exercised in an isolated system-mode probe; the kernel candidate was not run until that probe passed.

The test starts with `r14=0x47454e4552414c44`. Both the independent probe and kernel frame retained that value through the fault, providing evidence that the faulting read did not update its destination. The authorized recovery target is the next bundle at `fault_site + 0x10`; the handler writes that exact address to `cr.iip` and restores the saved `cr.ipsr` before `rfi`. The handler never blindly increments IIP.

The record's subtype value `0` is the test contract's `ILLEGAL_OPERATION` enum. The architecture provides the General Exception vector and saved interruption registers; the selected opcode, its exact IIP, vector identity, and pinned Ski behavior jointly identify this one fault. `ISR=0`, `IFS=0`, and `IFS.valid=0` were observed and required by this test. The record does not claim that ISR carries a general-purpose subtype identifier.

## Frame and handler contract

`GeneralExceptionRecord` is a 256-byte, 16-byte-aligned, version-1 record in writable non-executable storage. Its fields capture the test subtype, vector, IIP and slot, IPSR, IFS and validity, ISR, handler-entry PSR/RSC/BSP, saved predicate image, fault site, return target, pre-fault RSC/BSP/UNAT/GR checksum, completion/count/return markers, and immediate post-return PSR/RSC/BSP/IVA/UNAT/GR checksum. The record becomes valid only after its fixed fields and the return state have been checked. A second marker is written after the immediate post-`rfi` capture and continuation checks.

The processor supplies the interruption state in its control registers. Before changing return state, assembly copies `cr.iip`, `cr.ipsr`, `cr.ifs`, and `cr.isr` plus handler-entry PSR/RSC/BSP into the record. Software captures the test identity, predicates, the pre-fault RSE/UNAT observations, and the post-return state. Before `rfi`, the handler checks the exact fault site, slot 0, `RSC=3`, `CPL=0`, `VM=0`, `IPSR.ic=1`, `IPSR.i=0`, zero IFS/ISR state, masked handler-entry `i` and `ic`, an in-range BSP, the live `r14` sentinel, and the expected predicate image. The saved IPSR's `BN=0` is also required by strict guest validation after return. Any failed pre-return check branches to a local fail-stop loop. Other vectors remain fail-stop.

The handler is assembly-only: no C++ calls, stack-frame setup, register-stack allocation, or spills occur before `rfi`. It saves and restores the full predicate image because its own `p6/p7` checks modify predicates. It leaves branch registers untouched, including the test's return branch register. The faulting transfer runs with `BN=0`; bank 1 and arbitrary banked state are outside this contract.

### Register and state liveness audit

- `r14` is the only test value whose preservation is asserted across the fault; it is the faulting instruction's destination and remains the sentinel until the continuation checks it. The handler itself does not write `r14`.
- `r1` is the global pointer. The continuation checks an XOR checksum of `r1` and the `r14` sentinel against the pre-fault checksum.
- `r8-r13` and `r15` are scratch for this hand-written test and handler path. They are not saved as a general interrupted C++ register image. The handler uses `r8-r13/r15` for record addresses, register reads, and comparisons; the continuation captures PSR in `r15` before any cleanup.
- Predicates are saved to the record before handler predicates are used and restored in full before `rfi`. The continuation checks the restored `0x401` predicate image before using `p6/p7`.
- The handler and continuation do not use or change branch registers. The continuation's return uses the original `b0`; `ar.pfs` is saved/restored around the explicit post-return `alloc` probe.
- `UNAT` is sampled before the fault and immediately after `rfi`; both were `0` in the kernel record and equal. The test's non-NaT GP values and checksum matched. This does not preserve or enumerate NaT bits for arbitrary interrupted GRs.
- No floating-point instruction or FP state save/restore is part of the handler or continuation. FP preservation is not qualified by this phase.
- RSE checks compare RSC and BSP before the fault, at handler entry, and after `rfi`. The test performs one `alloc` after return and restores `ar.pfs`. It does not read `ar.bspstore` or RNAT in mode 3; those values remain unverified here.

The test path intentionally does not enter ordinary C++ from the handler. This frame is not a complete ABI save area: in particular, do not use it to resume arbitrary C++ code with live static registers, stack references, alternate bank state, FP values, or NaT values. The narrow liveness contract is the hand-written fault/test/continuation sequence only.

## Independent probe

The standalone General Exception ELF was built and statically inspected before the kernel execution:

- ELF: `/home/guidex/src/ia64-4d-general-exception-probe-20261110-r3/general-exception-probe.elf`
- ELF SHA-256: `aaf8abcb0c0cb246b357864bc50240595fa3302fd522f843fcbe97c21e597c58`
- Manifest: `/home/guidex/src/ia64-4d-general-exception-probe-20261110-r3/general-exception-probe.manifest.json`
- Independent capture: `/home/guidex/src/ia64-4d-general-exception-probe-run-20261110-r3/`
- Ski: version 1.5.1, source commit `dfc2902ea1423d9b32543a5daf8026213f2b37a1`, executable SHA-256 `ff6b19cb87dbf2db39cda4478d0e49995c9a5ec9ddd05aac83191f4ba1e40b45`.

The probe reported `GENERAL_EXCEPTION_PROBE_PASS`, one fault, 117,930 instructions, process status 0, intentional SSC exit, and complete cleanup. Its recorded vector was `0x5400`, subtype `0`, IIP/site `0x10ac80`, slot 0, IPSR `0x2000`, IFS/ISR zero, RSC 3, target `0x10ac90`, and one continuation. Immediate post-`rfi` PSR was `0x2000`; BSP, RSC, UNAT, and the live-GR checksum matched across return. No second exception or timeout occurred.

Two earlier probe builds (`r1` and `r2`) timed out at the local fail-stop loop because the handler incorrectly required `BN=1`. Ski debugger evidence showed the General Exception's saved IPSR was `0x2000`, with `BN=0`; the handler and validators were corrected to that vector-specific state. Those failed attempts were not qualifying evidence, and no guideXOS kernel was executed until the corrected `r3` probe passed. Their artifacts and diagnostic captures remain preserved.

## Kernel candidate and one-run result

The fresh isolated candidate is `/home/guidex/src/ia64-4d-general-exception-kernel-20261110-r2/bin/kernel.elf`, SHA-256 `f3cbe809c055f1e41a4f09c3cf56a829c7ce4386f412321b1da5fc043c4ce1b4`. It is ELF64 little-endian IA-64 `ET_EXEC`, entry `_start=0x108000`, GP `0x678000`, three non-overlapping PT_LOAD segments with no writable-executable segment, and a 68-slot architectural IVT at `0x100000`. The separate 64-KiB stack is `0x4c65e00..0x4c75e00`; the 64-KiB RSE store is `0x4c75e00..0x4c85e00`. Target disassembly binds the authorized fault site `0x3d3880` to `mov r14=ar.bspstore` and the continuation `0x3d3890` to its first instruction `mov r15=psr`. ELF, IVT, early-exec, CPU-state, Break, and General Exception static gates passed before execution.

The matching manifest SHA-256 is `aa880f2f256396e4ae03d6f945f7acee511ec02b53848ad4b27c8d1b1a9e0432`; the loader record SHA-256 is `7321a3e86f873a744d5724485409d6a92f4a7c6243418a1f18c944e28a6341cf`. The manifest keeps guest execution authorization false. The saved invocation is `/home/guidex/src/ia64-4d-general-exception-kernel-20261110-r2/preflight/kernel-invocation.txt`; it used the qualified runner's explicit authorization, Ski `-forcesystem`, one CPU, a 60-second wall limit, and 2 GiB / 120-second resource limits.

Exactly one candidate kernel execution occurred, captured under `/home/guidex/src/ia64-4d-general-exception-kernel-run-20261110-r1/`. The runner classified it `GENERAL_EXCEPTION_KERNEL_PASS`, exit status 0, 1.067 seconds, 56,718,591 instructions, three faults, no timeout, and complete process cleanup. Guest termination was the intentional SSC exit 66. Raw capture hashes:

| Capture | SHA-256 |
|---|---|
| `stdout.log` | `aed9019584ccc27e67a538e70391a281cbd1d0ad92f127e75c3449222b9f5ae1` |
| `stderr.log` | `18269891daa525b0b6a2553f6b20d7006758d173d5d4028bc5d9212d5df64f95` |
| `instruction-counts.txt` | `5d6db91269d68d41cfe56aa289d27089749c97035f62ec03c04514007df8ab1e` |

The General Exception record from that run was:

| Field | Observed | Field | Observed |
|---|---:|---|---:|
| valid / version / size | `0xbeefc0de47455631` / `1` / `0x100` | vector / subtype | `0x5400` / `0` |
| IIP / site / slot | `0x3d3880` / `0x3d3880` / `0` | IPSR | `0x2000` |
| IFS / IFS.valid / ISR | `0` / `0` / `0` | handler PSR / RSC | `0` / `3` |
| handler, pre, and post BSP | `0x4c75e68` | pre/post RSC | `3` / `3` |
| saved PR / return target | `0x401` / `0x3d3890` | handler status | `0xbeefc0de47454831` |
| pre/post UNAT | `0` / `0` | pre/post GR checksum | `0x47454e4552414c44` / `0x47454e4552414c44` |
| completion / count / return | `0xbeefc0de47454331` / `1` / `0xbeefc0de47455231` | post PSR / IVA | `0x2000` / `0x100000` |
| post validity | `0xbeefc0de47505331` | continuation count | `1` |

Both earlier Break records, their distinct return markers, and their direct post-`rfi` PSR observations also passed. Each direct capture was `0x2000` (`PSR.i=0`, `PSR.ic=1`). Checkpoints A–D passed in order; C and D each reported IVA `0x100000`, RSC 3, and PSR `0x2000`. The General Exception postflight revalidated the raw record, continuation markers, runner-parsed frame, both Break records, Ski fault summary, artifact/manifest/loader identity, and the raw capture hashes. It accepted exactly two Break faults plus one General Exception fault, with no unclassified fault.

The full machine-readable run result is `run-result.json` in the capture directory. The candidate loader record, manifest, target disassembly, and saved invocation remain beside the isolated candidate in its `preflight/` directory.

## Verification record

The following targeted regression suites passed after the final validator changes:

| Suite / gate | Result |
|---|---|
| IA-64 foundation contracts | PASS, 17 tests |
| early-exec contracts | PASS, 9 tests |
| Ski loader contracts | PASS, 8 tests |
| CPU-state/checkpoint contracts | PASS, 13 tests, including strict 3-fault runtime evidence |
| Break exception contracts | PASS, 9 tests |
| General Exception contracts | PASS, 7 tests |
| malformed ELF negative cases | PASS, 3 rejected fixtures |
| General Exception static candidate gate | PASS |
| strict CPU-state runtime postflight | PASS; four checkpoints, two Break records, one General Exception frame, 3 expected faults |
| independent register/SSC diagnostic | PASS for register output and FPSR; RNAT remains unverified |
| IA-64 FP codegen harness | PASS |
| Mbed TLS profile verifier | PASS; Mbed TLS 4.1.0, TF-PSA-Crypto 1.1.0, 3 patches |
| targeted shell syntax checks | PASS |
| `git diff --check` | PASS |

## Remaining limits and next phase

This result is limited to one mode-3 `ar.bspstore` read at the recorded site, its General Exception vector entry, and the fixed `rfi` continuation under single-CPU Ski system mode. Unsupported vectors remain fail-stop. The test does not establish nested-exception safety, arbitrary frame recovery, interrupt delivery, scheduling, context switching, user mode, firmware, hypervisor behavior, general FP preservation, full NaT preservation, RNAT/BSPSTORE values in mode 3, or desktop initialization.

The next useful phase is a separate static design and safety audit for a second exception class or a full software save frame. Do not enable external interrupts or scheduling until register, bank, branch, NaT, FP, and RSE ownership and reentrancy have their own implementation and qualification.

## References

- Intel, [Itanium Architecture Software Developer's Manual, Volume 2](https://www.intel.com/content/dam/www/public/us/en/documents/manuals/itanium-architecture-software-developer-rev-2-3-vol-2-manual.pdf).
- Intel, [Itanium Architecture Software Developer's Manual, Volume 3](https://www.intel.com/content/dam/www/public/us/en/documents/manuals/itanium-architecture-vol-3-manual.pdf).
- Pinned Ski source, commit `dfc2902ea1423d9b32543a5daf8026213f2b37a1`, with local source/build and executable identity recorded in the independent probe evidence.
