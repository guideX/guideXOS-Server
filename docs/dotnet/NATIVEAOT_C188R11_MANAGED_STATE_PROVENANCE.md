# NativeAOT C188R11 Managed State Provenance

**Phase:** C188R11 — NativeAOT managed state lifetime provenance and reentry diagnosis
**Repository:** `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT`
**Branch:** `v1.1_DOTNET_SUPPORT`
**Starting HEAD:** `92049e0be16c4c77197badbf3b53b53556829bae`
**Ending HEAD (C188R11 phase commit):** `95edd5c1a23c254474b950bd84342ad2ddfde909`
**Classification:** **Outcome C — DIAGNOSTIC_INCONCLUSIVE**, after one diagnostic guest.
**Product behavior correction:** none.

## 1. C188R10R1 findings

C188R10R1 observed five registered descriptors (selectors 4–8), a C181 resident mask of 1, Notes generation 1, and a successful managed launch return. Its first input callback instead reported descriptor count 0, resident mask 0, `TryFind(4)` false, and dispatch result `-4`. C181 lookup and `ManagedNotes.HandleInput` were not reached.

The old `owner` tokens were derived from static reference-slot addresses. They did not identify the objects referenced by those slots. The fixed `init=1` field was a sentinel, not an initialization epoch. The run also lacked a native marker at the initial launch boundary, so it could not compare the launch and input NativeAOT image contexts reliably.

The R11 work replaces those diagnostic methods with reference-equality identities, actual initialization and lifecycle events, and a native image-map identity. The R11 guest did not reach input, so it does not establish why the historical input snapshot was empty.

## 2. Preflight and preservation baseline

The exact repository, branch, and full HEAD matched the requested values. The checked-out commit history at start was:

```text
92049e0b C188R10R1: keep preservation report reference stable
33564600 C188R10R1: include preservation comparison
6d6702e6 C188R10R1: record managed state reentry guest
a94e0083 C188R10: diagnose managed state reentry
d4f8e0c1 C188R9 record preservation comparison
b494c0dd C188R9 diagnose managed registry reentry
```

The required R10, R9, and external-identity reports were read before edits. The preflight inventory recorded 13 protected roots, 8,497 protected files, and 136 pre-existing worktree status paths. Before the guest, the comparison found all protected files unchanged and all 136 original status flags unchanged. The original modified `ESP/ramdisk.img` retained SHA-256 `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8`.

The historical C185 uncertainty concerning 212 generated files is retained. No old guest capture, candidate, or protected media was overwritten.

## 3. Backing-object identity method

Under `HOSTLOGPROOF_C188R11_MANAGED_STATE_PROVENANCE`, `C188R11ObjectIdentityTracker` retains a strong reference and compares references with `ReferenceEquals`. It assigns a nonzero monotonically increasing token when it first observes an object or observes a different object. It records initialization and clear events separately. It uses no object address, `GetHashCode`, pinning, or unsafe pointer.

Three actual references are tracked independently:

| Identity | Referenced object |
|---|---|
| `rg` | The registry descriptor backing array, `GuideXosApplicationRegistry.s_entries` |
| `p` | The `GuideXosManagedApplicationLifetime` C181 owner object, `s_lifetime` |
| `ps` | The five-element `Entry[]` backing storage held by that lifetime object |

Identity tokens are meaningful within one managed registry initialization epoch. They are intentionally not presented as globally unique across reconstructed static state. Kernel-owned initialization counters and module-map identity provide the independent evidence for a repeated managed initializer or image mapping.

The 12 hosted R11 cases exercise: repeated observation of one object; replacement by another object; reinitialization of the same storage; clear of the same storage without replacement; stable live registry/pool/storage identities; and lifecycle state checks. The clear tracker event and the C181 pool clear counter are separate from identity replacement. Registry descriptor storage is `static readonly` and has no clear operation.

## 4. Initialization and reset provenance

The proof-only registry type initializer records an event after `s_entries` and `s_lifetime` are constructed. The lifetime constructor records the actual C181 pool creation event. Each snapshot reports registry and pool initialization epochs, object identities, replacement and clear counts, descriptor count, the derived resident mask, generation high-water mark, and teardown count.

The registry and pool epochs inside managed state can reset if their owning static data is reconstructed. To make that reconstruction observable, the diagnostic kernel also keeps independent, kernel-owned counters. The managed `C188R11-INIT` record increments separate native registry and pool initialization-event counters. `startupMarker` records NativeAOT host initialization begin/ready events and ready reentries. These records do not depend on the C# registry or pool counters surviving.

Counters are bounded and saturating. Managed state traces are capped at 48 records; native image invocations at 24; native init records at 16; and host startup records at 24. An overflow marker is emitted when the independent native event-record limits are exceeded. These are proof-only counters, not product services.

Managed snapshots identify lifecycle stages and are correlated by serial order, managed call, module ID, and epochs. They do not include one explicit monotonic sequence field on every state row. Add that field before the next guest run so ordering remains directly machine-checkable across all marker families.

The snapshot reads the descriptor array, pool references, resident slot entries, and generation values directly. It does not call `TryFind` or `TryGet`, allocate a replacement registry, or dispatch an application callback. The snapshot can advance only diagnostic trace bookkeeping; hosted tests verify that it leaves application residency, generation, clear count, teardown count, and identities unchanged.

## 5. Native launch-boundary marker correction

The first selector-4 launch uses `launchInternal`: the image is loaded and mapped there, then the managed entrypoint is called directly. `launchResident` is the later same-image resident-launch route. The earlier marker placement did not cover the first-load direct call, which explains the missing launch-side native marker without implying that the launch path had bypassed NativeAOT.

R11 instruments both first-load `launchInternal` and resident launch before and immediately after the managed entrypoint call. Input tracing surrounds the callback's call through `invokeManagedWithHostMetadata`. A kernel-owned module ID is assigned when `mapElf` successfully maps an image. Each call records the module ID, entrypoint offset relative to the artifact base, load-segment count, artifact span, selector, generation, input flag, and result. It emits no raw image or entrypoint address.

The R11 runner requires a valid launch-boundary pair and initialization evidence before it can mark the guest ready for input. Missing or malformed launch records cause an explicit failure before input. The first guest encountered a harness predicate defect: it expected the managed stage name `launch-return`, while the actual last pre-return record is named `managed-return`. That expectation is corrected and covered by 12 offline runner-predicate cases. The guest was already shut down when the mismatch was identified; it was not booted again.

The native marker is immediately after the managed entrypoint returns inside the native launch wrapper. It is not a separate marker after the outer C++ `launchInternal` function returns to its caller; the report does not claim that additional boundary was observed.

## 6. NativeAOT image and data-lifetime audit

The diagnostic composition is one NativeAOT PE converted to one ELF64 x86-64 `ET_EXEC`. The validated image has seven `PT_LOAD` segments and zero relocations. Its NativeAOT link map contains the R11 tracker and trace methods and writable managed static data. The final ELF has no dynamic-loader sections or relocation table; it has no section-header table, while the link map retains `.data` and `.bss` details. The writable load segment has file size `0x1200` and memory size `0x4bed0`, demonstrating a zero-fill tail.

`mapElf` allocates and zeroes pages for the load segments, copies each segment's `p_filesz` bytes, and leaves the remainder through `p_memsz` zeroed. The first load maps the image and records its entrypoint, base, span, and segment count in kernel-owned `g_application`. A resident launch or input call uses the stored entrypoint. There is no successful unload/reload path between ordinary launch return and callback reentry; `releaseMappedPages` is used by failed load or initialization cleanup paths.

The NativeAOT startup seam's static `g_startupState` transitions from uninitialized to initializing and then ready, or to failed. On ready reentry it installs TLS and calls `ManagedMain` without repeating `RhInitialize`. Its state is inside the mapped image. The C188R11 host-event counters and image IDs are kernel-owned and independent. The native callback constructs a fresh stack `NativeGxAppContext` for each invocation and enters the same stored NativeAOT entrypoint; selector and generation are carried in that invocation context. This context is not the owner of the static registry.

The source and one guest show one image mapping and one managed initialization event before launch return. Since the runner prevented input, the input callback's loaded-image context and repeated-init behavior remain unobserved. There is no evidence supporting a claim that launch and callback use separate runtime instances.

## 7. State-writer inventory

| State | Writer and condition | Between successful launch return and ordinary input? | Lifecycle meaning | Can empty registry and pool together? |
|---|---|---|---|---|
| Descriptor count | `s_entries.Length`; compile-time descriptor array is initialized in the registry static field initializer. No later count writer. | No ordinary writer. | One-time static type initialization. R11 candidate has selectors 4–8. | No. |
| Registry backing reference | `GuideXosApplicationRegistry.s_entries` is `static readonly`; no replacement or clear method. | No source writer. | Recreated only with the containing managed static state/image. | No explicit writer. |
| Descriptor entries | Each descriptor is created in the array initializer; fields are get-only. `TryFind` only reads. | No. | Static registration. | No explicit writer. |
| Pool owner reference | `s_lifetime` is `static readonly`; no reassignment. | No source writer. | Recreated only with managed static state/image. | No explicit writer. |
| C181 slot storage | `GuideXosManagedApplicationLifetime._entries` is a readonly five-slot array allocated in the owner constructor. | `TryStart` and `Clear` can change slot values. | Launch/replacement or legitimate teardown. | A `Clear` only changes the pool; it does not alter descriptors. |
| C181 resident mask | No stored mask. R11 derives it by scanning `Entry.Application` references by slot. | Changes only when `TryStart` writes or `Clear` empties a slot. | Ordinary launch/replacement/teardown. | No single pool writer can also clear the immutable descriptor table. |
| Generation | `_generation` advances in `TryStart` when the assigned generation exceeds the high-water value. `Clear` does not reset it. | No ordinary return writer. | Close/reopen preserves monotonic generation in the same pool. A recreated pool starts at zero. | It may reset only with pool-owner reconstruction. |
| Teardown | `OnTearingDown` runs when `TryStart` replaces an existing resident, or `Clear` removes a matching resident. | No teardown on successful ordinary `ManagedNotes.Launch` return. | Replacement, explicit application close, or failed launch cleanup. | A teardown targets one pool entry; it does not clear descriptors. |
| Host initialization | `c102_startup.c`: `g_startupState` changes only from uninitialized to initializing to ready, or failed. Ready reentry installs TLS and invokes `ManagedMain`. | It may run on callback entry, but ready reentry does not reinitialize the NativeAOT runtime. | One runtime initialization per loaded C102 image; ordinary callback reentry thereafter. | Only a new/reconstructed image could recreate managed statics; native source exposes no ordinary unload between calls. |
| Native image ownership | `launchInternal` maps on initial load and sets `g_application` Resident. Resident launch reuses the mapped entrypoint. `mapElf` cleanup paths release failed mappings. | No successful unload/replacement writer in the observed single-image lifecycle. | Kernel-owned image lifetime. | No ordinary input path that clears both managed owners. |

The native and managed source contains no single ordinary lifecycle writer capable of clearing both the descriptor registry and C181 pool. That does not exclude a second managed static initialization or image reconstruction; the R11 guest did not reach input to test that boundary.

The managed writer locations are `samples/managed/HostLogProof/GuideXos/GuideXosApplication.cs` (the `s_entries` descriptor initializer, `s_lifetime`, registry type initializer, `TryFind`, `TryStart` call site, and `Clear` forwarding path) and `samples/managed/HostLogProof/GuideXos/GuideXosManagedApplicationC150Tests.cs` (the five-slot `_entries` allocation, `_generation`, `TryStart`, both `Clear` overloads, and `OnTearingDown` call sites). Host startup transitions are in `samples/managed/HostLogProof/c102_startup.c` (`g_startupState` and `NativeAot_Entry`). Image allocation, release, launch, and callback invocation are in `kernel/core/nativeaot_application.cpp` (`mapElf`, `releaseMappedPages`, `launchInternal`, `launchResident`, and `invokeManagedWithHostMetadata`); `startupMarker` records the native initialization path. This inventory found no ordinary successful-launch-to-input writer that clears both owners.

## 8. Hosted regressions

Fresh runs passed:

| Qualification | Result |
|---|---:|
| Managed UI aggregate | 38 suites / 930 cases |
| C188 hosted identity/lifecycle | 25 cases |
| C188R9 registry reentry | 14 cases |
| C188R10 state reentry | 14 cases |
| C188R11 provenance | 12 cases |
| R11 runner predicates | 12 cases |
| C129/C136/C138 plus Notes input routing | 136 cases: 31 + 36 + 62 + 7 |
| C182 / C185 / C186 / C187 | 15 / 28 / 15 / 22 cases |
| C188 checkpoint-sequence fixtures | 20 cases |

The R11 hosted cases cover stable registry and pool identities, generation-1 Notes residency across launch return, read-only snapshots, stale-generation rejection without state mutation, no cross-selector delivery, explicit close teardown, and generation-2 reopen. Existing assertions were not weakened. The `ManagedUiContracts` and independent CounterApp projects were restored and built in isolated outputs.

Fresh output used isolated artifact roots; the installed .NET SDK/NuGet package cache was reused. The R11 hosted build emitted two unused-field warnings for the inactive R10 proof counters. The 136-case C188R8 input run emitted one existing `CS0162` unreachable-code warning. All listed qualifications returned PASS.

The final combined provenance run is retained at `out/dotnet/c188r11-managed-state-provenance/hosted/state-provenance-013/manifest.json`, SHA-256 `E798457E85E81CACDE6B9236BD98B81F3A20B3DDC10AD167EB65DFEE3715A029`. Its runner-predicate qualification passed all 12 cases with SHA-256 `019903DA1E16DE1E7ADB2D3703E37F3513FC5EAA88B7F989F2F5796A77B8EA60`. Runs used fresh isolated output roots; dependency/package caches were reused.

## 9. Diagnostic and production builds

The isolated diagnostic build outputs are under `out/dotnet/c188r11-managed-state-provenance/build/`:

- Diagnostic NativeAOT composition, PE-to-ELF conversion, ELF validation, and link map passed: ELF64 x86-64, seven load segments, zero relocations, retained R11 symbols.
- Diagnostic freestanding kernel build passed with `-ffreestanding`, `-fno-exceptions`, `-fno-rtti`, `-nostdlib`, `-nostdinc++`, and `-fno-builtin`.
- Proof-disabled production NativeAOT and kernel builds passed. Production manifests retain selectors 4–8 and omit development selector 9. Binary and map scans found no R11 markers in either production composition.

The production ABI remains v4 / 128 bytes with callback offsets 104, 112, and 120. Five managed residency slots and C181 generation checks remain intact. No application model, loader, Notes editing, or save behavior was changed. All R11 C# and kernel markers are compile-time gated and absent from proof-disabled output.

## 10. QAC admission and the single guest

The fresh pre-run census reported an online coordinator, zero QEMU processes (zero foreign), zero reservations, zero managed sessions, and zero queued work. The local process census also found no QEMU process. Candidate hashes were rechecked immediately before QAC `run`.

QAC granted exclusive reservation `3043f63c53484d0dba5706c1ac04c4dc` to owner `LEON2_RDT\guideX` (runner PID 14608). QEMU PID 26668 started at `2026-10-25T19:11:44.7009953+00:00`. QAC later logged: `reservation ... is degraded. Exclusive conditions compromised: QEMU process census is incomplete.` The runner's managed-launch-state predicate then failed before screenshot or input because it expected `launch-return` instead of the actual `managed-return` stage. Its error cleanup issued QMP `quit`; QEMU exited with code 0 and QAC logged the matching reservation release. A post-run QAC census showed zero processes, reservations, sessions, or queued requests.

The QAC degradation and runner gate make the required runtime qualification incomplete. The one authorized boot has been consumed; no second QEMU guest was started.

Raw QAC stdout/stderr, QMP log, serial output, arguments, pre/post census, process summary, and hashes are retained in `out/dotnet/c188r11-managed-state-provenance/runs/diagnostic-001/guest-001/`. The exact QEMU command line is recorded in `r11-guest-summary.json` and `qac-run-arguments.json`.

## 11. Actual launch and input timeline

| Boundary | Registry | Resident pool | Native/managed context | Evidence status |
|---|---|---|---|---|
| Before managed initialization | No managed snapshot yet. | No managed snapshot yet. | Image mapped as module 1. C102 host `initialize-begin`: host epoch 1; registry/pool event counters 0. | Actual. |
| Registry initialization / first dispatch entry | 5 descriptors; registry identity 1; registry epoch 1. | Pool identity 1; slot-storage identity 1; pool epoch 1; mask 0; generation 0. | C188R11 initialization event increments independent kernel registry/pool event counters to 1. | Actual. |
| After `TryFind(4)` in launch dispatch | Count 5; `TryFind(4)` result 0. | Mask 0; instance not yet started. | Launch route in same module 1. | Actual. |
| After Notes instance creation | Count 5. | Mask 1; generation high-water 1; teardown 0; pool clear 0; replacement 0. | `instance-start`; Notes surface generation 1. | Actual. |
| Immediately before managed return | Count 5; `rg=1`, `re=1`, `rr=0`, `rcl=0`. | `p=1`, `ps=1`, `pe=1`, mask 1, generation 1, clear 0, replacement 0, teardown 0. Result 0. | Managed `managed-return`. | Actual. |
| Immediately after managed entrypoint returns to native launch wrapper | Same launch snapshot immediately precedes it. | Same pool state. | `resident-launch`, call 1, selector 4, generation 1, input 0, module 1, entry offset `0x100A10`, seven segments, span `0x24C000`, return 0. | Actual. |
| Before first native input callback | No new snapshot. | No new snapshot. | Runner stopped before QMP screenshot or input. | Not observed. |
| At managed input entry | No input record. | No input record. | No `route=input` marker or ready reentry. | Not observed. |
| Before input `TryFind(4)` | No input lookup. | No input lookup. | Managed dispatch was never entered from a pointer/key event. | Not observed. |

The first launch snapshots show the same tracked object identities from static initialization through successful return. No registry replacement, pool replacement, initialization repeat, clear, or teardown event occurred during that launch.

## 12. First inconsistent boundary and root-cause classification

R11 did **not** observe the historical launch-to-input inconsistency. Its first and only guest confirms the launch-side state, but no input-side observation exists. Therefore, there is no first proven state-divergence boundary in R11 and no defensible A–F application root-cause classification.

Classification is **G — observation ambiguity remains**. R11 proves that the descriptor array, C181 owner, and C181 backing array existed with Notes resident at managed return, and that the native wrapper still referenced the same mapped image at that point. It does not prove what the first input callback would see. Missing input state in the older run is not evidence of memory corruption.

The verified execution blockers are the R11 runner's stage-name mismatch and the QAC runtime census-degraded warning. Both are diagnostic execution issues; neither explains the managed state loss.

## 13. Notes window and input observations

Serial evidence records selector-4 Notes launch, a generation-1 Notes surface, initial render return, successful Notes launch result, and the kernel input-loop readiness marker. This supports successful logical launch and render initialization. The runner failed before its QMP framebuffer capture, so R11 has no screenshot proving visible pixels or focus.

No pointer click was sent, so pointer dispatch, editor focus, and `ManagedNotes.HandleInput` were not observed. No keyboard character was sent, and no document mutation, redraw-after-edit, or save result is claimed. The previous `(379,359)` click was not reused because the R11 guest did not provide a current screenshot or window geometry to confirm it.

## 14. Candidate and evidence hashes

| Artifact | SHA-256 |
|---|---|
| Diagnostic NativeAOT PE | `33253B739D721F1233472220BB1D34DA67F939A8F3B767A177B936890872FDF8` |
| Diagnostic NativeAOT ELF | `C60A7694B64470F27CCD4CAF0F7DA19373D24D27EB6CA36F445A61690C8E0413` |
| Diagnostic NativeAOT map | `C40215C83A495792BE90866E28DDDBB37947647A5ACFEC5053AB82BD88183CC6` |
| Diagnostic NativeAOT qualification manifest | `3E5EAE80715EFC63D0C8E60F5DB64F5D982CC3C559B9DD3668D33D6F21D72B42` |
| Diagnostic kernel ELF | `26FF6C2CD5B96517A45C0DD4AF4B37F294867D79E1BF9E9D0158C4DC4D2A63AC` |
| Diagnostic kernel qualification manifest | `3FF01EAFE3DF1B4907D864FD3114D72D74D168B09D78684143951BEC0705F6AB` |
| Independent ManagedUiContracts DLL | `9CF8194FD070BA2BD202F7E1E685CB6BA1E9D9DEAF8352CF38D560B4177181B5` |
| Independent CounterApp DLL | `D549804E0B1046CEAEBCE4D63B48665216D514908B14824543F50B6D5B4F255C` |
| Proof-disabled production NativeAOT ELF | `AB669B54A4BE637045D462684C30EDCDA0CAB09D027BB808E4A1C84BCC82C804` |
| Proof-disabled production NativeAOT map | `09B1A8F3AAF390B8A83E604104DD09A27AABD65A5CF56955A2D0044DAE278E8B` |
| Proof-disabled production NativeAOT qualification manifest | `2A968F268B99B6E76B4872CD591E0B05315B221C79EAE56482252732BB463439` |
| Proof-disabled production kernel ELF | `22BC4AFE228263E4187B2AA57EAF21D1CEA46D3992A139F81EC185B1C22154AF` |
| Proof-disabled production kernel qualification manifest | `53E8D4F79B7ED8F3278E81AC5C10321DF8389F12B3BD41CDBB44E3BA3B5B6171` |
| Guest UEFI loader | `D72A35DA882892BA97356A1C006562ABA2908D9DD909152B96DCBB655D6C2D9B` |
| Guest staged ramdisk | `D1E2AF2DE0BDD5DD33CB3201AA27BA7CC80353291869DEBFC1C9A670FE3E801F` |
| Guest raw serial | `ED4AACC3A06A468F5E3846973D7CE0061F17EBBF8EB12C2CE8D82891E6F688F0` |

The staged RAM disk contains the diagnostic ELF hash above. The original modified `ESP/ramdisk.img` was not changed by staging. Candidate hashes, qualification manifests, QAC logs, and additional raw-log hashes are in `candidate-integrity-002.json` and `r11-guest-summary.json`.

## 15. Preservation comparison

The pre-guest and post-guest comparisons found 13/13 protected roots and 8,497/8,497 protected files with no missing, added, or changed protected file. All 136 original worktree status paths retained their original flags. New status paths were confined to R11 diagnostics, tests, documentation, isolated build/run outputs, or generated build inputs. `ESP/ramdisk.img` retained its expected SHA-256. The prior C185 212-file uncertainty is unchanged.

## 16. Remaining C188 gates

The state-loss cause is still unknown. A first input callback must be observed with launch and input module IDs, entry offsets, segment counts, spans, actual object identities, initialization events, descriptor count, resident mask, and `TryFind(4)` result. If state is available, the follow-on evidence still needs a verified Notes editor click, character insertion, document mutation, redraw, and the existing Notes save semantics.

The proof-disabled production build passed static qualification only. No proof-disabled production guest was run. The six-scenario C188 acceptance campaign was not started. C189 was not started.

## 17. Recommended C188R12 scope

After separate authorization, correct the QAC census/runtime issue and retain the fixed `managed-return` runner predicate. Then run a new single targeted diagnostic guest that requires launch snapshots before input, captures the current Notes framebuffer, sends one confirmed editor click, checks the first callback state, and sends one character only if the descriptor and resident lookup succeed. Preserve the no-input path when lookup rejects. Make a product correction only after the first actual state transition or ownership change is identified; qualify any correction in separate hosted, build, and guest evidence.

This is a recommendation only. C188R12 and C189 were not started.
