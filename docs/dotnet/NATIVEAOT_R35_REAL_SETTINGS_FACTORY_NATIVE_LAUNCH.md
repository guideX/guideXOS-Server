# NativeAOT R35 Settings Factory Launch

## Result

**Outcome: `SETTINGS_LAUNCH_GUEST_DEFERRED`.** The unchanged R33 candidate and corrected R35 runner passed full offline qualification. The fresh QAC preflight could not connect to the coordinator: both `status` and `list` returned exit code 2 with `Coordinator unavailable: Coordinator connection timed out or is unavailable.` The host process census completed (309 processes, zero QEMU processes), but reservation and session state could not be established. The runner therefore deferred before creating a real boot guard or starting a guest.

No R35 guest boot, Settings click, selector-5 dispatch, or managed factory invocation occurred. This is an execution-lane availability result, not a NativeAOT or application failure. R34's consumed guard and all prior evidence remain intact.

## Repository and preservation

- Starting branch: `v1.1_DOTNET_SUPPORT`
- Starting HEAD: `928206cb767077a4940115284bb76d916a726d8b`
- Final HEAD: the commit containing this report and the in-scope runner qualification changes; the exact post-commit OID is reported in the task closeout.
- No push was made.

The three pre-existing modified object files were never staged or committed. Their final SHA-256 values match their canonical values:

| Protected object | SHA-256 |
| --- | --- |
| `c188r12r4r5r4r5r2r2_native_alloc_operator_new_test.obj` | `3507A24E07257422C98CAB4F7430E094A2C83DF93647E6A02621010DD585A2BD` |
| `c188r12r4r5r4r5r2r2_native_alloc_record_test.obj` | `605D6F7F066BAD3669B52C21A89722A7C9750F9C6595EDFDA26BC06E3125AEC3` |
| `guidexos_nativeaot_native_alloc_record.obj` | `2CD941199D5F34F81F72B0B718A8D1C1023CEAD831464724DEBFEA435F044183` |

## Candidate provenance and R35 package

R35 restaged the exact R33 candidate identified by R34 into a distinct package. The managed ELF, kernel, ramdisk, and bootloader were copied unchanged; no NativeAOT build was run.

| Component | Identity / SHA-256 |
| --- | --- |
| R35 execution package | `4c766a47a738476f8f7b8b80d5649d68` |
| Fresh execution attempt | `67f513a3d37645759648fce2e34ebb0d` |
| Execution package manifest | `46625EFAAB24A147B6534A8D186CE2F743203FB31ECD572B9349C7000A32EC28` |
| Candidate manifest | `787A46FFFF09C23355E585F6A7C02BC1ABA19429DED156BC64C19F28A6F510E2` |
| Candidate integrity inventory | `72B913375CA2438C4FB3705D3B1F6AA514DBA0DDB1A46F5F964DEA44BBF89A94` |
| Original R25 sidecar | `818D8724D900BE34A6EFD73160B00F9278A164BB05495925E070F37283F087DE` |
| Original R33 sidecar | `303F989980BA1EC7E737C6D9B96469ED4BE995F0FE18AFA86F6286CDCE39A817` |
| R35 R25 sidecar bound to the new candidate path | `CABA1D293D8242C7A216157740D722403E77F5712050E2B254DCE7A3EBC3E50C` |
| R25 analyzer | `6E30A583F5145947C820EDD1D414AC00981B156F122DC9F800B915C904CF4105` |
| R34 corrected nested runner | `D79C5CCB90F68F348E7F53A110C77CAEE6AA13425E318A23684585B1A574B7BC` |
| R35 nested Settings runner | `05259914FAB761763B7E84BE3D45A9FD09314397FDF9A449BE649706BBD14986` |
| R35 parent QAC runner | `CF9011A9B50ED181C5459BF226152DA812F3127837B1ABAB2D66E169CB3C9172` |
| R35 top-level runner | `2C4327CE590FBAF19741D78DF47611FF2568E3D9F93C7B05D1A455402170AB34` |

The R35 package binds the `C188R30_THREAD_ATTACHMENT_TRACE_TWO_DESCRIPTORS` readiness profile, expected descriptor count `2`, and ordered selectors `4,5`. Its candidate metadata binds the genuine `GuideXosApplicationRegistry` selector-5 factory to `ManagedSettingsCenter` and constructor `ManagedSettingsCenter___ctor_0`.

The candidate hashes remain the R34-qualified values: kernel `9931827D7F6186FF98E1DA580A0B3ED2EAAF725359A7EC096F0ED16ADF495D6B`, staged ramdisk `055B9FDCFEAB8C65D8292F2008F11D6CC39B9134CC3C57B11AC9F30320701374`, EFI bootloader `D72A35DA882892BA97356A1C006562ABA2908D9DD909152B96DCBB655D6C2D9B`, managed ELF `14C61298B00665B9981C7FE177670C529C69B4F8AF2D9EA96BF4C53F888C40C8`, and runtime-pack manifest `D4AB05BAB6D2B8ED8859BF11B41A8B173B49C910D9C4D3FAA14AE44191C36FAD`.

All 349 candidate inventory hashes were verified during staging, again by the R35 runner, and after the fresh QAC preflight. The candidate directory contained 351 files total: the 349 inventoried files plus the candidate manifest and integrity inventory. The final audit found 349/349 matching hashes and zero mismatches. The original R33 candidate and both original sidecars remain unchanged.

## Runner audit and offline rehearsal

The parent and nested execution path now derives the descriptor count and identities from the candidate readiness profile. The R30 profile requires one descriptor-lookup record with `requested=4`, `gen=1`, `input=0`, `count=2`, ordered slots `4,5`, `live=0`, and `result=0`. Missing or duplicated identity evidence, duplicate selectors, a wrong order, or a count/profile mismatch fails closed. The secondary audit, initial click gate, input evidence, post-click evidence, and outcome classification all carry the profile count. Selector-5 factory entry is classified from selector-5 `type-init-enter` or `constructor-enter` events after a selector-5 `factory-invoke`; the parent runner's unrelated `M11` capture-complete field is not used as factory-entry evidence.

Remaining five-valued defaults are for legacy compatibility profiles, and `heldLen=5` describes the actual backing-memory capacity. They were not globally changed to two.

The final end-to-end offline rehearsal passed with the package above. It accepted the candidate identity and complete inventory, passed both parent and nested readiness gates using the authentic R34 serial, accepted the intended precreated evidence root, and carried the corrected runner and analyzer hashes. It recorded `qacInvoked=false`, `qemuLaunched=false`, `guestExecuted=false`, `settingsClickSubmitted=false`, and `realAttemptGuardConsumed=false`. Rehearsal used only a separate synthetic guard.

Negative coverage rejects the wrong two-descriptor order, duplicate selectors, missing or duplicated identity trace, a profile count mismatch, an invalid package manifest, missing offline-readiness evidence, and reuse of a consumed candidate guard. The R35 wrapper rejects the latter three before QAC admission.

## Settings target and milestone status

The preserved R34 live desktop evidence is a 1280×800 screenshot with Managed Notes visible and frontmost. The source-validated Settings button bounds are desktop x=770–860 and y=536–564, putting its center at **(815, 550)**. The mouse pointer is elsewhere, and the “Welcome to guideXOS” notification is in the upper-right without covering the button. R34 serial evidence establishes the managed input loop and Notes focus. The button reaches the existing Notes action, App Model launch, and genuine selector-5 factory path; no synthetic factory call is used.

The screenshot and target were used to qualify the R35 input stage, but the guest was not available for the click. R34's screenshot hashes are PPM `F2FF19367B096ED5360B80014DAB7886EE81DA084768EF6C4BE0442629905CD5` and PNG `D256A7150EF8DCE31F1131FD7D88A4503AC568258A50D16755FAF77BACC2D44C`. Its QMP log contains no input-send-event command.

| Milestone | R35 status |
| --- | --- |
| M1 NativeAOT initialized | Established by R34; no new R35 boot |
| M2 `InitializeModules` completed | Established by R34; no new R35 boot |
| M3 Managed Notes rendered | Established by R34; no new R35 boot |
| M4 Notes focus and input-loop readiness | Established by R34; no new R35 boot |
| M5 Profile-bound `n=2`, selectors `4,5` accepted by active gates | Rehearsed with the R34 serial; no new R35 boot |
| M6 Settings target verified in guest layout | Verified from R34 screenshot, surface state, and source geometry |
| M7 Settings click submitted | Not reached |
| M8 Guest received and processed click | Not reached |
| M9 Settings App Model request dispatched | Not reached |
| M10 Selector-5 factory invocation attempted | Not reached |
| M11 Genuine managed factory entered | Not reached |
| M12 Factory constructed expected result | Not reached |
| M13 Factory returned through ABI | Not reached |
| M14 Native App Model accepted or registered result | Not reached |
| M15 Settings window rendered | Not reached |
| M16 Runtime validity and QAC cleanup after R35 guest | No R35 guest; prior R34 cleanup remains preserved |

There is no R35 input-delivery or dispatch evidence because no click was sent. No new NativeAOT failure or application defect was observed.

## Hosted verification

The R35 focused regressions passed **547 assertions** in total. The individual suite results were:

| Command | Assertions |
| --- | ---: |
| `pwsh -NoProfile -File scripts/dotnet/test-c188r11-runner-predicates.ps1` | 35 |
| `pwsh -NoProfile -File scripts/dotnet/test-c188r11r2-runner-predicates.ps1` | 27 |
| `pwsh -NoProfile -File scripts/dotnet/test-c188r12r1-settings-handoff-predicates.ps1` | 39 |
| `pwsh -NoProfile -File scripts/dotnet/test-c188r12r2r1-settings-dispatch-predicates.ps1` | 12 |
| `pwsh -NoProfile -File scripts/dotnet/test-c188r12r3-lifetime-predicates.ps1` | 13 |
| `pwsh -NoProfile -File scripts/dotnet/test-c188r12r4-factory-predicates.ps1` | 9 |
| `pwsh -NoProfile -File scripts/dotnet/test-c188r12r4r1-link-boundary.ps1` | 9 |
| `pwsh -NoProfile -File scripts/dotnet/test-c188r11r2-keyboard-contracts.ps1` | 27 |
| `pwsh -NoProfile -File scripts/dotnet/test-c188r21-settings-factory.ps1` | 9 |
| `pwsh -NoProfile -File scripts/dotnet/test-c188r22-settings-bootstrap-handoff.ps1` | 29 |
| `pwsh -NoProfile -File scripts/dotnet/test-c188r23-settings-module-preparation.ps1` | 12 |
| `pwsh -NoProfile -File scripts/dotnet/test-c188r25-failfast-origin.ps1` | 34 |
| `python scripts/dotnet/test-c188r25-failfast-origin.py` | 21 |
| `pwsh -NoProfile -File scripts/dotnet/test-c188r26-precreated-evidence-root.ps1` | 18 |
| `python scripts/dotnet/test-c188r26-first-failure.py` | 13 |
| `pwsh -NoProfile -File scripts/dotnet/test-c188r27-prelaunch-rehearsal.ps1` | 13 |
| `python scripts/dotnet/test-c188r28-gc-state-guard.py` | 16 |
| `python scripts/dotnet/test-c188r29-preallocation-gc-state-guard.py` | 19 |
| `python scripts/dotnet/test-c188r30-thread-attachment.py --candidate-root "E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r35-settings-launch\package-4c766a47a738476f8f7b8b80d5649d68\candidate-r35-4c766a47a738476f8f7b8b80d5649d68"` | 25 |
| `pwsh -NoProfile -File scripts/dotnet/test-c188r33-settings-handoff-predicates.ps1` | 23 |
| `pwsh -NoProfile -File scripts/dotnet/test-c188r24-initializemodules-snapshot.ps1` | 14 |
| `pwsh -NoProfile -File scripts/dotnet/test-c188r12r4r5r4-threadstore-readiness.ps1` | 7 mode + 6 telemetry |
| `pwsh -NoProfile -File scripts/dotnet/test-c188r12r4r5r4-qac-admission.ps1` | 35 |
| `python scripts/dotnet/test-c188r19-gc-root-completion.py` with the preserved R18 candidate/run/analysis and pinned runtime source | 12 |
| `python scripts/dotnet/test-c188r20-iterator-advancement.py` with the preserved R18 candidate/run/analysis and pinned runtime source | 31 |
| `pwsh -NoProfile -File scripts/dotnet/test-c188r35-settings-launch.ps1 -PackageRoot "E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r35-settings-launch\package-4c766a47a738476f8f7b8b80d5649d68"` | 39 |

The R19 command was:

```powershell
python scripts/dotnet/test-c188r19-gc-root-completion.py --candidate-root "E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r13-managed-gc-root\candidate-r17-progress-pair-20261112T034829000Z" --run-root "E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r13-managed-gc-root\qac-guest-managed-gc-root-C188R13-20261113T003700302Z-0317d25e6f6c456b850c4904190019fa" --saved-analysis "E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r13-managed-gc-root\r18-hosted-20261113T002215470Z\native-progress-analysis.json" --runtime-source "D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT\out\dotnet\gc-feasibility-baseline\nativeaot-runtime" --output-directory "E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r35-settings-launch\package-4c766a47a738476f8f7b8b80d5649d68\r19-hosted-r35"
```

The R20 command used the same preserved inputs and output directory `E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r35-settings-launch\package-4c766a47a738476f8f7b8b80d5649d68\r20-hosted-r35-final`:

```powershell
python scripts/dotnet/test-c188r20-iterator-advancement.py --candidate-root "E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r13-managed-gc-root\candidate-r17-progress-pair-20261112T034829000Z" --run-root "E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r13-managed-gc-root\qac-guest-managed-gc-root-C188R13-20261113T003700302Z-0317d25e6f6c456b850c4904190019fa" --saved-analysis "E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r13-managed-gc-root\r18-hosted-20261113T002215470Z\native-progress-analysis.json" --runtime-source "D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT\out\dotnet\gc-feasibility-baseline\nativeaot-runtime" --output-directory "E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r35-settings-launch\package-4c766a47a738476f8f7b8b80d5649d68\r20-hosted-r35-final"
```

Both hosted runs launched no guest and reserved no QAC. The preserved R20 native baseline remains `NATIVEAOT_SINGLE_COLLECTION_VERIFIED`: one collection completed, the managed object survived, and the returned result was `1123` (`0x463`), as recorded in the R20 root-cause report.

`git diff --check` passed. PowerShell parsing passed for the R35 parent, nested, guest, predicate, staging, and test scripts.

## Fresh QAC preflight and next phase

The R35 preflight was captured at `2026-11-20T03:27:31.2238787Z` and is preserved under the R35 package. The process census completed with 309 processes and zero QEMU processes. QAC `status` and `list` both exited 2 and returned the coordinator timeout message; their captured output SHA-256 is `76E47B31F6840364755513DE8420DA224DA4D560CCCF94926600780861576DFA`. Since the coordinator could not establish reservation/session state or exclusive admission, the run was deferred. No foreign process was stopped, no reservation was released or bypassed, and no coordinator state was changed.

R35 has no real one-boot guard in its package and no guest run record beyond the deferred outcome. R34's consumed guard remains consumed at its original path; its SHA-256 at closeout is `9B0B7DE1ACBCD6729C3E4A057A03B2F058D4FFF320E2F5A715A4E601104F8BF0`.

The next phase is a fresh independently identified R35 package and one-shot native attempt after the QAC coordinator is online and its status/list and process census establish exclusive admission. Reuse the unchanged R33 binaries and this qualification, create a new attempt identity and guard, then capture click delivery through selector-5 factory entry. Do not reuse this deferred attempt identity or the R34 guard.
