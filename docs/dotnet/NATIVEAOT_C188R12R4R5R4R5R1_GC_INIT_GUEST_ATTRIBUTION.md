# NativeAOT C188 R4R5R1 GC Initialization Guest Attribution

## Result

**Outcome A — `GC_INIT_ROOT_CAUSE_PROVEN` at the candidate-operation boundary.** The one qualified guest proves that `init_semi_shared()` returned integer `0` (false), which caused `initialize_gc()` and then `GCHeap::Initialize()` to return `E_FAIL` (`0x80004005`). The alternative `WaitForGCEvent` allocation/create path was not entered. The failure branch inside `init_semi_shared()` and its underlying allocator or event mechanism remain unproven, so this report recommends a narrower probe and no speculative repair.

## Repository state and retained R3 evidence

- Branch: `v1.1_DOTNET_SUPPORT`.
- Starting HEAD, and ending HEAD when qualification finished before this closeout commit: `dbf5de6cd3acfc75eb8ed0788169e71b52c2184c`.
- The pre-existing modification to `ESP/ramdisk.img` was preserved. Its SHA-256 before and after the guest is `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8`. Existing generated `bin`/`obj` and host-smoke outputs were preserved. No reset, clean, stash, restore, branch switch, push, or source-ramdisk write occurred.
- Prior R4R3 guest-003 raw serial remains byte-identical at SHA-256 `BAEF3ACE33F44924DA99DEC535EF52401075F125DBF9D05E04138B2812DFD8E9`. Its initialization analysis remains incomplete at 54 recovered records against 55 declared; the recovery manifest is `out/dotnet/c188r12r4r5r4-threadstore-readiness/postrun-recovery-r3-20261101T181441952Z/recovery-manifest.json` (SHA-256 `ABC7EEBF1048A1EDE7FA5AC3993D5BA2FDEAD15C9C039B02A76AECA9EC4E2366`).

## Candidate verification

The exact guest candidate was `out/dotnet/c188r12r4r5r4r5-gc-init-probes/candidate-20261101T195924998Z/`. Its root candidate manifest SHA-256 is `9AD3F50BAA29A4CB3A0324B346A23E656DA6C94AEB3BEBABD2175A5566D7D1EF`.

The staged qualification manifest verifies all 61 payload files: 61 present, zero hash or size mismatches, zero missing files, and zero extras. The root and staged diagnostic candidate manifests match. The full per-file and selected-artifact check is retained at `out/dotnet/c188r12r4r5r4r5-gc-init-probes/candidate-integrity-r1-20261101T203455837Z/candidate-integrity.json`; post-guest verification is at `out/dotnet/c188r12r4r5r4r5-gc-init-probes/postrun-validation-r1-20261101T204019280Z/postrun-validation.json`.

| Artifact | SHA-256 |
| --- | --- |
| Diagnostic NativeAOT ELF | `928A3BBC995CEC67E60885C7159FC8EE6B16BD641A3A0B5C23593ABAA3F080C5` |
| Diagnostic kernel ELF and staged ESP kernel | `714A7DBC77EDC32513221F63B6789BC0FED267A3495C5EF54F5E886ADCEB3341` |
| Candidate ESP ramdisk | `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8` |
| Workstation GC replacement object containing the probe | `C0503979BBB13DCBB51F29A74B6C5A306D303414FFF51E2FCAD55515BDC5387A` |
| Production NativeAOT ELF | `A69CABA99C4EF0BF7E0DA46F9C3BC91F3B30AF22E744F557C97CA84CCB695BF0` |
| Production kernel ELF | `A21A951C8DF337CEA08A7FE93580C556F0AC87F33D1B1766070FFF204B1EA168` |

The diagnostic record is the pinned NativeAOT 9.0.0 AMD64 Workstation GC from runtime commit `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`. Its record is 64 bytes with 16 fields; the archive has exactly one replacement GC member and one retained ThreadStore PAL replacement. The diagnostic map retains the GC-init symbols, the production map contains no GC-init probe symbols, production persistent-composite lifecycle checks pass, and the staged PE report and qualification manifest report zero unsupported loader imports. The diagnostic was not rebuilt for freshness. Build warning counts remain in the qualification manifest: diagnostic NativeAOT 65, diagnostic kernel 219, production NativeAOT detailed log 67, and production kernel 425.

Candidate provenance records source HEAD `1e27d474aa78d9aecf7b6ac4963a3baa32a193f5` and pinned runtime commit above. The candidate manifest's runner label says `c188r12r4r5r4r1-one-boot-guard-planned`; the shared runner was extended and hash-checked before launch. The one-boot guard records the actual runner SHA-256 used for this guest.

## Hosted preflight

Before QAC admission, both modified PowerShell files parsed with zero AST errors and all nine required hosted regression scripts passed. Results and individual logs are under `out/dotnet/c188r12r4r5r4r5-gc-init-probes/preflight-r1-final-20261101T203327435Z/`. The GC-init hosted suite ran the 19-assertion C++ record test and eight analyzer scenarios: success, `init_semi_shared` failure, `WaitForGCEvent` create failure, missing completion, missing whole record, conflicting evidence, retained R3 fixture/raw preservation, and source result-format compatibility. The earlier R3 raw hash matched before and after analysis.

One initial static assertion used the wrong capitalization for a runner source string. The test was corrected to match the source spelling; the complete pre-launch suite then passed. After the guest, a separate narrow runner correction changed `moduleRegistrationAttemptedByCandidate` to derive from an observed serial stage. Both changed scripts parsed cleanly and the same nine hosted regressions passed again under `out/dotnet/c188r12r4r5r4r5-gc-init-probes/postrun-hosted-regressions-r1-20261101T203802198Z/`. No post-run hosted test launched QEMU or reserved QAC.

`git diff --check` passed before the guest and after the closeout edits. The command emitted only Git's line-ending conversion notice for the modified test script.

## Fresh QAC admission and one-boot ownership

The independent fresh snapshot is `out/dotnet/c188r12r4r5r4r5-gc-init-probes/qac-live-prelaunch-r1-20261101T203533959Z/admission-preflight.json`, captured at `2026-11-01T20:35:35Z`. Status and list exited 0 and parsed as complete; the coordinator was online; reservations, QEMU processes, foreign QEMU processes, sessions, queued work, blocked work, and census warnings were all zero. The Windows process census and reservation inspections were complete, and exclusive clear admission was true. The runner repeated status/list, inspection, and Windows process checks immediately before launch and admitted the run.

The sole guard is `out/dotnet/c188r12r4r5r4r5-gc-init-probes/guest-launch-attempt-C188R12R4R5R4R5R1.json`, SHA-256 `1D80020B4CD3ED845DCD30B0EA7D78A74DAC5649B9BAB2699E72591A1A17D00C`. It records `maxGuestBoots=1`, `automaticRetry=false`, candidate manifest SHA-256 `9AD3F50BAA29A4CB3A0324B346A23E656DA6C94AEB3BEBABD2175A5566D7D1EF`, and runner SHA-256 at launch `AE5332D4A759FB0BF2DC2BC24F890F437CCCF86BAE3232344CC9E8B8BFFD30E5`. The current runner SHA after the post-run manifest-field correction is `EEB0CFA1391D5BEB6F85CB5E5F4CA14572956CA6B76C6264DECE085669584E36`; no second guest was run.

QAC granted reservation `5f0b7c1231524c73a98ff8f97c4a508d` to owner process 30016. Its QEMU child was PID 15940 (`C:\Program Files\qemu\qemu-system-x86_64.exe`), matching the inspected reservation and runner's recorded child identity. The runner owner PID in the guard was 27504. QEMU exited 0 after the runner issued QMP `quit`; QMP recorded the host-requested shutdown. QAC logged reservation release, and the post-run status/list, inspections, and complete Windows process census verified zero remaining reservations and QEMU processes. The run evidence is kept outside the candidate tree; post-run validation confirms all 61 staged payloads still match.

## Guest sequence and failure attribution

Raw serial is `out/dotnet/c188r12r4r5r4r5-gc-init-probes/qac-guest-r4r5r1-001/serial.log`, 14,711 bytes, SHA-256 `F071C9A7205167F8C0EF6270A3B88C60973D7EB544BD318327842406B7B61098`. It was not rewritten. The analyzer recorded the same SHA before and after analysis. The initialization trace is complete: 55 records observed and 55 declared (`0x37`), with zero ring overflow and zero serial-sink drops. The 16-field `C188R12R4R5R4R5_GCINIT` record was captured after `RhInitialize` returned.

| Order | Guest evidence |
| --- | --- |
| 1 | Runtime instance and ThreadStore creation/publication passed. GC heap/handle publication passed; singleton bits reached `0x1F`. |
| 2 | GC VM adapter initialization returned success. Two reserve callbacks completed for `0x4000` and `0x95` pages, and four commit callbacks returned `0x00000000`. |
| 3 | Two earlier `GCEventCreate` platform callbacks returned success. These precede the later `WaitForGCEvent` in source order. |
| 4 | `init_semi_shared()` completed with integer result `0` / false. Record state is 3 (explicit failure), and `firstFailureSite=1` (`GX_GCINIT_FAILURE_INIT_SEMI_SHARED`). |
| 5 | `initialize_gc()` recorded `0x80004005`; `GCHeap::Initialize()` returned `0x80004005` (`E_FAIL`). Its earlier return of the failed `initialize_gc()` result prevented entry to `WaitForGCEvent`. Wait-event allocation and create states are both 0 (not entered), with unavailable result sentinels `0xFFFFFFFF`. |
| 6 | Native `RhInitialize` returned false, mapped adapter status `00000001`. The R4 analyzer reports `INIT_FAILURE_BLOCKED` and consistent Boolean/status mapping. Module registration was not observed; ThreadStore handoff, current-thread attachment qualification, and managed entry were not reached. No managed allocation, collection, or GC ownership proof ran. |

The pinned source call is `gc.cpp:14406`; `init_semi_shared()` starts at line 14417 and its cleanup return is line 14661. The caller maps false to `E_FAIL` at lines 14406–14412. `GCHeap::Initialize()` propagates that failed `initialize_gc()` result at lines 48946–48947, before the `WaitForGCEvent` allocation at line 48955 and event creation at lines 48962–48965. This proves the function-level failure path and excludes the second candidate for this guest.

The underlying native/PAL status is unavailable: record field `nativeStatus` is `0xFFFFFFFF` and `nativeStatusSource` is 0. The aggregate `init_semi_shared()` result does not identify which cleanup branch returned false. Source/build order plus the complete trace gives one useful but unproven lead: `BACKGROUND_GC`, `FEATURE_EVENT_TRACE`, and `FEATURE_LOH_COMPACTION` are active; after the two observed full-GC event callbacks, `create_bgc_threads_support()` runs at `gc.cpp:39488`, beginning with `background_gc_done_event` at line 39492, before the event-trace arrays. The complete trace has no later platform event callback. The local `GCEvent::Impl` already-initialized guard and nothrow allocation are at `guidexos_gcenv.cpp:181–182`; the serial callback marker begins later in `guidexos_nativeaot_gcenv_startup_bridge.cpp:127`. Failure before that callback is consistent with the evidence, but the record cannot distinguish an already-populated `m_impl` from a nothrow allocation failure. This is not reported as a proven native allocation failure.

The original runner-written `guest-manifest.json` is preserved at SHA-256 `482FDDE1F8AA81B5E7F186D0B35E674CA36BF89AE3470E1D1B63C0F29399632C`. It contains a misleading `moduleRegistrationAttemptedByCandidate=true` despite `moduleRegistrationStageObserved=false`. The post-run correction is preserved as `guest-manifest-reconciled.json` (SHA-256 `897958FFB6A9577BA9AF23B1C4A979C651ECDD39B526048605CF4EBD8D913C16`); the original manifest and raw serial remain untouched. Profile-aware managed-entry property handling also remained active: the R4 schema's `managedEntryReached=false` was captured without a final-manifest exception. The reviewed derived attribution is `gc-init-attribution-analysis-reviewed.json` (SHA-256 `3B62C7CD8D6596531B273B4A3D9ECFC5F27967236EE0E8B299825DF0BD4C1BE5`).

## Next action

Add one production-isolated, allocation-free subsite code to the GC-init record around the active `init_semi_shared()` cleanup branches, with a result captured before the first background event's platform callback. The first check should distinguish the `GCEvent::Impl` guard/allocation from the later event-trace-buffer returns. Keep the fixed record bounded, hosted-test the added parser cases, and do not change allocator or PAL behavior until a branch-specific guest result supports that change.
