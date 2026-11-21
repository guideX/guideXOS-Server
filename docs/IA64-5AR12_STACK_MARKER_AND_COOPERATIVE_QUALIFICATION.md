# IA64-5AR12 Stack Marker and Cooperative Qualification

**Outcome C — IA64_COOPERATIVE_REQUALIFICATION_BLOCKED (2026-11-19).** The immutable IA64-5AR11 r4 candidate failed the mandatory preflight because its A stack-marker assertions compared against the same source constants used to initialize those markers. The assertions therefore did not independently specify the expected bytes. Per the qualification boundary, r4 was not run. A separately hashed static-only r5 r2 candidate now has independent expectations and checks A's seeded markers before suspension and after both resumptions. It passed static qualification, but this phase did not authorize or perform a guest execution.

## Scope and preserved state

- Repository: D:\dev\guideXOSServer_IA64, branch IA64_SUPPORT, starting HEAD 9c0d3227afedc95ca1c951441c3a84e8c7687add; origin/IA64_SUPPORT is 9be0a05ecbbf00057ca17d0bdcedc065980fb82d. The branch was five commits ahead at phase start.
- No guideXOS kernel was built or run. No r4 or r5 guest ELF was loaded or executed. This phase's cooperative guest execution count is zero; the single historical standalone guest run remains the IA64-5AR11 r3 run.
- No reset, clean, pull, merge, rebase, or push was performed. Historical candidate artifacts, manifests, captures, and untracked Python caches were preserved.
- The accepted IA64-4G kernel remains unchanged at SHA-256 ab13fabb6a92ebe039afa2afd9729f6f6ceb2529b63be2b47b0c87cf264d960c.
- The pinned Ski 1.5.1 bskinc executable remains SHA-256 ff6b19cb87dbf2db39cda4478d0e49995c9a5ec9ddd05aac83191f4ba1e40b45.

## Immutable r4 preflight finding

The 5AR11 r4 ELF and manifest hashes still match the earlier report:

| Artifact | SHA-256 | Phase disposition |
|---|---|---|
| r4 ELF, out/ia64-5ar11-coop-probe-20261118-r4-static/ia64-cooperative-context-probe.elf | 17b2afa102d50bd6aa8e907333b3b0c7aacc42c889368b4767d06a72dc7825c9 | Immutable; not run |
| r4 manifest, out/ia64-5ar11-coop-probe-20261118-r4-static/ia64-cooperative-context-probe.manifest.json | 32e53c2d066a9f62afe1687631f3f31c125ba8ef90b8c9918d5b236e0d54c923 | Immutable; not run |

The A stack marker was seeded at SP−4096, but the source and target code reused the initializer symbols as assertion expectations. That makes the check self-derived: matching bytes would only show that the initializer and comparison agree, not that they match independently specified marker values. This is a mandatory preflight defect, so no r4 guest execution was permitted.

## Static-only repairs and candidate identities

The first static-only repair, r5 r1, introduced independent expected symbols but lacked an explicit check of A's marker before A's first suspension. It was preserved without execution. r5 r2 adds that pre-suspension check and retains both resumed checks.

| Artifact | SHA-256 | Qualification |
|---|---|---|
| r5 r1 ELF | a417a8f4f1717c6fa65a6833e0b9787e2d05bb754e614fb1c3738f6abe846dad | Static-only; not run |
| r5 r1 manifest | 648d5348c6fd1ebbcf7a0958e5a84f412b085aebf8135af77ae4acf8deed30e2 | Bound to r5 r1; not run |
| r5 r2 ELF, out/ia64-5ar12-coop-probe-20261119-r2-static/ia64-cooperative-context-probe.elf | 8209d2f779363fdde9cdb23f5ec68ae88884a87d01d75aeb5955128eb1a1c4c1 | Static-only; passed; not run |
| r5 r2 manifest, same candidate directory | 43a6897c903f18c73435659c98a7856bc75d0e0d956231f21d5529c19a5599f9 | Bound to r5 r2; not run |

The r5 r2 source defines separate initializer and expected-value constants for A and B. The static validator requires those independent expected constants, the exact two-word marker geometry, stack bounds, stack growth direction, non-overlap with either RBS and context/probe metadata, seed-before-check ordering, and checks that setup cannot overwrite the markers. It also inspects target disassembly to confirm the stores, loads, expected literals, and failure branches.

For Context A, the static layout is stack [0x115010,0x125010), with entry SP 0x125010. The 16-byte marker occupies [0x124010,0x124020): low word 0x415f535441434b30 at 0x124010, high word 0x415f535441434b31 at 0x124018. Both words lie within A's downward-growing stack. They are seeded at A's entry and checked immediately while A is active, before its first A→B suspension, then checked after A's first resumption and after its final resumption. B's stack-use checks also compare against independent expected constants.

The r5 r2 static map identifies the first A resumption at 0x10cc10, first resumed marker check at 0x10d380, second A→B transfer at 0x10d510, B second resumption at 0x10e8b0, final B→A transfer at 0x10f1b0, final A continuation at 0x10d520, final A exit at 0x10de20, and fail-stop at 0x10f1d0. Static address resolution does not establish that runtime reached any of these points.

## Static and no-guest verification

The r5 r2 candidate passed the static cooperative validation, readelf --lint -W -h -l -S -s, the Mbed TLS profile verifier, and the FP code-generation harness. The candidate-specific debugger preflight accepted six unique breakpoint targets (capacity ten) and a finite plan of 471 step commands capped at 240,193 instructions. Its no-guest parser and breakpoint-capacity smokes loaded no guest ELF and executed zero guest instructions. The command plan itself remains unexecuted.

The debugger plan writes a 16-byte A marker memory dump at each of its 471 bounded snapshots, in addition to the register/context/probe snapshots, so a future separately authorized run can compare both marker words through the checked switch transitions. The generated command plan has SHA-256 7fae78c1606511bdd5957d257840745008ebe952c49d222f9b1730385757259c.

Verification results:

- Full IA64 host unit suite: 196 passed, 8 skipped (204 tests run).
- Focused cooperative-context suite: 57 passed, 0 skipped.
- Cooperative debugger contract suite: 31 passed.
- Static cooperative candidate validation: passed.
- ELF lint, FP code-generation harness, and Mbed TLS profile verifier: passed.
- Python AST and shell syntax checks: passed.
- git diff --check: passed.

These results qualify the candidate's static structure and diagnostic plan only. They do not establish runtime stack-marker survival, a complete A→B→A→B→A round trip, RSE/RNAT/NaT preservation across all transitions, final FP preservation, or intentional SSC completion.

## Historical r3 runtime frontier

The one prior guest execution remains the IA64-5AR11 r3 run, documented in [IA64-5AR11](IA64-5AR11_EPILOG_COUNT_AND_COOPERATIVE_QUALIFICATION.md). It passed B bootstrap, legal B EC initialization and assertion, B→A, and A's first-resume state checks, then failed the A low stack marker. The observed value was 0x535441434b484947 (STACKHIG) rather than 0x415f535441434b30 (A_STACK0). The run did not reach the second A→B transfer, B's second resume, final B→A, final A checks, or intentional SSC exit. That historical result is not evidence for r5 r2.

## Qualification boundary

The static-only r5 r2 repair does not reopen the one-run authorization consumed in IA64-5AR11 and does not override the failed mandatory r4 preflight. The cooperative sequence and runtime preservation contract remain unqualified. Any next guest execution requires separate authorization tied to the exact r5 r2 ELF and manifest hashes above. No guideXOS kernel build or run is authorized by this phase.

## Follow-up audit from the current checkout (2026-11-21)

The task specified expected starting HEAD `9c0d3227afedc95ca1c951441c3a84e8c7687add`, but the actual checkout already contained the IA64-5AR12 commit `de20e044a33b56bef7235bfb1891675ce5e7a1e9`. At audit start, branch `IA64_SUPPORT` tracked `origin/IA64_SUPPORT` at `9be0a05ecbbf00057ca17d0bdcedc065980fb82d` and was six commits ahead. Tracked files were clean; the existing untracked Python caches were preserved. No reset, clean, pull, merge, rebase, or push was performed.

The r4 ELF and manifest hashes were recomputed and still match `17b2afa102d50bd6aa8e907333b3b0c7aacc42c889368b4767d06a72dc7825c9` and `32e53c2d066a9f62afe1687631f3f31c125ba8ef90b8c9918d5b236e0d54c923`. The r5 r2 pair still matches `8209d2f779363fdde9cdb23f5ec68ae88884a87d01d75aeb5955128eb1a1c4c1` and `43a6897c903f18c73435659c98a7856bc75d0e0d956231f21d5529c19a5599f9`. No r4 or r5 run capture exists; the only cooperative guest capture remains historical r3. This audit made zero guest launches and observed no runtime transition frontiers, instructions, faults, or SSC exit.

The static r5 r2 evidence records A's descending memory stack as `[0x115010,0x125010)`, with initial SP `0x125010`. The 16-byte marker occupies `[0x124010,0x124020)`, with 8-byte stores of `0x415f535441434b30` at `0x124010` and `0x415f535441434b31` at `0x124018`. This is within A's stack, aligned for the access width, and disjoint from B's `[0x125210,0x135210)` memory stack, A/B RBS ranges `[0x135600,0x145600)` and `[0x145a00,0x155a00)`, both context records, and the probe record. Source and target evidence bind the two seed stores to independent fixed expected constants; A checks them before its first suspension, at its first resume (`0x10d380`), and at its final resume (`0x10da60`). The target loads at `0x10c7f0`, `0x10d380`, and `0x10da60` branch to the failure path at `0x10f1d0` on mismatch. The marker validates these two selected words only; it does not establish preservation of every byte in A's stack. The switch implementation was not changed.

The stored r5 r2 diagnostic map remains finite at 240,193 instructions and six simultaneous breakpoints. It identifies A's first resume at `0x10cc10`, A's marker check at `0x10d380`, second A→B at `0x10d510`, B's second resume at `0x10e8b0`, final B→A at `0x10f1b0`, and General Exception entry at `0x105000`. The no-guest parser/capacity smoke recorded zero guest instructions and complete cleanup. The command plan itself was not executed.

The full host suite passed on both systems: Windows reported 196 passed and 8 skipped (204 tests run); Ubuntu/WSL reported 204 passed and 0 skipped. The focused cooperative suite passed 57 tests and the debugger contract suite passed 31 tests on Windows. One initial Windows run exposed a path-separator assumption in the debugger-plan test; the assertion now normalizes separators, with no coverage reduction. A concurrent WSL rerun briefly reported a missing temporary ELF fixture; the isolated test passed and a serial full-suite rerun passed. Both r4 and r5 r2 passed `readelf --lint -W -h -l -S -s` in this audit. Mbed TLS profile verification, Python AST parsing for four changed/relevant modules, Bash syntax checks, and `git diff --check` passed.

The preserved 2026-11-19 r5 r2 static validation, FP code-generation, and no-guest debugger records remain PASS. Current repeat attempts of the r5 static runner and FP harness were blocked because the recorded IA-64 `ia64-linux-objdump` is unavailable in this container. This adds no evidence against the preserved candidate; it means those checks could not be repeated here. After the initial clean-status check, additional uncommitted debugger edits appeared in `scripts/diagnose-ia64-coop-switch.py`, `tests/ia64_coop_debug_contract_test.py`, and `tests/ia64_cooperative_context_switch_contract_test.py`, along with new `tests/ia64_cooperative_stack_marker_evidence_test.py`. Those edits raise the planned simultaneous breakpoint limit from six to ten; they were preserved and excluded from the closeout commit. Reconcile them to the six-breakpoint cap before any future run. The r4 self-derived marker expectations still fail mandatory preflight, so no guest execution was authorized. The accepted IA64-4G kernel hash remains `ab13fabb6a92ebe039afa2afd9729f6f6ceb2529b63be2b47b0c87cf264d960c`. Outcome remains **C — `IA64_COOPERATIVE_REQUALIFICATION_BLOCKED`**. The next milestone is to restore the pinned IA-64 inspection toolchain, reconcile the debugger plan to the six-breakpoint cap, repeat static preflight, and obtain separate authorization tied to the exact r5 r2 hashes before a single standalone run. This follow-up is committed locally; its full final HEAD is given in the closeout for this audit.
