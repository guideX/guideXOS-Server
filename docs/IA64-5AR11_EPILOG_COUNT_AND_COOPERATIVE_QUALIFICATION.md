# IA64-5AR11 Epilog Count Register and Cooperative Switch Qualification

**Outcome B — `IA64_COOPERATIVE_FRONTIER_ADVANCED` (2026-11-18).** The invalid Context B EC expectation and missing post-prologue initialization were corrected. The one authorized Ski run verified B's legal EC initialization and first B→A transfer, then resumed A and failed at A's low stack-use sentinel. The failure is a probe stack-fixture defect. The full A→B→A→B→A contract remains unqualified.

## Scope and preserved state

- Repository: `D:\dev\guideXOSServer_IA64`, branch `IA64_SUPPORT`, starting HEAD `e1e646513b7a8f13564def9c40bb0cea0041b6a4`, tracking `origin/IA64_SUPPORT` and four commits ahead at phase start.
- Exactly one standalone cooperative probe was executed. No guideXOS kernel was built or run, and there was no guest retry.
- No reset, clean, pull, rebase, merge, or push was performed. Historical candidates, manifests, captures, and reports were preserved. Pre-existing untracked Python caches remain untouched.
- The accepted IA64-4G kernel artifact remains `ab13fabb6a92ebe039afa2afd9729f6f6ceb2529b63be2b47b0c87cf264d960c`.
- The IA64-5AR10 executed candidate and original capture were not modified. Its report remains at [IA64-5AR10](IA64-5AR10_BRANCH_REGISTER_AND_COOPERATIVE_QUALIFICATION.md).

## Architectural EC and PFS contract

Intel identifies `ar.ec` as application register 66. EC is six bits wide, so its valid values are `0x00` through `0x3f`. The manual labels bits 63:6 ignored and defines EC as the count used by modulo-scheduled loop epilogs. This phase rejects any test expectation with bits outside the six-bit field. It does not claim that Ski was tested by writing `0x62`: neither 5AR10 nor this phase writes that invalid value, so the simulator's readback for such a write is unknown. The 5AR10 probe used `0x62` as an expected comparison value, not as an EC write. See Intel's [Itanium Software Developer's Manual, Volume 1](https://www.intel.com/content/dam/www/public/us/en/documents/manuals/itanium-architecture-software-developer-rev-2-3-vol-1-manual.pdf).

EC is live loop state. `br.ctop` and `br.cexit` use and may decrement EC in modulo-scheduled counted loops; `br.wtop` and `br.wexit` use and may decrement it in modulo-scheduled while loops. `br.cloop` decrements LC instead. Calls and returns also affect the EC/PFS path: `br.call` copies EC into `PFS.pec`, and `br.ret` restores EC from `PFS.pec`. `PFS.pec` occupies bits 57:52 and has the same six-bit layout as EC. `alloc` saves the incoming PFS into a general register; a non-leaf routine must preserve and restore its caller's PFS before returning. The [IA-64 Software Conventions and Runtime Architecture Guide](https://portal.cs.umbc.edu/help/architecture/24535801.pdf) also states that the epilog counter is automatically preserved with `ar.pfs`. These rules are specified in Intel's [Itanium Software Developer's Manual, Volume 3](https://www.intel.com/content/dam/www/public/us/en/documents/manuals/itanium-architecture-vol-3-manual.pdf) and [Volume 1 PFS description](https://www.intel.com/content/dam/www/public/us/en/documents/manuals/itanium-architecture-software-developer-rev-2-3-vol-1-manual.pdf).

The cooperative test now keeps bootstrap and application EC checks separate:

| Point | Required EC/PFS state | Purpose |
|---|---|---|
| Context B bootstrap, before its test procedure | `EC=0`, `PFS=0` | Retains the first-entry bootstrap contract. |
| B test after `alloc` | Explicit write of `EC_B=0x2a` | Establishes a legal, nonzero B application sentinel after its prologue. |
| B's first application-state assertion | `EC=0x2a`, `LC=0x42` | Tests initialized B state independently of bootstrap state. |
| B→A outgoing call | Saved `PFS.pec=0x2a` and saved EC `0x2a` | Tests that the outgoing call captures B's live state. |
| A's first resume | `EC=0x15`, saved PFS match and `PFS.pec=0x15` | Tests A's state after the first cooperative transfer. |
| B's later resume | `EC=0x2a` and saved PFS match | Still required; the guest run did not reach this frontier. |

The probe and switch assembly contain no loop-control branch in the A/B switch path where these values are established or checked. The source validator rejects insertion of a counted-loop branch into that path. Compiler-generated C++ reporting helpers may contain their own loop branches outside the assembly switch frontier; they do not provide evidence for the switch-state checks. EC writes and dependent reads are separated by instruction-group stops (`;;`). The switch saves both `C_EC` and `C_PFS`, restores PFS before `br.ret`, and has a static contract requiring the final return to restore the selected PFS. Explicit context EC state and the return's PFS restoration are checked separately.

## IA64-5AR10 failure attribution

The preserved 5AR10 report and target disassembly establish:

1. `EC_B` was defined as `0x62`, outside the six-bit EC field.
2. The initial B context had `C_EC=0` and `C_PFS=0`, as required by its bootstrap entry.
3. B's first-entry test executed its procedure prologue but did not write `EC_B` after that prologue.
4. At `0x10D126`, the probe read `ar.ec` into `r14`; it loaded the comparison value `0x62`, compared, and branched to the failure loop at `0x10D15C`.
5. The observed zero therefore matched the initialized bootstrap state. It did not prove that the context switch discarded a nonzero B EC value. Nor did the saved-state evidence show PFS restoration of a nonzero value.

The 5AR10 defect was both an illegal expected sentinel and missing B application-state initialization. The repair leaves bootstrap EC/PFS at zero, writes legal `EC_B=0x2a` only after B's `alloc`, then separately validates the initialized value and later saved PFS. The RSE switch routine was not changed to mask or hide the original expectation.

## Focused special-register and sentinel audit

- **EC and PFS:** `EC_A=0x15` and `EC_B=0x2a` are distinct, nonzero six-bit values. `scripts/run-ia64-ski.py` independently defines the architectural EC width and validates legal range, distinct A/B values, expected constants, bootstrap zero, post-prologue B initialization, PFS save/restore order, and the PFS.pec/EC relationship. Negative tests reject `EC_B=0x62`, an in-range but wrong `0x22`, a bad PFS.pec, and a loop-control instruction in the test path.
- **LC:** LC is 64 bits. A uses `0x31`; B uses `0x42`. No counted-loop instruction executes in the assembly switch path, so these sentinels are not consumed there. The parser fix below also prevents decimal Ski output from being misread as hexadecimal.
- **Predicates and rotating state:** the test checks only its required predicate bits and has no rotating-register loop branch in the measured switch path. The initialized A/B predicate values are separately validated.
- **GR/BR/GP and PFS:** GR4–GR7 and BR1–BR5 use distinct context sentinels. Context layout assertions keep `C_RP`, `C_PFS`, `C_EC`, and branch-register slots at their documented offsets. B's initial BR1–BR5 and BSP/frame checks passed; A's preserved GR4–GR7/BR1–BR5 checks passed after its first resume.
- **RSC/BSP/RNAT/NaT:** RSC is explicitly checked in eager mode (`RSC.mode=3`); the 12-register frame is 96 bytes and its BSP is bounds-checked against each backing store. RNAT validation derives a mask from the BSP collection position and compares only defined bits. The switch saves caller UNAT separately from spill-slot UNAT. The test's stacked NaT checks remain in place.
- **CCV/FPSR/FP:** A and B use distinct CCV values. FPSR is checked against the test image. The declared FP contract covers the selected preserved register set and its static code-generation policy. The runtime reached A's first-resume FP capture, but final FP-image validation was not reached, so this run does not qualify FP preservation.

No additional invalid special-register sentinel was found in the focused code path. The A stack-use marker failure below is ordinary test memory, not EC/PFS or switch-state corruption.

## Candidate, build, and static qualification

The guest-executed r3 candidate and its manifest are preserved. The correction discovered after the run produced r4, a separately hashed static-only candidate with A's stack-use sentinels seeded inside the stack.

| Artifact | Location | SHA-256 | Qualification |
|---|---|---|---|
| Executed r3 ELF | `out/ia64-5ar11-coop-probe-20261118-r3-static/ia64-cooperative-context-probe.elf` | `d827b017a3af2924dced50891cbe153c1f0fe9e43f7e6354f9848a2aab907734` | One standalone Ski run |
| Executed r3 manifest | `out/ia64-5ar11-coop-probe-20261118-r3-static/ia64-cooperative-context-probe.manifest.json` | `e7cee9bc70d804b727cbc425df79d0cdeeb57d8a21510b190271b0ed1515d134` | Bound to r3 ELF |
| Corrected r4 ELF | `out/ia64-5ar11-coop-probe-20261118-r4-static/ia64-cooperative-context-probe.elf` | `17b2afa102d50bd6aa8e907333b3b0c7aacc42c889368b4767d06a72dc7825c9` | Static-only; no guest execution |
| Corrected r4 manifest | `out/ia64-5ar11-coop-probe-20261118-r4-static/ia64-cooperative-context-probe.manifest.json` | `32e53c2d066a9f62afe1687631f3f31c125ba8ef90b8c9918d5b236e0d54c923` | Bound to r4 ELF |
| Pinned Ski 1.5.1 `bskinc` | Host tool | `ff6b19cb87dbf2db39cda4478d0e49995c9a5ec9ddd05aac83191f4ba1e40b45` | Executed r3 only |

The r4 static-only qualification validates the legal EC contract, source initialization and stack-seed structure, context offsets, BSP/frame expectations, RSE/RNAT rules, saved continuations, branch-register mapping, FP/ABI policy, manifest identity, and target disassembly. GNU `readelf --lint -W -h -l -S -s` passed. The executed-r3 debugger preflight resolved the six unique targets below, within Ski's ten slots, and 471 finite step commands capped at 240,193 instructions. The no-guest parser and breakpoint-capacity smoke loaded no ELF and executed zero guest instructions.

| Breakpoint frontier | r3 address |
|---|---:|
| Context B first-entry bootstrap | `0x10c6a0` |
| Context B nonzero EC initialization | `0x10de20` |
| Context B EC assertion passed | `0x10e370` |
| First B→A switch call | `0x10e7e0` |
| Context A first resumption | `0x10cb50` |
| Context B second resumption | `0x10e7f0` |

The executed r3 capture is preserved at `out/ia64-5ar11-cooperative-qualification-20261118-r3-run-01/`. Its authorization record binds one launch, one simulated CPU, direct `-forcesystem` loading, PTY-backed stdin, a 2 GiB address-space limit, 20/22-second CPU limits, a 30-second wall limit, six simultaneous breakpoints, and the bounded instruction plan. No retry was made.

## Single guest run and parser correction

The run completed in 17.481 seconds. Ski exited with code 0 after the finite debugger command stream, reported zero architectural faults, no exception, no timeout, and complete process-group cleanup. This was not the probe's intentional SSC exit. The runner correctly returned qualification failure for this incomplete trace. The first failed assertion was captured before the failure loop modified its operands.

Ski's textual debugger `eval` output printed bare LC and EC values in decimal (for example, `lc = 66`, `ec = 42`); the fixed register snapshots use hexadecimal. The first saved `analysis.json` was generated by a parser that interpreted bare `lc` and `ec` evaluation values as hexadecimal; it consequently reported B EC as 66 and the run as Outcome C. The immutable capture was left unchanged. `parse_eval_register_samples` now parses bare LC/EC evaluation values as decimal unless explicitly prefixed, with regression coverage. Replaying the saved stderr offline and checking the raw register snapshots gives:

| Capture frontier | Instruction count | Raw values and interpretation |
|---|---:|---|
| B bootstrap | 136,175 | `EC=0`, `PFS=0`, `LC=66` decimal = `0x42` |
| B post-prologue EC write | 136,239 | `EC=42` decimal = `0x2a`; B's legal value was written |
| B EC assertion passed | 136,470 | `EC=0x2a`, `LC=0x42`; the assertion passed |
| First B→A call | 136,671 | `EC=0x2a`, `PFS.pec=0x2a`; outgoing B state was captured |
| First A resumption | 137,497 | `EC=0x15`, `LC=0x31`, `PFS.pec=0x15`; saved A state was restored and its PFS equality check passed |
| A low stack-use assertion failed | 138,009 | Actual `r14=0x535441434b484947` (`STACKHIG`), expected `r15=0x415f535441434b30` (`A_STACK0`); active context A |

The event record contains, in order, `A_TO_B`, `B_FIRST_ENTRY`, `B_TO_A_FIRST`, and `A_RESUMED_FIRST`. B's initial BSP/frame and all five initial branch-register checks passed. B then reached B→A. On A's first resume, execution passed the PFS, stacked-NaT, GP, GR4–GR7, BR1–BR5, predicate, LC, EC, CCV, FPSR, and RSC checks before reaching its stack-low marker load. This proves the first A EC/PFS restoration, but not a second switch or B's resumed state.

For the stack failure, the failure branch is the conditional branch at `0x10d2fc` in the captured r3 disassembly. It follows the low-marker load at `0x10d2c0`, expected-marker load at `0x10d2d6`, and comparison at `0x10d2e0`. The failure-loop progress checkpoint is separately reported as IIP `0x10f240`, slot 1 at instruction count 138,009; that is a later sample inside the failure loop, not the assertion branch address. The last cumulative debugger progress sample reached 238,873 instructions, below the 240,193 budget. The simulator's `ski_instruction_count=512` field is the last debugger step quantum, not the cumulative count.

The captured r3 A context entered its test with SP at the stack's high guard. The old test then checked `[SP]` for `A_STACK0`; it had not reserved the 4096-byte A stack-use area and seeded the A use markers there. The value read was the high-guard canary `STACKHIG`. The post-run r4 source now moves SP down 4096 bytes and writes the low/high A stack-use markers before testing them, matching the validator's expected in-stack geometry. This fixture fix is static-only pending a future guest authorization.

## Runtime evidence and limits

| Area | Established in the one run | Not established |
|---|---|---|
| EC bootstrap and initialization | B bootstrap began at EC/PFS zero; legal `EC_B=0x2a` was written after B's prologue and its first application-state assertion passed. | Ski behavior for a write of out-of-range `0x62`; it was not written. |
| PFS and call/return | The first B→A call captured `PFS.pec=0x2a`; A resumed with `EC=0x15`, `PFS.pec=0x15`, and a matching saved PFS. | Restoring B from its saved PFS on a later B resumption. |
| Cooperative frontier | A→B→A reached the first A resumption; four ordered events were recorded. | The second A→B, B resume, second B→A, final A resume, and intentional SSC exit. |
| RSE/NaT | B's BSP/frame checks passed; A's first-resume stacked-NaT check passed. The captured state used legal RSC mode and defined RNAT masks. | Full two-context RSE/RNAT/NaT round-trip and independence checks. |
| ABI/general registers | B's first-entry branch checks and A's first-resume GR4–GR7/BR1–BR5 checks passed. | Complete resumed B state and final return-link contract. |
| FP | The selected preservation set was built and captured at A's first resumption. | Final image comparison and runtime FP acceptance. |
| Faults and cleanup | Zero Ski faults, no unexpected exception, no timeout, clean process-group cleanup. | Intentional guest exit; the run stopped in the failure loop when the finite debugger plan ended. |

## Verification results

| Check | Result |
|---|---|
| Complete IA-64 host contract suite | PASS, 188 tests; 8 skipped |
| Focused cooperative context-switch contract suite | PASS, 43 tests; includes invalid `0x62`, wrong in-range value, PFS.pec, initialization order, loop-branch, stack fixture, branch mapping, BSP/frame, return-link, RSE/RNAT, and ABI negative cases |
| Cooperative debugger parser/frontier contract suite | PASS within the complete suite; includes decimal LC/EC parsing and transition/frontier attribution |
| r4 static ELF/manifest and target-disassembly validation | PASS; static-only |
| GNU `readelf --lint -W -h -l -S -s` | PASS |
| Six-breakpoint debugger-plan preflight and bounded instruction budget | PASS |
| No-guest Ski parser/capacity smoke | PASS; zero guest instructions, six of ten breakpoint slots |
| FP code-generation harness | PASS with GCC 13.5.0 and Binutils 2.41; report `/tmp/guidexos-ia64-fp-repro.6QBfYc/report.md` in WSL |
| Direct Mbed TLS profile verifier | PASS |
| Targeted Python/Bash syntax checks and `git diff --check` | PASS |

## Outcome and next milestone

This is **Outcome B**, `IA64_COOPERATIVE_FRONTIER_ADVANCED`. The EC issue was corrected and tested with architecturally legal, distinct values. B reached its first B→A transfer, and A resumed with its saved EC/PFS state. The next failing assertion is an A stack-use fixture error, not proof of context-switch corruption. The stack fixture fix is present in r4, which has a distinct hash and static-only qualification.

The next milestone is to obtain a separately authorized single Ski run of r4 and verify the complete A→B→A→B→A sequence, intentional SSC exit, both contexts' PFS.pec/EC on resume, stack/RBS independence, valid RNAT bits, stacked NaT state, ABI registers, and the declared FP contract. Kernel integration remains a separate and unqualified milestone.
