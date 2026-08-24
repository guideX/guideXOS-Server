# NativeAOT Workstation GC dead-object reclamation

## Result

C011EC38 is an evidence-only blocker at success level 0. The C37 target is
logically dead and its short-weak handle is cleared, but the active locked
Workstation plan chooses compaction for the condemned-generation-1
collection. Therefore the authentic noncompacting sweep/free-space hooks were
not entered, no allocator-visible free span was attributed to the target, and
no post-GC allocation reuse claim is made.

This distinction is intentional: a cleared weak slot is not treated as proof
that the target's physical storage was published to an allocator free list.
No diagnostic code created a free object, changed a mark, changed an object
header, moved an allocation cursor, or called a free-list API.

## Locked identity and predecessor

The proof preserved the locked identity:

| Item | Value |
| --- | --- |
| NativeAOT | 9.0.0 |
| Architecture | AMD64 |
| GC | Workstation |
| Interfaces | 5.3 / 2 |
| Locked runtime source | `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3` |
| C37 predecessor | `a4a4eac457e3689ff9faea5a4ff6b780a53afb0e` |

All three fresh QEMU 11.0.0 boots retained the C37 boundary: Collection 1
relocated the target and resumed managed execution; Collection 2 condemned
generation 1, found zero strong target reachability, left the target
unmarked, cleared the short-weak slot, returned through `RestartEE`, resumed
managed execution, and emitted `C011EC37-MANAGED` and `C011EC37`.

The C37 completion marker reports `collection2Compacting=0`. C37's stored field
is populated from the earlier request-side `collectionCompactingMode` value.
C38 added a scalar observer at the locked `gc_heap::plan_phase` decision. The
actual locked decision for the same condemned-generation-1 collection was
`compacting=1` on all three boots. This is the path discrepancy that blocks
the requested sweep proof; it is not silently relabeled as C37 success.

## Target extent

C37 supplied the current-run identity:

| Item | Value |
| --- | --- |
| Original target | `0x100A01F38` |
| Relocated/dead target | `0x100901F50` |
| Managed type | `byte[64]` |
| Logical payload | 64 bytes |
| Array header | `0x10` bytes |
| Locked base size | `0x18` bytes |
| Component size | 1 byte |
| Element count | 64 |
| Raw size | `0x18 + 64 = 0x58` bytes |
| Alignment | 8 bytes on AMD64 |
| Actual allocated size | `0x58` (88) bytes |
| Target start | `0x100901F50` |
| Exclusive target end | `0x100901FA8` |
| EEType | `typeof(byte[])`; method-table pointer was intentionally not fabricated because the responsible sweep callback was not entered |
| Mark before sweep | Unmarked, from C37's authentic liveness record |
| Weak slot | `0x1040213F8` |
| Weak slot after C2 scan | Null |

The extent calculation uses the locked NativeAOT array layout and the active
AMD64 `Align` rule. It does not equate the 64-byte payload with the heap
extent. The target range is the interval `[0x100901F50, 0x100901FA8)`.

The Workstation build has one GC heap, so the logical heap number is 0. A
specific segment address, segment end, allocation boundary, and brick/card
owner were not recorded because C38's authentic sweep entry did not run. No
arbitrary segment scan was substituted for that missing structural callback.
The C37 generation evidence identifies the target as generation 1.

## Locked reclamation semantics

The locked source exposes two relevant WKS implementations:

1. The active runtime-pack command line does not define `USE_REGIONS`, so the
   classic segment sweep route is `gc_heap::make_free_lists` at locked
   `gc.cpp:35476`.
2. The source also contains the region route
   `gc_heap::sweep_region_in_plan` at `gc.cpp:35233`; that route is guarded by
   `USE_REGIONS` and was retained as a source-guarded alternate probe.

For the active classic path, the authentic chain is:

`make_free_lists` -> `make_free_list_in_brick` (`gc.cpp:35941`) ->
`thread_gap` (`gc.cpp:36032`) -> `make_unused_array` (`gc.cpp:35765`) ->
generation allocator publication.

`thread_gap` represents a sufficiently large gap by setting the production
free-object header and threading the range into the generation allocator;
smaller gaps contribute to generation free-object accounting. The allocator
selection path is `allocator::thread_item`/`a_fit_free_list_p` and the segment
tail fallback is `a_fit_segment_end_p`. The region alternative uses
`heap_segment::thread_free_obj` (`gc.cpp:35195`),
`thread_final_regions`/`find_first_valid_region` (`gc.cpp:34809`), and
`allocator::thread_sip_fl`.

These are the locked-runtime structures C38 was prepared to observe:

- free-object headers created by production `make_unused_array`;
- free-list entries and byte totals in the generation allocator;
- region free-object/free-list fields when `USE_REGIONS` is active;
- segment-local allocation and generation bookkeeping;
- brick updates performed by the production sweep code.

The C38 observers only read scalar metadata and emit serial evidence. They do
not create or thread any of these structures.

## What the three boots proved

Each run used the restored C37 workload and an eight-allocation C38 cap. The
direct observer was inserted before the locked `plan_phase` compact/sweep
branch. The final path for the target's condemned-generation-1 collection was:

```text
PATH marker=C011EC38-PATH compacting=00000001 condemnedGeneration=00000001
```

Consequently, none of these C38 events occurred in any boot:

- `C011EC38-PREFLIGHT`;
- `C011EC38-RECLAIMED`;
- `C011EC38-MANAGED`;
- `C011EC38`.

The C38 sweep entry, dead-range observer, free-gap observer, generation
publication observer, and allocator-reuse observer all remained uncalled for
the target. The bounded allocation test therefore did not run as a reuse
claim, and no Collection 3 was intentionally triggered.

The three serial hashes are:

| Boot | Serial SHA-256 |
| --- | --- |
| first-run | `FE59BE0E9A0F6765A3C851C8D8FD997DF0F8E94C25A79232F5F67EB79AB41069` |
| repeat-1 | `5BE8CD1FD415F465FE4710944E271F103133133ABC0A101768BD274C69A6C0F1` |
| repeat-2 | `8908B7A9D9E263DDD105123D3414C5403EAF607D957F1D8E6A3BC6BE127C8FF3` |

The serial bytes vary in expected QEMU/runtime addresses and interrupt-text
placement, but the semantic path agrees: C37 completes and C38 observes
compaction for generation 1 with no sweep/reclamation marker.

## Artifact hashes and restoration

The proof artifact hashes from the three-boot evidence root were:

| Artifact | SHA-256 |
| --- | --- |
| Proof kernel | `8D7EF070533EDBBE0CABD39F9726095A58986ECFC363931289BC9A6DAD528C14` |
| PE | `FC359AF23838555481C06B5A784419B477B13D5B9BB7AA4812CC9C05DD90CA0F` |
| ELF | `0B3DBFA6619A084D22E599190AC951198D6319700F7154942074454138E23470` |
| MAP | `ED1610A88D14280EBECDD10BDAA57916E6A64E8DE2ECA58549F73B05DBF31D81` |

The ordinary kernel and ESP were restored in the harness `finally` path. A
fresh post-run check found both ordinary source-state artifacts equal to:

```text
75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6
```

No repository-owned QEMU process remained after cleanup. Unrelated virtual
machines were not targeted.

## Retained guards and checks

The C38 changes retain the existing C19-C37 chronology and source/linker
guards, complete stack walk, mark-closure checks, weak live/dead semantics,
Collection-1 relocation and root/handle updates, two completed collections,
both `RestartEE` boundaries, managed resumption, PE-to-ELF conversion,
PowerShell parsing, linker/table validation, ordinary kernel boot smoke, and
`git diff --check`.

The suspended path retains zero diagnostic allocations, zero managed re-entry,
zero mark mutation, zero object-header mutation, zero free-list mutation, and
zero arbitrary heap scans. Safe stopping remained fail-closed when the C38
preflight predicate could not be established.

## Next smallest milestone

Correct the C37 production-path metadata so its compact/sweep field records the
locked `plan_phase` decision, or supply a naturally noncompacting condemned
generation-1 workload under the same locked identity. Then rerun the existing
C38 sweep observer. Only after `make_free_lists`/`thread_gap` (or the active
region equivalent) structurally covers `[0x100901F50, 0x100901FA8)` should
`C011EC38-PREFLIGHT`, `C011EC38-RECLAIMED`, and an allocator reuse level be
considered.

This milestone does not claim physical reclamation or reuse, and it does not
require exact-address reuse.
