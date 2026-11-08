# C188 R7 — First Managed Entry Probe

## Outcome

**`NATIVEAOT_MANAGED_ENTRY_HOSTED_READY_GUEST_DEFERRED` (Outcome D).** The isolated candidate, generated managed entry, production builds, and hosted contracts passed. No R7 native guest boot was admitted because the QAC coordinator did not report an empty, consistent lane while a foreign reservation was present. Therefore R7 has no guest evidence for managed entry, computation, return, or shutdown.

This is a bounded proof candidate, not authorization for the Settings or Notes applications, general managed execution, garbage collection, or a persistent managed lifecycle.

## Repository and provenance

- Repository: `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT`
- Branch: `v1.1_DOTNET_SUPPORT`
- Expected starting HEAD: `d73463ceacc999ae4bd4727614ec78209e7bf595`
- R7 implementation commit and candidate source HEAD: `c5a16010e9064ce4a0f387b29fff49a517e5ce5a` (`C188 R7: add isolated NativeAOT managed entry probe`)
- Pinned NativeAOT runtime source: `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`
- R7 report is committed separately after the implementation commit; the repository HEAD after that report-only commit is in Git history.

The R7 candidate was built after the implementation commit. The three pre-existing modified root `.obj` files were not staged or used as candidate inputs; their hashes were checked again after builds:

| File | SHA-256 |
|---|---|
| `c188r12r4r5r4r5r2r2_native_alloc_operator_new_test.obj` | `3507A24E07257422C98CAB4F7430E094A2C83DF93647E6A02621010DD585A2BD` |
| `c188r12r4r5r4r5r2r2_native_alloc_record_test.obj` | `605D6F7F066BAD3669B52C21A89722A7C9750F9C6595EDFDA26BC06E3125AEC3` |
| `guidexos_nativeaot_native_alloc_record.obj` | `2CD941199D5F34F81F72B0B718A8D1C1023CEAD831464724DEBFEA435F044183` |

## R6 boundary preserved

R6 result was `NATIVE_TRANSITION_ADVANCED`, documented in [the R6 report](NATIVEAOT_C188R12R4R5R4R5R2R2R4R2R3R6_NATIVE_THREAD_LAYOUT_REQUALIFICATION.md). Its native evidence is under:

`E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r12r4r5r4r5r2r2r4r2r3-transition-attribution\qac-guest-transition-attribution-C188R12R4R5R2R3R1-20261108T010954570Z-45becc864dfd4ac78f315f3111c5fccb`

- R6 serial SHA-256: `A5B142C50FFFD6DAEC2BB0D6035BE3DC510E453E501DF193AF2B122F7112CA5B`
- R6 QAC reservation: `3561fc445a9444f4a362f8e59a6d8bc2`
- R6 candidate manifest SHA-256: `807613ADFC0D6E5F8F75A0DC9C4B123982FB5AECCCAAFB2F9CB47A9357160EB6`

R7 retains the R6 transition gate and its diagnostic-only early return. In `kernel/core/nativeaot_pal_qemu_test.cpp`, the transition prepare call must return status zero before dispatch. The R7 profile then calls the separately linked `ManagedMain` probe; the existing `#else` branch still reports `initialize_modules_returned managedEntry=not_requested` and returns. Thus the R6 acceptance behavior remains selectable and unchanged for the non-R7 diagnostic profile.

The runtime path continues to install the NativeAOT TLS/runtime environment and invoke generated `InitializeModules` through `GuideXosNativeAotC188R12R4R5R2R3InitializeModulesAttributionPrepare`. The prepare routine first initializes runtime state in read-only-gate mode and captures prerequisites; on success it runs the real initialization path. The generated reverse-P/Invoke prologue calls the platform `RhpReversePInvoke`, which initializes the runtime state, delegates attachment and transition-frame work to pinned `guideXosStockRhpReversePInvoke`, and validates actual ThreadStore publication. It returns through the generated reverse-P/Invoke epilogue. R7 calls its probe only after this gate and the successful `InitializeModules` return.

R6 had already verified corrected Thread field offsets, flags, transition-frame layout, PAL LocalStorage FLS, and authentic ThreadStore membership. R7 keeps those checks and additionally captures membership, `TSF_Attached`, matching PAL FLS/current-thread identity and OS/PAL thread IDs, preemptive mode, `TOP_OF_STACK_MARKER`, stack bounds/current stack pointer, module initialization, and code-manager registration after the probe returns.

## Managed entry contract and probe

The probe source is `samples/managed/HostLogProof/ManagedEntryProbe.cs`. Its method is:

```csharp
[UnmanagedCallersOnly(
    EntryPoint = "ManagedMain",
    CallConvs = new[] { typeof(CallConvCdecl) })]
[MethodImpl(MethodImplOptions.NoInlining)]
public static int ManagedMain(ManagedEntryProbeRecord* record, uint seed)
```

The record has an explicit packed 16-byte layout. With seed `14`, the method writes entry marker `0x5237454E`, the input, the computed value `14 * 3 + 7 = 49`, completion marker `0x5237444E`, and returns `49`. Writes are volatile. The body uses fixed-width integer operations and caller-owned record memory; it contains no allocation, P/Invoke, managed exception, threading, I/O, persistent state, or collection request.

The generated map resolves `HostLogProof_HostLogProof_ManagedEntryProbe__ManagedMain` at `0x0000000100001920`. The PE disassembly at that address shows the two managed-side marker immediates and arithmetic, followed by return of the result. The generated method also contains the required stock `RhpReversePInvoke` call at `0x000000010004CB20` and `RhpReversePInvokeReturn` call at `0x000000010004D170`. They bracket the managed body and are the transition wrapper/return path; they are not evidence that the complete runtime startup path is allocation-free.

`InitializeModules` may allocate during runtime initialization. The pinned implementation performs module/type-manager initialization and can allocate an `object[]` and a runtime handle. The probe body is allocation-free, but **the complete R7 initialization-plus-entry sequence is not claimed to be allocation-free**. R7 does not request a collection. Generated entry plus hosted source/disassembly checks establish the contract candidate, but a native guest run is still needed to establish execution and return.

## Candidate and artifact evidence

Candidate root:

`E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r12r4r5r2r3-managed-entry\candidate-20261108T035453893Z`

- Candidate result: `MANAGED_ENTRY_PROBE_HOSTED_READY`
- Candidate manifest SHA-256: `60059682598DBCCD7D30468E2E48C7020DDD42C7A585A590481A40ED615CF1D8`
- Integrity inventory SHA-256: `71896949B9250933651828BD72B4F44EF77153B2E88AD7462F5AB2EE560251E1`; inventory verification passed for 1,124 files.
- Runtime-pack manifest SHA-256: `AF98AA5F3BDDDADF6465F133954F15772505B50947E7485EADEE257053D25808`
- Managed PE SHA-256: `0AE00154971F0398BCED656F1EE95C9B8013A0F08922C51160B57D00DB93657F`
- Managed ELF SHA-256: `4A234C28DC61CECD4FDB0DD07D04F6E3C873C93C29D8C014742C2A0FE8BE56D1`
- Isolated diagnostic kernel SHA-256: `EEAAC4A907A11132B783DC3A8067E4FF23AD775FD0DBDCB0297387005E7855C9`
- Production NativeAOT qualification: PASS; qualification file SHA-256 `0A08ACD86F6E2B61C5AF5BE65297AEBEE159AB484A0CA0F25BB6A55E497FB195`
- Production kernel qualification: PASS; qualification file SHA-256 `E2424A42A9B90A907670F1EF9B4B9C065A295311CC707310E824489AA78473AE`
- The R7 source is selected only by the isolated profile. Production compilation excludes `ManagedEntryProbe.cs`; production NativeAOT and kernel qualifications passed without the probe selector.

Final candidate hosted-preflight record:

`E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r12r4r5r2r3-managed-entry\qac-guest-managed-entry-C188R12R4R5R2R3R7-20261108T040103594Z-21398041a1554e208610dcab50585ff8\hosted-preflight.json`

It records `TRANSITION_PROFILE_HOSTED_PREFLIGHT_PASS`, candidate hash match, `qacQueried=false`, `qacReserved=false`, `qemuLaunched=false`, `guestBoots=0`, and `attemptGuardCreated=false`. This was a hosted-only final-candidate preflight, not QAC admission and not a guest run.

## Hosted commands and results

The R7 focused check was run against the candidate runtime manifest, generated map, managed disassembly, and isolated kernel build log:

```powershell
powershell -NoProfile -File .\scripts\dotnet\test-c188r12r4r5r2r3-managed-entry-probe.ps1 `
  -OutputDirectory 'E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r12r4r5r2r3-managed-entry\candidate-20261108T035453893Z\managed-entry-probe-hosted-tests' `
  -RuntimePackManifest 'E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r12r4r5r2r3-managed-entry\candidate-20261108T035453893Z\runtime-pack\runtime-pack.manifest.json' `
  -ManagedMap 'E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r12r4r5r2r3-managed-entry\candidate-20261108T035453893Z\managed\artifacts\HostLogProof.map' `
  -ManagedDisassembly 'E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r12r4r5r2r3-managed-entry\candidate-20261108T035453893Z\managedEntryProbe.disassembly.txt' `
  -KernelBuildLog 'E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r12r4r5r2r3-managed-entry\candidate-20261108T035453893Z\kernel-build.log'
```

Result: PASS, 16 assertions. It checked symbol/ABI, init-before-entry ordering, safe probe body, marker/result semantics, production isolation, pinned reverse-P/Invoke behavior, post-call ThreadStore/FLS/stack state checks, linked machine code, safe failure gates, no collection request, and null-safe blocked-run reporting.

Other hosted regressions passed:

| Command | Result |
|---|---|
| `powershell -NoProfile -File .\scripts\dotnet\test-c188r12r4r5r2r3-transition-attribution.ps1` (correct production NativeAOT/runtime-pack manifests) | PASS, 43 fixtures/assertions |
| `powershell -NoProfile -File .\scripts\dotnet\test-c188r12r4r5r2r3-transition-runner-contract.ps1` (against preserved R6 candidate) | PASS, 20 assertions; no guest |
| `powershell -NoProfile -File .\scripts\dotnet\test-c188r12r4r5r4-qac-admission.ps1` | PASS, 32 cases |
| `powershell -NoProfile -File .\scripts\dotnet\test-c188r12r4r5r4-threadstore-readiness.ps1` | PASS |
| `powershell -NoProfile -File .\scripts\dotnet\test-c188r12r4r5r2r2r4-safe-handoff.ps1` | PASS |
| PowerShell parser check for 11 modified scripts | PASS |
| Candidate production NativeAOT qualification | PASS |
| Candidate production kernel qualification | PASS |
| `git diff --check` | PASS before the report-only commit |

The R7 hosted result JSON is `candidate-20261108T035453893Z\managed-entry-probe-hosted-tests\hosted-test-results.json`. Hosted evidence is not native guest evidence.

## QAC admission and guest evidence

One full-runner admission attempt was made against the earlier pre-commit candidate. It stopped before reservation and before QEMU because the QAC coordinator/census was not online, empty, and internally consistent. The recorded result was `MANAGED_ENTRY_DEFERRED_QAC_BUSY` / `BLOCKED_QAC_PREFLIGHT`. The captured foreign reservation was `3c00c9e4b96440c0abc0f1296b62995d`, project `guideXOSServer-AARCH64-ABI3-Studio` (owner PID 12220; foreign `qemu-system-aarch64.exe` PID 12504 at capture time). QAC snapshots through `qac-after-029` still showed that reservation active. No foreign session was modified or released.

The blocked run recorded zero guest boots, no serial log, no one-boot guard, and no run-owned QEMU process. It did not acquire a R7 QAC reservation. The runner also exposed a strict-mode null formatting defect after writing its blocked manifest; the R7 implementation commit fixes the reporting path, and the 16-assertion R7 focused test covers it. The full runner was not retried, and no QEMU launch was attempted while the lane was occupied.

The final committed candidate's hosted preflight described above did not query QAC. R7 guest result remains deferred until a fresh, safe admission is available. There is no R7 serial, guest trace, or post-boot inventory to report.

## Milestones

| Milestone | R7 evidence/status |
|---|---|
| M1 — Native initialization / `InitializeModules` return | PASS in R6 historical guest evidence. Not rerun in R7 because QAC admission was blocked. |
| M2 — Genuine generated managed entry reached | Not observed; R7 guest was not booted. |
| M3 — Managed computation and markers | Not observed in a guest. Hosted generated-code inspection confirms the body encodes marker stores and `14 * 3 + 7`, but is not execution evidence. |
| M4 — Return to native with result and valid runtime state | Not observed in a guest. Native caller validation is implemented and hosted-tested only. |
| M5 — Safe guest shutdown and release of R7 QAC ownership | Not attempted; no R7 reservation was acquired. |

## Remaining limits and next phase

The candidate establishes a reviewed entry contract and preserves production isolation, but only a guest trace can prove the first managed instruction ran, that markers were written at runtime, and that the Cdecl result and thread state survived return. Runtime initialization may allocate; no collection, exception path, general allocation behavior, application framework, Settings/Notes path, or long-lived managed lifecycle was qualified.

Next: when the QAC coordinator reports a fresh, empty, internally consistent lane, perform one separately authorized single native guest run with this immutable candidate and the existing one-boot guard protocol. Do not retry automatically. Evaluate the ordered trace from native startup through `InitializeModules`, the managed call/markers/result, return-state capture, guest termination, and QAC release. Keep Outcomes A–C distinct if execution stops at an earlier boundary.
