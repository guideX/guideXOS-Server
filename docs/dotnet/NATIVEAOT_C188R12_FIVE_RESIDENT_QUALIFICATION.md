# C188R12 Five-Resident Managed Application Qualification

**Outcome: `INTEGRATION_DEFECT_LOCALIZED` — qualification stopped at the Notes-to-Settings surface transition.**

The hosted gates and diagnostic/production NativeAOT build qualifications passed. The one authorized QAC guest boot confirmed five registered descriptors, Notes selector lookup, and persistent managed heap/storage through Notes callbacks and the Settings launch attempt. The Notes Settings action closed Notes and logged a Settings AppModel launch, but no Settings surface was created. The guest was stopped at that boundary. The other four app operations, five-resident coexistence, revisit checks, and the post-transition Notes edit were not run.

## Baseline and preservation

- Branch: `v1.1_DOTNET_SUPPORT`.
- Starting HEAD and end of guest qualification: `02fa8dcce8bffd530dc4742b635f7a5ff451e5c3`. The closeout commit adds this report only.
- The pre-existing modified `ESP/ramdisk.img` remains unchanged at SHA-256 `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8`.
- Existing generated and untracked `bin`, `obj`, and smoke artifacts were preserved and excluded from the commit. No reset, clean, stash, branch switch, source ramdisk write, or source implementation change was made.
- No C188R12 report or guest-run root existed at baseline. Hosted outputs used isolated C188R12 paths; the diagnostic guest was staged and executed under the existing ignored C188R11 evidence tree without overwriting prior guest evidence.

## Registry and residency contract

The production identities and selectors are in `built_in_app_metadata.h`; the diagnostic descriptor registry and static factories are in `samples/managed/HostLogProof/GuideXos/GuideXosApplication.cs`. There is no separate `.app.json` manifest for these five compiled NativeAOT built-ins. The built-in metadata, static registry, and the build/staging manifests define their identity.

`GuideXosManagedApplicationLifetime` has `Capacity = 5`. It retains one live application object per selector, reuses that selector's entry on same-selector replacement, preserves unrelated entries, clears an entry on teardown, and returns failure when no slot is available. Descriptor array order is separate from the dynamic resident-pool slot. C181 previously demonstrated five concurrent production surfaces and a Task Manager count of five; the R12 run did not repeat that coexistence sequence.

| Application / identity | Selector | Descriptor order | Planned pool slot | Launch and supported operation | Callback / success evidence | R12 guest result |
|---|---:|---:|---:|---|---|---|
| Managed Notes — `com.guidexos.apps.managed.notes` | 4 | 0 | 0 initially; 1 after Settings handoff | Boot launch; document pointer route followed by one ordinary `a` insertion | `C150-APP-LAUNCH`, `TryFind(4)=0`, C120 route and C188R11R4R2 before/after editor snapshots | Startup and selector lookup passed. Settings handoff closed Notes. No `a` was sent. |
| Managed Settings Center — `com.guidexos.apps.managed.settingscenter` | 5 | 1 | 0 after Notes closes | Notes Settings action (action ID 24 / `0x18`); successful surface creation is the operation | `C112-ACTION-DISPATCH` with managed return 0, Settings AppModel launch, then `C150-SURFACE action=create` | Action and AppModel launch were logged; no Settings surface-create marker followed. **Failed at this boundary.** |
| Managed File Explorer — `com.guidexos.apps.managed.fileexplorer` | 8 | 4 | 2 | Start Menu → All Programs; select a listed row and Refresh (`C163`, read-only `/system/apps` operation) | Normal launch or `C162` focus-existing marker; `C163-STATE` refresh result | Not attempted. |
| Managed Calculator — `com.guidexos.apps.managed.calculator` | 6 | 2 | 3 | Start Menu → All Programs; keyboard `7`, `Shift+8`, `8`, `Enter` | `C158-CALC-STATE display=56 phase=3 commands=4 result=PASS` | Not attempted. |
| Managed Task Manager — `com.guidexos.apps.managed.taskmanager` | 7 | 3 | 4 | Start Menu → All Programs; `Ctrl+R` refresh | `C161-TM-R` refresh with `n=5` and `ap=true` | Not attempted. |

The expected pool masks for the planned sequence use bits 0–4 in first-free slot order. Settings replaces/closes Notes in slot 0, then Notes reopens into slot 1:

| Planned stage | Expected resident mask | R12 evidence |
|---|---:|---|
| Boot Notes | `1` | Observed at Notes instance start and managed return. |
| Notes → Settings action, before Notes teardown | `1` | Observed in the action callback snapshots. |
| Notes teardown for Settings | `0` | Observed at `resident-clear-teardown`. |
| Settings resident | `1` | Expected after Settings factory start; no instance-start or surface-create evidence was produced. |
| Reopened Notes | `3` | Not reached. |
| Explorer | `7` | Not reached. |
| Calculator | `15` | Not reached. |
| Task Manager and revisit checks | `31` | Not reached. |

## NativeAOT runtime and build identity

The reused diagnostic and production runtime-pack manifests both report `persistentCompositeLifecycle=true`, `Primary64KiB` / 65,536 bytes, and `bounded-static-image-backed-no-collection`. Their `guidexos_nativeaot_platform.cpp` source hashes match the current source: `C47A6DF47EC42EFFE820E97F18D1832C4CD43F80DF2060DF4B6D1EF084544224`. No heap-size increase or rebuild was needed.

- Diagnostic build: PASS. The staged diagnostic app is `/system/apps/GXOSAPP.ELF`, SHA-256 `751F6A1934CD8FF5F690ABE1692BC3130CB2B05BBC1213888BF6C2EEB8ED34BE`; diagnostic kernel SHA-256 `0032BD297D86314D86B182C8CFDC7A15E3C12FE07BDEE5F192A2BFFD78403B93`.
- Production build: PASS. The proof-disabled image has selectors 4–8 and no CounterApp/C188 diagnostic route; ELF SHA-256 `7371BC8A1C4DB3552501E60F076EDBB5AA258BC17EDC323CD2713E04E4AD5838`.
- Staged diagnostic ramdisk SHA-256: `45A64BC1719AD2EA1C6B634D87F157C475EE187ED155FBE42BCCD8862019A507`. Its staging manifest reports 185 unrelated files preserved and `changedOtherFiles=0`. This is an isolated candidate image and does not replace the user's `ESP/ramdisk.img`.
- `EFI/BOOT/BOOTX64.EFI` SHA-256: `D72A35DA882892BA97356A1C006562ABA2908D9DD909152B96DCBB655D6C2D9B`.

The diagnostic guest was used for its existing C188R11 state evidence. This is not a production-compatibility guest run.

## Hosted qualification

All requested hosted gates ran before QEMU and passed:

- C188 provenance and predicate aggregate: **PASS** — R9 14, prior R10 14, R11 15, runner predicates 30, output bounds 6, runtime lifecycle 7, QAC ownership 15, QAC stream capture 2, keyboard contracts 27, keyboard runner predicates 24.
- C188 host integration: **PASS** — 25 cases; identity stress 1,000, valid resize stress 500, invalid geometry 250, stale generation 250, 100 lifecycle iterations, and zero resize-stress allocations.
- Prior host regressions: **PASS** — C182 15, C185 28, C186 15, C187 22.
- Managed UI aggregate: **PASS** — 38 suites / 932 cases, including C181 residency; its manifest also records C167 20 cases / 60 stress iterations and C168 31 cases / 250 page cycles, 250 selection changes, 100 apply/reset cycles, and 250 popup cycles.
- Standalone C167 and C168 reruns: **PASS** with the same counts. C168 emitted the existing `CS0162` unreachable-code warning at `Program.cs:2703`; the test result remained PASS, and warning policy was not changed.
- `git diff --check` and `git diff --cached --check`: PASS for the closeout report.

The earlier aggregate wrapper's Windows PowerShell native-command warning issue was not hidden. The current managed-UI manifest reports all three steps PASS, and C167/C168 were also run directly under PowerShell Core and passed. No compiler warning was suppressed.

The C188 host and prior-regression wrappers also rejected one initial output root outside their documented C188 phase directory before any tests ran. They were rerun under fresh paths inside the accepted phase root and passed; this validation rejection did not start QEMU or affect the later QAC run.

## Single guest plan and observed result

Before boot, the exact intended flow was: verify boot Notes and five descriptors; invoke Settings from Notes; reopen Notes; launch Explorer and refresh; launch Calculator and compute 56; launch Task Manager and refresh to five rows; focus existing Explorer, Calculator, and Notes; then perform one route-verified Notes `a` insertion. The planned operations were bounded to one guest session and used the existing C181/C188 input paths.

The session reached boot Notes and the Settings handoff only:

1. Boot reached the normal input loop at 1280×800. The identity registry reported production selectors 4–8 preserved. Notes launched as selector 4, generation 1. `TryFind(4)` returned 0; the registry trace reported five descriptors (`4,5,6,7,8`). Initial resident mask was `m=1`.
2. Notes state snapshots retained `n=5`, `gc=0`, and the same registry/pool storage addresses. The managed Notes Settings action returned success (`C112-ACTION-DISPATCH selector=4 action=00000018 managedReturn=00000000 result=PASS`). The Notes return target was armed, close was accepted, and the Notes surface was destroyed with the documented `close-or-replace` reason.
3. The AppModel then logged a normal Settings launch attempt, selector 5, generation 2, image `/system/apps/GXOSAPP.ELF`. **No** `C150-SURFACE action=create appId=com.guidexos.apps.managed.settingscenter` marker followed. The final screen capture still shows the Notes window. The runner acceptance manifest is `FAIL` because the planned managed-state/Notes-key scenario was stopped before the remaining input; QAC release itself is verified.
4. The guest sequence stopped at Settings. Explorer, Calculator, Task Manager, revisits, and the Notes editing regression were not attempted. No character key was sent and no Notes document edit is claimed.

The Settings action input trace contains two pointer clicks. The first agent-prepared coordinate `(875,538)` did invoke the Settings action; an initial notes file incorrectly characterized that click and is preserved with a correction record. A second click at the established C181 coordinate `(815,538)` was sent after the first AppModel launch attempt and before a Settings surface had been confirmed. The later capture still showed Notes. No key was sent and no Settings surface was observed. See `interaction-deviation.json` and `interaction-deviation-correction.json` in the run evidence. No further guest input was sent after the failure was identified.

## Heap, registry, and callback evidence

- The guest emitted exactly one persistent heap initialization: base `0x00000001001F2600`, size `0x0000000000010000` (65,536 bytes).
- It emitted 14 `action=preserve` markers through shutdown, each with `resetCount=1` and the same heap base. The allocation cursor advanced from `0x00000001001F2600` to `0x0000000100201008`. No destructive runtime reinitialization or GC marker was observed.
- C188R11 snapshots showed stable registry array `0x00000001001F5F50`, pool owner `0x00000001001F61E0`, pool entry-array field `0x00000001001F61E8`, and live/held pool storage `0x00000001001F6218`, with `heldLen=5` and `gc=0`. These values remained stable through the Notes action and its teardown.
- Notes lookup remained `TryFind(4)=0` before the action. Its expected close cleared the Notes entry and changed the observed pool mask from `1` to `0`. There is no R12 evidence of a Settings resident object after that clear, so the expected post-launch mask `1` is not claimed.
- The Notes action callback completed with result 0. The keyboard callback, C120 document focus transaction, single `a` insertion, text-buffer delta, and redraw were not exercised in this run. The prior `NOTES_TEXT_ENTRY_ACCEPTED` result remains the baseline; C188R12 did not rerun it.

## QAC ownership, shutdown, and raw evidence

QAC preflight was empty and the single acceptance reservation was `ac81f2121a454a3db48866f80980a06e` (run identity `5f747144a5cc4a2cad516962b9a01ff7`). Owner PID 14288, runner PID 17560, and QEMU child PID 16104 matched the verified QAC reservation and local parent/start-identity census. QAC reported one managed QEMU and zero foreign processes during the run. A fresh ownership gate passed immediately before the supported QMP `quit` to this guest. QAC recorded child exit code 0 and the reservation as `Released`; the final census is zero QEMU processes, reservations, sessions, and queued requests. No foreign process was touched and no retry was started.

One early agent-authored recheck helper produced a false negative after converting an expected process timestamp to a localized string. The runner's original ownership proof passed, the failed gate record was preserved, and the corrected timestamp-aware recheck passed before guest input. This was a gate-helper formatting error, not a QAC degraded-state observation.

Raw evidence is preserved under [`guest-001`](../../out/dotnet/c188r11-managed-state-provenance/runs/c188r12-five-resident-001/guest-001/):

- `serial.log`, `qmp.log`, and `qmp-control.log` retain the guest and input traces.
- `desktop-ready.png` and `settings-check.png` retain the before/after visual evidence.
- `qac-before.*`, `qac-during.*`, `qac-after.*`, `qac-gate-v2-*.json`, and `acceptance-manifest.json` retain QAC ownership and release evidence.
- `preflight.json` records the staged media hashes and planned resident masks.

## Classification and smallest next action

**Outcome C — `INTEGRATION_DEFECT_LOCALIZED`.** The five descriptors and Notes selector are valid, and persistent runtime/registry storage survived the attempted handoff. The localized failure is that the Notes-to-Settings AppModel launch logged selector 5 but did not produce a Settings managed surface. Full five-resident acceptance is not claimed; the other applications and post-transition Notes behavior remain unqualified.

The smallest next step is to trace the return from `desktop::launch_app_with_context` through Settings surface creation for this diagnostic composite, then add a focused hosted regression for the observed no-surface result. Any new guest qualification requires a separately authorized single boot after that boundary is fixed or explained.
