# C188R12R4R5R4R5R2R2R4R2R3R18 — GC Root-Scan Continuation

## Outcome

**Outcome C — `GC_ROOT_SCAN_CONTINUATION_VERIFIED`.** One QAC-owned boot of the preserved R17 candidate produced two time-separated CPU and stack snapshots. The first snapshot was inside `LookupUnwindInfoForMethod` beneath `CalculateCurrentMethodState`; the second was back in `Thread::GcScanRootsWorker`. The verified caller chains retain the `RhCollect` → `RhpCollect` → `GarbageCollect` → mark phase → root-scan path. This is new native continuation evidence beyond R16's sampled lookup frontier.

This does not qualify a completed root scan or collection. There is no verified collection-completion counter/result, post-collection object access, managed return value `1123`, or post-return ThreadStore/FLS/transition evidence. The runtime probe analysis remains `MANAGED_GC_ROOT_INCOMPLETE`; no specific GC failure was proven.

## Repository state and preservation

- Repository: `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT`
- Branch: `v1.1_DOTNET_SUPPORT`
- Starting HEAD: `ccac043ea62df97a2e17aa148c2043b7457756b9`
- Final HEAD: the closeout commit containing this report and the focused hosted-test updates; the exact commit ID is recorded in the closeout response.
- No push was performed.

The three pre-existing modified object files remained outside the staged scope. Their SHA256 values were unchanged after hosted verification and native execution:

| Object | SHA256 |
|---|---|
| `c188r12r4r5r4r5r2r2_native_alloc_operator_new_test.obj` | `3507A24E07257422C98CAB4F7430E094A2C83DF93647E6A02621010DD585A2BD` |
| `c188r12r4r5r4r5r2r2_native_alloc_record_test.obj` | `605D6F7F066BAD3669B52C21A89722A7C9750F9C6595EDFDA26BC06E3125AEC3` |
| `guidexos_nativeaot_native_alloc_record.obj` | `2CD941199D5F34F81F72B0B718A8D1C1023CEAD831464724DEBFEA435F044183` |

No candidate was rebuilt, renamed, or copied. Hosted native-allocation compile outputs were isolated under the R18 artifact root.

## R17 candidate and lookup findings

The R17 report, candidate inventory, lookup outputs, runner tests, and prior QAC refusal evidence were reviewed. R17 had staged but not booted this candidate:

`E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r13-managed-gc-root\candidate-r17-progress-pair-20261112T034829000Z`

Candidate manifest SHA256: `25B3C278B35AAD1A0B09060AE079C50112C78BA656F1339745EF4AE15D8D2619`
Integrity inventory SHA256: `0A63EF85EBBB9AC20E02971B7EA77F86DE1867C89339E4557E24937734B124A5`
Pinned runtime source commit: `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`
Candidate PE SHA256: `A82AFB9CDA5D9D39A7E31598CA8883CB2FB1ABA49CF78BE8F5D3D1A235385A76`
Candidate map SHA256: `216AE93CADF1140161ABFE6EF2780CD3A83E222C87551CE9F06DE43D470379A1`

The initial inventory check passed for all 1,198 entries, with zero mismatches and zero unlisted payload. The candidate contained the GC root probe and diagnostics needed for R18. Production and diagnostic outputs remained separated in the manifest. The latest runner packaging changes are in the host runner, not embedded in or substituted for the candidate. The current runner SHA256 was `50E8B6C5385E005B557C4212C8DDDD81DDB6E5D6D5FD6DCF71DB59F4AA842C65`.

Before R18, the candidate's guard file was absent, preserving its original identity and permitting one bounded attempt. The runner created the candidate-bound guard before QEMU launch; it now records `maxGuestBoots=1`, `automaticRetry=false`, the candidate manifest hash, and the runner hash. That consumed guard is preserved. Post-run integrity verification again passed all 1,198 entries with no mismatches or unlisted payload; manifest and inventory hashes remained unchanged.

R17's offline lookup was reused without reopening the implementation. Its captured executable address `0x10001E956` belongs to the expected candidate image. The candidate's `.pdata` table contains 3,063 entries. The pinned bounded replay used at most 8 midpoint comparisons, then 10 linear comparisons, then a final check (19 `BeginAddress` comparisons total). It reproduced `GC_LOOKUP_OFFLINE_QUALIFIED` for the captured PE and map hashes above and found no contradictory unwind metadata. This qualifies only that captured lookup; it does not establish all lookups, temporal progress, or collection completion.

## QAC health and one-boot admission

The R17 refusal state was not assumed to persist. Before R18 admission, the documented QAC CLI responded and the coordinator reported online (QAC 2.0.0, PID 17584). Status and list agreed: zero reservations, sessions, queued items, or blocked items. The complete Windows process census found no QEMU process; ownership identity matched the current `LEON2_RDT\guideX` user. There was no stale reservation, QMP listener, or cleanup condition to recover. The admission tests now distinguish coordinator-unavailable, coordinator-occupied, and incomplete-census cases; unavailable status cannot pass as an empty lane.

The single runner invocation acquired reservation `8bf0459dac02478989ebe1d3eaa53571` in `guideXOSServer` acceptance mode. The QAC-verified owner was PID 17980. The owned QEMU child was PID 19296 and used QMP port 60198. Ownership was rechecked before resuming and before the second CPU/stack sample. The runner used a 180-second boot timeout, a 30-second admission window, and a 5-second inter-sample delay. No retry or second boot was attempted.

After QEMU exited, QAC status and list both showed zero reservations and sessions. The final Windows census was complete and found no QEMU process; reservation inspections were complete with zero reservations, and port 60198 was no longer listening. The runner recorded QAC release as verified. The QEMU and QAC logs, serial output, QMP transcript, sample metadata, and final manifest are preserved under:

`E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r13-managed-gc-root\qac-guest-managed-gc-root-C188R13-20261113T003700302Z-0317d25e6f6c456b850c4904190019fa`

The run manifest SHA256 is `3D93A277CA4994D4CDA3CF35DBC480A6372DF943BF7BD6AC6F9ECFDF5A413FD1`. The serial log SHA256 is `AC10CBB77C83A5CACC4CB7AD32E2818877667DCEB30C525515E27475A7B7B591`; the QMP log SHA256 is `D2349D22CC6DCB418C7C2137AA3BFC2F12C01C5126BAD07EFAC502270ED08D10`.

## Hosted verification and packaging rehearsal

All outputs were written under the separate R18 hosted artifact root:

`E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r13-managed-gc-root\r18-hosted-20261113T002215470Z`

| Suite | Result |
|---|---|
| R17 runtime-function lookup qualification | PASS, 625 assertions; offline result `GC_LOOKUP_OFFLINE_QUALIFIED`, 3,063 table entries |
| R17 candidate table-only verification | PASS; exact preserved candidate PE/map, no guest sample or QEMU |
| R17 CPU/stack progress classification | PASS, 10 assertions |
| R17 runner packaging rehearsal | PASS, 35 assertions across successful, incomplete, fatal, timeout, QAC-blocked, and guard paths |
| QAC admission | PASS, 35 cases; unavailable, occupied, incomplete census, conflicting process/ownership, and release states fail closed |
| R16 caller attribution | PASS, 30 assertions against the preserved R15 capture and candidate profile required by that suite |
| R13 managed GC probe | PASS, 75 assertions |
| R12 managed settings/object allocation | PASS, 23 assertions |
| R11 managed entry | PASS, 23 assertions |
| Transition attribution | PASS, 44 assertions |
| ThreadStore and PAL FLS | PASS, 7 mode-model and 6 telemetry assertions |
| Native allocation | PASS; compile and fixture outputs isolated under the R18 artifact root |
| Safe-handoff | PASS; 22 analyzer fixtures and 10 source-contract checks |
| Production/diagnostic isolation | PASS in candidate manifest, hosted preflight, and R13 verification |

Focused hosted coverage also verified that time-separated snapshots at different instructions in the root-scan chain can qualify progress, identical snapshots remain inconclusive, changed targets do not satisfy same-target lookup progress, and missing GC-completion evidence remains incomplete. Synthetic completed-GC evidence exercises the completion path separately from native progress. Packaging rehearsal verified candidate guard behavior for consumed and unconsumed candidates, required evidence hashes, optional metadata handling, and clean manifest creation after QEMU file handles are closed. The rehearsal did not launch QEMU or acquire a QAC reservation.

## Native milestones

The runner ended with `MANAGED_GC_ROOT_EVIDENCE_INCOMPLETE`, `ADMITTED=true`, and `CAPTURE_COMPLETE=true`. The R13 probe analysis reported `MANAGED_GC_ROOT_INCOMPLETE`; collection-started/completed flags, live-object survival, and managed return value were not established by its serial probe markers.

| Milestone | R18 evidence |
|---|---|
| M1 — native initialization complete | Yes; serial initialization-complete marker present. |
| M2 — `InitializeModules` returned | Yes; serial return marker present. |
| M3 — managed GC probe entered | Yes; managed entry attempt marker is present, and the native stack reaches the managed `__managedcode_a` caller. The probe did not log managed-entry return. |
| M4 — managed object allocated and initialized | Not independently established by the probe's serial evidence. |
| M5 — collection request reached | Native caller chain enters `RhCollect`/`RhpCollect` from the managed caller; no separate completed request marker was recorded. |
| M6 — GC mark/root-enumeration path entered | Yes; both samples resolve through `RhCollect`, `RhpCollect`, `GarbageCollect`, `garbage_collect`, `gc1`, `mark_phase`, `GCToEEInterface::GcScanRoots`, `Thread::GcScanRoots`, and `Thread::GcScanRootsWorker`. |
| M7 — runtime-function lookup completed or advanced | Yes, continuation evidence: sample 1 was in `LookupUnwindInfoForMethod`; sample 2 was back in `Thread::GcScanRootsWorker`. This shows the later snapshot was beyond the earlier sampled lookup call path. The offline lookup result alone is not used as temporal evidence. |
| M8 — stack-frame iteration advanced | Partial evidence only. The first sample's caller chain includes `CalculateCurrentMethodState`; the later sample is back in the worker. The captured iterator storage address and decoded control PC are the same in both samples, and `MethodInfo` remains opaque, so multiple iterator advances or a new logical frame are not claimed. |
| M9 — root enumeration completed | No. |
| M10 — GC collection completed | No; collection counter/result and completion evidence are absent. |
| M11 — live managed object accessed afterward | No evidence. |
| M12 — result `1123` returned to native | No evidence. |
| M13 — post-return ThreadStore, FLS, and transition state valid | Not reached. |
| M14 — QEMU exited and QAC released | Yes; QEMU exited, QAC release was independently verified, and final census/inspection were complete and empty. |

The sample pair uses candidate-bound CPU/stack captures approximately 5.42 seconds apart. Sample 1 RIP was `0x000000010005CFD0` (`LookupUnwindInfoForMethod`, offset `0x20`, `mov %r9d,%eax`); sample 2 RIP was `0x000000010005357A` in `Thread::GcScanRootsWorker`. Both exact RIPs map to the preserved PE. Both stack walks resolve a verified root-scan caller chain, with 13 verified caller frames in sample 1 and 10 in sample 2. The active iterator's decoded `controlPc` remained `0x000000010001E956` in both snapshots. The R17 offline analyzer classified the pair `GC_STACKWALK_PROGRESS_VERIFIED` / `gc-root-scan-call-chain-observed-at-a-different-instruction`, with QAC release verified and native collection acceptance false.

Serial output stopped after the managed entry-attempt and GC-root-entry markers. No managed completion, object survival, post-GC access, or `1123` return marker followed. This is a progress observation, not proof of a completed collection or a persistent stall.

## Remaining limits and next phase

The existing candidate and runner successfully exposed time-separated progress past a sampled unwind lookup, so no instrumentation change or candidate rebuild is justified by this run. The next phase should preserve this candidate and evidence, then add only a narrowly scoped observation if needed to distinguish iterator advancement and root-scan completion from time spent in the same worker. The qualification target remains one collection completion, verified survival and access of the rooted `DisplaySettingsProbe`, return value `1123`, and valid post-return ThreadStore/FLS/transition state. Any further guest requires a new candidate identity or an explicitly prepared one-boot guard and a fresh QAC admission; this R18 candidate guard is consumed and must not be reused for another boot.

## Key artifact paths

- R18 hosted verification: `E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r13-managed-gc-root\r18-hosted-20261113T002215470Z`
- Native run root: `E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r13-managed-gc-root\qac-guest-managed-gc-root-C188R13-20261113T003700302Z-0317d25e6f6c456b850c4904190019fa`
- Consumed candidate guard: `E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r13-managed-gc-root\guest-launch-attempt-candidate-r17-progress-pair-20261112T034829000Z.json`
- Saved post-run progress analysis: `E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r13-managed-gc-root\r18-hosted-20261113T002215470Z\native-progress-analysis.json`
