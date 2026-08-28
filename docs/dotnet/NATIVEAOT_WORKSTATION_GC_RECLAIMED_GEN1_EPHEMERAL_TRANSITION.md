# C011EC54 — NativeAOT Workstation GC reclaimed gen1 ephemeral transition

## Result

C011EC54 continues the C52/C53 productionized NativeAOT 9.0.0 AMD64
Workstation-GC path. C53 established the reclaimed generation-1 tail and the
domain mismatch: the tail is mapped and allocator-visible, while ordinary
new objects are allocated from the generation-0 ephemeral domain.

The final C54 proof used three fresh QEMU 11.0.0 boots. All three agreed on:

* Outcome C — the bounded workload observed the authoritative generation
  publication point, but did not reach the older-generation transition needed
  to make the reclaimed gen1 tail eligible.
* Success Level 1 — source-correlated generation state was captured.
* two natural collections, both condemned generation 0;
* `fix_generation_bounds` observed, but no `adjust_ephemeral_limits` change;
* no ephemeral-segment transition, retirement, recycling, eligibility,
  consideration, selection, or consumption;
* seven retained-survivor observations, zero C54 invariant failures, and zero
  sensitive diagnostic allocations.

This is not a direct-reuse claim. The strongest proven no-reuse classification
is Code 7: the required older-generation/full collection did not occur within
the bounded ordinary workload.

## Locked identity and C53 baseline

The run started from branch `v1.1_DOTNET_SUPPORT`, HEAD
`f1c6c4b6b7af89e22baab155350582870e9dc8fc`, subject
`Trace NativeAOT reclaimed gen1 production lifecycle`. C53 remains in history
and was an ancestor of the C54 work.

The locked runtime is NativeAOT `9.0.0`, AMD64, Workstation GC, GC interfaces
`5.3 / 2`, source commit
`9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`, with
`nativeaot-amd64-fp-handoff.patch` SHA-256
`4185495724D48E2962BA9042AF352718BF9188032DEE4C9DE6FFE9F145A1DC31`.
No C46/C47/C48 semantic harness rewrite or FP patch change was made.

C53's historical equivalent was:

```text
[0x100900028, 0x100943000)
size 0x42FD8 / 274392 bytes
segment 0x104010668, generation 1
allocator-visible yes; eligible/considered/selected/consumed no
ordinary allocation segment 0x104010710, generation 0
```

C54 reproduced the same semantic range on all three fresh boots. A
representative run recorded heap `0x10190DA0`, segment `0x104010668`, range
`[0x100900028, 0x100943000)`, size `0x42FD8`, initial and final tail
generation `1`, and `tailStillMapped=1`.

## Exact source audit

The observation points are in the locked Workstation GC source, not inferred
from managed symptoms:

| Lifecycle | Locked source path | C54 use |
| --- | --- | --- |
| collection entry | `gc.cpp:GCHeap::GarbageCollectGeneration` | collection ordinal, condemned generation, reason |
| planning | `gc.cpp:gc_heap::plan_phase` / `decide_on_compacting` | planner and actual compact/sweep phase |
| generation publication | `gc.cpp:gc_heap::fix_generation_bounds` | generation 0/1/2 starts, allocation contexts, segments, before/after snapshots |
| ephemeral limits | `gc.cpp:gc_heap::adjust_ephemeral_limits` | source-correlated boundary callback; no transition occurred in this run |
| ephemeral segment replacement | `gc.cpp:gc_heap::expand_heap` | old/new ephemeral segment callback |
| segment retirement/relinking | `gc.cpp:gc_heap::rearrange_heap_segments` | retire/recycle callbacks |
| allocation | `gc.cpp:gc_heap::soh_try_fit` / `allocate_soh` | fast, free-list, allocation-context, and segment-tail candidate observations |
| context publication | locked allocation-context setup adjacent to `allocate_soh` | active ordinary allocation domain |
| managed continuation | locked `RestartEE` return path | C54 resume evidence |

The 64-bit locked build defines `USE_REGIONS`. In that configuration there is
no standalone `generation::allocation_start` or non-regions
`ephemeral_low/high` field at the injected call sites. C54 therefore records
the region allocation context (`generation_allocation_pointer`, limit, and
segment) plus the authoritative generation start-segment metadata; it does
not invent fields that do not exist in this production build.

The relevant source mechanism is:

1. `fix_generation_bounds` publishes the post-plan generation allocation
   state.
2. An older-generation/full collection is the natural event that can alter
   the relationship between the generation-1 tail, the ephemeral boundary,
   and active allocation segments.
3. `adjust_ephemeral_limits` and/or `expand_heap` can publish a changed
   ephemeral domain or replace its segment.
4. `rearrange_heap_segments` can instead unlink and retire a non-ephemeral
   segment, which is different from direct tail reuse.
5. Only after the reclaimed range is inside the authoritative ordinary
   allocation domain may allocator candidate observation be classified as
   eligible, considered, selected, or consumed.

## Bounded natural workload

The managed proof performs no `GC.Collect`, private GC call, condemned-
generation request, address targeting, pointer mutation, free-range injection,
segment-link mutation, or allocator preference change. It performs at most 64
ordinary `byte[65536]` allocations and retains at most four explicitly named
survivors. The native C54 record has a fixed eight-collection capacity; the
final workload reached two collections and stopped at the 64-allocation cap.

Each allocation is pattern-filled, read back, and followed by normal managed
continuation. `GC.GetGeneration` and survivor address observations occur only
after `GC.CollectionCount(0)` changes. Survivor markers retain initial/current
address, initial/current generation, movement, segment when available, valid
readback, and sentinel.

Observed survivor sequence in the representative final boot:

| Survivor | Collection observation | Initial → current generation | Movement | Segment |
| --- | --- | --- | --- | --- |
| 0 | runtime count 5, then 6 | 0 → 1 | moved | `0x104010668` |
| 1 | runtime count 5, then 6 | 0 → 1 | moved | `0x104010668` |
| 2 | runtime count 5, then 6 | 0 → 1 | stayed first, then moved | `0x104010710`, then `0x104010668` |
| 3 | runtime count 6 | 0 → 1 | moved | `0x104010668` |

All survivor sentinels and managed readbacks were valid. The runtime collection
counter includes predecessor activity, so its values 5 and 6 correspond to
C54's two source-correlated collection ordinals 1 and 2.

## Collection and generation evidence

Both source-correlated collections were condemned generation 0, planner
decision `0x1`, actual phase `0x1` (compact), and were followed by `RestartEE`
and managed resume. The two collection snapshots were semantically identical:

```text
gen0 before: start=0x100A00028 pointer=0x100A00028 limit=0x100A00028 segment=0x104010710
gen0 after:  start=0x100A00028 pointer=0 limit=0 segment=0x104010710
gen1 before: start=0x100900028 segment=0x104010668; allocation context fields=0
gen1 after:  start=0x100900028 segment=0x104010668; allocation context fields=0
gen2 before: start=0x100800028 segment=0x1040105C0; allocation context fields=0
gen2 after:  start=0x100800028 segment=0x1040105C0; allocation context fields=0
```

The C54 resume state showed the ordinary ephemeral allocation segment as
`0x104010710`; the reclaimed tail segment remained `0x104010668` and
generation 1. No `C011EC54-EPHEMERAL`, `-RETIRE`, or `-RECYCLE` marker was
emitted. No `C011EC54-ELIGIBLE`, `-CONSIDERED`, `-SELECTED`, `-REUSE`, or
consumption marker was emitted because those states were not proven.

The causal frontier is therefore precise: promotion of retained objects from
gen0 to gen1 occurred, but no condemned-gen1/full collection or ephemeral
segment rotation occurred to change ownership of the reclaimed C40 tail.
The C53 mismatch remains unresolved under this safe bound.

## Regression and safety results

The final serial streams retained the C18 valid transition-frame path, valid
`CoffNativeCodeManager`, successful `FindMethodInfo`, C26 root scanning, C28
mark-queue closure, C37 repeated-collection chronology, C39 planner evidence,
C40 reclamation, C41 allocator-domain provenance, C46/C48 durable FP fixes,
and C52/C53 predecessor evidence. The C26/C28 stream recorded four promoted
roots and zero mark-queue invariant failures in the retained predecessor proof.

Across all three C54 boots:

* no C18 fail-fast, stale-FP page fault, `0xFFFFFFFFFFFFFF90` fault, or
  fail-fast marker occurred;
* planner, compaction, `RestartEE`, managed resume, live-object readback, and
  sentinel checks passed;
* C54 `invariantFailures=0x00000000` and
  `sensitiveDiagnosticAllocations=0x00000000`;
* the semantic rewrite guard, PE→ELF conversion, linker/source/table guards,
  and ordinary-boot restoration passed;
* MASM was not applicable because no assembly source changed.

The inherited C52 release-gate baseline remains the control. C54 changed only
the managed proof workload, fixed-size diagnostics, and smoke-harness parser;
it did not change production GC semantics. A full C52 Tier-All rerun is not
claimed by this document.

## Final three-boot evidence

Evidence root:
`out/dotnet/c011ec54-reclaimed-gen1-ephemeral-transition/run-20260828-154933910`

QEMU version:
`QEMU emulator version 11.0.0 (v11.0.0-12122-ga4bb4b10c9)`

Proof payload hashes:

```text
proof kernel ELF  E77445C46E58E947EB441EA667B603077C2FA45C342C27C47BFF1CCCDD9DD2CF
PE                B14C7F18D17D33A80D69A426F855642AEA2DBC4AF7A6A0D55682087AE41401CF
ELF               CF62A9D8D2DA86E9028CCD46E74D37113307182D87BE8F0A356EC02D34137458
MAP               5F8B3EBC76D7475392E7722810BA28BC09FBE69097D92E3E3D1BE41CD3B0B7AB
```

Serial SHA-256 values, in boot order:

```text
A4CB6F124DAF35E02DE71DFBECB0886752B3C90DE4B81CBB92E7CEC83EF6469E
4AF28D3DAE84386CA6EA61C9C8F6DDC56BBC50FEB9C2BCBBF1F5C24AEF7DED46
4A5BFEADD4BA8B68E65BC010A1DB7BE14DD6657D323CB9D60B65C34692999E37
```

The final manifest is JSON-parseable and records semantic agreement across all
three runs. The ordinary kernel and ESP were restored in `finally` and both
matched the expected SHA-256:

```text
75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6
```

## Marker contract

C54 emits only bounded, source-correlated markers that are proven by the
current state: `C011EC54-PREFLIGHT`, `-RECLAIMED`, `-LIFECYCLE`,
`-GEN-BEFORE`, `-GEN-AFTER`, `-RESUME`, `-SURVIVOR`, `-ELIGIBILITY`, and the
`C011EC54` completion marker. Eligibility and allocator markers are absent in
this result by design. Records are fixed-size scalars; no diagnostic callback
retains a runtime pointer or writes through generation, segment, free-list, or
allocation-context state.

## Next smallest milestone

Extend only the natural pressure window, still with a fixed allocation and
collection cap, while retaining a small survivor set. The next milestone is
to reach and source-correlate the first condemned-gen1/full collection or
ephemeral-segment replacement. If that event retires or recycles the segment,
report it as segment lifecycle rather than direct reclaimed-tail reuse.
