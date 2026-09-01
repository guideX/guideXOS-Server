# C011EC65 — Provenance of Post-Debit Gen2/OOS Preemption

Date: 2026-09-01
Repository: `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT`
Branch: `v1.1_DOTNET_SUPPORT`
Result: **Outcome A / Success Level 5**

## Executive result

C65 closes the C64 provenance question on the locked .NET 9 NativeAOT
Workstation GC with `USE_REGIONS`:

> The first authentic post-debit collection request is redirected to
> generation 2 because the ordinary gen0 allocation context is exhausted and
> the subsequent gen0 refill cannot fit, cannot obtain a valid new gen0
> region, and sets `commit_failed_p`. `allocate_soh` then enters the full
> compaction state with `reason_oos_soh`; `trigger_full_compact_gc` sets
> `last_gc_before_oom` and calls `trigger_gc_for_alloc(max_generation, ...)`.
> Since `max_generation == 2`, the production caller supplies generation 2 and
> `generation_to_condemn` begins with `n_initial == 2`.

The later `generation_to_condemn` region-reserve check is not the source of
the generation-2 request. In the selected run it successfully created and
returned a candidate region (`freeRegionsBefore=0`, `freeRegionsAfter=1`,
`result=1`, `branch=2`). It is downstream policy evidence. The first causal
divergence is the earlier failed gen0 refill:

`context exhausted -> soh_try_fit -> no next segment -> get_new_region(0) returns null -> commit_failed_p=TRUE -> full-compaction state -> reason 5 -> last_gc_before_oom -> request generation 2 -> n_initial=2`

The run used the inherited C64 W3 baseline: `0x4000` tail payload and 320
tail allocations. C64 itself remains a useful negative baseline: Outcome C /
Success Level 2, with a complete ordinary caller already reporting
`requestedGeneration=2`, `n_initial=2`, `reason=5`, and
`last_gc_before_oom=1`, but without the allocator/refill provenance that C65
records.

## Locked identity and baseline

- Runtime: NativeAOT / .NET `9.0.0`, AMD64, Workstation GC, interfaces `5.3 / 2`.
- Locked source checkout:
  `out/dotnet/c52-runtime-source/source-04371d8e`.
- Locked source commit:
  `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`.
- Durable patch: `nativeaot-amd64-fp-handoff.patch`.
- Durable patch SHA-256:
  `4185495724D48E2962BA9042AF352718BF9188032DEE4C9DE6FFE9F145A1DC31`.
- Canonical ordinary kernel and ESP SHA-256:
  `75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6`.
- C64 starting HEAD and upstream-aligned HEAD:
  `962a83568b531e86856fd1943023fa4c6194f371` —
  `Trace NativeAOT post-debit normal condemnation entry`.
- Starting branch: `v1.1_DOTNET_SUPPORT`.
- Starting upstream: `origin/v1.1_DOTNET_SUPPORT`.
- Starting divergence: `0 ahead / 0 behind`.
- Locked runtime source checkout: untouched.

C65 is proof-mode instrumentation layered on the C64 harness. It does not
change the production GC policy, allocator, free-region behavior, requested
generation, OOS behavior, budget, pointer or boundary decisions, candidate
order, or collection entry point.

## C64 baseline and C65 scope

| Milestone | Result relevant to C65 |
| --- | --- |
| C61 | Naturally preserved promotion and debit timing in the inherited P2 workload. |
| C62 | Reproduced promotion, gen1 debit, `RestartEE`, managed resume, and post-resume allocation/refill observations. |
| C63 | Reproduced the same transition with the bounded R2 tail. |
| C64 | Kept the post-resume window open and found a complete ordinary caller/policy near-miss, already at `requestedGeneration=2`, `n_initial=2`, `reason=5`, `last_gc_before_oom=1`. |
| C65 | Adds bounded allocator, refill, region, OOS, `last_gc_before_oom`, GC-call, and `n_initial` provenance to identify the causal predecessor of that caller. |

C64 selected W3 baseline:

- result: Outcome C / Success Level 2;
- commit: `962a83568b531e86856fd1943023fa4c6194f371`;
- variant: W3;
- tail payload: `0x4000`;
- requested tail allocations: `320`;
- topology: `PROMOTE -> GEN1-DEBIT -> RestartEE -> managed resume ->
  post-debit caller -> GarbageCollectGeneration(2, reason_oos_soh) ->
  generation_to_condemn(n_initial=2)`.

## Locked source call graph

The audit was performed against the locked `gc.cpp`/`gc.h` checkout. The
relevant production chain is:

| Function / source location | Causal role |
| --- | --- |
| `GCHeap::Alloc` — `gc.cpp:49905-49997` | Passes the aligned request to the heap allocator. |
| `gc_heap::allocate` — `gc.cpp:19555-19588` | Fast bump allocation; on context exhaustion restores the pointer and calls `allocate_more_space(..., 0)`. |
| `gc_heap::allocate_more_space` — `gc.cpp:19490-19553` | Workstation retry wrapper around `try_allocate_more_space`. |
| `gc_heap::try_allocate_more_space` — `gc.cpp:18949-19060` | Preserves `gen_number=0`; after budget handling calls `allocate_soh(0, ...)`. |
| `gc_heap::soh_try_fit` — `gc.cpp:17896-17979` | Tries free-list/segment-end fit; under `USE_REGIONS`, walks or requests another ephemeral region. |
| `gc_heap::a_fit_segment_end_p` — `gc.cpp:17621-17775` | Attempts the committed-end fit and possible heap-segment growth. |
| `gc_heap::allocate_soh` — `gc.cpp:17982-18256` | Uses `gr=reason_oos_soh`; maps `commit_failed` to `a_state_trigger_full_compact_gc`. |
| `gc_heap::trigger_full_compact_gc` — `gc.cpp:18462-18522` | Writes `last_gc_before_oom` and calls `trigger_gc_for_alloc(max_generation, gr, ...)`. |
| `gc_heap::trigger_gc_for_alloc` — `gc.cpp:18881-18927` | Calls `vm_heap->GarbageCollectGeneration(gen_number, gr)` at line 18909. |
| `GCHeap::GarbageCollectGeneration` — `gc.cpp:50960-51040` | Stores `gc_trigger_reason=reason` and retains the caller’s `gen` as `condemned_generation_number`. |
| `gc_heap::generation_to_condemn` — `gc.cpp:21486-21875` | Begins with `n=n_initial`; consumes `last_gc_before_oom` to force max-generation blocking policy. |

The region helpers audited for the branch distinction are:

- `try_get_new_free_region` — `gc.cpp:21345-21371`;
- `get_free_region` — `gc.cpp:11906-12020`;
- `get_new_region` — `gc.cpp:34988-35017`;
- `allocate_new_region` — `gc.cpp:35019-35046`;
- `make_heap_segment` — `gc.cpp:12313-12350`;
- `grow_heap_segment` — `gc.cpp:15464-15512`.

## Exact causal chain

### 1. Context exhaustion is the entry condition

`gc_heap::allocate` first bumps `acontext->alloc_ptr` and checks it against
`acontext->alloc_limit` (`gc.cpp:19562-19569`). The selected C65 post-debit
request was `0x4018` bytes. Its context had:

| Field | Normal refill | Post-debit failed refill |
| --- | ---: | ---: |
| generation | `0` | `0` |
| request | `0x10018` | `0x4018` |
| allocation context | `0x3998CC0` | `0x3998CC0` |
| allocation pointer | `0x100A10810` | `0x0` |
| allocation limit | `0x100A22090` | `0x0` |
| remaining bytes | `0x11880` | `0x0` |
| active segment | `0x104010710` | `0x104011628` |
| fit result | `1` | `0` |
| branch | `1` | `3` |
| commit failed | `0` | `1` |

The failed context check sends the request through
`allocate_more_space(acontext, size, flags, 0)` (`gc.cpp:19578`). This
distinguishes context exhaustion from the deeper heap-space failure: the
context boundary is what enters the refill path; the later region failure is
what causes the OOS/full-compaction branch.

### 2. The first divergence is the gen0 region miss

`try_allocate_more_space` computes `loh_p = (gen_number > 0)` and therefore
keeps this request on the SOH/gen0 path (`gc.cpp:18962-18963`). After ordinary
budget handling, it calls `allocate_soh(0, ...)` (`gc.cpp:19055-19057`).

In `soh_try_fit`, the locked `USE_REGIONS` path first tries the active
segment-end fit. When that fails, it fixes the allocation context and
youngest area, then checks `heap_segment_next(ephemeral_heap_segment)`
(`gc.cpp:17925-17940`). At the selected divergence:

1. there was no next segment;
2. `get_new_region(gen_number)` was called with `gen_number=0`
   (`gc.cpp:17943-17947`);
3. no valid region was returned;
4. `*commit_failed_p=TRUE` was written and `soh_try_fit` returned `FALSE`
   (`gc.cpp:17965-17969`).

C65 records this as:

`ALLOCATION-REGION generation=0 requestSize=0 resultRegion=0 freeRegionsBefore=0 freeRegionsAfter=0 result=0 branch=3`

and the first-divergence marker is:

`eventOrdinal=0x301 branch=3 allocationState=0xF commitFailed=1 shortSegmentEnd=0`

The locked `allocation_state` enum is in `gcpriv.h:538-562`; value `0xF`
is `a_state_trigger_full_compact_gc`. This is the first state transition
that differs from the successful normal refill and it is upstream of reason
5, `last_gc_before_oom`, generation 2, and `n_initial=2`.

### 3. Reason 5 is selected by the OOS allocator branch

`gc.h:58-79` defines `reason_oos_soh = 5`; the text mapping is in
`gc.cpp:163-180`. `allocate_soh` initializes its local reason as
`gc_reason gr = reason_oos_soh` at `gc.cpp:18014`.

The initial fit result maps to the full-compaction state at
`gc.cpp:18044-18051`: a failed fit with `commit_failed_p` selects
`a_state_trigger_full_compact_gc`. The reason is therefore an output of the
already-selected OOS allocation path. It is passed into
`trigger_full_compact_gc` and onward; it does not itself create the
generation-2 request.

### 4. `last_gc_before_oom` is written before the GC call

`last_gc_before_oom` is a static heap-global boolean declared at
`gc.cpp:2526`. It is initialized/reset to `FALSE` around `gc.cpp:15256` and
reset after a non-concurrent max-generation collection at
`gc.cpp:22968-22973`.

When the full-compaction allocation state is entered,
`trigger_full_compact_gc` writes it at `gc.cpp:18471-18475`:

`if (!last_gc_before_oom) { last_gc_before_oom = TRUE; }`

This is the upstream write observed by C65:

`LAST-GC-BEFORE-OOM value=1 writeEventOrdinal=0x302`.

There is a second, downstream possible write in the policy function:
`generation_to_condemn` calls `try_get_new_free_region` and can set the same
flag on failure at `gc.cpp:21721-21729`. C65 separates these events. In this
run the policy reserve check succeeded, so the decisive upstream write is the
one in `trigger_full_compact_gc`.

### 5. Generation 2 is first supplied by the full-compaction caller

The exact caller path is:

`trigger_full_compact_gc` (`gc.cpp:18498`)
`-> trigger_gc_for_alloc(max_generation, gr, ...)`
`-> vm_heap->GarbageCollectGeneration(gen_number, gr)` (`gc.cpp:18909`)
`-> GarbageCollectGeneration(2, reason_oos_soh)`.

For this locked Workstation build, `max_generation == 2`. The GC entry stores
the reason at `gc.cpp:51006` and retains the supplied generation at
`gc.cpp:51040`. The policy function then starts with:

`int n = n_initial;` — `gc.cpp:21507`.

C65 observed the first authentic post-debit production request as:

| Field | Value |
| --- | ---: |
| requested generation | `2` |
| `n_initial` | `2` |
| collection reason | `5` (`reason_oos_soh`) |
| `last_gc_before_oom` | `1` |
| origin branch | `4` (ordinary single-heap `GarbageCollectGeneration` caller) |
| policy-selected generation | `2` |
| `nAlloc` | `2` |
| blocking collection | `1` |

This proves provenance, not merely final policy state: the caller supplied
generation 2 before `generation_to_condemn` began. The downstream
`try_get_new_free_region` check cannot manufacture the caller’s
`n_initial=2`.

## Policy reserve check versus allocation failure

The policy reserve check in `generation_to_condemn` is a distinct event from
the failed gen0 allocation refill. Under `USE_REGIONS`,
`try_get_new_free_region` first checks the basic free-region list. If it is
empty, it calls `allocate_new_region(__this, 0, false)` and, after table
initialization, returns that region to the free list (`gc.cpp:21345-21371`).

C65 recorded:

| Event | Result |
| --- | --- |
| allocation-region miss during gen0 refill | `free before=0`, result region `0`, result `0`, branch `3` |
| later policy reserve search | `eventCount=3`, `free before=0`, `free after=1`, candidate nonzero, result `1`, branch `2` |
| policy candidate | `0x104010E48` in the selected boot; run-local address |

Therefore the successful policy reserve search did not rescue the already
failed allocation request and did not cause the original generation-2 caller.
It is evidence that the allocator failure and policy reserve path are
temporally and causally separate.

The C65 region-result observer also recorded
`expansionAttempted=1`, `expansionSucceeded=0`, with committed bytes
`0x102101000` and reserved bytes `0x102200000`. This corresponds to the
growth possibility in `a_fit_segment_end_p` (`gc.cpp:17621-17775`). It does
not change the decisive classification: no valid gen0 region was obtained,
and the allocation-region result remained null with `commit_failed=1`.

## Region and reclaimed-tail topology

The active segment addresses are run-local. In the selected evidence they
were:

- successful normal refill active segment: `0x104010710`;
- failed post-debit refill active segment: `0x104011628`.

The inherited C54 reclaimed tail remained mapped and allocator-visible but was
not eligible for reuse:

| Field | C54 observation |
| --- | ---: |
| range | `[0x100900028, 0x100943000)` |
| size | `0x42FD8` |
| segment | `0x104010668` |
| generation before / after promotion | `1 / 2` |
| mapped | `true` |
| allocator-visible | `true` |
| eligible / considered / selected / consumed for reuse | `false / false / false / false` |

The C65 evidence therefore does not attribute the post-debit miss to reuse of
the reclaimed tail. The normal refill and the failed refill are separate
active-region observations, while the historical C54 tail remains outside
the eligible reuse path.

## Bounded diagnostics and invariants

C65 adds fixed native records and deferred serial formatting in proof mode:

- event ring cap: `0x400` = 1024 records;
- selected event count: `0x3ED` = 1005;
- event overflow: `0`;
- serial cap: 16 MiB;
- sensitive diagnostic allocations: `0`.

The observers cover managed allocation entry/return, normal refill,
post-debit request, SOH fit/state, failed allocation-region lookup, region
expansion, policy reserve search/candidate/result, OOS reason, the
`last_gc_before_oom` write, the GC call, `n_initial`, restart, resume, and
completion. The observer callbacks are bounded and accounting-only; they do
not select a generation, write policy or allocator state, alter free-region
lists, suppress OOS, force a collection, or shape the workload.

The selected completion marker reports:

`normalRefillObserved=1 postDebitObserved=1 allocationFailureObserved=1 regionSearchObserved=1 regionResultObserved=1 oosObserved=1 lastGcBeforeOom=1 gcCallObserved=1 requestedGeneration=2 nInitial=2 firstDivergenceBranch=3 invariantFailures=0 sensitiveDiagnosticAllocations=0 noPolicyMutation=1 noAllocatorMutation=1 freeRegionMutation=0 requestedGenerationMutation=0 noOosSuppression=1`

Inherited C18, C26, C28, C34, C37, C39, C40, C41, C53, C54, C55, C56, C57,
C58, C61, C62, and C64 checks passed. Source identity, runtime-pack,
managed/native build, PE-to-ELF, linker/source-table/archive, serial
completion, semantic agreement, and `git diff --check` guards passed.

## Three fresh QEMU boots

Evidence root:

`out/dotnet/c011ec65-post-debit-gen2-oos-preemption-final/run-20260901-130038819`

QEMU: `11.0.0 (v11.0.0-12122-ga4bb4b10c9)`
Variant: W3
Fresh boots: 3
Semantic agreement: `true`
Safe stop marker: `C011EC65`

| Boot | Serial SHA-256 | Outcome / level |
| --- | --- | --- |
| first-run | `5794711BED6691DCF1CFF6BD12101534607ED9E3E324CB335E8D1DECB372E4D9` | A / 5 |
| repeat-1 | `01C731005F7450129E8581E61C2A2DBCEE1BC1324E65CFEA8ABB31F008A94C0C` | A / 5 |
| repeat-2 | `6F8E99673F8345F27B76545F26B26C27CEC6FBA5B12086D043B3FF9DDADDBF91` | A / 5 |

All three boots agreed semantically on the complete chain. Run-local pointer
values may differ; the serial hashes are consequently not expected to be
equal.

The proof kernel SHA-256 for this final run was:

`6B2E31B2D63CD1D269F2C284E170C29BB9519E4501CD64939DE9B6095782D51E`

The run manifest is:

`out/dotnet/c011ec65-post-debit-gen2-oos-preemption-final/run-20260901-130038819/manifest.json`

## Restoration and safety

The harness restored the ordinary artifacts in its `finally` path. The final
manifest reports:

- restored by `finally`: `true`;
- ordinary kernel SHA-256 after the run:
  `75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6`;
- ordinary ESP SHA-256 after the run:
  `75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6`;
- proof-only artifact active: `false`;
- C65-owned QEMU processes: stopped;
- unrelated pre-existing QEMU process: preserved;
- fail-fast: `0`;
- page fault: `0`.

## C66 recommendation and limitations

C66 should vary exactly one natural managed variable: the post-debit tail
allocation count. Start below the first refill-failure boundary and test
whether preserving a valid gen0 region/context keeps the first collection
request on the normal gen0 path. C66 should not change the GC source, force a
region, force a collection, or introduce a second workload variable. No C66
implementation is included here.

C65 characterizes the observed locked Workstation `USE_REGIONS` allocator
branch and separates it from the later policy reserve check. It does not
prove a universal impossibility of every normal post-debit `n_initial=0`
outcome, and it does not shape the workload to manufacture the observed
transition.

## Exact conclusion

C65 establishes the production causal chain at Outcome A / Success Level 5:

`post-debit ordinary allocation -> context exhaustion -> gen0 refill -> no fit and no valid new gen0 region -> commit_failed -> allocate_soh full-compaction state -> reason_oos_soh (5) -> last_gc_before_oom=TRUE -> trigger_gc_for_alloc(max_generation=2) -> GarbageCollectGeneration(2, reason_oos_soh) -> generation_to_condemn(n_initial=2)`.

The first generation-2 assignment is the `max_generation` argument in
`trigger_full_compact_gc`, not the downstream `try_get_new_free_region`
policy check. Ordinary artifacts were restored, inherited invariants passed,
and no push was performed.
