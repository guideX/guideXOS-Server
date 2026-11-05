# C188R12R4R5R4R5R2R2R4R2R1 — NativeAOT Safe-Handoff Guest Acceptance

**Outcome: SAFE_HANDOFF_DIAGNOSTIC_INCOMPLETE.** One QAC-authorized boot reached the intended diagnostic safe stop after successful NativeAOT runtime and Workstation GC initialization, PAL TLS/FLS setup, OS module registration, and code-manager registration. The guest stopped before InitializeModules and any managed transition. The initialization stage trace itself is complete, but the separate native-allocation backend record overflowed once and the safe-handoff analyzer misparsed the guest's 16-digit sequence values. The evidence is therefore not sufficient for full bounded-diagnostics acceptance.

This outcome is not a runtime initialization failure and does not establish that the prior InitializeModules fail-fast is fixed. ThreadStore attachment, managed execution, Notes, Settings, and collection were not attempted or accepted.

## Baseline and preservation

| Item | Verified value |
|---|---|
| Repository / branch | D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT / v1.1_DOTNET_SUPPORT |
| Starting HEAD | 4b80325161738acc3a6fb466e2b4e3b526087bef |
| HEAD after guest execution and before closeout commit | 4b80325161738acc3a6fb466e2b4e3b526087bef |
| Starting working-tree changes | Three pre-existing modified tracked object files listed below |
| Guest boots | Exactly one QEMU launch; no retry |
| Source ramdisk | ESP/ramdisk.img SHA-256 AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8 |

The source ramdisk hash still matches the required value. The following three pre-existing tracked object files retained their recorded hashes and were excluded from staging:

| File | Preserved SHA-256 |
|---|---|
| c188r12r4r5r4r5r2r2_native_alloc_operator_new_test.obj | 3507A24E07257422C98CAB4F7430E094A2C83DF93647E6A02621010DD585A2BD |
| c188r12r4r5r4r5r2r2_native_alloc_record_test.obj | 605D6F7F066BAD3669B52C21A89722A7C9750F9C6595EDFDA26BC06E3125AEC3 |
| guidexos_nativeaot_native_alloc_record.obj | 2CD941199D5F34F81F72B0B718A8D1C1023CEAD831464724DEBFEA435F044183 |

The previous PAL-capacity guest closeout is docs/dotnet/NATIVEAOT_C188R12R4R5R4R5R2R2R4_SAFE_HANDOFF.md, which also points to the R4R1 PAL-capacity acceptance report. The previous raw serial identity remains D9F14AC9DB9E39868D26F2CDBA798815D6A33C89A475DC9BEAFE998FFAD28FBF; its runner analysis remains F12A36E27F6D284A78453F0BCB37476E8A6E30B251B12AE4165736D8794EFD06. The previous run established 256-slot PAL occupancy, the 24-byte backing request, GCEvent::Impl allocation/construction, semi-shared initialization, GCHeap::Initialize, RhInitialize, and module registration. It did not establish ThreadStore attachment, managed entry, or collection.

The candidate PAL slot source remains kernel/core/nativeaot_gc_vm_slots.h at SHA-256 C536DC8218AB6E18193DC8E3B442D2BFC7EF512D72C1EDF2156FB70581CF5626. The accepted 256-slot hosted and guest evidence was reused as an unchanged baseline; no rebuild was run that could rewrite the pre-existing object files.

## Candidate identity and integrity

The exact candidate root booted was out/dotnet/c188r12r4r5r4r5r2r2r4r2-safe-handoff/candidate-20261105T054500000Z-linkedtrace/. Its candidate-manifest.json SHA-256 is 48316C803A164C9C99B278617540A75FC2F1691FC39A52BAA919799D70B5BCA9. The nested staged-resume-20261105T055402859Z/qualification-manifest.json SHA-256 is 70CEA263AE01D191A1EB53CF3227053AEAC73150C03956E5D7C9CF7B964AA42B. All 18 declared files were checked at their manifest-relative paths; every full SHA-256 and size matched.

| Declared file | Role | Bytes | SHA-256 | Result |
|---|---|---:|---|---|
| diagnostic/analyze-c188r12r4r5r2r2r4-safe-handoff.ps1 | Safe-handoff analyzer | 18035 | 49ED2A111698485096FF97B1D30CB70C860CA1CDF10408DC87EACFD1380288F7 | PASS |
| diagnostic/analyze-c188r12r4r5r4r5r2r2-native-alloc.ps1 | Native-allocation analyzer | 15551 | C9A1F4B2BC26F90C951B346B0C530052C8EAFED184E54DE989D292EEFF26516D | PASS |
| diagnostic/run-c188r12r4r5r2r2r4-safe-handoff-qac-guest.ps1 | Pinned candidate runner | 1864 | EFD296C289B8EEE68E89FCE472D644BA46BD9CA1CEE79B87BA14E8AF5FA4E8E6 | PASS |
| diagnostic/safe-handoff-hosted-test-results.json | Hosted qualification results | 1767 | 5D30D684960903FA096C2052454E3985C918BCD8290D0245FAFD3098F223CABB | PASS |
| diagnostic/safe-handoff-stage-map.json | Initialization stage map | 4345 | 4EEE4BA6FA31BF9727D21EEA4A881B144DE9861C378BDB52A2BC4066970F34C4 | PASS |
| diagnostic/test-c188r12r4r5r2r2r4-safe-handoff.ps1 | Safe-handoff hosted tests | 21751 | EFB0F79471AB9B084C1240862E023372346CEDD85B591D1BC9272162F1F0F5FF | PASS |
| diagnostic/esp/kernel.elf | Diagnostic guest kernel | 3474104 | 8D84045384518E1E2D1390AD22A86915DD1B2027D477CE2AB31B676A3FE63966 | PASS |
| diagnostic/esp/ramdisk.img | Source ramdisk | 67108864 | AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8 | PASS |
| diagnostic/esp/EFI/BOOT/BOOTX64.EFI | UEFI bootloader | 52224 | D72A35DA882892BA97356A1C006562ABA2908D9DD909152B96DCBB655D6C2D9B | PASS |
| diagnostic/runtime-pack/gc.cpp | Workstation GC runtime | 1933318 | 12032F90901CECC4F8D1AA9923E135FAEAB7DE300AAAF407412430FDB0FAC37D | PASS |
| diagnostic/runtime-pack/gcwks.cpp | Workstation GC adapter | 724 | 399CC34A0C1D3EE9EE544CFF63AFA9A9FD213DB410467D8B931C688C0465E446 | PASS |
| diagnostic/runtime-pack/guidexos_gcenv.cpp | guideXOS GC environment | 17766 | 93F17C5E56FC83295FEEBF8AE2F5E8FC3DE568D1825F439B584A6BFB632789F5 | PASS |
| diagnostic/runtime-pack/guidexos_nativeaot_native_alloc_record.cpp | Bounded allocation record implementation | 12032 | 042A10A3C849D259287D3155F2C1326192A413505AEB0936E616059112AB117D | PASS |
| diagnostic/runtime-pack/guidexos_nativeaot_native_alloc_record.h | Bounded allocation record contract | 8442 | 2CC16FA567A64FB067DFD0539C4A7CD56EA7481EAB6CFD21C29C1052EEC90919 | PASS |
| diagnostic/runtime-pack/guidexos_nativeaot_pal_contract.cpp | NativeAOT PAL contract | 21119 | C5DDFAF982A9A9DA118EB47969804BC62B610B991A48904BE2C300247F78CCAA | PASS |
| diagnostic/runtime-pack/guidexos_nativeaot_semishared_record.cpp | Semi-shared diagnostic implementation | 12126 | AFDE4E8797EA9CA98B0BF67489426864AA1FCFADA09FD3F3DF771D5131FA8AD4 | PASS |
| diagnostic/runtime-pack/guidexos_nativeaot_semishared_record.h | Semi-shared diagnostic contract | 6071 | B2021B42EB8107C77298B574E58376366B120E16126FF5E5F07D4335A4721B7D | PASS |
| diagnostic/runtime-pack/guidexos_nativeaot_startup_imports.cpp | NativeAOT startup imports | 33550 | 7EE4F99ACC296A85BE8E04EA07799A25F16E6E3207185A07BC2EA763BE8BC0F1 | PASS |

The candidate manifest selects mode NativeAotSafeHandoff, sets c188R12R4R5R2R2R4SafeHandoff=true, and records the transition as disabled before RhpReversePInvoke and InitializeModules. Its semantics explicitly state initialization accepted, module registration accepted, intentionally stopped, and no managed transition or runtime acceptance. The diagnostic NativeAOT ELF identity is 247D1D97FD773BA6BB588C0D62EFF78C2B969CF31DB4AABF32C86EA53BB05EB8. The diagnostic kernel identity is 8D84045384518E1E2D1390AD22A86915DD1B2027D477CE2AB31B676A3FE63966. The staged source ramdisk identity is the required AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8.

The candidate used the 64-stage fixed initialization summary recorder with four saturating counters per stage and independent first-failure storage. Its declared native-allocation backend capacity was nine. The pinned analyzer SHA-256 is 49ED2A111698485096FF97B1D30CB70C860CA1CDF10408DC87EACFD1380288F7; the pinned candidate runner wrapper SHA-256 is EFD296C289B8EEE68E89FCE472D644BA46BD9CA1CEE79B87BA14E8AF5FA4E8E6. Candidate hosted results and stage-map identities are 5D30D684960903FA096C2052454E3985C918BCD8290D0245FAFD3098F223CABB and 4EEE4BA6FA31BF9727D21EEA4A881B144DE9861C378BDB52A2BC4066970F34C4.

The candidate manifest's generic runnerVersion string is c188r12r4r5r4r1-one-boot-guard-planned. The exact staged wrapper and analyzer hashes above, plus the actual host runner hash recorded below, identify the executable contracts used for this boot.

## Hosted preflight

The focused hosted preflight passed before guest admission. The preserved logs are under out/dotnet/c188r12r4r5r4r5r2r2r4r2-safe-handoff/hosted-preflight-20261105T161407516Z/ and hosted-runner-correction-20261105T162414977Z/.

| Check | Result |
|---|---|
| Safe-handoff source contracts and analyzer fixtures | PASS: 10 source-contract checks and 13 analyzer cases, including ordering, safe stop, overflow, and preservation fixtures |
| QAC admission predicates | PASS: 32 cases |
| QAC ownership predicates | PASS: 15 cases |
| QAC stream capture | PASS: 2 cases |
| QMP event and raw-serial preservation | PASS |
| ThreadStore readiness regression | PASS |
| GC initialization probes | PASS |
| R3 initialization return contract | PASS |
| R1 initialization attribution | PASS |
| Semi-shared record regression | PASS: 30 assertions |
| Runner contract after correction | PASS: candidate capacity field, stage-map loading, fresh census and one-boot-guard ordering |
| Final runner contract after QAC ownership audit | PASS: QAC-owned run path and pre-launch guard; log SHA-256 589D518B02C60C40E910B312DB2FB4D6C4F9E574A2AE2CC9E644FEF1374C45BB |
| PAL capacity baseline reused | PASS: 2,425 slot assertions and 1,024 reserve/release cycles from the accepted prior baseline |
| UI baseline reused | PASS: 40 suites / 969 cases from the prior qualified baseline |
| git diff --check | PASS |

Two earlier runner invocations stopped before QAC admission and before any QEMU launch: the runner referenced a capacity field absent from the candidate manifest and did not load the verified stage map. The narrow runner corrections and a new hosted runner-contract test were completed and passed before the single guest launch. The actual host runner SHA-256 used by the guard was FDA758FA79474EED4ADBC3BFA3CC34720892C943DB2B73548845EC6AC803923F. The pinned candidate analyzer and wrapper remained unchanged.

The final runner-contract log is out/dotnet/c188r12r4r5r4r5r2r2r4r2-safe-handoff/hosted-final-runner-contract-20261105T165016801Z/runner-contract.log.

## QAC admission, ownership, and one-boot guard

The fresh pre-admission census is retained in qac-guest-r2r2r4r2r1-001/qac-before.*. QAC was online; its reservation list was empty; the complete Windows QEMU census found zero processes; process inspection completed; no foreign or conflicting owner was present. QAC granted exclusive acceptance reservation 701dafa109ef461ea48e0617cb04129c to guideXOSServer. Owner PID 8568 was LEON2_RDT\guideX; the owned QEMU child was PID 19724, with parent PID 8568. The during-run inspection is qac-during-002.inspect.json.

The phase-specific guard is out/dotnet/c188r12r4r5r4r5r2r2r4r2-safe-handoff/guest-launch-attempt-C188R12R4R5R4R5R2R2R4R2R1.json, SHA-256 F71AAC7495F0D7672B60B3356AA5118234ED7EE86918F268ECC2401A1BCE9DB9. It records maxGuestBoots=1, automaticRetry=false, the candidate manifest, ELF, kernel, ramdisk identities, and the host runner hash. Its attemptedAtUtc is 2026-11-05T16:25:20.8441188Z; QAC's grant timestamp is 2026-11-05T16:25:21.1260432Z, so the guard preceded the reservation grant by about 0.28 seconds. The prepared runner creates the guard after the clear census and before invoking QAC's run command. The QAC CLI's run command acquires the reservation and launches QEMU as one operation; its separate request command holds a reservation but does not launch QEMU. Therefore the requested literal post-grant guard ordering was not met by this runner. QAC still granted exclusive admission before its QEMU child started, and no QEMU launch occurred without the one-boot guard. This procedural deviation is retained as a qualification limitation. QAC launched the candidate's ESP directory; the serial command line identifies the exact candidate root above.

## Guest milestones and stopping boundary

The original raw log is out/dotnet/c188r12r4r5r4r5r2r2r4r2-safe-handoff/qac-guest-r2r2r4r2r1-001/serial.log. It has 955 lines and 107,658 bytes; its SHA-256 before and after analyzer processing is 27C653B08FCF0CBBD4696498FE86AD97591DA3EFFB7F27BD40A2230A18CE425C. Analysis did not rewrite the raw serial.

The execution evidence shows this order:

1. Native adapter entry: raw thread marker at serial line 170.
2. PAL and runtime initialization: stages PalInit (0x10), PAL FLS (0x11), GCConfig (0x12), GCToOS (0x13), GCVM adapter init (0x55), and the runtime initialization stages all report pass/observe without failure.
3. Workstation GC initialization: GCInitialize (0x30), heap publication (0x31), GCHeapInitialize (0x32), finalization (0x33), and handle manager (0x34) report pass. The separate GC-init record is complete: finalHeapHresult=0, initializeGcHresult=0, semi-shared initialization result=1, wait-event allocation and creation results=1, and firstFailureSite=0.
4. GCHeap::Initialize completion: the GC-init diagnostic classifies gc_heap_initialize_succeeded; the bounded stage summary for 0x32 is pass with result 0.
5. RhInitialize success: the raw thread marker at line 180 and later init-result marker report nativeRhInitializeBoolean=1 and startup adapter status=0.
6. Safe-stop gate state: managed_entry_gate_closed is reported at serial line 911 before TLS setup and registration. This confirms the managed-entry gate is closed.
7. TLS/FLS setup: PAL FLS stage 0x11 passes; tls_installed and tls_gsbase markers at lines 914-915 carry nonzero TLS block, vector, and GS base.
8. RuntimeInstance and ThreadStore publication: lines 916-919 report published pointers and current-thread TLS/state. The current-thread accessor remains null and currentThreadAttached=false, so this does not qualify attachment.
9. OS module registration and code-manager registration: raw module_registration_enter and module_registration_complete markers appear at lines 920-921; bounded stage 0x61 and 0x62 each record one pass with result 0.
10. Explicit safe-stop completion: serial line 953 contains diagnostic_safe_stop; line 954 reports intentionally_stopped, initialization=accepted, moduleRegistration=accepted, managedTransition=not_attempted, managedEntry=not_reached, runtimeError=none; line 955 is SAFE_HANDOFF_STOP.

The corrected direct decoder is retained as bounded-stage-summaries-parsed-v2.json and safe-stop-classification-v2.json in the guest evidence directory. The initialization trace contains 29 stage summaries and 93 events, of which 64 were coalesced into bounded summaries; 93 event outcomes are represented. Trace overflow=0, serialDropped=0, no first-failure record, terminal adapter status=0. The trace is complete for initialization-stage evidence and has no repeated uncontrolled summary emissions.

The trace also shows 10 GCVMCommit (0x52) begin/pass events, all passing. Module registration and code-manager registration each pass. There is no failure marker contradicting those successes. The prior fail-fast code 0x50414C06 did not appear during this run.

The safety boundary is confirmed by the raw log and completed stage map: no InitializeModules stage (0x63), module_initialize_enter, transition_enter, managed_dispatch, managed_main_call, or reverse-P/Invoke entry was observed. No managed entry was reached; the similarly named marker at line 911 says managed_entry_gate_closed and records the gate state. The candidate intentionally stopped before InitializeModules and any managed transition.

No ThreadStore attachment, reverse P/Invoke, ManagedMain, managed entry, isolated object-graph proof, GC collection, Notes, or Settings operation was executed. The runtime and module-registration milestones passed; managed-runtime acceptance remains false.

## Diagnostic completeness and independent statuses

The initialization stage recorder did not overflow. However, the auxiliary native-allocation backend record has capacity nine, stored nine attempts, and reports overflow=1. The runtime-side attempt record captured eight entries with runtime overflow=0. The native-allocation analyzer therefore returns INCOMPLETE_DIAGNOSTIC_RECORD and provides no allocation details. Its first-failure index is 0xFFFFFFFF; no allocation failure was recorded. The stage trace shows 10 GCVMCommit passes while the backend retained nine allocation attempts, but the backend records have no initialization-stage ID, so the dropped append cannot be conclusively assigned to GCVMCommit.

The runner's top-level diagnosticErrorStatus is none and runtimeErrorStatus is none; the GC-init and initialization stage records show successful native initialization. The native-allocation diagnostic status is independently incomplete because of the one backend overflow. The runner's aggregate diagnosticEvidenceComplete=true did not account for this auxiliary record; nativeAllocationRecordCaptureComplete=false is the more specific result and is preserved in guest-manifest.json.

There are also tooling inconsistencies. The pinned safe-handoff analyzer returned MALFORMED_OR_CONTRADICTORY_MILESTONE_ORDER because its line-order regular expression accepts the 16-digit sequence alternative without the seq= prefix, while the guest emits seq= followed by 16 hex digits. The raw sequence order itself is intact and shows module registration entry before completion. The GC-init source analyzer reports traceRecordCountMatches=false and initTraceComplete=false because it counts 30 trace records (29 stage summaries plus the terminal row) against the terminal count of 29 stages. Direct decoding shows the terminal count is the stage count; no stage overflow or serial loss occurred. The original analyzer outputs remain unchanged, and the corrected post-processing files are explicitly labeled v2.

Statuses are kept separate:

| Dimension | Observed result |
|---|---|
| Runtime initialization | Successful through RhInitialize and GCHeap::Initialize; runtimeErrorStatus=none |
| Initialization-stage diagnostics | Complete: 29 stages / 93 events / overflow 0 / serial dropped 0 / no first failure |
| Native-allocation diagnostics | Incomplete: backend overflow 1; no runtime allocation failure reported |
| Tooling | Analyzer ordering false negative and GC-init record-count mismatch; raw serial remains authoritative |
| Managed transition | Intentionally not attempted; ThreadStore attachment and managed runtime not accepted |

## QAC shutdown and retained evidence

The runner classified its result as DIAGNOSTIC_EVIDENCE_INCOMPLETE and exited 2; QAC recorded the owned QEMU child exit code 0 and released reservation 701dafa109ef461ea48e0617cb04129c. qmp.log records query-status=running, the host-issued quit command, and SHUTDOWN reason host-qmp-quit. The retained QMP log SHA-256 is 47E8B4FEB769C2E835AD025814211A5130F198D705CA615CDC2BD99C065ADDBE. No foreign process was terminated, and no recovery manifest was needed.

The final census is retained in qac-final-20261105T163317408Z.*: QAC online, zero reservations, zero managed sessions, zero queued or blocked sessions, zero QEMU processes, zero foreign QEMU processes; the complete Windows QEMU census also reports zero processes. The coordinator process inspection is retained alongside those snapshots.

Key retained evidence identities include:

| Evidence | SHA-256 |
|---|---|
| Raw serial, serial.log | 27C653B08FCF0CBBD4696498FE86AD97591DA3EFFB7F27BD40A2230A18CE425C |
| Runner analysis, serial-analysis.json | 0662275A98FE6A05E06CF97172C433FD98A55661A7896ACCFF3F4F6DA4CA56A3 |
| GC-init source analysis | 57B9AF53FB11AB68C9B41029218ECB160609C195D8015D3CB0236A46358810A4 |
| Native-allocation analysis | FAD9D06FC8F84359C3051E39266AF36308DA0697A9740C7C82FF4FC168FF6895 |
| Corrected bounded-stage decode, v2 | CB72BCE6853BD26F03C3F09296A2DAD8F874D29E545A596B1C27F06376871BED |
| Corrected safe-stop classification, v2 | FD800A307F1598E9B3F9BF59E74484B0F34E0C8C86D762D1DC2066E4E00E4365 |
| Guest manifest | A89A1319B0989B37B09E47E693E58EF45E1F48E6F4592B17EDF03138314A4524 |
| QAC ownership identity | A6D7A979C2F9A51470544776CFBDB9DB8E4BB1A6CF133DAD21CDE906775BC093 |
| QAC stderr / reservation lifecycle | 2FFCA71EC34B996C40FDF055307D79E6CE8E90E210B2DD2A764C40BF42514BB1 |
| QMP command/response log | 47E8B4FEB769C2E835AD025814211A5130F198D705CA615CDC2BD99C065ADDBE |
| Final runner invocation log | CE5B2835A04B905F5AE9667F7DA20811D24B50EE253183C4BDE6C59B980768E9 |

The source paths are under out/dotnet/c188r12r4r5r4r5r2r2r4r2-safe-handoff/qac-guest-r2r2r4r2r1-001/, except the final runner invocation log under hosted-runner-correction-20261105T162414977Z/. Candidate manifests, QAC before/during/after snapshots, one-boot guard, QAC stdout/stderr, and hosted runner/analyzer logs remain in their phase output directories.

The original raw serial and historical PAL-capacity raw serial were not rewritten. The three modified object files and the original ESP ramdisk retained their required hashes.

## Classification and next engineering action

**Classification: SAFE_HANDOFF_DIAGNOSTIC_INCOMPLETE.** The guest reached the intended safe stop and demonstrated successful supported initialization and registration. The initialization trace is complete, but one bounded native-allocation backend append was lost and pinned tooling did not correctly classify the 16-digit sequence markers. The runner also created its one-boot guard before QAC's reservation grant, as described above. These limitations prevent Outcome A. No specific native initialization or registration defect was observed, so this is not Outcome B. The safe stop prevented a prohibited transition, so this is not Outcome E. QAC admission, ownership, and launch prerequisites succeeded, so this is not Outcome D.

The smallest next engineering action is hosted-only: correct the safe-handoff analyzer's 16-digit seq= line matcher and add a matching fixture; clarify its terminal stage-count semantics; and raise or otherwise bound the native-allocation backend capacity to cover the observed tenth startup call while keeping the independent overflow signal. Before another guest run, resolve the guard-order requirement through a QAC-supported launch flow that can preserve QAC ownership and the one-boot guard. Re-run focused hosted overflow and analyzer tests before requesting a future guest boot. This run used its sole boot; no second boot is authorized here.

After bounded diagnostics are complete, the next guest phase may focus specifically on ThreadStore attachment and the InitializeModules path, including attribution of the previous 0x50414C06 PalPrintFatalError fail-fast. Avoiding that transition in this phase is safe-stop acceptance evidence only and does not show that the transition defect is fixed.
