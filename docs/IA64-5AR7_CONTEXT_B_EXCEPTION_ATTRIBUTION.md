# IA64-5AR7 Context B Exception Attribution

**Status (2026-11-15): Outcome B — `IA64_CONTEXT_B_ILLEGAL_OPERATION_ATTRIBUTED`.** The single authorized standalone Ski run reached Context B and captured the first General Exception vector entry before any handler instruction ran. The saved state identifies an Illegal Operation at `0x10c150`: Context B read `ar.bspstore` while `RSC.mode=3`. The cooperative probe's full A→B→A→B→A sequence remains unqualified. A one-instruction probe-source correction was built and passed static gates, but was not executed because the one-run authorization is consumed. No guideXOS kernel was built or run.

## Repository and preserved inputs

- Repository: `D:\dev\guideXOSServer_IA64`; branch `IA64_SUPPORT`; starting HEAD `9be0a05ecbbf00057ca17d0bdcedc065980fb82d`.
- The starting tracked worktree was clean. Pre-existing untracked Python cache artifacts were preserved. No reset, clean, pull, rebase, merge, or push was used.
- The accepted IA64-4G kernel was checked by identity only: SHA-256 `ab13fabb6a92ebe039afa2afd9729f6f6ceb2529b63be2b47b0c87cf264d960c`. It was not built or executed.

| Input | Identity |
|---|---|
| IA64-5AR5 cooperative probe ELF | `ae0fc8fc49e576f37d8d6bf7d9846d0b293603ca7b9d15467afc81730992c74f` |
| IA64-5AR5 manifest | `1dbebf04c3be50909316024fec69e67b1410d55c33a0cf9f17dac3b1c8475585` |
| Pinned Ski source | `dfc2902ea1423d9b32543a5daf8026213f2b37a1`, version 1.5.1 |
| Pinned `bskinc` executable | `ff6b19cb87dbf2db39cda4478d0e49995c9a5ec9ddd05aac83191f4ba1e40b45` |

The run capture is `/home/guidex/src/ia64-5ar7-context-b-exception-20261114-r1/`. The reviewed debugger command file hash is `88258af506f06a66251b9ecf6dca341536c0a3248ab8d1dea1098d96c928facb`. Its six statically resolved breakpoints were installed before the first step; the plan left four of Ski's ten program-breakpoint slots unused. The only guest-executing commands were finite steps, with a ceiling of 136,677 instructions. The run used one CPU, a 2 GiB address-space limit, a 20/22-second CPU soft/hard limit, a 30-second wall limit, direct system mode, and verified PTY stdin.

A no-guest register-alias smoke passed first, loading no ELF and executing zero guest instructions. The authorization record then consumed exactly one standalone probe launch. The simulator exited with status 0 after 0.068 seconds; process and process-group cleanup completed. The raw per-step result was `38 insts (1 fault)` for the step that reached the vector. The cumulative debugger `$insts$` sample at the vector was 135,190. The runner's generic classification was `SIMULATOR_EXIT_UNCLASSIFIED` because the bounded diagnostic intentionally quit before the probe's success marker; this is not a cooperative qualification pass.

Raw run evidence:

| File | SHA-256 |
|---|---|
| `simulator/stderr.log` | `07818bd5670543898d3bf76d9c27aeecf232a0ca32efe04c5c75ac88743f7fd5` |
| `simulator/stdout.log` (empty) | `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` |
| `simulator/instruction-counts.txt` | `6e60db83df57085894dbd7fd293e713b1b9bf2fe5a7bd0556b63c2d99d97d14a` |
| Offline `analysis.json` | `f9d0618d0d1b85676225c8d8991e6fdce08d39e5a766f4c16b0cc47a1c795abc` |

## Exact exception capture

The observed frontiers were:

| Frontier | Current IP | Cumulative `$insts$` |
|---|---:|---:|
| Context B first instruction | `0x10aec0` | 135,141 |
| B bootstrap call boundary | `0x10aef0` | 135,149 |
| `ia64_coop_b_test` entry | `0x10c070` | 135,152 |
| General Exception vector entry (`IVA+0x5400`) | `0x105400` | 135,190 |

At `0x105400`, the debugger stopped before the vector's first bundle (`rsm psr.i|psr.ic`) executed. Thus the following values are the saved interruption state before handler changes:

| Register/state | Captured value | Interpretation |
|---|---:|---|
| `CR.IVA` | `0x100000` | Matches the probe IVT base |
| Current IP | `0x105400` | Exact General Exception vector entry |
| Current `PSR` | `0x10` | `PSR.ic=0` at vector entry |
| `CR.IIP` | `0x10c150` | Faulting 16-byte bundle |
| `IPSR` | `0x2010` | Saved interruption state; `IPSR.ic=1`, `IPSR.ri=0` |
| `ISR` | `0x0` | `ISR.code=0` (Illegal Operation), `ISR.ei=0` (slot 0) |
| `IFS` | `0x0` | `IFS.v=0`; other IFS fields were not interpreted |
| `IIPA` | `0x10c140` | Raw debugger value recorded without inferring a second fault address |
| `RSC` | `0x3` | `RSC.mode=3` (eager) |
| `BSP` / debugger `BSPSTORE` | `0x143400` / `0x143400` | Both captured out-of-band before handler execution |
| `SP` / `PFS` / `B0` | `0x131c10` / `0x0` / `0x10af00` | Context B state at the exception |

The `IPSR.ri` and `ISR.ei` fields agree on slot 0. Target disassembly at `0x10c150` is:

```text
10c150: [MMI] mov.m r14=ar.bspstore;;
10c156:       nop.m 0x0
10c15c:       nop.i 0x0;;
```

The probe's B-entry test performs this read before writing its `B_FIRST_ENTRY` event. The General Exception therefore occurred after the first A-to-B handoff and B bootstrap, but before the B test's first-event record and before any B-to-A switch. The saved context dumps identify Context A as `0xA` and Context B as `0xB`; the event record still contains only `A_TO_B`.

## Root cause and source correction

IA-64 requires `RSC.mode=0` for guest reads of `ar.bspstore` and `ar.rnat`. With `RSC=3`, the `mov r14=ar.bspstore` at `0x10c150` is an Illegal Operation. The exact vector, IIP, slot, ISR code, and RSC value establish this attribution. The independent IA64-4D qualification documents the same architecturally selected faulting operation and its mode requirement. See Intel's [Itanium Architecture Software Developer's Manual, Volume 2](https://www.intel.com/content/dam/www/public/us/en/documents/manuals/itanium-architecture-software-developer-rev-2-3-vol-2-manual.pdf), the [IA64-4D General Exception qualification](IA64-4D_GENERAL_EXCEPTION_QUALIFICATION.md), and pinned Ski's [Illegal Operation dispatch](https://github.com/trofi/ski/blob/dfc2902ea1423d9b32543a5daf8026213f2b37a1/src/interruption.c).

The probe's check only needs to prove that Context B's active backing-store pointer is within B's RSE range. The minimal source correction in `tests/ia64_cooperative_context_switch/probe_sequence.s` reads `ar.bsp`, which is available in every RSC mode, for that bounds check. The debugger still captures `BSPSTORE` out-of-band when exact observation is needed. The context-switch implementation and kernel sources were not changed. A focused host contract test rejects a mode-3 `ar.bspstore` read in the B test.

The fresh static-only candidate is `/home/guidex/src/ia64-5ar7-coop-probe-fix-20261114-r1/ia64-cooperative-context-probe.elf`, SHA-256 `4f503b27f634ffa2205bb558a5de1fd998ff7222c598525366278bee75e4d430`; its manifest SHA-256 is `274819e549e9a44f2f77640232a4fab6c5a85ed8f52f5866c0701ce2228a1890`. The existing cooperative artifact/manifest/layout/disassembly static gate passed. This repaired candidate was not executed.

## 5AR6 evidence correction

The preserved IA64-5AR6 raw step log contains `1 fault` in the step that entered the IVT; its later pause-loop steps contain `0 faults`. The old high-level `run-result.json` value of zero summarized the final step, not cumulative faults. The pause-loop register sample was taken after the vector's `rsm`, so its CR.IIP/IPSR/ISR/IFS values could not identify the original fault. This report leaves the historical IA64-5AR6 report and capture unchanged; IA64-5AR7 now provides the first exact subtype and instruction attribution.

The first IA64-5AR7 offline analysis also exposed a provenance-check bug: it compared each register-dump file hash with the ELF hash. That comparison is now correctly split between the dump's own SHA-256 and the selected ELF identity. A read-only `--analyze-existing-capture` path produced the analysis from the saved run without launching Ski again.

## Regression and supporting checks

| Check | Result |
|---|---|
| Full IA-64 host contract suite, run in WSL/Linux | PASS, 151 tests across 12 suites |
| Malformed ELF/IVT negative fixtures | PASS, 3/3: bad IVT size, handler boundary, and source offset |
| Corrected cooperative probe static gate | PASS; ELF, manifest, layout, and disassembly |
| Context B exception attribution negative fixtures | PASS, including wrong vector, slot, ISR class, missing state, stale/duplicate snapshots, wrong context, and mismatched artifact/simulator identities |
| Debugger command contract | PASS; unsupported commands, excess breakpoints, and unbounded steps rejected |
| IA-64 FP code-generation harness | PASS; 21 compile cases, 13 expected passes and 8 expected allocation failures reproduced; no guest executed |
| Direct Mbed TLS profile verifier | PASS; Mbed TLS 4.1.0, TF-PSA-Crypto 1.1.0, three patches |
| Bash syntax | PASS, five build/run scripts |
| Python AST and `git diff --check` | PASS |

## Disposition

The first Context B exception is attributed to the probe's mode-3 `ar.bspstore` read. The repaired probe source and artifact are statically qualified only. Since the single guest execution is consumed, a separate 5AR8 authorization is needed to verify the corrected probe through the complete A→B→A→B→A sequence and intentional exit. No guideXOS kernel, production scheduler, interrupt path, or general context-switch preservation behavior is qualified by IA64-5AR7.
