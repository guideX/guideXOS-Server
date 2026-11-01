# NativeAOT C188R12R4R5R4R5R2R1 Semi-Shared Guest Attribution

## Result

**Outcome B — SEMISHARED_FAILURE_SITE_PROVEN.** The one authorized guest identifies the first failing Workstation GC semi-shared branch as **0x0120 GX_SEMISHARED_OP_BGC_DONE_EVENT**. Its nested first failed operation is **0x0140 GX_SEMISHARED_OP_EVENT_IMPL_ALLOC**: a nothrow allocation for an 8-byte GCEvent::Impl returned null. The record has no allocator or PAL error explaining why, so the failure site is proven but the platform root cause is not.

No repair is justified yet. The narrow next step is targeted allocator-path instrumentation at this GCEvent::Impl allocation, preserving allocator behavior, to learn why it returns null.

## Baseline and preservation

- Branch: v1.1_DOTNET_SUPPORT.
- Starting HEAD and source HEAD at closeout preparation: a900deae8b997830652e853b62b17564b9404339.
- Candidate source commit: 6ba37a5691e561206df4604c613fa91c6b0861d5, an ancestor of the starting HEAD.
- The report and analyzer changes were prepared on the same branch; no branch switch, reset, stash, history rewrite, or push occurred.
- The pre-existing tracked modification to ESP/ramdisk.img was preserved. Its SHA-256 before and after the guest was AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8. It is not included in the closeout commit.
- Existing generated and untracked bin/obj outputs were left in place.
- The final closeout commit is on this branch; the HEAD recorded before that reporting commit is listed above.

## Candidate integrity

Candidate root: out/dotnet/c188r12r4r5r4r5r2-semishared-attribution/candidate-20261101T225112623Z/

The candidate manifest SHA-256 is DF24081DADF8505888D17A3E4C0F8700DE18B883F67DA20D6D9A8DEB29B41936; its result is PASS_BUILD_READY_FOR_SEMISHARED_ATTRIBUTION_GUEST. The post-run review resolves all 44 declared root artifact hashes: 39 against candidate-tree files and five against their declared source-provenance files. The seven staged-final payloads each match their declared size and SHA-256. The candidate source commit remains an ancestor of the checkout.

The guest caused QEMU to create candidate.../esp/NvVars (9,788 bytes; SHA-256 380831A9852EF7B0B3BB9DA2DB5DE5D7D7FD579EBEAD4EEB69D527E4EEFA2146). It is retained as writable ESP state; it is not one of the seven staged payloads. The candidate file inventory increased from 930 to 931, with no staged-payload change.

Integrity identities:

| Artifact | SHA-256 |
| --- | --- |
| Diagnostic NativeAOT ELF | 7B5DF43C0FFCC2B0F94BF5251CF3FFE03BF2491885E9D10CF11CCBB44C4A6CD3 |
| Diagnostic kernel | 065999CAE0C9BD9100BD58590E305A1B122AA9F4DF5F7554CE8BECC1D7494CEA |
| Production NativeAOT ELF | FD610CAF813F472A548A146035D7638EF8A7D8844690F9AC8C433337D810154C |
| Production kernel | A21A951C8DF337CEA08A7FE93580C556F0AC87F33D1B1766070FFF204B1EA168 |
| Diagnostic semishared record implementation | AFDE4E8797EA9CA98B0BF67489426864AA1FCFADA09FD3F3DF771D5131FA8AD4 |
| Diagnostic semishared record header | B2021B42EB8107C77298B574E58376366B120E16126FF5E5F07D4335A4721B7D |

The diagnostic ELF includes the R2 record accessors and all 11 active failure branches; 0x0102 is excluded because MULTIPLE_HEAPS is not compiled. The Workstation GC replacement object, runtime-pack GC-init and ThreadStore/PAL replacements, and pinned runtime source commit passed the candidate checks. Production NativeAOT and kernel outputs exclude the diagnostic record, and the production persistent-composite lifecycle remains enabled.

The complete post-run hash audit is out/dotnet/c188r12r4r5r4r5r2-semishared-attribution/postrun-candidate-integrity-reviewed-codex-20261101T235610647Z.json. The original pre-run candidate audits remain alongside it.

## Hosted preflight

All required hosted qualifications completed before the one guest. After the narrow analyzer correction, the semi-shared test and dependent init tests were rerun and passed.

| Qualification | Result and retained output |
| --- | --- |
| R2 semi-shared native record and analyzer fixtures | PASS; 30 native assertions plus revised nested-operation, close-status, incomplete, missing, and malformed-record cases. out/dotnet/c188r12r4r5r4r5r2-semishared-attribution/hosted-20261101T234800375Z/ |
| GC-init probes | PASS. out/dotnet/c188r12r4r5r4r5-gc-init-probes/hosted-20261101T234945594Z/ |
| R1 initialization attribution and raw-serial preservation | PASS. out/dotnet/c188r12r4r5r1-runtime-init-attribution/hosted-20261101T234945608Z/ |
| ThreadStore readiness and PAL focused checks | PASS. out/dotnet/c188r12r4r5r4-threadstore-readiness/hosted-20261101T233859100Z/ |
| R3 initialization return contract | PASS. out/dotnet/c188r12r4r5r3-init-return-contract/hosted-20261101T234945649Z/ |
| QAC admission and GC ownership | PASS; admission suite had 32 cases. |
| C188 link boundary, factory, lifetime and runtime lifecycle | PASS. |
| QMP capture and runner admission/ownership predicates | PASS. |
| Managed UI baseline | PASS; 40 suites, 969 cases, zero failures. out/dotnet/c188r12r4r5r4r5r2-semishared-attribution/ui-baseline-20261101T230843523Z/ |
| PowerShell AST parsing and git diff --check before guest | PASS. |

The regression and runner-predicate logs are under hosted-regressions-20261101T230700599Z, hosted-regressions-followup-20261101T230805740Z, and runner-predicate-tests-20261101T231334784Z in the R2 attribution output root.

## QAC admission, one-boot guard, and ownership

The fresh live prelaunch census at 2026-11-01T23:44:01Z reported QAC online, valid status and list snapshots, zero reservations, zero managed sessions, zero inspections, a complete Windows QEMU census with zero processes, no warnings, and a clear exclusive lane. The runner performed its own fresh census immediately before admission.

The R2R1-specific guard at out/dotnet/c188r12r4r5r4r5r2-semishared-attribution/guest-launch-attempt-C188R12R4R5R4R5R2R1.json has SHA-256 DD8BF3302DCF13DEAF9AE3A1E46E65796E8FA62D760C8978DE911A94F0887281. It binds the candidate manifest and diagnostic artifacts, sets maxGuestBoots=1 and automaticRetry=false, and records the actual runner script SHA-256 0C9AAB5733641539B5F8AF77968D63E562F2A92A5F261D666C6AA276904466E3. That launch hash matches the current runner. The candidate manifest's runner-version text is older than this guard; the guard's recorded script hash is the launch identity.

Exactly one QEMU guest was launched:

- QAC reservation: 6f4e504b81714f5da4ce73ee14ad7cc8.
- QAC owner PID: 11164; runner orchestration PID: 4992.
- QEMU child PID: 26180, parent PID 11164, executable C:\Program Files\qemu\qemu-system-x86_64.exe.
- The valid qac-during-003 inspection showed degraded=false, verified owner liveness, and the matching child process identity.
- QAC stderr contains a transient warning that a QEMU process census was incomplete. The captured during-003 snapshot was complete and consistent; the independent post-run census was also complete, had no warnings, and found zero reservations, sessions, or QEMU processes.
- QEMU exited with code 0 after the host sent QMP quit. The trace records SHUTDOWN with reason host-qmp-quit.

Fresh admission evidence is in qac-live-prelaunch-r2r1-final-20261101T154401007/admission-preflight.json. Ownership snapshots and the post-run release census are under qac-guest-r4r5r2r1-001/ and qac-postrun-final-codex-20261101T155103378/. The release census reports exclusiveLaneClearAfterRun=true.

## Guest result and first failure

Raw serial: out/dotnet/c188r12r4r5r4r5r2-semishared-attribution/qac-guest-r4r5r2r1-001/serial.log
Raw serial SHA-256: 39402A5495C0DF35492C403B776EC9214A26D64D31B2D1CA558F196124905C5B

The serial trace contains the complete 55/55 initialization events and the complete semi-shared record: 103 marker lines, 16 operation slots, no operation overflow, and no issues in the reviewed analysis. It records:

1. Native adapter entry and raw RhInitialize result false (00000000); adapter status 00000001.
2. Runtime instance, ThreadStore, GC heap, GC handle manager, and configuration singleton bits reached 0x1F. The ThreadStore handoff was not ready.
3. GCHeap::Initialize() entered Workstation GC initialization and called init_semi_shared().
4. The earlier mark-list and segment-table allocations succeeded. Both full-GC events and their nested implementation, handle, and platform callback operations succeeded.
5. BGC support failed at 0x0120 GX_SEMISHARED_OP_BGC_DONE_EVENT. The record's first failed primitive is 0x0140 GX_SEMISHARED_OP_EVENT_IMPL_ALLOC, sequence 12: resource GC_EVENT_IMPL, requested size 8, alignment 8, resource result 0, operation result false. Detail identifies event-implementation allocation failure.
6. The subsequent event-handle allocation (0x0141) and platform create-event callback (0x0142) for this BGC-done event were not reached. The record's first-failure status is 0xFFFFFFFF, meaning no native/PAL status is available.
7. init_semi_shared() returned false. initialize_gc() and the final heap initialization result are 0x80004005 (E_FAIL). The GC-init record shows WaitForGCEvent allocation and creation were not reached.
8. Module registration and managed entry were not observed. No managed allocation, collection, or experimental GC acceptance is claimed.

### Source attribution and cleanup

In the staged candidate's pinned Workstation GC source, gc_heap::create_bgc_threads_support() calls background_gc_done_event.CreateManualEventNoThrow(TRUE) at runtime-pack/gcinit-workstation-source/gc.cpp:39580. A false return records GX_SEMISHARED_OP_BGC_DONE_EVENT and takes the helper cleanup path at lines 39583–39586.

The GC environment implementation is GCEvent::CreateOSManualEventNoThrow() in tools/dotnet/runtime-pack/src/gcenv/guidexos_gcenv.cpp. At line 236 it executes new (std::nothrow) GCEvent::Impl(). The recorded 8-byte allocation returns null; the null branch returns false at lines 243–248 before event-handle creation or the PAL create-event callback. This identifies the failed operation, not why the allocator returned null. There is no evidence for memory exhaustion, a page-mapping fault, invalid synchronization, or another platform mechanism.

Both cleanup scopes entered and completed: masks 0x3/0x3. The BGC helper had no BGC-done event object to close. Outer cleanup closed the previously created full-GC approach and end events; each raw PAL close result was 0 (success). There was no cleanup failure, and cleanup did not replace the first failure.

## Analyzer correction and preserved originals

The runner's original guest-manifest.json remains DIAGNOSTIC_INCONCLUSIVE, and the original serial-semishared-analysis.json is preserved. Its analyzer falsely rejected two valid record properties: raw PAL close status 0 is successful cleanup, and nested operation slots are stored in begin order, so a parent aggregate slot can precede its child failure.

The corrected analyzer validates close success against raw status 0 and checks the first failed operation by ID rather than slot order. The separately written reviewed analysis is qac-guest-r4r5r2r1-001/serial-semishared-analysis-reviewed.json; its evidence state is complete, its raw serial hash matches the original, and it attributes the first failure to 0x0120 with nested operation 0x0140. The original serial and runner manifest were not rewritten. The analyzer and its focused test file are the only qualification code changes in this closeout; no GC, PAL, candidate, or guest behavior was changed.

The reviewed attribution record is out/dotnet/c188r12r4r5r4r5r2-semishared-attribution/reviewed-attribution-codex-20261101T235648833Z.json.

## Outcome and next action

**Outcome B — SEMISHARED_FAILURE_SITE_PROVEN.** The exact branch and failing allocation are established. The underlying allocator/PAL mechanism remains unknown because the observed API returns only a pointer/Boolean and the record contains no status code.

The smallest justified follow-up is to add targeted, behavior-preserving instrumentation around the 8-byte GCEvent::Impl nothrow allocation to record the allocator path and any available failure status. Use that result to select a correction. Do not make a speculative allocator, PAL, GC initialization, or resource-lifecycle change from this guest alone.
