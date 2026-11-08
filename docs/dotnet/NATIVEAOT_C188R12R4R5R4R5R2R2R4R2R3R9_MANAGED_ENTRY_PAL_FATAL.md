# C188R12R4R5R4R5R2R2R4R2R3R9 — Managed-Entry PAL Fatal

**Outcome C — `MANAGED_ENTRY_FAILURE_BOUNDARY_NARROWED`**  
The exact PAL fatal caller and violated NativeAOT invariant are proven. The operation that changed the transition frame after `InitializeModules` returned is not yet isolated, so no runtime behavior change was made and a successful managed-entry return remains unverified.

## Git and preservation

- Starting HEAD: `537274263a4c2c81b8da0b1288f2e3ce72a19ae0` on `v1.1_DOTNET_SUPPORT`.
- Ending R9 implementation HEAD, before the report-only commit: `96697a8929803dfa6e086a131b2db7a8d01b197a`.
- No push was performed. The final report commit follows the implementation commit.
- The three pre-existing modified `.obj` files were not staged or changed. Their SHA256 values remain `3507A24E07257422C98CAB4F7430E094A2C83DF93647E6A02621010DD585A2BD`, `605D6F7F066BAD3669B52C21A89722A7C9750F9C6595EDFDA26BC06E3125AEC3`, and `2CD941199D5F34F81F72B0B718A8D1C1023CEAD831464724DEBFEA435F044183`.
- R6–R8 candidates, serials, manifests, and attempt guards were preserved. R9 build/test outputs and guest evidence are under `E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r12r4r5r2r3-managed-entry`.

## Preserved R8 evidence

R8 candidate `E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r12r4r5r2r3-managed-entry\candidate-20261108T035453893Z` passed its 1,124-entry integrity check (inventory SHA256 `71896949B9250933651828BD72B4F44EF77153B2E88AD7462F5AB2EE560251E1`; candidate manifest SHA256 `60059682598DBCCD7D30468E2E48C7020DDD42C7A585A590481A40ED615CF1D8`). Its retained serial at `qac-guest-managed-entry-C188R12R4R5R2R3R7-20261108T153340698Z-ad6b4ecd48a344a5b98b74762d75eee9\serial.log` has SHA256 `FB978D9D38892F67293FD6D560BF1CA001890938010BF9921142FB4466BE9E91`.

The serial has two ordered snapshots for the same transition sequence: a nonfatal pre-initialization gate snapshot, followed by a fatal snapshot. The first has no current Thread and says stock attachment may initialize it. The fatal snapshot has the same raw Thread pointer, an attached current Thread, matching native/observed OS thread ID `1`, cooperative mode, and a null transition frame. `InitializeModules` completed through stage `0x63`, and one `ManagedMain` dispatch attempt was logged. No managed-entry body marker, result `49`, completion marker, or managed return was recorded.

The R8 caller RIP was `0x0000000100052D94`. Its historical call-site map did not contain that exact linked return address, so offline analysis correctly retains the source attribution as unresolved. The corrected analyzer preserves both snapshots rather than rejecting them as duplicates. The R8 raw serial was not changed.

## Exact fatal attribution

R9 candidate `candidate-r9-20261108T183305726Z` captured the same failure with caller RIP `0x00000001000531A4` and PAL reason `0x50414C06`. The serial intentionally contains no dereferenced fatal message bytes. The target PAL replacement in `tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_pal_minwin_replacement.cpp` sends `PalPrintFatalError` through the `0x50414C06` fail-fast path. The R9 candidate map resolves the captured return address exactly to site 6:

- Symbol: `guideXosStockRhpReversePInvokeAttachOrTrapThread2`.
- Pinned source: `src/coreclr/nativeaot/Runtime/thread.cpp`, line 1194.
- Direct call instruction to `PalPrintFatalError`: `0x000000010005319F`; captured return address: `0x00000001000531A4`.
- Source-defined message: “Fatal error. Invalid Program: attempted to call a UnmanagedCallersOnly method from managed code.”
- Attribution method: unique exact captured RIP match in the linked R9 candidate map, with `linkedInCandidate=true` and `resolutionStatus=exact-direct-call-mapped`.

The pinned runtime checks for a null `m_pTransitionFrame` in this reverse-P/Invoke helper and issues this fatal when it determines that an `UnmanagedCallersOnly` entry is being called from managed/cooperative mode. The pinned reverse-P/Invoke return helper restores the saved transition frame. See the [pinned `thread.cpp` assertion and attach path](https://github.com/dotnet/runtime/blob/9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3/src/coreclr/nativeaot/Runtime/thread.cpp#L1081-L1112), [pinned `thread.inl` reverse-P/Invoke entry and return helpers](https://github.com/dotnet/runtime/blob/9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3/src/coreclr/nativeaot/Runtime/thread.inl#L83-L143), and [pinned Thread construction](https://github.com/dotnet/runtime/blob/9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3/src/coreclr/nativeaot/Runtime/thread.cpp#L237-L262).

This is source attribution, not message text copied from the guest. The PAL fatal code identifies the fail-fast path; the linked caller and literal establish the specific invariant violation.

## R6, R8, and R9 transition comparison

| Run | Transition and Thread evidence | Result |
|---|---|---|
| R6 | `InitializeModules` completed. ThreadStore publication and PAL FLS set the same Thread pointer (`0x3D77360`) on observed OS thread ID `1`. The run did not dispatch `ManagedMain`. Serial SHA256: `A5B142C50FFFD6DAEC2BB0D6035BE3DC510E453E501DF193AF2B122F7112CA5B`. | Successful initialization only; no second transition was tested. |
| R8 | Same raw Thread pointer across the two snapshots (`0x3D76360`); pre-initialization state was unattached, then the fatal snapshot showed attached/ThreadStore-current, OS thread ID `1`, cooperative mode, and frame `0`. No post-return frame event existed. Serial SHA256: `FB978D9D38892F67293FD6D560BF1CA001890938010BF9921142FB4466BE9E91`. | Fatal before managed body; historical map lacked the captured caller RIP. |
| R9 | `InitializeModules` completed on raw Thread `0x3D76360`, OS thread ID `1`. Immediately after stock `RhpReversePInvokeReturn`, the saved and current `m_pTransitionFrame` values were both `0xFFFFFFFFFFFFFFFF` (the preemptive top-of-stack marker). The post-return PAL FLS event reported index `0` and the same Thread pointer; ThreadStore publication reported membership `1`. The later fatal snapshot showed that same Thread attached on OS thread `1`, but cooperative mode and frame `0`. | Exact caller mapped to site 6. The dispatch attempt reached the stock reverse-P/Invoke prologue, which failed before the managed body marker. |

The R6 serial is `E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r12r4r5r4r5r2r2r4r2r3-transition-attribution\qac-guest-transition-attribution-C188R12R4R5R2R3R1-20261108T010954570Z-45becc864dfd4ac78f315f3111c5fccb\serial.log`. Its SHA256 is shown above. R8 and R9 raw serial paths are recorded in their respective evidence sections and run roots.

The first R9 state record is captured by the explicit read-only gate before `InitializeModules`; it is not a post-initialization Thread/FLS snapshot. The `palFlsValue` field repeated in the fatal record was inherited from that earlier snapshot because fatal capture refreshes the live Thread fields but not the FLS field. Therefore the fatal record’s zero FLS value is not evidence that FLS was zero at the fatal. The post-return `transition_return_fls` event is the live FLS observation and matches the Thread pointer.

The linked `ManagedMain` disassembly calls `RhpReversePInvoke` at `0x10004CB40` before its first entry-marker store (`0x5237454E`). The fatal RIP is in the helper reached by that prologue; the missing entry marker is consistent with the source and machine-code boundary.

R9 proves the frame was restored to the preemptive marker after `InitializeModules` returned, then observes a null frame/cooperative mode at the fatal helper. It does not capture the precise instruction or operation that changed the state between those observations. ThreadStore attachment, OS thread identity, valid stack bounds, the Cdecl entry contract, and the post-return PAL FLS value do not identify that operation. The evidence supports **Phase 4 classification C — transition-frame or execution-mode defect** as the first demonstrated failure condition. It does not establish a safe correction point. No attachment, FLS, ABI, or R5 layout correction was inferred.

## Changes made

- Added bounded, read-only observations immediately after stock `RhpReversePInvokeReturn` for saved/current transition frames, cached frame/current Thread, and PAL FLS index/value. No runtime transition behavior changed.
- Changed the transition analyzer to retain and validate ordered fatal/state snapshots, reject monotonicity or attribution contradictions, and resolve an unresolved stable site ID only when one exact linked candidate RIP match exists. Unmatched or ambiguous RIPs remain unresolved/incomplete.
- Fixed the managed-entry analyzer to classify a valid `ManagedMain` dispatch attempt followed by a mapped PAL fatal without claiming the managed body ran or returned.
- Made the fatal-line parser tolerate Windows CRLF records. Added regressions for R8 multiple snapshots and regression rejection, exact-RIP sentinel resolution, fatal-after-dispatch handling, and offline R9 attribution.
- Added the R9 candidate call-site entry for the inlined stock helper. Updated the runtime-pack source anchor to accept CRLF in the restored pinned runtime checkout.
- No runtime assertion was disabled, no transition state was synthesized, and no production code path was changed.

## Hosted qualification

All checks were hosted-only; each report records `qemuLaunched=false` and `qacReserved=false`.

| Suite | Result | Exact coverage |
|---|---|---|
| Native allocation hosted suite | PASS | 2,425 PAL slot-table assertions; 1,024 reserve/release cycles; 2,048-attempt bounded backend case; linked operator-new/PAL integration test. |
| Safe-handoff suite | PASS | 22 analyzer cases and 10 source-contract checks, including contradictory ordering and production isolation. |
| Transition-attribution suite after R9 | PASS | 46 fixtures; compiled gate harness has 42 assertions. Includes preserved R8 offline analysis, R9 exact-RIP offline analysis, multi-snapshot progression, contradiction rejection, ThreadStore/FLS/frame checks, and R6 initialization evidence. |
| Managed-entry suite after R9 | PASS | 19 assertions. Includes Cdecl UCO/seed `14`/expected `49`, allocation-free probe and no collection request, generated prologue/epilogue contract, runner final-manifest behavior, and a synthetic mapped fatal after dispatch that does not claim managed entry. |

The original candidate build also passed before admission: Native allocation, safe-handoff, 44 transition fixtures, and 18 managed-entry assertions. The 46-fixture and 19-assertion reruns above include the R9 post-run evidence and analyzer regressions.

## R9 candidate and one native qualification

- Candidate manifest SHA256: `C5347B4E63CBD74F659C054D3F12650CF59E8700071D11C94CC0E9C2A66B831C`.
- Integrity inventory: 1,130 entries; inventory SHA256 `B6CB7FC189C1ADAF45EF945962997DBE73A7614A657BFD17AA6D2CC6B2A915F0`. The runner verified the inventory before and after the guest; `candidateIntegrityPreservedAfterGuest=true`, with no post-run error.
- Diagnostic kernel SHA256: `A398DE0D9F78289F4CB6D9632F10C228B66802E8BD4BC273A13CF5C299864B87`; diagnostic ELF SHA256: `8786AF2EE55D1CFC3A04F0DDD11CA232884CE1F782DF3B2B17425763549C2E8F`.
- Candidate call-site map SHA256: `DFAA24CBE3F76C1F630B46044BF4DA4CA410F5C30260532D30D3050524455142`.
- Proof-disabled production kernel SHA256: `A21A951C8DF337CEA08A7FE93580C556F0AC87F33D1B1766070FFF204B1EA168`. The production manifest records transition-attribution and managed-entry probe profiles as absent. Diagnostic and production outputs used isolated build directories.
- The three premodified root `.obj` filenames do not occur in the R9 build/test logs; their hashes remained unchanged.
- Hosted runner preflight passed with a fresh isolated ESP and writable 540,672-byte UEFI variables image. The candidate-bound one-boot guard was absent before admission and consumed atomically for this run. `maxGuestBoots=1`, `automaticRetry=false`.

The single authorized QAC run used run root `E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r12r4r5r2r3-managed-entry\qac-guest-managed-entry-C188R12R4R5R2R3R7-20261108T184317404Z-e480eb7f72a1443ba78c862b55cca794`. It was admitted as project `guideXOSServer`, owner `LEON2_RDT\guideX`, reservation `7883f5eed7a14b9d80ae9c2c4f558610`. The `serial.log` SHA256 is `5AD61695A85DEB15023062242B36283E28A9D066948F417B5CF38C35B304AC8C`. QAC reported QEMU exit code `0`; `qmp.log` recorded `SHUTDOWN` with reason `host-qmp-quit`; the runner verified reservation release. The post-run `qac-after-001.status.txt` and `.list.txt` snapshot at 18:43 UTC showed zero reservations and zero QEMU processes.

An offline reanalysis at `r9-investigation\r9-managed-entry-analysis-offline.json` classifies the run as `RUNTIME_FAIL_FAST_ATTRIBUTED`, `managedEntryAttempted=true`, `managedEntryReturned=false`, with no parse error. The runner’s original report remains preserved as `MANAGED_ENTRY_PROBE_INCOMPLETE`; it predates these offline analyzer corrections.

### Native milestone results

- Native runtime initialization and `InitializeModules`: **observed complete** through stage `0x63`.
- Post-initialization ThreadStore publication and PAL FLS identity: **observed** on the same Thread/OS thread; PAL FLS value matched the Thread at reverse-P/Invoke return.
- `ManagedMain` dispatch and stock reverse-P/Invoke prologue: **attempted and reached**.
- PAL fatal caller and source-defined message: **attributed** to the exact R9 candidate callsite.
- First managed body marker, computation `49`, completion marker, managed return, and managed post-return state validation: **not observed**.
- Safe QEMU shutdown and QAC release: **verified**.

## Remaining gap and recommended next phase

The captured failure boundary is the stock `RhpReversePInvokeAttachOrTrapThread2` invalid-UCO-from-managed check. The missing observation is the live Thread state **immediately after `InitializeModulesAttributionPrepare` returns and immediately before the kernel calls `ManagedMain`**, paired with the same fields at the first instruction of the generated `RhpReversePInvoke` entry. Record the Thread flags, `m_pTransitionFrame`, deferred/cached frames, ThreadStore identity, OS thread ID, and PAL FLS value at those two points. That would locate where the post-return preemptive marker becomes a null/cooperative state. A further guest requires a new explicit one-boot authorization; this R9 run’s guard is consumed and was not retried.

At final audit, QAC showed a new reservation with a different ID (`826d11c596e04e33bb15d5082982d38a`) under the same `guideXOSServer` project and user, with one managed QEMU session. It appeared after R9’s verified release. It was left untouched and is not part of the R9 run.
