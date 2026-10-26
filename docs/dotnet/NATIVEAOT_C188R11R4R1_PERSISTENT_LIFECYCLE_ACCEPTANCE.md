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
