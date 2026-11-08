# C188 R8 — First Managed Entry Native Qualification

## Outcome

**`NATIVEAOT_MANAGED_ENTRY_NATIVE_BLOCKED` (Outcome C).** The R7 candidate completed native runtime initialization and returned from generated `InitializeModules`. Its `ManagedMain` call then failed in the NativeAOT reverse-P/Invoke attachment path before the probe body produced evidence. The expected computation (`49`) and a safe return from `ManagedMain` were not observed.

The single authorized guest boot completed its capture and QAC cleanup. The runner exited nonzero afterward while constructing its final manifest; the raw serial, QMP, QAC, and analyzer files were preserved, and an offline review records the native result.

## Repository and candidate

- Repository: `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT`
- Branch: `v1.1_DOTNET_SUPPORT`
- Starting HEAD: `3cec0d4167d512cbe0c220c3bab5b3e07e64522b`
- Final implementation HEAD before this report-only closeout: `001a3dd8a7d80aefb003267c76a6e9a524a406da`
- The closeout commit adds this report only; its SHA is recorded in the task closeout.
- No push occurred.

R7 candidate root:

`E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r12r4r5r2r3-managed-entry\candidate-20261108T035453893Z`

Candidate manifest SHA-256 is `60059682598DBCCD7D30468E2E48C7020DDD42C7A585A590481A40ED615CF1D8`. The inventory SHA-256 is `71896949B9250933651828BD72B4F44EF77153B2E88AD7462F5AB2EE560251E1`; pre-run and post-run checks both passed for 1,124 files, with zero mismatches and zero unlisted payload files. The candidate was not rebuilt or changed.

The diagnostic NativeAOT runtime is 9.0.0 AMD64 at source revision `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`. Its runtime-pack manifest SHA-256 is `AF98AA5F3BDDDADF6465F133954F15772505B50947E7485EADEE257053D25808`. The recorded R6 ThreadStore source files were found under the relocated runtime tree on `E:` and all ten source hashes matched the runtime-pack manifest.

Candidate artifact hashes:

- Managed PE: `0AE00154971F0398BCED656F1EE95C9B8013A0F08922C51160B57D00DB93657F`
- Managed ELF: `4A234C28DC61CECD4FDB0DD07D04F6E3C873C93C29D8C014742C2A0FE8BE56D1`
- Diagnostic kernel: `EEAAC4A907A11132B783DC3A8067E4FF23AD775FD0DBDCB0297387005E7855C9`

The generated map resolves `HostLogProof_HostLogProof_ManagedEntryProbe__ManagedMain` at `0x0000000100001920`. The method is a Cdecl `UnmanagedCallersOnly` entry with seed `14`, expected value `49`, entry marker `0x5237454E`, and completion marker `0x5237444E`. The generated code contains the marker stores and arithmetic. The body uses caller-owned memory and fixed-width integer operations; it requests no collection. Runtime initialization may allocate.

## Hosted preflight

- Candidate profile and complete integrity inventory: PASS.
- Focused managed-entry hosted check: PASS, 18 assertions. It verified the candidate ABI, linked symbols and disassembly, runtime contract, production isolation, and runner guard/reporting contracts. Output: `E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r12r4r5r2r3-managed-entry\hosted-preflight-r8-postcapture-fix\hosted-test-results.json`.
- Runner hosted preflight against this exact candidate: PASS. It produced an isolated writable ESP and UEFI state, with `qacQueried=false`, `qacReserved=false`, `qemuLaunched=false`, and `guestBoots=0`.
- QAC admission validation: reused R7's compatible PASS result, 32 cases. The admission module and its predicate contract were unchanged.
- Production NativeAOT and production kernel qualifications remained PASS with their R7 hashes. Production outputs remain isolated from the managed-entry diagnostic profile.
- One-boot guard: the R7 runner originally named its guard with a fresh correlation ID, which would not prevent a later invocation from selecting another path. Before launch, the runner was corrected to bind the atomic `CreateNew` guard path to the candidate manifest SHA-256. The guard was absent at preflight, created after exclusive admission, and preserved after the one boot. Its SHA-256 is `CE98436432404CAC22F09CD913D94F2D70FA77D82FDFB4E51B978E4A5711636E`. It binds one guest boot and disables automatic retry.

The relocated runtime source path is now an explicit hosted-check input; no candidate path was rewritten. The R8 hosted checker passed against the `E:` source tree without rebuilding any candidate artifacts.

## QAC ownership and execution

Immediately before launch, QAC reported `Coordinator: Online`, zero foreign QEMU processes, zero reservations, zero managed sessions, and zero queued requests. No local QEMU process was present.

QAC granted exclusive reservation `7e8e69c202334869b158b7e20af2b2de` for project `guideXOSServer`, mode `acceptance`. The recorded owner was `LEON2_RDT\guideX (PID 7176)` with verified liveness. QEMU PID `22232` ran from `2026-11-08T15:33:44.985Z` through `2026-11-08T15:33:58.815Z` UTC. The existing QAC runner used a fresh run directory and writable UEFI variable image; the R7 candidate remained the input.

The stable attempt record is:

`E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r12r4r5r2r3-managed-entry\guest-launch-attempt-60059682598DBCCD7D30468E2E48C7020DDD42C7A585A590481A40ED615CF1D8.json`

There was exactly one QEMU launch. No retry or second invocation occurred.

## Native milestones

| Milestone | Result | Evidence |
|---|---|---|
| M1 — Native runtime initialized | PASS | Native serial state recorded `runtime=1`, `rhInitialize=1`, ThreadStore ready, GC ready, module registration, and code-manager registration. |
| M2 — `InitializeModules` returned | PASS | `module_initialize_complete` and `result=initialize_modules_returned`; the transition record had `completed=1`, `fatal=0`. |
| M3 — Managed entry reached | NOT MET | The native caller printed `event=attempt`, followed by reverse-transition entry and fail-fast. No managed-body record or entry-state marker was emitted. The native pre-call marker does not prove `ManagedMain` body execution. |
| M4 — Managed computation completed | NOT MET | No recorded entry/completion markers and no result `49`. |
| M5 — Managed return succeeded | NOT MET | No managed returned marker or native post-return validation. |
| M6 — Guest shutdown completed | PASS | The runner issued QMP `quit` after capture; QEMU exited with code `0`; QAC reported release and an empty post-run census. |

### First failing boundary

The serial log records `FAIL_FAST reason=50414C06` at line 1014. The following state record reports `callSite=FFFFFFFE`, `callerRip=0x0000000100052D94`, `rawThread=currentThread=0x0000000003D76360`, a null transition frame, and PAL FLS value zero. The candidate PE map places that return address inside `guideXosStockRhpReversePInvokeAttachOrTrapThread2`, whose mapped start is `0x0000000100052CF0` and whose next helper starts at `0x0000000100052DE0`.

The pinned `thread.cpp` reverse-P/Invoke path checks `Thread::IsCurrentThreadInCooperativeMode()` and calls `PalPrintFatalError`/`RhFailFast` for an invalid UnmanagedCallersOnly transition. The captured failure is in that attachment/prologue path, before the `ManagedMain` body emitted its record. The call-site ID remains unresolved by the direct call-site attribution map; this report does not infer a deeper runtime defect from the sentinel.

## Runner and preserved evidence

The QAC runner process exited with status `1` after the guest capture and cleanup. Strict mode rejected access to the optional top-level `failFast` property while the runner built its final guest manifest, so `guest-manifest.json` was not written. QAC's own log records QEMU exit code `0` and reservation release; the QMP log records host-initiated shutdown.

The original analyzer output classifies the run as incomplete because it rejects the two fatal/state snapshots emitted across the successful initializer return and later fail-fast. It preserves serial SHA-256 `FB978D9D38892F67293FD6D560BF1CA001890938010BF9921142FB4466BE9E91`. The added offline review uses the preserved raw log, QAC records, QMP log, candidate map, and runtime source evidence:

`E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r12r4r5r2r3-managed-entry\qac-guest-managed-entry-C188R12R4R5R2R3R7-20261108T153340698Z-ad6b4ecd48a344a5b98b74762d75eee9\r8-offline-qualification-review.json`

The runner's optional-field access was corrected and covered by the 18-assertion hosted check. No guest was relaunched to verify this post-capture correction.

QAC post-run status and list both showed zero reservations and sessions; the Windows QEMU census was complete with zero processes. No owned QEMU process remains. The candidate inventory passed again after execution. The source ramdisk and firmware templates retained their recorded hashes.

Raw and analyzed artifacts remain in the run directory above, including `serial.log`, `qmp.log`, QAC before/during/after snapshots, ownership identity, run arguments, and both original analyzer outputs. Serial SHA-256 is `FB978D9D38892F67293FD6D560BF1CA001890938010BF9921142FB4466BE9E91`; QMP SHA-256 is `36C130C2947E4D594FA61B8458837420E3A396D0467A5E59FD6D6EACFE56DE09`.

## Remaining limits and next phase

This run did not qualify the `ManagedMain` computation or return. It also does not qualify general managed allocation, collection, exceptions, managed threads, application framework behavior, persistent managed lifecycles, Settings or Notes, or production managed execution.

Next, investigate the Thread mode and transition-frame state presented to the second reverse-P/Invoke entry after `InitializeModules` returns, including PAL FLS and ThreadStore identity. Keep this R7 candidate and its consumed one-boot guard immutable. Any follow-on native qualification should use a separately prepared candidate and a fresh, explicit one-boot authorization after hosted evidence is ready.

## Preservation and working tree

The three pre-existing modified object files were preserved with their original SHA-256 values:

- `c188r12r4r5r4r5r2r2_native_alloc_operator_new_test.obj`: `3507A24E07257422C98CAB4F7430E094A2C83DF93647E6A02621010DD585A2BD`
- `c188r12r4r5r4r5r2r2_native_alloc_record_test.obj`: `605D6F7F066BAD3669B52C21A89722A7C9750F9C6595EDFDA26BC06E3125AEC3`
- `guidexos_nativeaot_native_alloc_record.obj`: `2CD941199D5F34F81F72B0B718A8D1C1023CEAD831464724DEBFEA435F044183`

The R8 runner/checker corrections are committed in `001a3dd8a7d80aefb003267c76a6e9a524a406da`. The report is committed separately as a documentation-only closeout. The object files remain modified and unstaged; no reset, clean, stash, or push was performed.