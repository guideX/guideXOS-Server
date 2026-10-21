# guideXOS Server .NET Support — C186 Managed Host Contract Lifecycle

## Outcome

**Outcome B — Partial.** C186 defines and exercises the lifecycle, identity, surface, callback, and resize semantics of the C185 sample boundary. The 38-suite/930-case aggregate, C182 starter, C185 host tests, independent CounterApp build, isolated NativeAOT link, and ELF conversion passed. No production application composition, kernel, NativeAOT ABI, or release package source changed, so no guest run was required. The preservation gate is not fully certifiable: an initial NativeAOT invocation wrote HostLogProof output into the existing C185 output tree before the root output override was discovered, and no pre-run file hash was recorded for those generated outputs. The final build and retained evidence are isolated under the C186 output root. This does not qualify external application installation or dynamic loading.

## Starting architecture and audit findings

The accepted C185 shape is an independent `GuideXos.ManagedUi.Contracts` project, an independent `GuideXos.C185.CounterApp` project, and private adapters and static composition in `HostLogProof`. The C185 composition has one statically constructed factory for the CounterApp sample. The private bridge calls the existing C174/C180 resize path and uses the existing C181 fixed lifetime pool, which remains bounded at five application selectors. C186 does not add a loader, registry, scheduler, thread model, or resident slot.

The audit found that C185's public methods had underspecified instance/generation meaning and callback lifetime. The bridge also remained callback-active after a successful direct window close, retained its private native surface wrapper, did not roll back resize geometry if an application callback threw, and allowed teardown exceptions to skip adapter release. The aggregate wrapper still expected the earlier C180 inventory of 36 suites / 899 cases. C186 addresses these gaps in the private C185 path and makes the accepted C181 inventory authoritative.

The bridge lifecycle is `Created → Initializing → Active → Closing → Closed`, with `Failed` as the terminal state for initialization failure. The bridge delivers rendering, input, actions, and resize callbacks only while `Active` and while its window remains open. Initialization exceptions and non-success results run teardown and release the adapters. Teardown is idempotent and uses `finally` to invalidate wrappers even if application cleanup throws. A successful direct close transitions to `Closing`; repeated close succeeds without a second native close, and later callbacks are rejected. A failed direct close keeps the live wrapper and active state available for retry.

## Ownership matrix

| Resource | Creator and owner | Access and validity | Release, failure, and stale-reference handling |
|---|---|---|---|
| Managed application instance | The static factory creates it; the C181 lifetime entry and C185 bridge retain it. | The matching selector/type and generation may dispatch to it. Ordinary callbacks begin only after successful initialization and stop when closing begins. | `OnTearingDown` is called during failed initialization or lifetime clear. Exceptions are caught; service release still runs. C181 only clears the matching generation. |
| NativeAOT host services facade | C185 bridge creates one private service adapter per running generation. The native host owns the callback table and native context. | The external app can call the narrow public service interface synchronously while active. It receives no raw pointer or callback-table address. | Bridge teardown releases the adapter. Released adapters reject window creation. Callback methods verify the active bridge/window state. Native callback errors map to the existing host result set. |
| Application type ID | Statically registered factory owns the stable ID; for CounterApp it is `0xC1850001`. | Identifies the registered application type, not a running instance. | Invalid or unregistered IDs fail through the dispatcher. No native window identifier is used as an application type ID. |
| Application instance identity | C181 lifetime owns the `(type ID, generation)` association. The host launch context supplies a generation when present; zero-generation hosted tests use C181's monotonic allocator. | Stable for that running lifetime. Input/action lookup must match both selector/type and generation. | Clear requires the same generation. Slot reuse receives a later generated value in the C181 tests. Stale generations fail dispatch. C185's one registered type has one current lifetime entry; the five entries remain shared capacity for five selectors, not five instances of the same selector. |
| Window identity | C185 window adapter creates `(type ID, generation, ordinal)`; C185 supports ordinal 1. | Visible to the app as a logical window identity. It contains no native handle and remains stable through accepted/rejected resize. | Window close or bridge teardown invalidates it and all child wrappers. A close failure leaves the window usable for retry until teardown starts. |
| Native window and drawing surface | `GuideXosHost` requests it through the existing native callback. The native compositor/host owns allocation and release; C185 keeps only a private opaque `GuideXosSurface` wrapper. | Public drawing is available only while the app is active and the window is open. Operations are synchronous and host-validated against the current invocation context, selector, generation, and native window ownership. | Successful close clears the private native wrapper. Cached public surface wrappers return `InvalidState` afterward. Native callback failure is returned as a managed result; stale or foreign handles are rejected by host ownership validation. No framebuffer copy is introduced. |
| Input callbacks | The host/compositor routes input; C185 converts each event to a copied `ManagedUiInputEvent`. There is no persistent public subscription object. | The app receives the event only for its active generation. Pointer and keyboard routing use the current native focus/routing path. | Close/teardown disables dispatch; stale generation dispatch is rejected. C185's controls route the sample's primary pointer and keyboard actions. |
| Resize callbacks and registrations | No separate registration is created. The host queries `TryGetResizeBounds` for the active C180 participant and uses C174/C180 transaction state. | Returning `false` or invalid bounds denies resizing. `HandleResize` receives one candidate and may stage application layout; the host commits only accepted layout/render/state. | Rejected geometry restores prior dimensions and invokes `CompleteResize(false)`. Application exceptions roll geometry back and return failure. Closing state fails the active callback guard, so no resize reaches a closed generation. |
| Control instances | C185 window adapter creates and owns the sample label/button objects and their adapters. C185 currently permits one label and one button per window. | The app receives narrow interfaces, not native widget IDs. Control wrappers are usable only while their window is open. | Window close resets the control host; cached control calls return `InvalidState`. Extra controls return `ResourceLimit`. |
| Application-owned state | The app instance owns its counter and layout state. | The app may read/write it during its own callbacks. Host services do not persist it. | The sample resets state in teardown. On a rejected resize the app restores its prior layout from `CompleteResize(false)`; C186 verifies no cumulative geometry drift. |
| Host-owned resources | Native windows/surfaces and host call table are owned by the compositor/runtime; fixed-capacity callback and control state is owned by the adapter. | Accessible only through the public managed interfaces and a valid active native callback. | Adapter teardown attempts close and invalidates wrappers in `finally`. Persistent native close failure during teardown cannot be repaired through an already-invalidated public wrapper; ordinary direct close failures remain retryable. This is a host failure limitation, not ownership transfer to the app. |
| Private native identifiers | Native code allocates window IDs and surface generations; they are not exposed by `ManagedUi.Contracts`. | Only private adapters and native ownership checks may use them. | Window ownership, active selector, and generation checks reject stale/foreign operations. Public callers cannot retain or forge these identifiers through the contract. |

## Identity, lifecycle, and callback rules

`ManagedUiApplicationIdentity.ApplicationId` (also exposed as `ApplicationTypeId`) names the registered type. `Generation` distinguishes a running lifetime of that type. The pair is the managed instance identity; the public API intentionally adds no global instance registry or native ID. `ManagedUiWindowIdentity` adds a per-generation ordinal. C185 creates one window, ordinal 1.

C181 continues to hold five bounded entries. For nonzero host generations, C185 stores and dispatches the supplied generation; the native production path encodes its application generation in the launch context. C181 tests also cover zero-generation launches, where it assigns monotonically increasing values and refuses overflow. `TryGet` and `Clear` compare selector and generation. Starting the same selector replaces its current lifetime after teardown, so the current C185 sample does not promise simultaneous instances of one registered type. C186's two-identity isolation check uses two real C185 bridge/service/window adapters over distinct fake host contexts to verify adapter ownership; it does not expand the runtime's registered-type policy.

Initialization runs once per bridge. Before it succeeds, no ordinary input, action, render, or resize reaches the application. A returned failure or thrown exception is reported as an application failure, partial resources are cleaned up, and the bridge enters `Failed`. After teardown begins, no ordinary callback is valid. A failed direct window close leaves the window active and allows retry; successful close is idempotent and invalidates the cached window, surface, and control views. Initialization and teardown callback exceptions are contained so host wrapper cleanup still executes.

The contract returns `ManagedUiResult.ApplicationFailure` for application callback failures. The existing C185 adapter maps this to the native ABI's `InvalidArgument` because the existing host ABI has no distinct application-exception code. `Unsupported` maps to the existing `InvalidAction`; the contract-level result remains precise for managed callers.

## Window, surface, and resize behavior

The host allocates and releases the native surface. Applications receive checked interfaces only: no unmanaged pointers, raw framebuffer, compositor data structure, or native window ID crosses the public contract. Surface drawing and redraw requests are synchronous. A retained surface view remains a managed object but every operation is guarded by its owning window's open state; after close it reports `InvalidState` and cannot reuse the native handle.

Successful window creation returns one window and surface. Failed creation returns no window and leaves no adapter ownership. Application failure after partial creation triggers teardown and host release. Closing invalidates views only after native close succeeds; a failed close leaves the view usable for retry. At final teardown, host cleanup invalidates public wrappers even if a native close attempt fails, as recorded in the ownership matrix.

Resizing remains on the shared C174/C180 path. C185 requires explicit valid bounds, preserves the prior geometry for rollback, supplies both outer and client dimensions, and retains the same logical window identity. The application stages its own layout; the host commits only when layout, render, and application-state checks pass. Rejected geometry rolls back. The C186 stress run measured zero resize allocations and zero geometry drift.

## Focus and activation

Programmatic activation is unsupported. `IManagedUiWindow.RequestActivation()` returns `Unsupported` while the window is open and does not mutate fake managed focus state. `RequestRedraw()` only requests a redraw.

Existing native behavior remains the owner of focus:

- Registering/presenting a native window focuses it; the native open path also explicitly sets focus after registration.
- Primary pointer interaction can focus the hit window; the existing keyboard path routes to the focused native compositor window.
- Closing a focused native window clears focus and selects another live window if one remains. Clicking the shell can explicitly clear compositor-window focus.
- C186 changes none of those native paths. Host tests exercise managed pointer/keyboard routing and classify app-requested activation as unsupported; actual guest focus transitions were not re-run because no native or production source changed.

## Aggregate regression correction

`run-managed-ui-regressions.ps1` previously accepted only the stale C180 expectation of 36 suites / 899 cases, while C181's accepted inventory contains 38 suites / 930 cases. C186 adds `managed-ui-regression-inventory.json` as the canonical inventory and `ManagedUiRegressionInventory.psm1` to validate the inventory ID and SHA-256, exact suite names, duplicates, per-suite counts, PASS status, and total summary. The runner now fails on missing/extra suites, case-count changes, failures, and summary mismatches. It does not suppress component exit failures.

The validator accepted the correct 38/930 manifest and rejected all six deliberately incorrect inputs: wrong inventory ID, wrong inventory hash, missing suite, wrong case count, failed suite, and wrong summary count. The repaired aggregate runner exited 0 with the C135 host compilation, all managed UI suites, and C167/C168 Settings checks passing. The manifest itself records `status=PASS`, the accepted inventory ID, and 38 suites / 930 cases.

## Hosted qualification

The isolated C185 host runner reports **28 C185 cases PASS** and **15 C186 cases PASS** (9 focused checks plus 6 stress checks). C186 checks initialization failure and exception cleanup, failed window creation, identity/ownership isolation, direct close invalidation, retryable close failure, resize exception rollback, slot release/reuse, stale callbacks, and unsupported activation.

| Qualification | Result |
|---|---|
| C185 focused host suite | 28 cases PASS |
| C185 stress | 1,000 operations; 500 resize transactions; 250 rejected requests; 250 failure/recovery operations; 100 lifecycles; 0 service allocations; 0 resize allocations |
| C186 focused host checks | 9 PASS |
| C186 stress checks | 6 PASS: 1,000 service operations; 500 valid resizes; 250 rejected geometries; 250 failure/recovery cycles; 250 stale callbacks; 100 lifecycles |
| C186 resize invariants | 0 hot-path allocation bytes; 0 cumulative geometry drift |
| C182 starter host | 15 checks PASS; 100 resize transactions; 0 resize allocations |
| Managed UI aggregate | 38 suites / 930 cases PASS |
| C167/C168 Settings | PASS in the aggregate runner |
| QEMU / guest | Not run; changes are isolated to contracts, managed adapters/tests, and scripts/docs. No production, kernel, compositor, or ABI source changed. No guest success is claimed. |

The final captured C185/C186 host qualification log is `out/dotnet/c186-managed-host-contract/host-qualification/host-qualification.log`. The C182 run used `out/dotnet/c186-managed-host-contract/c182-artifacts/c182-host-qualification.log`. The aggregate manifest and component logs are under `out/dotnet/c186-managed-host-contract/aggregate/evidence`.

The first NativeAOT invocation unexpectedly routed HostLogProof intermediates to `out/dotnet/c185-managed-host-services/host-bin/release_win-x64` and `host-obj/release_win-x64` because the repository's root `Directory.Build.props` overrides the artifacts path for the C185 boundary. Those generated files were written before the override was discovered; their pre-run hashes were not captured, so their prior bytes cannot be verified from this run. No files were deleted. The NativeAOT build was then rerun with an isolated `DirectoryBuildPropsPath` and explicit C186 output roots; the retained NativeAOT evidence below refers only to that isolated build. The protected sample `bin/obj` paths remained present, and `ESP/ramdisk.img` retained SHA-256 `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8`.

## Independent compilation and NativeAOT retention

`GuideXos.C185.CounterApp.csproj` independently compiles the CounterApp and references only `GuideXos.ManagedUi.Contracts.csproj`. Contracts have no project reference to HostLogProof. HostLogProof references both public contracts and CounterApp only when `HostLogProofC185Boundary=true`; it provides the private adapters and a static factory. CounterApp does not include HostLogProof source, use reflection-based registration, or reference a private host API. The C185 example is not included as a sixth production application.

The independent build passed with zero warnings/errors. The isolated C185 NativeAOT publish passed and generated a PE image and linker map. The map retains `CounterApplicationFactory.Create`, `CounterApplication` constructor, `HandleAction`, `HandleInput`, `HandleResize`, `OnTearingDown`, and `ManagedMain`. The PE-to-ELF converter found `ManagedMain` at `0x10008540`, selected the code prologue at `0x10008600`, and emitted an ELF64 x86-64 executable with seven load/program headers. The image was validated with `readelf`; the map is the retained-code evidence. The executable was not run in a guest.

Existing build warnings were: `CA2014` in the unchanged `ManagedNotes.cs:322`; the existing explicit `Microsoft.DotNet.ILCompiler` package-version warning; linker `LNK4237` for `advapi32.lib` and `kernel32.lib` under the native subsystem; `LNK4281` for the fixed `0x10000000` proof image base; and `LNK4210` CRT section warnings. No C186-specific source warning was reported.

## ABI compatibility and source scope

NativeAOT ABI v4 / 128 bytes, the callback offsets 104, 112, and 120, production selectors 4–8, five-instance resident capacity, C181 generation safeguards, and C174/C180 default-deny resize behavior remain unchanged. C186 changes only managed contracts, the private C185 adapter/test path, the host test runner output, regression inventory tooling, and this document. No kernel/native C/C++, C ABI structure, callback offset, production selector, or filesystem/association source changed. Adding `ApplicationFailure` at the end of the byte-sized managed enum preserves existing enum values.

## Artifact paths and SHA-256

All paths below are relative to the repository root. Hashes were computed from the retained C186 artifacts.

| Artifact | SHA-256 |
|---|---|
| `scripts/dotnet/managed-ui-regression-inventory.json` | `25402CC6D1D92A912C4D3B918001155941DE6E43AA67A132ED0099F7618044D3` |
| `out/dotnet/c186-managed-host-contract/aggregate/evidence/managed-ui-host-manifest.json` | `1FAFB83ABC00573333D2A8E896E3002DDC96B289329F339734A85CE2926D685A` |
| `out/dotnet/c186-managed-host-contract/aggregate/evidence/managed-ui-host-suites.txt` | `E2F0947C031B721BC37D39F52C7B1744A395FBC22E867D249972ECE4C02BC08C` |
| `out/dotnet/c186-managed-host-contract/aggregate/evidence/c135-focused-host-build.txt` | `50961C2527B850850055F9F49E06A5087208300C38ACA75BE63566466023CAAD` |
| `out/dotnet/c186-managed-host-contract/aggregate/evidence/c167-c168-settings-host-suites.txt` | `17CC25F3C80005B5E273385BC4B2887612F5A9214DE98171F476798A81E147F7` |
| `out/dotnet/c186-managed-host-contract/aggregate/evidence/managed-ui-inventory-validation.txt` | `3EE1F3F623EED17B9AD48CDA1AC8FF7A4D6B998F4BC8EBE8DF8FA06AA61F5105` |
| `out/dotnet/c186-managed-host-contract/host-qualification/host-qualification.log` | `E907EEB2A7EC966343DFBC115E473AA8059FAE1AD21E978D1329726177E86143` |
| `out/dotnet/c186-managed-host-contract/c182-artifacts/c182-host-qualification.log` | `993D51862059EAB8516DBE6A84556C4C61192105DBEC2604120A6289F2FFA8F4` |
| `out/dotnet/c186-managed-host-contract/counterapp-independent/independent-build-and-references.log` | `4311F8019D9A53520F97CA40506060303701F985F9C8CE03672DA2713CFBC20C` |
| `out/dotnet/c186-managed-host-contract/counterapp-independent/artifacts/bin/GuideXos.C185.CounterApp/release/GuideXos.C185.CounterApp.dll` | `C361C2382FECCC1AF4BE3C16A14B0005279E3B8A488E5D28E268DC75D6D84C5E` |
| `out/dotnet/c186-managed-host-contract/counterapp-independent/artifacts/bin/GuideXos.ManagedUi.Contracts/release/GuideXos.ManagedUi.Contracts.dll` | `4EF75B400F98FDF40C9AE3E28630E36675B1BBDD7B5BB378355EAA56849B4391` |
| `out/dotnet/c186-managed-host-contract/nativeaot/build-c185-nativeaot-isolated.log` | `62936B41B29E71F8445375ED19A49D1A7C2AAEBA4320226E2917FE4F198D7989` |
| `out/dotnet/c186-managed-host-contract/nativeaot/publish-isolated/HostLogProof.exe` | `449EAA1630E6EEB75E3F6EBC37EB755FF336743D41108310050B9064348A1E94` |
| `out/dotnet/c186-managed-host-contract/nativeaot/HostLogProof.isolated.map` | `84184240AFEEF6EBBB691027AA7AEF60F57D80971428FBC3B801AE5DA05C9919` |
| `out/dotnet/c186-managed-host-contract/nativeaot/HostLogProof.isolated.elf` | `68615C7689082E713C4E8FBA646E78A2330B48171DC1A2E67DE92AC699E4FAEC` |
| `out/dotnet/c186-managed-host-contract/nativeaot/retained-code-map-evidence.txt` | `FCA85C8E24D22797CD30ED7F9A53D41E0B2495CC57D9A23B153CB36A668C4F4D` |
| `out/dotnet/c186-managed-host-contract/nativeaot/runtime_support.obj` | `D1E4344BEA8BF0587CB08D48E31D52F7D179652396A7B6A55E20C5A761D095EC` |
| `out/dotnet/c186-managed-host-contract/nativeaot/artifacts-isolated/bin/GuideXos.C185.CounterApp/release/GuideXos.C185.CounterApp.dll` | `0A0EE16DD6BEF1C48D26AE8857C08196B619DFE8E6569A3FCBF0D6B407C8651A` |
| `out/dotnet/c186-managed-host-contract/nativeaot/artifacts-isolated/bin/GuideXos.ManagedUi.Contracts/release/GuideXos.ManagedUi.Contracts.dll` | `66D8248CA5A82754F0482F73AAB870A5CDE02FD192098071F458C8E84ACD36F4` |

The aggregate manifest additionally contains hashes for its three referenced component logs. The full per-artifact SHA-256 listing is retained at `out/dotnet/c186-managed-host-contract/artifact-sha256.txt`.

## Remaining limitations and recommended C187 direction

C185 supports one static sample factory and one window with one label and one button; it is not a general app loader or multi-window SDK. The same registered selector has one current lifetime entry, while the shared C181 table still holds at most five selectors. Input and rendering are synchronous. Programmatic activation remains unsupported. The public contract does not promise persistence for application-owned state. If the native host persistently fails a close during final teardown, C185 invalidates its public wrappers after its cleanup attempt and cannot retry through them.

If C187 expands this boundary, first specify and qualify native close notification and persistent close-failure semantics for a real guest application. Keep dynamic loading, additional application slots, and broad framework extraction out of that work unless separately accepted. C186 does not start C187.
