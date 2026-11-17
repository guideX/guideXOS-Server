# NativeAOT R33 — Settings Handoff and Factory Entry

## Result

**Outcome F — `SETTINGS_HANDOFF_GUEST_DEFERRED`.** R32 evidence showed that the guest had already completed `InitializeModules`, returned through the stock reverse-P/Invoke path, launched and rendered Managed Notes, and entered the kernel input loop. The R32 Settings runner still required the older five-descriptor state and an obsolete keyboard-focus marker. Those checks rejected the real two-descriptor R30 profile and the already focused native Notes surface, so the runner stopped before taking a screenshot or sending the Settings click. This was a runner readiness defect, not evidence of a new guest failure.

The profile-aware readiness correction passed hosted tests, candidate integrity checks, and the offline media rehearsal. QAC was online, but its census showed a managed guest running and a second reservation queued. The exclusive admission check therefore failed. The R25 parent wrote a `NOT_CLEAR` preflight record and returned before creating an R33 guard, reservation, or guest boot. R33 did not enter the Settings factory; no claim is made about post-click guest behavior.

## Source and artifacts

- Repository: `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT`
- Branch: `v1.1_DOTNET_SUPPORT`
- Starting HEAD: `9ce1ba8875d9a5ec6ce2dacfa83e4e0ae7b294fa`
- Runner correction commit / R33 candidate source HEAD: `2ea13dff512af06becbbd4b7a865d444b998bb39`
- Final source HEAD for the candidate: `2ea13dff512af06becbbd4b7a865d444b998bb39`
- R33 candidate: `E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r33-settings-handoff\candidate-20261117T050157105Z-0fcf664c63df40e7ae3c8d3d759d5378`
- R33 sidecar manifest SHA-256: `303F989980BA1EC7E737C6D9B96469ED4BE995F0FE18AFA86F6286CDCE39A817`
- Candidate package mode: verbatim copy of the qualified R32 package; no runtime rebuild. All 349 inventory entries matched their recorded hashes.
- Kernel SHA-256: `9931827D7F6186FF98E1DA580A0B3ED2EAAF725359A7EC096F0ED16ADF495D6B`
- Ramdisk SHA-256: `055B9FDCFEAB8C65D8292F2008F11D6CC39B9134CC3C57B11AC9F30320701374`
- Runtime-pack manifest SHA-256: `D4AB05BAB6D2B8ED8859BF11B41A8B173B49C910D9C4D3FAA14AE44191C36FAD`
- R33 one-boot policy: maximum one boot, no retry. The candidate manifest records `guardCreated=false`.

## R32 sequence and handoff contract

The preserved R32 serial log is `E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r30-thread-attachment\qac-guest-r25-0237e351e4dc423897f5592cc6b9bfb2\serial.log`, SHA-256 `D35F173911D01B6738EDF6027CD75F5938619E616B9A59CB42C09F323592FFBA`.

The relevant observed order is:

1. `module_initialize_complete` at serial line 396.
2. Startup `C188B005` and `C188B002` at lines 398 and 400.
3. The selector-4 Notes launch and its managed lifetime/factory path.
4. Native Notes surface creation reports `visible=1 minimized=0 focused=1 front=1 result=PASS` at line 454; `C166-ORDINARY-READY app=ManagedNotes render=PASS` follows at line 488.
5. `C188R11 state ... st=managed-return ... n=2 result=0` at line 505, then `C188R11-NATIVE ... stage=after-managed-return ... rc=00000000` at line 508.
6. The kernel reaches `Entering main loop (waiting for input)` at line 871.

The R32 R25 record reports `INITIALIZEMODULES_RETURN_VERIFIED`, candidate integrity verified, and clean QAC release. The accepted ThreadStore write-lock detail remains source-backed: the stock attachment path acquires the lock, and the later observer found the same thread published in the list; R32 did not emit a separate lock-acquired event. R20 unwind, R30 stock reverse-P/Invoke entry/return, and R32 PAL stack-bound fixes remain in the copied candidate.

No selector-5 Settings lifetime entry, App Model Settings launch request, or `C188R12R4_FACTORY ... selector=5 ... result=begin` appears in the R32 serial log. Thus module initialization and the successful selector-4 Notes launch do not prove Settings factory entry. R32's Settings evidence also records `bootReady=false` and `clickAttempted=false`; there was no guest input to classify as delivered or processed.

The existing Settings contract is event-driven. `GuideXosApplicationRegistry` registers selector 5 as a zero-argument managed `Func<GuideXosApplication>` that returns a genuine `ManagedSettingsCenter`. The delegate and returned reference are managed-to-managed; there is no native factory ABI. `ManagedMain(void*)` is entered through the existing native `GuideXosNativeAotApplicationEntry(void*)` `__cdecl` boundary. `GuideXosManagedApplicationLifetime.TryStart` invokes the selector-5 factory on an App Model launch request, rejects null, and strongly retains the instance in its managed lifetime pool. The application then calls `ManagedSettingsCenter.Launch(host)`, which requests its native surface through `TryCreateSurface`. The current R12R1 flow uses the real Settings button and screenshot-derived target; it does not synthesize a boot-time factory call.

## Cause and correction

The old `Test-C188R11ManagedLaunchState` predicate hard-coded `n=5`. The qualified R30 runtime contains two registered descriptors, selectors 4 and 5, so R32's valid `n=2` managed-return state failed with `managed-return-state-missing-or-invalid`. Separately, the outer R12R1 click gate required `[C188R11R2_KEY] stage=main-loop-focus`, a marker absent from the current runtime logger and therefore unavailable before the first guest input. The actual native Notes surface marker already proves visible, non-minimized, focused, and frontmost state.

The correction adds a candidate-bound readiness profile. It allows exactly two descriptors only when the candidate's runtime manifest hash, exact R30 identity and marker, Workstation GC setting, no-image-arena setting, and genuine selector-5 factory/constructor identity all match. Otherwise, legacy candidates keep the five-descriptor expectation. The QAC runner only accepts the two-descriptor setting for a validated Settings-handoff run with a matching profile and runtime hash. The click gate now requires the real startup, initialization, managed/native return, Notes render, and focused/frontmost surface evidence, plus valid screen dimensions; it rejects a duplicate Settings launch or factory entry. It does not create a readiness marker or invoke the factory itself.

The new negative tests establish that `InitializeModules` completion is not Settings factory completion, wrong descriptor counts and unbound runtime hashes are rejected, startup-only markers do not open the click gate, missing render or native focus is rejected, and an existing Settings launch/factory event prevents a duplicate click.

## Hosted qualification and candidate rehearsal

Passing focused suites (command names are relative to the repository root):

| Command | Result |
|---|---:|
| `pwsh -NoProfile -File scripts/dotnet/test-c188r33-settings-handoff-predicates.ps1` | 16 passed |
| `pwsh -NoProfile -File scripts/dotnet/test-c188r11-runner-predicates.ps1` | 35 passed |
| `pwsh -NoProfile -File scripts/dotnet/test-c188r11r2-runner-predicates.ps1` | 27 passed |
| `pwsh -NoProfile -File scripts/dotnet/test-c188r27-prelaunch-rehearsal.ps1` | 13 passed |
| `pwsh -NoProfile -File scripts/dotnet/test-c188r21-settings-factory.ps1` | 9 passed |
| `pwsh -NoProfile -File scripts/dotnet/test-c188r22-settings-bootstrap-handoff.ps1` | 29 passed |
| `pwsh -NoProfile -File scripts/dotnet/test-c188r23-settings-module-preparation.ps1` | 12 passed |
| `pwsh -NoProfile -File scripts/dotnet/test-c188r25-failfast-origin.ps1` | 34 passed |
| `pwsh -NoProfile -File scripts/dotnet/test-c188r26-precreated-evidence-root.ps1` | 18 passed |
| `pwsh -NoProfile -File scripts/dotnet/test-c188r12r1-settings-handoff-predicates.ps1` | 37 passed |
| `pwsh -NoProfile -File scripts/dotnet/test-c188r12r2r1-settings-dispatch-predicates.ps1` | 12 passed |
| `pwsh -NoProfile -File scripts/dotnet/test-c188r12r3-lifetime-predicates.ps1` | 13 passed |
| `pwsh -NoProfile -File scripts/dotnet/test-c188r12r4-factory-predicates.ps1` | 9 passed |
| `pwsh -NoProfile -File scripts/dotnet/test-c188r12r4r5r4-threadstore-readiness.ps1` | 7 mode + 6 telemetry assertions passed |
| `pwsh -NoProfile -File scripts/dotnet/test-c188r12r4r5r4-qac-admission.ps1` | 35 passed |
| `python scripts/dotnet/test-c188r25-failfast-origin.py` | 21 passed |
| `python scripts/dotnet/test-c188r26-first-failure.py` | 13 passed |
| `python scripts/dotnet/test-c188r28-gc-state-guard.py` | 16 passed |
| `python scripts/dotnet/test-c188r29-preallocation-gc-state-guard.py` | 19 passed |
| `python scripts/dotnet/test-c188r30-thread-attachment.py --candidate-root <R33 candidate>` | 25 passed |

The R32-preserved R20 iterator/unwind regression has 31 passing assertions and used no guest boot. A separate `test-c188r24-initializemodules.py --candidate-root <R32 candidate>` attempt exited 1 because that harness expects the older `managed/artifacts` layout while this R30 candidate uses `managed/nativeaot/artifacts`; it is not counted as a pass. The R30 linked suite above passed and owns generated-entry verification for this candidate profile. PowerShell parsing of all seven changed/new runtime scripts and `git diff --check` passed.

The R33 candidate builder verified the original R30/R25 sidecars and all 349 copied candidate inventory entries, ran the R25 analyzer, and passed the 25-check linked R30 suite. The offline Settings-handoff rehearsal returned `PASS`, copied kernel, ramdisk, and bootloader with matching source/destination hashes, and recorded `qacInvoked=false`, `qemuLaunched=false`, and `guestExecuted=false`.

## Native attempt and status

The QAC read-only census at `2026-11-17T05:03Z` reported the coordinator online, with one managed guest running, two reservations total, and one reservation queued. The R25 parent performed its exclusive preflight and recorded `NOT_CLEAR` at `E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r33-settings-handoff\r25-run-evidence-a3d9ad6aeab14ed089e9bc5171365de5.json`. It states `nativeExecutionAttempted=false` and “no reservation or boot was attempted.” No R33 one-boot guard was created. No guest input was sent; therefore there is no QMP acknowledgement or guest-side input-processing evidence for R33. No R33 QEMU process existed to clean up or release.

The two guards already present in the shared R25 guard directory belong to historical runs; the R33 candidate is not bound by either. The pre-existing modified object hashes remain unchanged:

| Protected object | SHA-256 |
|---|---|
| `c188r12r4r5r4r5r2r2_native_alloc_operator_new_test.obj` | `3507A24E07257422C98CAB4F7430E094A2C83DF93647E6A02621010DD585A2BD` |
| `c188r12r4r5r4r5r2r2_native_alloc_record_test.obj` | `605D6F7F066BAD3669B52C21A89722A7C9750F9C6595EDFDA26BC06E3125AEC3` |
| `guidexos_nativeaot_native_alloc_record.obj` | `2CD941199D5F34F81F72B0B718A8D1C1023CEAD831464724DEBFEA435F044183` |

## Next milestone

After QAC reports zero running processes, reservations, managed sessions, and queued requests, perform one fresh R33 R25-parent invocation. It will create a new one-boot guard only after exclusive preflight passes. Preserve that single-attempt rule. The next native evidence should distinguish screenshot-derived target validation, QMP command acknowledgement, guest input processing, selector-5 App Model launch, genuine factory entry/return, and native surface registration. A returned factory result alone must not be reported as a functional Settings UI.
