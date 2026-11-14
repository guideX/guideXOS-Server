# NativeAOT R25 Fail-Fast Origin Attribution

## Result

**Outcome F — `FAILFAST_QUALIFICATION_GUEST_DEFERRED`.** R24's preserved CPU state identifies the terminal Windows fast-fail loop, not the source operation that entered it. R25 adds a bounded first-failure record and linked-image attribution checks. The fresh R25 candidate passed offline qualification, but the authorized guest boot was deferred because QAC already had another project's verified acceptance reservation. No cause is claimed, and no runtime repair was made without execution evidence.

## Git and artifact provenance

- Starting HEAD: `e8b89f4a1539d89f24b755030cf65f10c2511ca9`
- Final source HEAD before this report commit: `2915c075783fe2052b628f60e39c5996b62a1728`
- Branch: `v1.1_DOTNET_SUPPORT`
- Pinned NativeAOT runtime source: `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`
- Fresh candidate: `E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r25-failfast-origin\candidate-20261114T223556232Z-46e7ccf8b0a64e5b80c343f7d42b0c0e`
- R25 manifest: `E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r25-failfast-origin\candidate-20261114T223556232Z-46e7ccf8b0a64e5b80c343f7d42b0c0e-r25-candidate-manifest.json`
- Candidate integrity inventory: 347 files; `unverifiedRootObjectInputsConsumed=false`.

| Artifact | SHA-256 |
| --- | --- |
| Candidate manifest | `161B4DC3A0E7168A041690917190ACE0DDB70D9139A7E3401752180BCE3E277F` |
| Candidate integrity inventory | `7F17D22B3572F2AC0C0486785D91EEF2DE95BE7A7B223527C15ACDE5EEA5E798` |
| R25 manifest | `B1A577D51AFDB3AE1E3072C4B295C18E8BCA82AB08D3C120A26F33A70798F0C0` |
| Kernel | `9931827D7F6186FF98E1DA580A0B3ED2EAAF725359A7EC096F0ED16ADF495D6B` |
| Managed ELF | `CB12F45663C6B45EA1DA47E5FFF9C79CC1C6A46246D6CE7F6D33FAEEBDAC4F38` |
| Runtime-pack manifest | `62656660363842F604148542C06A7E7220C2EC2D49EB53633919F4B2FF9234DE` |

The candidate manifest identifies `GuideXosApplicationRegistry` selector 5 as the real `ManagedSettingsCenter` factory and `ManagedSettingsCenter___ctor_0` constructor. It retains Workstation GC, no image-backed arena, and the accepted R20 unwind primitive. The three pre-existing modified objects remain outside the commit and retain their original hashes:

| Object | SHA-256 |
| --- | --- |
| `c188r12r4r5r4r5r2r2_native_alloc_operator_new_test.obj` | `3507A24E07257422C98CAB4F7430E094A2C83DF93647E6A02621010DD585A2BD` |
| `c188r12r4r5r4r5r2r2_native_alloc_record_test.obj` | `605D6F7F066BAD3669B52C21A89722A7C9750F9C6595EDFDA26BC06E3125AEC3` |
| `guidexos_nativeaot_native_alloc_record.obj` | `2CD941199D5F34F81F72B0B718A8D1C1023CEAD831464724DEBFEA435F044183` |

## R24 fail-fast reconstruction

The exact R24 candidate is `E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r24-initializemodules-registration\candidate-20261114T201159975Z-141494eeb9f64abb942a6b979d59bfa5`. Its preserved snapshot recorded `RIP=0x1000f2f77`, `RCX=7`, `RSP=0x517f058`, and `HLT=0`. The saved QMP/register snapshot contains no stack-memory words. The preserved serial hash is `2EC81622655DC2B02DDC692CB498BC276504C7E5F7E3435DAF98DAC90B4E6344`; its last markers are `C188B006`, `C188B007`, and `[C188R11-HOST] stage=initialize-modules-call`. There is no earlier fatal record or Settings-factory marker.

In that linked image, `guideXosFailFast` begins at `0x1000f2f70`. The exact terminal instructions are `B9 07 00 00 00` (`mov ecx,7`), `CD 29` (`int 0x29`), then `EB FE` at `0x1000f2f77` (a self-loop). Source defines `kFastFailFatalAppExit = 7u` and invokes `__fastfail(kFastFailFatalAppExit)`. Windows defines the `__fastfail` argument as a `FAST_FAIL_*` termination code; code 7 is `FAST_FAIL_FATAL_APP_EXIT` ([Microsoft `__fastfail` reference](https://learn.microsoft.com/en-us/cpp/intrinsics/fastfail?view=msvc-170), [Microsoft example identifying subcode 7](https://learn.microsoft.com/en-us/answers/questions/2357838/windows-subsystem-for-android)).

That RCX value is the fast-fail ABI code installed by the terminal helper; it does not preserve the helper's original `reason` argument. The source has two literal `guideXosFailFast(7u)` call sites, in managed allocation-report and array-host-log validation. Many other call sites use different or dynamic reasons. The R24 snapshot cannot select either reason-7 source call, and no source-specific fatal text precedes the loop in the saved serial output. The RIP is in the local implementation, not a shared exception dispatcher. The snapshot cannot establish whether a prior exception or trap was converted before entering the helper, or whether a caller used a call or jump. That missing caller state is an evidence limit, not proof that no caller existed.

The existing PAL fatal path is separate: it covers `PalPrintFatalError` and the PAL reason `0x50414C06`. R24 contains no evidence that this PAL path ran. The prior [R24 handoff report](NATIVEAOT_R24_INITIALIZEMODULES_REGISTRATION_HANDOFF.md) records the preserved snapshot and linked registration analysis.

## Initializer and R20 comparison

The pinned [`StartupCodeHelpers.InitializeModules`](https://github.com/dotnet/runtime/blob/9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3/src/coreclr/nativeaot/Common/src/Internal/Runtime/CompilerHelpers/StartupCodeHelpers.cs#L910-L917) calls [`RuntimeImports.RhpRegisterOsModule`](https://raw.githubusercontent.com/dotnet/runtime/9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3/src/coreclr/nativeaot/System.Private.CoreLib/src/System/Runtime/RuntimeImports.cs#L467-L469) before type-manager creation. The pinned [`RuntimeInstance.cpp` implementation](https://github.com/dotnet/runtime/blob/9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3/src/coreclr/nativeaot/Runtime/RuntimeInstance.cpp#L1370-L1385) allocates an OS-module entry, publishes it on the runtime instance's add-only list, and returns the module handle; the [`RuntimeInstance.h` list definition](https://github.com/dotnet/runtime/blob/9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3/src/coreclr/nativeaot/Runtime/inc/RuntimeInstance.h#L456-L469) defines that ownership. R24 had already confirmed the five-argument generated ABI and real linked registration implementation. R25's candidate-specific linked checks repeat those as positive controls; they do not treat registration as a known failure.

The R25 linked candidate resolves these boundaries dynamically:

| Operation | Address / instruction |
| --- | --- |
| `initializeRuntimeState` | `0x1000f3060` |
| Direct call to `InitializeModules` | `0x1000f3285` (`+0x225`), return `0x1000f328a` |
| Generated `InitializeModules` | `0x1000ad660` |
| First reverse-P/Invoke transition | `InitializeModules+0x1b`, target `0x1000f3a40` |
| First runtime import, `RhpRegisterOsModule` | `InitializeModules+0x23`, target `0x1000f9650`, return `+0x28` |
| Native allocator / PAL virtual allocator | `0x1000f67c0` / `0x1000f5750` |
| Type-manager helper | `InitializeModules+0x41`, target `0x1000ad820` |

The R25 caller adds stage writes around the generated call, moving the shared-helper call from R24's `+0x211` to `+0x225`. The R25 hosted ELF test finds that direct call in the linked image, checks its target, and validates the actual stack/register argument setup. The generated initializer still calls the reverse-P/Invoke transition, then `RhpRegisterOsModule`, then type-manager creation in that order. These are static candidate facts only; they do not prove any of those instructions ran in R24 or R25.

R20 previously initialized NativeAOT, performed a managed collection, retained a live object, and returned `1123`. Its accepted unwind adapter and Workstation GC path remain unchanged. R24 has no CPU or stack evidence showing whether it reached module registration, reverse-P/Invoke attachment, ThreadStore/PAL FLS, GC startup, import resolution, or a later initializer operation. The evidence therefore does not justify blaming GC, registration, Settings, or an exception conversion. The historical `5,624`-byte heap figure is not evidence of available bootstrap stack.

## R25 capture and attribution

The focused diagnostic is in [`guidexos_nativeaot_failfast_origin.h`](../../tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_failfast_origin.h) and [`guidexos_nativeaot_platform.cpp`](../../tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_platform.cpp). Under the R25 profile, each `guideXosFailFast(reason)` call captures its immediate code return PC through a no-inline helper and passes it to the R25 fail-fast wrapper. The wrapper uses an interlocked first-writer claim, records one fixed 80-byte ten-word payload, publishes the validity marker last, and emits a bounded UART marker only when the transmit-ready bit is set. It then calls the original `__fastfail(7)` path and terminal loop. It does not allocate, wait, scan the stack, or access managed, GC, ThreadStore, transition-frame, or code-manager state. No thread/transition state is recorded because obtaining it safely is not established here.

The record holds magic, schema, byte length, original helper reason, fast-fail code, immediate caller capture PC, fail-fast implementation address, initialization stage, flags, and publication marker. Stages distinguish unknown, native-module preparation, pre-`InitializeModules`, and a successful initializer return. R25 defines no separate ordinal per source call; the map and captured PC supply call-site identity.

The linked-image analyzer verified the 80-byte record lies in writable zero-filled segment memory, verified the executable helper and fail-fast implementation, and checked that the captured caller PC is preceded by a direct `E8 rel32` call to the capture helper. It resolves the caller from the exact linked map and object provenance; it never trusts an arbitrary stack word. Its pass means the diagnostic is correctly linked and analyzable. It does not establish a failure cause, and the qualification result explicitly leaves `failureContractProven=false`.

For this candidate, the record is at `0x1001e14e0`, the capture helper is at `0x1000f2f80`, and the preserved `__fastfail(7)` implementation is at `0x1000f2f70`.

## Hosted verification

The fresh candidate build completed offline without QEMU. Counts below are the actual reported counts; older hosted probes that do not print an assertion total are marked `PASS` without inventing one.

| Coverage | Command / result |
| --- | --- |
| R25 first-failure and ELF behavior | `python scripts/dotnet/test-c188r25-failfast-origin.py` — 20 passed; `pwsh.exe -NoProfile -File scripts/dotnet/test-c188r25-failfast-origin.ps1` — 33 passed |
| Linked R25 record, map, executable caller, direct capture call | `python scripts/dotnet/analyze-c188r25-failfast-origin.py --candidate-root <R25 candidate>` — `PASS`; record `0x00000001001E14E0`, 80 bytes, writable segment |
| R24 initializer/registration checks on R25 link | Candidate builder's `python scripts/dotnet/test-c188r24-initializemodules.py --candidate-root <R25 candidate>` — 34 passed; discovered shared call offset `0x225` |
| R24 snapshot classifier | Candidate builder's `pwsh.exe -NoProfile -File scripts/dotnet/test-c188r24-initializemodules-snapshot.ps1 -CandidateRoot <R25 candidate>` — 14 passed |
| Preserved R24 baseline regressions | `python scripts/dotnet/test-c188r24-initializemodules.py --candidate-root <R24 candidate>` — 39 passed; `pwsh.exe -NoProfile -File scripts/dotnet/test-c188r24-initializemodules-snapshot.ps1` — 14 passed |
| R23 preparation, R22 handoff, R21 Settings linkage | Candidate build logs — 12, 29, and 9 passed, respectively |
| R20 unwind adapter | `scripts/dotnet/test-c188r20-iterator-advancement.py` with the preserved R20 candidate, QAC run, R18 saved analysis, pinned runtime source, and fresh output directory — 31 passed; shared Windows-ABI harness compiled and executed |
| R11/R11R2/R11R4 contracts | `test-c188r11-runtime-lifecycle.ps1`, `test-c188r11-output-bounds.ps1`, `test-c188r11-runner-predicates.ps1`, `test-c188r11r2-keyboard-contracts.ps1`, `test-c188r11r2-runner-predicates.ps1`, `test-c188r11r4-qac-ownership-predicates.ps1`, and `test-c188r11r4-qac-stream-capture.ps1` — 7, 6, 30, 27, 24, 15, and 2 assertions passed |
| R12 handoff, dispatch, lifetime, factory/link, QAC admission, semi-shared | `test-c188r12r1-settings-handoff-predicates.ps1`, `test-c188r12r2r1-settings-dispatch-predicates.ps1`, `test-c188r12r3-lifetime-predicates.ps1`, `test-c188r12r4-factory-predicates.ps1`, `test-c188r12r4r1-link-boundary.ps1`, `test-c188r12r4r5r4-qac-admission.ps1`, and `test-c188r12r4r5r4r5r2-semishared.ps1` — 37, 12, 13, 9, 9, 35, and 30 assertions passed |
| ThreadStore mode/readiness and telemetry | 7 and 6 assertions passed; native guest mode remained `not_attempted` |
| Transition attribution and managed-entry probes | 44 fixtures and 23 assertions passed with their matching preserved candidates |
| GC ownership/init probes, safe handoff, return contract, native allocations | Hosted qualification passed; native allocation executables ran successfully |

The R20 regression command used by the fresh candidate builder was:

```powershell
python scripts/dotnet/test-c188r20-iterator-advancement.py --repo-root D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT --candidate-root E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r13-managed-gc-root\candidate-r17-progress-pair-20261112T034829000Z --run-root E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r13-managed-gc-root\qac-guest-managed-gc-root-C188R13-20261113T003700302Z-0317d25e6f6c456b850c4904190019fa --saved-analysis E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r13-managed-gc-root\r18-hosted-20261113T002215470Z\native-progress-analysis.json --runtime-source D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT\out\dotnet\gc-feasibility-baseline\nativeaot-runtime --output-directory E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r25-failfast-origin\candidate-20261114T223556232Z-46e7ccf8b0a64e5b80c343f7d42b0c0e\r20-unwind-regression
```

The historical `test-c188r12r4r5r2r3-transition-runner-contract.ps1` replay was not a passing regression: its preserved candidate failed the current source-hash binding (`Transition analyzer hash mismatch`) during hosted preflight. No QAC boot or reservation was attempted for that replay. The matching transition-attribution fixtures did pass. This stale replay is retained as a limitation rather than treated as a product failure or success.

## QAC decision and next phase

The R25 runner verified candidate integrity and performed read-only QAC preflight. The coordinator was online, but preflight was `NOT_CLEAR`: one verified acceptance reservation was already running for project `guideXOSServer`, with QEMU PID `9516` and owner PID `25132`. R25 therefore returned `FAILFAST_QUALIFICATION_GUEST_DEFERRED`; `nativeExecutionAttempted=false`, no R25 reservation ID was acquired, and no one-boot guard was created or consumed. The other reservation was left untouched.

Run evidence is `E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r25-failfast-origin\r25-run-evidence-ca75205e465047dc9c27bed88edec89e.json`; preflight captures are under `qac-preflight-r25-ca75205e465047dc9c27bed88edec89e`. Since QEMU never started, there is no CPU/stack record, no post-QEMU artifact-hash manifest, and no QAC release to attribute to this run. Candidate integrity hashes above were checked before preflight. No R24 or earlier candidate, log, or consumed guard was modified.

When exclusive QAC admission is again clear, the next phase is one fresh preflight and at most one R25 boot using the preserved candidate. The boot should capture M1–M7 and either the first-failure record plus CPU/stack snapshot or verified initializer return. A marker or clean host-triggered QEMU exit alone must not be counted as managed execution. The real selector-5 Settings factory remains unverified until the guest reaches it.
