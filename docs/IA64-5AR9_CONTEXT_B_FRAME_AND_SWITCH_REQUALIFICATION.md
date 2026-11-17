# IA64-5AR9 Context B Frame and Switch Requalification

**Outcome B — `IA64_COOPERATIVE_FRONTIER_ADVANCED` (2026-11-16).** The immutable IA64-5AR8 r2 probe passed the corrected Context B BSP bounds check at `0x144a00`. It then failed its first Context B branch-register preservation check: `b2` contained the `BR1_B` sentinel where `BR2_B` was expected. The probe reached Context B but did not switch back to A. The full A→B→A→B→A sequence and intentional SSC exit remain unqualified.

## Scope and preserved baseline

- Repository: `D:\dev\guideXOSServer_IA64`; branch `IA64_SUPPORT`, tracking `origin/IA64_SUPPORT`.
- Starting and validation HEAD: `a8fd15d760c30487643cc86329e4467649039d0c` (two commits ahead of the tracking reference at start). The scoped 5AR9 changes and this report were committed locally at phase close; the resulting HEAD is the closeout commit reported with this report.
- The existing IA64-4G accepted kernel identity remains `ab13fabb6a92ebe039afa2afd9729f6f6ceb2529b63be2b47b0c87cf264d960c`. It was verified and not loaded or executed.
- No guideXOS kernel was built or executed. No push, reset, clean, pull, merge, or rebase was performed. Existing probe and diagnostic artifacts were preserved, as were the pre-existing untracked Python caches.

## Immutable candidate and architecture checks

The exact r2 candidate and matching manifest were verified before execution:

| Artifact | Path | SHA-256 |
|---|---|---|
| Probe ELF | `/home/guidex/src/ia64-5ar8-coop-probe-20261115-r2-static/ia64-cooperative-context-probe.elf` | `dec92c95c500bd70b058540fcbf44e490629c31c451906f8acba9af86fc469f3` |
| Manifest | `/home/guidex/src/ia64-5ar8-coop-probe-20261115-r2-static/ia64-cooperative-context-probe.manifest.json` | `4addffab0102076d74ae23704bcea46f87517e18abf45f50cbdffeba666254b1` |

The candidate was the static-only artifact produced after the r1 assertion failure. Its expected qualification capture root and authorization marker were absent before the 5AR9 attempt, confirming that r2 had not already been executed. The ELF and manifest were not modified or rebuilt.

ELF symbols derive Context A at `0x113c00`, Context B at `0x113800`, A's RBS as `[0x134600,0x144600)`, and B's RBS as `[0x144a00,0x154a00)`. The B memory stack is `[0x124210,0x134210)`. Static validation ties Context B's RBS base, BSP, and BSPSTORE setup fields to its own ELF-derived base and its RBS limit to its own limit.

The generated disassembly around `ia64_coop_b_test` verifies:

- `alloc r16=ar.pfs,6,4,2,0` requests 6 inputs, 4 locals, and 2 outputs: a 12-register frame, or 96 bytes. The frame span is `[0x144a00,0x144a60)` and does not cross an RNAT collection slot.
- The check at `ia64_coop_b_bsp_check` (`0x10ce00`) reads `ar.bsp`, checks the ELF-derived lower and upper RBS bounds, then requires the current-frame start to equal `0x144a00`. It does not require BSP or BSPSTORE to advance by 96 bytes.
- The bounds-valid frontier is `0x10cea0`. The first-failure loop is `0x10df60`.

Intel's RSE description distinguishes the BSP location for the current frame from the frame span, and its `alloc` definition forms SOF from inputs, locals, and outputs. The corrected expectation is therefore BSP at the backing-store base while the 96-byte frame fits within the RBS. See the [Intel Itanium Architecture Software Developer's Manual, Volume 2](https://www.intel.com/content/dam/www/public/us/en/documents/manuals/itanium-architecture-software-developer-rev-2-3-vol-2-manual.pdf), §§6.3–6.5 and §6.11.3, and [Volume 3](https://www.intel.com/content/dam/www/public/us/en/documents/manuals/itanium-architecture-vol-3-manual.pdf) for the `alloc` operand definition.

The r2 target-static audit also retained the ABI-correct `out0/out1` call arguments and `in0/in1` reception, setup `b0`/PFS preservation across its nested call, separate A/B memory stacks and RBS allocations, first-entry versus resumed-entry paths, RSC/RNAT constraints, and the existing FP/ABI preservation checks. GNU `readelf --lint -W -h -l -S -s` exited 0. These are static properties; they do not establish runtime preservation.

## Diagnostic plan and preflight

The pinned Ski 1.5.1 executable was `/home/guidex/opt/ski-dfc2902/bin/bskinc`, SHA-256 `ff6b19cb87dbf2db39cda4478d0e49995c9a5ec9ddd05aac83191f4ba1e40b45`. The validated plan used six simultaneous program breakpoints, within Ski's ten-slot capacity:

| Target | Ski IP |
|---|---:|
| Context B first-entry bootstrap | `0x10b7c0` |
| Context A first resumption | `0x10bbe0` |
| Context B BSP bounds passed | `0x10cea0` |
| Context B second resumption | `0x10d6c0` |
| First assertion-failure loop | `0x10df60` |
| Context-switch `flushrs` | `0x10ecb4` |

The failure-loop target replaced the final-A breakpoint so the debugger could capture the first failure operands before the loop changes them. The finite command plan contains 471 step commands, caps execution at 240,193 instructions, and leaves four slots unused at peak. No dynamic breakpoint replacement is required.

The full IA-64 contract suite, source/static validation, ELF lint, hashes, debug-plan parser/capacity checks, Mbed TLS verifier, FP codegen harness, and syntax/whitespace checks passed before the authorized run. A fresh no-guest Ski parser/capacity smoke reported `guest_elf_loaded=false` and `guest_instructions_executed=0`.

One initial authorization-command attempt completed only its no-guest smokes, then stopped before writing the authorization marker or loading an ELF because `diagnose-ia64-coop-switch.py` lacked a `subprocess` import. That preparation failure and its zero-guest capture were preserved at `/home/guidex/src/ia64-5ar8-cooperative-qualification-20261115-r2/`. The import was fixed, the 165-test suite and preflight were rerun, and the single actual guest launch used a fresh capture root. No guest retry was made.

## Single authorized runtime result

Exactly one guest execution loaded the verified standalone probe in direct system mode. The run used one simulated CPU, the pinned Ski executable, PTY-backed stdin, a 2 GiB address-space limit, 20/22-second CPU limits, a 30-second wall limit, and the 240,193-instruction maximum. The authorization record explicitly sets `kernel_execution=false`.

The complete capture is preserved at `/home/guidex/src/ia64-5ar8-cooperative-qualification-20261115-r2-run-01/`, including `authorization-consumed.json`, `candidate.disassembly.txt`, `debugger-commands.txt`, `analysis.json`, raw register/context/event snapshots, `simulator/stderr.log`, `simulator/stdout.log`, `simulator/instruction-counts.txt`, and `simulator/run-result.json`. The captured disassembly SHA-256 is `40357712c946d3c80c95bc2fabbd0669c411b75b46f8ad3e6db3ba231fc6da11`; the stderr SHA-256 is `38f74bff5b0382d70c2dec310985000c819ae79b6eb8427183bc8b849e0289c4`; instruction-count file SHA-256 is `607a827b74dfae69b4fa0ce5943aa3de126140a4cc26561a60a90134bc7e6704`.

| Runtime frontier | Evidence |
|---|---|
| Initial A→B save | One `flushrs` frontier at `0x10ecb4`, instruction count 135,925; Context A snapshot records its suspended return at `0x10bbe0`. |
| Context B first entry | Reached once at `0x10b7c0`, instruction count 136,170; B's context identity is `0xB` and its first-entry count is 1. |
| Corrected BSP check | Passed once at `0x10cea0`, instruction count 136,245; debugger register capture shows `BSP=BSPSTORE=0x144a00`, `RSC=3`, and the stack pointer in B's ELF-derived stack range. |
| First failing invariant | Entered `ia64_coop_test_fail` at `0x10df60`, instruction count 136,299, before B's first-entry event record and before any B→A switch. |
| Later A/B resumes | No A resume, B second resume, second B→A transfer, or final A continuation was observed. |

At the failure loop, the analyzer captured Context B as active from SP, along with BSP, RSC, GP, `r14`, `r15`, context snapshots, event snapshot, and the loop disassembly. The preceding candidate instructions identify the failed comparison: `mov r14=b2` at `0x10cfa6`, expected `BR2_B` in `r15`, compare at `0x10cfc0`, and branch to the failure loop at `0x10cfdc`. The observed operand was `r14=0x4252314242423032` (`BR1_B`); the expected operand was `r15=0x4252324242423032` (`BR2_B`). The preceding `b1` comparison passed. The event snapshot contains only the initial `A_TO_B` event; B's first-entry preservation check fails before `B_FIRST_ENTRY` is recorded.

Candidate disassembly and [probe_sequence.s](../tests/ia64_cooperative_context_switch/probe_sequence.s) localize the seed defect: the B setup writes `BR1_B` to context offset 160 (`C_BR1`), then writes `BR1_B` again to offset 168 (`C_BR2`); it writes `BR2_B` at offset 176 (`C_BR3`) and shifts the following branch-register values. The switch restore reads offset 168 into `b2`. This is an initial Context B branch-register setup error; the run does not show a B→A save/restore failure. The BSP correction itself is valid.

| RSE / NaT / ABI / FP evidence | Result |
|---|---|
| Context B BSP and BSPSTORE | Both captured as `0x144a00`; corrected bounds check passed. |
| RSC and faults | B was in mode 3 at the check; Ski reported 0 faults and no unexpected exception. |
| Context A save | One initial A→B `flushrs` and saved continuation were observed; this run does not qualify a later restore. |
| Context B register state | GR4–GR7 and b1 checks passed before the b2 mismatch. Remaining ABI/application/GP/PFS checks and the B event record were not reached. |
| NaT/RNAT | The context snapshot records A's saved RNAT value and its valid/defined bits; there is no B→A round trip or restoration evidence. Tested NaT preservation remains unqualified. |
| FP | The B first-entry FP capture was after the failing check; no runtime FP preservation was established. |
| Full switch | No B→A frontier or A/B/A/B/A completion. |

The bounded run produced 472 register snapshots. The first assertion failure is recorded at aggregate `$insts$=136299`; the final progress sample is 238,699, below the 240,193 configured cap. Ski reported zero faults, no timeout, and a 4.981-second run. The simulator process exited with code 0 after the debugger command stream ended, but its classification was `SIMULATOR_EXIT_UNCLASSIFIED`; the runner status was 1. Process and process-group cleanup completed, PTY verification passed, stdout was empty, and intentional SSC exit was not observed. Output validation correctly rejected the missing/empty transition log. The failure loop was not bypassed.

## Regression and verification results

| Check | Result |
|---|---|
| Full IA-64 suite after final runner change | PASS: 165 tests |
| Cooperative context/RSE/NaT contracts | PASS, including negative cases for BSP advancing by frame size, invalid allocation/frame metadata, wrong B RBS identity, out-of-range BSP, invalid continuation, transition-record errors, and post-BSP assertion failure |
| Static-only r2 validation and target disassembly | PASS; r2 ELF and manifest hashes match the pinned identities |
| GNU `readelf --lint` | PASS |
| Diagnostic preflight and no-guest breakpoint/parser smoke | PASS; six of ten breakpoint slots used; zero guest instructions in smoke |
| Direct Mbed TLS profile verifier | PASS: Mbed TLS 4.1.0, TF-PSA-Crypto 1.1.0, three patches |
| FP codegen harness | PASS with GCC 13.5.0 and pinned Binutils 2.41; report `/home/guidex/src/ia64-5ar9-fp-codegen-20261116-r2/report.md` |
| Bash syntax, Python AST, and `git diff --check` | PASS |
| Kernel build / execution | NOT RUN / NOT RUN |

## Acceptance and next phase

This is **Outcome B**, `IA64_COOPERATIVE_FRONTIER_ADVANCED`: corrected Context B BSP validation passed, and the probe advanced to Context B's first register-preservation checks. The exact blocker is the initial B branch-register seed mapping at `C_BR2`, not the BSP/frame contract. The immutable r2 ELF remains unchanged and its single authorized execution is consumed. No new static candidate was prepared in this phase.

The next phase should correct the B seed mapping so `C_BR2` receives `BR2_B` and the remaining B branch-register fields map to their matching sentinels, add source-contract coverage for that mapping, build and statically validate a new candidate with a new manifest/hash, then seek separate authorization before any further guest execution. Preserve this capture and require the full A→B→A→B→A, RSE/NaT, FP/ABI, fault, and intentional-SSC evidence before probe acceptance. Production kernel integration remains pending.
