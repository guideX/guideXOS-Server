# C011EC60 — NativeAOT pre-last-N0 promotion timing

Date: 2026-08-31  
Result: **Outcome C — accounting chronology solved; useful survivor promotion is too late for the C59 final normal N0 decision**  
Success level: **2**

## Scope and locked identity

C011EC60 continues C011EC59 from `bffaba0c95642a424c588024180bed2de7b87041` (`Trace NativeAOT last-N0 gen1 budget window`). The C59 commit was already present on `origin/v1.1_DOTNET_SUPPORT`; C60 started from a clean worktree and did not rewrite C59 history.

The proof uses the C52 productionized runtime-pack: NativeAOT/.NET 9.0.0, AMD64 Workstation GC, interfaces 5.3 / 2, runtime source `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`, and the unchanged durable `nativeaot-amd64-fp-handoff.patch` (`4185495724D48E2962BA9042AF352718BF9188032DEE4C9DE6FFE9F145A1DC31`). C60 adds workload and scalar observational diagnostics only. It does not rewrite C46/C47/C48 behavior or mutate production GC policy.

## Source audit

The locked source audit is in `out/dotnet/c52-runtime-source/source-21a4a3a9/src/coreclr/gc/gc.cpp`:

* `gc_heap::add_to_promoted_bytes` (line 26657) adds each retained object's aligned size to `survived_per_region[]` under `USE_REGIONS`. This is the first authoritative promoted-byte counter update; C60 intentionally does not instrument this per-object point.
* `gc_heap::get_promoted_bytes` (line 24526) sums the region counters. C60's `SURVIVE` probe is a read-side snapshot here, not a new accounting increment.
* `gc_heap::sync_promoted_bytes` (line 24573) gathers region survivor values into `heap_segment_survived`.
* `gc_heap::process_last_np_surv_region` (line 31926), `gc_heap::allocate_in_condemned_generations` (line 20475), and the `gc_heap::relocate_plug` transfer path add aligned target-generation bytes to `generation_allocation_size`. C60 probes all three source transfer forms with bounded `PROMOTE` records.
* `gc_heap::compute_in` (line 44026) reads `generation_allocation_size(gen1)`, subtracts it from `dd_gc_new_allocation`, assigns the result to `dd_new_allocation`, records `gen_data->in`, and clears `generation_allocation_size`.
* `gc_heap::compute_new_dynamic_data` (line 44147) calls `compute_in` for non-zero generations, computes the next desired budget, assigns `dd_gc_new_allocation = dd_desired_allocation`, and under `USE_REGIONS` publishes `dd_new_allocation = dd_gc_new_allocation - in` (lines 44247–44257).
* `gc_heap::generation_to_condemn` (line 21486) later evaluates `get_new_allocation(i) <= 0` (lines 21593–21606). This is the B02-visible threshold used by the C58 policy trace.

The exact accounting pipeline is therefore:

`A1` ordinary gen0 managed allocation → `A2` live object survives → `A3` survivor is classified for promotion → `A4` `add_to_promoted_bytes` updates `survived_per_region[]` → `A5` region survivor data is aggregated/transferred into `generation_allocation_size` → `A6` `compute_in` debits `gc_new_allocation` → `A7` dynamic-data publication recomputes/publishes `new_allocation` → `A8` the later `generation_to_condemn` B02 check reads `new_allocation`.

Promoted bytes, survivor snapshots, retained aligned bytes, and `generation_allocation_size` are kept as separate fields. No linearity between them is assumed.

## C60 diagnostics and bounds

The fixed C60 record table has 512 scalar records, a monotonic per-process event ordinal, and no per-allocation serial output. Every record carries collection ordinal, allocation wave, survivor cohort, policy-entry ordinal, entry ordinal, generation, byte values, raw/signed gen1 values, and retained/survivor counts. Overflow is fail-closed.

Markers are emitted only at directly observed source/runtime points: `C011EC60-PREFLIGHT`, `BASELINE`, `SURVIVE`, `PROMOTE`, `COMPUTE-IN`, `DYNAMIC`, `GCNEW`, `GEN1-DEBIT`, `ENTRY`, `ENTRY2`, `B02`, `POLICY`, `TIMING`, `TAIL`, `RESTART`, `RESUME`, and `C011EC60` completion. The final manifest sorts the event records by the global ordinal.

The bounded workload allows at most three strategies, 48 retained survivors, `0x300480` retained aligned bytes, 48 transient allocations per main wave, six allocation waves, 296 explicit allocations across the tested T0/T1 shapes, `0x1280000` / 19,398,656 managed bytes, 256 collection observations, and 512 C60 events. The workload contains no `GC.Collect`, internal GC entrypoint, GC-stress mode, condemned-generation override, policy mutation, promotion-counter write, free-region manipulation, segment manipulation, or OOS suppression.

## T0 baseline chronology

T0 is the C59-selected S2 workload: six ordinary cohorts of 16 retained 64-KiB objects, bounded transient pressure, and no explicit collection. The required C58 sequence reproduced on all three fresh boots: `n_initial = 0, 0, 2`.

The first fresh boot's complete C60 table is in the evidence manifest. The key rows are:

| Event | Ordinal | Collection | Entry | Gen1 value | Promoted / debit | Meaning |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| ENTRY | `0x01` | `0x02` | `0x01` | pre-policy `0` | — | entry 1, N0 |
| POLICY | `0x02` | `0x02` | `0x01` | `0x1CDB68` | snapshot `0x41F58` | policy observation |
| SURVIVE | `0x03–0x09` | `0x02` | `0x01` | — | snapshot `0x20510` | repeated read-side survivor snapshots |
| COMPUTE-IN / GCNEW / GEN1-DEBIT | `0x0A–0x0C` | `0x02` | `0x01` | `0x1CDB68 → 0x1AD658` | `in = debit = 0x20510` | generic gen1 allocation debit before entry 2 |
| ENTRY2 | `0x0D` | `0x03` | `0x02` | immediate pre-policy `0x1CDB68` | incorporated `0` | final normal N0 decision begins |
| POLICY | `0x0E` | `0x03` | `0x02` | final `0x1AD658` | snapshot `0x41F58` | C58 policy result remains positive |
| SURVIVE | `0x0F–0x14`, `0x16` | `0x03` | `0x02` | — | snapshot `0xE0150` | survivor accounting is present but not yet transferred to gen1 |
| PROMOTE | `0x15` | `0x03` | `0x02` | — | `0x10018`, target allocation `0xE0150` | first observed gen1 promotion transfer |
| COMPUTE-IN / GCNEW / GEN1-DEBIT | `0x17–0x19` | `0x03` | `0x02` | `0x1AD658 → 0xCD508` | `in = debit = 0xE0150` | first transfer/debit after entry 2 |
| ENTRY | `0x1A` | `0x04` | `0x03` | `0xCD508` | — | entry 3, `n_initial=2` |
| POLICY | `0x1B` | `0x04` | `0x03` | `0xCD508` | — | final policy selects gen2 |
| PROMOTE | `0x22–0x32` | `0x04` | `0x03` | — | gen2, ending at `0x100660` | gen2 transfer records |
| PROMOTE | `0x33–0x52` | `0x04` | `0x03` | — | gen1, 32 × `0x10018` | later gen1 transfer records |
| SURVIVE | `0x53` | `0x04` | `0x03` | — | gen2 snapshot `0x302F50` | retained cohort snapshot |
| COMPUTE-IN / GCNEW / GEN1-DEBIT | `0x54–0x56` | `0x04` | `0x03` | `0xCD508 → 0xFFFFFFFFFFECD208` | `in = debit = 0x200300` | later gen1 debit is after N2 policy entry |
| DYNAMIC / GCNEW | `0x57–0x58` | `0x04` | `0x03` | dynamic recomputation | — | later dynamic-data publication |

The C60 completion record reports 88 total event records, 50 promotion records, and three gen1-debit records. The gen1-debit ordinals are `0x0C`, `0x19`, and `0x56`. There are no gen1 `PROMOTE` records before entry 2; the first is `0x15`. Thus the entry-1-to-entry-2 interval contains one generic gen1 debit but zero gen1 promotion-transfer events, while the entry-2-to-entry-3 interval contains 33 gen1 promotion events and two debit events.

## Entry-2 snapshot and timing classification

At the C60 `ENTRY2` record (`0x0D`), immediately before C58's final policy publication:

* gen1 desired = `0x1CDB68`
* gen1 `gc_new_allocation` = `0x1CDB68`
* gen1 `new_allocation` raw = `0x1CDB68`
* gen1 `new_allocation` signed = `+0x1CDB68`
* survivor snapshot observed = `0x20510`
* promoted bytes already incorporated into the transfer accumulator = `0`
* survivor count = `0x10`
* retained aligned bytes = `0x100180`

The final C58 entry-2 policy marker then publishes a still-positive gen1 budget of `0x1AD658` (`+1,758,808`). M2 remained healthy: free-region observed/result `1/1`, B12 eligibility `0`, and `last_gc_before_oom=0`.

The first post-entry-2 accounting event is `GEN1-DEBIT` at `0x19`, after the first gen1 promotion at `0x15`. It transfers/incorporates `0x10018` promoted bytes as the first post-entry-2 promotion observation and debits `0xE0150`; gen1 `gc_new_allocation` and raw/signed `new_allocation` become `0xCD508`.

This is **D3** in the broad accounting sense—one generic gen1 debit before entry 2 and additional debit after entry 2—but the meaningful gen1 promotion transfer is entirely after entry 2. C60 therefore classifies the topology as Outcome C, not as a solved earlier-promotion path. The observed transfer and debit are correlated by chronology, but no linear byte-for-byte attribution is claimed.

## Timing candidates

| Strategy | Promotion before entry 2? | Debit before entry 2? | Entry-2 result | M2 | B02 |
| --- | --- | --- | --- | --- | --- |
| T0 | no gen1 transfer; first at `0x15` | yes, `0x0C` | N0; final budget `0x1AD658` | healthy | not crossed |
| T1 | no useful gen1 transfer before entry 2 | yes, generic `0x0C` | rejected: `0,0,0,2` (entry 2 no longer the last N0) | not a valid candidate | not evaluated as a C60 candidate |
| T2 | not run | — | — | — | — |

T1 adds an eight-object early retained cohort and 48 ordinary transient allocations, then continues through the same bounded harness. It did not produce a gen1 promotion before the target entry; its first useful gen1 promotion was after the third N0, and the observed C58 sequence was `0,0,0,2`. The harness rejected it at the required `0,0,2` gate. A second schedule was not justified because T1 disproved the required preservation condition and C60 is capped at three strategies.

Using `TimingGain = baselineEntry2Gen1Budget - strategyEntry2Gen1Budget`, T0 is the zero baseline. T1 has no valid entry-2 comparison because its entry-2 identity is not the C59 last-N0 boundary. No causal timing improvement is claimed.

## B02 and collection result

B02 did not cross. There is no proven crossing raw value, signed value, event ordinal, survivor count, promoted-byte value, or B02 strategy. The captured zero fields are `n_initial=0`, pre-B02 `n=0`, post-B02 `n=0`; no later B02 override branch occurred. Entry 3 instead followed the C58 N2/OOS path with collection reason `5`, final condemned generation `2`, and `last_gc_before_oom=1`. This is not a direct gen1 selection.

The authentic later N2 collection completed through the inherited C54/C55 path: planner observed, compaction and relocation observed, generation bounds fixed, `RestartEE` observed, managed resume observed, and bounded managed continuation completed. The direct-gen1 completion claim is intentionally not made.

## Bound, tail, and regression evidence

The C54 generation-bound publication showed before/after state and the fixed reclaimed-tail interval `[0x100900028, 0x100943000)`. The tail segment was `0x104010668`, generation `1 → 2`, still mapped, allocator-visible, but ineligible; it was neither retired nor recycled. The active ephemeral segment changed from `0` to `0x104010E48`. No forced reuse was attempted.

Retained cohorts passed sentinel/readback validation, coherent generation observations, and address-movement checks. The observed survivor sequence was gen0 → gen1 → gen2 for the retained workload. C18 remained valid with an authentic `CoffNativeCodeManager`, `FindMethodInfo` result `1`, valid transition-frame/control-PC provenance, and no zero/`0xFFFFFFFFFFFFFF90` FP result. C26 root scan, C28 mark closure, C34 preflight, C37/C39/C40/C41, C46/C48 FP repairs, C49–C59 productionized invariants all passed; invariant failures, sensitive diagnostic allocations, fail-fast, and page-fault fields remained zero.

## Reproduction and artifacts

The final proof used three fresh QEMU 11.0.0 boots with semantic agreement and the same T0 strategy. Evidence is under `out/dotnet/c011ec60-pre-last-n0-promotion-timing/run-20260831-140027787`.

Serial SHA-256 values:

* `2A6AC76340BE6D650F92C28203E567B66907FA903B2BE8B5CCB27031EEB6FE3A`
* `86E147C9D014CFF26C00FDA7299BCDFD116D486418DEBE065B8A6347F47DA50C`
* `132809A1876A56AE2030BD2EC041C964AA60FE32E25694BD773122D6BC87615B`

Proof payload hashes:

* kernel: `2C03C7B385FE7F125C0A2F702A2A9E12E94671AF571325D6D53890B6EB84FA71`
* PE: `A679E57A1E61F7A2711C6149CA0EE16F24513F92949E757284ADAE8602EFFC9D`
* ELF: `8B62AC1C1D72AC477BE11C71924D451B1A1D4CB429D1BE9B114FCFAAA6E4A667`
* map: `573E667B2CED588D788B6E42E8B62E2E4AA9D93C591889B3223001F0022B5F0D`

The static/release gate passed: managed NativeAOT build, PowerShell parse, JSON/XML parse, locked source/runtime identity, FP patch identity, semantic rewrite guard, PE→ELF, linker/source/table guards, and `git diff --check`. MASM was not applicable. Ordinary kernel and ESP were restored and both matched `75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6`; the proof-only artifact is inactive. Cleanup stopped only C60-owned QEMU and preserved unrelated QEMU.

## Remaining limitation and C61 handoff

C60 proves the exact limitation: the generic `GEN1-DEBIT` at `0x0C` precedes entry 2 at `0x0D`, but no gen1 `PROMOTE` event precedes entry 2. The first gen1 promotion-transfer event is `0x15`, followed by the first post-entry-2 gen1 debit at `0x19`; entry 3 is already the first N2. The next smallest milestone is a bounded C61 topology/timing experiment that observes one additional natural gen0 survivor-promotion cycle before the final N0 while preserving the exact `0,0,2` sequence and M2 headroom. It must remain ordinary managed allocation timing and must not return to raw pressure scaling or policy manipulation.
