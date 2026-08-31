# NativeAOT Workstation GC last-normal-`n_initial=0` direct-gen1 window

## Result

C011EC59 adds a bounded observation layer for the last ordinary `n_initial=0`
allocation-triggered collection before the first `n_initial=2` full out-of-space
collection. The experiment preserves the existing C56/C57/C58 lifecycle and
does not alter the GC policy result, generation selection, free-region state, or
segment state.

The selected S2 workload completed three fresh QEMU boots with stable
`Outcome C / Level 1`:

| observation | result |
| --- | --- |
| entry chronology | `n_initial=0`, `n_initial=0`, `n_initial=2` |
| last normal N0 | entry 2 |
| first N2 | entry 3 |
| B02 (`get_new_allocation(1) <= 0`) | not crossed |
| direct gen1 selection | not selected |
| entry-2 M1 | signed `0x1AD658` = `1,758,808` bytes |
| entry-2 bytes to B02 | `0x1AD658` |
| entry-2 current-region remaining | `0x0` |
| entry-2 free-region result | observed/called, result `1` |
| entry-2 `last_gc_before_oom` | `0` |
| first N2 free-region result | `2`, with `last_gc_before_oom=1` |
| C56/C54/C58 invariant failures | `0` |
| sensitive diagnostic allocations | `0` |
| fail-fast/page fault | `0` / `0` |

This is a successful last-N0 window characterization, not a direct-gen1
condemnation proof. The normal entry-2 refill succeeds, so the source's B02
threshold is not crossed in this runtime topology. The later entry-3 N2 path is
the source-grounded full-OOS escalation.

## Locked runtime and source identity

The run used the fresh C52 Tier-B runtime-pack manifest:

`out/dotnet/c011ec59-runtime-pack-validation/run-20260831-114237054-2201cac4/runtime-pack/runtime-pack.manifest.json`

| item | value |
| --- | --- |
| NativeAOT/.NET | `9.0.0` |
| architecture | `AMD64` |
| GC | Workstation |
| GC/EE interfaces | `5.3 / 2` |
| runtime source commit | `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3` |
| FP handoff patch | `nativeaot-amd64-fp-handoff.patch` |
| FP patch SHA-256 | `4185495724D48E2962BA9042AF352718BF9188032DEE4C9DE6FFE9F145A1DC31` |
| adapted runtime archive SHA-256 | `558C9F8368996A9C29DDFA08BFDEC772179D44B440130427EA4D1CB525D762AA` |

The clean locked source checkout was
`out/dotnet/c52-runtime-source/source-21a4a3a9`.

## Source audit

The C59 manifest records the source locations and the experiment uses them as
observations rather than reimplementing their decisions:

- Active single-heap collection call: `src/coreclr/gc/gc.cpp:24200`,
  `gc_heap::garbage_collect`; the `MULTIPLE_HEAPS` path at `24144` is inactive
  for this build.
- `generation_to_condemn`: `src/coreclr/gc/gc.cpp:21486`.
- Entry-2 gen1 threshold loop: `src/coreclr/gc/gc.cpp:21593-21607`, whose
  authoritative condition is `get_new_allocation(i) <= 0`.
- Source full-OOS call: `src/coreclr/gc/gc.cpp:21721-21730`, where a failed
  `try_get_new_free_region()` sets `last_gc_before_oom`.
- N0 budget path: `src/coreclr/gc/gc.cpp:18909` and the N0 budget branch
  around `18919-19035`.
- N1 ephemeral OOS path: `src/coreclr/gc/gc.cpp:17877`.
- N2/full-OOS path: `src/coreclr/gc/gc.cpp:18498`, followed by the source
  `GarbageCollectGeneration` call.

M1 is deliberately the signed entry-2 `get_new_allocation(1)` value captured by
the existing C58 policy observer. M2 is not invented as a scalar free-region
count: it is the tuple of entry-2 allocation-limit minus allocation-pointer
remaining bytes, allocation/refill path, `try_get_new_free_region` called/result,
and `last_gc_before_oom`. C40's reclaimed tail remains a separate C54
classification and is not counted as M2 capacity unless the source observer
reports it eligible.

## Bounded workload and strategy comparison

All strategies use ordinary managed allocations, fixed-size serial records,
sentinel/readback survivor validation, and the inherited C56/C54 reachability
gates. They do not call `GC.Collect`, override a generation, mutate GC policy,
manipulate free regions or segments, or intentionally OOM.

| strategy | shape | result |
| --- | --- | --- |
| S1 | 6 cohorts; 8,16,24,32,40,48 retained survivors; 24 transient allocations per cohort | `Outcome C / Level 1`; entry 2 remains N0, M1 `0x1AD658`; no B02; no later N2 reached within this lighter shape |
| S2 | 16 survivors for 3 active cohorts, then 3 pressure-tail cohorts; 48 transient allocations per cohort; max 288 explicit allocations | `Outcome C / Level 1`; entry sequence N0,N0,N2; selected for final proof |
| S3 | 24 survivors for 2 active cohorts, then 4 pressure-tail cohorts; 48 transient allocations per cohort; max 288 explicit allocations | bounded `Outcome D / Level 0` timeout before C59 completion during the fourth cohort; no policy or allocator state was forced |

The fixed safety envelope is 6 cohorts, 48 survivors, aligned retained bytes
`0x300480`, at most 288 explicit allocations, at most `0x1200000` transient
bytes, at most 256 collections, at most 2,048 diagnostic records, and a
90-second per-boot timeout. S2 was selected because it supplies the strongest
controlled survivor/promotion pressure that still completes the target
chronology within the envelope. Its promotion records reach retained aligned
bytes `0x300480`; the measured `promotedBytes` record is `0x41F58` and remains
separate from M1.

## Final three-boot evidence

Final command:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\smoke-nativeaot-gc-single-thread-suspend-ee-qemu.ps1 `
  -ProofMode last-n0-direct-gen1-window -C57Strategy S2 -FreshBootCount 3 `
  -TimeoutSeconds 90 `
  -RuntimePackManifest .\out\dotnet\c011ec59-runtime-pack-validation\run-20260831-114237054-2201cac4\runtime-pack\runtime-pack.manifest.json `
  -LockedRuntimeRoot .\out\dotnet\c52-runtime-source\source-21a4a3a9
```

Manifest:

`out/dotnet/c011ec59-last-n0-direct-gen1-window/run-20260831-122816680/manifest.json`

The manifest reports `semanticAgreement=true`, QEMU `11.0.0`, and these serial
SHA-256 values:

- `12FA160E18BA929216D136639B43A1A8FD45FA8549474097030E4B4687375982`
- `817576CD84E9980A715C06669E834A71EC24469F5B44697DEB8DC382148E1D25`
- `62A5E03B7A9895E587CB23917BB564B62CED1A08F13E55B1028CE7CB899C9CCD`

The proof-only kernel SHA-256 was
`D69594C06A2BEA027F93D198B2802CB61E729A9FBF99DF3ECE7232AD38134F36`.
The ordinary kernel and ESP artifacts were restored in `finally` and both
matched `75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6`.
No QEMU process remains owned by the experiment.

## Regression gate

The final C59 manifest retains PASS for C18, C26, C28, C34, C37, C39, C40,
C41, C46/C48 unchanged, C49, C50, C53, C54, C55, C56, C57, and C58. The
existing C58 entry sequence and source provenance remain present. The C59
diagnostics are reporting-only; the native lifecycle is attached to the
existing C56 start/finish boundary so the C58 ordering and return semantics are
unchanged.

## C60 handoff condition

C60 should continue from the selected S2 shape only if it can make the source
condition `get_new_allocation(1) <= 0` true at entry 2 while retaining all of
the C59 gates: entry 2 must remain normal `n_initial=0`, entry 3 must remain a
later source-observed N2 boundary, and C18/C26/C28/C34/C37/C39/C40/C41/C46-C58
must remain green. Any attempt must stay within the fixed allocation and
diagnostic caps, use natural Workstation GC survivor/promotion behavior, and
must not call `GC.Collect`, override generation selection, mutate policy, alter
free-region/segment state, or intentionally OOM. If that threshold cannot be
crossed under those constraints, C59's Outcome C is the terminal
source-grounded result for this runtime identity.

