# IA64-5AR5 Return Link Repair and Cooperative Probe Qualification

**Date:** 2026-11-14 · **Branch:** `IA64_SUPPORT` · **Starting HEAD:** `2b05dbed16783ca29453e14bab3a0d38b7f51f0c`

**Outcome C — `IA64_COOPERATIVE_REQUALIFICATION_BLOCKED`.** The demonstrated setup return-link defect is repaired and passes target disassembly, host contract, ELF, and cooperative static gates. Exactly one standalone Ski launch was consumed as authorized. Ski loaded the verified candidate at ELF entry but rejected the diagnostic's 16-breakpoint plan with `All breakpoints in use` before executing a guest instruction. The setup return and every context-switch frontier therefore remain runtime-unobserved. No guideXOS kernel was loaded or run, and the probe was not rerun.

## Repository and preservation

- Repository: `D:\dev\guideXOSServer_IA64`, WSL2 Ubuntu 24.04 host `LEON2RDT` (Windows host `LEON2_RDT`).
- Branch: `IA64_SUPPORT`; starting HEAD `2b05dbed16783ca29453e14bab3a0d38b7f51f0c`.
- The initial worktree had only the pre-existing untracked Python cache files. They were preserved. Historical probe ELFs, manifests, Ski captures, and diagnostic records were not overwritten or removed.
- The accepted IA64-4G kernel was identity-checked only: SHA-256 `ab13fabb6a92ebe039afa2afd9729f6f6ceb2529b63be2b47b0c87cf264d960c`. It was not loaded or executed.
- No reset, clean, pull, rebase, merge, or push was used.

## Original defect and repair

IA64-5AR4 disassembly had `ia64_coop_probe_setup` at `0x10a2d0`, called by `ia64_coop_probe_run` with expected return continuation `0x10ae00`. Setup issued `br.call.sptk.many b0 = ia64_coop_set_nat8` at `0x10a2dc`; that call set `b0` to its continuation `0x10a2e0`. Setup later executed `br.ret.sptk.many b0` at `0x10ab4c` without restoring the caller's value. Control therefore branched back into setup at `0x10a2e0`, repeatedly rewriting the context records and never reaching the first switch.

The smallest repair in `tests/ia64_cooperative_context_switch/probe_sequence.s` allocates a two-register stacked frame before the helper call. It saves the old PFS in `r32` and incoming `b0` in stacked local `r33`, then restores PFS and `b0` before the final return. Those stacked locals remain owned by this procedure across the nested call; they are not caller-scratch static registers and are not helper arguments. The original helper, NaT setup, A/B records, stack/RBS maps, canaries, and setup work remain in place. No return address is hardcoded.

| Boundary | IA64-5AR4 | IA64-5AR5 candidate |
|---|---:|---:|
| Setup entry | `0x10a2d0` | `0x10a2d0` |
| Incoming caller continuation | `0x10ae00` | `0x10ae30` |
| Save frame / PFS and incoming `b0` | absent | `alloc` `0x10a2d0`; `r32` receives PFS at `0x10a2d6`; `r33` receives `b0` at `0x10a2e6` |
| Nested `br.call` / helper continuation | `0x10a2dc` / `0x10a2e0` | `0x10a2fc` / `0x10a300` |
| Restore PFS / `b0` | absent | `ar.pfs = r32` at `0x10ab66`; `b0 = r33` at `0x10ab6c` |
| Final `br.ret b0` | `0x10ab4c`, targets `0x10a2e0` | `0x10ab7c`, targets the saved `0x10ae30` caller continuation |

The target disassembly also confirms that the two-register frame is restored before return. `ar.pfs` is restored from the value captured by `alloc`, and `b0` is restored from the stacked local. This keeps the caller's register-frame ownership and legal continuation intact across the helper call.

## Related routine and switch audit

- `ia64_coop_probe_run` is non-leaf and preserves its incoming PFS, return link, and UNAT in its own stacked locals before calling setup and Context A.
- `ia64_coop_a_test` and `ia64_coop_b_test` preserve their incoming PFS and return link in stacked locals. Their switch calls allocate two output registers and pass pointers through `out0/out1`; the switch reads `in0/in1`.
- `ia64_coop_b_first_entry` calls the B test and then remains in its fail-safe loop; it has no return path that relies on the overwritten link. `ia64_coop_set_nat8` saves/restores its branch link around its RFI sequence and does not make a nested procedure call. `guidexos_ski_ssc` remains a leaf SSC bridge.
- The four switch call sites in this linked ELF are `0x10b24c`, `0x10b92c`, `0x10c84c`, and `0x10cf4c`. The output-register contracts pass the host and static gates.
- `tests/ia64_cooperative_context_switch/context_switch.s` is unchanged. Its target disassembly still contains the checked PFS save/restore, `flushrs` at `0x10dcb6`, incoming `ar.bspstore` write at `0x10de46`, and `br.ret b0` at `0x10e1cc`. These are static findings only.

## Fresh candidate and static qualification

The isolated build used the pinned IA-64 toolchain and wrote a new directory; the IA64-5AR3 artifact was left intact.

| Item | Identity |
|---|---|
| ELF | `/home/guidex/src/ia64-5ar5-coop-probe-20261114-r1/ia64-cooperative-context-probe.elf` |
| ELF SHA-256 | `ae0fc8fc49e576f37d8d6bf7d9846d0b293603ca7b9d15467afc81730992c74f` |
| Manifest SHA-256 | `1dbebf04c3be50909316024fec69e67b1410d55c33a0cf9f17dac3b1c8475585` |
| Disassembly | `/home/guidex/src/ia64-5ar5-coop-probe-20261114-r1/ia64-cooperative-context-probe.disassembly.txt` |
| Ski 1.5.1 executable SHA-256 | `ff6b19cb87dbf2db39cda4478d0e49995c9a5ec9ddd05aac83191f4ba1e40b45` |

Target preflight passed the ELF/manifest hash checks, IA-64 system-mode loader checks, PT_LOAD inspection, setup return-link disassembly gate, context-switch static gate, and the corrected call-site/output-register contracts. PT_LOAD ranges were `[0x100000,0x10e200)`, `[0x10f000,0x10f678)`, and `[0x110000,0x153428)`; the IVT is `[0x100000,0x108000)` and text is `[0x108000,0x10e200)`. Context identities, separate 64 KiB memory stacks and RSE backing stores, alignment, NaT setup, PFS paths, continuation bounds, and switch RSC/RNAT ordering passed static validation.

The linked disassembly frontier map is:

| Frontier | Target address |
|---|---:|
| ELF entry | `0x108000` |
| Setup entry | `0x10a2d0` |
| Nested call / return from helper | `0x10a2fc` / `0x10a300` |
| Setup exit `br.ret` / setup caller continuation | `0x10ab7c` / `0x10ae30` |
| First switch entry | `0x10cfc0` |
| Context A save (`flushrs`) | `0x10dcb6` |
| Incoming RBS handoff | `0x10de46` |
| First Context B instruction | `0x10aec0` |
| Return to Context A | `0x10b250` |
| Second Context B resumption | `0x10c850` |
| Final Context A continuation / exit | `0x10b930` / `0x10c060` |

Ski breakpoint IPs use the bundle plus encoded slot representation, so slot 1/2 addresses differ from the objdump byte addresses. The diagnostic tool maps these representations explicitly. Its command-file gate now caps the plan at nine breakpoints; all remaining A/B transitions are also sampled through bounded instruction checkpoints and memory-record snapshots.

## One authorized Ski launch

The authorization record is `/home/guidex/src/ia64-5ar5-coop-instruction-diagnostic-20261114-r1/authorization-consumed.json`. It records exactly one guest launch, `kernel_execution=false`, one CPU, 2 GiB address-space limit, 20/22-second CPU soft/hard limits, and a 30-second wall limit. The launch used the pinned `bskinc`, `-noconsole -forcesystem -i`, a verified PTY on child fd 0, and a verified snapshot of the candidate ELF. PTY attestation passed (`/dev/pts/1`); process-group cleanup completed. The standalone no-guest Ski command smoke also completed before the guest launch.

The command file hash was `eefe2ff1e54afdd82ac440d174f2fa1f478308561394fb13ae4a4093a5f28c9e`. It attempted to register **16** breakpoints. Ski emitted `All breakpoints in use` and exited with status 1 after the initial entry snapshot. This happened before the first `step` command:

- Initial IIP was `0x108000` and `$insts$` was `0`.
- One initial register dump was saved. No context or event memory dump was produced.
- Guest instruction count and fault count are unavailable; there is no Ski `-icnt` output.
- Guest stdout was empty (SHA-256 `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855`). Stderr SHA-256 is `ad2ee74299bdeb367794b59c2ec26509f77b645877cc9b9ce87a08564f330cf6`.
- The derived analysis was reclassified offline to record `SIMULATOR_OBSERVATION_LIMITATION`, failure frontier “ELF entry before first guest instruction,” zero executed instructions, and 16 requested breakpoints. Raw streams and initial register capture were not changed.

The static diagnostic contract now requests nine breakpoints and rejects an over-capacity plan. The capacity threshold itself was not measured in this one launch; nine is the conservative plan already exercised by the earlier IA64-5AR4 command file. This correction was verified by host tests and static preflight only. **There was no second guest launch.**

## Runtime qualification and attribution

The one launch did not execute a guest instruction, so it proves neither the repaired setup return nor a switch failure. The setup `br.ret`, its saved `b0`, the actual caller target, the first switch, Context A save, BSPSTORE handoff, Context B bootstrap, return/resume continuations, and final SSC exit were not observed. Runtime outgoing context pointers, stack/RBS ownership, RSC/RNAT validity, NaT, PFS, preserved ordinary registers, FP state, instruction count, and fault count are unavailable. Static source/disassembly evidence must not be presented as runtime preservation evidence.

Failure attribution is **Ski debugger observation limitation before first instruction**, not setup return failure, invalid switch arguments, RSE failure, or context-switch failure. Outcome C is required because the sole authorized launch was blocked before entering the probe.

## Regression results

| Check | Result |
|---|---|
| Full IA-64 host contract and malformed-ELF suite | PASS, 125 tests, 0 failures |
| Focused return-link contract | Included in full suite: save/PFS/restore/return positive case and negative missing-save, wrong-restore-order, and wrong-branch-continuation cases |
| Debugger contract | Included in full suite: 9-breakpoint plan, 10th-breakpoint rejection, finite step budget, zero-instruction capacity-failure classification, and old IA64-5AR4 offline localization fixtures |
| Candidate static preflight | PASS; exact ELF/manifest, system-mode loader, and setup/switch disassembly gates |
| FP code-generation harness | PASS; 21 source/profile compilations, with expected reload/allocation failures recorded in `/home/guidex/src/ia64-5ar5-fp-codegen-20261114-r1/report.md` |
| Direct Mbed TLS verifier | PASS; Mbed TLS 4.1.0, TF-PSA-Crypto 1.1.0, three patches |
| Targeted Bash syntax | PASS, three scripts |
| Targeted Python AST parse | PASS, four changed Python modules/tests |
| `git diff --check` | PASS |

No kernel run or additional guest diagnostic run occurred.

## Recommended next phase

The setup return-link correction is ready for runtime verification, but the one-run authorization in this phase is consumed. A future separately authorized standalone probe attempt should use the nine-breakpoint plan, preserve the same out-of-band registers/context/event checkpoints and finite limits, and stop after the first new failure frontier. It must observe setup returning to `0x10ae30` before attributing any subsequent failure. The context-switch contract and guideXOS kernel integration remain unqualified.
