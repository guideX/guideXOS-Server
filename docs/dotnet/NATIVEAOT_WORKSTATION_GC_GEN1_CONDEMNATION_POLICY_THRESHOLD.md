# C011EC56 — NativeAOT Workstation GC generation-condemnation policy threshold

## Result

C56 is Outcome A / Success Level 4. Three fresh QEMU 11.0.0 boots agreed on
the policy records and completed the first natural older-generation collection.
The first collection with `condemnedGeneration >= 1` was a gen2 collection, not
a gen1-only collection. The decisive production branch was the USE_REGIONS
`last_gc_before_oom` path (B12), reached after `try_get_new_free_region()` could
not provide an empty region. The gen1 allocation-budget comparison remained
positive and was not the first crossing.

**C56 identified the exact production Workstation-GC generation-escalation condition and naturally crossed it using bounded promoted survivor residency. The collector selected gen1-or-higher without `GC.Collect`, condemned-generation override, policy mutation, segment manipulation, or allocation-pointer forcing, and the older-generation collection completed through `RestartEE` and managed resume.**

The sentence above describes the first authentic `>=1` selection. The narrower
gen1 budget threshold (`get_new_allocation(1) <= 0`) was identified exactly but
was pre-empted by B12 within the safe bound; its final observed value was still
positive.

## Repository and ancestry

| Item | Value |
|---|---|
| Repository | `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT` |
| Branch | `v1.1_DOTNET_SUPPORT` |
| Starting HEAD | `5792da5337664a2b9c6892590497387af209ac02` |
| Starting subject | `Trace NativeAOT natural older-generation GC transition` |
| Starting upstream | `origin/v1.1_DOTNET_SUPPORT` |
| Starting ahead/behind | `0 / 0` |
| Starting worktree | clean; no untracked files |
| C55 in history | yes; starting HEAD was C55 |
| C55 pushed | yes; C55 is an ancestor of `origin/v1.1_DOTNET_SUPPORT` |

The C56 worktree began from the clean C55 commit. Run manifests made during
development show the intentional C56 edits as dirty; those are not the
repository preflight state.

## Locked runtime identity

- NativeAOT / ILCompiler `9.0.0`, AMD64, Workstation GC.
- GC interfaces `5.3 / 2`; one Workstation heap; Server, background, and
  concurrent GC disabled.
- Runtime source commit:
  `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`.
- FP repair: `nativeaot-amd64-fp-handoff.patch`.
- FP patch SHA-256:
  `4185495724D48E2962BA9042AF352718BF9188032DEE4C9DE6FFE9F145A1DC31`.
- Productionized runtime-pack path was used, with the C52 validated pack
  manifest. No C46/C47/C48 semantic smoke rewrite was enabled and the FP
  patch was not modified.

## C55 control and the hypothesis

C55 supplied the control result: 631 authentic gen0 collections per boot,
1,893 total across three boots; every collection completed `RestartEE` and
managed resume; the eight tracked survivors naturally reached gen1; the
reclaimed gen1 tail stayed mapped, allocator-visible, and ineligible; no gen1,
gen2, full collection, ephemeral transition, retirement, recycling, fail-fast,
or page fault occurred.

Its stable generation-selection snapshot was:

| Generation | desired | new, raw | new, signed interpretation | current | fragmentation |
|---|---:|---:|---:|---:|---:|
| gen0 | `0x28000` | `0xFFFFFFFFFFFF5F50` | `-41136` | not captured as nonzero | not captured as nonzero |
| gen1 | `0x1CDB68` | `0x14DAA8` | `+1366696` | `0` | `0xC0` (`192`) |
| gen2 | `0x40000` | `0xFFFFFFFFFFFFE0A8` | `-8024` | not captured as nonzero | not captured as nonzero |

The numerical observation was valid but not a literal field identity:

```text
0x1CDB68 - 0x14DAA8 = 0x800C0 = 524480
8 * 0x10018            = 0x800C0
```

Classification: **B — derived but indirect**. Source proves that `desired` is
a separately modeled target while `new` is a signed remaining budget. In the
USE_REGIONS path, `compute_in()` subtracts incoming allocation accounting from
`gc_new_allocation` and publishes that result as `new_allocation`. The retained
survivor allocation can therefore affect the budget difference, but
`promotedBytes` is not literally the eight-survivor value: the C56 cohort
records reported gen1 `promotedBytes=0x41F58` (`270168`) while the eight-array
aligned residency is `0x800C0`. The equality is budget/allocation accounting,
not a direct interpretation of `desired - new` as the promoted-byte field.

## Exact source audit

The source-expression audit is anchored to the C52-validated locked checkout
at `out/dotnet/c52-runtime-source/source-282a5259/src/coreclr/gc/` (the
equivalent preserved source mirror is at
`out/dotnet/gc-feasibility-baseline/nativeaot-runtime/src/coreclr/gc/`). The
authoritative locked checkout identity used for the proof pack was
`out/dotnet/c52-runtime-source/source-282a5259`, whose checkout head and
runtime-pack source commit were both
`9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`.

Locked source locations:

| Location | Role |
|---|---|
| `gc.cpp:21486` | `gc_heap::generation_to_condemn(int n_initial, BOOL*, BOOL*, BOOL)` |
| `gcpriv.h:1115` | `dynamic_data` definitions |
| `gc.cpp:44026` | `compute_in()` |
| `gc.cpp:44147` | `compute_new_dynamic_data()` |
| `gc.cpp:29148` | `get_memory_info()` |
| `gc.cpp:53223` | memory-load threshold initialization |
| `gc.cpp:45215` | `ephemeral_gen_fit_p()` |
| `gc.cpp:21345` | `try_get_new_free_region()` |

The relevant source expressions are:

```cpp
// Initial higher-generation allocation-budget elevation.
for (i = n+1; i <= (check_max_gen_alloc ? max_generation
                                         : (max_generation - 1)); i++)
{
    if (get_new_allocation(i) <= 0)
        n = i;
    else
        break;
}
```

```cpp
// USE_REGIONS reserve check, after low-ephemeral evaluation.
if (!check_only_p)
{
    if (!try_get_new_free_region())
        last_gc_before_oom = TRUE;
}
```

```cpp
// The branch that actually elevated C56's first older-generation collection.
if (last_gc_before_oom)
{
    n = max_generation;
    *blocking_collection_p = TRUE;
    local_condemn_reasons->set_condition(gen_before_oom);
}
```

`try_get_new_free_region()` first checks the basic free-region list and then
tries `allocate_new_region(__this, 0, false)`, initializing and returning a
new region only if that succeeds. C56's source-derived B12 marker therefore
means the region-reserve check changed from available to unavailable; it is
not a memory-load threshold and is not a gen1 budget comparison.

Other branches capable of changing `n` from 0 or otherwise elevating it were
given stable C56 IDs:

| ID | Source-derived branch |
|---:|---|
| B01 | no earlier branch changed `n`; stay at gen0 |
| B02 | `get_new_allocation(i) <= 0`; generation allocation budget |
| B03 | low card-table efficiency; skip to at least gen1 |
| B04 | `!ephemeral_gen_fit_p(...)`; low ephemeral space |
| B05 | background time tuning |
| B06 | ephemeral-generation fragmentation loop |
| B07 | low-latency clamp |
| B08 | `memory_load >= high_memory_load_th` |
| B09 | `memory_load >= v_high_memory_load_th` or low-memory status |
| B10 | high fragmentation during memory-load evaluation |
| B11 | expand-in-full-GC blocking path |
| B12 | `last_gc_before_oom`; `n=max_generation`, blocking |
| B13 | induced blocking collection |
| B14 | induced no-force collection |
| B15 | gen2 new/desired ratio below 90 percent |
| B16 | high fragmentation; elevate to full/gen2 |
| B17 | low-ephemeral/high-memory elevation without high fragmentation |
| B18 | gen2 budget check after gen1 elevation |
| B19 | maximum-generation fragmentation blocking path |

The final pre-B12 policy records had B01, B01, B01, then B12. Thus the exact
condition keeping the collector at gen0 was that no earlier budget, low-space,
fragmentation, memory, time, or induced branch changed `n`. The exact direct
gen1 condition was present in source but false because gen1 `new_allocation`
remained positive.

## Policy terms and signedness

The C56 record stores `new_allocation`, `gc_new_allocation`, and the derived
budget depletion as signed `int64_t` values. It stores desired allocation,
current size, fragmentation, promoted/survived bytes, begin-data size, and
available memory as unsigned `uintptr_t`-sized values. Boolean policy controls,
generation numbers, reasons, thresholds, and `n_alloc` are `uint32_t`.

| Field | Source/type | Meaning and update point |
|---|---|---|
| `desired_allocation` | `size_t` | modeled target returned by `desired_new_allocation`; recomputed when condemned generation data is updated |
| `new_allocation` | signed `ptrdiff_t` | remaining allocation budget; decremented by allocation and allowed to become negative |
| `gc_new_allocation` | signed `ptrdiff_t` | GC-time counterpart to `new_allocation`; `compute_in()` subtracts incoming bytes |
| `current_size` | `size_t` | live generation size after accounting for fragmentation in `compute_new_dynamic_data` |
| `begin_data_size` | `size_t` | bytes occupied by objects at the beginning of a GC |
| `survived_size` | `size_t` | bytes occupied by objects that survived marking |
| `promoted_size` | `size_t` | dynamic-data promoted/survived accounting; not the retained-cohort request size |
| `fragmentation` | `size_t` | free-list plus free-object space used by fragmentation tests |
| `n_initial` / selected `n` | `int` | initial and final condemned-generation decision |
| `n_alloc` | `int` | generation selected by the initial allocation-budget stage before later policy checks |
| memory load | `uint32_t` percent | `GCToOSInterface::GetMemoryStatus`; under USE_REGIONS the source also considers VA load |
| memory thresholds | `uint32_t` percent | high/v-high/medium thresholds initialized from configuration or defaults |
| low ephemeral | `BOOL` | `!ephemeral_gen_fit_p(...)` at the condemned-generation tuning point |
| elevation requested | `BOOL` | final elevation predicate; B12 can select full without setting this flag |

The C56 final policy records show raw and signed values:

| selection | n initial → final | branch | gen0 desired/new | gen1 desired/new | gen1 depletion | gen1 fragmentation | gen2 desired/new | memory load |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | `0 → 0` | B01 | `0x28000 / 0xFFFFFFFFFFFF5F50` (`163840 / -41136`) | `0x1CDB68 / 0x1CDB68` (`1891176 / 1891176`) | `0` | `0` | `0x40000 / 0xFFFFFFFFFFFFE0A8` (`262144 / -8024`) | `0` |
| 2 | `0 → 0` | B01 | `0x286540 / 0xFFFFFFFFFFFF5D90` (`-41584`) | `0x1CDB68 / 0x1AD658` (`1891176 / 1758808`) | `0x20510` (`132368`) | `0x48` (`72`) | `0x40000 / 0xFFFFFFFFFFFFE0A8` | `0` |
| 3 | `0 → 0` | B01 | `0x780B40 / 0xFFFFFFFFFFFFF4C0` (`-2880`) | `0x1CDB68 / 0x14D5C8` (`1891176 / 1365448`) | `0x805A0` (`525728`) | `0xD8` (`216`) | `0x40000 / 0xFFFFFFFFFFFFE0A8` | `0` |
| 4 | `2 → 2` | B12 | `0x1401E00 / 0xBD0570` (`20976128 / 12387696`) | `0x1CDB68 / 0x4D448` (`1891176 / 316488`) | `0x180720` (`1574688`) | `0x258` (`600`) | `0x40000 / 0xFFFFFFFFFFFFE0A8` (`-8024`) | `0x32` (`50`) |

The second-row gen0 signed decimal was not used for policy reasoning; the
record is retained in raw hex because that field is a signed remaining budget.
All threshold comparisons used the signed `new_allocation` interpretation.

Memory provenance was authentic: `memoryLoad=0x32` (`50`) at the B12 record,
with `availablePhysical=availablePageFile=0x4000000` (`67108864`), below
`high_memory_load_th=0x5A` (`90`) and
`v_high_memory_load_th=0x61` (`97`). `lowMemoryDetected=0`,
`lowEphemeral=0`, and `highFragmentation=0` in every C56 policy record.
Memory load did not participate in the first elevation.

## Bounded survivor workload

All limits were fixed before execution:

| Bound | Value |
|---|---:|
| maximum cohorts | 6 |
| survivor step | 8 arrays |
| maximum retained survivors | 48 |
| survivor shape | `byte[65536]` |
| aligned survivor size | `0x10018` (`65560`) |
| maximum retained aligned bytes | `0x300480` (`3146880`) |
| transient arrays per cohort | 64 |
| maximum explicit arrays | 384 |
| maximum explicit managed bytes | `0x1800000` (`25165824`) |
| maximum policy records | 512 |
| maximum survivor observations | 1024 |
| maximum diagnostic records | 2048 |
| maximum collection observations | 1024 |
| stop condition | first C56 older-generation marker or fixed six-cohort bound |

Each retained array was allocated in managed code, initialized with a
deterministic sentinel pattern, read back, rooted in a managed array, and
checked initially with `GC.GetGeneration(value)==0`. Survivor addresses and
generations were sampled only after `GC.CollectionCount(0)` changed. No
`GC.Collect`, internal collection entrypoint, condemned-generation override,
GC stress mode, budget mutation, generation mutation, segment operation, or
allocation-pointer forcing was used.

The first older-generation event stopped the ladder after cohort 5; cohort 6
was not started. The observed endpoints were:

| survivors | retained aligned bytes | promoted bytes | gen1 desired | gen1 new | difference (`desired-new`) | policy ordinal | selected gen |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 8 | `0x800C0` | `0x41F58` | `0x1CDB68` | `0x1AD658` | `0x20510` | 2 | 0 |
| 16 | `0x100180` | `0x41F58` | `0x1CDB68` | `0x1AD658` | `0x20510` | 2 | 0 |
| 24 | `0x180240` | `0x41F58` | `0x1CDB68` | `0x14D5C8` | `0x805A0` | 3 | 0 |
| 32 | `0x200300` | `0x41F58` | `0x1CDB68` | `0x14D5C8` | `0x805A0` | 3 | 0 |
| 40 | `0x2803C0` | `0x41F58` | `0x1CDB68` | `0x4D448` | `0x180720` | 4 | 2 |

The endpoint table is deliberately not described as a linear eight-survivor
budget law: allocation/model updates occur at GC boundaries, and B12
pre-empted the direct gen1 budget crossing.

On the first proof boot, the process `GC.CollectionCount(0)` observations
while the active cohorts were resident were `5,6` for cohort 1, none for
cohort 2 before the next endpoint, `7` for cohort 3, none for cohort 4, and
`8` for cohort 5. The C56 policy/lifecycle stream contained four authentic
collection ordinals; the process count is higher because earlier C37/C40
activity shares the same managed process.

Promotion proof was successful for the tracked residency that preceded the
older collection. Every recorded survivor had `initialGeneration=0`, a valid
sentinel/readback, and a nonzero managed address. The `moved` flag was true
for the ordinary observations that changed address; one partial collection-5
observation retained its address, so the record does not claim that every
observation moved.
Natural observations included gen1 states for the retained objects before the
first older collection; the first older collection then moved the observed
survivors through the gen2 plan. The run did not claim that the eight newest
cohort-5 objects had an independent gen1-only stop before the first older
collection; they were part of the authentic B12-triggered gen2 collection.

## First threshold crossing and lifecycle effect

The first C56 threshold marker was:

```text
thresholdOrdinal     = 4
thresholdKind        = 2  (selected-generation crossing)
thresholdBranch      = B12
thresholdPreValue    = 0
thresholdPostValue   = 2
thresholdValue       = 1
```

The first older-generation marker was:

```text
collectionOrdinal    = 4
condemnedGeneration  = 2
collectionReason     = 5 = reason_oos_soh
exact policy branch  = B12 / last_gc_before_oom
```

The C54/C55 planner/reclamation chronology completed. Planner observation and
decision were valid; the inherited C37 planner record reported a completed
phase chronology with `compacting=0` and `relocating=0` for its recorded
collection. C56 does not claim that the B12 collection was compacting or
relocating because those two flags were not separately populated in the C54
wrapper record.

`fix_generation_bounds` was observed. `adjust_ephemeral_limits` was not
observed (`0`), and no ephemeral-boundary transition was emitted. The
reclaimed C55 tail had this immediate result:

| Field | before | after |
|---|---:|---:|
| tail range | `[0x100900028, 0x100943000)` | same range |
| segment | `0x104010668` | `0x104010668` |
| generation | `1` | `2` |
| allocator-visible | yes | yes |
| eligible | no | no |
| mapped | yes | yes |
| ephemeral segment | `0` | `0x104010E48` |
| segment retired | no | no |
| segment recycled | no | no |

Thus the immediate domain effect was a natural generation/segment-domain
change of the retained tail from gen1 to gen2 while it remained mapped and
allocator-visible but still ineligible. No consideration or selection was
forced, and no reclaimed-tail reuse claim is made.

## Invariants and regression evidence

- C18 remained valid: authentic code-manager lookup,
  `FindMethodInfo==1`, durable caller-FP and iterator-FP publication, no stale
  or zero FP, no `0xFFFFFFFFFFFFFF90`, and invalid-state fail-closed behavior.
- C26 root scan completed: 4 total roots, 2 category-3 roots, 1 register root,
  1 stack root, 4 promote attempts and 4 promote entries.
- C28 mark closure completed with zero queue/invariant failures and retained
  the valid first-object/child checkpoints.
- C34 preflight and C37 repeated-collection chronology remained intact.
- C39 planner provenance, C40 reclamation, and C41 allocation-domain evidence
  remained intact.
- C46/C48 durable FP repairs were unchanged. C49/C50/C51/C52 release-gate
  assumptions and C53/C54 classifications were retained.
- C56 diagnostics: invariant failures `0`, sensitive diagnostic allocations
  `0`, fail-fast `0`, page fault `0`.
- `RestartEE`: observed and valid. Managed resume: observed and valid.

## Three fresh boots and artifacts

QEMU version: `11.0.0 (v11.0.0-12122-ga4bb4b10c9)`.

| Boot | outcome/level | serial SHA-256 |
|---|---|---|
| first-run | A / 4 | `9FA472B52ADBD391CA850AB765572C015214CD23424A3C98B4E54DF066EE6A74` |
| repeat-1 | A / 4 | `E7A3A66FF8BD8814403824F47429F5B277A63E397424593397D6C91DA7B56A6F` |
| repeat-2 | A / 4 | `C91A41CE683DCF5E398FBC761436A3DAEB436D095D95ACF98042B5BDF96581F7` |

All three runs had 4 policy records, 5 completed cohort endpoints, one
threshold marker, one first-older marker, one resume marker, B12, gen2, and
zero invariant/safety failures. Semantic agreement was true.

Final proof artifact hashes from the three-boot manifest:

```text
proof kernel  45F90D532152F61B434306D83CEC7673E09B1C87E5B0C28BD266E53253FE621C
PE            B5B6F61DF6E88BFA933F1FC07B0AFC2F92B4057F0A2EC369AFD01848DF9B544D
ELF           E3196DA908C24D89A91F3DB080374340AD7D4FF2B0F982CFE705F6C66A3EDB72
map           D61DEF368849BCD0CC792379DC119A095DD6D45835880E589FC4B98D46D80492
```

The authoritative artifact hashes are retained in the final C56 manifest at
`out/dotnet/c011ec56-natural-gen1-condemnation-policy-threshold/run-20260828-213146204/manifest.json`.

## Release gates and restoration

C56 changed only the managed proof workload, bounded diagnostics, and smoke
harness; production GC policy and the locked FP patch were not changed. The
applicable production control remains C52. The latest C52 control manifest
completed Tier All (A/B/C/D) at Level 5; C56 reran the relevant static guards,
the productionized runtime-pack proof, and three fresh C56 boots.

The following were run or supplied by the proof harness:

- managed NativeAOT build: PASS;
- PowerShell parse: PASS;
- JSON/XML/runtime-pack manifest parsing: PASS;
- locked runtime source and FP patch identity: PASS;
- semantic rewrite guard: PASS; C46/C47/C48 semantic injection disabled;
- PE-to-ELF conversion, linker/source/table guards: PASS;
- `git diff --check`: run for the final tree;
- MASM: not applicable; no assembly changed.

Ordinary artifact restoration was verified live after each proof run:

```text
ordinary kernel SHA-256 = 75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6
ordinary ESP SHA-256    = 75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6
proof-only artifact active = false
```

The C56-owned QEMU instances were removed by the harness finalizer. The two
unrelated QEMU instances belonging to `guideXOSServer_NAVIGATOR_IMPROVEMENTS`
were preserved.

## Files and conclusion

Files changed:

- `samples/managed/HostLogProof/HostLogProof.csproj`
- `samples/managed/HostLogProof/Program.cs`
- `scripts/smoke-nativeaot-gc-single-thread-suspend-ee-qemu.ps1`
- `tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_allocation_diagnostics.h`
- `tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_platform.cpp`
- `docs/dotnet/NATIVEAOT_WORKSTATION_GC_GEN1_CONDEMNATION_POLICY_THRESHOLD.md`

Final classification: **Outcome A / Success Level 4**. The exact direct gen1
budget threshold is understood but remains a limitation of this bounded
profile: B12 reaches gen2 first when the region reserve is exhausted. The next
smallest milestone is not more raw churn; it is an even smaller ordinary
allocation-boundary workload that gives the 40-survivor state a natural gen0
collection before the region-reserve B12 branch, allowing the direct gen1
`new_allocation <= 0` comparison to be tested independently.
