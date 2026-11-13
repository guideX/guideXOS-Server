# IA64-5A Cooperative Context Switch Qualification

**Date:** 2026-11-12 · **Branch:** `IA64_SUPPORT` · **Starting HEAD:** `2640e00b40353f2094f87b502d76d01661399f12`

**Outcome C — `IA64_COOPERATIVE_SWITCH_BLOCKED`**

The bounded two-context mechanism and its independent Ski probe were implemented and passed the host and target-static gates. The single authorized standalone probe did not produce guest output and timed out at its 60-second bound. Its instruction and fault counts are unavailable. That result is ambiguous, so the runtime switch is not qualified and no kernel candidate was built or executed. The probe was not retried.

This report records a synchronous A/B switching experiment only. It does not qualify timer preemption, an interrupt path, a production scheduler, arbitrary contexts, or production multitasking.

## Preservation and provenance

- The work began from the specified `IA64_SUPPORT` HEAD with a clean tracked worktree and upstream divergence `0 0`. Existing untracked Python caches were preserved. No reset, clean, rebase, merge, pull, or push was used.
- The accepted IA64-4G kernel ELF remains preserved at `/home/guidex/src/ia64-4g-stacked-nat-rse-kernel-20261111-r3/bin/kernel.elf`, SHA-256 `ab13fabb6a92ebe039afa2afd9729f6f6ceb2529b63be2b47b0c87cf264d960c`.
- The pinned Ski executable is `/home/guidex/opt/ski-dfc2902/bin/bskinc`, version 1.5.1, SHA-256 `ff6b19cb87dbf2db39cda4478d0e49995c9a5ec9ddd05aac83191f4ba1e40b45`, commit `dfc2902ea1423d9b32543a5daf8026213f2b37a1`.
- The standalone probe uses GCC 13.5.0 for `ia64-linux-gnu` and the established Binutils shims. The code-generation policy is `-mfixed-range=f2-f5,f16-f31`.
- Probe build directories and the failed run capture remain under `/home/guidex/src/ia64-5a-*`; none was deleted or overwritten.

## Synchronous context contract

The contract follows Intel Itanium Architecture Software Developer's Manual, Volume 2, §6.11.3; the IA-64 Software Conventions and Runtime Architecture Guide, §10.2; and the historical Linux IA-64 `entry.S` context-switch sequence. Those references describe a cooperative procedure-call boundary and the required synchronous RSE handoff. They do not make this test a general task-switch implementation.

The private fixed-size `Context` is 1,024 bytes and 512-byte aligned. Each instance stores:

- GR4–GR7 spill images, a separate NaT-bearing GR8 bootstrap image, and the memory stack pointer, GP, TP, and return continuation.
- PFS, RSC, BSP, BSPSTORE, RNAT, caller UNAT, spill UNAT, LC, EC, CCV, all predicates, B1–B5, FPSR, and the preserved FP images F2–F5 and F16–F31.
- Stack and backing-store bounds, first-entry state, identity, switch and entry counters, profile magic, canaries, observed pointers/PSR, RNAT validity mask, and allowed code bounds.

GR4–GR7 and the FP images cover procedure-call-preserved state for this profile. The test also preserves GP, TP, SP, PFS, B0, B1–B5, PR, LC, EC, CCV, FPSR, and both UNAT meanings because the switch implementation and its probes rely on them. GR8 is part of the test-specific bootstrap/NaT contract. Caller-scratch registers are not promised across a cooperative call. The FP policy leaves caller-saved F6–F15 and F32–F127 available to GCC; absence of explicit source-level floating-point arithmetic is not treated as proof that generated code avoids FP registers.

The switch uses the following order:

1. Validate profile magic and A/B identities, nonoverlapping stack/RBS maps, bounds and alignment, target RSC, first-entry versus saved PFS, and return continuation inside the linked text range.
2. Save the outgoing SP/GP/TP/continuation/PFS and the required ABI and application state. Save GR4–GR7 with `st8.spill`; capture AR.UNAT after the spill separately from the caller's original UNAT. Save preserved FP images with `stf.spill`.
3. Run `flushrs`, `mf`, enter RSC mode 0, serialize with `srlz.d`, then capture RNAT and BSPSTORE. Compare BSP with the drained BSPSTORE and derive the valid RNAT mask from the BSPSTORE collection index. Reject an out-of-range mask; never treat undefined RNAT bits as meaningful.
4. Invalidate the ALAT with `invala`. Enter enforced-lazy mode before installing the incoming BSPSTORE. Serialize that write before restoring the target RNAT, because writing BSPSTORE invalidates RNAT. Restore PFS, UNAT, GR4–GR7, predicates, branches, application state, preserved FP images, TP, GP, SP, and B0.
5. Restore the target RSC and serialize before the synchronous `br.ret`. First entry and saved-context resume have separate PFS rules: B's bootstrap has PFS zero; a suspended context must have nonzero PFS. Unsupported profiles fail-stop with interrupts masked.

The routine does not copy active backing-store memory with ordinary loads/stores. Stacked GR33's A-side NaT value is carried by the RSE spill/fill path and checked with `tnat` at the probe continuation. The test reports this architectural observation directly; it does not infer NaT from an integer value.

## Independent storage and bootstrap

The probe is a standalone Ski system-mode ELF. Context A is a controlled assembly context in the probe, not the guideXOS kernel's current execution context. Context B has a separate first-entry continuation. The B bootstrap starts on B's memory stack and 64 KiB backing store with RSC 3, PFS zero, a context-specific ordinary GR8 sentinel, and a clear GR8.NaT state. Subsequent B switches use its saved continuation and nonzero PFS.

Each memory stack and each RSE backing store is 64 KiB with guard canaries. Context objects are distinct 1,024-byte, 512-byte-aligned records. The static linked ranges were:

| Region | Start | End |
|---|---:|---:|
| IVT | `0x100000` | `0x108000` |
| Probe text | `0x108000` | `0x10e1d0` |
| Context B | `0x112200` | `0x112600` |
| Context A | `0x112600` | `0x112a00` |
| A memory stack | `0x112a10` | `0x122a10` |
| B memory stack | `0x122c10` | `0x132c10` |
| A RSE backing store | `0x133000` | `0x143000` |
| B RSE backing store | `0x143400` | `0x153400` |

The linker and probe static gate check the guards, 16-byte stack alignment, 512-byte RBS alignment, fixed sizes, and pairwise nonoverlap. The static-only report also verified that the target disassembly contains the synchronous save/restore sequence and no `rfi` in the switch routine.

## Static probe identity and one runtime attempt

The final freshly built static-only probe is `/home/guidex/src/ia64-5a-coop-probe-20261112-r10/ia64-cooperative-context-probe.elf`, SHA-256 `9189f5c26b819016dd0bd5f663f201d30cdcfb58fc7ad9046381806894bb4b45`. Its manifest SHA-256 is `000a98cebc36a28dd4d5afa81e6faf2a29b0028f4a40a4793af89bf90f7d8f0c`. The same ELF hash was used by the one guest attempt in r8; the r10 build contains the same generated instructions, with a source label/comment making the separate PFS path explicit. R10 passed `cooperative-context-probe --static-only` after that static source check was added.

The cooperative-context probe was executed once from `/home/guidex/src/ia64-5a-coop-probe-20261112-r8/capture/` with the qualified runner, one CPU, and `-forcesystem`. Separately, the requested register-diagnostic harness ran two standalone diagnostic ELFs. Across this phase there were three standalone Ski guest executions and zero guideXOS kernel executions. The runner invoked for the cooperative probe:

```text
/home/guidex/opt/ski-dfc2902/bin/bskinc -stats -icnt /home/guidex/src/ia64-5a-coop-probe-20261112-r8/capture/instruction-counts.txt -forcesystem /tmp/guidexos-ia64-ski-elf-hi8ggm9u/selected.elf
```

| Probe postflight evidence | Result |
|---|---|
| Executions | 1 cooperative probe, 2 standalone register diagnostics; 0 guideXOS kernel executions |
| Classification | `COOPERATIVE_CONTEXT_PROBE_FAIL` (`TIMEOUT`) |
| Bound / exit | 60.002 seconds; exit `-15` after timeout |
| Stdout / stderr | Both captured files exist and are empty (0 bytes) |
| Transition and state records | None; no A/B events, context-state lines, NaT, PFS, FP, or RNAT observations |
| Ski instruction count / fault count | Unavailable / unavailable; the instruction-count file exists but the runner could not parse a final count |
| Process cleanup | Complete |
| Automatic retry / kernel run | None / none |

The run record's classification and qualification errors are in `run-result.json` in the capture directory. With no guest records, this result does not establish whether either context entered, whether a fault occurred, or where execution stopped. No cause is assigned. Because the independent runtime gate did not pass, no isolated early-exec kernel candidate was built, authorized, or run.

## Host validation and regression results

| Check | Result |
|---|---|
| Cooperative probe ELF, manifest, layout, and target-disassembly static gate | PASS; IVT, text, contexts, stacks, RBS, and switch ordering inspected |
| IA-64 contract regression suite | PASS, 95 tests across the existing IA-64 contracts plus 11 cooperative failure-containment tests |
| Malformed ELF/IVT fixtures | PASS, 3/3: bad IVT size, handler boundary, and source offset |
| Independent Ski register diagnostics | PASS: fixed ABI wrapper byte-exact output, known IVA/BSPSTORE/RSC/PSR checks, and FPSR readback; RNAT explicitly remains unverified by this separate diagnostic |
| IA-64 FP code-generation harness | PASS; its report records expected GCC reload-failure cases and verifies the ABI-scratch profile. It is a compile/code-generation harness, not runtime FP-switch evidence. |
| Direct Mbed TLS profile verification | PASS: Mbed TLS 4.1.0, TF-PSA-Crypto 1.1.0, 3 patches |
| Targeted IA-64 shell and Python syntax checks | PASS |
| `git diff --check` | PASS |

The cooperative negative tests exercise host postflight rejection for wrong context identity, overlapping backing stores, bad stack bounds/alignment, invalid return continuation, missing/changed PFS, lost NaT, invalid defined RNAT bits, duplicate/reordered/restarted events, PSR.i set, and unsupported manifest/profile state. Source checks also reject the wrong first-entry PFS predicate and an unsupported RSC value. These checks do not substitute for guest execution of negative contexts.

## Qualification limits and next phase

IA64-5A remains blocked at the independent runtime probe. There is no kernel candidate SHA-256, kernel manifest, loader record, kernel fault count, or kernel state record for this phase. The five accepted early-exec exceptions and IA64-4G result were not rerun, because the prerequisite probe failed.

The current primitive and record stay isolated under `tests/ia64_cooperative_context_switch/`; the incomplete production scheduler in `kernel/arch/ia64/context_switch.cpp` was not activated. No claim is made for interrupt-driven switching, timer preemption, arbitrary thread frames, user mode, paging changes, device initialization, SMP, or production scheduling. FP coverage is limited to the defined preserved image; scratch FP state is caller-clobbered at this synchronous ABI boundary. RSE coverage is limited to the configured 64 KiB stores and this exact A/B probe.

The next phase should start from this static-only artifact and first address the timeout using a separately authorized diagnostic plan. Do not retry this artifact automatically. A future one-run probe must produce the complete ordered A/B/A/B/A log, both final context maps, valid NaT/PFS/FP/RNAT records, zero unexpected faults, and intentional SSC exit before any kernel candidate can be considered.

## Architectural references

- Intel, [Itanium Architecture Software Developer's Manual, Volume 2](https://www.intel.com/content/dam/www/public/us/en/documents/manuals/itanium-architecture-software-developer-rev-2-3-vol-2-manual.pdf), §6.11.3, synchronous backing-store switch and RSE state rules.
- Intel, [IA-64 Software Conventions and Runtime Architecture Guide](https://portal.cs.umbc.edu/help/architecture/24525601.pdf), §10.2, non-preemptive thread switching conventions.
- Linux, [historical IA-64 `entry.S`](https://kernel.googlesource.com/pub/scm/linux/kernel/git/stable/linux-stable/%2B/4a6808f347a3f969e56e8e4274e70849ecdc33de/arch/ia64/kernel/entry.S), context-switch implementation reference.
