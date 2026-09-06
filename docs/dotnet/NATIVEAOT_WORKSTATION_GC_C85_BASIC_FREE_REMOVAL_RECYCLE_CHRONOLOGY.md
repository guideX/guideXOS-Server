# C85 — NativeAOT basic-free removal, recycle, and consumer chronology

Status: complete at Outcome G / Success Level 3.

## 1. Exact question and result

C85 asked: for each of `EXTRA0`–`EXTRA4`, which production operation removes
or recycles the region after it becomes available on ONE, why SIX retains the
corresponding region through `RestartEE`, and where the earliest semantic
ONE/SIX divergence occurs.

The answer is mixed. `EXTRA0`–`EXTRA3` are removed from
`free_regions[basic_free_region]` by the ordinary region-acquisition path:
`gc_heap::soh_try_fit` requests a new region, `gc_heap::get_new_region(0)`
calls `gc_heap::get_free_region(0)`, and `get_free_region` selects the basic
free-list head through `region_free_list::unlink_region_front`, which calls
the physical `region_free_list::unlink_region` primitive. The region then
becomes a generation-0/generation-tail region.

`EXTRA4` is the authenticated mixed case. Its first removal is also the same
physical unlink primitive, but the consumer is decommit/reclaim chronology:
`gc_heap::distribute_free_regions` calls
`region_allocator::move_highest_free_regions`, which unlinks the region from
the basic list and adds it to `global_regions_to_decommit`; `gc_heap::decommit_step`
later unlinks that global-list entry and calls `decommit_region`. It is not a
basic-list reinsertion.

SIX has the same materialized target births and basic-list insertions, but no
target-specific post-insertion unlink in the equivalent observation window.
The earliest supported semantic divergence is therefore the first
target-specific ONE `REGION-UNLINK` after the final target insertion, with the
consumer class differing for EXTRA4. This is Outcome G (mixed mechanism), not
a supply or candidate-eligibility result.

## 2. Causal progression

* C76 established that basic-size eligibility was common.
* C77–C80 traced region supply but did not identify the final retention
  consumer.
* C82 repaired observer-layout perturbation; its earlier diagnostic footprint
  could change ONE from `1/1` to `0/0`.
* C84 proved that all five normalized target regions exist and are committed
  before GC on both controls, and that SIX does not have five uniquely new
  region births.
* C85 therefore followed the later production removal/retention chronology.
  It identifies ordinary allocation consumption for EXTRA0–EXTRA3 and
  decommit/reclaim consumption for EXTRA4.

## 3. Controls and layout gate

The accepted controls were retained without workload crossover:

| Control | Case | C66 tail | Pre-GC basic | Post-Restart basic | Post-resume basic | Semantic |
| --- | --- | ---: | ---: | ---: | ---: | --- |
| ONE | `15mid8` | 320 | 1 | 1 | 1 | `1/1/1` |
| SIX | `baseline16` | 216 | 1 | 6 | 6 | `1/6/6` |

The intended observer remains the C67 event-store tail-slot architecture.
C85 adds no permanent diagnostic array and no fixed-event-capacity increase.
The C85 list/link fields are scalar serialization fields on existing C67
events; chronology is reconstructed offline. The diagnostic BSS values remain
ONE `0x4F0840` and SIX `0x3947E0`, equal to the C82/C84 baseline; delta is
zero. The C85 event capacity is `0x40`; representative peaks are ONE `0x12`
and SIX `0x0E`, with zero overflow.

## 4. Source audit

The audit used the locked runtime source at
`out/dotnet/c011ec71-pinned-runtime-source/src/coreclr/gc/gc.cpp`, commit
`9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`.

The one physical unlink primitive is
`region_free_list::unlink_region` (`gc.cpp:12931-12974`). Its head wrapper is
`region_free_list::unlink_region_front` (`gc.cpp:12920-12928`). The primitive
rewires the neighboring descriptors, updates list head/tail, clears the
containing-list link, and decrements count and committed/free-size totals.

Audited production paths were:

* allocation: `get_free_region` (`11906-11948`), `get_new_region`
  (`34988-35016`), `soh_try_fit` (`17896-17979`), and `allocate_soh`;
* final-region acquisition: `thread_final_regions` (`34910-34947`) and the
  SIP reserve path around `35174`;
* candidate/gen0 supply: `try_get_new_free_region` (`21345-21371`),
  `extend_soh_for_no_gc` (`23720-23758`), and planner `get_new_region(0)`;
* surplus/list migration: `remove_surplus_regions` (`12737-12747`),
  `add_regions` (`12750-12765`), and `transfer_regions` (`13036-13080`);
* decommit/reclaim: `region_allocator::move_highest_free_regions`
  (`4270-4304`), `distribute_free_regions` (`13276`, `13380-13393`,
  `13554`), and `decommit_step` (`44483-44503`);
* producer return: `return_free_region` (`11860-11873`); and
* allocation-context maintenance: `fix_allocation_contexts` (`7963`) and
  `fix_allocation_context_heaps` (`8014`), neither of which directly unlinks
  the target from the basic list in this run.

The same unlink primitive can serve allocation, candidate, surplus, and
decommit consumers. The consumer is distinguishable here by the source event
and list identity: basic-list acquisition is followed by generation-tail
ownership, while EXTRA4 changes list identity to
`global_regions_to_decommit` and is followed by `decommit_step`.

## 5. Five-region chronology

All targets are basic-size (`0x100000`) and use the C84 normalized ranges.
Ordinals are run-local. The full compact records are in
`out/dotnet/c011ec85-basic-free-removal-recycle/offline-chronology/target-chronology.md`.

| Region | ONE final insert | ONE first removal | ONE consumer/result | SIX corresponding event | SIX retained? |
| --- | --- | --- | --- | --- | --- |
| EXTRA0 `0x1400000`, descriptor `0x104010DA0` | `0xAB`, basic count `0->1`, state `8->A` | `0xC3`, basic count `4->3`, state `A->8` | `unlink_region_front` -> `unlink_region`; `get_free_region(0)` `0xC5`, `get_new_region(0)` `0xC6`; gen0 tail | insert `0xB9`; no target removal | Yes |
| EXTRA1 `0x1500000`, descriptor `0x104010E48` | `0xAC`, count `1->2`, state `8->A` | `0xC8`, count `3->2`, state `A->8` | same allocation chain; `get_free_region(0)` `0xCA`, `get_new_region(0)` `0xCB` | insert `0xBA`; no target removal | Yes |
| EXTRA2 `0x1600000`, descriptor `0x104010EF0` | `0xAD`, count `2->3`, state `9->B` | `0xCD`, count `2->1`, state `A->8` | same allocation chain; `get_free_region(0)` `0xCF`, `get_new_region(0)` `0xD0` | insert `0xBB`; no target removal | Yes |
| EXTRA3 `0x1700000`, descriptor `0x104010F98` | `0xAE`, count `3->4`, state `D->F` | `0xB4`, count `9->8`, state `E->C` | `unlink_region_front` -> `unlink_region`; `get_free_region(0)` `0xB6` | insert `0xBC`; no target removal | Yes |
| EXTRA4 `0x1A00000`, descriptor `0x104011190` | corrected rerun `0xAB`, basic count `6->7`, state `9->B` | `0xB2`, basic count `6->5`, state `A->8` | `move_highest_free_regions` links to global decommit at `0xB3`; `decommit_step` unlinks at `0xB7` | insert `0xBF`; no target removal or decommit transfer | Yes |

For EXTRA0–EXTRA3, the final basic insertion is followed by a target
`BASIC_REMOVE` and no target reinsert. For EXTRA4, the first subsequent
operation is the basic removal; the next two operations are an explicit
non-basic-list transfer and global decommit-list unlink. This is why the
five-region result must not be summarized as five ordinary allocations.

## 6. List accounting and candidate bridge

The authentic C77 summaries reconcile the accepted controls:

* ONE: `1 + 15 basic insertions - 15 basic removals + 0 basic reinsertions =
  1` at `RestartEE`, and remains 1 after resume.
* SIX: `1 + 13 basic insertions - 8 basic removals + 0 basic reinsertions = 6`
  at `RestartEE`, and remains 6 after resume.

The one EXTRA4 global-decommit relink is deliberately excluded from basic
reinsertion arithmetic. The raw list pointers and count transitions prove
that distinction.

None of the five targets entered the separate candidate genealogy. None was
selected as a candidate and none was rejected by candidate eligibility. The
later downstream C70 observation recorded `get_new_region(0)` with
`requestedGeneration=2`, `condemnedGeneration=2`, no normal refill, and OOS
reason `5` on both sides. Its raw `commit_failed` operand was `0` on ONE
(`candidateCount=1`, result 1) and `1` on SIX (`candidateCount=0`, result 0);
the C73 finish-time reconciliation was zero. These are downstream context,
not target-specific candidate rejection.

Accordingly B02 was not evaluated and remains `STILL_PREMATURE`: C85 did not
isolate an authentic target reaching a decisive candidate eligibility test
with a specific rejection operand.

## 7. Integrity and mutation controls

C18, code manager, `FindMethodInfo`, authentic root scan, mark closure,
planner authenticity, and survivor integrity all passed. C85 invariant
failures, sensitive diagnostic allocations, overflow, fail-fast, and page
faults were all zero.

No production GC, allocator, list, region, generation, candidate, planner, or
promotion behavior was changed. No survivor, root, or region was fabricated.
The direct C85 call-site observer remains compile-disabled to preserve the
accepted C84/C83 layout; the active C85 result uses existing C67/C77 source
events and offline reconstruction.

## 8. Evidence and next milestone

Evidence root:
`out/dotnet/c011ec85-basic-free-removal-recycle/`, separated into
`source-audit`, `layout-gate`, `per-target-discovery`,
`final-confirmation`, and `offline-chronology`.

Final confirmation used three fresh boots for each control. All six semantic
results agreed with the accepted controls; serial differences are expected
run-local hashes, not semantic nondeterminism.

The next smallest milestone is C86: isolate why ONE performs the extra
ordinary acquisitions for EXTRA0–EXTRA3 and separately trace the exact
decommit/reuse destination predicate for EXTRA4. B02 should remain deferred
unless C86 produces the specific authentic candidate rejection event required
by its gate.
