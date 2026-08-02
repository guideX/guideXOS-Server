# NativeAOT Workstation GC first post-startup segment boundary

Status: 2026-08-01. This report records the bounded follow-up authorized after
the closed first allocation-context refill milestone. It continues real
Workstation-GC primitive-array allocations through multiple allocation-context
refills and stops at the first collector-owned heap commitment after initial
heap establishment. No collection or managed finalization was permitted.

## 1. Objective

The experiment ran one managed entry with one intended primitive-array
allocation per loop iteration. It stopped immediately after validating the
first object returned across the first post-establishment GC heap commitment
or segment transition. The immutable hard limit was 384 allocations.

The trace contained a commitment while the first collector-backed allocation
was establishing the initial segment quantum. Task 8 excludes initial startup
commitments, so that event was recorded separately and did not set the
boundary. The first later commitment was the measured boundary.

## 2. Closed first-refill baseline

The prior milestone remains closed and was preserved before this experiment:

| Metric | Closed result |
| --- | ---: |
| Primitive array | `byte[256]` |
| Source-derived object size | `0x118` / 280 bytes |
| Total allocations | 15 |
| Fast allocations | 13 |
| Real-GC/refill allocations | 2 |
| Collections | 0 |
| Managed finalizers | 0 |
| Fresh QEMU processes | 3 |

Its evidence remains under the separate ignored first-refill evidence root.
This report does not overwrite that artifact or change its stopping rule.

## 3. Runtime and collector identities

The locked identity was NativeAOT source commit
`9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`, runtime pack `9.0.0`, AMD64,
Workstation GC interface 5.3, EE interface 2, one heap, server GC disabled,
concurrent GC disabled, and background GC disabled. The active PAL archive was
`C617D95647A20862947B52A1301DF96FE9104E5A13F11BB0C016B8370DDE115F`.

The PAL hook ABI was v1, 232 bytes, capability mask `0x1FF`; the GC startup
extension was v1, 216 bytes, capability mask `0x7`. The authorized adapted
GC identity and replacement-object hashes are captured in the run manifest.

## 4. Primitive-array selection

`byte[256]` was evaluated first. With source-derived size `0x118`, it would
exercise the same small context quantum with many more refills before the next
committed boundary. The selected candidate was `byte[4096]`: it remains a
primitive, non-pinned, non-finalizable object and reaches the next committed
quantum within a small, deterministic run. It is strictly below the matching
large-object threshold of 85,000 bytes.

The managed source contains no strings, boxing, reference arrays, lists,
delegates, reflection, tasks, async operations, managed threads, or explicit
GC calls. The local array reference is discarded after native validation.

## 5. Source-derived object size

The selected object size is derived from the real array EEType at runtime:

```text
ALIGN_UP(baseSize + componentSize * length, pointerAlignment)
= ALIGN_UP(0x18 + 1 * 0x1000, 8)
= 0x1018 (4120) bytes
```

The diagnostic record reports `derivedObjectSize=0x1018`,
`requestedObjectSize=0x1018`, `sourceSizeValid=1`,
`belowLargeObjectThreshold=1`, and `largeObjectCount=0`.

## 6. Hard allocation limit

The immutable limit was `384` allocations. It was embedded in the
segment-boundary artifact before QEMU execution and was never increased by the
managed loop or native diagnostics. The observed boundary occurred at
allocation 16, leaving a large safety margin. The run therefore did not test
segment exhaustion, heap exhaustion, physical-memory exhaustion, or a
collection boundary.

## 7. Initial segment geometry

The source-backed `heap_segment` descriptor was obtained through the collector
metadata path, including its identity and committed/reserved bounds. The
initial and boundary objects remained on the same segment:

| Field | Initial observation | Boundary observation |
| --- | ---: | ---: |
| Segment identity | `0x104014730` | `0x104014730` |
| Segment start/base | `0x100A00028` | `0x100A00028` |
| Allocated boundary | `0x100A00028` | `0x100A00028` |
| Committed boundary | `0x100A11000` | `0x100A21000` |
| Reserved boundary | `0x100B00000` | `0x100B00000` |

The initial committed value is the post-first-allocation segment descriptor;
the separate excluded initial commitment advanced it from `0x100A01000` to
`0x100A11000`. The thread allocation context and object range were recorded
separately from segment metadata.

## 8. Refill-history design

The native record contains a fixed 128-entry scalar refill table. Each entry
records refill ordinal, allocation ordinal, previous context and limit,
remaining bytes, returned object range, segment identity and geometry, VM trace
range, collection counters, and commit fields. Overflow is a hard failure;
the table was not extended or dynamically allocated.

The first entry records the initial context-establishment commitment with
`boundaryType=0` and `vmCommitObserved=1`. The measured boundary is the ninth
refill entry, allocation 16, with `boundaryType=1` (commitment).

## 9. Fast-allocation behavior

The fast path uses the current Workstation `gc_alloc_context` bump pointer. The
object address equals the previous context pointer, the pointer advances by
`0x1018`, and the limit is unchanged. Seven allocations used this path.

## 10. Refill behavior

The rare path follows the real chain:

```text
ManagedMain
  -> RhpNewArray
  -> guideXosStockRhpNewArray
  -> RhpNewArrayRare
  -> RhpGcAlloc
  -> GcAllocInternal
  -> WKS::GCHeap::Alloc
  -> allocation-context publication
  -> object return
```

Nine real collector/refill entries were recorded. The refill history occurred
at allocations 1, 2, 4, 6, 8, 10, 12, 14, and 16. No refill was simulated,
and no allocation pointer was seeded manually.

## 11. Collector VM operations

The bounded trace began at `vmTraceStartCount=0xD` and ended at
`vmTraceEndCount=0xF`. It recorded the initial excluded commitment at trace
index `0xD`, then the measured commitment at trace index `0xE`:

| Operation | Trace index | Address | Requested | Actual | Segment committed after |
| --- | ---: | ---: | ---: | ---: | ---: |
| Initial heap establishment, excluded | `0xD` | `0x100A01000` | `0x10000` | `0x10000` | `0x100A11000` |
| First post-establishment boundary | `0xE` | `0x100A11000` | `0x10000` | `0x10000` | `0x100A21000` |

The measured commitment had old committed boundary `0x100A11000`, new
committed boundary `0x100A21000`, and no overlap with already committed heap
space. The locked startup-platform ABI exposes the exact virtual commit call;
it does not expose a physical-frame or mapping counter to the NativeAOT
diagnostic record. Consequently frame delta and mapping delta are recorded as
`NOT EXPOSED BY LOCKED PAL`, not fabricated as zero. No unexplained failed or
rolled-back commit was observed.

## 12. New commitment detection

The measured event is a successful `TraceOperation::Commit` from the
collector's `GCToOSInterface::VirtualCommit` path through `gcVirtualCommit`
and the installed GC platform commit hook. Its address lies within the
source-described segment reservation, and the source segment committed bound
increased by the exact `0x10000` request. Metadata, card-table, worker-stack,
page-table-only, and unrelated native allocations were not classified as the
boundary.

## 13. Segment-transition detection

Segment identity is obtained from the source-backed `WKS::heap_segment` lookup
for each returned object. The transition count was zero; the old/new segment
identity fields therefore are `0x104014730` / `0x104014730` and no segment
transition boundary was claimed. A new allocation context inside the same
segment was not classified as a transition.

## 14. First boundary reached

The first counted boundary was a new heap commitment within the same segment:

```text
boundaryType                 = commitment
boundaryAllocationOrdinal   = 16
boundaryRefillOrdinal       = 9
boundaryCommitAddress       = 0x100A11000
boundaryCommitRequested     = 0x10000
boundaryCommitActual        = 0x10000
committedBefore             = 0x100A11000
committedAfter              = 0x100A21000
segmentTransitionCount      = 0
```

The native loop status changed to stop only after the triggering allocation
returned and was validated. No later managed allocation was requested.

## 15. Stop object

The stop object was `0x100A0F250`, with end `0x100A10268`, length 4096, and
source-derived size `0x1018`. Its previous context was
`0x100A0F238` with limit `0x100A10110`; after return the published context was
`0x100A10268` with limit `0x100A12130`.

The object was non-null, correctly aligned, had the expected primitive-array
EEType and length, passed full zeroing and deterministic pattern readback,
preserved its header and length, and passed the no-overlap and no-write-beyond
checks. `boundaryStopObserved=1` and `noPostRefillAllocation=1`.

## 16. Heap and segment ownership

The collector's `IsHeapPointer`/range ownership path described the stop object
as owned by the same source segment identity `0x104014730`. The object range
was within the segment reservation and the committed heap range after the
commit. No rounded-address comparison was used to infer ownership.

## 17. Zeroing and pattern validation

The selected array's logical data bytes were all initially zero. The bounded
native validator then confirmed a deterministic pattern without creating a
managed diagnostic object. Zeroing, pattern, layout, alignment, ownership,
overlap, monotonicity, and context-geometry failure counts were all zero.

## 18. Collection and suspension gates

The run reported nine allocation opportunities considered for collection and
zero requests or entries:

| Counter | Result |
| --- | ---: |
| Collection considered | 9 |
| Collection requests | 0 |
| Collection entries | 0 |
| Collections entered | 0 |
| Suspension requests | 0 |
| GC count before/after | 0 / 0 |
| Finalization scans | 0 |
| Managed finalizers | 0 |
| Mark/plan-relocate/sweep entries | 0 / 0 / 0 |

The collector was allowed to take its ordinary allocator locks. It was not
allowed to enter marking, planning, relocation, sweeping, background GC, or
finalization.

## 19. Helper/finalizer state

The initial runtime thread remained attached with valid NativeAOT TLS and
transition-frame state. The finalizer worker remained parked, helper wake count
was zero, and the managed entry count was one. Runtime-level orderly shutdown
and same-process reinitialization remain unsupported; disposable QEMU process
teardown is the lifetime boundary.

## 20. First QEMU run

The authoritative runner was
`scripts/smoke-nativeaot-gc-multiple-refills-first-segment-boundary-qemu.ps1`.
Its latest manifest is under the ignored run root
`out/dotnet/gc-multiple-refills-first-segment-boundary/`.

| Artifact | SHA-256 |
| --- | --- |
| Experiment PE | `C2AA7C100309B31FF16E6091CB718540699783A334CBF6D97C10CBF1804CC196` |
| Converted ELF | `21E8C71F26A834E9778F5D616FB1BBE035C5747F2F52694630B83F1C78071D5D` |
| Adapted boundary GC archive | `7092E01A87656A5A8337D4FE0AA233FF7F6F833A912BAB5FFF87A7F4F6077CBC` |
| Embedded artifact object | `12FDB3B073ADF5910089A9E7A60FAA9BB47AC5B1D67D051746D3996FBADFD13E` |
| Specialized kernel | `1B386A9D4233E6AF0FF726F51036113CE98D54B0DFE53EB43A5A2007E3B01AAC` |
| Runner | `9DB8C8162370F81BCA0EE11E071EBFBC5A7C8517298378D9C881F77C9380FE76` |
| Active PAL archive | `C617D95647A20862947B52A1301DF96FE9104E5A13F11BB0C016B8370DDE115F` |

## 21. Fresh-process reproducibility

Three fresh QEMU processes used the same PE, ELF, specialized kernel, and
ESP kernel. All produced `ALL_PASS`, the same boundary type and ordinal, the
same refill count, equivalent same-segment geometry, valid stop-object
ownership, zero collection/finalization, and process teardown PASS.

| Process | Serial SHA-256 |
| --- | --- |
| first-run | `B7DAE8E31DC114D7E0DF9C774117334F7572D5A336B858E7B43607A5B398FB7E` |
| repeat-1 | `18A917D73F90791DA1B4004EF303265B2330E0DA862D589B75F44244C915BCEF` |
| repeat-2 | `C93C3D7F197C08705B38A42C8F9F73982E7BA9D7873C78585AD1922DED3AA7C9` |

The serial hashes differ only because timer IRQ text can interleave with
diagnostic output; normalized validation passed in all three processes.

## 22. Process teardown

`Process teardown: PASS` was emitted for each run. Same-process orderly GC
shutdown is explicitly `NOT SUPPORTED`; the runner terminated each disposable
QEMU process after validation and restored the ordinary kernel in `finally`.

## 23. Regression results

The preserved closed regression baseline remains:

| Regression | Result |
| --- | --- |
| 4 KiB bounded no-collection proof | 14 / controlled OOM PASS |
| 64 KiB bounded no-collection proof | 234 / controlled OOM PASS |
| Startup-only Workstation GC | PASS |
| First collector-backed allocation | PASS |
| First subsequent refill | 15 total / 13 fast / 2 real-GC PASS |
| HostLog | PASS |
| Runtime-pack identity/state | PASS |
| Generic ELF | PASS |
| Inventory isolation | PASS |
| Dedicated QEMU foundations | PASS in preserved baseline |
| Hosted runtime foundations | PASS in preserved baseline |
| PE-to-ELF zero-fill | PASS in preserved baseline |

The new runner itself passed its three-process regression gate. Existing
milestone evidence was not overwritten.

## 24. Historical fault/WER status

The historical `0xC0000419` remains nonreproducible and has no source-backed
association with this experiment. The preserved provenance audit recorded one
correlated `0xC0000005` WER event for PID 21872; it did not correlate to the
current guest result and did not change the prior bounded proof classification.
No current boundary run reported a fault marker.

## 25. Decision outcome

**Outcome A — first post-startup GC heap commitment succeeds.** Multiple real
allocation-context refills succeeded, the first later commitment was traced to
the collector's own VM commit path, the triggering object was valid and
collector-owned, no allocation followed it, and no collection, suspension for
collection, or managed finalization occurred. Three fresh QEMU processes
reproduced the result.

## 26. Exact next experiment

Because the first boundary was a commitment within the same segment, the exact
next experiment is:

> Continue bounded allocations until the first GC segment transition, without
> allowing collection.

That is a new experiment and is not performed by this pass. Runtime shutdown,
collection, full segment exhaustion, and post-boundary allocations remain
outside scope.

## Validation report

| Field | Result |
| --- | --- |
| Runtime identity | PASS; locked NativeAOT/runtime-pack identity |
| Collector identity | PASS; Workstation, one heap, nonconcurrent/nonbackground |
| Selected array length | `4096` |
| Source-derived object size | `0x1018` / 4120 |
| Large-object path avoided | PASS |
| Hard allocation limit | `384` |
| RhInitialize result | `0` / PASS |
| Managed entries | `1` |
| Allocation requests | `16` |
| Fast allocations | `7` |
| Rare-path entries | `9` |
| Real-GC allocations | `9` |
| Context refills | `9` |
| Initial segment identity | `0x104014730` |
| Initial segment start | `0x100A00028` |
| Initial allocated boundary | `0x100A00028` |
| Initial committed boundary | `0x100A11000` |
| Initial reserved boundary | `0x100B00000` |
| Commit events after startup | `1` |
| First commit event iteration | `16` |
| Old committed boundary | `0x100A11000` |
| New committed boundary | `0x100A21000` |
| Commit requested bytes | `0x10000` |
| Commit actual bytes | `0x10000` |
| Commit frame delta | NOT EXPOSED BY LOCKED PAL |
| Segment transitions | `0` |
| First transition iteration | none |
| Old/new segment identity | same `0x104014730`; no transition |
| Boundary type | commitment |
| Boundary iteration | `16` |
| Boundary refill ordinal | `9` |
| Stop object | `0x100A0F250` |
| Stop object length | `4096` |
| Stop object size | `0x1018` |
| Stop object zero bytes | full logical data, PASS |
| Stop object pattern | PASS |
| Stop object ownership | PASS |
| Stop object segment | `0x104014730` |
| Overlap/layout/zero/pattern/ownership failures | `0 / 0 / 0 / 0 / 0` |
| Collection considered | `9` |
| Collection requests | `0` |
| Collections entered | `0` |
| Suspension requests | `0` |
| Mark/plan-relocate/sweep entries | `0 / 0 / 0` |
| Finalization scans | `0` |
| Managed finalizers | `0` |
| Helper state | finalizer worker parked; helper wakes `0` |
| Process teardown | PASS |
| Fresh QEMU runs | `3` |
| Fresh-process reproducibility | PASS |
| Startup regression | PASS, preserved baseline |
| First-allocation regression | PASS, preserved baseline |
| First-refill regression | PASS, preserved baseline |
| 4 KiB proof | `14` / controlled OOM PASS |
| 64 KiB proof | `234` / controlled OOM PASS |
| HostLog | PASS, preserved baseline |
| Generic regressions | PASS, preserved baseline |
| Historical `0xC0000419` recurrence | no |
| Correlated `0xC0000005` event | preserved PID 21872 provenance record; not current-run correlated |
| Normal kernel restored | PASS |
| Restored kernel hash | `D68791B66BF268425B6E646F3EA94CE7B3777B97D9CCED6682C10A9E1389066C` |
| Evidence ignored | PASS; new evidence root added to `.gitignore` |
| `git diff --check` | PASS |

## First segment-transition gate - 2026-08-01

The next bounded experiment is documented in
[NATIVEAOT_WORKSTATION_GC_FIRST_SEGMENT_TRANSITION.md](NATIVEAOT_WORKSTATION_GC_FIRST_SEGMENT_TRANSITION.md).
The locked source gate classified the selected 4120-byte `byte[4096]` object
as SOH and showed that standalone Workstation GC reaches collection handling
before a new SOH segment can be selected. The new-segment path was therefore
not entered.

Three fresh QEMU runs used a fixed no-collection cap of 32 allocations, 20
context refills, and 4 post-startup commit observations. Each recorded 32
allocations (`15` fast / `17` rare), 17 refills, two post-startup commits,
zero collection requests/entries/suspensions, zero segment transitions, the
same segment identity `0x104014730`, and a valid stop object. A commit-boundary
flag was observed inside the same segment, but `boundaryStopObserved` remained
zero; this is not a segment transition.

The single decision is **Outcome B — collection is required before a SOH
segment transition**. An overlong 246-allocation calibration probe entered six
collections and was discarded; it is not part of the authoritative evidence.
The exact next experiment is a first-GC collection-readiness audit, not
collection execution. The ordinary kernel was restored to
`D68791B66BF268425B6E646F3EA94CE7B3777B97D9CCED6682C10A9E1389066C`.
