# NativeAOT C188R6 Notes Post-Render Startup Diagnosis

## Scope and current status

C188R6 continues the focused Notes startup investigation after the C188R5 correction. Its initial `diag-001` guest admission was blocked by an occupied QAC lane and remains historical evidence. Phase C188R6R1 later ran one targeted guest from the retained candidate under a verified exclusive reservation. That run identified the C137 proof suite being selected on the ordinary Notes startup route; it did not establish input-loop readiness or full C188 acceptance. No C188 acceptance scenario was resumed and C189 was not started.

The original R6 preflight confirmed branch `v1.1_DOTNET_SUPPORT` at `3b0c9ecc96f2056c5cb059dedd8c8f35dd5fb992`; C188R5 implementation is at that commit. The C188R6R1 continuation started on the same branch at `77f2b46816d979d1c48bbf7f9cb1adf105b6b43b`. The preserved `ESP/ramdisk.img` SHA-256 remained `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8`. Fresh pre-guest and post-guest inventories both recorded 136 pre-existing worktree status paths and 13 protected roots with 8,497 files; all pre-existing paths and protected hashes matched.

## C188R5 findings carried forward

C188R5 separated C133/C134 proof-suite invocation from shared Notes setup and retained its 23 context-predicate, 8 C152 document-state, 24 C133, and 30 C134 cases. Its diagnostic reached real selector-4 Notes initialization, created a surface, returned from the first render, and reached `C188R3-163`; it did not establish a Notes launch return or input-loop readiness. The prior report and evidence remain unchanged.

## Source call path after checkpoint 163

The actual `RenderMain` return is separately bracketed by `C188R3-160/161`; checkpoint `C188R3-163` occurs later, after the C147/C149 post-render settings-tip operation. The path after 163 is:

| Source location | Operation and branch | Potential blocking behavior |
|---|---|---|
| `ManagedNotes.cs:2773–2779` | `C188R3-163`, then R6 boundary markers 200/201 | `host.TryLog` is synchronous. |
| `ManagedNotes.cs:2780–3011` | C125–C136 initial state summaries. Each is gated by its proof context. In the C147 Notes launch, C151/C152 shared setup makes C133/C135 contexts available; C135's initial-state summary is selected. C133's summary is skipped when C135 context is active. | Local comparisons plus fixed `host.TryLog` calls. No surface creation or focus mutation occurs here. |
| `ManagedNotes.cs:3012–3020` | R6 marker 202 closes the context-summary span; markers 234/235 bracket the two unconditional C117 post-render status logs. | Fixed synchronous `host.TryLog` calls. |
| `ManagedNotes.cs:3021–3115` | Computes `runFocusedProofTests`, then forces it false for `_c147RuntimeConsumerContext`. R6 marker 203 records the branch; 204/205 bracket the catalog only if taken. | C147 path should skip the catalog. |
| `ManagedNotes.cs:3116–3286` | Historical C117–C136 suites are gated by `runFocusedProofTests` or explicit test contexts. C133/C134 invocation has the R5 predicate correction. | Qualification suites are not expected on C147 startup. |
| `ManagedNotes.cs:3287–3375` | C137 suite predicate and result summary. R6 markers 206–217 record branch, runtime capture, defaults commit, 46-case helper call, restore, and status log. | C137 helper uses finite in-memory state tests and does not call the host. Its path is nevertheless qualification-only work. |
| `ManagedNotes.cs:3376–3425` | C138 status and optional C139 54-case suite; R6 markers 218–223 record C139 branch and call. | C138/C139 should be false for the C147 launch. |
| `ManagedNotes.cs:3426–3511` | Remaining label, separator, radio, and other focused helpers; explicit contexts gate their execution. R6 catalog completion marker 205 is emitted only for a taken broad catalog. | Not expected on the C147 launch. |
| `ManagedNotes.cs:3512–3578` | Calculates and logs the Notes launch result; R6 markers 224–227 bracket result calculation/logging and the managed return. | `TryLog` is synchronous; result calculation is local. |
| `GuideXosApplication.cs:329–345` | Calls `application.Launch(host)`, receives its result, then emits R6 markers 011–013 around the caller result log. | Managed application dispatch and a synchronous host log. |
| `nativeaot_application.cpp:4673–4690` (and resident path 4918–4939) | Native code receives the managed entry return and emits `[C102-LAUNCH-RETURN]`. | Native serial output is synchronous. |
| `main.cpp:875–879, 6904–6908` | Initializes the input manager, emits the existing input-loop marker, then polls input. | Existing kernel/device initialization; already has serial boundaries. |

The C188R6 validator preserves the C188R4 branch-aware ordering: operation markers are required only when their branch reports `TAKEN`. The input-loop boundary is already covered by `[KERNEL] Input manager initialized` and `[KERNEL] Entering main loop (waiting for input)...`.

## Context behavior: initialization versus test authorization

| Category | Context or behavior | C188R6 treatment |
|---|---|---|
| **A — Required application initialization** | Selector-4 Notes initialization, C147 runtime-consumer state, C148 settings use, shared C151/C152 document/menu setup, actual control registration, rendering, and input handlers | Retain and verify. These make the real Notes surface usable. |
| **B — Explicit qualification operation** | C137's 46-case mouse-wheel suite | Should be authorized only by its explicit C137 launch contexts (`c137`, `c137-native`, `c137-api`, `c137-host-tests`, `c137-relaunch`). R6 currently instruments the inherited predicate without changing it. |
| **C — Shared setup dependency** | `_c151ProofContext` and C152 document setup | Preserve even when a C151/C152-derived test suite must not run. R5 already applies this separation to C133/C134; R6 checks for another distinct leak. |

Current static lead: `ManagedNotes.cs:770–782` sets `_c152Enabled` for this composite and merges it into `_c151ProofContext`. `ManagedNotes.cs:825–830` folds C151 shared setup into `_c147RuntimeConsumerContext`. Later, `ManagedNotes.cs:1347–1360` initializes `_c137ProofContext` from explicit C137 contexts **and** merges `_c151ProofContext`; `ManagedNotes.cs:1400–1403` also merges C138. At `ManagedNotes.cs:3287–3294`, the C137 suite predicate consumes `_c137ProofContext` (unless C138 is active). Thus the real C147 Notes path appears to select a qualification-only C137 suite, despite the broad catalog being skipped. The first guest must confirm the runtime branch and progress boundaries before any correction is made.

## Diagnostic hypotheses before the first R6 guest

| Hypothesis | Distinguishing observation |
|---|---|
| H1 — A remaining proof-test branch executes unexpectedly | R6 reports `C137-TEST=TAKEN` on `c147-native-notes`; markers show whether runtime capture, settings reset, suite, and restore complete. |
| H2 — Notes post-render initialization does not return | A specific before/after pair is incomplete, or the context-summary or C117 status-log span fails to close. |
| H3 — A host-service callback blocks | Managed progress stops within a bracketed `TryLog` boundary. The R6 markers narrow the span but cannot prove which synchronous log callback blocked. |
| H4 — Notes launch returns but native dispatch does not advance | R6-227 and caller marker R6-011 appear, but native launch-return/input-loop markers do not. |
| H5 — Diagnostic logging affects progress | A missing marker coincides with a synchronous `TryLog`; this remains an alternative explanation until corroborated by the surrounding operation and QMP/serial evidence. |

These are hypotheses, not established defects.

## Instrumentation and logger limitations

R6 adds fixed-literal, proof-only markers after checkpoint 163; around the C133/C135 post-render summaries, C117 status logs, optional focused catalog, C137 and C139 branches/helpers, Notes result/return, and the managed caller return/result log. The C137 markers also split runtime capture, defaults commit, helper execution, restore, and summary logging. All are guarded by `HOSTLOGPROOF_C188R6_NOTES_POSTRENDER`; ordinary production builds do not define it.

The markers use existing `GuideXosHost.TryLog`. That method checks capabilities, validates the fixed byte-span length, uses `stackalloc`, and synchronously calls the ABI log callback (`GuideXosHost.cs:346–363`). The native callback writes each message through serial (`nativeaot_application.cpp:3920–3986`); `serial::putc` spins until the UART transmit-empty bit is set (`serial_debug.h:42–46`). The logger can therefore stall and can affect timing. This phase adds no ABI, locks, dynamic formatting, heap allocation, or new logging facility. A missing “after” marker identifies the bounded code span but must be interpreted with the logging caveat and QMP state.

## Static qualification and blocked guest admission

### Hosted regressions

All R6 outputs are isolated below `out/dotnet/c188r6-notes-postrender/host-tests/`.

- C188R5 compatibility regression: **PASS** — 23 context-predicate cases, 8 C152 document-state cases, 24 C133 cases, and 30 C134 cases.
- Managed UI aggregate: **PASS** — 38 suites / 930 cases, including C137 (46 cases), C174 shared resize, C180 default-deny, and C181 residency. Resize hot-path allocations and cumulative geometry drift were zero across C172–C177 and C180 coverage.
- Inherited hosted suites: **PASS** — C182 15, C185 28, C186 15, C187 22.
- C188 host suite: **PASS** — 25 cases; 1,000 identity stress operations, 500 valid resizes, 250 invalid geometries, 250 stale-generation attempts, 100 lifecycles, and zero resize allocations.
- Checkpoint validator: **PASS** — 13 tests, retaining the prior R3/R4/R5 cases and adding R6 ordering and taken/skipped C137-branch checks.

### NativeAOT and kernel qualification

- Diagnostic NativeAOT composite: **PASS** — ELF64, 7 load segments, zero relocations, 8 required retained symbols, including all R6 markers. ELF SHA-256 `B97C7EA3BA2D636415F5EF4E09D23D8C341610D249710F7EC81E399A37DC3883`; PE SHA-256 `A5EA36E0DBFE60863571B3D520BA2AFB06920FFA8CDEB5170D9918C72F7AA32F`; link map SHA-256 `EFEFEBEB3207274EEAFD950A9D421E68B933E66E7A8B8A9319B23AC502706FEA`.
- Separately built project-reference outputs: CounterApp DLL SHA-256 `6EBFC0939B54E1D6491F242AFAC3265CAE7F0735A7847AC859E108A9D1546CE2`; ManagedUiContracts DLL SHA-256 `BE399C19DCCF3D6EF5E2DB1C4E680997645C59C4B4BB93CA08866D7DDA523CB4`.
- Freestanding candidate kernel: **PASS** — ELF64 x86-64, no STL, exceptions, or RTTI flags; C188 candidate-only identity markers retained. Kernel SHA-256 `9C12F856569E4FE40E1D70CEB56252D3D8E0138DCD4016F83E6F8627DAC261FA`.
- Proof-disabled production NativeAOT: **PASS** — SHA-256 `E1CE8051E128AD7B6C837EB0E5B59A9AB8B292598DF2C9E7DB8950A38981E041`; selectors 4–8 retained, CounterApp selector 9 and C188 selector route absent, and R3/R4/R6 proof markers excluded.
- Proof-disabled production kernel: **PASS** — SHA-256 `22BC4AFE228263E4187B2AA57EAF21D1CEA46D3992A139F81EC185B1C22154AF`; production Notes, Settings, Calculator, Task Manager, and File Explorer identities retained; candidate-only descriptor absent.

The existing ABI and application model were not edited. The C188 hosted suite and production build manifests preserve ABI/host-contract and selector coverage; no dynamic loading or production application-model changes were introduced. The five residency slots, C174 transactions, C180 default-deny behavior, C181 generation-safe lifetime, and C185/C186 contracts remain under the inherited passing hosted gates.

### Prepared diagnostic media

The initial R6 candidate (still using the inherited C137 test predicate) is staged at `out/dotnet/c188r6-notes-postrender/runs/diag-001/`. The source `ESP/ramdisk.img` and isolated pre-stage copy both hashed to `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8`. Only the isolated run copy was updated with `/APPS/GXOSAPP.ELF`; staged ramdisk SHA-256 is `3A0E57E85C38F929CFF6B2FC71F0EC15AB40748FD544E5CDE372AD702DFF80EF`, preserving 185 unrelated files. The candidate kernel SHA-256 is `9C12F856569E4FE40E1D70CEB56252D3D8E0138DCD4016F83E6F8627DAC261FA`; UEFI loader SHA-256 is `D72A35DA882892BA97356A1C006562ABA2908D9DD909152B96DCBB655D6C2D9B`.

### Historical QAC admission for diag-001

At the fresh census, QAC was online, but one verified managed acceptance session was already running: reservation `8b84465adf184cc6a1f416744168127c`, owner `LEON2_RDT\guideX` (owner PID 31704), QEMU PID 24052. QAC reported one managed session and one QEMU process. The exact status, list, process, and blocked-admission records are in `runs/diag-001/qac-preflight-blocked.*` and `runs/diag-001/blocked-admission.json`.

No R6 reservation was requested, no guest was queued or started, and the other owner was left untouched. Therefore R6 has no guest serial sequence, QMP status, screenshot, Notes input result, input-loop result, launch-return marker, or R6 reservation release. **The first runtime-incomplete operation remains unknown.** No correction was made from the static C137 lead alone, and no production guest was started.

### Preservation and outcome

The pre-commit blocked-admission comparison recorded 136 pre-existing status paths and 154 current paths, including the R6 changes. All 136 pre-existing paths remained present and byte-identical. The final post-commit comparison returned to 136 current paths, with no missing or changed pre-existing paths. All 13 protected roots / 8,497 files and the 6,944 files in the previous R3–R5 evidence roots matched their preflight SHA-256 inventories exactly, with no additions, removals, or changes. The original modified ramdisk hash stayed unchanged. The final report is `out/dotnet/c188r6-notes-postrender/preservation-comparison.json`.

**Historical outcome for diag-001: Outcome D — blocked.** QAC ownership prevented that targeted diagnostic guest after static qualification. NativeAOT/kernel build success did not establish launch return or Notes readiness. At that point no runtime evidence confirmed the C137 branch or any other post-render stopping point.

At the time of the initial `diag-001` block, remaining gates were a fresh QAC census, one R6 diagnostic boot, and readiness/input evidence. C188R6R1 satisfied the census and diagnostic-boot gates; launch return, input-loop readiness, visible Notes rendering, and accepted Notes input remain unverified. The full six-scenario C188 campaign remains pending.

The recommendation in the initial R6 report was to resume the focused diagnostic guest after QAC became free. That run is recorded below.

## C188R6R1 deferred diagnostic guest

### Admission and candidate integrity

At `2026-10-24T20:58:52.1759707Z`, the single fresh QAC census reported the coordinator online, zero QEMU processes, zero reservations, and zero managed sessions; its snapshot is `runs/diag-002/qac-census-before.json`. QAC granted reservation `708517e1010242c5bd47c55eadfd85d6` at `2026-10-24T21:00:43.3758848Z` for project `guideXOSServer`, mode `acceptance`. The during-run list verified owner `LEON2_RDT\guideX` (owner PID `25364`) and child QEMU PID `17616`. The existing runner used a one-second admission window and a 120-second guest readiness bound.

The retained candidate was reused without rebuilding. Its original `diag-001` files were left unchanged; the prepared ESP was copied to `runs/diag-002/esp/` for the new run. Full SHA-256 checks matched the retained C188R6 manifest:

| Artifact | SHA-256 |
|---|---|
| Source `ESP/ramdisk.img` (unchanged) | `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8` |
| Diagnostic NativeAOT ELF | `B97C7EA3BA2D636415F5EF4E09D23D8C341610D249710F7EC81E399A37DC3883` |
| NativeAOT PE | `A5EA36E0DBFE60863571B3D520BA2AFB06920FFA8CDEB5170D9918C72F7AA32F` |
| NativeAOT link map | `EFEFEBEB3207274EEAFD950A9D421E68B933E66E7A8B8A9319B23AC502706FEA` |
| Freestanding diagnostic kernel | `9C12F856569E4FE40E1D70CEB56252D3D8E0138DCD4016F83E6F8627DAC261FA` |
| Staged guest ramdisk | `3A0E57E85C38F929CFF6B2FC71F0EC15AB40748FD544E5CDE372AD702DFF80EF` |
| UEFI bootloader | `D72A35DA882892BA97356A1C006562ABA2908D9DD909152B96DCBB655D6C2D9B` |

The guest used the existing C188R6 proof-instrumented NativeAOT composite, its matching freestanding kernel, and the `/APPS/GXOSAPP.ELF` staged route. Candidate hashes and launch settings are in `runs/diag-002/candidate-integrity-before.json` and `runs/diag-002/runner-launch.json`. The complete guest command line is in `runs/diag-002/qemu-process.json`.

### Actual post-render sequence and source finding

The serial capture contained 251 ordered expected checkpoints. `RenderMain` returned successfully at `C188R3-299` / `C188R3-161`, then the C147/C149 post-render settings operation returned at `C188R3-163`. R6 markers `200/201` followed. The C133 initial summary was skipped; the C135 summary and C117 status-log span both completed; the broad focused proof catalog was skipped.

The first non-completing span was the C137 suite:

| Checkpoint | Observation |
|---|---|
| `C188R6-206 BRANCH C137-TEST=TAKEN` | Ordinary Notes startup entered the C137 proof-test branch. |
| `C188R6-207/208` | Runtime-settings capture completed. |
| `C188R6-209/210` | Temporary defaults commit completed. |
| `C188R6-211 BEFORE C137-SUITE` | Emitted immediately before the suite call. |
| `C188R6-212 AFTER C137-SUITE` | First expected missing checkpoint. |

The validator therefore records `lastExpectedCheckpointCompleted=C188R6-211 BEFORE C137-SUITE` and `firstExpectedMissingCheckpoint=after C137-SUITE`. The following restore, Notes result calculation, `ManagedNotes.Launch` return, application-dispatch return, and native `[C102-LAUNCH-RETURN]` markers were not reached.

The source explains why the suite is incorrectly selected. In `samples/managed/HostLogProof/Applications/ManagedNotes.cs`, `_c152Enabled` is set at line 771; `_c151ProofContext` includes that shared C152 setup state at lines 776–782. The real `c147-native-notes` launch also makes `_c147RuntimeConsumerContext` true at line 785, with shared C151 setup retained at lines 825–830. Later, line 1357 merges `_c151ProofContext` into `_c137ProofContext`. The branch at lines 3285–3292 runs C137 whenever that merged context is true, C138 is false, and `_c137TestsRun` is false. The candidate emitted `C137-TEST=TAKEN`, confirming this branch at runtime on the ordinary Notes route.

At line 3321, `ManagedNotes.Launch` calls `GuideXosMouseWheelC137Tests.Run(host)`. The helper is a 46-case qualification suite (`GuideXosMouseWheelC137Tests.cs:20–73`); its `GuideXosHost` argument is unused by the suite body. The runtime did not emit the immediate after-call marker at line 3323, and the QMP status remained `running` at the 120-second bound. This is a concrete startup failure mechanism: shared Notes setup accidentally authorizes proof-only C137 work during ordinary Notes startup. The precise non-returning inner test operation is not isolated, and the synchronous `host.TryLog` that emits marker 212 remains a possible adjacent stall point. The report does not attribute the failure to `RenderMain`.

### Readiness, QMP, and release

The Notes surface was created for app instance `id=4`, generation `1`, and `C150-SURFACE` reported success. The captured final framebuffer (`runs/diag-002/final-stage.ppm`; PNG preview at `runs/diag-002/final-stage.png`) shows only the desktop shell and welcome notification; no Notes window is visible. The input manager was not initialized and the kernel never reached `Entering main loop (waiting for input)`. Accordingly, the runner sent no pointer or keyboard input, and Notes interaction/responsiveness is unverified.

The runner's QMP log records `query-status` returning `running`, a successful final `screendump`, and the runner's own `quit`. No QMP events were recorded. QAC logged the guest exit with code `0`, then released reservation `708517e1010242c5bd47c55eadfd85d6`. The post-run census recorded zero QEMU processes, reservations, or managed sessions; `qacReleaseVerified=true`. Exit code zero is not guest readiness evidence.

The complete raw serial, ordered validator output, QMP log, screenshot, QAC records, candidate hashes, and process command line are retained under `out/dotnet/c188r6-notes-postrender/runs/diag-002/`. Key hashes: serial `3FB19BDEABF8F30C367D97A3FEE00214D220FB9CB7780FCD1A5D93F3EB3EDF60`; QMP log `497E2EABA17B5999DCCB12A82816A3B3FCC3BDAA3198405442E8A8A7912C7F6D`; framebuffer PPM `85CC26C7C696CCEF6CEF9A364F48DA06C193319569FD1D42AE3CC302F25C86B5`.

### Preservation, outcome, and next scope

The fresh baseline and post-guest comparison recorded 13 protected roots / 8,497 files and 136 pre-existing worktree paths. All protected file paths, sizes, and hashes matched; all 136 original status paths remained byte-identical. The source `ESP/ramdisk.img` still hashes to `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8`. The pre-existing C185 limitation concerning 212 generated files whose earlier contents cannot be verified remains unresolved. The comparison is `runs/diag-002/preservation-comparison.json`.

**Current C188R6R1 result: Outcome A — the post-render failure mechanism was identified.** Ordinary Notes startup accidentally selects the C137 qualification suite through the C151/C152 shared-context merge, then fails to confirm suite completion within the bounded guest run. The inner suite operation is still unisolated, and Notes launch/input-loop readiness remains unverified.

Recommended C188R7 scope: make the smallest predicate correction that separates C137 test authorization from C151/C152 setup state while preserving shared Notes initialization. Add a focused regression proving `c147-native-notes`, `c151-notes`, and `c152-notes` keep required setup but skip C137, while explicit `c137-*` contexts still run it. Then run one bounded confirmation guest and inspect whether launch return and input-loop readiness follow; add per-case C137 checkpoints only if the same span remains incomplete. Keep the full six-scenario C188 campaign and C189 separate. Do not begin either automatically.
