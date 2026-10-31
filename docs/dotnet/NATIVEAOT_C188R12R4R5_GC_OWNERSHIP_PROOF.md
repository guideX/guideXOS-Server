# NativeAOT C188R12R4R5 GC Ownership Proof

**Date:** 2026-10-31
**Branch / source HEAD:** `v1.1_DOTNET_SUPPORT` / `48a80c11cbd59c4551d7282536e2741d2853c1f6`
**Outcome:** `EXPERIMENT_BLOCKED`

## Decision

The hosted graph and diagnostic candidate built successfully, but the one authorized QAC guest returned failure from `RhInitialize` before entering `ManagedMain`. No object or array was allocated by the target guest, no callback was completed, and no target collection was requested. This is neither GC graph acceptance nor evidence of a root/collection defect. NativeAOT Workstation GC ownership in the guideXOS guest therefore remains **unproven**.

The first observed blocker is runtime startup: `RhInitialize` returned `1`. The adapter only exports a Boolean startup result, generation, and startup-success bit; it does not export the GC startup operation stage or singleton-publication state. The evidence cannot identify which internal startup callback failed, so attributing the failure to a particular VM, thread, or root operation would be speculation.

The next phase should add pointer-free startup diagnostics for the existing `g_guidexos_nativeaot_gc_startup_stage`, last PAL/VM result, and the `RuntimeInstance`, `ThreadStore`, `g_pGCHeap`, and `g_pRhConfig` publication bits. It should then fix only the first evidenced failed prerequisite. This phase used its one QAC guest and must not retry it. No resident application should move to GC ownership until a new, separately authorized guest proves the complete graph contract.

## Baseline and preservation

- The starting and candidate-build HEAD remained `48a80c11cbd59c4551d7282536e2741d2853c1f6` on `v1.1_DOTNET_SUPPORT`.
- The pre-existing modified `ESP/ramdisk.img` was not edited. Its SHA-256 before and after the experiment is `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8`. The isolated ESP contains a copy with the same hash.
- Existing generated and untracked work was left in place. No reset, clean, restore, stash, branch switch, history rewrite, or push was performed.
- The retained managed UI baseline is **40 suites / 969 cases** from the C188R12R4R4 evidence. It was not rerun because this proof-only profile does not change the UI or its dependencies.
- Focused reruns passed: the new hosted ownership proof; C188R12R4 factory predicates (9 cases); C188R12R4R1 link-boundary predicates (9 cases); C188R11 runtime lifecycle predicates (7 cases); QAC ownership predicates (15 cases); and QAC stream-capture predicates (2 cases). The QAC-related tests used local fixtures/process streams and launched no guest. PowerShell Core syntax parsing passed for the candidate builder, hosted test, and QAC runner; the BOM-free QMP writer framing check passed.
- The proof-disabled production NativeAOT build passed. It retained `persistentCompositeLifecycle=true`, emitted an x86-64 ELF, and contained no C188 proof instrumentation. ELF SHA-256: `E4D56DFE23624C8ACD720146A332FFD3D5F9A7B69812A1274ADBBE854E2DD268`.

## Previously established C188 findings

The prior guest/source results remain unchanged:

- The C188 image-backed arena is 64 KiB, has no refill or independent global cursor, and is not registered as a Workstation GC segment/root domain.
- The 88-byte array request failed against 64 bytes of allocation-context availability in `RhpNewArray` before reaching the stock array slow path. The wrapper used fail-fast reason 6.
- The stock object slow path can reach Workstation GC. That establishes an allocation route, not a rooted lifetime or safe collection.
- `persistentCompositeLifecycle=true` prevents destructive runtime reinitialization and arena clearing across entries. It does not register arena references as GC roots.
- No finite arena size has been justified for indefinite resident operations. Increasing it alone is not an accepted fix.

These are recorded in `NATIVEAOT_C188R12R4R2_FACTORY_GUEST_ATTRIBUTION.md`, `NATIVEAOT_C188R12R4R3_ARRAY_CAPACITY_CORRECTION.md`, `NATIVEAOT_C188R12R4R4_PERSISTENT_ARENA_CAPACITY.md`, and the C188R11R4 lifecycle acceptance evidence.

## Ownership contract and domain separation

```text
Accepted C188 resident path (unchanged):
  resident graph -> 64 KiB image arena (not a GC segment or root)
  stock slow path -> Workstation GC heap, but cross-domain ownership is unqualified

C188R12R4R5 proof profile (arena omitted):
  static GCHandle -> GC root table -> root -> child / reference array / data arrays
                                      all allocations intended for Workstation GC
```

The diagnostic runtime manifest reports `imageBackedArenaPresent=false`, `managedHeapBytes=0` (no statically reserved C188 arena), and `persistentCompositeLifecycle=false`. The proof does not take references into the arena, route resident application allocations to GC, or run Settings. The ordinary production profile remains on its existing lifecycle configuration.

The minimum contract for a future resident graph is: GC-owned object and array allocation; replenished allocation contexts; registered static/handle roots; valid object, array, and inter-object references; root updates during moving collection; write-barrier correctness; discoverable managed stacks and attached threads; a defined resident root lifetime; and one well-defined runtime initialization/shutdown lifetime. One allocation or one persistent arena cursor does not establish these properties.

## Proof graph and hosted results

The isolated graph uses two `Node` objects in a cycle, a `Node[2]` reference array, an `int[6]`, and a `byte[16]`. It checks zero initialization, array lengths, deterministic sentinels, reference identity, and a static `GCHandleType.Normal` root. A separate weak handle tracks a no-inline temporary target. The graph is retained across three `ManagedMain` calls; the two later entries request blocking, forced, compacting Gen2 collections.

The native diagnostic uses the pinned runtime's GC object/heap inspection API to check that each observed address is within the GC heap, has a nonzero EEType, and has the expected array length. It reads native heap and thread-allocation counters and emits bounded fixed-format markers. The diagnostic path itself performs no managed allocation.

Hosted result: **PASS** — 3 callbacks, 17 object observations, 2 compacting collections, and 3 negative collection-qualification guards. Five injected bad marker cases were rejected. The hosted marker parser also accepted a valid collection marker with `moved=0`; relocation is recorded, not required by the contract. In the actual hosted run, the root and child moved in both collections, the strong handle resolved to the updated root, and the weak target cleared. Output SHA-256: `D1F2067BF72A8BE3D3D19914F34A9A5B7B5DEABFC8BCEC72C2C440E1097A4E41`. This validates the graph/test logic on the host CLR only; it does not qualify the freestanding target runtime.

## Pinned runtime and freestanding prerequisites

The candidate used NativeAOT 9.0.0 AMD64 Workstation GC, GC interface 5.3, EE interface 2, source commit `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`. The evidence below distinguishes source presence and older runtime evidence from this candidate's failed startup.

| Prerequisite | Classification | Evidence and boundary |
|---|---|---|
| Object allocation | Implemented; this guest unqualified | The real-GC profile excludes the C188 arena and routes `RhpNewFast` to the stock NativeAOT path. The guest stopped in `RhInitialize`, before object allocation. Prior object fallback evidence proves only that allocation path can be reached. |
| Array allocation | Implemented; this guest unqualified | The profile routes `RhpNewArray` through the stock array slow path. The candidate map includes `RhpNewArray` and `guideXosStockRhpNewArray`; no target array was allocated because startup failed. |
| Allocation-context refill | Implemented in the pinned Workstation GC and bridge; this guest unqualified | The GC VM adapter supplies reserve/commit/decommit/release/reset and page-size callbacks. Prior C011 GC evidence exercises the runtime, but this C188R12R4R5 image did not pass `RhInitialize`. |
| Page allocation / release | Source-verified; not qualified by this run | The kernel startup callbacks reserve, commit, decommit, release, and reset virtual-memory regions. Commit uses read/write memory. This run did not establish which of those callbacks completed before `RhInitialize` failed. |
| Virtual-memory reservation and commitment | Source-verified; not qualified by this run | `gcVirtualReserve` and `gcVirtualCommit` forward to the installed platform table; the QEMU harness uses the kernel true-VM reserve/commit operations. No candidate guest heap reservation/commit result was emitted. |
| Page protection | Present but unqualified | The PAL exposes `PalVirtualProtect` through the kernel protection hook. The GC startup VM table has no separate protection callback and commits read/write pages. No protection transition was observed in this proof. |
| Thread enumeration | Implemented for the registered ThreadStore; multi-thread use unqualified | The adapter tracks at most 32 attached runtime threads and the GC platform uses `ThreadStore::Iterator`. The proof configuration requests no managed worker threads and the QEMU harness uses one CPU/current thread. |
| Stack bounds and stack scanning | Source-verified; prior single-thread guest evidence only | The kernel supplies current stack bounds to the ThreadStore. The new graph uses a registered strong handle rather than relying on a stack local. Earlier C011 evidence demonstrated an updated stack root in a compacting collection; this image did not initialize. |
| Suspension and resumption | Prior single-thread guest-proven; not requalified here | The pinned collector's `SuspendEE`/`RestartEE` integration and prior C011 collection evidence establish the one-thread precedent. No suspension occurred in this failed startup. |
| Allocation-context maintenance | Implemented in the runtime integration; not guest-proven here | The runtime integration binds allocation state to the current attached thread. The startup failure occurred before any managed allocation or refill evidence. |
| Exception and failure reporting | Missing for recoverable managed exceptions in this profile | The runtime manifest has `managedExceptions=false`. PAL fail-fast and native error returns exist; this proof cannot recover from managed OOM or general managed exceptions. The expected success path does not depend on managed exception recovery. |
| Managed static roots / GC handles | Runtime and proof code present; target unqualified | The graph uses a static normal GCHandle and a weak GCHandle; the candidate map contains GCHandle construction/free paths. Hosted execution validated update/clear semantics. The target did not reach `GCHandle.Alloc`. |
| Runtime module registration | Startup path present; target unqualified | The candidate links the application startup entry and NativeAOT initialization code. `RhInitialize` returned failure; module and GC singleton publication state was not exported by the minimal adapter. |
| Object references and write barriers | Runtime code present; target unqualified | The graph writes cyclic fields and a reference array and validates them after host compaction. The pinned Workstation GC write-barrier implementation is linked in the candidate map. No target reference store or collection executed. |
| Native transition frames | Present in the runtime integration; not exercised by this guest | `RhpReversePInvoke` and return helpers are provided. The intended outer lifetime is repeated native calls to `ManagedMain`; the target failed before the first call. No unsafe object pointer is passed to native code. |
| Resident-object lifetime | Not established for guideXOS | A static strong handle is the intended prototype lifetime root. The target did not run. `persistentCompositeLifecycle` alone remains insufficient evidence. |
| GC initialization and shutdown | Initialization failed; shutdown untested | The serial log records `RhInitialize return=00000001`, `preGcState=00000000`, and zero startup allocations. In this adapter, `preGcState` is only a success bit, not a report of individual GC singletons. |

## Candidate and guest evidence

The isolated candidate built and passed its map, PE/ELF, pinned-runtime, and arena-exclusion gates. Candidate root:

`out/dotnet/c188r12r4r5-gc-ownership-proof/candidate-20261031T034601Z`

| Artifact | SHA-256 |
|---|---|
| Runtime-pack platform object | `0DA46D2878AD3BD7DA3868546A7B2779EEB2B752AA5E334D8654C968E90FC7FB` |
| NativeAOT PE | `51EADC9F386635DB05A9259F94549896C53B5D6B083A0F309DC3671D83B8EE08` |
| NativeAOT ELF | `703C3CB14C0A26E9CADA9A00E94A6D28D75C4DA5A0A87E7710A22D986F0FA638` |
| Specialized kernel ELF | `C462629BC58C1168D388D553057CC5B2F2A12C76150313091CE34A73A3797EE9` |
| Candidate ramdisk copy | `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8` |
| Candidate manifest | `32CAAA258FD63C1764E0C38DE81D2DC5268CE880ABF2CB972730AAA17DC855B6` |
| Runtime-pack manifest | `1A2853CBB566D39D2AFDDD6DFB191888D9CA8E7B87C6224E077DD9F1F3F55719` |
| Guest recovery manifest | `88E36E93BE2D8111BBEED25027615065E7DD8C1EA8299736197ED9E66F86C567` |
| Raw guest serial log | `2FD460E80F57FD664C25D7D5686204668C069C938C2E5CBE233BECDCE9BF941C` |
| Original (BOM-rejected) QMP log | `884D33E957DC53CF38436FFE10F69A9E88F71F771F235DC7ADA2BA3916E50953` |
| BOM-free manual QMP shutdown log | `37160E5A248D86EAA845A3246C51FCD27787A3F7958345389212F360222E7B3E` |
| Phase-wide one-shot attempt marker | `472BB5C3E7DC589CF5607EA95676AC708AAFA6E05DC2AA624AB5F12EB357BBC7` |
| Post-shutdown QAC status snapshot | `4391846DCE006B72DFA1A6724879D363FAA0A7CA0A9A4D2B16AFDC47DC6CE138` |
| Post-shutdown QAC list snapshot | `12D7C7877835DFEFD4B593A7B0FA48EC41D59C40ED1E23040EBFE1D0D1E57B8A` |

The candidate was admitted once through QAC reservation `3cd7ce6691f448cd9ed08a8bde038b1b` (owner PID 22580, QEMU PID 16944). The pre-admission census was online and empty. The guest installed both PAL hook tables and staged the artifact, then stopped at `RhInitialize: FAIL`; no managed proof marker was emitted.

The first runner invocation failed before QAC start due a PowerShell bare `false` value. The corrected one-shot runner then admitted the one guest. Its initial QMP writer emitted a UTF-8 BOM, which QEMU rejected. A BOM-free QMP `query-status` and `quit` were sent to that same QAC-owned QEMU and are recorded in `qmp-manual-shutdown.log`. PID 16944 and owner PID 22580 are no longer running. Fresh QAC status/list both report zero QEMU processes, zero foreign processes, zero reservations, zero sessions, zero queued runs, and zero blocked runs.

The runner then hit a post-run logging defect: it tried to rewrite QEMU's serial file while the file was still open. The raw serial file was left intact. `guest-manifest.json` is a recovery record written from the preserved serial/QMP logs and a fresh post-shutdown QAC snapshot; `qacExitCode` is explicitly null because the runner exited before recording it. The QAC release and both process terminations were independently verified. The phase-wide `guest-launch-attempt.json` remains as a one-shot guard. There was no second guest attempt.

## Production isolation and retained behavior

- The candidate runtime manifest selects `GcOwnershipProof`, `imageBackedArenaPresent=false`, `managedHeapBytes=0`, `persistentCompositeLifecycle=false`, `managedThreads=false`, and `managedExceptions=false`.
- Proof-only native diagnostics are guarded by `GUIDEXOS_NATIVEAOT_C188R12R4R5_GC_OWNERSHIP_PROOF`. The ordinary production NativeAOT build passed with diagnostics absent and `persistentCompositeLifecycle=true`.
- No Settings selector, Settings UI, five-resident campaign, or production resident graph was run or changed.
- The previous 40-suite / 969-case UI baseline remains the retained regression evidence. The new proof project mode and native diagnostics are isolated from its runtime/UI dependencies.
- `ESP/ramdisk.img` remains at the required hash.

## Smallest justified next phase

1. Extend only the isolated startup adapter with a pointer-free report of the GC startup stage, last platform callback result, and runtime/ThreadStore/GCHeap/GCConfig publication bits. Rebuild the candidate and verify the report against the pinned startup bridge.
2. Identify and implement only the first missing or incompatible startup operation. Keep the C188 arena excluded and production allocator unchanged.
3. In a separately authorized phase, request one QAC guest. Require `RhInitialize` success before executing the graph. Then prove target GC ownership for every object/array, root-handle update, weak-target clearing, repeated native entry, reference integrity, and two real compacting collections; report relocation as observed or not observed.
4. Only after that guest passes should a separately scoped migration design select one resident boundary. Keep rollback to the current arena/lifecycle configuration and rerun the relevant C188 factory, lifecycle, allocator, and full UI regressions before any production rollout.

No migration, allocator redirection, arena expansion, Settings change, or push is part of this phase.
