# C188R12R4R5R4R5R2R2R2 Native Allocation QAC Reentry

**Outcome E — `QAC_OR_EXECUTION_BLOCKED`**

## Result

The qualified allocation-attribution candidate and hosted preflight passed. Fresh QAC admission found a verified Disk Manager guest occupying the managed lane. The runner stopped before creating the R2R2R2 one-boot guard, requesting a reservation, or launching QEMU. No allocation-route observation was made and no new runtime failure or regression is inferred.

The existing Disk Manager guest was left untouched. No QMP command was sent. The existing R2R2R1 guest manifest remains unchanged with its original `REGRESSION_DETECTED` label; the R2R2R1 report correctly explains that its later classification is QAC blocking.

## Repository and preserved state

| Item | Value |
| --- | --- |
| Branch | `v1.1_DOTNET_SUPPORT` |
| Starting full HEAD | `872d4629bd0b3d0e2a2c2a9af16647d7d6289615` |
| Ending qualification code/test HEAD before this closeout commit | `872d4629bd0b3d0e2a2c2a9af16647d7d6289615` |
| Initial worktree | Clean |
| Source `ESP/ramdisk.img` SHA-256 | `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8` |

The source ramdisk hash was verified before and after hosted preflight and appears unchanged in the blocked-run manifest. The staged candidate ramdisk has the same hash. No reset, clean, restore, stash, branch switch, history rewrite, push, or source ramdisk modification occurred.

The hosted C++ test compiler updated these three tracked root-level object outputs. They are retained as generated outputs and excluded from the closeout commit:

- `c188r12r4r5r4r5r2r2_native_alloc_operator_new_test.obj`
- `c188r12r4r5r4r5r2r2_native_alloc_record_test.obj`
- `guidexos_nativeaot_native_alloc_record.obj`

## Candidate integrity and provenance

Reused without rebuilding:

```text
out/dotnet/c188r12r4r5r4r5r2r2-native-allocation-attribution/candidate-20261102T022027433Z/
```

The candidate manifest SHA-256 is `F1645DD3A73426C28CD6F1EB0D1F577F361841B0D999782F1A56CEE85A2B3391`. The retained integrity inventory SHA-256 is `D0B0E2E75C8C71AD0723514041E5B7C36309A91D9432E2395A48B6E56754AB7E`. All 71 inventory entries were rehashed and size-checked: 55 manifest-declared artifact hashes, 13 staged payloads, and 3 provenance files. Roles, source HEAD `7fec6954bf2848627a496e16ea77e24e69efa830`, branch, and pinned runtime source commit `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3` matched the complete inventory.

| Artifact | Verified SHA-256 |
| --- | --- |
| Diagnostic NativeAOT ELF | `B356F869D3E29187857F4FCAE50A2F3C11EEDAAD9A9361D515E15A057FDF4A16` |
| Production NativeAOT ELF | `F0E32D79CA5A4DB7FBF85F115690B84BB46D77517F4C5A27C9575F12886DAC0B` |
| Diagnostic kernel and staged ESP kernel | `CBB363EA24C1346DE885B8A1D408B1AA043539934C37D63D43E3E5420C3F79B8` |
| Source and staged ramdisk | `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8` |
| Linked allocation disassembly | `79F52044AD4EC0E8C9059F6CFCBA5BB7C8BED3927265CAA0AB28AA18671CC4D6` |

The verified disassembly binds `GCEvent::Impl`'s nothrow allocation to the selected global `operator new`, startup malloc, and PAL virtual allocation. The candidate manifest and runtime-pack manifest retain the native-allocation, semi-shared, and GC-init record profiles; their source and staged artifacts passed hash checks. The previous GC-init and semi-shared attribution instrumentation remains in the diagnostic candidate. Production isolation remains proof-disabled. No allocation success or guest failure cause is claimed from these hosted checks.

## Runner and hosted preflight

The prior corrected blocked-preflight classifier remains active. The native-allocation test exercised a missing-serial rejected preflight and returned `QAC_OR_EXECUTION_BLOCKED`; it also retained the separate captured-serial mutation regression case. The runner change adds a distinct `R2R2R2` attempt ID, evidence directory, and guard name while keeping the guard's `CreateNew`, one-boot, and no-retry behavior. The candidate and runtime behavior were not changed.

| Check | Result |
| --- | --- |
| Native allocation fixed-record tests, analyzer fixtures, and actual linked operator-new/startup/PAL integration | PASS; callback refusal is synthetic |
| Analyzer fixtures: success, VM commit/reserve branches, PAL slot capacity, PAL not ready, and incomplete records | PASS |
| Semi-shared nested first-failure and cleanup retention | PASS (30 assertions) |
| GC-init record and analyzer | PASS |
| ThreadStore and PAL stack-bounds regressions | PASS |
| QAC admission predicates | PASS (32 cases) |
| QAC ownership predicates | PASS (15 cases) |
| QAC live-stream capture | PASS when invoked directly |
| QMP response parsing, QMP writer encoding, raw serial preservation | PASS |
| Retained post-run QAC recovery snapshot predicates | PASS |
| PowerShell parser, `git diff --check`, source ramdisk hash | PASS |

The QAC live-stream test initially failed its live stdout assertion when run through an extra nested `pwsh`; it passed when invoked directly. No stream-capture test/helper source was changed. QAC admission tests validate retained post-run status/list evidence. This runner's durable blocked-run recovery record is `guest-manifest.json`; the current blocked invocation generated it with its before/after QAC snapshots and ownership evidence.

Focused hosted evidence is retained under:

- `candidate-20261102T022027433Z/hosted-codex-r2r2r2-preflight-20261102T143928066Z/`
- `out/dotnet/c188r12r4r5r4r5r2-semishared-attribution/hosted-20261102T143948661Z/`
- `out/dotnet/c188r12r4r5r4r5-gc-init-probes/hosted-20261102T143948524Z/`
- `out/dotnet/c188r12r4r5r4-threadstore-readiness/hosted-20261102T143948557Z/`
- `out/dotnet/c188r12r4r5r4-threadstore-readiness/hosted-qac-admission-20261102T143948533Z/`
- `out/dotnet/c188r12r4r5r1-runtime-init-attribution/hosted-20261102T143948699Z/`
- `out/dotnet/c188r12r4r5r3-init-return-contract/hosted-20261102T143948697Z/`

The earlier broader lifecycle/UI qualification remains preserved and was not rerun.

## Fresh QAC admission and ownership

The corrected runner captured fresh status, reservation/session list, inspection for every listed reservation, and a complete Windows QEMU process census in:

```text
out/dotnet/c188r12r4r5r4r5r2r2-native-allocation-attribution/qac-guest-r2r2r2-001/
```

At the initial snapshot (`2026-11-02T14:42:32Z`):

- QAC coordinator was online, version 2.0.0, PID `24548`.
- QAC reported one managed session, no queued or blocked requests, and one QEMU process.
- Reservation `2a88ba01d1a64dbba8e3add8ae6a045b` was `Running`, project `guideXOSServer`, mode `acceptance`, owner `LEON2_RDT\guideX (PID 30508)`, with owner verification and liveness both `Verified`.
- Its inspected QEMU child was PID `8172`, parent PID `30508`, executable `C:\Program Files\qemu\qemu-system-x86_64.exe`.
- The complete command line uses `-accel whpx`, the Disk Manager workspace `guideXOSServer_DiskManagerImprovements`, the `dm37-gatef-removal-replacement` ESP, and guest name `dm37-gatef-removal-replacement`.
- The Windows census was complete and found exactly that one QEMU process. QAC inspection and census agreed on the reservation, owner PID, child PID, executable, and parent.

The final bounded after-snapshot at `2026-11-02T14:43:02Z` showed the same running reservation, owner, child, and guest. No reservation request was submitted. The R2R2R2 guard was not created; the guest manifest records a null guard hash and null attempted time. No QEMU process was launched by this run, no QMP interaction occurred, and the existing Disk Manager guest was not terminated. QAC release is not claimed because this run did not own the active reservation.

Key admission evidence:

- Before status/list: `qac-before.status.txt`, `qac-before.list.txt`
- Before reservation inspection: `qac-before-2a88ba01d1a64dbba8e3add8ae6a045b.inspect.json`
- Before process census: `qac-before.windows-qemu-census.json`
- Final status/list: `qac-after-025.status.txt`, `qac-after-025.list.txt`
- Final inspection/census: `qac-after-025-2a88ba01d1a64dbba8e3add8ae6a045b.inspect.json`, `qac-after-025.windows-qemu-census.json`
- Blocked-run manifest SHA-256: `70461CE3570449FBA4B8CA96DB2ABC5A37578195ED83F2CBC8B8F9D7E64A35BC`

## Guest attribution and outcome

There is no R2R2R2 raw guest serial, native-allocation record, GC-init record, semi-shared record, QMP log, or allocation analyzer output because the guest did not boot. Therefore this phase did not observe any of the requested route stages:

`GCEvent::Impl → operator new → startup malloc → PAL virtual allocation → kernel VM reserve/commit`

The R2R1 raw serial remains the last guest evidence. It records an 8-byte, 8-aligned nothrow allocation returning null at the `GCEvent::Impl` allocation site and semi-shared cleanup, but it lacks allocator/PAL status. Neither it nor this blocked reentry proves the underlying failure mechanism.

The preserved R2R1 serial is `out/dotnet/c188r12r4r5r4r5r2-semishared-attribution/qac-guest-r4r5r2r1-001/serial.log`, SHA-256 `39402A5495C0DF35492C403B776EC9214A26D64D31B2D1CA558F196124905C5B`. The R2R2 analyzer is `scripts/dotnet/analyze-c188r12r4r5r4r5r2r2-native-alloc.ps1`, SHA-256 `314B75DFF77AB980847ED5011BC8CDEB6A03CF8497ACFC28D293110FB1227C97`; it was not run because no R2R2R2 serial was produced. The R2R2R2 run directory contains no raw serial or derived runtime records.

**Classification: Outcome E — `QAC_OR_EXECUTION_BLOCKED`.** No runtime regression is inferred from QAC occupancy. No speculative allocation correction is justified.

The original R2R2R1 guest manifest was rechecked after this run: SHA-256 `FAEF4678BB2BCA35246E314DA66EC9ACF9D5D88FA5DFBC3001DAF47F02BF4C2D`, original result still `REGRESSION_DETECTED`. The separate R2R2R1 report classifies the event as QAC blocking and documents why the original manifest label was retained.

## Smallest next action

After the Disk Manager owner releases QAC, conduct a separately authorized reentry with a fresh attempt ID and evidence directory, reusing this unchanged candidate. Preserve the current R2R2R2 blocked evidence. Do not change the allocator until a guest record identifies a concrete failure operation and its runtime state.

No guest boot occurred in this phase; the one-boot limit and no-retry policy were preserved.
