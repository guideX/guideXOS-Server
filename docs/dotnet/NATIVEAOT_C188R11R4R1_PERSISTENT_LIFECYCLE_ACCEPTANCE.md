# NativeAOT C188R11R4R1 — Persistent Managed Lifecycle Acceptance

**Date:** 2026-10-26
**Branch:** `v1.1_DOTNET_SUPPORT`
**Starting HEAD:** `43c5e3e04d7f25dde85a2355f556cffa7579a6ee`
**Ending audited implementation HEAD:** `a813779ad7794f5eac053aa8be49a0be01fd64c6`
**Outcome:** **D — QAC_OR_EXECUTION_BLOCKED**

## Result

The existing R4 correction and the diagnostic and proof-disabled production artifacts remain qualified. The hosted acceptance gate now checks a current QAC reservation inspection, status census, current run token, owning runner, and QEMU child identity. Its positive and negative cases pass.

The first immediate QAC preflight found an unmanaged/foreign QEMU, so I left it running and waited for it to exit naturally. A fresh preflight then passed and exactly one QAC-authorized guest boot started. The guest reached Notes launch and main-loop focus, but the during-run QAC `list` did not satisfy the new parser. The runner failed closed, sent no key, and shut down the owned guest cleanly. No second guest was started. The actual keyboard callback and Notes text editing remain unqualified.

## Baseline and preservation

- The requested branch and starting HEAD were verified. No reset, restore, clean, stash, history rewrite, or branch switch was performed.
- `ESP/ramdisk.img` was `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8` before and after qualification. It remains an unrelated pre-existing modification and is excluded from both commits.
- Pre-existing generated/untracked sample paths were preserved. New hosted logs and the QAC preflight snapshot are under `out/dotnet/c188r11-managed-state-provenance/` and are not staged.
- No NativeAOT runtime source or managed registry code was changed in this phase.

## Lifecycle correction and artifact reuse

Both C188 builders enable the existing `PersistentCompositeLifecycle` option: `scripts/dotnet/build-c188-nativeaot-candidate.ps1` and `scripts/dotnet/build-c188-production-nativeaot.ps1`. The diagnostic and production runtime-pack manifests report:

- `managedHeapConfiguration=Primary64KiB`
- `managedHeapBytes=65536`
- `allocationStrategy=bounded-static-image-backed-no-collection`
- `persistentCompositeLifecycle=true`

The current runtime source hash is `C47A6DF47EC42EFFE820E97F18D1832C4CD43F80DF2060DF4B6D1EF084544224`, matching both runtime-pack manifests. In `tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_platform.cpp`, `RhpReversePInvoke` calls `initializeRuntimeState`. With the persistent lifecycle define, initialization zeros the 64 KiB arena and sets its allocation bounds only while `g_persistentManagedHeapInitialized` is false. Later entries increment the preservation counter and retain the heap and allocation cursor. The managed heap stays rooted by the continuing NativeAOT thread. This corrects the R4 reset mechanism without adding a heap, allocator, registry workaround, or runtime redesign.

The existing R4 artifacts were reused:

| Artifact | Result | SHA-256 |
|---|---|---|
| Diagnostic NativeAOT ELF | PASS | `15ABAAC80ED7C306D54572A701D6075D237D14C4F4C1AC5B04636820C376809D` |
| Diagnostic kernel | PASS | `0032BD297D86314D86B182C8CFDC7A15E3C12FE07BDEE5F192A2BFFD78403B93` |
| R4 staged candidate ramdisk | PASS | `3E9FC2B3757EF4BA7D528E7B56AF60CB4CE66CBAB249A4808D270514B543B703` |
| Staged managed ELF | PASS | `15ABAAC80ED7C306D54572A701D6075D237D14C4F4C1AC5B04636820C376809D` |
| Proof-disabled production NativeAOT ELF | PASS | `F8F7B89F6E79A242A96215F2164C1D290D0A8810EE3735B7E9045D1D14069732` |
| Production kernel | PASS | `2BFCB9D3BEDEA1C4C161B5978EC1A71B33BCF11FAC846815B6D9EE49986465BF` |

The R4 staging manifest reports 185 unrelated files preserved and zero other files changed. The candidate kernel, staged application, and ramdisk match their qualification manifests. No rebuild was required.

## Prior managed-state evidence

R3 showed why stable logical identity was insufficient: between launch return and first managed input, the descriptor array's length changed from 5 to 0, the lifetime pool's `_entries` became null, and the Notes resident mask changed from 1 to 0. `TryFind(4)` then returned `-4`.

The existing R4 serial log (`runs/r4-repair-qualification-001/guest-001/serial.log`, SHA-256 `D90595ADB57ACDBD0018F45C521E17297AA407AA2C30F4C9151DBCB3B529BF34`) shows the corrected startup path:

- Heap initialization count 1, followed by a preserve event with reset count still 1.
- At launch return, registry count `n=5`, Notes resident mask `m=1`, generation `g=1`, and result `0`.
- `TryFind(4)` succeeded with `n=5`; Notes reached its normal main-loop focus record.

That R4 run stopped before input because the prior runner could not reconcile QAC `status` and `list`. It contains no first-input observation or Notes keyboard callback and does not qualify this phase's requested callback boundary.

## QAC semantics and ownership gate

QAC source (`D:\dev\guideXOS_QAC\src\GuideXOS.QAC.Cli\Program.cs` and `D:\dev\guideXOS_QAC\src\GuideXOS.QAC.Coordinator\CoordinatorServer.cs`) shows that:

- `status` summarizes the QEMU process count/foreign count and managed-session, queue, and blocked counts.
- `list` prints the process census, reservation rows, managed-session rows, and census warnings.
- `inspect <reservation-id>` reads one identified reservation and returns its owner, state, owner liveness, child PID/start time/path, and degradation state.
- `status` and `list` each request a fresh snapshot. An empty `list` is not by itself proof that a running session is absent or harmless; contradictory rows, warnings, foreign ownership, or mismatched identities fail closed.

The R4 runner snapshot had `status` reporting one non-foreign QEMU and one managed session while `list` showed zero reservations and zero managed sessions. The updated R11R4 gate permits an empty list only when fresh status and `inspect` evidence independently identify the same running acceptance reservation, and local process evidence confirms the QAC owner is the current runner's direct child and the inspected QEMU is that owner's live direct child with matching PID, start time, and executable path. It also requires a fresh run ID and exact evidence-directory match. Any nonempty contradictory list fails closed.

Focused hosted coverage includes matching empty/full enumerations and negative cases for contradictory identity, foreign census, released/stale reservation, missing verified owner, wrong child parent, contradictory list rows, and missing child identity.

## Hosted validation

The final aggregate at `hosted/persistent-lifecycle-acceptance-006/manifest.json` is **PASS**. It records restore/build exit code 0 and:

| Suite | Cases | Result |
|---|---:|---|
| R9 managed registry | 14 | PASS |
| R10 state reentry | 14 | PASS |
| R11 state provenance | 15 | PASS |
| R11 runner predicates | 30 | PASS |
| Output bounds | 6 | PASS |
| Runtime lifecycle | 7 | PASS |
| QAC ownership predicates | 10 | PASS |
| Live QAC stream capture | 2 | PASS |
| Keyboard contracts | 19 | PASS |
| R2 runner predicates | 9 | PASS |

The managed UI aggregate also passed **38 suites / 930 cases** at `hosted/persistent-lifecycle-acceptance-001/managed-ui/qualification.log`. Diagnostic and proof-disabled production qualification manifests both report PASS. `git diff --check` passed; Git emitted only its line-ending normalization notices.

## Guest preflight, boot, and outcome

The first read-only census is preserved in `preflight/persistent-lifecycle-acceptance-qac-blocked-001/`. At `2026-10-26T23:30:05Z`, QAC reported `QEMU processes: 1 (1 foreign)`. Its list identified PID `28728` as `unmanaged/foreign` at `C:\Program Files\qemu\qemu-system-x86_64.exe` and warned that acceptance reservations were blocked. The local process record matched that PID and executable. I did not terminate or alter it. It exited on its own before the next preflight.

The fresh exclusive preflight at `2026-10-26T23:35:16Z` is preserved in `preflight/persistent-lifecycle-acceptance-qac-exclusive-003/`. QAC and the local process census both showed zero QEMU processes, zero foreign processes, zero reservations, and zero managed sessions. Exactly one QAC reservation was then granted:

- Reservation: `683985dcd021477cbb0a1bf6419e2b1d`
- QAC owner process: PID `10196`
- QEMU child: PID `23808`, `C:\Program Files\qemu\qemu-system-x86_64.exe`
- Child start: `2026-10-26T23:35:59.3491744Z`
- QAC exit: code `0`; reservation later inspected as `Released`, owner liveness `Verified`, and `degraded=false`
- Post-run QAC status/list and local QEMU census: empty

A later read-only poll briefly observed a different unmanaged/foreign PID `15932` (parent PID `26776`, start `2026-10-26T23:38:16.9445773Z`). It was not this run's QEMU child and was left untouched. The subsequent QAC snapshot recorded at `2026-10-26T23:40:07Z` again showed zero QEMU processes, reservations, and managed sessions; the local QEMU process census was empty at the `23:40:40Z` recheck. The transient observation is recorded in `preflight/persistent-lifecycle-acceptance-qac-postrun-transient-foreign.json`.

The guest initialized the persistent arena once (`base=00000001001F1600`, `bytes=65536`) and logged a preserve event with reset count still 1. Its launch-side state reached registry count `n=5`, successful `TryFind(4)` (`result=0`), valid pool backing identity (`ps=1`), Notes resident mask `m=1`, and generation `g=1`. It then reached the focused Managed Notes main loop. The serial log is `runs/persistent-lifecycle-acceptance-001/guest-001/serial.log` (SHA-256 `3B368F846240966DD0860D4DC779115AAD9A0251A12CB317CF4EBB732E15686B`). These launch observations agree with the corrected R4 startup evidence.

Before any input, the ownership predicate rejected the during-run QAC list with `qac-list-qemu-census-unparseable` and `qac-list-partial-census-contradicts-independent-ownership`. The outer runner exited with code 1 before its input gate. QAC stderr records the reservation and child launch/exit/release; `qac-post-run-reconciliation.inspect.json` records the released reservation and verified owner. The QMP log contains only a `quit` command. No `a` transaction was sent, no first-input snapshot or Notes callback was recorded, and no before/after screenshots were captured. Text insertion is untested.

The original runner wrote the during-run status/list/inspection files only after the ownership check, so it did not preserve the exact list text that triggered rejection. A closeout follow-up changes the runner to save those raw snapshots and the rejection evidence before it can fail closed; the hosted contract checks that ordering. This does not change or reinterpret the already completed guest run.

**Outcome D — QAC_OR_EXECUTION_BLOCKED.** The arena and registry survived through launch and Notes became resident, but the QAC ownership reconciliation did not complete, so the first keyboard callback was not attempted. The one-boot limit has been used. Any further callback qualification requires a separately authorized phase with the snapshot-preserving gate and fresh exclusive QAC admission.

## Git disposition

The implementation and ownership-gate changes are committed locally:

- `034b070f17dd4b58f943d50e4835cc3d710fef84` — inspected QAC ownership and live process capture.
- `7036b931` — reject partial QAC list censuses.
- `a813779ad7794f5eac053aa8be49a0be01fd64c6` — preserve gate evidence before a fail-closed result.

This acceptance report is the closeout documentation commit. The pre-existing ramdisk modification and unrelated untracked sample outputs remain unstaged and uncommitted. No remote push was attempted under the existing C188 phase policy.

## Authorized continuation from the actual workspace HEAD

The follow-up request arrived with the worktree at branch `v1.1_DOTNET_SUPPORT`, actual starting HEAD `c5ae6286772cd73aa636271d7d3a5ae8db97e94c`. The R4R1 report above already records one earlier guest attempt. This continuation used one additional QAC-authorized boot, then stopped without retrying when the input gate failed. The ownership-parser and hosted-test changes are at implementation HEAD `ba9e47e2d70e8f1138517eff9853236238d228d0` before this report update; the final closeout documentation is committed separately afterward.

### QAC command semantics and ownership predicate

QAC2's README documents `list` as the reservation and process-census view and `inspect <reservation-id>` as the read for one reservation. In the QAC coordinator source, `status` prints aggregate QEMU/foreign, managed-session, queued, and blocked counts. `list` prints QEMU process rows, active reservation rows, managed-session rows, and census warnings. Both commands request fresh snapshots; within any one coordinator snapshot, reservation and managed-session rows are derived from the same active reservation set. The commands do not provide an atomic cross-command snapshot, so a transition between separate calls can produce different results. A stable running session should appear in both `list` enumerations; an empty enumeration is not by itself evidence that a current run is authorized.

The corrected predicate accepts an omitted or empty `list` enumeration only when independent current-run proof agrees across the requested reservation ID, QAC `inspect`, the exclusive `status` census, the owning QAC runner process, its verified direct QEMU child (PID, start time, and executable), and the run-specific evidence path/token. It records list omissions and status/list count disagreements in the ownership evidence. Any malformed row, conflicting identity, warning, foreign process, degraded/stale reservation, or failed process-ownership check still rejects the run. This handles empty list output through correlated evidence rather than treating the mismatch as harmless.

The single guest run in this continuation showed a separate parser defect: QAC emitted normal Windows CRLF process rows, while the gate's process-row regular expression rejected the trailing carriage return. The gate therefore failed closed with only `qac-list-qemu-census-unparseable` even though the saved status, reservation row, managed-session row, process row, inspection, and checked local process identities agreed. The process-row parser now accepts CRLF, and the focused tests cover complete, empty, and partial lists; CRLF output; contradictory reservation/session/child identities; foreign census; stale/released session; missing owner; and missing or mis-parented child. QAC ownership predicate coverage is **13 passing cases**.

### Lifecycle settings and reused artifacts

Both diagnostic and proof-disabled production runtime-pack manifests report `persistentCompositeLifecycle=true`, a 65,536-byte `Primary64KiB` heap, and `bounded-static-image-backed-no-collection`. Both use runtime source SHA-256 `C47A6DF47EC42EFFE820E97F18D1832C4CD43F80DF2060DF4B6D1EF084544224`. The existing `RhpReversePInvoke` path still calls `initializeRuntimeState`; with the persistent define it zeros and initializes the arena only on first attachment, and later entries preserve the existing arena and TLS allocation cursor. The registry and lifetime-pool roots remain ordinary NativeAOT static roots. No new heap, allocator, runtime change, registry re-registration, or application-model change was introduced.

The qualified R4 artifacts were reused without rebuilding:

| Artifact | Qualification | SHA-256 |
|---|---|---|
| Diagnostic NativeAOT ELF | PASS | `15ABAAC80ED7C306D54572A701D6075D237D14C4F4C1AC5B04636820C376809D` |
| Diagnostic kernel | PASS | `0032BD297D86314D86B182C8CFDC7A15E3C12FE07BDEE5F192A2BFFD78403B93` |
| R4 staged diagnostic ramdisk | PASS | `3E9FC2B3757EF4BA7D528E7B56AF60CB4CE66CBAB249A4808D270514B543B703` |
| Proof-disabled production NativeAOT ELF | PASS | `F8F7B89F6E79A242A96215F2164C1D290D0A8810EE3735B7E9045D1D14069732` |
| Production kernel | PASS | `2BFCB9D3BEDEA1C4C161B5978EC1A71B33BCF11FAC846815B6D9EE49986465BF` |

The diagnostic and production qualification manifests both report PASS and persistent lifecycle enabled. The R4 staging manifest reports PASS, 185 unrelated files preserved, and zero other files changed. Its staged managed ELF hash matches the diagnostic candidate.

### Hosted validation for this continuation

The final C188 aggregate is PASS at `out/dotnet/c188r11-managed-state-provenance/hosted/persistent-lifecycle-acceptance-009/manifest.json` (SHA-256 `E5F96F9B5CB62AACA9B9CD978499C0A21A7BC17903EEA202CA193E14C47A02F5`). It recorded restore/build exit code 0 and:

| Suite | Cases | Result |
|---|---:|---|
| R9 managed registry | 14 | PASS |
| R10 state reentry | 14 | PASS |
| R11 state provenance | 15 | PASS |
| R11 runner predicates | 30 | PASS |
| Output bounds | 6 | PASS |
| Persistent runtime lifecycle | 7 | PASS |
| QAC ownership predicates | 13 | PASS |
| QAC stream capture | 2 | PASS |
| Keyboard contracts | 19 | PASS |
| Keyboard runner predicates | 9 | PASS |

The managed UI runner's 38 focused suites / 930 cases passed, including Notes resize, navigation, and newline/save suites. Its top-level manifest is marked FAIL because a separate C167/C168 child invocation under Windows PowerShell converted `dotnet.exe` stderr into a `NativeCommandError`; the 38-suite summary itself is PASS. I reran the Settings host directly under PowerShell Core: C167 passed 20 focused cases plus 60 stress iterations, and C168 passed 31 cases, 250 page cycles, 250 selection changes, 100 apply/reset cycles, and 250 popup cycles. Its PASS manifest is `out/dotnet/c188r11-managed-state-provenance/hosted/persistent-lifecycle-acceptance-007/c168-settings-core-validation-002/c168-settings-ui-host-manifest.json` (SHA-256 `52F519F62C64285552FE4C250085537E56A406B586DF7C457C6ADDF654E0F56E`).

### One-boot QAC result

The first read-only preflight found foreign QEMU PID `30252` from a PacMan hosted-soak command line. It was left untouched and exited naturally. The fresh recheck at `2026-10-27T01:18:44Z` showed QAC Online with zero QEMU, foreign processes, reservations, managed sessions, queued requests, and blocked requests; the local QEMU census was also empty. Evidence is under `out/dotnet/c188r11-managed-state-provenance/preflight/persistent-lifecycle-acceptance-rerun-001/`.

Exactly one QAC acceptance reservation then launched the corrected R4 diagnostic candidate:

- Run identity: `e4ffb5c8cfed4a71ad0d16fdfb21a7af`
- Reservation: `11a8812d43b84abd938073751186cfbb`
- QAC owner process: PID `24388`, owner liveness `Verified`
- QEMU child: PID `32760`, `C:\Program Files\qemu\qemu-system-x86_64.exe`, start `2026-10-27T01:19:00.9702288Z`
- During-run QAC status: one QEMU, zero foreign, one managed session, zero queued/blocked
- During-run list and `inspect`: matching reservation/session/child, `Running`, `degraded=false`

The gate stopped before the screenshot/input stage because of the CRLF parser defect. The QAC stderr log records child exit code 0 and reservation release. Post-run inspection reports the exact reservation as `Released`, owner liveness `Verified`, and `degraded=false`; QEMU PID `32760` is absent locally. A different unmanaged QEMU PID `23668` appeared in the subsequent post-run census and was left running. Its presence does not change the verified exit/release of this run. The QMP log contains only `quit`; no keyboard event was sent, and no before/after screenshot or keyboard acceptance manifest was produced.

Key retained evidence:

| Evidence | SHA-256 |
|---|---|
| Guest serial log | `C71FC94A54A42135C471904732FF1E42DFD10AE4BDE073FB99B6AF850B09B3AD` |
| QAC stderr lifecycle log | `5028C66D1F94E2ABE391E24B3C43B0265D7AA5DE4913BF59A14AE8AAAD337930` |
| During-run QAC list | `F932510ADBB53E74131F9503E083E036B149308042C443D67C7250454AFD357D` |
| During-run QAC inspection | `347DC85EB2B9F1A18ED9E8AE67BA3F2D63F91972EA843FB5ECE4C52D2C12B5CE` |
| QMP log | `53C53FAC0C21C25D3CEB83E2955A5FE8657502E8BFDC172D9DA7B9558CA81CA0` |
| Post-run QAC reconciliation | `8880281AB954F1D553337B198641976074195FA5CCC3EDA641342F01E828AF39` |

### Managed state and Notes result

The guest log at `runs/persistent-lifecycle-acceptance-002/guest-001/serial.log` recorded:

| Checkpoint | Observation |
|---|---|
| Heap initialization | One initialization at `0x00000001001F1600`, size `0x10000` (65,536 bytes) |
| Later reverse entry | `action=preserve`, reset count remains 1, allocation cursor preserved |
| Registry initialization | Five selectors 4–8; `re=1`, `pe=1`, `rg=1` |
| Physical storage snapshots | Registry array `0x00000001001F4F50`; pool owner `0x00000001001F51E0`; `_entries` slot `0x00000001001F51E8`; pool array `0x00000001001F5218`. These addresses remained stable from registry initialization through Notes instance start and lie inside the initialized 64 KiB arena. |
| Lookup | `TryFind(4)` returned `0` with descriptor count 5 |
| Notes residency | At instance start and managed return: resident mask `m=1`, owner/pool generation `1`, pool backing present, held array length 5 |
| Keyboard dispatch | Not attempted; no input-dispatch snapshot or callback result |

This is stronger than relying on the stable logical identity token alone: the launch snapshots include physical object/storage addresses and the runtime's explicit preserve event. It still does not establish their state at a keyboard boundary. R3's first input snapshot had descriptor count 0, null pool backing, resident mask 0, and `TryFind(4)=-4`. R4's corrected boot established `n=5`, `m=1`, and successful lookup at launch but stopped before input. This continuation reached the same healthy launch state and focused Notes main loop, then stopped before input because the gate parser failed.

Notes received no character in this run. Its keyboard callback was not invoked, no callback return or text-buffer delta exists, and visible text insertion remains unqualified. No screenshot was captured.

The source `ESP/ramdisk.img` SHA-256 was `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8` before and after this continuation. It remains the pre-existing modification and is excluded from the implementation and report commits. Generated/untracked sample paths are also preserved.

**At the first closeout, the outcome was D — QAC_OR_EXECUTION_BLOCKED.** The lifecycle correction was active and launch-side state was healthy, but the first run stopped at a CRLF ownership-parser defect before keyboard dispatch. The parser was corrected and its hosted regressions passed. The following user-authorized retry is recorded separately below.

The implementation changes are committed as `ba9e47e2d70e8f1138517eff9853236238d228d0` (QAC ownership reconciliation). This report is the subsequent documentation-only closeout commit. No remote push was attempted under the existing C188 phase policy.

### User-authorized QEMU-empty retry

Starting branch was `v1.1_DOTNET_SUPPORT` at HEAD `e65f4e479a233d0de0fc708aa24f39c926f2937d`. Ending source HEAD at guest shutdown, before this documentation-only closeout, was the same commit. The preflight at `2026-10-27T02:27:13Z` found QAC Online with zero QEMU processes, foreign processes, reservations, managed sessions, queued requests, or blocked requests; the local QEMU census was empty. The source `ESP/ramdisk.img` hash was `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8` both before and after the run.

The validated R4 diagnostic candidate was reused. Exactly one guest boot launched under QAC:

| Evidence | Observation |
|---|---|
| Run identity | `c67056d2b2c04ef790678815c4071b53` |
| QAC reservation | `b5b9a0dfa1974dd68db3f14aed789289` |
| QAC owner process | PID `22128`; QAC inspection reported `ownerLiveness=Verified` |
| Runner process | PID `28028`; launch snapshot recorded the owner as its child |
| QEMU child | PID `18156`, `C:\Program Files\qemu\qemu-system-x86_64.exe` |
| During-run status/list | One QEMU, zero foreign; one managed session; one reservation, session and QEMU row all matched the reservation and child IDs |
| During-run inspection | Same reservation, `Running`, `degraded=false`, owner verified, matching child PID/start time/executable |
| Launch ownership predicate | Accepted with no reasons |
| Input ownership predicate | Failed closed with `reservation-owner-does-not-match-current-runner-process`; no key event was sent |
| Shutdown and release | Owned QEMU exit code 0; reservation state `Released`, `degraded=false`; post-run QAC status/list show zero processes and reservations; local QEMU census is empty |

The status/list/inspect interfaces and QAC process row agreed at both the in-run observation and the input gate. The final local owner/runner identity comparison still failed, despite the launch-time process-chain snapshot. The saved gate artifact reports the combined owner/runner predicate reason but does not retain each refreshed local comparison value, so the specific disagreeing field cannot be isolated after shutdown. The gate correctly failed closed; the observed agreement in QAC enumeration alone does not authorize relaxing the required local process-ownership proof. No second guest was started.

The guest reached the corrected launch state and Managed Notes main loop before the input gate:

| Checkpoint | Observation |
|---|---|
| Persistent heap | One 65,536-byte initialization and one later `action=preserve` marker |
| Registry | Five descriptors, selectors 4–8; launch `TryFind(4)` succeeded |
| Physical storage | Registry array `0x00000001001F4F50`; pool owner `0x00000001001F51E0`; `_entries` slot `0x00000001001F51E8`; pool array `0x00000001001F5218` |
| Notes residency | Selector 4; resident mask `m=1`; owner/pool generation 1; pool backing present; held array length 5 |
| Main loop and focus | Normal main-loop readiness; compositor reported Managed Notes focused |
| Input transition | Not attempted; no `input-dispatch-entry` snapshot, lookup result at input, callback marker, or callback return |

The only screenshot is the launch/readiness image [desktop-ready.png](../../out/dotnet/c188r11-managed-state-provenance/runs/persistent-lifecycle-acceptance-003/guest-001/desktop-ready.png). It shows the Managed Notes window open and focused before any input. The ownership failure occurred before the runner's explicit `before-input` capture and key commands. There is no after-input screenshot, delivered character, Notes text-buffer delta, or callback result; visible text insertion remains unqualified. The screenshot confirms the editor's launch presentation only.

Key retry artifact hashes:

| Artifact | SHA-256 |
|---|---|
| Acceptance manifest | `60CFB9D5FD3D60A1C318435F44F576187934F7D6A9E695D531070AD39CE94721` |
| Guest serial log | `17163F3765762BCF1639AACE7B7E13FACEDD3C155234E2BC88F23D24761563F8` |
| Launch ownership proof | `C30280C4864CE7B70D532BBA3DCAFF01367CB6422F8D5AF119F6B37180723587` |
| Input ownership rejection | `3DB3C6013BFDD19143AA8B7F3A5C6595E7E66FB66B4118B991ADD107C5D903A5` |
| Input QAC inspection | `CBE6353913A1B479605EE1D9693B1044B49D3246999105F3E4E195B212D7A10F` |
| QAC stderr lifecycle log | `02C2C1982B9E2463E7B2036DED058FD2B676E8AA807C6F48F6767F57D2DDE7CE` |
| QMP control log | `731950772C2B759F505801E06156248AD731E6AFEC093BA3054DC605993874C8` |
| Launch screenshot (PPM) | `D4D620FF9C211F849A8827ED054B7B54CCEDA701B1B2BE26EB43410F5E9782C0` |
| Display copy (PNG) | `D6EF0C500A19273BB2375AB73ABA8F0A8483D34F92BE1A397B2F76CEED13039B` |

**Current outcome: D — QAC_OR_EXECUTION_BLOCKED.** Persistent heap and resident Notes state are demonstrated through launch and lookup, but the one retry failed the required fresh local ownership correlation at the keyboard gate. No key was sent, so heap survival through a real keyboard callback, Notes callback execution, and text insertion remain unqualified. The smallest remaining step is to retain each refreshed local owner/runner/child identity field in the gate diagnostics, add a hosted regression for the observed relationship, and seek authorization for a new single guest acceptance run only after the ownership predicate can prove the current runner chain. The existing hosted aggregate remains PASS with the counts above; no implementation or NativeAOT runtime change was made in this retry.
