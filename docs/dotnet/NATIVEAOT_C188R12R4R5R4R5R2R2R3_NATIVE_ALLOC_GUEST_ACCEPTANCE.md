# C188R12R4R5R4R5R2R2R3 Native Allocation Guest Acceptance

**Outcome A — `NATIVE_ALLOC_ROOT_CAUSE_PROVEN`**

## Result

One authorized QEMU guest boot captured the native allocation failure. The 8-byte `GCEvent::Impl` nothrow allocation returned null because the diagnostic startup PAL's VM tracking array had reached its fixed capacity of 16 active slots. The PAL rejected the request while looking for a free tracking slot, before calling kernel virtual-memory reserve or commit. This establishes the mechanism for this observed failure; it is not evidence of system memory exhaustion or of a failure in the kernel VM allocator.

The candidate stopped during Workstation GC initialization. `RhInitialize` returned false and the startup adapter reported failure; managed entry remained blocked. No managed runtime or GC safety prerequisite was bypassed. Semi-shared cleanup completed for both entered cleanup operations.

The first runner-generated guest manifest remains unchanged and reports `DIAGNOSTIC_INCONCLUSIVE` because its analyzer did not understand the runtime's emitted summary field IDs. The raw serial was preserved. A separate corrected analysis of that same immutable serial produced the capacity attribution documented below; the parser/correlation defect and its fix are recorded separately from the guest result.

## Repository and preserved state

| Item | Value |
| --- | --- |
| Branch | `v1.1_DOTNET_SUPPORT` |
| Qualification full HEAD before the closeout commit | `72f6ff0899872c742fa42e0b9deabd84c85ef53a` |
| Initial worktree changes | Three pre-existing modified tracked `.obj` outputs; no other tracked changes |
| `git diff --check` | PASS |
| Source `ESP/ramdisk.img` SHA-256 | `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8` |

The three pre-existing modified object files were retained and excluded from the closeout commit:

- `c188r12r4r5r4r5r2r2_native_alloc_operator_new_test.obj` — SHA-256 `3507A24E07257422C98CAB4F7430E094A2C83DF93647E6A02621010DD585A2BD`
- `c188r12r4r5r4r5r2r2_native_alloc_record_test.obj` — SHA-256 `605D6F7F066BAD3669B52C21A89722A7C9750F9C6595EDFDA26BC06E3125AEC3`
- `guidexos_nativeaot_native_alloc_record.obj` — SHA-256 `2CD941199D5F34F81F72B0B718A8D1C1023CEAD831464724DEBFEA435F044183`

The source ramdisk hash was checked during preflight and after guest execution. It matches both the required value and the staged candidate ramdisk. The original ramdisk was not changed.

## Candidate and hosted preflight

Reused without rebuilding the staged candidate:

```text
out/dotnet/c188r12r4r5r4r5r2r2-native-allocation-attribution/candidate-20261102T022027433Z/
```

The candidate manifest SHA-256 is `F1645DD3A73426C28CD6F1EB0D1F577F361841B0D999782F1A56CEE85A2B3391`. The reviewed integrity inventory SHA-256 is `D0B0E2E75C8C71AD0723514041E5B7C36309A91D9432E2395A48B6E56754AB7E`; all 71 entries were rehashed and size-checked, including the 55 manifest-declared artifact hashes, 13 staged payloads, and 3 provenance files. The candidate source HEAD recorded in its manifest is `7fec6954bf2848627a496e16ea77e24e69efa830`. No relevant NativeAOT runtime source delta was found between that candidate and the qualification HEAD.

| Candidate artifact | Verified SHA-256 |
| --- | --- |
| Diagnostic NativeAOT ELF | `B356F869D3E29187857F4FCAE50A2F3C11EEDAAD9A9361D515E15A057FDF4A16` |
| Diagnostic kernel | `CBB363EA24C1346DE885B8A1D408B1AA043539934C37D63D43E3E5420C3F79B8` |
| Staged and source ramdisk | `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8` |

Hosted preflight passed the fixed-record tests, actual linked `operator new`/startup/PAL contract integration (with synthetic callback refusal), analyzer fixtures, and the R2R2R3 runner contract. QAC admission predicate checks passed (32 cases). After the guest exposed the serial encoding mismatch, the analyzer and fixture coverage were corrected and the hosted suite passed again, including low-ID runtime summary fields and correlation of three runtime attempts with seven global backend records. This suite did not launch QEMU or reserve QAC.

## QAC admission, guard, and owned boot

A fresh coherent census was captured before admission in:

```text
out/dotnet/c188r12r4r5r4r5r2r2-native-allocation-attribution/qac-preflight-r2r2r3-20261102T233315404Z/
```

Before and after status/list snapshots reported the coordinator online, zero reservations, zero managed sessions, zero queued or blocked work, and zero QEMU processes. Both complete Windows QEMU process censuses found zero processes. No reservation inspection was needed because the reservation list was empty. The snapshots agreed and the recorded `clearForAdmission` result is true.

After admission, the R2R2R3-specific one-boot guard recorded `maxGuestBoots=1` and `automaticRetry=false`. Guard SHA-256: `F474A04274479A4F87644CC9F438D873C1FB00D1A1A290A64F3AAF5D2B6F1E25`. Exactly one QEMU process was launched: PID `2644`, owned through QAC reservation `76a54ff59159422389890dd11c91534f`; QAC owner PID was `13200`. The boot used the verified candidate ESP and diagnostic kernel. No foreign process was terminated and no retry was made.

The runner's terminal matcher initially required an `rhResult` field, while this candidate emitted `adapterStatus`. It therefore waited through its bounded observation interval before issuing `quit` over the owned guest's QMP connection. QMP recorded the `host-qmp-quit` shutdown event. QAC returned exit code 0 and released the reservation. Runner after-snapshots and an independent post-release census again showed an online coordinator, zero reservations/sessions, and zero Windows QEMU processes. The single-boot guard was not reset or reused.

## Guest allocation evidence

Raw serial, independently retained at:

```text
out/dotnet/c188r12r4r5r4r5r2r2-native-allocation-attribution/qac-guest-r2r2r3-001/serial.log
```

It is 71,680 bytes with SHA-256 `6BB79575CADE1035B5D081E25183AA3066503B395E3DD6EFD5601CAE7EFEB7D0`. Its hash was identical before and after analysis. The original analyzer output is left intact. Corrected analysis is a separate file, `native-allocation-analysis-corrected.json`, SHA-256 `BE93751593239D68701D4D60E217140EAF7781D27E3D4C238107DF4D01FA9D88`, and accepts `KERNEL_PAL_SLOT_CAPACITY` as the first-failure class.

The trace shows:

1. Native adapter initialization and `RhInitialize` were reached. `RhInitialize` returned `false` (`nativeBool=00000000`); adapter status was `00000001` (`failure`). Workstation GC initialization failed during `init_semi_shared()`, and the managed-main gate remained blocked.
2. Semi-shared initialization's first failed operation was `GX_SEMISHARED_OP_BGC_DONE_EVENT` (`0x0120`). The associated `GCEvent::Impl` allocation was 8 bytes, aligned to 8. Two earlier `GCEvent::Impl` allocations succeeded; this third allocation returned null before its constructor ran.
3. `operator new` entered the configured startup malloc callback for the 8-byte request. The callback was entered with PAL state ready. Startup malloc computed a 24-byte backing request (`0x18`) and returned null because its PAL allocation callback returned null.
4. The PAL backend recorded request size `0x18`, page-rounded size `0x1000`, `addressSpaceReady=true`, VM region capacity `128`, slot capacity `16`, active slots `16`, and free-slot index `0xFFFFFFFF`. The first backend failure is operation `0x0204` (kernel virtual-allocation adapter), branch `0x0310` (`KERNEL_VM_SLOT_CAPACITY`), status `0`, status source `2` (the adapter's capacity result). The pointer remained null.
5. `startupReserve()` could not find a free `GcVmSlot` and returned before calling `virtual_memory::reserve()`. Consequently the record has no reserve result, no commit request, and no commit result. No kernel reserve/commit failure or physical-memory exhaustion was observed.
6. The null propagated through PAL and startup malloc to `operator new`, then to `GCEvent::Impl` and `init_semi_shared()`, causing `RhInitialize` to fail. Semi-shared cleanup's entered and completed masks were both `0x3`; both cleanup operations succeeded.

The backend records seven global PAL attempts while the allocation-scoped runtime record contains three attempts. The corrected analyzer correlates the runtime failure to the backend summary's first failed attempt (index 6); the two bounded traces are not one-to-one indexed.

### Tooling result separation

The runner-generated `native-allocation-analysis.json` classified its record as incomplete because the serial logger emitted runtime summary field IDs without the high bit expected by the original analyzer. The analyzer also initially assumed backend attempt count must equal runtime attempt count. Raw evidence and the runner's original `guest-manifest.json` were not rewritten. The corrected analyzer accepts the observed runtime summary encoding and correlates the backend first-failure index against the seven global PAL attempts; it then verifies the full slot table and absent free slot. The resulting accepted attribution is an independent post-run analysis of the same raw serial, not a second guest execution.

## Focused correction and qualification boundary

The diagnostic GC startup PAL declared `kMaxGcVmSlots = 16`, while the production NativeAOT PAL VM slot capacity is 256. The smallest supported correction is applied in `kernel/core/nativeaot_pal_qemu_test.cpp`: set the diagnostic capacity to 256 and document that it mirrors production. The hosted test now asserts that capacity relationship and covers the observed slot-capacity classification. Analyzer decoding and the runner's `adapterStatus` terminal matcher were also corrected narrowly.

The focused hosted suite passed after these changes, and `git diff --check` passed. The diagnostic kernel/candidate has not been rebuilt with the capacity change and has not been booted after the change. No post-fix GC initialization, managed execution, or allocation success is claimed. The source fix is ready for an isolated rebuild and later guest qualification under a fresh QAC admission and guard.

## Retained evidence

Primary evidence is under:

```text
out/dotnet/c188r12r4r5r4r5r2r2-native-allocation-attribution/
```

It includes the candidate integrity inventory, preflight census, R2R2R3 guard, raw serial, initial and corrected analysis files, native allocation record, semi-shared record and analysis, GC initialization record, QAC before/during/after status and list snapshots, reservation inspection and ownership identities, Windows process census, QMP log, and guest manifest. Raw serial SHA-256 is `6BB79575CADE1035B5D081E25183AA3066503B395E3DD6EFD5601CAE7EFEB7D0`; QMP log SHA-256 is `94D22264FA7A1C3408B6C07E1880875A1A123B359715F224671FF357468209DA`.

## Smallest next action

Build a new diagnostic kernel/candidate with the 256-slot startup PAL table, then schedule one separately admitted guest qualification to confirm the observed allocation now proceeds and record the next GC initialization milestone. Keep the production runtime and managed-main gate unchanged unless that next guest supplies new evidence.
