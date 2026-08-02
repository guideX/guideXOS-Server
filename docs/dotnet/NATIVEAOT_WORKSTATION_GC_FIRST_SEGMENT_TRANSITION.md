# NativeAOT Workstation GC First Segment Transition

## Decision

**Outcome B — collection is required before a small-object-heap segment
transition.** The source-backed Workstation-GC path does not acquire a new SOH
segment immediately after the selected segment becomes unsuitable. It first
enters collection handling. This pass therefore stops before collection and
does not execute a segment transition.

This is the final decision for this experiment. The earlier first
post-startup commitment remains a separate completed milestone; a commitment
within the current segment is not a segment transition.

## Experiment identity and safety gate

The locked identity was preserved:

- NativeAOT source commit `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`;
- runtime pack `9.0.0`, AMD64, Workstation GC;
- interface version `5.3`, EE version `2`, one heap;
- server, concurrent, and background modes disabled;
- active PAL archive SHA256
  `C617D95647A20862947B52A1301DF96FE9104E5A13F11BB0C016B8370DDE115F`;
- ordinary-kernel SHA256
  `D68791B66BF268425B6E646F3EA94CE7B3777B97D9CCED6682C10A9E1389066C`.

The selected managed object was a bounded primitive `byte[4096]` array. Its
source-derived size is `0x1018` / 4120 bytes, below the locked
`LARGE_OBJECT_SIZE` of 85,000 bytes, so every selected allocation remains on
the SOH path.

The authoritative runner is
`scripts/smoke-nativeaot-gc-first-segment-transition-qemu.ps1`. Its fixed
precollection limits are:

| Limit | Value | Purpose |
| --- | ---: | --- |
| Array length | `4096` | Primitive SOH allocation |
| Hard allocation limit | `32` | Stop before the collection budget can be entered |
| Hard refill limit | `20` | Bound rare-path/context-refill history |
| Hard post-startup commit limit | `4` | Bound current-segment commitment observations |
| Hard segment-transition limit | `1` | A transition is allowed only as an observed stopping condition |

The stop condition is native and explicit: once allocation 32 returns, the
managed loop returns 0 and makes no later allocation request. The stop reason
is `1` (hard limit), not a collection or a segment transition.

## Source-backed reconstruction

The selected archive is compiled as standalone GC. `gc/CMakeLists.txt` adds
`BUILD_AS_STANDALONE`, and `gcpriv.h` defines `USE_REGIONS` only when the
build is not standalone. The selected build therefore uses the classic
reserved-segment path, not the region-acquisition path.

For a sub-LOH primitive array, the relevant path is:

```text
GCHeap::Alloc
  -> gc_heap::try_allocate_more_space
  -> gc_heap::allocate_soh
  -> gc_heap::soh_try_fit
  -> gc_heap::a_fit_segment_end_p
```

`a_fit_segment_end_p` can commit more pages inside the current reserved
segment. If the requested object cannot fit at the current segment end, the
SOH path proceeds through collection handling (`trigger_ephemeral_gc`, with
the full-compaction path available after retry); it does not select or
reserve a new SOH segment in this standalone configuration.

The source also contains `uoh_get_new_seg`, but that is the UOH/large-object
path and is not applicable to the selected 4120-byte arrays. The source
locations used for this gate are:

- `gc.cpp`: `GCHeap::Alloc`, `try_allocate_more_space`, `allocate_soh`,
  `soh_try_fit`, `a_fit_segment_end_p`, `grow_heap_segment`, and
  `uoh_get_new_seg`;
- `gcpriv.h`: the declarations for `try_allocate_more_space`,
  `a_fit_segment_end_p`, `soh_try_fit`, `allocate_soh`, and `uoh_get_new_seg`;
- `gc.h`: `LARGE_OBJECT_SIZE`;
- `gc/CMakeLists.txt` and `gcpriv.h`: standalone and `USE_REGIONS` selection.

The transition instrumentation records the requested stage sequence
`S00` through `S14`, including allocation request, fast-capacity failure,
rare-path entry, current-segment inspection, collection-decision evaluation,
new-segment search/reserve/commit metadata stages, and the final stop-object
return. The source gate makes the new-segment stages non-enterable for this
SOH experiment without first allowing collection.

## Fresh QEMU result

Evidence is retained under the ignored root:

`out/dotnet/gc-first-segment-transition/run-20260801-220336597/`

The manifest records:

- artifact PE SHA256
  `1CBBE5E1124728EFC7A297171B0269F309750BF60182121C0C5710F09129D7DC`;
- staged ELF SHA256
  `F3B6F21116383090A558F50B831F14911AAD99788B36F77ED061B3E65B3F80CD`;
- adapted GC archive SHA256
  `619E8BEAC893B569BFF8E13610545FC14849CEF02982034810B31587BA1DDDC6`;
- specialized kernel SHA256
  `0C62F485B47F22CB37AE7155D1BF79B04A960C1C5AFAA55FBC2FEB86E6A8EB2A`;
- three fresh disposable QEMU runs, each `ALL_PASS`;
- normal-kernel restoration SHA256 equal to
  `D68791B66BF268425B6E646F3EA94CE7B3777B97D9CCED6682C10A9E1389066C`.

The representative first-run diagnostics were:

| Field | Result |
| --- | --- |
| Allocations | `32` |
| Fast / rare allocations | `15 / 17` |
| Context refills | `17` |
| Post-startup commit observations | `2` |
| Collection considered | `17` |
| Collection requested / entered | `0 / 0` |
| Collection suspensions | `0` |
| Finalization scans / managed finalizers | `0 / 0` |
| Segment transitions | `0` |
| Boundary type | `1` (commit observation only) |
| Boundary stop observed | `0` |
| Initial segment identity | `0x104014730` |
| Current segment identity | `0x104014730` |
| Initial committed boundary | `0x100A11000` |
| Current committed boundary | `0x100A31000` |
| Reserved boundary | `0x100B00000` |
| Stop object | `0x100A1F490` |
| Stop object end | `0x100A204A8` |
| Stop-object validation | zero/pattern/layout/ownership PASS |
| Process teardown | PASS; runtime-level shutdown unsupported |

The commit flag is intentionally reported separately from segment identity.
It confirms that the current segment can grow its committed boundary; it is not
evidence of a new segment. No new-segment reserve, commit, metadata
initialization, link, or allocation-context publication occurred.

## Discarded overlong probe

An exploratory 246-allocation cap was run while calibrating the safe bound. It
entered six collections and was rejected as evidence. It did not enter a new
segment, but it violated the no-collection rule for this experiment. The
authoritative runner was then reduced to the fixed 32-allocation cap above;
the manifest and decision rely only on the three fresh no-collection runs.

## Regressions

After the transition work, the following fresh checks passed:

- startup readiness: three fresh disposable QEMU launches;
- first real Workstation-GC allocation: three fresh launches;
- first subsequent context refill: three fresh launches;
- first post-startup commitment/segment-boundary: three fresh launches;
- existing 4 KiB and 64 KiB controlled-OOM proofs remain preserved by the
  prior locked evidence.

The timer-IRQ serial interleaving issue was corrected in the affected smoke
pollers by removing the complete fixed diagnostic token before checking
completion markers. Raw serial logs remain preserved for audit.

## Exact next experiment

The next experiment is a **first-GC collection-readiness audit**, not a
collection execution and not another segment-transition attempt. It should
freeze the same source/archive/PAL identity, record the allocation-budget and
`try_allocate_more_space` decision inputs, verify the current segment's
reserved/committed geometry, and stop before the first collection request.
Any future collection-entry experiment requires a separately authorized
collection contract covering suspension, roots, finalization, post-GC segment
identity, and process teardown. It must not be folded into this no-collection
transition runner.
