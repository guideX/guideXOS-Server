# IA64-5AR6 Breakpoint-Safe Cooperative Qualification

**Status (2026-11-14, local time): Outcome B — `IA64_COOPERATIVE_FRONTIER_ADVANCED`.** The corrected setup return was observed and verified. The standalone probe then entered Context B for the first time, but control moved into the probe's IVT pause loop before the B-to-A transition. The complete A→B→A→B→A sequence is not qualified. The one authorized guest execution is consumed; no guideXOS kernel was built or run.

## Scope and identities

This phase replaced IA64-5AR5's over-capacity 16-address debugger plan with a fixed eight-target plan and used the single authorized standalone Ski launch to observe the repaired return and the next switch frontiers. The launch started from branch `IA64_SUPPORT` at `c27dd45346cd2d94523e8e67758068952d291998`; the recorded upstream tracking ref was `2b05dbed16783ca29453e14bab3a0d38b7f51f0c`. The existing one-commit-ahead state and untracked Python caches were preserved.

| Input | Identity |
|---|---|
| Cooperative probe ELF | SHA-256 `ae0fc8fc49e576f37d8d6bf7d9846d0b293603ca7b9d15467afc81730992c74f` |
| Probe manifest | SHA-256 `1dbebf04c3be50909316024fec69e67b1410d55c33a0cf9f17dac3b1c8475585` |
| Pinned IA64-4G kernel identity checked by the loader | SHA-256 `ab13fabb6a92ebe039afa2afd9729f6f6ceb2529b63be2b47b0c87cf264d960c` |
| Ski source | `dfc2902ea1423d9b32543a5daf8026213f2b37a1`, version `1.5.1-32-gdfc2902` |
| Batch Ski executable (`bskinc`) | SHA-256 `ff6b19cb87dbf2db39cda4478d0e49995c9a5ec9ddd05aac83191f4ba1e40b45` |

The accepted kernel hash above was used only as an identity guard. The probe ran as a verified system-mode standalone ELF. No guideXOS kernel build, loader invocation with a kernel, or kernel execution occurred.

## Breakpoint capacity and plan

The [HP Ski IA-64 Simulator Reference Manual, Section 8.3](https://manualzilla.com/doc/7031936/hp-ia-64-user-s-manual) documents ten program breakpoint IDs, 0–9, and explains that `bl` may display locations symbolically. Inspection of the pinned Ski source (`src/ssDBT.c`) independently confirmed ten program breakpoint entries, separate from data breakpoints; duplicate canonicalized addresses do not allocate another slot, `bd` frees one ID for reuse, and `bD` clears the program breakpoints. A no-guest Ski smoke observed IDs 0–9, lowest-slot reuse after `bd`, clearing after `bD`, duplicate-alias handling, rejection of unsupported slot 3, and rejection of an eleventh unique address. No guest ELF was loaded in these debugger smokes.

The diagnostic deliberately used a conservative maximum of eight unique targets, leaving two documented program-breakpoint slots free. Its targets were setup entry, nested helper return, setup's final `br.ret`, setup caller continuation, first switch entry, incoming `ar.bspstore` handoff, Context B first-entry bootstrap, and first return to Context A. The exact statically resolved targets were `0x10a2d0`, `0x10a300`, `0x10ab78`, `0x10ae30`, `0x10aec0`, `0x10b250`, `0x10cfc0`, and `0x10de44` (listed here by address, not allocation order). All were validated against the candidate ELF, PT_LOAD ranges, and target disassembly before launch.

The command file listed breakpoints with `bl` before its first `step`. Startup used a 64-instruction step and a one-instruction follow-up, followed by at most 469 finite 512-instruction steps: an absolute ceiling of 240,193 guest instructions. The plan contains no `run` or `cont` command and no dynamic breakpoint replacement. The executed command file's SHA-256 was `a1292933e270527391ecfcd134ff6eeb74ab9ff5ee7bacb18e083d824b578e3f`.

Ski's runtime `bl` output contained eight physical program-breakpoint rows with IDs 0–7, matching the planned count and sequential slot allocation. Its symbol column was abbreviated (for example `ia64_coop_probe*` and `ia64_coop_*+0e84`), so the list did not expose exact runtime addresses for a separate address-by-address comparison. The host-side ELF and command-file preflight had already resolved and checked all eight addresses. The original `analysis.json` was produced before the symbol-aware parser correction and therefore records an empty parsed breakpoint list; its raw `simulator/stderr.log` is retained unchanged. The updated parser preserves symbolic locations and marks the separate runtime address comparison indeterminate while accepting the checked slot count against the statically validated command plan. Regression tests cover numeric and symbolized `bl` output.

After the run, a fresh **preflight-only** invocation passed against the same pinned candidate and eight-target plan. It generated a different command-file hash (`94ae39881a8840fdbbe9dba926334f3299d249edbbeea1222cf865a7d0b61e83`) because its capture directory was different; it did not launch a guest.

## The single authorized run

The authorization record is at `/home/guidex/src/ia64-5ar6-coop-breakpoint-safe-qualification-20261114-r1/authorization-consumed.json`. The simulator ran once with one simulated CPU, 2 GiB address-space limit, 20/22-second CPU soft/hard limits, a 30-second wall limit, verified PTY stdin, and Ski `-stats -icnt`. It exited with status 0 after 3.141 seconds, reported zero Ski faults, and was not timed out. Cleanup completed. The stdout file was empty (SHA-256 `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855`); stderr SHA-256 was `a30f58d446312c5d25a6710f45125fcf4269a63293d8ac5c0187b39a766c44d8`.

The runner's validation status is 1 because the probe did not produce its expected ordered transition record or intentional SSC exit. This is consistent with the simulator's clean process exit: the simulator exited normally, while the cooperative qualification did not complete. The captured RAM event record had no errors and contained only one event, `A_TO_B`.

## Runtime frontiers

| Frontier | Captured evidence |
|---|---|
| Setup entry, 133,713 instructions | `b0=0x10ae30`, `PFS=774`, `SP=0x122a10`, `GP=0x310000`, `RSC=3`; entry was reached. |
| Nested helper return, 133,817 | Nested `b0=0x10a300`; saved incoming `b0` in `r33=0x10ae30` and saved incoming `PFS` in `r32=774`. |
| Setup final `br.ret`, 134,162 | Restored `b0=0x10ae30`, `PFS=774`; the caller continuation at `0x10ae30` was observed next at 134,163. All setup return checks passed. |
| First switch entry, 134,312 | Correct context A/B pointers were present in the incoming registers; outgoing `r42/r43` were `0x112600` / `0x112200`. |
| Incoming BSPSTORE handoff, 134,971 | Context A snapshot: `BSP=BSPSTORE=0x133080`, `RSC=3`, `RNAT=0x80`, valid RNAT mask `0x1ffff`, defined bits `0x80`, saved return `0x10ae90`, and `switch_count=1`. |
| Context B first-entry bootstrap, 135,141 | Hit `0x10aec0`. At entry Context B had `SP=0x131c10`, `GP=0`, `BSP=BSPSTORE=0x143400`, `RSC=3`, `PFS=0`, and return target `0x10aec0`. The next snapshot, at 135,653, recorded `first_entries=1`; Context A remained at `switch_count=1`, and the event record still contained only `A_TO_B`. |
| First post-entry sample, 135,653 | `IIP=0x105410`, `IP=0x105414` (slot 1), inside `ia64_ivt`. Later samples reported the same bundle at `0x105410` / `0x105418` (slots 0/2) and stayed in that IVT loop through the final captured sample. |

The standalone probe's `entry.s` gives its IVT entries a fail-safe sequence that disables interrupts, synchronizes, then loops on `hint @pause` and a backward branch. The observed `ia64_ivt+0x5410`/adjacent-slot sequence is consistent with that pause loop. This capture does **not** identify what caused control to enter the IVT; Ski reported zero faults, and the qualification did not record a classified exception cause. No cause is inferred here.

No breakpoint for return to Context A was hit. No `B_FIRST_ENTRY`, `B_TO_A_FIRST`, `A_RESUMED_FIRST`, second B entry, later A return, or final exit event was captured. The furthest proven cooperative frontier is **Context B first-entry bootstrap**; the first post-entry sample is already in the IVT loop. A complete A→B→A→B→A sequence, B-to-A switching, resumed-state preservation, and intended exit remain unqualified.

There are 472 register evaluations and 471 context/event snapshots, with no snapshot collection errors. The highest captured `$insts$` value was 237,541, below the 240,193 absolute plan ceiling. Ski's last `-icnt` line says 512 instructions for that final step; this is the final step's count, not a cumulative run total. The exact cumulative guest instruction count is therefore not reported as an independent statistic.

## Regression and supporting checks

| Check | Result |
|---|---|
| Full IA-64 host contract, malformed-ELF, debugger, and loader suite after parser change | PASS: 131 tests, 0 failures |
| Focused cooperative debugger and context-switch suites | PASS: 43 tests, 0 failures |
| Exact eight-target static preflight on the pinned probe/manifest/kernel identities | PASS; 8 unique entries, no duplicates, two free slots, bounded step plan |
| No-guest Ski parser/capacity smoke | PASS; ten IDs listed, `bd` reuse, `bD` clear, duplicate alias handled, 11th unique target and unsupported slot 3 rejected |
| Mbed TLS direct profile verifier | PASS; Mbed TLS 4.1.0, TF-PSA Crypto 1.1.0, three patches |
| FP code-generation harness | PASS as a diagnostic harness: 21 compile attempts recorded; the runtime-reserved cases reproduce GCC failures, and selected `compile-only-abi-scratch` profiles pass. Compile-only output is not executable. Evidence: `/home/guidex/src/ia64-5ar6-fp-codegen-20261114-r1/report.md`. |
| Python AST checks, Bash syntax checks for the IA-64 build/run scripts, native `git diff --check` | PASS |

The first broad test invocation was interrupted during a fake-simulator loader rejection test while its safety behavior was being audited. The complete suite was then rerun to completion; all 131 tests passed, including that synthetic test. It uses a fake ELF and a sentinel simulator, not a guideXOS kernel.

## Disposition and next phase

This is **Outcome B**, because the runtime advanced past the repaired setup return and through the first A-to-B handoff, but stopped short of the required full transition sequence. It is neither full cooperative qualification (Outcome A) nor debugger blocked before runtime observation (Outcome C). The sole authorized guest execution is consumed; the capture was not retried. Any next diagnostic needs separate authorization and should focus on the first instruction path from `ia64_coop_b_first_entry` to the first IVT sample, preserving the same identity checks and bounded stepping. Do not treat the present partial run as proof of general RSE, task switching, scheduler, interrupt, or kernel integration behavior.
