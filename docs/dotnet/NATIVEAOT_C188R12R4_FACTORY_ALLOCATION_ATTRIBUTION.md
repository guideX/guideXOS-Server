# NativeAOT C188R12R4 — Settings Factory and Allocation Attribution

**Outcome: `FACTORY_ATTRIBUTION_PARTIAL`**

This was a hosted-only investigation. No QEMU boot or QAC admission was requested or performed. The Settings factory path and bounded allocation diagnostics are now instrumented and hosted-tested, but the diagnostic NativeAOT executable did not link, so there is no R4 guest candidate to stage. The NativeAOT guest root cause remains unknown.

## Baseline and integrity

- Branch: `v1.1_DOTNET_SUPPORT`
- Starting HEAD: `ed4d3c2f4f85586fa19add438bfacf480dc7b882`
- Ending implementation HEAD: `ced2ac48` (`diag: add C188R12R4 factory allocation attribution`). This report is committed in a documentation-only commit on top of that implementation commit.
- `ESP/ramdisk.img` SHA-256 before and after: `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8`. The pre-existing modified image was preserved and was not staged.
- Existing phase records reviewed: [C188R12R3 lifetime start attribution](NATIVEAOT_C188R12R3_LIFETIME_START_ATTRIBUTION.md) and [C188R12R3R1 guest attribution](NATIVEAOT_C188R12R3R1_LIFETIME_GUEST_ATTRIBUTION.md).

The retained guest trace established that selector 5 resolves, the resident/capacity/generation checks pass, and `factory-enter` is emitted. No factory return or throw, resident registration, Settings launch, or Settings surface followed. At the last pre-factory sample, the 64 KiB persistent arena had 5,624 bytes available. The earlier hosted Settings constructor measurement was about 18,000 bytes under hosted .NET; it is not a NativeAOT arena measurement. The guest trace contains no allocation request or failure record. These facts do not prove exhaustion, a deadlock, or any other specific cause.

## Factory and construction path

The normal Managed Notes and Managed Settings descriptors use static factories and share the same synchronous lifetime manager and resident pool. In `TryStartCore`, the lifetime code finishes its resident, capacity, generation, and descriptor checks; it emits the existing `factory-enter` marker immediately before `application = factory()`. Managed exceptions are caught for attribution and rethrown. Native fail-fast does not return through this catch.

For the diagnostic build only, the selector-5 descriptor routes through `C188R12R4FactoryDiagnostics.CreateSettingsCenter()`. The path is:

1. Start native attribution for the Settings invocation, then emit `factory-invoke`.
2. Emit `type-init-enter`; access the Settings type's static fields; emit `type-init-return` if those accesses return.
3. Emit `object-allocation-request`; evaluate `new ManagedSettingsCenter()`.
4. NativeAOT allocates the Settings object and runs its instance field initializers. Those initializers create the initial control graph, groups, stacks, dialogs, and other Settings-owned objects before the first constructor-body marker.
5. The base constructor marker and then `constructor-enter` run. The constructor emits boundaries around `_sectionLeaves` model-array creation, control setup, dialog setup, and constructor return.
6. The factory emits its construction result and factory return. The `finally` boundary prints native allocation counters when the factory returns or throws. The existing lifetime code then continues to registration, owner assignment, and lifetime return.
7. `ManagedSettingsCenter.Launch` remains downstream of a successful lifetime start; it is not part of factory construction.

The base-constructor marker is **not** a pre-field-initializer marker. The NativeAOT module initialization path calls `InitializeModules`, which owns eager constructor execution. Therefore the `type-init-enter`/`type-init-return` pair brackets the first Settings static-field access at the factory boundary; it cannot establish that the type initializer first ran there. It also cannot observe an initializer that NativeAOT already ran during module initialization.

Settings' construction differs from Notes by creating its own static `SectionTops` `int[4]`, four group objects and a four-entry group array, four stack objects and a four-entry stack array, a larger set of controls and dialogs, and the `_sectionLeaves` graph (an outer four-entry array plus inner arrays of 3, 6, 6, and 3 references). This is five arrays and 22 reference slots for `_sectionLeaves`, in addition to arrays owned by its controls. `ResizeBounds` is a value-type static field. Notes is already resident when selector 5 is dispatched, so its prior allocation remains part of the arena cursor and is not charged to the Settings invocation.

No lock, wait, file I/O, or service initialization appears in the Settings constructor body or the factory wrapper. Shared runtime Settings initialization occurs before `TryStart`. The constructor contains bounded setup loops, but no unbounded loop or recursive factory call was found. This source audit does not rule out runtime type-initialization synchronization, stack exhaustion, or an uninstrumented runtime termination.

## NativeAOT allocation behavior and budget

The diagnostic runtime pack uses a 64 KiB, 16-byte-aligned persistent arena and the current thread's allocation cursor/limit. It does not increase the arena or replace the allocator.

- Array size is derived from EEType `BaseSize` and `ComponentSize`, with overflow checks and 8-byte rounding: `(BaseSize + ComponentSize * length + 7) & ~7`. The bounded `RhpNewArray` path checks the cursor and remaining capacity before calling the stock array allocator. Invalid context or insufficient capacity records the bounded failure and calls `guideXosFailFast(6)`; it does not return null for the managed caller to catch.
- The NativeAOT `RhpNewFast` object entry uses the EEType `BaseSize` for its fast-path request. On a fast-capacity miss, the stock path enters `RhpGcAlloc`; that path can return an object in or outside the persistent arena, or can terminate without returning. The R4 wrapper reports the request size, cursor and availability before this fallback and reports the result if the fallback returns. This source trace does not show that the Settings factory takes that slow path.
- No allocation retry loop was found in the bounded array wrapper. The stock object fallback's exact behavior for the Settings guest execution was not observed.
- Allocation telemetry writes directly to the polling UART and does not allocate managed memory. UART writes can block and a serial capture is not guaranteed. Failure telemetry is issued before the existing array fail-fast; the fatal path is not suppressed.

The 5,624-byte pre-factory headroom is a measured guest value. The listed Settings arrays and control objects are source-visible, but their NativeAOT sizes, EEType metadata, alignment costs, and cumulative constructor allocation are not measured. Runtime roots and the resident Notes object graph are already reflected in the pre-factory cursor. No single source-known Settings allocation is proven to exceed 5,624 bytes, and there is no supported hosted harness for the exact NativeAOT TLS cursor/allocator at that budget. The 18,000-byte hosted measurement is not used to infer OOM.

## R4 diagnostics

Managed marker family: `C188R12R4_FACTORY`. Markers include invocation id, selector, operation, stage, result code, and result where available. The live marker formatter uses stack storage and `GuideXosText`; test-only capture arrays and `ToArray()` are excluded from the NativeAOT build.

Native allocation family: `C188R12R4_ALLOC`. It is scoped to selector 5, records the initial cursor and operation id, reports array capacity failures before fail-fast, and instruments only object fast-capacity misses (including fallback entry and a result if one returns). It counts object and array requests/returns and prints cursor movement, remaining capacity, and result at factory return. It does not print every successful allocation.

The hosted suite verifies Settings factory construction, marker ordering and values, lifetime registration, Notes residency, Notes-to-Settings transition, and explicit managed factory failure. The synthetic serial fixtures cover factory entry without constructor entry, constructor without return, allocation request without result, explicit allocation fail-fast, constructor success without factory return, factory return without registration return, type-initialization without return, and complete startup. These fixture results are parser tests, not guest evidence.

Not covered: allocation success near NativeAOT capacity, NativeAOT allocator exhaustion, or allocator-failure emission through the actual guest allocator. No supported hosted harness exercised that allocator contract; the synthetic explicit-failure line only checks evaluator classification.

## Qualification and build results

| Qualification | Result |
|---|---|
| Focused R4 hosted factory suite | PASS, 14 cases |
| Synthetic R4 attribution predicates | PASS, 9 cases; fixtures only |
| Main managed UI gate | PASS, 40 suites / 969 cases |
| Prior hosted regressions | PASS: C182 15, C185 28, C186 15, C187 22 cases |
| C188 identity/lifetime host | PASS: 25 focused cases; stress 1,000 operations, 500 valid resizes, 250 invalid geometry requests, 250 stale-generation attempts, 100 launch/close lifecycles |
| R11 provenance/lifecycle | PASS: R9 14, R10 14, R11 15, R12R2 8, runner predicates 30, runtime lifecycle 7 |
| Existing R12R3 runner predicates | PASS, 13 cases |
| Diagnostic kernel | PASS; ELF SHA-256 `568B2C084387E45FE3718D422570EE761798542E461674AE8CE14A6B7804E864` |
| Production kernel | PASS; development descriptor absent; ELF SHA-256 `A21A951C8DF337CEA08A7FE93580C556F0AC87F33D1B1766070FFF204B1EA168` |
| Diagnostic runtime-pack compilation | PASS; `c188r12r4FactoryAttribution=true`, `persistentCompositeLifecycle=true` |
| Proof-disabled production NativeAOT | PASS; `persistentCompositeLifecycle=true`, R4 runtime instrumentation excluded; ELF SHA-256 `DA910DA5CE61E73A7994E90BD61B2558E2C1CCF0C5C8E04F98BC0B8EB8B91A50` |
| Diagnostic NativeAOT candidate | FAIL at link; no final ELF and no staged candidate |

The focused host run recorded one existing `CS0162` unreachable-code warning at `Program.cs:2712`. The runtime-pack build reported two C++ `C4005` macro-redefinition warnings (`ARRAY_SIZE` and `STRING_LENGTH`). No hosted test failure was reported. PowerShell parsing passed for 13 changed build/test scripts, and `git diff --check` passed.

The diagnostic link log reports unresolved `FreeLibrary`, `GetProcAddress`, `LoadLibraryExW`, and `SetThreadErrorMode` references from the generated `HostLogProof.obj`. The link error was not hidden with stubs or a runtime resolver change. The failed build output is preserved at `out/dotnet/c188r12r4-factory-attribution/nativeaot-candidate-003/`.

Artifact provenance and hashes (all outputs remain under `out/dotnet/c188r12r4-factory-attribution/`):

| Artifact | SHA-256 | Qualification |
|---|---|---|
| Direct diagnostic runtime-pack object `runtime-pack-direct-002/guidexos_nativeaot_platform.obj` | `301AFE8CFDC9D8399828D39CF84603B5DC0EA557B9D9FE891455FEDBD59C48E3` | Compiled; manifest identity `...c188r12r4-factory-attribution-v1`; R4 attribution and persistent lifecycle enabled |
| Direct runtime-pack archive `runtime-pack-direct-002/sdk/Runtime.WorkstationGC.lib` | `DA7C6CB12378BAA18185E0FF71B560A88ACD813C46159CC4046821F33D2814B0` | Archive membership PASS |
| Candidate NativeAOT runtime-pack object `nativeaot-candidate-003/runtime-pack/guidexos_nativeaot_platform.obj` | `97A7AE4DAEB78ADDE40D4321FD2461AA45ECEF94F2C237B6EB2B5743D22F0731` | Runtime-pack manifest PASS; persistent lifecycle enabled |
| Candidate runtime-pack archive `nativeaot-candidate-003/runtime-pack/sdk/Runtime.WorkstationGC.lib` | `EF462F635C79DD703BE69EB25B0D55D41B4EEBB992DA6225C24A8D14766DAFB2` | NativeAOT application link then failed |
| Candidate runtime-pack manifest | `3F7996A04FE4F90003717FC94C6885CDD238B084CAB2EBCB63F4B2A0B2C52D8E` | Captures source SHA `61A7308A68A007060F177791314DA46D00C959D0F4A51911FB304A355A0C207F` |
| Failed candidate generated `HostLogProof.obj` | `F753A21412F92EFED721967E72703C731A1B17FB098339048905C6C219AC2BE8` | Preserved for link diagnostics |
| Failed candidate NativeAOT build log | `CC1022532FE36E9A0F7449490FBAC606B3FAC6958E255F837475D539F1D310B5` | Preserved; reports publish/link failure |
| Production NativeAOT runtime-pack object | `EC606F5FCF320466761BFAA0F94345EB94A978AEC717563DC85DA0145E6BC6B5` | Standard persistent-composite identity; R4 disabled |
| Production NativeAOT runtime-pack manifest | `B648BCD3FB35BC22A4FE3C22EEDE393DF493755D63DDF2FA4CBB137254E06067` | Persistent lifecycle true |
| Production NativeAOT ELF | `DA910DA5CE61E73A7994E90BD61B2558E2C1CCF0C5C8E04F98BC0B8EB8B91A50` | Production selectors 4–8; development CounterApp and C188 route absent |

The build manifests were generated from the unchanged starting HEAD plus the modified worktree source. They record `ed4d3c2f4f85586fa19add438bfacf480dc7b882`; the source changes and diagnostics are now committed at `ced2ac48`. The production build uses its proof-disabled configuration and retains its prior runtime identity.

## Attribution confidence and next action

High confidence: the last guest evidence localizes the missing completion to the synchronous selector-5 factory call, and the current source has allocation, type-access, constructor, dependency, and return boundaries to narrow that interval in a future trace. Source inspection confirms a bounded array fail-fast can occur before managed exception handling, while the object slow path has a distinct fallback contract.

Low confidence: the mechanism that prevented the previous guest factory from returning. No NativeAOT request/failure, constructor marker, stack, synchronization, or runtime-termination evidence was captured. OOM and deadlock remain unproven. No correction is proposed.

The smallest next action is to resolve the four NativeAOT link imports without adding unsupported loader behavior, rebuild and stage a fresh diagnostic candidate, then request one exclusive QAC-owned boot using the established Notes-to-Settings route. That future run should capture selector-5 dispatch, R4 factory and allocator markers, the last completed operation, registration/lifetime return, and Settings launch/surface evidence if reached; then release the guest and preserve raw serial/screenshots. No guest run is part of this phase.
