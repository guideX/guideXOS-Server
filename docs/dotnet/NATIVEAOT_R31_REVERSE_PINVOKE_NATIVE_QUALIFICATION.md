# NativeAOT R31 Reverse-P/Invoke Native Qualification

**Outcome C — `THREAD_ATTACHMENT_BOUNDARY_ADVANCED`.** The prepared R30 candidate reached generated `InitializeModules`, entered the stock reverse-P/Invoke path, and completed PAL FLS assignment for the current Thread. It failed before ThreadStore publication, execution-mode validation, or the first managed array allocation. The Settings factory was not reached.

One QAC-owned guest boot was used. Its one-boot guard is consumed; there was no retry. Candidate files and the three protected object files were preserved.

## Repository and candidate identity

- Repository: `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT`, branch `v1.1_DOTNET_SUPPORT`.
- Starting checkout HEAD: `153d5adaef6fcd280ce2be5f9fa933d5b70febf5`.
- Candidate source HEAD: `5829d635df33ca072ceaaf7a6a45b722fda1bced`.
- Final HEAD: the R31 report commit containing this file. The exact commit OID was recorded with the completion result after commit.
- Prepared candidate: `E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r30-thread-attachment\candidate-20261115T211220185Z-33d0d240002c4be298cc6df59c3af9f8`.
- Recorded R30 candidate-manifest SHA-256: `4D4AE0791FEAF1CA154A729DC0EF4E0D1A9840AC2067F98D499AE384438A7B38`.
- Candidate manifest SHA-256: `C310C15CB43EE6CB3C61ADBFDE39DB1723A7A261674C2CD722981BB979162F0C`.
- Candidate inventory SHA-256: `93F633B550EC5F570958F747B18D99546EB98E9429F6A249EE48892296F38A3D`; all 347 inventoried files matched.
- Candidate ELF SHA-256: `61542816430D58FD72632FE0CC69C2A4D513D851E68154FF212E3EB8F01777D8`; map SHA-256: `57112D5A2048AAB61FC268A9D7E053C30ED2388B8B09B2681EB7D3A2F7EF3BC8`; kernel SHA-256: `9931827D7F6186FF98E1DA580A0B3ED2EAAF725359A7EC096F0ED16ADF495D6B`.
- Runtime-pack manifest SHA-256: `4BE5F2AA05F487D07BEC7A6CBA3A43DDD26E57A9EC980C4E5A68F785E9CBAFA2`. It pins NativeAOT/runtime `9.0.0` at `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`, selects the R30 trace profile, and uses workstation GC without an image-backed arena.
- The real selector-5 factory remains linked: `GuideXosApplicationRegistry` → `ManagedSettingsCenter`, constructor symbol `ManagedSettingsCenter___ctor_0`. The accepted R20 unwind repair is retained; its source hash is `9F94B5388535F2224D94318AC30D69260944ADB942C5F77FF5C90F0CDA9A3D3E`.

The candidate was inspected without modification. After the post-run wrapper-only fix, all 25 source-input hashes still matched the candidate manifest; the runner and its test are not candidate source inputs. The temporary runner checkout was aligned to candidate source HEAD; after correcting a Git line-ending mismatch, its candidate source-input hashes matched as well.

The R30 report's recorded value for the first object below contains 65 hexadecimal characters, so it cannot be a SHA-256 digest. The current file's 64-character digest was independently confirmed with PowerShell and Python. The other two current hashes match the 64-character values in the R30 report. No protected object was changed during R31.

Protected object hashes still match the R30 baseline:

| Object | SHA-256 |
|---|---|
| `c188r12r4r5r4r5r2r2_native_alloc_operator_new_test.obj` | `3507A24E07257422C98CAB4F7430E094A2C83DF93647E6A02621010DD585A2BD` |
| `c188r12r4r5r4r5r2r2_native_alloc_record_test.obj` | `605D6F7F066BAD3669B52C21A89722A7C9750F9C6595EDFDA26BC06E3125AEC3` |
| `guidexos_nativeaot_native_alloc_record.obj` | `2CD941199D5F34F81F72B0B718A8D1C1023CEAD831464724DEBFEA435F044183` |

## Guard provenance and QAC admission

The earlier R30 runner failure was the inherited R24 hosted-log gate. It occurred before nested candidate integrity validation, QAC reservation, or guest launch. The candidate-specific guard directory had no earlier guard; the earlier read-only preflight belonged to a different candidate and was not reused.

A fresh bounded admission was run against this exact candidate. The report at `...\qac-preflight-r30-02dab41c54ed4cda90d2b2a380c4b7f4\qac-preflight.json` records `CLEAR`, successful QAC status/list calls, a responsive coordinator, consistent reservations, exclusive authorization, a complete host process census with zero QEMU processes, and no reservation or guard created by preflight.

The consumed guard is `...\one-boot-guards\attempt-r25-96362071a1c949b994a31a2428b389d2.json`. It binds the candidate-manifest hash above and inventory hash above; it records attempt 1 of 1, `consumed=true`, `automaticRetry=false`, and `retryAllowed=false`. Exactly one QAC-owned QEMU boot was launched.

The complete offline R30 launch path was also rehearsed with the exact candidate, including the nested runner and evidence packaging path, without requesting a QAC reservation or launching a guest. Media-copy hashes matched their source files. No unrelated R24 profile was weakened.

## Runner correction and hosted qualification

The R24 gate accepts its ordinary hosted marker or the exact R30-scoped marker, and in both cases still requires the 14-assertion snapshot result. R30 hosted checks preserve positive and negative profile cases, including rejection of a TLS-only candidate. Candidate integrity, first-failure extraction, timeout/fatal classification, QAC ownership/release, and one-shot child invocation remain required.

The first native run exposed a post-run packaging edge: the invalid, zeroed R25 record has no optional `caller` property, but the R30 wrapper dereferenced it under strict mode. I made caller extraction null-safe in `run-c188r30-thread-attachment.ps1` and added a hosted regression. The R30 suite now passes 22 checks (the prior 21 plus the new missing-caller case); a PowerShell strict-mode smoke check also confirmed that a record without `caller` is handled safely. This wrapper fix was committed with the report, after the boot; it did not change the candidate binaries, and no second boot was attempted.

Focused hosted and offline regressions:

| Qualification | Result |
|---|---:|
| R30 reverse-entry / attachment suite | 22 checks passed |
| R29 pre-allocation guard | 19 passed |
| R28 GC-state guard | 16 passed |
| R25 fail-fast origin | 21 passed |
| R24 hosted assertions and snapshot | 39 + 14 passed |
| R21 / R22 / R23 | 9 / 29 / 12 passed |
| R26 / R27 offline rehearsal | 13 / 13 passed |
| R12 ThreadStore readiness model and telemetry | 7 + 6 passed |
| R12 QAC admission | 35 passed |
| R11 lifecycle / output bounds | 7 / 6 passed |
| R20 iterator advancement hosted suite | 31 assertions passed |

The retained R20 native managed-GC evidence records one collection with a live object surviving and returning `1123`. That prior result supports the existing GC path but does not establish this startup thread-attachment contract. Archived R12 fixtures with known hash mismatches were not relaxed or treated as passing.

## Linked reverse-P/Invoke contract

The candidate ELF/map and pinned runtime sources were inspected directly. Generated `InitializeModules` at `0x1000AD660` calls `RhpReversePInvoke` at `0x1000F4900` before its first `RhpNewArray` call at `0x1000F4070`; it later calls the reverse-return helper. The generated call uses the Windows x64 argument registers and stack argument. `RhpReversePInvoke` calls the linked stock entry at `0x1000FA060`; the return stub tail-jumps to the stock return helper at `0x1000FA1B0`. The stock array wrapper calls the genuine stock allocator at `0x1000FB1B0`, behind the retained R29 schema-3, 192-byte pre-allocation guard.

Pinned `ThreadStore::AttachCurrentThread` performs PAL attachment and `Thread::Construct` before setting `TSF_Attached` and publishing the Thread under the ThreadStore lock. The R30 bridge delegates to these stock helpers; it does not manually write Thread state or use the TLS-only bridge. The stock return implementation restores the transition frame, but the guest did not reach the return path.

The preserved disassembly and machine-code review are under `...\r31-machine-code-review-24531deebc6a4e18998ae164cbb93e15`.

## Native boot evidence and milestones

Boot evidence is under `...\qac-guest-r25-96362071a1c949b994a31a2428b389d2`. The preserved serial log SHA-256 is `4F32D4FBA10021DE5D534A1D0DF02A1930B6385590060F6D13CD1EEC02A5A00A`.

| Milestone | Result | Evidence |
|---|---|---|
| M1 NativeAOT initialization reached | Pass | NativeAOT runtime and ThreadStore were initialized; module registration completed. |
| M2 Generated `InitializeModules` entered | Pass | `module_initialize_enter`, startup marker `C188B007`, then the transition event emitted from the generated call path. |
| M3 Stock reverse-P/Invoke entry invoked | Pass | `transition_enter` followed by stock PAL attachment events. |
| M4 Current NativeAOT Thread initialized and attached | **Incomplete** | PAL attach/FLS completed. The pre-entry state flags were `0`, and no post-attach state, ThreadStore publication, or transition-attach completion was recorded. |
| M5 Required attachment and execution-mode contract | **Not verified** | No post-attach state/mode checkpoint was emitted. |
| M6 Pre-allocation guard passed | Not reached | No `rhp_array_gc_state_valid` event. |
| M7 Stock `RhpNewArray` allocator entered | Not reached | No stock-array entry event. |
| M8 Managed array allocation completed | Not reached | No stock-array return or array-return event. |
| M9 Module/type-manager initialization continued | Not reached | No module-initialize-complete event. |
| M10 `InitializeModules` returned | Not reached | No return evidence. |
| M11 Real Settings factory entered | Not reached | No selector-5 factory event. |
| M12 Factory returned its actual result | Not reached | Factory was not entered. |
| M13 Reverse-P/Invoke return restored valid runtime state | Not reached | Managed entry did not return. |
| M14 QEMU shutdown and QAC release | Pass | QAC post-run verification reported release, complete process census, and no QEMU processes. |

The attachment trace shows:

- TLS block address `0x384F260`, slot index `0`; the separate Thread pointer observed by the transition is `0x384F290`.
- The pre-entry Thread state event reports pointer `0x384F290` with initialized flag `0`.
- `transition_enter` records frame `0x517F290` and raw Thread `0x384F290`.
- `pal_attach_enter` uses Thread `0x384F290`, FLS index `0`. The prior FLS value is `0`; PAL then records setting index `0` to the same Thread pointer and reports attach result `0`.
- `pal_thread_id` returns ID `1` with result `0`.
- No `threadstore_publication`, `transition_attach_state`, `transition_attach_complete`, stack-bound event, or array event follows.

These values establish that PAL assigned the expected Thread pointer to its FLS slot. They do **not** establish that the NativeAOT Thread finished construction, was published in ThreadStore, or entered the required cooperative mode.

The subsequent serial fail-fast is `reason=43525402 detail=0000000000000000`. In the linked startup-import source, `guidexos_nativeaot_startup_RaiseFailFastException` maps to this generic guideXOS fail-fast reason. This identifies the fail-fast route, not the runtime condition that requested it. The R25 record at `0x1001E34D0` contains only zero words and fails schema/magic/size/publication/caller validation; its `caller` attribution is therefore absent and no R29 `RhpNewArray` cause can be claimed. The bounded CPU snapshot pauses in the kernel halt loop at RIP `0x1F35E1` (`HLT=1`) with the same reason in RDI; it is after fail-fast and does not identify the caller.

The evidence localizes the incomplete transition to after PAL attach and thread-ID retrieval, before ThreadStore publication. Pinned source places `Thread::Construct` and subsequent ThreadStore publication in this span. The run did not capture which operation in that span requested fail-fast, so no specific remaining defect is claimed.

## Cleanup, tooling result, and next phase

R25 run evidence records `qacReleaseVerified=true`. The R30 post-run report independently records a successful final QAC status/list check, complete host process census, and an empty QEMU process list. QAC was Online with zero reservations and sessions after cleanup. A nested Settings-handoff runner timed out waiting for its kernel input loop and requested bounded QMP cleanup; this is a runner timeout, not evidence of Settings behavior. The subsequent final QAC census verified release. The outer R30 wrapper also exited during packaging because of the missing optional `caller` field; the null-safe fix and regression are included in this change. Preserved child evidence remains authoritative for the native milestones.

The next phase should keep the stock transition and existing safety guards, then add a narrowly scoped observation at the first incomplete operation between `PalAttachThread` and ThreadStore publication. In particular, capture the result and bounds from `PalGetMaximumStackBounds` and the relevant `Thread::Construct` call boundary so the generic fail-fast can be attributed. Use a new, explicitly authorized candidate/guard; this consumed guard cannot be reused. The eventual acceptance proof remains the genuine pre-allocation guard, stock allocator entry, and completed managed array allocation. Only after those milestones should testing continue toward the linked Settings factory.

No push was performed. The report and R30 optional-caller packaging fix were committed; the three protected object files remain modified in the worktree with their recorded hashes.
