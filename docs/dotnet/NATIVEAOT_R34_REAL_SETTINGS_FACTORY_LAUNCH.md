# NativeAOT R34 — Real Settings Factory Launch

## Result

**Outcome D — `SETTINGS_LAUNCH_CAUSE_PROVEN`.** One authorized QAC boot reached successful NativeAOT initialization, generated `InitializeModules` completion, the stock reverse-P/Invoke return, Managed Notes rendering/focus, and the kernel input loop. The first R33 readiness gate accepted the bound two-descriptor R30 profile. A second read-only audit in the nested Settings runner still used the legacy five-descriptor default and rejected the valid `n=2` managed-return marker. The runner stopped before target validation or input submission. No Settings App Model request, selector-5 factory attempt, factory entry, construction, return, or Settings surface occurred.

The stale audit was corrected offline to receive the descriptor count from the candidate-bound readiness profile. A regression now exercises both the old default rejection and the profile-bound acceptance, and verifies that the actual nested runner passes the profile count. The corrected runner passes the offline no-QAC rehearsal. The consumed R34 guard was not reused and no second guest boot was attempted.

## Repository, provenance, and preservation

- Repository: `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT`
- Branch: `v1.1_DOTNET_SUPPORT`
- Starting HEAD: `725a0f343114d2b10e80f13ef9b60d40ed6f6233`
- R33 candidate binary source HEAD: `79f0036c6cdd1e48ee9e73cbfa03c072bc120375`
- R33 runner source HEAD used for the guest attempt: `2ea13dff512af06becbbd4b7a865d444b998bb39`
- R34 closeout HEAD: this report and the runner correction are committed together; the exact commit OID is recorded in the task closeout.
- No push was made.

The original candidate remains at:

`E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r33-settings-handoff\candidate-20261117T050157105Z-0fcf664c63df40e7ae3c8d3d759d5378`

Its candidate manifest SHA-256 is `787A46FFFF09C23355E585F6A7C02BC1ABA19429DED156BC64C19F28A6F510E2`; its integrity inventory SHA-256 is `72B913375CA2438C4FB3705D3B1F6AA514DBA0DDB1A46F5F964DEA44BBF89A94`. The inventory contained 349 entries, and all 349 hashes matched both before and after the guest run. The R25 manifest SHA-256 is `818D8724D900BE34A6EFD73160B00F9278A164BB05495925E070F37283F087DE` and binds this exact candidate root, manifest, and inventory.

The candidate preserves the accepted R30 NativeAOT Workstation GC profile, with no image-backed arena, the R20 unwind implementation, and the R32 ThreadStore attachment and stack-bounds fixes. The runtime-pack manifest SHA-256 is `D4AB05BAB6D2B8ED8859BF11B41A8B173B49C910D9C4D3FAA14AE44191C36FAD`. The linked candidate records the real `GuideXosApplicationRegistry` selector-5 `ManagedSettingsCenter` factory and constructor symbol `ManagedSettingsCenter___ctor_0`. The kernel hash is `9931827D7F6186FF98E1DA580A0B3ED2EAAF725359A7EC096F0ED16ADF495D6B`; the staged ramdisk hash is `055B9FDCFEAB8C65D8292F2008F11D6CC39B9134CC3C57B11AC9F30320701374`; the managed ELF hash is `14C61298B00665B9981C7FE177670C529C69B4F8AF2D9EA96BF4C53F888C40C8`.

At the time of the one-shot invocation, every R33 runner source input matched the source hashes recorded by the R33 sidecar. The nested Settings runner hash used for that boot was `F8DE2A50399204EE3B3C465E720578731B7051502D55AA8CC730EB6CDAF35718`. The offline R34 correction changes only that runner source; its new SHA-256 is `D79C5CCB90F68F348E7F53A110C77CAEE6AA13425E318A23684585B1A574B7BC`. The R33 candidate, R33 sidecar, binaries, and original provenance remain unchanged.

The three pre-existing modified object files remain untouched and retain their verified hashes:

| Protected object | SHA-256 |
| --- | --- |
| `c188r12r4r5r4r5r2r2_native_alloc_operator_new_test.obj` | `3507A24E07257422C98CAB4F7430E094A2C83DF93647E6A02621010DD585A2BD` |
| `c188r12r4r5r4r5r2r2_native_alloc_record_test.obj` | `605D6F7F066BAD3669B52C21A89722A7C9750F9C6595EDFDA26BC06E3125AEC3` |
| `guidexos_nativeaot_native_alloc_record.obj` | `2CD941199D5F34F81F72B0B718A8D1C1023CEAD831464724DEBFEA435F044183` |

## Readiness contract and R34 correction

The R33 profile permits two descriptors only for the exact R30 runtime identity/hash, Workstation GC, no image-backed arena, and candidate metadata binding selector 5 to `ManagedSettingsCenter` and its constructor. The live managed-return state identifies selector 4 as Managed Notes; the compiled candidate's linked selector-5 identity is verified by the preserved R30 qualification. This profile remains scoped to this candidate family; legacy candidates retain the five-descriptor expectation.

The outer R33 gate accepted the live `n=2` state. After that gate, `run-c188r12r1-settings-handoff.ps1` called `Get-C188R11ReadinessFailureReason` without its optional count argument. That helper therefore used its legacy default of five and returned `managed-return-state-missing-or-invalid` against the exact same live serial log. Offline replay after the correction produced:

- Legacy default-five audit: `managed-return-state-missing-or-invalid`.
- Profile-bound two-descriptor audit: no failure.
- R33 click gate on the captured live log: `True`.

The fix passes `readinessProfile.expectedDescriptorCount` into the second audit. `test-c188r33-settings-handoff-predicates.ps1` now asserts that the old default still rejects the R30 two-descriptor fixture, the profile-bound call accepts it, and the nested runner actually passes its candidate profile count. The R33 readiness suite passes 19 assertions.

## Production Settings activation path

The legitimate activation is the **Settings button in the Managed Notes window**. The candidate renders it as the source-defined button at `(430, 292)` within the Notes client area, 90×28 pixels. The native Notes surface in the guest was at `(340, 220)`, size 600×360, on a 1280×800 display. The source-derived button center is desktop coordinate **`(815, 550)`**. The preserved `desktop-ready.ppm` visibly shows Managed Notes and this Settings button; the captured native surface state reports visible, focused, and frontmost.

The button sends selector 4/action 24 to the existing Managed Notes action handler. After a successful managed return, native `onWidgetClick` calls `launchSettingsCenterFromNotes`. That path validates the Notes and Settings app identities and selectors, arms the existing Notes return target, closes Notes, and calls `desktop::launch_app_with_context` for the Settings app. The App Model then emits a `DesktopLaunch` request for `com.guidexos.apps.managed.settingscenter`, selector 5. The managed lifetime invokes the registered selector-5 factory, retains its returned `ManagedSettingsCenter`, and the application requests its native surface through the existing `TryCreateSurface` contract. No diagnostic imitation or boot-time factory call is used.

The R34 runner stopped before target derivation and input routing because of the stale audit. After the fix, the preserved live serial was evaluated offline against the actual target helper and returned `accepted=true`, `x=815`, `y=550`, `reason=source-defined-settings-button-center`. The runner did not move the pointer or send a click during R34. The QMP log contains one screenshot command and a quit command, with **zero input-send-event commands**. Therefore QMP input delivery, guest hit-testing, Notes action dispatch, the Settings launch request, and selector-5 dispatch remain unobserved.

## Hosted qualification and offline rehearsal

The focused hosted suite passed **476 assertions**. Commands are relative to the repository root unless an absolute path is shown.

| Scope | Command | Result |
| --- | --- | ---: |
| R33 readiness, post-gate audit, identity/profile binding | `pwsh -NoProfile -File scripts/dotnet/test-c188r33-settings-handoff-predicates.ps1` | 19 passed |
| R11 state and R11R2 focus/input runners | `pwsh -NoProfile -File scripts/dotnet/test-c188r11-runner-predicates.ps1`; `pwsh -NoProfile -File scripts/dotnet/test-c188r11r2-runner-predicates.ps1` | 35 + 27 passed |
| R12R1/R12R2 dispatch, R12R3 lifetime, R12R4 factory | `pwsh -NoProfile -File scripts/dotnet/test-c188r12r1-settings-handoff-predicates.ps1`; `pwsh -NoProfile -File scripts/dotnet/test-c188r12r2r1-settings-dispatch-predicates.ps1`; `pwsh -NoProfile -File scripts/dotnet/test-c188r12r3-lifetime-predicates.ps1`; `pwsh -NoProfile -File scripts/dotnet/test-c188r12r4-factory-predicates.ps1` | 37 + 12 + 13 + 9 passed |
| Settings factory link boundary | `pwsh -NoProfile -File scripts/dotnet/test-c188r12r4r1-link-boundary.ps1` | 9 passed |
| Input contracts and valid/invalid click targets | `pwsh -NoProfile -File scripts/dotnet/test-c188r11r2-keyboard-contracts.ps1`; included in the R12R1 and R33 suites above | 27 passed |
| R21/R22/R23 Settings regressions | `pwsh -NoProfile -File scripts/dotnet/test-c188r21-settings-factory.ps1`; `pwsh -NoProfile -File scripts/dotnet/test-c188r22-settings-bootstrap-handoff.ps1`; `pwsh -NoProfile -File scripts/dotnet/test-c188r23-settings-module-preparation.ps1` | 9 + 29 + 12 passed |
| R25/R26 first-failure and evidence-root checks | `pwsh -NoProfile -File scripts/dotnet/test-c188r25-failfast-origin.ps1`; `python scripts/dotnet/test-c188r25-failfast-origin.py`; `pwsh -NoProfile -File scripts/dotnet/test-c188r26-precreated-evidence-root.ps1`; `python scripts/dotnet/test-c188r26-first-failure.py` | 34 + 21 + 18 + 13 passed |
| R27 no-QAC rehearsal contract | `pwsh -NoProfile -File scripts/dotnet/test-c188r27-prelaunch-rehearsal.ps1` | 13 passed |
| R28/R29 GC guards | `python scripts/dotnet/test-c188r28-gc-state-guard.py`; `python scripts/dotnet/test-c188r29-preallocation-gc-state-guard.py` | 16 + 19 passed |
| R30 linked attachment and initializer qualification | `python scripts/dotnet/test-c188r30-thread-attachment.py --candidate-root "E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r33-settings-handoff\candidate-20261117T050157105Z-0fcf664c63df40e7ae3c8d3d759d5378"` | 25 passed |
| ThreadStore readiness and telemetry | `pwsh -NoProfile -File scripts/dotnet/test-c188r12r4r5r4-threadstore-readiness.ps1` | 7 mode + 6 telemetry assertions passed |
| QAC admission predicates | `pwsh -NoProfile -File scripts/dotnet/test-c188r12r4r5r4-qac-admission.ps1` | 35 passed |
| R20 preserved unwind replay | `python scripts/dotnet/test-c188r20-iterator-advancement.py` with the preserved R18 candidate/run/analysis, pinned runtime checkout, and a fresh output directory | 31 passed; no guest boot |

The successful R20 replay used the pinned runtime checkout `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT\out\dotnet\gc-feasibility-baseline\nativeaot-runtime` at `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`. The R30 linked suite used the R33 candidate path shown above. `git diff --check` and PowerShell parsing of both changed scripts passed.

The actual nested runner's `-PrelaunchRehearsal` completed after the correction. Its evidence is `E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r33-settings-handoff\r34-corrected-prelaunch-17949276898024153.evidence.json`. Kernel, ramdisk, and bootloader source/copy hashes matched; it records `qacInvoked=false`, `qemuLaunched=false`, and `guestExecuted=false`. An earlier invocation that supplied a precreated evidence directory was refused before QAC access, as intended; the clean runner-owned rehearsal passed.

## Fresh QAC preflight and one authorized boot

The fresh read-only preflight at `2026-11-17T14:49:47Z` was clear: QAC was online, status and list records were consistent at zero reservations/sessions/queued/blocked requests, and a complete host QEMU census found zero processes. The exact candidate manifest and inventory hashes were recorded in `r34-qac-preflight-f8763f2930494674ab5f64957421dffe`.

The R25 parent performed its own clear candidate-bound preflight and created one consumed guard:

- Guard: `...\one-boot-guards\attempt-r25-0daceb0b87f64f9ca45a0340ceacce26.json`
- Boot evidence: `...\qac-guest-r25-0daceb0b87f64f9ca45a0340ceacce26`
- Reservation: `23fba43bd7ae4911a6b36671d6e571a8`
- QEMU child PID: `4404`; one boot; no retry.

QAC emitted a transient warning that its process census was incomplete at reservation startup. The subsequent ownership snapshot accepted the run, matched owner/runner/child process identities and reservation IDs, showed one QEMU process with zero foreign processes, and reported `degraded=false`. QAC release was verified after exit.

## Guest milestones

The serial log is `...\qac-guest-r25-0daceb0b87f64f9ca45a0340ceacce26\serial.log`, SHA-256 `C57ACEC13AEE9B85B1E4C5EF0C02D9629918A199EDBF92618114713356A5FE28`.

| Milestone | R34 result |
| --- | --- |
| M1 — NativeAOT runtime initialized | **Yes.** Startup initialization markers and the completed module-initialization path are present. |
| M2 — Generated `InitializeModules` completed | **Yes.** `module_initialize_complete` is present at serial line 396. |
| M3 — Reverse-P/Invoke return completed | **Yes.** The native `after-managed-return` marker reports `rc=00000000`. |
| M4 — Managed Notes rendered and received focus | **Yes.** Notes surface is visible/focused/frontmost; `C166-ORDINARY-READY app=ManagedNotes render=PASS` is present. |
| M5 — Desktop input loop active | **Yes.** The kernel reports `Entering main loop (waiting for input)`. |
| M6 — Valid two-descriptor readiness accepted | **Partial.** The R33 profile gate accepted `C188R30_THREAD_ATTACHMENT_TRACE_TWO_DESCRIPTORS`, expected count 2, and `bootReady=true`. The following audit reverted to five and stopped the handoff. |
| M7 — Genuine Settings launch requested | **No.** The runner stopped before input. |
| M8 — Guest processed Settings input/dispatch | **No.** No input event was submitted. |
| M9 — Selector-5 factory invocation attempted | **No.** No Settings dispatch was observed. |
| M10 — Genuine managed factory entered | **No.** Factory event count is zero; lifetime evidence says `not-entered`. |
| M11 — Factory construction progressed | **No.** |
| M12 — Factory returned its genuine result | **No.** |
| M13 — Native App Model accepted/registered the result | **No.** |
| M14 — Settings window rendered | **No.** |
| M15 — Runtime remained valid | **Yes through controlled shutdown.** No fail-fast was observed; the runner sent QMP `quit`, and QEMU exited with code 0. |
| M16 — QEMU exited and QAC released | **Yes.** The run's after-status/list show zero; the run evidence reports `qacReleaseVerified=true`. |

The first incomplete Settings operation was **M7**. The nested evidence reports `clickAttempted=false`, zero button/key input events, and failure `C188R11 read-only readiness audit failed: managed-return-state-missing-or-invalid`. Its broad outcome label `C_POINTER_ROUTE_UNQUALIFIED` does not establish an input-routing defect: pointer placement and click dispatch were never attempted. No NativeAOT or factory defect is proven.

## Post-run cleanup and current QAC state

The R34 reservation exited with code 0 and logged release at `2026-11-17T14:53:34Z`. The run-owned after-status and after-list confirmed no remaining R34 QEMU process or reservation. The one-boot guard remains consumed and is preserved.

A later global snapshot at `2026-11-17T15:00:27Z` found a QAC-managed QEMU process PID `17700`, reservation `b90d05e15a4844bfbbb75cfb2219cfc1`, with its command line pointing into `D:\dev\guideXOSServer_TASKBAR_IMPROVEMENTS`. That later reservation is not the released R34 reservation. It was left untouched. The snapshot is preserved under `...\r34-late-qac-state-7b1c4aa7bb814a709665a4f537a589c7`.

## Source correction and next step

R34 commits the runner correction and regression test with this report. The correction does not change the R33 candidate binaries or inventory. Any follow-on guest attempt must use a separately identified execution package that binds the corrected runner SHA-256 `D79C5CCB90F68F348E7F53A110C77CAEE6AA13425E318A23684585B1A574B7BC` to the unchanged R33 manifest/inventory/kernel/ramdisk hashes. It must use a fresh one-boot guard and wait for a new clear QAC preflight; the consumed R34 guard cannot be reused.

The next native milestone is one source-validated Notes Settings action at `(815, 550)`, then separate evidence for guest hit-testing, action 24 processing, the App Model selector-5 request, genuine factory entry, construction/return, and native surface registration. No R34 retry was made.
