# C011EC64 — Post-Debit Normal Condemnation Entry Timing

Date: 2026-09-01  
Repository: `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT`  
Branch: `v1.1_DOTNET_SUPPORT`  
Result: **Outcome C / Success Level 2**

## Executive result

C64 is the direct follow-on to C63. It keeps the C61 P2 survivor schedule and
the C62/C63 promotion/debit workload, then adds one fixed-size observer window
that remains open through every ordinary managed allocation after the
`RestartEE`/managed-resume boundary. The only workload variable tested was the
post-debit tail allocation count. The target was an authentic production
`generation_to_condemn(..., n_initial=0, ...)` entry after:

`PROMOTE -> GEN1-DEBIT -> RestartEE -> managed resume -> ordinary pressure`

The bounded sweep was:

| Variant | Tail payload | Requested tail allocations | One-boot result | Candidate / full policy / N0 |
| --- | ---: | ---: | --- | --- |
| W0 | `0x4000` | 192 | Outcome D / 1 | `0 / 0 / 0` |
| W1 | `0x4000` | 224 | Outcome C / 2 | `1 / 1 / 0` |
| W2 | `0x4000` | 256 | Outcome C / 2 | `1 / 1 / 0` |
| W3 | `0x4000` | 320 | Outcome C / 2 | `1 / 1 / 0` |

W1-W3 all reached a complete, ordinary, single-heap
`GCHeap::GarbageCollectGeneration` caller with full policy state. The common
candidate was not the requested doorway: it reported
`callerRequestedGeneration=2`, `nInitial=2`, `lastGcBeforeOom=1`, and
`originBranch=4`. Therefore the exact classification is:

> **C64 bounded negative: a complete ordinary post-resume caller/policy
> near-miss was observed, but no authentic post-debit normal `n_initial=0`
> entry was observed. This is Outcome C, not Outcome A or B.**

W3 was selected for the required three fresh QEMU boots. All three emitted
Outcome C / Level 2 with identical semantic fields. No GC, policy, allocator,
OOS, B02, source-checkout, or durable FP behavior was forced or mutated.

## Locked identity and repository preflight

- NativeAOT / .NET `9.0.0`, AMD64, Workstation GC, interfaces `5.3 / 2`.
- Locked runtime source commit:
  `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`.
- Locked source checkout: `out/dotnet/c52-runtime-source/source-04371d8e`.
- Durable patch: `nativeaot-amd64-fp-handoff.patch`.
- FP patch SHA-256:
  `4185495724D48E2962BA9042AF352718BF9188032DEE4C9DE6FFE9F145A1DC31`.
- Canonical ordinary kernel/ESP SHA-256:
  `75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6`.
- Live preflight branch: `v1.1_DOTNET_SUPPORT`.
- Live preflight HEAD:
  `fd902b74f35ec67dcf4df69dc52ff022b13d2842` —
  `Trace NativeAOT post-promotion earlier headroom`.
- Live preflight upstream: `origin/v1.1_DOTNET_SUPPORT`.
- Live preflight divergence: `0 ahead / 0 behind`; the expected local-ahead
  state was not present in the actual checkout and was not manufactured.
- Live preflight worktree: clean.
- Direct ancestry was verified:
  `C61 3256b51a... -> C62 f888dcea... -> C63 fd902b74...`.

The C64 evidence manifest records the intended C64 files as dirty because the
run was performed before the final focused commit. The locked runtime source
checkout itself remained untouched.

## C61-C64 lineage

| Milestone | Commit | Result relevant to C64 |
| --- | --- | --- |
| C61 | `3256b51aa43d9f7ba8c411f9f52a9b26e5b4f8c7` | P2 naturally preserved promotion and debit timing, but the useful debit still followed the final normal N0. |
| C62 | `f888dcea0b3c1718bab478d28104882b1ada1d8e` | R0/R1 reproduced `PROMOTE 0x15 -> GEN1-DEBIT 0x19`, restart/resume, and ordinary rare/refill continuation; the post-debit N0 doorway was not retained. |
| C63 | `fd902b74f35ec67dcf4df69dc52ff022b13d2842` | P2 plus the bounded R2 tail reproduced the same transition; the observation window ended after the first resumed allocation. |
| C64 | this focused follow-on | The window now retains every post-resume managed allocation and records caller/policy provenance until completion. |

## Native observer design

C64 is observational. It does not replace `generation_to_condemn`, select a
generation, call a collection entry point, write a budget, alter allocator
state, suppress OOS, or force B02. It adds fixed-size records to the existing
diagnostics root:

- `128` candidate records;
- `512` post-resume allocation records;
- one lifecycle record containing promotion/debit/restart/resume state,
  candidate provenance, full copied C56 policy state, and invariant flags.

The C64 callbacks are placed in the existing C58/C61/C62 observation chain:

| Event | C64 observation |
| --- | --- |
| C58 entry | Records the production caller mapping, `n_initial`, requested generation, reason, origin branch, and last-GC-before-OOM flag. |
| C56 policy | Copies the complete policy record, including all gen0/gen1/gen2 desired/new/current/fragmentation/promoted/survived/begin values and `gen1BudgetDepletion`. |
| C62 restart | Records `RestartEE` and managed resume for the debit collection. |
| Managed allocation | Records every ordinary post-resume allocation and its completion, without closing the C64 window after the first allocation. |

Caller mapping is explicit and source-derived:

- call site `1`: `gc_heap::check_for_full_gc` (`check-only`);
- call site `2`: `GCHeap::GarbageCollectGeneration` multi-heap call;
- call site `3`: `GCHeap::GarbageCollectGeneration` single-heap call.

The qualifying predicate requires all of the following:

`ordinaryNormalCaller && callerRequestedGeneration == 0 && nInitial == 0 && checkOnly == 0 && lastGcBeforeOom == 0 && restartObserved && managedResumeObserved && ordinaryPressureObserved`

This prevents a historical N0, a check-only probe, an OOS/full caller, or an
incomplete policy record from being promoted to a C64 success.

## Workload and bounds

The managed workload uses the inherited C61 P2 schedule: two main survivor
cohorts with 16 survivors per cohort, followed by bounded ordinary pressure.
The C64 tail uses one fixed payload size (`0x4000`) and varies only its count.
The W3 selected run uses 320 requested tail allocations. The observed C64
allocation count is `0x184` because the fixed workload also contains the
inherited post-resume setup allocations; all were recorded and completed.

The C64 bounds are:

| Bound | Value |
| --- | ---: |
| maximum candidate records | `0x80` = 128 |
| maximum post-resume allocation records | `0x200` = 512 |
| maximum QEMU time per boot | 90 seconds |
| retries per boot | 1 |
| selected fresh independent boots | 3 |
| selected variant | W3 |

The harness builds the productionized NativeAOT path from the explicit C58
runtime-pack manifest. It validates required PE/ELF symbols, the source audit,
serial completion, semantic agreement, and restoration. It does not call
`GC.Collect`, an internal GC entry point, or a policy override from managed
code.

## Selected W3 evidence

Evidence root:

`out/dotnet/c011ec64-post-debit-normal-condemnation-entry/selected-w3-final3/run-20260901-091314350`

The selected manifest reports:

- outcome: `Outcome C / C011EC64 post-debit normal condemnation entry timing`;
- success level: `2`;
- variant: `W3`;
- tail payload: `0x4000`;
- tail allocations: `320`;
- promotion observed: `1`;
- debit observed: `1`;
- debit collection ordinal: `0x3`;
- RestartEE observed: `1`;
- managed resume observed: `1`;
- ordinary pressure observed: `1`;
- post-resume allocation count: `0x184`;
- candidate count: `1`;
- full policy state observed: `1`;
- caller provenance observed: `1`;
- post-debit normal N0 observed: `0`;
- candidate and allocation overflow: `0`;
- invariant failures: `0`;
- no policy mutation: `1`;
- no allocator mutation: `1`;
- no OOS suppression: `1`.

The common W3 candidate was:

`entryOrdinal=0x3 sourceEventOrdinal=0x1A collectionOrdinal=0x4 allocationOrdinal=0x11D callSite=3 callerRequestedGeneration=2 nInitial=2 checkOnly=0 collectionReason=5 maximumGeneration=2 selectedGeneration=0 originBranch=4 lastGcBeforeOom=1 ordinaryNormalCaller=1 postResume=1 restartObserved=1 caller=GCHeap::GarbageCollectGeneration/single-heap`

Its policy record was present at `policyOrdinal=0x3` and included:

- policy initial/selected generation: `2 / 2`;
- blocking collection: `1`;
- `nAlloc=2`;
- memory load: `0x35`;
- available physical/page file: `0x4000000 / 0x4000000`;
- gen0 desired/new: `0x1181A40 / 0x9CE4D0`;
- gen1 desired/new/gc-new: `0x1CDB68 / 0xCD508 / 0xCD508`;
- gen2 desired/new: `0x40000 / 0xFFFFFFFFFFFFE0A8`;
- gen1 budget depletion: `0x100660`.

The candidate is valuable provenance, but it is not a qualifying C64 entry:
the production caller itself supplied `requestedGeneration=2` and
`n_initial=2`, and `lastGcBeforeOom=1`. The target N0 marker fields remain
zero because no qualifying entry was observed.

## Three-boot agreement

The selected W3 run emitted three completed fresh QEMU boots. Each boot
reported Outcome C / Level 2, candidate count `1`, full policy state `1`,
caller provenance `1`, promotion/debit/restart/resume/pressure all `1`, and
post-debit normal N0 `0`.

| Boot | Serial SHA-256 | Outcome / level |
| --- | --- | --- |
| first-run | `8DEA6C7203D504E5A6CD464DE68CD49C925F07FD18E07C9EEDB8589CDDE001E9` | C / 2 |
| repeat-1 | `D60914DAC562ED98B5AAAB0F0F492F26B0952ED103DA208A16AC3451CFD0C670` | C / 2 |
| repeat-2 | `3976A3EA103ECD9126D48A1F2D06E2EDA6A052D486F987E61D29285BDEE3988E` | C / 2 |

The harness semantic-agreement field is `true`. Different serial hashes are
expected because addresses and other run-local details differ; the parsed
semantic fields agree.

## Restoration and safety

The harness restored the canonical ordinary kernel and ESP in its `finally`
path. The selected manifest reports:

- restored-by-finally: `true`;
- kernel SHA-256 after run:
  `75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6`;
- ESP SHA-256 after run:
  `75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6`;
- proof-only artifact active: `false`;
- C64-owned QEMU processes: stopped;
- unrelated pre-existing QEMU process: preserved.

The source audit and regression manifest report:

`policyMutation=false allocatorMutation=false forcedGc=false oosSuppression=false lockedCheckoutUntouched=true`

Inherited C18, C26, C28, C34, C37, C39, C40, C41, C53, C54, C55, C56, C57,
C58, C61, and C62 checks passed. The C64 fixed-size caller/policy/pressure
observer and semantic rewrite guard also passed.

## Exact conclusion

C64 did not reach the requested authentic post-debit normal
`generation_to_condemn(..., n_initial=0, ...)` doorway. It did establish the
next useful fact: after the C63 transition, a natural tail extension reaches a
real ordinary single-heap collection caller with full policy provenance, but
that caller is already the `n_initial=2`, `last_gc_before_oom=1` path. The
bounded experiment therefore stops at **Outcome C / Level 2**. No B02/direct
gen1 experiment, policy change, allocator change, forced GC, or broader timing
search is justified by this result.

