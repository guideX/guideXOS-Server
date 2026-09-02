# C011EC66 — Post-Debit Normal Gen0 Refill

Date: 2026-09-01
Repository: `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT`
Branch: `v1.1_DOTNET_SUPPORT`

Result: **Outcome D / Success Level 2**

## Executive result

C011EC66 did not preserve a normal post-debit gen0 refill. The final
three-boot control at tail allocation count 216 reached the exact C65
context-exhaustion decision, but production region acquisition remained an
empty search (`candidateCount=0`, `result=0`, `branch=3`). The runtime then
set `commit_failed=1` and followed the already-proven full/OOS path.

This is a bounded negative result, not a runtime regression. The primary
allocator blocker remains the C65 gen0 region miss; no successful region was
obtained, so C66 did not reach the normal-refill or post-refill N0 doorway.

## Starting state and locked identity

- Starting HEAD: `5c8898de96be15c1bd99334ebe67ed0e6794be88`
- Starting subject: `Prove NativeAOT post-debit gen2 OOS preemption`
- Starting branch: `v1.1_DOTNET_SUPPORT`
- Upstream: `origin/v1.1_DOTNET_SUPPORT`
- Starting divergence: `0 ahead / 0 behind`; starting worktree clean.
- C63: `fd902b74f35ec67dcf4df69dc52ff022b13d2842`
- C64: `962a83568b531e86856fd1943023fa4c6194f371`
- C65: `5c8898de96be15c1bd99334ebe67ed0e6794be88`
- C65 was already upstream, not local-only.
- C65 report: `docs/dotnet/NATIVEAOT_WORKSTATION_GC_C65_POST_DEBIT_GEN2_OOS_PREEMPTION.md`
- Locked runtime: NativeAOT/.NET `9.0.0`, AMD64, Workstation GC, interfaces `5.3 / 2`.
- Locked source: `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`.
- Durable FP patch SHA-256: `4185495724D48E2962BA9042AF352718BF9188032DEE4C9DE6FFE9F145A1DC31`.
- Canonical ordinary kernel/ESP SHA-256: `75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6`.
- C46/C47/C48 semantic rewrites remained disabled; the durable FP repair remained inherited from the runtime-pack patch.

## C65 baseline and first divergence

C65 was reproduced unchanged before C66 experimentation. The fresh baseline
evidence is:

`out/dotnet/c011ec65-post-debit-gen2-oos-preemption/run-20260901-164244946`

It was Outcome A / Success Level 5 with the causal chain:

`context exhausted`
→ `soh_try_fit` cannot fit
→ no next segment
→ `get_new_region(0)` returns null
→ `commit_failed=1`
→ `a_state_trigger_full_compact_gc`
→ `reason_oos_soh (5)`
→ `last_gc_before_oom=1`
→ `GarbageCollectGeneration(2)`
→ `generation_to_condemn(n_initial=2)`.

C65's successful earlier normal refill had request `0x10018`, valid context
pointer/limit, fit result `1`, and branch `1`. The decisive failed refill had
request `0x4018`, allocation context `0x3998CC0`, null allocation
pointer/limit, zero remaining bytes, and active segment `0x104011628` in the
selected C65 evidence. The first production divergence was the gen0 region
acquisition branch at `gc.cpp:17943-17969`, recorded as event `0x301`,
branch `3`, allocation state `0xF`, and `commit_failed=1`.

C011EC66 emits `C011EC66-C65-BASELINE PASS` only when the complete C65 chain
is present. The marker passed in all three final C66 boots.

## Question and one-variable experiment

The question was whether one ordinary managed-workload change could keep the
post-debit allocation context on the normal gen0 refill path after authentic
promotion, gen1 debit, `RestartEE`, and managed resume.

C65 evidence selected the post-debit tail allocation count as the first
workload-controlled dimension. The final experiment held the payload at
`0x4000`, the C64 variant at W3, and the C61/C65 cohort schedule at P2 (two
main cohorts with 16 survivors per cohort). Only the ordinary post-debit tail
count changed in the final study.

| Variant | Single changed variable | Result | Classification |
| --- | --- | --- | --- |
| C65 W0 control | 192 tail allocations | Context exhaustion not reached | Outcome D / Level 1 / code 1 |
| C66 T200 | 200 tail allocations | Context exhaustion not reached | Outcome D / Level 1 / code 1 |
| C66 T208 | 208 tail allocations | C65 region miss and full/OOS path | Outcome D / Level 2 / code 3 |
| C66 T216 | 216 tail allocations | C65 region miss and full/OOS path | Outcome D / Level 2 / code 3 |
| C65 W1 control | 224 tail allocations | Same C65 region miss | Outcome A / Level 5 for C65 topology |
| C65 W2/W3 controls | 256 / 320 tail allocations | Same C65 region miss | Outcome A / Level 5 for C65 topology |
| C66 P1 screen | Cohort schedule changed to 5 × 8 at tail 320 | Same C65 topology | Rejected; not retained |

The final retained value was T216 because it reached the decisive boundary
reliably while remaining the smallest tested value on the boundary-reaching
side. It was not a successful crossing; no larger value was increased after
the same C65 failure was reproduced.

Hard bounds were declared and held: at most 512 managed allocations, 32 MiB
aggregate bytes, `0x10000` individual payload, 64 survivor references, four
survivor cohorts, 1024 region records, 512 refill records, 16 MiB serial
output, and 120 seconds per QEMU boot. The actual C66 payload was `0x4000`.

## Final three-boot evidence

Evidence root:

`out/dotnet/c011ec66-post-debit-normal-gen0-refill/run-20260901-174402706`

All three fresh boots agreed semantically. Proof kernel SHA-256:

`5592F346D7A58E0560E1C3EEBB42CBE9E0DB31D54EBE4B0AD0C5D229BC48062B`

| Boot | Serial SHA-256 | Result |
| --- | --- | --- |
| first-run | `A4F1464F03CEB35C6AD070DA3EDBC76A16DD39B005A8D610E53C4A8E0CF597C6` | Outcome D / Level 2 |
| repeat-1 | `4AE446D81C0790443B4445271A1FA57A2603EFB0D9E18031F9C65F0FB38DE046` | Outcome D / Level 2 |
| repeat-2 | `3601A837EF33866132CB7AF1A7E530D71A6B41141369CDB93F7EBC610FB09040` | Outcome D / Level 2 |

Each final boot recorded:

- promotion: observed;
- gen1 debit: observed, `0xE0150` bytes, gen1 budget `0x1AD658 → 0xCD508`;
- `RestartEE`: observed;
- managed resume: observed;
- context exhaustion: observed for request `0x4018`;
- allocation context: `0x399BCC0`;
- pointer/limit: `0 / 0`;
- remaining bytes: `0`;
- active segment: `0x1040114D8`;
- region generation/domain: gen0 (`0`), but no candidate identity was returned;
- candidate count: `0`;
- region-search result: `0`, branch `3`;
- `commit_failed`: `1` at the first-changed production state;
- full/OOS preemption: `1`;
- reason: `reason_oos_soh (5)`;
- `last_gc_before_oom`: `1`;
- requested GC generation: `2`;
- `n_initial`: no qualifying post-debit N0 entry; the C65 downstream caller remained `n_initial=2`;
- `POST_DEBIT_NORMAL_REFILL`: `FAIL reason-code=3-region-search-failed`;
- `FULL_OOS_PREEMPTION`: `1`;
- `POST_DEBIT_N0`: `0`;
- invariant failures: `0`;
- sensitive diagnostic allocations: `0`;
- fail-fast: `0`;
- page faults: `0`.

The complete machine-readable record is the `manifest.json` in the evidence
root. The final marker is `C011EC66 outcome=D successLevel=2`.

## Refill comparison and first changed production state

The final C66 failure was not a new allocator topology:

| Field | Earlier normal refill | Final C66 decisive refill |
| --- | ---: | ---: |
| aligned request | `0x10018` | `0x4018` |
| generation | `0` | `0` |
| allocation context | valid | `0x399BCC0` |
| allocation pointer | valid | `0` |
| allocation limit | valid | `0` |
| context remaining | positive | `0` |
| active segment | run-local normal segment | `0x1040114D8` |
| candidate count | not applicable to direct fit | `0` |
| candidate identity | not applicable | `0` |
| region generation | gen0 path | `0` |
| region search | not required by direct fit | result `0`, branch `3` |
| expansion | not the successful path | no acceptable region returned |
| `commit_failed` | `0` | `1` |
| allocator state | normal continuation | `a_state_trigger_full_compact_gc` |

The exact source success condition remains the production expression in
`soh_try_fit`: a valid gen0 region must be returned by the ordinary
`get_free_region`/`get_new_region` path, the request must fit, and
`commit_failed_p` must remain false. That condition did not become true in
C66. The first changed production state is therefore still the C65 region
miss, not `n_initial` or a later policy value.

The `commit_failed` write remains the locked-source write in
`soh_try_fit` at `gc.cpp:17965-17969`; the full-compaction transition is in
`allocate_soh` at `gc.cpp:18044-18051`. The downstream policy reserve check
was not treated as causal.

## Source audit

The locked source audit covered:

- `GCHeap::Alloc` — `gc.cpp:49905-49997`;
- `gc_heap::try_allocate_more_space` — `gc.cpp:18949-19060`;
- `gc_heap::allocate_soh` — `gc.cpp:17982-18256`;
- `gc_heap::soh_try_fit` — `gc.cpp:17896-17979`;
- `gc_heap::try_get_new_free_region` — `gc.cpp:21345-21371`;
- `gc_heap::get_free_region` — `gc.cpp:11906-12020`;
- `gc_heap::get_new_region` — `gc.cpp:34988-35017`;
- `gc_heap::allocate_new_region` — `gc.cpp:35019-35046`;
- `gc_heap::make_heap_segment` — `gc.cpp:12313-12350`;
- `gc_heap::grow_heap_segment` — `gc.cpp:15464-15512`;
- `gc_heap::trigger_full_compact_gc` — `gc.cpp:18462-18522`;
- `GCHeap::GarbageCollectGeneration` — `gc.cpp:50960-51040`;
- `gc_heap::generation_to_condemn` — `gc.cpp:21486-21875`.

## N0 and reclaimed-tail observations

No successful refill occurred, so no post-refill `generation_to_condemn` call
with caller-supplied `n_initial=0` was observed. `POST_DEBIT_N0` is therefore
false. Direct B02/gen1-budget testing is not justified by C66.

The inherited historical C54 semantic tail remains observational only:

- range: `[0x100900028, 0x100943000)`;
- size: `0x42FD8` historical reference;
- historical segment: `0x104010668`;
- generation: gen1 before / gen2 after promotion;
- mapped: true;
- allocator-visible: true;
- eligible, considered, selected, consumed: false / false / false / false.

C66 did not target, make eligible, insert, reorder, or consume that tail.

## Safety, diagnostics, and validation

The final manifest records inherited C18–C65 safety checks as passing,
including valid executable managed PC/code manager, `FindMethodInfo`, root
scan, promoted roots, mark closure, planner, survivor integrity, and the
restart/resume chronology. All mutation guards remained clear:

- allocator mutation: false;
- policy mutation: false;
- free-region mutation: false;
- `commit_failed` mutation: false;
- requested-generation mutation: false;
- OOS suppression: false;
- invariant failures: zero;
- sensitive diagnostic allocations: zero.

Focused validation passed: locked runtime identity, FP patch identity,
semantic-rewrite guard, NativeAOT/native build, managed publish, runtime-pack
manifest validation, PowerShell syntax, JSON/XML parsing, `git diff --check`,
PE→ELF conversion, symbol checks, and linker/source/table/archive guards.
MASM was not applicable because no assembly source changed. C52 Tier-All was
not run because C66 changed only proof-mode observers and the bounded managed
workload; no production GC/allocator semantics were changed.

Ordinary artifacts were restored after testing:

- ordinary kernel SHA-256:
  `75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6`;
- ordinary ESP SHA-256:
  `75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6`;
- proof-only artifact active: false;
- only C66-owned QEMU processes were stopped;
- unrelated QEMU state was preserved.

## Conclusion and next milestone

C66 establishes that the tested ordinary post-debit tail-count departure does
not change the C65 allocator topology. T200 is below the boundary; T208 and
T216 reach it and fail identically. The successful post-debit normal refill
predicate remains false, so the correct next milestone is:

**C67 should first target a normal post-debit `n_initial=0` policy entry
without changing the proven refill topology, only after a future experiment
identifies a workload variable that actually supplies an acceptable gen0
region.**

Direct B02 testing is not justified. C66 did not prove a successful refill or
an authentic post-debit N0 doorway.
