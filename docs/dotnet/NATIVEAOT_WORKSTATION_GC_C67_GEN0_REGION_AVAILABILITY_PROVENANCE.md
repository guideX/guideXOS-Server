# C011EC67 — NativeAOT Workstation-GC gen0 region-availability provenance

## Result

C011EC67 is an observational, proof-mode-only follow-up to C011EC66.  The
unchanged C66 workload was run with `C64Variant=W3`, `C66Strategy=P2`, and
`C66TailAllocations=216`; C67 did not change allocation pressure, generation
policy, region state, list links, commit behavior, requested generation, or
the OOS path.

The final result is **Outcome A / Success Level 3**.  Three fresh QEMU boots
agreed on the semantic result.  C67 source-proves the authoritative candidate
container, its producer/consumer lifecycle, the earlier headroom-only refill,
and the final source-backed event that removes the last basic free-region
candidate before the decisive `get_new_region(0)` call.  It does not claim a
workload intervention or perform C68.

Evidence run:

`out/dotnet/c011ec67-gen0-region-availability-provenance/run-20260901-192004410`

The proof artifact was restored immediately after validation; the ordinary
kernel and ESP both returned to the canonical hash.

## C66 control and exact C67 question

C66 reproduced the control chain: authentic promotion, gen1 debit,
`RestartEE`, managed resume, ordinary post-debit pressure, context exhaustion,
`soh_try_fit`, `get_new_region(0)`, candidate count zero, null region result,
`commit_failed=1`, full/OOS preemption, `reason_oos_soh (5)`,
`last_gc_before_oom=1`, `GarbageCollectGeneration(2)`, and
`generation_to_condemn(n_initial=2)`.  Its decisive aligned request was
`0x4018`; the C66 allocation-count experiment did not alter this immediate
zero-candidate topology.

C67 asks: **which production region structure does `get_new_region(0)` use,
how does a region enter and leave it, and which exact lifecycle event leaves
that structure empty at the decisive post-debit refill?**

## Candidate-container source model

The authoritative container is `gc_heap::free_regions[basic_free_region]`.
The member is declared in the locked `src/coreclr/gc/gcpriv.h` at line 3885;
its `region_free_list` implementation and list fields are at lines 1417–1464.
It is a per-heap `region_free_list`, not a global tree or array and not a
generation-specific list.  `basic_free_region` is list kind `0`.

The candidate count emitted by C67 is
`free_regions[basic_free_region].get_num_free_regions()`.  It counts regions
discoverable in that basic list, regardless of their prior generation number.
`get_free_region(0)` takes the list entry and initializes/assigns it for gen0;
the requested generation is therefore applied by the consumer after list
selection.  The per-generation counts in C67 snapshots are a separate view of
the generation-linked region lists and must not be confused with the basic
free-list candidate count.

The production chain is:

`gc_heap::return_free_region`
→ `region_free_list::add_region_descending` / `add_region_front`
→ `gc_heap::get_free_region(0)`
→ `region_free_list::unlink_region_front`
→ `gc_heap::get_new_region(0)`
→ `soh_try_fit` / allocation.

Active, allocation-owned, and next regions are not basic free-list candidates.
A mapped region is not necessarily a candidate.  A region must already be
discoverable in the basic free list for the reuse branch; the list-selection
path does not itself turn an arbitrary mapped region into a candidate.  A
newly allocated region is a separate `allocate_new_region` branch.

## Region lifecycle established by the locked source

The exact audited functions are:

| Operation | Locked source location | C67 interpretation |
| --- | --- | --- |
| Return/recycle a region | `gc.cpp:11860-11904` | Clears region info and adds the descriptor to the basic free list. |
| Add at front | `gc.cpp:12840-12861` | Links a region into a free list and updates list accounting. |
| Add in descending order | `gc.cpp:12862-12919` | Links a reusable descriptor according to the production ordering. |
| Remove/unlink | `gc.cpp:12920-12974` | Removes a descriptor and updates list accounting; the final C67 loss is this operation. |
| Select a free region | `gc.cpp:11906-12020` | Reads the basic-list head; if absent, calls `allocate_new_region`. |
| Acquire a new region for a generation | `gc.cpp:34988-35017` | Calls `get_free_region`, then links a returned region to the generation tail. |
| Allocate a new region | `gc.cpp:35019-35046` | Uses the global region allocator and builds a heap segment. |
| Commit a new segment | `gc.cpp:12313-12350` | `make_heap_segment` performs the initial `virtual_commit`. |
| Assign generation | `gc.cpp:12353+` | `init_heap_segment` sets the region generation. |
| Fit allocation / request more space | `gc.cpp:17896-17979`, `17982+`, `18949+` | `soh_try_fit` can use existing segment capacity or request another region. |
| Allocate another free region | `gc.cpp:21345-21371` | `try_get_new_free_region` checks the local basic list, otherwise attempts a new region. |
| Reclassify/recycle after collection | `gc.cpp:34702-34807`, `34528+`, `34809+` | `find_first_valid_region`, `fix_generation_bounds`, and `thread_final_regions` rethread/reclassify regions and return empty regions when applicable. |
| Global allocator accounting | `gc.cpp:3960+` | Supplies allocator free bytes and used-region counts; visibility here does not imply free-list candidacy. |

The C67 hooks observe these operations using fixed native records.  Formatting
is deferred until the safe completion path.  No diagnostic allocation is made
inside a list mutation, allocator, GC-planning, or EE-suspension callback.

## C67 chronology and decisive comparison

The bounded recorder retained 364 candidate snapshots and 220 lifecycle events
in the first boot.  It retained no overflow and no invariant failure.  The
candidate snapshot checkpoints cover startup/normal control, promotion and
generation-boundary work, post-resume `soh_try_fit`, pre/post region-search
states, and repeated post-resume refill observations.  Snapshot ordinals and
region-event ordinals are independent counters.

Earlier normal refill:

* `requestSize=0x10018`.
* `activeRegion=0x104010710`.
* `availableCapacity=0x1868` after the successful operation.
* `candidateCount=1` was visible in the bounded control snapshot.
* The source is explicitly `existing-region-headroom`.
* It did not require a new region and did not prove candidate consumption.

This is the important separation: the earlier success was supplied by unused
capacity in the active region.  Candidate availability was not the cause of
that success.

Decisive post-debit refill:

* `generation=0`.
* `requestSize=0x4018` (the native `get_new_region` size argument is zero in
  this locked call shape; C67 carries the authoritative aligned C65/C66
  request into the marker).
* `activeRegion=0x104011388`.
* `nextRegion=0`.
* basic free-list candidate count `0`.
* `get_new_region(0)` result `0`.
* `commit_failed=1`.
* source branch `3`, meaning no basic-list candidate and no successful new
  region was returned by the observed branch.

The final nonzero candidate marker was:

`C011EC67-LAST-NONZERO-CANDIDATE observed=1 eventOrdinal=0x16C checkpoint=7 candidateCount=6`

That event ordinal is a snapshot ordinal.  The final capacity loss then
occurred in the retained region-event stream as the basic list changed from
one entry to zero.

The first zero marker was:

`C011EC67-FIRST-ZERO-CANDIDATE observed=1 eventOrdinal=0x5 checkpoint=0 candidateCount=0`

This first transition was transient: later lifecycle work repopulated the
basic list.  It is the earliest observed region-availability divergence, not
the final decisive loss.

## Exact loss and first divergence

`C011EC67-LAST-USABLE-GEN0-CAPACITY-SOURCE = existing-region headroom during the earlier normal refill`

`C011EC67-CAPACITY-LOSS-EVENT = region_free_list::unlink_region on the final basic free-list entry, event 0xDC, region 0x104011238, freeCount 1 -> 0`

The final capacity-loss marker is:

`C011EC67-CAPACITY-LOSS eventOrdinal=0xDC checkpoint=0 generationBefore=0 generationAfter=0 stateBefore=0xA stateAfter=0x8 listKind=0 region=0x104011238 freeBytes=0xFFFD8 freeCountBefore=1 freeCountAfter=0`

The exact source operation is the locked `region_free_list::unlink_region`
path at `gc.cpp:12931-12974`, observed after the production unlink accounting
update.  State `0xA` means the diagnostic state had the region in a free list
and observed the committed-beyond-base condition; state `0x8` retains only the
committed-beyond-base bit after list membership was cleared.  The transition
eliminates the last discoverable basic-list candidate.  It is earlier than the
decisive `get_new_region(0)` source record and explains why that source record
has `freeCountBefore=0`, `result=0`, and source branch `3`.

`C011EC67-FIRST-REGION-AVAILABILITY-DIVERGENCE = initial basic-list unlink at event 0x5, region 0x104010CF8, state 0xA -> 0x8, freeCount 1 -> 0`

The first divergence is also `region_free_list::unlink_region` at
`gc.cpp:12931-12974`.  It is causally relevant because it demonstrates the
production state transition that removes the last visible entry; it is not
claimed to be the final depletion because subsequent `add_region_*` events
repopulated the list.  The final depletion is the later event `0xDC` above.

## GC, allocation, reclassification, and expansion

The C67 summary records `gcCausedCandidateLoss=0`,
`ordinaryAllocationCausedCandidateLoss=1`, and
`promotionCausedCandidateLoss=0`.  The ordinary flag means the final
transition was observed in the post-resume allocation window; it is not a
claim that a single managed instruction directly mutated the list.  There is
no C67 evidence that promotion itself caused the final unlink.  Three
generation transitions were observed separately, including the inherited
historical segment transition, but no promotion/reclassification attribution
was asserted for the final loss.

C67 observed expansion attempts in the normal `a_fit_segment_end_p` path,
whose locked source is in the `gc.cpp:17896-17979` region-fitting path.  The
relevant normal mechanism is `grow_heap_segment`/segment-end fitting before or
around region acquisition; `get_new_region` itself first searches the basic
free list and only then reaches the new-region allocator branch.  Expansion
was attempted, but the decisive C67 summary is:

* `expansionAttempted=1`.
* `expansionSucceeded=0`.
* the decisive `C011EC67-EXPANSION` request was `0x4018`, result `0`, with
  `hardLimitShort=0`.

C67 did not force a commit, increase a region budget, alter virtual-memory
limits, or classify the failed grow as system OOM.  The source-backed result
is narrower: no replacement region became available in the observed basic
list/new-region sequence before the decisive refill, so the inherited C65
failure chain remained active.

## Bounded nearby region inventory

The following compact inventory uses the first boot’s addresses; addresses are
not treated as stable identities across boots.

| Region | Generation/topology | State or list evidence | Basic-list membership at decisive source | Interpretation |
| --- | --- | --- | --- | --- |
| `0x104011388` | gen0; decisive active region | created/committed in the observed lifecycle; `next=0` | no | Active allocation ownership is distinct from a reusable candidate. |
| `0x104011238` | gen0 | final loss `0xA -> 0x8`; list count `1 -> 0` | no after event `0xDC` | Last discoverable basic-list candidate; exact capacity-loss region. |
| `0x104011040` | gen0-linked topology in final snapshot | final nonzero snapshot head; candidate count `6` | later drained by rethread/unlink events | Demonstrates that nonzero availability existed earlier, then disappeared before the decisive source record. |
| `0x104010CF8` | observed gen0 → gen1 transition | first divergence `0xA -> 0x8` | no after first unlink; later lifecycle reintroduced related entries | Earliest transient zero-candidate example, not the final loss. |
| `0x104010668` | historical gen1 → gen2 transition | no direct C67 free-list state event | not observed in the basic candidate list | Mapped/allocator-visible control capacity, not a gen0 candidate. |

The C67 diagnostic state bits are observational encodings: bit `0x1` means
`allocated == mem`, bit `0x2` means `containing_free_list != nullptr`, bit
`0x4` means `committed == reserved`, and bit `0x8` means
`committed > mem`.  They are not substituted for the runtime’s own region
state machine.

## Historical reclaimed-tail control

The inherited historical range is `[0x100900028,0x100943000)` with size
`0x42FD8` and historical segment `0x104010668`.  C67 continues to treat this
as a control example only:

* it remains mapped/allocator-visible in the inherited semantic record;
* its observed older-generation transition is gen1 → gen2;
* no C67 event places it in `free_regions[basic_free_region]`;
* its detailed current region-state bitfield was not directly sampled by the
  C67 list hook because it never entered that list;
* it is not gen0-eligible through the `get_new_region(0)` candidate mechanism;
* mapped address-space existence is insufficient for candidate membership.

C67 did not target its address, reuse it, reclassify it, or make it a
candidate.

## Inherited proof and safety checks

The C67 completion record was identical in semantics across all three boots:
promotion, debit, `RestartEE`, managed resume, C66 baseline, post-debit refill,
candidate transition, and null `get_new_region` result were all observed.
The final C67 record had `eventCount=0xDC`, `snapshotCount=0x16C`,
`eventOverflow=0`, `snapshotOverflow=0`, `invariantFailures=0`, and
`sensitiveDiagnosticAllocations=0`.

The inherited C18 authentic managed-PC / `CoffNativeCodeManager` /
`FindMethodInfo` gate, C26 authentic root scan, C28 mark closure, durable
C46/C48 FP handoff/rehome correction, former-C47 near-null-root guard,
planner/collection/RestartEE/managed-resume continuation, and survivor
sentinel/readbacks remained intact.  The C67 serials contain no fail-fast and
no page fault.

## C68 boundary

C67 stops at provenance.  It does not change the retained survivor cohort,
allocation count, request size, timing, region policy, or collection behavior.

`C011EC68-RECOMMENDED-VARIABLE = retained survivor references entering the promotion collection`

`C011EC68-BASELINE-VALUE = C66 unchanged retained survivor cohort / 4 survivor references`

`C011EC68-DIRECTION = decrease`

`C011EC68-SOURCE-BACKED-RATIONALE = the earlier refill used active-region headroom, while the decisive post-debit path had no basic free-region candidate after the GC/rethread lifecycle; retained survivors are the single ordinary workload input selected for a later test because they influence how much region topology remains live versus returnable/reusable after rethreading.`

No C68 experiment was performed.  `DIRECT_B02_TEST_JUSTIFIED = NO`: the
unchanged workload still produced no post-debit N0 doorway, so the direct B02
gate remains closed.

## Validation and restoration

The C67 proof mode completed the locked runtime-pack/native build, managed
build/publish, kernel build, artifact PE→ELF conversion, symbol/linker/source/
table/archive guards, and QEMU execution.  PowerShell parsing, JSON manifest
parsing, and `git diff --check` were run.  Full C52 Tier-All was not required:
C67 changes are proof-only diagnostics, harness wiring, and documentation; no
production GC semantics were changed.

Three fresh boots agreed as Outcome A / Level 3.  The proof kernel hash was
`5CA848E4D91BD4479ADDF96B8989DA43227AAB4DEA53C81100A0AD1E0C8EE6D6`.  The
three serial hashes were:

1. `05DA0ADBB083056C6DCE0DAD1EE2CD67B389F8DF64D40A6F7293B4A0DD059A8B`
2. `087FE4AFA153755CF5F8C201FB87E050D3AE4A0278D1ED042AA31CBFDCC4CB3`
3. `6E686EAB9C78D61FF6CF11A8786622EDD6ECB0F57C96B390A5EE62E5C2729DE0`

The ordinary kernel and ESP were restored to:

`75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6`

Only C67-owned QEMU processes were cleaned up; unrelated QEMU state was
preserved.  The direct B02 gate remains closed.
