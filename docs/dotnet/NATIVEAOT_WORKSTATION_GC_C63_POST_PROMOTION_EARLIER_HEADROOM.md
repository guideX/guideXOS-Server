# C011EC63 — Post-Promotion Earlier-Headroom Frontier

Date: 2026-09-01
Repository: `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT`
Result: **Outcome D / Success Level 1**

## Executive result

C63 tested the exact next-smallest experiment recommended by the committed C62
report: retain the authentic `PROMOTE -> GEN1-DEBIT` event and change only the
ordinary survivor/cohort shape that precedes the restart, using C61's smaller
P2 schedule together with C62's bounded R2 tail.

All three fresh QEMU boots agreed semantically. C63 naturally observed:

`PROMOTE 0x15 -> GEN1-DEBIT 0x19 -> RestartEE/resume -> rare/refill allocation`

It did **not** observe a post-debit normal `n_initial=0` policy entry, B02, a
later N2 decision, or any allocator use of the reclaimed tail. The post-GC
allocation-context snapshot was valid but empty; the first ordinary managed
allocation succeeded through the normal rare/refill path in a current gen0
region. This is Outcome D, not Outcome A/B/C: the qualifying post-debit normal
decision was not reached within the explicit safe bound, while the C62
accounting state precisely identifies what remained missing.

The C62 authoritative frontier was:

> **C62 authoritative frontier: post-debit normal `n_initial=0` eligibility/refill was not naturally reached; the reclaimed tail remained mapped and allocator-visible but ineligible, and the caller/refill topology moved directly toward full/OOS behavior before another ordinary N0 opportunity.**

Therefore:

> **C63 exact question: does the smallest earlier ordinary survivor schedule, C61 P2, preserve an eligible normal refill frontier after `PROMOTE -> GEN1-DEBIT` and before OOS N2, while retaining C62's bounded R2 tail?**

C63's answer is **no within the bound**. The schedule preserved the
promotion/debit and completed managed continuation, but the post-debit normal
frontier was still not observed.

## Locked identity and preflight

- NativeAOT / .NET `9.0.0`, AMD64, Workstation GC, interfaces `5.3 / 2`.
- Locked runtime source commit:
  `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`.
- Locked checkout:
  `out/dotnet/c52-runtime-source/source-04371d8e`.
- `gc.cpp` SHA-256:
  `5BD029B77A973145B12C142E870A549C527DAC19AEB527012F7CC6F99D362FF9`.
- Durable patch: `nativeaot-amd64-fp-handoff.patch`.
- FP patch SHA-256:
  `4185495724D48E2962BA9042AF352718BF9188032DEE4C9DE6FFE9F145A1DC31`.
- Canonical ordinary kernel/ESP SHA-256:
  `75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6`.
- Starting branch: `v1.1_DOTNET_SUPPORT`.
- Starting HEAD:
  `f888dcea0b3c1718bab478d28104882b1ada1d8e`,
  `Trace NativeAOT post-promotion refill topology`.
- Starting upstream: `origin/v1.1_DOTNET_SUPPORT`, `0 ahead / 0 behind`.
- Starting worktree: clean.

C63 changed only the proof workload, bounded observational diagnostics, the
smoke harness, and this report. The locked source checkout and production GC
policy/allocator were not edited. The source-injection audit reports
`policyMutation=false`, `allocatorMutation=false`, `forcedGc=false`, and
`oosSuppression=false`; the semantic rewrite guard is PASS.

## C57 -> C62 reconstruction

The following table is reconstructed from the live parent chain, committed
reports, harness modes, and their retained evidence. “Not proved” is kept
separate from “observed.”

| Milestone / commit | Outcome / level | Central question and exact proof | Not proved / first blocker | Next smallest milestone |
| --- | --- | --- | --- | --- |
| **C57** `a48a5a73a2751a307cccb3287803cab9dab2221e` — `Trace NativeAOT gen1 budget versus OOS escalation` | C / 1 | S2 showed B12/OOS-SOH winning while gen1 budget remained positive; the natural caller supplied `n_initial=2`. | B02/direct-gen1 was skipped; the mapped gen1->gen2 tail had no allocator lifecycle use. | Produce a later normal `n_initial=0` decision without changing policy/OOS. |
| **C58** `6497d5624f92727f2ac5740359dc2dc35197b410` — `Add n_initial provenance diagnostics for workstation GC` | A / 2 | Three boots established `n0=2, n1=0, n2=1` provenance and the `n0 -> n2` caller path; B02 remained zero and B12 was eligible in the relevant predecessor record. | Tail remained visible but not eligible, considered, selected, or consumed. | Trace the timing of the last normal N0 and its gen1 budget. |
| **C59** `bffaba0c95642a424c588024180bed2de7b87041` — `Trace NativeAOT last-N0 gen1 budget window` | C / 1 | The last normal N0 retained signed gen1 budget `0x1AD658` (+1,758,808), so B02 did not cross. | No B02 evaluation occurred at the decisive entry; N0,N0,N2 still ended in N2. | Make B02 true at the relevant entry while preserving N0/N2 provenance. |
| **C60** `fc5023d7247f57c86d8261f3e459c9a413fefe83` — `Trace NativeAOT gen1 promotion budget timing` | C / 2 | Source accounting showed the useful gen1 promotion/debit chronology. | The promotion arrived too late for the final normal N0 budget decision. | Move natural promotion earlier, before final N0. |
| **C61** `3256b51aa43d9f7ba8c411f9f52a9b26e5b4f8c7` — `Trace NativeAOT promotion versus final-N0 topology` | C / 2 | P1/P2 naturally produced gen1 promotion and its debit, with source events `0x15` and `0x19`; survivors moved gen0->gen1 and the later topology remained valid. | The promotion-derived debit still followed final N0; the next policy entry went toward N2. The tail was visible/mapped but not eligible or used. | Make one smaller survivor-lifetime/cohort change before the post-promotion normal N0. |
| **C62** `f888dcea0b3c1718bab478d28104882b1ada1d8e` — `Trace NativeAOT post-promotion refill topology` | C / 2 | R0/R1 reproduced `PROMOTE 0x15 -> GEN1-DEBIT 0x19`, restart/resume, and a successful first rare/refill allocation. | No post-debit normal N0 was preserved; the context was empty, the tail was ineligible, and R2 was unrun. | Run one bounded R2-style earlier-headroom shape before considering B02/direct-gen1 work. |

The reconstructed collection/planner progression is Workstation gen0/N0
entries followed by the natural full/OOS `n_initial=2` topology when that path
was reached. C57-C62 did not justify changing condemnation policy, the
allocator, segment metadata, OOS behavior, or the durable FP repair.

## Source-level production mechanism

The audit used the locked `src/coreclr/gc/gc.cpp` above. The causal path is:

| Function and lines | Relevant input / conditional | Result and next consumer |
| --- | --- | --- |
| `GCHeap::Alloc`, `49905-49997` | Ordinary SOH allocation enters the heap and calls the heap's allocator. | `allocate()` receives the request and eventually reaches `allocate_soh`. |
| `gc_heap::try_allocate_more_space`, `18949-19060` | For gen0, `new_allocation_allowed(gen_number)` is checked; if false, the production path calls `trigger_gc_for_alloc(0, reason_alloc_soh, ...)`, then calls `allocate_soh`. | The caller/refill topology, not C63, decides whether a collection is requested. |
| `gc_heap::allocate_soh`, `17982-18255` | The finite state machine asks `soh_try_fit`; failure can lead to an ephemeral GC, a second ephemeral GC, or a full compacting GC. | `soh_try_fit` and the ordinary refill state feed the next allocation state. |
| `gc_heap::soh_try_fit`, `17896-17979` | Tests the production free-list/segment-end fit for the requested generation and size. | A successful fit supplies the active region; C63 only records the result. |
| `gc_heap::generation_to_condemn`, `21486-21607` | Starts with `n_initial`; the signed budget loop tests `get_new_allocation(i) <= 0` only for `i = n + 1` onward. | The selected generation is passed into the collection settings. A caller that already supplies `n_initial=2` on this two-generation heap has no lower-generation B02 loop to traverse. |
| `gc_heap::try_get_new_free_region`, `21345-21371` | Checks the basic free-region structure, otherwise attempts a new region and returns it to the free-region structure. | C63 distinguishes this callback from the predecessor's scalar free-region classification; the first C63 refill did not reach this callback. |
| `gc_heap::fix_generation_bounds`, `34528-34631` | Under `USE_REGIONS`, finalizes regions and publishes gen0's start as the ephemeral heap segment. | The latest C54 snapshot shows the gen0 boundary moved while the active allocation segment identity stayed the same. |
| `gc_heap::adjust_ephemeral_limits`, `14003-14017` | Its assignments are under `#ifndef USE_REGIONS`. | It is a no-op in this locked regions build; C63 records `adjustEphemeralObserved=0`. |
| `GCHeap::GarbageCollectGeneration`, `50960-51040` | Receives a production collection request and performs the normal suspend/collection entry. | C63 inherits the restart/resume proof and does not call this entry point from managed code. |

The signed budget type is `ptrdiff_t` (`gc_heap::get_new_allocation`,
`gc.cpp:8137-8139`; `new_allocation_allowed`, `8085-8095`). The relevant raw
gen1 value before the C62 promotion debit is `0x1AD658`, interpreted as
`+1,758,808`, not as an unsigned large value. The promotion debit publishes
`0xCD508`; C63 did not observe a later post-debit N0 record at which B02 could
be evaluated.

## C63 workload and bounds

C63 uses a proof-only wrapper around the existing C62 scalar observer. It
selects the already-audited C61 P2 shape:

- 16 early survivor references;
- two main survivor cohorts, 16 survivors per cohort;
- bounded ordinary managed allocation pressure;
- the C62 R2 tail: 192 ordinary allocations of `0x4000` bytes;
- no explicit or internal GC request and no retained reference fabricated for
  the purpose of the diagnostic.

The fixed C63 bounds emitted by `C011EC63-PREFLIGHT` are:

| Bound | Value |
| --- | ---: |
| maximum allocation count | `0x160` = 352 |
| maximum aggregate allocated bytes | `0x1600000` |
| maximum individual allocation | `0x10000` = 65,536 bytes |
| maximum survivor references | `0x30` = 48 |
| maximum survivor cohorts | `0x3` = 3 |
| maximum observed collections | `0x100` = 256 |
| maximum diagnostic records/events | `0x200` records / `0x100` events |
| maximum serial output | `0xC00000` = 12 MiB |
| maximum QEMU runtime | `0x5A` = 90 seconds per boot |
| maximum retries per boot | 1 |
| fresh independent boots | 3 |

The workload marker is:

`strategy=R2 c61Schedule=P2 earlierHeadroomShape=2-main-cohorts-x-16-survivors c62Tail=192-x-0x4000 ordinaryManagedAllocations=0x160 maxAggregateBytes=0x1600000`

The workload is natural: managed code performs bounded ordinary allocations,
retains/drops ordinary survivor references, and lets the runtime choose
collection, generation, compaction, region selection, and refill behavior. It
does not call `GC.Collect`, an internal GC entry point, or any policy override;
it does not write budgets, generation boundaries, ephemeral/allocation
segments, free lists, mark bits, or historical addresses.

## C63 decisive observations

All three boots emitted the full C63 lifecycle marker set:

`PREFLIGHT, C62-FRONTIER, SOURCE, WORKLOAD, POLICY, COLLECTION, GEN-BEFORE, GEN-AFTER, SEGMENT, ELIGIBILITY, CONSIDERED, SELECTED, CONSUMED, RESTART, RESUME, COMPLETE`

The common C62 scalar observer result was:

- promotion observed: `1`, source event `0x15`;
- promotion bytes: `0x10018`;
- gen1 debit observed: `1`, source event `0x19`;
- debit-producing collection ordinal: `0x3`;
- debit bytes: `0xE0150`;
- desired gen1 allocation: `0x1CDB68`;
- gen1 budget before debit: raw/signed `0x1AD658` / `+1,758,808`;
- published gen1 new allocation after debit: `0xCD508`;
- event count: `0x0C`, overflow `0`.

The post-GC allocation context was valid (`contextValid=1`) but had pointer
`0`, limit `0`, and remaining bytes `0`. The first managed request was
`0x10018` bytes, aligned size `0x10018`, generation/domain gen0/SOH. It did
not fit the stale context (`contextFit=0`), took the rare/refill path
(`fast=0`, `rare=1`), reached `soh_try_fit` with result `1`, reached
`allocate_soh` with result `1`, and completed successfully. The observed
supplying region was `[0x101300028, 0x101400000)` with `0xD0F90` remaining.
This proves a successful ordinary refill, not tail consumption.

No C63 post-debit normal N0, B02, N2 commit, or first-N2 marker was observed.
The C62 R0 historical N2 comparison had `n_initial=2`, reason `5`, and
`last_gc_before_oom=1`; C63 did not claim that historical N2 event as a new
C63 event. C63's bounded result is therefore “promotion/debit reached, next
normal N0 frontier not reached,” not “N2 was selected.”

The latest available C54 policy snapshot records maximum generation `2`,
condemned generation `0`, gen0 desired/new values
`0x286540` / raw `0xFFFFFFFFFFFF5D90` (signed `-41,584`), gen1
desired/new values `0x1CDB68` / `0x1AD658` (signed `+1,758,808`), and gen2
desired/new values `0x40000` / raw `0xFFFFFFFFFFFFE0A8` (signed `-8,024`).
The recorded elevation, low-ephemeral, high-memory, very-high-memory, and
high-fragmentation selection flags were zero for this snapshot. C63 does not
invent a collection-reason value for the gen0 snapshot; the only decisive
full/OOS reason available in the inherited C62 comparison is reason `5` on
the historical `n_initial=2` entry noted above.

## Generation and segment state

The C63 snapshots are the latest authoritative C54 collection snapshot
available at the safe point (`collectionOrdinal=0x2`, `condemnedGeneration=0`)
and are explicitly not mislabeled as the debit collection's own before/after
snapshot.

### Generation boundaries

`C011EC63-GEN-BEFORE`:

- gen0 start: `0x100A00028`;
- gen1 start: `0x100900028`;
- gen2 start: `0x100800028`;
- allocation start/limit: `0x101300028` / `0x101300028`;
- allocation segment: `0x104010CF8`;
- ephemeral segment: `0x104010CF8`.

`C011EC63-GEN-AFTER`:

- gen0 start: `0x101300028`;
- gen1 start: `0x100900028`;
- gen2 start: `0x100800028`;
- allocation start/limit: `0` / `0` in the post-collection snapshot;
- allocation segment: `0x104010CF8`;
- ephemeral segment: `0x104010CF8`.

Thus `fix_generation_bounds` was observed and the gen0 boundary moved; the
allocation/ephemeral segment identity did not change. `adjust_ephemeral_limits`
was not observed because it is compiled out under `USE_REGIONS`, consistent
with the locked source. A temporary zero allocation pointer/limit is reported
as an empty post-GC publication, not as a zero segment identity.

### Reclaimed-tail identity and lifecycle

C63 rediscovered the semantic tail as:

- range `[0x100900028, 0x100943000)`;
- size `0x42FD8`;
- segment `0x104010668`;
- tail generation `1 -> 1` in the decisive C63 snapshot;
- allocated range before `[0x100900028, 0x100943000)`;
- committed range before ending at `0x100943000`;
- allocated range after ending at `0x1009F07F0`;
- committed range after ending at `0x1009FE000`;
- reserved end `0x100A00000` before and after.

The segment marker reports `segmentRetired=0` and `segmentRecycled=0`. The
applicable lifecycle classification is **1 — same segment, generation
boundary moved** for the active allocation segment; the reclaimed tail itself
did not undergo a C63 ownership transition. C62's earlier R0 report recorded a
later `1 -> 2` tail transition in its own subsequent collection chronology;
C63 does not relabel its unchanged C54 snapshot as that later event.

The causal chain stopped at:

`natural survivor promotion -> source gen1 debit -> empty post-GC allocation context -> ordinary gen0 rare/refill -> current gen0 region`

The tail remained genuinely reclaimed and allocator-visible, but its gen1
ownership/domain did not make it eligible for the current gen0/SOH request.

## Allocator lifecycle distinctions

The six required states remain separate:

| State | C63 result | Evidence |
| --- | --- | --- |
| reclaimed | observed | The inherited C40/C54 lifecycle reports dead capacity in the tail range. |
| allocator-visible | observed | `allocatorVisible=1`; the segment/range remains published and mapped. |
| eligible | **not observed** | `eligible=0`; tail generation/domain is not the active gen0 allocation domain. |
| considered | **not observed** | `considered=0`; no candidate range was passed to the C63 considered marker. |
| selected | **not observed** | `selected=0`; candidate start/end are zero. |
| consumed | **not observed** | `consumed=0`; the first object address is recorded only as an ordinary survivor/observation and does not overlap the tail. |

There is no exact allocator rejection callback to report because the range was
not eligible and therefore was not considered. The source-backed reason for
non-progression is domain/ordering: the active request is gen0/SOH, the tail
is still a gen1-domain range, and the caller/refill state did not produce the
normal post-debit N0 opportunity required to evaluate a new eligible candidate.
The first allocation's successful region is not direct reuse. No recycled
reuse was observed either.

## Collection, planner, restart, and safety state

- C63 collection marker: `collectionCount=2`, `plannerValid=1`,
  `fixBoundsObserved=1`, `adjustEphemeralObserved=0`.
- C63 `GEN-BEFORE/AFTER` reflects a normal gen0 collection boundary and the
  active segment stayed `0x104010CF8`.
- No new C63 direct-gen1 compaction, relocation, sweeping, segment retirement,
  or segment recycling claim is made.
- `RestartEE` / restart validity: `1`.
- Managed resume: `1`.
- Live-object integrity: `1`.
- Inherited C26 root enumeration: PASS, including the established four-root
  evidence; C63 did not fabricate roots or seed mark state.
- C18 valid-path and fail-closed code-manager checks: PASS.
- `CoffNativeCodeManager`: valid on the inherited authentic managed path.
- `FindMethodInfo`: PASS / `1`.
- C28 mark closure: PASS; no mark queue invariant failure and no sensitive
  diagnostic allocation.
- Survivor movement/sentinel/readback: PASS; inherited survivors moved
  naturally gen0->gen1 and readback remained valid.
- Invariant failures: `0`.
- Sensitive diagnostic allocations: `0`.
- Fail-fast count: `0`.
- Page-fault count: `0`.
- Former near-null reverse-P/Invoke root fault around `0xFFFFFFFFFFFFFF90`:
  not observed.

## Three fresh QEMU boots

QEMU: `11.0.0 (v11.0.0-12122-ga4bb4b10c9)`.

| Boot | C63 outcome / level | Promotion/debit | Post-debit N0 / B02 / N2 | Restart/resume | Invariants / sensitive alloc / fail-fast / page fault | Serial SHA-256 |
| --- | --- | --- | --- | --- | --- | --- |
| `first-run` | D / 1 | `0x15 -> 0x19` | `0 / 0 / 0` | `1 / 1` | `0 / 0 / 0 / 0` | `AA64D5346694D86051CA9FA517E3684774236789FD1C712000D221BEBD187BAB` |
| `repeat-1` | D / 1 | `0x15 -> 0x19` | `0 / 0 / 0` | `1 / 1` | `0 / 0 / 0 / 0` | `94637128D21053C5A09C62872BCC636845A3E81ACC0CF1A3AA1A4A7D6E3B478A` |
| `repeat-2` | D / 1 | `0x15 -> 0x19` | `0 / 0 / 0` | `1 / 1` | `0 / 0 / 0 / 0` | `3DC3F1BFCD2BCA6FCF9FBDDE299D2420E9AB0BE1D2BC468D8DF56A83A9D15A70` |

Semantic agreement: **PASS**. All three boots emitted identical semantic C63
workload, policy, generation, segment, eligibility, restart/resume, and
completion fields; addresses were stable in this image but were not used as
hard-coded targets.

## Validation and restoration

The single focused C63 harness run completed managed build, NativeAOT link,
runtime-pack/native build, PE->ELF conversion, three independent QEMU boots,
marker parsing, and `finally` restoration successfully. The final manifest
reports PASS for the locked source/runtime identity, FP patch identity,
productionized runtime-pack, semantic rewrite guard, C18/C26/C28/C34/C37/C39/
C40/C41/C53/C54/C55/C56/C57/C58/C61/C62 predecessor gates, linker/source/table
guards, and QEMU semantic agreement.

Proof payload hashes from the authoritative C63 manifest:

- proof kernel: `348E3824D694027334ED2169F28672465E907B5F1F239DD3DCE5FF7AA1E29B1B`;
- managed PE: `1A940AE02CAE3266DEA55BBC0B9DCD6A298D63FAA5FC03FC398460D9FA7D0D7B`;
- ELF: `3280937A6AAC7E918FB774EC1F635CC375B08D895F1955A64614CF913EAB5493`;
- map: `C4C33CEC45308087EF3D9B128260BE05D4145452A97385DB4E759602C7362D31`.

Additional focused checks were PASS:

- managed build: PASS;
- relevant runtime-pack/native build: PASS;
- PowerShell parser for the modified smoke script: PASS;
- JSON/XML parsing for structured project/manifest files: PASS;
- PE->ELF validation: PASS;
- linker, source, P/Invoke table, archive, and production runtime-pack guards:
  PASS;
- `git diff --check`: PASS;
- C52 Tier-All: **not required** because C63 changes no production runtime
  semantics; focused identity, rewrite, artifact, and three-boot validation
  was used.

After proof execution, the ordinary kernel and ESP both matched the canonical
SHA-256 `75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6`.
The proof-only artifact is inactive. Cleanup stopped only C63-owned QEMU
processes; unrelated QEMU instances were preserved.

## Evidence and handoff

Authoritative evidence root:

`D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT\out\dotnet\c011ec63-post-promotion-earlier-headroom\run-20260901-053608685`

The root contains `manifest.json`, `commands.txt`, the three serial logs and
their hashes, proof payloads, build/link logs, PE/ELF inspection, and restored
artifact hashes.

C63's exact remaining limitation is narrow: the managed workload preserved
the C62 promotion/debit but did not preserve a post-debit ordinary
`n_initial=0` policy/refill frontier before the bounded tail ended. The tail
was visible and mapped but not eligible, considered, selected, or consumed;
no direct or recycled reuse claim is justified.

The next-smallest milestone remains one bounded ordinary workload perturbation:
change a single pre-restart/in-flight allocation-timing or earlier-headroom
parameter toward a nonempty post-debit normal N0 frontier, then recheck
`POST_DEBIT_N0`. Do not begin B02/direct-gen1 work, alter allocator policy, or
force a collection until that normal frontier is naturally observed.
