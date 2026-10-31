# NativeAOT C188R12R4R4 — Persistent Arena Capacity and Ownership Qualification

**Outcome C — `OWNERSHIP_OR_CAPACITY_UNPROVEN`**

This hosted-only phase established the current arena and allocation-context model, verified the pinned NativeAOT slow paths, audited the five registered applications, and reran the relevant hosted gates. The existing Settings failure cannot be corrected safely by routing arrays to the stock GC slow path: the persistent C188 graph is not registered with or traced by that collector. A larger arena could avoid the observed miss, but no finite capacity is justified for the supported cumulative workload because the arena does not reclaim storage and the residency contract limits concurrent residents, not total launches or callbacks. No runtime, application, kernel, runner, or build source was changed. No QEMU or QAC action occurred and no guest candidate was staged.

## Baseline and preservation

| Item | Value |
|---|---|
| Repository | `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT` |
| Branch | `v1.1_DOTNET_SUPPORT` |
| Starting HEAD | `b2a4bc43fbedd3db1fdb12ae9c8e38b4bca0180e` |
| Ending source HEAD before this report-only closeout | `b2a4bc43fbedd3db1fdb12ae9c8e38b4bca0180e` |
| Original source ramdisk SHA-256, before and after | `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8` |
| Build/host artifacts at start | Existing untracked `bin`, `obj`, and host-smoke outputs listed below; preserved |

The initial worktree had the pre-existing modification `ESP/ramdisk.img` and these unrelated untracked outputs: `samples/host/ManagedHostServicesC185/{bin,obj}/`, `samples/host/ManagedStarterC182/obj/`, `samples/host/ManagedUiRegression/{bin,obj}/`, `samples/managed/C185CounterApp/{bin,obj}/`, `samples/managed/HostLogProof/c182-host-smoke/`, and `samples/managed/ManagedUiContracts/{bin,obj}/`. None was staged, reset, cleaned, stashed, or overwritten. Hosted qualification evidence was written into fresh `out/dotnet/` subdirectories; pre-existing evidence and the source ramdisk were left in place. No branch switch, history rewrite, or push occurred.

The report is the only intended tracked change for this phase. No `AGENTS.md` was present in the repository tree.

## Allocation-context model

The C188-specific implementation is in `tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_platform.cpp`; its 64 KiB default is selected by `GUIDEXOS_MANAGED_HEAP_BYTES`. The buffer is global image BSS, aligned to 16 bytes. With `GUIDEXOS_NATIVEAOT_PERSISTENT_COMPOSITE_LIFECYCLE`, the first managed-thread attachment clears the buffer and diagnostics once, then writes the buffer base and end into the active TLS runtime cell at offsets `+0x30` and `+0x38`. Later reverse-P/Invoke entries preserve the cursor and heap. The lifecycle flag and heap are process-global; the cursor/limit pair is per-thread TLS.

| Required element | Established behavior |
|---|---|
| Persistent arena base/end | `g_guideXosManagedHeap[65536]`, 16-byte aligned; the active cell is initially `[heapBase, heapBase + 65536]`. The R4R2 guest recorded base `0x1001F8610` and end `0x100208610`. |
| Allocation cursor | The active cursor is the pointer in the TLS runtime cell. There is no separate authoritative global arena reservation cursor or per-domain usage ledger. |
| Context limit | The active limit is the second pointer in that TLS cell. Initially it is the arena end; after NativeAOT's stock GC allocation path the observed active cursor is outside the image arena. |
| Context initialization | First persistent attachment clears the arena and seeds that thread's cursor/limit. Repeated callbacks preserve the state. The runtime-pack manifest selects `persistentCompositeLifecycle=true`. |
| Refill / retirement | No arena-local context refill, quantum reservation, unused-tail return, or context retirement code exists. The initial arena is one cursor/limit span, not a sequence of refillable local quanta. |
| Array fast path | C188's `RhpNewArray` wrapper computes checked `BaseSize + ComponentSize * length`, rounds to eight bytes, checks the active cursor/limit, calls the renamed stock array entry only on fit, then checks the returned address and cursor delta. |
| Array miss | A miss or invalid context is logged in the diagnostic build and immediately calls `guideXosFailFast(6)`. The wrapper does this before the stock `RhpNewArray` rare path. Production retains the bounded miss behavior without the diagnostic records. |
| Object fast path/fallback | The stock NativeAOT `RhpNewFast` uses the current thread context and falls through to `RhpNewObject` on a miss. In the R4 diagnostic build, the interposer records this miss and delegates to the stock function. The guest observed a 136-byte result outside the arena. |
| String path | The runtime-pack only renames/interposes `RhpNewArray` and, for R4 attribution, `RhpNewFast`; it has no C188 `RhNewString` wrapper. Pinned `RhNewString` uses the thread allocation context and its rare path enters `RhpNewArrayRare`/`RhpGcAlloc`. Dynamic string allocations therefore do not have an arena-local C188 slow path. |
| Request bounds | Array length is limited to `0x7fffffff` by the entry; object size comes from EEType `BaseSize`. Checked arithmetic and eight-byte size alignment are present. No smaller C188 fixed maximum request size is established. |
| Accounting | Heap base/size and allocator diagnostics are static; factory-attributed object/array counters and cursor fields are diagnostic-only. The failing factory never reached its aggregate end marker. String allocations are not counted by this C188 attribution wrapper. |
| Callback/reinitialization | The persistent lifecycle evidence records one initialization and preserved storage/cursor across reverse-P/Invoke callbacks. It does not establish a fresh context for another native thread; managed threads are disabled in this runtime-pack profile. |

This is not a local context with a refill quantum. The one TLS span initially covers the entire arena. There is no independent cursor from which to reserve another arena slice. A second attached thread would have a separate TLS cell, while the persistent initialization flag is global; initializing or partitioning contexts across multiple threads is not implemented or qualified.

### Exact Settings boundary

The retained R4R2 trace establishes the following ordered events for selector 5:

| Request | Cursor/context evidence | Result |
|---|---|---|
| Settings object, 136 bytes (`0x88`) | Cursor `0x1002085A8`; 104 bytes remained before arena end `0x100208610` | Stock `RhpNewFast` fallback entered and returned outside the arena |
| Next array, 88 bytes (`0x58`) | Cursor `0x100D00F90`, outside the persistent arena; 64 bytes remained in that active context | C188 wrapper emitted `bounded-failfast`; `guideXosFailFast(6)` ran before the stock array slow path |

At the first request, 65,432 of 65,536 arena bytes had been consumed. The array failure's 64 bytes describe the then-active local context, not the global arena and not total Workstation GC capacity. The trace does not name the active segment; given the preceding stock GC fallback and the outside-arena address, it is consistent with a GC-provided context. The 64-byte observation alone does not establish global exhaustion.

If both observed requests had to remain in the persistent arena, they require 224 bytes from the first request cursor. The arena then had 104 bytes left, so at least 120 additional bytes would be needed just to cover those two requests, before any later Settings graph or operational headroom. This is a lower bound for this one boundary, not a capacity recommendation. The guest trace lacks EEType and array-length fields, so the exact field initializer for the 88-byte array remains unknown. No Settings constructor entry, factory return, resident registration, or application launch was reached.

## Ownership and GC-safety map

The pinned upstream runtime is locked at NativeAOT 9.0.0 commit `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`. The [AMD64 allocation assembly](https://github.com/dotnet/runtime/blob/9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3/src/coreclr/nativeaot/Runtime/amd64/AllocFast.asm) sends an object miss through `RhpNewObject` and an array/string miss through `RhpNewArrayRare`; each calls `RhpGcAlloc`. The pinned [GC helper](https://github.com/dotnet/runtime/blob/9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3/src/coreclr/nativeaot/Runtime/GCHelpers.cpp) sets GC-reference allocation flags, calls `GCHeapUtilities::GetGCHeap()->Alloc`, installs EEType metadata and array length, and follows the normal GC allocation failure path. This is a valid NativeAOT Workstation GC allocator. It does not make the image-backed C188 arena part of that heap.

| Domain | Storage/layout and ownership | Roots, lifetime, zeroing, relocation |
|---|---|---|
| C188 persistent arena | Static image BSS; NativeAOT object/array headers and eight-byte allocation sizes are used on the accepted fast path. It is not registered as a GC heap or frozen segment, and no collector scan of its object graph is installed. | The arena is cleared once, then monotonic; newly used bytes start zero and are not reused. The resident object graph survives callbacks because the arena is not reset. No allocator deallocation or collection is provided. Addresses remain stable only for these arena objects. |
| NativeAOT Workstation GC | Stock slow paths allocate using the current NativeAOT thread allocation context and GC heap. The 136-byte object was directly observed outside the arena. | Stack transition frames protect in-flight calls; GC roots, barriers, zeroing, and possible object relocation follow NativeAOT GC rules. Persistence after a factory returns depends on collector-visible roots. C188 has not shown that its arena-held fields are scanned or updated. |
| Other/native storage | Platform TLS blocks, NativeAOT thread/runtime structures, kernel surfaces, VFS buffers, and native diagnostic storage are outside the 64 KiB object arena. | Ownership follows their native subsystems. There is no general managed-reference or relocation contract connecting these buffers to managed objects beyond the specific host ABI calls. |

Both cross-domain directions are unqualified. A GC object referenced only from an arena object's field may not be discovered as live or updated if moved. A GC object containing a reference to an arena object has no registered arena segment/barrier contract. The guest's successful return of the 136-byte object proves only that allocation returned; it does not prove rooting or validity across later collection or reverse-P/Invoke callbacks. The prior Notes callback evidence proves the existing all-arena resident graph survived callbacks with no observed GC collection; it does not qualify a mixed graph.

The 88-byte C188 array wrapper checks capacity before invoking the stock array allocator, so the stock rare path is not entered on the observed request. Removing that check or adding a mixed-domain fallback would change the ownership behavior. Even when a request fits, the wrapper verifies the returned address against the active cursor but does not establish that the active cursor belongs to the image-backed arena. Thus a “successful” allocation in a GC context would not by itself satisfy the persistent arena contract.

## Five-application capacity budget

The production registry contains five descriptors: Notes (selector 4), Settings Center (5), Calculator (6), Task Manager (7), and File Explorer (8). The resident pool has `Capacity = 5`; it limits simultaneous resident applications. `Clear` removes a resident reference and allows slot reuse, but it does not move the arena cursor backward or free storage.

| Budget component | Evidence and current bound | Classification |
|---|---|---|
| Runtime/module and registry allocations | Five descriptors, static factory entries, runtime state, and type/module initialization. Guest evidence captured the registry array and related roots/backing storage, but did not measure their individual bytes. Settings type access alone coincided with a 5,520-byte cursor reduction versus the preceding phase's pre-factory observation; the cause was not isolated. | Direct evidence of allocation; aggregate bytes unknown |
| Resident-pool backing | Capacity 5 and held array length 5. Guest snapshots identify the registry, pool owner, pool entries field, and backing array addresses. Per-object/array native sizes were not emitted. | Source-derived element bound; bytes unknown |
| Managed Notes | Text area default maximum 256 characters; text history default capacity 16 and maximum supported capacity 32. The backing text storage is `maximumCharacters * capacity` `char`s. The VFS ABI caps a file at 16 KiB and paths at 96 bytes. Notes also owns controls, picker/editor state, status/path arrays, and VFS result arrays. | Source-derived component bounds; total NativeAOT bytes unknown |
| Settings construction and retained state | Directly measured object request 136 bytes and following array request 88 bytes. Source has four groups, four stacks, four section arrays, a 24-member view capacity (22 active members), and settings persistence capped at 64 bytes. Numerous control/dialog objects are initialized as fields; exact NativeAOT object sizes and transitive field allocations are not measured. | Two direct request sizes plus source-derived counts; total unknown |
| Calculator | Source defines an 18-button array plus controller, surface, and application state. | Source-derived count; bytes unknown |
| Task Manager | Controller, close dialog/buttons, pending-close target, and live process/window observations; any snapshot/state allocation depends on runtime process state. | Source-derived fixed components; dynamic bytes unknown |
| File Explorer | Directory browser caps entries at 64; controller has six control slots and a 64-row source limit through the shared file dialog. Each resident also retains list, browser, path, and status state. | Source-derived item/control bounds; total bytes unknown |
| Arrays, strings, metadata, and alignment | NativeAOT array size includes base size/header/length and component storage; accepted fast allocation rounds to eight bytes. The 136/88 requests are already allocator sizes. Exact padding/type metadata and dynamic string allocations are not aggregated. | Source-derived layout rules; cumulative overhead unknown |
| Local-context reservation/headroom | Initial context limit is the whole arena end; no separate context quantum or refill reservation exists. At the Settings object request only 104 bytes remained. No application-level safety margin is defined. | Direct local/global boundary evidence; headroom unknown |
| Temporary versus persistent | Stack-allocated render/input buffers are temporary. Managed arrays and objects allocated from the arena remain consumed even when references are replaced or a resident slot is cleared. | Source-derived monotonic behavior |
| Repeated callbacks and launches | Notes editor storage is bounded per instance; VFS/editor results, browser refresh state, Settings graphs, and app reconstruction can allocate new managed objects/arrays. The lifecycle permits slot reuse but specifies no lifetime launch/refresh/callback cap. | Repeated allocation is source-evident; cumulative total has no supported upper bound |
| Required operational headroom | No explicit capacity envelope for launch order, resident combination, Settings open/close count, Explorer refreshes, or runtime lifetime. | Unknown |

The hosted Settings measurement of roughly 18,000 bytes is not treated as a NativeAOT requirement. Neither it nor the one failed guest factory accounts for NativeAOT metadata, all five app graphs, actual alignment, image/runtime initialization, repeated operations, or allocator-domain changes. Existing hosted memory measurements use the host CLR and are not allocator-faithful to the freestanding NativeAOT runtime.

Because the arena is monotonic and the app contract does not bound cumulative creation/replacement/callback counts, a finite arena size cannot be proven sufficient for the full normal runtime lifetime from the current contract. A five-entry resident pool bounds *live slots*, not total bytes ever allocated. The supported workload also has no explicit shutdown/reset interval that would reclaim the arena safely.

## Image-backed growth constraints

`build-runtime-pack.ps1` supports `Primary64KiB`, `Primary128KiB`, `Primary256KiB`, `Primary4MiB`, and `Primary8MiB`. The current retained diagnostic and production ELFs each have seven `PT_LOAD` segments. Direct `readelf -l` inspection recorded:

| Artifact | File size | RW load segment | RW segment file/memory size |
|---|---:|---|---|
| Diagnostic NativeAOT ELF | 2,146,304 bytes | `0x1001F5000` | `0x1200` / `0x4BFB0` |
| Proof-disabled production NativeAOT ELF | 2,179,072 bytes | `0x1001FD000` | `0x1200` / `0x4D620` |

The image loader validates ELF64 AMD64 `PT_LOAD` ranges, alignment/congruence, flags, overflow, entry point, file size, and total virtual span. Current source limits an application artifact to 4 MiB on disk, 12 MiB virtual span, and 4,096 mapped pages; it maps every segment page using frames from the active physical memory map. The 64 KiB arena is in BSS, so increasing it grows mapped `p_memsz` and physical page demand even if the file grows little. Compile-time heap options do not establish that more physical frames are available or that diagnostic and production images remain within all runtime/mapping limits. No larger image was built or loader-tested in this phase.

## Correction decision

**Strategy A — arena-local context refill:** not supported by the current code. The initial arena span is the whole context; there is no global reservation cursor, refill quantum, synchronization, or tail accounting. The first Settings request also occurs with less than its own size left in the arena. A correct allocator would need to preserve an arena cursor independently of any GC context, reserve aligned non-overlapping ranges, initialize all allocated memory correctly, and route object, array, and string misses coherently. This is more than a capacity check change.

**Strategy B — increased persistent arena:** build-time sizes exist and the ELF loader has bounded PT_LOAD mapping, but the five-application byte demand and cumulative operation horizon are unbounded/unknown. The minimum `+120` bytes implied by the first two Settings requests is not a qualified size or adequate headroom. No arbitrary multiplier was selected.

**Strategy C — stock GC slow path:** rejected for C188 persistent allocation. The upstream path is a proper Workstation GC allocation path, including object layout and array length, but its roots and collection behavior are not compatible with C188's unregistered arena-held graph on the available evidence. The 136-byte object fallback already crosses that domain boundary. No GC redesign or mixed-domain array fallback was made.

No implementation satisfies the task's ownership, capacity, exhaustion, and lifetime requirements with current evidence. In particular, disabling `guideXosFailFast(6)` would not provide a safe result. Genuine bounded failure remains the current defined behavior for the observed array-context miss.

## Hosted qualification

All listed commands ran without QEMU or QAC. They validate managed behavior, source predicates, and runners; none executes the freestanding NativeAOT allocation implementation.

| Gate | Result |
|---|---|
| C188R12R4 factory/marker/link-boundary host runner | PASS — 14 factory cases, 9 marker predicates, 9 link-boundary cases; `out/dotnet/c188r12r4r4-persistent-arena-qualification/hosted/factory-baseline-001/20261030-185734-805/hosted-qualification.json` |
| Main managed UI gate, including Settings UI | PASS — 40 suites / 969 cases; `out/dotnet/c188r12r4r4-persistent-arena-qualification/hosted/managed-ui-001/managed-ui-host-manifest.json` |
| C188 R9/R10/R11/R12R2 registry, lifecycle, provenance, and runner host aggregate | PASS — 14 + 14 + 15 + 8 managed cases, 30 runner predicates, 7 runtime-lifecycle cases; `out/dotnet/c188r12r4-factory-attribution/hosted/state-provenance-r4r4-001/manifest.json` |
| C188 prior host regressions | PASS — C182 15, C185 28, C186 15, C187 22 cases; `out/dotnet/c188r12r4-factory-attribution/hosted/prior-regressions-r4r4-001/prior-host-regressions.json` |
| C188 host identity/resizing/lifecycle checks | PASS — 25 cases, 1,000 identity operations, 500 valid resizes, 250 invalid geometry requests, 250 stale generations, 100 launch/close cycles; `out/dotnet/c188r12r4-factory-attribution/hosted/c188-host-r4r4-001/c188-host-test-manifest.json` |
| Settings handoff / selector-5 dispatch / lifetime predicates | PASS — 37 / 12 / 13 cases |
| Output bounds / runtime lifecycle / keyboard contracts / keyboard runner | PASS — 6 / 7 / 27 / 24 cases |
| QAC ownership / stream capture / R10 runner predicates | PASS — 15 / 2 / 13 cases (offline fixtures only; no QAC admission) |
| NativeAOT allocator boundary A–J regressions | NOT RUN — no corrected allocator exists, and no harness in this checkout faithfully executes the freestanding runtime allocator. Host CLR measurements and synthetic cursor simulation would not establish production allocation behavior. |
| Diagnostic/production correction builds | NOT RUN — no runtime change was justified; existing prior artifacts were rehashed, not rebuilt. |

## Artifact provenance and staging

The retained R4R1 diagnostic candidate manifest identifies source commit `95eda71428b884c697163b2d4b53cd375477e3e6`. This phase rehashed the retained artifacts and matched the prior manifests:

| Retained artifact | SHA-256 |
|---|---|
| Diagnostic NativeAOT ELF | `953E4489B9158317BD8D975C995FFE661B3431855075EE27C893838028487C10` |
| Proof-disabled production NativeAOT ELF | `F962464B35F36195B6A22C24B67467AD19C56BD9F868075490009413F4472C78` |
| Diagnostic kernel | `568B2C084387E45FE3718D422570EE761798542E461674AE8CE14A6B7804E864` |
| Production kernel | `A21A951C8DF337CEA08A7FE93580C556F0AC87F33D1B1766070FFF204B1EA168` |
| Retained staged diagnostic ramdisk | `67315FC61B90E162587D3471AE84F0C0C527B19E325897E020A75C5CAE6DE7D4` |
| Original source `ESP/ramdisk.img` | `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8` |

The staged diagnostic ramdisk manifest confirms the embedded `GXOSAPP.ELF` hash matches the retained diagnostic ELF. These artifacts predate this phase and contain the uncorrected allocator. No files were copied over the source ramdisk, no new diagnostic/production ELF or kernel was built, and no output was staged under `out/dotnet/c188r12r4r4-persistent-arena-qualification/` as a guest candidate.

## Guest acceptance readiness

No guest candidate is ready, so the future single-boot acceptance sequence remains conditional on a separately proven correction and successful diagnostic/production builds. The later QAC-authorized phase should retain the requested order: verify five descriptors, launch and retain Notes, take one screenshot-verified Settings click, capture selector-5 dispatch and both arena/context capacities, verify safe allocation-context handling of the 88-byte request, stop at the first new failure if any, verify Settings factory return/registration/surface and Notes state, then release QAC and shut down. Maximum one guest boot. No QEMU or QAC action was taken in this hosted-only phase.

## Remaining risks and smallest justified next step

The source and guest evidence do not establish a coherent mixed persistent/GC ownership model, a refillable bounded arena, a total five-application byte demand, a lifetime allocation ceiling, or physical mapping headroom for a selected larger arena. The exact array type/length and its requesting field initializer remain unknown. Dynamic string allocation does not have a C188-specific bounded wrapper. The existing 136-byte object fallback is already outside the arena and is not qualified across collection or callbacks.

The smallest justified next step is to define a finite supported lifetime allocation envelope (including app replacement and repeated callback operations), then measure the actual NativeAOT allocations within that envelope while preserving one allocation domain. Until both the envelope and collector/arena ownership contract are established, there is no defensible arena size, correction candidate, or guest-ready claim.

**Final classification: Outcome C — `OWNERSHIP_OR_CAPACITY_UNPROVEN`.**
