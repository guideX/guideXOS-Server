# IA64-5AR8 Context B Recovery and Cooperative Qualification

**Outcome C — `IA64_COOPERATIVE_REQUALIFICATION_BLOCKED` (2026-11-15).** One authorized guest run reached the Context B first-entry bootstrap, then the probe's own BSP assertion entered its failure loop before the bounds-valid frontier. The read of `ar.bsp` was legal; the test incorrectly expected `alloc` to advance BSP by the 96-byte frame size. The single-run allowance is consumed. A corrected r2 candidate passed static-only qualification and was not executed.

## Scope and preserved baseline

- Repository: `D:\dev\guideXOSServer_IA64`; branch `IA64_SUPPORT`.
- Starting HEAD: `e48344248ba8d869b1375efda71720fe9d0cbfce`, tracking `origin/IA64_SUPPORT` and one commit ahead at start.
- The accepted IA64-4G kernel identity remains `ab13fabb6a92ebe039afa2afd9729f6f6ceb2529b63be2b47b0c87cf264d960c`.
- No kernel build or kernel execution occurred. No push, reset, clean, pull, rebase, or merge occurred. Historical probe files and the pre-existing untracked Python caches were preserved.

The repair retains IA64-5AR5's setup `b0`/PFS preservation and the existing four ABI-correct switch call sites. The switch implementation itself was not changed. The runtime did not establish the complete A→B→A→B→A sequence or preservation contract.

## IA64-5AR7 fault and r1 candidate

IA64-5AR7 attributed the Context B fault to `mov r14=ar.bspstore` at IIP `0x10c150`, slot 0. Ski reported General Exception vector `0x5400`, `ISR.code=0` (Illegal Operation), with `RSC.mode=3`. `BSPSTORE`, `RNAT`, and `loadrs` accesses are restricted to RSC mode 0; reading `BSP` is permitted in any RSE mode. The eager-mode BSPSTORE instruction was replaced with a BSP read.

The first 5AR8 candidate was:

| Artifact | Path | SHA-256 |
|---|---|---|
| Probe ELF (r1, executed once) | `/home/guidex/src/ia64-5ar8-coop-probe-20261115-r1/ia64-cooperative-context-probe.elf` | `1a98fe0fa0f1c06ec8a0ada1cdeabd7dbf6fd35fc0d92c19c79fd10d018dc399` |
| Matching manifest | `/home/guidex/src/ia64-5ar8-coop-probe-20261115-r1/ia64-cooperative-context-probe.manifest.json` | `dfc2c77ee35e51c1f375d50d4eff79f8b40b5922a185d53d7e2b49252ad96f50` |

The linked check used `mov r14=ar.bsp`, checked the Context B RBS bounds, and then compared BSP with `RBS base + 96`. The Intel manual defines BSP as the backing-store address where the current frame's GR32 would be written. Its RSE register-allocation table says `alloc` leaves BSP unchanged. The 12-register frame occupies 96 bytes beginning at that BSP address; the frame size is not a BSP increment. Since the B bootstrap has no allocated frame and the B test's `alloc r16=ar.pfs,6,4,2,0` allocates 12 stacked registers, the expected BSP at this check is the aligned B RBS base, with the 96-byte frame span fitting inside the allocation. The B RBS base is 512-byte aligned, and the span does not cross an RNAT collection slot. See the [Intel Itanium Architecture Software Developer's Manual, Volume 2](https://www.intel.com/content/dam/www/public/us/en/documents/manuals/itanium-architecture-software-developer-rev-2-3-vol-2-manual.pdf), RSE §§6.1, 6.5.3, 6.5.4 and Table 6-1.

The old assertion failed before `ia64_coop_b_bsp_bounds_valid`. The captured live BSP at the failure loop was `0x144a00`, equal to the B RBS base. The B stack pointer at the loop was within the statically validated B stack bounds, and BSP equaled the inclusive lower RBS bound and remained below its limit. The exact `base + 96` comparison was therefore the rejecting check. This is an incorrect probe expectation, not evidence that the cooperative switch algorithm is defective.

## Single authorized runtime result

The one guest execution used pinned Ski 1.5.1 (`/home/guidex/opt/ski-dfc2902/bin/bskinc`, SHA-256 `ff6b19cb87dbf2db39cda4478d0e49995c9a5ec9ddd05aac83191f4ba1e40b45`) in `-forcesystem` mode, one simulated CPU, a PTY-backed console, a 2 GiB address-space limit, 20/22-second CPU limits, and a 30-second wall limit. Its command plan allowed at most 240,193 guest instructions and six simultaneous breakpoints.

The six r1 debugger frontiers, resolved from the linked ELF, were:

| Frontier | Ski IP |
|---|---:|
| Context B first-entry bootstrap | `0x10b7c0` |
| Context B BSP bounds passed | `0x10cea0` |
| Switch `flushrs` | `0x10ecb4` |
| Context A first resumption | `0x10bbe0` |
| Context B second resumption | `0x10d6c0` |
| Context A final resumption | `0x10c490` |

The installed list contained six program breakpoints in slots 0–5; the no-guest capacity smoke also verified slot reuse, deletion, and rejection of an invalid IA-64 slot. Raw register, context, and event captures were saved incrementally.

| Runtime evidence | Result |
|---|---|
| A→B transition | One `A_TO_B` event, sequence 1, context A (`0xA`) |
| B first-entry bootstrap | Reached once at `0x10b7c0`, aggregate `$insts$=136170`; context B identity is `0xB` |
| BSP bounds-valid frontier | 0 hits; the probe had already entered its failure loop |
| Later flushrs / A resume / B resume / final A resume | No later transition; one flushrs hit was the initial A→B save |
| Probe error counter | 0 at the B bootstrap sample; 1 at the next checkpoint in `ia64_coop_test_fail` |
| First unexpected exception | None; Ski reported 0 faults and the raw sample did not enter the IVT |
| Instruction progress | 472 register snapshots; last aggregate `$insts$=239594` of 240,193 allowed |
| Simulator | Exit code 0, no timeout, cleanup complete; classification `SIMULATOR_EXIT_UNCLASSIFIED` |
| Intentional SSC result | Not reached; stdout was empty and SSC 66 was not observed |
| Guest/kernel executions | 1 standalone probe / 0 kernel |

The recorded debugger values for BSPSTORE and RNAT are debugger introspection, not guest instructions and not backing-store preservation evidence. This run does not qualify stored RSE data, RNAT contents, NaT preservation, FP preservation, or either context's resumption. No retry was made.

The complete capture is preserved at `/home/guidex/src/ia64-5ar8-cooperative-qualification-20261115-r1/`. Its `analysis.json` reports the furthest proven frontier as Context B first-entry bootstrap and records the internal fail-loop snapshots. The simulator reached the finite step plan without a fault; its normal `quit` command ended the debugger session rather than the probe's intentional SSC exit.

## Corrected r2 static-only candidate

The source and static validator now distinguish the current-frame BSP address from frame size. Context B compares BSP with `ia64_coop_rbs_b_base`; separately, the source/validator prove the `6,4,2,0` allocation spans 96 bytes and fits in the RBS without crossing a NaT collection slot. No guest context-switch or setup code was modified.

| Artifact | Path | SHA-256 |
|---|---|---|
| Probe ELF (r2, static-only) | `/home/guidex/src/ia64-5ar8-coop-probe-20261115-r2-static/ia64-cooperative-context-probe.elf` | `dec92c95c500bd70b058540fcbf44e490629c31c451906f8acba9af86fc469f3` |
| Matching manifest | `/home/guidex/src/ia64-5ar8-coop-probe-20261115-r2-static/ia64-cooperative-context-probe.manifest.json` | `4addffab0102076d74ae23704bcea46f87517e18abf45f50cbdffeba666254b1` |

R2 passed `run-ia64-ski.py cooperative-context-probe --static-only`: the target disassembly has the mode-independent BSP read, both RBS bounds, the exact current-frame-start comparison, and the expected `0x144a00` BSP. Its 96-byte frame span stays within `[0x144a00,0x154a00)`. GNU `readelf --lint -W -h -l -S -s` exited 0. The r2 debugger preflight passed with six exact frontiers and the same finite 240,193-instruction plan. A separate no-guest Ski parser/capacity smoke passed with `guest_elf_loaded=false` and `guest_instructions_executed=0`. R2 was not executed.

## Preflight and regression results

| Check | Result |
|---|---|
| Full IA-64 contract suite after the semantic correction | PASS: 159 tests across 13 IA-64 test modules |
| Focused setup/ABI/RSE/NaT/FP/debugger contracts | Included in the 159 passing tests |
| FP code-generation regression | PASS with pinned GCC 13.5.0 and Binutils inspection tools; report `/home/guidex/src/ia64-5ar8-fp-codegen-20261115-r1/report.md` |
| Direct Mbed TLS profile verifier | PASS: Mbed TLS 4.1.0, TF-PSA-Crypto 1.1.0, three tracked patches |
| Python/shell syntax | PASS: four changed Python files parsed; probe build shell passed `bash -n` |
| ELF and manifest identities | PASS: hashes match each artifact's manifest and pinned runner arguments |
| Debugger preflight / no-guest parser and breakpoint smoke | PASS for the executed r1 plan and corrected r2 plan; no guest loaded in these smokes |
| Kernel build / execution | NOT RUN / NOT RUN |

## Acceptance and next milestone

This is Outcome C, `IA64_COOPERATIVE_REQUALIFICATION_BLOCKED`. R1 exposed and captured the faulty probe assertion after Context B's first-entry frontier. It did not pass the corrected BSP check, so Outcome B is not claimed. R2 corrects the architecture-based assertion and is statically qualified only. The original cooperative context-switch acceptance contract remains open.

The next milestone is a separately authorized, single bounded runtime qualification of the exact r2 ELF and manifest. It should first confirm B's `ar.bsp == 0x144a00` at the bounds-valid frontier, then gather the later B→A→B→A continuation, ABI/GP/PFS, NaT, FP, stack, RSE handoff, fault-count, and intentional-SSC evidence. Preserve the existing one-guest-per-phase limit and do not promote r2 based on static results alone.
