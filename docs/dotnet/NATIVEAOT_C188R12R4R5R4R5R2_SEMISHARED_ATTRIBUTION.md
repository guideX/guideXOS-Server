# C188R12R4R5R4R5R2 — Workstation `init_semi_shared()` failure attribution

## Outcome

**`SEMISHARED_PROBES_HOSTED_READY`.** The pinned Workstation GC source has 11 reachable false-return branches in this build profile. A bounded, allocation-free record now captures each branch, supporting operations, resource requests/results, cleanup entry/completion, the first failure, and the final result. Hosted tests and diagnostic/production builds passed, and a separate diagnostic candidate is staged.

This is hosted-only qualification. It does not identify which branch failed in the prior guest, prove a root cause, or establish successful guest GC initialization. No QEMU run or QAC reservation was made.

## Baseline and provenance

- Repository: `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT`; branch: `v1.1_DOTNET_SUPPORT`.
- Starting HEAD: `e606f241a34dcf5f301fec62691388971ba36889`.
- Implementation/build ending HEAD: `6ba37a5691e561206df4604c613fa91c6b0861d5`. The candidate manifest records this committed source HEAD. The documentation-only closeout commit follows it.
- Pinned NativeAOT runtime: `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`.
- `out/dotnet/gc-feasibility-baseline/nativeaot-runtime/src/coreclr/gc/gc.cpp` and `gcpriv.h` are the source of record. The runtime pack replaces the selected `Runtime.WorkstationGC:gcwks.cpp.obj` member with the instrumented copy; the build also includes the fixed record implementation in that object.
- The pre-existing `ESP/ramdisk.img` modification was preserved. Its SHA-256 before and after qualification is `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8`. Existing untracked host `bin`/`obj` and sample outputs were preserved and excluded from the commit.
- Prior R4R5R1 raw serial: `out/dotnet/c188r12r4r5r4r5-gc-init-probes/qac-guest-r4r5r1-001/serial.log`, SHA-256 `F071C9A7205167F8C0EF6270A3B88C60973D7EB544BD318327842406B7B61098`. It reports `init_semi_shared()` false, `firstFailureSite=1`, propagated `E_FAIL`, and no `WaitForGCEvent` entry. Its reviewed analysis explicitly leaves the internal branch and native/PAL mechanism unknown. R2 analysis preserved the raw hash before and after reading it.

## Selected build profile

The replacement `gcwks.cpp` compile command is retained at `out/dotnet/c188r12r4r5r4r5r2-semishared-attribution/candidate-20261101T225112623Z/runtime-pack/build-gcwks-gcinit-probes.bat`. It defines `HOST_64BIT`, `HOST_AMD64`, `FEATURE_BASICFREEZE`, and `FEATURE_EVENT_TRACE`; it does not define `BUILD_AS_STANDALONE` or `MULTIPLE_HEAPS`. In the pinned `gcpriv.h`, that selects `USE_REGIONS` (line 148), `BACKGROUND_GC` (line 191), and `FEATURE_LOH_COMPACTION` (line 175); the `MULTIPLE_HEAPS` define remains commented out at line 219. Thus the selected profile is Workstation, single heap, with regions, background-GC support code, basic freeze, event tracing, and LOH compaction.

The `MULTIPLE_HEAPS` mark-list-copy failure site is retained in the source ID map for completeness but is not compiled in this profile. `USE_REGIONS` is enabled; it does not remove the single-heap `g_mark_list` allocation branch. The previous guest's two successful platform event callbacks occur before the `init_semi_shared()` false result. In source order they are consistent with the two full-GC events; that trace did not expose a later BGC or buffer failure.

## Source-backed control flow and branch map

`gc_heap::initialize_gc()` calls `init_semi_shared()` at `gc.cpp:14406`; false is transformed to `E_FAIL` before returning at lines 14406–14412. `gc_heap::init_semi_shared()` begins at line 14417. Its failure branches share `cleanup` at line 14647 and return `ret` at line 14660. `GCHeap::Initialize()` returns a failed `initialize_gc()` result before reaching `WaitForGCEvent` at lines 48946–48965. These source facts match the R4R5R1 guest boundary result; they do not identify the internal branch.

| Stable ID | Source site in pinned `gc.cpp` | Failure condition and requested resource | Resources acquired before the branch; source cleanup |
| --- | --- | --- | --- |
| `0x0101` `GX_SEMISHARED_OP_MARK_LIST` | `14457–14467`; allocator helper `10165` | `make_mark_list(mark_list_size)` returns null. Records `mark_list_size * sizeof(uint8_t*)` and pointer alignment. | No mark-list resource was acquired on this branch. Shared cleanup closes no later resources. |
| `0x0102` `GX_SEMISHARED_OP_MARK_LIST_COPY` | `14450–14454`, under `MULTIPLE_HEAPS` | `g_mark_list_copy` returns null after the first list allocation. Records the requested pointer-array size/alignment. | Inactive in this profile. If compiled, the already acquired `g_mark_list` is not released by this shared cleanup label. |
| `0x0103` `GX_SEMISHARED_OP_SEGMENT_TABLE` | `14478–14484`; `sorted_table::make_sorted_table()` at `3554–3564` | The table allocation returns null. The pinned helper requests `sizeof(sorted_table) + 401 * sizeof(bk)` bytes via `new (nothrow) char[]`; the operation records size, alignment, and pointer result. | `g_mark_list` remains allocated. This cleanup label does not release it. |
| `0x0110` `GX_SEMISHARED_OP_FULL_GC_APPROACH_EVENT` | `14489–14495` | Manual event creation returns false. Nested records capture GCEvent wrapper allocation, bridge handle allocation, and event callback result. | Mark list and segment table remain. The failed event wrapper uses local nothrow ownership and is not published as valid; outer cleanup checks event validity. |
| `0x0111` `GX_SEMISHARED_OP_FULL_GC_END_EVENT` | `14493–14499` | Second manual event creation returns false. | The approach event may already be valid; outer cleanup closes valid approach/end events in that order. Mark list and segment table are not released here. |
| `0x0120` `GX_SEMISHARED_OP_BGC_DONE_EVENT` | `create_bgc_threads_support()`, `39492–39496` | First manual BGC event creation returns false. | Both full-GC events and earlier allocations may be live. BGC helper cleanup checks each of its four event objects; outer cleanup then closes the full-GC events. |
| `0x0121` `GX_SEMISHARED_OP_BGC_SYNC_EVENT` | `39496–39500` | Second manual BGC event creation returns false. | The BGC-done event may be valid; helper cleanup closes valid BGC events, then outer cleanup closes the two full-GC events. |
| `0x0122` `GX_SEMISHARED_OP_EE_PROCEED_EVENT` | `39500–39504` | Auto-reset EE-proceed event creation returns false. | The prior two BGC events may be valid and are checked/closed by helper cleanup. |
| `0x0123` `GX_SEMISHARED_OP_BGC_START_EVENT` | `39504–39508` | Fourth manual BGC event creation returns false. | The prior three BGC events may be valid and are checked/closed by helper cleanup. |
| `0x0130` `GX_SEMISHARED_OP_GC_TIME_INFO` | `14620–14625` | `new (nothrow) uint64_t[max_compact_time_type]` returns null; requested size/alignment are recorded. | Mark list, table, two full-GC events, and all four BGC events may be live. Outer cleanup closes only the two full-GC events. It does not free the arrays or close BGC events after BGC setup has returned success. |
| `0x0131` `GX_SEMISHARED_OP_BGC_TIME_INFO` | `14626–14632`, under `BACKGROUND_GC` | BGC timing array allocation returns null; size/alignment are recorded. | Includes `gc_time_info`, which remains allocated, plus the resources above. Same outer cleanup behavior. |
| `0x0132` `GX_SEMISHARED_OP_LOH_COMPACT_INFO` | `14634–14640`, under `FEATURE_LOH_COMPACTION` | LOH-compaction info array allocation for `get_num_heaps()` returns null; size/alignment are recorded. | Earlier GC/BGC timing arrays remain allocated. Same outer cleanup behavior. |

`0x0124` `GX_SEMISHARED_OP_BGC_SUPPORT_RESULT` records the BGC helper's aggregate Boolean return at `gc.cpp:14600`; it is not an additional internal failure site. Its false value follows one of the four BGC event branches above.

The two cleanup scopes are source-defined: outer cleanup scope `0` and BGC-helper scope `1`. The outer cleanup at `14647–14659` records entry/completion and closes only `full_gc_approach_event` and `full_gc_end_event` when valid. The helper cleanup at `39517–39550` records entry/completion and closes valid BGC-done, BGC-sync, EE-proceed, and BGC-start events in that order. Successful BGC event creation followed by a later trace-array failure does not enter the BGC helper's failure cleanup. `destroy_semi_shared()` at `15323` frees the mark list and segment table when `shutdown_gc()` calls it at `15390`; it is not called by the `initialize_gc()` false-return path. The trace arrays are not freed by `destroy_semi_shared()`.

The record also defines nested event operation IDs `0x0140` (GCEvent implementation allocation), `0x0141` (bridge event-handle allocation), and `0x0142` (platform create-event callback); cleanup event IDs are `0x0171–0x0172` for the outer events and `0x0174–0x0177` for the BGC events. `0x01FF` is the final initializer result.

## PAL paths and status limits

- GC array/list/table storage uses the pinned C++ `new (nothrow)` call sites. The record captures requested bytes, alignment, and pointer success/failure; no PAL error code is exposed by these return values.
- `GCEvent::Create*NoThrow` in `tools/dotnet/runtime-pack/src/gcenv/guidexos_gcenv.cpp:188–253` first allocates `GCEvent::Impl`. The startup bridge at `guidexos_nativeaot_gcenv_startup_bridge.cpp:120–171` allocates an `EventHandle`, then calls `guidexos_nativeaot_gc_create_event()`.
- `guidexos_nativeaot_gc_startup_platform_contract.cpp:117–118` returns only the event pointer or null. In the kernel, `startupCreateEvent()` at `kernel/core/nativeaot_pal_qemu_test.cpp:1076–1089` returns null if event initialization fails or the fixed event slots are exhausted. Neither interface exposes an error code, so the diagnostic reports pointer/Boolean results and `STATUS_SOURCE_UNAVAILABLE` rather than inventing a status.
- Event close differs: the platform hook returns `int32_t`; `startupCloseEvent()` returns `0` for success and `-1` for failure. `destroyEvent()` records this raw value as `EVENT_CLOSE_PAL` before the GC environment converts it to `EventStatus`. Cleanup failure is stored separately from the first initialization failure.
- A failed close causes `GCEvent::CloseEvent()` to invoke the existing fatal `unexpectedPlatformEntry()` path. The record is written before that path, but the adapter serializes after `RhInitialize` returns; if fatal handling prevents the return, no post-return marker can be promised. This preserves the existing failure behavior and is a remaining boundary for a future guest result.
- No locks, GC VM reserve/commit calls, managed services, or managed allocation are introduced in `init_semi_shared()` by the instrumentation.

## Record, output, and analyzer

`guidexos_nativeaot_semishared_record.cpp` uses a fixed 60-byte summary, 48 fixed 56-byte operation slots, and an eight-entry operation nesting stack. Total record storage is 2,748 bytes. No logging allocation, native heap allocation, managed service, exception, PAL call, or lock is used by the recorder. Summary IDs and field layout are declared in `guidexos_nativeaot_semishared_record.h`: 15 summary fields and 14 fields per operation. Capacity overflow is recorded and causes the analyzer to reject the record as incomplete.

Operation completion captures the first failed operation/status only once. Explicit source branches set the first failure branch once. Cleanup failures retain their own operation, raw status, and status source; they do not replace the first failure. The summary records outer/BGC cleanup entered and completed masks and the final 0/1 result. Event callback APIs that return only pointers or Booleans keep the unavailable-status sentinel.

The startup adapter at `guidexos_nativeaot_c188r12r4r5_gc_startup_adapter.cpp:59–98` emits the fixed record through the existing bounded init-trace sink after `RhInitialize` returns. The marker is `C188R12R4R5R4R5R2_SEMISHARED`, stage `0x71`. It does not alter `init_semi_shared()` result semantics, resource ordering, or the `RhInitialize` success gate.

`scripts/dotnet/analyze-c188r12r4r5r4r5r2-semishared.ps1` resolves operation IDs from the source header; checks magic/version, invocation, capacity, marker count/order, operation result completeness, first-failure consistency, cleanup masks, and final result; and fails closed on missing or conflicting records. It preserves the original serial and writes a separate JSON analysis. The archived R1 serial remains `unresolved_internally` with the outer `init_semi_shared()` failure recognized and no internal cause inferred.

`test-c188r12r4r5r4r5r2-semishared.ps1` compiled and ran the fixed-record C++ test (30 assertions) and passed 11 branch-attribution fixtures, success and allocation-size decoding, resource acquisition followed by initializer and cleanup failure, raw event-close status, first-cause preservation, missing operation result, incomplete record, conflicting summary, and the archived R1 trace/hash fixture. The hosted manifest reports 26 source-defined operation IDs and equal R1 raw serial hashes before/after analysis.

## Hosted qualification

All requested R2, prior GC-init, analyzer, startup, PAL/ThreadStore, link, ownership, QAC-admission, QMP, C188 lifecycle, factory, and UI checks passed. No test reserved QAC or launched QEMU.

| Qualification | Result |
| --- | --- |
| R2 bounded record | PASS, 30 C++ assertions |
| R2 analyzer/source fixtures | PASS, all 11 reachable branches and malformed/incomplete cases |
| R1 attribution / raw serial preservation | PASS; R1 serial SHA unchanged |
| R4R5 GC-init and R3 return-contract tests | PASS |
| R4 ThreadStore / PAL regressions | PASS |
| QAC admission | PASS, 32 cases |
| Existing isolated GC ownership proof | PASS |
| C188 link boundary / lifetime / factory | PASS, 9 / 13 / 9 cases |
| R11 runtime lifecycle / QAC ownership / QAC stream | PASS, 7 / 15 / 2 cases |
| R10 / R11 / R11R2 runner predicates | PASS, 13 / 30 / 24 cases |
| C181 QMP event capture | PASS |
| Managed UI regression baseline | PASS, 40 suites / 969 cases |
| PowerShell AST parse and `git diff --check` | PASS |

Evidence and logs are retained under `out/dotnet/c188r12r4r5r4r5r2-semishared-attribution/hosted-*`, `hosted-regressions-*`, `hosted-regressions-followup-*`, `runner-predicate-tests-*`, and `ui-baseline-*`. The UI inventory is `managed-ui-regressions-c188r12r4-factory-attribution-v1`; the suite manifest and logs are in `ui-baseline-20261101T230843523Z`.

## Diagnostic and production build qualification

Candidate root: `out/dotnet/c188r12r4r5r4r5r2-semishared-attribution/candidate-20261101T225112623Z`. Candidate result: `PASS_BUILD_READY_FOR_SEMISHARED_ATTRIBUTION_GUEST`. Runtime pack identity: `guidexos-nativeaot-runtime-pack-amd64-c188r12r4r5r4r5r2-semishared-v1`; pinned source commit is recorded above.

The candidate manifest SHA-256 after source-revision finalization is `DF24081DADF8505888D17A3E4C0F8700DE18B883F67DA20D6D9A8DEB29B41936`. It records the pre-commit workspace HEAD `e606f241a34dcf5f301fec62691388971ba36889`, committed source HEAD `6ba37a5691e561206df4604c613fa91c6b0861d5`, and that the candidate was built before the unchanged source tree was committed. The staged qualification manifest SHA-256 is `B2B0C9E037B4FC60E7989A87C65ADFA0CD2F95E6FF924FB75137BE615451BC04` and records the same committed source HEAD.

| Artifact | SHA-256 |
| --- | --- |
| Diagnostic NativeAOT ELF | `7B5DF43C0FFCC2B0F94BF5251CF3FFE03BF2491885E9D10CF11CCBB44C4A6CD3` |
| Diagnostic NativeAOT map | `1FF08ABBB79711AF868E735EBD5BF82BD562BB78ADD8938820E9DF2F68D568E1` |
| Instrumented Workstation GC replacement object | `5B792F765EC4A122FD94A471640F1A0A31B3D5249CC41C219CF08CE9765BD383` |
| Diagnostic kernel ELF and staged ESP kernel | `065999CAE0C9BD9100BD58590E305A1B122AA9F4DF5F7554CE8BECC1D7494CEA` |
| Proof-disabled production NativeAOT ELF | `FD610CAF813F472A548A146035D7638EF8A7D8844690F9AC8C433337D810154C` |
| Proof-disabled production NativeAOT map | `A2CE4211103EB058F08BF04E4877C57F888C4EDA1C298670D082313F8FE3E8D0` |
| Proof-disabled production kernel ELF | `A21A951C8DF337CEA08A7FE93580C556F0AC87F33D1B1766070FFF204B1EA168` |
| Runtime pack library | `7E3294EA67E682F8E3AE76006782011D45B8B4ABA7EB1E13021D93F961098FB7` |
| Staged/source ramdisk | `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8` |

The diagnostic NativeAOT map contains `GuideXosNativeAotSemiSharedGetSummaryField`, `GuideXosNativeAotSemiSharedGetOperationCount`, and `GuideXosNativeAotSemiSharedGetOperationField`. The production NativeAOT map contains none of those accessors or the R2 marker. Production runtime-pack R2 profile is false, `persistentCompositeLifecycle=true`, and production NativeAOT/kernel qualifications both report PASS. Diagnostic and production output roots are separate; PE import, map, runtime-pack, and kernel gates passed.

The staged payload is `candidate-20261101T225112623Z/staged-final`; its qualification manifest is `staged-final/qualification-manifest.json` (SHA-256 `B2B0C9E037B4FC60E7989A87C65ADFA0CD2F95E6FF924FB75137BE615451BC04`). It inventories these seven files, sizes, and hashes:

| Staged path | Bytes | SHA-256 |
| --- | ---: | --- |
| `diagnostic/esp/kernel.elf` | 3,461,384 | `065999CAE0C9BD9100BD58590E305A1B122AA9F4DF5F7554CE8BECC1D7494CEA` |
| `diagnostic/esp/ramdisk.img` | 67,108,864 | `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8` |
| `diagnostic/esp/EFI/BOOT/BOOTX64.EFI` | 52,224 | `D72A35DA882892BA97356A1C006562ABA2908D9DD909152B96DCBB655D6C2D9B` |
| `diagnostic/runtime-pack/gc.cpp` | 1,933,318 | `12032F90901CECC4F8D1AA9923E135FAEAB7DE300AAAF407412430FDB0FAC37D` |
| `diagnostic/runtime-pack/gcwks.cpp` | 724 | `399CC34A0C1D3EE9EE544CFF63AFA9A9FD213DB410467D8B931C688C0465E446` |
| `diagnostic/runtime-pack/guidexos_nativeaot_semishared_record.cpp` | 12,126 | `AFDE4E8797EA9CA98B0BF67489426864AA1FCFADA09FD3F3DF771D5131FA8AD4` |
| `diagnostic/runtime-pack/guidexos_nativeaot_semishared_record.h` | 6,071 | `B2021B42EB8107C77298B574E58376366B120E16126FF5E5F07D4335A4721B7D` |

The root candidate manifest and full source-file hashes remain in the candidate directory. `qemuLaunched=false` and `qacReserved=false` are recorded in the candidate manifest.

## Remaining uncertainty and next guest

The exact failing branch and underlying allocator/event mechanism remain unknown until the next guest. GC array allocations and event-creation APIs expose only pointers/Booleans; no last-error or HRESULT exists to report for those failures. A raw event-close callback status is available. If close failure reaches the existing fatal abort path, the post-return adapter may not run; the fixed record still preserves the values written before abort, but no serial output is promised for a non-returning fatal path.

The runner is prepared for a separate maximum-one-boot `C188R12R4R5R4R5R2R1` phase. Its output path is `out/dotnet/c188r12r4r5r4r5r2-semishared-attribution/qac-guest-r4r5r2r1-001`, and its fresh guard is `guest-launch-attempt-C188R12R4R5R4R5R2R1.json`. The runner requires a fresh QAC census, candidate hash checks, exclusive admission, and one-boot guard. This phase did not execute it.

**Smallest justified next action:** run the prepared R2R1 one-boot attribution procedure after fresh QAC admission. Use the first recorded internal branch and available callback status to select any later source correction; do not infer a guest root cause from this hosted result.
