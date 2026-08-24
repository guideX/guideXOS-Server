# C011EC39 Collection Plan Mode Provenance

Date: 2026-08-24

## 1. Result

C011EC39 is complete at Success Level 2. The locked Workstation planner was
observed at runtime for both bounded workload variants. Collection 2 selected
compaction in every final boot because the locked planner's shared
fragmentation test crossed both thresholds and selected `compact_high_frag`.
No threshold, collection reason, generation, compacting flag, relocation flag,
or planner heuristic was changed.

The apparent C37/C38 mode discrepancy is Outcome B with the resulting
production conclusion of Outcome E:

- C37's `compacting=0` and `relocating=0` were request-side/root-scan,
  phase-specific fields. They remain valid evidence for the completed
  two-collection chronology, but they are not the final plan result.
- C38's `compacting=1` and observed `compact_phase` were authoritative for the
  final plan path.
- C39 independently reached `gc_heap::plan_phase`, captured its decision
  inputs, and entered `relocate_phase` followed by `compact_phase` for both
  variants.

No guideXOS plan-state defect was found, so there is no production fix.

## 2. Locked identity and repository baseline

The test identity remained exactly:

| Field | Value |
| --- | --- |
| NativeAOT | 9.0.0 |
| Architecture | AMD64 |
| GC | Workstation |
| Interfaces | 5.3 / 2 |
| Locked runtime source | `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3` |

The active build source root was the generated
`out/dotnet/pal-runtime-active-replacement-build/locked-source` copy selected
by the harness. The pre-existing PAL replacement manifest also records a
source-checkout metadata head of `051c1fd3c13fea8c1e60b68d9c7f63c3173e5593`
alongside the locked-source field. For C39, the actual locked files used by
the planner trace (`gc.cpp`, `gcpriv.h`, `gcrecord.h`, `gcwks.cpp`, `gc.h`,
`gcenv.ee.cpp`, and `GcEnum.cpp`) were compared against `git show
9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3:<path>` after line-ending
normalization and matched exactly. No runtime source checkout was changed.

Actual repository state at the C011EC39 starting boundary:

| Field | Value |
| --- | --- |
| Branch | `v1.1_DOTNET_SUPPORT` |
| HEAD | `4fc655b81ed61a292b303dfa51ad4fc7bc43b3b4` |
| Upstream | `origin/v1.1_DOTNET_SUPPORT` |
| Divergence | 0 ahead / 0 behind |
| Tracked worktree | clean |
| Untracked entries | 0 |
| Prior HEAD subject | `Trace NativeAOT sweep reclamation` |

The user-supplied expected divergence of ahead 1 was not the actual state; the
actual repository state was used. No reset, amend, rebase, squash, history
rewrite, or push was performed.

## 3. Locked planner call chain

The locked source call chain is:

1. `gc_heap::garbage_collect` completes `mark_phase`, then calls
   `plan_phase(n)` at locked `gc.cpp:22399-22406`.
2. `gc_heap::plan_phase(int condemned_gen_number)` begins at locked
   `gc.cpp:32553`. It rebuilds the generation plan, free/plug state, and
   promotion allocation state before calculating the scalar fragmentation
   value at `gc.cpp:33717-33722` through `generation_fragmentation(...)`.
3. On the active AMD64 Workstation build, the fast compacting condition at
   `gc.cpp:33740-33760` is checked first. It requires a nonconcurrent,
   nonprovisional collection below max generation with either
   `gen0_reduction_count > 0` or `entry_memory_load >= 95`.
4. If that fast condition is false, `gc_heap::decide_on_compacting(...)` at
   `gc.cpp:44900-45104` evaluates forced reasons, ephemeral/region space,
   fragmentation, gap allocation, and the final pause-mode override.
5. The final dispatch is the `if (should_compact)` branch at locked
   `gc.cpp:34081`. The compact branch calls `relocate_phase` and
   `compact_phase` at `gc.cpp:34192-34193`; the sweep branch sets
   `settings.compaction = FALSE` and reaches `make_free_lists` at
   `gc.cpp:34376` and `gc.cpp:34440`.

For this build, the region-space subdecision is also active. The locked
`decide_on_compaction_space()` at `gc.cpp:44802-44835` first asks whether the
free regions available to sweep can satisfy the next gen0 allocation. Its
result is then followed by the shared fragmentation test at
`gc.cpp:45007-45031`; the latter is outside the `USE_REGIONS` conditional and
can still select compaction after the region-space test says sweep is
sufficient. The C39 call-site observer captured both results.

The compact mechanism enum is locked `gcrecord.h:239-253`; value `1` is
`compact_high_frag` and is nonmandatory. The final C39 branch is still the
authoritative result; the mechanism is supporting provenance, not a policy
override.

The final C39 compile selected single-heap Workstation behavior. The compile
guards did not select `MULTIPLE_HEAPS`, `BACKGROUND_GC`, or
`GC_CONFIG_DRIVEN`; therefore the GC-configured compact/sweep frequency gate
and its compact/sweep history ratio were not active inputs in this proof.

## 4. Authoritative state and diagnostic provenance

| Reported value | Source | Owner / phase | Status |
| --- | --- | --- | --- |
| C37 `compacting` | `diagnostics.collectionCompactingMode`, copied to `r.compacting` at `guidexos_nativeaot_platform.cpp:10721` | Request-side root scan, before `plan_phase` | Correct but phase-specific; not final |
| C37 `relocating` | C37 request-side collection record | Same pre-plan/root-scan observation | Correct as an early diagnostic; not a final relocation decision |
| C38 `compacting` | Direct locked `plan_phase` final branch observer | `if (should_compact)` immediately before dispatch | Authoritative |
| C39 final compacting | Local `should_compact` at locked `plan_phase:34081`, cross-checked with final `settings.compaction` and actual phase hook | Final plan/dispatch | Authoritative |
| C39 relocating | No independent Workstation planner field exists; derived from the compact branch entering `relocate_phase` | Dispatch | Authoritative derived result |
| C39 sweep/compact path | `should_compact` branch: `relocate_phase`/`compact_phase` versus `make_free_lists` | Phase dispatch | Authoritative |

`gc_mechanisms::init_mechanisms()` at locked `gc.cpp:7749-7775` initializes
`settings.compaction` to true. That is a pre-plan/default state. C39 therefore
records it separately as `prePlanCompacting=1` and records the final branch
separately as `finalDecision=1`, `finalCompacting=1`, and
`actualPhase=1` (`compact_phase`).

## 5. C37 and C38 audit

### C37

C37 set the guideXOS request-side fields at
`guidexos_nativeaot_platform.cpp:458-468`:

```text
requestedGeneration=1
collectionReason=reason_out_of_so_h (5)
collectionCompactingMode=noncompacting_not_selected
collectionBlockingMode=blocking
```

The C37 root-scan record then copied that request-side compacting value at
`guidexos_nativeaot_platform.cpp:10721`, before the locked plan phase had
committed the final mode. That is why C37 reported `compacting=0` and
`relocating=0`. The historical C37 claim is not relabeled: its two consecutive
collections, dead-target proof, weak clearing, RestartEE, and managed resume
remain valid. Only its final-mode subclassification is narrowed to “non-final
request-side observation.”

C37 also reported reason 5. C39 proves that the locked planner's actual
`settings.reason` for the observed Collection 2 was `0`,
`reason_alloc_soh`. The two reason values are from different owners and
phases; reason 5 is retained as the C37 request-side record.

### C38

C38 added a direct locked planner observer and reported `compacting=1` on all
three historical boots. It also observed actual `compact_phase`, with no
authentic sweep callback for the target. C39 reproduced the same authoritative
final branch, so C38's mode interpretation is confirmed rather than
corrected. Its reclamation blocker remains intact: C38 did not prove sweep
free-space publication or target-space reuse.

## 6. Collection-2 planner evidence

The following values are stable across all final C37-equivalent and
C38-equivalent C39 boots. Hex values are the exact bounded scalar snapshots;
decimal values are provided only for readability.

| Input / state | C39 value | Interpretation |
| --- | ---: | --- |
| Condemned generation | `1` | Gen1 |
| Planner `settings.reason` | `0` | `reason_alloc_soh` |
| C37 request-side reason | `5` | `reason_out_of_so_h`; not planner reason |
| Heap / maximum generation | `0 / 2` | Workstation heap 0, max gen2 |
| Pre-plan compacting / promotion / concurrent | `1 / 1 / 0` | Default compacting state, promotion on, blocking |
| Pre-plan / final pause mode | `0 / 0` | Neither is `pause_no_gc` (`4`) |
| Entry memory load | `0x1F` (31) | Below the fast high-memory threshold 95 |
| gen0 reduction count | `0` | Fast gen0-reduction compact branch not selected |
| Entry available physical memory | `0x4000000` (64 MiB) | Snapshot captured at plan entry |
| Force compact / last GC before OOM | `0 / 0` | Not selected |
| Induced compacting / aggressive / PM full GC | `0 / 0 / 0` | Not selected |
| Provisional / low ephemeral / high memory | `0 / 0 / 0` | Not selected |
| Ensure-gap forced compaction | `0` | Not the deciding branch; observer was not entered after the plan was already compact |
| Fragmentation | `0xF2238` (991,800) | `generation_fragmentation(...)` result |
| Generation sizes | `0xF2238` (991,800) | Denominator used for burden |
| Fragmentation threshold | `0x13880` (80,000) | `991,800 >= 80,000` |
| Fragmentation burden | `0xF4240` (1,000,000 ppm = 1.0) | `1.0 >= 0.5` |
| Burden threshold | `0x7A120` (500,000 ppm = 0.5) | Locked dynamic threshold |
| Generation size / plan size | `0x42040 / 0x43FD8` (270,400 / 278,488) | Bounded generation planning values |
| Free-list / free-object bytes | `0 / 0` | No non-region free-list bytes at this snapshot |
| Condemned / sweep allocated | `0 / 0` | Bounded scalar fields |
| Pinned compact / sweep bytes | `0 / 0` | No pinned influence recorded |
| Promoted / survived bytes | `0x41F58 / 0x41F58` (270,168 / 270,168) | Promotion/survival inputs |
| Desired / new / current allocation | `0x40000 / 0xFFFFFFFFFFFFE050 / 0` | Exact locked snapshots; the new-allocation value is retained verbatim and not normalized |
| Region-space observer | observed `1` | Exact active region-space branch reached |
| Sweep-space sufficient | `1` | `0x100000` free bytes met the region sweep-space test |
| Region compact-space sufficient | `0` / not needed | The compact-side test was not the selected subpath after sweep sufficiency |
| Region gen0 demand | `0x6F180` (455,040) | Demand used by `decide_on_compaction_space()` |
| Region sweep free bytes | `0x100000` (1,048,576) | Aggregate bounded scalar; no segment traversal |
| Region pinned free / end-gen0 bytes | `0 / 0` | No pinned/end-space influence recorded |
| Region large chunk found | `0` | No large free chunk flag |
| Region call-site decision | `0` | Region-space subdecision was sweep/noncompacting |
| Shared fragmentation branch | `1` | Later shared `frag_exceeded` branch selected compacting |
| Compaction mechanism | `1` = `compact_high_frag` | Set at locked `gc.cpp:45028` |
| Segment count | unavailable | `segmentCountAvailable=0`; no arbitrary segment scan was added |

The decision record is therefore:

```text
condemnedGeneration = 1
plannerReason = reason_alloc_soh (0)
regionSpaceDecision = SWEEP
fragmentation = 991800 >= 80000
fragmentationBurden = 1.0 >= 0.5
compactionMechanism = compact_high_frag (1)
finalDecision = COMPACT
actualPhase = relocate_phase -> compact_phase
```

This is the exact reconciliation: “region free space is sufficient for a
sweep” is an intermediate result, not the final Workstation plan. The locked
shared fragmentation rule subsequently forces a compaction plan for this
Collection 2.

## 7. Workload comparison

The C37-equivalent and C38-equivalent C39 variants share the target setup,
Collection-1 relocation, four retained allocation sentinels, ordinary
64-KiB allocation pressure, Collection-2 trigger, weak handle, dead target,
and managed checkpoint. The C38-equivalent variant adds eight ordinary
`byte[64]` allocations after Collection 2 has completed and after the C37
managed resume checkpoint. It does not call the C38 reclamation observer and
does not gate or force an allocation path.

Historical C38 additionally wrapped its eight post-C2 allocations with
reclamation observers. Those callbacks were after the C2 decision and were
scalar-only on the suspended path. They cannot explain a different C2 plan.

The final C39 evidence shows no C2 planner-input delta between variants:

| Field | C37-equivalent | C38-equivalent |
| --- | --- | --- |
| Condemned generation | 1 | 1 |
| Planner reason | 0 (`reason_alloc_soh`) | 0 (`reason_alloc_soh`) |
| Allocated/free/live values | identical snapshots | identical snapshots |
| Fragmentation / thresholds | 991,800 / 80,000; burden 1.0 / 0.5 | identical snapshots |
| Region sweep-space result | sufficient; decision 0 | sufficient; decision 0 |
| Compaction mechanism | `compact_high_frag` | `compact_high_frag` |
| Final mode | COMPACT | COMPACT |

No workload input crossed between variants. The difference between C37 and C38
was diagnostic observation timing, not a guideXOS plan-state defect or a
legitimate C2 workload threshold crossing.

## 8. Runtime markers and sensitive-path rules

`C011EC39-PREFLIGHT` was emitted only after runtime evidence showed C37
completion, planner entry, the `decide_on_compacting` return, the final
`plan_phase` branch, and the actual compact phase. `C011EC39` then followed
after the managed checkpoint and validated the same C2 chronology.

All final boots retained:

- C37 two consecutive full-collection completions;
- gen1 Collection 2, request-side reason 5, and planner reason 0 recorded
  separately;
- zero strong roots, dead/unmarked target, weak slot cleared;
- RestartEE and managed resume;
- no Collection 3 trigger;
- zero sensitive managed diagnostic allocations;
- zero planner mutations, mark/handle writes, heap metadata writes, arbitrary
  heap scans, or managed re-entry while EE was suspended;
- bounded scalar-only planner snapshots;
- safe stop after the completion marker.

## 9. Final three-boot evidence

The final evidence roots are:

- C37-equivalent:
  `out/dotnet/c011ec39-plan-mode-c37/run-20260824-051933360`
- C38-equivalent:
  `out/dotnet/c011ec39-plan-mode-c38/run-20260824-052155592`

Both roots used QEMU 11.0.0 and passed 3/3 fresh boots with
`C011EC39-PREFLIGHT`, `C011EC39`, final `COMPACT`, and
`relocate_phase -> compact_phase`.

### C37-equivalent hashes

Serial SHA-256:

```text
11040A24441CDF2714B1DF7C2D20B51D532E3F929F3D3C490F722BF5DC4843CB
1AC5589FFC3F41E14E62E21F0D1BB3736C0EEA697AC529D1BDA0048910AC9CBC
1BACE03B9CE27BC5BF66F93AAD641B98CC342BD88321C27040F9FDC9486FA185
```

Proof kernel, PE, ELF, MAP:

```text
6A9C1D0F23579C0DC56CE98A08C32DA75BE8E9D9ABBD2CAF4711564BAE777156
520246A42F2EBC71E57B2C905D1EC79D844DC763069C6B68B38BCC88ECBDCAF5
A3BC69AFA2AD632BD89E0E50A376A87AB6B1BAAE1DCD4530E5A2D642A3E9CE3A
4A98F7999F15F78EFE75849987A177E991849C103982B905CE6FCBA389EA353A
```

### C38-equivalent hashes

Serial SHA-256:

```text
7B8786D5473EC457C542E4786EE712F9F1863E79D5AABD15547BFBA58001D4F7
B22E7DBD6337FF12F9BD2A43C92A4C1F833CDC854FB1EE5400C4CDDC9411109
8D061DFAF8ADD7325373C71B30BA30B15E4F83DA22724E65FBF9C1EFE633D47E
```

Proof kernel, PE, ELF, MAP:

```text
6B85279AECEB9FA70123714441E8B825BFBD929ADAF9770A4D46C2DE2F5AD4A0
32E588B666338B2E48B4C9B1EAC847B32FDF0B619F46DACB8CE3A05B11F28401
073928C812157D0761B3A2776FC7053CED17506C5B63FC680EC094AB2DCA8CEF
80B640D7F306FFB40EC4C29A0A3F62E65DA7350A5314FB4E0F16352D57425878
```

The PE-to-ELF converter, linker/table validation, locked-source guards,
PowerShell parse, ordinary boot smoke, and `git diff --check` all passed. The
C19-C38 chronology guards were retained, including C35 relocation and
managed-resume proof and C36 same-handle live-to-dead transition proof. The C37
repeated-GC regression passed in every final run, and the C38 reclamation
blocker was retained without a fabricated free-space claim.

## 10. Artifact restoration and QEMU cleanup

The ordinary source-state kernel and ESP SHA-256 before proof and after every
final proof run was:

```text
75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6
```

Proof payloads were removed from the ordinary source state by the harness's
restoration path. The final ordinary boot smoke passed. No repository-owned
QEMU process remained after the runs. An unrelated QEMU process using
`D:\dev\guideXOS_NET10_nativeaot-managed-kernel-integration\...` was observed
and preserved.

## 11. Files and completion

Intended files changed for this milestone:

- `samples/managed/HostLogProof/HostLogProof.csproj`
- `samples/managed/HostLogProof/Program.cs`
- `scripts/smoke-nativeaot-gc-single-thread-suspend-ee-qemu.ps1`
- `tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_allocation_diagnostics.h`
- `tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_platform.cpp`
- this document

The production GC source identity and production planner policy were not
changed. The instrumentation is bounded scalar provenance only.

## 12. C40 recommendation

Natural noncompacting gen1 was not reproduced. C40 should therefore stop
chasing `make_free_lists` for this locked workload and prove reclamation
through the compacting collector's own mechanism:

1. retain the same dead, unmarked target and cleared weak slot;
2. observe the target's old span excluded from live compacted plugs;
3. observe the compacted allocation frontier/free tail or equivalent region
   space increase;
4. perform a later ordinary allocation and prove that it consumes space made
   available by compaction.

That is the production-correct follow-up for the authoritative C39 result.
