# C188R12R4R5R4R5R2R2R4R2R3 — InitializeModules Transition Attribution

## Result

**Outcome A — `TRANSITION_ATTRIBUTION_HOSTED_READY`.** The source contract and bounded diagnostics passed hosted qualification. A fresh diagnostic candidate and a separate proof-disabled production build are staged. The guest phase is prepared and has not run. This result does not prove current-thread attachment, a successful reverse-P/Invoke transition, `InitializeModules` completion, managed entry, or GC collection in a guest.

No QEMU process was launched, no QAC reservation was requested, no guard was materialized, and no guest boot or managed collection occurred.

## Repository and provenance

| Checkpoint | HEAD | Branch |
|---|---|---|
| Starting baseline | `2235f8a742dac013ee987f4613e8a4512641825a` | `v1.1_DOTNET_SUPPORT` |
| Candidate source and build provenance | `da927ccca237be992f40a36d6eb75141d7743446` | `v1.1_DOTNET_SUPPORT` |
| Source/test closeout before this documentation commit | `517edcdc` | `v1.1_DOTNET_SUPPORT` |

The candidate records `da927ccca237be992f40a36d6eb75141d7743446` as its source HEAD. The later `517edcdc` commit updates only the safe-handoff runner-contract regression so it accepts this explicitly gated transition profile; the candidate’s runtime artifacts and pinned analyzer/wrapper identities were rechecked against the candidate manifest. This documentation is committed separately.

The NativeAOT runtime sources are locked to commit `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`.

The source ramdisk remained unchanged: `ESP/ramdisk.img` SHA-256 `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8`.

The three pre-existing modified object files were not staged or committed. Their verified SHA-256 values remain:

| File | SHA-256 |
|---|---|
| `c188r12r4r5r4r5r2r2_native_alloc_operator_new_test.obj` | `3507A24E07257422C98CAB4F7430E094A2C83DF93647E6A02621010DD585A2BD` |
| `c188r12r4r5r4r5r2r2_native_alloc_record_test.obj` | `605D6F7F066BAD3669B52C21A89722A7C9750F9C6595EDFDA26BC06E3125AEC3` |
| `guidexos_nativeaot_native_alloc_record.obj` | `2CD941199D5F34F81F72B0B718A8D1C1023CEAD831464724DEBFEA435F044183` |

## Transition call path

The direct-ELF NativeAOT startup adapter calls `RhInitialize(false)` in `GuideXosNativeAotGcStartupMain`. The kernel then installs the loader-provided TLS block and invokes the attribution prepare export only in the explicit `GXOS_NATIVEAOT_C188R12R4R5R2R3_INITIALIZEMODULES_ATTRIBUTION` build profile.

`GuideXosNativeAotC188R12R4R5R2R3InitializeModulesAttributionPrepare` first calls `initializeRuntimeState(block, true)`. This reuses the established native startup, TLS/FLS setup, and genuine `RhRegisterOSModule`/code-manager registration path. The registration-only flag returns after registration and before the generated module initializer. A read-only snapshot then checks the ThreadStore, current TLS identity, current-thread state, PAL FLS, OS thread ID, PAL stack bounds, GC initialization trace, and module-registration state.

If the gate reports any missing or inconsistent prerequisite, the adapter returns a safe-stop status to the kernel before entering `InitializeModules`. If the gate passes, the adapter records `transitionAttempted` and calls `initializeRuntimeState(block, false)`, which invokes the real linked `InitializeModules` with the PE module handle, linker-generated module bookends, and NativeAOT class-library function table. It does not substitute a stub or skip module registration.

The linked `InitializeModules` prologue at candidate PE address `0x0000000100029020` creates its reverse-P/Invoke frame with `lea rcx,[rsp+0x30]` and directly calls `RhpReversePInvoke` at `0x000000010002903B`. Its epilogue passes the same `[rsp+0x30]` frame to `RhpReversePInvokeReturn` at `0x00000001000290EB`. The gate does not fabricate attachment or frame state. The stock reverse-P/Invoke helper remains responsible for attachment and frame linkage.

After the prepare export returns, the kernel emits the transition record and returns. Even when `InitializeModules` returns, the transition branch returns before `ManagedMain`; it never requests collection.

## Fatal attribution contract

The fixed-size `GuideXosNativeAotTransitionAttributionRecord` captures the caller return RIP, PAL fatal reason, original-status availability, first initialization failure independently, last successful stage, ThreadStore/TLS/FLS/frame state, readiness overflows, and an integrity seal. Capture is first-writer and allocation-free.

The PAL receives only a message pointer and has no stable original-status argument. The diagnostic therefore records `messageCaptureStatus=UNAVAILABLE`, zero message bytes, and the raw pointer value without dereferencing it. It resolves an exact caller RIP against a map generated from the linked candidate PE. A map miss remains unresolved. The original PAL fail-fast call with reason `0x50414C06` is preserved; that code is not presented as the underlying defect.

The pinned source audit found these five `PalPrintFatalError` callers. The candidate map binds only direct callers actually linked into this target:

| ID | Source caller and stable message | Candidate status | Caller return RIP |
|---:|---|---|---|
| 1 | `Runtime/thread.cpp:1199`, `Thread::ReversePInvokeAttachOrTrapThread` — invalid `UnmanagedCallersOnly` transition | Exact linked direct call | `0x0000000100053DA4` |
| 2 | `Runtime/startup.cpp:186`, `DetectCPUFeatures` — required instruction sets unsupported | Source caller not linked to this candidate | Unresolved (`0`) |
| 3 | `Runtime/EHHelpers.cpp:447`, `RhpHardwareExceptionHandler` — stack overflow fatal path | Source caller not linked to this candidate | Unresolved (`0`) |
| 4 | `Runtime/EHHelpers.cpp:588`, `RhpVectoredExceptionHandler` — stack overflow fatal path | Exact linked direct call | `0x0000000100054FD4` |
| 5 | `Runtime/windows/PalRedhawkMinWin.cpp:403`, `RtlRestoreContextFallbackExceptionFilter` — context restore fallback | Source caller not linked to this candidate | Unresolved (`0`) |

The absent callers retain their source IDs and message labels in `transition-callsite-map.json`, but receive zero return-address entries and cannot be resolved by the analyzer. This preserves the distinction between a source caller and code present in this linked image.

## ThreadStore prerequisite matrix

Statuses below describe the evidence available before a future guest run. “Guest-proven” refers to the earlier accepted startup/safe-handoff evidence and does not transfer to current-thread attachment or transition success.

| Prerequisite | Status | Evidence and candidate behavior |
|---|---|---|
| Runtime instance creation | Guest-proven | Earlier accepted `RhInitialize` path; candidate rechecks the live runtime pointer. |
| ThreadStore publication | Guest-proven | Earlier safe-handoff observed a non-null ThreadStore; candidate rechecks it. Publication is not treated as attachment. |
| Current-thread TLS identity | Guest-proven | Earlier guest observed the TLS vector/block; candidate compares the GS TLS vector slot with the loader block. |
| Current-thread attachment | Unqualified | Earlier safe-handoff did not establish attachment. Candidate inspects flags and FLS. An absent attachment is allowed only for exactly zero state flags, empty PAL FLS, a live raw thread object, thread ID, and valid stack bounds; the unchanged stock helper must construct/attach it. Other absent or ambiguous states stop. |
| Native thread object lifetime | Source-only | Pinned `threadstore.inl` returns the current thread-local `tls_CurrentThread` object. `Thread::Construct` initializes that object before setting `TSF_Attached`. |
| PAL stack bounds | Hosted-qualified | Linked `PalGetMaximumStackBounds` is used; the snapshot requires valid low/high bounds and the current stack pointer inside them. Guest values remain unknown. |
| Reverse-P/Invoke entry ABI | Hosted-qualified | Exact linked `InitializeModules` disassembly verifies direct `RhpReversePInvoke` and `RhpReversePInvokeReturn` calls with the same frame address. |
| Transition-frame linkage and alignment | Source-only | Pinned stock helper saves/restores the previous transition frame. An attached thread must show the top-of-stack marker or an aligned frame inside stack bounds. The linked transition has not run in a guest. |
| Cooperative/preemptive state | Unqualified | Current guest state is unknown. An attached thread passes only in preemptive mode; cooperative or ambiguous state stops. |
| GC heap readiness | Guest-proven | Earlier accepted guest completed Workstation GC initialization; candidate requires the successful GC heap trace stage. |
| OS module registration | Guest-proven | Earlier safe-handoff performed real module registration; candidate repeats and checks the registration path. |
| Code-manager registration | Guest-proven | Earlier safe-handoff registered the production code manager; candidate checks the actual registration result. |
| Module initialization | Unqualified | The target of this phase. The gate conditionally enters the real generated `InitializeModules`; no guest result exists. |
| Managed entry | Unqualified | Not requested by this candidate, even if `InitializeModules` returns. |

The absent-thread branch does not call an eager attachment helper itself. Source inspection shows that the stock `Thread::ReversePInvokeAttachOrTrapThread` calls `ThreadStore::AttachCurrentThread()` when the current thread is not initialized. The gate admits that unchanged path only when the observed state is exactly uninitialized; initialized-but-unattached state is rejected.

## Bounded diagnostic storage

- Initialization trace: 64 fixed stage slots with bounded saturating counters, separate first-failure storage, and explicit overflow/serial-drop accounting.
- ThreadStore readiness: 48 fixed marker slots, explicit overflow, and an 8-frame transition telemetry stack.
- Native allocation: fixed attempt and backend summaries; the hosted shared PAL slot-table test exercised 256 slots, 2,425 assertions, and 1,024 reserve/release cycles. Its 2,048-attempt fixture retained bounded detail rows and reported dropped details explicitly.
- Transition fatal record: fixed-size storage, first-writer capture, no allocation or dynamic message formatting, and an integrity seal.
- Existing safe-handoff evidence: its 29-stage/93-event trace remains representable with overflow `0` and serial drops `0`.

## Hosted tests and regression qualification

The final candidate hosted results report `PASS`, 20 transition fixtures, 27 compiled C++ gate assertions, and 22 safe-handoff analyzer cases. Synthetic analyzer outcomes cover attributed fail-fast, unresolved caller RIP, safe-stop, `InitializeModules` returned without managed entry, malformed/contradictory evidence, and a false claim against an unlinked caller. Synthetic attachment and fatal conditions are explicitly identified as synthetic.

The same candidate run passed the native-allocation fixed-record, actual linked nothrow `operator new`/PAL integration, and 256-slot table tests. QEMU and QAC flags in these results are both false.

Relevant regression results:

| Suite | Result |
|---|---|
| R1 init attribution | PASS |
| R3 init return contract | PASS |
| R4 ThreadStore readiness | PASS; frame preservation `2`, nested transition `1`, overflow rejection `1` |
| R4/R5 GC initialization probes | PASS |
| R4/R5/R2 semi-shared attribution | PASS; 30 assertions |
| R4/R5/R2/R2 native allocation and 256-slot PAL tests | PASS; 2,425 slot assertions, 1,024 cycles |
| Safe-handoff source/analyzer fixtures | PASS; 22 analyzer cases and 10 source-contract checks |
| Safe-handoff QAC runner contract | PASS against this candidate’s current pinned wrapper/analyzer; guard-before-run, ownership, shutdown, and release checks passed |
| QAC admission | PASS; 32 cases |
| R11 QAC ownership predicates | PASS; 15 cases |
| R11 QAC stream capture | PASS; 2 cases |
| QMP event capture | PASS |
| C188 factory predicates | PASS; 9 cases |
| R12R4R1 link boundary | PASS; 9 cases |
| R11 runtime lifecycle | PASS; 7 cases |
| Managed UI regression inventory | PASS; 40 suites, 969 cases, six incorrect manifests rejected |

The default archived safe-handoff candidate pins an older analyzer hash. The current runner-contract test now accepts the transition candidate only when both safe-handoff and transition-attribution profile flags are set. It passed against the candidate manifest without modifying the historical safe-handoff candidate or evidence.

No managed GC ownership test that would initiate collection was run. No concurrent PAL stress was run. The UI inventory is a manifest/regression inventory, not an execution of all 969 UI cases.

## Builds and candidate identity

Candidate root:

`out/dotnet/c188r12r4r5r4r5r2r2r4r2r3-transition-attribution/candidate-20261106T042749067Z`

Candidate result: `TRANSITION_ATTRIBUTION_HOSTED_READY`; mode `NativeAotInitializeModulesTransitionAttribution`; explicit transition profile enabled; safe-handoff remains represented as a separate reference mode. The production manifest reports `persistentCompositeLifecycle=true`.

| Artifact | SHA-256 |
|---|---|
| Diagnostic NativeAOT ELF | `98B621619530EC75916B9D03302BAD03090C65188056DAAF687836DDD9ACF6DA` |
| Diagnostic kernel ELF | `061C3FAADC6645728A22B69B9248A65434CEA7A73119E8FD9CA1C2BF7201B7C0` |
| Proof-disabled production NativeAOT ELF | `263CF20A1625257E0F046CFC57B289A9013F951B74DDF39E4430613129DB29FE` |
| Proof-disabled production kernel ELF | `A21A951C8DF337CEA08A7FE93580C556F0AC87F33D1B1766070FFF204B1EA168` |
| Source and staged candidate ramdisk | `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8` |
| Exact linked call-site map | `643259EDC9D4FC7EDE8E6132066071B90A82945A806C08ADD98A73EB3CF45222` |
| Generated call-site header | `7730791C697AB14C6D20F27189CE60D5809E639616F340F67581D1DBC7FD71EF` |
| Candidate manifest | `3880A326C5BDAC0E5DCE789B024BB63C095C84A5CB79B728A1F157A87A349057` |
| Candidate integrity inventory | `16DB9CF39CAB5EB5FF5E86BB207A3E346480537374898FA2C01EDB274E8E4FBC` |

The inventory records 1,482 candidate files. A separate verification recomputed every listed size and SHA-256, checked the candidate-manifest hash, and found zero mismatches.

Production isolation checks passed: diagnostic runtime/kernel profiles and transition controls are absent, the production map excludes safe-handoff and transition symbols, and the production composite lifecycle flag is true.

Recorded build warnings were non-fatal: diagnostic NativeAOT had 62 compiler/linker warning lines (including macro redefinitions and a `.CRT` section warning); diagnostic kernel had 223 warning lines (primarily unused parameters/helpers). Production NativeAOT had 65 compiler warning lines and 61 missing-PDB `LNK4099` warnings in its inner host-link log. Production kernel had 425 warning lines, primarily unused parameters/helpers. All four ELF builds and their qualification manifests completed successfully.

Candidate generation had three packaging/tooling corrections: map generation now records source callers absent from the linked image as unlinked; production builders allow the dedicated transition candidate output root; and the prerequisite-matrix path is retained through phase-plan generation. Failed attempts and their logs remain in the candidate root and are included in the final integrity inventory.

## Preserved guest evidence

Earlier safe-handoff evidence remains unchanged:

- Raw serial: `out/dotnet/c188r12r4r5r4r5r2r2r4r2-safe-handoff/qac-guest-r2r2r4r2r1-001/serial.log`, SHA-256 `27C653B08FCF0CBBD4696498FE86AD97591DA3EFFB7F27BD40A2230A18CE425C`.
- Reviewed analysis: `out/dotnet/c188r12r4r5r4r5r2r2r4r2r2-diagnostic-recovery/recovery-analysis/safe-handoff-reviewed-r2r1-final.json`, SHA-256 `150EE8075B23BC55F2BE8CE18F1F9995D091F497F97C7A12681AFBAFA60C8256`; result `SAFE_HANDOFF_INTENTIONALLY_STOPPED`, initialization and module registration accepted, 29 stages, 93 events, overflow `0`, serial drops `0`, current-thread attachment not established.

Earlier R4 fail-fast evidence remains unchanged:

- Raw serial: `out/dotnet/c188r12r4r5r4r5r2r2r4-pal-capacity-fix/qac-guest-r2r2r4-001/serial.log`, SHA-256 `D9F14AC9DB9E39868D26F2CDBA798815D6A33C89A475DC9BEAFE998FFAD28FBF`.
- Reviewed analysis: `out/dotnet/c188r12r4r5r4r5r2r2r4r2r2-diagnostic-recovery/recovery-analysis/prior-transition-failfast-reviewed-r4.json`, SHA-256 `62412AE42ED29029AFBD360F87E2CEA4286B579B6D3F55EBBC3A5BE6F98461F9`; result `MANAGED_TRANSITION_FAILED`, reason `0x50414C06`. That reason remains an uninterpreted PAL fail-fast code, not a proven defect.

The source-history reports are `NATIVEAOT_C188R12R4R5R4R5R2R2R4R2R1_SAFE_HANDOFF_GUEST_ACCEPTANCE.md` and `NATIVEAOT_C188R12R4R5R4R5R2R2R4R2R2_DIAGNOSTIC_RECOVERY.md` under `docs/dotnet/`.

## Prepared guest phase and remaining unknowns

`guest-phase/phase-plan.json` is marked `prepared_not_executed`. The one-boot guard is only a template: it is not materialized, permits at most one boot, disables automatic retry, and binds the candidate manifest, diagnostic ELF/kernel, and ramdisk. Its instructions require fresh coherent exclusive QAC admission before atomic `CreateNew` guard creation.

The next guest-only step is to perform that fresh admission and run one boot with the verified candidate. The guest must re-confirm the accepted startup milestones, record live TLS/FLS, ThreadStore/attachment, OS thread ID, stack bounds, and transition-frame state, and let the gate either stop safely or enter the real generated `InitializeModules`. Preserve raw serial before independent analysis, do not request managed collection or managed entry, and shut down only the owned guest before verifying QAC release.

Until that phase runs, current attachment and cooperative/preemptive state, actual guest frame linkage, whether the gate will stop or permit entry, the actual fatal call site/message ID, the original underlying runtime status if any, `InitializeModules` completion, managed entry, and GC collection remain unknown. The smallest justified next step is that separately authorized one-boot guest qualification; this closeout does not initiate it.
