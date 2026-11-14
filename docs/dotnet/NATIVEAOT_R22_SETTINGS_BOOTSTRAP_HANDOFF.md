# NativeAOT R22 — Settings Bootstrap Handoff

## Result

**Outcome C — SETTINGS_BOOTSTRAP_CAUSE_PROVEN.** Source and linked-code analysis established an R21 bootstrap-order defect: the direct application entry called ManagedMain after RhInitialize and TLS setup without first registering the actual NativeAOT OS module/code manager and invoking the generated InitializeModules entry. R22 adds that preparation at the shared runtime/PAL boundary before the host declares startup ready and before ManagedMain.

The one R22 QAC boot reached the new bootstrap-handoff-begin marker, then timed out before the preparation call returned. It did not emit bootstrap-handoff-ready, C188B002, managed Settings launch evidence, or the real Settings factory marker. The source-level omission is corrected; native completion and Settings acceptance remain unverified. The remaining execution boundary is inside shared runtime preparation and needs a narrower stage trace.

## Git and preservation

- Branch: v1.1_DOTNET_SUPPORT
- Starting HEAD: df73239998b45f5d61667e9c071cedff172e0caa
- Implementation commit: c9905eea25c6b727c60e36c1a93cd94a7fa408bd.
- Final repository HEAD: the documentation closeout commit immediately above the implementation commit; its exact object ID is in the task completion response.
- No push was made.

The three pre-existing modified root object files were preserved, excluded from staging, and not consumed as unverified candidate inputs:

| File | Preserved SHA-256 |
| --- | --- |
| c188r12r4r5r4r5r2r2_native_alloc_operator_new_test.obj | 3507A24E07257422C98CAB4F7430E094A2C83DF93647E6A02621010DD585A2BD |
| c188r12r4r5r4r5r2r2_native_alloc_record_test.obj | 605D6F7F066BAD3669B52C21A89722A7C9750F9C6595EDFDA26BC06E3125AEC3 |
| guidexos_nativeaot_native_alloc_record.obj | 2CD941199D5F34F81F72B0B718A8D1C1023CEAD831464724DEBFEA435F044183 |

## What C188B002 means

The marker is emitted by GuideXosNativeAotApplicationEntry in samples/managed/HostLogProof/c102_startup.c. In the R21 source, the host first installed PAL hooks, hook tables, and GC startup hooks, called RhInitialize(0), installed the current thread TLS, advanced startup stage 7, set the host startup state to ready, and emitted C188B002. The next source operation was a direct call to ManagedMain.

C188B002 therefore marked completion of this native host initialization sequence. It did not mean that managed application entry had started, that NativeAOT modules were initialized, that the Settings factory ran, or that Settings had failed. Source and disassembly show that ManagedMain's generated entry begins with RhpReversePInvoke. That prologue enters the shared runtime initialization path, which registers the actual OS module and code manager and invokes generated InitializeModules. R21 deferred this essential work into the first reverse-P/Invoke transition from ManagedMain.

The expected next operation after the historical marker was ManagedMain's generated RhpReversePInvoke prologue, followed by shared runtime state initialization and generated module initialization, then the managed dispatcher. No source branch, wait, or intentional early return sits between the marker and the ManagedMain call. ManagedMain can return later through the established integer result contract. The guest's captured serial ended before any managed dispatcher marker.

The R21 marker was synchronous startup telemetry and the serial capture includes the complete output produced before the runner's bounded cleanup. The nested runner reported that it did not reach the input loop before timeout and requested QMP quit. Thus the old runner label guest-runner-exited-before-Notes-readiness did not establish an independent guest crash.

No diagnostics after the last observed marker are present in the preserved serial. R22 brackets the preparation call with new synchronous native markers, so its missing ready marker identifies a non-returning interval inside that call rather than relying on the old C188B002 interpretation.

## R20 and R21 startup comparison

| Boundary | Accepted R20 path | R21 path | Finding |
| --- | --- | --- | --- |
| Candidate workload | Managed execution and collection probe | Existing C160 composite App Model and real selector-5 Settings factory | The application workload differs; this alone does not establish a runtime defect. |
| GC configuration | NativeAOT Workstation GC, without the C188 arena | NativeAOT Workstation GC, without the C188 arena | No meaningful GC topology difference was found at this startup boundary. |
| Runtime initialization | The R20 transition probe reached a stock reverse-P/Invoke, observed InitializeModules return, then verified modules and code manager were registered before ManagedMain | The direct entry called ManagedMain immediately after RhInitialize and TLS | This is the earliest meaningful source divergence. |
| Managed entry | Entered after module initialization; the accepted serial recorded modules=1 and codeManager=1 | The first generated ManagedMain operation was RhpReversePInvoke, which deferred initialization into that transition | R21 lacked an explicit pre-entry completion boundary. |
| Unwind implementation | Shared guideXosRtlVirtualUnwind production implementation linked; 31-assertion R20 regression passed | Same R20 implementation and Workstation GC profile | The R20 unwind repair remains present and is not implicated by the R21 pre-managed boundary. |
| Settings path | Not the R20 probe's purpose | Selector 5 resolves to ManagedSettingsCenter; constructor symbol ManagedSettingsCenter___ctor_0 is linked | The real factory was available in the image, but was not reached. |
| Stack and heap | Not the cause of the R20 startup difference | 16 KiB configured bootstrap stack; remaining stack at factory entry unknown | The historical 5,624-byte figure was heap capacity, not stack. No exhaustion is demonstrated. |

The exact generated InitializeModules contract is five arguments: OS module handle, module-header pointer array and count, class-library function pointer array and count. The R22 hosted source contract checks this against the pinned NativeAOT StartupCodeHelpers ABI. The shared helper uses the current TLS block and existing runtime tables, registers the real module and code manager, and invokes the generated initialization path. It does not create a Settings-specific runtime copy or use a fabricated ABI.

R20's accepted guest went beyond startup: it completed one real Workstation GC collection, reported collection count 0 to 1, preserved the managed object, and returned the expected value. Its captured serial observed InitializeModules return and verified modules=1 and codeManager=1 before ManagedMain. R21 keeps that runtime and unwind foundation but changes the direct startup ordering and adds the real Settings workload.

The real selector-5 contract remains unchanged. The registry maps selector 5 to the managed ManagedSettingsCenter factory delegate. The constructor and factory are managed-to-managed and take no native-facing arguments. Native code enters the established ManagedMain context ABI; the existing application model owns and dispatches the resulting managed reference. The candidate map contains the real constructor symbol.

## Runner correction

The preserved R21 nested runner stderr says “Guest did not reach the input loop before timeout.” Its nested QMP log contains the runner's quit request, and QAC records QEMU exit code 0 followed by reservation release. The R21 analyzer's former terminal label treated the completed nested runner as a guest exit before readiness.

R22's terminal predicate now distinguishes nested input-loop readiness timeout and runner-requested QMP cleanup from QAC/QEMU exit before readiness. Reanalysis of the original R21 stdout, stderr, and QMP evidence yields:

- terminal cause: guest-input-loop-readiness-timeout
- readiness timeout: true
- guest exited before readiness: false
- runner quit after readiness timeout: true
- QMP quit observed: true

This corrects the terminal classification without changing runtime behavior. The original R21 candidate, guard, and run artifacts remain preserved.

## Source changes

- tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_platform.cpp exports guidexos_nativeaot_prepare_managed_entry. After TLS is installed, it calls the existing shared initializeRuntimeState path when modules are not initialized and returns success only when module initialization and code-manager registration are both recorded.
- samples/managed/HostLogProof/c102_startup.c calls the shared preparation function before setting startup ready, emitting C188B002, and calling ManagedMain. New C188B004 and C188B005 markers bracket this operation; a nonzero return fails startup.
- kernel/core/nativeaot_application.cpp maps those markers to bootstrap-handoff-begin and bootstrap-handoff-ready host events.
- The runtime-pack and managed-host build scripts add a separately guarded R22 profile based on the established R21 factory profile and identify its Workstation GC runtime pack.
- scripts/dotnet/c188r22-bootstrap-handoff-predicates.ps1 adds boundary and terminal classification checks. C188B002 by itself is never classified as managed entry.
- scripts/dotnet/run-c188r12r1-settings-handoff.ps1 consumes nested runner and QMP evidence so readiness timeouts are not reported as guest exits.
- scripts/dotnet/build-c188r22-settings-candidate.ps1 builds an isolated, hashed R22 candidate and runs its hosted regressions.
- scripts/dotnet/test-c188r22-settings-bootstrap-handoff.ps1 adds the regression cases for the source order, marker meaning, module initialization ABI, Settings symbol, runner terminal cause, and fail-closed candidate checks.
- scripts/dotnet/test-c188r12r4r5r3-init-return-contract.ps1 now matches the runner's artifact-root-based output path; the former assertion expected a stale literal path.

No Settings factory, GC algorithm, image arena, unwind implementation, ThreadStore rule, or application ownership policy was changed.

## Hosted qualification

The candidate builder's focused R22 contract ran as:

| Command | Result |
| --- | --- |
| pwsh -NoProfile -File scripts/dotnet/test-c188r22-settings-bootstrap-handoff.ps1 | 29 cases passed |
| pwsh -NoProfile -File scripts/dotnet/test-c188r21-settings-factory.ps1 | 9 cases passed |
| Preserved R20 iterator/unwind regression invoked by the candidate builder | 31 assertions passed |
| git diff --check | Passed; only LF-to-CRLF working-copy notices |

The additional focused regression scripts run during the R22 qualification passed:

| Coverage | Command or test script | Result |
| --- | --- | --- |
| R11 lifecycle, runner predicates, output bounds | pwsh -NoProfile -File scripts/dotnet/test-c188r11-runtime-lifecycle.ps1; pwsh -NoProfile -File scripts/dotnet/test-c188r11-runner-predicates.ps1; pwsh -NoProfile -File scripts/dotnet/test-c188r11-output-bounds.ps1 | 7, 30, and 6 cases passed |
| R11R2 and R11R4 runner/QAC ownership | pwsh -NoProfile -File scripts/dotnet/test-c188r11r2-runner-predicates.ps1; pwsh -NoProfile -File scripts/dotnet/test-c188r11r4-qac-ownership-predicates.ps1 | 24 and 15 cases passed |
| R12 settings handoff and dispatch | pwsh -NoProfile -File scripts/dotnet/test-c188r12r1-settings-handoff-predicates.ps1; pwsh -NoProfile -File scripts/dotnet/test-c188r12r2r1-settings-dispatch-predicates.ps1 | 37 and 12 cases passed |
| R12 lifetime and factory/link contracts | pwsh -NoProfile -File scripts/dotnet/test-c188r12r3-lifetime-predicates.ps1; pwsh -NoProfile -File scripts/dotnet/test-c188r12r4-factory-predicates.ps1; pwsh -NoProfile -File scripts/dotnet/test-c188r12r4r1-link-boundary.ps1 | 13, 9, and 9 cases passed |
| GC ownership, initialization attribution, safe handoff | pwsh -NoProfile -File scripts/dotnet/test-c188r12r4r5-gc-ownership.ps1; pwsh -NoProfile -File scripts/dotnet/test-c188r12r4r5r1-init-attribution.ps1; pwsh -NoProfile -File scripts/dotnet/test-c188r12r4r5r2r2r4-safe-handoff.ps1; pwsh -NoProfile -File scripts/dotnet/test-c188r12r4r5r2r2r4-safe-handoff-runner-contract.ps1 | Passed |
| Initialization return contract | pwsh -NoProfile -File scripts/dotnet/test-c188r12r4r5r3-init-return-contract.ps1 | Passed after its stale path assertion was corrected |
| QAC admission and ThreadStore readiness | pwsh -NoProfile -File scripts/dotnet/test-c188r12r4r5r4-qac-admission.ps1; pwsh -NoProfile -File scripts/dotnet/test-c188r12r4r5r4-threadstore-readiness.ps1 | 35 cases; 7 mode and 6 telemetry assertions |
| Semishared runtime contract | pwsh -NoProfile -File scripts/dotnet/test-c188r12r4r5r4r5r2-semishared.ps1 | 30 assertions passed |

These are hosted source, ABI, link, and evidence-predicate results. They do not establish native Settings entry or factory success.

## Candidate identity and integrity

Latest candidate:

E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r22-settings-bootstrap-handoff\candidate-20261114T161901255Z-4424f0e675da434cb80db343364ddd41

- Candidate result: PASS_BUILD_READY_FOR_QAC_PREFLIGHT
- Profile: C160Composite+C185+C188R11+C188R12R3+R20WorkstationGC+R21SettingsFactory+R22BootstrapHandoff
- Runtime identity: guidexos-nativeaot-runtime-pack-amd64-c188r22-settings-bootstrap-handoff-workstationgc-v1
- Candidate manifest SHA-256: 8D767CA63C4CAD696F1CACF392120DB79F23049D55C74E28DF2706B44B2ED2F7
- Integrity inventory: 660 files, zero hash failures
- Integrity inventory SHA-256: 32A04E9D5DAE6403019AF973C7EC197A302E443DBCF7681977FBE88F24EC830
- Managed Settings ELF SHA-256: B49B5D36761B617EC848B5EDDB312E7CA21892D7386C7981FD41D9F6E5589981
- Kernel ELF SHA-256: 43D6FB68D3EA1311F9CDC178CC0090FA899AEAE830A0C65784D112EEA7044E47
- Ten source inputs rehashed successfully after the build.
- The runtime manifest and link map contain the shared preparation symbol, generated InitializeModules path, ManagedMain, real Settings constructor, and R20 unwind implementation.
- No unverified root object input was consumed.

The candidate was built from the expected starting HEAD plus the listed R22 working-tree inputs. A later independent verification rehashed all 660 inventory files with zero mismatches and rehashed all ten source inputs with zero mismatches.

## Native qualification

The first preflight found an existing foreign managed-acceptance reservation. It was preserved and no guard was created for that preflight. A later fresh preflight at 2026-11-14T16:23:05Z found QAC online with zero reservations, sessions, or QEMU processes. Exactly one candidate-specific one-boot guard was then consumed. No automatic retry was permitted or performed.

| Milestone | R22 evidence |
| --- | --- |
| Native runtime and host startup | Native runtime initialization, RhInitialize stages, TLS bridge, and startup stage 7 were observed. |
| New preparation boundary begins | C188B004 and bootstrap-handoff-begin were observed. |
| Preparation returns | Not observed; C188B005 and bootstrap-handoff-ready are absent. |
| C188B002 and ManagedMain | Not observed in this R22 run because R22 deliberately moved C188B002 after successful preparation. |
| Managed Settings launch and factory | Not observed. The selector-5 launch marker, factory entry, constructor, and factory return are absent. |
| Runner terminal | The nested guest runner timed out waiting for the input loop and requested QMP quit. QEMU then exited with code 0. |
| QAC cleanup | Verified independently: reservation c4568d0fcca845cc836e5cacb78bb27f is Released; owner liveness is Verified; degraded is false; reason is Managed child exited; fresh QAC status/list showed zero processes, reservations, and managed sessions. |

The parent Settings runner's qacReleaseVerified field is false because it did not find its normal nested acceptance manifest after the nested readiness timeout. Independent QAC inspect and post-run census provide the release proof and are preserved in:

E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r22-settings-bootstrap-handoff\qac-settings-bootstrap-20261114T162427624Z-05cdfd7d2dc1496a84c108cd3a4cde31

The boundary analysis is r22-native-boundary-analysis.json in that directory. The consumed guard is:

E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r22-settings-bootstrap-handoff\one-boot-guards\settings-bootstrap-05cdfd7d2dc1496a84c108cd3a4cde31.json

No Settings input was sent because Notes/input-loop readiness was never observed. The QAC lane is clear after cleanup. No second R22 boot was attempted.

## Root cause status and next phase

The proven R21 integration defect is the missing pre-ManagedMain module/code-manager initialization. R22 moves that operation to the direct startup boundary and exposes begin/ready markers. The native attempt establishes that the guest entered the shared preparation function, but not which internal operation failed to return. The remaining possibilities are within the existing initialization sequence, including OS-module registration, code-manager setup, generated InitializeModules, or their runtime prerequisites. Current evidence does not distinguish them and does not prove stack exhaustion or a Settings prerequisite failure.

The next phase should add a small bounded trace inside the shared initialization helper around OS-module registration, code-manager registration, generated InitializeModules entry, and its return. Preserve the real five-argument generated ABI and existing runtime state. Build a fresh candidate, repeat hosted and packaging checks, and require a new clear QAC preflight and fresh one-boot guard before any later guest attempt. R22's consumed guard remains closed.

The Settings factory, constructor, window integration, collection/lifetime check, and native return contract are all still pending native execution beyond this bootstrap boundary.
