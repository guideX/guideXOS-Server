# IA64-4E Unaligned Reference Qualification

**Outcome A — `IA64_UNALIGNED_EXCEPTION_RETURN_ACCEPTED` (2026-11-11).** The standalone probe first qualified one ordinary unaligned `ld8` in pinned Ski system mode. A fresh `early-exec` kernel then ran once. Its deliberate Unaligned Reference at IVT offset `0x5A00` matched the authorized site, IFA, ISR, and saved state; the handler resumed once at the exact continuation with alignment checking cleared; immediate post-`rfi` state and cleanup passed. Both accepted Break returns and the General Exception return remained valid. Ski attributed exactly four expected faults, the guest exited through SSC, and process cleanup completed.

This result qualifies one deliberately skipped load in the single-CPU Ski `early-exec` profile. It does not qualify arbitrary unaligned accesses or general memory-fault recovery.

## Baseline and preservation

- Repository: `D:\dev\guideXOSServer_IA64`; branch: `IA64_SUPPORT`.
- Starting HEAD: `ca224b0ee89298a27b721968b3d4c4b85cd70bd1`; the branch already contained two commits ahead of its upstream.
- Preserved accepted IA64-4D candidate SHA-256: `f3cbe809c055f1e41a4f09c3cf56a829c7ce4386f412321b1da5fc043c4ce1b4`.
- Earlier accepted artifacts, run records, and pre-existing untracked Python caches were left in place. No reset, clean, pull, merge, rebase, or push was performed.
- No QEMU or QAC execution was performed. The corrected standalone probe passed before the one authorized guideXOS kernel run; no kernel retry was made.

## Architectural contract and Ski behavior

The handler is installed at the architectural Unaligned Reference vector offset `0x5A00`. The selected stimulus is a non-speculative, ordinary 8-byte `ld8`, with its effective address one byte past a 16-byte initialized buffer. The test enables `PSR.ac` only for the serialized faulting access. It uses no atomic, speculative, MMIO, or device-memory operation.

The test and handler use the interruption resources according to the Itanium contract: `cr.iip` identifies the faulting bundle, `cr.ipsr` supplies the saved instruction-slot field, `cr.ifa` supplies the fault address, and `cr.isr` supplies access characteristics. For this slot-0 read, the observed `ISR` was `0x400000000`: `r=1`, `w=0`, `ed=ei=ni=sp=0`. `IFS.valid` was zero; the other IFS bits are recorded but not interpreted because they are not valid for this interruption. Pinned Ski delivered the deliberate load to `0x5A00` and reported the expected IFA and ISR in both the standalone probe and kernel run. This records the behavior of this Ski build and test instruction, not a broader claim about every implementation or access type.

The standalone program is loaded in Ski direct system mode. Its manifest records the `PSR.ac` test precondition, single-CPU requirement, exact load, expected vector, and fail-stop handling for other vectors. The successful probe established the selected fault and return before the production early-exec call was added. An earlier probe build reached its fail-stop path due to a handler comparison error; it was corrected and rebuilt. That failed attempt was not accepted as evidence and no kernel was run until the corrected probe passed.

## Test sequence and return mechanics

The target disassembly binds the fault site to the first instruction of an MII bundle:

```text
00000000003d7070 <ia64_unaligned_reference_fault_site>:
  3d7070: 01 78 00 1c 18 10  [MII] ld8 r15=[r14]
  3d7076: 00 00 00 02 00 00        nop.i 0x0
  3d707c: 00 00 04 00              nop.i 0x0;;
00000000003d7080 <ia64_unaligned_reference_continuation>:
  3d7080: 01 78 00 00 25 04  [MII] mov r15=psr
```

The test holds the expected address in `r14`; the faulting load destination `r15` is dead. The continuation's first instruction replaces `r15` with the live PSR sample, and no code reads the load result. Therefore the test records a controlled skipped access; it does not claim that the load completed or returned data.

The assembly handler validates the exact IIP/site, slot zero, saved PSR mode bits, `IFS.valid`, the complete expected ISR value and decoded fields, the precise IFA, the access width, and the address range in the candidate's own initialized buffer. It claims sequence 1 only from a zeroed record; a stale validity marker or nonzero sequence takes the fail-stop path. The exception entry has no C++ call, stack-frame creation, or RSE allocation. It captures processor state before setting the exact continuation in `cr.iip`, restores the saved predicate image, clears only `PSR.ac` from the saved return IPSR, serializes, and executes `rfi`. It does not infer a continuation by incrementing IIP and does not emulate a memory operation.

On return, the first instruction samples PSR before any cleanup. The continuation then clears and serializes `PSR.ac` before its subsequent record/data accesses. It checks the resumed PSR, RSC/BSP, IVA, UNAT, the live `r14` address, predicate state, GR checksum, and one-continuation marker. Other vectors remain fail-stop.

### Versioned frame

`UnalignedReferenceRecord` is a version-1, 16-byte-aligned, 384-byte record. The assembly offsets are checked against the C++ layout at build time.

| Byte offsets | Captured fields |
|---|---|
| `0–40` | Validity, schema version/size, sequence, vector, subtype |
| `48–152` | Fault site, IIP, slot, IPSR, IFS/validity, IFA, ISR and decoded access bits |
| `160–216` | Handler PSR/RSC/BSP, predicates, pre-fault PSR/RSC/BSP/UNAT |
| `224–248` | Requested address, buffer base/size, access width |
| `256–296` | Authorized return target/IPSR, handler/completion markers, continuation count and return marker |
| `304–376` | Immediate post-RFI PSR/RSC/BSP/IVA/UNAT, GR checksums, AC-cleanup marker, post-return validity and reserved field |

The sequence, validity, completion, return, cleanup, and post-return markers distinguish stale state, rejected entry, successful return, and completed cleanup. Strict guest and host validators require one frame and one post-return marker; missing or duplicate markers fail.

### Register and state limits

- The handler saves/restores the predicate image (`PR=0x401`) and leaves branch registers untouched. It runs with `BN=0`; alternate bank and arbitrary banked state are outside this contract.
- `r14` is the live effective address and is unchanged across the exception. The test compares an `r1`/`r14` checksum before and after the return. Other GR scratch values are not a general C++ register image.
- RSC remained 3 and BSP stayed unchanged across handler and `rfi`; no RSE frame is allocated by the handler. The test separately exercises one post-return allocation and restores PFS. RNAT/BSPSTORE behavior in this mode is not claimed.
- UNAT was sampled as zero and equal across the return for this test. This does not establish preservation of arbitrary NaT-tagged registers.
- No floating-point save/restore or general FP-interruption contract is added. Interrupt delivery remains masked, and scheduling/device initialization remain disabled.

## Independent Ski probe

The standalone probe was built and statically inspected before early-exec integration:

- ELF: `/home/guidex/src/ia64-4e-unaligned-reference-probe-20261111-r5/unaligned-reference-probe.elf`
- ELF SHA-256: `0fcddbb24082298a7bd40f23b7ff0f57af3493627c72b744c612300f260737f3`
- Manifest SHA-256: `d74fee7303045baecb3937e1297d45cfc1e11eb5bd8a4356f247a7e42452ae10`
- Capture: `/home/guidex/src/ia64-4e-unaligned-reference-probe-run-20261111-r2/`
- Ski: 1.5.1, source commit `dfc2902ea1423d9b32543a5daf8026213f2b37a1`, executable SHA-256 `ff6b19cb87dbf2db39cda4478d0e49995c9a5ec9ddd05aac83191f4ba1e40b45`.

The runner classified `UNALIGNED_REFERENCE_PROBE_PASS`: one expected fault, 130,630 Ski instructions, 0.064 seconds, process status 0, intentional SSC exit 66, no timeout, and complete cleanup. The probe observed vector `0x5A00`, subtype 0, site/IIP `0x10B9A0`, slot 0, IPSR `0x2008`, IFS/IFS.valid `0/0`, IFA `0x10E001`, ISR `0x400000000`, handler RSC/BSP `3/0x11F078`, target `0x10B9B0`, return IPSR `0x2000`, post-RFI PSR `0x2000`, and one continuation. Its buffer base was `0x10E000`, confirming the `+1` offset. The output explicitly does not consume the fault destination.

## Kernel candidate and single authorized run

The fresh isolated candidate is `/home/guidex/src/ia64-4e-unaligned-reference-kernel-20261111-r4/bin/kernel.elf`, SHA-256 `10c338d93b86b23b1b5ef0e58ff69f6b6e0b3ae3e3ce8e6b1dea42b18138fa65`. It is an ELF64 little-endian IA-64 `ET_EXEC` with entry `0x108000`, GP `0x67d000`, three load segments, and the 68-slot IVT at `0x100000`. The fault and continuation addresses above were read from this candidate's symbols/disassembly, as was the buffer address; prior candidate addresses were not reused. The fresh build, ELF/IVT, early-exec, CPU-state, Break, General Exception, and Unaligned Reference gates all passed before execution.

The matching artifact manifest SHA-256 is `0ad9cae1c8aa9c1b8e01e8433c4a7c16c20429284f77f0acc1090f17bcd803c2`; it records `guest_execution_authorized=false`. The candidate loader record SHA-256 is `475d46b81eff71dda8a98b41306fe945de36eee46c8ec889b933b8e72600e994`. The saved authorized invocation is `/home/guidex/src/ia64-4e-unaligned-reference-kernel-run-20261111-r1/invocation.txt`; the runner enforced the explicit authorization flag, pinned Ski binary, direct `-forcesystem` loading, one CPU, a 60-second wall limit, and 2-GiB address-space / 120-second CPU limits.

Exactly one guideXOS kernel execution occurred, captured at `/home/guidex/src/ia64-4e-unaligned-reference-kernel-run-20261111-r1/`. It was classified `UNALIGNED_REFERENCE_KERNEL_PASS`, exited with process status 0 after 1.067 seconds and 56,764,656 Ski instructions, had no timeout, and completed cleanup. Ski counted exactly four expected faults: two Break, one General Exception, and one Unaligned Reference. The guest used intentional SSC exit function 66. Raw capture hashes:

| Capture | SHA-256 |
|---|---|
| `stdout.log` | `03e128e5d4afc951d4bcd0f5bd68e8c46c772fbcd628f29519032e499c9bdd62` |
| `stderr.log` | `787bdc6c2fe63dddd7cb6d1caed746d3eb170866185ef3b3aff285bf3ff54dde` |
| `instruction-counts.txt` | `98b17f1d0e02e510810955739f3e41f0a7cc226602127ae09472796a5f3206d7` |

The kernel's unaligned frame was:

| Field | Observed | Field | Observed |
|---|---:|---|---:|
| valid / version / size / sequence | `0xbeefc0de55415631` / `1` / `0x180` / `1` | vector / subtype | `0x5A00` / `0` |
| IIP / site / slot | `0x3D7070` / `0x3D7070` / `0` | saved IPSR | `0x2008` |
| IFS / IFS.valid | `0` / `0` | IFA / ISR | `0x493251` / `0x400000000` |
| decoded ISR `ed/ei/ni/sp/r/w` | `0/0/0/0/1/0` | handler PSR / RSC | `0` / `3` |
| handler, pre-fault, and post-RFI BSP | `0x4C7AE78` | pre/post RSC | `3` / `3` |
| buffer / requested address / width | `0x493250` / `0x493251` / `8` | predicates | `0x401` |
| return target / return IPSR | `0x3D7080` / `0x2000` | completion / count / return | valid / `1` / valid |
| immediate post-RFI PSR / IVA | `0x2000` / `0x100000` | post-RFI UNAT / GR checksum | `0` / `0x2EE251` (same before return) |
| cleanup / post-return validity | valid / valid | reserved | `0` |

The saved-run CPU-state postflight revalidated artifact, manifest, loader record, simulator identity, raw-capture hashes, invocation authorization, checkpoints A–D, all four ordered guest records, both Break continuations, the General Exception continuation, the unaligned IFA/ISR and single continuation, intentional SSC exit, and process cleanup. It reported `IA64_CPU_STATE_RUNTIME_VALIDATION=PASS`; there were no evidence errors or unexpected faults.

## Static rejection and regression record

The Unaligned Reference contract tests reject wrong vector, site/IIP/slot, IFA, ISR/access bits, unexpected fault count, missing/duplicate continuation, invalid return IPSR or PSR, missing AC cleanup, stale/mismatched artifact identity, unauthorized execution, and external-interrupt/scheduler activation. The final static candidate gate also validates the AC lifecycle, the bundle encoding/site, handler/return-state constraints, vector bounds, and target disassembly.

| Suite / gate | Result |
|---|---|
| IA-64 unittest discovery | PASS, 71 tests, including foundation, early-exec, loader, CPU-state, Break, General Exception, Unaligned Reference, and malformed ELF cases |
| Fresh candidate build and artifact/IVT gates | PASS; manifest and ELF identities match |
| Break, General Exception, CPU-state, early-exec, and Unaligned Reference static gates | PASS |
| Unaligned Reference negative fixtures | PASS; all eight test methods reject their malformed or unsafe cases |
| Saved-run CPU-state/runtime postflight | PASS; four checkpoints, 2 Break + 1 General + 1 Unaligned fault, no unexpected faults |
| Independent Ski register/SSC diagnostics | PASS for register output and FPSR |
| IA-64 FP codegen harness | PASS |
| Mbed TLS profile verification | PASS; Mbed TLS 4.1.0 and TF-PSA-Crypto 1.1.0 |
| Targeted shell syntax checks and `git diff --check` | PASS |

## References

- Intel, [Itanium Architecture Software Developer's Manual, Volume 2](https://www.intel.com/content/dam/www/public/us/en/documents/manuals/itanium-architecture-software-developer-rev-2-3-vol-2-manual.pdf).
- Intel, [Itanium Architecture Software Developer's Manual, Volume 3](https://www.intel.com/content/dam/www/public/us/en/documents/manuals/itanium-architecture-vol-3-manual.pdf).
- Pinned Ski source commit `dfc2902ea1423d9b32543a5daf8026213f2b37a1`; Ski executable identity is recorded above and in the probe/run manifests.
