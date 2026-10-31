# NativeAOT C188R12R4R5R1 Runtime Initialization Attribution

**Outcome A — `INIT_DIAGNOSTICS_HOSTED_READY`**

The previous C188R12R4R5 guest failure is still unattributed. This phase adds bounded markers at the pinned NativeAOT startup boundaries, records safe singleton readiness bits, fixes the runner's raw-serial handling, and stages a diagnostic candidate with a separate proof-disabled production build. The diagnostic and production NativeAOT images and kernels passed their build and isolation gates. No guest was launched and QAC was not reserved.

The candidate is ready for one later, explicitly authorized QAC preflight. A successful `RhInitialize` will still leave the managed entry and GC proof gated until real ThreadStore membership can be established safely.

## Revisions and preserved worktree state

| Item | Value |
|---|---|
| Starting branch | `v1.1_DOTNET_SUPPORT` |
| Starting HEAD | `97ff83a36681c0df025bc6fa47674971db32563b` |
| Ending implementation HEAD used by the candidate | `0387695f7ccf9e3bcd78d4efaaecfca19bb09012` |
| Pinned NativeAOT source | `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3` |
| NativeAOT identity | `9.0.0`, AMD64, Workstation GC, GC interface 5.3, EE interface 2 |
| QEMU / QAC in this phase | Not launched / not reserved |

`ESP/ramdisk.img` was already modified at the start and remains modified. Its starting, final, and staged-copy SHA-256 are all `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8`. It was not staged or committed. Existing untracked sample `bin`/`obj` output and `samples/managed/HostLogProof/c182-host-smoke/` were left in place and excluded from commits. Candidate and hosted-test outputs are under ignored `out/dotnet/` directories; kernel build output is isolated under `kernel/build/`.

The implementation was committed as `6cac05f9` and the source-backed module-identity classification correction as `0387695f`. The ending implementation HEAD above is the exact source revision in the final candidate manifest. The closeout document is committed separately and does not alter the candidate inputs.

## Previous R4R5 evidence reviewed

The retained guest is `out/dotnet/c188r12r4r5-gc-ownership-proof/candidate-20261031T034601Z/qac-guest-001/`. I inspected its raw serial, guest manifest, QAC census snapshots, original and manual QMP logs, and candidate manifest rather than relying on the derived parser summary alone.

The raw serial SHA-256 is `2FD460E80F57FD664C25D7D5686204668C069C938C2E5CBE233BECDCE9BF941C`. It ends with `RhInitialize return=00000001`, `preGcState=00000000`, zero startup native allocations, `RhInitialize: FAIL`, and `ALL_FAIL`. There is no `ManagedMain` entry, target allocation, or target collection. The adapter's `preGcState` was only a success bit and did not identify a GC substage.

That single guest booted and reached the native adapter, but it did not report the internal return boundary. Its QAC record says the preflight census was empty, one reservation was admitted, QAC release was verified, and no retry occurred. The original QMP record has SHA-256 `884D33E957DC53CF38436FFE10F69A9E88F71F771F235DC7ADA2BA3916E50953`; QEMU rejected its BOM-prefixed command. A BOM-free query and quit were recorded for the same guest in `qmp-manual-shutdown.log` (SHA-256 `37160E5A248D86EAA845A3246C51FCD27787A3F7958345389212F360222E7B3E`). The runner then failed while redundantly rewriting the serial file; raw serial stayed intact. QAC and QEMU cleanup were independently verified, and no second guest was started.

The R4R5 hosted ownership graph remains useful regression evidence: three callbacks, 17 ownership observations, two compacting collections, and root/child, strong-handle, and weak-reference checks passed. That hosted result does not qualify the freestanding startup path. The R4R2 Settings failure was a bounded array capacity miss after an object fallback, not proof that the whole arena was exhausted; R4R4 found no justified arena expansion. No allocator, resident graph, Settings, or Notes changes were made here.

## Pinned source call graph

The source audit used the exact pinned checkout at `out/dotnet/gc-feasibility-baseline/nativeaot-runtime/src/coreclr/`. Main locations are `nativeaot/Runtime/startup.cpp:92-174,361-391`, `RuntimeInstance.cpp:305-323`, `GCHelpers.cpp:43-82`, `gcheaputilities.cpp:52-83`, and `threadstore.cpp:83-106`.

```text
kernel diagnostic harness
  -> install existing PAL hook tables
  -> GuideXosNativeAotGcStartupMain()
       -> RhInitialize(false)
            -> PalInit()
                 -> allocate PAL FLS slot
                 -> GCConfig::Initialize()                 [void]
                 -> GCToOSInterface::Initialize()           [bool]
            -> optional RtlDllShutdownInProgress lookup     [observation only]
            -> optional atexit registration                 [return ignored]
            -> PalGetModuleHandleFromPointer(&RhInitialize) [observation only]
            -> InitDLL(startupModule)
                 -> optional InitializeInterfaceDispatch()  [bool]
                 -> InitializeGCEventLock()                 [void]
                 -> optional EventPipe/EventTracing setup   [void calls]
                 -> RestrictedCallouts::Initialize()        [bool]
                 -> RuntimeInstance::Initialize()
                      -> allocate RuntimeInstance
                      -> ThreadStore::Create()
                           -> allocate ThreadStore
                           -> optional PalRegisterHijackCallback()
                      -> publish ThreadStore and RuntimeInstance
                 -> optional exception/stress-log setup     [void calls]
                 -> InitializeGC()
                      -> initialize free-object EEType
                      -> GCHeapUtilities::InitializeGC()     [HRESULT]
                           -> on success publish heap and handle-manager pointers
                      -> g_pGCHeap->Initialize()              [HRESULT]
                      -> RhInitializeFinalization()           [bool]
                      -> GCHandleManager::Initialize()        [bool]
                 -> optional EventPipe finish
                 -> optional DetectCPUFeatures()              [bool / fail-fast path]
            -> PopulateDebugHeaders()                         [void]
            -> return true
       -> record exact bool result as 0 (success) or 1 (failure)
       -> emit bounded trace_end and gate ManagedMain

If a future candidate is allowed past that gate:
  RhpReversePInvoke -> initialize runtime state
                    -> initializeNativeAotModules()
                         -> validate actual module/linker bookends
                         -> RhRegisterOSModule()
                         -> InitializeModules()
                              -> metadata/TypeManager, static bases,
                                 and eager module initialization
                    -> managed entry
```

Module registration is not part of `RhInitialize` in this build. The direct-ELF harness stops after `RhInitialize`; the separate `RhpReversePInvoke` path in `guidexos_nativeaot_platform.cpp:32673-32807,34933-34969` contains module registration and initialization. The candidate does not fabricate a module descriptor or call `InitializeModules` early.

## Explicit return and failure map

| Boundary | Source operation and state | Explicit failure / propagation | Diagnostic meaning |
|---|---|---|---|
| PAL initialization | `PalInit` allocates an FLS slot, initializes GC configuration, then initializes the GC-to-OS interface. | FLS exhaustion returns false; `GCToOSInterface::Initialize()` false returns false; `RhInitialize` returns false. `GCConfig::Initialize()` is void. | `PalFls`, `GCConfig`, `GCToOS`, and `PalInit` markers identify completed returns; a begin without completion remains incomplete. |
| Optional Windows lookup | `GetModuleHandleW`/`GetProcAddress` are redirected to fixed guideXOS import shims; they return null. The `RtlDllShutdownInProgress` function is optional. | Absence is not an explicit startup failure; the result is observed, not treated as a failing return. `atexit`'s return is ignored. | `startupImportLookup` is an observation; it cannot become the earliest explicit failure. |
| Module identity | `PalGetModuleHandleFromPointer(&RhInitialize)` is passed to `InitDLL`. | Pinned `RhInitialize` does not reject a null result at this point. | `moduleIdentity` is an observation only. It no longer emits a fabricated failure event for null. |
| `InitDLL` | Optional interface-dispatch setup; GC event lock and optional tracing setup; restricted callouts; RuntimeInstance; then GC. | Explicit false returns from `InitializeInterfaceDispatch`, `RestrictedCallouts::Initialize`, `RuntimeInstance::Initialize`, and `InitializeGC` propagate as `RhInitialize == false`. Void calls can trap, fail-fast, or fail to return without a false result. | The outer `InitDLL` begin/return brackets the route; selected boolean operations have their own stage. Uninstrumented void operations remain a bounded incomplete subregion. |
| RuntimeInstance | `new (nothrow) RuntimeInstance`, `ThreadStore::Create`, then stores the ThreadStore and RuntimeInstance pointers. | Allocation or ThreadStore creation failure returns false. With `FEATURE_HIJACK`, failed `PalRegisterHijackCallback` makes `ThreadStore::Create` return null. | Separate allocation, ThreadStore-create, publication, and outer return events expose where this explicit false originated. |
| GC publication and initialization | `GCHeapUtilities::InitializeGC()` selects/initializes the linked GC and, on success, publishes `g_pGCHeap` and `g_pGCHandleManager`; then heap initialization, finalization, and handle-table initialization run. | Each HRESULT/bool failure in `GCHelpers.cpp` returns false through `InitializeGC` and `InitDLL`. The HRESULT at GC publication is retained in the trace; later bool boundaries report their explicit false. | Heap/handle pointer publication is separate from heap initialization and handle-manager initialization. A pointer being non-null is not a completion proof. |
| CPU features | `DetectCPUFeatures()` is after GC initialization when compiled. | Its ordinary return is true in the pinned implementation; unsupported required instructions take the `RhFailFast` path rather than a false return. | A begin without return is not labelled a false return. |
| Runtime modules | Later `initializeNativeAotModules()` validates linker ranges, registers the OS module/code manager, then calls `InitializeModules`. | Source-defined failure paths call fail-fast with codes `10`, `0xC011EC17`, or related code-manager status; they do not propagate through `RhInitialize`, which already returned. | Stages 0x61-0x63 apply only if the later reverse-P/Invoke path is actually entered. |

The earliest possible explicit false boundary in `RhInitialize` is `PalInit`. The existing R5 raw serial does not say whether that, a later `InitDLL` boolean boundary, or a non-returning operation caused the observed result. This phase does not attribute the failure to GC initialization. It also does not change runtime ordering to improve visibility.

## Singleton readiness and thread attachment

Every trace record captures only bounded flags. The capture checks published pointer values and calls supported accessors; it does not read object fields beyond `RuntimeInstance::GetThreadStore()` after the RuntimeInstance pointer is non-null.

| Bit | Meaning and expected transition | Qualification limit |
|---:|---|---|
| 0 | RuntimeInstance pointer published by `RuntimeInstance::Initialize`. | Expected clear before publication; set after successful publication. |
| 1 | ThreadStore pointer available from the published RuntimeInstance. | Expected clear before publication; set with bit 0 after `ThreadStore::Create` succeeds. |
| 2 | GC heap pointer published by `GCHeapUtilities::InitializeGC`. | Pointer publication does not mean `g_pGCHeap->Initialize()` completed. |
| 3 | GC handle-manager pointer published by `GCHeapUtilities::InitializeGC`. | Pointer publication does not mean `GCHandleManager::Initialize()` completed. |
| 4 | `g_pRhConfig` is non-null. | Pointer comparison only; the pinned startup source points it at the static `RhConfig`. |
| 5 | Internal module-initialization guard is set. | The runtime-pack sets this guard immediately before `InitializeModules`; it is not proof that `InitializeModules` returned. |
| 6 | `RhRegisterOSModule` succeeded and the code-manager registration flag was set. | Only reachable after successful `RhInitialize` and reverse P/Invoke. |
| 7 | `ThreadStore::GetCurrentThreadIfAvailable()` is non-null. | This accessor does not prove the Thread is linked into the ThreadStore or that it is safe for GC suspension. |

`RuntimeInstance::Initialize` creates the ThreadStore but does not call `ThreadStore::AttachCurrentThread`. The R1 kernel therefore suppresses `ManagedMain` even after `RhInitialize` success, with reason `runtime-threadstore-attachment-unqualified`. Bit 7 is evidence about the accessor only; it does not open that gate. No GC collection or ownership proof can run through this candidate.

## Freestanding platform prerequisite audit

| Prerequisite | Implementation / evidence | This R5 target qualification |
|---|---|---|
| Virtual reserve, commit, decommit, release, reset, and page size | `guidexos_nativeaot_gcenv_startup_bridge.cpp` calls the existing kernel GC virtual-memory contract. The candidate traces reserve/commit/decommit/release and adapter page-size checks. | Source-verified only for this candidate; the old R5 guest has no per-callback result. Earlier C011 collection evidence is a different phase/configuration. |
| Page protection | `PalVirtualProtect` is part of the platform contract. The runtime's GS-cookie protection branch is Unix-target-only; this candidate is built as the locked Windows/AMD64 runtime target. | No page-protection operation is established as the source of the R5 return. Unqualified for this target. |
| FLS/TLS | Fixed kernel PAL hooks provide FLS allocation/get/set. The runtime's current-thread accessor reads the current slot. | The prior R5 boot did not export FLS results. The R1 candidate will report FLS allocation and singleton state, but has not run in a guest. |
| Thread identity and stack bounds | The startup imports provide a one-CPU identity (`GetCurrentThreadId` 1, processor 0); kernel stack bounds are available for the current kernel thread. | One-CPU configuration is source-visible. Runtime ThreadStore attachment and suspension safety remain unqualified. |
| Events and synchronization | Fixed event/critical-section adapters are present; GC event creation and VM adapter initialization have bounded diagnostic stages. | Implemented in source, but no per-operation result was captured by the old R5 guest. |
| Module discovery and registration | Direct-ELF `PalGetModuleHandleFromPointer` and actual linker module bookends are used; `RhRegisterOSModule`/`InitializeModules` are later stages. | The previous guest failed before `ManagedMain`; module registration was not reached. |
| GC heap and handles | Pinned Workstation GC code and the platform contract are linked; publication and initialization return boundaries are instrumented. | Unqualified in the old R5 guest; no claim of guest GC readiness is made. |
| Exceptions and managed threads | Candidate manifest selects `managedExceptions=false` and `managedThreads=false`; platform import shims are fixed contracts, not a Windows loader or general compatibility layer. | No managed exception or worker-thread path is qualified by this phase. |

The source includes a narrow `GetModuleHandleW`/`GetProcAddress` replacement for the optional Rtl shutdown query. These replacements return null; the guest does not depend on the Windows PE loader. That optional lookup is observable and non-fatal.

## Hosted versus freestanding startup

The R4R5 hosted proof used the normal hosted managed execution path and established the ownership graph under that host's services. The failing target is a NativeAOT 9.0.0 AMD64 PE linked by ILCompiler, converted to a direct ELF entry, and run with the guideXOS PAL/GC startup contracts. Its adapter directly calls `RhInitialize(false)` rather than the Windows bootstrapper's process entry. It supplies a one-CPU identity and fixed PAL imports, has no Windows PE loader, and does not run `InitializeModules` before `RhInitialize`.

The R1 diagnostic build compiles the transformed startup, GCHelpers, and RuntimeInstance sources using the pinned NativeAOT Windows/AMD64 Release feature set, including the pinned EventPipe/event-trace feature switches, then builds a separate proof-disabled production image. The source subset omits generated `runtime_version.h`; the isolated diagnostic output recreates that header from the locked ILCompiler package version `9.0.0`. Its SHA-256 is `9D6EEB3DFD55ADB0F0B41E2F8301E06EDDFCF281CB2E4D3F0118D5A495774DCF`. This is build metadata only; it does not add runtime behavior or broaden platform services.

The direct ELF route and PAL boundary are the relevant differences. A hosted success cannot prove that the guest's FLS, VM, event, startup import, or thread-registration contracts behave the same way. No feature switches were broadly changed to make startup succeed.

## Diagnostic marker and runner changes

Marker family: `C188R12R4R5R1_INIT`. The NativeAOT side stores a fixed 64-record ring; the early kernel serial sink emits at most 96 trace-record lines and counts drops. Each fixed-format record contains invocation, stage, event, code, and singleton bits. The post-return `trace_end` contains record count, ring overflow, serial drops, and exact boolean `RhInitialize` result. No managed allocation, GC call, dynamic logging buffer, runtime pointer address, or Windows loader call is used by the trace path.

Events are `begin=1`, `pass=2`, `fail=3`, and `observe=4`. Stage IDs are:

| IDs | Stages |
|---|---|
| `01-02` | adapter, RhInitialize |
| `10-13` | PalInit, PAL FLS, GC config, GC-to-OS |
| `20-2A` | InitDLL, interface dispatch, GC event lock, restricted callouts, RuntimeInstance, module-identity observation, startup-import observation, debug headers, RuntimeInstance allocation, ThreadStore create, RuntimeInstance publication |
| `30-34` | GC initialization, heap/handle pointer publication, heap initialization, finalization, handle-manager initialization |
| `40`, `50-55` | CPU features, GC event create, VM reserve/commit/decommit/release, VM adapter init |
| `60-63` | later reverse P/Invoke, module registration, code-manager registration, module initialization |

The analyzer reads raw bytes, hashes the raw file, and writes a separate JSON artifact. It flags missing terminal newlines, count mismatches, truncated traces, ring overflow, and serial drops. A begin with no completion is `begin_without_completion`, not an explicit failure. `trace_end` absence leaves the trace incomplete. The current R5 raw log is never rewritten.

The R5 runner retains the corrected QMP writer using `UTF8Encoding(false)`, validates the isolated candidate manifest and media hashes, requires an empty QAC preflight census, writes a phase-wide one-guest attempt marker before starting QAC, and separately records QAC release and trace/application outcomes. For R1 it analyzes raw serial into `serial-analysis.json`, compares the raw serial hash before and after analysis, and considers a failure capture successful only as a diagnostic capture (`INIT_FAILURE_CAPTURED`), never as GC proof success.

Hosted regressions cover complete ordered startup, PAL/FLS failure before GC, an explicit GC HRESULT failure, missing ThreadStore readiness, later module-registration fail-fast, incomplete traces, failure gating, successful-dispatch parsing, raw serial preservation, and BOM-free QMP framing. These use synthetic serial traces and do not prove that any stage worked in a guest. The existing QAC ownership and stream-capture predicate suites also passed without launching a guest.

## Hosted qualification results

| Qualification | Result |
|---|---|
| R1 synthetic trace/parser suite (10 scenarios) | PASS; newest output `out/dotnet/c188r12r4r5r1-runtime-init-attribution/hosted-20261031T055248793Z/` |
| Existing R4R5 hosted ownership graph | PASS; 3 callbacks, 17 observations, 2 collections |
| C188R12R4 factory predicates | PASS, 9 cases |
| C188R12R4R1 link-boundary predicates | PASS, 9 cases |
| C188R11 runtime lifecycle | PASS, 7 cases |
| C188R11R4 QAC ownership predicates | PASS, 15 cases |
| C188R11R4 QAC stream capture | PASS, 2 cases |
| C188R11R2 runner predicates | PASS, 24 cases |
| PowerShell parsing for changed build, analyzer, test, and runner scripts | PASS |
| `git diff --check` before closeout documentation | PASS |

The existing 40-suite/969-case managed UI baseline remains valid and was not rerun. This phase changed no UI, Settings, Notes, or application source. The retained R4R5 hosted proof was rerun only as a regression and was not used to diagnose the guest startup failure.

## R4R5R3 return-contract correction

The R4R5R3 audit compared the linked R2 image with the exact pinned runtime and adapter. `RhInitialize` is `extern "C" bool RhInitialize(bool isDll)`: native `true` means initialization succeeded and native `false` means it failed. The C188 adapter maps that Boolean to its own integer status with `true -> 0` and `false -> 1`. Therefore the historical R2 `RhInitialize return=00000001`, `trace_end ... rhResult=00000001`, and `RhInitialize: FAIL` are adapter failure status, not raw Boolean `1`; the status correctly indicates native false. The R2 serial itself did not capture the raw Boolean, so this mapping is established by the linked map/object and adapter code, not by rewriting or reinterpreting bytes in the raw log.

The earlier trace description above that calls the footer an “exact boolean `RhInitialize` result” is imprecise. The footer stores the adapter status. R1 stage event pass/fail is derived from the native Boolean, but its numeric result field is also the adapter status. R4R5R3 now emits distinct `nativeBool` and `adapterStatus` values and checks their mapping in the analyzer. The R2 raw serial remains unchanged; see the [R4R5R3 return-contract closeout](NATIVEAOT_C188R12R4R5R3_INIT_RETURN_CONTRACT.md).

## Diagnostic candidate and production separation

Candidate root: `out/dotnet/c188r12r4r5r1-runtime-init-attribution/candidate-20261031T055308282Z/`.

| Artifact / provenance | SHA-256 or value |
|---|---|
| Candidate manifest | `CAE5B5F308784ED8DB439754B7A69D139E180AA01F18AB3A8A3D3460CBC92B6A` |
| Runtime-pack object | `71A50BCA2BA654E9E6C5B634F28799DE4DB90379935532E6F893700F8129E763` |
| NativeAOT PE | `0AD3831D96B3FEFD7CC794BF44EB95819DA799DC71B7E6A44766C1AA7179E6A8` |
| Diagnostic NativeAOT ELF | `4D0A3993C5EC8291275514AE8FBF86A75FD4B88B7461CD61AE014C34E39401AA` |
| Diagnostic kernel and staged ESP kernel | `4D64C305A9E8D415849F49442D17655482FA782EA8AEA22E43B82AF398BA64B1` |
| Staged ESP ramdisk | `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8` |
| Staged bootloader | `D72A35DA882892BA97356A1C006562ABA2908D9DD909152B96DCBB655D6C2D9B` |
| Proof-disabled production NativeAOT ELF | `FC9420A82064870625A4F01E907A1210A84DFFD0503504DD077F422DCC00A10E` |
| Proof-disabled production kernel | `A21A951C8DF337CEA08A7FE93580C556F0AC87F33D1B1766070FFF204B1EA168` |
| Candidate manifest result | `PASS_BUILD_READY_FOR_INIT_DIAGNOSTIC_QAC_PREFLIGHT` |
| Candidate build source HEAD | `0387695f7ccf9e3bcd78d4efaaecfca19bb09012` |

The runtime-pack and candidate manifests confirm `runtimeInitDiagnostics=true`, `imageBackedArenaPresent=false`, and `managedHeapBytes=0` for the diagnostic profile. The production manifests report `persistentCompositeLifecycle=true`; all four production-isolation checks passed, including absence of diagnostic instrumentation and the R5 proof profile. The staged ESP contains the bootloader, kernel, and a copy of the unchanged source ramdisk. The candidate records `qemuLaunched=false`, `qacReserved=false`, zero managed graph callbacks, and a fail-closed ManagedMain gate.

## Remaining uncertainty and next action

Outcome A is appropriate: the instrumentation, analyzer, runner correction, focused hosted gates, diagnostic/production builds, and isolated candidate are ready. Outcome B is not established. There is no new guest trace, no attributed `RhInitialize` false return, and no guest acceptance of GC ownership.

The next action is one explicitly authorized C188R12R4R5R2 QAC preflight and at most one boot using this candidate root. The runner must retain its empty/exclusive QAC census, one-guest attempt guard, media hashes, raw-serial preservation, and release verification. If `RhInitialize` fails, use the earliest explicit fail event and singleton bits; treat a begin without completion as an incomplete/non-returning boundary. If it succeeds, this candidate still suppresses `ManagedMain`; establish ThreadStore attachment/suspension safety through a supported, read-only contract before any later isolated GC ownership proof. Do not infer GC guest success from the hosted graph or from pointer publication alone.
