# NativeAOT Workstation GC reclaimed generation-1 lifecycle

## Result

C011EC53 returned to the C40/C41 allocator question on the productionized C52
NativeAOT runtime. It reproduced an authentic reclaimed generation-1 range and
then observed two later authentic Workstation GC collections. The range stayed
allocator-visible, mapped, and generation 1, while the bounded ordinary
generation-0 workload remained on a separate ephemeral segment. No allocator
eligibility, consideration, selection, or consumption of the recovered range
was observed.

**Outcome C — lifecycle advanced but remained unavailable.**  
**Success Level 2 — reclaimed-range lifecycle advanced.**

This is a precise non-reuse result. C53 proved:

`reclaimed -> allocator-visible -> not eligible for the observed request domain`

It did not promote that state to consideration, selection, or consumption.

## Historical context

C40 established authentic compaction reclamation of the semantic range
`[0x100900028, 0x100943000)`, size `0x42FD8`, owned by generation 1 on segment
`0x104010668`. C41 then traced ordinary resumed `byte[64]` allocations and
showed that they used a generation-0 ephemeral allocation domain on a separate
segment; reclaimed, eligible, considered, selected, and consumed were kept as
separate states.

C42 attempted to continue this lifecycle investigation, but exposed the
transition-frame/unwind defects that led through C43-C52. C42 remains excluded
from the C52 release gate. C53 uses the productionized C52 runtime and bounded
observational probes; it does not enable the old C42 mode wholesale and does
not introduce the C46-C48 semantic harness rewrites.

## Locked runtime and release precondition

The authoritative run used:

| Property | Value |
|---|---|
| NativeAOT | `9.0.0` |
| Architecture | AMD64 |
| GC | Workstation |
| GC interfaces | `5.3 / 2` |
| Runtime source | `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3` |
| Durable FP patch | `nativeaot-amd64-fp-handoff.patch` |
| FP patch SHA-256 | `4185495724D48E2962BA9042AF352718BF9188032DEE4C9DE6FFE9F145A1DC31` |

C52 source/patch locks, the semantic rewrite guard, productionized FP path,
C18 fail-closed checks, and the absence of stale proof-only runtime semantics
all passed. The C53 harness also built a fresh runtime pack and ran three fresh
QEMU boots. C52 Tier A was rerun and passed; its partial invocation reports an
aggregate Outcome D by design, so it is recorded as **Tier A PASS**, not as a
C53 lifecycle outcome. Tiers B-D were not rerun as separate C52 invocations:
C53 changed only bounded diagnostics and the managed workload, not production
GC source, the runtime-pack build, or the locked patch. The C53 proof itself
performed the fresh production runtime-pack build, PE-to-ELF conversion,
linker/source/table checks, three boots, and ordinary-artifact restoration.

## Fresh reclaimed range

The current-run equivalent of the C40 recovered range was:

| Field | Value |
|---|---:|
| Start | `0x100900028` |
| End | `0x100943000` (exclusive) |
| Size | `0x42FD8` (`274392` bytes) |
| Heap | `0x1017F380` |
| Segment | `0x104010668` |
| Generation before/after reclamation | `1 / 1` |
| Producing event | C40/C37 Collection 2 compaction/reclamation |
| Representation | C40 compacted SOH frontier/free tail published by the locked Workstation path |

The range is not a hard-coded reuse target. C53 matched the current run's
retained segment identity and range, while the historical C40 values are used
only as reference evidence. C40's current-run retained proof includes a dead
target, six live plugs, five dead gaps, an authentic target dead gap, hole
closure, a valid reduced compacted frontier, no target destination overlap, a
cleared weak slot, and zero stale target references. The compacted frontier
moved from `0x100942068` to `0x100900028`; the published allocator-visible tail
is the range above. The neighboring live-plug source was
`[0x100901FC0, 0x100911FD8)` and its destination was
`[0x100801F20, 0x100811F38)`.

## Subsequent lifecycle

The bounded workload performed `64` ordinary managed `new byte[65536]`
allocations, retaining four survivors and validating payload readbacks. It
naturally produced two subsequent authentic collections; no collection was
forced and no internal allocator was called solely to target the recovered
range.

| Ordinal | Trigger | Condemned | Max gen | Planner | Phase | RestartEE | Managed resume | Ephemeral-boundary callback |
|---:|---|---:|---:|---|---|---:|---:|---:|
| 1 | ordinary managed `byte[65536]` allocation pressure | 0 | 2 | observed, decision `1` | `COMPACT` | 1 | 1 | 0 |
| 2 | ordinary managed `byte[65536]` allocation pressure | 0 | 2 | observed, decision `1` | `COMPACT` | 1 | 1 | 0 |

The collection reason was captured as raw locked enum value `0`; C53 does not
over-decode it. Both collection events agreed across all three boots. After
each resume, the recovered range remained on segment `0x104010668`, remained
mapped, and remained generation 1. Segment allocation metadata changed only in
the independently observed production state: before, allocated
`0x100900028`, committed `0x100943000`, reserved `0x100A00000`; after, the
same segment had allocated `0x1009400E8`, committed `0x100943000`, and reserved
`0x100A00000`.

C53's global `adjust_ephemeral_limits` observer did not fire, so C53 does not
claim a global generation-boundary transition. The C41 production context
record supplies the relevant comparison: the ordinary request used generation
0 on segment `0x104010710`, with context range
`[0x100A00028, 0x100AF1000)`. The recovered generation-1 tail stayed separate;
the C41 generation/domain mismatch therefore persisted through C53.

## Allocator state, kept separate

The C53 final state was:

| State | Result | Evidence |
|---|---|---|
| Reclaimed | yes | C40 target-dead/dead-gap/frontier/reclaimed proof |
| Allocator-visible | yes | `allocatorVisible=1`, tail remained mapped and identifiable |
| Eligible | no | `tailEligible=0` for requested generation/domain `0/0` |
| Considered | no | `tailConsidered=0`; no candidate enumeration line |
| Selected | no | `tailSelected=0`; no selection line or selected range |
| Consumed | no | `tailConsumed=0`; no object address in the recovered range |

The request was an ordinary generation-0 SOH allocation. The exact locked
production helper chain is:

`RhpNewArray -> RhpNewArrayRare/RhpGcAlloc -> GcAllocInternal -> GCHeap::Alloc -> soh_try_fit/allocate_soh`

The observed candidate path was the ordinary fast allocation context (`1`),
with the first empty-context request naturally taking the rare/refill path.
The refill came from the production allocation context, not a diagnostic
pointer, limit, segment, free-list insertion, or allocator preference. The
competing production context was the separate generation-0 segment
`0x104010710`; no unobserved candidate was promoted to a claim.

There was no eligibility transition, so there is no consideration or selection
evidence and no first consuming allocation ordinal. The exact classification is:

**Code 2 — allocator-visible reclaimed space remained outside the requested
generation/domain eligibility.**

No segment retirement, release, recycle, or indirect segment-level reuse was
observed. Direct tail reuse: no. Segment-level reuse: not claimed.

## Managed object integrity record

Because no allocation consumed the tail, the record below is the first ordinary
non-reuse object. Its zero range offset is not a reuse claim and is not
applicable to the recovered tail.

| Field | Value |
|---|---:|
| Allocation ordinal | `0` (first ordinary object; no consuming ordinal) |
| Type | `0x10321280` |
| Payload | `0x10000` |
| Aligned/object size | `0x10018` |
| Object start/end | `[0x100A10330, 0x100A20348)` |
| Pointer before/after | `0x100A10318 / 0x100A20348` |
| Limit | `0x100A12060` |
| Supplying segment/generation | `0x104010710 / 0` |
| Range offset | `0` (not applicable; object is outside the tail) |
| Sentinel | ordinal `0` marker; no sentinel failure |
| Readback | `1` |
| Managed continuation | `1`; subsequent bounded managed execution completed |

The object was initialized and read back successfully. Live-object integrity
also remained valid across the two collections, with the existing C26/C28
root/mark evidence retained.

## Source audit

The audit used the locked source snapshot
`out/dotnet/c52-runtime-source/source-c2db9088`:

- `src/coreclr/nativeaot/Runtime/amd64/AllocFast.S:190-312` — `RhpNewArray`
  fast path, rare branch, and `RhpNewArrayRare`/`RhpGcAlloc` entry.
- `src/coreclr/nativeaot/Runtime/GCHelpers.cpp:474-604` —
  `GcAllocInternal`, `RhpGcAlloc`, and the handoff to the GC allocator.
- `src/coreclr/gc/gc.cpp:49905` — `GCHeap::Alloc`.
- `src/coreclr/gc/gc.cpp:17896-18220` — `soh_try_fit` and `allocate_soh`,
  including free-list and segment-end fit behavior.
- `src/coreclr/gc/gc.cpp:14003-14017` and `22551-22556` —
  `adjust_ephemeral_limits` and its post-GC invocation/assertions.
- `src/coreclr/gc/gc.cpp:50960` — `GCHeap::GarbageCollectGeneration`.

This source explains the C41/C53 result: the normal request is allocated in
the requested generation's active SOH domain. C53 observes that production
state; it does not move boundaries, fabricate contexts, insert the reclaimed
tail into a free list, or change the allocator's preference.

## Regression and safety status

The authoritative C53 manifest retained PASS for C18, C26, C28, C34, C37,
C39, C40, C41, C43-C48, C49, C50, C51, and C52 assumptions. C18 retained a
valid `CoffNativeCodeManager` and `FindMethodInfo` result `1`; no malformed
transition frame, stale REGDISPLAY FP, zero-iterator-FP ownership fault,
`0xFFFFFFFFFFFFFF90` fault, fail-fast, or page fault occurred. Root scan
completion and four promoted roots were retained; C28 mark closure had zero
queue invariant failures. `RestartEE=1`, managed resume `1`, live-object
integrity `1`, invariant failures `0`, and sensitive diagnostic allocations
`0`.

The C53 markers are:

`C011EC53-PREFLIGHT`, `C011EC53-RECLAIMED`, `C011EC53-LIFECYCLE`,
`C011EC53-GEN`, `C011EC53-ELIGIBLE`, `C011EC53-CONSIDERED`,
`C011EC53-SELECTED`, `C011EC53-REUSE`, `C011EC53-RESUME`,
`C011EC53-ELIGIBILITY`, and `C011EC53`.

The manifest lists the capability marker names; only proven events are emitted
in the serial stream. This run emitted preflight, reclaimed, lifecycle,
resume, eligibility, and complete records. GEN, eligible, considered,
selected, and reuse were not emitted because those states were not observed.
All records are bounded fixed-size scalar diagnostics, allocation-free in
GC-sensitive paths, non-recursive, lock-safe, and fail-closed. Impossible
state combinations incremented no invariant counter.

## Three-boot evidence

Evidence root:

`out/dotnet/c011ec53-reclaimed-gen1-natural-reuse/run-20260828-132931724`

QEMU was `11.0.0 (v11.0.0-12122-ga4bb4b10c9)`. Three fresh boots reached the
C53 completion marker with semantic agreement. Serial SHA-256 values were:

1. `A6AB17F2329A87DE0DBEF96855A730D2FAEC1DCDDB0AF0688208D47685320D62`
2. `F96F17EB1840D7258F8435CABBA4EB3966DBA9D7A716F75142F7C43D338D59D4`
3. `D941045604D7DF7E1A689F72FB40216788AD32C2C357C155E252956AE17DBA53`

Proof artifact SHA-256 values:

- proof kernel: `6B2B19AE6DE7DC6BC17B217DED2FBAF19D2B73C51259F9A1894F2B57D6D233E4`
- PE: `94F61632AA739ECB206D336E1FDA111F8C6411D8BB7D558AD4C87094FF5DD56C`
- ELF: `A762C23406306D557DD0EAEB818A92CA7B8E3EB8D10DDBA224FE25813A36EBC5`
- MAP: `1C2A3E4F41674790928107BCD33ABB333A08A52B1F7D36A12CD9E1D6CF4C361E`

PE-to-ELF conversion, linker/source/table guards, PowerShell parsing, JSON
manifest parsing, semantic rewrite checks, and `git diff --check` passed.

## Restoration and next milestone

The ordinary kernel and ESP were restored and verified at:

`75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6`

The C53-owned QEMU processes exited and were cleaned up. Unrelated QEMU
instances were preserved. The proof-only artifact is not active.

The remaining limitation is that this bounded ordinary generation-0 workload
did not naturally move the recovered generation-1 tail into its allocation
domain, so eligibility, consideration, selection, and consumption remain
unproven. The next smallest milestone is a separately bounded,
production-normal generation-1-targeted allocation-domain experiment, only if
that domain is naturally reachable; it must continue to observe rather than
force a boundary, segment, free-list, address, object size, or allocator
preference.
