# NativeAOT R36 Settings Factory Guest Acceptance

## Result

**Outcome: E — SETTINGS_LAUNCH_CAUSE_PROVEN.**

The single authorized R36 guest boot reached and processed the real Settings click in Managed Notes. The first incomplete Settings milestone was M9: the native Settings launch did not reach its App Model request. The native trace and exact managed build profile establish why: the kernel requires the C155 Notes return-session handshake, but the packaged managed image was built without the C155 feature define. Its older Notes action path returned success without arming the session, and the kernel stopped before selector 5.

The genuine selector-5 Settings factory did not execute. This is a proven application dispatch/build-profile defect, not a QAC, click-delivery, NativeAOT initialization, or factory-construction result. R36 stopped after this one boot; no retry or rebuild was performed.

## Repository and preservation

- Branch: v1.1_DOTNET_SUPPORT
- Starting HEAD: 690c90748e222935bf35a5986ee40bd2670edca6
- Final HEAD: the in-scope closeout commit; its exact OID is recorded in the task closeout.
- No push was made.
- No runtime, kernel, ramdisk, bootloader, or managed Settings factory was rebuilt. The guest binaries remained the R33/R35-qualified binaries.
- The three pre-existing modified object files remained unchanged and were not staged:

| Protected object | SHA-256 |
| --- | --- |
| c188r12r4r5r4r5r2r2_native_alloc_operator_new_test.obj | 3507A24E07257422C98CAB4F7430E094A2C83DF93647E6A02621010DD585A2BD |
| c188r12r4r5r4r5r2r2_native_alloc_record_test.obj | 605D6F7F066BAD3669B52C21A89722A7C9750F9C6595EDFDA26BC06E3125AEC3 |
| guidexos_nativeaot_native_alloc_record.obj | 2CD941199D5F34F81F72B0B718A8D1C1023CEAD831464724DEBFEA435F044183 |

R34's consumed guard was preserved. R35 had no real guard or guest run. R36 used a fresh, single-use guard for its new attempt; its SHA-256 is F033BD641820B40F84955632D64E36B1C5F6B684EED34683C1F398BE8FBCC006.

## Candidate, package, and runner provenance

R36 reused the already qualified R35 package workflow and unchanged R33 candidate. The fresh wrapper was staged at:

E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r35-settings-launch\package-b665b019537546139402e46d181478e8

| Item | Identity / SHA-256 |
| --- | --- |
| Source R33 candidate | E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r33-settings-handoff\candidate-20261117T050157105Z-0fcf664c63df40e7ae3c8d3d759d5378 |
| R33 source candidate manifest | 303F989980BA1EC7E737C6D9B96469ED4BE995F0FE18AFA86F6286CDCE39A817 |
| Prior R35 execution package | 4c766a47a738476f8f7b8b80d5649d68 (deferred before guard or guest) |
| Prior R35 attempt | 67f513a3d37645759648fce2e34ebb0d (not reused) |
| R36 wrapper / execution package | b665b019537546139402e46d181478e8 |
| R36 execution attempt | 4c71d6d0bb6d4a3d8762816457451cf4 |
| R36 package manifest | 2D2BA00F1C6503D49A404A5DF7044FCBF96570428DD00DD7650C7ABF3736AD8C |
| Candidate manifest | 787A46FFFF09C23355E585F6A7C02BC1ABA19429DED156BC64C19F28A6F510E2 |
| Candidate integrity inventory | 72B913375CA2438C4FB3705D3B1F6AA514DBA0DDB1A46F5F964DEA44BBF89A94 |
| Candidate inventory | 349 files; all hashes matched at staging and post-run revalidation |
| R25 attempt manifest bound to this package | 90C3310A1ACDC29647CA5A3BD601663DD70C54AABF73D35CCF94FAC8DCE6C9AB |
| R25 analyzer | 6E30A583F5145947C820EDD1D414AC00981B156F122DC9F800B915C904CF4105 |
| R34 corrected nested runner (baseline) | D79C5CCB90F68F348E7F53A110C77CAEE6AA13425E318A23684585B1A574B7BC |
| R36 nested Settings runner | EE1FB1C60A8270D745DB6436CAFD96497D7D0DF727EFEC8A0541557E5A86AE6B |
| R36 screenshot target verifier | CF6E9C4B3ED8DB86C603D976B5EC8AA8FDB31B7AB98611654BD24C9125C89A38 |
| R25 parent QAC runner | CF9011A9B50ED181C5459BF226152DA812F3127837B1ABAB2D66E169CB3C9172 |
| R36 top-level runner | 2C4327CE590FBAF19741D78DF47611FF2568E3D9F93C7B05D1A455402170AB34 |
| QAC guest runner | 33D7D724FE9036D1FBA9166DA8674F53E90215454FCB3BD263E8B774721D1DC9 |

The package retained the R35 C188R30_THREAD_ATTACHMENT_TRACE_TWO_DESCRIPTORS profile (n=2, ordered selectors 4,5) and the genuine selector-5 linkage to ManagedSettingsCenter / ManagedSettingsCenter___ctor_0. Candidate hashes matched the R35-qualified values: kernel 9931827D7F6186FF98E1DA580A0B3ED2EAAF725359A7EC096F0ED16ADF495D6B, ramdisk 055B9FDCFEAB8C65D8292F2008F11D6CC39B9134CC3C57B11AC9F30320701374, bootloader D72A35DA882892BA97356A1C006562ABA2908D9DD909152B96DCBB655D6C2D9B, managed ELF 14C61298B00665B9981C7FE177670C529C69B4F8AF2D9EA96BF4C53F888C40C8, and runtime-pack manifest D4AB05BAB6D2B8ED8859BF11B41A8B173B49C910D9C4D3FAA14AE44191C36FAD.

The R32 NativeAOT initialization and ThreadStore fixes remained part of the accepted candidate. R36 did not reopen those subsystems.

## QAC recovery and admission

An early bounded health snapshot found an unrelated RX39 integration acceptance reservation and QEMU PID 24232. It was left untouched. The later read-only R36 preflight at 2026-11-20T04:14:48.3177259Z established a healthy, exclusive lane: coordinator online; status and list succeeded; their counts agreed; 396 host processes were captured; QEMU count, reservations, and managed sessions were all zero. The capture is in E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r36-settings-launch\qac-blocked-preflight-20261120T041448270Z-1f5a1126f9d64765aaa99dc417fe428b. Its directory name says “blocked,” but its contents report exclusiveAdmissionEligible=true.

After the offline checks, QAC granted the R36 attempt reservation f84917c609084540ba49c11e18d8a7ea for project guideXOSServer, with QEMU PID 2788. During-run ownership checks showed one reservation, one managed session, one QEMU process, and zero foreign owners, all consistent with local process evidence. QEMU exited with code 0; QAC marked the reservation Released because the managed child exited. Final status/list showed zero QEMU processes, zero foreign QEMU processes, zero reservations, and zero managed sessions. The runner recorded qacReleaseVerified=true.

## Offline qualification

The R36 wrapper was distinct from R35 and did not alter the candidate binaries. Final offline rehearsal passed, including parent-to-nested runner execution with the actual n=2 candidate profile, both descriptor audits, and the real Notes render/input readiness path. It did not require the obsolete keyboard-focus marker and no stale readiness gate blocked the click. The existing R35 hosted results (547 assertions) were reused for unchanged components. The changed R36 Settings runner and screenshot gate passed 42 focused assertions:

    pwsh -NoProfile -File scripts/dotnet/test-c188r35-settings-launch.ps1 -PackageRoot "E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r35-settings-launch\package-b665b019537546139402e46d181478e8"

Result: C188R35_SETTINGS_LAUNCH_TESTS=42 RESULT=PASS.

The target-verification change compares the live 90×28 button patch with the pinned R34 reference before sending input. In the guest it confirmed a 1280×800 display, target center (815, 550), bounds (770,536)–(860,564), and 2520/2520 matching pixels. Reference screenshot SHA-256: F2FF19367B096ED5360B80014DAB7886EE81DA084768EF6C4BE0442629905CD5; live pre-click screenshot SHA-256: 9E5C4C6CC0AA2831BFABB8CE1696AE329AEC47F6799B151E5132EF601A89E4CE.

## Native guest milestones

Evidence is under E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r35-settings-launch\package-b665b019537546139402e46d181478e8\qac-guest-r25-4c71d6d0bb6d4a3d8762816457451cf4.

| Milestone | R36 result |
| --- | --- |
| M1 NativeAOT initialization completed | **Pass.** Serial trace reached module_initialize_complete and C188B005. |
| M2 Generated InitializeModules completed | **Pass.** Trace recorded C188B007 and completed initialization. |
| M3 Managed Notes rendered and focused | **Pass.** Notes was the visible active managed window. |
| M4 Guest input loop reached | **Pass.** Notes input readiness and focus were established. |
| M5 Both n=2 readiness audits accepted | **Pass.** The ordered managed descriptors were selectors 4,5. |
| M6 Settings button target visible and unobstructed | **Pass.** Live target pixels matched the pinned reference 2520/2520. |
| M7 Settings click submitted | **Pass.** Exactly one left-button click at (815,550) was submitted; no key input. |
| M8 Guest received and processed click | **Pass.** Guest compositor logged button-down x=0000032F y=00000226 button=1 hit=1 window=000003E8; managed dispatch logged selector 4, action 0x18, return 0, result PASS. This is independent guest receipt evidence; QMP acknowledgement alone was not used. |
| M9 Genuine Settings launch dispatched | **Not reached.** The return-session check failed before the App Model launch request. This is the first incomplete milestone. |
| M10 Selector-5 dispatch reached | **Not reached.** |
| M11 Managed ManagedSettingsCenter factory entered | **Not reached.** |
| M12 Factory construction completed | **Not reached.** |
| M13 Factory returned through its ABI | **Not reached.** |
| M14 Native App Model registered the result | **Not reached.** |
| M15 Settings window rendered | **Not reached.** Post-click screenshot still showed Managed Notes and no Settings window. |
| M16 QAC release and guest cleanup verified | **Pass.** QEMU exited 0, reservation released, and final QAC census was empty. |

The complete serial log SHA-256 is BD0A3DD5AB629457B786F9DD87C3B0E66970DBADCEC7FE5B6FEB0DEA144BF862. The target check is recorded in settings-target-visual-check.json, SHA-256 76A63F1FE66B468DAD15D735F575020EE7E24B0ED340C357614C61E26AD2642B. Dispatch evidence classifies the first missing boundary as appmodel-launch-request; it records the Notes action as invoked and successful, but settingsAppModelLaunchRequested=false. No selector-5 dispatch or Settings factory entry was observed.

## Proven dispatch cause

The native click path invokes Notes action 24 and, after that succeeds, checks the C155 return-session state before it arms the return target or requests the Settings App Model launch. R36 recorded:

- [C112-ACTION-DISPATCH] selector=00000004 action=00000018 managedReturn=00000000 result=PASS
- [C155-SESSION-CLEAR-REQUEST] result=FAIL
- [C155-RETURN-PAIR] target=Notes session=missing result=FAIL

The exact packaged candidate explains this sequence:

1. Its candidate manifest binds source head 79f0036c6cdd1e48ee9e73cbfa03c072bc120375, managed mode C160Composite, and a build profile that lists C160 but not C155.
2. The bound R23 candidate builder (build-c188r23-settings-candidate.ps1, SHA-256 352518D8E03C085C99C9C5F14C38117C24A60AE86E16BCF9F5C9E06F63893776) passed -ManagedProjectMode C160Composite and -C156ControlModifierShortcuts, but omitted -C155ManagedNotesSession.
3. build-managed-hostlog-proof.ps1 adds HostLogProofC155ManagedNotesSession=true only when that C155 switch is passed. The project file defines HOSTLOGPROOF_C155_MANAGED_NOTES_SESSION only when that property is true. C160 enables the C152 save workflow, but does not imply C155.
4. The managed action-24 C155 arming path is conditional on that define. Without it, the older Notes action path can return success without creating the return session. The guest's C155 check/clear then fails, so native dispatch returns before selector 5. The packaged managed ELF also lacks the C155 session markers, consistent with the profile and guest trace.

This establishes a specific managed/kernel feature-flag mismatch in the R33 candidate build composition. The selector-5 factory linkage was present, but linkage does not prove runtime factory entry. No code was changed in the R33 candidate or its binaries during R36.

## Next action

Correct the managed candidate build configuration so the C155 Notes session feature is enabled wherever the kernel's GXOS_NATIVEAOT_C155_MANAGED_NOTES_SESSION path is enabled. Then qualify a new candidate and execution package with a new identity and, after a fresh QAC admission check, a separately authorized guest attempt. Do not reuse the consumed R36 guard or R34's consumed guard. The R36 attempt is complete at outcome E; no further boot was made.

## Evidence index

- R36 package manifest: E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r35-settings-launch\package-b665b019537546139402e46d181478e8\r35-execution-package.json
- R36 run summary: E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r35-settings-launch\package-b665b019537546139402e46d181478e8\r35-run-evidence-4c71d6d0bb6d4a3d8762816457451cf4.json
- Settings dispatch evidence: E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r35-settings-launch\package-b665b019537546139402e46d181478e8\qac-guest-r25-4c71d6d0bb6d4a3d8762816457451cf4\r12r2r1-settings-dispatch-evidence.json
- Complete guest serial: E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r35-settings-launch\package-b665b019537546139402e46d181478e8\qac-guest-r25-4c71d6d0bb6d4a3d8762816457451cf4\serial.log
- Before/after screenshots: before-settings.png and settings-check.png in the guest evidence directory above
- R35 deferral and prior qualification: docs/dotnet/NATIVEAOT_R35_REAL_SETTINGS_FACTORY_NATIVE_LAUNCH.md
