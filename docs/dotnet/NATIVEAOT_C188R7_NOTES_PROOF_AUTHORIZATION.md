# NativeAOT C188R7 Notes Proof Authorization

**Date:** 2026-10-24

**Outcome:** **D — Blocked at QAC admission.** The C137 authorization change, hosted regressions, diagnostic NativeAOT candidate, diagnostic kernel, and proof-disabled production builds passed. A fresh post-build QAC census found two foreign QEMU processes from another project; QAC reported acceptance reservations blocked. No R7 guest was queued or launched, so guest startup and Notes interaction remain unverified.

**Preflight:** branch `v1.1_DOTNET_SUPPORT`, starting HEAD `a1f2f2499ebf54ce0605f91590ff6df14d7a6876`; 13 protected roots / 8,497 files and 136 pre-existing worktree status paths. The modified source `ESP/ramdisk.img` started and ended with SHA-256 `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8`.

## C188R6R1 finding

The preserved C188R6R1 serial capture has SHA-256 `3FB19BDEABF8F30C367D97A3FEE00214D220FB9CB7780FCD1A5D93F3EB3EDF60`. It showed successful `RenderMain` and post-render settings completion, then:

| Checkpoint | Historical observation |
|---|---|
| `C188R6-206 BRANCH C137-TEST=TAKEN` | The real selector-4 Notes launch entered the C137 test path. |
| `C188R6-207/208` | Runtime settings capture returned. |
| `C188R6-209/210` | Temporary defaults commit returned. |
| `C188R6-211 BEFORE C137-SUITE` | Last expected checkpoint completed. |
| `C188R6-212 AFTER C137-SUITE` | First expected checkpoint missing. |

The QMP status remained `running` through the 120-second bound. The launch-return, native dispatch-return, input-manager, and main-loop checkpoints were absent; the screenshot showed the desktop shell without a visible Notes window. No Notes input was sent. The historical capture establishes that the suite branch was selected, but does not distinguish a stall inside a C137 case from a stall in the synchronous log immediately after the suite. The raw serial and other historical evidence remain unchanged.

## Context propagation and authorization

In the C160 feature build, `_c152Enabled` is set before proof-context construction. `_c151ProofContext` includes that shared C152 state, and `_c137ProofContext` later merges `_c151ProofContext` and `_c138ProofContext`. Thus ordinary Notes and `c147-native-notes` can carry C137 setup state without explicitly requesting C137 qualification.

The setup flag is used by Notes behavior beyond test selection: it configures the control/list setup, control registration and input routing, and C137-specific document/layout behavior. The fix leaves `_c137ProofContext`, C151/C152 document state, and C147 runtime consumer state intact. C147 still initializes its runtime settings path and C148 still reports the shared v2 settings context.

| Launch/context | Application/setup state | Explicit C137 qualification | C137 suite |
|---|---|---:|---:|
| Ordinary Notes, empty raw context | C152 is enabled; C151 and C137 setup flags can be inherited in the C160 feature build. | No | Skipped |
| `c147-native-notes` | C147 consumer setup remains enabled; C151/C152 merge can set C137 setup state. | No | Skipped |
| C148 settings-v2 behavior reached through C147 | Shared settings initialization and C148 reporting remain enabled. | No | Skipped |
| `c151-notes` | C151 file-dialog and related Notes setup remain selected; C137 setup may be merged. | No | Skipped |
| `c152-notes` | C152 save/document state remains enabled; C151/C137 setup may be merged. | No | Skipped |
| C151/C152 merged into another Notes context | Merged setup is retained for Notes behavior. | No, unless the raw launch context is an explicit C137 qualifier | Skipped |
| Any other context without an explicit C137 qualifier | Setup flags can configure Notes behavior, but do not authorize C137 tests. | No | Skipped |
| `c137`, `c137-native`, `c137-api`, `c137-host-tests`, or `c137-relaunch` | C137 setup is enabled for the qualification route. | Yes, when C138 setup is not active and the suite has not already run | Runs all 46 cases |
| C138/C139 context | Scrollbar/viewport setup remains selected; C138 setup suppresses C137 authorization. | No | C137 skipped; C138/C139 retain their own routes |

These are separate decisions: setup context preserves Notes behavior, a qualification context selects a test scenario, and the authorization predicate decides whether the C137 suite executes.

## Bounded proof-suite invocation audit

The audit is limited to direct proof calls in `ManagedNotes.Launch` around C120–C139 and the C147/C148/C151/C152 interactions. No broad proof-framework refactor was made.

| Suite / call site | Invocation predicate or profile guard | Ordinary/C147 exposure | R7 treatment |
|---|---|---|---|
| C120 control-host suite (`ManagedNotes.cs:3188–3202`) | `_c120ProofContext && !_c120HostTestsRun`; when C121 is compiled, the older C120 suite call is replaced with `controlHostTests = true`. | C120 setup context can inherit component flags; the R7 C160 profile includes C121, so this legacy suite is not called. | No change; control-host initialization remains in the normal Notes path. |
| C121 checkbox host/API suites (`ManagedNotes.cs:3203–3206`) | `_c121HostTestContext` or `_c121CheckboxTestContext`, each with an already-run guard; raw test contexts are `c121-host-tests` and `c121-checkbox-tests`. | Setup context may be inherited from later component contexts, but those merged flags do not set either test-context field. | No change; ordinary/C147/C151/C152 launches do not select these suites. |
| C131 checkbox host/API suites (`ManagedNotes.cs:3220–3224`) | `_c131HostTestContext` or `_c131CheckBoxTestContext`, with already-run guards; raw contexts are `c131-checkbox-host-tests` and `c131-checkbox-tests`. | `_c131ProofContext` merges setup from other components, but suite selection uses the dedicated raw test-context fields. | No change; ordinary/C147/C151/C152 launches do not select these suites. |
| C132 radio host/API suites (`ManagedNotes.cs:3238–3241`) | `_c132HostTestContext` or `_c132RadioTestContext`, with already-run guards; raw contexts are `c132-radio-host-tests` and `c132-radio-tests`. | Component setup can be merged into other control contexts; the test fields remain explicit. | No change; ordinary/C147/C151/C152 launches do not select these suites. |
| C122 label host/API suites (`ManagedNotes.cs:3506–3509`) | `_c122LabelHostTestContext` / `_c122LabelTestContext`, with already-run guards; raw contexts are `c122-label-host-tests` and `c122-label-tests`. | `_c122ProofContext` may be inherited by later setup, but it does not authorize either suite. | No change; ordinary/C147/C151/C152 launches do not select these suites. |
| C123 separator host/API suites (`ManagedNotes.cs:3519–3524`) | `_c123SeparatorHostTestContext` / `_c123SeparatorTestContext`, with already-run guards; raw contexts are `c123-separator-host-tests` and `c123-separator-tests`. | Setup context can be merged from C124; the test fields remain explicit. | No change; ordinary/C147/C151/C152 launches do not select these suites. |
| C124 radio-button host/API/group suites (`ManagedNotes.cs:3534–3542`) | `_c124RadioHostTestContext` or `_c124RadioTestContext`, with already-run guards; raw contexts are `c124-radio-host-tests` and `c124-radio-tests`. | `_c124ProofContext` includes later control setup routes, but suite selection uses the explicit test fields. | No change; ordinary/C147/C151/C152 launches do not select these suites. |
| C125 progress-bar suite (`ManagedNotes.cs:3546–3549`) | `_c125ProgressTestContext && !_c125ProgressTestsRun`; raw context `c125-progress-tests`. | `_c125ProofContext` merges C126/C127/C128 setup, but does not authorize this suite. | No change; ordinary/C147/C151/C152 launches do not select it. |
| C126 group-box suite (`ManagedNotes.cs:3551–3554`) | `_c126GroupBoxTestContext && !_c126GroupBoxTestsRun`; raw context `c126-group-box-tests`. | The setup context is exact `c126-notes`; test selection is separate. | No change; ordinary/C147/C151/C152 launches do not select it. |
| C127 panel suite (`ManagedNotes.cs:3556–3559`) | `_c127PanelTestContext && !_c127PanelTestsRun`; raw context `c127-panel-tests`. | `_c127ProofContext` includes C128/C131 setup routes, but the suite field remains explicit. | No change; ordinary/C147/C151/C152 launches do not select it. |
| C128 panel-lifecycle suite (`ManagedNotes.cs:3561–3566`) | `_c128PanelLifecycleTestContext && !_c128PanelLifecycleTestsRun`; raw context `c128-panel-lifecycle-tests`. | C128 setup can inherit C131 Notes setup, but it does not set the lifecycle test field. | No change; ordinary/C147/C151/C152 launches do not select it. |
| C129 Shift-Tab suite (`ManagedNotes.cs:3570–3572`) | `_c129ShiftTabTestContext`; raw context `c129-shift-tab-tests`. | Setup context is selected by `c129-shift-tab-proof`; suite authorization is a separate context. | No change; ordinary/C147/C151/C152 launches do not select it. |
| C133 host suite (`ManagedNotes.cs:1540`, `3269`) | Explicit `c133-host` or `c133-combo-host-tests`; both sites share an already-run guard. | Not selected by ordinary/C147/C151/C152 context merging. | Uses the exact context helper; R7 checkpoints record the early branch. |
| C134 host suite (`ManagedNotes.cs:1556–1570`) | `_c134HostTestContext && !_c134HostTestsRun`, with `_c134HostTestContext` set for `c134-host-tests`. | Not selected by ordinary/C147/C151/C152 context merging. | Existing exact predicate retained; R7 checkpoints record the branch. |
| C134 host companion call (`ManagedNotes.cs:1610–1625`) | `ShouldRunC134HostTests` for the explicit C133 host routes or `c134-host-tests`, with already-run protection. | Not selected by ordinary/C147/C151/C152 context merging. | Existing accepted R5 correction retained; R7 checkpoints record the companion branch. |
| C133 combo API suite (`ManagedNotes.cs:3257–3266`) | Explicit `c133-api` or `c133-combo-tests`, with already-run protection. | Shared C133 setup flags do not authorize the suite. | Uses the exact-context helper; R7 checkpoints record the branch. |
| C135 popup-menu API/host suites (`ManagedNotes.cs:1540`, `3285–3287`) | `_c135MenuTestContext` (`c135-api`) or `_c135HostTestContext` (`c135-host-tests`), with already-run guards. | `_c135ProofContext` can merge C151/C138 setup, but neither test-context field is set by that merge. | No change; ordinary/C147/C151/C152 launches do not select these suites. |
| C136 secondary-pointer suite (`ManagedNotes.cs:3312`) | `_c136ProofContext` (unless C138 setup is active), or explicit C136 API/host context. `_c136ProofContext` includes explicit C137 contexts. | C147/C151/C152 do not set `_c136ProofContext`; the legacy C137 qualification route can also select C136 coverage. | Preserved as a qualification-path dependency; not changed in R7. |
| C137 mouse-wheel suite (`ManagedNotes.cs:3326–3370`, `3395–3403`) | Previously, merged `_c137ProofContext` could authorize it. Now setup must be present, raw context must be one of the five explicit C137 qualifiers, C138 setup must be absent, and the suite must not have run already. | The R6R1 guest proved the unintended C147/C151/C152-derived selection. | Corrected in both runtime-settings and fallback call paths; before/after suite markers are conditional on the branch being taken. |
| C138 scrollbar and C139 viewport suites (`ManagedNotes.cs:2768`, `3460–3472`) | `_c138ProofContext` / `_c139ProofContext` with already-run guards; C139 setup is intentionally merged into C138. | Their explicit contexts do not come from C147/C151/C152. | Preserved; C137 authorization rejects a C138 setup context. |
| C151/C152 regression suites (`ManagedNotes.cs:2718–2733`) | Compiled only when neither `HOSTLOGPROOF_C167_UI_FOCUSED_BUILD` nor `HOSTLOGPROOF_C166_ORDINARY_BUILD` is defined; gated by `_c151ProofContext` / `_c152Enabled`. | In non-ordinary proof profiles, `_c152Enabled` may make these suites reachable as part of Notes launch. This is a profile-dependent separate authorization risk. | R7 uses C166 ordinary, so these launch-time proof suites are compiled out. No new C151/C152 gate was introduced in this bounded phase. |
| C147/C148 | Shared runtime settings initialization and context reporting; no C137 suite call. | Needed for the real Notes runtime consumer path. | Retained and required by the R7 guest checkpoint sequence. |

The C151/C152 compile-profile behavior is recorded for a separate review. It was not altered here because it requires its own explicit qualification semantics and dedicated route coverage.

## Predicate change and qualification results

`GuideXosNotesProofTestInvocation` now separates C137 invocation authorization from `_c137ProofContext`:

- `IsC137QualificationContext` recognizes exactly the five explicit C137 launch contexts in the matrix.
- `IsC137ProofInvocationAuthorized` requires an explicit raw context and rejects an active C138 setup context.
- `ShouldRunC137MouseWheelTests` additionally requires C137 setup state and rejects an already-run suite.
- The summary/log branch uses the same explicit authorization, so inherited setup cannot produce a false C137 test summary.

The C137 setup state and its C120/control/input consumers were not cleared. C147-to-C120 behavior, C151/C152 save/document state, C133/C134 exact-context authorization, control-host initialization, and Notes rendering code remain in place.

Fresh hosted results:

| Qualification | Result |
|---|---|
| C188R5 context predicate regression | 23 cases, PASS |
| C152 document-state regression | 8 cases, PASS |
| C133 host suite | 24 cases, PASS |
| C134 host suite | 30 cases, PASS |
| C137 mouse-wheel suite through the explicit qualification harness | 46 cases, PASS |
| C188R7 authorization matrix and legacy C133/C134 checks | 15 cases, PASS |
| Checkpoint sequence validator | 16 tests, PASS; includes branch-aware R7 skip and reject cases |
| Managed UI regression aggregate | 38 suites / 930 cases, PASS |
| C188 hosted identity regression | 25 cases, PASS; 1,000 identity stress operations, zero resize-hot-path allocations |
| Prior hosted regressions | C182: 15; C185: 28; C186: 15; C187: 22, all PASS |

C174 passed 1,000 geometry transitions, 500 alternating cycles, 500 independent-window transitions, and 500 rollback checks with zero hot-path allocations and zero drift. C180 passed 1,000 transactions and its 7 stress cases with zero allocations and zero geometry drift. C181 passed its 24 residency cases and 7 stress cases, retaining five application slots and rejecting stale generations. C185/C186 contract suites passed without changes to their public contracts.

## NativeAOT and kernel qualification

All new build outputs are under `out/dotnet/c188r7-notes-proof-authorization/`.

| Candidate | Result | SHA-256 |
|---|---|---|
| R7 diagnostic NativeAOT composite, `candidates/diag-nativeaot-002/build/nativeaot/artifacts/HostLogProof.elf` | PASS; PE-to-ELF verification passed, ELF64 x86-64, 7 LOAD segments, zero relocations, 8 required retained symbols; R7 markers retained | `91B8FEB2C15FAE36DDB47DB730C7AAEF0FEC693266EDFC5E5C5BA65D844A183C` |
| Diagnostic candidate kernel, `candidates/diag-kernel-001/kernel/bin/kernel.elf` | PASS; ELF64 x86-64 with freestanding C++14, no exceptions/RTTI, no STL | `9C12F856569E4FE40E1D70CEB56252D3D8E0138DCD4016F83E6F8627DAC261FA` |
| Proof-disabled production NativeAOT composite, `candidates/prod-nativeaot-001/build/nativeaot/artifacts/HostLogProof.elf` | PASS; ELF64 x86-64, standard dispatch symbols present; CounterApp identity and selector 9 absent; R3/R4/R6/R7 markers absent | `608CD7E8C895890250B0C7A733ECF379A134DDC1C186F70D02984C3BF89B6626` |
| Proof-disabled production kernel, `candidates/prod-kernel-001/kernel/bin/kernel.elf` | PASS; ELF64 x86-64; production identities retained; development descriptor, CounterApp, selector-9 controls, and diagnostic markers absent | `22BC4AFE228263E4187B2AA57EAF21D1CEA46D3992A139F81EC185B1C22154AF` |

The candidate preserves production selectors 4–8 and leaves selector 9 development-only. The normal production NativeAOT and kernel candidates exclude CounterApp registration, C188 selector routing, and R7 diagnostics. No kernel source changes or managed residency-slot changes were made. The native host ABI remains v4 / 128 bytes, with appended callbacks at offsets 104, 112, and 120; no ABI declaration or layout changed.

The first diagnostic NativeAOT attempt compiled successfully but its verifier found that checkpoint `C188R7-208` was inside the R3-only dispatch block and therefore absent from an R7-only build. The marker was moved to the shared post-launch point, guarded to initial launches, and the isolated `diag-nativeaot-002` candidate then passed the full artifact verifier. The failed first output remains isolated under `candidates/diag-nativeaot-001/`.

## Guest admission and runtime evidence

The candidate media was staged only in `runs/diag-001/esp/`. The staged RAM disk was a copy of the pre-existing modified source RAM disk; its staged SHA-256 is `FF8130B427858A68997D57ABF06855357CA8FE7C9CF625D65871FF296650B8CB`. The candidate kernel hash in the staged ESP matches the qualified diagnostic kernel. The UEFI loader hash is `D72A35DA882892BA97356A1C006562ABA2908D9DD909152B96DCBB655D6C2D9B`.

After hosted and build qualification, a fresh QAC census was recorded in `qualification/qac-census-post-build-001/`. QAC was online but reported two foreign QEMU processes and warned that acceptance reservations were blocked. `list` showed zero reservations and zero managed sessions. Read-only process inspection confirmed both QEMU processes were owned by `LEON2_RDT\guideX` and running diagnostic ISO media from the separate `E:\BM3B\out` project (PIDs 20392 and 21932). Their command lines and owner records are in `foreign-qemu-processes.json`.

No R7 reservation was requested, no guest QEMU process was launched, and no cancellation, shutdown, or release action was sent to those foreign processes. Reservation release is not applicable because R7 never acquired one. Consequently there is no R7 raw serial, QMP session, framebuffer screenshot, guest checkpoint sequence, Notes launch-return evidence, input-loop evidence, or actual keyboard/pointer interaction. The R7 validator's synthetic skip-path tests are not guest observations. The existing C188R6R1 logger/helper uncertainty remains unresolved.

## Artifact and preservation record

Artifact sizes and SHA-256 values are also recorded in `qualification/artifact-hashes.json`. The QAC census, preflight baseline, hosted logs/manifests, candidate build manifests, staged media, and this report are retained below the R7 output root. Historical C188R6R1 evidence was read-only and was not edited.

The final protected-tree comparison is `qualification/protected-tree-comparison.json`. It compares all 13 protected roots and all 8,497 files against `preflight/baseline.json`, verifies all 136 pre-existing worktree status paths, and checks the original modified `ESP/ramdisk.img` hash. Result: PASS; no protected path, size, or hash changed, no pre-existing status path was lost or altered, and the original ramdisk remains `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8`.

The historical C185 uncertainty concerning 212 generated files whose earlier byte identities were unavailable remains unresolved and is preserved.

## Remaining C188 gates and C188R8 direction

C188 is not accepted. R7 has established the source authorization rule, retained explicit hosted C137 qualification, and passed hosted plus diagnostic/production build checks. It has not demonstrated that selector-4 Notes returns from launch, reaches the input loop, renders a visible usable Notes window, or accepts real input because external QEMU processes blocked QAC admission.

When QAC is clear of foreign QEMU processes and reservations, the next runtime action should be one bounded R7 diagnostic guest using the staged candidate, followed by guest shutdown and release verification. If it reaches readiness, a proof-disabled production readiness check remains a separate later gate before any full six-scenario C188 campaign. If it stops earlier, use the first missing R7 checkpoint to scope a bounded diagnosis before changing another proof predicate. Do not start C188R8, C189, or the six-scenario campaign automatically.

## C188R7R1 deferred Notes startup confirmation — 2026-10-24

**Outcome D — Blocked at QAC admission.** Preflight confirmed repository `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT`, branch `v1.1_DOTNET_SUPPORT`, and starting HEAD `de32fa7f1c9ad70c7d5349b8b9c9845bf30527b5` (the C188R7 implementation commit; one local commit ahead of origin). The live preflight inventory matched the retained expectations: 13 protected roots, 8,497 files, 136 existing worktree status paths, and `ESP/ramdisk.img` SHA-256 `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8`. All protected files matched the R7 baseline. No `.phase`, `.lock`, or duplicate-execution guard file was present under the R7 output root; the existing runner itself rejects non-unique run roots and overwriting prior evidence.

One fresh QAC census was captured at `2026-10-24T22:16:53.2426609Z` in `qualification/qac-census-r7r1-001/`. The coordinator was online. QAC reported one QEMU process with zero foreign, two acceptance reservations, one managed session, and one queued reservation. Running reservation `8b30e94f21e44394808624e6bf192733` belongs to `guideXOSServer`, owner `LEON2_RDT\guideX` (PID 22776, owner verified), with QEMU child PID 25196. Waiting reservation `5f8b3867de034c4fb39ed76e4a8f75aa` belongs to `guideXOS_NET10`, queue position 1, owner `LEON2_RDT\guideX` (PID 26060, owner verified). The simultaneous Windows process snapshot identified the managed child command line, but direct CIM owner lookup for that child was unavailable; QAC's reservation owner verification succeeded. Existing reservations made exclusive admission unavailable. No reservation was requested, no C188R7R1 runner or QEMU guest was launched, and no foreign or managed process was touched.

The prepared `runs/diag-001/esp/` candidate was verified and reused unchanged; the exact file sizes and hashes plus the QAC and preservation snapshots are retained in `qualification/preflight-r7r1-001/candidate-verification.json`, `protected-tree-before.json`, and `qualification/qac-census-r7r1-001/`. Diagnostic NativeAOT ELF SHA-256 is `91B8FEB2C15FAE36DDB47DB730C7AAEF0FEC693266EDFC5E5C5BA65D844A183C`, PE is `CCF0B091BA1917573BB32EECB442EBCB0726F53AA6E58DAB9A681898879B2227`, and link map is `7D3B1698DF38DB1DBBE182D3B99483D2CBFA1DF434A6099A3EF01BAC103B7037`. Diagnostic kernel ELF is `9C12F856569E4FE40E1D70CEB56252D3D8E0138DCD4016F83E6F8627DAC261FA` and PE is `91AC0EB6E18F5DF8D5D4D5F6A048951F6932C98A301C66702B0EF7DDBF5BFDAA`. Staged RAM disk SHA-256 is `FF8130B427858A68997D57ABF06855357CA8FE7C9CF625D65871FF296650B8CB`; bootloader is `D72A35DA882892BA97356A1C006562ABA2908D9DD909152B96DCBB655D6C2D9B`. The manifest identifies `C160Composite+C185Boundary+C188ExternalAppIdentity+C188R7ProofAuthorization`, retains production selectors 4–8, and retains all R7 Notes checkpoints. The previously qualified proof-disabled production NativeAOT and kernel candidates also remain present and hash-matched; neither was booted.

The pre-launch source audit found no predicate change necessary. Selector-4 `c147-native-notes` remains the selected context; `ManagedNotes.Launch` enables and retains C151/C152 setup and the C147/C148 shared runtime-settings path. C133 host/combo and C134 host/companion call sites require their explicit C133/C134 invocation contexts. `ShouldRunC137MouseWheelTests` still requires C137 setup, an explicit one-of-five C137 qualification context, no C138 context, and a not-yet-run suite. Explicit C133, C134, and C137 qualification routes remain in the candidate.

No guest runtime result exists for this continuation. The actual checkpoint sequence is **not started**: no selector dispatch, Notes launch, proof skip/taken marker, Notes surface/render, managed or native launch return, input-manager initialization, main-loop readiness, Notes window, keyboard/pointer input, QMP session, framebuffer capture, guest shutdown, or reservation release was observed. The R7 checkpoint profile retains post-render and launch-return markers, but their presence in the compiled candidate is static evidence only. The first missing runtime checkpoint cannot be named because QAC did not admit the guest. No build or hosted regression was rerun; R7's historical accepted results remain historical.

The post-preflight protected-tree and status comparison passed: 13 roots / 8,497 protected files unchanged, all 136 pre-existing status paths retained, and the modified RAM disk hash unchanged. No R7R1 guest-media artifacts were created. QAC cleanup and release are not applicable to this attempt because it acquired no reservation; both observed reservations remain owned by their other QAC workflows. The C185 historical uncertainty about 212 generated files remains preserved. C188 acceptance remains open; C188R8, C189, and the six acceptance scenarios were not started. Resume only with one bounded diagnostic guest after a fresh QAC census verifies zero foreign QEMU processes, reservations, managed sessions, and queued work, and after QAC grants the exclusive reservation.
