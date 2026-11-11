# C188R12R4R5R4R5R2R2R4R2R3R14 — Corrected GC Probe Native Qualification

**Outcome C — `NATIVEAOT_GC_EXECUTION_PARTIAL`**

The corrected runner passed its hosted regressions and the fresh candidate advanced beyond the R13 termination point: it emitted the R13 `ManagedMain` attempt marker and entered the managed-entry bridge. The guest then produced no allocation, collection, return, or post-return state evidence during the bounded wait. QMP shut it down after timeout. This is an incomplete execution, not evidence of a GC or root-lifetime defect.

The captured guest and QAC logs are preserved. Post-capture, the runner itself failed while reading a missing top-level `recordIntegrity` property from the R13 analyzer result. The analyzer/runner schema handoff is now corrected and covered by the hosted R13 suite. Offline reanalysis of the preserved serial confirms the same incomplete guest result. The candidate's one-boot guard remains consumed; no retry was made.

## Repository and historical R13 reconstruction

- Repository: `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT`.
- Branch: `v1.1_DOTNET_SUPPORT`.
- Starting HEAD: `265f665218b2ba39df5b3f241f8515033e1db863`.
- R13 runner-terminal correction used to build the R14 guest: `eb7f59c213e77a588f3a50f16e6c771b004da3a7`.
- R14 analyzer/runner schema correction commit: `b650962f56a9ff34d67b07f11cb50fa14f6f9046`.
- Final source HEAD before this report-only closeout: `b650962f56a9ff34d67b07f11cb50fa14f6f9046`. The documentation closeout is committed immediately after it; the resulting repository HEAD is recorded in the final closeout response.
- No push was performed.

The prior [R13 qualification report](NATIVEAOT_C188R12R4R5R4R5R2R2R4R2R3R13_MANAGED_GC_ROOT_QUALIFICATION.md) records the candidate that actually booted: `out/dotnet/c188r13-managed-gc-root/candidate-r13-final-20261111T062000000Z`. Its consumed guard pinned manifest SHA256 `51DA2728EFAAC65AC26709BFDE062A4572147AB98BC5D853F621FA265ACED4A8`, inventory SHA256 `9D65447A02B6B1E46998C61F6515643A249ED7C84A13CC3DA8C619BDD50021F4`, diagnostic kernel SHA256 `695F8EAB2BCAFE75CFB65A7FF9BC709389886577514FCC9EA20934172B7018B0`, and ramdisk SHA256 `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8`.

That runner treated `[C188R12R4R5R2R3_TRANSITION] result=initialize_modules_returned` as terminal for the generic transition profile. QMP showed QEMU still `running`; the runner then sent host `quit`. The serial contained the R13 `event=attempt` marker but no allocation or collection marker, and no explicit fail-fast. The guest was stopped at an intermediate milestone, so R13 did not establish a GC result.

After that boot, the R13 candidate directory was refreshed with hosted-test and runner metadata. Its refreshed 1,409-file state had manifest SHA256 `AB72AC0BF142383220F9677F4D164BA86B03404CFCA59D935A307561A9F042EA` and inventory SHA256 `CE08882D5ADBA094E6D69CF023A5EDF0D8417DA95ABDD6AB5268FB1F4EBE95DC`; it was not booted. Its directory still had the consumed R13 one-boot guard, so neither that refreshed state nor other incomplete R13 candidate directories were eligible for another launch. The original R13 guard and raw evidence were preserved.

The local runner correction gives R13 its own terminal classifier. It continues through `InitializeModules` and managed-entry milestones, recognizes explicit fail-fast immediately, and waits for a post-`ManagedMain` state result or the bounded timeout. Missing terminal evidence remains incomplete. The corrected source and runner fixtures were committed in `eb7f59c2`; the R14 candidate below was built from that commit.

## Probe and GC readiness

The R13 probe source was not changed. It still enters compiled C#, constructs `DisplaySettingsProbe(640, 480, 2)`, expects `1122`, requests one synchronous `GC.Collect()`, reads the native collection counter, changes the same object's `Scale` to `3`, expects `1123`, and keeps the managed reference alive through the last access.

Pre-boot disassembly and map evidence showed the `RhpCollect` call path, NativeAOT `.managedcode$I` metadata, linked `GcInfoDecoder::EnumerateLiveSlots` and `ReportSlotToGC`, and the same managed reference held in RSI across collection and consumed afterward. The diagnostic kernel uses the pinned NativeAOT Workstation GC source and has no image-backed arena or managed heap substitute. Existing startup evidence showed initialized GC state, an attached thread, ThreadStore membership, PAL FLS identity, stack bounds, module initialization, and code-manager registration. The bounded single-thread probe had no missing safety prerequisite, so the one authorized diagnostic boot proceeded.

These are readiness and generated-code facts only. They do not prove that collection ran or that the guest enumerated or updated the live root.

## Hosted regression and production isolation

All checks in this table ran without a QEMU launch or QAC reservation.

| Check | R14 result | Preserved output |
|---|---|---|
| R13 managed GC/root suite, including terminal-state and analyzer regressions | PASS, 44 assertions | `out/dotnet/c188r14-managed-gc-root/hosted-reruns/r13-schema-fix-regression-final` |
| R11 managed-entry regression | PASS, 23 assertions | `out/dotnet/c188r14-managed-gc-root/hosted-reruns/r11-managed-entry-rerun` |
| R12 managed settings-object regression | PASS, 23 assertions | `out/dotnet/c188r14-managed-gc-root/hosted-reruns/r12-managed-settings-object-rerun` |
| Transition attribution | PASS, 44 fixtures | `out/dotnet/c188r14-managed-gc-root/hosted-reruns/transition-attribution-rerun` |
| Native allocation | PASS, 2,425 assertions, 1,024 reserve/release cycles, and operator-new path check | `out/dotnet/c188r14-managed-gc-root/hosted-reruns/native-allocation-correct-objects` |
| Safe-handoff analyzer/source suite | PASS, 22 analyzer cases and 10 source checks | `out/dotnet/c188r14-managed-gc-root/hosted-reruns/safe-handoff` |
| Safe-handoff runner contract | PASS | `out/dotnet/c188r14-managed-gc-root/hosted-reruns/safe-handoff-runner-contract` |
| QAC admission predicates | PASS, 32 cases | `out/dotnet/c188r14-managed-gc-root/hosted-reruns/qac-admission` |
| Candidate R13 hosted probe suite | PASS, 44 assertions | Candidate `managed-gc-root-hosted-tests/hosted-test-results.json` |
| Production NativeAOT and kernel qualification | PASS; diagnostic GC probe excluded from production | Candidate production qualification manifests |

The R14 R13 suite additionally checks that the top-level GC analyzer forwards `recordIntegrity` and `transitionParseError`, and that the shared runner reads the transition parse error only for the R13 profile. This fixes the post-capture strict-mode exception without changing the other managed-entry analyzer schemas.

## R14 candidate identity and integrity

A fresh candidate was built with the supported candidate builder from corrected source HEAD `eb7f59c213e77a588f3a50f16e6c771b004da3a7`, because no complete, eligible, unbooted R13 candidate remained.

Candidate root: `E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r13-managed-gc-root\candidate-r14-20261111T133436496Z-56112718`.

| Identity | Value |
|---|---|
| Profile | `NativeAotManagedGcRootProbe` |
| Candidate manifest SHA256 | `70830CB0127CF38AECDB60659EFE5EF52D995EE4AD2BF036019DDED8C39CDFCC` |
| Integrity inventory SHA256 | `722013348F0FDE211D8894D4BD282870433F4865B04A00787D560DC852E4179E` |
| Inventory payload files | 1,173 |
| Diagnostic NativeAOT ELF SHA256 | `AD2FE3ACC8F33B70E9EEAD2FFCC7FB51D2DD5BA98A56F44E37AF8606087FDB8E` |
| Diagnostic kernel SHA256 | `B905E2F6AFB49255D21CDDA2A1E3E606CD3B49FE3CF4DEE963CB4ECD761A4FFE` |
| Candidate/source ramdisk SHA256 | `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8` |
| Runtime-pack source commit | `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3` |
| Production isolation | Diagnostic and production outputs separate; probe absent from production; persistent production lifecycle check passed |

Before launch, the candidate-specific guard was absent and the candidate inventory was complete. After the guest, `Test-QacCandidateIntegrityInventory` returned `PASS`, `FileCount=1173`, `UnlistedPayloadFileCount=0`, and `MismatchCount=0`; manifest and inventory hashes remained unchanged. The R14 candidate was not renamed or rewritten to bypass a prior guard.

## One authorized QAC boot

- Run root: `E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r13-managed-gc-root\qac-guest-managed-gc-root-C188R13-20261111T134615409Z-15d34566623c4111b2b4b1aaf32c6168`.
- Before-admission Windows QEMU census: complete, zero processes. QAC was online with zero QEMU processes, zero foreign processes, and zero managed sessions/queued/blocked work.
- Reservation: `a3029b35282e41fa805a682a861b5163`, project `guideXOSServer`, mode `acceptance`; QAC granted the exclusive reservation and launched QEMU PID `29156` at `2026-11-11T13:46:20.2364067Z`.
- Boot bound: 180 seconds; one guest boot; `automaticRetry=false`.
- Consumed guard: `E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r13-managed-gc-root\guest-launch-attempt-candidate-r14-20261111T133436496Z-56112718.json`; SHA256 `3732CF0D0DEA45A6052FE3FDF2E6B9BCBC3DC66DC080D0C21174342BDC01D125`. It records `maxGuestBoots=1`, `automaticRetry=false`, the candidate manifest/inventory identity, and runner SHA256 `C857D7192C08ABA194E8B39B8EC0972DD38D6DAA514B172D39BD71CE692D48FD`.
- QEMU remained running at the end of the terminal-evidence wait. The runner issued QMP `quit`; QMP recorded `SHUTDOWN reason=host-qmp-quit`. QAC logged QEMU exit code `0` at `2026-11-11T13:49:26.7465986Z` and reservation release.
- QAC emitted a during-run warning that its QEMU process census was incomplete and marked the reservation degraded. The initial census had been complete and empty; the post-run census was complete with zero processes, QAC showed zero reservations/sessions, and a current process check found zero QEMU processes. This warning is retained as a qualification limitation.
- Raw serial SHA256: `5615F503E780FF0C137326C8C18CADA72596C3993794BAFF0A50D28050659461`.
- QMP log SHA256: `57ED05D6007A24FEC444618D13E8E7F503B04B480562D80F369ACC079277404C`.

### Guest milestones

| Milestone | Evidence and result |
|---|---|
| M1 — Native runtime initialized | PASS. Runtime, `RhInitialize`, ThreadStore, and GC-ready records were present. |
| M2 — `InitializeModules` returned | PASS. The transition marker was present. It was treated as an intermediate event. |
| M3 — Managed body entered | PASS. `[C188R13_GC_ROOT_ENTRY] event=attempt` identified `ManagedMain`, `DisplaySettingsProbe`, and the one-collection contract. |
| M4 — Managed object allocated and constructed | Not observed. No R13 allocation record appeared. |
| M5 — Initial result `1122` | Not observed. |
| M6 — Collection requested/started | Not observed. The managed body did not produce a collection request marker or a native counter increase. |
| M7 — Collection completed | Not observed. No native collection-completion record. |
| M8 — Live managed reference survived | Not observed. |
| M9 — Post-collection object access and mutation | Not observed. |
| M10 — Managed result `1123` | Not observed. |
| M11 — Native caller received `1123` | Not observed. |
| M12 — Post-return ThreadStore, FLS, and transition-frame checks | Not observed. A pre-entry handoff checkpoint showed ThreadStore membership, PAL FLS match, stack bounds, module/code-manager readiness, and the transition-frame sentinel; it is not a post-collection check. |
| M13 — Shutdown and QAC cleanup | PASS with the QAC census warning above. QEMU exited `0`, reservation released, after-census complete and empty, and candidate integrity passed. |

The last guest serial records were `transition_attach_complete` and managed-entry bridge state. No explicit runtime fail-fast was captured. The runner waited for the bounded interval and stopped the still-running guest because no R13 post-managed terminal state appeared. The first unproven boundary is therefore execution beyond the managed-entry bridge; there is no attributable GC or root-lifetime failure boundary.

## Offline analysis and post-capture runner correction

The original `managed-gc-root-analysis.json` SHA256 is `350E17F6DE61DF30AEFCC7835D79A6B2FEB195B8C5D4F3F0FE1FE6A66153EF53`. It reports `MANAGED_GC_ROOT_INCOMPLETE`, with managed entry attempted but no return, allocation, collection request/start/completion, live-object survival, or post-return state. Its nested transition analysis SHA256 is `8E3D2B9D4F7086F5B127781EFBFC42DC66929409C96EDAE33A9601DB7A468D4A` and has `recordIntegrity=1`.

After QAC released the guest, the runner attempted to read `recordIntegrity` from the top-level R13 analysis object. R13 exposed integrity only in its nested transition analysis, so strict PowerShell mode terminated the runner before it wrote `guest-manifest.json` or an aggregate outcome. This was a post-capture tooling failure, not a guest failure and not a GC result.

The analyzer now forwards `recordIntegrity` and `transitionParseError`; the runner validates both while preserving the separate R13 parse error and only evaluates the transition-specific field for R13. The corrected analyzer was run offline against the preserved raw serial. `managed-gc-root-analysis-postfix.json` SHA256 is `8A1FBD69382AE6EC441D14147CFBDF93D5863679ADBE9B86C7982BF0615D289B`; it again reports `MANAGED_GC_ROOT_INCOMPLETE`, `recordIntegrity=1`, blank transition and R13 parse errors, `evidencePreserved=true`, and no collection or object-survival evidence. The raw serial SHA did not change. No guest was relaunched.

## Limits and next phase

R14 proves that the corrected runner no longer exits on the earlier `InitializeModules returned` marker and that the guest reaches the R13 managed-entry attempt. It does not prove allocation, actual `RhpCollect` execution, native root enumeration, relocation/update of the object reference, post-collection field access, or return of `1123`. The trace does not locate why execution stopped after `transition_attach_complete`; it does not show a GC failure.

Next phase: investigate the narrow managed-entry bridge path after `transition_attach_complete` using the preserved candidate/map/disassembly and host-side analysis first. Once that boundary is understood, build a fresh candidate containing the committed schema correction, run hosted checks, and request a new one-boot authorization. Do not reuse the consumed R13 or R14 guards, and do not broaden the probe into concurrent or persistent managed workloads.

## Preservation

The original R13 serial/QMP evidence and both consumed guards remain preserved. R14's consumed guard and run logs remain under the run root above. The probe source was unchanged. The three pre-existing modified root `.obj` files remain unstaged with their original SHA256 values:

| File | SHA256 |
|---|---|
| `c188r12r4r5r4r5r2r2_native_alloc_operator_new_test.obj` | `3507A24E07257422C98CAB4F7430E094A2C83DF93647E6A02621010DD585A2BD` |
| `c188r12r4r5r4r5r2r2_native_alloc_record_test.obj` | `605D6F7F066BAD3669B52C21A89722A7C9750F9C6595EDFDA26BC06E3125AEC3` |
| `guidexos_nativeaot_native_alloc_record.obj` | `2CD941199D5F34F81F72B0B718A8D1C1023CEAD831464724DEBFEA435F044183` |
