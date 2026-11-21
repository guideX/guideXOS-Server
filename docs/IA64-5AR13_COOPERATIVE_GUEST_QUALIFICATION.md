# IA64-5AR13 Cooperative Guest Qualification

**Outcome C — guest qualification blocked.** The r5 r2 candidate passed its independent static checks and the host regression suites passed, but the Windows host could not start WSL (`Wsl/Service/E_UNEXPECTED`). No Ski process started, no candidate ELF was loaded, and no authentic guest checkpoint was observed. Marker survival across either resumption therefore remains unqualified.

## Repository and execution boundary

- Repository: `D:\dev\guideXOSServer_IA64`
- Branch: `IA64_SUPPORT`
- Requested starting commit: `de20e044a33b56bef7235bfb1891675ce5e7a1e9`
- Actual starting commit: `de20e044a33b56bef7235bfb1891675ce5e7a1e9`; the requested commit was the starting HEAD.
- During this work, newer local commit `f83aa275024a3de9f9bd20e6c2a364c2f7dd190e` appeared on the branch. It is preserved. It updated the 5AR12 audit and the debugger path-separator assertion; no reset, rebase, merge, cherry-pick, stash, clean, branch switch, or push was performed.
- The requested starting commit is an ancestor of the preserved newer commit. The upstream is `origin/IA64_SUPPORT` at `9be0a05ecbbf00057ca17d0bdcedc065980fb82d`.
- Existing untracked Python caches and historical ignored output were preserved. The separate IA-64 Hypervisor repository was not accessed or modified.

The r5 r2 manifest authorizes one standalone `-forcesystem` probe execution on one CPU. Its scheduler and external interrupts are disabled. This is an isolated Ski system-mode probe, not a guideXOS kernel build or a scheduled kernel boot. The pinned Ski tooling runs inside WSL. Attempts to start WSL as the default user and as root failed before a guest command could run, with `Catastrophic failure` / `Wsl/Service/E_UNEXPECTED`. No guest run or QAC reservation occurred. QEMU/QAC is not part of this standalone Ski path; an unrelated QEMU process found on the host was left untouched.

## Candidate identity and marker contract

The selected candidate remains the immutable r5 r2 artifact:

| Artifact | Path | SHA-256 |
|---|---|---|
| ELF | `out/ia64-5ar12-coop-probe-20261119-r2-static/ia64-cooperative-context-probe.elf` | `8209d2f779363fdde9cdb23f5ec68ae88884a87d01d75aeb5955128eb1a1c4c1` |
| Manifest | `out/ia64-5ar12-coop-probe-20261119-r2-static/ia64-cooperative-context-probe.manifest.json` | `43a6897c903f18c73435659c98a7856bc75d0e0d956231f21d5529c19a5599f9` |

The manifest binds the expected eight-event A→B→A→B→A sequence, one authorized probe-only execution, zero expected faults, and intentional SSC exit 66. Hashes were recomputed during this phase and match the accepted identities. The saved 5AR12 `static-validation.log` records `COOPERATIVE_CONTEXT_STATIC_PASS` for this same ELF and records the target map, marker geometry, independent expectations, and disassembly checks. A six-breakpoint command plan is now enforced by the diagnostic checker and its host contract tests. The live Ski preflight and parser smoke could not be repeated after WSL became unavailable.

Context A uses memory stack `[0x115010,0x125010)` with entry SP `0x125010`. Its two 8-byte markers occupy `[0x124010,0x124020)` and expect `0x415f535441434b30` and `0x415f535441434b31`.

In `tests/ia64_cooperative_context_switch/probe_sequence.s`, the initializer literals are defined as `STACK_USE_A_LOW` and `STACK_USE_A_HIGH`; the separate expected literals are defined as `STACK_USE_A_LOW_EXPECTED` and `STACK_USE_A_HIGH_EXPECTED`. The code stores the initializer values to `[r12]` and `[r12+8]`. At the initial check, first A resumption, and final A resumption, target code loads those actual stack addresses, compares them with the independent expected literals, and branches to `ia64_coop_test_fail` on mismatch. `scripts/run-ia64-ski.py` statically checks the initializer, linked stack geometry, three load/compare/fail paths, and that the checks precede the corresponding suspension/resumption boundaries.

The updated diagnostic checker writes a 16-byte Ski memory dump for the marker address and correlates the raw words with the ELF hash, task A, stack pointer, event count, IP, and instruction count. It rejects a dump starting at another address, missing or incomplete words, a changed value, a missing checkpoint, or observations at the wrong execution frontier. This path is covered by host fixtures; no such guest memory dump was produced in 5AR13.

The debugger plan is held to six simultaneous program breakpoints: A marker seed, B first entry, B EC assertion, first B-to-A switch, A first resumption, and A final resumption. The direct marker dump points are A's seed and both A resume entries. The full eight-event record and final context checks remain required for the cooperative sequence. This six-slot plan passed host contract validation only; it was not installed in Ski during this phase.

## Preflight and historical comparison

Immutable r4 remains rejected before guest execution. Its ELF SHA-256 is `17b2afa102d50bd6aa8e907333b3b0c7aacc42c889368b4767d06a72dc7825c9` and its manifest SHA-256 is `32e53c2d066a9f62afe1687631f3f31c125ba8ef90b8c9918d5b236e0d54c923`. The r4 checker reused initializer-derived expectations, so it did not provide an independent correctness test. r4 was not launched.

Historical 5AR11 r3 is reference evidence only. Its one Ski guest run reached B bootstrap, B EC initialization/assertion, B-to-A, and A's first resumed checks, then failed the A low stack-use marker: actual `STACKHIG` (`0x535441434b484947`) instead of `A_STACK0` (`0x415f535441434b30`). It did not reach the second A-to-B transfer, B's second resume, final B-to-A, final A checks, or intentional exit. That result is not evidence for r5 r2.

## Guest evidence

No r5 r2 guest execution occurred. Consequently there is no 5AR13 serial output, application event trace, direct stack-memory observation, task/SP snapshot, preserved-register observation, switch/resume evidence, return value, or guest exit result. The initial checkpoint, first resumption, and second resumption are all **unobserved**. No kernel was built or run. No runtime defect was demonstrated; the blocker is the host WSL service.

## Host validation

All commands below ran on the Windows host, not in an IA-64 guest:

| Check | Result |
|---|---|
| Full IA64 host suite, `python -m unittest discover -s tests -p 'ia64*test.py'` | PASS: 214 run, 206 passed, 8 skipped |
| Focused cooperative-context suite | PASS: 57 tests |
| Debugger contract suite | PASS: 31 tests |
| Independent stack-marker and negative evidence suite | PASS: 10 tests, including changed value, wrong address, missing checkpoint, corruption after either resumption, incorrect execution point, wrong raw dump address, and incomplete raw dump |
| Candidate and manifest SHA-256 | PASS: exact accepted hashes |
| r5 r2 static qualification | PASS in the saved static validation record; no guest execution |
| Live Ski/WSL preflight and parser smoke | BLOCKED: WSL service could not start |
| Authentic guideXOS guest launch | NOT RUN: zero guest launches |
| Kernel build/boot and QAC reservation | NOT RUN / not applicable to the standalone Ski probe |
| `git diff --check` | See closeout commit record |

During test development, one new raw-dump fixture assertion and the full-suite run containing it failed because the fixture expected the word `incomplete`, while the parser correctly reported `has 1 qwords, expected 2`. The test was corrected to assert the parser's specific message. The final marker suite and final full suite both passed; there are no unresolved test failures.

Captured Windows test logs and the exact artifact identities are indexed in [the 5AR13 evidence manifest](IA64-5AR13_EVIDENCE_MANIFEST.json). The host tests establish the diagnostic's rejection behavior and the static acceptance structure. They do not establish runtime marker survival, all preserved-register behavior, RSE/RNAT/NaT preservation across both transitions, or application completion.

## Follow-up

Restore WSL command startup, repeat the exact-hash six-breakpoint no-guest debugger preflight, and then use the separately authorized single r5 r2 probe execution. Preserve raw marker dumps at all three A checkpoints, event/context snapshots, failure state, and exit status. Keep marker integrity, cooperative switch completion, general-register/branch-register state, RSE/backing-store state, and application completion as separate acceptance claims.

## Local closeout

The implementation, host tests, report, and evidence manifest were committed locally as `953fa2769c84b8f86abd6f88309b6aa080d5c276` on `IA64_SUPPORT`. `git diff --check` passed. At that commit, the tracked worktree was clean and the branch was eight commits ahead of `origin/IA64_SUPPORT` with no commits behind. The initial closeout update was committed as `e2bd7e9ed22a36cfe39339c230f74811f1da01c0`; this final report update records the transient test assertion. The final HEAD and ten-commit upstream divergence are given in the task closeout. Untracked Python caches were preserved, and nothing was pushed.
