# NativeAOT Workstation GC `n_initial` provenance

## C011EC58 result

The existing C57 S2 workload was rerun with bounded C58 provenance instrumentation on
three fresh QEMU boots. All three boots produced the same result:

```
C011EC58 outcome=A successLevel=2
entries=3 n0Count=2 n1Count=0 n2Count=1
lastN0Entry=2 firstN2Entry=3 n0ToN2Proven=1
originObservations=3 allocationAttempts=174 freeRegionObservations=3
b02Observed=0 b02Crossed=0 b12Eligible=1
restartObserved=1 resumeObserved=1
tailEligible=0 tailConsidered=0 tailSelected=0 tailConsumed=0 tailStillMapped=1
invariantFailures=0 sensitiveDiagnosticAllocations=0
```

Evidence:

`out/dotnet/c011ec58-ninitial-provenance/run-20260830-072439104/`

The three serial logs are `first-run/serial.log`, `repeat-1/serial.log`, and
`repeat-2/serial.log`. Their C011EC58 completion fields agree byte-for-byte on the
semantic values above. The run manifest is
`out/dotnet/c011ec58-ninitial-provenance/run-20260830-072439104/manifest.json`.

The proof used the existing C57 S2 workload: 16 survivors, three active cohorts, three
pressure-tail cohorts, 48 transient objects per cohort, and 288 transient objects in
total. No bounded workload was needed after the existing workload supplied the decisive
N0-to-N2 chronology.

## Locked runtime and build identity

- NativeAOT/.NET: 9.0.0, AMD64, Workstation GC.
- GC/EE interfaces: 5.3 / 2.
- `USE_REGIONS` enabled; `MULTIPLE_HEAPS` and `BACKGROUND_GC` disabled.
- Locked source: `out/dotnet/c52-runtime-source/source-04371d8e`.
- Locked source commit: `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`.
- `gc.cpp` SHA256: `5BD029B77A973145B12C142E870A549C527DAC19AEB527012F7CC6F99D362FF9`.
- FP handoff patch SHA256:
  `4185495724D48E2962BA9042AF352718BF9188032DEE4C9DE6FFE9F145A1DC31`.
- FP repair state: `PATCHED_CORRECTLY`.
- Fresh runtime-pack manifest:
  `out/dotnet/runtime-pack-c58-production/runtime-pack.manifest.json`.
- Fresh platform object SHA256:
  `1C87C8DB61FA97D062A65631E48C2D31F716E867D3A6D2B2E87BB4C0BC8BE437`.

## Complete direct-call-site audit

The locked source contains exactly three textual production calls to
`generation_to_condemn(...)`:

| ID | Source | Function | Mode | Status |
| --- | --- | --- | --- | --- |
| E01 | `src/coreclr/gc/gc.cpp:17175` | `gc_heap::check_for_full_gc` | `check_only=TRUE` | Active; local `n` begins at 0 and is set by the preceding budget loop. |
| E02 | `src/coreclr/gc/gc.cpp:24144` | `gc_heap::garbage_collect` | `check_only=FALSE` | Inactive in this proof; it is under `MULTIPLE_HEAPS`, which is disabled. |
| E03 | `src/coreclr/gc/gc.cpp:24200` | `gc_heap::garbage_collect` | `check_only=FALSE` | Active single-heap production call. |

`joined_generation_to_condemn` at `src/coreclr/gc/gc.cpp:20779` is not another direct
call. It is the post-selection multi-heap join helper. The active proof call site is
therefore E03.

The active single-heap transport preserves the caller value: the allocation trigger
passes `gen_number` to `GCHeap::GarbageCollectGeneration` at
`src/coreclr/gc/gc.cpp:18909`, and `GCHeap::GarbageCollectGeneration` passes its `gen`
parameter unchanged to `pGenGCHeap->garbage_collect` at
`src/coreclr/gc/gc.cpp:51064`. E03 then supplies that value as `n_initial` to
`generation_to_condemn`.

## Caller-supplied `n_initial` provenance

The source-side observers record the value immediately before the three GC transport
calls, and the entry observer records the value immediately before E01/E02/E03. The
caller branches are:

| Branch | Source path | Caller value | Reason |
| --- | --- | ---: | --- |
| B-N0-BUDGET | `try_allocate_more_space` -> `trigger_gc_for_alloc` at `gc.cpp:19047` -> `GarbageCollectGeneration` at `gc.cpp:18909` | 0 | `reason_alloc_soh` or `reason_alloc_loh`; ordinary allocation-budget GC. |
| B-N1-EPHEMERAL-OOS | `allocate_soh` -> `a_state_trigger_ephemeral_gc` -> `trigger_ephemeral_gc` at `gc.cpp:17877` | `max_generation - 1 = 1` | `reason_oos_soh`; first out-of-space retry requests Gen1. |
| B-N2-FULL-OOS | `a_state_trigger_full_compact_gc` -> `trigger_full_compact_gc` at `gc.cpp:18498` -> `trigger_gc_for_alloc` | `max_generation = 2` | `reason_oos_soh`; failed ephemeral retry escalates to a full OOS GC. |

The decisive C57 S2 sequence is therefore:

1. Two ordinary budget-triggered collections reach E03 with `n_initial=0` and emit
   `C011EC58-N0`.
2. The pressure-tail allocation runs out of space. The ephemeral retry is the N1
   branch, but it does not satisfy the request.
3. The retry enters `a_state_trigger_full_compact_gc`; `trigger_full_compact_gc`
   calls `trigger_gc_for_alloc(max_generation, reason_oos_soh)`, so the active E03
   call receives `n_initial=2`.
4. The first N2 entry is entry 3, after the last N0 entry 2. The runtime emits
   `C011EC58-LAST-N0` and `C011EC58-N0-N2` before emitting the N2 entry record.

This proves why C57's decisive call had `n_initial=2`: it was caller-supplied by the
full-OOS escalation branch, not synthesized by the diagnostics or forced by a
generation override.

## B02 and B12 semantics

The function signature is `gc_heap::generation_to_condemn(int n_initial, ...)` at
`src/coreclr/gc/gc.cpp:21486`.

B02 is the non-check-only allocation-budget loop at
`src/coreclr/gc/gc.cpp:21593-21607`. It starts at `i = n_initial + 1`, tests
`get_new_allocation(i) <= 0`, and only then assigns `n = i`. Its upper bound is
`max_generation` while maximum-generation allocation checks are enabled, otherwise
`max_generation - 1`. Thus `n_initial=0` can elevate through a higher-generation budget
check, `n_initial=1` begins at Gen2 on this two-generation heap, and `n_initial=2` has
no higher generation to inspect. The C58 run observed no B02 crossing
(`b02Observed=0`, `b02Crossed=0`); the N2 value came from the caller branch above.

B12 is the region-availability branch at `src/coreclr/gc/gc.cpp:21721-21730`:

```
if (!try_get_new_free_region()) {
    last_gc_before_oom = TRUE;
}
```

`try_get_new_free_region` first checks the free-region table and then attempts
`allocate_new_region`, table initialization, and returning a free region. In all three
boots, entry 3 recorded `freeRegionResult=2`, `b12Eligible=1`, and
`lastGcBeforeOom=1`, proving the unavailable-free-region path. The final summary reports
the same state without changing that policy decision.

## Allocation, refill, free-region, tail, and restart evidence

C58 uses fixed-size records (256 entry slots, no diagnostic heap allocation). The
source-side allocation observer captures request size, aligned size, allocation-context
address, allocation pointer/limit, allocation segment, and the ephemeral segment. The
existing C57 allocation/refill and C54 tail callbacks enrich the most recent C58 entry.

The decisive entry recorded request size `0x10018`, the live allocation/refill addresses
in the detailed entry record, and a latest policy Gen1 budget of `0xCD508` in the final
summary. The run captured 174 bounded allocation attempts and three free-region
observations per boot.

The tail marker was stable across boots:

```
tailEligible=0 tailConsidered=0 tailSelected=0 tailConsumed=0 tailStillMapped=1
tailGenerationBefore=1 tailGenerationAfter=1
```

This is retained C54 tail state, not a tail-consumption or forced-remap action. The
existing restart/resume path also remained intact: `C011EC58-RESTART` and
`C011EC58-RESUME` were observed with `restartObserved=1` and `managedResumeObserved=1`.

## Invariant and artifact gates

All three boots reported zero invariant failures, zero sensitive diagnostic allocations,
and no fail-fast or page-fault evidence. The manifest records the retained C57, C56,
C54, C39/C41 chronology and explicitly verifies no forced collection, no generation
override, no GC-policy mutation, and no OOS suppression.

The harness restored the ordinary kernel and ESP kernel after validation. Both restored
SHA256 values were:

`75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6`

The proof-only artifact was inactive after cleanup. No changes were pushed.
