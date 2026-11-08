# C188R12R4R5R4R5R2R2R4R2R3R10 — Managed-Entry Thread-Mode Handoff

**Outcome B — `MANAGED_ENTRY_HANDOFF_CAUSE_PROVEN`**

R10 identified and corrected the exact thread-mode overwrite behind the R9 failure. The corrected candidate passed hosted validation, but the only authorized R10 guest boot was used on the pre-fix diagnostic candidate. Native execution of the corrected candidate, managed result `49`, and managed return therefore remain unverified.

## Repository state and preservation

- Starting HEAD: `8b6134df4c14683c704779e0aad2e63a85ded124` on `v1.1_DOTNET_SUPPORT`.
- Ending implementation HEAD (source and hosted-regression commit): `ad4b720007c928898b27c8bbf9e584391619b289`. The report-only closeout commit follows it; final repository HEAD is reported in the task closeout.
- No push was performed.
- The three pre-existing modified `.obj` files were not staged or changed. Their verified SHA256 values are:
  - `c188r12r4r5r4r5r2r2_native_alloc_operator_new_test.obj`: `3507A24E07257422C98CAB4F7430E094A2C83DF93647E6A02621010DD585A2BD`
  - `c188r12r4r5r4r5r2r2_native_alloc_record_test.obj`: `605D6F7F066BAD3669B52C21A89722A7C9750F9C6595EDFDA26BC06E3125AEC3`
  - `guidexos_nativeaot_native_alloc_record.obj`: `2CD941199D5F34F81F72B0B718A8D1C1023CEAD831464724DEBFEA435F044183`
- The hosted-test inventory contains three generated `.obj` files with matching basenames, but their hashes differ from the protected root files. The hosted test runner compiles the test and runtime `.cpp` sources into the isolated candidate `hosted-tests` directory. Its linked integration inputs are separately named `guidexos_nativeaot_startup_imports.production.obj` and `guidexos_nativeaot_pal_contract.obj`; the protected root objects were not consumed. The protected root hashes remain unchanged.

## R9 failure and R10 native observations

R9's retained serial and exact linked call-site map identified fatal code `0x50414C06` at caller return address `0x00000001000531A4`, site 6, `RhpReversePInvokeAttachOrTrapThread2`. The source-defined fatal is the NativeAOT invalid `UnmanagedCallersOnly` entry from managed/cooperative mode. The R9 thread was attached, ThreadStore-published, and had matching PAL FLS identity; its fatal snapshot had a null transition frame and cooperative mode. R9 did not show the operation that changed the frame after `InitializeModules` returned.

The single authorized R10 QAC boot added checkpoints around initialization return, the native-to-managed dispatch, and the generated `ManagedMain` reverse-P/Invoke entry. It ran the pre-fix candidate at:

`E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r12r4r5r2r3-managed-entry\qac-guest-managed-entry-C188R12R4R5R2R3R7-20261108T201853084Z-40c5cb4e11a64fffb1d67c957551bc71`

The serial SHA256 is `2E676C2CA939E2ADB42982BBD7521D0800966A9CE214B8D09B3E032CCBE0991C`. Its relevant sequence is:

1. The stock reverse-P/Invoke return reports saved and live `m_pTransitionFrame` as `0xFFFFFFFFFFFFFFFF`, the preemptive top-of-stack marker.
2. PAL FLS still points at Thread `0x3D78360`; ThreadStore publication remains true.
3. The telemetry pop reports a restored telemetry value of null.
4. `InitializeModules` completes through stage `0x63`, but its following frame checkpoint is null.
5. Immediately before `ManagedMain`, Thread `0x3D78360` is still published and attached (`flags=1`, membership `1`); FLS and runtime Thread IDs match, both IDs are `1`. Its mode is cooperative (`coop=2`) with `frame=0`. The diagnostic capture returns status `0x00000008` (invalid frame); this early capture does not supply stack-bound fields.
6. The generated Cdecl `UnmanagedCallersOnly` probe is attempted with seed `14`. The fatal is attributed by the pre-fix candidate's exact map to the stock helper at RIP `0x0000000100053314`. The fatal state also has valid stack bounds (`0x52B1000`–`0x52B5000`) and current stack `0x52B4958`, the same Thread and OS thread ID, cooperative mode, and null frame.

No managed body marker, completion marker, result `49`, or managed return appears in this guest. The offline analysis is `RUNTIME_FAIL_FAST_ATTRIBUTED`, `managedEntryReturned=false`, and `preDispatchHandoffReady=false`.

## Pinned NativeAOT contract and proven cause

The candidate uses NativeAOT source revision `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`. In the pinned `Thread::InlineTryFastReversePInvoke` contract, a reverse entry saves the thread's current transition-frame pointer into its stack-owned reverse-P/Invoke frame. An attached thread must be preemptive (a non-null transition frame) for an ordinary reverse entry. The helper switches it to cooperative mode for managed execution. `Thread::InlineReversePInvokeReturn` restores the saved frame. A null frame is expected while managed code is active; after returning from `InitializeModules` to native dispatch, the saved top-of-stack marker should have been restored. Native C++ execution by itself does not establish the current GC mode.

The generated `ManagedMain` wrapper is a genuine reverse-P/Invoke entry and uses the existing Cdecl ABI. The failure was not an ABI mismatch, another thread, an FLS identity mismatch, or an absent ThreadStore attachment. `InlinePInvoke` and `InlinePInvokeReturn` describe the opposite managed-to-native direction and do not repair this native-to-managed entry.

The source trace proves the telemetry alias:

- `ThreadStore::RawGetCurrentThread()` returns `&tls_CurrentThread` in pinned `threadstore.inl`.
- The platform documents that TLS block offset `+0x30` is `tls_CurrentThread`; `runtimeCell(block)` is `block + 0x30`.
- The platform's `kRuntimeCellTransitionFrameOffset` is `0x40`, and the pinned runtime-layout assertion places `Thread::m_pTransitionFrame` at offset `0x40` within `tls_CurrentThread`.
- Therefore `runtimeCell(block) + 0x40` aliases `tls_CurrentThread + 0x40`, exactly `Thread::m_pTransitionFrame`.

In the ThreadStore diagnostic profile, `GuideXosThreadStorePushTelemetryFrame` was called after the stock reverse helper entered cooperative mode and wrote its telemetry frame into this aliased slot. On return, the stock helper correctly restored the saved top marker; the telemetry pop then wrote its saved null over that same live slot. R10's `transition_return_frames` event observes the stock restoration, and the next telemetry-return event plus the pre-dispatch snapshot observe the overwrite. This is **Case C/F: diagnostic telemetry corrupted the live transition frame after stock restoration**.

## Correction and source changes

The smallest correction moves telemetry nesting entirely into the fixed diagnostic side table. Push records the active frame and previous telemetry frame there; pop checks LIFO ownership, returns the previous side-table value, and decrements the bounded depth. Neither operation writes `Thread::m_pTransitionFrame` or a stock reverse frame. Overflow and invalid return order still fail closed.

Additional diagnostic checkpoints capture Thread mode and PAL FLS at `InitializeModules` return, after initialization preparation, before `ManagedMain`, and around the generated reverse-P/Invoke bridge. The managed-entry analyzer and hosted fixture validate that the failure boundary is preserved and do not classify a prologue failure as managed execution. These changes remain behind the managed-entry/ThreadStore diagnostic profile; the production NativeAOT and kernel manifests exclude the diagnostic profile.

The implementation changes are in `kernel/core/nativeaot_pal_qemu_test.cpp`, `tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_platform.cpp`, `tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_threadstore_readiness.h`, and `tools/dotnet/runtime-pack/tests/c188r12r4r5r4_threadstore_transition_test.cpp`. The runtime-pack builder, candidate builder, managed-entry test/analyzer, and runner-contract test carry the diagnostic-profile wiring, evidence checks, and hosted regression coverage.

The fix does not synthesize a frame, change GC policy, set `TSF_DoNotTriggerGc`, suppress the fatal, or alter the existing probe contract (Cdecl, seed `14`, expected result `49`, no requested collection). R6's `InitializeModules`-only qualification remains available.

## Hosted validation

All following suites ran hosted-only except the separately identified pre-fix guest. A hosted model is source-contract regression evidence, not native runtime acceptance.

| Suite | Result and coverage |
|---|---|
| Native allocation | PASS; 2,425 GC VM slot-table assertions, 1,024 reserve/release cycles, 2,048 bounded backend attempts, and linked operator-new/PAL integration. |
| Safe-handoff | PASS; 22 analyzer cases and 10 source-contract checks. |
| Transition attribution on corrected candidate | PASS; 45 assertions, including preserved R8 analysis, synthetic attribution/gate/fatal cases, and production isolation. |
| R9 offline regression with matching R9 serial and R9 candidate map | PASS; 45 assertions, including exact fatal caller, ordered post-return snapshots, and unchanged serial hash. No guest was launched. |
| ThreadStore/PAL correction regression | PASS; 7 hosted mode-model assertions and 6 side-table telemetry assertions. It covers initial unattached state, first reverse entry/return, valid second entry/return, rejection of invalid cooperative entry, side-table LIFO, overflow, and live-frame preservation. |
| Managed-entry contract on corrected candidate | PASS; 23 assertions, including the new side-table source contract, Cdecl/seed/result contract, generated prologue and return source checks, and production isolation. |
| Runner/analyzer contract | PASS; 20 assertions, including the corrected candidate inventory and hosted preflight with no QAC reservation, guard consumption, or QEMU launch. |
| QAC admission predicates | PASS; 32 cases. |

The final R9 replay used `E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r12r4r5r2r3-managed-entry\qac-guest-managed-entry-C188R12R4R5R2R3R7-20261108T184317404Z-e480eb7f72a1443ba78c862b55cca794\serial.log` and that R9 candidate's map. Two earlier exploratory invocations used mismatched candidate-map or manifest inputs; those setup failures were not counted as passing evidence. The correctly paired replay passed.

## Corrected candidate provenance

The distinct, corrected, hosted-tested candidate is:

`E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r12r4r5r2r3-managed-entry\candidate-r10-threadstore-telemetry-fix-20261108T202921768Z`

It records source HEAD `8b6134df4c14683c704779e0aad2e63a85ded124`, pinned NativeAOT source commit `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`, a 1,132-entry integrity inventory, production isolation, and no guest boot. Key SHA256 values:

- Candidate manifest: `8AEE184F12F2BF29AE4761AC7A50B4574F99863AF05D448C1B32A01DC8E2787C`.
- Integrity inventory: `A8E4342FE8F715FC0C40E1A1787CC56A88348B52A70DC9CCC507FA8A0BC65696`.
- Runtime-pack manifest: `127CD673DD9D8FA4BCB8137D316F4ABDF8555436FCB7F444AA16988E0EB35B9D`.
- Managed PE: `00C26A131C779AC320EAE05EE39BC6DCF6B1C777F0C21007BB3B80F70AF25417`.
- Diagnostic managed ELF: `BC0CB5B085A76143B02106754EEBEFD33AA9DC93C08554A67B812B3B97B3047D`.
- Diagnostic kernel and ESP kernel: `6659374B3260AE6028486D8DC1A981F5A11341C0CB4A1F1A3809A0E64A1A27F1`.
- Managed map: `7523AB2CE5CECAA3DD88C1ED62A67BC52E3D23A4E3A0BF3A82D0F364C586CD01`.
- Corrected candidate call-site map: `22761B81B271CC948712477163F90D8D8CCF673FDB6D56F07D61CA918383CDC1`.
- Proof-disabled production kernel: `A21A951C8DF337CEA08A7FE93580C556F0AC87F33D1B1766070FFF204B1EA168`.
- Proof-disabled production NativeAOT ELF: `B4F1134E350F4F7668749EFE30D6B0CA10B4614677F2F1EF814425C15218F765`.

The corrected candidate's preflight did not consume its one-boot guard and did not launch QEMU. The boot described above belongs to the earlier pre-fix diagnostic candidate (manifest SHA256 `A33029D69FD49933D1A80369A13BE035830687037F5E4BBC75991A4FDEB6D6A9`); it is not evidence that the corrected candidate ran natively.

## Native milestones and QAC cleanup

| Milestone | Evidence |
|---|---|
| Native runtime initialization | Observed on the single pre-fix guest. |
| `InitializeModules` completion | Observed through stage `0x63` on the single pre-fix guest. |
| ThreadStore/FLS identity | Same attached Thread `0x3D78360`, same OS thread ID `1`, and matching PAL FLS observed. |
| Corrected handoff | Source cause and correction are proven; corrected-candidate native execution is unverified. |
| Managed body / result `49` / managed return | Not observed. The pre-fix guest failed in the generated reverse-P/Invoke prologue. |
| Guest cleanup | QMP recorded `SHUTDOWN reason=host-qmp-quit`; QEMU exited `0`; QAC release verified. |

The one-boot guard for the pre-fix guest was consumed with boot count `1`, automatic retry disabled. QAC reservation `49d3b2a2f44e4709836a6f3025b4447c` was released. The post-run QAC snapshot recorded zero reservations, zero managed sessions, and zero QEMU processes. No second boot was made.

## Remaining unverified work and next phase

R10 does not establish that the corrected side-table build reaches the generated managed body, computes `49`, completes the managed marker, or safely returns. It does not qualify general .NET support, managed exceptions, GC safety, persistent managed lifetimes, or Settings application behavior.

**Recommended next phase: R11 corrected-candidate native qualification.** After a new explicit one-boot authorization and a fresh QAC exclusivity check, boot only the corrected candidate once. Capture the same mode/FLS/frame checkpoints, require the genuine managed entry and completion markers plus result `49`, verify reverse-P/Invoke return restores the preemptive marker, then verify QEMU shutdown and QAC release. Do not automatically retry. If it fails, retain and analyze that first corrected-candidate boundary offline.
