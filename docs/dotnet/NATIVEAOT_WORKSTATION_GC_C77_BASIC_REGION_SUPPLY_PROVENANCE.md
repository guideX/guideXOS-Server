# NativeAOT workstation GC C77: basic-region supply provenance

## Result

C77 is complete as a bounded provenance experiment. It is **Outcome G / Level 1**: the C67-backed observer is clean, both accepted controls reproduce their C76 result across three confirmation boots, and the earliest stable 1-versus-6 basic-free count difference is visible at checkpoint 7. The final accepted post-Restart/post-resume result remains ONE=1 versus SIX=6.

The experiment does not identify the earliest production birth, split, coalesce, context-ownership, or reclaim event for the five-region difference. The correct causal classification is **UNRESOLVED**. B02 was not run because no later candidate event was isolated strongly enough to justify it.

## Controls and observed results

| Control | Accepted input | C76 final basic | C77 Level | C77 events | Max observed regions |
| --- | --- | ---: | ---: | ---: | ---: |
| ONE | retained=15; object=0x10E80; promoted/live=0xFD980 | 1 / 1 | 1 | 218 (0xDA) | 14 |
| SIX | retained=16; object=0x10018; promoted/live=0x100180 | 6 / 6 | 1 | 208 (0xD0) | 13 |

Each side has one discovery boot and three accepted confirmation boots. Semantic values agree across all three confirmations per side. The C76 predicate is unchanged: region_size == BASIC_REGION_SIZE.

The detailed 192-item closeout is generated at:

- out/dotnet/c011ec77-basic-region-supply-provenance/c77-final-report.md
- out/dotnet/c011ec77-basic-region-supply-provenance/c77-final-manifest.json
- out/dotnet/c011ec77-basic-region-supply-provenance/c77-region-census.json
- out/dotnet/c011ec77-basic-region-supply-provenance/c77-comparison-table.md
- out/dotnet/c011ec77-basic-region-supply-provenance/c77-extra-five-table.md
- out/dotnet/c011ec77-basic-region-supply-provenance/c77-source-audit.md

## What C77 observes

C77 is source-accounting only. It reuses the accepted C76 managed control path and the existing C67 lifecycle observer. It emits bounded records for:

- region-count snapshots;
- list add/remove and free/source transitions;
- region create/commit;
- expansion;
- decommit/reclaim;
- generation and ordinal-preserving event mapping.

The event capacity is 2048 and the snapshot capacity is 1024. Accepted runs report zero event overflow, snapshot overflow, invariant failures, sensitive diagnostic allocations, fail-fast events, and page faults. Explicit zero markers for split, coalesce, context acquire, and context release mean **not observed by this bounded observer**, not proven absent from production.

The earliest repeatable bounded difference is:

| Checkpoint | ONE | SIX | Interpretation |
| --- | ---: | ---: | --- |
| 3 | 14 total / 0 basic-free | 13 total / 0 basic-free | no comparable basic-free count at the end of the checkpoint |
| 5 | 14 total / 0 basic-free | 13 total / 0 basic-free | no comparable basic-free count at the end of the checkpoint |
| 7 | 8 total / 1 basic-free | 5 total / 6 basic-free | earliest stable 1-versus-6 bounded difference |
| 8 | 7 total / 2 basic-free | 13 total / 0 basic-free | later lifecycle transition; not the final C76 aggregate |
| 9 | 8 total / 1 basic-free | 13 total / 0 basic-free | later lifecycle transition; not the final C76 aggregate |
| C76/C77 post-Restart | 1 basic | 6 basic | accepted final 1-versus-6 result |

These snapshots do not close checkpoints 1, 2, 4, 10, 11, or 12 as a region-identity ledger. Therefore C77 can show where the bounded observations first differ, but cannot map the five extra SIX regions backward to source-backed births or prove their pre-GC ownership.

## Locked-source audit

The source audit is against the C68 locked NativeAOT runtime under out/dotnet/c68-locked-nativeaot-runtime-2/src/coreclr/gc/.

- Creation/allocation: region_allocator::allocate_region, gc_heap::make_heap_segment, gc_heap::init_heap_segment, gc_heap::get_new_region, and gc_heap::allocate_new_region.
- Expansion: gc_heap::expand_heap, get_new_region, and allocate_new_region.
- Free-pool/list operations: return_free_region, get_free_region, region_free_list::add_region_front, add_region_in_descending_order, unlink_region, and get_region_kind.
- Retirement/decommit: find_first_valid_region, thread_final_regions, decommit_region, and decommit_ephemeral_segment_pages.
- Split: no production region-split function was observed; the locked source contains a TODO that SOH should be able to split a large region.
- Coalesce: gcpriv.h documents allocator free-block coalescing, but no production region-coalesce event was observed.
- Context ownership: acquire/release was not instrumented; the nearest observed transitions are get_free_region and return_free_region.

No production allocator, planner, region-list, candidate, policy, survivor, or root mutation was added. Ordinary kernel/ESP restoration remained active, proof artifacts were inactive after cleanup, and the smoke watchdog stopped only C77-owned QEMU processes.

## Closeout

The implementation is in:

- scripts/smoke-nativeaot-gc-single-thread-suspend-ee-qemu.ps1
- tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_platform.cpp
- scripts/dotnet/Invoke-C011EC77BasicRegionSupplyProvenance.ps1

The local commit subject is Trace NativeAOT basic region supply provenance. The change is intentionally not pushed. The next smallest milestone is C78: add a bounded, source-backed pre-GC region identity/ownership census at the first allocation-boundary divergence while preserving the accepted promotion controls.
