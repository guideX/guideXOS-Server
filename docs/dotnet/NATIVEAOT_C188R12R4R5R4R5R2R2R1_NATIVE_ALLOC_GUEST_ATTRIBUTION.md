# C188R12R4R5R4R5R2R2R1 Native Allocation Guest Attribution

**Classification: Outcome E — `QAC_OR_EXECUTION_BLOCKED`**

## Result

The native allocation candidate passed its hosted and integrity checks, but the authorized guest did not boot. Fresh QAC admission found an already-running Disk Manager guest occupying the coordinator. The runner stopped before creating the R2R2R1 attempt guard or starting its own QAC process. No allocation operation was observed, so this run establishes no allocator, startup, PAL, or kernel failure mechanism.

No retry was made. The active QAC guest and its QEMU process were left untouched.

## Repository and candidate identity

- Starting repository HEAD: `9e515f61a0e2b29767f223f37a23239bb34c6094`
- Candidate build HEAD: `7fec6954bf2848627a496e16ea77e24e69efa830`
- Ending qualification code/test HEAD: `484c5215113a831c317b690bb6cc399602c0455d`
- Candidate: `out/dotnet/c188r12r4r5r4r5r2r2-native-allocation-attribution/candidate-20261102T022027433Z/`
- Candidate manifest SHA-256: `F1645DD3A73426C28CD6F1EB0D1F577F361841B0D999782F1A56CEE85A2B3391`
- Runtime pack: `guidexos-nativeaot-runtime-pack-amd64-c188r12r4r5r4r5r2r2-native-allocation-v1`
- Pinned runtime source commit: `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`

The reviewed integrity inventory is `out/dotnet/c188r12r4r5r4r5r2r2-native-allocation-attribution/candidate-integrity-reviewed-codex-20261102T043219932Z.json`, SHA-256 `D0B0E2E75C8C71AD0723514041E5B7C36309A91D9432E2395A48B6E56754AB7E`. Its 55 manifest-declared artifact hashes, 13 staged payloads, and 3 provenance files passed re-hashing and size checks (71 files total). The candidate reports `NATIVE_ALLOC_PROBES_HOSTED_READY`, `qemuLaunched=false`, and `qacReserved=false`.

Key full artifact hashes:

| Artifact | SHA-256 |
| --- | --- |
| Diagnostic NativeAOT ELF | `B356F869D3E29187857F4FCAE50A2F3C11EEDAAD9A9361D515E15A057FDF4A16` |
| Diagnostic kernel | `CBB363EA24C1346DE885B8A1D408B1AA043539934C37D63D43E3E5420C3F79B8` |
| Linked allocation disassembly | `79F52044AD4EC0E8C9059F6CFCBA5BB7C8BED3927265CAA0AB28AA18671CC4D6` |
| `GCEvent::Impl` source | `93F17C5E56FC83295FEEBF8AE2F5E8FC3DE568D1825F439B584A6BFB632789F5` |
| Production NativeAOT ELF | `F0E32D79CA5A4DB7FBF85F115690B84BB46D77517F4C5A27C9575F12886DAC0B` |
| Production kernel | `A21A951C8DF337CEA08A7FE93580C556F0AC87F33D1B1766070FFF204B1EA168` |
| Preserved source ramdisk | `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8` |

The linked map and disassembly identify the selected nothrow `operator new`, startup `malloc`, and PAL virtual allocation route. The source contains the `new (std::nothrow) GCEvent::Impl()` call. The native probe, GC-init and semi-shared records, failure-branch map, and production isolation checks all match the candidate. Production remains proof-disabled and retains `persistentCompositeLifecycle=true`. The hosted callback-refusal case remains explicitly synthetic; it is not guest evidence.

The candidate manifest's `runnerVersion` is the stale label `c188r12r4r5r4r1-one-boot-guard-planned`. The native-allocation analyzer itself was hash-verified at `314B75DFF77AB980847ED5011BC8CDEB6A03CF8497ACFC28D293110FB1227C97`. The runner used for the blocked invocation had SHA-256 `3E142FFA52AA7B44A9D79361E45E98590F4F7D0AC1E026C3CB4D8E6B657BFBB6`; the follow-up outcome-classifier correction committed at `484c5215113a831c317b690bb6cc399602c0455d` has runner SHA-256 `94ABE4E8455F89ADF04CE4DEE8E316CD310EC9E9C9CE762AC9F58FCB5076541B`. The candidate binaries were not rebuilt or changed.

`ESP/ramdisk.img` retained the required SHA-256 before and after preflight and the blocked admission. No reset, restore, stash, or cleanup was performed. Existing unrelated modifications and generated files were preserved.

## Hosted preflight

All final focused preflight checks passed:

| Check | Result |
| --- | --- |
| Native allocation fixed-record and operator-new/PAL integration; callback refusal and VM branch fixtures | PASS |
| Semi-shared nested first-failure and cleanup preservation | PASS (30 assertions) |
| GC-init record and analyzer | PASS |
| ThreadStore and stack-bounds regressions | PASS |
| QAC admission predicates | PASS (32 cases) |
| NativeAOT initialization return-contract predicates | PASS |
| NativeAOT initialization attribution and raw-serial preservation | PASS |
| QMP event capture and strict response handling | PASS |
| QAC output-stream capture | PASS (2 cases) |
| QAC ownership predicates | PASS (15 cases) |
| Earlier runner predicates | PASS (24 cases) |
| PowerShell parser and `git diff --check` | PASS |

The first GC-init suite invocation exposed an outdated static test assertion that listed only the earlier R4R5 and semi-shared modes. The assertion was extended to include the native-allocation mode and the suite passed on rerun. The native hosted suite also tests that a missing serial after rejected QAC preflight maps to blocked execution, while a changed captured serial remains a regression. Prior broader lifecycle and UI evidence (40 suites / 969 cases) remains retained and was not rerun.

## Fresh QAC admission

The runner captured fresh status, reservation list, inspection, and Windows QEMU census under `out/dotnet/c188r12r4r5r4r5r2r2-native-allocation-attribution/qac-guest-r2r2r1-001/`.

- QAC coordinator: online, version 2.0.0, PID `24548`.
- Existing reservation: `0f690ad12b6242cd83eb7f9ea02cc02a`, `Running`, acceptance mode, owner PID `18048`, owner verified.
- Existing QEMU child: PID `22304`, a single managed QEMU process; the census reported zero foreign processes.
- QAC labels the reservation `guideXOSServer`; its inspected command line points into `guideXOSServer_DiskManagerImprovements/out/dm37/continuation16/gatec-protected-inventory-20261101/` and names the guest `dm37-cont16-gatec-protected-inventory`.
- QAC preflight was false because the existing managed reservation and guest were active. Repeated bounded post-run snapshots still showed that same reservation and process.

This is the Disk Manager workload occupying QAC described by the stop condition. No QAC reservation was requested by this run, no QEMU child was launched by this run, and no QMP command was sent.

The phase-specific guard path was `out/dotnet/c188r12r4r5r4r5r2r2-native-allocation-attribution/guest-launch-attempt-C188R12R4R5R4R5R2R2R1.json`; it does not exist because admission failed before the guard's `CreateNew` step. There is no `serial.log`, native allocation record, guest ownership identity, or recovery manifest from this run. `guest-manifest.json` and the QAC snapshots preserve the blocked preflight evidence. QAC release is not reported as verified because the active reservation belongs to the existing Disk Manager guest.

## Runner outcome correction

The preserved run manifest is `out/dotnet/c188r12r4r5r4r5r2r2-native-allocation-attribution/qac-guest-r2r2r1-001/guest-manifest.json`, SHA-256 `FAEF4678BB2BCA35246E314DA66EC9ACF9D5D88FA5DFBC3001DAF47F02BF4C2D`. Its fields show `qacPreflightPassed=false`, `qacAdmissionGranted=false`, `sourceRamdiskPreserved=true`, and null serial hashes. Its original `result` is `REGRESSION_DETECTED` because the previous outcome logic treated the absent serial as a failed preservation check. The manifest is preserved unchanged.

Commit `484c5215113a831c317b690bb6cc399602c0455d` adds native-allocation profile admission and evidence handling to the runner, plus a fail-closed outcome classifier. A rejected preflight with no guest serial now classifies as `QAC_OR_EXECUTION_BLOCKED`; a changed captured serial still classifies as a regression, unknown analyzer classifications remain inconclusive, and a complete allocation success does not imply GC success. Hosted regression checks for these cases passed. The same commit updates the GC-init static profile assertion for the new candidate mode.

## Attribution and next step

No guest operation sequence was captured. There is no observation of `GCEvent::Impl`, `operator new`, startup `malloc`, PAL allocation, VM reserve/commit, cleanup, `GCHeap::Initialize()`, or `RhInitialize()` for this candidate. The earlier R2R1 attribution remains the last guest evidence: `out/dotnet/c188r12r4r5r4r5r2-semishared-attribution/qac-guest-r4r5r2r1-001/serial.log`, SHA-256 `39402A5495C0DF35492C403B776EC9214A26D64D31B2D1CA558F196124905C5B`; semi-shared operation `0x0120` with nested allocation `0x0140`, an 8-byte nothrow allocation returning null, successful cleanup, and no underlying allocator/PAL status. It does not establish the current allocation failure mechanism.

No runtime or kernel correction is justified by this blocked run. A later qualification must be separately authorized after the Disk Manager QAC reservation is released; this attempt performed no guest boot and was not retried.
