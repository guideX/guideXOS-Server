# C188R12R4R5R4R5R2R2R4R2R3R12 — Managed Settings Object Qualification

**Outcome A — `NATIVEAOT_MANAGED_OBJECT_VERIFIED`**

One real NativeAOT managed reference object was allocated on the Workstation GC heap, constructed, read by compiled C#, and used to return `1122` to the native caller. The guest passed its post-call ThreadStore, PAL FLS, and transition-frame checks, then shut down cleanly in the single authorized QAC boot. This result qualifies this one-object, single-threaded diagnostic path. It does not qualify GC collection, concurrent threads, or persistent Settings state.

## Repository and R11 baseline

- Branch: `v1.1_DOTNET_SUPPORT`.
- Starting repository and candidate source HEAD: `dbe928321d67a044d27d5ece74b360a09273f423`.
- Final R12 implementation HEAD before this report-only closeout: `78802b4053e6e43401c2c4ff5d743e34378d18d1` (`Qualify NativeAOT managed settings object`).
- No push was performed.
- R11 remains the accepted baseline: its compiled C# `ManagedMain` returned `49` and passed ThreadStore, FLS, and transition-frame checks. R12 retained the separate R11 regression suite and did not modify R11 candidate artifacts or its consumed one-boot guard.

The three pre-existing modified `.obj` files were left unstaged and unchanged at their recorded hashes:

| File | SHA256 |
|---|---|
| `c188r12r4r5r4r5r2r2_native_alloc_operator_new_test.obj` | `3507A24E07257422C98CAB4F7430E094A2C83DF93647E6A02621010DD585A2BD` |
| `c188r12r4r5r4r5r2r2_native_alloc_record_test.obj` | `605D6F7F066BAD3669B52C21A89722A7C9750F9C6595EDFDA26BC06E3125AEC3` |
| `guidexos_nativeaot_native_alloc_record.obj` | `2CD941199D5F34F81F72B0B718A8D1C1023CEAD831464724DEBFEA435F044183` |

## Managed workload and allocation contract

`HostLogProof.ManagedSettingsObjectProbe.ManagedMain` is a distinct no-inline Cdecl `UnmanagedCallersOnly` entry. It writes R12 entry markers, constructs `DisplaySettingsProbe(640, 480, 2)`, invokes its no-inline `Calculate()`, records the computed value and completion marker, keeps the reference alive through the native return record, and returns the result. The object reference is never saved in native diagnostic state.

The constructor writes all three fields and a private construction witness. `Calculate()` checks that witness before reading the fields and returns `-1` if the constructor did not run. The generated candidate disassembly shows the constructor stores and the consumer's witness comparison and field loads/adds. The R12 managed entry's generated body resolves `RhpNewFast`, the managed constructor, and `Calculate`; hosted map and source checks found no native calculation substitute.

The allocation path preserves stock NativeAOT behavior:

- Allocator: `RhpNewFast`, through `guideXosStockRhpNewFast` on the pinned real Workstation GC heap.
- Heap/object validation: allocation-context readiness, thread attachment, method-table match, and GC-heap ownership are recorded from the actual allocation path. No C188 image-backed arena or fabricated object header is used.
- Failure behavior: stock `RhpNewFast`/`RhpGcAlloc` failure handling is retained; there is no fallback object allocation.
- The probe does not request collection. GC counts around the allocation remained zero. This does not establish that a collection or suspension can safely occur while a managed reference is live.

The candidate stays diagnostic-only. Production NativeAOT and kernel builds passed compatibility qualification, and their maps exclude the Settings probe and diagnostic profiles.

## Hosted qualification

All listed runs completed without a QEMU guest or QAC reservation unless noted as the later native boot.

| Suite | Result |
|---|---|
| R12 managed Settings object contracts | PASS, 22 assertions |
| R11 managed-entry regression | PASS, 23 assertions |
| Transition attribution | PASS, 45 assertions |
| Safe-handoff analyzer and source contracts | PASS, 22 analyzer cases and 10 source checks |
| Native allocation hosted suite | PASS; includes 2,425 shared PAL GC/VM slot-table assertions and 1,024 reserve/release cycles, plus allocation-record, backend-summary, and linked `operator new` checks |
| ThreadStore readiness model and telemetry | PASS, 7 mode-model assertions and 6 telemetry assertions |
| QAC admission predicates | PASS, 32 cases |
| Safe-handoff runner contract | PASS; fresh census, create-new guard, QAC ownership, QMP shutdown, and release contract |
| Production NativeAOT and production kernel compatibility | PASS |

Hosted object and analyzer checks are supporting evidence only; the managed heap result below comes from the native guest.

## Candidate identity and integrity

Fresh diagnostic candidate:

`E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r12r4r5r4r5r2r2r4r2r3r12-managed-settings-object\c12-20261109T083000Z`

Candidate source HEAD is the starting HEAD above. Runtime pack identity is `guidexos-nativeaot-runtime-pack-amd64-c188r12r4r5r2r3-managed-settings-object-v1`, built from pinned runtime source commit `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`.

| Artifact | SHA256 |
|---|---|
| Candidate manifest | `AC8B540302BF2F2C37F61E16625A3B929D930F2F5CF31E16F919A8A3B34E5D68` |
| Complete candidate inventory, 1,148 files | `F5864854753909C26AE8CF65B0A9CDDFBA161742BFA6036995EFE9C5BC883F92` |
| Diagnostic NativeAOT ELF | `9C5C108B8E5989BD3A3EC8A89726B1C783C9A8D91EB7EA2A3E301F0931A1A152` |
| Diagnostic kernel and staged ESP kernel | `89D3A4003D067CD96085D875F4B7952272EF69511A879FDE4D57270A3E2361DA` |
| Production NativeAOT ELF | `B792BF19E256BA6200AC0DE06900A8F9AA4E70FFA6FBE49BC7CC615DB2923DB3` |
| Production kernel | `A21A951C8DF337CEA08A7FE93580C556F0AC87F33D1B1766070FFF204B1EA168` |

The candidate manifest and complete inventory were verified after the guest. All 1,148 inventoried file hashes remained unchanged, and the runner recorded `candidateIntegrityPreservedAfterGuest=true`.

## One authorized QAC boot

Run directory:

`E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r12r4r5r4r5r2r2r4r2r3r12-managed-settings-object\qac-guest-managed-settings-object-C188R12R4R5R2R3R12-20261109T064644072Z-91564604c0e745e8b31dd306da90ac63`

The preflight saw an online coordinator with no reservations, managed sessions, or QEMU processes. QAC granted exclusive reservation `321b65e9f47a489d8fa3bd2ca3d35d13`. QAC owner PID `15396` launched its owned QEMU child PID `18396` once, at `2026-11-09T06:46:48.269Z`; QEMU exited `0` at `06:46:59.592Z`. The host sent QMP `quit`, QEMU logged `SHUTDOWN reason=host-qmp-quit`, QAC release was verified, and the post-run snapshot was clear. There was no retry or second boot.

The consumed one-boot guard is:

`E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r12r4r5r4r5r2r2r4r2r3r12-managed-settings-object\guest-launch-attempt-AC8B540302BF2F2C37F61E16625A3B929D930F2F5CF31E16F919A8A3B34E5D68.json`

Its SHA256 is `E73A6CCD021C2EED08AF1C681C865D1BEC5E97A55721889699C8910CC7AA39C6`; `maxGuestBoots=1` and `automaticRetry=false`.

## Native guest evidence

| Milestone | Evidence |
|---|---|
| Runtime initialization and `InitializeModules` | Serial records native initialization and `initialize_modules_returned` before managed dispatch. |
| Genuine managed entry | R12 attempt record identifies `UnmanagedCallersOnly-Cdecl`, `ManagedMain`, seed `14`, and `DisplaySettingsProbe`. |
| Real allocation | One `RhpNewFast` request and completion; stock allocator status `0`; size `0x20`; allocation context ready before and after; context advanced `0x20`; method table matched; GC heap ownership and thread attachment true. |
| Constructor and field calculation | The constructed-marker witness passed. The returned record has entry `52313245`, computed value `00000462`, completion `52313244`, returned/expected value `00000462`, and `record=PASS`. `0x462` is decimal `1122`. |
| Managed return to native | The native record after the call contains the same `1122` return value. No object pointer was persisted. |
| ThreadStore, PAL FLS, and transition frame | Post-call state result is `PASS`: membership and attached flags set, FLS matches the current Thread, thread IDs match, stack bounds are ready and in range, modules and code manager are ready, and transition-frame top sentinel is `FFFFFFFFFFFFFFFF`. |
| Cleanup | QMP shutdown, QEMU exit code `0`, verified QAC release, and clear post-run QAC/QEMU census. |

Key preserved evidence:

| Evidence | SHA256 |
|---|---|
| Raw serial | `ABF237C9189771E65562DA212878A25F3784CE1CFF35AEB7F26768E08A4AD664` |
| QMP log | `7561252C171751C57A82C7A1C23EF510ECD61B5D1A27BDBE1A3B883035E8895E` |
| Corrected offline analyzer output | `8D009AE597DB0F959F7E3B73AAEC36F2C71E4A4D466A64D38AEC0FEA4CFF7265` |
| Offline post-run qualification | `901667C8FC6222982949EA8252D085868B1401DC73BA453FFD293AFC42653FB4` |

## Analyzer boundary and outcome

The original online runner manifest says `MANAGED_SETTINGS_OBJECT_INCOMPLETE` with parse error `r12_managed_entry_return_contract_mismatch`. Its analyzer version, SHA256 `39D08E6C9DBC8D136DB80A74418B8A5C1AFD1207EAC9A52D8D4D5429C31B73F0`, had an event-order contract that rejected the actual `new` sequence. The raw serial records allocation between entry attempt and method return. This was an analyzer false negative, not a guest allocation or runtime failure.

The corrected analyzer was run offline against the preserved serial without rebuilding or relaunching the candidate. It validates the order `attempt < allocation < returned < post-call state`, verifies the single stock allocation and all allocation fields, and reports `MANAGED_SETTINGS_OBJECT_RETURNED_RUNTIME_STATE_READY`. Its source SHA256 is `135D5FDFC4068EA5174E8A1B0D0D4FB9275444A58273ED0676A54F803C1B4791`. The serial hash before and after analysis was identical. The offline qualification records `NATIVEAOT_MANAGED_OBJECT_VERIFIED`; the original runner outcome remains preserved and visible.

There was no guest failure boundary. No unsupported collection was attempted, no fatal was reported, and the accepted Outcome A evidence is from the one authorized guest boot plus the corrected offline analysis.

## Limits and recommended next phase

R12 qualifies one short-lived managed object, its constructor, field reads, deterministic calculation, native return, and the observed single-thread runtime state. `GC.KeepAlive` retains the object through this call, but the test observed no collection; it therefore does not prove stack-root updating under a moving collection. The probe does not establish general allocation workloads, allocation failure on the guest, finalization, exceptions, GC suspension or collection, concurrent managed threads, or persistent Settings lifetimes. Production Settings and Notes remain outside this diagnostic path.

Recommended next phase: use a separate diagnostic candidate and one-boot guard to validate one deliberately controlled collection with a live managed root, including root relocation and post-collection object-field verification. Keep concurrency and persistent application state out of that phase until collection behavior is established.
