# NativeAOT Workstation GC C71: Promotion Live-Byte Threshold

## Result

C71 replaces the earlier retained-count interpretation with an adjacent
retained-live-byte result. The controlled pair retains 15 identical managed
objects and changes only the requested payload by 8 bytes per object.

| Case | Retained count | C54-backed actual object size | Retained live bytes | Semantic result |
| --- | ---: | ---: | ---: | --- |
| LOW (`15mid9`) | 15 | `0x10E78` | `0xFD908` | no authentic promotion |
| HIGH (`15mid8`) | 15 | `0x10E80` | `0xFD980` | authentic promotion |

The object-count difference is zero. The per-object difference is `0x8`, and
the total retained-live-byte difference is `0x78`. The final object-size step
is runtime-aligned to 8 bytes. The literal `0x100000` hypothesis is therefore
rejected.

The final corrected evidence is:

* LOW: `out/dotnet/c011ec71-promotion-decision-live-byte-threshold/15mid9/run-20260903-073648197`
* HIGH: `out/dotnet/c011ec71-promotion-decision-live-byte-threshold/15mid8/run-20260903-074122804`

Each final case contains `first-run`, `repeat-1`, and `repeat-2`. All three
boots per side agree on retained count, C54-backed size, retained total, and
promotion class. The superseded discovery variants remain in the evidence
root for history but are not used as the final C71 pair.

## Source-backed first decision

The earliest directly attributable production quantity is the
`survived_per_region` accumulation in `gc_heap::add_to_promoted_bytes`.
The locked runtime source is
`9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`, at
`src/coreclr/gc/gc.cpp:26657-26665`:

```text
survived_per_region[get_basic_region_index_for_address (object)] += obj_size;
```

The next policy consumer is `gc_heap::decide_on_promotion_surv`, at
`src/coreclr/gc/gc.cpp:29462-29488`. Its production condition is:

```text
(threshold > older_gen_size) || (promoted > threshold)
```

The operands are `threshold`, `hp->total_promoted_bytes`, and
`older_gen_size`, where `older_gen_size` is derived from the next generation's
dynamic data. The decision is published to `settings.promotion` by the
post-mark path at `src/coreclr/gc/gc.cpp:30216-30230`.

C71 observed the retained quantity through the C54 native allocation records,
not through the superseded post-resume allocation marker. The final semantic
promotion markers report:

| Field | LOW | HIGH |
| --- | ---: | ---: |
| Authentic promotion | `0` | `1` |
| Unique promoted objects | `0` | `15` |
| Promoted bytes | `0x0` | `0xFD980` |
| Generation before / after | `0 -> 1` | `0 -> 1` |
| Gen1 budget before | `0x0` | `0x1AB988` |
| Gen1 debit | `0x0` | `0xDBC80` |
| Gen1 budget after | `0x0` | `0xCFD08` |

The C71 observer also audited `gc_heap::sync_promoted_bytes` and
`gc_heap::should_sweep_in_plan`; it did not alter either function or claim a
region-level genealogy. That is the purpose of C72.

## Controls and corrections

The object type, reference topology, allocation order, release order, GC
schedule, pressure schedule, runtime pack, runtime source, NativeAOT
configuration, Workstation GC configuration, durable FP repair, and C46/C47/C48
semantic rewrite state were held constant. The managed-mode precedence issue
was corrected before the final pair was accepted. The final evidence uses the
corrected C71 artifacts only.

C71 observers were bounded and source-accounting only. They made no allocator,
region, region-list, candidate, planner, policy, root, survivor, or OOS
mutation. The final manifests report zero invariant failures, zero sensitive
diagnostic allocations, zero fail-fast events, zero page faults, and zero
diagnostic overflow. Ordinary kernel and ESP restoration returned to:

`75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6`

No B02 experiment was performed. C71 establishes the numerical and
source-level boundary; it does not establish how the boundary propagates into
region formation, free-list membership, candidate consumption, refill, or
OOS. C72 must trace those links.

## Evidence and identity

Runtime identity: NativeAOT 9.0.0, AMD64, Workstation GC, GC interfaces 5.3 / 2.

FP-repair artifact SHA-256:

`4185495724D48E2962BA9042AF352718BF9188032DEE4C9DE6FFE9F145A1DC31`

The case manifests retain per-boot serial hashes and proof artifact hashes.
The C71 evidence root is:

`out/dotnet/c011ec71-promotion-decision-live-byte-threshold/`

The C71 implementation and this report are closed locally on the branch
`v1.1_DOTNET_SUPPORT`; no push is performed.
