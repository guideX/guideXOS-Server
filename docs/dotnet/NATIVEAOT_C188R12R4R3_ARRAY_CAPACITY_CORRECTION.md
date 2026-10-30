# NativeAOT C188R12R4R3 — Array Capacity Correction Audit

**Outcome: `CORRECTION_UNSAFE_OR_UNPROVEN`**

This hosted-only phase confirmed the array fast-capacity failure and traced the available NativeAOT slow path. The stock array slow path allocates on the Workstation GC heap, while the C188 resident profile keeps its primary object graph in a separate persistent image-backed arena and does not have qualified collection support. The retained evidence does not establish a safe lifetime contract for mixing those regions, and it does not size the full five-application workload well enough to select a larger arena. No allocator or capacity change was made, no corrected candidate was built or staged, and no QEMU boot or QAC admission occurred.

## Baseline and preservation

| Item | Value |
|---|---|
| Branch | `v1.1_DOTNET_SUPPORT` |
| Starting HEAD | `6c7124120bbfb128bdb03269af85f5d65ecc37fc` |
| Ending source HEAD before this report-only commit | `6c7124120bbfb128bdb03269af85f5d65ecc37fc` |
| Worktree at start | `ESP/ramdisk.img` was modified; unrelated generated `bin/obj` and host-smoke outputs were untracked |
| Original source ramdisk SHA-256 | `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8` |
| Source ramdisk after hosted work | `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8` |

The starting branch was ahead of its upstream by 10 commits. I did not reset, restore, clean, stash, rewrite history, switch branches, or modify the source ramdisk. Existing untracked build outputs were left in place. This phase adds only this report; hosted evidence is under ignored `out/dotnet/` paths.

Reviewed phase records:

- [C188R12R4 factory allocation attribution](NATIVEAOT_C188R12R4_FACTORY_ALLOCATION_ATTRIBUTION.md)
- [C188R12R4R1 diagnostic link repair](NATIVEAOT_C188R12R4R1_DIAGNOSTIC_LINK_REPAIR.md)
- [C188R12R4R2 factory guest attribution](NATIVEAOT_C188R12R4R2_FACTORY_GUEST_ATTRIBUTION.md)
- [C188R11R4 memory ownership](NATIVEAOT_C188R11R4_MEMORY_OWNERSHIP.md)

## Proven failure and allocation-context semantics

The C188 runtime-pack profile configures `GUIDEXOS_MANAGED_HEAP_BYTES=65536`. `g_guideXosManagedHeap` is a 16-byte-aligned BSS region. With `persistentCompositeLifecycle=true`, `initializeRuntimeState` clears it and seeds the current NativeAOT thread allocation context's cursor and limit once, then preserves them across reverse-P/Invoke entries. The cursor/limit cell is represented in the platform TLS runtime block at offsets `+0x30` and `+0x38`. It is a per-thread allocation context, initially backed by the static arena; it is not itself a global arena-capacity counter or a GC segment descriptor. After the object slow path, the guest trace reports this context at an address outside the image arena, so the 64-byte array miss belongs to the then-current context. The trace does not report that region's full segment capacity.

The C188 R4 array wrapper derives the requested array size from the EEType component size and base size, rounds to eight bytes, reads the current cursor/limit pair, and calls `guideXosFailFast(6)` if the request does not fit. It does so before calling the renamed stock `guideXosStockRhpNewArray`. The fail-fast routine is `[[noreturn]]`, calls `__fastfail(kFastFailFatalAppExit)`, and never returns to the managed factory. In this allocator path, reason `6` is the bounded-allocation failure reason; it is not a managed `OutOfMemoryException` result.

The R4R2 trace establishes these values in the same Settings invocation:

| Boundary | Request | Cursor and available capacity | Result |
|---|---:|---|---|
| Settings object | 136 bytes (`0x88`) | Cursor `0x1002085A8`; 104 bytes remained before the 64 KiB arena end at `0x100208610` | Stock `RhpNewFast` fallback returned outside the arena |
| Next array | 88 bytes (`0x58`) | Cursor `0x100D00F90`; 64 bytes available in the then-current context, outside the image-backed arena | `bounded-failfast`, then `guideXosFailFast(6)` |

The 64-byte value is local cursor-to-limit capacity; it does **not** mean that only 64 bytes of all managed memory remained. At the preceding object request, the image-backed arena had 104 bytes remaining, so it was nearly full at that point. The object fallback then supplied storage outside it. The trace does not report the remaining capacity or ownership state of every other region, so it does not establish total managed-memory exhaustion.

The 88-byte array size already includes NativeAOT `EEType::BaseSize` (including the object and array header/length layout), `ComponentSize * length`, and eight-byte rounding. The trace omits the EEType and array length, so it does not identify the requesting field. The 136-byte object request is the EEType base-size request used by the stock object fast allocator; it includes its object header and the layout's alignment.

The managed path was selector 5 through `C188R12R4FactoryDiagnostics.CreateSettingsCenter`: type access returned, the factory emitted `object-allocation-request`, and `new ManagedSettingsCenter()` began. The Settings constructor's instance field initializers run before the constructor body; the guest observed no `constructor-enter`, factory return, resident registration, or `ManagedSettingsCenter.Launch`. The 88-byte request therefore occurred during object construction before the constructor body, but the missing EEType/length means the exact initializer is still unknown. The `_sectionLeaves` arrays in the constructor body are later and cannot be assigned as the failed request from this trace.

## Array and object allocator comparison

### Array route

`RhpNewArray` in `guidexos_nativeaot_platform.cpp` validates the EEType and length, computes the checked size, and in the bounded C188 branch checks the image TLS cursor/limit. A capacity miss is recorded as `bounded-failfast`; the stock array entry is never reached. On a fitting request, the wrapper calls the stock NativeAOT entry and verifies the returned address and cursor movement.

The locked NativeAOT 9.0.0 AMD64 `AllocFast.asm` has an array rare path: when its thread allocation context cannot fit the size, `RhpNewArrayRare` creates a cooperative P/Invoke transition frame and calls `RhpGcAlloc`. `GCHelpers.cpp` then passes the request to `GcAllocInternal` / `GCHeap::Alloc`, publishes the EEType and array length, and returns the object or follows NativeAOT's allocation-failure helper. The sources are pinned by `tools/dotnet/runtime-pack/runtime-pack.lock.json` to commit `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`: [AllocFast.asm](https://github.com/dotnet/runtime/blob/9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3/src/coreclr/nativeaot/Runtime/amd64/AllocFast.asm), [GCHelpers.cpp](https://github.com/dotnet/runtime/blob/9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3/src/coreclr/nativeaot/Runtime/GCHelpers.cpp).

### Object route and fallback suitability

The R4 diagnostic object wrapper records a fast-capacity miss and then delegates to the stock `RhpNewFast`; production keeps the same stock NativeAOT object entry. NativeAOT's object rare path also reaches `RhpGcAlloc`. The guest observation therefore confirms that a 136-byte object request can succeed outside `g_guideXosManagedHeap` through the stock object path.

That observation proves allocation returned; by itself it does not prove the fallback object remains rooted, immovable, or unreclaimable for the persistent C188 application lifetime. The stock fallback is a Workstation GC allocation. It has NativeAOT's object layout and GC allocation accounting, and the array rare path has the proper EEType, length, and zero-initialized GC allocation contract. Those properties make it a valid *GC object allocator*. They do not make it a valid extension of the current non-GC arena contract.

The persistent C188 graph includes arrays and resident state allocated from the image-backed arena. The runtime source does not register `g_guideXosManagedHeap` as a GC heap or frozen segment. `persistentCompositeLifecycle` preserves those bytes across callbacks; it does not make references inside that region visible to a moving collector. If a stock fallback object is reachable only through a reference stored in an arena object, a later collection could reclaim or move it without a proven root/update path through that arena. Existing Workstation GC evidence qualifies bounded allocation/refill and reaches the first collection boundary, but collection stops at the unsupported `GCToEEInterface::SuspendEE` contract. It does not qualify this mixed-heap lifetime.

The prior first-refill evidence is useful but bounded: it validates stock NativeAOT arrays and a refill before collection. It does not prove collections, repeated resident ownership across those collections, or safe reuse of a fallback object referenced only by an arena object. The stock array helper also cannot be treated as a no-op retry: after the segment's supported no-collection capacity is exhausted, it can enter the unqualified collection path.

## Strategy decision

| Option | Assessment |
|---|---|
| A — stock array slow path | NativeAOT provides the correct layout-aware slow path, but its GC ownership is not proven compatible with persistent references held in the separate C188 arena. Using it without a complete root/collection contract is unsafe. |
| B — allocation-context refill | A stock refill is a GC operation. The retained no-collection refill evidence is bounded and does not authorize eventual collection or mixed-region lifetime. A new arena-segment/refill policy does not exist in source. |
| C — increase persistent arena | The 64 KiB arena had only 104 bytes left before the first Settings object request, and the earlier C147 evidence records a 64 KiB exhaustion during an allocation-heavy Settings workflow. However, the full NativeAOT Settings construction total and five-resident workload are not measured. Choosing 128 KiB, 256 KiB, 4 MiB, or 8 MiB now would be an unsupported capacity guess; no increase was made. |
| D — size/boundary arithmetic | Not supported by evidence. The 88-byte calculation follows NativeAOT EEType base/component sizing and eight-byte alignment. The observed mismatch is between request size and the current context's remaining bytes, not a demonstrated off-by-one or overflow error. |

The narrow failure cause is proven, but no safe bounded correction is proven. In particular, copying the object fallback to arrays would bypass the capacity fail-fast while introducing the unresolved GC lifetime boundary. Outcome C is therefore the defensible classification.

## Managed-memory budget and evidence limits

| Quantity | Value | Evidence class and limit |
|---|---:|---|
| Configured persistent image arena | 65,536 bytes; 16-byte aligned | Source and diagnostic/production manifests. It is initialized once and preserved across callbacks. |
| Image arena remaining at first Settings object request | 104 bytes | Guest measured at cursor `0x1002085A8`, ending at `0x100208610`. |
| Array request at failure | 88 bytes | Guest measured; complete EEType-derived object size including header/layout and alignment. Array length/type not emitted. |
| Current context capacity at array failure | 64 bytes | Guest measured at cursor `0x100D00F90`, outside the 64 KiB arena. Not a global heap measurement. |
| Settings hosted allocation | About 18,000 bytes | Prior hosted .NET measurement recorded in the R4 attribution note. It is not a NativeAOT measurement and does not include NativeAOT-specific EEType and allocator costs. |
| Notes and registry allocation | Not separately totaled | Five descriptors and Notes residency are established; their cumulative NativeAOT allocation bytes are not in the failed factory trace. The 104-byte pre-object remainder includes prior module, registry, Notes, and Settings type-access effects in that run; it cannot be assigned to Notes alone. |
| Settings object/control/array graph | At least the observed 136-byte object plus later arrays/objects; total unknown | Source lists the Settings model and control graph. Factory termination prevented the successful-allocation summary and final cursor. |
| Other application residents | Unknown | Calculator, File Explorer, Task Manager, and any concurrent resident graphs are not measured together with Settings in this profile. |
| Metadata/alignment and post-startup headroom | Unknown in aggregate | Individual sizes include headers and eight-byte alignment, but cumulative padding, static roots, and workload headroom were not measured. |
| GC-managed region | Stock Workstation GC segment allocation is available to `RhpGcAlloc` | It is a separate region, not an extension of the 64 KiB arena. Collection and mixed-root lifetime remain unqualified. |

The earlier C147 report independently records a 64 KiB resident NativeAOT heap exhaustion when an allocation-heavy Settings suite followed invalid-record startup. This supports that 64 KiB can be insufficient for a larger Settings workflow; it still does not determine the required five-resident capacity. The smallest justified follow-up is an ownership/budget design that either makes the arena visible to a fully supported collector or keeps all persistent objects in a bounded non-GC region whose total five-app demand is measured with NativeAOT allocation telemetry.

## Hosted qualification

The following hosted baseline gates passed. These do not execute the corrected `RhpNewArray` implementation or prove bare-metal allocation behavior because no allocator correction was made.

| Gate | Result |
|---|---|
| C188R12R4 factory host and marker fixtures | PASS — 14 factory cases, 9 synthetic predicates, 9 link-boundary checks |
| Prior C188 host regressions | PASS — C182 15, C185 28, C186 15, C187 22 cases |
| C188 registry/lifetime/reentry hosted suites | PASS — R9 14, R10 14, R11 15, R12R2 launch boundary 8, runner predicates 30, runtime lifecycle 7 |
| Notes keyboard contract checks | PASS — 27 cases |
| C188 output bounds | PASS — 6 cases |
| Settings dispatch predicates | PASS — 12 cases |
| C188 lifetime predicates | PASS — 13 cases |
| Settings handoff predicates | PASS — 37 cases |
| Main managed UI gate, including Notes and Settings/UI | PASS — 40 suites, 969 cases |
| Exact 88-byte/64-byte allocator test; boundary, overflow, exhaustion, fallback, and cross-callback tests A–G | NOT RUN — no safe corrected allocator was implemented; no hosted test can substitute for the NativeAOT guest allocator/lifetime contract |
| NativeAOT Settings factory case H | Hosted factory path passed in the 14-case suite; **native allocator/factory completion not exercised** |

Evidence is retained under:

- `out/dotnet/c188r12r4r3-array-allocation-correction/hosted/factory-baseline-001/20261029-194115-816/`
- `out/dotnet/c188r12r4r3-array-allocation-correction/hosted/main-ui-baseline-001/`
- `out/dotnet/c188r12r4-factory-attribution/hosted/r4r3-baseline-prior-001/`
- `out/dotnet/c188r12r4-factory-attribution/hosted/r4r3-baseline-provenance-001/`

## Retained build, link, and artifact verification

No corrected-profile build was attempted because there is no safe source correction to build. The retained R4R1/R4R2 candidate evidence was rechecked. Both diagnostic and proof-disabled production manifests report `persistentCompositeLifecycle=true` and 65,536 bytes. The diagnostic ELF still has the direct `__Internal` link repair and retains the bounded factory/allocator markers. `readelf -Ws` found none of `FreeLibrary`, `GetProcAddress`, `LoadLibraryExW`, or `SetThreadErrorMode` in the retained diagnostic ELF. The production profile has no R4 factory instrumentation.

| Retained artifact | SHA-256 |
|---|---|
| Diagnostic NativeAOT ELF used by R4R2 | `953E4489B9158317BD8D975C995FFE661B3431855075EE27C893838028487C10` |
| Proof-disabled production NativeAOT ELF | `F962464B35F36195B6A22C24B67467AD19C56BD9F868075490009413F4472C78` |
| Diagnostic kernel | `568B2C084387E45FE3718D422570EE761798542E461674AE8CE14A6B7804E864` |
| Production kernel | `A21A951C8DF337CEA08A7FE93580C556F0AC87F33D1B1766070FFF204B1EA168` |
| R4R1 staged diagnostic ramdisk | `67315FC61B90E162587D3471AE84F0C0C527B19E325897E020A75C5CAE6DE7D4` |
| R4R1 candidate qualification manifest | `2D75552838E5A1C4D00FDBC74C997EDD9355CA8792E8286F34C636C5839031CE` |
| Diagnostic runtime-pack manifest | `337F46F217803EB84799CF8F6BBA9667C9758D03026118389EA144717FAC1B28` |
| Production runtime-pack manifest | `538C81E95E37C15A836BCF1D515EF17072BAAD0BF78E80AB5F7961421ED6C0CC` |
| Diagnostic NativeAOT qualification manifest | `3F5C8B61D9D768388E06A119CC6F357DAB24F73BAFBC3AE318D7BE1CD7558D48` |
| Production NativeAOT qualification manifest | `4B352B97AC87B8A94626A3D1FDB2EF8DD90594A1CC420EDC2ABCE62D8883A547` |
| R4R1 ramdisk staging manifest | `5672E84181C20216E5D75F71C151A5C09023F420176C3D448EB4A775C6FA5896` |
| New R4R3 hosted factory baseline manifest | `234AD1177A725C16CAD9995EE93687162FA997F044DEA17BA92BF0FD2DEB824A` |
| New main managed UI baseline manifest | `02F3DB3AD4D1C287D31EE67EC1978F73535DEB29DEEF2C66E674C086E8C4FE6C` |
| New prior regression manifest | `496664130EBD68750B6A0AE96FD13B3EB89A756167B6823D3E3F42AA1F822825` |
| New C188 lifetime/provenance manifest | `5887A44FFAAAC196890826947FE6148F4C26DAF3FC48CF5461528D8E14D858E6` |

No new diagnostic/production NativeAOT ELFs, kernels, runtime-pack manifests, or staged correction ramdisk exist for R4R3. `git diff --check` and the source ramdisk hash were checked at closeout.

## Future single-guest acceptance plan

No R4R3R1 boot is ready because no corrected candidate exists. Once a safe allocation ownership strategy is implemented and hosted-qualified, prepare exactly one QAC-managed correction-acceptance run under `C188R12R4R3R1` with the following order:

1. Verify exclusive QAC admission and zero foreign/competing sessions.
2. Boot the corrected diagnostic candidate and verify five registered managed descriptors.
3. Launch Notes through the ordinary route; verify its persistent state.
4. Screenshot-verify the Settings button and send exactly one click.
5. Confirm selector-5 dispatch, allocator context, slow-path/refill result, Settings factory return, resident registration, and lifetime completion. Confirm `ManagedSettingsCenter.Launch` and the visible Settings surface if reached.
6. Explicitly verify the 88-byte request did not invoke fail-fast; capture the first subsequent incomplete operation if factory construction still does not complete.
7. Verify registry integrity and Notes/resident-state preservation, release the QAC-owned guest, and preserve raw serial, screenshots, and ownership evidence.

The run must not send exploratory input or start a second boot automatically. A successful 88-byte allocation alone is not Settings acceptance. No QEMU or QAC action was performed in this phase.

## Remaining risks and smallest next action

The immediate fail-fast cause is proven. The proposed stock array slow path has valid NativeAOT array layout and allocation mechanics, but its lifetime contract when referenced by objects in the persistent non-GC arena is not proven, and the collector's first collection suspension remains unsupported. Increasing the arena may be necessary, but the five-resident NativeAOT byte demand and required headroom are unknown.

**Smallest justified next action:** establish one supported persistent ownership model—either qualify collection and make the image-backed arena part of the GC's traced/pinned object space, or measure and size a bounded non-GC arena for the full five-resident workload. Then test the allocation/failure boundary under that model before producing a corrected candidate. Until then, no allocator fallback, arena expansion, or guest-acceptance claim is safe.
