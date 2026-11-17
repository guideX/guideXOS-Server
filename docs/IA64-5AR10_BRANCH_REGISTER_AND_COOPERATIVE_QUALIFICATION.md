# IA64-5AR10 Branch-Register and Cooperative Qualification

**Outcome B — `IA64_COOPERATIVE_FRONTIER_ADVANCED` (2026-11-17).** The repaired Context B `b1`–`b5` values and BSP/frame checks passed in the one authorized run. Context B then failed its first application-state check: observed `ar.ec` was `0`, expected `EC_B` was `0x62`. The probe did not reach B→A, and the full A→B→A→B→A preservation contract remains unqualified.

## Scope and preserved state

- Repository: `D:\dev\guideXOSServer_IA64`; branch `IA64_SUPPORT`.
- Starting HEAD: `c7710322275ab386eb9cd1dba374aa7cc797c750`, tracking `origin/IA64_SUPPORT`, ahead by three commits.
- No reset, clean, pull, merge, rebase, or push was performed. Existing untracked Python caches and historical probe artifacts were left in place.
- The IA64-5AR8 r2 ELF and manifest remain unchanged: `dec92c95c500bd70b058540fcbf44e490629c31c451906f8acba9af86fc469f3` and `4addffab0102076d74ae23704bcea46f87517e18abf45f50cbdffeba666254b1`.
- The accepted IA64-4G kernel remains `ab13fabb6a92ebe039afa2afd9729f6f6ceb2529b63be2b47b0c87cf264d960c`. It was verified, not built or run.
- No guideXOS kernel was built or executed. The run loaded only the standalone cooperative probe. Interrupts, scheduling, paging, timer preemption, user mode, and device initialization stayed disabled.

The prior result and its complete analysis are preserved in [IA64-5AR9](IA64-5AR9_CONTEXT_B_FRAME_AND_SWITCH_REQUALIFICATION.md).

## IA64-5AR9 defect and repair

IA64-5AR9 r2 reached Context B's branch assertions after passing the BSP/frame check. It read `b2 = 0x4252314242423032` (`BR1_B`) where `0x4252324242423032` (`BR2_B`) was expected. The first B→A switch was not reached.

The preserved r2 target disassembly identifies the fixture error. At `0x10afc6`, setup loaded `BR1_B` and stored it at offset 160 (`C_BR1`). At `0x10b1a6`, setup loaded `BR1_B` again and stored it at offset 168 (`C_BR2`). It then loaded `BR2_B` at `0x10b1c6` for offset 176 (`C_BR3`), shifting the remaining values. The context-switch restore used offset 168 for `b2`; the storage layout, restore mapping, assertion, 64-bit values, and debugger readback were consistent. The defect was the B initialization mapping alone, not a reason to change the expected `b2` value.

The corrected setup now stores each B sentinel into its matching `C_BR1`–`C_BR5` slot at offsets 160, 168, 176, 184, and 192. Context A and B use five unique, register-specific 64-bit values each. Initial-value constants and assertion constants are independent. Static assertions in `context.h` pin every slot offset. The target validator inspects the generated ELF disassembly for the immediate values, stores, architectural loads, comparisons, switch saves, and switch restores.

The positive target contract confirms that Context A and B each load the matching values into `b1`–`b5`, and that switch save/restore uses the five matching offsets. `b0` remains separate as the return link at `C_RP` offset 64. The switch algorithm was not changed to mask an initialization error.

## Branch-register ABI review

Intel's *IA-64 Software Conventions and Runtime Architecture Guide*, §5.5, Table 5-4, classifies `b0` as a scratch return-link register, `b1`–`b5` as preserved, and `b6`–`b7` as scratch. It also says `b0` contains the return address on procedure entry and is otherwise scratch. See the [Intel guide](https://portal.cs.umbc.edu/help/architecture/24525601.pdf).

The ordinary-call convention explains why callers must preserve `b1`–`b5`; it does not by itself prove that this cooperative context switch preserves them while suspending one context and resuming another. The implementation explicitly saves and restores `b1`–`b5`, while saving/restoring `b0` separately as the continuation. IA64-5AR10's runtime passed Context B's first-entry `b1`–`b5` checks, but it did not reach a switch back to A. Runtime preservation across a cooperative transfer is therefore still unproven.

## Candidate and static qualification

The fresh candidate is under `/home/guidex/src/ia64-5ar10-coop-probe-20261117-r1-static/`.

| Artifact | SHA-256 |
|---|---|
| Probe ELF | `98c7bbf8604fa1443e1b9af9200592aa4d4a21f214b384b667ebbb494b05bb4c` |
| Manifest | `d43458f66978df73c910a3da28db900daf28467e53bb2f9c327e6fcd1da8dbcd` |
| Pinned Ski 1.5.1 `bskinc` | `ff6b19cb87dbf2db39cda4478d0e49995c9a5ec9ddd05aac83191f4ba1e40b45` |

The manifest binds the ELF hash. `run-ia64-ski.py cooperative-context-probe --static-only` passed on this exact artifact and inspected its generated disassembly. GNU `readelf --lint -W -h -l -S -s` passed. The static contracts also verified the corrected `out0/out1` to `in0/in1` context-pointer mapping, setup `b0`/PFS preservation across its nested call, Context B's BSP assertion, 12-register/96-byte frame span, eager-mode legal `ar.bsp` access, no diagnostic `ar.bspstore` read, separate memory stacks and RSE backing stores, first-entry bootstrap, saved continuations, and the declared FP/ABI policy.

| Check | Result |
|---|---|
| Full IA-64 host contract suite under WSL | PASS, 176 tests |
| Branch-register contract suite | PASS, 9 tests, including the exact BR1/BR2 slot swaps, duplicate sentinels, wrong offsets, source/target mismatch, assertion drift, Context A value in B, and corrupted restore fixtures |
| Context layout, argument mapping, setup return-link, BSP/frame, RSE/RNAT, and diagnostic contracts | PASS within the full suite |
| Probe static ELF/manifest and target-disassembly validation | PASS |
| GNU `readelf --lint -W -h -l -S -s` | PASS |
| Debugger-plan preflight | PASS: six exact breakpoint targets, 471 finite step commands, 240,193 maximum guest instructions |
| Pinned Ski no-guest parser/capacity smoke | PASS: no ELF loaded, zero guest instructions, six of ten slots used, four free at peak |
| FP code-generation regression | PASS with GCC 13.5.0 and Binutils 2.41; report `/home/guidex/src/ia64-5ar10-fp-codegen-20261117-r1/report.md` |
| Direct Mbed TLS profile verifier | PASS: Mbed TLS 4.1.0, TF-PSA-Crypto 1.1.0, three patches |
| Python/Bash syntax and `git diff --check` | PASS |

The six Ski IP breakpoints were B bootstrap `0x10b7c0`, B branch checks passed `0x10d0a0`, first B→A switch call `0x10d6b0`, first A resume `0x10bbe0`, second B resume `0x10d6c0`, and assertion failure loop `0x10df60`. Ski's observed capacity was ten, while the plan's conservative simultaneous limit was six. The final-return evidence remains the probe's validated transition log and intentional SSC exit; the failure loop occupied the sixth breakpoint to capture assertions that can fail while Ski reports zero architectural faults.

## Single authorized runtime result

Exactly one cooperative guest execution was launched. The authorization record is under `/home/guidex/src/ia64-5ar10-cooperative-qualification-20261117-r1-run-01/authorization-consumed.json` and records one launch, `cpu_count=1`, `kernel_execution=false`, direct `-forcesystem` loading, PTY-backed stdin, a 2 GiB address-space cap, 20/22-second CPU limits, a 30-second wall limit, and the 240,193-instruction cap. The exact ELF, manifest, Ski executable, disassembly, and six-breakpoint plan hashes are bound there. No retry was made.

The full capture is preserved under `/home/guidex/src/ia64-5ar10-cooperative-qualification-20261117-r1-run-01/`, including the consumed authorization, debugger command file, candidate disassembly, `analysis.json`, all register/context/event snapshots, simulator stdout/stderr, instruction statistics, and run result. Key capture hashes:

| Capture | SHA-256 |
|---|---|
| Authorization | `a2df88ba11070ca5ead455bdb5c127b4f027af1866ef0531d716e7c9114b6ced` |
| Debugger command file | `257cc7164d34ad49703df9d0b13b0760bb0eca84b5828a6c500e14a409438988` |
| Candidate disassembly | `366454dc5d0646af82e15551551743791a1decd147e034f8c3672a405a5ef1d1` |
| Analysis | `3389751645690ff081bddc9413881fcd65fd7c2c071de7d5c99f899510ac990d` |
| Simulator stderr | `d4fe2eaf22e8d871fcbbdacf2aa85ecb28e4bef4990bb8058f4a8270b07783ba` |
| Instruction statistics | `3a254c931d2f2a2438be64032064087c228833e4b37e1ec3be49a1c5381c98cf` |
| Run result | `a24c76ce482f969e049af15d3de0c644437a6a50fc2b03d9d3169a9d98826da4` |

### Furthest verified frontier and first remaining failure

| Frontier | Runtime evidence |
|---|---|
| A→B | Reached B bootstrap. The event record contains one `A_TO_B`; Context A identity is `0xA`, switch count 1, and its saved continuation is `0x10bbe0`. |
| Context B bootstrap | Reached once at aggregate `$insts$=136170`. Context B identity is `0xB`, first-entry count 1, and its stack pointer `0x133210` lies in B's `[0x124210,0x134210)` stack. |
| BSP/frame check | Passed at the branch-check frontier: `BSP=0x144a00`, `RSC.mode=3`; this is B's RBS base. Static layout keeps the 96-byte frame within B's `[0x144a00,0x154a00)` RBS. |
| B branch-register check | Passed at `0x10d0a0`, aggregate `$insts$=136332`. Live values were `b1=0x4252314242423032`, `b2=0x4252324242423032`, `b3=0x4252334242423032`, `b4=0x4252344242423032`, and `b5=0x4252354242423032`. The Context B initial memory slots contained the same five distinct values. Context A's saved `b1`–`b5` slots also matched its five A sentinels. |
| First remaining assertion | Context B entered `ia64_coop_test_fail` at `0x10df60`, aggregate `$insts$=136365`. The preserved operands are `r14=0` and `r15=0x62`. The target disassembly reads `ar.ec` into `r14` at instruction address `0x10d126`, loads expected `EC_B=0x62` into `r15` at `0x10d136`, compares at `0x10d140`, and branches to fail at `0x10d15c`. This is the Context B application-state EC assertion, after all five B branch-register assertions passed. |
| Later transfers and completion | The first B→A switch call, A's first resume, B's second resume, final A return, and intentional SSC exit were not observed. Output validation rejected the missing transition log. |

The source and disassembly show that Context B's setup record contains `EC_B`, and the switch restore has a separate `C_EC` load. However, B's first-entry check runs after its prologue and no explicit post-prologue `mov ar.ec = EC_B` appears in `ia64_coop_b_test`; Context A explicitly establishes its application registers after its own prologue. This points to a Context B first-entry EC setup mismatch. The captured failure proves the observed value and check, but does not by itself prove which intervening instruction made `ar.ec` zero. The assertion was not changed or bypassed after the run.

### State preservation limits

| State area | Evidence and limit |
|---|---|
| Branch registers | Context B's initialized/live `b1`–`b5` and Context A's saved initial slots matched. No outgoing B save or resumed A/B branch-register state was observed. |
| Return link / PFS | Static target inspection confirms distinct `b0` save/restore through `C_RP@64`; Context A's saved continuation is `0x10bbe0`. No B→A return-link restoration was executed. |
| Memory stack / GP / ABI | B's live SP was within its own stack. The `GR4`–`GR7` and `b1`–`b5` checks precede the branch-pass label and passed. Later B GP/PFS and stack-use checks were not reached. A did not resume. |
| BSP/RSC/RSE | B's first-entry BSP/RSC check passed. A's saved context records `BSP=BSPSTORE=0x134680`, `RSC=3`; B's initial context records `BSP=BSPSTORE=0x144a00`, `RSC=3`. No B→A RBS handoff or restored RSE state was exercised. |
| RNAT / NaT | The A snapshot records `RNAT=0x80`, defined bits `0x80`, valid mask `0x1ffff`; B's initial context records zero RNAT. These are saved/initial context records, not a round-trip preservation test. B's GR8 NaT checks occur after the failed EC check and were not reached. |
| FP | The first B FP capture follows the failed application-state check. No runtime FP preservation was established. The FP code-generation harness is static evidence only. |
| Faults / termination | Ski reported zero architectural faults and no exception. The process exited 0 after the finite debugger command stream, classified `SIMULATOR_EXIT_UNCLASSIFIED`; no intentional guest SSC exit was observed. Timeout was false, cleanup and process-group cleanup passed, and PTY verification passed. |

The final cumulative debugger `$insts$` sample was 239,277, below the 240,193 plan maximum. The failure frontier occurred at 136,365. The run lasted 22.552 seconds. The runner returned qualification status 1 because the probe did not complete and did not produce its validated transition/result output; it did not report a Ski fault or timeout.

## Acceptance and next milestone

This phase is **Outcome B**, `IA64_COOPERATIVE_FRONTIER_ADVANCED`: the original B branch-register fixture defect is repaired and its first-entry checks pass, but the probe's next assertion fails on Context B `ar.ec` before the first B→A transfer. This does not qualify switch preservation, NaT/RNAT restoration, FP restoration, or the complete two-context sequence.

The next milestone should resolve the B first-entry EC state contract with a separate static-only candidate: inspect the B prologue and restore ordering, preserve the independent EC check, validate source and target disassembly, and rerun the complete host/static checks. Any later guest execution requires a new phase authorization. Kernel integration remains unqualified.
