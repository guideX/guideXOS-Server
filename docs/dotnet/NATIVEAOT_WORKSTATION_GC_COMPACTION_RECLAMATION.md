# NativeAOT Workstation GC Compaction Reclamation

## C011EC40 result

C011EC40 proves Outcome A: authentic Workstation GC compaction reclaims the dead C37 target's storage from the live layout and publishes allocator-visible capacity. The validated success level is Level 1. A bounded post-GC allocation probe was also run, but the allocator selected another valid region; therefore this milestone does not claim Level 2 or direct former-dead-space reuse.

The proof used three fresh QEMU 11.0.0 boots. The semantic result was stable on all three boots.

The locked identity was preserved exactly:

- NativeAOT 9.0.0
- AMD64
- Workstation GC
- GC interfaces 5.3 / 2
- runtime source `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`

## Why C40 follows C39

C38 looked for sweep/free-list reclamation and did not observe a sweep callback. That was the wrong model for this workload. C39 established the production Collection-2 decision:

```text
gc_heap::plan_phase
  -> gc_heap::decide_on_compacting
  -> final if (should_compact)
  -> relocate_phase -> compact_phase
```

The locked C39 planner evidence remains:

```text
condemnedGeneration = 1
plannerReason = reason_alloc_soh (0)
heap = 0
maximumGeneration = 2
fragmentation = 991800
fragmentationThreshold = 80000
fragmentationBurden = 1.0
fragmentationBurdenThreshold = 0.5
fragmentationExceeded = 1
compactionMechanism = compact_high_frag (1)
finalDecision = COMPACT
actualPhase = relocate_phase -> compact_phase
```

No planner policy was changed and no sweep was forced.

## Locked WKS compaction model

The authoritative source path in the locked runtime is:

| Responsibility | Locked WKS function/source location |
| --- | --- |
| Plan the collection | `gc_heap::plan_phase`, `gc.cpp:32553` |
| Decide compact versus sweep | `gc_heap::decide_on_compacting`, `gc.cpp:44900-45104` |
| Dispatch the selected mechanism | final `if (should_compact)`, `gc.cpp:34081` |
| Read a plug's dead gap | `node_gap_size`, `gc.cpp:30603` in the generated locked source |
| Read a plug's relocation distance | `node_relocation_distance`, `gc.cpp:30567` in the generated locked source |
| Move a live plug | `gc_heap::compact_plug`, `gc.cpp:37187` |
| Walk the plug/tree context and close gaps | `gc_heap::compact_in_brick`, `gc.cpp:37359` |
| Run compaction over the condemned segments | `gc_heap::compact_phase`, `gc.cpp:37456` |
| Publish post-compaction generation/segment bounds | `gc_heap::fix_generation_bounds`, `gc.cpp:34528` |

The plug representation is the locked GC plug/tree metadata, not a guideXOS heap abstraction. `node_gap_size(tree)` reads the `plug_and_gap` metadata immediately before a plug. `node_relocation_distance(tree)` reads the relocation field in `plug_and_reloc`, with the low flag bits removed. `compact_in_brick` tracks the previous live plug in `compact_args::last_plug`, obtains the intervening gap from `node_gap_size`, and calls `compact_plug`. `compact_plug` calculates the destination as `plug + args->last_plug_relocation` and copies the live plug with the production `gcmemcopy` path.

The authoritative segment state is the WKS `heap_segment` metadata: `mem`, `allocated`, `committed`, `reserved`, `used`, `plan_allocated`, and generation number. Generation allocation state is represented by the WKS generation helpers (`generation_allocation_start`, `generation_allocation_pointer`, `generation_allocation_limit`, and their plan equivalents). In this NativeAOT `USE_REGIONS` build, the publication relevant to this proof is `heap_segment_allocated(region) = heap_segment_plan_allocated(region)` in `fix_generation_bounds`; the post-publication region tail is bounded by the committed pointer. The segment owner was resolved through the authentic `WKS::gc_heap::find_segment` path exposed by `guidexos_nativeaot_gc_describe_segment`.

## Current-run target and segment evidence

The historical C37 logical allocation was `0x100A01F38`. Collection 1 relocated it to the current-run target below. These addresses are evidence from the run, not constants used by the harness.

| Fact | Evidence |
| --- | --- |
| Original target | `0x100A01F38` |
| Relocated/dead target | `0x100901F50` |
| EEType | `0x10278020` |
| Payload | `0x40` / 64 bytes |
| Aligned allocation size | `0x58` / 88 bytes |
| Exact extent | `[0x100901F50, 0x100901FA8)` |
| Generation | 1 |
| Mark state | unmarked |
| Owning heap | 0 |
| Segment pointer | `0x104010668` |
| Segment memory/start | `0x100900028` |
| Segment committed end | `0x100943000` |
| Segment reserved end | `0x100A00000` |
| Weak slot | `0x1040213F8` |
| Weak value after Collection 2 | null |

C37 regression evidence in each boot retained no strong root, no graph reachability, no Promote, no mark, and no stale weak reference. Collection 2 completed, `RestartEE` returned, and managed execution resumed.

## Dead gap, exclusion, and hole closure

The target was structurally inside a dead gap reported by the compacting plug walk:

```text
dead gap = [0x100901F50, 0x100901FC0)
gap size = 0x70
target   = [0x100901F50, 0x100901FA8)
```

Thus the target's complete `0x58` extent is within the larger dead gap. The aggregate dead space removed from the compacted plug layout was `0xD0`; C40 attributes only `0x58` to this target and does not claim that all `0xD0` came from it.

The target had no live-plug membership, no relocation callback, and no copy/move operation:

```text
targetLivePlug = false
targetRelocationCallbacks = 0
targetCopyMoves = 0
```

The neighboring live plug supplied direct hole-closure evidence:

```text
live source      = [0x100901FC0, 0x100911FD8)
live destination = [0x100801F20, 0x100811F38)
shift            = 0x1000A0
```

The source begins after the target's dead extent, while the destination is lower than the source. This is a live plug moving across the dead gap, not the dead target being relocated. The destination did not overlap the former target extent in the captured neighboring movement. The proof therefore distinguishes logical death, hole closure, and recovered capacity without relying on stale bytes at the old address.

## Frontier and free-tail publication

The owning segment's compacting frontier changed as follows:

```text
old allocated/frontier = 0x100942068
new allocated/frontier = 0x100900028
frontier delta         = 0x42040
```

The published allocator-visible tail was:

```text
free tail start = 0x100900028
free tail end   = 0x100943000
free tail size  = 0x42FD8
```

The frontier was valid, aligned, reduced, and published by the authentic compacting path. This is the physical reclamation proof: the target was omitted from the live layout, later live data closed over its gap, and the segment's post-compaction allocation state exposed the remaining tail.

C011EC40 emitted both required runtime markers only after the relevant state had been reached:

```text
C011EC40-PREFLIGHT
C011EC40-RECLAIMED
C011EC40
```

## Optional allocation probe

After normal Collection-2 completion, `RestartEE`, and managed resume, the managed harness attempted eight ordinary `new byte[64]` allocations. Each requested payload was `0x40` and each aligned allocation size was `0x58`. No Collection 3 was triggered. The last captured allocation was `[0x100A102C0, 0x100A10318)`.

The probe reported:

```text
allocationConsumedTail = false
formerDeadSpaceOverlap = false
exactHistoricalTargetReuse = false
collection3Triggered = false
```

The ordinary allocator selected another valid region. This is Outcome H for the optional reuse experiment, not a failure of compaction reclamation and not evidence against the published tail. The internal allocator sub-path was intentionally not inferred; the diagnostic records the ordinary managed boundary as `allocationPath=0xFFFFFFFF`.

## Integrity and sensitive-path rules

The compacting diagnostics were bounded scalar observers. They did not scan arbitrary heap memory, allocate managed objects, create dynamic strings or containers, mutate planner state, mutate segment/frontier pointers, or move objects manually while EE was suspended. Compact integrity failures, allocator integrity failures, stale target references, and relocation resurrection counts were all zero. The weak slot remained null after managed resume.

The proof stops before any Collection-3 trigger. No target bytes after compaction were inspected as an object.

## Three-boot validation and hashes

Evidence root for the final three-boot validation:

`out/dotnet/c011ec40-compaction-reclamation/run-20260824-062549085`

QEMU version: `QEMU emulator version 11.0.0 (v11.0.0-12122-ga4bb4b10c9)`.

Serial SHA-256 values, in boot order:

1. `8FADF6F875B43F66D01BFFE341DA998BE6B3DF2C683033D9B80BE188C4A33BE8`
2. `5CD5CF8C251A552AD56016BB258722D6244B1C810993C3F62FA0AFCFF7E92703`
3. `FDDDCC778386A76E050E016EA153E660E3F0A3C4747CDF5D3D39FCA27128A687`

Proof artifact hashes:

```text
proof kernel = D6C9992A3FC521F90F701136E4089707A4414BBA1FBF39EAD8A5B4A6B04AA082
PE           = 895777C5BE4C9D444BD9A79ACBF5776ECE143C15D9265D6C3C9D75C27B2B4C75
ELF          = 86EE871EDC46BD962A180B0B7D24273DBE366D83564AD6635E270C43B8CAC769
MAP          = 2F26CCB97BF40BD7241E477B09B88E9AF5D6B883D97219CA9C65A80AC8F139B8
```

The three-run manifest reports stable semantic agreement for C37 two-cycle completion, C39 final COMPACT, target death, target exclusion, dead-gap membership, hole closure, frontier reduction, and allocator-visible tail publication.

## Regression and restoration

C19-C39 chronology guards, C36 same-handle transition, C35 weak relocation, C34 managed-root relocation, the C39 planner provenance, the PE-to-ELF converter, linker/table guards, PowerShell parsing, source/linker guards, and ordinary boot smoke were retained. `git diff --check` passed.

The ordinary source-state kernel and ESP were restored after testing. Both restored SHA-256 values are:

`75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6`

The harness terminated only the repository-owned QEMU processes it started. No push was performed.

## Next milestone

The smallest follow-up is optional allocator-path provenance: instrument the normal post-GC allocation boundary sufficiently to identify which allocator region served the bounded allocations, without requiring Collection 3 or exact historical-address reuse. C011EC40 itself is complete at Level 1.
