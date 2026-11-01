# C188R12R4R5R4R4 — NativeAOT GC heap initialization prerequisites

## Decision

**Case B — the failure is narrowed to a small set of initialization exits; the retained guest evidence cannot identify which exit fired.** The next engineering action is a production-isolated, allocation-free failure-site record around the selected Workstation GC's `GCHeap::Initialize` exits and its `GCEvent` wrapper allocation. Hosted-qualify that diagnostic build, then use a new one-boot guard for a separate guest attribution run. Do not reuse the consumed R3 guard.

The observed `GCHeapInitialize` marker is correctly linked to the concrete Workstation GC `GCHeap::Initialize()` method. The R3 guest passed runtime/ThreadStore publication, GC heap and handle publication, the reported region reserve/commit calls, and two platform event-create callbacks. The concrete `Initialize()` later returned `E_FAIL`. Source reduces the active `E_FAIL` paths principally to `gc_heap::initialize_gc()` returning `E_FAIL` (including its `init_semi_shared()` aggregate failure) or `WaitForGCEvent` creation returning false. Existing markers do not distinguish those paths or the underlying local allocation/event operation.

This was a source/artifact audit only. No runtime or PAL code was changed; no QEMU or QAC action was performed.

## Baseline, retained evidence, and provenance

- Branch: `v1.1_DOTNET_SUPPORT`.
- Starting HEAD: `6fe77c89f49621f8f0b3f9ff5cf91e5c95ea7b3e`.
- Source-audit ending HEAD before this documentation commit: the same `6fe77c89f49621f8f0b3f9ff5cf91e5c95ea7b3e`.
- The pre-existing worktree had `ESP/ramdisk.img` modified and generated `bin/obj` and `samples/managed/HostLogProof/c182-host-smoke` outputs untracked. They were preserved. No reset, clean, restore, stash, branch switch, or push was performed.
- `ESP/ramdisk.img` SHA-256 before and after audit: `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8`.
- The retained candidate is `out/dotnet/c188r12r4r5r4-threadstore-readiness/candidate-20261031T174601065Z`. Its build manifest records source HEAD `5038777b9f6b1b489ceb9de3e26cf6299b46956f` on the same branch. That is the candidate's build provenance, while `6fe77c89...` is the later audit checkout HEAD; the pinned runtime package and guest-run candidate identities agree.
- Candidate `candidate-manifest.json` SHA-256: `36657A671D9088E51005D87F3D72033E88C65E234F932AFA32014F5858B9442E`.
- Runtime package lock: `tools/dotnet/runtime-pack/runtime-pack.lock.json`; NativeAOT ILCompiler and `runtime.win-x64.microsoft.dotnet.ilcompiler` are 9.0.0 from runtime commit `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`. The candidate manifest repeats that runtime identity. The machine SDK version and compiler/runtime package version are separate fields; this is not a runtime-version mismatch.
- Candidate `Runtime.WorkstationGC.lib` SHA-256: `EA0C6D64397BE22B06DF19784019F79891C4CB6EFADCF4B7CDD4B0C3EE9D34F`. Candidate runtime-pack manifest SHA-256: `71C2ACF9355432D590505BC6F4554602724F26760DA4EC2C0C78A28C4D0363DA`.
- Candidate managed ELF SHA-256: `3B79938BEAC61F828D012BBAE9017934FCD0A536FCC464A7B5B5D39B03633175`; its final map SHA-256: `CD56254E4A7645D3F7E597D66E5CB40E2144C1A995D0ECE5E88391635036A070`.
- R3 raw serial: `.../qac-guest-003/serial.log`, SHA-256 `BAEF3ACE33F44924DA99DEC535EF52401075F125DBF9D05E04138B2812DFD8E9` (13,495 bytes). The retained recovery manifest is `out/dotnet/c188r12r4r5r4-threadstore-readiness/postrun-recovery-r3-20261101T181441952Z/recovery-manifest.json`. It records one guest boot, the consumed guard, no automatic retry, and release of the QAC reservation. The raw serial and derived analysis remain available.

## Linked implementation and build selection

The ELF map and candidate manifests—not the presence of code in upstream sources alone—identify the implementation used by R3:

| Operation | Selected source / linked member | Evidence |
|---|---|---|
| `GC_Initialize` | `coreclr/gc/gcload.cpp`, `Runtime.WorkstationGC:gcload.cpp.obj` | Final ELF map symbol provenance |
| `InitializeGCSelector` | `coreclr/nativeaot/Runtime/clrgc.disabled.cpp`, `standalonegc-disabled:clrgc.disabled.cpp.obj` | Map and locked `standalonegc-disabled.lib`; selector calls `InitializeDefaultGC` |
| `GCHeapUtilities::InitializeGC` / `InitializeDefaultGC` | `coreclr/nativeaot/Runtime/gcheaputilities.cpp`, `Runtime.WorkstationGC:gcheaputilities.cpp.obj` | Final ELF map |
| Concrete `GCHeap::Initialize` | `coreclr/gc/gc.cpp`, `Runtime.WorkstationGC:gcwks.cpp.obj`; symbol is `?Initialize@GCHeap@WKS@@UEAAJXZ` | Final ELF map identifies WKS implementation |
| `gc_heap::initialize_gc` and `init_semi_shared` | `coreclr/gc/gc.cpp`, linked as `Runtime.WorkstationGC:gcwks.cpp.obj` | Final ELF map includes both WKS methods |
| GC environment and platform event/VM adapters | Local `tools/dotnet/runtime-pack/src/gcenv/guidexos_gcenv.cpp` and `src/platform/guidexos_nativeaot_gcenv_startup_bridge.cpp`, linked as `guidexos_gcenv.production.obj` and `guidexos_nativeaot_gcenv_startup_bridge.production.obj` | Final map; candidate pack manifest records replacement of stock `gcenv.windows.cpp.obj` |
| Startup diagnostics | Candidate `runtime-pack/guidexos_nativeaot_gchelpers_init_diagnostics.cpp` | Candidate source wraps the exact `GCHeapUtilities::InitializeGC()` and `g_pGCHeap->Initialize()` calls and records the unmodified HRESULT |

This is NativeAOT 9.0.0 AMD64 Workstation GC, one heap, GC interface 5.3 / EE interface 2. The candidate pack configuration labels server/background/concurrent GC disabled. In the pinned `gcpriv.h`, `MULTIPLE_HEAPS` is commented out, while `BACKGROUND_GC`, `USE_REGIONS` (AMD64 non-standalone), `FEATURE_LOH_COMPACTION`, and `FEATURE_PREMORTEM_FINALIZATION` are defined; `FEATURE_BASICFREEZE` and event tracing are supplied by the candidate's compile configuration. The linked WKS and one-heap selection excludes server/multiple-heap-only branches. Background-GC support code is present in the Workstation library/map, but this does not establish that background collection or worker-thread creation was enabled or reached. The local GC environment reports WriteWatch unsupported; that is evidence against concurrent-GC capability, not evidence of a missing startup service.

The guideXOS GC environment is the local adaptation: the stock Windows GC environment object was removed from the archive and the local `guidexos_gcenv.production.obj` was inserted. `GCToOSInterface::Initialize` obtains page/processor facts and initializes the virtual-memory adapter; the startup bridge implements event, lock, and VM callbacks. The managed ELF map also binds the local NativeAOT startup replacement and guideXOS GC environment. The linked map confirms the adapter selection; it does not qualify every callback's runtime behavior.

## Initialization call graph and stage meaning

```text
RhInitialize
└─ PalInit
   └─ InitDLL
      ├─ NativeAOT interface setup and restricted callouts
      ├─ RuntimeInstance::Initialize
      │  ├─ create RuntimeInstance
      │  ├─ create and publish ThreadStore on RuntimeInstance
      │  └─ publish RuntimeInstance singleton
      └─ InitializeGC                         [outer GC initialization]
         └─ GCHeapUtilities::InitializeGC     [diagnostic stage 0x31]
            └─ InitializeGCSelector           [linked disabled standalone-GC selector]
               └─ InitializeDefaultGC
                  └─ GC_Initialize(nullptr, &heap, &manager, &dac_vars)
                     └─ publish g_pGCHeap, GC handle manager, DAC vars on S_OK
         └─ g_pGCHeap->Initialize()            [stage 0x32: GCHeapInitialize]
            └─ Workstation GCHeap::Initialize
               ├─ query QPF, processor count, physical-memory limit/config
               ├─ compute hard limits and Workstation heap/region settings
               ├─ gc_heap::initialize_gc
               │  ├─ reserve/commit region and GC bookkeeping ranges
               │  └─ init_semi_shared
               │     ├─ mark-list / optional segment-table allocations
               │     ├─ full-GC synchronization events
               │     ├─ compiled background-GC synchronization support
               │     └─ event-trace / LOH-compaction buffers
               ├─ create WaitForGCEvent
               └─ GCHeap::Init(0), make the single Workstation heap
         ├─ RhInitializeFinalization             [only after concrete Initialize succeeds]
         └─ GCHandleManager::Initialize          [only after concrete Initialize succeeds]
```

Source anchors in the pinned runtime snapshot are `nativeaot/Runtime/startup.cpp` (`RhInitialize` around 361, `InitDLL` around 92–152), `Runtime/RuntimeInstance.cpp` (RuntimeInstance and ThreadStore publication around 305–325), `Runtime/GCHelpers.cpp` (`InitializeGC`, `GCHeapUtilities::InitializeGC`, concrete heap call around 43–83), `Runtime/gcheaputilities.cpp` (selector/default selection and GC pointer publication around 52–87), `Runtime/clrgc.disabled.cpp` (default-selector body around 15–18), `gc/gcload.cpp` (`GC_Initialize` around 64–132), and `gc/gc.cpp` (`GCHeap::Initialize` around 48665 onward; `gc_heap::initialize_gc` around 14120 onward).

The `GCHeapInitialize` marker is not the GC implementation-selection call. The candidate diagnostic replacement places it immediately around `g_pGCHeap->Initialize()` and emits the HRESULT returned by that call. `GCHeapPublication` stage `0x31` wraps `GCHeapUtilities::InitializeGC()` and passed before stage `0x32` began. Thus GC implementation initialization and publication succeeded; the later concrete heap initialization failed.

## `E_FAIL` provenance

The concrete heap returns `HRESULT`. In the selected one-heap path, relevant returns are:

| Source site | Result and mapping | R3 relevance |
|---|---|---|
| `gc.cpp:14265–14269`, `gc_heap::initialize_gc` with `USE_REGIONS` | Direct `E_FAIL` if configured `regions_range == 0`; this branch logs “Cannot use regions without specifying the range.” | The guest records two successful reserve callbacks from the region setup path, so zero range is inconsistent with the observed path. Reserve callback values do not expose the actual base/alignment, but the zero-range branch would not issue these reservations. |
| `gc.cpp:14406–14412`, `gc_heap::initialize_gc` | If `init_semi_shared()` returns integer false, `initialize_gc` maps it to `E_FAIL`. Its internal failures share cleanup and return `ret == 0`. | Active and a primary candidate. The guest stage does not expose the internal failure site. |
| `gc.cpp:48946–48947`, `GCHeap::Initialize` | Propagates any non-`S_OK` HRESULT from `initialize_gc()` unchanged. | Explains how the `init_semi_shared` `E_FAIL` reaches the stage marker without a different code. |
| `gc.cpp:48962–48965`, `GCHeap::Initialize` | If `WaitForGCEvent->CreateManualEventNoThrow(TRUE)` returns false, logs and returns direct `E_FAIL`. The preceding `new (nothrow) GCEvent` failure at 48955–48960 instead returns `E_OUTOFMEMORY`. | Also active if `initialize_gc()` succeeded. A failure allocating the local `GCEvent::Impl` can return false before the instrumented platform callback and therefore leave no stage-`0x50` failure marker. |
| `gc.cpp:49054–49059` | `E_FAIL` for NUMA reservation/commit failure in the multi-heap balancing path. | Inactive for the linked Workstation one-heap configuration; it is guarded by multiple-heap configuration. |
| `gc.cpp:14128–14137` | Direct `E_FAIL` if the `GC_CONFIG_DRIVEN` log cannot be opened. | `GC_CONFIG_DRIVEN` is not selected by the candidate build; no linked config-log path was found. Not a current candidate. |

Other direct returns in `GCHeap::Initialize` (`CLR_E_GC_BAD_HARD_LIMIT`, bad-region/configuration HRESULTs, large-page configuration errors, and allocation failures) are not `E_FAIL`. The selected Workstation `GCHeap::Init(0)` path reports heap creation failure as `E_OUTOFMEMORY`. The `WaitForGCEvent` path follows `initialize_gc()` and therefore is not reached if the earlier aggregate failure was returned.

Inside `init_semi_shared()` the source's shared false-return candidates include failure to allocate `g_mark_list_copy` or `g_mark_list` (`gc.cpp:14451–14467`), optional `seg_table` under `FEATURE_BASICFREEZE` (`14478–14482`), either full-GC event (`14489–14495`), compiled BGC support event creation (`14593–14605`, helper at `39488–39538`), and event-trace / LOH-compaction arrays (`14620–14640`). Each failure takes cleanup and returns false, which the caller maps to `E_FAIL`. `GCEvent::Create*NoThrow` also allocates its local `GCEvent::Impl` before calling the instrumented platform bridge (`tools/dotnet/runtime-pack/src/gcenv/guidexos_gcenv.cpp:162–187`); failure at that wrapper allocation is invisible to the current stage-`0x50` callback markers.

The `GCHeap::Initialize` failure can therefore be narrowed to: (1) `init_semi_shared()` false, with its internal allocations/event setup as the bounded subsite set; or (2) `WaitForGCEvent` creation false after `initialize_gc()` success. The region-range direct `E_FAIL` is contradicted by successful region reservations. The raw HRESULT alone cannot distinguish (1) and (2), nor identify an internal allocation versus an event creation failure.

## Freestanding prerequisite matrix

“Guest-proven” below applies only to the observed call or successful callback and its recorded result. It does not qualify all future uses of that service.

| Prerequisite | Selected-path use and implementation | Classification from retained evidence |
|---|---|---|
| Runtime instance and ThreadStore publication | `RuntimeInstance::Initialize` creates the ThreadStore and publishes the runtime instance before `InitializeGC`. | **Guest-proven** for creation/publication (R3 singleton bits reached `0x13`). This does not mean the current thread is attached. |
| GC heap / handle-manager publication | `GC_Initialize` and `GCHeapUtilities::InitializeGC` publish the heap and handle manager before concrete `GCHeap::Initialize`. | **Guest-proven** (stage `0x31` passed; singleton bits reached `0x1F`). |
| Current-thread attachment, TLS, reverse-P/Invoke frame, stack bounds | These are not invoked by the path before the observed `GCHeapInitialize` return. R4 reverse-P/Invoke and `PalGetMaximumStackBounds` work is after `RhInitialize` and remains guest-unqualified. | **Not applicable to this failure path / unqualified for later entry.** Do not use it as the direct cause or add eager attachment here. |
| Page size and GC VM adapter install | Local `GCToOSInterface::Initialize` reads page size; adapter initialization checks the guideXOS GC page size. Adapter-init stage passed (4096-byte page contract). | **Guest-proven** for adapter initialization and page-size check. |
| VM reserve and commit | Local bridge invokes the guideXOS reserve/commit callbacks. R3 recorded reserve requests of `0x4000` and `0x95` pages, both returned non-null; four commits returned success. | **Guest-proven** for those callbacks and requests. Address, alignment, flags, committed byte ranges, protection, and accounting values are not in the trace. Decommit/release and other ranges are **source-implemented but guest-unqualified**. |
| Page alignment, bounds, protections, memory accounting | Implemented through local VM adapter and host callbacks; only requested page counts and callback success were emitted for this run. | **Source-implemented but guest-unqualified** beyond the accepted reservations/commits. No demonstrated defect. |
| GC synchronization events | The local GCEvent platform create callback returned success twice. The callback stage is below `GCEvent::Impl` allocation. | **Guest-proven** for two platform event callbacks only. Wrapper allocation, other event instances, signaling, waits, close, and lock behavior are **guest-unqualified**. |
| Locks, atomics, thread identity | Local startup bridge implements critical-section storage/operations and obtains the current thread ID through the PAL hook; no failure-specific markers cover these operations. | **Source-implemented but guest-unqualified** for the concrete initialization uses. No trace supports attributing the observed HRESULT to them. |
| CPU topology, timing, memory limit/status | `GCHeap::Initialize` queries QPF, total processors, configured/host physical memory, then computes limits. The local GC environment provides these services. Values are not recorded in R3. | **Source-implemented but guest-unqualified** for returned values and limit decisions. |
| GC configuration / region range | Local GCConfig replacement is linked; `USE_REGIONS` path is selected. Successful reserve callbacks show execution entered a nonzero region setup path. The actual configuration value is not printed. | **Guest-proven** that region setup progressed to reservation; detailed limits/settings remain **unqualified**. |
| Worker-thread creation and managed attachment | BGC worker creation occurs only when later GC work requests it; no allocation, collection, module registration, current-thread attach, or managed main was reached. | **Not applicable to the failing initialization interval.** Do not infer thread-service failure from later unqualified status. |
| GC finalization and handle initialization | `RhInitializeFinalization` and final `GCHandleManager::Initialize` are sequenced after concrete heap initialization. | **Not reached** after this failure. |

No required service is demonstrated missing or incompatible. VM reserve/commit, two event callbacks, and singleton publication succeeded; unqualified callbacks remain hypotheses only when tied to a concrete source return path.

## R3 evidence correlation and diagnostic limits

The raw serial and derived `serial-init-trace-analysis.json` agree on the essential sequence:

1. Runtime instance / ThreadStore publication and GC heap publication passed.
2. GC VM adapter initialization passed; GC heap publication stage `0x31` passed.
3. During concrete stage `0x32`, two reserve callbacks passed (`0x4000` pages and `0x95` pages), four commit callbacks passed, and two event-create callbacks passed.
4. `GCHeapInitialize` returned `0x80004005`; the outer GC/InitDLL/RhInitialize diagnostic markers then returned boolean/status failure. There is no earlier explicit failure marker.
5. `trace_end` declares 55 records, while the parser recovered 54 (`overflow=0`, `serialDropped=0`); `initTraceComplete=false`. Preserve this incompleteness when interpreting absence of a marker.

The marker coverage has three material gaps:

- Stage `0x32` wraps the whole concrete `GCHeap::Initialize`, not a substep. Its HRESULT is exact but has no return-site attribution.
- Stage `0x50` is placed in the local platform `createEvent` callback. It does not cover `GCEvent::Impl` allocation in `guidexos_gcenv.cpp`, nor the mark-list, segment-table, event-trace, and LOH-compaction allocations in `init_semi_shared()`.
- Reserve tracing records requested pages; commit tracing records boolean success. Neither logs the address, reserve alignment/flags, requested commit size, or memory-limit inputs. These omissions do not turn the successful callbacks into a demonstrated fault.

The two successful event callbacks are consistent with the two full-GC events created in `init_semi_shared()`. They do not show whether later event wrapper allocation, `WaitForGCEvent` creation, or another shared cleanup return failed. A one-record count discrepancy further prevents relying on the trace as complete.

R3 established no module registration, current-thread attachment qualification, reverse-P/Invoke execution, `ManagedMain`, experimental managed allocations, or collections. The R4 reverse-P/Invoke frame and stack-bounds corrections occur after the observed startup failure and are not in this root-cause set.

## Minimum next probe

Add a diagnostic-only fixed-size record for this selected Workstation GC build. It must not allocate, lock, print, call managed code, or depend on a fully initialized heap while a failure is being recorded. Record scalars in static storage and read/serialize them only after `RhInitialize` has returned false through the existing early-boot-safe path.

Record:

- a compact `failure_site` enum and substage (including the two `GCHeap::Initialize` E_FAIL returns, `init_semi_shared` cleanup sites, and the later `WaitForGCEvent` create result);
- whether `initialize_gc()` returned and its exact HRESULT before `GCHeap::Initialize` proceeds;
- for each `GCEvent::Create*NoThrow`, event ordinal/type and whether failure occurred allocating `GCEvent::Impl`, entering the platform callback, or creating the OS event;
- the `init_semi_shared()` local failure site/return value and final concrete heap HRESULT.

Do not add broad GC or PAL logging. Keep the record compile-time production-isolated, verify the production ELF has no probe symbols or map changes beyond existing production sources, add/extend a hosted parser check for bounded records, and then conduct a separate guest run under a new one-boot guard. That run should decide among the remaining source return paths; it should not be presented as GC acceptance unless later attachment, allocation, and collection gates are separately qualified.

## Checks performed

- Read the requested R4, R4R2, R4R3, and isolated GC ownership documentation, plus the retained candidate manifest, ELF map, raw serial, derived trace analysis, and recovery manifest.
- Rehashed the source ramdisk and checked candidate provenance against the final map and runtime pack lock.
- Ran existing hosted attribution check: `scripts/dotnet/test-c188r12r4r5r1-init-attribution.ps1` — **PASS**, output under `out/dotnet/c188r12r4r5r1-runtime-init-attribution/hosted-20261101T185220938Z`. This validates hosted parser/source expectations only; it is not guest acceptance.
- No runtime source, PAL source, candidate, guest, or ramdisk changes were made for this audit.
